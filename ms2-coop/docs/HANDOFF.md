# Handoff - Mortal Shell II co-op mod

Snapshot: **2026-10-22 04:40**, end of the second working round.
Everything below is verified on this machine unless marked *(open)*.

Working tree: `G:\Freespace\shell_coop` (game untouched at `E:\Games\Mortal Shell II`,
tests run on copies at `E:\Games\_MS2-coop-test` and `E:\Games\_MS2-coop-b`).

---

## 1. Where the project stands

The mod does this today, in a real two-client session on one PC:

| Behaviour | Evidence |
|---|---|
| Each client shows the other player **wearing their own shell mesh** (incl. after a shell swap) | `SHELL: PASS`, `peerbody=SK_DarkForm_Bulk` on both sides, `bones=164` |
| The peer body is **positioned after the peer** (< 1 uu) and moves smoothly | `A: PASS puppet tracks peer (dist 0.4, 30/30)`, same for B |
| The peer body **animates**: idle -> walk -> sprint, play rate scaled to speed | `puppet locomotion -> walk (playing=true)`, `-> sprint (playing=true)` |
| Near enemies are **mirrored** from host to guest | earlier run `20/20 within 30u`, still green this round |
| **Damage** done on one client lands on the other's copy | guest wrote 0.5 -> `host hp ...=0.500`, `DAMAGE: PASS` |
| An enemy **killed on one client dies on the other**, with the game's own death animation | `killTail ... (peer): startDeath ok=true`, `DEATH: PASS` |
| Peer **weapon**: path + socket are transmitted, drawing it is **not finished** (off by default) | `wpn=.../SK_ProxyWeap` both sides; spawn gated behind `WEAPON_SPAWN.txt` |

Not done: weapon visual, attacks/parry animations of the peer, drops, quest/boss state,
two-machine test, long-session stability, Steam message leg between two accounts.

---

## 2. How to run it

### 2.1 Playing (friend package)

```powershell
# host
G:\Freespace\shell_coop\ms2-coop\package\build-package.ps1      # rebuild the zip
# then on each PC: Install.ps1 -GameDir "<game>", MSCoop.ps1 -Host / -Join -Lobby <id>
```
Latest package zip: `ms2-coop\package\dist\ms2-coop-<stamp>.zip`
(contains `Install.ps1`, `MSCoop.ps1`, `README.md`, UE4SS + MS2 config, `CoopMod`, relay).

### 2.2 Automated two-client test (one PC, background)

```powershell
cd G:\Freespace\shell_coop\ms2-coop
.\deploy.ps1 -GameDir "E:\Games\_MS2-coop-test" -Enable
.\deploy.ps1 -GameDir "E:\Games\_MS2-coop-b"    -Enable
.\run-two.ps1 -GameA "E:\Games\_MS2-coop-test" -GameB "E:\Games\_MS2-coop-b" `
              -BootWait 95 -TimeoutSeconds 220
```
Prints `SHELL / WEAPON / ANIM / DAMAGE / DEATH / A / B / RESULT` and restores the owner's
saves afterwards. Runs ~6 minutes with both windows parked off-screen.

### 2.3 Lab (single client, engine experiments)

```powershell
.\deploy.ps1 -GameDir "E:\Games\_MS2-coop-test"
.\lab.ps1 -Commands "clearanim@2,setasset@6,stat@1,play idle@4,seqstate@2" -Tag x
```
Results (screenshots, `lab.log`, files the lab writes) land in
`G:\Freespace\shell_coop\local\lab\<timestamp>-<tag>\`.

---

## 3. File map

```
ms2-coop/
  mod/CoopMod/Scripts/main.lua        the shipped mod (state sync, body, anim, combat)
  mod/CoopLab/Scripts/main.lua        experiment bench (mesh/anim/health/weapon probes)
  mod/CoopProbe/Scripts/main.lua      first-round probe (kept, unused)
  relay/cooprelay.py                  file / udp / steam relay, file contract
  relay/steamlink.py                  Steamworks via the game's own steam_api64.dll
  deploy.ps1                          copy mods into an install + mods.txt + coop_dir.txt
  lab.ps1                             single client lab runner (screenshots, commands)
  run-two.ps1                         two clients + relays + feature verdicts
  drive.ps1 / run-steam-smoke.ps1     single-client probe runner / Steam lobby smoke
  verify-quiet.ps1                    proves no cmd.exe spam from mod ticks
  package/                            Install.ps1, MSCoop.ps1, build-package.ps1, README
  docs/DESIGN.md                      architecture + why each decision was made
  docs/QA.md                          every run with its log lines and the bugs found
  docs/HANDOFF.md                     this file
