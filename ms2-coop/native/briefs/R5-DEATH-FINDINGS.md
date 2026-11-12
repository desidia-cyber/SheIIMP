# R5-DEATH — findings from the research runs (living document, written as I go)

Everything here is a FACT with its evidence: a log line from `coop\probe.log` /
`ms2coop-native.log` of a run whose directory is named, or a line of a dump under
`G:\Freespace\shell_coop\research\`. Conventions: `[RUN n]` = lab run, `[DUMP]` =
`research\mine\*.tsv` / `research\raw\ObjectDump_*`, `[DOC]` = `docs\*.md`.

## 0. Where this started from (read, not measured)

- `[DUMP]` `USpartaHealthComponent` members: `AbilitySystemComponent @0x118`,
  `HealthSet @0x120`, `DeathState @0x128` (`ESpartaDeathState`), delegates
  `OnDeathStarted @0xF8`, `OnDeathFinished @0x108` (`mine\topic-05-body.props.tsv`,
  `SYNC-DATA-MINE.md` §7.2). `ESpartaDeathState`: `NotDead 0`, `DeathStarted 1`,
  `DeathFinished 2`.
- `[DUMP]` Getters on it: `GetHealth`, `GetHealthNormalized`, `GetCurrentHealthNormalized`,
  `GetShellHealth`, `GetShellHealthNormalized`, `GetMaxHealth`, `GetMaxShellHealth`,
  `GetDeathState`, `IsDeadOrDying`, and `StartDeath()` (Native, Public, BlueprintCallable).
- `[DUMP]` `USpartaCharacter.HealthComponent @0x6A0` — the reflection property
  `HealthComponent` on the pawn is the way in; no `FindAllOf` needed.
- `[DUMP]` `USpartaHealthSet` attributes are `FGameplayAttributeData` (16 bytes,
  BaseValue +0x08, CurrentValue +0x0C): `Health @0x60`, `MaxHealth @0x70`,
  `ShellHealth @0x80`, `MaxShellHealth @0x90` (`SYNC-DATA-MINE.md` §7.1).
- `[DUMP]` The candidate control points:
  - `BP_PlayerController_C.S_KillMe()` (0 params) — the game's own "kill me";
  - `BP_PlayerController_C.S_ReviveShell()` (0 params) — its own revive;
  - `BP_PlayerController_C.S_ResetHUDState()`, `ClearDeathMix()`, `ApplyDeathMix(EventData)`;
  - `BPFL_Player.ForceReviveShell(Silent, __WorldContext)`, `BlockShellRevive`,
    `UnblockShellRevive` (a blueprint function library: STATIC);
  - `USpartaWorldGenSubsystem.BeginWorldReset()` (Native, Event, Public,
    BlueprintCallable, BlueprintEvent) — "the death-driven world reset";
  - `BPFL_SaveLoad.GetRespawnPlayerStart(__WorldContext) -> (Success, ReturnValue Name,
    PlayerStartName Name)` and `SetRespawnPlayerStart(Value Name, Save bool, …)`;
  - `BPFL_SaveLoad.GetProloguePlayerStart`, `GetHUBPlayerStart`, `GetAllHUBPlayerStarts`;
  - GameMode: `BP_SpartaGameMode_C.ChoosePlayerStart(Player)`,
    `GetCachedSpartaPlayerStarts()`, `PlayerStarts`, and Engine's
    `GameModeBase.RestartPlayerAtPlayerStart(NewPlayer, StartSpot)` / `RestartPlayer`.
  - `BPSO_PlayerSession_C.StartTransform @0xF0` + `GetSessionStartTransform()` — the
    respawn transform; `BPSO_PlayerSaveObject_C.CurrentSpawnLocation @0x8A0` — the
    spawn *id*.
- `[DOC]` `docs\HANDOFF-REMAINING-SYNC.md` G9 and `SYNC-DATA-MINE.md` §7/§8/§17 claim
  the flow `SpartaHealthComponent::StartDeath()` → `GA_Death_Player_C:HandlePlayerDeath`
  → `WBP_DeathScreen_C` (`OnDeathSelection`, `ResetWorld`, `CreateLoadingScreen`) →
  `USpartaWorldGenSubsystem.BeginWorldReset()`.  That is a READING OF NAMES, not a
  trace; run 1 below is the trace.

## 1. The engine-layer instruments this round added (so the facts are reproducible)
- `ue::ProbeOn(seconds, "a|b|c")` / `ue::ProbeOff()` / `ue::ProbeActive()` — logs every
  ProcessEvent whose FUNCTION name contains one of the substrings, with the time since
  the probe was armed and the object it ran on (`PROBE +12.34s  <fn>  on <object>`).
  Name-cached by FName, so the FName→string conversion happens once per distinct
  function; per-name cap 60; lines are written OUTSIDE the detour (frame boundary).
  Driven from `cmd.txt`: `deathprobe <seconds> [<patterns>]`.
- `ue::HookPreSkip(spec, fn)` — a pre-hook that can VETO the original UFunction call.
  Needed because the reset is authored in blueprint and ProcessEvent is the only lever
  that does not patch engine code.  A skip hook that faults never blocks the call.

## 2. THE DEATH SEQUENCE, MEASURED (lab run 1, `local\lab\20261025-094547-death-run1`)

Instruments: `deathprobe 150 death|reset|respawn|revive|load|travel|checkpoint|restart|worldreset|deathscreen`
armed, then `die` (which calls `BP_PlayerController_C.S_KillMe()`; `ok=true`).

Time is seconds after the probe was armed; `die` was issued at +3.5 s.

| t | UFunction | on |
|---|---|---|
| +3.53 | `OnPlayerDeath` | `BP_UIAudioHandler_C` (GameInstance subobject) |
| +3.53 | `ApplyDeathMix` | `BP_PlayerController_C` |
| +3.53 | `OnDeathOrBossDefeat` | `BPC_QuestLogComponent_C` |
| +3.66 | `ExecuteUbergraph_GA_Death_Player` | `GA_Death_Player_C` (on the pawn) |
| +4.50 | `EndDeath` | `GA_Death_Player_C` |
| +11.69 | `CreateLoadingScreen` | `WBP_DeathScreen_C` (GameInstance subobject) |
| +12.67 | `OnLoadingScreenCompleted` | `WBP_DeathScreen_C` |
| +12.67 | `HandleResetReady` | `BP_WorldGenSubsystem_C` |
| +14.17 | `SaveLoaded`, `SessionReset` | `SpartaDataLayerSubsystem` |
| +14.20 | `ReceiveOnLoaded` | `BPSO_PlayerSession_C` |
| +14.22 | `ClientRestart` | `BP_PlayerController_C` (a NEW instance) |
| +17.45… | `SaveLoaded` storm | every `SpartaSaveComponent` in the level |

FACTS that fall out of it, and each one changes the implementation:

1. **The reset is IN PLACE, not a level travel.** The level stays
   `L_Core_World_01`; the save/data layers are reloaded (`SessionReset`) and the
   controller is restarted (`ClientRestart` on a new instance). There is no
   `OpenLevel`, no `ServerTravel`, no `LoadMap` in the trace.
2. **`WBP_DeathScreen_C.ResetWorld` is NOT on this path.** The widget calls
   `CreateLoadingScreen`, then `OnLoadingScreenCompleted`, then the reset goes
   through `BP_WorldGenSubsystem_C.HandleResetReady`. (`ResetWorld` exists as a
   UFunction -- `funcflags.tsv` -- it is just not what runs here.)
3. **`USpartaWorldGenSubsystem.BeginWorldReset()` never appears as a ProcessEvent
   call.** It is a native Event: either C++ calls it directly (a ProcessEvent veto
   cannot see it) or `HandleResetReady` on the BP subclass is the game's real entry
   point. This is the single most important correction to `docs\HANDOFF-REMAINING-SYNC.md`
   G9, which lists `BeginWorldReset` as the reset.
4. **`ESpartaDeathState` on the PLAYER does not move.** `dstate` printed
   `deathState=0 readable=1` before the death, throughout it, and after the reset
   (run 4 confirmed it twice). The enum `NotDead/DeathStarted/DeathFinished` is real
   and readable (it is what the AI characters use) but it is NOT the player's death
   signal -- run 4's first build trusted it and detected nothing.
5. **The player's death flag is `BP_PlayerController_C.bDeathMixActive`** (bool,
   `0x9A0`, `topic-18-player-controller.props.tsv`), set by `ApplyDeathMix` and
   cleared by `ClearDeathMix`. That pair is the dead/alive state the module now
   reads (one byte) and hooks.
6. **The death penalty is Gloom, and it is taken at the death, not at the reset.**
   Run 4, first death: `bHasLostGloom=1 LostGloomAmount=4021`; second death
   `0 / 0`. `BP_SpartaPlayerSaveObject_C` carries `LostGloomAmount @0x578`,
   `LostGloomLocation @0x580`, `LostGloomBiome @0x598`, `bLostGloomInsideObjective
   @0x561`, and the spoils are a `BP_DeathSpoils_C` actor with `GiveGloom`
   (`topic-08-economy.props.tsv`, `funcflags.tsv`). So `pay_death_penalty=1` needs
   nothing from the mod; see §6 for what `=0` can and cannot do.
7. **Timing budget**: the death event is at t, the reset at t+9 s. Nine seconds is
   the window a suppression has to work in, which is comfortable for a 15 Hz tick
   and hopeless for a "block the reload after the loading screen appears" design.

## 2b. What a hook can and cannot reach (measured, run 4)

- Name-based `ue::HookPreSkip("BP_WorldGenSubsystem_C.HandleResetReady")` resolved
  for **1 of 4** targets only (`death: 1 reset-veto hook(s) installed of 4`):
  `FindClass("WBP_DeathScreen_C")` logs `index miss -- make=0:0, full scan found
  nothing` even while a `WBP_DeathScreen_C` instance exists, and the BP subclass
  names do not resolve either.
- The fix that works: anchor on a class that DOES resolve (the NATIVE base, e.g.
  `SpartaWorldGenSubsystem`, `SpartaDataLayerSubsystem`), find the instance with
  `FindAllOf`, ask the INSTANCE for the function (`ue::FindFunction`), and register
  the veto on that UFunction (`ue::HookPreSkipFn`). Run 5 uses this.
- All hook resolution must be RETRIED after the level loads: at `coop::Start()` the
  game is in the menu with ~6 700 objects and 5 indexed classes, so every spec fails
  (runs 2/3 detected nothing at all because of this). The module now retries while
  it is in gameplay with a valid pawn, every 3 s, until every target is in. The first
  successful resolution costs ~250 ms (three `FindAllOf` walks) -- a one-off hitch at
  the start of gameplay, not a per-frame cost.

## 2c. A crash that was NOT this module (run 2 and run 3, analysed)

Runs 2 and 3 lost their client ~10 s after `phase -> sync` with exception
`0xc0000374` (`STATUS_HEAP_CORRUPTION`). The dump
(`%LOCALAPPDATA%\CrashDumps\MortalShell2-Win64-Shipping.exe.51228.dmp`) analyzed with
cdb gives:

```
FAILURE_BUCKET_ID: HEAP_CORRUPTION_ACTIONABLE_BlockNotBusy_DOUBLE_FREE_c0000374_ntdll.dll!RtlpFreeHeapInternal
STACK: ntdll!RtlFreeHeap <- steam_api64!SteamInternal_FindOrCreateUserInterface+0x19b570
       <- steam_api64!SteamAPI_ISteamClient_GetIPCCallCount+0x76d0
       <- MortalShell2_Win64_Shipping ...
