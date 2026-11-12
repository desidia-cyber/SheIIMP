# Mortal Shell II — notes

Game slug: `mortal-shell-2`. Sessions: 2026-10-22.

Owner state (paths, saves, accounts) lives in `local/` (gitignored), never here.

## Phase 0 — Triage (2026-10-22)

### Identity (rule 5)

| Field | Value | Source |
|---|---|---|
| Title | Mortal Shell II (2026) | Wikipedia |
| Developer / publisher | Cold Symmetry / Playstack | Wikipedia |
| Store | **not Steam-installed on this PC** — Inno Setup install (`unins000.exe`), no `steam_api64.dll` next to the exe, no `appmanifest_*` for it, Steam library index has no Mortal entry | dir listing, `unins000.dat` ("Inno Setup Uninstall Log", "Mortal Shell II", "mortal-shell-2"), Steam `libraryfolders.vdf` |
| Install path | `<game dir>` (owner's PC; see `local/`) | dir listing |
| Main exe | `<game dir>\MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe`, 194 378 752 bytes | dir listing |
| Main exe SHA-256 | `BDEDDB33D61E876E94B6C30C8EA198AECFDEFFB557D6F5678C92D1C4C17AAE69` | Get-FileHash, 2026-10-22 |
| Build string | `++Sparta-Depot+Day1+CL92086-CL-0` (FileVersion + ProductVersion) | exe version info |
| Engine | **Unreal Engine 5.6** | UE4SS official custom game config `MortalShell2/UE4SS-settings.ini` sets `[EngineVersionOverride] MajorVersion=5 MinorVersion=6` (independent corroboration: `/Script/MovieSceneCore` + `/Script/MovieSceneCoreTypes` modules, which exist from 5.4 onward; `/Script/IrisCore`, `/Script/StateStream`, `/Script/DataflowCore`, `/Script/AudioLinkCore` in the exe) |
| Packaging | IoStore: `pakchunk*.utoc`/`.ucas` + `pakchunk*.pak` under `MortalShell2\Content\Paks` (16 chunks, largest `pakchunk2-Windows.ucas` 38.8 GB) | dir listing |
| Install size | 65.93 GB, 213 files | `Get-ChildItem -Recurse` |
| Module name | `Sparta` (`/Script/Sparta.SpartaGameUserSettings` in `GameUserSettings.ini`) | config file |
| Release | 2026-08-20 | Wikipedia |
| Steam categories | single-player only; devs' position: no multiplayer/co-op "planned" (on their radar) | Steam community thread 581679396200410540, game8 |

### Answers to the triage questions

1. **Which exact game?** Mortal Shell II, PC, build `CL92086`, UE 5.6, Inno-Setup copy (not a
   Steam manifest install).
2. **Already done?** **No.** No `DIRECTORY.md` entry for Mortal Shell of any kind (searched
   `Mortal`, `Cold Symmetry`) and no `games/mortal-shell*` folder. Web: no co-op/multiplayer
   mod for MS2; Nexus MS2 category has UE4SS, cheat menus, ReShade, cosmetics.
3. **Engine?** UE 5.6 (above). UE4SS has an official per-game config for MS2.
4. **Source available?** No scripts or source shipped. Path is a mod loader: UE4SS v3.0.1
   (Lua/C++), console via mod, class/property access. Community proves this works: Nexus
   "UE4SS for MS2" (mod 45) and "UE4SS Cheat Menu" (mod 20), YouTube install guides.
5. **Engine multiplayer?** The engine's replication stack is compiled in — every UE build has
   it. Beyond boilerplate, this build contains **UE's `LobbyBeacon` multiplayer scaffolding**
   as real classes: `ALobbyBeaconState`, `ALobbyBeaconHost`, `ALobbyBeaconClient`,
   `ALobbyBeaconPlayerState`, `ELobbyBeaconJoinState`, `bInLobby`, `bLobbyStarted`,
   `ServerDisconnectFromLobby` (UTF-16 string scan of the exe). Also present:
   `OnlineSubsystem`, `OnlineSubsystemUtils`, `OnlineSubsystemSteam`, `/Script/OnlineSubsystemSteam`,
   `/Script/Lobby`, `NetDriver*`, `bReplicates`, `ServerRPCBufferInput`, `NetCore`, `IrisCore`,
   `PacketHandler`, and Steam lobby async tasks (`FOnlineAsyncTaskSteamCreateLobby`, …).
   **`unknown`:** whether any of it is *used* by game code — the game ships single-player only
   and no `steam_api64.dll` in this copy. Treat the online stack as dormant until a dump says
   otherwise.
6. **Shipping check.** No anti-cheat, no online service to stay away from. Mod ships as a
   separate package (UE4SS + Lua + relay), no game files. Legal: players own the game.

### Difficulty read (playbook `how-coop-mods-work.md` §4)

Unreal Engine 5 with a proven mod loader and engine-level replication compiled in, but no
multiplayer game logic and (probably) no working native netcode path for the SP game. That is
the "engine with MP + mod loader" row: **host-authoritative state sync over our own transport**,
built as an in-process UE4SS Lua mod + a relay helper next to the game (the Poppy Playtime /
[games/poppy-playtime](../poppy-playtime/NOTES.md) precedent, same engine family).

## Phase 1 — Research (2026-10-22)

### Observations (each with a source)

1. No co-op mod exists for MS2 (web search 2026-10-22; Steam thread: "no multiplayer or coop
   features in development or planned"; game8: "will not have multiplayer co-op").
2. MS2 is moddable today through UE4SS: Nexus mod 45 ("UE4SS for MS2", UE4SS v3.0.1 Beta,
   updated 2026-09-16) and Nexus mod 20 (UE4SS Cheat Menu, F6, parry/harden toggles, shell swap).
3. UE4SS upstream supports UE 4.7–5.8; 5.6 support landed via issue #977. The
   `zCustomGameConfigs.zip` from `experimental-latest` **contains a `MortalShell2` folder**
   with a `UE4SS-settings.ini` (engine override 5.6, `bUseUObjectArrayCache=false`,
   `HookProcessInternal=1`) and `UE4SS_Signatures/StaticConstructObject.lua`
   (`4C 8B DC 55 53 41 56 49 8D AB 28 FE FF FF 48 81 EC C0 02 00 00 48 8B`) — i.e. the
   StaticConstructObject AOB is game-customised, which is the usual "this game fought UE4SS"
   fix.
4. The Poppy Playtime attempt (same playbook, UE4.27) established the reusable pieces we plan
   to copy: `summon` instead of `SpawnActor` (upstream #527), `ExecuteInGameThread` for every
   UObject mutation, puppet scale must be copied from the player mesh, relay = Python+ctypes
   with `--transport udp|steam`, Lua↔relay contract = files in a net dir.

### Derived

- Two-player co-op with player puppets + host-authoritative enemy/world sync is in scope;
  full quest sync is a later stage.
- The fastest first proof is: UE4SS running in the game, console available, object dump, then
  a puppet driven from a local loop, then a peer file, then the relay.

### Guesses (unverified)

- `LobbyBeacon*`/`OnlineSubsystemSteam` presence is boilerplate from a shared engine build,
  not MS2 gameplay netcode. Check the object dump for `*Lobby*`/`*Beacon*` actors and for
  `NetDriver` config in the shipped `DefaultEngine.ini` before believing otherwise.
- The Inno-Setup copy is a retail (non-Steam) distribution; the friend will need the same
  build `CL92086` for byte-equal behaviour.

### Unknowns

- Engine minor version from the game's own files (only UE4SS's config says 5.6 — confirm from
  the first UE4SS log line, which prints the detected engine version).
- Whether the game's GameMode refuses a second local player, and whether `open <map>?listen`
  works in this shipping build.
- Whether the pak is AES-encrypted (affects FModel/asset extraction; not needed for the mod).
- Save format: `WorldState_0.sav`, `MetaSaveGame.sav`, `SpartaGameSettings.sav` under
  `%LOCALAPPDATA%\MortalShell2\Saved\SaveGames` — whether co-op must share or ignore them.

## Phase 3 — Understand (2026-10-22)

### Observation — UE4SS hooks this build, engine 5.6 confirmed from the log

Test copy launch (`<test dir>`, UE4SS `experimental-latest` + the MS2 custom config) logs:

```
UE4SS - v3.0.1 Beta #0 - Git SHA #83330635
[PS] Found GUObjectArray / GMalloc / FName::ToString / ConsoleManagerSingleton / GameEngineTick
StaticConstructObject_Internal address: 0x7ff7184040b0 <- Lua Script
Using engine version: 5.6
```

So the engine is **UE 5.6** by UE4SS's own detection, independent of the config override.
Note it says "No specific game configuration found, using default configuration file" even
with the MS2 config folder copied in — the override file still applied. The `dwmapi.dll`
proxy is the load path; `UE4SS.log` lands next to the exe.

### Observation — the launch recipe (three things that cost hours)

1. **Launch the game's own bootstrap exe, not the Shipping exe** — and let it pass the
   project name: `<game dir>\MortalShell2.exe` (a `BootstrapPackagedGame`) spawns
   `"<...>\MortalShell2-Win64-Shipping.exe" MortalShell2` with the game dir as CWD.
   Launching the Shipping exe without that argument exits with code 1 and no log.
2. **An incomplete copy fails with a misleading error.** While the 66 GB robocopy was still
   running, every launch showed a modal
   `Failed to open descriptor file ../../../MortalShell2/MortalShell2.uproject` and the game
   never started. Once `robocopy` finished (exit 1 = "files copied"), the same folder
   launched normally. Triage rule: verify the copy is *finished* before blaming the mod.
   There is no `.uproject` in either install — packaged builds don't need one.
3. **Windowed, quiet, background.** `MortalShell2-Win64-Shipping.exe MortalShell2 -windowed
   -ResX=1024 -ResY=576 -nosound` works and keeps the machine usable; saves live in
   `%LOCALAPPDATA%\MortalShell2\Saved\SaveGames`, shared by every copy, so the test driver
   snapshots and restores them around each run.

### Observation — object dump: 63.6 MB, and what it says

`DumpAllObjects()` + `DumpAllActors()` are plain Lua globals (no keypress): written straight
from our probe mod at the main menu. Facts from the dump:

- **Menu classes**: `BP_PlayerController_MainMenu_C`, `BP_MainMenuCharacter_C`,
  `BP_GameMode_MainMenu_C`, `BP_SpartaGameInstance_C`, world
  `/Game/Sparta/Maps/MAPS/Menu/L_MainMenu`.
- **Gameplay classes**: `BP_PlayerCharacter_C`, `BP_PlayerController_C`,
  `BP_PlayerCameraManager_C` under `/Game/Sparta/Core/Player/`, plus blueprint function
  libraries `BPFL_Player`, `BPFL_Player_Equipment`, `BPFL_SaveLoad`, `BPFL_ShellMemory`.
- **C++ game module**: **508 classes** under `/Script/Sparta`, including a full Gameplay
  Ability System layer (`AbilityTask_*`, `BTDecorator_*`, `EnemyAttributeSet`,
  `DarkPowerAttributeSet`, `ArmorComponent`, `AIDirectorSubsystem`) and the save/world
  layer: `SpartaSaveSubSystem` (`GetCurrentWorldStateSave`,
  `CurrentOnDiskWorldStateSave`), `SpartaWorldGenSubsystem` (`CurrentWorldState`),
  `SpartaLevelScript:WorldStateTags`.
- **Save-object layer**: dozens of `BPSO_*` classes (Blueprint Save Object) under
  `/Game/Sparta/Core/SaveObjects/` — `BPSO_PlayerSession_C`, `BPSO_QuestLogComponent_C`,
  per-entity objects (doors, gates, bosses, chests, NPCs). This is the "one flag store"
  the co-op story sync should replicate.
- **Save/load API**: `BPFL_SaveLoad_C` has `SaveGame`, `SaveSession`, `ResetSession`,
  `IsSaveLoaded`, `ShouldResumeSession`, `GetPlayerSaveObjectFromCurrentSave`,
  `GetRespawnPlayerStart`, `SetGatesRevealed`, `SetNewGamePlusLevel`, …
- **Menu entry point**: `WBP_MainMenu_C:ContinueGame` / `:LoadGame`.
- **Netcode: none of the game's own.** Every replication hit in the dump is engine
  boilerplate (`/Script/Engine.Actor:bReplicates`, `IrisCore.*ReplicationBridge`,
  `OnlineSubsystemUtils.PartyBeacon*`, `GameInstance:OnlineSession`), which any UE 5.6
  build carries. `LobbyBeacon*`/`OnlineSubsystemSteam` in the exe are likewise compiled-in
  plugin code, not game logic. The earlier "Lobby" lead is closed: **no game netcode**.

### Derived

- Co-op is a retrofit: puppets + host-authoritative sync over our own transport
  ([how-coop-mods-work.md](../../playbook/how-coop-mods-work.md) stage 2 onward). GAS being
  present helps: attributes/abilities already have replication semantics in the engine,
  even if the game never turns them on.
- World/quest sync should target `SpartaSaveSubSystem` + `BPSO_*` objects rather than
  individual actors: they are the game's own persistence boundary, which is exactly what
  the playbook's "one flag store" rule asks for.
- Automation path is clear: probe mod reads `cmd.txt` (file channel) and calls UFunctions
  directly (`ContinueGame`), so no keypresses are needed to enter the game.

### Unknowns

- Whether `WBP_MainMenu_C:ContinueGame` can be invoked from Lua without the widget being
  alive/pumped (first attempt found 0 widgets — `FindAllOf("UserWidget")` returned nothing).
- Whether `UWorld::SpawnActor` is usable on UE5.6 with this UE4SS build (the Poppy
  precedent needed `summon` instead; upstream #527).
- Whether two copies of the game can run side by side on this PC (single instance, GPU
  memory, emulator locks) — untested since the copy became complete.

## Phase 2 — Workspace (2026-10-22)

- Owner's install untouched. Test copy at `E:\Games\_MS2-coop-test` (robocopy /E /MT:16,
  same drive). Owner's saves backed up **before** anything ran: full `Saved` tree
  (SaveGames + Config + ImGui + Telemetry) copied to `G:\...\local\save-backups\backup-20261022-001113`,
  all three `.sav` files SHA-256-verified against the originals (MATCH).
- Nothing written to `C:` except what the game itself writes (its own Save dir; the mod's
  own logs are kept on E:/G:).
- Tool: **RE-UE4SS `experimental-latest`**, asset `UE4SS_v3.0.1-1181-g83330635.zip`, 8.3 MB,
  own pin SHA-256 `B9A166A756605B21058A64CDE93BCAAB1A855DBA3553E5C7E6CE0FA8FDFAD7B9`;
  custom configs `zCustomGameConfigs.zip` SHA-256
  `0D91B15A75A7787BD4A469D1B3338A27C3E19CA843B5F8778F69A4D13F63B5DE` (release publishes no
  checksums — these are our pins). Installed per the playbook: `dwmapi.dll` proxy +
  `ue4ss/` contents into `MortalShell2\Binaries\Win64`, plus the MS2 custom config.

## TODO

- [x] Identify game, build, engine, store; record hashes — done 2026-10-22
- [x] Back up saves (hash-verified) — done 2026-10-22
- [x] Isolated test copy + UE4SS + MS2 custom config — done 2026-10-22
- [ ] First smoke test: UE4SS log, engine version it detects, console available
- [ ] Object dump: game classes (PlayerController/Pawn/GameMode), lobby/beacon presence, levels
- [ ] Map the flag store (quest/persistent state) and the save system
- [ ] Puppet slice: second player-shaped body in the world, driven locally
- [ ] Transport seam + relay (udp test stand-in → Steam Spacewar 480)
- [ ] Two-client automated test rig
- [ ] Ship: one-click install for host/join, README, QA level honest

### Observation — gameplay is reachable from code (the automation gate)

Calling the menu widget's own action works; no keypresses, no menu navigation:

```
widgetcall: 1 WBP_MainMenu_C /Engine/Transient...:BP_SpartaGameInstance_C_2147482225.WBP_MainMenu_C_2147481668
widgetcall: WBP_MainMenu_C:ContinueGame ok=true
=== world now: World /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 ===
pc    : BP_PlayerController_C   ...PersistentLevel.BP_PlayerController_C_2147479960
pawn  : BP_PlayerCharacter_C    ...PersistentLevel.BP_PlayerCharacter_C_2147479939
loc   : 48998.7 -134177.5 -42196.9  rot: 0.0 -55.0 0.0
mode  : BP_SpartaGameMode_C
```

The gameplay world is `/Game/Sparta/Maps/Core_Levels/L_Core_World_01` (a World Partition
map; player coordinates are large-world: ~49 000 / -134 000). Finding the widget by
substring full name picks the wrong object (`ComponentDelegateBinding ...:WBP_MainMenu_C`);
`FindAllOf("WBP_MainMenu_C")` returns exactly one instance and its `ContinueGame()` works.

### Observation — what still fails

- `summon /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C` produced no new
  `BP_PlayerCharacter_C` within 10 s. Cause found in our own Lua, not the game: `spawnPuppet`
  called a `local function runConsole` defined *later* in the file, so the call was nil and
  the console command never ran (the error was swallowed by the command pcall). Re-tested
  after making it global; see TODO.
- Transitional worlds (`/Temp/Untitled_0`) hand out null pawn objects; `K2_GetActorLocation`
  on them raises. The probe now skips pawns whose full name is a bare `UObject: <addr>`.

### Observation — second client for two-client tests

`<client B>` was built without a second 66 GB copy: `Engine` and `MortalShell2\Content` are
directory junctions to the original install and only `Binaries` (+ root launcher/manifests) is
real — 236 MB on disk. UE4SS and the mod are installed there with its own `coop/` state dir
and `playerid.txt`, so the two clients never share a network directory. Whether two copies run
side by side on one PC is still untested.

### Observation — the puppet slice passes (2026-10-22, 01:11)

`summon` on the game's own player class works and the actor can be driven exactly:

```
auto: menu widget after 0s
auto: ContinueGame ok=true
auto: gameplay after 5s, pawn BP_PlayerCharacter_C ..._2147479939
spawn: 1 BP_PlayerCharacter_C before summon
console [summon /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C]
auto: puppet adopted after 1s: BP_PlayerCharacter_C ..._2147471046
auto: placing puppet at 49252.2 -134182.5 -42196.9 (player 49002.2 ...)
drive: target ... -> actual ... (dist 0.0)
auto: PASS puppet at commanded spot (dist 0.0)
```

Two distinct `BP_PlayerCharacter_C` actors now exist: the possessed local pawn and an
inert puppet we own. `K2_SetActorLocation` on the puppet returns it exactly where asked.
This is playbook stage 2 (second player visible) on a real client, with no human input.

### Observation — window focus is load-bearing for automation

`WBP_MainMenu_C` only exists when the game window is *not minimized*: with the process
started `SW_MINIMIZE`, `FindAllOf("WBP_MainMenu_C")` returned nothing for 240 s twice in a
row; with the same code and a normally-created window parked off-screen
(`SetWindowPos(-30000,-30000)`, `SWP_NOACTIVATE`), the widget existed 0 s after the probe
asked for it. Automation launches keep the window off-screen but normal.

Second trap from the same runs: `FindFirstOf("WBP_MainMenu_C")` returns nil here while
`FindAllOf("WBP_MainMenu_C")` returns the single instance — use `FindAllOf`.

### Observation — tooling traps that cost runs

- A PowerShell `.Replace()`/`Set-Content` edit of a Lua file added a **UTF-8 BOM**; UE4SS
  then refuses the whole mod (`unexpected symbol near '<\xEF>'`) and *nothing* runs. Write
  Lua with `UTF8Encoding($false)`.
- A duplicated harness script silently launched **two** game processes per run. Check the
  script's line count after editing.
- The game crashes on teardown with UE4SS attached (`ACCESS_VIOLATION reading 0x0`), which
  raises a crash-reporter dialog on the owner's desktop. Test harness: launch with
  `-nocrashreport`, force-kill the process instead of asking it to close, and sweep
  `CrashReportClient` afterwards.

### Observation — a 15 Hz `io.popen` in a UE4SS mod wrecks the desktop

The mod's peer discovery did `io.popen('dir /b "<dir>/peer_*.txt"')` every tick. UE4SS's
Lua runs `io.popen` by spawning a real `cmd.exe`, so the machine got a console window
twice a second for minutes — enough to make the laptop unusable for its owner. The fix is
to derive the file name instead of listing the directory (`peer_<other>.txt`, two players
for now) and never call `io.popen`/`os.execute` from a tick callback. Verified afterwards:
`CMD_MAX=1` over a 60 s run with the game in gameplay.

Other things that leave windows on the owner's desktop and must be avoided in test runs:
`Start-Process` without `-WindowStyle Hidden` for helpers, and UE's crash reporter (use
`-nocrashreport` and force-kill the game instead of asking it to close).

### Observation — two clients on one PC need separate save dirs

Both copies share `%LOCALAPPDATA%\MortalShell2\Saved`, so a two-instance test writes one
save from two processes. The harness now gives each client its own `LOCALAPPDATA`
(`local-appdata\A`, `local-appdata\B`) seeded from a copy of the owner's saves, so the
owner's real save directory is never touched by a test at all.

### Observation — never hardcode a path in the mod

The first two-client attempt had the state dir hardcoded to client A's install; client B's
game loaded B's copy of the mod but read *A's* `coop_dir.txt`, so both processes wrote to
one state dir and the logs looked like one client had never started. The mod now derives
its state dir from `os.getcwd()` (`<cwd>/MortalShell2/Binaries/Win64/coop_dir.txt`) with a
fallback, so any install works.

### Observation — two real clients sync, over UDP (2026-10-22, 01:51)

```
A: PASS puppet tracks peer (dist 0.0, 30/30 good)
B: PASS puppet tracks peer (dist 0.0, 30/30 good)
RESULT: PASS
```

Two game copies (`<client A>` and `<client B>`), each in its own save with its own
`LOCALAPPDATA`, each entering gameplay, summoning a puppet, publishing its transform at
15 Hz and driving the peer's puppet from the received line; two `cooprelay.py` processes
move the lines over UDP on localhost (host owns the port, client takes an ephemeral one —
both binding the same port silently broke one direction). B was teleported ~800 units away
before the check so a shared spawn point could not fake a pass. The owner's saves were not
touched: each client ran with a seeded copy of it under `local-appdata\`.

### Observation — the game's own steam_api64.dll has the flat API

`Engine\Binaries\ThirdParty\Steamworks\Steamv157\Win64\steam_api64.dll` (1,118,360 bytes)
exports the flat C API, so the Steam relay can load the DLL from the player's own install
(no download, nothing to redistribute) — see the check in `ms2-coop/relay/`.

### State at end of session 2026-10-22 (~02:00)

Built and verified:

| Piece | File | Status |
|---|---|---|
| Save backup (hash-verified) | `local/save-backups/backup-20261022-001113` | done |
| Isolated test install (`<client A>`) + UE4SS + MS2 config | — | done |
| Second client (`<client B>`, junctions, 236 MB) | — | done |
| Probe mod (boot, dump, puppet, commands) | `ms2-coop/mod/CoopProbe` | worked |
| Co-op mod (boot → puppet → 15 Hz sync) | `ms2-coop/mod/CoopMod` | PASS both directions |
| Relay (`file`/`udp` working, `steam` written not yet run) | `ms2-coop/relay/` | udp PASS |
| Two-client harness (isolated saves, off-screen windows) | `ms2-coop/run-two.ps1` | PASS |
| Friend package (UE4SS + mods + relay + installer/launcher) | `ms2-coop/package/dist/ms2-coop-20261022-0158.zip` | built |

Open items (in order):

1. **Enemy/actor sync.** Not started. Need a gameplay object dump that survives (a
   `DumpAllObjects()` during gameplay coincided with a crash once; retry with the dump at a
   calmer moment). Then: host publishes nearest enemies (id, transform, hp, dead), guest
   spawns/updates a mirror with AI suppressed.
2. **Animation on the puppet.** The puppet is a real `BP_PlayerCharacter_C`, but it is
   teleported, so its own AnimBP sees no velocity and idles. Options: drive a movement
   component, or attach the local player's anim instance if it does not cast to a
   controller (Poppy precedent says it usually does).
3. **Steam transport smoke test.** `relay/steamlink.py` loads the game's own
   `steam_api64.dll` (flat API confirmed present), creates a friends-only AppID-480 lobby
   and moves lines over `ISteamNetworkingMessages`; it has never run. Needs Steam signed
   in and a second account/friend for the message leg.
4. **Stability.** UE4SS on this UE 5.6 build crashes occasionally (access violation at
   0x0), sometimes right after a summon, always leaving a crash-reporter dialog. Test
   harness now kills the reporter on sight, launches with `-nocrashreport`, and prefers
   force-kill; a crash in the middle of a run is still possible and must be retried.
5. **Quest/world sync** via `SpartaSaveSubSystem` + `BPSO_*` (design only).

## Phase 5 — Build, NPC mirror (2026-10-22 02:12)

### Observation — the hub is full of Characters, and three of them are enemies

`chars.txt` (a probe command listing every `Character`/`Pawn`/`AIController` in the world)
shows 21 characters loaded in the hub: dialogue NPCs (`BP_Cultist_HUB_*`, `BP_Merchant`,
`BP_Blacksmith`, `BP_NPC_ShellKeeper`, `BP_NPC_Genessa_Training`, …) and **three
`BP_TurretLivingBase_PairTest_C`** — enemy-class turrets placed in the hub for testing.
Enemy assets live under `/Game/Sparta/Characters/Enemies/...`.

### Observation — actors keep identical names across instances

Both clients' saves spawn the same actors with the **same object names** (e.g.
`BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_..._1417419333`, and the player pawn kept
`BP_PlayerCharacter_C_2147479936` on both). So a mirror can match the peer's actors by
name tail, with no id allocation protocol at all — the world is deterministic from the save.

### Observation — NPC mirror passes, and the first "pass" was hollow

First run: `PASS npc mirror 18/20 within 30u` — but the verdict was taken *before* the
host moved anything: two copies of the same save trivially agree, so the test proved
nothing. Tightened: the guest records each actor's first reported position and only starts
counting once the peer's coordinates have moved more than 200 units from it.

Second run:

```
host: npctp -> BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_..._1417419333 at 49400 -134182 -42197
B   : npc verdict: 20/20 good
NPC : PASS npc mirror after host move 20/20 within 30u (last 0.0, ...)
A/B : PASS puppet tracks peer (dist 0.0, 30/30 good)
RESULT: PASS
```

### Observation — Steam transport brings up a real lobby

`python relay/cooprelay.py --transport steam --role host --game <game dir>`:

```
steam dll: <game dir>\Engine\Binaries\ThirdParty\Steamworks\Steamv157\Win64\steam_api64.dll
steam init ok
logged on as <steamid>
manual dispatch ready (pipe 1)
LobbyCreated result=1 lobby=108086395351860111
LobbyEnter lobby=108086395351860111 response=1
LOBBY=108086395351860111  (friend joins with --lobby 108086395351860111)
```

So the game's own shipped `steam_api64.dll` drives Steamworks from Python: init, logon,
friends-only lobby and entering it all succeed, and the relay keeps dispatching callbacks.
The **message leg** (`ISteamNetworkingMessages` between two accounts) is still untested —
it needs a second Steam login on another machine, which this session did not have.

Model: host publishes the 10 nearest Characters (excluding players) at 15 Hz inside the
existing player message; guest maps them by name tail, sets their location and asserts.
Known gap: health/damage/death are not published, so a kill is not mirrored, and only the
host's copy drives positions (a guest's enemy AI still runs locally and fights the mirror).

### Observation — overridden LOCALAPPDATA does not move a UE game's saves

Isolating two clients by setting `$env:LOCALAPPDATA` **does not work**: Unreal resolves its
user/save directory through the Windows known-folder API, so the game ignored the override
and kept writing to the real `%LOCALAPPDATA%\MortalShell2\Saved\SaveGames` (it created new
`WorldState_Backup_0_*.sav` files there during test runs). What works is **snapshot and
restore**: copy `*.sav` before the run, and afterwards restore them and delete any `*.sav`
that appeared during it. Verified by hash against the pre-session backup.

Two further test-design lessons from the same stretch:

- **A pass measured before the action proves nothing.** The first NPC-sync verdict
  (`18/20 within 30u`) was taken before the host moved anything: two copies of one save
  agree trivially. The test now requires the peer's coordinates to move > 200 units from
  the first observed value before it starts counting.
- **A packaged deliverable needs an install test.** Installing from the built zip into a
  clean client exposed three packaging bugs the dev deploy never hit (wrong `dwmapi.dll`
  path, wrong UE4SS folder nesting, relay copy skipped). After the fix, a clean install
  from the zip plus the two-client suite passes end to end.