```

State directory contract (per install): `<game>\MortalShell2\Binaries\Win64\coop\`
- `out_<id>.txt` mod -> relay, one JSON line, rewritten 15x/s
- `peer_<id>.txt` relay -> mod
- `role.txt` (`host`/`guest`), `playerid.txt` (`A`/`B`), `coop_dir.txt` next to the exe
- `sync.txt` 2 Hz status written by the mod (harness reads this)
- `heartbeat.txt` 1 Hz loop-liveness counters (a dead Lua thread stops this file)
- `result*.txt` verdicts, `probe.log` mod log
- `ANIM_OFF.txt` / `WEAPON_SPAWN.txt` kill switches (see 6)

---

## 4. How the mod works (short version)

Three loops, each step wrapped so one error cannot kill the loop:

| Loop | Job |
|---|---|
| 30 Hz | move the puppet to the peer's reported transform (snap > 400 uu, else 40 %/tick smoothing) |
| 15 Hz | publish own state, read peer state, mirror enemies, combat adopt, judge |
| 2 Hz | boot state machine, command channel, body/weapon setup, health sampling, `sync.txt` |

Message (one line, JSON):

```jsonc
{"t":"p","id":"A","x":..,"y":..,"z":..,"yaw":..,"pitch":..,"anim":"walk","spd":260.0,
 "body":"/Game/.../SK_DarkForm_Bulk.SK_DarkForm_Bulk",     // own shell mesh
 "apart":"/Game/.../ABP_Player.ABP_Player_C",               // own anim BP (informational)
 "wpn":"/Game/Sparta/Weapons/Proxy/SK_ProxyWeap.SK_ProxyWeap","wsock":"<socket>",
 "npc":[["<actorTail>",x,y,z,hp,dead],...],                 // host only: 10 nearest enemies
 "nhp":[["<actorTail>",0.500],...],                         // every client: health it has seen
 "dead":["<actorTail>",...]}
```

Authority: the host owns the enemy list and positions; **combat is monotonic**, not
authoritative - both clients publish the *lowest* normalized health they have seen and adopt
anything lower, so damage from either side accumulates in both worlds and a death (health 0
or `IsDeadOrDying`) is broadcast and triggered with `StartDeath()` on the peer.

Detailed reasoning and every engine fact is in `docs/DESIGN.md`.

---

## 5. Engine facts worth not re-learning (all cost a run or a crash)

1. UE 5.6 shipping build, no game netcode; the only viable route is a UE4SS Lua mod + a
   helper process (Lua has no sockets).
2. The menu widget only gets created if the window is **not minimized**. Park it off-screen
   with `SetWindowPos(-30000,-30000, SWP_NOACTIVATE)`.
3. Launch: `MortalShell2-Win64-Shipping.exe MortalShell2 -windowed -ResX=1024 -ResY=576
   -nosound -nosplash -nocrashreport`, CWD = game root. Exit code 1 without the `MortalShell2`
   argument.
4. `summon /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C` produces a
   `BP_PlayerCharacter_C` actor. Its `SpartaSkeletalMeshComponent` (`CharacterMesh0`) has an
   AnimBP (`ABP_Player_C`) but **no mesh asset** -> invisible body.
5. Swapping the mesh on that component while its AnimBP instance is alive **crashes** in
   `memcpy` during the anim re-init. `SetAnimInstanceClass(nil)` first makes it safe
   (`bones 0 -> 164`).
6. A possession-less pawn sits in `MovementMode=0`; `AddMovementInput` does nothing.
   Setting `SetMovementMode(1)` and feeding input **crashes the game** inside the movement
   tick (3/3). The puppet is therefore moved directly, never walked by the engine.
7. Animation: `PlayAnimation()` alone hung a guest client twice. The explicit sequence is
   stable: `SetAnimationMode(1, true)` -> `SetAnimation(seq)` -> `Play(true)` ->
   `SetPlayRate(x)`. Sequences: `A_Shared_Idle_H`, `..._Walk_Fwd_H2`, `..._Sprint_800_H2`
   under `/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/`.
8. `LoadAsset` only works on the game thread; calling it via `ExecuteInGameThread` from a
   loop killed a client. Use `StaticFindObject` and retry (assets get loaded by the game
   itself).
9. `GetBoneTransform` on the Sparta mesh component froze the UE4SS Lua thread for the rest of
   the session - do not sample bones for telemetry.
10. This UE4SS build has **no working `UStruct:ForEachProperty`**, and method calls need the
    *full* parameter list (`SetAnimationMode(mode, bForceInitAnimScriptInstance)`,
    `BeginDeferredActorSpawnFromClass(world, cls, xf, handling, owner, scaleMethod)`,
    `FinishSpawningActor(actor, xf, ...)` - the last one still needs the third parameter).
11. A Lua error inside a `pcall` is invisible: a single bad `dist()` call froze the whole
    15 Hz sync loop and the only symptom was "the other player never moves". Every loop step
    now calls `step(label, fn, ...)` which logs `ERR <step>: ...`.
12. UTF-8 **BOM** in a Lua file makes UE4SS fail to load the mod (`unexpected symbol near
    '<\xEF>'`). Always write with `UTF8Encoding($false)`.
13. Real saves live in `%LOCALAPPDATA%\MortalShell2\Saved\SaveGames` and ignoring an
    overridden `LOCALAPPDATA` (Windows known-folder API), so tests snapshot/restore the real
    folder. Current hashes match `local/save-backups/backup-20261022-001113/SaveGames`.

Combat surface used: `SpartaHealthComponent` (`GetHealth`, `GetMaxHealth`,
`GetCurrentHealthNormalized`, `IsDeadOrDying`, `StartDeath`) and `SpartaHealthSet.Health`
(`FGameplayAttributeData`, written with `UProperty::ImportText("(BaseValue=..,CurrentValue=..)")`).

---

## 6. Kill switches (keep them working)

| File in the coop dir | Effect |
|---|---|
| `ANIM_OFF.txt` | puppet stays in the rest pose (used to isolate the animation path) |
| `WEAPON_SPAWN.txt` | enables the unfinished peer-weapon spawn (off by default) |

Both are read once at mod load, so create them before launching the game.

---

## 7. Next steps, in the order I would do them

1. **Weapon visual (closest to done).** `FinishSpawningActor` needs its third argument
   (`ESpawnActorScaleMethod`); then verify `K2_AttachToComponent(mesh, socket, 2, 2, 0, false)`
   places it correctly. Test in `lab.ps1` with `weaponinfo` + `spawnprop` before enabling
   `WEAPON_SPAWN.txt` for real play. The peer's weapon path/socket already arrive in the
   state line (`wpn`, `wsock`).
2. **Peer attack/parry visuals.** When the peer publishes `anim:"attack"`, play a one-shot
   attack sequence on the body (same single-node API, `Play(false)`), then return to
   locomotion. Detect attacks on the sender from the player's active montage
   (`Character:GetCurrentMontage()`), not from input.
3. **Real walking instead of sliding.** The engine movement tick crashes for possession-less
   pawns; the alternative is to spawn a *native* actor (`ASkeletalMeshActor` or an
   `ACharacter` subclass with a stock controller) as the body instead of reusing the
   summoned pawn - the pound-for-pound biggest visual win, but it needs a lab round.
4. **Combat fidelity.** Health writes do not play hit reactions on the receiving client.
   Consider applying a hit reaction montage or `SpartaHittable:SpartaApplyHit` with a
   constructed payload when adopting a damage event.
5. **Two-machine test** over the UDP transport, then the **Steam message leg**
   (`ISteamNetworkingMessages` between two accounts; only lobby bring-up is proven).
6. **World/quest state** (`BPSO_*`, `WorldStateTags`) is untouched - big, separate slice.

---

## 8. State of the environment when this handoff was written

- No game, relay or crash-reporter process left running (stale relays were killed).
- Test installs up to date with the repo: `deploy.ps1` run against both.
- Owner saves verified hash-identical to the pre-session backup (see 5.13).
- Crash dumps from the experiments remain in the two test installs
  (`E:\Games\_MS2-coop-*`) and can be deleted freely; nothing was written to `C:` beyond
  Python/pip caches and the harness temp files.
- Useful evidence folders: `local\lab\<stamp>-<tag>\` (screenshots + logs per run) and
  `local\dump-analysis\` (slices of the 66 MB UE4SS object dump: classes, functions,
  attribute sets, animation assets).

---

## 9. Reproducing QA evidence

```powershell
# full feature run (this is the one that produced the table in section 1)
.\run-two.ps1 -GameA "E:\Games\_MS2-coop-test" -GameB "E:\Games\_MS2-coop-b" `
              -BootWait 95 -TimeoutSeconds 220
# per-client evidence files
Get-Content E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\coop\sync.txt
Get-Content E:\Games\_MS2-coop-b\MortalShell2\Binaries\Win64\coop\probe.log -Tail 40
```
`docs/QA.md` lists every run, the exact log lines, and the bugs each run exposed.