```

A DOUBLE FREE inside `steam_api64.dll`, no `dxgi.dll` frame anywhere in the stack.
It is the game's (or Steam's) own heap bug and it is flaky: run 1, 4 and 5 ran the same
build through the same transition and survived, and `%LOCALAPPDATA%\CrashDumps` holds
the same signature from before this round (22.10 ×4, 25.10 02:21/02:54). Recorded
because "the client disappeared" is exactly the kind of thing that must not be
attributed to the wrong module later.

## 2e. THE RESPAWN LADDER, MEASURED (lab runs 6 and 7, solo override armed)

`coop\SOLO_DEATH.txt` makes a solo death case 2 (the harness override; a two-client
session ignores it).  With it armed, `die` on a single client:

| run 6 (`local\lab\20261025-105952-death-run6`) | result |
|---|---|
| death detected from `ApplyDeathMix` | `COOP DEATH #1 victimCase=2 partyCase=2 peer=solo` |
| stack | `stack 0 -> 1 (death; factor 0.900)` |
| debuff write | `MaxHealth 75.0 -> 67.5 ... ok=true`, verified `GetMaxHealth=67.50 wrote 67.50` |
| the world | `worldresets:1` for the whole run -- **the reset never happened** |
| death screen | `death screen dismissed (suppressed death) ok=true` |
| `S_ReviveShell()` | `ok=true` (nothing observable changed) |
| `GameMode.RestartPlayerAtPlayerStart(NewPlayer,StartSpot)` | `ok=true`, `start=PlayerStart_UAID_...` `d=102` |
| hand respawn | teleport ok, `health -> max (67.5)` ok |
| **stuck** | `deathmix=1` for the rest of the run -> the player stayed "down" |

