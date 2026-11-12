-- Mortal Shell II co-op probe v4
-- Self-timed automation: the mod waits for each milestone itself (menu widget,
-- gameplay pawn, summoned puppet), so shader-compile stalls cannot break a run.
--
-- Commands (one per line in <coop>/cmd.txt):
--   auto          full script: menu -> ContinueGame -> summon -> drive -> verify
--   info          log world/pawn/mode
--   spawn         summon a player-character puppet and adopt it
--   puppet        log the puppet's state
--   drive x y z   teleport the puppet
--   inv / find <s> / console <cmd> / widgetcall <Class> <Func> / dump
--
-- Results land in <coop>/result.txt as "PASS ..." / "FAIL ...".

local MODNAME = "CoopProbe"
local UEHelpers = require("UEHelpers")

local EXE_DIR = "E:/Games/_MS2-coop-test/MortalShell2/Binaries/Win64/"
local DIR = EXE_DIR .. "coop/"
do
    local f = io.open(EXE_DIR .. "coop_dir.txt", "r")
    if f then
        local p = f:read("*l")
        f:close()
        if p and #p > 1 then
            DIR = p
            if DIR:sub(-1) ~= "/" then DIR = DIR .. "/" end
        end
    end
end

local PLAYER = "A"
do
    local f = io.open(EXE_DIR .. "Mods/CoopProbe/Scripts/playerid.txt", "r")
    if f then local p = f:read("*l"); f:close(); if p and #p > 0 then PLAYER = p end end
end

local function now() return os.date("%H:%M:%S") end

local logfile
local function log(fmt, ...)
    local ok, msg = pcall(string.format, fmt, ...)
    if not ok then msg = tostring(fmt) end
    local line = string.format("[%s] %s", now(), msg)
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

local function safe(label, fn, ...)
    local ok, res = pcall(fn, ...)
    if not ok then log("ERR %s: %s", label, tostring(res)); return nil end
    return res
end

local function fullname(obj)
    if obj == nil then return "<nil>" end
    return safe("GetFullName", function() return obj:GetFullName() end) or tostring(obj)
end

local function validActor(obj)
    if not obj then return false end
    return not fullname(obj):match("^UObject")
end

local function vecstr(v)
    if not v then return "nil" end
    return string.format("%.1f %.1f %.1f", v.X or 0, v.Y or 0, v.Z or 0)
end
local function rotstr(r)
    if not r then return "nil" end
    return string.format("%.1f %.1f %.1f", r.Pitch or 0, r.Yaw or 0, r.Roll or 0)
end

-- ---------------------------------------------------------------------------
-- discovery
-- ---------------------------------------------------------------------------
local function getWorld() return safe("GetWorld", function() return UEHelpers.GetWorld() end) end
local function getPC() return safe("GetPlayerController", function() return UEHelpers.GetPlayerController() end) end
local function getPawn(pc)
    pc = pc or getPC()
    if not pc then return nil end
    local p = safe("pc.Pawn", function() return pc.Pawn end) or safe("K2_GetPawn", function() return pc:K2_GetPawn() end)
    if validActor(p) then return p end
    return nil
end

function runConsole(text)
    local world, pc = getWorld(), getPC()
    if not (world or pc) then log("console: no world/pc"); return end
    local ksl = UEHelpers.GetKismetSystemLibrary()
    safe("ExecuteConsoleCommand", function() return ksl:ExecuteConsoleCommand(world or pc, text, pc) end)
    log("console [%s]", text)
end

local function playerChars()
    return safe("FindAllOf:BP_PlayerCharacter_C", function() return FindAllOf("BP_PlayerCharacter_C") end) or {}
end

local function reportInfo()
    local world, pc, pawn = getWorld(), getPC(), getPawn()
    log("world : %s", fullname(world))
    log("pc    : %s", fullname(pc))
    if pawn then
        log("pawn  : %s", fullname(pawn))
        log("loc   : %s  rot: %s",
            vecstr(safe("loc", function() return pawn:K2_GetActorLocation() end)),
            rotstr(safe("rot", function() return pawn:K2_GetActorRotation() end)))
    end
    log("mode  : %s", fullname(safe("mode", function() return UEHelpers.GetGameModeBase() end)))
    log("inst  : %s", fullname(safe("inst", function() return UEHelpers.GetGameInstance() end)))