---

## 10. Round 3 (2026-10-22 05:20): peer body sync

`run-two-r17`: SHELL / WEAPON / ANIM / DAMAGE / DEATH / A / B all PASS, 0 `ERR` lines.

- `updatePuppetAnimation` was never called: the body slid in its rest pose. Now called from
  the 30 Hz drive with the peer's velocity: idle, walk fwd/back/left/right (H2), sprint.
- Actions: sender resolves `GetCurrentMontage()` to the segment sequence under the play head
  (`mseq/mpos/mrate/mid`), receiver plays it single-node and keeps it in step (seen: kneel).
  Anim instance `RootMotionMode = 1` so root-motion clips do not pull the mesh off the capsule.
- Movement: packets carry `sq` and velocity; receiver extrapolates <= 0.25 s, eases yaw.
- Weapons: the visual is the weapon actor's `SM_Weapon` StaticMeshComponent (`WeaponMesh`
  has no asset). Sent as `wpns` (path, kind, socket, offset); rebuilt with `AddComponentByClass`
  and attached to the same socket. On by default, `WEAPON_OFF.txt` disables.
- Body only drawn while the peer's `sq` advances (stale files / peer in menu -> hidden).
- Facts: `os.clock()` is CPU time (runs fast) - use `GetRealTimeSeconds`. Elements of
  `K2_GetComponentsByClass` are `RemoteUnrealParam`, unwrap with `:get()`. An invalid UObject
  returns `TrivialObject` for any member, check `IsValid()` first. `Get-Process .CommandLine`
  is empty on PS 5.1, so run-two never killed old relays; now uses CIM.

## 11. Round 4 (2026-10-22 10:11): world state sync (doors, pickups, save-object bools)

`run-two` round 3 with the new step: `WORLD: PASS` (host `wstest` opened `BP_HubDoubleDoor_C_..._1808979936`
and picked up `BP_CurrencyBag_Pickup_C_..._1764479806`; guest replayed both, read back true).

- Code: `CoopMod` section "world state sync" (`wsTick` in the 2 Hz loop, `ws` array in the state line,
  `ws=` in `sync.txt`, `wstest` command, `WS_OFF.txt` kill switch, `result_ws.txt` verdict).