THE ONE THING RUN 6 TAUGHT: **nothing clears `bDeathMixActive` on a suppressed death.**
The game clears it on its own reset path (`ClientRestart` on the new controller, §2), so
the mod has to clear it itself, with the game's own `ClearDeathMix()`.  That is step 4
of the ladder.

| run 7 (`local\lab\20261025-110912-death-run7`) | result |
|---|---|
| final status | `death=me:1(alive) case:2 victim:2 serial:1 stacks:1 ... suppressed:1 worldresets:1` |
| | `deathstate=0 deathmix=0 maxhp=67.5 maxshell=0.0 ... missrevive:0 hooks:3` |
| so | the player is BACK, in the SAME world (`worldresets:1`, i.e. only the initial load), with one stack and 90 % max health |

Two further facts from run 7's log:

- **Dismissing the death screen is what actually stops the reset in this game.** With
  the widget removed there is no `CreateLoadingScreen`, hence no
  `OnLoadingScreenCompleted`, hence no `HandleResetReady`: the chain dies at its first
  link.  The UFunction vetoes (`HandleResetReady`, `BeginWorldReset`, `ResetWorld`)
  installed fine (3 of 4; `SpartaDataLayerSubsystem.SessionReset` is the one that did
  not, because its class name does not resolve) and `suppressed:1` says one of them did
  fire -- but the dismissal runs FIRST and is the load-bearing part.  Consequence for
  the policy: the dismissal is only allowed when the policy says suppress, so a case-1
  host keeps its widget and therefore its reset.
