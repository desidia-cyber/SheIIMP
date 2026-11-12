# R6-SPAWN — SPAWN / MIRROR flaky in run-two (C/D, port 47020)

Status: fix implemented, measured in the lab, and verified with two batches of `run-two.ps1` on C/D —
**SPAWN PASS + MIRROR PASS in 4 of 4 runs that reached the lane**, one of them `RESULT: PASS`; the other
three were stopped from `RESULT: PASS` by the WORLD lane (`coop_world.cpp`, untouched here — see §5/§6).

## 0. TL;DR

- The replay is not broken: every replayed proximity trigger DOES spawn the enemy on the host
  (`AttemptedEnemySpawn=1`, stage "Finish Spawn Enemy" → "Load Weapon", 15/15 fires in one lab session).
- **The game then destroys that enemy ~0.4–1.6 s after it spawns, at FULL health, but only on the client
  whose own player is not near the spawner.** Measured: with the host player 20 800 uu away the enemy is
  gone at t≈1.0 s (`spawngone … lastHp=1.000`); with the host player 1 600 uu away (inside the trigger
  sphere) the same spawner keeps its enemy for the whole run (`overlap=1`, `destroyed=0`, 30 s+).
- The mod only looked for that enemy in its **2 Hz** `SpawnerScan`, so SPAWN/MIRROR depended on a scan
  landing inside a ~1 s window. That is the flakiness: `spawnask/spawnok` was 31/4, 26/11, 18/3, 17/0,
  21/3 across runs of identical behaviour — the code did the right thing every time it looked in time.
- **Fix:** the claim→alias registration is now also driven by a 15 Hz claim watch (`combat::Tick15` →
  `SpawnHotTick`), which only polls the claimed spawners' `SpawnedEnemy`. SPAWN and MIRROR now land
  within ~1 s of the spawn instead of waiting for the next 2 Hz scan.
- The destroy itself is a native/game-internal cleanup: a ProcessEvent hook on `Actor.K2_DestroyActor`
  never fires for those enemies (hook installed, `spawndestroy:` lines: none), so it is not vetoable the
  way the round-5 death screen was. See §6 (open).

## 1. What the harness asks for

`run-two.ps1` (SPAWN/MIRROR lane), C/D, port 47020:

1. B (guest) is teleported onto a proximity spawner: `tp 66992 -125076 -44454`. `tp` is
   `K2_SetActorLocation(sweep=false)`, so the game never raises the overlap a walking player would;
   `SpawnerScan` fires the spawner's own `BndEvt__…ComponentBeginOverlapSignature` event once B has stood
   inside its `PlayerTrigger` sphere for 1.5 s (`spawnerself`), and the enemy spawns on B.
2. B aliases that enemy as `SP_<spawner tail>` and publishes it; A receives the alias as a peer claim.
3. `SpawnReplay` fires the same event on A's own copy of the spawner, with A's pawn.
4. When A's spawner reads back a live `SpawnedEnemy`, `SpawnerScan` writes `result_spawn.txt` (SPAWN) and
   aliases A's copy to the same key; `MirrorTake` then drives that copy from B's published data (MIRROR,
   counter `taken`).

## 2. Instruments added (this workspace only)

`native\src\coop_combat.cpp` — test hooks on the `cmd.txt` channel, and `coop\spawn-lab.ps1` +
`coop\run-lab.ps1` + `coop\run-two-x3.ps1` as the harness around them:

| Command | What it answers |
|---|---|
| `spawndump [tail\|nearest\|all\|claim\|spawned]` | the spawner's own gate properties, one line each: `FinishedSpawn`, `DebugAttemptedEnemySpawn`, `DebugFailedToSpawnEnemy`, `DebugSpawnStage`, `IsSpawnedEnemyDead/Destroyed`, `PendingSpawnedEnemies`, `WaitForStreaming`/`SpawnWhenLoaded`/`DoCustomSpawnWhenLoaded`, `AllowSpawnIfInSafeZone`/`PlayerReqArtifactsToSpawn`/`RequirePath`, `SpawnTransform`, `SpawnedEnemy`/`SpawnedAICharacter`, proximity `CachedClass`/`AssociatedSpawnTriggers`/`SphereRadius`/`IsOverlappingActor`, and the count of characters standing at the spawner |
| `spawnfire <tail\|claim>` | fire that spawner's trigger by hand and watch it |
| `spawncall <tail\|claim> <Fn>` | call one of the spawner's own BP functions (`SpawnMe`, `SpawnMeInternal`, `CanSpawnEnemy`, …) |
| `spawnprobe [secs] [patterns]` | arm the round-5 ProcessEvent probe on spawner-shaped names — the CALL ORDER, not just the state |
| `spawnset <who> <BoolProp> <0\|1>` | write one of the spawner's BP flags (used for the gravity experiment) |

and, always on (bounded, throttled): `spawngate <key> t=… <state>` on each CHANGE of a replayed spawner's
state, `spawngone <key> t=… lastHp=… at (…)` when the enemy it produced disappears.