- Watched: `BP_HubDoor_C.DoorIsOpen` (actor), `BP_SO_SimpleBool_C.MyBool`, `BPSO_CraftingItemPickup_C.ItemCollected`,
  `BPSO_FlowerChest_C.ItemCollected`, `BPSO_DoorLockedDungeon_C.DoorOpened` (save objects; owner = the actor
  directly under `PersistentLevel`). Key = FNV-1a of the owner name (UAID names are identical on both clients).
- Merge is monotonic (true wins). Flip during the session -> sent as live -> `OpenDoor()` / owner `PickUp()`
  (shared loot: both players get the item). Already true in the peer's save -> flag set, pickup hidden, no item.
  Others: field set only (actor visuals follow on next load). Packets carry recent flips + a rotating 40-key window.
- Facts: `FindAllOf("BP_HubDoor_C")` also returns subclasses. A freshly loaded door reads `DoorIsOpen=true` for
  ~3 s before its save applies, so values are ignored until an object has existed 8 s (`WS_SETTLE`).
  `CoopLab` was still enabled in the test installs and ate `cmd.txt` commands; `deploy.ps1 -Enable` now sets `CoopLab : 0`.
- Not covered yet: store B maps for other actors (chest `TimesUsed`), levers/lifts, `BPSO_*` quest/boss objects, DataLayers.
- Open: `WEAPON` failed once in this run (host had not applied the peer weapon at the one-shot check; passed in the 2 runs before).

## 12. Round 5 (2026-10-22 10:46): chests, quest/boss objects, quest log, data layers

`run-two` all PASS incl. new `PROGRESS: PASS world progression` (host marked a chest opened, dropped and restored a
narrative tag, flipped `DL_AU_PROLOGUE` 2->1->2; guest wrote the chest value, removed + re-added the tag, followed the layer).

- Store A (`WS_KINDS`): now multi-field. Added BossGate, Gate, CloseAndOpenGate, ShepherdSheepGate, TarGate, BearTrap,
  SpawnerSaveObj (enemy permadeath), GrishaGhost, Hutchback, Eridrim, FC_FinalBossManager/CorruptionCore/TarRoots,
  EvilStatue, Ovum, GenessaSensor, GrishaCage, FallingCorpse/Statue, SlayerDungeonChalice, TarGolemLevelScript,
  LandingArea_Boss, CinematicDLUpdate (field set only). Two kinds scanned per tick.
- New `WP` section (world progression), sections `wb` / `wq` / `wd` in the state line as `["name",value,live]`:
  - `wb` store B (`SpartaSaveGame.ActorInts/ActorBools`, iterated with `TMap:ForEach`, FName keys are safe). Ints max,
    bools true-wins, `Sparta_Player` excluded. Written with `Default__SpartaWorldStateHelper:WriteAnInt/WriteABool`
    (ctx = pawn), read back with `ReadAnInt(ctx, owner, name, out)` -> `out.OutValue`. Chest open count = `TimesUsed`.
  - `wq` quest log (`BPC_QuestLogComponent_C` under the PlayerController): narrative tags (`AddNarrativeTag({TagName=FName(t)}, false)`
    verified both ways), active/completed quests (`AddNewQuest` / `CompleteQuest`, untested in game). Removals travel live
    as -1; a locally removed tag is not re-added from the peer's baseline.
  - `wd` data layers, host authoritative: `DataLayerManager:GetDataLayerInstanceRuntimeState(inst)` /
    `SetDataLayerInstanceRuntimeState(inst, state, false)` over `DataLayerInstanceWithAsset` (104 in hub), 10 s cooldown per layer.
  - All sets prime only after 10 s of readable data (quest log and layers fill in after load; without this the guest
    re-applied 8 tags and 7 layers at boot).
- Kill switches: `QS_OFF.txt` (quests), `DL_OFF.txt` (layers); `WS_OFF.txt` still disables everything. Test: `wptest`, verdict `result_wp.txt`.
- Lab: `CoopLab` has `luafile <file>` (runs a Lua file from the coop dir with `log` in scope). Probes in `local/probes/`.
- Facts: `SpartaSaveSubSystem.SaveObjects` (`TMap<FGuid,...>`) - a probe iterating it died (not proven, the lab also died
  once without any probe). Most `BPSO_*` instances are `/Engine/Transient` copies for unloaded actors; only live ones are synced.
  `LoadChestState()` does not change `TimesUsed`/lid on a live chest; `OnChestOpen()` from Lua does nothing.
  `BP_HubLiftLever_C:ActivateLever()` and `BP_HubLift_C:MoveLift()` do nothing without a player interaction.
- Not done: lift/lever live replay, chest lid + loot on the peer, store A for unloaded actors, quest add/complete verified in game.

## 13. Round 6 (2026-10-22 11:02): hub lift live replay

`run-two` all PASS incl. new `LIFT: PASS` (host `lifttest` started `BP_HubLift_C_..._1653632767`; guest replayed it,
its cabin rose -42570 -> floor 1; guest seq stayed 0, so no echo). 0 `ERR` lines on both clients.