end

-- ---------------------------------------------------------------------------
-- puppet
-- ---------------------------------------------------------------------------
local puppet = nil

function logPuppet()
    if not puppet then log("puppet: none"); return end
    log("puppet: %s loc=%s", fullname(puppet),
        vecstr(safe("puppet loc", function() return puppet:K2_GetActorLocation() end)))
end

local function summonPuppet()
    local baseline, n = {}, 0
    for _, o in pairs(playerChars()) do baseline[fullname(o)] = true; n = n + 1 end
    log("spawn: %d BP_PlayerCharacter_C before summon", n)
    if n == 0 then log("spawn: not in gameplay yet"); return false end
    runConsole("summon /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C")
    return true, baseline
end

local function adoptNewActor(baseline)
    for _, o in pairs(playerChars()) do
        if not baseline[fullname(o)] then return o end
    end
    return nil
end

local function drivePuppet(x, y, z)
    if not puppet then log("drive: no puppet"); return false, -1 end
    safe("puppet set loc", function()
        return puppet:K2_SetActorLocation({ X = x, Y = y, Z = z }, false, {}, true)
    end)
    local got = safe("puppet loc after", function() return puppet:K2_GetActorLocation() end)
    if not got then return false, -1 end
    local dx, dy, dz = got.X - x, got.Y - y, got.Z - z
    local dist = math.sqrt(dx * dx + dy * dy + dz * dz)
    log("drive: target %.1f %.1f %.1f -> actual %s (dist %.1f)", x, y, z, vecstr(got), dist)
    return dist < 5.0, dist
end

-- list actors near the player: the inventory enemy sync needs
local function nearActors(radius)
    radius = radius or 3000
    local pawn = getPawn()
    if not pawn then log("near: no pawn"); return end
    local me = safe("near me", function() return pawn:K2_GetActorLocation() end)
    if not me then log("near: no location"); return end
    local out = io.open(DIR .. "near.txt", "w")
    local n, kept = 0, 0
    local all = safe("near all actors", function() return FindAllOf("Actor") end)
    if all then
        for _, a in pairs(all) do
            n = n + 1
            local loc = safe("near loc", function() return a:K2_GetActorLocation() end)
            if loc then
                local dx, dy, dz = loc.X - me.X, loc.Y - me.Y, loc.Z - me.Z
                local d = math.sqrt(dx * dx + dy * dy + dz * dz)
                if d <= radius then
                    kept = kept + 1
                    if out then
                        out:write(string.format("%8.1f  %s\n", d, fullname(a)))
                    end
                end
            end
        end
    end
    if out then out:close() end
    log("near: %d of %d actors within %.0f units -> near.txt", kept, n, radius)
end
-- every Character (and every Pawn) in the world, with its class: enemy discovery
local function listCharacters()
    local out = io.open(DIR .. "chars.txt", "w")
    local function w(s) if out then out:write(s, "\n") end end
    for _, cls in ipairs({ "Character", "Pawn", "AIController" }) do
        local objs = safe("chars:" .. cls, function() return FindAllOf(cls) end) or {}
        w("### " .. cls)
        local n = 0
        for _, o in pairs(objs) do
            n = n + 1
            local c = safe("cls", function() return o:GetClass() end)
            w(string.format("  %-40s %s", c and fullname(c) or "?", fullname(o)))
        end
        w(string.format("  (%d)", n))
    end
    if out then out:close() end
    log("chars.txt written")
end
-- ---------------------------------------------------------------------------
-- self-timed automation
-- ---------------------------------------------------------------------------
local auto = nil
local function autoFail(why) log("auto: FAIL %s", why); writeFile("result.txt", "FAIL " .. why .. "\n"); auto = nil end
local function autoPass(why) log("auto: PASS %s", why); writeFile("result.txt", "PASS " .. why .. "\n"); auto = nil end
local function autoBegin() auto = { phase = "menu", entered = os.time(), noted = 0 }; writeFile("result.txt", "RUNNING\n"); log("auto: begin") end
local function phaseTime() return os.time() - auto.entered end
local function enter(phase) auto.phase = phase; auto.entered = os.time(); auto.noted = 0 end