Visual check (`spawn-lab.ps1` takes screenshots of both clients): `local\lab\20261025-163424-spawn5\A-near-1.png`
and `…-164428-spawn6\A-near-1.png` show the host standing at the spawner area, but the camera faced away from
the brigands in both frames, so the screenshots do NOT prove the enemy visually — the actor-level evidence is
what does: the guest's own `collect` line at the same second reads
`chars=22 near=8 own=5 … nearest=BP_Horde_Brigand_C_2147468499_2147468500 d=455` and the guest is killed by
them and respawns 10 k away (`B@67080,-125186` → `B@59755,-134517` in the next lab step), i.e. the spawned
enemies are real actors that fight, not phantom pointers.

## 3. Root cause
### 3.1 The replay works — the spawner runs, and the enemy appears

Lab run `20261025-162825-spawn4` (A = host, B = guest, A 20 k from the spawner):

```
[16:29:43][A] spawn replay requested for SP_BP_Spawner_Proximity_C_…_1555889259_1555889260 (trigger event ok=true fn=found )
[16:29:44][A] spawngate … t=0.6 cls=BP_Spawner_Proximity_C enemy=BP_Horde_Brigand_C_2147467586_2147467587
             ai=… pending=0 finished=0 attempt=1 failed=0 step=3 dead=0 destroyed=0
             stage="Load Weapon" … xform=67576,-125188,-44761 … dist=20803 … overlap=0
[16:29:44][A] spawngone … t=1.0 enemy=BP_Horde_Brigand_C_2147467586_2147467587 lastHp=1.000
             at (67576 -125188 -44761)
```

The event call returns true, the Blueprint enters its spawn path, creates the Horde Brigand at its own
`SpawnTransform`, loads its weapon, runs its abilities — and then the actor is destroyed **at full health**
and `IsSpawnedEnemyDestroyed` goes to 1, `step` back to 0, `FinishedSpawn` stays 0.

The ProcessEvent probe on the same fire (`20261025-161033-spawn2`) shows the teardown inside the enemy's
own setup, with no spawner function involved at all:

```
PROBE +  2.98s  BndEvt__BP_Spawner_Proximity_Sphere_…ComponentBeginOverlapSignature  on BP_Spawner_Proximity_C_…1555889259…
PROBE +  3.03s  EnemySpawned          on BP_SpawnerSessionSaveObject_C (…1555889259…Save Component)
PROBE +  3.52s  OnWeaponEquip         on WP_Brigand_Horde_Axe_C_2147467408_2147467409…
PROBE +  3.73s  WeaponsSpawned_BB4ADE7241F2DDC35F1D06AE7988197A  on BP_Spawner_Proximity_C_…1555889259…
PROBE +  3.88s  CommitPostSpawn       on BP_Horde_Brigand_C_2147467555_2147467556.GA_…
PROBE +  3.89s  OnWeaponDestroyed     on BP_Horde_Brigand_C_2147467555_2147467556.WeaponsComponent
PROBE +  3.89s  SpawnedEnemyDestroyed on BP_Spawner_Proximity_C_…1555889259…
```

Function inventory of that probe session: 16 overlap events, 15 `EnemySpawned`, 15 `WeaponsSpawned`,
15 `SpawnedEnemyDestroyed` — **every** replayed spawn ended in a destroy, and `HandleWorldStreaming`,
`TryDespawnEnemy`, `SpawnMeInternal`, `CustomSpawnMe`, `SpawnerStep` never ran.

### 3.2 It is the missing local player, not the replay path

Lab run `20261025-163424-spawn5`, same build. Far phase (A 20 800 uu away): every enemy gone at t = 1.0 s,
`lastHp=1.000`. Then A was teleported to 1 600 uu from the spawner (inside its 4 791 uu sphere) and the
same spawners fired again — from the `spawndump` timeline:

```
[16:36:39][A] … t=28.1 enemy=BP_Horde_Brigand_C_2147461055_2147461056 destroyed=0 stage="Begin Spawn Enemy" overlap=1
[16:36:41][A] … t=29.1 enemy=BP_Horde_Brigand_C_2147461055_2147461056 destroyed=0 stage="Finish Spawn Enemy" overlap=1
[16:36:43][A] … t=33.6 enemy=BP_Horde_Brigand_C_2147461055_2147461056 destroyed=0 stage="Load Weapon"       overlap=1
```

…and no `spawngone` line for any of those keys for the rest of the run. So the enemy survives indefinitely
when a local player is near the spawner and is cleaned up within about a second when none is — this is the
game's own rule for a spawner whose area no player is in, not anything the mod does:

- it is not a death: `lastHp=1.000` at the last read (and `Spawngone` prints the position unchanged from
  `SpawnTransform`, i.e. it did not fall away first);
- it is not the spawner's Blueprint: no spawner/despawn function is called;
- it is not BP-callable destroy: `Actor.K2_DestroyActor` was hooked (log: `spawndestroy: hook on
  Actor.K2_DestroyActor installed`) and never fired for these actors — the destroy is native;