- Correction to §12: `BP_HubLift_C:ActivateLift()` **does** move the lift. The earlier probe read the actor root, which
  never moves; the cabin is the `SM_Lift` component (z -42570 floor 0, -10180 floor 1, ~7 s). Lab `lift1` hooks showed the
  chain: `ActivateLift` -> `UpdateTargetFloor` (flips `TargetFloor`) -> `LiftButtonDown` -> `LiftStart` delegate ->
  lever `OnLiftStart`/`CheckTargetFloor`/`ActivateLever` (lever visual) -> `MoveLift` -> `EnableInteraction` at the end.
  `UpdateTargetFloor()`+`MoveLift()` alone do nothing; calling the `BndEvt__..._OnInteraction` event with nil args crashed.
- Code: `CoopMod` section "lift sync" (`LF` table, `liftTick` in `wsTick`, `wl` in the state line as
  `["name",seq,floor,active]`, `lift=` in `sync.txt`, `lifttest` command, `LIFT_OFF.txt`, verdict `result_lift.txt`).
  Detection is the false->true edge of `bLiftActive` (so any trigger counts: lever or cabin button). Receiver calls
  `ActivateLift()` when the peer's seq advances and its own lift rests on the other floor (waits if moving); guest also
  aligns once to the host's resting floor 10 s after load. Its own replay edge is not re-sent.
- Not covered: a real lever pull by a player (detected by the same edge, not exercised), the return trip, and the
  other lift families (`BP_Interaction_LiftCall_C` / `BP_Drawbridge_C` via `TriggerMovingActor`, not loaded in the hub).

## 14. Round 7 (2026-10-22 11:50): chest lid + shared loot, ledger for unloaded areas

`run-two-r7a`: all PASS incl. new `CHEST: PASS` (host `cstest` opened chest `..._1718900240`; guest played the lid to
pos 5.07 and received the stand-in loot `ID_Charges_Default` 1 -> 2). `run-two-r7b` (second session, saves restored):
`LEDGER: A PASS` (host re-applied the door and pickup from its ledger at load); `WORLD` reported nothing there only
because `wstest` found that door/pickup already open (test interaction, not a regression). 0 `ERR` lines.

- Chests (`CS`, section `wc` = `["name",seq,count,done,"path=n;..."]`, `chest=` in `sync.txt`, `cstest`, `CHEST_OFF.txt`,
  verdict `result_chest.txt`): a settled loaded chest whose `TimesUsed` rises without a peer cause is a local open. The
  lid is replayed with `SK_Chest:PlayAnimation(ChestMontage,false)` (runs 5.07 s and stays open); `PlayChestOpenAnim`,
  `OnChestOpen`, `OnInteract(handler,nil)` do nothing from Lua. Loot = the opener's inventory gain over the 8 s after the
  open (item def path -> `GetTotalItemCountByDefinition` delta, from `ItemManager:GetAllItems()[i].ItemDef`); the peer
  adds it with `AddItemByClass`. The store B follow-up (`wbRefreshActor`) also plays/snaps the lid. Unrelated gains in that
  window (a kill) are shared too. Hub chests are all `_Skin` chests (skin unlock, no items); a real player open was not
  exercised, only the recorded effect (`TimesUsed` + store B).
- Ledger (`WSL`, `ws_ledger_<slot>.txt` in the coop dir, `ledger=` in `sync.txt`, `WS_LEDGER_OFF.txt`, verdict
  `result_ledger.txt`): every store A key ever known true (local or from the peer) is persisted and, after load, announced
  and applied when its actor streams in, so changes in areas nobody has loaded arrive later. Engine route ruled out:
  `SpartaSaveSubSystem.SaveObjects:ForEach` hard-crashes (now proven), `SpartaSaveGame.ActorObjectMap` is empty at runtime,
  and the `/Engine/Transient` copies carry no owner/guid. Ledger is dropped when >=10 keys read false and bad > good.
  Store B, quest log and data layers already covered unloaded actors (they are global maps).
- Lab facts: a lab crash with no `UE4SS.log` line right at the first `luafile` can be the probe (RegisterHook on many
  functions crashed once); a crash inside `steam_api64` at boot is environmental, just rerun.

## 15. Round 8 (2026-10-22 12:20): quest add/complete verified in game

`run-two-r8q2`: new `QUEST: PASS quest LocationQuest added then completed (active now=false, wq=4/0)`; PROGRESS, ANIM,
DAMAGE, DEATH, A, B, LEDGER A PASS, 0 `ERR` lines. Overall `RESULT: FAIL` only because `WORLD` found its door/pickup already
open (same as §14); `LIFT`/`CHEST` replayed on the guest (log: lift -> floor 1, chest lid + loot) but their verdict files
were not written inside the harness windows - timing of the harness, not checked further.

- Lab (`local/probes/q2.lua`): `DT_QuestData1` rows = `TestQuest, LocationQuest, AutoQuest, VlasQuest, GorfQuest, TielsQuest`
  (`Default__DataTableFunctionLibrary:GetDataTableRowNames(dt, out)`, elements need `out[i]:get()`). The test save has no quests.
  `AddNewQuest(FName(id))` adds to `CurrentActiveQuests` and spawns a quest object; `CompleteQuest` moves it to
  `CompletedQuests`. `AddNewQuest` accepts any name, even one not in the table.
- Code: `qstest` command (host adds an unused table quest, completes it 12 s later), guest verdict `result_qs.txt`
  (added live, completed live, no longer active), `=== QUEST:` in `run-two.ps1`. Sync logic in `wqApply` was already right.
