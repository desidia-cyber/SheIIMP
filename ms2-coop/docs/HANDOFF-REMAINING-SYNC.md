# HANDOFF — everything that is still NOT synced (implementer's doc)

Written 2026-10-22, after round 13. **Read this together with `HANDOFF.md`** (which records what
was built, round by round) and `DESIGN.md` (why it is built that way). This file is the opposite
side of the ledger: *what is missing, where the missing state lives, how to read and write it, how
to test it, and what will bite you.*

Nothing here is implemented yet. Nothing in the mod, the handoff docs or the dumps was changed to
produce this file — it is a read-only audit of:

| Source | What was taken from it |
|---|---|
| `ms2-coop\mod\CoopMod\Scripts\main.lua` (4 133 lines, 186 015 B, mtime 17:27) | every sync section, wire field, cap, kill switch, with `main.lua:NNN` line refs |
| `ms2-coop\docs\{HANDOFF,DESIGN,QA}.md`, `package\README.md` | rounds 1–13, all 21 harness verdicts |
| `ms2-coop\relay\cooprelay.py` (277 lines) | transport, loss model, absence of session/versioning |
| **live state line from the last run**: `E:\Games\_MS2-coop-b\MortalShell2\Binaries\Win64\coop\out_B.txt` (4 157 B) + `out_A.txt` (2 852 B) + `sync.txt` | the *measured* wire content and per-subsystem counters |
| `research\10-world-state.md` (1 755 l), `20-quests-progression.md` (953), `20-…-part2.md` (1 076), `30-player-look.md` (2 060), `40-player-actions.md` (1 968), `40-…-part2.md` (1 967), `50-enemies-bosses.md` (1 792), `60-items-inventory.md` (2 260), `70-flow-menus.md` (1 834), `90-verified.md` (322), `README.md` (294) | every state container, field, offset, function signature |
| `research\05-dumper7-vs-ue4ss.md` (624) | the dump cross-check (78 740 property slots, 0 conflicts) and the patched-Dumper-7 facts |

Game build: `5.6.1-0+++Sparta-Depot+Day1+CL92086`, exe SHA-256
`BDEDDB33D61E876E94B6C30C8EA198AECFDEFFB557D6F5678C92D1C4C17AAE69`, UE 5.6 shipping, **no netcode**.

---

## 0. Conventions used in this file

* **[FACT]** — proven by a dump line, a header offset, a log line or a run artifact. The proof is
  cited inline (`10-world-state.md §5.1`, `main.lua:2491`, `out_B.txt`, `DAMAGE: PASS` …).
* **[PROPOSAL]** — my design suggestion, not implemented, not proven. Safe to ignore or replace.
* **[UNKNOWN]** — nobody knows yet; the doc says what experiment would settle it.
* Every `main.lua:NNN` reference is the file as of mtime 17:27 (round 13). Line numbers will
  drift; grep the function name.

### Standing rules (from the research/QA briefs — keep them)

1. Never touch the real install `E:\Games\Mortal Shell II`; test copies are
   `E:\Games\_MS2-coop-test` (client A) and `E:\Games\_MS2-coop-b` (client B).