- the spawner's own streaming flag is not the gate either: `WaitForStreaming=0`, `SpawnWhenLoaded=0`,
  `DoCustomSpawnWhenLoaded=1` on BOTH clients, and `bSpawnEnemyWithoutGravity=1` (lab spawn5, `spawnset`)
  did not keep it alive.

### 3.3 Why that made the VERDICTS flaky

The enemy existed for ~0.4–1.6 s; `SpawnerScan` ran every 2 s (`if (mono - g_spawnScanAt < 2.0) return`).
So SPAWN passed only when a scan happened to land inside that window, and MIRROR could only follow SPAWN
(a take needs the alias). The same build therefore produced, on the same C/D pair:

| run | `spawnask` / `spawnok` | SPAWN | MIRROR |
|---|---|---|---|
| `run-two-fixall-1` | 31 / 4 | PASS | FAIL (`taken` came 31 s later) |
| `run-two-fixall-2` | 31 / 4 | empty (30 s) | FAIL |
| `run-two-r5-merged-cd` | 26 / 11 | PASS | PASS |
| lab spawn2 (60 s) | 21 / **0** | would be empty | would FAIL |
| lab spawn3 | 18 / 3 | would PASS | `taken:3` |
| lab spawn4 | 18 / 3 | PASS | `taken:3` |
| lab spawn6 (+15 Hz watch) | 17 / 5 by t+19 s, `taken:14`, `driven:1` | PASS | PASS |

## 4. The fix

`native\src\coop_combat.cpp`:

1. The alias/claim registration that used to live inline in `SpawnerScan` is now `ClaimEnemyAppeared(spawnerTail, enemy)`:
   it sets `spawnAlias[enemy tail] = "SP_<spawner tail>"`, logs `spawn alias`, increments `spawnOk` and writes
   `result_spawn.txt` (`spawn replayed: …`). Behaviour is identical to before — one call site instead of inline code.
2. New `SpawnHotTick()` (15 Hz): walks only `peerClaims` with an `SP_` key, resolves the local spawner through
   the existing `spawners` index and reads **one** property (`SpawnedEnemy`) per claim. No GObjects walk, no
   allocation beyond the claim loop. Any live enemy goes straight to `ClaimEnemyAppeared`.
3. New `combat::Tick15()` (declared in `coop_state.h`) is wired into the 15 Hz loop in `coop_net.cpp`
   (`RunGuardStep("spawnHot", skip-hold, skip-settle, combat::Tick15)`) **before** `local::Tick15`, so
   `RefreshChars` folds the new alias into `tickTails` and the same frame's `ApplyPeer` can mirror it —
   which is what makes MIRROR immediate, and `MirrorTake`'s `MOVE_None` is what a mirror copy gets anyway.
4. `run-two.ps1` SPAWN/MIRROR windows: 45 s → **30 s** and 45 s → **20 s**, with the measured numbers and the
   reason written into the script (5–10 s for SPAWN, ~1 s for MIRROR). The old 45 s/45 s hid this defect for
   two rounds, so they are tightened, not relaxed.

Nothing else changes: the self-trigger, the 10 s replay throttle, the spawner list refresh and the mirror
code are untouched. Spawners that already worked behave exactly as before (the hot watch only *notices*
sooner).

### 4.1 Exact diff summary

| File | Functions / regions |
|---|---|
| `native\src\coop_combat.cpp` | **new** `[R6-SPAWN]` probe block: `SpawnGateRow`, `g_spawnGates`, `PropText`, `PropBit`, `PropInt`, `PropObj`, `PropClass`, `PropArrayNum`, `SpawnGateState(sp, withNear)`, `SpawnGateWatch`, `SpawnGateTick`; **new** `ClaimEnemyAppeared(spawnerTail, enemy)` + fwd-decl `FindSpawnerAt`; **new** `SpawnHotTick()`, `combat::Tick15()`; **changed** `SpawnerScan` (its inline alias block → `ClaimEnemyAppeared(st, e)`), `SpawnReplay` (adds `SpawnGateWatch` after firing), `EnemyTick` (adds `SpawnGateTick`); **new** test verbs `SpawnDump`, `SpawnFire`, `SpawnCall`, `SpawnProbe`, `SpawnSet` (+ `#include <utility>`) |
| `native\coop_state.h` | declares `combat::Tick15`, `combat::SpawnDump/SpawnFire/SpawnCall/SpawnProbe/SpawnSet` |
| `native\src\coop_net.cpp` | `Loop15`: new guarded step `spawnHot` (`combat::Tick15`) before `local::Tick15`; `RunCommands`: dispatch for `spawndump`, `spawnfire`, `spawncall`, `spawnprobe`, `spawnset` |
| `run-two.ps1` | SPAWN window 45 s → 30 s, MIRROR poll 45 s → 20 s, with the measurement + why it is a tightening written into the script |
| **new** `spawn-lab.ps1` | two-client lab: guest tp's onto the spawn point, both sides dump the claimed spawners, optional `-Phase2` (veto/near control phases), screenshots, native logs copied to `local\lab\` |
| **new** `run-lab.ps1` | lock → deploy C/D → one `spawn-lab.ps1` session → unlock |
| **new** `run-two-x3.ps1` | lock → deploy C/D → N fresh `run-two.ps1` sessions → unlock (lock held for the whole batch) |