- Not synced: per-quest stage / objective counters (`QuestObject.CurrentQuestStage`, `CurrentQuestObjectiveProgress`),
  `TurnInQuest`, tracked quest, `NPCRelationships`. A real NPC-given quest was not exercised.


## 16. Round 9 (2026-10-22 12:50): quest stages, counters, turn-in, tracked quest, NPC relationships

`run-two-r9q2`: new `QUEST2: PASS quest state GorfQuest: ... stage s=3, tracked, rel r|Gorf=2, turned in`; QUEST, PROGRESS,
LIFT, CHEST, ANIM, DAMAGE, DEATH, A, B PASS, 0 `ERR`. `RESULT: FAIL` only from `WORLD` (door/pickup already open, as §14).

- Lab (`local/probes/q3.lua`, `q4.lua`): `BP_QuestObject_C:OnObjectiveIDHeard(FName(obj))` raises the counter; once every
  objective of the stage is full the game advances `CurrentQuestStage` on the next frame and clears the counter map (a
  1-stage quest goes to stage 1 = waiting for turn-in). Completed/turned-in quests stay in `ActiveQuests`. `IsTurnedIn` is the
  reliable turn-in flag (`QueryCompletedQuests` out `TurnedIn` reads true even before `TurnInQuest`). `CurrentTrackedQuest`
  write + `TrackQuests()` works. `NPCRelationships` keys have spaces (`Sester Genessa`, `Tavern NPC`, `Gorf`, `Vlas`...), all 0
  in the test save; `TMap:Add(key, v)` works on it, on `NarrativeEvents` and on the progress map. `QuestDetails.Stages` reads 0
  on the object and `GetQuestDetails` takes no params from Lua, so the stage count is unknown.
- Code (`wqScan`/`wqApply` in `CoopMod`, same `wq` section, new key kinds): `s|quest` stage+1, `o|quest|stage|obj` counter,
  `u|quest` turned in, `k|quest` tracked (live both ways, guest follows host baseline), `r|npc` / `e|event` relationship and
  narrative-event maps (live: any value, baseline: max). Peer ahead on a stage -> hear all objectives of the local stage
  (game advances itself, rewards included); still behind 8 s later -> write `CurrentQuestStage` + `InitializeQuest()`.
  Counter behind on the same stage -> `OnObjectiveIDHeard` per missing step, fallback `Add` on the map. Peer turned in ->
  `CompleteQuest` if needed, then `TurnInQuest`. Counters of a stage already left are ignored.
- Test: `qs2test` (host: add `GorfQuest`, track it, Gorf relationship +2, hear its objective at 6 s, push one stage at 12 s,
  complete + turn in at 30 s), verdict `result_qs2.txt`, `=== QUEST2:` in `run-two.ps1`. Guest log showed: heard 1 objective of
  stage 0 -> the game moved it to stage 1 by itself, stage 2 by direct write, then turn-in.
- Not done: a quest given through real NPC dialogue (needs driving a `DlgContext` / player input; not attempted). Direct stage
  write leaves `CurrentStageDetails` as `InitializeQuest` sets it (stage name read empty past the last stage; a real
  multi-stage quest was not available to check). `PlayerChoice`, `BlisterBoiDonations` and the per-player gold maps are not synced.

## 17. Round 10 (2026-10-22 13:20): far-apart players - remote streaming + enemy handover

Full `run-two`: new `STREAM: PASS remote area streamed around peer 91647 uu away: actors 85 -> 672` and
`NPCFAR: PASS host mirrors an enemy owned by the far guest`; SHELL, WEAPON, ANIM, DAMAGE, DEATH, PROGRESS, QUEST, QUEST2,
LIFT, CHEST, LEDGER, A, B PASS, 0 `ERR`. `RESULT: FAIL` only from `WORLD` (door/pickup already open, as §14).
Quick check: `run-two.ps1 ... -StreamOnly` (skips anim..chest, ~4 min, passes on its own).

- Streaming: the puppet is a summoned `BP_PlayerCharacter_C` and already carries the player's two
  `SpartaStreamingSourceComponent`s (`Streaming Source (Blocking)` / `(Non Blocking)`, both enabled after summon).
  `setupPuppet` -> `streamSetup`: Non Blocking on, Blocking off (it would stall this client whenever the peer runs into
  unloaded cells). `streamTick` re-asserts every 10 s. Lab `st2` (`local/probes/st_on.lua`, `st_off.lua`, `st_count.lua`):
  puppet at a landing area 91k away, actors within 15k 24 -> 24 with both off, -> 666 with Non Blocking on. Symmetric: the
  guest keeps the host's area loaded the same way. `STREAM_OFF.txt` disables both sources; `stream=` in `sync.txt`,
  verdict `result_stream.txt` (written once when the peer has been > 30k away for 25 s, PASS if >= 100 actors around it).
- Enemies: `collectNPCs` now takes enemies within `NPC_RADIUS` of the player **or** the puppet, with ownership: host owns
  everything within 4000 of itself or not clearly nearer the guest; guest owns enemies within 4000 of itself, > 4300 from
  the host and 1300 nearer the guest. Both clients publish their owned set in `npc` (max 16) and mirror the peer's set
  (`OWNED_TAILS` skip). When both claim one (their copies diverged), the host yields if the guest's reported position is
  clearly on the guest side (`PEER_CLAIMS`). Health/death sync was already symmetric and now covers enemies near either player.