- The nearest respawn point works: `PlayerStart_UAID_D4E98A7EF567430A02_...` at
  `d=102` from the death spot, found with `FindAllOf("SpartaPlayerStart")` and picked
  by distance.  Nothing else is needed to answer "the nearest respawn point on the same
  level".

## 2f. Lock incident (recorded because it affected the timetable, not the code)
The shared game lock was taken by the `body` agent at 08:55:16 and never released.
Their last lab run ended at 09:08:55 (`local\lab\20261025-090556-body-probe2`, whose
logs stop there), no `MortalShell2-Win64-Shipping` process existed at 09:40:56, and
nothing of theirs had written for 31 minutes -- against the brief's "hold it at most
~20 min".  The lock directory was therefore removed at 09:40:56 and re-acquired
normally.  `world` took the lock 40 s later, so this agent waited for it in turn.
Nothing was deployed or killed outside the lock; `E:\Games\Mortal Shell II` was never
touched.

## 3. Two clients: the liveness word works (lab run `20261025-114029-death-two`)

The first two-client run looked broken ("the peer is always down") and the cause was
NOT the wire: the second client was running ANOTHER agent's `dxgi.dll` (its log wrote
that module's `result_bh.txt`), so `lv` never reached my parser.  With both clients on
this build (`DLL under test: 27df0169855d` in both), the diagnostics show the key
working exactly as specified:

```
[B] death: the peer's liveness is now readable (lv=1 -> 1, case 0)
[B] death: publishing lv=1   (liveness 1, case 0, serial 0)
[B] death: publishing lv=290 (liveness 2, case 2, serial 1)     <- B's death: 2 | 2<<4 | 1<<8
[B] death: publishing lv=289 (liveness 1, case 2, serial 1)     <- B back up, case kept
[A] death: peer lv 1(case 0) -> 2(case 2) serial 1
```

`run-two-death.ps1` now REFUSES to run unless both instances carry the hash of this
copy's build, because a poisoned run reads as a code fault.

## 4. DCASE2, twice, on two clients (PASS)

```
=== DCASE2 baseline: B maxhp=75 worldresets A=1 B=1
=== DCASE2 case: PASS the death was decided as case 2
=== DCASE2: B me:1(alive) case:2 victim:2 serial:1 stacks:1 peer:1(seen) boss:0 pending:0
           suppressed:0 worldresets:1 ... maxhp 75 -> 67.5 (ratio 0.9) worldresets A 1->1 B 1->1
=== DCASE2: PASS B respawned in the live world, no reset, stacks=1, max health ~90%
```

The mechanism in the log: death event -> case 2 -> stack 1 -> `MaxHealth 75.0 -> 67.5`
(verified against `GetMaxHealth`) -> death screen dismissed -> the reset chain stopped
(`HandleResetReady BLOCKED` when it tried anyway) -> 10 s countdown -> `S_ReviveShell`
-> `RestartPlayerAtPlayerStart` at the nearest `PlayerStart` (d=102) -> `ClearDeathMix` ->
input restored -> alive, same world.  Nothing reloaded on either machine.

## 5. DHOLD: the hold and its release work, the CAMERA does not (partial)

```
--- B(hold) death: me:3(down) case:3 victim:3 serial:1 stacks:0 peer:1(seen) boss:1 pending:1
=== DHOLD: PASS B is held (case 3, pending)
=== DHOLD: FAIL viewpuppet=0
=== DHOLD release: PASS B respawned once the fight was over   (recase 3 -> 2, stack 1)
```

So the three-case core of it is right: a death with `bossfake 1` is HELD, adds no
stack, and when the boss flag goes away the death is re-decided to case 2, charges the
stack the hold put off, and respawns.  What does not work is the spectate CAMERA, and
the reason is measured, not guessed:

- `ue::FindFunction(pc, "SetViewTarget")` returns NOTHING on this build's
  `BP_PlayerController_C` (log: "this controller has no SetViewTarget").
- `ClientSetViewTarget` DOES resolve and the call returns true -- and nothing happens,
  because it is a Net RPC and this game has no NetDriver, so a `Client*` RPC is dropped.
- `APlayerController.ViewTarget` is not a reflected property here
  (`PropOffset(pc,"ViewTarget") < 0`), so the struct cannot be written by name either.

What is left is to find the camera manager's own setter (`PlayerCameraManager`,
`UpdateViewTarget`, or the C++ offset of `APlayerController::ViewTarget` -- which the
dump does not carry).  NOT DONE, and the verdict says so (`viewpuppet=0`) rather than
being counted as a pass.

## 6. The death penalty (Gloom), measured

`bHasLostGloom=1 LostGloomAmount=4021` on every first death of a session, in every run.
So `pay_death_penalty=1` needs nothing from the mod (the game's loss stands, the
DeathSpoils actor is its recovery), and the mod logs the numbers as evidence.
`pay_death_penalty=0` (a refund) is NOT implemented: the spendable balance's owner is
not identified, and the module says so once, in the log, instead of pretending.

## 7. The test verb `die` is one-shot per session (and that shaped the verdict runs)

`BP_PlayerController_C.S_KillMe()` returns true and produces a death -- once per
session.  From the second call on, nothing happens: measured on all four clients in
this round.  The fallback this module then tries, `HealthComponent.StartDeath()`
(the enemy path), also produces no death for the player (the call is made and logged,
and no `ApplyDeathMix` follows).  Consequences:

- each verdict that needs a death runs in its OWN session (`run-two-death.ps1 -Phase
  case2|hold|wipe`); the brief allows separate runs for exactly this reason;
- a real player death is unaffected: the game's own damage path calls
  `StartDeath` from C++ with its own prerequisites met, which is what the harness
  cannot reproduce.