Deployed to C/D under the run lock:
`native\build\dxgi.dll` sha256 `04b8ea7c75570b7f5c77954097ae9566ec6e89314ab464927890083cd2b28bd6` (both installs).

## 5. Verification

Build: `cmd /c G:\Freespace\shell_coop\wt\r6-spawn\ms2-coop\native\build.bat test`
→ `coop_policy: 112 checks passed, 0 failed`, `Built: …\native\build\dxgi.dll`; no compiler warnings in
`coop_combat.cpp`; `COST spawnHot` 0.001 ms average (max 0.03–0.70 ms on the tick it hooks), so the new
15 Hz step is free.

Two batches of `run-two.ps1 -GameA E:\Games\_MS2-coop-c -GameB E:\Games\_MS2-coop-d -Port 47020`
(wrapper `run-two-x3.ps1`, lock held for the whole batch, fresh process per run):

| file | SPAWN | MIRROR (`spawnask/spawnok`, `taken`) | RESULT |
|---|---|---|---|
| `local\lab\run-two-r6a-1.txt` | **PASS** `SP_…1555889259…` | **PASS** `spawnask:1 spawnok:1`, `taken:1 driven:1` | FAIL (WORLD) |
| `local\lab\run-two-r6a-2.txt` | **PASS** `SP_…1440305372…` | **PASS** `spawnask:3 spawnok:3`, `taken:3 driven:3` | FAIL (WORLD) |
| `local\lab\run-two-r6a-3.txt` | – | – | aborted: `WARNING: only 1 game client(s) alive` |
| `local\lab\run-two-r6b-1.txt` | **PASS** `SP_…1555889259…` | **PASS** `spawnask:1 spawnok:1`, `taken:1 driven:1` | **PASS** |
| `local\lab\run-two-r6b-2.txt` | **PASS** `SP_…1440305372…` | **PASS** `spawnask:3 spawnok:3`, `taken:3 driven:3` | FAIL (WORLD) |
| `local\lab\run-two-r6b-3.txt` | – | – | aborted: `WARNING: only 1 game client(s) alive` |

- **SPAWN and MIRROR: PASS in 4 of 4 runs that reached the lane**, and `spawnok == spawnask` in every one of
  them: r6a-1 and r6b-1 both read `spawnask:1 spawnok:1` (the FIRST replay attempt was noticed), r6a-2 and
  r6b-2 `spawnask:3 spawnok:3`. Full host log of a clean run (`local\lab\native-r6b-1-A.log`):
  ```
  [17:43:51][A] spawn replay requested for SP_…_1555889259_1555889260 (trigger event ok=true fn=found )
  [17:43:52][A] spawn replayed: SP_…_1555889259_1555889260 -> local BP_Horde_Brigand_C_2147453955_2147453956
  [17:43:52][A] mir take SP_…_1555889259_1555889260 (BlueprintGeneratedClass …/BP_Horde_Brigand.BP_Horde_Brigand_C)
  ```
  (`native-r6b-2-A.log` shows three keys replaying and being taken inside 1 s.)
- **`has no parameter` : 0 matches** in both install logs and in the two archived host/guest logs
  (`local\lab\native-r6b-{1,2}-{A,B}.log`).