2. Never corrupt the real saves (`%LOCALAPPDATA%\MortalShell2\Saved\SaveGames`). Snapshot before a
   run, restore + hash-compare after (`QA.md` "Save protection is snapshot/restore, not
   isolation", `HANDOFF.md` §5.13).
3. In-game probes go through `mod\CoopLab` + `lab.ps1` only. **`CoopMod\Scripts\main.lua` is the
   shipped mod** — change it deliberately, and bump a backup under `local\` first (the round-13
   work left `local\main.lua.bak-before-*` as the precedent).
4. Lua files must be UTF-8 **without BOM** (`HANDOFF.md` §5.12).
5. Kill every game / `CrashReportClient` / `cooprelay.py` you start.
6. One client per machine while measuring perf; both clients are parked off-screen
   (`SetWindowPos(-30000,-30000)`, never minimized — `HANDOFF.md` §5.2).

---

## 1. Baseline: what exists, so you do not break it

### 1.1 Architecture [FACT]

```
game.exe + UE4SS (dwmapi proxy)
   └── Mods/CoopMod (Lua, 4 133 lines)  ── files ──  cooprelay.py (Python)  ── udp / steam ──  peer PC
```

* Three loops, each step guarded (`main.lua:4004 step()`, `main.lua:4055 runLoop()`):
  **30 Hz** puppet drive (`driveTick` 1668), **15 Hz** publish/apply (`sendOwn` 1566,
  `applyPeer` 1936), **2 Hz** boot + all world/progress/chest/lift/stream/enemy work
  (`writeSyncStatus` 3968 wires the 2 Hz list).
* File contract per install: `<game>\MortalShell2\Binaries\Win64\coop\` —
  `out_<id>.txt` (mod→relay, one JSON line, rewritten every publish), `peer_<id>.txt`
  (relay→mod), `role.txt`, `playerid.txt`, `coop_dir.txt`, `sync.txt` (2 Hz status),
  `heartbeat.txt` (1 Hz loop liveness), `probe.log`, `result*.txt`, `ws_ledger_<slot>.txt`.
* Lua has **no sockets** — the relay is mandatory (`DESIGN.md` "Why a helper process").

### 1.2 The wire format, measured [FACT]

`out_B.txt` from run 13 is **4 157 bytes**; `out_A.txt` **2 852 bytes**; sent at 15 Hz →
**≈ 60 KB/s per direction**, one UDP datagram each (`cooprelay.py:117` `sendto(payload)`), i.e.
above the 1 472-byte MTU ⇒ **IP fragmentation, all-or-nothing, no retransmit**.

Sections in one line (`main.lua:1599-1604`):

| Key | Content | Cap | Code |
|---|---|---|---|
| `t`,`id`,`sq` | message type, player id, sequence | — | 1569, 1599 |
| `rdy` | `0` while this client is not in gameplay; then the whole line is `{"t":"p","id","sq","rdy":0}` and the peer hides the body | — | 1567-1571 |
| `x,y,z,vx,vy,vz,yaw,pitch` | own transform + velocity | — | 1599 |
| `anim,spd` | `idle`/`walk`/`run`/`action` + speed | — | 1593-1596 |
| `mseq,mpos,mrate,mid` | current montage **resolved to its segment sequence** + position/rate/id | — | 1595, `ownMontage` 1485 |
| `body`,`apart` | own shell mesh path, own AnimBP path | — | `computeMyBody` 611 |
| `wpn`,`wsock`,`wpns` | primary weapon + **every rendered weapon mesh** (path, kind, socket, x,y,z,r,p,y) | — | `localWeaponInfo` 706 |
| `lk` | look signature: body materials + every attachable/extra mesh (kind, mesh, socket, loc, quat, scale, leader-pose flag, materials) | — | `LOOK.capture` 980 |
| `npc` | up to **16** enemies within **4 000 uu** of either player: `[key,x,y,z,hp,dead,vx,vy,vz,yaw,"montage"]` | `MAX_NPC=16` (1253), `NPC_RADIUS=4000` (1254) | `collectNPCs` 1265, `npcJSON` 1332 |
| `pd` | cumulative damage the peer's body took | — | `puppetDamage` 3898 |
| `nhp` | lowest normalized health seen per mirrored enemy | ≤16 | `nhpJSON` 1356 |
| `dead` | tails this client considers dead | 40 (`main.lua:1370`) | `deadJSON` 1365 |
| `ws` | store-A keys known true, `[hash,0\|1]`; live flips + rotating window | window 40 (2246), live 20 s (2245) | `WS_KINDS` 2211, `wsBuildPayload` 2455 |
| `wb` | store B `["i\|owner\|key",v,live]` / `["b\|…")` | rotating window | `wpSection` 2653, `wbScan` 2704 |
| `wq` | quest log keys `t\|c\|a\|s\|o\|u\|k\|r\|e` | rotating window | `wqScan` 2824 |
| `wd` | data-layer runtime states (+1 biased so 0 travels) | 2 applies/tick, 10 s/layer, gate 15 000 uu | `wdScan` 3059, `wdApply` 3076 |
| `wl` | lifts `["name",seq,floor,active]` | — | `LF.build` 3446 |
| `wc` | chests `["name",seq,count,done,"loot"]` | — | `CS.build` 3611 |

Measured counters in the same run (`sync.txt`): `ws=seen:145 true:12 peer:12 base:2`,
`wp=b:5/5+0!0 q:24/18+0!0 d:104/0+0!0`, `lift=n:1`, `chest=n:9`, `stream=npcown:2 npcmirror:708`,
`enemy=driven:5 taken:33 released:28 montages:13 spawnask:76 spawnok:5`, `perf=near:64.67ms`.

### 1.3 The regression suite you must keep green [FACT]

`run-two.ps1` prints these verdicts (`run-two.ps1:191-394`):
`SHELL`, `WEAPON`, `LOOK`, `ANIM`, `DAMAGE`, `DEATH`, `WORLD`, `PROGRESS`, `QUEST`, `QUEST2`,
`LIFT`, `CHEST`, `SPAWN`, `MIRROR`, `PEERHIT`, `AITARGET`, `STREAM`, `NPCFAR`, `PERF`, `LEDGER`,
`NPC`, `A`, `B` → `RESULT: PASS|FAIL`.

Two known false reds, do not "fix" them by weakening the test:

* **`WORLD` fails in every run since round 5** because its fixture (one door + one pickup) is
  already open in the test save (`HANDOFF.md` §14–§18 all end "`RESULT: FAIL` only from `WORLD`").
  The replay itself still logs `ws apply … ok`.
* **`LIFT`/`CHEST`** replay but occasionally miss the harness verdict window (round 8 note).

### 1.4 Kill switches [FACT] (read once at mod load — create before launching)

| File in the coop dir | Disables | Code |
|---|---|---|
| `WS_OFF.txt` | **everything** world/progress (store A + B + quests + layers + lifts + chests) | 669 |
| `WS_LEDGER_OFF.txt` | store-A ledger only | 2304 |
| `QS_OFF.txt` / `DL_OFF.txt` | quest log / data layers | 2624-2625 |
| `LIFT_OFF.txt` / `CHEST_OFF.txt` | lift replay / chest lid+loot | 3359, 3514 |
| `MIRROR_OFF.txt` | non-owner enemy mirroring | 3814 |
| `ANIM_OFF.txt` / `WEAPON_OFF.txt` / `LOOK_OFF.txt` / `STREAM_OFF.txt` | peer anim / weapons / look / streaming source | 383, 663, 855, 511 |
| `LOAD_ON.txt` | **enables** the game-thread asset loader (off by default; suspected in crashes, then ruled out — still off) | 255 |
| `GAME_THREAD.txt` | **enables** running all loops on the game thread (proven to crash both clients; never on) | ~4045 |

### 1.5 Verified vs merely implemented [FACT]

**Green in an automated two-client run:** transform, shell body, locomotion anim, action montage,
look, weapons, enemy mirror + ownership handover, enemy health, enemy death, AI retarget, damage on
the peer's body, proximity-spawn replay, streaming around the peer, store A replay (last clean
verification round 7), store-A ledger, store B ints/bools, chest lid + shared loot, quest
add/complete, quest stage/counter/turn-in/tracked/relationships, one data layer, hub lift.

**Implemented, never verified against a real player action:** chest opened by a player, lift by a
real lever and the return trip, lift-call/drawbridge families, a quest from real dialogue, the other
104 data layers, masks/clothing/leader-pose, **different shells on the two clients**, skins with real
material swaps, dark-form attachables, enemy montage replay inside a real fight, every store-A class
that is "field set only" (boss gates etc. only show the new state after a reload).

---

## 2. The state model you have to extend

### 2.1 The five places game state lives [FACT]

| Store | Object | What it holds | Evidence |
|---|---|---|---|
| **A** | per-actor `USpartaSaveObject` in `SpartaSaveComponent.RuntimeData` | one object per stateful actor; class from `SaveObjectClass` | `10-world-state.md` §1.1; component size `0x120`, `SaveObjectClass 0xC0`, `SaveProtocol 0xF8`, `Guid 0xFC`, `RuntimeData 0x110`, `bSaveLoaded 0x118` |
| **B** | `USpartaSaveGame` (`ActorBools/Ints/Floats/Names` + `ActorObjectMap`) | named values by owner+key, written through `SpartaWorldStateHelper` | §1.2; offsets `0x168 / 0xc8 / 0x78 / 0x118 / 0x1b8`, `ChangedActors 0x28` |
| **C** | `SpartaDataLayerSubsystem.RuntimeData` → `DataLayersSave.SavedDataLayerStates` | data-layer runtime states | §8.10 |
| **D** | `BPSO_PlayerSession_C` (session, not on disk) | HP/shell HP/resolve/charges/revive/transform/darkform | §5.3 |
| **Player save** | `BP_SpartaPlayerSaveObject_C` (per profile slot) + `BPSO_Progression_C` + `BPSO_ShellMemories_C` | unlocks, achievements, cinematics, NG+, levelling, shell memories | §5.4, `20-part2` §9.3/§11 |

Registry/authority: `USpartaSaveSubSystem` — `GetCurrentWorldStateSave()`,
`SaveObjects: TMap<FGuid,…>` (0xb8), `CurrentOnDiskWorldStateSave` (0x108), `SessionSaveObject`
(0x120), `RuntimeSessionSaveObject` (0x128), `SaveToDisk(SaveEvents, bSaveImmediately,
bSaveAsync)`, `SaveSession()`, `ResetSaveObjectByGuid(GuidString)`, `GetIsSaveLoaded()`,
`SetPreventFurtherSaves(bool)`, delegates `OnSaveGameLoaded`/`OnSaveStarted`/`OnSaveFinished`/
`PostSessionReset` (`10-world-state.md` §1.3).

### 2.2 Identity model and where it breaks [FACT]

* Actor names are `<Class>_UAID_<hash>_<hash>_<int>` and the tail is stable across launches **for
  the same map from the same save** (`90-verified.md` §2 vs §11 shows the identical
  `BP_HubDoubleDoor_C_UAID_D4E98A7EF567D94802_83a1dc45e2a01438_1808979936` in two runs;
  `README.md` §3.1).
* A save object is `<ActorName>.SpartaSave.RuntimeData_<n>` (`90-verified.md` §5).
* The mod keys store A by **FNV-1a of the owner name** (`wsHash` 2262) and store B by
  `owner|key` names. **Consequence:** two players standing in different maps/level instances have
  no matching keys at all — nothing in store A/B/quests syncs across zones (§3 G9).
* Copies under `/Engine/Transient` (save objects of *unloaded* actors) carry **no owner name** and
  `SpartaSaveSubSystem.SaveObjects:ForEach` **hard-crashes the game** (`HANDOFF.md` §14). Hence
  the name-based ledger workaround.

### 2.3 Master coverage matrix — 51 save-object classes [FACT]

Enumerated in `10-world-state.md` §5 (`save_objects_blueprint = 51` = 46 `BPSO_*` + 3 `BP_SO_*` +
`BP_SpartaPlayerSaveObject_C` + `BP_SpawnerSessionSaveObject_C`) and `20-part2` §11 (52 rows incl. 6
`BPO_*` that are **not** save objects). ✅ = watched by `WS_KINDS` (`main.lua:2211-2243`).

| Save object | Watched | Field(s) the mod reads / misses |
|---|---|---|
| `BP_SO_SimpleBool_C` | ✅ `MyBool` | — |
| `BPSO_CraftingItemPickup_C` | ✅ `ItemCollected` | ❌ `ItemInitialized` |
| `BPSO_FlowerChest_C` | ✅ `ItemCollected` | — |
| `BPSO_DoorLockedDungeon_C` | ✅ `DoorOpened` | — |
| `BPSO_BossGate_C` | ✅ `Unlocked` | — |
| `BPSO_Gate_C` | ✅ `GateOpened` | ❌ transforms/`GotVarsOnce` (harmless) |
| `BPSO_CloseAndOpenGate_C` | ✅ `CombatEncCompleted` | — |
| `BPSO_ShepherdSheepGate_C` | ✅ `GateOpened` | — |
| `BPSO_TarGate_C` | ✅ `GateClensed` | ❌ `PlayerClensed` |
| `BPSO_BearTrap_C` | ✅ `IsDisarmed` | — |
| `BPSO_SpawnerSaveObj_C` | ✅ `WasEverKilled` | — |
| `BPSO_GrishaGhost_C` | ✅ `BossDefeated` | — |
| `BPSO_HutchbackBossTrigger_C` | ✅ `HutchbackDefeated` | — |
| `BPSO_EridrimUnlockManager_C` | ✅ `WardenJumped`,`WardenDefeated` | ❌ `ShellClaimed` |
| `BPSO_FC_FinalBossManager_C` | ✅ 2 bools | ❌ `ClensedCores` (int), `TotalCoresNeeded` |
| `BPSO_FC_CorruptionCore_C` | ✅ `Cleansed` | — |
| `BPSO_FC_TarRoots_C` | ✅ 4 bools | — |
| `BPSO_EvilStatue_C` | ✅ `Cleansed` | ❌ `RewardGiven` |
| `BPSO_Ovum_C` | ✅ `Cleansed` | ❌ `OvumCollected` |
| `BPSO_GenessaSensor_C` | ✅ `SensorClensed` | — |
| `BPSO_GrishaCage_C` | ✅ `CageOpened` | ❌ `OpenType` (tag) |
| `BPSO_FallingCorpse_C` / `_FallingStatue_C` | ✅ | — |
| `BPSO_SlayerDungeonChalice_C` | ✅ `SealBroken` | — |
| `BPSO_TarGolemLevelScript_C` | ✅ `StoneSealClaimed` | — |
| `BPSO_LandingArea_Boss_C` | ✅ `BeaconRisen` | — |
| `BPSO_CinematicDLUpdate_C` | ✅ `CinematicFired` | — |
| `BP_SO_SavedKeys_C` | ❌ | `SavedKeyIDs: TArray<FString>` |
| `BP_SO_SavedLockedDoors_C` | ❌ | `SavedDoorIDs` |
| `BPSO_SavedLockedChests_C` | ❌ | `SavedChestIDs`, `HasBeenOpened` |
| `BPSO_MovingObject_C` | ❌ | `MovedToEnd` (+ start/end transforms) |
| `BPSO_WoodenElevator_C` | ❌ | `StartingFloor: int32`, `HasBrokenOnce` |
| `BPSO_NPC_C` | ❌ | `HasTriggeredInitialGreeting`, `WasEverKilled` |
| `BPSO_ShellRevive_C` | ❌ | `StationUsed` |
| `BPSO_ShellMemories_C` | ❌ | `MemoryProgress: TMap<tag,int32>`, `MemoriesWatched: TSet<FName>` |
| `BPSO_ShopData_C` | ❌ | `CachedItemsMap`, `TotalGoldSpent`, `StolenItems` |
| `BPSO_Tarforge_C` | ❌ | `StoredTarcores: int32` |
| `BPSO_TavernArena_C` | ❌ | `WagerPlaced`, `ArenaCombatLVL`, `RewardGiven` |
| `BPSO_BossDLManager_C` | ❌ | `CurrentBoss`, `RelevantDL` |
| `BPSO_WordMapObjective_C` | ❌ | `Visible/Revealed/Completed/ShowArea/PlayedRevealScript` (133 live) |
| `BPSO_WorldMap_C` | ❌ | `Trackers`, `ObjectiveGUID`, `IsInsideObjective` |
| `BPSO_Progression_C` | ❌ | `CurrentLevel`, `AvailablePoints`, `AssignedPoints`, `BondingPoints`, `GoldSpentAtShop` |
| `BPSO_PlayerSession_C` | ❌ | `Health/ShellHealth/Resolve/HealCurrentCharges/ShellReviveProgress/StartTransform/IsInDarkform` |
| `BPSO_NarrativeEventsTracker_C` | ❌ | `NarrativeEventTimeTracking: TMap<tag,FF_NarrativeEventFloatTracking>` |
| `BPSO_QuestLogComponent_C` | ~ | tags/quests read from the **component**, not this object; `NarrativeTags` = its only field |
| `BPSO_PlayerTrapDungeon_C` | ❌ | `TrappedOnce` |
| `BPSO_PetrifiedCultistManager_C` | ❌ | `Harros` |
| `BPSO_ShellGhost_C` | ❌ | `StateIndex: int32` |
| `BPSO_Egon_C` | ❌ | `GloomDonated: int32` |
| `BP_SpartaPlayerSaveObject_C` | ❌ | 60+ fields — see §3 G5 |
| `BP_SpawnerSessionSaveObject_C` | ❌ | `WasKilled`, `DidSpawn`, `AttemptedSpawn` |

Plus one **actor** field that is not a save object: `BP_HubDoor_C.DoorIsOpen` (`0x2e0`) is watched
explicitly (`WS_KINDS` entry 1) because the door also has a store-B key (`DoorIsOpenName 0x2d8`).

---

## 3. The gap register (the work)

Ordered by what a player notices first. Each item: *what / where the state is / how to read / how to
write / wire proposal / how to test / hazards*.

---

### G1 — Store A: 24 of the 51 save-object classes + 8 fields are invisible

Count: `WS_KINDS` has **28 entries** = 27 save-object classes (26 `BPSO_*` + `BP_SO_SimpleBool_C`)
plus the actor field `BP_HubDoor_C.DoorIsOpen`; 51 − 27 = **24 classes not watched** — of which one
(`BPSO_QuestLogComponent_C`) is covered another way (`wqScan` reads the *component*), so 23 classes
are fully invisible.

**Why it matters.** These are the "I did a thing, my friend's world still shows the old thing" bugs.
Store A is a flat object per actor, so adding a class is cheap — it is a table entry, not new code
(`WS_KINDS`, `main.lua:2211`). Merge rule already implemented: **true wins**, live flips replayed
through the game's own action, later-in-streamed actors covered by the ledger.

**How to read** [FACT]: `SpartaSaveComponent.GetRuntimeData()` → object; fields are plain
UPROPERTYs; the mod already resolves instance → owner actor → name (`wsOwnerActor` 2484).
**How to write** [FACT]: for bools the mod does `obj[field] = true` (`wsApplyOne:2509`); for
actors with a real action it calls the action (`OpenDoor`, `PickUp`).

**[PROPOSAL]** add to `WS_KINDS` (class → field list), plus a small "typed value" mode for the ints
and arrays that do not fit the boolean table:

| New entry | Fields | Merge | Notes |
|---|---|---|---|
| `BPSO_MovingObject_C` | `MovedToEnd` | true wins | drawbridges, `BP_Electric_StoneGate_C`, moving platforms. Visual side: `BPI_LiftCall:TriggerMovingActor(bool)`, `GetLiftStateBool(bool&)` (reuse the `LF` pattern, `main.lua:3345`) |
| `BPSO_WoodenElevator_C` | `StartingFloor` (int) | host wins | `BP_Circle_Wooden_Elevator_C` / `_Cathedral_` / `BP_Wooden_Elevator_Platform_01_C` / `BP_Grand_Elevator_Snow_C` all expose `As BPSO_WoodenElevator` |
| `BPSO_NPC_C` | `HasTriggeredInitialGreeting`, `WasEverKilled` | true wins | stops "the dead cultist greets me again" |
| `BPSO_ShellRevive_C` | `StationUsed` | true wins | pairs with G4's revive meter |
| `BP_SO_SavedKeys_C` / `BP_SO_SavedLockedDoors_C` / `BPSO_SavedLockedChests_C` | `SavedKeyIDs` / `SavedDoorIDs` / `SavedChestIDs` (arrays) | set union | needs a value codec (JSON array of strings / soft class paths) — see G1b |
| `BPSO_ShopData_C` | `CachedItemsMap` (map), `TotalGoldSpent` (int), `StolenItems` (bool) | host wins / min-max | see G5 |
| `BPSO_Tarforge_C` | `StoredTarcores` (int) | host wins | |
| `BPSO_TavernArena_C` | `WagerPlaced`, `ArenaCombatLVL`, `RewardGiven` | true/host wins | |
| `BPSO_BossDLManager_C` | `CurrentBoss` (enum), `RelevantDL` (object) | host wins | also G7 |
| `BPSO_ShellGhost_C` | `StateIndex` (int) | host wins | |
| `BPSO_Egon_C` | `GloomDonated` (int) | host wins | |
| `BPSO_PlayerTrapDungeon_C` | `TrappedOnce` | true wins | 1-line add |
| `BPSO_PetrifiedCultistManager_C` | `Harros` | true wins | 1-line add |
| `BPSO_CinematicDLUpdate_C` | ✅ already | | |
| `BPSO_NarrativeEventsTracker_C` | `NarrativeEventTimeTracking` (map of struct) | host wins | probably only needed if you sync cinematics (G10) |
| `BPSO_WordMapObjective_C` | 5 bools | **per-player decision** | 133 instances; decide reveal-vs-completed split first (G8) |

Field additions to **already watched** classes (pure table edits): `CraftingItemPickup.ItemInitialized`,
`EridrimUnlockManager.ShellClaimed`, `EvilStatue.RewardGiven`, `Ovum.OvumCollected`,
`TarGate.PlayerClensed`, `FC_FinalBossManager.ClensedCores`, `GrishaCage.OpenType`.

**G1b — value codec** [PROPOSAL]: the wire is a hand-rolled JSON subset parsed by `section()` +
`gmatch` (`main.lua:1435`). Arrays/maps/ints need either (a) a `wt` section of
`["<key>","<type>",<n>,"<payload>"]` triples with a JSON-ish payload string, or (b) one key per
element (`k|<key>|<index>`), which needs no new parser and is the safer choice given the Lua
`gmatch` style already in use.

**Test** [FACT harness]: every new class gets a `cmd` hook like `wstest`/`liftTest` (`main.lua:2570`,
`3470`) + a `result_*.txt` verdict + a `=== NAME:` line in `run-two.ps1`. Cheapest fixtures:
`reset` the save object first — `SpartaSaveSubSystem:ResetSaveObjectByGuid(GuidString)` exists and is
callable from Lua (`10-world-state.md` §10.2 #9), which also **fixes the perennial `WORLD: FAIL`**.

**Hazards** [FACT]: field names that do not exist silently return `nil` (no error) — verify each
against `raw\CXXHeaderDump\<Class>.hpp` before shipping; `FGameplayTag`/`FText`/`FGuid` fields come
back as opaque userdata (`90-verified.md` §13), so only bool/int/name-ish fields are directly usable.

---

### G2 — Store B is only half read

**Why it matters.** Store B is the *generic* mechanism: any actor can persist `WriteMyFloat` /
`WriteAName`. The mod syncs `ActorInts` + `ActorBools` and ignores the other two maps, so any
float- or name-typed world value is invisible.

[FACT] `USpartaSaveGame`: `ActorFloats 0x78` (`TMap<FName,FSpartaMapFloat>`), `ActorInts 0xc8`,
`ActorNames 0x118` (`TMap<FName,FSpartaMapName>`), `ActorBools 0x168`, `ActorObjectMap 0x1b8`,
`ActorObjectMapAsString 0x208`, `ChangedActors 0x28` (`10-world-state.md` §1.2).
[UNKNOWN] whether the shipped content actually writes floats/names — nobody has counted them at
runtime. **Experiment:** a one-shot CoopLab probe that iterates `GetCurrentWorldStateSave()` and
logs `ActorFloats`/`ActorNames` counts + the first 20 keys (the `TMap:ForEach` route is proven safe —
`wbScan:2712` uses it every 2 Hz).

**[PROPOSAL]** mirror `wbScan`/`wbApply` for the two missing maps:
`wf` section (`["o|key",<float 3dp>,live]`) and `wn` (`["o|key","Name",live]`), float merge = host
wins-or-max (decide per key), name merge = last-writer-from-owner. `ActorObjectMap` (object
references) should be **read-only telemetry** — writing object refs across clients is meaningless
(pointers differ) and the map is empty at runtime anyway (`HANDOFF.md` §14).

**Test**: extend `wpTest` (`main.lua:3110`) to also write one float and one name
(`WriteAnFloat` / `WriteAName` exist, `10-world-state.md` §10.2 #3) and let the guest read it back
with `ReadAnFloat`/`ReadAName`.

---

### G3 — Live world objects that persist nothing (only a sync layer can carry them)

[FACT] these have **no** `USpartaSaveComponent`, so no store can remember them; the only way both
clients see them is an explicit one-shot event. All are pure visual/gameplay state.

| Object | State field | Evidence |
|---|---|---|
| `BP_VFX_Breakable_C` (barrels, pots, carts, chandeliers — 92 `BP_VFX_*` classes) | `IsBroken 0x518`, `OnBroken 0x578`, `TriggerDamage(...)`, `CustomBreak(HitResult, Instigator)`, `ConvertToBreakable()` | `10-world-state.md` §8.6 |
| `BP_PressurePlateOpenDoor_C` (+`_Morgue`,`_Oriental`) | `Opened 0x361`, `RemainOpenAfterFirstInteract 0x360` | §8.4 |
| `BP_FlowerChest_C` | `HitsReceived 0x338` / `RequiredHits 0x348` (progress toward breaking) | §8.3 |
| `BP_LockedLightChest_C` | `CurrentLights 0x460`, `Unlocked 0x468` (light-key counter) | §8.3 |
| `BP_BossGate_C` / `BP_BlockedBossGate_C` / fog walls | `ApproachingGate()` + timeline, no save | §8.2 |
| `BP_Interaction_Traversal_C*` (squeeze/vault/tarpad) | `IsDisabledDueToDamage`, `LinkedTraversals` | §8.9 |
| `BP_MakeRockTrap_C`, `BP_SpikeTrap_C`, `BP_MushroomTrap_C` | no save | §8.7 |

**[PROPOSAL]** a one-shot `wx` event channel (`["k|kind|tail",seq]`, deduplicated by seq) plus a
receiver that calls the *engine-side* equivalent: `TriggerDamage`/`CustomBreak` on breakables
(careful: needs a hit result — start with `SetActorHiddenInGame` + `SetActorEnableCollision(false)`
as the chest-loot path already does at `main.lua:2505`), `Opened=true` for plates, and
`LF`-style `TriggerMovingActor` for `BPI_LiftCall` actors.

**Test**: a `breaktest` command that breaks the nearest `BP_VFX_Breakable_C` and a verdict that the
peer's copy is hidden/broken within 3 s.

**Hazards**: `CustomBreak` wants an `FHitResult` — do **not** construct one blindly; the proven
route for "hit" from Lua is `SpartaApplyHit` (G6) or the plain-visual route above.

---

### G4 — The other player's body state (nothing is published)

This is the single biggest "it does not feel like co-op" gap. Today only transform / look / anim /
weapons cross the wire. Not published: **health, shell health, resolve, heal charges, PP resource,
dark-form, harden/stone form, block/parry held states, sidearm ammo, death state**.

[FACT] where it lives:

| Value | Attribute / field | Offset |
|---|---|---|
| health | `USpartaHealthSet.Health` / `.MaxHealth` | `0x60` / `0x70` |
| resolve | `USpartaHealthSet.Resolve` / `MaxResolve` | `0xD0` / `0xE0` |
| poise / armor / break | `USpartaHealthSet.Poise 0xF0`, `Armor 0xC0`, `MaxBreakResistance 0x220`, `RiposteWeakness 0x230` | `40-player-actions.md` §2.1 |
| heal flasks | `UPlayerAttributeSet.HealCurrentCharges 0xE0`, `HealMaxCharges 0xF0`, `HealAmountPerCharge 0x100` | §2.3 |
| shell revive meter | `UPlayerAttributeSet.ShellReviveCurrentProgress 0x150`, `ShellReviveMaxProgress 0x160`, `ShellReviveProgressGain 0x170` | §2.3 |
| PP resource | `PPItemResource 0x190`, `PPItemMaxResource 0x1A0`, `PPItemCost 0x1B0` | §2.3 |
| EXP | `CurrentEXP 0x30` | §2.3 |
| econ buffs | `GoldMultiplier 0x130`, `GoldBonus 0x140`, `GloomMultiplier 0x110` | §2.3 |
| sidearm ammo | `USidearmAttributeSet.CurrentAmmo 0x30`, `MaxAmmo 0x40`; `UDarkPowerAttributeSet.CurrentAmmo 0x30` | §2.5-2.6 |
| death | `USpartaHealthComponent.DeathState` + delegates `OnDamageReceived` / `OnDeathStarted` / `OnDeathFinished`; enum `ESpartaDeathState NotDead=0/DeathStarted=1/DeathFinished=2` | `50-enemies-bosses.md` §6.2, `30-player-look.md` §8.1 |
| harden | `GE_Harden_Active`, `GE_Harden_Perfect`, `bInStoneForm`, ability `GA_Harden_Original_C` | `30-player-look.md` §6.6, `40-player-actions.md` §7.4 |
| block/parry | `GA_ActiveBlock_C` (`WhileInputActive` held state), `GA_Parry_*` | `40-player-actions.md` §7.3/7.5 |

**Read path** [FACT]: the mod already reads enemy health with
`GetHealth/GetMaxHealth/GetCurrentHealthNormalized/IsDeadOrDying` and writes with
`UProperty::ImportText("(BaseValue=..,CurrentValue=..)")` (`setHealthNorm` 1227). The **same
primitives work on the player pawn** — `applyPeerDamage` (3909) already writes the local pawn's
health. So this is a *publish* gap, not a capability gap.
`SetFloatAttributeValue(Attribute, NewValue)` also exists (`40-…-part2` §15 #4) but has **never been
called from Lua** — [UNKNOWN] whether a `GameplayAttribute` struct can be built from Lua; the
`ImportText` route is the proven one (`90-verified.md` §7).

**[PROPOSAL]** new publish fields (2 Hz, this is not a 15 Hz quantity):
`"php":<0..1>` (health), `"prs":<0..1>` (resolve), `"phc":[cur,max]` (heal charges),
`"ppp":<0..1>` (PP resource), `"pdf":0|1` (dark form), `"phd":[0|1,0|1,0|1]`
(harden, blocking, parried/riposte-ready), `"pam":[cur,max]` (sidearm), `"pds":<0|1|2>` (death state).
Receiver: (a) a floating health bar / nameplate over the puppet if you build UI (none exists today),
(b) `LOOK`-style local visual for harden (`GE_Harden_*` cannot be applied without an
`FGameplayEffectSpec` — [UNKNOWN]; fallback: attach the shell's stone material via the existing
`LOOK` material path), (c) **peer death visual**: on `pds>=1` play a death sequence on the puppet
(`/Game/MS1/Characters/DeathsLibrary/death_093_front` is already what the state line carries in
`mseq` — see `out_B.txt`) instead of hiding it.

**Test**: `hpwrite <frac>` on the sender, verdict that the peer's `sync.txt` shows the received
values; then `killself` and check the peer draws a dying body.

**Hazards**: `ImportText` on `Health` while a GAS tick is running is a write outside the ability
system — it works (proven) but does not fire `OnDamageReceived`, so no hit reaction/VFX (G6).

---

### G5 — Player inventory, economy, progression (zero coverage)

[FACT] the whole inventory is `USpartaItemManagerComponent` (`ItemList: FSpartaItemList` =
`FFastArraySerializer`, `AddItemByClass`, `RemoveItemStacksByClass`, `GetItemStacksByClass`,
`GetAllItems`, delegates `OnItemStacksUpdated` / `OnLootPickedUp`) — **`UPlayerInventoryComponent` is
an empty class (0xB8, 0 members)** (`60-items-inventory.md` §1.6/§3.2, `90-verified.md` §9).
Currency is **not a scalar**: each currency is an item definition (`ID_Gold_C`, `ID_Gloom_C`,
`ID_Glimpses_C`, `ID_Ovum_C`, `ID_Tarcore_C`, `ID_Undersap_C`, `ID_Specimen_C`, `ID_TFM_*`…) and the
amount is its stack count (§4.1).

**The gift for this gap** [FACT]: `BP_PlayerController_C` carries a whole **`S_*` API with plain
parameters only** (no tags, no structs) — `60-items-inventory.md` §4.2:

| Function | Params |
|---|---|
| `S_AddGold` / `S_AddGloom` / `S_AddGlimpses` / `S_AddOvums` / `S_AddTarcores` / `S_AddVentrium` / `S_AddLaterite` / `S_AddDorsalite` / `S_AddThoracium` / `S_AddThoraciumPrime` | `Stacks: int` |
| `S_AddShellPoints` | `Delta: int` |
| `S_AddItemQuantity` | `RowName: Name, Amount: int, Item: Class` |
| `S_AddItem` / `S_AddAllItems` | `RowName: Name, Item: Class` / `Item: Class` |
| `S_AddUpgrade` / `S_RemoveUpgrade` | `ID: Name, UpgradeLevel: int` |
| `S_AddWeaponLevel` / `S_AddSidearmLevel` | `Delta: int` |
| `S_AddAllTarstonesMelee` / `…Sidearm` / `…Support` | (none) |
| `S_AddNarrativeTag` / `S_RemoveNarrativeTag` | `TagToAdd: FGameplayTag` |

The mod already proves the analogous pattern works: it calls `AddItemByClass` for chest loot
(`CS.grant` 3649) and currency grants are the same shape.

**Persistent progression objects** [FACT]: `BPSO_Progression_C` (`CurrentLevel 0x1a8`,
`AvailablePoints 0x1ac`, `AssignedPoints 0x1b0`, `BondingPoints 0x200`, `GoldSpentAtShop 0x250`),
`BPSO_ShellMemories_C` (`MemoryProgress 0xb8`, `MemoriesWatched 0x108`), and
`BP_SpartaPlayerSaveObject_C` (`DropItems 0x390`, `ItemPoolState 0x3e0`, `CurrentPrologueCheckpoint
0x4c0`, `TotalDeaths 0x524`, `TotalBossesDefeated 0x528`, `UnlockedLandingAreas 0x550`,
`CleansedLandingAreas 0x5a0`, `CinematicsWatched 0x5b0`, `EarnedAchievements 0x650`,
`NewGamePlus 0x798`, `SkippedPrologue 0x9b8`) with helpers `SetActiveShell/Weapon/Sidearm/
Charges`, `IncrementTotalDeaths`, `AddUnlockedEquipment`, `UpdateCleansedLandingAreas`,
`SetAchievementEarned`, `SetCinematicWatched`, `GetFinishedPrologue` (`10-world-state.md` §5.4).

**Decision needed first** (`20-part2` §10): *which of these is shared?* The dump proves the
containers, not the intent.
* share: keys, locked doors/chests, shop stock, tarforge pool, arena wager, shell memories if you
  want "same memories watched".
* keep per player (my read): `BPSO_Progression` (levels/points), inventory + gold, `PPItems`,
  tarstones, achievements/tutorials/cinematics, `NewGamePlus`.

**[PROPOSAL]** three wire sections, all *delta*-based (never absolutes, so a shared stash cannot
duplicate items):
1. `ig` — item deltas: `["<itemDefPath>",<delta>]`, applied with `AddItemByClass` for +n,
   `RemoveItemStacksByClass` for −n. Source: diff of `GetAllItems()[i].ItemDef` +
   `GetTotalItemCountByDefinition` at 2 Hz (the chest code already does exactly this diff,
   `CS.snapshot` 3533).
2. `pg` — shared progression ints (`["p|<name>",<v>,live]`), covering `BPSO_Tarforge.StoredTarcores`,
   `BPSO_ShopData.TotalGoldSpent`, `BPSO_PlayerSession`-style counters; needs G1's codec.
3. `pi` — per-player info published but not applied (peer's gold/level shown in a HUD/tooltip only).

**Test**: `gv <n>` (grant n gold on A) → B's `sync.txt` shows the received delta and
`S_AddGold` effect; `give <itemPath> <n>`/`take …` for items.

**Hazards**: `RemoveItemStacksByClass` below 0 is undefined — clamp; item definitions are class
objects, so send the **path string** and resolve with `StaticFindObject` (the mod already does this
for shells/weapons, `findObjectByPath` 279).

---

### G6 — Enemy combat fidelity: drops, hit reactions, montage proof

| Sub-gap | Fact | Proposal | Test |
|---|---|---|---|
| **Drops / loot** | `FSpartaCharacterDeathData{GloomOnDeath 0x0, GoldOnDeath 0x4, GoldDropChance 0x8, ItemDropTable 0x10, Size 0x60}`; `BP_AICharacter_C.CharacterDeathData 0xda8`, `.AdditionalDropItems 0xe08`, `.UseCharacterDataRewards 0xda0`, `SetDeathResources(...)`; boss side `BPC_BossAI_C.VestigeItem 0x238`, `VestigeEffects 0x240`, `ItemStacks 0x250` (`50-enemies-bosses.md` §3.5) | **Roll on the owner only.** The owner (the client whose copy was killed) tells the other side *which* currencies/items dropped and how many; receiver grants them with `S_Add*` / `AddItemByClass` instead of re-rolling. Do **not** try to replicate world pickup actors (no `SpawnPickup` exists — §3.5). | kill an enemy on A, check B's gold/items delta equals A's |
| **Hit reactions / VFX** | one receive hook: `ISpartaHittable::SpartaApplyHit(FHitResult, ASpartaCharacter* Instigator, USpartaHitPayload*, AActor* DamageCauser)`; payload `USpartaHitPayload` **0x158**, 19 setters (`SetHealthDamage`, `SetPoiseDamageOption`, `SetReactionTag`, `SetStrikeDirection`, …); receivers `ASpartaCharacter.OnHitReceived 0x8E8`, `OnHitApplied 0x8D8`, `CharacterHit 0x990` (`50-enemies-bosses.md` §6.1-6.3) | [PROPOSAL] replay the *payload*, not the number: on "peer hit X for Y with tag T", construct a payload via the setters and call `SpartaApplyHit` with the local hit result from a line trace. **Blocked by** [UNKNOWN]: an `FHitResult` cannot be built from Lua — the practical route is `TriggerDamage(...)` on the victim or `PlayAnimMontage(<hit reaction>, 1.0, FName("None"))`. | a hit on A makes B's copy flinch |
| **Enemy animation** | mirror already plays `PlayAnimMontage(mo, 1.0, FName("None"))` (`mirrorDrive` 1815) | — | measured **13 montage replays** in the last run (`sync.txt: montages:13`) but never checked visually |
| **Enemy health reset** | `applyHealth` (1895) only raises a value when `deadTails[tail]` was set | [PROPOSAL] add an authoritative "owner says this enemy is at X" message for the case the local value is *lower* but the owner respawned it (world reset, G9) | after a host death, the boss is full on both sides |
| **Enemy casts / projectiles** | per-family projectile classes (`50-…` §2.4), `USpartaHitTrace` (`CheckOnce`, `BeginContinuousCheck`) | [PROPOSAL] frequency-based: montage + projectile spawn path in the mirror entry; fire the same projectile actor class on the non-owner | a boss's ranged attack shows on the peer |

---

### G7 — Bosses and encounters

[FACT] `BPC_BossAI_C`: `OnBossAggro 0xE8`, `OnBossDeath 0xD8`, `OnSecondPhaseStarted 0x118`,
`OnThirdPhaseStarted 0x260`, `SecondPhaseHealthPercent 0xF8`, `ThirdPhaseHealthPercent 0x100`;
functions `BroadcastFightStartEvent`, `BroadcastFightEndEvent`, `HandleAggro`, `HandleDeaggro`,
`IsAlreadyAggroed`. Encounter machinery: `SpartaEncounterManager.OnHordeEncounterCompleted`,
`OnEnemyDeath`, `EncounterStartDelegate`, `SpartaHordeEncounter.StartEncounter(InSpawnTrigger)`,
`SpartaLevelScript.OnEncounterStarted/Finished` (`50-enemies-bosses.md` §3.1/3.3).

| Gap | Why | Proposal |
|---|---|---|
| Boss phase thresholds | phases are driven by HP %; with monotonic health the *damage* lands, but the phase **abilities** (`GA_*_PhaseChange_C`) run only on the client that owns the boss | publish `["b\|<tail>",phase]` from the `OnSecond/ThirdPhaseStarted` delegate (needs a `RegisterHook`/`NotifyOnNewObject` — [UNKNOWN] whether UE4SS can bind a multicast delegate from Lua; fallback = poll `phase` off HP% and let each side's own boss script fire) |
| Boss arena / fog walls | `BP_BossGate_C` has **no save component**; the gate is opened by `BPSO_BossGate.Unlocked` (already synced) but the *trigger* (`ApproachingGate`) and fog volumes are local | call `ApproachingGate()` on the peer's gate when the owner enters |
| Boss data layer | `BPSO_BossDLManager.CurrentBoss` + `RelevantDL` (data-layer asset pointer) | add to `WS_KINDS`; apply via `SetDataLayerInstanceRuntimeState` (G1 + `wdApply` 3076) |
| Encounter spawners | only `BP_Spawner_Proximity_C` is replayed (`spawnReplay` 3856); `BP_Spawner_C` / `BP_SpawnTrigger_C` are not | [FACT] `BP_AICharacter_C.SpawnTrigger 0xcd0`, `.spawner 0xd30`, `.DespawnEnemy()`, `.ResetSpawnTrigger()`, `.SpawnControllerDeferred()`; `SpartaHordeEncounter.StartEncounter(SpawnTrigger)` is the cleanest entry — [UNKNOWN] whether it works without the player overlap |
| Session spawn state | `BP_SpawnerSessionSaveObject_C` (`WasKilled`, `DidSpawn`) | read it as an additional "already spawned" guard so both sides agree whether an encounter has fired |
| Scripted enemy events | `GA_AllEnemies_Retreat`, `_RetreatAndDespawn`, `_Despawn`, `BroadcastNearbyEvent(EventEffect, Distance, duration)` | one-shot `wx` event (G3) |

**Test**: a `bossphase` command that writes `SecondPhaseHealthPercent`-crossing damage on the owner
and asserts the peer's phase widget changed (`50-…` §3.4 lists the widgets).

---

### G8 — Quests: the log is synced, everything around it is not

Synced [FACT]: narrative tags (add + `-1` removal), `CurrentActiveQuests`, `CompletedQuests`,
`CurrentTrackedQuest`, per-quest `CurrentQuestStage`, `CurrentQuestObjectiveProgress`,
`IsTurnedIn`, `NPCRelationships`, `NarrativeEvents` (`wqScan` 2824, keys `t|c|a|s|o|u|k|r|e`).

Not synced:

| Gap | Where | Evidence |
|---|---|---|
| **Dialogue** | `DialogueSubsystem` (WorldSubsystem) — `ActiveDialogues 0xa0`, `SpartaStartDialogue(Dialogue, Participants, Instigator, AdditionalInterruptTags, AdditionalTags, AdditionalBlockingTags) -> DlgContext*`, `StopDialogue`, `InterruptDialogues`, `EndAllDialoguesWithTags`, delegates `OnDlgStarted/OnDlgEnded/OnDlgNodeEnter/OnDlgNodeExit` | `20-quests-progression.md` §6.1 |
| **Dialogue choices** | `DlgContext` — `ChooseSpeechSequenceOptionFromReplicated(OptionIndex)` (the DlgSystem replication hook!), `OnRep_SerializedParticipants`, `SerializedParticipants` | §6.4 |
| **`PlayerChoice`** | `BPC_QuestLogComponent_C.PlayerChoice 0x1d0` (single global story tag) | §1.3 |
| **Economy counters on the quest log** | `BlisterBoiDonations 0x288`, `ImmortalBrigSpoils 0x290`, `WormfishGold 0x2e0`, `TotalGoldSpentMerchant 0x340`, `MemoryOverride 0x330` | §1.3 |
| **Live quest objects beyond stage/counter** | `QuestObject.CurrentStageDetails`, `QuestDetails`, `GetQuestDetails()` takes no params → stage count unknown from Lua | `HANDOFF.md` §16 |
| **NPC-side quest state** | `BPSO_NPC_C`, quest-giver components | `10-world-state.md` §5.1, `20-…` §1.5 |
| **Tutorials / achievements / cinematics-watched** | `BP_SpartaPlayerSaveObject_C.ShownTutorials`, `.EarnedAchievements 0x650`, `.CinematicsWatched 0x5b0` | `20-quests-progression.md` §10; `10-world-state.md` §5.4 |
| **Areas / run completion** | `SpartaAreaSubsystem` (WorldSubsystem) and `SpartaRunManagerSubsystem` (GameInstanceSubsystem) have **no save object at all** | `20-part2` §10 |

**[PROPOSAL] dialogue is a host-driven state machine:** host publishes `["dlg",<dialoguePath>,<nodeId>,<seq>]` +
`["dlgopt",<seq>,<optionIndex>]`; guest calls `SpartaStartDialogue` with its own participant actor
(name-matched NPC + puppet? [UNKNOWN] whether the puppet can be a participant) and then
`ChooseSpeechSequenceOptionFromReplicated(idx)`. Audio is per client (`OnDlgAudioStarted`), so it
must be muted on the non-owner to avoid double voice. Fallback if the participant list can't be
faked: **audio-only** sync — guest just shows the subtitle widget. Cheapest v1: block local dialogue
starts on the guest while the host has one active (`IsDialogueCurrentlyActive`).

**Test**: driving real dialogue needs player input; the lab can call `SpartaStartDialogue` directly
on a `DlgDialogue` asset path — a lab probe first, verdict second.

---

### G9 — Death, world reset, checkpoints, fast travel, level instances (the big flow gaps)

| Gap | Fact | Consequence today | Proposal |
|---|---|---|---|
| **Death / world reset** | player death → `SpartaHealthComponent:StartDeath()` → `GA_Death_Player_C:HandlePlayerDeath(GrantDeathState)` → `WBP_DeathScreen_C` (`OnDeathSelection`, `ResetWorld`, `CreateLoadingScreen`) → `SpartaWorldGenSubsystem.BeginWorldReset()` (`70-flow-menus.md` §3.2/3.5) | each client resets **its own** world: enemies, lift, chests, boss state; the other side keeps playing in a world that no longer matches. `applyHealth` can only lower values ⇒ a reset does not propagate | publish `["reset",<seq>]` on `BeginWorldReset`; receiver runs its own `BeginWorldReset()` (or, safer for a client that is mid-fight: re-apply store A/B + kill all mirrored enemies) |
| **Respawn point** | `BPSO_PlayerSession_C.StartTransform 0xf0` (`GetSessionStartTransform()`, `SaveHealth/…`), `BP_SpartaPlayerSaveObject_C.CurrentPrologueCheckpoint 0x4c0`; `BP_PrologueCheckpoint_C` is an `ASpartaPlayerStart` with `AreaID`, `GetIsUnlocked()`, `GetDesiredPlayerStartLocation()` | after a respawn the peer's puppet teleports to a different place; nothing announces it | publish `["spawn",<x,y,z>]` on respawn; the puppet already snaps beyond `TELEPORT_DIST` = 900 uu (`main.lua:70`) |
| **Co-op revive** | `BPSO_ShellRevive_C.StationUsed`, `GA_ShellReviveBase_C` (`ShellReviveStart/Initialize/Complete/FX/Recall`, `ReviveShell`), `UPlayerAttributeSet.ShellReviveCurrentProgress 0x150`; `BPC_Player_Health_C:ReviveShell` — [FACT] single-player only, "co-op revive has to be built on top" (`40-…-part2` §10.2) | no way to revive a friend | v1: publish peer death (`pds`, G4) + let the survivor refill the peer's meter by granting `ShellReviveCurrentProgress` (ImportText) — [PROPOSAL, needs a lab round] |
| **Fast travel / landing areas** | `GA_Traversal_FastTravel_C` (`OnPreFastTravelNotify`, `OnFastTravelNotify`, `OnFadeToBlack`), `BP_FastTravelPoint_C` (`DisplayName 0x2f8`, `CanFastTravel`, `EligibleForFastTravel`), `ASpartaPlayerStart.EligibleForFastTravel/CanFastTravel`, unlock state in `BP_SpartaPlayerSaveObject.UnlockedLandingAreas 0x550`; **`CanFastTravel` was `false` in the test save** (`HANDOFF.md` §18) | the two players fast-travel independently to places the other is not | publish `["ft",<landingAreaName>,seq]` and drive the peer through `BPFL_WorldStreaming:TeleportPlayerWithStreaming(Location, Rotation, ControlRotation, ScreenTransitionClass, TransitionZOrder, FadeIn, FadeOut, OptionalZoneData, WorldContext)` (`20-part2` §14.1) — this is *the* game-wide teleport-with-streaming entry point; `GA_TeleportPlayerWithStreamingSupport` wraps it, and `GlobalEvent_TeleportPlayerWithStreaming` carries the payload struct `Location 0x28 / Rotation 0x40 / ControlRotation 0x58`. Note both are **method-style on a BlueprintFunctionLibrary with 0 instances** (`90-verified.md` §10) ⇒ call the **CDO** (`/Game/Sparta/Core/Utility/BPFL_WorldStreaming.Default__BPFL_WorldStreaming_C`), the same trick the mod already uses for the native helper (`wpHelper` 2696) |
| **Level instances (dungeons)** | `BP_LevelInstanceSelector_C` (`LoadLevel`, `UnloadLevel`, `Level: TSoftObjectPtr<World>`, `SpartaSave 0x2e0`), `BP_DungeonEventSelector_C` (`Event 0x320`, `OnLevelInstanceLoaded 0x340`), `BP_LevelInstanceSelector_RestSite_C`; 371 map packages | **the two players can be in different maps**; nothing in the mod checks this, and every store-A/B/quest key is a per-map actor name ⇒ silently zero sync (§2.2) | v1: publish a **zone id** (`soft-object path of the loaded World` + `SpartaAreaSubsystem` area) at 0.5 Hz and show it in `sync.txt`; v2: on mismatch, tell the guest to load the host's level instance (`SelectLevel/LoadLevel`) or fall back to "far apart" mode (streaming around the puppet, `streamTick` 3755) |
| **Difficulty** | `SpartaWorldGenSubsystem.SetDifficulty(BiomeTag, NewDifficulty)` / `IncreaseDifficulty(BiomeTag, IncreaseBy)` / `GetDifficulty(BiomeTag)` (`70-…` §3.5) | different biomes can have different difficulty on each side | host-authoritative int per biome tag (needs tag construction — see §4.1) |

---

### G10 — Cinematics, menus, pause, session flow

| Gap | Fact | Proposal |
|---|---|---|
| Cinematics | `BP_CinematicPlayer_C` (`LevelSequenceToPlay 0x2c8`, `CutsceneActor 0x2c0`, `CinematicEndReached 0x2e8`, `PlayCutscene()` / `EndCutscene()` / `ForceStopCinematic()` — **no params**), 52 `LevelSequence` assets; global events `GlobalEvent_Cinematic_Start_C` / `_End_C` | host publishes `["cine","<sequencePath>",seq]`; guest calls `PlayCutscene()` on its own `BP_CinematicPlayer_C` **after** teleporting the camera/locking input, or simply fades its screen and pauses its player. `BPSO_CinematicDLUpdate.CinematicFired` is already synced, so the *world effect* is covered; only the video is not |
| Pause / menu | pause is a parameter on the UI-input helpers, no `SetPause` wrapper (`70-…` §2.1); `GlobalEvent_OnMenuOpen_C` / `_OnMenuClose_C` exist | one player's pause must not freeze the other (it does not — single-player pause is local) but the *peer's* body should look idle and the HUD should say "friend is in a menu": the `rdy` gate already hides the body, so this is mostly cosmetic |
| Session start / join | `WBP_MainMenu_C:ContinueGame/LoadGame/StartNewGame/LoadMap(LoadedIndex)` (`70-…` §1.7); `SpartaSaveSubSystem.HasValidSessionSave()`, `HasValidSaveForProfileSlot(Slot)`, `GetActiveSaveProfileSlotIndex()` | [PROPOSAL] a real handshake: publish save-slot index + game build + mod version; refuse to sync on mismatch instead of half-syncing |
| Credits / completion | `BPFL_UI_C:PlayCredits()`, `WBP_Credits_C`, `ReturnFromEndStranding`, `BPSO_FC_FinalBossManager.EndStrandingCompleted` (synced ✅) | publish `["done",seq]` so both see the ending |

---

### G11 — Persistence and divergence control

[FACT] The mod **never** calls `SaveToDisk` / `SetPreventFurtherSaves` and never binds
`OnSaveLoaded` (grep: 0 hits each). Every write it makes lives in the in-memory save object and is
persisted whenever the game autosaves — at different moments on each side.

**Consequences**: (a) if a player quits right after a synced change, the other side has it and he
does not; (b) both saves accumulate a *union* only for covered keys — every gap in G1/G2/G5 stays
divergent forever; (c) `WorldState_Backup_0_*.sav` files are created by the game and can end up in
the test installs (`HANDOFF.md` §20/§12).

**[PROPOSAL]** (cheap, low risk): after applying a peer-sourced change, call
`SpartaSaveSubSystem:SaveToDisk(emptyTagContainer, false, true)` (async) through a rate limiter
(≤1 per 30 s); and on `OnSaveGameLoaded` publish a **state hash** per store so a divergence can be
detected instead of silently living on. Do **not** call `SetPreventFurtherSaves` unless you own the
save moment deliberately.

**Test**: apply a peer change, quit both clients, relaunch, compare `ws_ledger` + store A/B.

---

### G12 — Transport, session and scaling

[FACT] `cooprelay.py`: one latest-value line each way (`StateDir.read_out` skips unchanged lines),
`TICK = 1/30`, UDP (`sendto`, 65 535 receive buffer), `file`, `steam` (`steamlink.py`,
`ISteamNetworkingMessages` — **never exercised between two accounts**). No handshake, no version, no
ack, no reconnect, no more than 2 players (`ME` defaults to `"A"`, `playerid.txt` decides).
Steam bring-up only: lobby created/entered (`QA.md` §4).

| Item | Fact | Proposal |
|---|---|---|
| Payload size | measured **4 157 B** at 15 Hz ≈ 60 KB/s; > MTU | split: **hot** (transform/anim/weapons, 15-30 Hz, < 500 B) and **warm** (look/world/progress, 2 Hz, compressed/diffed). Or move to a length-prefixed binary/serialized-stream protocol — [FACT] the relay already owns the socket so this is a relay+mod change only |
| Loss tolerance | latest-line-wins + rotating windows is *designed* for loss, but a >MTU datagram dies as a whole | keep windows (they already recover), and cap the hot packet |
| Reconnect | none | add `sq` gap detection + a `hello`/`welcome` exchange carrying version, save slot, zone id |
| >2 players | not possible | ids from `playerid.txt` → a list; authority model (`DESIGN.md`) is host-authoritative, so N guests is mostly a fan-out change in the relay |
| Steam message leg | unproven | the one remaining transport experiment: two machines, two accounts, `ms2-coop\run-steam-smoke.ps1` + a real session |
| Debuggability | `probe.log`, `sync.txt`, `relay_<id>.log` | keep: every new subsystem must add a `sync.txt` counter (the existing style, `writeSyncStatus` 3968) |

---

## 4. Cross-cutting constraints that shape every item above

### 4.1 `FGameplayTag` — **correct the earlier claim** [FACT]

`README.md` open question #2 says a tag cannot be built from Lua. That is **partly wrong** and the
mod already disproves it: `23` call sites pass `{ TagName = FName(id) }` as an `FGameplayTag` and
`AddNarrativeTag` / `RemoveNarrativeTag` were **verified both ways in game** (`main.lua:3029-3030`,
`HANDOFF.md` §12). What is *actually* blocked:

* `FGameplayTagContainer` construction (multi-tag containers — needed by `EndAllDialoguesWithTags`,
  `ResetForNewGamePlus`, `SaveToDisk(SaveEvents, …)`) is **unproven**;
* BlueprintFunctionLibrary calls need an instance, and `FindAllOf("BPFL_Player_Equipment_C")` returns
  **0** objects (`90-verified.md` §10). **[PROPOSAL]** call the **CDO**
  (`/Game/Sparta/Core/Player/BPFL_Player_Equipment.Default__BPFL_Player_Equipment_C`) exactly like
  the mod already calls `/Script/Sparta.Default__SpartaWorldStateHelper` (`wpHelper` 2696). If that
  works, `ChangeShell(ShellId, Save, WorldContext)`, `ChangeShellSkin`, `ChangeClothing`,
  `ChangeMask`, `ChangeDarkForm` become available — which would let you sync the peer's *actual*
  equipment instead of only its visual (G4/G5). **This is the highest-value 20-minute experiment in
  this document.**

### 4.2 Opaque values [FACT]
`FName`, `FText`, `FGuid` and some bools come back as `<userdata>` in this UE4SS build
(`90-verified.md` §13): `AreaName` (checkpoint), `LocationName`/`Category` (teleports),
`DoorIsOpenName`, `Guid`, `BeaconRisen`. Workaround proven in the mod: read `GetFName()` and convert
with `ToString()` (`wpStr` 2628), or read the *string* copy (`ActorObjectMapAsString`) — for
`FGuid` there is no proven formatter.

### 4.3 Proven-good primitives (reuse, do not reinvent)

`StaticFindObject(path)`, `FindAllOf(cls)` / `FindFirstOf(cls)`, `UEHelpers.GetWorld()`,
`UEHelpers.GetPlayerController()`, CDO call on a native class default, `FName(str)`,
`{TagName=FName(x)}` struct literal, out-params via `out.OutValue`, `TMap:ForEach(function(k,v) …
k:get() … v:get() end)`, `TArray:GetArrayNum()` + `arr[i]`, `UProperty::ImportText`,
`SetActorHiddenInGame`, `SetActorEnableCollision`, `AddComponentByClass`, `SetLeaderPoseComponent`,
`K2_AttachToComponent`, `PlayAnimMontage(montage, rate, FName("None"))`,
`SpartaHealthComponent:{GetHealth,GetMaxHealth,GetCurrentHealthNormalized,IsDeadOrDying,StartDeath}`,
`AddItemByClass`, `SpartaWorldStateHelper.{Write,Read}An{Int,Bool}`, `DataLayerManager:{Get,Set}DataLayerInstanceRuntimeState`,
`BP_HubLift_C:ActivateLift()`, `BP_HubDoor_C:OpenDoor()`, `BP_*_Pickup_C:PickUp()`,
`BP_QuestObject_C:OnObjectiveIDHeard(FName)`, `BPC_QuestLogComponent_C:{AddNewQuest,CompleteQuest,TurnInQuest,AddNarrativeTag,RemoveNarrativeTag}`,
console `summon <class>`.

### 4.4 Proven-crash list (do not retry these) [FACT]

1. `SetMovementMode(1)` + `AddMovementInput` on a controller-less pawn → crash in the movement tick (3/3).
2. `SetSkeletalMeshAsset` while the AnimBP instance is alive → `memcpy` crash. Clear
   `SetAnimInstanceClass(nil)` first.
3. `PlayAnimation()` alone (hung a client twice) — use `SetAnimationMode(1,true)` → `SetAnimation` → `Play(true)`.
4. `GetBoneTransform` on `SpartaSkeletalMeshComponent` → freezes the Lua thread for the session.
5. `LoadAsset` from a loop / via `ExecuteInGameThread` → killed a client (and crashed later in 5/7 runs); the mod keeps it off (`LOAD_ON.txt`).
6. `SpartaSaveSubSystem.SaveObjects:ForEach` → hard crash (this is why the ledger exists).
7. All loops on the game thread (`GAME_THREAD.txt`) → crashed both clients.
8. `FindAllOf("Actor")` while cells stream → froze the Lua thread.
9. Calling `BndEvt__…_OnInteraction` with nil args → crash.
10. `OnChestOpen()`, `PlayChestOpenAnim()`, `OnInteract(handler, nil)` → no effect (use `PlayAnimation(ChestMontage)`).
11. `UpdateTargetFloor()` + `MoveLift()` alone → no effect (use `ActivateLift()`).
12. `summon <enemyClass>`, `BeginSpawningEnemy` → crash; `SpawnMe(nil)` → nothing.
13. `LoadChestState()` on a live chest → does not change `TimesUsed`/lid.
14. Iterating `SpartaSaveSubSystem.SaveObjects` (any form) → crash; `/Engine/Transient` copies have no owner.

### 4.5 Instrumentation rules [FACT]

* `pcall` swallows Lua errors: every step must log (`step()` 4004 logs `ERR <step>: …`).
* `os.clock()` is **CPU time** — use `GetRealTimeSeconds` / `os.time` (`HANDOFF.md` §10).
* Two UE4SS wrappers of one UObject do **not** compare equal — compare `GetFullName()`
  (`sameObj` 957; this bug cost round 13 a run).
* An invalid UObject returns `TrivialObject` for any member — check `IsValid()` first (`isLive` 135).
* Elements of `K2_GetComponentsByClass` are `RemoteUnrealParam` — unwrap with `:get()` (`unwrap` 683).

---

## 5. How to verify each new feature (extend the existing harness)

The harness pattern that already works, per feature:

1. a `cmd` hook in `runCommands()` (`main.lua:2080`), e.g. `mytest <args>`;
2. a `result_<feature>.txt` verdict written by the **receiving** client
   (`writeFile("result_x.txt", "PASS …")`);
3. a `sync.txt` counter line for live debugging (`writeSyncStatus` 3968);
4. a `Wait-File` + `Write-Host "=== X: …"` block in `run-two.ps1` (see 305-321) and the file added
   to `$pass`'s AND-list (line ~400);
5. a kill switch `<X>_OFF.txt` read once at load.

**Fix the fixtures first.** Two of the 21 verdicts are permanently red because their fixture state
is baked into the shared test save. `SpartaSaveSubSystem:ResetSaveObjectByGuid(GuidString)` (callable
from Lua — `10-world-state.md` §10.2 #9) plus `ResetSaveObjectByClass(SaveObjectClass)` are the
clean route: reset `BPSO_*` state at test start so `WORLD`, `LIFT`, `CHEST` are re-runnable. That is
worth doing **before** adding ten new verdicts on top of a red baseline.

**Suggested new verdict names** (one per gap): `STORE_A2` (G1), `STORE_BF` (G2), `BREAK` (G3),
`PEERHP` (G4), `INV` (G5), `DROPS` (G6), `BOSSPHASE` (G7), `DIALOG` (G8), `RESET`/`ZONE`/`TRAVEL`
(G9), `CINE` (G10), `PERSIST` (G11).

---

## 6. Suggested order of work (dependencies respected)

| Phase | Content | Why here | Rough size |
|---|---|---|---|
| **0** | Fix the round-13 guest crash (`HANDOFF.md` §20: `ProcessEvent` on a freed object from the async thread, around data-layer switch / far travel / death reload). The ideas recorded: log the step label to a ring file flushed per call; get the Lua traceback from UE4SS's crash handler; guard `readyTick`'s `widgetOnScreen`/`streamSources` and the `wd`/`ws` scans during a switch | nothing else can be *verified* on a crashing client | 1–2 lab rounds |
| **1** | Harness hygiene: `ResetSaveObjectByGuid` fixtures so `WORLD`/`LIFT`/`CHEST` are green again; add `ZONE` (G9 v1) because it explains every "nothing synced" bug report | a red baseline cannot measure new work | half a round |
| **2** | G4 peer combat state (health/resolve/charges/darkform/harden/ammo/death) + peer death visual + `pds` | highest player-visible payoff per byte, and it unblocks G9 revive | 1 round |
| **3** | G1 field-only additions (12 cheap table entries) + `ResetSaveObjectByGuid`-backed tests; then G1b codec for ints/arrays (keys, locked doors/chests, moving objects, elevators) | mostly table edits + one parser addition | 1–2 rounds |
| **4** | The CDO experiment (§4.1) → real equipment sync (shell/skin/mask/clothing) if it works | converts the current visual-only look sync into state | half a round |
| **5** | G6 drops (owner-rolls + grant) and G7 boss phases/DL + encounter spawners | the "boss fight" experience | 1–2 rounds |
| **6** | G9 flow: world reset, respawn point, fast travel via `BPFL_WorldStreaming`, zone/level-instance agreement, then revive | makes "start to finish" possible at all | 2–3 rounds |
| **7** | G5 inventory/economy deltas + G8 dialogue + G10 cinematics | the remaining "story co-op" layer | 2 rounds |
| **8** | G11 persistence/divergence + G12 transport split (hot/warm), Steam leg, 3+ players, reconnect | hardening for real users, two machines | 2 rounds |

**Definition of done for "full sync, start to finish"** — all of: every store-A class watched
(G1) *or* explicitly excluded with a written reason; store B fully read (G2); peer combat state and
death visible (G4); drops and boss phases shared (G6/G7); dialogue, cinematics and fast travel
host-driven (G8–G10); the same zone guaranteed (G9); saves converge across a quit/relaunch (G11);
two machines over Steam with no crash for 60 min (G12); and the friend package rebuilt from that
tree (`package\build-package.ps1` — note: `dist\ms2-coop-20261022-1557.zip` currently carries a
15:51 `main.lua` (162 652 B) while the source is the 17:27 build (186 015 B), and
`package\README.md` (04:34) still describes round-2 state — rebuild both).

---

## 7. Open questions with the experiment that settles each

| # | Question | Experiment | Blocks |
|---|---|---|---|
| 1 | Can a `FGameplayTagContainer` be built from Lua? | pass `{GameplayTags={…}}` / `{}` to `SaveToDisk` or `EndAllDialoguesWithTags` in a lab probe | G10 (dialogue end), G11 (save events) |
| 2 | Can a BlueprintFunctionLibrary **CDO** be called (tag-driven equipment APIs)? | `StaticFindObject("/Game/Sparta/Core/Player/BPFL_Player_Equipment.Default__BPFL_Player_Equipment_C"):ChangeShell({TagName=FName("…")}, false, pawn)` in the lab | G4/G5 real equipment sync |
| 3 | Can `SetFloatAttributeValue(Attribute, NewValue)` be called from Lua (needs a `GameplayAttribute`)? | lab probe on the player's ASC | nicer G4 writes (no `ImportText`) |
| 4 | Can a multicast delegate be bound from Lua (`RegisterHook`/`NotifyOnNewObject`) so boss phases / `OnDamageReceived` fire instead of polling? | hook `BPC_BossAI_C.OnSecondPhaseStarted` in a lab run | G6/G7 quality |
| 5 | Can `SpartaApplyHit` be called with a Lua-built payload (and where does the `FHitResult` come from)? | lab: payload setters + `TriggerDamage` fallback | G6 hit reactions |
| 6 | Do the content's `ActorFloats`/`ActorNames` maps have any keys at runtime? | one-shot probe reading `GetCurrentWorldStateSave()` | G2 |
| 7 | Does `SpartaHordeEncounter.StartEncounter(SpawnTrigger)` work without a player overlap? | lab probe on a hub encounter | G7 |
| 8 | Is `BPFL_WorldStreaming.TeleportPlayerWithStreaming` callable from the CDO with `OptionalZoneData=nil`? | lab: teleport the player to a known landing area | G9 fast travel |
| 9 | Can the puppet be a valid dialogue participant? | lab: `SpartaStartDialogue` with the puppet in the participant array | G8 |
| 10 | Which of `BPSO_Progression`/`BPSO_ShellMemories`/`BPSO_ShopData` is per world vs per player? | play one session with only one client, then copy the save to the other machine and diff | G5 ownership |

---

## 8. Appendix — quick reference

### 8.1 CoopMod function index (round-13 source, for navigation)

| Area | Functions (line) |
|---|---|
| plumbing | `readFirstLine` 33, `log` 85, `writeFile` 96, `readLine` 103, `safe` 111, `fullname` 117, `isLive` 135 |
| game access | `getWorld` 157, `getPC` 158, `getPawn` 159, `locOf` 166, `dist` 170, `runConsole` 176, `skinnedMeshesOf` 228, `requestGameLoad` 258, `findObjectByPath` 279 |
| puppet | `applyBodyMesh` 328, `ensurePuppetBody` 350, `setupPuppet` 545, `bootTick` 560, `enter` 498, `computeMyBody` 611 |
| anim | `playSeq` 402, `setRate` 424, `playMontage` 431, `updatePuppetAnimation` 454, `segmentsOf` 1458, `ownMontage` 1485 |
| weapons | `localWeaponInfo` 706, `applyPeerWeapons` 797, `clearPeerWeapons` 787 |
| look | `LOOK.hash` 863, `LOOK.capture` 980, `LOOK.tick` 1050, `LOOK.silencePuppet` 1065, `LOOK.apply` 1113, `chainToBody` 964, `compose` 900 |
| combat | `healthCompOf` 1204, `healthNormOf` 1211, `setHealthNorm` 1227, `startDeath` 1243, `sampleCombat` 1377, `killTail` 1397, `applyHealth` 1895, `applyDead` 1926 |
| enemies | `collectNPCs` 1265, `applyNPCs` 1838, `mirrorTake` 1779, `mirrorRelease` 1801, `mirrorDrive` 1815, `spawnerScan` 3822, `spawnReplay` 3856, `aiTargetPeer` 3879, `puppetDamage` 3898, `applyPeerDamage` 3909 |
| readiness | `widgetOnScreen` 1514, `readyTick` 1526, `streamSources` 516, `streamSetup` 531, `streamTick` 3755, `actorsNear` 3744 |
| world state A | `wsHash` 2262, `wsScan` 2411, `wsBuildPayload` 2455, `wsReadPeer` 2471, `wsOwnerActor` 2484, `wsApplyOne` 2491, `wsApply` 2524, `wsTick` 2556, `wsTest` 2570, `WSL.*` 2307-2410 |
| store B / quests / layers | `wpPut` 2636, `wpPrime` 2648, `wbScan` 2704, `wbApply` 2755, `wqScan` 2824, `wqApplyQuest` 2901, `wqApply` 2991, `wdScan` 3059, `wdApply` 3076, `wpTest` 3110, `qsTest` 3159, `qs2Test` 3210, `wpTick` 3310 |
| lifts / chests | `LF.scan` 3366, `LF.apply` 3397, `LF.build` 3446, `liftTick` 3481, `CS.im` 3518, `CS.snapshot` 3533, `CS.scan` 3565, `CS.grant` 3649, `chestTick` 3722 |
| loops / status | `writeSyncStatus` 3968, `localHold` 4003, `step` 4004, `onGame` 4045, `runLoop` 4055 |

### 8.2 Numbers to keep in mind

| Fact | Value |
|---|---|
| save-object classes (Blueprint) | **51** (46 `BPSO_*`, 3 `BP_SO_*`, 2 other) — 27 watched |
| classes owning a `USpartaSaveComponent` | **134** (`10-world-state.md` §7) |
| live save components in one hub zone | **471** (`90-verified.md` §12) |
| `BP_SO_SimpleBool_C` live in the hub | **27** |
| data layers in the hub | **104** |
| `GlobalEvent` subclasses | **348** (14 world-state ones listed in `10-*` §11) |
| item definition classes | **494** |
| `Dmg`/weapon classes / player weapons | 58 weapon classes, 48 meshes |
| attribute sets | 7 (Health 36, Combat 50, Player 32, ShellAbility 32, Sidearm 8, DarkPower 4, Enemy 3) |
| state line size | **4 157 B** (B), 2 852 B (A), 15 Hz |
| mirror caps | 16 enemies / 4 000 uu / dead list 40 / ws window 40 / live 20 s |
| dumps | UE4SS all-assets, Dumper-7 menu/`allassets`/`gameplay`; 78 740 property slots cross-checked, 0 conflicts; 16 040 classes in the all-assets SDK |
| build / exe | `5.6.1-0+++Sparta-Depot+Day1+CL92086`, `BDEDDB33…AAE69` |
| engine offsets (for Dumper-7-style tooling) | GObjects `0xb03aef0`, `FUObjectItem 0x18`, FName::AppendString `0x13F8FA0`, FNamePool `0xAF57380`, `ProcessEvent` index `0x4C`, GWorld `0xAD4E7F0` |

### 8.3 Where to read the proof for each area

| Topic | Report |
|---|---|
| stores A/B/C/D, 134 save owners, world objects by kind, tags | `research\10-world-state.md` |
| quests, dialogue, narrative tags/events, NPC state, tutorials | `research\20-quests-progression.md`, `-part2` |
| shells, skins, masks, clothing, weapons, `USpartaSkeletalMeshComponent` | `research\30-player-look.md` |
| GAS, attribute sets, abilities, parry/block/harden, revives | `research\40-player-actions.md`, `-part2` |
| enemies, bosses, damage path, hit payload, spawners | `research\50-enemies-bosses.md`, `-part2` |
| items, currency, drops, shops, tarforge, upgrades | `research\60-items-inventory.md` |
| menus, death, loading, cinematics, fast travel, completion | `research\70-flow-menus.md` |
| animation catalogue (2 979 sequences / 2 023 montages) | `research\80-anim-catalog*.md` |
| what was verified live (10 objects, before/after) | `research\90-verified.md` |
| dump cross-check + patched Dumper-7 | `research\05-dumper7-vs-ue4ss.md` |
| the ten facts that shape everything | `research\README.md` §0 |