## 8. Cost

`COST death` (the 15 Hz tick) in steady state, from run 7's log: **avg 0.07-0.10 ms per
call, p95 <= 0.06-0.13 ms**, i.e. ~1.1 ms per SECOND, or **0.001 ms per frame** at
60 fps -- three orders of magnitude inside the 0.15 ms budget.  `deathStack` (2 Hz) is
0.03 ms per call.  The one-off maxima (12-72 ms) are the class scans while the hooks
resolve (once, at the start of gameplay) and the death event itself; after they are
installed `MaintainHooks` returns on a single integer compare.

## 9. DWIPE, two clients (PASS)

```
=== DWIPE baseline: worldresets A=1 B=1 stacks B=0
--- A(wipe) death: me:4(down) case:1 victim:2 serial:1 ... peer:4(seen) pending:1 host:1
--- B(wipe) death: me:4(down) case:1 victim:2 serial:1 ... peer:2(seen) pending:1 host:0
=== DWIPE case: PASS a party wipe was seen
--- A(after wipe) death: me:1(alive) case:0 ... worldresets:2 wr:1 host:1
--- B(after wipe) death: me:1(alive) case:1 ... worldresets:1 wr:0 peerwr:1 host:0
=== DWIPE: host reset A worldresets 1->2 ; guest did NOT reset its own (B 1->1)
=== DWIPE: both alive=True stacks A=0 B=0
=== DWIPE: PASS host reset, guest followed, both alive, 0 stacks
```

Both clients died inside the same second, each computed case 1 (its own death + the
partner down), the HOST ran the game's own reset, the GUEST blocked its own (its
`worldresets` stayed at 1 and it recorded the host's `wr:1`), and the guest was put
back on its feet once the host was up.  Stacks 0 on both: the wipe cleared them, as the
rule says.  Note `victim:2` on both: each machine had already decided case 2 for itself
before the partner's death reached it, and the 4 Hz re-decision turned both into the
wipe (`COOP DEATH recase 2 -> 1`) -- which is exactly the transition a compute-once
design never makes.

## 10. What this round did NOT achieve

1. The DHOLD camera (see ��5): the hold and its release are correct, the view target is
   not moved.  Needs the camera manager's setter or the C++ offset of
   `APlayerController::ViewTarget`, neither of which this build exposes by name.
2. `pay_death_penalty=0` (the refund): not implemented, said so in the log (��6).
3. The rest-clears-stacks rule is APPROXIMATE: `BPFL_Player_C.RestorePlayerHealth` /
   `RestoreResolve` (the checkpoint restore pair) plus a 600 uu proximity test to a
   `SpartaPlayerStart`.  It never fired in the verdict runs (nobody rested), so it is
   unverified in a session.
4. The `die` test verb is one-shot per session (��7), which is why each verdict ran in
   its own session.

## 11. The existing baseline, run unchanged with this build in (last run of the round)

`run-two.ps1` was run unmodified against both instances carrying this build
(`DLL under test: 64e1679f54aa`) and printed:

```
=== SHELL: PASS        === WEAPON: PASS      === LOOK: PASS     === ANIM: PASS
=== DAMAGE: PASS       === DEATH: PASS       === PROGRESS: PASS === QUEST: PASS
=== QUEST2: PASS       === LIFT: PASS        === CHEST: PASS    === SPAWN: PASS
=== MIRROR: PASS       === PEERHIT: PASS     === AITARGET: PASS === STREAM: PASS
=== NPCFAR: PASS       === LEDGER: A PASS    === A: PASS         === B: PASS
=== WORLD: (no verdict line)
RESULT: FAIL
```

The ONLY condition that failed is WORLD: `result_ws.txt` was never written because the
world-state test found no LIVE key to act on (`=== WORLD: A seen:109 true:13 peer:13
live:0 base:0 fail:0` -- 109 keys seen, 13 true, none live in that session; the log
shows "ws judge: waiting for a live world key (door=0 field=0 pickup=0)").  That is the
test's own precondition, it is not touched by this round (the death module writes only
its own keys and never calls the world module), and the same "waiting for a live world
key" line appears in the `body` agent's runs from earlier today.