local function autoTick()
    if not auto then return end
    local t = phaseTime()

    if auto.phase == "menu" then
        local w = nil
        local objs = safe("FindAllOf widget", function() return FindAllOf("WBP_MainMenu_C") end)
        if objs then for _, o in pairs(objs) do w = o; break end end
        if w and validActor(w) then
            log("auto: menu widget after %ds", t)
            local ok = pcall(function() return w:ContinueGame() end)
            log("auto: ContinueGame ok=%s", tostring(ok))
            enter("world")
        elseif t > 240 then autoFail("no main menu widget in 240s")
        elseif t - auto.noted >= 20 then auto.noted = t; log("auto: waiting for menu widget (%ds)", t) end

    elseif auto.phase == "world" then
        local pawn = getPawn()
        if pawn and fullname(pawn):find("BP_PlayerCharacter_C", 1, true) then
            if t >= 5 then
                log("auto: gameplay after %ds, pawn %s", t, fullname(pawn))
                reportInfo()
                enter("summon")
            end
        elseif t > 300 then autoFail("no gameplay pawn 300s after ContinueGame")
        elseif t - auto.noted >= 20 then auto.noted = t; log("auto: waiting for gameplay (%ds)", t) end

    elseif auto.phase == "summon" then
        local ok, baseline = summonPuppet()
        if ok then auto.baseline = baseline; enter("adopt")
        else autoFail("summon not attempted (no player character)") end

    elseif auto.phase == "adopt" then
        local a = adoptNewActor(auto.baseline or {})
        if a then
            puppet = a
            log("auto: puppet adopted after %ds: %s", t, fullname(a))
            enter("place")
        elseif t > 25 then autoFail("summon produced no BP_PlayerCharacter_C in 25s") end

    elseif auto.phase == "place" then
        local pawn = getPawn()
        local loc = pawn and safe("loc", function() return pawn:K2_GetActorLocation() end)
        if not loc then autoFail("lost the local pawn"); return end
        local tx, ty, tz = loc.X + 250, loc.Y, loc.Z
        log("auto: placing puppet at %.1f %.1f %.1f (player %s)", tx, ty, tz, vecstr(loc))
        drivePuppet(tx, ty, tz)
        auto.expect = { x = tx, y = ty, z = tz }
        enter("verify")

    elseif auto.phase == "verify" then
        local got = puppet and safe("puppet loc", function() return puppet:K2_GetActorLocation() end)
        local e = auto.expect
        if not got then autoFail("puppet has no location"); return end
        local dx, dy, dz = got.X - e.x, got.Y - e.y, got.Z - e.z
        local dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        if dist < 50 then
            autoPass(string.format("puppet at commanded spot (dist %.1f) %s", dist, fullname(puppet)))
            writeFile("puppet.txt", fullname(puppet) .. "\n")
        elseif t > 15 then
            autoFail(string.format("puppet drifted %.1f units from commanded spot", dist))
        end
    end
end

-- ---------------------------------------------------------------------------
-- inventories
-- ---------------------------------------------------------------------------
local TERMS = { "Lobby", "Beacon", "NetDriver", "PlayerState", "PlayerController", "GameMode",
    "GameState", "Sparta", "Pawn", "Character", "SaveGame", "AIController", "UserWidget" }

local function inventory(tag)
    local out = io.open(DIR .. "inventory_" .. tag .. ".txt", "w")
    local function w(s) if out then out:write(s, "\n") end end
    w("## inventory " .. tag .. " " .. now())
    for _, term in ipairs(TERMS) do
        local objs = safe("ia:" .. term, function() return FindAllOf(term) end)
        local n = 0
        w("### " .. term)
        if objs then
            for _, o in pairs(objs) do n = n + 1; if n <= 150 then w("  " .. fullname(o)) end end
        end
        w(string.format("  (%d)", n))
    end
    if out then out:close() end
    log("inventory_%s.txt written", tag)
end