- The three runs that did **not** end in `RESULT: PASS` failed on the **WORLD lane, not on SPAWN/MIRROR**:
  `=== WORLD:` is empty and the log says `ws judge: waiting for a live world key`. It correlates exactly with
  how many live keys the receiving client had (`live:2` on B → WORLD PASS in r6b-1; `live:1` → empty verdict
  in r6b-1's neighbours), and the same empty WORLD is in the baseline runs
  `run-two-r5-merged-cd.txt` and `run-two-fixall-1.txt` **taken before this change** — this lane is flaky in
  the tree as handed over and its code (`coop_world.cpp`) is untouched here.
- Two aborted runs: the host client died silently ~45 s after launch (`only 1 game client(s) alive`, its
  native log stops mid-tick, no crash dump) — the documented pre-existing crash (HANDOFF-R5 §3.9:
  double free inside `steam_api64.dll`, 47–62 s after launch, no `dxgi.dll` frame). It hit the *third* boot
  of each batch, which is also when the machine has two 6.5 GB clients' worth of previous runs behind it.

## 6. Left open (honest)

- **The host's copy of a far-away enemy still dies after ~1 s.** The mod now detects and mirrors it inside
  that window (SPAWN/MIRROR verdicts are deterministic), but the copy itself is cleaned up by the game
  because no local player is in that part of the world. Keeping it alive needs either (a) the player near
  the spawner — which is the normal co-op case and works — or (b) a native-path fix for that cleanup, which
  this round did not find: it is not `Actor.K2_DestroyActor` (hooked, never fires) and not a spawner
  Blueprint gate (`spawnprobe`: no `HandleWorldStreaming`/`TryDespawnEnemy`/`SpawnFailed` call, and
  `bSpawnEnemyWithoutGravity=1` does not prevent it). Retrying the replay every 3 s instead of 10 s was
  considered and rejected: it would produce a copy that appears and disappears every few seconds instead of
  fixing the cause.
- **WORLD lane** (`coop_world.cpp`, not part of this task): its verdict is empty whenever the receiving
  client has fewer than two live world keys, which is what keeps `RESULT` at FAIL in most runs. With WORLD
  passing (r6b-1) the whole run is `RESULT: PASS`, so SPAWN/MIRROR are no longer the blocker.
- The `spawndump`/`spawnfire`/`spawncall`/`spawnprobe`/`spawnset` verbs, `spawn-lab.ps1`/`run-lab.ps1`, and
  the `spawngate`/`spawngone` log lines stay in the build as the instruments for the follow-up; they are idle
  unless a lab command runs or a claimed spawner is active.

## 7. Test hygiene (a mistake of mine, for the record)

The first wrapper released the run lock right after the deploy and then ran three `run-two.ps1` sessions
unlocked — wrong (a second session could have killed the clients mid-run and shares the save folder). It is
fixed: `run-two-x3.ps1` now holds the lock for the whole batch and releases it in a `finally`; the second
batch (r6b) ran that way. `spawn-lab.ps1` always took the lock itself.

---

# WORLD — `=== WORLD:` empty in 3 of 4 runs

Follow-up, same workspace / C/D / port 47020 / lock `r6spawn`. Root cause found from log lines, fixed in
`native\src\coop_world.cpp` (mod bug, not a harness bug — the harness was asking the right question).

## W1. What the lane asks for

`run-two.ps1` runs `wstest` on the HOST: it opens the nearest closed door (or flips an inert world bool when the
save has every hub door open) and picks up the nearest uncollected pickup. Both are store-A booleans
(`BP_HubDoor_C.DoorIsOpen`, `BPSO_CraftingItemPickup_C.ItemCollected`, `BPSO_NPC_C.HasTriggeredInitialGreeting`,
`BP_SO_SimpleBool_C.MyBool`). The GUEST has to apply each of them through its own replay path and report
`liveOk` for at least one non-pickup key AND one pickup key; `WsJudge` then writes `result_ws.txt`
(`live=N base=M fail=F` in the `ws=` sync line is the evidence). The per-player rule is untouched: no currency
amount is read, written or synced here — only world-object state.

## W2. Root cause (measured)

The live channel was dead for exactly the keys the test exercises. Four things together:

1. **`WriteBoolField` only ever writes TRUE** — `ue::Set<unsigned char>(o, field, 1)`. Its name reads like
   "write a bool", every caller used it as "mark this world key as set", and that is why the first attempt to
   RESET a key in `wstest` read back `still-true` and produced no change to publish (lab `ws4`:
   `wstest: 2e9e83c8 (world bool) reset to false, readback=still-true`).
2. **`W().done` is permanent.** A key that was ever applied from the peer or the ledger can never be published as
   a live change again, even after the world goes back to false and the game changes it for real
   (`if (settled && v && prevV && !W().done.Has(key))`). Measured, lab `ws1`/`ws2`: the world ledger re-applies
   the PREVIOUS run's flips at boot —
   ```
   [18:49:29][A] ws apply ledger 8e9149a9 BP_SO_SimpleBool_C.MyBool (BP_CurrencyBag_Pickup_C_...1764479806...)
   [18:49:29][A] ws apply ledger a8ca0e96 BP_HubDoor_C.DoorIsOpen (BP_HubDoubleDoor_C_...1808979936...)
   ```
   — so `wstest` then "opened an already-open door / took an already-taken bag": no flip, **no `ws local flip`
   line at all**, the guest only ever received those keys as `base`, and the judge sat on
   `ws judge: waiting for the live pickup readback (live=1 base=8 fail=0)` until the 45 s window ran out.
   That is exactly why WORLD passed on the FIRST fresh run (`flag=set-by-PickUp`, key not yet anywhere) and
   failed on every later one: the previous success was persisted into `coop\ws_ledger_0.txt` and replayed.
3. **`WsApply` claimed the row was true without reading it back** (`gRowKnown[idx] = 2;  // e.v = true`). A
   write that did not stick (the game re-creating its store-A object, a bag already consumed) therefore marked the
   row "known true", and the later real false->true flip could never be detected — the same silence as (2).
4. **The test's fixture was not reset.** With (1)+(3) nothing the verb did could produce a change on a world
   whose state the mod itself had already restored.

## W3. Fix (`native\src\coop_world.cpp`; nothing outside this lane changes)

It took four defects, because each one alone was enough to silence the live channel. The first three were
found from the log lines of the failing case; the last two came out of the labs that reproduced it.

| Change | Why |
|---|---|
| new `WriteBoolValue(o, field, v)`; `WriteBoolField` stays the "set true" helper | `WriteBoolField` never wrote false (`ue::Set<unsigned char>(o, field, 1)`), so the first reset attempt read back `still-true` (lab ws4) |
| `wstest` resets the keys it is about to change: `WsResetKeyFalse(idx, what)` writes FALSE, verifies the readback, clears the applied marker and records "known false" | the ledger/save re-applies the previous run's flips at boot, so without a reset the verb's action is a no-op — same idea as the existing `ResetFixtures(...)`, one store-A row at a time |
| the candidate search in `TestWs` accepts any SETTLED row (`RowSettled`) instead of only settled-FALSE ones | after a ledger replay every flippable row is already true, so the old filter left the door half of the test with no candidate at all (`wstest: no closed door loaded and no flippable world bool either`, run-two r6c-1) |
| door fallback: if `OpenDoor` leaves the key false, force it and say so | the door's own state machine may think it is already open |
| `WsLocalChange(idx, what)`: on a successful verb write, clear the applied marker and protect the key for `kLocalNewsSecs` (8 s) | the peer's base snapshot still carries the OLD value for that key (both clients restore the same world), and applying it in the same tick re-marked the row known-true and swallowed the flip (labs ws6/ws8/ws9: reset + game call logged, no `ws local flip`) |
| echo rule in `WsScan`: per-key `gSelfWriteSeq` (bumped on every apply we make) vs `gFalseSeq` (the counter's value the last time the row was seen/written FALSE); a flip is published unless our write is newer | replaces the permanent `W().done` test: one apply used to silence a key forever, so the next real change never reached the peer |
| `if (settled && !v)` clears `done`, `gLiveDone` and re-arms `gFalseSeq` | a key the world has reset is news again, and one live apply per false->true cycle |
| receiver: a LIVE arrival may bypass `done` and the settled-FALSE gate (`gLiveDone` keeps it to one apply per cycle) | every key also arrives in the base snapshot, so `done` already held it and the live packet was skipped — the guest answered `live=0` while the host was publishing the flip (lab ws6) |
| `gRowKnown[idx]` in `WsApply` comes from a real `ReadBoolField` readback (was an unconditional `2`) | only claim "this row is true" when the property really reads true |
| a live apply of a pickup that is already collected confirms by readback instead of calling `PickUp` again | re-calling PickUp on a gone bag can answer false and turn a correct state into a `fail` |
| the pickup's `flag=forced` is decided by a readback | the old code reported `forced` even when the value did not change |
| diagnostics: `ws peer live <key>`, `ws live skip <key>: …`, `ws apply skip <key>: our own change is newer …` | the live channel now says why it did or did not happen |
| new `ws-lab.ps1` | two-client WORLD-only lab (boot -> settle -> `wstest` -> watch the guest's verdict), ~4 min per iteration instead of a 9 min full run; `-PreWait` because store-A rows only count after `kWsSettle` (8 s) plus a full 14-tick scan pass |

Working chain, lab `20261025-200731-ws10` — **with the ledger already holding the previous run's keys**, i.e.
the case that used to fail:

```
[20:07:5x][A] wstest: a8ca0e96 (door)    reset to false, readback=false
[20:07:5x][A] wstest OpenDoor BP_HubDoubleDoor_C_... ok=true
[20:07:5x][A] wstest: 8e9149a9 (pickup)  reset to false, readback=false
[20:07:5x][A] wstest PickUp BP_CurrencyBag_Pickup_C_... ok=true flag=set-by-PickUp
[20:07:5x][A] ws local flip a8ca0e96 BP_HubDoor_C.DoorIsOpen -> true (BP_HubDoubleDoor_C_...)
[20:07:5x][A] ws local flip 8e9149a9 BP_SO_SimpleBool_C.MyBool -> true (BP_CurrencyBag_Pickup_C_...)
[20:07:5x][B] ws peer live a8ca0e96 / 8e9149a9  -> ws apply live ... -> readback now true
[20:07:5x][B] PASS world state: door BP_HubDoubleDoor_C_... opened, BP_CurrencyBag_Pickup_C_... collected
              (live=2 base=0 fail=0)
```

(An earlier lab, `ws5`, passed with the first two changes but only because a key that run happened to use was
still false everywhere; `r6c-1` then showed the door half with no candidate. The labs above are the ones that
cover the real case: ledger populated, both keys already true.)

## W4. Acceptance — full `run-two.ps1` runs on C/D

Build `e99ea56b…` deployed to both installs under the lock (the runs below used the byte-identical previous
build of the same sources, `603d20c6…`); batch wrapper `run-two-x3.ps1` (lock held for the whole batch, fresh
process per run, 45 s settle between boots).

| file | WORLD | other verdicts | RESULT |
|---|---|---|---|
| `local\lab\run-two-r6e-1.txt` | **PASS** `door BP_HubDoubleDoor_C_… opened, BP_CurrencyBag_Pickup_C_… collected` (`B live:2 base:4`) | STORE_A2/AITARGET/STREAM/NPC empty, NPCFAR FAIL | FAIL |
| `local\lab\run-two-r6e-2.txt` | **PASS** (same pair, `B live:2`) | STORE_A2/NPC empty | FAIL |
| `local\lab\run-two-r6e-3.txt` | **PASS** (same pair) | STORE_A2/NPC empty | FAIL |
| `local\lab\run-two-r6f-1.txt` | **PASS** (same pair) | STORE_A2/STREAM/ANIM/NPC empty, NPCFAR FAIL | FAIL |

**WORLD: PASS in 4 of 4 full runs** (and in the lab that reproduces the failing precondition, §W3) — the lane
this task is about is fixed. `RESULT: PASS` is NOT reached, and the block is now other lanes, in every case with
its own "empty verdict file" rather than a `FAIL` of this lane:

- `STORE_A2` (store-A2 **typed** table, `coop_world_ws2.cpp` — another lane's code) is empty in all four runs.
  Its own log says why it cannot judge, and it is the same class of problem as this one, in THAT lane:
  `ws2 judge: waiting for a live typed value (recv=474 applied int=1 str=0)` — the guest is receiving the typed
  payload and applying its ints, but the host never sends a typed entry with `live=1`. `wa2test` on the host does
  run (`ws2 wa2test: bool … := true`) and writes its ints, so the typed "local change → live flag" path
  (`gRecent` in `coop_world_ws2.cpp`) is not being armed; nothing in this WORLD change touches that module
  (its scan/publish/judge are its own, and the boolean `WsApply`/`WsScan` code I changed only feeds it through
  the unchanged `Ws2OnApplied` hook). No `ERR` line appears in either client's log in any of these runs.
- `NPC` / `NPCFAR` / `STREAM` / `ANIM` also come back empty/FAIL in some of these runs. They are the
  combat/stream/anim lanes; the two acceptance batches from the SPAWN task (r6a/r6b) show them passing on the
  same build lineage, so this is the environment's known flakiness rather than a deterministic break.
- One honest caveat: a WORLD PASS now arrives ~10 s after `wstest` instead of timing out at 45 s, so every lane
  after it starts ~35 s earlier than it used to. Whether that shifts the downstream windows (STREAM/NPCFAR have
  fixed waits) is plausible but **not proven here**; the downstream failures are reported as they are, not
  explained away.
- No `steam_api64` crash boot happened in the r6e batch (the two previous batches each lost one boot to it).

Files changed for this follow-up: `native\src\coop_world.cpp` (all of §W3), `native\coop_state.h` (nothing
kept — the `kEchoSecs` experiment was removed; `kLocalNewsSecs` lives in `coop_world.cpp`), new
`ws-lab.ps1`. `run-two.ps1` was NOT changed for this lane: the harness was asking the right question.

---

# WORLD follow-up — is `STORE_A2` a regression, and why the other lanes moved

## F1. Proof that `STORE_A2` is NOT a WORLD regression (and what it is)

The reviewer's suspicion is the right question, so this is the measurement, not an argument:

- `run-two-r6b-1.txt` (before any WORLD change): `=== STORE_A2: PASS`. The verdict line was written by the
  guest at **17:43:47** — **2 s after `wa2test` ran** (17:43:45). In that run the guest's judge had been
  printing `ws2 judge: waiting for a live typed value` from 17:39:35 right up to 17:43:45, and what armed it
  was a typed value that changed *by itself* on the host at that moment (`ClensedCores` 5 → 6; `B` then
  applied `= 6` at 17:43:47).
- `run-two-r6e-1/2/3` and `r6f-1`: the same judge line runs for the whole 130 s window with
  `applied int=1 str=0` and no verdict. The host's `wa2test` still ran and wrote its ints (e.g.
  `ws2 wa2test: int BPSO_WoodenElevator_C.StartingFloor 0 -> 1 … key=…`), but the guest never received a
  typed entry with `live=1`.
- The host and guest logs of both runs are otherwise identical at this level (same fixture resets, same
  `ws2 apply int … ClensedCores`), and there is no `ERR` line in any client log. What differs is only the
  value: `ClensedCores` was 4→5 in r6b-1 and 9→10 in r6e — the typed **ledger** (`wt_ledger_0.txt`,
  `ws2 ledger: 9d46b516|ClensedCores offered N`) carries the max forward every run, so the value stops
  changing between runs and the accidental "live typed value" that STORE_A2 had been passing on disappears.

So: not caused by the WORLD change, but the WORLD change **exposed** it — WORLD used to eat the whole 45 s
window, and it now returns in ~10 s, so `wa2test` runs ~35 s earlier, before any typed value has drifted.

The real defect is the same one as WORLD's, in the typed lane: a test verb's own write is only published as
"live" if the 14-tick scan happens to observe it on an already-settled row — and the verb resets its fixtures
first, which re-creates those save objects and leaves the rows *unsettled*, so the scan sees nothing.

## F2. Fix (typed lane, same rule as §W3)

| Change | Why |
|---|---|
| `world::MarkLocalWorldKey(key)` (new, `coop_world.cpp` + `coop_state.h`): erase `done`, take `kLocalNewsSecs` precedence over the peer's snapshot, `recent[key] = now`, re-arm `gFalseSeq` | one helper both test verbs use for "this value is MY change, publish it live now" |
| `wa2test` calls it for every boolean it writes (`MarkLocalWorldKey(Hash(owner))`) | the store-A bool half of the verdict (`boolRow` in the PASS text) needs a live apply |
| `wa2test` sets `gRecent[Hash(owner)\|field] = Now()` for every int it writes | the typed half; this is the gate that was starving (`gSawLiveWt`) |
| `WsLocalChange` (WORLD's verb) now calls `MarkLocalWorldKey` | one implementation instead of two |

Lab `20261025-213136-ws2lane` (ledger populated, i.e. the failing case):
`=== WORLD: PASS store A2: BPSO_WoodenElevator_C.HasBrokenOnce replayed on BP_EndDungeonRewardDoor_C_…,
BPSO_FC_FinalBossManager_C.ClensedCores=14 … applied from wt (kinds=10 typed=3 recv=2568 applied=2 fail=0)`
with `B ws2: … applied=2 fail=0` in the same run — and the WORLD lane PASSed in the same session.

## F3. The other lanes (`NPC`/`NPCFAR`/`STREAM`/`ANIM`)

`NPC` (`=== NPC:`) is not part of `$pass` in `run-two.ps1` (it is the informational npc line), so it cannot
block the acceptance. `STREAM` and `ANIM` are measured again in the acceptance batch below rather than papered
over with longer windows.

**`NPCFAR` had a real cause, found the same way** (`r6b-1` PASS vs `r6g-1` FAIL, same build lineage):

```
r6b-1  [B] npctp -> BP_Cultist_HUB_C_…1464626973 at 42982 -42703 -42382
       [B] collect: … own=1 … nearest=BP_Cultist_HUB_C_…1464626973 d=600 dp=91419
       [A] mirroring peer-owned enemy BP_Cultist_HUB_C_…1464626973 at 42982 … → mir take
r6g-1  [B] npctp -> BP_Cultist_HUB_C_…1464626973 at 42982 -42703 -42382
       [B] collect: … own=0 … nearest=BP_Cultist_HUB_C_…1464626973 d=11798 dp=1791
       (no mirror line on the host, NPCFAR FAIL)
```

The verb moved the guest's copy to the guest's side, and ~20 s later that copy was back **next to the peer's
puppet** (`dp=1791`): the actor is peer-owned there, so `MirrorDrive` writes the peer's published position into
it on every tick and undoes the teleport. `own` is computed from geometry, so it decayed to 0 and nobody ever
handed the NPC over.

Fix (`coop_combat.cpp` + `coop_net.cpp`): new `combat::MirrorClaimLocal(tail)` — drop the mirror for that tail
(`MirrorRelease`) and add it to `mirrorSkip` so the take cannot immediately resume — called by `npctp` after a
successful move, logged as
`npctp: <tail> is this client's own now (mirror dropped, not re-taken)`.
A teleport the mod itself performs must not be reverted by the mod's own mirror drive; that is the whole
content of the change.

## F4. Acceptance — 3 consecutive full `run-two.ps1` runs, C/D, port 47020

Build `2db8fb5d…` (the WORLD + STORE_A2 + `npctp`-claim changes), deployed to both installs under the lock
before the `r6h`/`r6i` batches; workspace build and both installs hash-identical afterwards.

| batch | run | WORLD | STORE_A2 | STREAM | NPCFAR | ANIM | RESULT |
|---|---|---|---|---|---|---|---|
| r6h | 1 | PASS | PASS | PASS | PASS | PASS | **PASS** |
| r6h | 2 | PASS | PASS | PASS | PASS | PASS | **PASS** |
| r6h | 3 | PASS | PASS | – | FAIL | – | FAIL (guest never travelled: no `npctp`, no `travel` in its log, `collect: … chars=14` at the hub — a degraded boot, see below) |
| r6i | 1 | PASS | PASS | PASS | PASS | PASS | **PASS** |
| r6i | 2 | PASS | PASS | PASS | PASS | PASS | **PASS** |
| r6i | 3 | PASS | PASS | PASS | PASS | PASS | **PASS** |

`r6i-1..3` are the three consecutive `RESULT: PASS` runs (each with WORLD + STORE_A2 + STREAM + NPCFAR + ANIM
PASS, plus every other lane in `$pass`). `r6h-1/2` are two more in a row from the batch before it.

Notes, honest:
- `r6h-3` failed on a **degraded boot**, not on a lane: the guest's log shows no `travel`/`npctp` at all and
  `collect: … chars=14` in the hub, so STREAM/ANIM/NPCFAR had no precondition to measure. Same environment
  flakiness as the `steam_api64` boots; retried by the `r6i` batch.
- No `steam_api64` crash boot happened in `r6h`/`r6i` (the earlier `r6a`/`r6b`/`r6e` batches each lost one
  boot to it, reported in §W4/§5).
- `has no parameter`: 0 in every archived log of this follow-up; no compiler warnings.