- `tp` command now hovers the player (`SetMovementMode(5)`) for 6 s before walking: without it a far teleport fell through
  unloaded ground and the game's fall recovery put the guest back in the hub (one run).
- Not covered: enemy AI on the non-owner still runs (position is overwritten, animation is its own); enemies whose spawner
  only fires on the real player's overlap may not exist on the non-owner; a real long-distance walk/fast-travel was not
  exercised (only teleport). Memory/perf cost of keeping two areas loaded was not measured.

## 18. Round 11 (2026-10-22 15:40): enemy spawns, owner-driven enemies, peer hits, long travel, crash fix

Full `run-two`: SPAWN, MIRROR, PEERHIT, AITARGET, STREAM (after a 60 s / 85k travel), NPCFAR, SHELL, WEAPON, ANIM, DAMAGE,
DEATH, PROGRESS, QUEST, QUEST2, LIFT, CHEST, LEDGER, A, B PASS; 0 `ERR`; both clients alive for the whole ~12 min run.
`RESULT: FAIL` only from `WORLD` (door/pickup already open, §14).

- Spawns: spawned enemies are keyed by their spawner (`SP_<spawner tail>`, `SPN_ALIAS`, spawner list re-read every 10 s),
  identical on both clients. A peer-published `SP_` key with no local enemy -> fire that proximity spawner's own trigger
  event `BndEvt__BP_Spawner_Proximity_Sphere_..._ComponentBeginOverlapSignature__DelegateSignature(trigger, OUR pawn,
  pawn capsule, 0, false, {})`: it spawns 20k away from the player (lab sp15). Ruled out: `SpawnMe(nil)` (nothing),
  `StreamingInOverlap()` with the puppet in the trigger (puppet is not a player), `BeginSpawningEnemy` and `summon` of an
  enemy class (both crash). Plain `BP_Spawner_C` (encounter spawners) is not replayed yet. Verdict `result_spawn.txt`.
- Non-owner copies (`MIR`): AI paused (`BrainComponent:StopLogic`, retried until the controller exists), movement mode 0
  (no gravity/braking), owner's position + velocity (anim BP locomotion reads it) + yaw + current montage
  (`PlayAnimMontage`/`StopAnimMontage`) each packet; npc entry is now `["key",x,y,z,hp,dead,vx,vy,vz,yaw,"montage"]`. Handed
  back (`RestartLogic`, old mode) when the peer stops publishing it or it becomes ours. Classes with `NPC` in the path
  (vendors, hub cultists) keep their own AI and only follow position. `MIRROR_OFF.txt` disables. Montage replay is wired
  but no enemy attacked in the test (`montages:0`).
- AI vs the peer: an owned enemy that is already fighting (`TargetSelectionComponent.CurrentTarget` valid) and is nearer the
  peer gets `SetTargetOverride(puppet, 3)` (aitest PASS: current target becomes the puppet). Damage on the puppet is summed,
  the puppet healed, and sent as `"pd"` (cumulative); the peer subtracts it from its own pawn (`StartDeath` at 0).
- Far apart: data-layer sync only applies while players are within 15k (region lighting/art layers follow each player);
  stream verdict uses `IsStreamingCompleted()` of the puppet's source + static mesh count, measured once the peer stopped moving.
  `travel x y z secs` command (straight line, hovering). Perf: host working set 6453 -> 6508 MB with both areas loaded; mean
  frame time near 67 ms / far 49 ms on the host (two clients on one PC, off-screen windows - relative numbers only).