local function findObjects(substr, limit)
    limit = limit or 300
    local out = io.open(DIR .. "find_" .. substr:gsub("[^%w]", "_") .. ".txt", "w")
    local n, hits = 0, 0
    local all = safe("all objects", function() return FindAllOf("Object") end)
    if all then
        for _, o in pairs(all) do
            n = n + 1
            local fn = safe("fn", function() return o:GetFullName() end)
            if fn and fn:find(substr, 1, true) then
                hits = hits + 1
                if out and hits <= limit then out:write(fn, "\n") end
            end
        end
    end
    if out then out:close() end
    log("find '%s': %d hits of %d objects", substr, hits, n)
end

-- ---------------------------------------------------------------------------
-- command channel
-- ---------------------------------------------------------------------------
local function consumeCommands()
    local f = io.open(DIR .. "cmd.txt", "r")
    if not f then return end
    local lines = {}
    for line in f:lines() do lines[#lines + 1] = line end
    f:close()
    os.remove(DIR .. "cmd.txt")
    for _, line in ipairs(lines) do
        local cmd, rest = line:match("^(%S+)%s*(.-)$")
        pcall(function()
            if cmd == "auto" then autoBegin()
            elseif cmd == "info" then reportInfo()
            elseif cmd == "inv" then inventory(tostring(os.time()))
            elseif cmd == "find" then findObjects(rest)
            elseif cmd == "near" then nearActors(tonumber(rest) or 3000)
            elseif cmd == "chars" then listCharacters()
            elseif cmd == "console" then runConsole(rest)
            elseif cmd == "spawn" then
                local ok, baseline = summonPuppet()
                if ok then auto = { phase = "adopt", entered = os.time(), baseline = baseline, noted = 0 } end
            elseif cmd == "puppet" then logPuppet()
            elseif cmd == "drive" then
                local x, y, z = rest:match("([%-%d%.]+)%s+([%-%d%.]+)%s+([%-%d%.]+)")
                if x then drivePuppet(tonumber(x), tonumber(y), tonumber(z)) end
            elseif cmd == "dump" then
                safe("DumpAllObjects", function() return DumpAllObjects() end)
                safe("DumpAllActors", function() return DumpAllActors() end)
                log("dumps written")
            elseif cmd == "widgetcall" then
                local cls, fn = rest:match("^(%S+)%s+(%S+)")
                if cls and fn then
                    local objs = safe("wa:" .. cls, function() return FindAllOf(cls) end)
                    local n = 0
                    if objs then
                        for _, o in pairs(objs) do
                            n = n + 1
                            if n == 1 then
                                local okc = pcall(function() return o[fn](o) end)
                                log("widgetcall %s:%s ok=%s", cls, fn, tostring(okc))
                            end
                        end
                    end
                    if n == 0 then log("widgetcall: no instances of %s", cls) end
                end
            elseif line ~= "" then log("unknown command: %s", line) end
        end)
        log("cmd handled: %s", line)
    end
end

-- ---------------------------------------------------------------------------
-- tick
-- ---------------------------------------------------------------------------
local ticks, lastWorld = 0, nil

local function tick()
    ticks = ticks + 1
    local world, pawn = getWorld(), getPawn()

    local wname = world and fullname(world) or "<none>"
    if wname ~= lastWorld then
        lastWorld = wname
        log("=== world now: %s ===", wname)
    end

    pcall(autoTick)

    if pawn then
        local loc = safe("loc", function() return pawn:K2_GetActorLocation() end)
        local rot = safe("rot", function() return pawn:K2_GetActorRotation() end)
        writeFile("state.txt", string.format("%.2f %.2f %.2f %.2f %.2f %d\n",
            loc and loc.X or 0, loc and loc.Y or 0, loc and loc.Z or 0,
            rot and (rot.Yaw or 0) or 0, rot and (rot.Pitch or 0) or 0, os.time()))
    elseif ticks % 20 == 0 then
        log("waiting: world=%s pawn=no", world and "yes" or "no")
    end
end

log("=== CoopProbe v4 start (player=%s dir=%s) ===", PLAYER, DIR)
LoopAsync(500, function()
    pcall(consumeCommands)
    pcall(tick)
    return false
end)
log("probe armed")
