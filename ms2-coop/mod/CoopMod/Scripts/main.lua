-- Mortal Shell II co-op mod (CoopMod)
--
-- Runs inside each player's game through UE4SS. Each client
--   1. enters its own save (same world map, own save),
--   2. summons a puppet body for the remote player,
--   3. publishes its own transform to <coop>/out_<id>.txt at 15 Hz,
--   4. reads <coop>/peer_<id>.txt (written by cooprelay.py) and drives the puppet,
--   5. mirrors nearby enemy positions (host owns the list) and syncs combat:
--        - enemy health is adopted with a monotonic "take the lower value" rule
--          (both clients publish, both adopt, so damage from either side lands
--           on both copies and the two worlds converge),
--        - a dead enemy is started on the other client through the game's own
--          SpartaHealthComponent:StartDeath(), so the death animation/ragdoll
--          plays natively instead of the body simply vanishing.
--
-- The puppet is no longer teleported every tick: it is driven through
-- CharacterMovement input so the game's own AnimBP (motion matching) picks
-- real walk/run poses. A teleport fallback handles long distances and stuck
-- situations (blocked by geometry, mid-air peer, ...).
--
-- The relay (Python) owns the network: udp for LAN/tests, steam (AppID 480)
-- for friends. The file contract keeps this mod transport-agnostic.

local MODNAME = "CoopMod"
local UEHelpers = require("UEHelpers")

-- ---------------------------------------------------------------------------
-- config
-- ---------------------------------------------------------------------------
-- Resolve this install's state dir at runtime. os.getcwd() is not usable in the
-- UE4SS Lua sandbox, so probe RELATIVE paths: io.open resolves against the
-- process working directory, which is either the game root or Binaries\Win64.
local function readFirstLine(p)
    local f = io.open(p, "r")
    if not f then return nil end
    local line = f:read("*l")
    f:close()
    return line
end

local DIR = nil
for _, rel in ipairs({
    "coop_dir.txt",                              -- CWD = Binaries\Win64
    "MortalShell2/Binaries/Win64/coop_dir.txt",  -- CWD = game root
    "./MortalShell2/Binaries/Win64/coop_dir.txt",
}) do
    local p = readFirstLine(rel)
    if p and #p > 1 then
        DIR = p:gsub("\\", "/")
        if DIR:sub(-1) ~= "/" then DIR = DIR .. "/" end
        break
    end
end
if not DIR then DIR = "coop/" end   -- relative fallback: still works inside the process

local ME = "A"
do
    local p = readFirstLine(DIR .. "playerid.txt")
    if p and #p > 0 then ME = p end
end

-- host publishes NPC state, guest mirrors it (single authority)
local ROLE = "host"
do
    local p = readFirstLine(DIR .. "role.txt")
    if p and #p > 0 then ROLE = p:gsub("%s", "") end
end

-- puppet drive tuning (uu = unreal units)
local TELEPORT_DIST = 900      -- farther than this: snap
local STOP_DIST = 70           -- closer than this: no more input
local MAX_SPEED = 900
local MIN_SPEED = 140
local STUCK_TICKS = 45         -- 1.5 s at 30 Hz without progress -> snap

local function actorTail(fn)
    local t = fn:match("([%w_]+_C_%d+)$")
    if t then return t end
    t = fn:match("([^%.%:]+)$")
    return t or fn
end

local function now() return os.date("%H:%M:%S") end
local logfile
local function log(fmt, ...)
    local ok, msg = pcall(string.format, fmt, ...)
    if not ok then msg = tostring(fmt) end
    local line = string.format("[%s][%s] %s", now(), ME, msg)
    print(string.format("[%s] %s\n", MODNAME, line))
    pcall(function()
        if not logfile then logfile = io.open(DIR .. "probe.log", "a") end
        if logfile then logfile:write(line, "\n"); logfile:flush() end
    end)
end

local function writeFile(name, text)
    pcall(function()
        local f = io.open(DIR .. name, "w")
        if f then f:write(text); f:close() end
    end)
end

local function readLine(name)
    local f = io.open(DIR .. name, "r")
    if not f then return nil end
    local line = f:read("*l")
    f:close()
    return line
end

local function safe(label, fn, ...)
    local ok, res = pcall(fn, ...)
    if not ok then log("ERR %s: %s", label, tostring(res)); return nil end
    return res
end

local function fullname(o)
    if o == nil then return "<nil>" end
    -- struct fields / soft pointers have no GetFullName; fall back quietly
    local okv, valid = pcall(function() return o:IsValid() end)
    if okv and valid == false then return "UObject <invalid>" end
    local okm, m = pcall(function() return o.GetFullName end)
    if not okm or m == nil then
        local okt, ok2 = pcall(function() return o.IsValid and o:IsValid() end)
        if okt and ok2 == false then return "UObject <invalid>" end
        return "UObject " .. tostring(o)
    end
    return safe("GetFullName", function() return o:GetFullName() end) or tostring(o)
end

local function validActor(o)
    if not o then return false end
    return not fullname(o):match("^UObject")
end
function isLive(o)
    if not o then return false end
    local ok, v = pcall(function() return o:IsValid() end)
    return ok and v == true
end

-- forward declarations: the puppet body code below needs these before the boot
-- state machine declares them, and Lua locals are scoped by declaration order
local puppet, baseline = nil, nil
MYBODY = ""
MYPART = ""
MYWPN = ""
MYWSOCK = ""
lastPeerBody = nil
lastPeerWeapon = nil
lastPeerWSock = nil
lastPeerWeapons = nil
lastPeerLook = nil
MYWPNS = "[]"
-- ---------------------------------------------------------------------------
-- game access
-- ---------------------------------------------------------------------------
local function getWorld() return safe("GetWorld", function() return UEHelpers.GetWorld() end) end
local function getPC() return safe("GetPC", function() return UEHelpers.GetPlayerController() end) end
local function getPawn(pc)
    pc = pc or getPC()
    if not pc then return nil end
    local p = safe("pc.Pawn", function() return pc.Pawn end) or safe("K2_GetPawn", function() return pc:K2_GetPawn() end)
    if validActor(p) then return p end
    return nil
end
local function locOf(a)
    if not a then return nil end
    return safe("loc", function() return a:K2_GetActorLocation() end)
end
local function dist(a, b)
    if not (a and b) then return nil end
    local dx, dy, dz = a.X - b.X, a.Y - b.Y, a.Z - b.Z
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

function runConsole(text)
    local world, pc = getWorld(), getPC()
    if not (world or pc) then return end
    local ksl = UEHelpers.GetKismetSystemLibrary()
    safe("console", function() return ksl:ExecuteConsoleCommand(world or pc, text, pc) end)
    log("console [%s]", text)
end

local function playerChars()
    return safe("FindAllOf:char", function() return FindAllOf("BP_PlayerCharacter_C") end) or {}
end