- Crash/stall root cause (guest "Fatal Error"): dump `crash_2026_10_22_14_44_11` = `ProcessEvent` on a freed object from the
  async Lua thread (streaming / data-layer switch freed an actor between cache and call); UE4SS's message box then parked the
  Lua thread. Fixes: `isLive()` (IsValid) before calls on cached actors / health components, 1.5 s pause of actor work after a
  data-layer switch (`WORLD_SETTLE`), `FindAllOf("Actor")` removed. `ExecuteInGameThread` for all loops was tried
  (`GAME_THREAD.txt` opt-in): it crashed both clients (the summon's LoadAsset and the puppet mesh swap from a queued job), so
  it is off by default; commands + boot always stay on the async thread.
- Bug fixed on the way: a sed rename had made `assetPath` global and a new helper overwrote it (weapon list went empty).
- Not covered: encounter (`BP_Spawner_C`/SpawnTrigger) spawns, enemy montage replay seen in a real fight, hit reactions on
  the peer's body, a real fast-travel through the map UI (`CanFastTravel` false in the test save; teleport/travel used).

## 19. Round 12 (2026-10-22 15:57): peer body hidden until the peer is really playing

- Bug: `applyBodyMesh` called `SetActorHiddenInGame(false)` while the drive gate thought the puppet was hidden, so the
  placeholder body (own shell) showed while the peer sat in the menu / loading screen. Now `PUPPET_HIDDEN` is the single
  state; the mesh swap re-applies it and a new puppet starts hidden.
- Sender gate `readyTick` (2 Hz) -> `PLAY_READY`. Becomes true only after 5 s (os.time) of: player pawn possessed by our PC,
  not `MainMenu` controller / `L_MainMenu`, no `WBP_LoadingScreen*` in viewport, move input not ignored, no fade-to-black,
  own streaming sources `IsStreamingCompleted`. Lost only on hard signals (no pawn, menu, loading screen). While not
  ready `sendOwn` writes `{"t":"p","id":..,"sq":..,"rdy":0}`; the peer marks `lastPeer.live=false` and hides at once.
- Why the streaming check: the pawn exists 2-4 s before the loading screen goes up (log: `ready` then `loading screen`).
- run-two: all feature verdicts PASS except WORLD (known, §14); peer body first shown on the other client the same second
  the peer logged `play ready=true`; a respawn (`no player pawn`) hid it and it came back after ready.

## 20. Round 13 (2026-10-22 17:45): full player look sync (masks, clothing, skins) + crash work (STOPPED, open)

What was added (`mod/CoopMod/Scripts/main.lua`, look module before `local hcCache`):
- Masks / clothing / heart are `BP_Attachable_Item_*` actors (`BPC_AttachableItems` on the pawn: `AddItem`, `ChangeMask`,
  `ChangeClothing`; concrete classes `BP_AIM_Frog/BagHead/Mango/FlowerCrown`, `BP_Gragu_Helmet`, `BP_AIC_Robe`), each a
  root + one Static/SkeletalMeshComponent attached to the body at a socket (crown: `Socket_CrownFlower`). Skins are
  material swaps on the body (`SwapShellMaterials`).
- The sender publishes what is actually rendered, field `"lk"` (string, 2 Hz, published after 2 equal samples):
  `bm=<body slot materials>;p=<S|K>|<mesh>|<socket>|loc|quat|scale|<leader 0/1>|<slot materials>;p=...`.
  Covers attachable actors of our pawn + extra mesh components on the pawn. Transforms are composed up the attach chain
  to the body socket (quaternion math in Lua). A MaterialInstanceDynamic is sent as its parent (MID params not carried).
- The receiver diffs parts by string and builds plain engine mesh components on the puppet (same socket, transform,
  materials, `SetLeaderPoseComponent(body)` for skeletal parts that use leader pose or sit at the root), re-applies body
  materials after every body mesh swap. The puppet's own `BPC_AttachableItems` is unsubscribed from global events and
  its attachables hidden (it would otherwise dress the puppet from the LOCAL save).
- Object identity: two UE4SS wrappers of one UObject did NOT compare equal in the attach-chain walk -> compare
  `GetFullName()` (`LOOK.same`). This was why the first runs saw 0 parts.
- Kill switch `LOOK_OFF.txt`. sync.txt: `look, lookparts, peerlook, lookapplied, peerlookparts, lookmissing, gameloads`.
- Test: commands `lookadd <attachable class path>` (calls our own `BPC_AttachableItems:AddItem`) and `lookdel`
  (`RemoveMe` + hide). run-two `LOOK` verdict: B puts on the flower crown, A must apply B's new look with 1 part, then
  follow the removal. **LOOK: PASS in runs 5-8** (A log: `look part on puppet: SM_DarkForm_Bulk_FlowerCrown @Socket_CrownFlower`).
- Game-thread asset loader `requestGameLoad` (one `LoadAsset` per `ExecuteInGameThread`, rate limited) for peer
  assets this client never loaded (other shell / mask / skin). **OFF by default** (`LOAD_ON.txt`): suspected in the
  crashes below, then ruled out (crash also without it), left off anyway. Without it a part whose mesh is not in memory
  here stays missing (`lookmissing`) and is retried every 3 s.
- Not verified: different shells on the two clients (test saves are both DarkForm_Bulk), skins with real material
  swaps, clothing (skeletal leader pose), dark-form attachables, shades/seals as separate visuals (seal items are
  weapons `WP_*Seal_C`, already covered by the weapon path). Visual check by screenshot not done.

Crash work (guest B, `ProcessEvent` on a freed object from the async Lua thread, run-two late phases):
- Added `LOCAL_HOLD` (readyTick hard signal or own `startDeath`): `HOLD_SKIP` steps pause during and 3 s after our own
  world reload. `SETTLE_SKIP` extended (myWeapon, stream, ensureBody, syncBody, look). `travelTick`/TP hold check
  `isLive(pawn)` (a death mid-travel kept moving the freed pawn). `localWeaponInfo` skips dead weapons.
- **B still crashes in most runs** (dumps `E:\Games\_MS2-coop-b\...\Win64\crash_2026_10_22_17_*.dmp`), around a
  data-layer switch / far travel / death reload. **Baseline check: the pre-look build (backup
  `main.lua.before-look`, scratchpad) crashed B the same way (17:42:58)** -> pre-existing, not caused by the look
  code. The current run-two sequence (DEATH, PEERHIT kill, far travel) hits it far more often than round 12 reported.
- Next: find which call it is. Ideas: log the step label before each 2 Hz/30 Hz step into a ring file flushed every
  call (the dump only shows UE4SS frames), or get the Lua traceback via UE4SS's crash handler; readyTick's
  `widgetOnScreen`/`streamSources` and the `wd`/`ws` scans run unguarded during the switch.

State at stop: source = look build (round 13). Both test installs are deployed with it. No new package built (latest is still `ms2-coop-20261022-1557.zip`).
