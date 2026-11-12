-- Mortal Shell II co-op lab v2 (CoopLab)
--
-- Experiment bench for two open questions:
--   1. ANIMATION  why does the summoned puppet body not animate, and does
--                 driving it through CharacterMovement input fix that?
--                 Measured with real bone transforms + AnimInstance state.
--   2. COMBAT     read/write enemy health and trigger the game's own death path
--                 (SpartaHealthComponent / SpartaHealthSet).
--
-- Commands (one per line in <coop>/cmd.txt):
--   meshinfo           every SkeletalMeshComponent owned by player/puppet -> meshinfo.txt
--   bones              bone name inventory of player + puppet -> bones.txt
--   animtest [secs]    idle sample, movement-drive sample, verdict -> result_anim.txt
--   drive2 x y z [sp]  drive the puppet with movement input to a point
--   stop2              stop driving
--   enemies [radius]   nearby characters + health -> enemies.txt
--   hcinfo             nearest enemy's health component values -> hc.txt
--   hurt <0..1>        write Health on nearest enemy, log readback
--   kill               SpartaHealthComponent:StartDeath on nearest enemy
--   get <prop>         read a property of the puppet (dotted paths ok)
--   set <f>            write normalized Health on the nearest enemy
--   info / console <c>
--   scan <ClassShortName> [props=A,B] [max=N]   live instances + property values -> scan_<Class>.txt
--   getp <ClassShortName> <Prop>                one property of every live instance
--   callf <ClassShortName> <Func> [a,b,c]       call a function on up to 3 live instances
--   dumpobj            DumpAllObjects() -> UE4SS_ObjectDump.txt in the Win64 dir
--   dumpcxx            GenerateSDK() -> CXXHeaderDump folder in the Win64 dir
--   dumpsm             DumpStaticMeshes()
--   If <coop>/HOLD_MENU.txt exists at boot the lab stays on the main menu instead
--   of pressing Continue (used for LoadAllAssetsBefore* dumps).
--
-- NOTE UE4SS in this build has no working UStruct:ForEachProperty, so property
-- access goes through explicit names (all discovered from UE4SS_ObjectDump.txt).

local MODNAME = "CoopLab"
local UEHelpers = require("UEHelpers")

local function readFirstLine(p)
    local f = io.open(p, "r")
    if not f then return nil end
    local line = f:read("*l")
    f:close()
    return line
end

local DIR = nil
for _, rel in ipairs({ "coop_dir.txt", "MortalShell2/Binaries/Win64/coop_dir.txt", "./MortalShell2/Binaries/Win64/coop_dir.txt" }) do
    local p = readFirstLine(rel)
    if p and #p > 1 then
        DIR = p:gsub("\\", "/")
        if DIR:sub(-1) ~= "/" then DIR = DIR .. "/" end
        break
    end
end
if not DIR then DIR = "coop/" end

local ME = "A"
do
    local p = readFirstLine(DIR .. "playerid.txt")
    if p and #p > 0 then ME = p end
end

local function now() return os.date("%H:%M:%S") end
local logfile
local function log(fmt, ...)
    local ok, msg = pcall(string.format, fmt, ...)
    if not ok then msg = tostring(fmt) end
    local line = string.format("[%s][%s] %s", now(), ME, msg)
    print(string.format("[%s] %s\n", MODNAME, line))
    pcall(function()
        if not logfile then logfile = io.open(DIR .. "lab.log", "a") end
        if logfile then logfile:write(line, "\n"); logfile:flush() end
    end)
end

local function writeFile(name, text)
    pcall(function()
        local f = io.open(DIR .. name, "w")
        if f then f:write(text); f:close() end
    end)
end

local function appendFile(name, text)
    pcall(function()
        local f = io.open(DIR .. name, "a")
        if f then f:write(text); f:close() end
    end)
end

local function safe(label, fn, ...)
    local ok, res = pcall(fn, ...)
    if not ok then log("ERR %s: %s", label, tostring(res)); return nil end
    return res
end

local function fullname(o)
    if o == nil then return "<nil>" end
    return safe("GetFullName", function() return o:GetFullName() end) or tostring(o)
end

local function validObject(o)
    if not o then return false end
    return not fullname(o):match("^UObject")
end

local function vecstr(v)
    if not v then return "nil" end
    return string.format("%.1f %.1f %.1f", v.X or 0, v.Y or 0, v.Z or 0)
end

local function getWorld() return safe("GetWorld", function() return UEHelpers.GetWorld() end) end
local function getPC() return safe("GetPC", function() return UEHelpers.GetPlayerController() end) end
local function getPawn(pc)
    pc = pc or getPC()
    if not pc then return nil end
    local p = safe("pc.Pawn", function() return pc.Pawn end) or safe("K2_GetPawn", function() return pc:K2_GetPawn() end)
    if validObject(p) then return p end
    return nil
end
local function locOf(a)
    if not a then return nil end
    return safe("loc", function() return a:K2_GetActorLocation() end)
end
local function dist(a, b)
    if not (a and b) then return 1e9 end
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
-- boot: menu -> gameplay -> puppet
-- ---------------------------------------------------------------------------
local S = { phase = "menu", entered = os.time(), noted = 0 }
local puppet
local function enter(p) S.phase = p; S.entered = os.time(); S.noted = 0; log("phase -> %s", p) end
local function pt() return os.time() - S.entered end

local function bootTick()
    if S.phase == "menu" then
        local w
        local objs = safe("widgets", function() return FindAllOf("WBP_MainMenu_C") end)
        if objs then for _, o in pairs(objs) do w = o; break end end
        if w and validObject(w) then
            log("menu ready after %ds", pt())
            -- HOLD_MENU.txt (created before launch) keeps the lab parked on the main
            -- menu so that dumps can be taken with LoadAllAssetsBeforeDumpingObjects=1
            local holdFlag = io.open(DIR .. "HOLD_MENU.txt", "r")
            if holdFlag then
                holdFlag:close()
                log("HOLD_MENU.txt present -> staying on the main menu")
                writeFile("menu_ready.txt", "1\n")
                writeFile("ready.txt", "1\n")
                enter("holdmenu")
            else
                local ok = pcall(function() return w:ContinueGame() end)
                log("ContinueGame ok=%s", tostring(ok))
                enter("world")
            end
        elseif pt() > 300 then
            log("boot: menu timeout"); enter("world")
        elseif pt() - S.noted >= 20 then S.noted = pt(); log("boot: waiting for menu (%ds)", pt()) end

    elseif S.phase == "world" then
        local pawn = getPawn()
        if pawn and fullname(pawn):find("BP_PlayerCharacter_C", 1, true) then
            if pt() >= 4 then
                log("gameplay ready: %s at %s", fullname(pawn), vecstr(locOf(pawn)))
                enter("summon")
            end
        elseif pt() > 400 then log("boot: gameplay timeout")
        elseif pt() - S.noted >= 20 then S.noted = pt(); log("boot: waiting for gameplay (%ds)", pt()) end

    elseif S.phase == "summon" then
        local baseline, n = {}, 0
        for _, o in pairs(playerChars()) do baseline[fullname(o)] = true; n = n + 1 end
        log("summoning puppet (%d player chars present)", n)
        if n > 0 then
            S.baseline = baseline
            runConsole("summon /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C")
            enter("adopt")
        else
            enter("world")
        end

    elseif S.phase == "adopt" then
        for _, o in pairs(playerChars()) do
            if not (S.baseline or {})[fullname(o)] then
                puppet = o
                log("puppet adopted: %s", fullname(o))
                writeFile("puppet.txt", fullname(o) .. "\n")
                enter("ready")
                writeFile("ready.txt", "1\n")
                return
            end
        end
        if pt() > 25 then log("boot: summon produced nothing"); enter("summon") end
    end