-- ---------------------------------------------------------------------------
-- puppet body (the peer's shell) + locomotion animation
--
-- Two hard-won facts from the lab (see docs/QA.md):
--   * a summoned BP_PlayerCharacter_C has a SpartaSkeletalMeshComponent with an
--     ABP but no mesh asset, so the body is invisible;
--   * setting that component's mesh asset while its AnimBP instance is alive
--     crashes the game, but SetAnimInstanceClass(nil) first makes it safe.
-- So: clear the AnimBP, assign the peer's shell mesh, and drive locomotion with
-- the game's own shared shell locomotion sequences in single-node mode
-- (PlayAnimation switches the component to single node by itself).
-- ---------------------------------------------------------------------------
ANIMBONES = {}
ANIM_MESH = nil
PUPPET_MESH = nil

local LOCO = "/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/"
local SEQ_PATHS = {
    idle = LOCO .. "Idles/A_Shared_Idle_H.A_Shared_Idle_H",
    walk = LOCO .. "Walk/H2/A_Shared_Locomotion_Walk_Fwd_H2.A_Shared_Locomotion_Walk_Fwd_H2",
    walk_b = LOCO .. "Walk/H2/A_Shared_Locomotion_Walk_Back_H2.A_Shared_Locomotion_Walk_Back_H2",
    walk_l = LOCO .. "Walk/H2/A_Shared_Locomotion_Walk_Left_H2.A_Shared_Locomotion_Walk_Left_H2",
    walk_r = LOCO .. "Walk/H2/A_Shared_Locomotion_Walk_Right_H2.A_Shared_Locomotion_Walk_Right_H2",
    sprint = LOCO .. "Sprint_800/H2/A_Shared_Locomotion_Sprint_800_H2.A_Shared_Locomotion_Sprint_800_H2",
}
local SEQ = {}
-- state: key of what is playing ("walk_l", "m:<id>:<path>"), kind: idle/walk/sprint/montage
local ANIM = { state = nil, rate = nil, kind = nil, mode = false }

-- wall clock with sub-second resolution. os.clock() is process CPU time (all
-- game threads), so it runs several times faster than real time in this game.
local GSTATICS = nil
WORLD = nil
local function rt()
    if not GSTATICS then GSTATICS = UEHelpers.GetGameplayStatics() end
    local ok, v = pcall(function() return GSTATICS:GetRealTimeSeconds(WORLD) end)
    if ok and type(v) == "number" then return v end
    return os.time()
end

local function skinnedMeshesOf(actor)
    local out = {}
    if not actor then return out end
    for _, m in pairs(safe("FindAllOf SkeletalMeshComp", function() return FindAllOf("SkeletalMeshComponent") end) or {}) do
        local owner = safe("mesh owner", function() return m:GetOwner() end)
        if owner == actor then out[#out + 1] = m end
    end
    return out
end

-- Only look at objects that are already in memory. LoadAsset() has to run on the
-- game thread and calling it from these loops (via ExecuteInGameThread) killed a
-- client mid-session, so an asset we have not loaded yet is simply retried later:
-- the game loads shell meshes and locomotion sets as it needs them.
local missingAssets = {}

-- The peer can wear a shell, mask, skin or weapon this client has never loaded,
-- and then StaticFindObject never finds it. A single LoadAsset queued on the game
-- thread worked in the lab (anim5); what crashed was the summon's class load and
-- queuing every loop there. So appearance assets (meshes, materials, attachable
-- classes) are loaded one at a time, at most every 2 s, three tries per path,
-- and nothing else runs inside the queued job. OFF by default: in run-two the
-- client that did one such load (a sword mesh) crashed later in 5 of 7 runs with
-- ProcessEvent on a freed object, the one that never loaded never crashed.
-- LOAD_ON.txt enables it for experiments.
GAMELOAD = { busy = false, busyAt = 0, nextAt = 0, tries = {}, done = 0, enabled = false }
do
    local f = io.open(DIR .. "LOAD_ON.txt", "r")
    if f then f:close(); GAMELOAD.enabled = true end
end
function requestGameLoad(path)
    local L = GAMELOAD
    if not L.enabled or not path or not path:match("^/Game/[%w_/%.%-]+$") then return end
    local now = os.time()
    if L.busy and now - L.busyAt > 10 then L.busy = false end
    if L.busy or now < L.nextAt then return end
    local n = L.tries[path] or 0
    if n >= 3 then return end
    L.tries[path] = n + 1
    L.busy, L.busyAt, L.nextAt = true, now, now + 2
    local ok, err = pcall(function()
        ExecuteInGameThread(function()
            pcall(function() return LoadAsset(path) end)
            L.busy = false
            L.done = L.done + 1
        end)
    end)
    if not ok then L.busy = false; log("game load queue failed: %s", tostring(err)) end
    log("game load %d: %s", n + 1, path)
end

local function findObjectByPath(path, load)
    if not path or #path < 4 then return nil end
    local obj = safe("StaticFindObject", function() return StaticFindObject(path) end)
    if obj and validActor(obj) then return obj end
    if not missingAssets[path] then
        missingAssets[path] = true
        log("asset not loaded yet: %s", path)
    end
    if load then requestGameLoad(path) end
    return nil
end
findAsset = findObjectByPath

local function footBonesOnMesh(mesh)
    local n = safe("numbones", function() return mesh:GetNumBones() end) or 0
    local out = {}
    for i = 0, math.min(n, 400) - 1 do
        local nm = safe("bonename", function() return mesh:GetBoneName(i):ToString() end)
        if nm then
            local low = nm:lower()
            if (low == "foot_l" or low == "foot_r" or low == "ball_l" or low == "ball_r") and #out < 3 then
                out[#out + 1] = nm
            end
        end
    end
    return out
end

BODY_STATE = "init"     -- init -> ready
APPLIED_BODY = nil      -- shell mesh path currently on the puppet
-- the puppet stays hidden until the peer reports it is in gameplay with a
-- controllable pawn; a mesh swap must not un-hide it behind that gate
PUPPET_HIDDEN = true
BODY_ATTEMPTS = 0

local function puppetMeshComponent()
    if PUPPET_MESH and validActor(PUPPET_MESH) then return PUPPET_MESH end
    for _, m in ipairs(skinnedMeshesOf(puppet)) do
        local n = safe("puppet bones", function() return m:GetNumBones() end)
        -- prefer the component that already has the most bones
        if n and n > 0 then PUPPET_MESH = m; return m end
    end
    -- none has bones yet: take the first skeletal mesh component (CharacterMesh0)
    for _, m in ipairs(skinnedMeshesOf(puppet)) do
        if not PUPPET_MESH then PUPPET_MESH = m end
    end
    return PUPPET_MESH
end

local function applyBodyMesh(path)
    local mesh = puppetMeshComponent()
    if not mesh then return false, "no mesh component" end
    local asset = findObjectByPath(path, true)
    if not asset then return false, "asset not found: " .. tostring(path) end
    -- MUST be cleared first: swapping the mesh with a live AnimBP instance crashes
    safe("clear animclass", function() return mesh:SetAnimInstanceClass(nil) end)
    safe("set body mesh", function() return mesh:SetSkeletalMeshAsset(asset) end)
    safe("body visible", function() return mesh:SetVisibility(true, true) end)
    safe("actor visible", function() return puppet:SetActorHiddenInGame(PUPPET_HIDDEN) end)
    local bones = safe("body bones", function() return mesh:GetNumBones() end) or 0
    log("puppet body <- %s : bones=%d", path, bones)
    if bones <= 0 then return false, "no bones after assign" end
    APPLIED_BODY = path
    if LOOK then LOOK.bodyKey = nil end   -- a new mesh comes with its default materials
    ANIM_MESH = mesh
    ANIMBONES = footBonesOnMesh(mesh)
    ANIM.state, ANIM.rate, ANIM.mode = nil, nil, false
    log("puppet body ready (feet bones: %s)", table.concat(ANIMBONES, ","))
    return true
end

function ensurePuppetBody()
    if not puppet then return false end
    local mesh = puppetMeshComponent()
    if not mesh then return false end
    local bones = safe("cur bones", function() return mesh:GetNumBones() end) or 0
    if bones > 0 then
        BODY_STATE = "ready"
        if ANIM_MESH ~= mesh then ANIM_MESH = mesh; ANIMBONES = footBonesOnMesh(mesh) end
        return true
    end
    -- peer's shell if we already know it, else our own shell as a placeholder
    local want = (lastPeerBody and #lastPeerBody > 8) and lastPeerBody or MYBODY
    if not (want and #want > 8) then return false end
    BODY_ATTEMPTS = BODY_ATTEMPTS + 1
    local ok = applyBodyMesh(want)
    if ok then BODY_STATE = "ready" end
    return ok
end

-- follow the peer's shell swaps: if they change shell, swap the body mesh
function syncBodyChoice()
    if BODY_STATE ~= "ready" or not lastPeerBody then return end
    if APPLIED_BODY == lastPeerBody then return end
    if #lastPeerBody < 8 then return end
    local ok = applyBodyMesh(lastPeerBody)
    if not ok then log("body swap failed for %s", lastPeerBody) end
end

-- idle / walk / sprint from the peer's real speed, play rate matched to it.
-- ANIM_OFF.txt disables this entirely: it is the switch used to isolate the
-- animation path when a client freezes.
local animOff = false
do
    local f = io.open(DIR .. "ANIM_OFF.txt", "r")
    if f then f:close(); animOff = true end
end

-- The peer's montage (attack, dodge, parry, heal, hit reaction...) arrives as the
-- sequence it is playing right now plus its position, resolved on the sender.
PEER_MONTAGE = nil   -- { path, pos, rate, id, at }

local function normDeg(a)
    a = a % 360
    if a > 180 then a = a - 360 end
    return a
end

-- explicit single-node playback (PlayAnimation() froze the guest client).
-- The mode switch re-creates the single-node instance, so it is done once, and
-- root motion is extracted but not applied: the body is placed by the network
-- transform, so a root-motion clip (attacks, dodges, sprint) must not also move
-- the mesh away from the capsule.
local function playSeq(key, seq, looping)
    if not ANIM.mode then
        if not pcall(function() return ANIM_MESH:SetAnimationMode(1, true) end) then
            log("puppet anim mode switch failed"); return false
        end
        ANIM.mode = true
        pcall(function()
            local inst = ANIM_MESH:GetAnimInstance()
            if inst and validActor(inst) then inst.RootMotionMode = 1 end
        end)
    end
    local okSet = pcall(function() return ANIM_MESH:SetAnimation(seq) end)
    local okPlay = pcall(function() return ANIM_MESH:Play(looping) end)
    if not (okSet and okPlay) then
        log("puppet anim switch failed (%s/%s) for %s", tostring(okSet), tostring(okPlay), key)
        return false
    end
    ANIM.state = key
    ANIM.rate = nil
    return true
end

local function setRate(rate)
    if ANIM.rate == nil or math.abs(ANIM.rate - rate) > 0.05 then
        safe("playrate", function() return ANIM_MESH:SetPlayRate(rate) end)
        ANIM.rate = rate
    end
end

local function playMontage(m, tnow)
    local seq = findObjectByPath(m.path)
    if not seq then return false end
    local expected = m.pos + math.min(math.max(tnow - m.at, 0), 0.5) * m.rate
    local key = "m:" .. tostring(m.id) .. ":" .. m.path
    if ANIM.state ~= key then
        if not playSeq(key, seq, false) then return false end
        safe("mpos", function() return ANIM_MESH:SetPosition(expected, false) end)
        if ANIM.kind ~= "montage" then log("puppet action -> %s @%.2f", m.path:match("([^%.]+)$") or m.path, expected) end
        ANIM.kind = "montage"
    else
        -- keep in step with the sender without re-seeking every packet
        local cur = safe("mgetpos", function() return ANIM_MESH:GetPosition() end)
        if cur and math.abs(cur - expected) > 0.12 then
            safe("mpos", function() return ANIM_MESH:SetPosition(expected, false) end)
        end
    end
    setRate(math.max(0.1, m.rate))
    return true
end

-- locomotion from the peer's real velocity: idle / directional walk / sprint,
-- play rate matched to the speed. vx, vy in uu/s, yaw = the body's facing.
function updatePuppetAnimation(vx, vy, yaw, tnow)
    if animOff or BODY_STATE ~= "ready" or not ANIM_MESH then return end
    local m = PEER_MONTAGE
    if m and m.path and #m.path > 4 and playMontage(m, tnow) then return end

    local speed = math.sqrt(vx * vx + vy * vy)
    local want, kind = "idle", "idle"
    if speed >= 520 then
        want, kind = "sprint", "sprint"
    elseif speed >= 45 then
        kind = "walk"
        local rel = normDeg(math.deg(math.atan(vy, vx)) - (yaw or 0))
        if math.abs(rel) <= 50 then want = "walk"
        elseif math.abs(rel) >= 130 then want = "walk_b"
        elseif rel > 0 then want = "walk_r"
        else want = "walk_l" end
    end
    if not SEQ[want] then SEQ[want] = findObjectByPath(SEQ_PATHS[want]) end
    -- a directional clip that is not loaded yet falls back to the forward walk
    if not SEQ[want] and kind == "walk" and want ~= "walk" then
        want = "walk"
        if not SEQ[want] then SEQ[want] = findObjectByPath(SEQ_PATHS[want]) end
    end
    local seq = SEQ[want]
    if not seq then return end
    if ANIM.state ~= want then
        if not playSeq(want, seq, true) then return end
        if ANIM.kind ~= kind then log("puppet locomotion -> %s", want) end
        ANIM.kind = kind
    end
    local rate = 1.0
    if kind == "walk" then
        rate = math.max(0.6, math.min(2.2, speed / 180))
    elseif kind == "sprint" then
        rate = math.max(0.7, math.min(1.6, speed / 800))
    end
    setRate(rate)
end


-- ---------------------------------------------------------------------------
-- boot state machine: menu -> gameplay -> puppet
-- ---------------------------------------------------------------------------
local S = { phase = "menu", entered = os.time(), noted = 0 }
local function enter(p) S.phase = p; S.entered = os.time(); S.noted = 0; log("phase -> %s", p) end
local function pt() return os.time() - S.entered end

-- ---------------------------------------------------------------------------
-- remote area streaming: the puppet is a summoned BP_PlayerCharacter_C and
-- carries the player's two World Partition streaming sources. The non-blocking
-- one keeps the level loaded around the peer (lab st2: actors within 15k of a
-- far landing area 24 -> 666 with it on, unchanged with both off); the blocking
-- one would stall this client whenever the peer runs into unloaded cells, so it
-- is switched off. STREAM_OFF.txt switches both off.
-- ---------------------------------------------------------------------------
STREAM_ENABLED = true
do
    local f = io.open(DIR .. "STREAM_OFF.txt", "r")
    if f then f:close(); STREAM_ENABLED = false end
end
STREAM = { src = "none", checked = 0, farAt = nil, n0 = nil, n1 = nil, verdict = nil }

local function streamSources(actor)
    local cls = StaticFindObject("/Script/Engine.WorldPartitionStreamingSourceComponent")
    if not (cls and cls:IsValid()) then return {} end
    local out = {}
    local list = safe("stream comps", function() return actor:K2_GetComponentsByClass(cls) end) or {}
    for i = 1, #list do
        local c = list[i]
        pcall(function() c = c:get() end)
        if c and safe("stream comp valid", function() return c:IsValid() end) then
            out[#out + 1] = { c = c, blocking = not fullname(c):find("Non Blocking", 1, true) }
        end
    end
    return out
end

local function streamSetup(actor)
    local parts = {}
    for _, e in ipairs(streamSources(actor)) do
        local want = STREAM_ENABLED and not e.blocking
        safe("stream set", function()
            if want then e.c:EnableStreamingSource() else e.c:DisableStreamingSource() end
        end)
        local en = safe("stream read", function() return e.c.bStreamingSourceEnabled end)
        parts[#parts + 1] = (e.blocking and "blk=" or "nb=") .. tostring(en)
    end
    STREAM.src = #parts > 0 and table.concat(parts, "/") or "none"
    log("puppet streaming sources: %s (enabled=%s)", STREAM.src, tostring(STREAM_ENABLED))
end

local function setupPuppet(actor)
    puppet = actor
    PUPPET_HIDDEN = true
    safe("hide new puppet", function() return actor:SetActorHiddenInGame(true) end)
    streamSetup(actor)
    -- make sure the body is a walking character even though nobody possesses it
    local mc = safe("puppet mc", function() return actor.CharacterMovement end)
    if mc then
        safe("walkmode", function() mc:SetMovementMode(1, 0) end)
        safe("orient", function() mc.bOrientRotationToMovement = false end)
        safe("accel", function() mc.MaxAcceleration = 2048 end)
    end
    log("puppet ready: %s (drive=movement-input)", fullname(actor))
end

local function bootTick()
    if S.phase == "menu" then
        local objs = safe("widgets", function() return FindAllOf("WBP_MainMenu_C") end)
        local w
        if objs then for _, o in pairs(objs) do w = o; break end end
        if w and validActor(w) then
            log("menu ready after %ds", pt())
            local ok = pcall(function() return w:ContinueGame() end)
            log("ContinueGame ok=%s", tostring(ok))
            enter("world")
        elseif pt() > 300 then log("boot: menu timeout"); enter("world") -- still try
        elseif pt() - S.noted >= 20 then S.noted = pt(); log("boot: waiting for menu (%ds)", pt()) end

    elseif S.phase == "world" then
        local pawn = getPawn()
        if pawn and fullname(pawn):find("BP_PlayerCharacter_C", 1, true) then
            if pt() >= 4 then
                log("gameplay ready after %ds: %s", pt(), fullname(pawn))
                enter("summon")
            end
        elseif pt() > 300 then log("boot: gameplay timeout")
        elseif pt() - S.noted >= 20 then S.noted = pt(); log("boot: waiting for gameplay (%ds)", pt()) end

    elseif S.phase == "summon" then
        baseline = {}
        local n = 0
        for _, o in pairs(playerChars()) do baseline[fullname(o)] = true; n = n + 1 end
        if n > 0 then
            log("summoning puppet (%d chars present)", n)
            runConsole("summon /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C")
            enter("adopt")
        else
            enter("world")
        end

    elseif S.phase == "adopt" then
        for _, o in pairs(playerChars()) do
            if not baseline[fullname(o)] then
                setupPuppet(o)
                writeFile("puppet.txt", fullname(o) .. "\n")
                enter("sync")
                return
            end
        end
        if pt() > 20 then log("boot: summon produced nothing"); enter("summon") end
    end
end

-- ---------------------------------------------------------------------------
-- body identity: the shell this player is currently wearing
-- ---------------------------------------------------------------------------
local function computeMyBody()
    local pawn = getPawn()
    if not pawn then return "", "" end
    local mesh
    for _, m in ipairs(skinnedMeshesOf(pawn)) do
        if (safe("mybones", function() return m:GetNumBones() end) or 0) > 0 then mesh = m; break end
    end
    if not mesh then return "", "" end
    local asset = safe("myasset", function() return mesh:GetSkeletalMeshAsset() end)
    local animClass = safe("myanimclass", function() return mesh:GetAnimClass() end)
    local function pathOf(o)
        if not o then return "" end
        local fn = fullname(o)
        return fn:match("(/Game/[^%s]+)") or ""
    end
    return pathOf(asset), pathOf(animClass)
end
-- ---------------------------------------------------------------------------
-- trait caches: Characters per tick, health components at 2 Hz
-- FindAllOf walks the whole GObjects array, so it must stay off the fast paths.
-- ---------------------------------------------------------------------------
local tickChars = {}
local tickTails = {}
-- enemy tail -> "SP_<spawner tail>": spawned enemies get runtime names that differ
-- between clients, their spawner (a placed actor) has the same name on both
SPN_ALIAS = {}

local function refreshChars()
    tickChars = safe("FindAllOf Character", function() return FindAllOf("Character") end) or {}
    tickTails = {}
    for _, a in pairs(tickChars) do
        if a ~= puppet then
            tickTails[actorTail(fullname(a))] = a
        end
    end
    for et, key in pairs(SPN_ALIAS) do
        local a = tickTails[et]
        if a then tickTails[key] = a end
    end
end

-- ---------------------------------------------------------------------------
-- weapons: the peer's weapon meshes are rebuilt as plain engine mesh components
-- attached to the same bone socket on the puppet body. The game's own weapon
-- actors carry gameplay logic, so only their visual mesh is replicated.
-- ---------------------------------------------------------------------------
local WEAPON = { comps = {}, applied = nil, retryAt = 0 }
WPN_LOGGED = {}

-- WEAPON_OFF.txt disables the peer weapon visuals (read once at mod load)
local WEAPON_SPAWN_ENABLED = true
do
    local f = io.open(DIR .. "WEAPON_OFF.txt", "r")
    if f then f:close(); WEAPON_SPAWN_ENABLED = false end
end
-- WS_OFF.txt disables world state sync (doors, pickups, save-object bools)
WS_ENABLED = true
do
    local f = io.open(DIR .. "WS_OFF.txt", "r")
    if f then f:close(); WS_ENABLED = false end
end

local CLS = {}
local function engineClass(name)
    if not CLS[name] then
        CLS[name] = safe("class " .. name, function() return StaticFindObject("/Script/Engine." .. name) end)
    end
    return CLS[name]
end

local function iterArray(arr, fn)
    if not arr then return end
    local function unwrap(e)
        local okg, v = pcall(function() return e:get() end)
        if okg and v ~= nil then return v end
        return e
    end
    if type(arr) == "table" then
        for _, v in pairs(arr) do fn(unwrap(v)) end
        return
    end
    pcall(function() arr:ForEach(function(_, e)
        fn(unwrap(e))
    end) end)
end

local function assetPath(o)
    if not o then return nil end
    return fullname(o):match("(/Game/[^%s]+)")
end

-- Every visible mesh of every weapon this player holds (main hand, off hand,
-- stowed), with the socket the weapon actor hangs from and the mesh's offset to
-- the weapon root. Encoded as a JSON array of
--   ["<asset path>","S|K","<socket>",x,y,z,pitch,yaw,roll]
local function localWeaponInfo()
    local pawn = getPawn()
    if not pawn then return "", "", "[]" end
    local meshCls = engineClass("MeshComponent")
    local firstPath, firstSock = "", ""
    local parts = {}
    for _, w in pairs(safe("FindAllOf SpartaWeapon", function() return FindAllOf("SpartaWeapon") end) or {}) do
        if not isLive(w) then goto nextWeapon end
        local holder = safe("wholder", function() return w.HoldingCharacter end)
        local parent = safe("wparent", function() return w:GetAttachParentActor() end)
        if (holder and holder == pawn) or (parent and parent == pawn) then
            local sock = safe("wsock", function() return w:GetAttachParentSocketName():ToString() end) or ""
            local root = safe("wroot", function() return w:K2_GetRootComponent() end)
            local comps = meshCls and safe("wcomps", function() return w:K2_GetComponentsByClass(meshCls) end)
            local list, seen = {}, {}
            local function add(c)
                if c and not seen[c] and validActor(c) then seen[c] = true; list[#list + 1] = c end
            end
            add(safe("wmesh", function() return w.WeaponMesh end))
            iterArray(comps, add)
            if not WPN_LOGGED[fullname(w)] then
                WPN_LOGGED[fullname(w)] = true
                -- one-time survey of everything the weapon actor is made of
                local all = {}
                iterArray(safe("wall", function() return w:K2_GetComponentsByClass(engineClass("SceneComponent")) end), function(c)
                    local fnm = fullname(c)
                    all[#all + 1] = (fnm:match("^(%S+)") or "?") .. ":" .. (fnm:match("([^%.]+)$") or "?")
                end)
                local wc = {}
                iterArray(safe("wcomparr", function() return w.WeaponComponents end), function(c) wc[#wc + 1] = fullname(c) end)
                log("weapon survey %s: comps=[%s] WeaponComponents=[%s] additional=%s",
                    fullname(w):match("([^%.]+)$") or "?", table.concat(all, ","), table.concat(wc, ","),
                    fullname(safe("wadd", function() return w.AdditionalWeapon end)))
                local names = {}
                for _, c in ipairs(list) do names[#names + 1] = fullname(c):match("^(%S+)") .. ":" .. (fullname(c):match("([^%.]+)$") or "?") .. ":vis=" .. tostring(safe("v", function() return c:IsVisible() end)) .. ":hid=" .. tostring(safe("h", function() return c.bHiddenInGame end)) end
                log("own weapon %s socket=%s meshes=%s", fullname(w):match("([^%.]+)$") or "?", sock, table.concat(names, ","))
            end
            for _, c in ipairs(list) do repeat
                local name = fullname(c)
                local kind = name:match("^%a*SkeletalMeshComponent") and "K"
                    or (name:match("^%a*StaticMeshComponent") and "S") or nil
                if not kind then break end
                local vis = safe("wvis", function() return c:IsVisible() end)
                local hid = safe("whid", function() return c.bHiddenInGame end)
                if vis == false or hid == true then break end
                local asset
                local path
                if kind == "K" then
                    for _, get in ipairs({
                        function() return c.SkinnedAsset end,
                        function() return c:GetSkeletalMeshAsset() end,
                        function() return c.SkeletalMeshAsset end,
                        function() return c.SkeletalMesh end }) do
                        local okA, a = pcall(get)
                        path = okA and assetPath(a) or nil
                        if path then break end
                    end
                else
                    path = assetPath(safe("wsa", function() return c.StaticMesh end))
                end
                if not path then
                    local key = "noasset:" .. name
                    if not WPN_LOGGED[key] then WPN_LOGGED[key] = true; log("weapon mesh without asset path: %s", name) end
                    break
                end
                local l, r = { X = 0, Y = 0, Z = 0 }, { Pitch = 0, Yaw = 0, Roll = 0 }
                if c ~= root then
                    l = safe("wrl", function() return c.RelativeLocation end) or l
                    r = safe("wrr", function() return c.RelativeRotation end) or r
                end
                parts[#parts + 1] = string.format('["%s","%s","%s",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]',
                    path, kind, sock, l.X or 0, l.Y or 0, l.Z or 0, r.Pitch or 0, r.Yaw or 0, r.Roll or 0)
                if firstPath == "" then firstPath, firstSock = path, sock end
            until true end
        end
        ::nextWeapon::
    end
    table.sort(parts)
    return firstPath, firstSock, "[" .. table.concat(parts, ",") .. "]"
end

local function clearPeerWeapons()
    for _, c in ipairs(WEAPON.comps) do
        safe("destroy weapon comp", function() return c:K2_DestroyComponent(c) end)
    end
    WEAPON.comps = {}
end

-- Rebuild the peer's weapon meshes as plain engine mesh components on the
-- puppet, attached to the same socket. The game's weapon actors carry gameplay
-- logic (abilities, hit detection), so only their visuals are replicated.
local function applyPeerWeapons(sig)
    if not (puppet and ANIM_MESH) then return false end
    local entries = {}
    for path, kind, sock, a, b, c, d, e, f in sig:gmatch(
        '%["([^"]*)","([SK])","([^"]*)",([%-%d%.eE]+),([%-%d%.eE]+),([%-%d%.eE]+),([%-%d%.eE]+),([%-%d%.eE]+),([%-%d%.eE]+)%]') do
        local asset = findObjectByPath(path, true)
        if not asset then return false end   -- not loaded here yet: retry later
        entries[#entries + 1] = { asset = asset, path = path, kind = kind, sock = sock,
            l = { X = tonumber(a), Y = tonumber(b), Z = tonumber(c) },
            r = { Pitch = tonumber(d), Yaw = tonumber(e), Roll = tonumber(f) } }
    end
    clearPeerWeapons()
    local xf = { Translation = { X = 0, Y = 0, Z = 0 }, Rotation = { X = 0, Y = 0, Z = 0, W = 1 },
        Scale3D = { X = 1, Y = 1, Z = 1 } }
    for _, en in ipairs(entries) do
        local cls = engineClass(en.kind == "K" and "SkeletalMeshComponent" or "StaticMeshComponent")
        local comp = cls and safe("add weapon comp", function() return puppet:AddComponentByClass(cls, true, xf, false) end)
        if comp and validActor(comp) then
            WEAPON.comps[#WEAPON.comps + 1] = comp
            safe("weapon collision", function() return comp:SetCollisionEnabled(0) end)
            if en.kind == "K" then
                safe("weapon mesh", function() return comp:SetSkeletalMeshAsset(en.asset) end)
            else
                safe("weapon mesh", function() return comp:SetStaticMesh(en.asset) end)
            end
            local ok, err = pcall(function()
                return comp:K2_AttachToComponent(ANIM_MESH, FName(en.sock), 0, 0, 0, false)
            end)
            if not ok then log("weapon attach failed (%s): %s", en.sock, tostring(err)) end
            safe("weapon offset", function()
                return comp:K2_SetRelativeLocationAndRotation(en.l, en.r, false, {}, true)
            end)
            safe("weapon visible", function() return comp:SetVisibility(true, true) end)
        end
    end
    WEAPON.applied = sig
    log("peer weapons -> %d mesh(es): %s", #WEAPON.comps, sig)
    return true
end
-- ---------------------------------------------------------------------------
-- look: everything visible on the player besides the body mesh and weapons.
-- Masks, clothing and the heart are BP_Attachable_Item_* actors the game
-- attaches to the body (BPC_AttachableItems), skins are material swaps on the
-- body (SwapShellMaterials). Rather than re-running the game's cosmetic logic on
-- the puppet (it reads the local save), the sender publishes what is actually
-- rendered: per-slot body materials plus every visible mesh component of its
-- attachables and extra pawn meshes, with the socket on the body they hang from,
-- the transform relative to that socket, leader-pose use and materials. The
-- receiver rebuilds exactly that as plain engine components on the puppet.
-- Wire format ("lk", one JSON string, rebuilt at 2 Hz, sent on change):
--   bm=<mat>,<mat>..;p=<S|K>|<asset>|<socket>|lx,ly,lz|qx,qy,qz,qw|sx,sy,sz|<0|1 leader>|<mat>,..;p=..
-- Dynamic material parameters (MID tint/emissive animation) are not carried: a
-- dynamic instance is sent as its parent material.
-- LOOK_OFF.txt disables both sides.
-- ---------------------------------------------------------------------------
LOOK = { comps = {}, pendMats = {}, logged = {}, owner = nil, applied = nil, bodyKey = nil, missing = 0, retryAt = 0,
    mine = "", cand = nil, candN = 0, enabled = true, nparts = 0, peerParts = 0, hiddenOwn = 0 }
do
    local f = io.open(DIR .. "LOOK_OFF.txt", "r")
    if f then f:close(); LOOK.enabled = false end
end
do -- look module: its helpers stay out of the main chunk (200-local limit)
LOOK.ATTACH_CLASSES = { "BP_Attachable_Item_C", "BP_Attachable_Item_Mask_C", "BP_Attachable_Item_Clothing_C",
    "BP_Attachable_Item_Heart_C", "BP_AIM_Frog_C", "BP_AIM_BagHead_C", "BP_AIM_Mango_C", "BP_AIM_FlowerCrown_C",
    "BP_Gragu_Helmet_C", "BP_AIC_Robe_C" }

function LOOK.hash(s)
    local h = 5381
    for i = 1, #s do h = (h * 33 + s:byte(i)) % 4294967296 end
    return string.format("%08x", h)
end

-- quaternion helpers (Unreal conventions: FRotator::Quaternion, A*B applies B first)
local function rotQuat(r)
    local d = math.pi / 360
    local sp, cp = math.sin((r.Pitch or 0) * d), math.cos((r.Pitch or 0) * d)
    local sy, cy = math.sin((r.Yaw or 0) * d), math.cos((r.Yaw or 0) * d)
    local sr, cr = math.sin((r.Roll or 0) * d), math.cos((r.Roll or 0) * d)
    return { x = cr * sp * sy - sr * cp * cy, y = -cr * sp * cy - sr * cp * sy,
        z = cr * cp * sy - sr * sp * cy, w = cr * cp * cy + sr * sp * sy }
end
local function qmul(a, b)
    return { x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z }
end
local function qrot(q, v)
    local tx = 2 * (q.y * v.z - q.z * v.y)
    local ty = 2 * (q.z * v.x - q.x * v.z)
    local tz = 2 * (q.x * v.y - q.y * v.x)
    return { x = v.x + q.w * tx + (q.y * tz - q.z * ty),
        y = v.y + q.w * ty + (q.z * tx - q.x * tz),
        z = v.z + q.w * tz + (q.x * ty - q.y * tx) }
end
local function relXf(c)
    local l = safe("lk rl", function() return c.RelativeLocation end) or {}
    local r = safe("lk rr", function() return c.RelativeRotation end) or {}
    local s = safe("lk rs", function() return c.RelativeScale3D end) or {}
    return { l = { x = l.X or 0, y = l.Y or 0, z = l.Z or 0 }, q = rotQuat(r),
        s = { x = s.X or 1, y = s.Y or 1, z = s.Z or 1 } }
end
-- child relative to parent, parent relative to grandparent -> child relative to grandparent
local function compose(p, c)
    local sl = { x = p.s.x * c.l.x, y = p.s.y * c.l.y, z = p.s.z * c.l.z }
    local rl = qrot(p.q, sl)
    return { l = { x = p.l.x + rl.x, y = p.l.y + rl.y, z = p.l.z + rl.z }, q = qmul(p.q, c.q),
        s = { x = p.s.x * c.s.x, y = p.s.y * c.s.y, z = p.s.z * c.s.z } }
end

local function matPath(m)
    if not m then return "" end
    local fn = fullname(m)
    if fn:match("^MaterialInstanceDynamic") then
        local parent = safe("mid parent", function() return m.Parent end)
        return parent and (assetPath(parent) or "") or ""
    end
    return assetPath(m) or ""
end
local function matsOf(c)
    local n = safe("lk nmat", function() return c:GetNumMaterials() end) or 0
    local out = {}
    for i = 0, math.min(n, 16) - 1 do
        out[#out + 1] = matPath(safe("lk mat", function() return c:GetMaterial(i) end))
    end
    return table.concat(out, ",")
end

local function hasLeader(c)
    local ok, v = pcall(function()
        local w = c.LeaderPoseComponent
        if w == nil then return false end
        local okg, o = pcall(function() return w:Get() end)
        if okg and o ~= nil then return isLive(o) end
        return isLive(w)
    end)
    return ok and v == true
end

-- every attachable actor that belongs to `who`
function LOOK.attachablesOf(who)
    local out, seen = {}, {}
    if not who then return out end
    for _, cn in ipairs(LOOK.ATTACH_CLASSES) do
        for _, a in pairs(safe("FindAllOf " .. cn, function() return FindAllOf(cn) end) or {}) do
            local key = fullname(a)
            if not seen[key] and isLive(a) then
                seen[key] = true
                local p1 = safe("att pawn", function() return a.Pawn end)
                local p2 = safe("att owner", function() return a.OwnerSpartaCharacter end)
                local p3 = safe("att parent", function() return a:GetAttachParentActor() end)
                if LOOK.same(p1, who) or LOOK.same(p2, who) or LOOK.same(p3, who) then out[#out + 1] = a end
            end
        end
    end
    return out
end

-- walk a component's attach chain up to the body mesh (or the actor root)
-- two wrappers of one UObject do not always compare equal: compare names
local function sameObj(a, b)
    if a == nil or b == nil then return false end
    if a == b then return true end
    local fa = fullname(a)
    return not fa:match("^UObject") and fa == fullname(b)
end
LOOK.same = sameObj
local function chainToBody(c, body, root)
    local xf, cur = relXf(c), c
    local bodyName, rootName = fullname(body), root and fullname(root) or "-"
    for _ = 1, 8 do
        local parent = safe("lk parent", function() return cur:GetAttachParent() end)
        if not parent or not validActor(parent) then return nil end
        local sock = safe("lk sock", function() return cur:GetAttachSocketName():ToString() end) or "None"
        local pn = fullname(parent)
        if pn == bodyName then return xf, sock end
        if pn == rootName then return xf, "@root" end
        xf = compose(relXf(parent), xf)
        cur = parent
    end
    return nil
end

function LOOK.capture()
    local pawn = getPawn()
    if not pawn then return "" end
    local body
    for _, m in ipairs(skinnedMeshesOf(pawn)) do
        if (safe("lk bones", function() return m:GetNumBones() end) or 0) > 0 then body = m; break end
    end
    if not body then return "" end
    local root = safe("lk root", function() return pawn:K2_GetRootComponent() end)
    local meshCls = engineClass("MeshComponent")
    local parts = {}
    local function addComp(c)
        if #parts >= 24 or not isLive(c) or sameObj(c, body) then LOOK.why = "dup/dead"; return end
        local name = fullname(c)
        local kind = (name:match("^%a*SkeletalMeshComponent") and "K") or (name:match("^%a*StaticMeshComponent") and "S")
        if not kind then LOOK.why = "kind"; return end
        if safe("lk vis", function() return c:IsVisible() end) == false then LOOK.why = "invisible"; return end
        if safe("lk hid", function() return c.bHiddenInGame end) == true then LOOK.why = "hidden"; return end
        local asset
        if kind == "K" then
            for _, get in ipairs({ function() return c:GetSkeletalMeshAsset() end, function() return c.SkeletalMeshAsset end,
                function() return c.SkinnedAsset end, function() return c.SkeletalMesh end }) do
                local ok, a = pcall(get)
                asset = ok and assetPath(a) or nil
                if asset then break end
            end
        else
            asset = assetPath(safe("lk sm", function() return c.StaticMesh end))
        end
        if not asset then LOOK.why = "noasset"; return end
        local xf, sock = chainToBody(c, body, root)
        if not xf then LOOK.why = "nochain"; return end
        local leader = (kind == "K" and (hasLeader(c) or sock == "None")) and 1 or 0
        parts[#parts + 1] = string.format("p=%s|%s|%s|%.2f,%.2f,%.2f|%.5f,%.5f,%.5f,%.5f|%.3f,%.3f,%.3f|%d|%s",
            kind, asset, sock, xf.l.x, xf.l.y, xf.l.z, xf.q.x, xf.q.y, xf.q.z, xf.q.w, xf.s.x, xf.s.y, xf.s.z,
            leader, matsOf(c))
    end
    -- extra meshes the game put on the pawn itself
    iterArray(meshCls and safe("lk pawncomps", function() return pawn:K2_GetComponentsByClass(meshCls) end), addComp)
    -- masks, clothing, heart
    for _, a in ipairs(LOOK.attachablesOf(pawn)) do
        local hidden = safe("att hidden", function() return a.bHidden end)
        local logIt = not LOOK.logged[fullname(a)]
        if logIt then
            LOOK.logged[fullname(a)] = true
            iterArray(safe("lk attall", function() return a:K2_GetComponentsByClass(engineClass("SceneComponent")) end), function(c)
                local par = safe("lkd par", function() return c:GetAttachParent() end)
                log("look survey %s: %s vis=%s hid=%s parent=%s sock=%s body=%s actorHidden=%s",
                    fullname(a):match("([^%.]+)$") or "?", fullname(c):match("^(%S+)") .. ":" .. (fullname(c):match("([^%.]+)$") or "?"),
                    tostring(safe("v", function() return c:IsVisible() end)), tostring(safe("h", function() return c.bHiddenInGame end)),
                    par and (fullname(par):match("([^%.]+)$") or "?") or "nil",
                    tostring(safe("s", function() return c:GetAttachSocketName():ToString() end)),
                    fullname(body):match("([^%.]+)$") or "?", tostring(hidden))
            end)
        end
        if hidden ~= true then
            local seenN = 0
            LOOK.why = nil
            iterArray(meshCls and safe("lk attcomps", function() return a:K2_GetComponentsByClass(meshCls) end),
                function(c) seenN = seenN + 1; addComp(c) end)
            if logIt then log("look survey result: comps=%d parts=%d last skip=%s", seenN, #parts, tostring(LOOK.why)) end
        end
    end
    table.sort(parts)
    LOOK.nparts = #parts
    return "bm=" .. matsOf(body) .. (#parts > 0 and (";" .. table.concat(parts, ";")) or "")
end

-- sender: rebuilt at 2 Hz; a change is published once it has held for two
-- samples, so a one-frame state during a swap is not broadcast
function LOOK.tick()
    if not LOOK.enabled then return end
    local s = LOOK.capture()
    if s == "" then return end
    if s == LOOK.mine then LOOK.cand, LOOK.candN = nil, 0; return end
    if LOOK.mine == "" or (LOOK.cand == s and LOOK.candN >= 1) then
        LOOK.mine, LOOK.cand, LOOK.candN = s, nil, 0
        log("own look -> %s (%d parts, %d bytes)", LOOK.hash(s), LOOK.nparts, #s)
        return
    end
    if LOOK.cand == s then LOOK.candN = LOOK.candN + 1 else LOOK.cand, LOOK.candN = s, 1 end
end

-- the puppet is a BP_PlayerCharacter_C: its own BPC_AttachableItems would dress
-- it from the LOCAL save and follow the local player's mask/clothing events
function LOOK.silencePuppet()
    if not isLive(puppet) then return end
    if LOOK.silenced ~= puppet then
        LOOK.silenced = puppet
        local bpc = safe("puppet bpc", function() return puppet.BPC_AttachableItems end)
        if bpc and isLive(bpc) then
            safe("puppet bpc unsub", function() return bpc:UnsubscribeFromGlobalEvents() end)
            log("puppet attachables unsubscribed from global events")
        end
    end
    for _, a in ipairs(LOOK.attachablesOf(puppet)) do
        if safe("att hidden", function() return a.bHidden end) ~= true then
            safe("hide own att", function() return a:SetActorHiddenInGame(true) end)
            LOOK.hiddenOwn = LOOK.hiddenOwn + 1
            log("hid puppet's own attachable %s", fullname(a):match("([^%.]+)$") or "?")
        end
    end
end

local function parseVec(s, n)
    local out = {}
    for v in s:gmatch("[^,]+") do out[#out + 1] = tonumber(v) or 0 end
    for i = #out + 1, n do out[i] = 0 end
    return out
end

local function applyMats(comp, mats)
    local i, missing = 0, 0
    for p in (mats .. ","):gmatch("([^,]*),") do
        if p ~= "" then
            local m = findObjectByPath(p, true)
            if m then safe("lk setmat", function() return comp:SetMaterial(i, m) end) else missing = missing + 1 end
        end
        i = i + 1
    end
    return missing
end

function LOOK.clear()
    for _, c in pairs(LOOK.comps) do
        if isLive(c) then safe("lk destroy", function() return c:K2_DestroyComponent(c) end) end
    end
    LOOK.comps, LOOK.pendMats, LOOK.applied, LOOK.bodyKey = {}, {}, nil, nil
end

-- receiver: diff the peer's parts against what is on the puppet; parts whose
-- asset is not loaded here yet are requested from the game-thread loader and
-- picked up on a later pass
function LOOK.apply(sig)
    if not (isLive(puppet) and ANIM_MESH and isLive(ANIM_MESH)) then return false end
    if LOOK.owner ~= puppet then LOOK.comps, LOOK.pendMats, LOOK.owner, LOOK.applied, LOOK.bodyKey = {}, {}, puppet, nil, nil end
    local missing = 0
    local bm = sig:match("^bm=([^;]*)") or ""
    local bodyKey = tostring(APPLIED_BODY) .. "#" .. bm
    if LOOK.bodyKey ~= bodyKey then
        local miss = applyMats(ANIM_MESH, bm)
        if miss == 0 then LOOK.bodyKey = bodyKey end
        missing = missing + miss
    end
    local want = {}
    for part in sig:gmatch("p=([^;]+)") do want[part] = true end
    for key, c in pairs(LOOK.comps) do
        if not want[key] or not isLive(c) then
            if isLive(c) then safe("lk destroy", function() return c:K2_DestroyComponent(c) end) end
            LOOK.comps[key] = nil
            LOOK.pendMats[key] = nil
        end
    end
    local xf0 = { Translation = { X = 0, Y = 0, Z = 0 }, Rotation = { X = 0, Y = 0, Z = 0, W = 1 },
        Scale3D = { X = 1, Y = 1, Z = 1 } }
    local n = 0
    for key in pairs(want) do repeat
        n = n + 1
        if LOOK.comps[key] then
            if LOOK.pendMats[key] then
                local miss = applyMats(LOOK.comps[key], LOOK.pendMats[key])
                if miss == 0 then LOOK.pendMats[key] = nil else missing = missing + miss end
            end
            break
        end
        local kind, path, sock, l, q, s, leader, mats = key:match("^([SK])|([^|]*)|([^|]*)|([^|]*)|([^|]*)|([^|]*)|(%d)|(.*)$")
        if not kind then break end
        local asset = findObjectByPath(path, true)
        if not asset then missing = missing + 1; break end
        local cls = engineClass(kind == "K" and "SkeletalMeshComponent" or "StaticMeshComponent")
        local comp = cls and safe("lk add", function() return puppet:AddComponentByClass(cls, true, xf0, false) end)
        if not (comp and isLive(comp)) then missing = missing + 1; break end
        LOOK.comps[key] = comp
        safe("lk collision", function() return comp:SetCollisionEnabled(0) end)
        if kind == "K" then
            safe("lk mesh", function() return comp:SetSkeletalMeshAsset(asset) end)
        else
            safe("lk mesh", function() return comp:SetStaticMesh(asset) end)
        end
        local parent = ANIM_MESH
        local sockName = sock
        if sock == "@root" then
            parent = safe("puppet root", function() return puppet:K2_GetRootComponent() end) or ANIM_MESH
            sockName = "None"
        end
        local okA, err = pcall(function() return comp:K2_AttachToComponent(parent, FName(sockName), 0, 0, 0, false) end)
        if not okA then log("look attach failed (%s): %s", sockName, tostring(err)) end
        local lv, qv, sv = parseVec(l, 3), parseVec(q, 4), parseVec(s, 3)
        safe("lk xf", function()
            return comp:K2_SetRelativeTransform({ Translation = { X = lv[1], Y = lv[2], Z = lv[3] },
                Rotation = { X = qv[1], Y = qv[2], Z = qv[3], W = qv[4] },
                Scale3D = { X = sv[1], Y = sv[2], Z = sv[3] } }, false, {}, true)
        end)
        if kind == "K" and leader == "1" then
            if not pcall(function() return comp:SetLeaderPoseComponent(ANIM_MESH, true, false) end) then
                safe("lk leader2", function() return comp:SetLeaderPoseComponent(ANIM_MESH, true) end)
            end
        end
        local mmiss = applyMats(comp, mats)
        if mmiss > 0 then LOOK.pendMats[key] = mats; missing = missing + mmiss end
        safe("lk visible", function() return comp:SetVisibility(true, true) end)
        log("look part on puppet: %s @%s", path:match("([^%.]+)$") or path, sock)
    until true end
    LOOK.peerParts, LOOK.missing = n, missing
    if missing == 0 then
        if LOOK.applied ~= sig then log("peer look applied %s (%d parts)", LOOK.hash(sig), n) end
        LOOK.applied = sig
        return true
    end
    return false
end
end
local hcCache = {}

local function fillHealthCache()
    local list = safe("FindAllOf HealthComp", function() return FindAllOf("SpartaHealthComponent") end) or {}
    local fresh = {}
    for _, hc in pairs(list) do
        local owner = safe("hc owner", function() return hc:GetOwner() end)
        if owner then fresh[fullname(owner)] = hc end
    end
    hcCache = fresh
end

local function healthCompOf(actor)
    if not isLive(actor) then return nil end
    local hc = hcCache[fullname(actor)]
    if hc and not isLive(hc) then return nil end
    return hc
end

local function healthNormOf(actor)
    local hc = healthCompOf(actor)
    if not hc then return nil, nil end
    local maxhp = safe("hp max", function() return hc:GetMaxHealth() end)
    if not maxhp or maxhp <= 0 then return nil, nil end   -- attributes not initialised yet
    local n = safe("hp norm", function() return hc:GetCurrentHealthNormalized() end)
    local dead = safe("hp dead", function() return hc:IsDeadOrDying() end)
    if n == nil then return nil, nil end
    if n <= 0 and not dead then
        -- a fresh actor can report 0 before its attributes are set up; never treat
        -- that as real damage
        return nil, nil
    end
    return n, dead
end

local function setHealthNorm(actor, norm)
    local hc = healthCompOf(actor)
    if not hc then return false end
    local hs = safe("healthset", function() return hc.HealthSet end)
    if not hs then return false end
    local maxhp = safe("maxhp", function() return hc:GetMaxHealth() end) or 0
    if maxhp <= 0 then return false end
    local v = norm * maxhp
    local prop = safe("health prop", function() return hs:Reflection():GetProperty("Health") end)
    if not (prop and safe("prop valid", function() return prop:IsValid() end)) then return false end
    return pcall(function()
        prop:ImportText(string.format("(BaseValue=%.3f,CurrentValue=%.3f)", v, v),
            prop:ContainerPtrToValuePtr(hs), 0, hs)
    end)
end

local function startDeath(actor)
    local hc = healthCompOf(actor)
    if not hc then return false, "no health component" end
    local ok, err = pcall(function() return hc:StartDeath() end)
    return ok, err
end

-- ---------------------------------------------------------------------------
-- NPC list (host owns it) + combat bookkeeping
-- ---------------------------------------------------------------------------
local MAX_NPC = 16
local NPC_RADIUS = 4000
local NPC_MARGIN = 1300    -- guest only claims an enemy this much closer to it than to the host
PEER_CLAIMS = {}           -- tail -> position, enemies the peer published last packet
local deadTails = {}
local hpSeen = {}          -- tail -> lowest normalized health seen locally

-- Enemies around either player. Ownership (who publishes the position) follows
-- the nearer player with a host bias: the host owns everything within
-- NPC_RADIUS of itself or not clearly nearer the guest; the guest owns enemies
-- that are clearly its own (far from the host). Both sides judge from the same
-- two positions, and the margin keeps them from both claiming one enemy.
local function collectNPCs()
    local pawn = getPawn()
    if not pawn then return {} end
    local me = locOf(pawn)
    if not me then return {} end
    local pl = puppet and locOf(puppet)
    local list = {}
    for _, a in pairs(tickChars) do
        if a ~= pawn and a ~= puppet then
            local cls = safe("npc cls", function() return a:GetClass() end)
            local cfn = cls and fullname(cls) or ""
            if not (cfn:find("BP_PlayerCharacter", 1, true) or cfn:find("BP_MainMenuCharacter", 1, true)) then
                local loc = locOf(a)
                local tl = actorTail(fullname(a))
                tl = SPN_ALIAS[tl] or tl
                if loc then
                    local dx, dy, dz = loc.X - me.X, loc.Y - me.Y, loc.Z - me.Z
                    local d = math.sqrt(dx * dx + dy * dy + dz * dz)
                    local dp = math.huge
                    if pl then
                        dx, dy, dz = loc.X - pl.X, loc.Y - pl.Y, loc.Z - pl.Z
                        dp = math.sqrt(dx * dx + dy * dy + dz * dz)
                    end
                    if d <= NPC_RADIUS or dp <= NPC_RADIUS then
                        local own
                        if ROLE == "host" then
                            own = d <= NPC_RADIUS or d <= dp + NPC_MARGIN
                            -- the guest claims it and its copy stands clearly on the
                            -- guest's side: judge by that copy (ours may lag behind)
                            local c = PEER_CLAIMS[tl]
                            if own and c and pl then
                                local ch = math.sqrt((c.X - me.X) ^ 2 + (c.Y - me.Y) ^ 2 + (c.Z - me.Z) ^ 2)
                                local cg = math.sqrt((c.X - pl.X) ^ 2 + (c.Y - pl.Y) ^ 2 + (c.Z - pl.Z) ^ 2)
                                if ch > NPC_RADIUS + 300 and ch > cg + NPC_MARGIN then own = false end
                            end
                        else
                            own = d <= NPC_RADIUS and dp > NPC_RADIUS + 300 and dp > d + NPC_MARGIN
                        end
                        list[#list + 1] = { a = a, loc = loc, d = d, dp = dp, own = own,
                            cls = cfn, tail = tl }
                    end
                end
            end
        end
    end
    -- nearest to either player first; commands that act on "the nearest enemy"
    -- still take list[1], which is the one nearest to this client when present
    table.sort(list, function(x, y)
        if (x.d <= NPC_RADIUS) ~= (y.d <= NPC_RADIUS) then return x.d <= NPC_RADIUS end
        return math.min(x.d, x.dp) < math.min(y.d, y.dp)
    end)
    return list
end

local function hpString(tail)
    local v = hpSeen[tail]
    if v == nil then return "1.0" end
    return string.format("%.3f", v)
end

NPC_OWNED = 0
OWNED_TAILS = {}           -- enemies this client publishes (never moved from the peer's list)
NPC_LAST = {}
function montagePath(o)
    if not o then return "" end
    return (fullname(o):match("(/Game/[^%s]+)")) or ""
end
local function npcJSON(list)
    local parts = {}
    OWNED_TAILS = {}
    NPC_LAST = list
    for _, e in ipairs(list) do
        if #parts >= MAX_NPC then break end
        if e.own then
            OWNED_TAILS[e.tail] = true
            local a = e.a
            if not isLive(a) then goto nextnpc end
            local v = safe("npc vel", function() return a:GetVelocity() end)
            local r = safe("npc rot", function() return a:K2_GetActorRotation() end)
            local m = safe("npc montage", function() return a:GetCurrentMontage() end)
            local mp = (m and safe("npc montage valid", function() return m:IsValid() end)) and montagePath(m) or ""
            parts[#parts + 1] = string.format('["%s",%.1f,%.1f,%.1f,%s,%d,%.1f,%.1f,%.1f,%.1f,"%s"]',
                e.tail, e.loc.X, e.loc.Y, e.loc.Z, hpString(e.tail), deadTails[e.tail] and 1 or 0,
                v and v.X or 0, v and v.Y or 0, v and v.Z or 0, r and r.Yaw or 0, mp)
        end
        ::nextnpc::
    end
    NPC_OWNED = #parts
    return ',"npc":[' .. table.concat(parts, ",") .. ']'
end

local function nhpJSON(list)
    local parts = {}
    for i = 1, math.min(#list, MAX_NPC) do
        local e = list[i]
        parts[#parts + 1] = string.format('["%s",%s]', e.tail, hpString(e.tail))
    end
    return ',"nhp":[' .. table.concat(parts, ",") .. ']'
end

local function deadJSON()
    local parts = {}
    local n = 0
    for tail in pairs(deadTails) do
        n = n + 1
        if n <= 40 then parts[#parts + 1] = '"' .. tail .. '"' end
    end
    return ',"dead":[' .. table.concat(parts, ",") .. ']'
end

-- sample local enemy health at 2 Hz: track the lowest value seen, record deaths,
-- and notice respawns (health back to full) so a revived enemy is not stuck dead
local function sampleCombat()
    refreshChars()
    for _, e in ipairs(collectNPCs()) do
        local hp, dead = healthNormOf(e.a)
        if hp then
            local prev = hpSeen[e.tail]
            if prev == nil then
                hpSeen[e.tail] = hp
            elseif hp >= 0.9 and deadTails[e.tail] and not dead then
                hpSeen[e.tail] = hp
                deadTails[e.tail] = nil
                log("local respawn: %s (hp=%.2f)", e.tail, hp)
            elseif hp < prev - 0.004 then
                hpSeen[e.tail] = hp
            end
            if dead or hp <= 0.01 then deadTails[e.tail] = deadTails[e.tail] or true end
        end
    end
end

local function killTail(tail, reason)
    if deadTails[tail] then return end
    deadTails[tail] = true
    local target = tickTails[tail]
    if target then
        local ok, err = startDeath(target)
        log("killTail %s (%s): startDeath ok=%s %s", tail, reason or "sync", tostring(ok),
            err and tostring(err) or "")
    else
        log("killTail %s (%s): no local actor with that tail", tail, reason or "sync")
    end
end

-- ---------------------------------------------------------------------------
-- send own state
-- ---------------------------------------------------------------------------
local lastPeer = nil       -- {x,y,z,yaw,t}
local lastPeerFile = nil
local sentPos = nil
local peerSeenAt = nil
local goodTicks, totalTicks = 0, 0

-- exactly two players for now: A <-> B. No shell, no directory listing (an
-- io.popen here spawned a cmd.exe every tick and made the desktop unusable).
local PEER = (ME == "A") and "B" or "A"
local function peerFile()
    return "peer_" .. PEER .. ".txt"
end

local function parseKV(line)
    local t = {}
    for k, v in line:gmatch('"([%w_]+)"%s*:%s*([%-%d%.eE]+)') do t[k] = tonumber(v) end
    local id = line:match('"id"%s*:%s*"([^"]*)"')
    t.id = id
    return t
end

-- robust section extractor: returns the [...] payload that follows "key":
local function section(line, key)
    local i = line:find('"' .. key .. '":', 1, true)
    if not i then return nil end
    local start = line:find("[", i, true)
    if not start then return nil end
    local depth, j = 0, start
    while j <= #line do
        local c = line:sub(j, j)
        if c == "[" then depth = depth + 1
        elseif c == "]" then
            depth = depth - 1
            if depth == 0 then return line:sub(start, j) end
        end
        j = j + 1
    end
    return nil
end

-- The montage this player is playing, resolved to the animation sequence under
-- the play head (montage sections map to segments of its slot track), so the
-- receiver can play it with the same single-node API as the locomotion.
local montageSegs = {}
local montageWarned = {}
local function segmentsOf(m)
    local key = fullname(m)
    if montageSegs[key] then return montageSegs[key] end
    local segs = {}
    pcall(function()
        m.SlotAnimTracks:ForEach(function(_, tr)
            if #segs > 0 then return end
            local track = tr:get()
            track.AnimTrack.AnimSegments:ForEach(function(_, s)
                local seg = s:get()
                local p = assetPath(seg.AnimReference)
                if p then
                    segs[#segs + 1] = { path = p, start = seg.StartPos or 0, a0 = seg.AnimStartTime or 0,
                        a1 = seg.AnimEndTime or 0, rate = seg.AnimPlayRate or 1, loops = seg.LoopingCount or 1 }
                end
            end)
        end)
    end)
    if #segs == 0 and not montageWarned[key] then
        montageWarned[key] = true
        log("montage without readable segments: %s", key)
    end
    montageSegs[key] = segs
    return segs
end

local OWN_M = { obj = nil, pos = -1, id = 0 }
local function ownMontage(pawn)
    local m = safe("cur montage", function() return pawn:GetCurrentMontage() end)
    if not (m and validActor(m)) then OWN_M.obj = nil; return nil end
    local inst = safe("own animinst", function() return pawn.Mesh:GetAnimInstance() end)
    if not inst then return nil end
    local pos = safe("mgetpos", function() return inst:Montage_GetPosition(m) end) or 0
    local mrate = safe("mgetrate", function() return inst:Montage_GetPlayRate(m) end) or 1
    if OWN_M.obj ~= m or pos < OWN_M.pos - 0.05 then OWN_M.id = OWN_M.id + 1 end
    OWN_M.obj, OWN_M.pos = m, pos
    for _, s in ipairs(segmentsOf(m)) do
        local r = math.abs(s.rate) > 0.001 and math.abs(s.rate) or 1
        local len = math.max(s.a1 - s.a0, 0.001)
        local span = len / r * math.max(s.loops, 1)
        if pos >= s.start and pos < s.start + span then
            local local_t = ((pos - s.start) * r) % len
            if s.rate < 0 then local_t = len - local_t end
            return s.path, s.a0 + local_t, r * mrate, OWN_M.id
        end
    end
    return nil
end

-- PLAY_READY: this player is in gameplay and controls its character. Becoming
-- ready needs every check to hold for 5 s (spawn / load / respawn settle with
-- input still blocked); it is lost only on hard signals (main menu, a loading
-- screen on screen, no player pawn) so lifts, doors and short scripted moves do
-- not make the body blink. Until ready we send {"rdy":0} and the peer hides us.
PLAY_READY = false
local READY_SINCE, READY_WHY = nil, ""
local function widgetOnScreen(cls)
    local list = safe("w " .. cls, function() return FindAllOf(cls) end)
    if not list then return false end
    for _, w in pairs(list) do
        if isLive(w) then
            local inv = safe("inviewport", function() return w:IsInViewport() end)
            local vis = safe("wvisible", function() return w:IsVisible() end)
            if inv and vis then return true end
        end
    end
    return false
end
function readyTick()
    local pc = getPC()
    local pawn = pc and getPawn(pc)
    local hard
    if not pawn or pawn == puppet or not fullname(pawn):find("BP_PlayerCharacter_C", 1, true) then hard = "no player pawn"
    elseif fullname(pc):find("MainMenu", 1, true) then hard = "main menu controller"
    elseif (fullname(getWorld()) or ""):find("L_MainMenu", 1, true) then hard = "main menu map"
    elseif widgetOnScreen("WBP_LoadingScreen_C") or widgetOnScreen("WBP_LoadingScreenBase_C") then hard = "loading screen" end
    local soft = hard
    if not soft then
        local ctrl = safe("pawn ctrl", function() return pawn.Controller end)
        if not (ctrl and isLive(ctrl) and fullname(ctrl) == fullname(pc)) then soft = "pawn not possessed"
        elseif safe("move ignored", function() return pc:IsMoveInputIgnored() end) == true then soft = "move input blocked"
        elseif widgetOnScreen("WBP_LoadingScreen_FadeToBlack_C") then soft = "fading in"
        else
            -- the pawn exists a few seconds before the loading screen goes up:
            -- wait until the player's own streaming sources report the area loaded
            for _, e in ipairs(streamSources(pawn)) do
                if safe("own stream done", function() return e.c:IsStreamingCompleted() end) == false then soft = "world streaming"; break end
            end
        end
    end
    local was = PLAY_READY
    -- our own world is being torn down / rebuilt (death reload, map travel):
    -- cached actors, the puppet included, are freed under us, so actor work holds
    if hard then LOCAL_HOLD = os.time() + 3 end
    if hard then PLAY_READY, READY_SINCE = false, nil
    elseif not PLAY_READY then
        if soft then READY_SINCE = nil
        else
            -- os.time: the world clock restarts with every map load
            READY_SINCE = READY_SINCE or os.time()
            if os.time() - READY_SINCE >= 5 then PLAY_READY = true end
        end
    end
    local why = PLAY_READY and "ready" or (hard or soft or "settling")
    if was ~= PLAY_READY or (not PLAY_READY and why ~= READY_WHY) then log("play ready=%s (%s)", tostring(PLAY_READY), why) end
    READY_WHY = why
end

local function sendOwn(list)
    if not PLAY_READY then
        SEND_SEQ = (SEND_SEQ or 0) + 1
        writeFile("out_" .. ME .. ".txt", string.format('{"t":"p","id":"%s","sq":%d,"rdy":0}\n', ME, SEND_SEQ))
        return
    end
    local pawn = getPawn()
    if not pawn then return end
    if not fullname(pawn):find("BP_PlayerCharacter_C", 1, true) or pawn == puppet then return end
    local loc = locOf(pawn)
    local rot = safe("own rot", function() return pawn:K2_GetActorRotation() end)
    if not loc then return end

    local v = safe("own vel", function() return pawn:GetVelocity() end)
    local vx, vy, vz = 0, 0, 0
    if v and v.X then vx, vy, vz = v.X, v.Y, v.Z end
    -- a teleported / scripted move (patrol, cutscene) has no engine velocity
    if sentPos and math.abs(vx) + math.abs(vy) < 1 then
        local dx, dy = (loc.X - sentPos.x) * 15, (loc.Y - sentPos.y) * 15
        if dx * dx + dy * dy > 900 and dx * dx + dy * dy < 4e6 then
            vx, vy, vz = dx, dy, (loc.Z - sentPos.z) * 15
        end
    end
    sentPos = { x = loc.X, y = loc.Y, z = loc.Z }
    local speed = math.sqrt(vx * vx + vy * vy)
    SEND_SEQ = (SEND_SEQ or 0) + 1

    local anim = "idle"
    if speed > 250 then anim = "run" elseif speed > 30 then anim = "walk" end
    local mpath, mpos, mrate, mid = ownMontage(pawn)
    if mpath then anim = "action" end

    local line = string.format(
        '{"t":"p","id":"%s","sq":%d,"x":%.2f,"y":%.2f,"z":%.2f,"vx":%.1f,"vy":%.1f,"vz":%.1f,"yaw":%.1f,"pitch":%.1f,"anim":"%s","spd":%.1f,"mseq":"%s","mpos":%.3f,"mrate":%.3f,"mid":%d,"body":"%s","apart":"%s","wpn":"%s","wsock":"%s","wpns":%s,"lk":"%s"%s%s%s%s}',
        ME, SEND_SEQ, loc.X, loc.Y, loc.Z, vx, vy, vz,
        rot and (rot.Yaw or 0) or 0, rot and (rot.Pitch or 0) or 0,
        anim, speed, mpath or "", mpos or 0, mrate or 1, mid or 0,
        MYBODY or "", MYPART or "", MYWPN or "", MYWSOCK or "", MYWPNS or "[]", (LOOK and LOOK.mine) or "",
        npcJSON(list) .. string.format(',"pd":%.3f', PD and PD.sent or 0), nhpJSON(list), deadJSON(), WS_ENABLED and (WS.payload .. WP.payload .. LF.payload .. CS.payload) or "")
    writeFile("out_" .. ME .. ".txt", line .. "\n")
end
-- ---------------------------------------------------------------------------
-- puppet drive (movement input, teleport fallback)
-- ---------------------------------------------------------------------------
local peerSpeed = 0
local driveTeleports, driveInputs = 0, 0
local stuckTicks = 0
local lastDist = nil

local function clearInput()
    if not puppet then return end
    safe("consume input", function() return puppet:ConsumeMovementInputVector() end)
    local mc = safe("mc", function() return puppet.CharacterMovement end)
    if mc then safe("zero velocity", function() mc.Velocity = { X = 0, Y = 0, Z = 0 } end) end
end

local function teleportPuppet(x, y, z)
    driveTeleports = driveTeleports + 1
    clearInput()
    safe("puppet move", function()
        return puppet:K2_SetActorLocation({ X = x, Y = y, Z = z }, false, {}, true)
    end)
    lastDist = nil
    stuckTicks = 0
end

-- The exported build crashes inside CharacterMovement when a controller-less
-- pawn is put in Walking mode and fed input, so the puppet is positioned
-- directly and smoothed toward the peer's reported transform instead.
local PUPPET_SPEED = 0
local prevPuppetPos = nil
local lastDriveT = nil

local peerEverSeen = false
local function setPuppetHidden(hide)
    if not puppet or PUPPET_HIDDEN == hide then return end
    PUPPET_HIDDEN = hide
    safe("hide puppet", function() return puppet:SetActorHiddenInGame(hide) end)
    log("puppet %s", hide and "hidden (peer not in session yet)" or "visible")
end

local PUPPET_YAW = nil

-- Packets come at 15 Hz and this runs at 30 Hz: the target is the peer's last
-- position extrapolated along its velocity (capped at 0.25 s), so the body moves
-- continuously instead of stepping, then eased toward it. Facing is eased on the
-- shortest arc.
function travelTick()
    local t = TRAVEL
    if not t then return end
    -- the pawn dies / the map reloads mid-travel: the cached pawn is freed
    if not isLive(t.pawn) then TRAVEL = nil; log("travel cancelled: pawn gone"); return end
    local k = math.min(1, (rt() - t.t0) / t.dur)
    local p = { X = t.from.X + (t.to.X - t.from.X) * k, Y = t.from.Y + (t.to.Y - t.from.Y) * k, Z = t.from.Z + (t.to.Z - t.from.Z) * k }
    safe("travel move", function() return t.pawn:K2_SetActorLocation(p, false, {}, true) end)
    if k >= 1 then
        TRAVEL = nil
        TP_HOLD = { pawn = t.pawn, at = rt() }
        log("travel arrived at %.0f %.0f %.0f", p.X, p.Y, p.Z)
    end
end

local function driveTick()
    if S.phase ~= "sync" or not puppet then return end
    if not lastPeer then
        setPuppetHidden(true)
        return
    end
    local tnow = rt()
    -- only a peer that is actually in gameplay and sending is drawn: a state
    -- file left over from an earlier session, or a peer sitting in the menu /
    -- loading screen, sends nothing new and keeps the body hidden
    if not lastPeer.live or tnow - (lastPeer.at or 0) > 3 then
        setPuppetHidden(true)
        return
    end
    peerEverSeen = true
    if BODY_STATE == "ready" then setPuppetHidden(false) end
    local loc = locOf(puppet)
    if not loc then return end

    local tyaw = lastPeer.yaw or 0
    if PUPPET_YAW == nil then PUPPET_YAW = tyaw end
    local dyaw = ((tyaw - PUPPET_YAW + 540) % 360) - 180
    if math.abs(dyaw) > 120 then PUPPET_YAW = tyaw else PUPPET_YAW = PUPPET_YAW + dyaw * 0.45 end
    safe("puppet yaw", function()
        return puppet:K2_SetActorRotation({ Pitch = 0, Yaw = PUPPET_YAW, Roll = 0 }, false)
    end)

    local age = math.min(math.max(tnow - (lastPeer.at or tnow), 0), 0.25)
    local vx, vy, vz = lastPeer.vx or 0, lastPeer.vy or 0, lastPeer.vz or 0
    local tx, ty, tz = lastPeer.x + vx * age, lastPeer.y + vy * age, lastPeer.z + vz * age
    local d = dist(loc, { X = tx, Y = ty, Z = tz })
    local nx, ny, nz
    if d > 400 then
        -- far away (spawn, teleport, big lag): snap
        nx, ny, nz = tx, ty, tz
        teleportPuppet(tx, ty, tz)
    elseif d > 0.5 then
        local k = 0.35
        nx, ny, nz = loc.X + (tx - loc.X) * k, loc.Y + (ty - loc.Y) * k, loc.Z + (tz - loc.Z) * k
        driveInputs = driveInputs + 1
        safe("puppet move", function()
            return puppet:K2_SetActorLocation({ X = nx, Y = ny, Z = nz }, false, {}, true)
        end)
    else
        nx, ny, nz = loc.X, loc.Y, loc.Z
    end

    -- body speed (uu/s) for telemetry
    if prevPuppetPos and lastDriveT then
        local dt = tnow - lastDriveT
        if dt > 0.001 then
            local spd = dist({ X = nx, Y = ny, Z = nz }, prevPuppetPos) / dt
            PUPPET_SPEED = PUPPET_SPEED * 0.6 + spd * 0.4
        end
    end
    prevPuppetPos = { X = nx, Y = ny, Z = nz }
    lastDriveT = tnow

    local ok, err = pcall(updatePuppetAnimation, vx, vy, PUPPET_YAW, tnow)
    if not ok then log("ERR puppet anim: %s", tostring(err)) end
end
-- test helper: move the local player along a small circle for N seconds, which
-- gives the peer something continuous to follow (used by the two-client test)
PATROL = nil

local function cmdPatrol(seconds)
    local pawn = getPawn()
    if not pawn then log("patrol: no pawn"); return end
    local loc = locOf(pawn)
    if not loc then return end
    PATROL = {
        origin = { X = loc.X, Y = loc.Y, Z = loc.Z },
        t0 = os.clock(),
        dur = tonumber(seconds) or 10,
        radius = 260,
        speed = 260,
    }
    log("patrol: %.0fs circle r=260 around %.1f %.1f %.1f", PATROL.dur, loc.X, loc.Y, loc.Z)
end

local function patrolTick()
    local p = PATROL
    if not p then return end
    local pawn = getPawn()
    if not pawn then PATROL = nil; return end
    local t = os.clock() - p.t0
    if t > p.dur then
        PATROL = nil
        log("patrol: done")
        return
    end
    local ang = (t * p.speed) / p.radius
    local x = p.origin.X + math.cos(ang) * p.radius
    local y = p.origin.Y + math.sin(ang) * p.radius
    safe("patrol move", function()
        return pawn:K2_SetActorLocation({ X = x, Y = y, Z = p.origin.Z }, false, {}, true)
    end)
end
-- ---------------------------------------------------------------------------
-- guest: mirror the host's NPC list onto our own copies (same actor tails)
-- ---------------------------------------------------------------------------
local npcTicks, npcGood, npcVerdict = 0, 0, nil
local npcFirst, npcMoved = {}, false

NPC_MIRRORED = 0
local mirroredOnce = {}
-- tail -> { a, mode, mont, seen }: enemies currently driven by the peer
MIR = {}
MIR_SKIP = {}
MIR_STATS = { taken = 0, released = 0, montages = 0 }

function mirrorTake(a, name)
    local m = MIR[name]
    if m and m.a == a then return m end
    if MIR_SKIP[name] then return nil end
    -- friendly NPCs (vendors, quest givers) keep their own AI: only their position is shared
    local cfn = fullname(safe("mir cls", function() return a:GetClass() end))
    if cfn:find("NPC", 1, true) then MIR_SKIP[name] = true; return nil end
    log("mir take %s (%s)", name, cfn)
    local ctrl = safe("mir ctrl", function() return a:GetController() end)
    -- a freshly spawned enemy gets its controller a moment later: retried in mirrorDrive
    local aiOff = ctrl and pcall(function() ctrl.BrainComponent:StopLogic("coop mirror") end) or false
    local mode = safe("mir mode", function() return a.CharacterMovement.MovementMode end) or 1
    -- MOVE_None: no gravity or braking fights the positions we write, and the
    -- velocity we set stays put for the anim BP's locomotion blend
    safe("mir mode set", function() a.CharacterMovement:SetMovementMode(0, 0) end)
    m = { a = a, mode = mode, mont = "", seen = rt(), aiOff = aiOff }
    MIR[name] = m
    MIR_STATS.taken = MIR_STATS.taken + 1
    log("enemy %s now driven by the peer (AI paused, mode %s -> 0)", name, tostring(mode))
    return m
end

function mirrorRelease(name, why)
    local m = MIR[name]
    if not m then return end
    MIR[name] = nil
    MIR_STATS.released = MIR_STATS.released + 1
    local a = m.a
    if not (a and safe("mir valid", function() return a:IsValid() end)) then return end
    safe("mir mode back", function() a.CharacterMovement:SetMovementMode(m.mode, 0) end)
    if not deadTails[name] then
        pcall(function() a:GetController().BrainComponent:RestartLogic() end)   -- dead / no controller: nothing to restart
    end
    log("enemy %s back under local AI (%s)", name, why)
end

function mirrorDrive(m, vx, vy, vz, yaw, mont)
    local a = m.a
    if not isLive(a) then return end
    m.seen = rt()
    if not m.aiOff then
        m.aiOff = pcall(function() a:GetController().BrainComponent:StopLogic("coop mirror") end)
    end
    safe("mir rot", function() return a:K2_SetActorRotation({ Pitch = 0, Yaw = yaw, Roll = 0 }, true) end)
    safe("mir vel", function() a.CharacterMovement.Velocity = { X = vx, Y = vy, Z = vz } end)
    if mont ~= m.mont then
        m.mont = mont
        if mont == "" then
            safe("mir montage stop", function() return a:StopAnimMontage(nil) end)
        else
            local mo = safe("mir montage find", function() return StaticFindObject(mont) end)
            if mo and safe("mir montage valid", function() return mo:IsValid() end) then
                safe("mir montage play", function() return a:PlayAnimMontage(mo, 1.0, FName("None")) end)
                MIR_STATS.montages = MIR_STATS.montages + 1
            end
        end
    end
end

local function applyNPCs(line)
    local sec = section(line, "npc")
    if not sec then return end
    local claims = {}
    for name, rest in sec:gmatch('%["([%w_]+)",([^%]]*)%]') do
        local f = {}
        for v in rest:gmatch('[^,]+') do f[#f + 1] = v end
        local a = tickTails[name]
        if a and not isLive(a) then a = nil end
        local tx, ty, tz = tonumber(f[1]), tonumber(f[2]), tonumber(f[3])
        local hp, dead = f[4], f[5]
        local isDead = dead == "1" or (tonumber(hp) or 1) <= 0.01
        local vx, vy, vz, yaw = tonumber(f[6]) or 0, tonumber(f[7]) or 0, tonumber(f[8]) or 0, tonumber(f[9])
        local mont = f[10] and f[10]:match('^"(.*)"$') or ""
        if not (tx and ty and tz) then goto continue end
        claims[name] = { X = tx, Y = ty, Z = tz }
        if a and not OWNED_TAILS[name] and not mirroredOnce[name] then
            mirroredOnce[name] = true
            log("mirroring peer-owned enemy %s at %.0f %.0f %.0f", name, tx, ty, tz)
        end

        if a and not deadTails[name] and not isDead and not OWNED_TAILS[name] then
            NPC_MIRRORED = NPC_MIRRORED + 1
            local m = MIRROR_ENABLED and mirrorTake(a, name)
            safe("npc move", function()
                return a:K2_SetActorLocation({ X = tx, Y = ty, Z = tz }, false, {}, true)
            end)
            if m and yaw then mirrorDrive(m, vx, vy, vz, yaw, mont) end
            -- a pass only counts after the host really moved this NPC: with
            -- identical saves two copies match trivially without any sync
            local first = npcFirst[name]
            if not first then npcFirst[name] = { x = tx, y = ty, z = tz }; first = npcFirst[name] end
            local mx, my, mz = tx - first.x, ty - first.y, tz - first.z
            if math.sqrt(mx * mx + my * my + mz * mz) > 200 then npcMoved = true end

            local got = locOf(a)
            if got and npcMoved and ROLE ~= "host" then
                local dx, dy, dz = got.X - tx, got.Y - ty, got.Z - tz
                local dd = math.sqrt(dx * dx + dy * dy + dz * dz)
                npcTicks = npcTicks + 1
                if dd < 30 then npcGood = npcGood + 1 end
                if npcTicks >= 20 and not npcVerdict then
                    npcVerdict = true
                    writeFile("result_npc.txt", string.format(
                        "%s npc mirror after host move %d/20 within 30u (last %.1f, %s)\n",
                        (npcGood >= 18) and "PASS" or "FAIL", npcGood, dd, name))
                    log("npc verdict: %d/20 good", npcGood)
                end
            end
        end
        ::continue::
    end
    PEER_CLAIMS = claims
end

-- adopt the peer's health when it is lower than ours (damage sync), and pick up
-- a peer side respawn when our copy is the one that is dead
local function applyHealth(line)
    local sec = section(line, "nhp")
    if not sec then return end
    for tail, hps in sec:gmatch('%["([%w_]+)",([%-%d%.eE]+)%]') do
        local a = tickTails[tail]
        if a and not isLive(a) then a = nil end
        local peerHp = tonumber(hps)
        if a and peerHp and peerHp >= 0 then
            local mine, dead = healthNormOf(a)
            if mine then
                if peerHp < mine - 0.02 then
                    setHealthNorm(a, peerHp)
                    hpSeen[tail] = peerHp
                    if peerHp <= 0.01 and not deadTails[tail] then
                        deadTails[tail] = true
                        local ok, err = startDeath(a)
                        log("peer damage killed %s: startDeath ok=%s %s", tail, tostring(ok),
                            err and tostring(err) or "")
                    end
                elseif deadTails[tail] and peerHp >= 0.9 and not dead then
                    -- peer says this enemy is alive again (respawn after a wipe)
                    deadTails[tail] = nil
                    setHealthNorm(a, peerHp)
                    hpSeen[tail] = peerHp
                    log("peer respawn adopted: %s (%.2f)", tail, peerHp)
                end
            end
        end
    end
end

local function applyDead(line)
    local sec = section(line, "dead")
    if not sec then return end
    for tail in sec:gmatch('"([%w_]+)"') do
        if not deadTails[tail] then
            killTail(tail, "peer")
        end
    end
end

local function applyPeer()
    local pf = peerFile()
    if not pf then return end
    local line = readLine(pf)
    if not line or line == "" then return end
    -- the peer is in the menu / loading / not yet in control: hide its body now
    if line:find('"rdy":0', 1, true) then
        if lastPeer then lastPeer.live = false end
        return
    end
    local m = parseKV(line)
    if not m.x then return end

    peerSpeed = m.spd or peerSpeed
    local pbody = line:match('"body"%s*:%s*"([^"]*)"')
    if pbody and #pbody > 4 then lastPeerBody = pbody end
    local pwpn = line:match('"wpn"%s*:%s*"([^"]*)"')
    local pwsock = line:match('"wsock"%s*:%s*"([^"]*)"')
    if pwpn and #pwpn > 4 then lastPeerWeapon = pwpn end
    if pwsock and #pwsock > 0 then lastPeerWSock = pwsock end
    local pwpns = section(line, "wpns")
    if pwpns then lastPeerWeapons = pwpns end
    local plk = line:match('"lk"%s*:%s*"([^"]*)"')
    if plk and #plk > 3 then lastPeerLook = plk end
    if applyPeerDamage then applyPeerDamage(m.pd) end
    -- the file is re-read every tick; only a new packet moves the clock that
    -- the extrapolation and the montage play head run from
    local fresh = not lastPeer or m.sq == nil or m.sq ~= lastPeer.sq
    local at = (fresh or not lastPeer) and rt() or lastPeer.at
    -- live = we have seen its counter move since this mod started
    local live = lastPeer and (lastPeer.live or (fresh and lastPeer.sq ~= nil)) or false
    lastPeer = { x = m.x, y = m.y, z = m.z, yaw = m.yaw or 0, t = os.time(),
        vx = m.vx or 0, vy = m.vy or 0, vz = m.vz or 0, sq = m.sq, at = at, live = live }
    local mseq = line:match('"mseq"%s*:%s*"([^"]*)"')
    if mseq and #mseq > 4 then
        if fresh or not PEER_MONTAGE then
            PEER_MONTAGE = { path = mseq, pos = m.mpos or 0, rate = m.mrate or 1, id = m.mid or 0, at = at }
        end
    else
        PEER_MONTAGE = nil
    end
    peerSeenAt = os.time()
    lastPeerFile = pf
    pcall(applyNPCs, line)
    pcall(applyHealth, line)
    pcall(applyDead, line)
    if WS_ENABLED then pcall(wsReadPeer, line); pcall(wpReadPeer, line); pcall(liftReadPeer, line); pcall(csReadPeer, line) end
end

-- ---------------------------------------------------------------------------
-- locomotion evidence (safe telemetry only)
--
-- Sampling bone transforms on the Sparta mesh component froze the UE4SS Lua
-- thread for the rest of the session (only a game restart recovered it), so the
-- evidence is the sequence player itself: does the animation state follow the
-- peer's speed and does the play position actually advance?
-- ---------------------------------------------------------------------------
animSamples = { idle = 0, walk = 0, sprint = 0, montage = 0 }
animMoveTicks = 0
animStillTicks = 0
animVerdictWritten = false

local function animInstrument(moving)
    if animVerdictWritten or not ANIM_MESH or BODY_STATE ~= "ready" then return end
    local state = ANIM.kind
    if state then animSamples[state] = (animSamples[state] or 0) + 1 end
    if moving and state and state ~= "idle" then
        animMoveTicks = animMoveTicks + 1
    else
        animStillTicks = animStillTicks + 1
    end
    -- moving with the loot-motion sequence active is the evidence: the body is not
    -- standing still while it travels
    if animSamples.walk >= 10 and animMoveTicks >= 20 and not animVerdictWritten then
        animVerdictWritten = true
        local text = string.format(
            "PASS puppet locomotion: sequence states idle=%d walk=%d sprint=%d, moving-while-animated=%d ticks\n",
            animSamples.idle, animSamples.walk, animSamples.sprint, animMoveTicks)
        writeFile("result_anim.txt", text)
        log("%s", text:gsub("\n", ""))
    end
end
-- ---------------------------------------------------------------------------
-- verdict
-- ---------------------------------------------------------------------------
local verdictWritten = false

local function judge()
    if verdictWritten then return end
    if not (puppet and lastPeer) then return end
    if os.time() - (peerSeenAt or 0) > 3 then return end
    local loc = locOf(puppet)
    local d = dist(loc, { X = lastPeer.x, Y = lastPeer.y, Z = lastPeer.z })
    if not d then return end
    totalTicks = totalTicks + 1
    if d < 120 then goodTicks = goodTicks + 1 end

    if totalTicks >= 30 then
        verdictWritten = true
        local verdict = string.format(
            "%s puppet tracks peer (dist %.1f, %d/30 good; moves %d, snaps %d)",
            (goodTicks >= 25) and "PASS" or "FAIL", d, goodTicks, driveInputs, driveTeleports)
        writeFile("result.txt", verdict .. "\n")
        log("%s", verdict)
    end
end

-- say loudly when the peer never shows up: a broken link is the difference
-- between "the mod is wrong" and "nothing arrived"
local lastPeerWarn = 0
local function peerWatchdog()
    if lastPeer then return end
    if os.time() - lastPeerWarn < 10 then return end
    lastPeerWarn = os.time()
    local f = io.open(DIR .. peerFile(), "r")
    local exists = f and "exists" or "missing"
    if f then f:close() end
    log("waiting for peer state (%s): %s", exists, peerFile())
end

-- ---------------------------------------------------------------------------
-- test command channel (cmd.txt), same idea as the probe
-- ---------------------------------------------------------------------------
-- lookadd: load the attachable class (game-thread loader), then let our own
-- BPC_AttachableItems spawn and attach it exactly as the game does
function lookTestTick()
    local a = LOOK_ADD
    if not a then LOOK_TEST = nil; return end
    if os.time() - a.at > 40 then log("lookadd gave up: %s", a.path); LOOK_ADD, LOOK_TEST = nil, nil; return end
    local cls = findAsset(a.path, true)
    if not cls then return end
    local pawn = getPawn()
    local bpc = pawn and safe("own bpc", function() return pawn.BPC_AttachableItems end)
    if not (bpc and isLive(bpc)) then return end
    local before = {}
    for _, x in ipairs(LOOK.attachablesOf(pawn)) do before[fullname(x)] = true end
    local ok, err = pcall(function() return bpc:AddItem(cls) end)
    log("lookadd AddItem(%s) ok=%s %s", a.path, tostring(ok), tostring(err or ""))
    for _, x in ipairs(LOOK.attachablesOf(pawn)) do
        if not before[fullname(x)] then LOOK_ADDED = fullname(x); log("lookadd spawned %s", LOOK_ADDED) end
    end
    LOOK_ADD, LOOK_TEST = nil, nil
end

local function runCommands()
    local f = io.open(DIR .. "cmd.txt", "r")
    if not f then return end
    local lines = {}
    for line in f:lines() do lines[#lines + 1] = line end
    f:close(); os.remove(DIR .. "cmd.txt")
    for _, line in ipairs(lines) do
        local cmd, rest = line:match("^(%S+)%s*(.-)$")
        if cmd == "tp" then
            local x, y, z = rest:match("([%-%d%.]+)%s+([%-%d%.]+)%s+([%-%d%.]+)")
            local pawn = getPawn()
            if x and pawn then
                safe("tp", function() return pawn:K2_SetActorLocation({ X = tonumber(x), Y = tonumber(y), Z = tonumber(z) }, false, {}, true) end)
                -- hover until the ground there has streamed in, or the game's fall
                -- recovery puts the player back where it came from
                safe("tp hold", function() pawn.CharacterMovement:SetMovementMode(5, 0) end)
                TP_HOLD = { pawn = pawn, at = rt() }
                ENEMY_TRACE = 0
                log("tp -> %s %s %s", x, y, z)
            end
        elseif cmd == "lookadd" then
            -- test hook: put an attachable (mask / clothing class path) on our own player
            LOOK_ADD = { path = rest, at = os.time() }
            LOOK_TEST = lookTestTick
            log("lookadd queued: %s", rest)
        elseif cmd == "lookdel" then
            for _, a in ipairs(LOOK.attachablesOf(getPawn())) do
                if LOOK_ADDED and fullname(a) == LOOK_ADDED then
                    local ok = pcall(function() return a:RemoveMe() end)
                    safe("lookdel hide", function() return a:SetActorHiddenInGame(true) end)
                    log("lookdel %s (RemoveMe ok=%s)", LOOK_ADDED, tostring(ok))
                end
            end
        elseif cmd == "info" then
            local nd = 0
            for _ in pairs(deadTails) do nd = nd + 1 end
            log("phase=%s puppet=%s peer=%s drive(input=%d,teleport=%d) dead=%d",
                S.phase, fullname(puppet), lastPeerFile or "-", driveInputs, driveTeleports, nd)
        elseif cmd == "console" then
            runConsole(rest)
        elseif cmd == "kill" then
            local list = collectNPCs()
            if list[1] then killTail(list[1].tail, "cmd") end
        elseif cmd == "hurt" then
            -- pretend this client dealt damage: drop the nearest enemy to the
            -- given fraction (default 0.5). It travels to the peer as health sync.
            local frac = tonumber(rest) or 0.5
            local list = collectNPCs()
            if list[1] then
                local ok = setHealthNorm(list[1].a, frac)
                local hp = healthNormOf(list[1].a)
                log("hurt %s -> %.2f (ok=%s, now %.3f)", list[1].tail, frac, tostring(ok), hp or -1)
            else
                log("hurt: no npc nearby")
            end
        elseif cmd == "patrol" then cmdPatrol(rest)
        elseif cmd == "wstest" then wsTest()
        elseif cmd == "wptest" then wpTest()
        elseif cmd == "qstest" then qsTest()
        elseif cmd == "qs2test" then qs2Test()
        elseif cmd == "lifttest" then liftTest()
        elseif cmd == "cstest" then csTest()
        elseif cmd == "travel" then
            -- straight-line trip at a fixed speed through whatever has to stream in
            local x, y, z, secs = rest:match("([%-%d%.]+)%s+([%-%d%.]+)%s+([%-%d%.]+)%s*([%d%.]*)")
            local pawn = getPawn()
            local from = pawn and locOf(pawn)
            if x and from then
                safe("travel hold", function() pawn.CharacterMovement:SetMovementMode(5, 0) end)
                TRAVEL = { pawn = pawn, from = { X = from.X, Y = from.Y, Z = from.Z },
                    to = { X = tonumber(x), Y = tonumber(y), Z = tonumber(z) }, t0 = rt(), dur = tonumber(secs) or 60 }
                log("travel %.0f uu over %.0fs", math.sqrt((TRAVEL.to.X - from.X) ^ 2 + (TRAVEL.to.Y - from.Y) ^ 2), TRAVEL.dur)
            end
        elseif cmd == "pdtest" then
            -- an enemy hit on the peer's body, as the game would deal it
            local ok = puppet and setHealthNorm(puppet, 0.9)
            log("pdtest: peer body health -> 0.9 (ok=%s)", tostring(ok))
        elseif cmd == "aitest" then
            local list = collectNPCs()
            local e = list[1]
            local tsc = e and safe("ai tsc", function() return e.a:GetController().TargetSelectionComponent end)
            if tsc and puppet then
                safe("ai override", function() tsc:SetTargetOverride(puppet, 10.0) end)
                local cur = safe("ai cur", function() return tsc.CurrentTarget end)
                log("aitest: %s target override -> puppet; current target now %s", e.tail, cur and fullname(cur) or "none")
                writeFile("result_ai.txt", string.format("%s enemy %s targets the peer's body after SetTargetOverride (current %s)\n",
                    (cur and fullname(cur) == fullname(puppet)) and "PASS" or "FAIL", e.tail, cur and fullname(cur) or "none"))
            else
                log("aitest: no enemy controller / puppet")
            end
        elseif cmd == "hp" then
            local list = collectNPCs()
            for i = 1, math.min(#list, 6) do
                local hp, dead = healthNormOf(list[i].a)
                log("hp %s = %s dead=%s d=%.0f", list[i].tail, tostring(hp), tostring(dead), list[i].d)
            end
        elseif cmd == "npctp" then
            local x, y, z = rest:match("([%-%d%.]+)%s+([%-%d%.]+)%s+([%-%d%.]+)")
            local list = collectNPCs()
            local pick = nil
            for _, e in ipairs(list) do
                if e.cls and (e.cls:find("Enemy", 1, true) or e.cls:find("Turret", 1, true)) then pick = e; break end
            end
            pick = pick or list[1]
            if x and pick then
                local a = pick.a
                safe("npctp", function()
                    return a:K2_SetActorLocation({ X = tonumber(x), Y = tonumber(y), Z = tonumber(z) }, false, {}, true)
                end)
                log("npctp -> %s at %s %s %s", pick.tail, x, y, z)
            else
                log("npctp: no npc nearby")
            end
        end
        log("cmd handled: %s", line)
    end
end

-- ---------------------------------------------------------------------------
-- world state sync: doors, pickups, per-actor save-object bools
--
-- Persistent world changes live in a USpartaSaveObject per actor
-- (SpartaSaveComponent.RuntimeData) or as a flag on the actor itself
-- (BP_HubDoor_C.DoorIsOpen, store B). Each client scans the watched classes
-- round-robin, keys every instance by a hash of its owning actor's name (stable
-- across clients, the UAID is baked into the map package), and publishes the
-- keys that are true. Merging is monotonic: true wins, like combat. A key that
-- flipped during this session is sent as "live" and replayed through the game's
-- own action (OpenDoor, PickUp -> both players get the loot); a key that was
-- already true in the peer's save only sets the flag and hides a pickup.
-- ---------------------------------------------------------------------------
local WS_KINDS = {
    -- FindAllOf includes subclasses (BP_HubDoubleDoor_C comes back here too)
    { cls = "BP_HubDoor_C", field = "DoorIsOpen", actor = true, door = true },
    { cls = "BP_SO_SimpleBool_C", field = "MyBool" },
    { cls = "BPSO_CraftingItemPickup_C", field = "ItemCollected" },
    { cls = "BPSO_FlowerChest_C", field = "ItemCollected" },
    { cls = "BPSO_DoorLockedDungeon_C", field = "DoorOpened" },
    -- gates, boss/quest objects and enemy permadeath (field set only: the
    -- owning actor shows the new state the next time it loads)
    { cls = "BPSO_BossGate_C", field = "Unlocked" },
    { cls = "BPSO_Gate_C", field = "GateOpened" },
    { cls = "BPSO_CloseAndOpenGate_C", field = "CombatEncCompleted" },
    { cls = "BPSO_ShepherdSheepGate_C", field = "GateOpened" },
    { cls = "BPSO_TarGate_C", field = "GateClensed" },
    { cls = "BPSO_BearTrap_C", field = "IsDisarmed" },
    { cls = "BPSO_SpawnerSaveObj_C", field = "WasEverKilled" },
    { cls = "BPSO_GrishaGhost_C", field = "BossDefeated" },
    { cls = "BPSO_HutchbackBossTrigger_C", field = "HutchbackDefeated" },
    { cls = "BPSO_EridrimUnlockManager_C", fields = { "WardenJumped", "WardenDefeated" } },
    { cls = "BPSO_FC_FinalBossManager_C", fields = { "EndStrandingCompleted", "AllCorruptionCoresClensed" } },
    { cls = "BPSO_FC_CorruptionCore_C", field = "Cleansed" },
    { cls = "BPSO_FC_TarRoots_C", fields = { "T1Cleansed", "T2Cleansed", "T3Clensed", "CenterClensed" } },
    { cls = "BPSO_EvilStatue_C", field = "Cleansed" },
    { cls = "BPSO_Ovum_C", field = "Cleansed" },
    { cls = "BPSO_GenessaSensor_C", field = "SensorClensed" },
    { cls = "BPSO_GrishaCage_C", field = "CageOpened" },
    { cls = "BPSO_FallingCorpse_C", field = "ShotDown" },
    { cls = "BPSO_FallingStatue_C", field = "StatueFallen" },
    { cls = "BPSO_SlayerDungeonChalice_C", field = "SealBroken" },
    { cls = "BPSO_TarGolemLevelScript_C", field = "StoneSealClaimed" },
    { cls = "BPSO_LandingArea_Boss_C", field = "BeaconRisen" },
    { cls = "BPSO_CinematicDLUpdate_C", field = "CinematicFired" },
}
for _, k in ipairs(WS_KINDS) do k.fields = k.fields or { k.field } end
local WS_LIVE_SECS = 20      -- a local flip is announced as live this long
local WS_WINDOW = 40         -- baseline keys per packet (rotating)
-- a freshly loaded actor reports its class default until its save is applied
-- (BP_HubDoor_C reads DoorIsOpen=true for ~3 s, then false): values are only
-- trusted once the object has been in the world this long
local WS_SETTLE = 8
WS = {
    seen = {},       -- key -> { v, kind, obj, owner }
    trueList = {},   -- keys known true locally, in discovery order
    isTrue = {},
    recent = {},     -- key -> rt() of a local false->true flip
    peer = {},       -- key -> "live" | "base"
    done = {},       -- key -> applied from peer
    rot = 1, kindIdx = 0, payload = "", scans = 0,
    applied = { live = 0, base = 0, ledger = 0, fail = 0 }, liveOk = {}, diag = {},
}

local function wsHash(s)
    local h = 2166136261
    for i = 1, #s do h = ((h ~ s:byte(i)) * 16777619) & 0xffffffff end
    return string.format("%08x", h)
end

local function wsBool(v)
    if type(v) == "boolean" then return v end
    return tostring(v) == "true"
end

local function wsValid(o)
    if not o then return false end
    local ok, v = pcall(function() return o:IsValid() end)
    return ok and v == true
end

local function wsMarkTrue(key)
    if WS.isTrue[key] then return end
    WS.isTrue[key] = true
    WS.trueList[#WS.trueList + 1] = key
    if WSL then WSL.add(key) end
end

-- ---------------------------------------------------------------------------
-- world state ledger: store A for actors nobody has loaded
--
-- A store A save object only exists as a live object while its actor is
-- streamed in; the copies the save system keeps for unloaded actors are
-- /Engine/Transient objects without an owner name, and the map that holds them
-- (SpartaSaveSubSystem.SaveObjects, TMap<FGuid,...>) kills the game when Lua
-- iterates it. So each client keeps a ledger of every key it has ever known
-- true (seen locally or announced by the peer) in ws_ledger_<slot>.txt. After
-- load the ledger is announced again and each of its keys is applied the
-- moment its actor streams in, even when the peer is offline or has never
-- loaded that area.
-- A different playthrough in the same slot shows up as ledger keys that read
-- false here: 10+ of those and more bad than good -> the ledger is dropped.
-- WS_LEDGER_OFF.txt disables it.
-- ---------------------------------------------------------------------------
WSL = { keys = {}, n = 0, fromLedger = {}, counted = {}, good = 0, bad = 0, state = "wait",
        file = nil, dirty = false, savedAt = 0, loadedAt = nil, applied = {} }
do local f = io.open(DIR .. "WS_LEDGER_OFF.txt", "r"); if f then f:close(); WSL = nil end end

if WSL then
function WSL.add(key)
    if WSL.keys[key] then return end
    WSL.keys[key] = true
    WSL.n = WSL.n + 1
    WSL.dirty = true
end

function WSL.seen(key, v)
    if not WSL.fromLedger[key] then return end
    if v then
        WSL.fromLedger[key] = nil
        WSL.good = WSL.good + 1
    elseif not WSL.counted[key] then
        WSL.counted[key] = true
        WSL.bad = WSL.bad + 1
    end
end

function WSL.load()
    local slot = "x"
    pcall(function()
        local sub = FindFirstOf("SpartaSaveSubSystem")
        local i = sub and sub:GetActiveSaveProfileSlotIndex()
        if tonumber(i) then slot = tostring(math.floor(tonumber(i))) end
    end)
    WSL.file = "ws_ledger_" .. slot .. ".txt"
    local f = io.open(DIR .. WSL.file, "r")
    local n = 0
    if f then
        for line in f:lines() do
            local key = line:match("^(%x%x%x%x%x%x%x%x)")
            if key and not WSL.keys[key] then
                WSL.keys[key] = true
                WSL.n = WSL.n + 1
                n = n + 1
                if not WS.peer[key] and not WS.isTrue[key] then
                    WS.peer[key] = "ledger"
                    WSL.fromLedger[key] = true
                end
            end
        end
        f:close()
    end
    WSL.loadedAt = rt()
    if n == 0 then WSL.state = "ok" end
    log("ledger %s: %d keys loaded", WSL.file, n)
end

function WSL.save()
    local parts = {}
    for key in pairs(WSL.keys) do parts[#parts + 1] = key end
    table.sort(parts)
    writeFile(WSL.file, table.concat(parts, "\n") .. "\n")
    WSL.dirty, WSL.savedAt = false, rt()
end

function WSL.tick()
    local tnow = rt()
    -- the slot index only means something once a world is loaded
    if not WSL.file then
        if getPawn() then WSL.load() end
        return
    end
    if WSL.state == "wait" then
        if WSL.bad >= 10 and WSL.bad > WSL.good then
            WSL.state = "dropped"
            for key in pairs(WSL.fromLedger) do
                if WS.peer[key] == "ledger" then WS.peer[key] = nil end
                WSL.keys[key] = nil
                WSL.n = WSL.n - 1
            end
            WSL.fromLedger = {}
            WSL.dirty = true
            log("ledger dropped: %d keys read false, %d true (another playthrough in this slot?)", WSL.bad, WSL.good)
        elseif WSL.good + WSL.bad >= 10 or tnow - WSL.loadedAt > 45 then
            WSL.state = "ok"
            log("ledger trusted (good %d, bad %d)", WSL.good, WSL.bad)
            -- announce everything the ledger knows, loaded here or not
            for key in pairs(WSL.keys) do wsMarkTrue(key) end
        end
    end
    -- verdict: a key known only from the ledger was applied on load and reads true
    if not WSL.verdict then
        for key, e in pairs(WSL.applied) do
            local okr, now = pcall(function() return e.obj[e.field] end)
            if okr and wsBool(now) then
                WSL.verdict = true
                local text = string.format("PASS ledger: %s.%s (%s) re-applied from the ledger on load (ledger %d keys, applied %d)\n",
                    e.kind.cls, e.field, e.owner, WSL.n, WS.applied.ledger)
                writeFile("result_ledger.txt", text)
                log("%s", (text:gsub("\n", "")))
                break
            end
        end
    end
    if WSL.dirty and tnow - WSL.savedAt > 5 then WSL.save() end
end

function WSL.status()
    return string.format("ledger=%s keys:%d good:%d bad:%d applied:%d\n", WSL.state, WSL.n, WSL.good, WSL.bad, WS.applied.ledger)
end
end

-- one class per call: FindAllOf walks the whole object array
local function wsScan()
    WS.kindIdx = WS.kindIdx % #WS_KINDS + 1
    local kind = WS_KINDS[WS.kindIdx]
    local list = safe("ws FindAllOf " .. kind.cls, function() return FindAllOf(kind.cls) end) or {}
    for _, o in ipairs(list) do
        local fn = fullname(o)
        -- the owning actor sits directly under a level: PersistentLevel.<actor>[.SpartaSave.RuntimeData]
        local owner = fn:match("PersistentLevel%.([%w_]+)")
        if owner and (kind.actor or fn:find(owner .. ".", 1, true)) then
          for fi, field in ipairs(kind.fields) do
            local key = wsHash(fi == 1 and owner or (owner .. "." .. field))
            local okv, raw = pcall(function() return o[field] end)
            if okv then
                local v = wsBool(raw)
                local prev = WS.seen[key]
                local born = (prev and prev.fn == fn and prev.born) or rt()
                local settled = rt() - born >= WS_SETTLE
                local dk = key .. kind.cls
                if WS.diag[dk] ~= v then
                    WS.diag[dk] = v
                    log("ws see %s %s=%s raw=%s(%s) %s", key, field, tostring(v), tostring(raw), type(raw), fn)
                end
                if not settled then v = nil end
                if v ~= nil and WSL then WSL.seen(key, v) end
                if v and prev and prev.v == false and not WS.done[key] then
                    WS.recent[key] = rt()
                    log("ws local flip %s %s.%s -> true (%s)", key, kind.cls, field, owner)
                end
                if v then wsMarkTrue(key) end
                WS.seen[key] = { v = v, kind = kind, field = field, obj = o, owner = owner, born = born, fn = fn }
                if v and WS.liveOk[key] == false then
                    WS.liveOk[key] = true
                    log("ws readback %s %s now true", key, owner)
                end
            end
          end
        end
    end
    WS.scans = WS.scans + 1
end

-- the "ws" section of the state line: every recent flip plus a rotating window
-- of everything else that is true here (the relay rewrites one line, so the
-- whole set has to come around again for a late or restarted peer)
local function wsBuildPayload()
    local parts, sent, tnow = {}, {}, rt()
    for key, at in pairs(WS.recent) do
        if tnow - at <= WS_LIVE_SECS then
            parts[#parts + 1] = '["' .. key .. '",1]'; sent[key] = true
        end
    end
    local n = #WS.trueList
    for i = 1, math.min(WS_WINDOW, n) do
        local key = WS.trueList[(WS.rot + i - 2) % n + 1]
        if not sent[key] then parts[#parts + 1] = '["' .. key .. '",0]'; sent[key] = true end
    end
    if n > 0 then WS.rot = (WS.rot + WS_WINDOW - 1) % n + 1 end
    WS.payload = ',"ws":[' .. table.concat(parts, ",") .. ']'
end

function wsReadPeer(line)
    local sec = section(line, "ws")
    if not sec then return end
    for key, l in sec:gmatch('%["(%x+)",(%d)%]') do
        if l == "1" then
            WS.peer[key] = "live"
        elseif not WS.peer[key] or WS.peer[key] == "ledger" then
            WS.peer[key] = "base"
        end
        if WSL then WSL.fromLedger[key] = nil; WSL.add(key) end
    end
end

local function wsOwnerActor(e)
    if e.kind.actor then return e.obj end
    local a = safe("ws owner", function() return e.obj:GetOwningActor() end)
    if wsValid(a) then return a end
    return nil
end

local function wsApplyOne(key, e, how)
    local o = e.obj
    WS.done[key] = true
    local ok, err
    if e.kind.door then
        ok, err = pcall(function() o:OpenDoor() end)
    else
        local owner = wsOwnerActor(e)
        if owner and fullname(owner):find("Pickup", 1, true) then
            ok = true
            if how == "live" then ok, err = pcall(function() owner:PickUp() end) end
            local okr, now2 = pcall(function() return o[e.field] end)
            if not (okr and wsBool(now2)) then pcall(function() o[e.field] = true end) end
            if how ~= "live" then
                pcall(function() owner:SetActorHiddenInGame(true) end)
                pcall(function() owner:SetActorEnableCollision(false) end)
            end
        else
            ok, err = pcall(function() o[e.field] = true end)
        end
    end
    if ok then
        WS.applied[how] = WS.applied[how] + 1
        wsMarkTrue(key)
        if how == "live" then WS.liveOk[key] = false end
        log("ws apply %s %s %s.%s (%s) ok", how, key, e.kind.cls, e.field, e.owner)
        if how == "ledger" and WSL then WSL.applied[key] = e end
    else
        WS.applied.fail = WS.applied.fail + 1
        log("ws apply %s %s %s FAILED: %s", how, key, e.owner, tostring(err))
    end
end

local function wsApply()
    local budget = 6   -- applies per tick: a fresh baseline can be hundreds of keys
    for key, how in pairs(WS.peer) do
        if budget <= 0 then break end
        local e = WS.seen[key]
        if e and e.v == false and not WS.done[key] and wsValid(e.obj) and (how ~= "ledger" or WSL.state == "ok") then
            budget = budget - 1
            wsApplyOne(key, e, how)
            e.v = true
        end
    end
end

local wsVerdict = false
local function wsJudge()
    if wsVerdict then return end
    local door, pick
    for key, ok in pairs(WS.liveOk) do
        local e = WS.seen[key]
        if ok and e then
            if e.kind.door then door = door or e.owner else pick = pick or e.owner end
        end
    end
    if door and pick then
        wsVerdict = true
        local text = string.format("PASS world state: door %s opened, %s collected (live=%d base=%d fail=%d)\n",
            door, pick, WS.applied.live, WS.applied.base, WS.applied.fail)
        writeFile("result_ws.txt", text)
        log("%s", (text:gsub("\n", "")))
    end
end

function wsTick()
    if WSL then local ok, err = pcall(WSL.tick); if not ok then log("ERR ledger: %s", tostring(err)) end end
    wsScan()
    wsScan()
    wsApply()
    wsBuildPayload()
    wsJudge()
    wpTick()
    liftTick()
    chestTick()
end

-- test hook: open the nearest closed hub door and pick up the nearest
-- uncollected pickup, through the game's own functions
function wsTest()
    local me = locOf(getPawn())
    local bestDoor, bestPick, dd, dp = nil, nil, 1e12, 1e12
    for _, e in pairs(WS.seen) do
        if e.v == false and wsValid(e.obj) then
            local a = wsOwnerActor(e)
            local d = a and dist(locOf(a), me) or nil
            if d then
                if e.kind.door then
                    if d < dd then bestDoor, dd = e, d end
                elseif fullname(a):find("Pickup", 1, true) and d < dp then
                    bestPick, dp = { e = e, a = a }, d
                end
            end
        end
    end
    if bestDoor then
        local ok, err = pcall(function() bestDoor.obj:OpenDoor() end)
        log("wstest OpenDoor %s d=%.0f ok=%s %s", bestDoor.owner, dd, tostring(ok), err and tostring(err) or "")
    else log("wstest: no closed door loaded") end
    if bestPick then
        local ok, err = pcall(function() bestPick.a:PickUp() end)
        log("wstest PickUp %s d=%.0f ok=%s %s", bestPick.e.owner, dp, tostring(ok), err and tostring(err) or "")
    else log("wstest: no uncollected pickup loaded") end
end

function wsStatus()
    local nseen, npeer = 0, 0
    for _ in pairs(WS.seen) do nseen = nseen + 1 end
    for _ in pairs(WS.peer) do npeer = npeer + 1 end
    return string.format("ws=seen:%d true:%d peer:%d live:%d base:%d fail:%d scans:%d\n",
        nseen, #WS.trueList, npeer, WS.applied.live, WS.applied.base, WS.applied.fail, WS.scans) .. (WSL and WSL.status() or "")
end

-- ---------------------------------------------------------------------------
-- world progression sync: store B (per-actor named ints/bools, e.g. chest
-- TimesUsed), quest log (narrative tags, active/completed quests) and data
-- layer runtime states (host authoritative).
--
-- Unlike store A these travel by name, not by hash: the receiver may not have
-- the entry (or the actor) at all, and writes it through the game's own
-- SpartaWorldStateHelper so it is saved and read back when the actor loads.
-- Every section is a list of ["name",value,live] triples: recent changes
-- (live=1) plus a rotating window over everything else.
-- ---------------------------------------------------------------------------
WP = {
    helper = nil, sub = nil, qlog = nil, dlm = nil,
    b = { mine = {}, list = {}, recent = {}, peer = {}, done = {}, rot = 1, applied = 0, fail = 0, ok = {} },
    q = { mine = {}, list = {}, recent = {}, removed = {}, peer = {}, done = {}, rot = 1, applied = 0, fail = 0, ok = {} },
    d = { mine = {}, list = {}, recent = {}, peer = {}, inst = {}, rot = 1, applied = 0, fail = 0, last = {}, ok = {} },
    payload = "", test = nil, seenTag = {},
}
QS_ENABLED, DL_ENABLED = true, true
do
    local f = io.open(DIR .. "QS_OFF.txt", "r"); if f then f:close(); QS_ENABLED = false end
    f = io.open(DIR .. "DL_OFF.txt", "r"); if f then f:close(); DL_ENABLED = false end
end

local function wpStr(v)
    if v == nil then return nil end
    local ok, r = pcall(function() return v:ToString() end)
    if ok and type(r) == "string" then return r end
    return nil
end

-- record a local value; a change after the first full read is a "recent" one
local function wpPut(set, name, v, primed)
    if set.mine[name] == nil then set.list[#set.list + 1] = name end
    if set.mine[name] ~= v then
        if primed then set.recent[name] = rt() end
        set.mine[name] = v
    end
end

-- a set counts as primed once it has been readable for WP_SETTLE seconds:
-- the quest log and layer states fill in over the first seconds after load,
-- and those must not go out as live changes or be overridden from the peer
local WP_SETTLE = 10
local function wpPrime(set)
    set.firstAt = set.firstAt or rt()
    if rt() - set.firstAt >= WP_SETTLE then set.primed = true end
end

local function wpSection(set, key, window)
    local parts, sent, tnow = {}, {}, rt()
    for name, at in pairs(set.recent) do
        if tnow - at <= WS_LIVE_SECS and set.mine[name] ~= nil then
            parts[#parts + 1] = string.format('["%s",%d,1]', name, set.mine[name]); sent[name] = true
        end
    end
    local n = #set.list
    for i = 1, math.min(window, n) do
        local name = set.list[(set.rot + i - 2) % n + 1]
        local v = set.mine[name]
        if v ~= nil and v > 0 and not sent[name] then
            parts[#parts + 1] = string.format('["%s",%d,0]', name, v); sent[name] = true
        end
    end
    if n > 0 then set.rot = (set.rot + window - 1) % n + 1 end
    return ',"' .. key .. '":[' .. table.concat(parts, ",") .. ']'
end

local function wpRead(set, line, key)
    local sec = section(line, key)
    if not sec then return end
    for name, v, l in sec:gmatch('%["([^"]+)",(%-?%d+),(%d)%]') do
        local p = set.peer[name]
        v = tonumber(v)
        -- live entries replace (they carry removals), baseline only raises
        if l == "1" then
            if not p or p.v ~= v or not p.live then set.peer[name] = { v = v, live = true }; set.done[name] = nil end
        elseif not p or (not p.live and v > p.v) then
            set.peer[name] = { v = v, live = false }
        end
    end
end

local function wpObj(cache, find)
    local o = WP[cache]
    if o and wsValid(o) then return o end
    o = find()
    if o and wsValid(o) then WP[cache] = o; return o end
    WP[cache] = nil
    return nil
end

local function wpHelper()
    if not (WP.helper and wsValid(WP.helper)) then
        WP.helper = StaticFindObject("/Script/Sparta.Default__SpartaWorldStateHelper")
    end
    return wsValid(WP.helper) and WP.helper or nil
end

-- ---- store B ----
local function wbScan()
    local sub = wpObj("sub", function() return FindFirstOf("SpartaSaveSubSystem") end)
    if not sub then return end
    local ws = sub:GetCurrentWorldStateSave()
    if not wsValid(ws) then return end
    local primed = WP.b.primed
    for _, mp in ipairs({ "ActorInts", "ActorBools" }) do
        local t = mp == "ActorInts" and "i" or "b"
        ws[mp]:ForEach(function(k, v)
            local owner = wpStr(k:get())
            -- Sparta_Player is the local player's own record (tutorials, counters)
            if owner and owner ~= "Sparta_Player" and owner:match("^[%w_]+$") then
                v:get().Map:ForEach(function(k2, v2)
                    local vn = wpStr(k2:get())
                    if vn and vn:match("^[%w_]+$") then
                        local raw = v2:get()
                        local n = t == "i" and (tonumber(raw) or 0) or (wsBool(raw) and 1 or 0)
                        wpPut(WP.b, t .. "|" .. owner .. "|" .. vn, n, primed)
                    end
                end)
            end
        end)
    end
    wpPrime(WP.b)
end

local function wbRead(h, owner, vn, t, ctx)
    local out = {}
    local ok = pcall(function()
        if t == "i" then return h:ReadAnInt(ctx, FName(owner), FName(vn), out) end
        return h:ReadABool(ctx, FName(owner), FName(vn), out)
    end)
    if not ok then return nil end
    if t == "i" then return tonumber(out.OutValue) end
    return wsBool(out.OutValue) and 1 or 0
end

-- live actor follow-up: a chest that is loaded right now
local function wbRefreshActor(owner, vn, v, live)
    if vn ~= "TimesUsed" then return end
    for _, c in ipairs(FindAllOf("BP_Interactable_Chest_C") or {}) do
        if wpStr(c:GetFName()) == owner then
            if CS then CS.ext[owner] = math.max(CS.ext[owner] or 0, v) end
            pcall(function() c.TimesUsed = v end)
            pcall(function() c:LoadChestState() end)
            if CS and CHEST_ENABLED then pcall(CS.lid, owner, c, v, not live) end
            log("wb chest %s refreshed (TimesUsed=%d)", owner, v)
        end
    end
end

local function wbApply()
    local pawn, h = getPawn(), wpHelper()
    if not (pawn and h and WP.b.primed) then return end
    local budget = 6
    for name, p in pairs(WP.b.peer) do
        if budget <= 0 then break end
        local mine = WP.b.mine[name] or 0
        if p.v > mine and not WP.b.done[name] then
            budget = budget - 1
            WP.b.done[name] = true
            local t, owner, vn = name:match("^(%a)|([%w_]+)|([%w_]+)$")
            if t then
                local ok, err = pcall(function()
                    if t == "i" then h:WriteAnInt(pawn, FName(owner), FName(vn), p.v, true)
                    else h:WriteABool(pawn, FName(owner), FName(vn), true) end
                end)
                local back = ok and wbRead(h, owner, vn, t, pawn) or nil
                if ok and t == "i" and back ~= p.v then
                    -- bModify=true may mean "add": fall back to a plain set
                    pcall(function() h:WriteAnInt(pawn, FName(owner), FName(vn), p.v, false) end)
                    back = wbRead(h, owner, vn, t, pawn)
                end
                if back == p.v then
                    WP.b.applied = WP.b.applied + 1
                    WP.b.ok[name] = p.live and "live" or "base"
                    wpPut(WP.b, name, p.v, false)
                    log("wb apply %s %s = %d (read back %s)", p.live and "live" or "base", name, p.v, tostring(back))
                    wbRefreshActor(owner, vn, p.v, p.live)
                else
                    WP.b.fail = WP.b.fail + 1
                    log("wb apply %s FAILED ok=%s back=%s %s", name, tostring(ok), tostring(back), err and tostring(err) or "")
                end
            end
        end
    end
end

-- ---- quest log ----
-- keys in WP.q (value is an int):
--   t|tag  a|quest  c|quest  k|quest   1 present, -1 removed (k = tracked quest)
--   s|quest           CurrentQuestStage + 1
--   o|quest|stage|obj objective counter (CurrentQuestObjectiveProgress) in that stage
--   u|quest           1 once turned in (BP_QuestObject_C.IsTurnedIn)
--   r|npc  e|event    NPCRelationships / NarrativeEvents (live: any value, base: max)
local function wqObj(q, id)
    local found
    pcall(function()
        local arr = q.ActiveQuests
        for i = 1, arr:GetArrayNum() do
            local o = arr[i]
            if o and o:IsValid() and wpStr(o.QuestID) == id then found = o end
        end
    end)
    return found
end

local function wqProgress(o)
    local t = {}
    pcall(function()
        o.CurrentQuestObjectiveProgress:ForEach(function(k, v)
            local n = wpStr(k:get())
            if n then t[n] = tonumber(v:get()) or 0 end
        end)
    end)
    return t
end

local WQ_PRESENCE = { t = true, a = true, c = true, k = true }

local function wqScan()
    local q = wpObj("qlog", function() return FindFirstOf("BPC_QuestLogComponent_C") end)
    if not q then return end
    local primed = WP.q.primed
    local now = {}
    pcall(function()
        local g = q.NarrativeTags.GameplayTags
        for i = 1, g:GetArrayNum() do
            local tag = wpStr(g[i].TagName)
            if tag and tag:match("^[%w_%.]+$") then now["t|" .. tag] = 1 end
        end
    end)
    for _, pair in ipairs({ { "CurrentActiveQuests", "a|" }, { "CompletedQuests", "c|" } }) do
        pcall(function()
            local arr = q[pair[1]]
            for i = 1, arr:GetArrayNum() do
                local id = wpStr(arr[i])
                if id and id:match("^[%w_%.]+$") then now[pair[2] .. id] = 1 end
            end
        end)
    end
    local tr = wpStr(q.CurrentTrackedQuest)
    if tr and tr ~= "None" and tr:match("^[%w_%.]+$") then now["k|" .. tr] = 1 end
    -- live quest objects (completed ones stay in ActiveQuests too)
    pcall(function()
        local arr = q.ActiveQuests
        for i = 1, arr:GetArrayNum() do
            local o = arr[i]
            local id = o and o:IsValid() and wpStr(o.QuestID) or nil
            if id and id:match("^[%w_%.]+$") then
                local st = tonumber(o.CurrentQuestStage) or 0
                now["s|" .. id] = st + 1
                for obj, n in pairs(wqProgress(o)) do
                    if n > 0 and obj:match("^[%w_%.]+$") then now["o|" .. id .. "|" .. st .. "|" .. obj] = n end
                end
                if wsBool(o.IsTurnedIn) then now["u|" .. id] = 1 end
            end
        end
    end)
    for _, pair in ipairs({ { "NPCRelationships", "r|" }, { "NarrativeEvents", "e|" } }) do
        pcall(function()
            q[pair[1]]:ForEach(function(k, v)
                local n = wpStr(k:get())
                if n and n:match("^[%w_%. ]+$") then now[pair[2] .. n] = tonumber(v:get()) or 0 end
            end)
        end)
    end
    for name, v in pairs(now) do
        if primed and WP.q.mine[name] == -1 then WP.q.removed[name] = nil end
        wpPut(WP.q, name, v, primed)
    end
    for name, v in pairs(WP.q.mine) do
        if v == 1 and not now[name] and WQ_PRESENCE[name:sub(1, 1)] then
            -- gone locally: a removed tag travels as -1 so the peer drops it too
            wpPut(WP.q, name, -1, primed)
            if primed then WP.q.removed[name] = true; log("wq local removal %s", name) end
        end
    end
    if next(now) then wpPrime(WP.q) end
end

-- adopt an applied value so the next scan does not report it as a local change
local function wqAdopt(name, v, how, p)
    WP.q.applied = WP.q.applied + 1
    WP.q.ok[name] = how
    WP.q.hist = WP.q.hist or {}
    WP.q.hist[name] = (WP.q.hist[name] or "") .. (how == "remove" and "-" or "+")
    if WP.q.mine[name] == nil then WP.q.list[#WP.q.list + 1] = name end
    WP.q.mine[name] = v
    WP.q.recent[name] = nil
    log("wq apply %s %s %s = %s", p.live and "live" or "base", how, name, tostring(v))
end

local WQ_STAGE_WAIT = 8

-- quest state inside a quest object: counters, stage, turn-in.
-- Returns false when the quest object is not ready yet (retry later).
local function wqApplyQuest(q, name, kind, rest, p)
    local mine = WP.q.mine[name] or 0
    if kind == "o" then
        local id, st, obj = rest:match("^([^|]+)|(%d+)|(.+)$")
        local o = id and wqObj(q, id)
        if not o then return false end
        local cur = tonumber(o.CurrentQuestStage) or 0
        -- a counter of a stage this side has already left (or a turned-in quest) is history
        if wsBool(o.IsTurnedIn) or cur > tonumber(st) then return true end
        if cur < tonumber(st) then return false end
        local have = wqProgress(o)[obj] or 0
        if have >= p.v then wqAdopt(name, have, "count", p); return true end
        -- the game's own path: one "objective heard" per missing step
        for _ = 1, math.min(p.v - have, 10) do pcall(function() o:OnObjectiveIDHeard(FName(obj)) end) end
        local back = wqProgress(o)[obj] or 0
        if back >= p.v then wqAdopt(name, back, "count", p)
        else
            -- not an objective of this stage's handler: write the counter
            pcall(function() o.CurrentQuestObjectiveProgress:Add(FName(obj), p.v) end)
            back = wqProgress(o)[obj] or 0
            if back >= p.v then wqAdopt(name, back, "countset", p)
            else WP.q.fail = WP.q.fail + 1; log("wq apply count %s FAILED (have %d)", name, back) end
        end
        return true
    elseif kind == "s" then
        local o = wqObj(q, rest)
        if not o or wsBool(o.IsTurnedIn) then return false end
        local cur = (tonumber(o.CurrentQuestStage) or 0) + 1
        if cur >= p.v then
            if WP.q.behind then WP.q.behind[name] = nil end
            wqAdopt(name, cur, "stage", p); return true
        end
        -- the peer finished this stage: hear its objectives here so the game
        -- advances the stage itself (rewards included). The stage moves on the
        -- same frame, so the counters are rarely seen by the peer's scan.
        WP.q.heard = WP.q.heard or {}
        local hk = name .. "@" .. cur
        if not WP.q.heard[hk] then
            WP.q.heard[hk] = true
            local objs = {}
            pcall(function()
                local ob = o.CurrentStageDetails.Objectives
                for i = 1, ob:GetArrayNum() do
                    objs[#objs + 1] = { wpStr(ob[i].ObjectiveID), tonumber(ob[i].Quantity) or 1, wsBool(ob[i].bOptional) }
                end
            end)
            local prog = wqProgress(o)
            for _, e in ipairs(objs) do
                if e[1] and not e[3] then
                    for _ = 1, math.min(e[2] - (prog[e[1]] or 0), 10) do
                        pcall(function() o:OnObjectiveIDHeard(FName(e[1])) end)
                    end
                end
            end
            log("wq stage %s: heard %d objective(s) of stage %d", rest, #objs, cur - 1)
            WP.q.stageHeard = WP.q.stageHeard or {}
            WP.q.stageHeard[rest] = (WP.q.stageHeard[rest] or 0) + #objs
            return false
        end
        -- still behind: give the game a moment, then write the stage
        WP.q.behind = WP.q.behind or {}
        WP.q.behind[name] = WP.q.behind[name] or rt()
        if rt() - WP.q.behind[name] < WQ_STAGE_WAIT then return false end
        WP.q.behind[name] = nil
        local before = wpStr(o.CurrentStageDetails.StageName)
        local ok, err = pcall(function() o.CurrentQuestStage = p.v - 1; o:InitializeQuest() end)
        local after = wpStr(o.CurrentStageDetails.StageName)
        local back = (tonumber(o.CurrentQuestStage) or 0) + 1
        if ok and back == p.v then
            wqAdopt(name, back, "stage", p)
            log("wq stage %s -> %d (stage name '%s' -> '%s')", rest, p.v - 1, tostring(before), tostring(after))
        else WP.q.fail = WP.q.fail + 1; log("wq apply stage %s FAILED %s", name, tostring(err)) end
        return true
    elseif kind == "u" then
        if p.v ~= 1 or mine == 1 then return true end
        local ok, err = pcall(function()
            if WP.q.mine["c|" .. rest] ~= 1 then
                if WP.q.mine["a|" .. rest] ~= 1 then q:AddNewQuest(FName(rest)) end
                q:CompleteQuest(FName(rest))
            end
            q:TurnInQuest(FName(rest))
        end)
        local o = wqObj(q, rest)
        if ok and o and wsBool(o.IsTurnedIn) then wqAdopt(name, 1, "turnin", p)
        else WP.q.fail = WP.q.fail + 1; log("wq apply turnin %s FAILED ok=%s %s", rest, tostring(ok), tostring(err or "")) end
        return true
    end
    return true
end

local function wqApply()
    local q = WP.qlog
    if not (q and wsValid(q) and WP.q.primed) then return end
    local budget = 4
    WP.q.tried = WP.q.tried or {}
    for name, p in pairs(WP.q.peer) do
        if budget <= 0 then break end
        local mine = WP.q.mine[name]
        local kind, id = name:match("^(%a)|(.+)$")
        if kind == "o" or kind == "s" or kind == "u" then
            if p.v > (mine or 0) and WP.q.tried[name] ~= p.v then
                -- not ready yet (quest object missing / earlier stage): retry later, no budget used
                if wqApplyQuest(q, name, kind, id, p) then WP.q.tried[name] = p.v; budget = budget - 1 end
            end
        elseif kind == "r" or kind == "e" then
            local want = (p.live and p.v ~= (mine or 0)) or (not p.live and p.v > (mine or 0))
            if want and WP.q.tried[name] ~= p.v then
                budget = budget - 1
                WP.q.tried[name] = p.v
                local map = kind == "r" and "NPCRelationships" or "NarrativeEvents"
                local ok, err = pcall(function() q[map]:Add(id, p.v) end)
                if ok then wqAdopt(name, p.v, "set", p)
                else WP.q.fail = WP.q.fail + 1; log("wq apply %s FAILED: %s", name, tostring(err)) end
            end
        elseif kind then
            local want
            if p.v == 1 and mine ~= 1 and (p.live or not WP.q.removed[name]) then want = "add"
            elseif p.v == -1 and p.live and mine == 1 then want = "remove" end
            -- never re-open a quest this side has completed
            if want and kind == "a" and WP.q.mine["c|" .. id] == 1 then want = nil end
            if want and kind ~= "t" and want == "remove" then want = nil end
            -- tracked quest: live changes both ways, the guest follows the host's baseline
            if want and kind == "k" and not p.live and ROLE == "host" then want = nil end
            if want and not WP.q.done[name] then
                budget = budget - 1
                WP.q.done[name] = true
                local ok, err = pcall(function()
                    if kind == "t" then
                        if want == "add" then q:AddNarrativeTag({ TagName = FName(id) }, false)
                        else q:RemoveNarrativeTag({ TagName = FName(id) }, false) end
                    elseif kind == "a" then q:AddNewQuest(FName(id))
                    elseif kind == "c" then
                        if WP.q.mine["a|" .. id] ~= 1 then q:AddNewQuest(FName(id)) end
                        q:CompleteQuest(FName(id))
                    elseif kind == "k" then
                        q.CurrentTrackedQuest = FName(id)
                        q:TrackQuests()
                    end
                end)
                if ok then
                    wqAdopt(name, want == "add" and 1 or -1, want, p)
                    WP.q.removed[name] = (want == "remove") or nil
                    if kind == "k" then
                        -- the quest tracked before is no longer tracked here
                        for n2, v2 in pairs(WP.q.mine) do
                            if n2 ~= name and v2 == 1 and n2:sub(1, 2) == "k|" then WP.q.mine[n2] = -1 end
                        end
                    end
                else
                    WP.q.fail = WP.q.fail + 1
                    log("wq apply %s %s FAILED: %s", want, name, tostring(err))
                end
            end
        end
    end
end

-- ---- data layers (host -> guest) ----
local function wdScan()
    local dlm = wpObj("dlm", function() return FindFirstOf("DataLayerManager") end)
    if not dlm then return end
    local primed = WP.d.primed
    for _, inst in ipairs(FindAllOf("DataLayerInstanceWithAsset") or {}) do
        local asset = inst.DataLayerAsset
        local name = asset and wsValid(asset) and wpStr(asset:GetFName()) or nil
        if name and name:match("^[%w_]+$") then
            WP.d.inst[name] = inst
            local ok, st = pcall(function() return dlm:GetDataLayerInstanceRuntimeState(inst) end)
            -- +1 so "unloaded" (0) still goes out in the rotating window
            if ok and tonumber(st) then wpPut(WP.d, name, tonumber(st) + 1, primed) end
        end
    end
    wpPrime(WP.d)
end

local function wdApply()
    if ROLE == "host" or not WP.d.primed then return end
    local dlm = WP.dlm
    if not (dlm and wsValid(dlm)) then return end
    -- many layers follow where a player stands (region lighting / art): while the
    -- players are apart each world keeps the layers of its own player's region
    local me, pl = locOf(getPawn()), puppet and locOf(puppet)
    if me and pl and math.sqrt((me.X - pl.X) ^ 2 + (me.Y - pl.Y) ^ 2) > 15000 then return end
    local budget, tnow = 2, rt()
    for name, p in pairs(WP.d.peer) do
        if budget <= 0 then break end
        local inst, mine = WP.d.inst[name], WP.d.mine[name]
        if inst and mine and mine ~= p.v and tnow - (WP.d.last[name] or -100) > 10 then
            budget = budget - 1
            WP.d.last[name] = tnow
            local ok, r = pcall(function() return dlm:SetDataLayerInstanceRuntimeState(inst, p.v - 1, false) end)
            local okb, now = pcall(function() return dlm:GetDataLayerInstanceRuntimeState(inst) end)
            local back = okb and tonumber(now) and (tonumber(now) + 1) or nil
            if ok and back == p.v then
                WP.d.applied = WP.d.applied + 1
                WP.d.ok[name] = (WP.d.ok[name] or "") .. tostring(p.v)
                wpPut(WP.d, name, back, false)
                log("wd apply %s %d -> %d ok", name, mine - 1, p.v - 1)
                WORLD_SETTLE = rt() + 1.5
            else
                WP.d.fail = WP.d.fail + 1
                log("wd apply %s -> %d FAILED ok=%s r=%s back=%s", name, p.v - 1, tostring(ok), tostring(r), tostring(back))
            end
        end
    end
end

-- test hook (host): bump the nearest chest's TimesUsed in store B, drop a
-- narrative tag and bring it back, and flip one audio data layer for a while
function wpTest()
    local pawn, h = getPawn(), wpHelper()
    local me, best, bd = locOf(pawn), nil, 1e12
    for _, c in ipairs(FindAllOf("BP_Interactable_Chest_C") or {}) do
        local d = dist(locOf(c), me)
        local owner = wpStr(c:GetFName())
        if d and d < bd and owner and (WP.b.mine["i|" .. owner .. "|TimesUsed"] or 0) == 0 then best, bd = c, d end
    end
    if best and h then
        local owner = wpStr(best:GetFName())
        local ok, err = pcall(function() h:WriteAnInt(pawn, FName(owner), FName("TimesUsed"), 1, true) end)
        log("wptest chest %s d=%.0f write ok=%s %s", owner, bd, tostring(ok), err and tostring(err) or "")
    else log("wptest: no unopened chest loaded") end
    local tag
    for name, v in pairs(WP.q.mine) do
        if v == 1 and name:sub(1, 2) == "t|" then tag = name:sub(3); break end
    end
    if WP.qlog and tag then
        local ok = pcall(function() WP.qlog:RemoveNarrativeTag({ TagName = FName(tag) }, false) end)
        log("wptest removed tag %s ok=%s (re-added in 12 s)", tag, tostring(ok))
    end
    local dl
    for name, v in pairs(WP.d.mine) do
        if v == 3 and name:find("_AU", 1, true) then dl = name; break end
    end
    if dl and WP.dlm then
        local ok = pcall(function() WP.dlm:SetDataLayerInstanceRuntimeState(WP.d.inst[dl], 1, false) end)
        log("wptest layer %s -> loaded ok=%s (restored in 12 s)", dl, tostring(ok))
    end
    WP.test = { at = rt(), tag = tag, dl = dl }
end

local function wpTestFollowUp()
    local t = WP.test
    if not t or t.finished or rt() - t.at < 12 then return end
    t.finished = true
    if t.tag and WP.qlog then
        local ok = pcall(function() WP.qlog:AddNarrativeTag({ TagName = FName(t.tag) }, false) end)
        log("wptest re-added tag %s ok=%s", t.tag, tostring(ok))
    end
    if t.dl and WP.dlm then
        local ok = pcall(function() WP.dlm:SetDataLayerInstanceRuntimeState(WP.d.inst[t.dl], 2, false) end)
        log("wptest layer %s -> activated ok=%s", t.dl, tostring(ok))
    end
end

-- quest test hook (host): start a quest from DT_QuestData1 that this side has
-- neither active nor completed, complete it 12 s later; the guest has to add
-- and then complete the same quest in its own log
function qsTest()
    local q = WP.qlog
    if not (q and wsValid(q)) then log("qstest: no quest log"); return end
    local dt = StaticFindObject("/Game/Sparta/Dialogue/QuestSystem/DT_QuestData1.DT_QuestData1")
    local lib = StaticFindObject("/Script/Engine.Default__DataTableFunctionLibrary")
    local out, id = {}, nil
    pcall(function() lib:GetDataTableRowNames(dt, out) end)
    for i = 1, #out do
        local n = wpStr(out[i]:get())
        if n and n ~= "TestQuest" and WP.q.mine["a|" .. n] ~= 1 and WP.q.mine["c|" .. n] ~= 1 then id = n; break end
    end
    if not id then log("qstest: no unused quest (%d rows)", #out); return end
    local ok, err = pcall(function() q:AddNewQuest(FName(id)) end)
    log("qstest added %s ok=%s %s (completed in 12 s)", id, tostring(ok), err and tostring(err) or "")
    WP.qtest = { at = rt(), id = id }
end

local function qsTestFollowUp()
    local t = WP.qtest
    if not t or t.finished or rt() - t.at < 12 then return end
    t.finished = true
    local ok, err = pcall(function() WP.qlog:CompleteQuest(FName(t.id)) end)
    log("qstest completed %s ok=%s %s", t.id, tostring(ok), err and tostring(err) or "")
end

-- guest verdict: a quest was added live and later completed live
local qsVerdict = false
local function qsJudge()
    if qsVerdict or ROLE == "host" then return end
    for name, how in pairs(WP.q.ok) do
        local id = name:match("^c|(.+)$")
        if id and how == "add" and WP.q.ok["a|" .. id] == "add" and WP.q.mine["c|" .. id] == 1 then
            -- read the log itself: the scan that adopts the completion runs next tick
            local active = false
            pcall(function()
                local arr = WP.qlog.CurrentActiveQuests
                for i = 1, arr:GetArrayNum() do if wpStr(arr[i]) == id then active = true end end
            end)
            qsVerdict = true
            local text = string.format("%s quest %s added then completed (active now=%s, wq=%d/%d)\n",
                active and "FAIL" or "PASS", id, tostring(active), WP.q.applied, WP.q.fail)
            writeFile("result_qs.txt", text)
            log("%s", (text:gsub("\n", "")))
            return
        end
    end
end

-- quest state test hook (host): start a hub NPC quest, track it, raise that
-- NPC's relationship, hear its first objective (6 s), move it one stage on (12 s),
-- then complete and turn it in (30 s). The guest has to mirror every step.
function qs2Test()
    local q = WP.qlog
    if not (q and wsValid(q)) then log("qs2test: no quest log"); return end
    local id
    for _, n in ipairs({ "GorfQuest", "VlasQuest", "TielsQuest", "AutoQuest" }) do
        if not id and WP.q.mine["a|" .. n] ~= 1 and WP.q.mine["c|" .. n] ~= 1 then id = n end
    end
    if not id then log("qs2test: no unused quest"); return end
    local npc = id:gsub("Quest$", "")
    local rel = WP.q.mine["r|" .. npc] or 0
    local ok, err = pcall(function()
        q:AddNewQuest(FName(id))
        q.CurrentTrackedQuest = FName(id)
        q:TrackQuests()
        q.NPCRelationships:Add(npc, rel + 2)
    end)
    log("qs2test %s added+tracked, rel %s %d -> %d ok=%s %s", id, npc, rel, rel + 2, tostring(ok), err and tostring(err) or "")
    WP.q2test = { at = rt(), id = id, npc = npc, rel = rel + 2, step = 0 }
end

local function qs2TestFollowUp()
    local t = WP.q2test
    if not t or t.step >= 3 then return end
    local q, el = WP.qlog, rt() - t.at
    local o = wqObj(q, t.id)
    if t.step == 0 and el >= 6 then
        t.step = 1
        local obj
        pcall(function() obj = wpStr(o.CurrentStageDetails.Objectives[1].ObjectiveID) end)
        local ok = obj and pcall(function() o:OnObjectiveIDHeard(FName(obj)) end)
        log("qs2test heard %s ok=%s now=%s", tostring(obj), tostring(ok), obj and tostring(wqProgress(o)[obj]) or "?")
    elseif t.step == 1 and el >= 12 then
        t.step = 2
        local before = wpStr(o.CurrentStageDetails.StageName)
        local ok, err = pcall(function() o.CurrentQuestStage = (tonumber(o.CurrentQuestStage) or 0) + 1; o:InitializeQuest() end)
        log("qs2test stage -> %s ok=%s %s (name '%s' -> '%s')", tostring(o.CurrentQuestStage), tostring(ok),
            err and tostring(err) or "", tostring(before), tostring(wpStr(o.CurrentStageDetails.StageName)))
    elseif t.step == 2 and el >= 30 then
        t.step = 3
        local ok, err = pcall(function() q:CompleteQuest(FName(t.id)); q:TurnInQuest(FName(t.id)) end)
        log("qs2test completed+turned in %s ok=%s %s turnedin=%s", t.id, tostring(ok), err and tostring(err) or "",
            tostring(o and wsBool(o.IsTurnedIn)))
    end
end

-- guest verdict: counter, stage, tracked quest, relationship and turn-in all arrived
local qs2Verdict, qs2Seen = false, nil
local function qs2Judge()
    if qs2Verdict or ROLE == "host" then return end
    local id
    for _, n in ipairs({ "GorfQuest", "VlasQuest", "TielsQuest", "AutoQuest" }) do
        if WP.q.ok["u|" .. n] == "turnin" then id = n end
    end
    if not id then return end
    qs2Seen = qs2Seen or rt()
    local count, stage, tracked, rel
    for name, how in pairs(WP.q.ok) do
        if name:find("o|" .. id .. "|", 1, true) == 1 then count = name .. "=" .. how end
        if not count and WP.q.stageHeard and WP.q.stageHeard[id] then count = "heard " .. WP.q.stageHeard[id] .. " objective(s)" end
        if name:sub(1, 2) == "r|" and how == "set" then rel = name .. "=" .. tostring(WP.q.mine[name]) end
    end
    stage = WP.q.ok["s|" .. id] == "stage" and ("s=" .. tostring(WP.q.mine["s|" .. id])) or nil
    tracked = (WP.q.mine["k|" .. id] == 1 or WP.q.ok["k|" .. id] == "add") and "tracked" or nil
    local all = count and stage and tracked and rel
    if not all and rt() - qs2Seen < 15 then return end
    qs2Verdict = true
    local text = string.format("%s quest state %s: count %s, stage %s, %s, rel %s, turned in (wq=%d/%d)\n",
        all and "PASS" or "FAIL", id, tostring(count), tostring(stage), tostring(tracked or "not tracked"),
        tostring(rel), WP.q.applied, WP.q.fail)
    writeFile("result_qs2.txt", text)
    log("%s", (text:gsub("\n", "")))
end

-- guest verdict: a store B value arrived live and a tag went away and came back
local wpVerdict = false
local function wpJudge()
    if wpVerdict or ROLE == "host" then return end
    local chest, tagCycle, layer
    for name, how in pairs(WP.b.ok) do
        if how == "live" and name:find("TimesUsed", 1, true) then chest = name end
    end
    -- the tag has to go away and then come back, the layer has to drop to
    -- "loaded" (2) and return to "activated" (3)
    for name, h in pairs(WP.q.hist or {}) do if h:find("-+", 1, true) then tagCycle = name end end
    for name, h in pairs(WP.d.ok) do if h:find("23", 1, true) then layer = name end end
    if chest and tagCycle and layer then
        wpVerdict = true
        local text = string.format("PASS world progression: %s, tag %s removed+restored, layer %s loaded+reactivated (wb=%d/%d wq=%d/%d wd=%d/%d)\n",
            chest, tagCycle, tostring(layer), WP.b.applied, WP.b.fail, WP.q.applied, WP.q.fail, WP.d.applied, WP.d.fail)
        writeFile("result_wp.txt", text)
        log("%s", (text:gsub("\n", "")))
    end
end

local function wpStep(label, fn)
    local ok, err = pcall(fn)
    if not ok then log("ERR %s: %s", label, tostring(err)) end
end

local wpTickN = 0
function wpTick()
    wpTickN = wpTickN + 1
    wpStep("wbScan", wbScan)
    wpStep("wbApply", wbApply)
    if QS_ENABLED then wpStep("wqScan", wqScan); wpStep("wqApply", wqApply) end
    -- the layer list is ~100 objects: every other tick is plenty
    if DL_ENABLED and wpTickN % 2 == 0 then wpStep("wdScan", wdScan) end
    if DL_ENABLED then wpStep("wdApply", wdApply) end
    local p = wpSection(WP.b, "wb", 12)
    if QS_ENABLED then p = p .. wpSection(WP.q, "wq", 16) end
    if DL_ENABLED and ROLE == "host" then p = p .. wpSection(WP.d, "wd", 16) end
    WP.payload = p
    if ROLE == "host" then wpStep("wpTestFollowUp", wpTestFollowUp) end
    wpStep("wpJudge", wpJudge)
    if QS_ENABLED then
        if ROLE == "host" then wpStep("qsTestFollowUp", qsTestFollowUp); wpStep("qs2TestFollowUp", qs2TestFollowUp) end
        wpStep("qsJudge", qsJudge)
        wpStep("qs2Judge", qs2Judge)
    end
end

function wpReadPeer(line)
    wpRead(WP.b, line, "wb")
    if QS_ENABLED then wpRead(WP.q, line, "wq") end
    if DL_ENABLED and ROLE ~= "host" then wpRead(WP.d, line, "wd") end
end

function wpStatus()
    local function cnt(t) local n = 0; for _ in pairs(t) do n = n + 1 end; return n end
    return string.format("wp=b:%d/%d+%d!%d q:%d/%d+%d!%d d:%d/%d+%d!%d\n",
        #WP.b.list, cnt(WP.b.peer), WP.b.applied, WP.b.fail,
        #WP.q.list, cnt(WP.q.peer), WP.q.applied, WP.q.fail,
        #WP.d.list, cnt(WP.d.peer), WP.d.applied, WP.d.fail)
end

-- ---------------------------------------------------------------------------
-- lift sync: BP_HubLift_C has no save data, so its floor is synced live.
-- A lever pull (or the button in the cabin) ends in BP_HubLift_C:ActivateLift,
-- which flips TargetFloor and sets bLiftActive while the cabin (SM_Lift, not the
-- actor root) travels ~7 s. The 2 Hz tick watches for the false->true edge of
-- bLiftActive; a local edge bumps seq and goes out as "wl":[["name",seq,floor,active]].
-- The receiver calls ActivateLift() when the peer's seq advanced (or, guest only,
-- once at boot when the host's resting floor differs) and its own lift rests on the
-- other floor. Its own edge from that call is marked as a replay and not re-sent.
-- LIFT_OFF.txt disables it.
-- ---------------------------------------------------------------------------
LF = { lifts = {}, peer = {}, peerSeen = {}, want = {}, replay = {}, applied = 0, fail = 0,
       payload = "", primed = nil, based = false, watch = nil, verdict = false }
LIFT_ENABLED = true
do local f = io.open(DIR .. "LIFT_OFF.txt", "r"); if f then f:close(); LIFT_ENABLED = false end end

function LF.cabinZ(o)
    local ok, z = pcall(function() return o.SM_Lift:K2_GetComponentLocation().Z end)
    return ok and tonumber(z) or nil
end

function LF.scan()
    local tnow = rt()
    for _, o in ipairs(FindAllOf("BP_HubLift_C") or {}) do
        local name = wsValid(o) and wpStr(o:GetFName()) or nil
        if name and name:match("^[%w_]+$") then
            local e = LF.lifts[name]
            if not e or e.obj ~= o then
                e = { obj = o, seq = e and e.seq or 0, last = nil }
                LF.lifts[name] = e
            end
            local okA, a = pcall(function() return o.bLiftActive end)
            local okF, fl = pcall(function() return o.TargetFloor end)
            if okA and okF and tonumber(fl) then
                local active = wsBool(a)
                e.floor, e.active = tonumber(fl), active
                if active and e.last == false then
                    local r = LF.replay[name]
                    if r and tnow - r < 6 then
                        LF.replay[name] = nil
                    else
                        e.seq = e.seq + 1
                        log("lift %s local start -> floor %d (seq %d)", name, e.floor, e.seq)
                    end
                end
                e.last = active
            end
        end
    end
    if not LF.primed then LF.primed = tnow end
end

function LF.apply()
    local tnow = rt()
    if not LF.primed or tnow - LF.primed < 10 then return end
    -- guest boot: rest on the host's floor
    if not LF.based and ROLE ~= "host" then
        LF.based = true
        for name, p in pairs(LF.peer) do
            local e = LF.lifts[name]
            if e and p.active == 0 and not e.active and e.floor ~= p.floor then LF.want[name] = p.floor end
        end
    end
    for name, floor in pairs(LF.want) do
        local e = LF.lifts[name]
        if not (e and wsValid(e.obj)) then LF.want[name] = nil
        elseif e.floor == floor then LF.want[name] = nil
        elseif not e.active then
            -- (still moving the other way: wait until it stops, then send it back)
            LF.want[name] = nil
            LF.replay[name] = tnow
            local z0 = LF.cabinZ(e.obj)
            local ok, err = pcall(function() e.obj:ActivateLift() end)
            local okA, a = pcall(function() return e.obj.bLiftActive end)
            if ok and okA and wsBool(a) then
                LF.applied = LF.applied + 1
                LF.watch = { name = name, z0 = z0, floor = floor }
                log("lift %s replay -> floor %d ok (cabin z %.0f)", name, floor, z0 or 0)
            else
                LF.fail = LF.fail + 1
                LF.replay[name] = nil
                log("lift %s replay -> floor %d FAILED ok=%s %s active=%s", name, floor, tostring(ok), err and tostring(err) or "", tostring(a))
            end
        end
    end
end

function LF.judge()
    local w = LF.watch
    if LF.verdict or not w then return end
    local e = LF.lifts[w.name]
    local z = e and LF.cabinZ(e.obj)
    if z and w.z0 and math.abs(z - w.z0) > 1000 and e.floor == w.floor then
        LF.verdict = true
        local text = string.format("PASS lift %s followed the peer to floor %d (cabin z %.0f -> %.0f, applied %d, fail %d)\n",
            w.name, w.floor, w.z0, z, LF.applied, LF.fail)
        writeFile("result_lift.txt", text)
        log("%s", (text:gsub("\n", "")))
    end
end

function LF.build()
    local parts = {}
    for name, e in pairs(LF.lifts) do
        if e.floor then
            parts[#parts + 1] = string.format('["%s",%d,%d,%d]', name, e.seq, e.floor, e.active and 1 or 0)
        end
    end
    LF.payload = #parts > 0 and (',"wl":[' .. table.concat(parts, ",") .. ']') or ""
end

function liftReadPeer(line)
    if not LIFT_ENABLED then return end
    local sec = section(line, "wl")
    if not sec then return end
    for name, seq, floor, active in sec:gmatch('%["([%w_]+)",(%d+),(%-?%d+),(%d)%]') do
        seq, floor, active = tonumber(seq), tonumber(floor), tonumber(active)
        local seen = LF.peerSeen[name]
        if seen and seq > seen then LF.want[name] = floor end
        LF.peerSeen[name] = seq
        LF.peer[name] = { seq = seq, floor = floor, active = active }
    end
end

-- test hook: start the nearest resting lift the way its lever does (ActivateLift)
function liftTest()
    local best, bd, me = nil, 1e12, locOf(getPawn())
    for name, e in pairs(LF.lifts) do
        local d = dist(locOf(e.obj), me) or 1e11
        if d < bd and not e.active then best, bd = name, d end
    end
    if not best then log("lifttest: no resting lift loaded"); return end
    local ok, err = pcall(function() LF.lifts[best].obj:ActivateLift() end)
    log("lifttest ActivateLift %s d=%.0f ok=%s %s", best, bd, tostring(ok), err and tostring(err) or "")
end

function liftTick()
    if not LIFT_ENABLED then LF.payload = ""; return end
    local function st(label, fn) local ok, err = pcall(fn); if not ok then log("ERR %s: %s", label, tostring(err)) end end
    st("liftScan", LF.scan)
    st("liftApply", LF.apply)
    st("liftJudge", LF.judge)
    st("liftBuild", LF.build)
end

function liftStatus()
    local n, seq, np = 0, 0, 0
    for _, e in pairs(LF.lifts) do n = n + 1; seq = seq + e.seq end
    for _ in pairs(LF.peer) do np = np + 1 end
    return string.format("lift=n:%d seq:%d peer:%d applied:%d fail:%d\n", n, seq, np, LF.applied, LF.fail)
end

-- ---------------------------------------------------------------------------
-- chest sync: lid replay + shared loot
--
-- Store B already carries a chest's TimesUsed (section wb), which is what the
-- chest reads when it loads. This adds what happens while both are playing:
-- a loaded chest whose TimesUsed rises without the peer having caused it is a
-- local open. It goes out at once as "wc":[["name",seq,count,done,"loot"]] so
-- the peer plays the lid (SK_Chest:PlayAnimation(ChestMontage) - the chest's own
-- open functions do nothing when called from Lua). The loot is whatever the
-- opener's inventory gained in the 8 s after the open (any reward type, read as
-- item definition -> count); it follows with done=1 and the peer adds the same
-- stacks with AddItemByClass, so both players get the chest's contents.
-- CHEST_OFF.txt disables it.
-- ---------------------------------------------------------------------------
CS = { chests = {}, snaps = {}, events = {}, seq = 0, ext = {}, lidCount = {}, done = {}, queue = {},
       applied = 0, loot = 0, fail = 0, payload = "", verdict = false, imCls = nil }
CHEST_ENABLED = true
do local f = io.open(DIR .. "CHEST_OFF.txt", "r"); if f then f:close(); CHEST_ENABLED = false end end
CS.SETTLE, CS.LOOT_SECS, CS.KEEP_SECS = 8, 8, 40

-- the local player's inventory (the puppet is a player character too)
function CS.im()
    local pawn = getPawn()
    if not pawn then return nil end
    if not (CS.imCls and wsValid(CS.imCls)) then CS.imCls = StaticFindObject("/Script/Sparta.SpartaItemManagerComponent") end
    local ok, im = pcall(function() return pawn:GetComponentByClass(CS.imCls) end)
    if ok and wsValid(im) then return im end
    return nil
end

function CS.count(im, def)
    local ok, n = pcall(function() return im:GetTotalItemCountByDefinition(def) end)
    return ok and tonumber(n) or nil
end

-- item definition path -> total count
function CS.snapshot()
    local im = CS.im()
    if not im then return nil end
    local out = {}
    local arr = im:GetAllItems()
    for i = 1, #arr do
        local inst = arr[i]
        pcall(function() if inst.get then inst = inst:get() end end)
        local okd, def = pcall(function() return inst.ItemDef end)
        if okd and def and wsValid(def) then
            local path = fullname(def):match("%s(%S+)$")
            if path and path:match("^/[%w_/%.]+$") and not out[path] then out[path] = CS.count(im, def) or 0 end
        end
    end
    return out
end

-- open the lid of a loaded chest; snap=true puts it straight at the end
function CS.lid(name, obj, count, snap)
    if (CS.lidCount[name] or 0) >= count then return true end
    CS.lidCount[name] = count
    CS.ext[name] = math.max(CS.ext[name] or 0, count)
    pcall(function() if (tonumber(obj.TimesUsed) or 0) < count then obj.TimesUsed = count end end)
    pcall(function() obj.RewardGiven = true end)
    local ok, err = pcall(function()
        obj.SK_Chest:PlayAnimation(obj.ChestMontage, false)
        if snap then obj.SK_Chest:SetPosition(1000.0, false) end
    end)
    log("chest %s lid %s ok=%s %s", name, snap and "snap" or "play", tostring(ok), err and tostring(err) or "")
    return ok
end

function CS.scan()
    local tnow = rt()
    for _, o in ipairs(FindAllOf("BP_Interactable_Chest_C") or {}) do
        local name = wsValid(o) and wpStr(o:GetFName()) or nil
        if name and name:match("^[%w_]+$") then
            local e = CS.chests[name]
            if not e or e.obj ~= o then
                e = { obj = o, born = tnow, used = nil }
                CS.chests[name] = e
            end
            local ok, u = pcall(function() return o.TimesUsed end)
            u = ok and tonumber(u) or nil
            if u and tnow - e.born >= CS.SETTLE then
                if e.used and u > e.used and (CS.ext[name] or 0) < u then
                    CS.seq = CS.seq + 1
                    -- the oldest snapshot (~3 s back) is the inventory before the open
                    CS.events[name] = { seq = CS.seq, count = u, at = tnow, base = CS.snaps[1], done = false, loot = "" }
                    CS.lidCount[name] = u
                    log("chest %s local open (TimesUsed %d -> %d, seq %d)", name, e.used, u, CS.seq)
                end
                e.used = u
            end
        end
    end
    -- inventory snapshots, 2 Hz, last 6 kept
    local snap = CS.snapshot()
    if snap then
        table.insert(CS.snaps, snap)
        if #CS.snaps > 6 then table.remove(CS.snaps, 1) end
    end
    -- loot window over: whatever the inventory gained is the chest's loot
    for name, ev in pairs(CS.events) do
        if not ev.done and tnow - ev.at >= CS.LOOT_SECS then
            ev.done = true
            local parts, now = {}, CS.snaps[#CS.snaps] or {}
            for path, n in pairs(now) do
                local d = n - ((ev.base or {})[path] or 0)
                if ev.base and d > 0 then parts[#parts + 1] = path .. "=" .. d end
            end
            ev.loot = table.concat(parts, ";")
            log("chest %s loot: %s", name, ev.loot ~= "" and ev.loot or "(nothing)")
        end
        if ev.done and tnow - ev.at > CS.LOOT_SECS + CS.KEEP_SECS then CS.events[name] = nil end
    end
end

function CS.build()
    local parts = {}
    for name, ev in pairs(CS.events) do
        parts[#parts + 1] = string.format('["%s",%d,%d,%d,"%s"]', name, ev.seq, ev.count, ev.done and 1 or 0, ev.loot)
    end
    CS.payload = #parts > 0 and (',"wc":[' .. table.concat(parts, ",") .. ']') or ""
end

function csReadPeer(line)
    if not CHEST_ENABLED then return end
    local sec = section(line, "wc")
    if not sec then return end
    for name, seq, count, done, loot in sec:gmatch('%["([%w_]+)",(%d+),(%d+),(%d),"([^"]*)"%]') do
        local id = name .. "#" .. seq
        local st = CS.done[id]
        if not st then st = { lid = false, loot = false }; CS.done[id] = st end
        if not st.lid then
            st.lid = true
            count = tonumber(count)
            CS.ext[name] = math.max(CS.ext[name] or 0, count)
            local e = CS.chests[name]
            if e and wsValid(e.obj) then
                if CS.lid(name, e.obj, count, false) then CS.applied = CS.applied + 1 else CS.fail = CS.fail + 1 end
                CS.watchLid = { name = name, at = rt() }
            else
                log("chest %s opened by peer (not loaded here; store B carries it)", name)
            end
        end
        if done == "1" and not st.loot then
            st.loot = true
            for path, n in loot:gmatch("([^;=]+)=(%d+)") do
                CS.queue[#CS.queue + 1] = { path = path, n = tonumber(n), tries = 0, chest = name }
            end
        end
    end
end

-- grant queued loot (an item definition not in memory yet is retried)
function CS.grant()
    if #CS.queue == 0 then return end
    local im = CS.im()
    if not im then return end
    local keep = {}
    for _, q in ipairs(CS.queue) do
        local def = StaticFindObject(q.path)
        if def and wsValid(def) then
            local before = CS.count(im, def) or 0
            local ok, err = pcall(function() im:AddItemByClass(def, q.n) end)
            local after = CS.count(im, def) or 0
            if ok and after > before then
                CS.loot = CS.loot + 1
                log("chest %s loot +%d %s (%d -> %d)", q.chest, q.n, q.path, before, after)
                CS.lastGrant = { path = q.path, before = before, after = after, chest = q.chest }
            else
                CS.fail = CS.fail + 1
                log("chest %s loot %s FAILED ok=%s %s (%d -> %d)", q.chest, q.path, tostring(ok), err and tostring(err) or "", before, after)
            end
        else
            q.tries = q.tries + 1
            if q.tries < 60 then keep[#keep + 1] = q
            else CS.fail = CS.fail + 1; log("chest %s loot %s: item definition never loaded", q.chest, q.path) end
        end
    end
    CS.queue = keep
end

function CS.judge()
    if CS.verdict or not CS.watchLid or not CS.lastGrant then return end
    local e = CS.chests[CS.watchLid.name]
    if not (e and wsValid(e.obj)) or CS.lastGrant.chest ~= CS.watchLid.name then return end
    local okp, pos = pcall(function() return e.obj.SK_Chest:GetPosition() end)
    if okp and tonumber(pos) and tonumber(pos) > 0.5 then
        CS.verdict = true
        local g = CS.lastGrant
        local text = string.format("PASS chest %s: lid replayed (anim pos %.2f), loot %s %d -> %d (applied %d, loot %d, fail %d)\n",
            CS.watchLid.name, pos, g.path:match("[^/]+$"), g.before, g.after, CS.applied, CS.loot, CS.fail)
        writeFile("result_chest.txt", text)
        log("%s", (text:gsub("\n", "")))
    end
end

-- test hook (host): open the nearest unopened chest the way the game records it
-- (TimesUsed on the actor + store B), play its lid, and put one item in the
-- inventory during the loot window, standing in for its reward
function csTest()
    local pawn = getPawn()
    local me = locOf(pawn)
    local best, bd, bn = nil, 1e12, nil
    for name, e in pairs(CS.chests) do
        local d = wsValid(e.obj) and dist(locOf(e.obj), me) or nil
        if d and d < bd and e.used == 0 and (WP.b.mine["i|" .. name .. "|TimesUsed"] or 0) == 0 then best, bd, bn = e, d, name end
    end
    if not best then log("cstest: no unopened chest loaded"); return end
    pcall(function() best.obj.TimesUsed = 1 end)
    pcall(function()
        local helper = StaticFindObject("/Script/Sparta.Default__SpartaWorldStateHelper")
        helper:WriteAnInt(pawn, FName(bn), FName("TimesUsed"), 1, true)
    end)
    pcall(function() best.obj.SK_Chest:PlayAnimation(best.obj.ChestMontage, false) end)
    local im = CS.im()
    local snap = CS.snaps[#CS.snaps] or {}
    local pick
    for path, n in pairs(snap) do if n > 0 and (not pick or path < pick) then pick = path end end
    if im and pick then
        local ok = pcall(function() im:AddItemByClass(StaticFindObject(pick), 1) end)
        log("cstest chest %s d=%.0f, +1 %s ok=%s", bn, bd, pick, tostring(ok))
    else
        log("cstest chest %s d=%.0f, no item to stand in for loot", bn, bd)
    end
end

function chestTick()
    if not CHEST_ENABLED then CS.payload = ""; return end
    local function st(label, fn) local ok, err = pcall(fn); if not ok then log("ERR %s: %s", label, tostring(err)) end end
    st("chestScan", CS.scan)
    st("chestGrant", CS.grant)
    st("chestJudge", CS.judge)
    st("chestBuild", CS.build)
end

function chestStatus()
    local n, ev = 0, 0
    for _ in pairs(CS.chests) do n = n + 1 end
    for _ in pairs(CS.events) do ev = ev + 1 end
    return string.format("chest=n:%d seq:%d events:%d applied:%d loot:%d queue:%d fail:%d\n", n, CS.seq, ev, CS.applied, CS.loot, #CS.queue, CS.fail)
end

-- ---------------------------------------------------------------------------
-- status file for the harness (2 Hz)
-- ---------------------------------------------------------------------------
-- count actors in a 15k box around a point (two calls per session at most)
-- (walking every Actor from this thread while cells stream in froze the Lua
-- thread of a travelling client; static mesh actors are a few hundred)
local function actorsNear(p)
    local n = 0
    for _, a in pairs(FindAllOf("StaticMeshActor") or {}) do
        local l = locOf(a)
        if l and math.abs(l.X - p.X) < 15000 and math.abs(l.Y - p.Y) < 15000 then n = n + 1 end
    end
    return n
end

-- 2 Hz: re-assert the puppet's sources every 10 s; when the peer is far away
-- (> 30k) measure once that its area really got loaded here (result_stream.txt)
local function streamTick()
    local now = rt()
    if TP_HOLD and now - TP_HOLD.at > 6 then
        local p = TP_HOLD.pawn
        TP_HOLD = nil
        if isLive(p) then safe("tp release", function() p.CharacterMovement:SetMovementMode(1, 0) end) end
        log("tp hold released")
    end
    if not puppet then return end
    if now - STREAM.checked > 10 then
        STREAM.checked = now
        for _, e in ipairs(streamSources(puppet)) do
            local want = STREAM_ENABLED and not e.blocking
            local en = safe("stream read", function() return e.c.bStreamingSourceEnabled end)
            if en ~= want then
                safe("stream set", function()
                    if want then e.c:EnableStreamingSource() else e.c:DisableStreamingSource() end
                end)
                log("stream source %s re-set to %s", e.blocking and "blocking" or "non-blocking", tostring(want))
            end
        end
    end
    if STREAM.verdict or not (lastPeer and lastPeer.live) then return end
    local me, pl = locOf(getPawn()), locOf(puppet)
    if not (me and pl) then return end
    local d = math.sqrt((me.X - pl.X) ^ 2 + (me.Y - pl.Y) ^ 2)
    if d < 30000 then STREAM.farAt = nil; return end
    local moving = STREAM.lastPl and math.sqrt((pl.X - STREAM.lastPl.X) ^ 2 + (pl.Y - STREAM.lastPl.Y) ^ 2) > 300
    STREAM.lastPl = { X = pl.X, Y = pl.Y }
    if moving and STREAM.farAt then STREAM.farAt = now; STREAM.travelled = true; return end
    if not STREAM.farAt then
        STREAM.farAt = now
        STREAM.n0 = actorsNear(pl)
        log("peer far away (%.0f uu): %d actors around it now", d, STREAM.n0)
    elseif now - STREAM.farAt > 25 then
        STREAM.n1 = actorsNear(pl)
        -- loaded around the peer now (n0 is high when this client had that area
        -- loaded already, e.g. the guest looking back at the host's hub)
        local done
        for _, e in ipairs(streamSources(puppet)) do
            if not e.blocking then done = safe("stream done", function() return e.c:IsStreamingCompleted() end) end
        end
        STREAM.done = done
        local ok = STREAM_ENABLED and done == true and STREAM.n1 >= 5
        STREAM.verdict = string.format(
            "%s remote area streamed around peer %.0f uu away%s: cells around it loaded=%s, static meshes %d -> %d (sources %s); enemies there: own %d mirrored %d",
            ok and "PASS" or "FAIL", d, STREAM.travelled and " after a long travel" or "", tostring(done), STREAM.n0, STREAM.n1,
            STREAM.src, NPC_OWNED or 0, NPC_MIRRORED or 0)
        writeFile("result_stream.txt", STREAM.verdict .. "\n")
        log("stream verdict: %s", STREAM.verdict)
    end
end

-- ---------------------------------------------------------------------------
-- enemy fidelity (2 Hz): spawn replay, AI hand-back, AI targeting the peer,
-- damage taken by the peer's body, frame time
-- ---------------------------------------------------------------------------
MIRROR_ENABLED = true
do
    local f = io.open(DIR .. "MIRROR_OFF.txt", "r")
    if f then f:close(); MIRROR_ENABLED = false end
end
SPN = { by = {}, at = 0, req = {}, ok = 0, asked = 0, done = {} }
AIT = { redirects = 0, logged = {} }
PD = { sent = 0, got = nil, applied = 0 }
FT = { nearSum = 0, nearN = 0, farSum = 0, farN = 0 }

function spawnerScan(now)
    if now - SPN.at < 2 then return end
    SPN.at = now
    if not SPN.list or now - (SPN.listAt or 0) > 10 then
        SPN.list = FindAllOf("UnitSpawner") or {}
        SPN.listAt = now
    end
    local by = {}
    for _, sp in pairs(SPN.list) do
        if not safe("spawner valid", function() return sp:IsValid() end) then goto nextsp end
        local st = actorTail(fullname(sp))
        by[st] = sp
        local e = safe("spawner enemy", function() return sp.SpawnedEnemy end)
        if e and safe("spawner enemy valid", function() return e:IsValid() end) then
            local key = "SP_" .. st
            local et = actorTail(fullname(e))
            if SPN_ALIAS[et] ~= key then
                SPN_ALIAS[et] = key
                if SPN.req[key] and not SPN.done[key] then
                    SPN.done[key] = true
                    SPN.ok = SPN.ok + 1
                    log("spawn replayed: %s -> local %s", key, et)
                    writeFile("result_spawn.txt", string.format(
                        "PASS enemy spawned by the peer appeared here too: %s -> %s\n", key, et))
                end
            end
        end
        ::nextsp::
    end
    SPN.by = by
end

-- the peer publishes an enemy of a spawner that has not fired here: fire the
-- spawner's own player-trigger event so it spawns the game's way
function spawnReplay(now)
    for key in pairs(PEER_CLAIMS or {}) do
        if key:sub(1, 3) == "SP_" and not tickTails[key] then
            local sp = SPN.by[key:sub(4)]
            if sp and (not SPN.req[key] or now - SPN.req[key] > 10) then
                SPN.req[key] = now
                SPN.asked = SPN.asked + 1
                -- the proximity spawner's own trigger event, with our player as the
                -- one who walked in (the puppet does not count as a player; lab sp15:
                -- spawned 20k away from the real player)
                local pawn = getPawn()
                local ok, err = pcall(function()
                    sp:BndEvt__BP_Spawner_Proximity_Sphere_K2Node_ComponentBoundEvent_1_ComponentBeginOverlapSignature__DelegateSignature(
                        sp.PlayerTrigger, pawn, pawn.CapsuleComponent, 0, false, {})
                end)
                log("spawn replay requested for %s (trigger event ok=%s %s)", key, tostring(ok), ok and "" or tostring(err))
            end
        end
    end
end

-- enemies this client owns that fight the local player while the peer is the
-- nearer one switch to the peer's body; hits on that body travel to the peer
function aiTargetPeer(now)
    if not (puppet and lastPeer and lastPeer.live) then return end
    for _, e in ipairs(NPC_LAST or {}) do
        if e.own and not deadTails[e.tail] and e.dp < e.d and e.dp < 3000 and isLive(e.a) then
            local tsc = safe("ai tsc", function() return e.a:GetController().TargetSelectionComponent end)
            local cur = tsc and safe("ai cur", function() return tsc.CurrentTarget end)
            if cur and safe("ai cur valid", function() return cur:IsValid() end) and fullname(cur) ~= fullname(puppet) then
                safe("ai override", function() tsc:SetTargetOverride(puppet, 3.0) end)
                AIT.redirects = AIT.redirects + 1
                if not AIT.logged[e.tail] then
                    AIT.logged[e.tail] = true
                    log("enemy %s retargeted to the peer's body (peer nearer: %.0f < %.0f)", e.tail, e.dp, e.d)
                end
            end
        end
    end
end

-- damage landed on the puppet is the peer's damage: count it, heal the body
function puppetDamage()
    if not puppet then return end
    local hp, dead = healthNormOf(puppet)
    if hp and hp < 0.995 and hp > 0.05 and not dead then
        PD.sent = PD.sent + (1 - hp)
        setHealthNorm(puppet, 1.0)
        log("peer body took %.3f damage (total %.3f)", 1 - hp, PD.sent)
    end
end

-- the peer reports damage its copy of us took; apply it to our own body
function applyPeerDamage(total)
    if not total then return end
    if PD.got == nil then PD.got = total; return end      -- baseline, never replay old hits
    if total <= PD.got + 0.0005 then return end
    local delta = total - PD.got
    PD.got = total
    local pawn = getPawn()
    local hp = pawn and healthNormOf(pawn)
    if not hp then return end
    local new = math.max(0, hp - delta)
    setHealthNorm(pawn, new)
    PD.applied = PD.applied + 1
    log("hit on us by the peer's enemy: %.3f -> %.3f", hp, new)
    writeFile("result_pd.txt", string.format("PASS peer-side hit applied to own body: %.3f -> %.3f\n", hp, new))
    if new <= 0 then LOCAL_HOLD = os.time() + 3; local ok = startDeath(pawn); log("killed by the peer's enemy: startDeath ok=%s", tostring(ok)) end
end

function frameTime()
    local gs = StaticFindObject("/Script/Engine.Default__GameplayStatics")
    local pawn = getPawn()
    if not (gs and pawn) then return end
    local dt = safe("delta", function() return gs:GetWorldDeltaSeconds(pawn) end)
    if not dt or dt <= 0 or dt > 1 then return end
    local me, pl = locOf(pawn), puppet and locOf(puppet)
    local far = me and pl and math.sqrt((me.X - pl.X) ^ 2 + (me.Y - pl.Y) ^ 2) > 30000
    if far then FT.farSum, FT.farN = FT.farSum + dt, FT.farN + 1 else FT.nearSum, FT.nearN = FT.nearSum + dt, FT.nearN + 1 end
end

ENEMY_TRACE = 0
local function enemyTick()
    local now = rt()
    -- the first ticks are traced: a call that blocks the Lua thread leaves no error,
    -- only the last label it reached
    ENEMY_TRACE = ENEMY_TRACE + 1
    local tr = ENEMY_TRACE <= 40 and log or function() end
    tr("et%d spawnerScan", ENEMY_TRACE); spawnerScan(now)
    tr("et%d spawnReplay", ENEMY_TRACE); spawnReplay(now)
    tr("et%d aiTargetPeer", ENEMY_TRACE); aiTargetPeer(now)
    tr("et%d puppetDamage", ENEMY_TRACE); puppetDamage()
    tr("et%d frameTime", ENEMY_TRACE); frameTime()
    tr("et%d done", ENEMY_TRACE)
    -- hand enemies back to local AI when the peer stops publishing them
    for name, m in pairs(MIR) do
        if OWNED_TAILS[name] then mirrorRelease(name, "now ours")
        elseif now - (m.seen or now) > 2 then mirrorRelease(name, "peer stopped publishing it") end
    end
end

local function streamStatus()
    local mir = 0
    for _ in pairs(MIR) do mir = mir + 1 end
    return string.format("stream=src:%s n0:%s n1:%s npcown:%d npcmirror:%d\n", STREAM.src,
        tostring(STREAM.n0), tostring(STREAM.n1), NPC_OWNED or 0, NPC_MIRRORED or 0) ..
        string.format("enemy=driven:%d taken:%d released:%d montages:%d spawnask:%d spawnok:%d retarget:%d pdsent:%.3f pdapplied:%d\n",
            mir, MIR_STATS.taken, MIR_STATS.released, MIR_STATS.montages, SPN.asked, SPN.ok, AIT.redirects, PD.sent, PD.applied) ..
        string.format("perf=near:%.2fms(%d) far:%.2fms(%d)\n",
            FT.nearN > 0 and FT.nearSum / FT.nearN * 1000 or 0, FT.nearN, FT.farN > 0 and FT.farSum / FT.farN * 1000 or 0, FT.farN)
end

local function writeSyncStatus()
    local hpParts, deadParts = {}, {}
    for tail, v in pairs(hpSeen) do hpParts[#hpParts + 1] = string.format("%s=%.3f", tail, v) end
    table.sort(hpParts)
    for tail in pairs(deadTails) do deadParts[#deadParts + 1] = tail end
    table.sort(deadParts)
    local pl = locOf(puppet)
    local me = locOf(getPawn())
    local ai = safe("animinfo", function() return puppet and puppet.CharacterMovement and puppet.CharacterMovement.MovementMode end)
    writeFile("sync.txt", string.format(
        "t=%s\nphase=%s\nrole=%s\npuppet=%s\npuppetpos=%.1f %.1f %.1f\nselfpos=%.1f %.1f %.1f\nmovemode=%s\npuppetspeed=%.1f\npeerspd=%.1f\nanim=%s\nanimidle=%d\nanimwalk=%d\nanimsprint=%d\nanimmoves=%d\nbody=%s\npeerbody=%s\nwpn=%s\npeerwpn=%s\nwpnapplied=%s\nlook=%s\nlookparts=%d\npeerlook=%s\nlookapplied=%s\npeerlookparts=%d\nlookmissing=%d\ngameloads=%d\nhp=%s\ndead=%s\n",
        ME, S.phase, ROLE, fullname(puppet),
        pl and pl.X or 0, pl and pl.Y or 0, pl and pl.Z or 0,
        me and me.X or 0, me and me.Y or 0, me and me.Z or 0,
        tostring(ai), PUPPET_SPEED or 0, peerSpeed or 0, tostring(ANIM.state),
        animSamples.idle or 0, animSamples.walk or 0, animSamples.sprint or 0, animMoveTicks or 0,
        MYBODY or "", lastPeerBody or "", MYWPN or "", lastPeerWeapon or "", WEAPON.applied or "",
        LOOK.hash(LOOK.mine), LOOK.nparts, lastPeerLook and LOOK.hash(lastPeerLook) or "", LOOK.applied and LOOK.hash(LOOK.applied) or "",
        LOOK.peerParts, LOOK.missing, GAMELOAD.done,
        table.concat(hpParts, ","), table.concat(deadParts, ",")) .. wsStatus() .. wpStatus() .. liftStatus() .. chestStatus() .. streamStatus())
end
-- ---------------------------------------------------------------------------
-- loops
-- ---------------------------------------------------------------------------
-- Every loop step is guarded on its own and logs its error instead of dying
-- silently: a single throwing call used to freeze sync for a whole session and
-- the only symptom was "the other player never moves".
local SETTLE_SKIP = { sendOwn = true, applyPeer = true, combat = true, enemy = true, healthCache = true,
    refreshChars = true, judge = true, anim = true, drive = true, worldState = true,
    myLook = true, lookSilence = true, lookTest = true, myWeapon = true, stream = true, ensureBody = true, syncBody = true }
-- held while our own world reloads (death, travel): see readyTick. sendOwn is
-- not held, it is what tells the peer to hide our body.
local HOLD_SKIP = { applyPeer = true, combat = true, enemy = true, healthCache = true, refreshChars = true,
    judge = true, anim = true, drive = true, worldState = true, myLook = true, lookSilence = true, lookTest = true,
    ensureBody = true, syncBody = true, myWeapon = true, stream = true, travel = true, patrol = true }
function localHold() return LOCAL_HOLD ~= nil and os.time() < LOCAL_HOLD end
local function step(label, fn, ...)
    if HOLD_SKIP[label] and localHold() then return true end
    if WORLD_SETTLE then
        if rt() < WORLD_SETTLE then
            if SETTLE_SKIP[label] then return true end
        else
            WORLD_SETTLE = nil
            pcall(refreshChars); pcall(fillHealthCache)
        end
    end
    local ok, err = pcall(fn, ...)
    if not ok then log("ERR %s: %s", label, tostring(err)) end
    return ok
end

-- loop liveness counters: if the Lua thread dies (it did once), heartbeat.txt
-- stops advancing and shows exactly how far each loop got
HB = { t30 = 0, t15 = 0, t2 = 0 }

LoopAsync(1000, function()
    HB.hb = (HB.hb or 0) + 1
    writeFile("heartbeat.txt", string.format("t=%s\nhb=%d\nt30=%d\nt15=%d\nt2=%d\nphase=%s\n",
        ME, HB.hb, HB.t30, HB.t15, HB.t2, S.phase))
    return false
end)

-- The loops live on UE4SS's async thread; touching UObjects from there races the
-- game thread (streaming and GC free actors mid-call): a guest died in
-- ProcessEvent on a freed object behind a hidden "Fatal Error" box. Cached
-- objects are therefore checked with IsValid before every call (isLive) and
-- actor work pauses for 1.5 s after a data layer switch. ExecuteInGameThread
-- would queue the work for the next frame (one pending job per loop).
-- GAME_THREAD.txt switches the loops to the game thread. Off by default: in this
-- build a queued game-thread job crashed in the puppet mesh swap and in the
-- summon's LoadAsset, which run fine from here (run 2026-10-22 14:58).
GAME_THREAD = false
do
    local f = io.open(DIR .. "GAME_THREAD.txt", "r")
    if f then f:close(); GAME_THREAD = true end
end
local PENDING = {}
local function onGame(key, fn)
    if PENDING[key] then return end
    PENDING[key] = true
    ExecuteInGameThread(function()
        PENDING[key] = nil
        local ok, err = pcall(fn)
        if not ok then log("ERR %s loop: %s", key, tostring(err)) end
    end)
end

local function runLoop(key, fn)
    if GAME_THREAD then return onGame(key, fn) end
    local ok, err = pcall(fn)
    if not ok then log("ERR %s loop: %s", key, tostring(err)) end
end

-- 30 Hz: puppet drive
LoopAsync(33, function() runLoop("t30", function()
    HB.t30 = HB.t30 + 1
    step("patrol", patrolTick)
    step("drive", driveTick)
    step("travel", travelTick)
end) return false end)

-- 15 Hz: network
LoopAsync(66, function() runLoop("t15", function()
    HB.t15 = HB.t15 + 1
    if S.phase ~= "sync" then return end
    step("refreshChars", refreshChars)
    step("sendOwn", sendOwn, PLAY_READY and collectNPCs() or nil)
    step("applyPeer", applyPeer)
    step("judge", judge)
    step("peerWatchdog", peerWatchdog)
    step("anim", animInstrument, PUPPET_SPEED > 45)
end) return false end)

-- 2 Hz: boot, commands, body mesh, health sampling
LoopAsync(500, function()
    -- boot (the puppet `summon`) stays on this thread: ConsoleCommandsMod's summon
    -- calls LoadAsset when it finds itself on the game thread, and LoadAsset from
    -- a queued game-thread job crashed the client (heap corruption) every time
    HB.t2 = HB.t2 + 1
    step("commands", runCommands)
    step("boot", bootTick)
    if S.phase == "sync" then step("ready", readyTick) end
    runLoop("t2", function()
    if S.phase == "sync" then
        local myBody, myPart = "", ""
        local ok = pcall(function() myBody, myPart = computeMyBody() end)
        if ok then MYBODY, MYPART = myBody, myPart end
        if BODY_STATE ~= "ready" then step("ensureBody", ensurePuppetBody) else step("syncBody", syncBodyChoice) end
        WORLD = getWorld() or WORLD
        step("myWeapon", function()
            local w, s, all = localWeaponInfo()
            MYWPN, MYWSOCK, MYWPNS = w or "", s or "", all or "[]"
        end)
        -- the weapon set changes on draw/stow (socket), swap and off-hand use
        if WEAPON_SPAWN_ENABLED and lastPeerWeapons and BODY_STATE == "ready" and not localHold() and not WORLD_SETTLE
            and WEAPON.applied ~= lastPeerWeapons and os.time() >= (WEAPON.retryAt or 0) then
            local ok, res = pcall(applyPeerWeapons, lastPeerWeapons)
            if not ok then log("ERR peerWeapon: %s", tostring(res)) end
            if not (ok and res) then WEAPON.retryAt = os.time() + 5 end
        end
        -- the rest of the look: skin materials, masks, clothing, extra meshes
        if LOOK.enabled then
            step("myLook", LOOK.tick)
            if BODY_STATE == "ready" then step("lookSilence", LOOK.silencePuppet) end
            if lastPeerLook and BODY_STATE == "ready" and not localHold() and not WORLD_SETTLE and isLive(puppet) and (not lastPeerBody or APPLIED_BODY == lastPeerBody)
                and (LOOK.applied ~= lastPeerLook or LOOK.owner ~= puppet) and os.time() >= (LOOK.retryAt or 0) then
                local ok, res = pcall(LOOK.apply, lastPeerLook)
                if not ok then log("ERR peerLook: %s", tostring(res)) end
                if not (ok and res) then LOOK.retryAt = os.time() + 3 end
            end
        end
        if LOOK_TEST then step("lookTest", LOOK_TEST) end
        step("healthCache", fillHealthCache)
        step("combat", sampleCombat)
        if WS_ENABLED then step("worldState", wsTick) end
        step("stream", streamTick)
        step("enemy", enemyTick)
        step("status", writeSyncStatus)
    end
end) return false end)

-- stale state from an earlier session must never draw a body
os.remove(DIR .. "out_" .. ME .. ".txt")
os.remove(DIR .. "peer_" .. ((ME == "A") and "B" or "A") .. ".txt")
log("=== CoopMod start (me=%s dir=%s role=%s) ===", ME, DIR, ROLE)
writeFile("result.txt", "RUNNING\n")