end

-- ---------------------------------------------------------------------------
-- mesh / bones
-- ---------------------------------------------------------------------------
local function meshComponentsOf(actor)
    local out = {}
    for _, m in pairs(safe("FindAllOf SkeletalMeshComp", function() return FindAllOf("SkeletalMeshComponent") end) or {}) do
        local owner = safe("mesh owner", function() return m:GetOwner() end)
        if owner == actor then out[#out + 1] = m end
    end
    return out
end

local function bonesOf(mesh, maxN)
    local n = safe("numbones", function() return mesh:GetNumBones() end) or 0
    local names = {}
    for i = 0, math.min(n, (maxN or n)) - 1 do
        local nm = safe("bn", function() return mesh:GetBoneName(i):ToString() end)
        if nm then names[#names + 1] = nm end
    end
    return n, names
end

local function cmdMeshInfo()
    local out = { "# meshinfo " .. now() }
    for tag, actor in pairs({ player = getPawn(), puppet = puppet }) do
        if actor then
            out[#out + 1] = string.format("## %s %s", tag, fullname(actor))
            local meshes = meshComponentsOf(actor)
            out[#out + 1] = string.format("   %d SkeletalMeshComponent(s)", #meshes)
            for _, m in ipairs(meshes) do
                local n = safe("numbones", function() return m:GetNumBones() end) or 0
                local asset = safe("asset", function() return m:GetSkeletalMeshAsset() end)
                local anim = safe("animclass", function() return m:GetAnimClass() end)
                local animinst = safe("animinst", function() return m:GetAnimInstance() end)
                local visible = safe("visible", function() return m:IsVisible() end)
                out[#out + 1] = string.format("   - %s", fullname(m))
                out[#out + 1] = string.format("       bones=%d visible=%s asset=%s", n, tostring(visible), fullname(asset))
                out[#out + 1] = string.format("       animclass=%s animinst=%s", fullname(anim), fullname(animinst))
                if n > 0 then
                    local _, names = bonesOf(m, 40)
                    out[#out + 1] = "       first bones: " .. table.concat(names, ", ")
                end
            end
            local propMesh = safe("prop mesh", function() return actor.Mesh end)
            out[#out + 1] = "   actor.Mesh = " .. fullname(propMesh)
        end
    end
    writeFile("meshinfo.txt", table.concat(out, "\n") .. "\n")
    log("meshinfo.txt written")
end

-- The summoned player body arrives without a skeletal mesh: BP_PlayerCharacter
-- applies its DefaultMesh (a soft object reference) on the real spawn path, so a
-- summoned copy renders nothing. Copy the local player's mesh asset + AnimBP.
local function copyPuppetMesh()
    if not puppet then log("copymesh: no puppet"); return end
    local pawn = getPawn()
    local srcMesh, srcBones
    for _, m in ipairs(meshComponentsOf(pawn)) do
        local n = safe("src bones", function() return m:GetNumBones() end) or 0
        if n > 0 and not srcMesh then srcMesh, srcBones = m, n end
    end
    if not srcMesh then log("copymesh: player has no skinned mesh"); return end
    local asset = safe("src asset", function() return srcMesh:GetSkeletalMeshAsset() end)
    local animClass = safe("src animclass", function() return srcMesh:GetAnimClass() end)
    log("copymesh: src=%s bones=%d asset=%s animClass=%s",
        fullname(srcMesh), srcBones, fullname(asset), fullname(animClass))
    local dst = meshComponentsOf(puppet)
    log("copymesh: puppet has %d skeletal mesh component(s)", #dst)
    for _, m in ipairs(dst) do
        local before = safe("dst bones", function() return m:GetNumBones() end) or 0
        if asset then safe("set asset", function() return m:SetSkeletalMeshAsset(asset) end) end
        if animClass then safe("set animclass", function() return m:SetAnimInstanceClass(animClass) end) end
        local after = safe("dst bones after", function() return m:GetNumBones() end) or 0
        local anim = safe("dst anim", function() return m:GetAnimInstance() end)
        log("copymesh: %s bones %d -> %d animInst=%s", fullname(m), before, after, fullname(anim))
    end
end

-- Splitting the body-mesh fix into two steps: the combined version crashed the
-- game inside one of the two engine calls, so they are tested separately.
local function firstSkinnedMesh(actor)
    for _, m in ipairs(meshComponentsOf(actor)) do
        if (safe("bones", function() return m:GetNumBones() end) or 0) > 0 then return m end
    end
    return nil
end

function bodyMesh()
    if BODY_COMP and validObject(BODY_COMP) then return BODY_COMP end
    local best
    for _, m in ipairs(meshComponentsOf(puppet)) do
        if (safe("pm bones", function() return m:GetNumBones() end) or 0) > 0 then best = m end
    end
    return best
end
local function loadAsset(path)
    local obj = safe("LoadAsset", function() return LoadAsset(path) end)
    if obj and validObject(obj) then return obj end
    return safe("StaticFindObject", function() return StaticFindObject(path) end)
end

local SEQS = {
    idle = "/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/Idles/A_Shared_Idle_H.A_Shared_Idle_H",
    walk = "/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/Walk/H2/A_Shared_Locomotion_Walk_Fwd_H2.A_Shared_Locomotion_Walk_Fwd_H2",
    sprint = "/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/Sprint_800/H2/A_Shared_Locomotion_Sprint_800_H2.A_Shared_Locomotion_Sprint_800_H2",
}
local function cmdSetAsset()
    if not puppet then log("setasset: no puppet"); return end
    local src = firstSkinnedMesh(getPawn())
    if not src then log("setasset: player has no skinned mesh"); return end
    local asset = safe("asset", function() return src:GetSkeletalMeshAsset() end)
    log("setasset: asset=%s", fullname(asset))
    for _, m in ipairs(meshComponentsOf(puppet)) do
        local before = safe("b0", function() return m:GetNumBones() end) or 0
        safe("anim off", function() return m:SetEnableAnimation(false) end)
        safe("stop", function() return m:Stop() end)
        safe("set asset", function() return m:SetSkeletalMeshAsset(asset) end)
        safe("visible", function() return m:SetVisibility(true, true) end)
        safe("anim on", function() return m:SetEnableAnimation(true) end)
        local after = safe("b1", function() return m:GetNumBones() end) or 0
        log("setasset: %s bones %d -> %d", fullname(m), before, after)
    end
end

local function cmdClearAnim()
    if not puppet then log("clearanim: no puppet"); return end
    for _, m in ipairs(meshComponentsOf(puppet)) do
        local before = safe("ac0", function() return m:GetAnimClass() end)
        safe("clear animclass", function() return m:SetAnimInstanceClass(nil) end)
        log("clearanim: %s animClass %s -> %s", fullname(m), fullname(before),
            fullname(safe("ac1", function() return m:GetAnimClass() end)))
    end
end
local function cmdSetAnim()
    if not puppet then log("setanim: no puppet"); return end
    local src = firstSkinnedMesh(getPawn())
    if not src then log("setanim: player has no skinned mesh"); return end
    local animClass = safe("animclass", function() return src:GetAnimClass() end)
    log("setanim: class=%s", fullname(animClass))
    for _, m in ipairs(meshComponentsOf(puppet)) do
        safe("set animclass", function() return m:SetAnimInstanceClass(animClass) end)
        local ai = safe("animinst", function() return m:GetAnimInstance() end)
        log("setanim: %s animInst=%s", fullname(m), fullname(ai))
    end
end

local function cmdStat()
    for _, m in ipairs(meshComponentsOf(puppet)) do
        local anim = safe("anim", function() return m:GetAnimInstance() end)
        local asset = safe("asset", function() return m:GetSkeletalMeshAsset() end)
        log("stat: %s bones=%s asset=%s animClass=%s animInst=%s playing=%s visible=%s",
            fullname(m),
            tostring(safe("bones", function() return m:GetNumBones() end)),
            fullname(asset),
            fullname(safe("ac", function() return m:GetAnimClass() end)),
            fullname(anim),
            tostring(safe("playing", function() return m:IsPlaying() end)),
            tostring(safe("visible", function() return m:IsVisible() end)))
        if validObject(anim) then
            log("stat:   state[0]=%s moving=%s",
                tostring(safe("st", function() return anim:GetCurrentStateName(0):ToString() end)),
                tostring(safe("im", function() return anim.IsMoving end)))
        end
    end
end

-- body identity: what exactly makes this player look the way they do
local function cmdShellInfo()
    local pawn = getPawn()
    if not pawn then log("shellinfo: no pawn"); return end
    local out = { "# shellinfo " .. now(), "pawn: " .. fullname(pawn) }
    local comp = safe("gcm", function() return pawn:GetCharacterSkeletalMeshComponent() end)
    out[#out + 1] = "GetCharacterSkeletalMeshComponent = " .. fullname(comp)
    local m = firstSkinnedMesh(pawn)
    local asset = m and safe("asset", function() return m:GetSkeletalMeshAsset() end)
    local animClass = m and safe("animclass", function() return m:GetAnimClass() end)
    out[#out + 1] = "mesh component = " .. fullname(m)
    out[#out + 1] = "mesh asset     = " .. fullname(asset)
    out[#out + 1] = "asset path     = " .. tostring(asset and safe("path", function() return asset:GetPathName() end) or "-")
    out[#out + 1] = "anim class     = " .. fullname(animClass)
    out[#out + 1] = "anim path      = " .. tostring(animClass and safe("apath", function() return animClass:GetPathName() end) or "-")
    local pc = getPC()
    local shell = pc and safe("getshell", function() return pc:GetShell() end)
    out[#out + 1] = "pc:GetShell    = " .. fullname(shell)
    local defaultMesh = safe("defmesh", function() return pawn.DefaultMesh end)
    out[#out + 1] = "pawn.DefaultMesh (soft) = " .. fullname(defaultMesh)
    local pcComp = pc and safe("pcshell", function() return pc.ActiveShellCharacterData end)
    out[#out + 1] = "pc.ActiveShellCharacterData = " .. fullname(pcComp)
    writeFile("shellinfo.txt", table.concat(out, "\n") .. "\n")
    log("shellinfo.txt written")
end

-- Build the puppet body on a *fresh plain engine* SkeletalMeshComponent: the
-- Sparta mesh component on the summoned pawn crashes when its mesh is swapped,
-- but a stock USkeletalMeshComponent has no game-specific overrides.
BODY_COMP = nil

local function cmdBodyTest()
    if not puppet then log("bodytest: no puppet"); return end
    local src = firstSkinnedMesh(getPawn())
    if not src then log("bodytest: player has no skinned mesh"); return end
    local asset = safe("asset", function() return src:GetSkeletalMeshAsset() end)
    local compClass = safe("compclass", function() return StaticFindObject("/Script/Engine.SkeletalMeshComponent") end)
    if not compClass then log("bodytest: no engine SkeletalMeshComponent class"); return end
    local comp = safe("construct", function() return StaticConstructObject(compClass, puppet) end)
    if not comp then log("bodytest: StaticConstructObject failed"); return end
    log("bodytest: created %s", fullname(comp))
    local regOk, regErr = pcall(function() return comp:RegisterComponent() end)
    log("bodytest: register ok=%s err=%s", tostring(regOk), tostring(regErr))
    safe("asset", function() return comp:SetSkeletalMeshAsset(asset) end)
    local ploc = locOf(puppet)
    if ploc then
        safe("world loc", function()
            return comp:K2_SetWorldLocation({ X = ploc.X, Y = ploc.Y, Z = ploc.Z - 88 }, false, {}, true)
        end)
    end
    safe("visible", function() return comp:SetVisibility(true, true) end)
    safe("mode", function() return comp:SetAnimationMode(1) end)
    log("bodytest: bones=%s visible=%s", tostring(safe("bones", function() return comp:GetNumBones() end)),
        tostring(safe("vis", function() return comp:IsVisible() end)))
    local seq = loadAsset(SEQS.idle)
    if seq then
        local ok2, err2 = pcall(function() return comp:PlayAnimation(seq, true) end)
        log("bodytest: play idle ok=%s err=%s", tostring(ok2), tostring(err2))
    else
        log("bodytest: idle sequence not found")
    end
    BODY_COMP = comp
end


-- --- weapon inspection -------------------------------------------------------
local function cmdWeaponInfo()
    local pawn = getPawn()
    local out = { "# weaponinfo " .. now(), "pawn: " .. fullname(pawn) }
    local n = 0
    for _, w in pairs(safe("FindAllOf SpartaWeapon", function() return FindAllOf("SpartaWeapon") end) or {}) do
        local owner = safe("w owner", function() return w:GetOwner() end)
        local parentActor = safe("w parent", function() return w:GetAttachParentActor() end)
        local socket = safe("w socket", function() return w:GetAttachParentSocketName():ToString() end)
        local mesh = safe("w meshcomp", function() return w.WeaponMesh end)
        local asset = mesh and safe("w asset", function() return mesh:GetSkeletalMeshAsset() end)
        n = n + 1
        out[#out + 1] = string.format("%s\n   owner=%s parent=%s socket=%s\n   mesh=%s\n   asset=%s",
            fullname(w), fullname(owner), fullname(parentActor), tostring(socket),
            fullname(mesh), fullname(asset))
    end
    out[#out + 1] = string.format("# %d SpartaWeapon actors", n)
    writeFile("weaponinfo.txt", table.concat(out, "\n") .. "\n")
    log("weaponinfo.txt written (%d weapons)", n)
end

local function cmdSpawnProp(path, socket)
    local cls = safe("cls", function() return StaticFindObject("/Script/Engine.SkeletalMeshActor") end)
    local gs = UEHelpers.GetGameplayStatics()
    local pawn = getPawn()
    local loc = pawn and locOf(pawn)
    if not (cls and gs and loc) then log("spawnprop: preconditions missing"); return end
    local asset = path and loadSeqStatic(path)
    log("spawnprop: asset=%s socket=%s", fullname(asset), tostring(socket))
    if not asset then return end
    local xf = {
        Translation = { X = loc.X + 120, Y = loc.Y, Z = loc.Z },
        Rotation = { X = 0, Y = 0, Z = 0, W = 1 },
        Scale3D = { X = 1, Y = 1, Z = 1 },
    }
    local actor = safe("spawn", function()
        return gs:BeginDeferredActorSpawnFromClass(getWorld(), cls, xf, 1, nil)
    end)
    log("spawnprop: actor=%s", fullname(actor))
    if not actor then return end
    safe("finish", function() return gs:FinishSpawningActor(actor, xf) end)
    local mesh = safe("mesh", function() return actor.SkeletalMeshComponent end)
    log("spawnprop: mesh=%s", fullname(mesh))
    if mesh then
        safe("set asset", function() return mesh:SetSkeletalMeshAsset(asset) end)
        log("spawnprop: bones=%s", tostring(safe("bones", function() return mesh:GetNumBones() end)))
    end
    local pm = safe("pmesh", function() return puppet and puppet.Mesh end)
    log("spawnprop: puppet mesh=%s", fullname(pm))
    if pm and socket and #socket > 0 then
        local ok, err = pcall(function()
            return actor:K2_AttachToComponent(pm, socket, 0, 0, 0, false)
        end)
        log("spawnprop: attach ok=%s err=%s", tostring(ok), tostring(err))
    end
    PROP_ACTOR = actor
end
-- --- body follow / locomotion test helpers -------------------------------------
-- sample the current pose for a fixed window and report how much the bones move:
-- proves whether a sequence/montage is actually driving this component
POSE = nil

local function bonesForMesh(mesh)
    local n = safe("pn", function() return mesh:GetNumBones() end) or 0
    local picked = {}
    for i = 0, math.min(n, 400) - 1 do
        local nm = safe("pbn", function() return mesh:GetBoneName(i):ToString() end)
        if nm then
            local low = nm:lower()
            if (low == "foot_l" or low == "foot_r" or low == "hand_l" or low == "hand_r") and #picked < 4 then
                picked[#picked + 1] = nm
            end
        end
    end
    return picked
end

local function cmdPoseCheck()
    if not puppet then log("posecheck: no puppet"); return end
    local mesh = bodyMesh()
    if not mesh then log("posecheck: no mesh"); return end
    local bones = bonesForMesh(mesh)
    log("posecheck: mesh=%s bones=%s playing=%s", fullname(mesh), table.concat(bones, ","),
        tostring(safe("playing", function() return mesh:IsPlaying() end)))
    if #bones == 0 then return end
    POSE = { mesh = mesh, bones = bones, stats = {}, ticks = 0, limit = 40 }
end

local function poseTick()
    local p = POSE
    if not p then return end
    p.ticks = p.ticks + 1
    sampleInto(p.stats, p.mesh, p.bones)
    if p.ticks >= p.limit then
        local amp = ampOf(p.stats)
        log("posecheck: amplitude=%.2f over %d samples bones=%s playing=%s pos=%s", amp, p.ticks,
            table.concat(p.bones, ","),
            tostring(safe("playing", function() return p.mesh:IsPlaying() end)),
            tostring(safe("pos", function() return p.mesh:GetPosition() end)))
        POSE = nil
    end
end
local function cmdWalkMode()
    if not puppet then log("walkmode: no puppet"); return end
    local mc = safe("mc", function() return puppet.CharacterMovement end)
    if not mc then log("walkmode: no movement component"); return end
    local before = safe("mode0", function() return mc.MovementMode end)
    local ctrl = safe("ctrl", function() return puppet:GetController() end)
    safe("setmode", function() return mc:SetMovementMode(1, 0) end)
    log("walkmode: mode %s -> %s controller=%s walking=%s", tostring(before),
        tostring(safe("mode1", function() return mc.MovementMode end)),
        fullname(ctrl),
        tostring(safe("iswalking", function() return mc:IsWalking() end)))
end

local function loadSeqStatic(path)
    return safe("StaticFindObject", function() return StaticFindObject(path) end)
end


local function cmdPlay(which)
    local m = bodyMesh()
    if not m then log("play: no skinned body component"); return end
    local name = ((which or "idle"):gsub("%s", ""))
    local seq = SEQS[name] and loadSeqStatic(SEQS[name])
    log("play: %s seq=%s on %s", name, fullname(seq), fullname(m))
    if not seq then return end
    local ok, err = pcall(function() return m:PlayAnimation(seq, true) end)
    log("play: ok=%s err=%s playing=%s pos=%s", tostring(ok), tostring(err),
        tostring(safe("playing", function() return m:IsPlaying() end)),
        tostring(safe("pos", function() return m:GetPosition() end)))
end

local function cmdSeqState()
    if not (BODY_COMP and validObject(BODY_COMP)) then log("seqstate: no body"); return end
    log("seqstate: playing=%s pos=%s rate=%s bones=%s vis=%s",
        tostring(safe("p", function() return BODY_COMP:IsPlaying() end)),
        tostring(safe("pos", function() return BODY_COMP:GetPosition() end)),
        tostring(safe("rate", function() return BODY_COMP:GetPlayRate() end)),
        tostring(safe("bones", function() return BODY_COMP:GetNumBones() end)),
        tostring(safe("vis", function() return BODY_COMP:IsVisible() end)))
end

local function cmdLoadTest()
    local path = SEQS.walk
    LOADTEST = nil
    local ok, err = pcall(function()
        ExecuteInGameThread(function()
            LOADTEST = LoadAsset(path)
            log("loadtest: inside game thread obj=%s", fullname(LOADTEST))
        end)
    end)
    log("loadtest: queued ok=%s err=%s", tostring(ok), tostring(err))
end

local function followBody()
    if not (BODY_COMP and validObject(BODY_COMP) and puppet) then return end
    local loc = locOf(puppet)
    if not loc then return end
    safe("body loc", function()
        return BODY_COMP:K2_SetWorldLocation({ X = loc.X, Y = loc.Y, Z = loc.Z - 88 }, false, {}, true)
    end)
    local rot = safe("bodyrot", function() return puppet:K2_GetActorRotation() end)
    if rot then
        safe("body rot", function() return BODY_COMP:K2_SetWorldRotation(rot, false, {}, true) end)
    end
end
local function cmdSeqTest(which)
    if not puppet then log("seqtest: no puppet"); return end
    local m = BODY_COMP
    if not (m and validObject(m)) then m = meshComponentsOf(puppet)[1] end
    if not m then log("seqtest: no mesh component"); return end
    local name = (which or "idle"):gsub("%s", "")
    local seq = loadAsset(SEQS[name] or SEQS.idle)
    log("seqtest: %s seq=%s", name, fullname(seq))
    if not seq then return end
    safe("mode single node", function() return m:SetAnimationMode(1) end)
    local ok, err = pcall(function() return m:PlayAnimation(seq, true) end)
    log("seqtest: PlayAnimation ok=%s err=%s playing=%s",
        tostring(ok), tostring(err), tostring(safe("playing", function() return m:IsPlaying() end)))
end

local function cmdBones()
    local out = { "# bones " .. now() }
    for tag, actor in pairs({ player = getPawn(), puppet = puppet }) do
        if actor then
            local meshes = meshComponentsOf(actor)
            out[#out + 1] = string.format("## %s %s (%d meshes)", tag, fullname(actor), #meshes)
            for _, m in ipairs(meshes) do
                local n, names = bonesOf(m, 400)
                out[#out + 1] = string.format("   mesh %s bones=%d", fullname(m), n)
                out[#out + 1] = "   all: " .. table.concat(names, ", ")
            end
        end
    end
    writeFile("bones.txt", table.concat(out, "\n") .. "\n")
    log("bones.txt written")
end

-- ---------------------------------------------------------------------------
-- animation experiment
-- ---------------------------------------------------------------------------
local function footBones(actor)
    local meshes = meshComponentsOf(actor)
    for _, m in ipairs(meshes) do
        local n, names = bonesOf(m, 400)
        if n > 0 then
            local picked = {}
            for _, nm in ipairs(names) do
                local low = nm:lower()
                if low == "foot_l" or low == "foot_r" or low == "ball_l" or low == "ball_r" or low == "pelvis" then
                    picked[#picked + 1] = nm
                end
            end
            if #picked > 0 then return m, picked end
        end
    end
    return nil, {}
end

local ANIM

function sampleInto(stats, mesh, bones)
    for _, bone in ipairs(bones) do
        local t = safe("bone xf", function() return mesh:GetBoneTransform(bone, 1) end)
        local tr = t and safe("bone tr", function() return t.Translation end)
        if tr then
            local st = stats[bone]
            if not st then st = { n = 0 }; stats[bone] = st end
            st.minX = math.min(st.minX or tr.X, tr.X); st.maxX = math.max(st.maxX or tr.X, tr.X)
            st.minY = math.min(st.minY or tr.Y, tr.Y); st.maxY = math.max(st.maxY or tr.Y, tr.Y)
            st.minZ = math.min(st.minZ or tr.Z, tr.Z); st.maxZ = math.max(st.maxZ or tr.Z, tr.Z)
            st.n = st.n + 1
        end
    end
end

function ampOf(stats)
    local best = 0
    for _, st in pairs(stats) do
        local dx = (st.maxX or 0) - (st.minX or 0)
        local dy = (st.maxY or 0) - (st.minY or 0)
        local dz = (st.maxZ or 0) - (st.minZ or 0)
        best = math.max(best, math.sqrt(dx * dx + dy * dy + dz * dz))
    end
    return best
end

local function animVerdict()
    local a = ANIM
    if not a then return end
    local endPos = locOf(puppet)
    local travelled = dist(endPos, a.start)
    local idleAmp = ampOf(a.idle)
    local moveAmp = ampOf(a.move)
    local lines = {
        "# animtest " .. now(),
        string.format("bones used     : %s", table.concat(a.bones, ",")),
        string.format("idle amplitude : %.2f", idleAmp),
        string.format("move amplitude : %.2f", moveAmp),
        string.format("travelled      : %.1f uu", travelled),
        string.format("maxSpeed(MC)   : %.1f", a.maxSpeed or 0),
        string.format("maxWalkSpeed   : %.1f", a.maxWalkSpeed or 0),
        string.format("movement mode  : %s", tostring(a.mode)),
        string.format("anim state[0]  : %s", tostring(a.stateName)),
        string.format("IsMoving var   : %s", tostring(a.isMoving)),
    }
    local moved = travelled
    local verdict
    if moved < 100 then
        verdict = "FAIL puppet did not move with AddMovementInput"
    elseif #a.bones > 0 and moveAmp > math.max(8.0, idleAmp * 2.5) then
        verdict = string.format("PASS pose animates while moving (idle %.1f -> move %.1f, %.0f uu)", idleAmp, moveAmp, moved)
    elseif #a.bones == 0 then
        verdict = string.format("PARTIAL puppet moves (%.0f uu) but has no skeleton to animate", moved)
    else
        verdict = string.format("FAIL pose static while moving (idle %.1f, move %.1f, %.0f uu)", idleAmp, moveAmp, moved)
    end
    lines[#lines + 1] = verdict
    local text = table.concat(lines, "\n") .. "\n"
    writeFile("result_anim.txt", text)
    appendFile("lab.log", text)
    log("%s", verdict)
    ANIM = nil
end

local function animBegin(seconds)
    if ANIM then log("animtest: already running"); return end
    if not puppet then log("animtest: no puppet"); return end
    local mesh, bones = footBones(puppet)
    local mc = safe("mc", function() return puppet.CharacterMovement end)
    if mc then
        safe("speed", function() mc.MaxWalkSpeed = 600 end)
        -- a summoned pawn without a controller sits in MOVE_None; without
        -- Walking mode CharacterMovement ignores every input vector
        safe("walkmode", function() mc:SetMovementMode(1, 0) end)
        log("animtest: movement mode=%s walking=%s",
            tostring(safe("m", function() return mc.MovementMode end)),
            tostring(safe("w", function() return mc:IsWalking() end)))
    end
    local loc = locOf(puppet)
    local pawnLoc = locOf(getPawn())
    local dir = { X = 1, Y = 0 }
    if loc and pawnLoc then
        local dx, dy = pawnLoc.X - loc.X, pawnLoc.Y - loc.Y
        local L = math.sqrt(dx * dx + dy * dy)
        if L > 60 then dir = { X = dx / L, Y = dy / L } else dir = { X = -1, Y = 0 } end
    end
    ANIM = {
        mesh = mesh, bones = bones or {},
        idle = {}, move = {},
        phase = "idle", tick = 0,
        idleTicks = 45, moveTicks = math.floor((seconds or 6) * 30),
        start = loc,
        target = { X = (loc and loc.X or 0) + dir.X * 1400, Y = (loc and loc.Y or 0) + dir.Y * 1400, Z = loc and loc.Z or 0 },
        maxSpeed = 0, maxWalkSpeed = 600, mode = mc and safe("mode", function() return mc.MovementMode end) or nil,
    }
    log("animtest: start (mesh=%s bones=%d target=%s)", mesh and fullname(mesh) or "none", #ANIM.bones, vecstr(ANIM.target))
end

local function animTick()
    local a = ANIM
    if not a then return end
    a.tick = a.tick + 1
    local mc = safe("mc", function() return puppet.CharacterMovement end)

    if a.phase == "idle" then
        if a.mesh then sampleInto(a.idle, a.mesh, a.bones) end
        if a.tick >= a.idleTicks then
            a.phase = "move"; a.tick = 0
            log("animtest: idle sampled (%d pts), moving", a.idleTicks)
        end

    elseif a.phase == "move" then
        local loc = locOf(puppet)
        local dx, dy = a.target.X - (loc and loc.X or 0), a.target.Y - (loc and loc.Y or 0)
        local L = math.sqrt(dx * dx + dy * dy)
        if L > 40 then
            safe("addinput", function() return puppet:AddMovementInput({ X = dx / L, Y = dy / L, Z = 0 }, 1.0, true) end)
            safe("yaw", function() return puppet:K2_SetActorRotation({ Pitch = 0, Yaw = math.deg(math.atan(dy, dx)), Roll = 0 }, false) end)
        end
        if a.mesh then sampleInto(a.move, a.mesh, a.bones) end
        local v = mc and safe("vel", function() return mc.Velocity end)
        local spd = v and math.sqrt((v.X or 0) ^ 2 + (v.Y or 0) ^ 2 + (v.Z or 0) ^ 2) or 0
        a.maxSpeed = math.max(a.maxSpeed, spd)
        a.maxWalkSpeed = mc and (safe("mws", function() return mc.MaxWalkSpeed end) or a.maxWalkSpeed) or a.maxWalkSpeed
        -- deliberately no AnimInstance calls here: GetCurrentStateName() on the
        -- puppet's placeholder anim instance crashes UE4SS itself
        if a.tick == 10 then
            a.mode = mc and safe("mode2", function() return mc.MovementMode end)
        end
        if a.tick >= a.moveTicks or (L <= 40 and a.tick > 20) then
            a.phase = "done"
        end
    end
end

-- ---------------------------------------------------------------------------
-- combat
-- ---------------------------------------------------------------------------
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
    if not actor then return nil end
    if not hcCache[fullname(actor)] then fillHealthCache() end
    return hcCache[fullname(actor)]
end

local function nearestEnemy(radius)
    radius = radius or 4000
    local pawn = getPawn()
    local me = pawn and locOf(pawn)
    local best, bestD = nil, radius
    for _, a in pairs(safe("FindAllOf Character", function() return FindAllOf("Character") end) or {}) do
        if a ~= pawn and a ~= puppet then
            local cls = safe("cls", function() return a:GetClass() end)
            local cfn = cls and fullname(cls) or ""
            if not (cfn:find("BP_PlayerCharacter", 1, true) or cfn:find("BP_MainMenuCharacter", 1, true)) then
                local d = dist(locOf(a), me)
                if d < bestD then best, bestD = a, d end
            end
        end
    end
    return best, bestD
end

local function cmdEnemies(radius)
    radius = tonumber(radius) or 4000
    local pawn = getPawn()
    local me = pawn and locOf(pawn)
    local out = { "# enemies " .. now() }
    local rows = {}
    for _, a in pairs(safe("chars", function() return FindAllOf("Character") end) or {}) do
        if a ~= pawn and a ~= puppet then
            local cls = safe("cls", function() return a:GetClass() end)
            local cfn = cls and fullname(cls) or "?"
            if not (cfn:find("BP_PlayerCharacter", 1, true) or cfn:find("BP_MainMenuCharacter", 1, true)) then
                local d = dist(locOf(a), me)
                if d <= radius then
                    local hc = healthCompOf(a)
                    local hp, hmax, norm, dead
                    if hc then
                        hp = safe("hp", function() return hc:GetHealth() end)
                        hmax = safe("hmax", function() return hc:GetMaxHealth() end)
                        norm = safe("norm", function() return hc:GetCurrentHealthNormalized() end)
                        dead = safe("dead", function() return hc:IsDeadOrDying() end)
                    end
                    rows[#rows + 1] = string.format("%8.1f  hp=%-7s max=%-7s norm=%-7s dead=%-5s %s",
                        d, tostring(hp), tostring(hmax), tostring(norm), tostring(dead), cfn:match("([^%.]+)$") or cfn)
                end
            end
        end
    end
    table.sort(rows)
    for _, r in ipairs(rows) do out[#out + 1] = r end
    out[#out + 1] = string.format("# %d characters within %.0f", #rows, radius)
    writeFile("enemies.txt", table.concat(out, "\n") .. "\n")
    log("enemies.txt: %d rows", #rows)
end

local function setHealthNorm(hs, norm, maxhp)
    local v = norm * maxhp
    local prop = safe("health prop", function() return hs:Reflection():GetProperty("Health") end)
    if not (prop and safe("prop valid", function() return prop:IsValid() end)) then return false, "no Health property" end
    local ok, err = pcall(function()
        prop:ImportText(string.format("(BaseValue=%.3f,CurrentValue=%.3f)", v, v),
            prop:ContainerPtrToValuePtr(hs), 0, hs)
    end)
    return ok, err
end

local function cmdHurt(fraction)
    fraction = tonumber(fraction) or 0.5
    local e = nearestEnemy(4000)
    if not e then log("hurt: no enemy nearby"); return end
    local hc = healthCompOf(e)
    if not hc then log("hurt: no health component"); return end
    local hs = safe("hs", function() return hc.HealthSet end)
    local before = safe("before", function() return hc:GetHealth() end)
    local maxhp = safe("maxhp", function() return hc:GetMaxHealth() end) or 0
    local ok, err = setHealthNorm(hs, fraction, maxhp)
    local after = safe("after", function() return hc:GetHealth() end)
    local norm = safe("norm", function() return hc:GetCurrentHealthNormalized() end)
    local dead = safe("dead", function() return hc:IsDeadOrDying() end)
    log("hurt[%s] frac=%.2f ok=%s err=%s before=%s max=%s after=%s norm=%s dead=%s",
        fullname(e):match("([^%.]+)$"), fraction, tostring(ok), tostring(err),
        tostring(before), tostring(maxhp), tostring(after), tostring(norm), tostring(dead))
end

local function cmdKill()
    local e = nearestEnemy(4000)
    if not e then log("kill: no enemy nearby"); return end
    local hc = healthCompOf(e)
    if not hc then log("kill: no health component"); return end
    local before = safe("db", function() return hc:IsDeadOrDying() end)
    local ok, err = pcall(function() return hc:StartDeath() end)
    local after = safe("da", function() return hc:IsDeadOrDying() end)
    log("kill[%s] ok=%s err=%s deadBefore=%s deadAfter=%s",
        fullname(e):match("([^%.]+)$"), tostring(ok), tostring(err), tostring(before), tostring(after))
    writeFile("result_kill.txt", string.format("kill: before=%s after=%s ok=%s\n",
        tostring(before), tostring(after), tostring(ok)))
end

-- ---------------------------------------------------------------------------
-- generic probe (added for research/90-verified.md)
--   scan  <ClassShortName> [props=A,B,C] [max=N]  read properties of live instances
--   getp  <ClassShortName> <Prop>                 one property, all instances
--   callf <ClassShortName> <Func> [a,b,c]         call a function on up to 3 instances
-- Class short names are what FindAllOf() takes: native class name ("SpartaSaveComponent")
-- or a Blueprint class name incl. the _C suffix ("BP_HubDoubleDoor_C").
-- ---------------------------------------------------------------------------
local unpack_ = table.unpack or unpack

-- non-logging pcall helper (the existing safe() logs an ERR line for every failure)
local function try(fn, ...)
    local ok, res = pcall(fn, ...)
    if ok then return res end
    return nil
end

local function valstr(v)
    if v == nil then return "nil" end
    local t = type(v)
    if t == "boolean" or t == "string" then return tostring(v) end
    if t == "number" then return string.format("%.4f", v) end
    if t == "table" then
        if v.X and v.Y and v.Z then return string.format("(%.1f,%.1f,%.1f)", v.X, v.Y, v.Z) end
        if v.Pitch and v.Yaw then return string.format("(p=%.1f,y=%.1f,r=%.1f)", v.Pitch, v.Yaw, v.Roll or 0) end
        -- FGameplayTag / FGameplayTagContainer-ish / FText-ish structs
        local tag = try(function() return v.TagName end)
        if tag ~= nil then return "tag:" .. tostring(tag) end
        local s = try(function() return v:ToString() end)
        if type(s) == "string" and #s > 0 then return s end
        return "<table>"
    end
    local s = try(function() return v:GetFullName() end)
    if type(s) == "string" then
        if s:match("^TrivialObject") then return "<absent>" end   -- property does not exist
        if s:match("^FNameUserdata") or s:match("^FTextUserdata") or s:match("^ScriptStruct") then
            local str = try(function() return v:ToString() end)
            if type(str) == "string" and #str > 0 then return str end
        end
        return s
    end
    return "<" .. t .. ">"
end

local DEFAULT_SCAN_PROPS = {
    "DoorIsOpen", "DoorIsOpenName", "GateOpened", "MyBool", "TargetFloor", "bOpened", "IsOpen",
    "Opened", "bIsOpen", "IsCollected", "bCollected", "Collected", "LocationName", "Category",
    "AreaName", "bSaveLoaded", "bActivated", "Activated", "LeverUp", "bLeverUp", "bIsActive",
}

local function instancesOf(clsName)
    local raw = safe("FindAllOf " .. clsName, function() return FindAllOf(clsName) end) or {}
    local world, all = {}, {}
    for _, o in pairs(raw) do
        if validObject(o) then
            all[#all + 1] = o
            local fn = fullname(o)
            if fn:find(":PersistentLevel.", 1, true) then world[#world + 1] = o end
        end
    end
    if #world > 0 then return world end
    return all
end

local function parseScanRest(rest)
    local clsName, tail = (rest or ""):match("^(%S*)%s*(.*)$")
    local props, maxN = {}, 5
    for k, v in (tail or ""):gmatch("(%w+)=(%S+)") do
        if k == "props" then
            for p in v:gmatch("[^,]+") do props[#props + 1] = p end
        elseif k == "max" then
            maxN = tonumber(v) or maxN
        end
    end
    if #props == 0 then props = DEFAULT_SCAN_PROPS end
    return clsName or "", props, maxN
end

local function cmdScan(rest)
    local clsName, props, maxN = parseScanRest(rest)
    if clsName == "" then log("scan: need a class name"); return end
    local inst = instancesOf(clsName)
    local ploc = locOf(getPawn())
    local rows = {}
    for _, o in ipairs(inst) do
        local loc = try(function() return o:K2_GetActorLocation() end)
        local d = 1e9
        if ploc and loc then d = dist(ploc, loc) end
        rows[#rows + 1] = { o = o, d = d, loc = loc }
    end
    table.sort(rows, function(a, b) return a.d < b.d end)
    local out = { string.format("# scan %s %s instances=%d player=%s", clsName, now(), #rows, vecstr(ploc)) }
    for i = 1, math.min(#rows, maxN) do
        local r = rows[i]
        local bits = {}
        for _, p in ipairs(props) do
            local v = safe("p " .. p, function() return r.o[p] end)
            bits[#bits + 1] = string.format("%s=%s", p, valstr(v))
        end
        out[#out + 1] = string.format("[%d] %s dist=%.0f loc=%s %s",
            i, fullname(r.o), r.d, vecstr(r.loc), table.concat(bits, " "))
    end
    local name = "scan_" .. clsName:gsub("[^%w_]", "_") .. ".txt"
    writeFile(name, table.concat(out, "\n") .. "\n")
    for _, l in ipairs(out) do log("%s", l) end
end

local function cmdGetp(rest)
    local clsName, prop = (rest or ""):match("^(%S+)%s+(%S+)$")
    if not (clsName and prop) then log("getp: need <Class> <Prop>"); return end
    local inst = instancesOf(clsName)
    log("getp %s.%s instances=%d", clsName, prop, #inst)
    for i, o in ipairs(inst) do
        if i > 8 then break end
        local v = safe("getp " .. prop, function() return o[prop] end)
        log("  [%d] %s %s=%s", i, fullname(o), prop, valstr(v))
    end
end

local function cmdCallf(rest)
    local clsName, funcName, argstr = (rest or ""):match("^(%S+)%s+(%S+)%s*(.*)$")
    if not (clsName and funcName) then log("callf: need <Class> <Func>"); return end
    local inst = instancesOf(clsName)
    if #inst == 0 then log("callf %s: no live instance", clsName); return end
    local args = {}
    for a in (argstr or ""):gmatch("[^,]+") do
        a = a:gsub("%s", "")
        if a == "true" then args[#args + 1] = true
        elseif a == "false" then args[#args + 1] = false
        elseif tonumber(a) then args[#args + 1] = tonumber(a)
        elseif a ~= "" then args[#args + 1] = a end
    end
    for i = 1, math.min(#inst, 3) do
        local o = inst[i]
        local f = safe("fn", function() return o[funcName] end)
        if f then
            local ok, r = pcall(function() return o[funcName](o, unpack_(args)) end)
            local extra = ""
            if ok and r ~= nil then
                if type(r) == "table" then
                    local n = 0
                    for _ in pairs(r) do n = n + 1 end
                    extra = string.format(" ret=<table with %d entries>", n)
                else
                    extra = " ret=" .. valstr(r)
                end
            end
            log("callf %s.%s[%d] %s ok=%s%s", clsName, funcName, i, fullname(o), tostring(ok), extra)
            if not ok then log("  err: %s", tostring(r)) end
        else
            log("callf %s: no function '%s' on %s", clsName, funcName, fullname(o))
        end
    end
end

local function cmdHcInfo()
    local e = nearestEnemy(4000)
    if not e then log("hcinfo: no enemy"); return end
    local hc = healthCompOf(e)
    local out = { "# hcinfo " .. now(), "enemy: " .. fullname(e), "hc: " .. fullname(hc) }
    if hc then
        out[#out + 1] = "Health            = " .. tostring(safe("a", function() return hc:GetHealth() end))
        out[#out + 1] = "MaxHealth         = " .. tostring(safe("b", function() return hc:GetMaxHealth() end))
        out[#out + 1] = "CurrentMaxHealth  = " .. tostring(safe("c", function() return hc:GetCurrentMaxHealth() end))
        out[#out + 1] = "HealthNormalized  = " .. tostring(safe("d", function() return hc:GetCurrentHealthNormalized() end))
        out[#out + 1] = "DeathState        = " .. tostring(safe("e", function() return hc.DeathState end))
        out[#out + 1] = "IsDeadOrDying     = " .. tostring(safe("f", function() return hc:IsDeadOrDying() end))
        local hs = safe("hs", function() return hc.HealthSet end)
        out[#out + 1] = "HealthSet         = " .. fullname(hs)
        if hs then
            for _, name in ipairs({ "Health", "MaxHealth", "ShellHealth", "MaxShellHealth", "Poise", "MaxPoise", "Resolve", "MaxResolve", "Armor", "Damage" }) do
                local val = safe("hs." .. name, function() return hs[name] end)
                local cur = val and safe("cur", function() return val.CurrentValue end)
                local base = val and safe("base", function() return val.BaseValue end)
                out[#out + 1] = string.format("  %-14s cur=%-10s base=%s", name, tostring(cur), tostring(base))
            end
        end
    end
    writeFile("hc.txt", table.concat(out, "\n") .. "\n")
    log("hc.txt written")
end

local function cmdGet(propPath)
    local obj = puppet
    local cur = obj
    for part in (propPath or ""):gmatch("[^%.]+") do
        if not cur then break end
        cur = safe("get " .. part, function() return cur[part] end)
    end
    log("get %s = %s", tostring(propPath), fullname(cur))
end

-- ---------------------------------------------------------------------------
-- drive (manual)
-- ---------------------------------------------------------------------------
local DRIVE

local function cmdDrive2(x, y, z, speed)
    if not puppet then log("drive2: no puppet"); return end
    x, y, z = tonumber(x), tonumber(y), tonumber(z)
    if not x then log("drive2: bad coords"); return end
    speed = tonumber(speed) or 600
    local mc = safe("mc", function() return puppet.CharacterMovement end)
    if mc then safe("speed", function() mc.MaxWalkSpeed = speed end) end
    DRIVE = { target = { X = x, Y = y, Z = z }, speed = speed }
    log("drive2 -> %.1f %.1f %.1f speed=%.0f", x, y, z, speed)
end

local function cmdStop2()
    DRIVE = nil
    if puppet then
        safe("stop", function() return puppet:ConsumeMovementInputVector() end)
    end
    log("drive2 stopped")
end

-- ---------------------------------------------------------------------------
-- command channel
-- ---------------------------------------------------------------------------
local function consumeCommands()
    local f = io.open(DIR .. "cmd.txt", "r")
    if not f then return end
    local lines = {}
    for line in f:lines() do lines[#lines + 1] = line end
    f:close(); os.remove(DIR .. "cmd.txt")
    for _, line in ipairs(lines) do
        local cmd, rest = line:match("^(%S+)%s*(.-)$")
        pcall(function()
            if cmd == "meshinfo" then cmdMeshInfo()
            elseif cmd == "copymesh" then copyPuppetMesh()
            elseif cmd == "setasset" then cmdSetAsset()
            elseif cmd == "setanim" then cmdSetAnim()
            elseif cmd == "clearanim" then cmdClearAnim()
            elseif cmd == "stat" then cmdStat()
            elseif cmd == "shellinfo" then cmdShellInfo()
            elseif cmd == "bodytest" then cmdBodyTest()
            elseif cmd == "seqtest" then cmdSeqTest(rest)
            elseif cmd == "walkmode" then cmdWalkMode()
            elseif cmd == "weaponinfo" then cmdWeaponInfo()
            elseif cmd == "spawnprop" then
                local path, sock = rest:match("^(%S+)%s*(%S*)$")
                if path then cmdSpawnProp(path, sock) end
            elseif cmd == "posecheck" then cmdPoseCheck()
            elseif cmd == "play" then cmdPlay(rest)
            elseif cmd == "seqstate" then cmdSeqState()
            elseif cmd == "loadtest" then cmdLoadTest()
            elseif cmd == "bones" then cmdBones()
            elseif cmd == "animtest" then animBegin(tonumber(rest) or 6)
            elseif cmd == "drive2" then
                local x, y, z, sp = rest:match("([%-%d%.]+)%s+([%-%d%.]+)%s+([%-%d%.]+)%s*([%-%d%.]*)")
                if x then cmdDrive2(x, y, z, sp) end
            elseif cmd == "stop2" then cmdStop2()
            elseif cmd == "enemies" then cmdEnemies(rest)
            elseif cmd == "hcinfo" then cmdHcInfo()
            elseif cmd == "hurt" then cmdHurt(rest)
            elseif cmd == "kill" then cmdKill()
            elseif cmd == "get" then cmdGet(rest)
            elseif cmd == "scan" then cmdScan(rest)
            elseif cmd == "getp" then cmdGetp(rest)
            elseif cmd == "callf" then cmdCallf(rest)
            elseif cmd == "info" then
                log("phase=%s pawn=%s puppet=%s", S.phase, fullname(getPawn()), fullname(puppet))
            elseif cmd == "dumpobj" then
                log("dumpobj: begin (LoadAllAssets is an ini setting)")
                safe("dumpobj", function() return DumpAllObjects() end)
                writeFile("dump_done_obj.txt", "1\n")
                log("dumpobj: DumpAllObjects returned")
            elseif cmd == "dumpcxx" then
                log("dumpcxx: begin")
                safe("dumpcxx", function() return GenerateSDK() end)
                writeFile("dump_done_cxx.txt", "1\n")
                log("dumpcxx: GenerateSDK returned")
            elseif cmd == "dumpsm" then
                log("dumpsm: begin")
                safe("dumpsm", function() return DumpStaticMeshes() end)
                writeFile("dump_done_sm.txt", "1\n")
                log("dumpsm: DumpStaticMeshes returned")
            elseif cmd == "console" then runConsole(rest)
            elseif cmd == "luafile" then
                -- run a probe script from the coop dir with log() in scope
                local env = setmetatable({ log = log, DIR = DIR }, { __index = _G })
                local fn, lerr = loadfile(DIR .. rest, "t", env)
                if not fn then log("luafile %s load error: %s", rest, tostring(lerr))
                else
                    local ok, e = pcall(fn)
                    log("luafile %s ok=%s %s", rest, tostring(ok), e and tostring(e) or "")
                end
            elseif line ~= "" then log("unknown command: %s", line) end
        end)
        log("cmd handled: %s", line)
    end
end

local function driveTick()
    if not DRIVE or not puppet then return end
    local loc = locOf(puppet)
    if not loc then return end
    local dx, dy = DRIVE.target.X - loc.X, DRIVE.target.Y - loc.Y
    local L = math.sqrt(dx * dx + dy * dy)
    if L > 40 then
        safe("addinput", function() return puppet:AddMovementInput({ X = dx / L, Y = dy / L, Z = 0 }, 1.0, true) end)
        safe("yaw", function() return puppet:K2_SetActorRotation({ Pitch = 0, Yaw = math.deg(math.atan(dy, dx)), Roll = 0 }, false) end)
    else
        cmdStop2()
    end
end

-- ---------------------------------------------------------------------------
-- loops
-- ---------------------------------------------------------------------------
log("=== CoopLab v2 start (me=%s dir=%s) ===", ME, DIR)
writeFile("lab_ready.txt", "1\n")

-- UObject work runs on the game thread (see CoopMod: async-thread calls raced
-- streaming/GC and crashed); one pending job per loop
local PENDING = {}
local function onGame(key, fn)
    if PENDING[key] then return end
    PENDING[key] = true
    ExecuteInGameThread(function() PENDING[key] = nil; pcall(fn) end)
end

LoopAsync(33, function() onGame("t30", function()
    pcall(animTick)
    pcall(driveTick)
    pcall(followBody)
    pcall(poseTick)
end) return false end)

-- commands and boot stay here: the puppet summon calls LoadAsset on the game
-- thread, which crashed from a queued job
LoopAsync(500, function()
    pcall(consumeCommands)
    pcall(bootTick)
    if ANIM and ANIM.phase == "done" then pcall(animVerdict) end
    return false
end)

log("lab armed")
