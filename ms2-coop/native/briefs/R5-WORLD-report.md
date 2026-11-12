# R5-WORLD — store A2 report (gap G1 + G1b)

Facts only. Everything below was measured on this workspace's build
(`native\build\dxgi.dll`, sha256 in the log of the run), on the two locked test
instances, with the game lock held. Nothing in this file is a plan.

## What was built

| Area | Where | Notes |
|---|---|---|
| 7 new save-object classes + 1 more boolean class | `coop_world.cpp` `kKinds` (marker `[R5-WORLD]`) | appended, never inserted: every Lua kind index is unchanged, `kWsKindCount` 28 → 36 |
| 5 extra fields on already-watched classes | `coop_world.cpp` `kKinds` | field 0 keeps the bare-owner key, later fields key on `"<owner>.<field>"`, so no existing key moves |
| typed table (ints + string arrays) | `coop_world_ws2.cpp` `kVal[]` | 10 rows, one `FindAllOf` per tick round-robin (5 s pass) |
| the `"wt"` wire section | `coop_world_ws2.cpp` (`BuildPayload`/`Ws2OnPeerLine`) | `,"wt":[[key,"i",<int>,<live>], [key,"s","<string>",<live>], …]` |
| fixtures | `coop_world_ws2.cpp` (`ResetFixtures`, `Wa2FixtureReset`) | `ResetSaveObjectByGuid` per instance first, `ResetSaveObjectByClass` as the fallback; `SaveObjects` is never touched |
| verdict + census + self test + array test | `coop_world_ws2.cpp` | `Judge()` writes `result_ws2.txt` on the receiving client |

Shared-file edits, all inside `// [R5-WORLD] begin/end` markers:
`coop_state.h` (kWsKindCount 28→36; `Toggles::ws2Enabled`; 10 declarations in
`namespace world`), `coop_world.cpp` (kKinds rows, `kLuaKindCount`, `IsNewWsRow`,
`WsRowName`/`WsKindName`, 6 one-line hooks in `Tick2`/`OnPeerLine`/`SerializeWs`/
`Status`/`Reset`, the fixture call in `TestWs`, `Ws2OnApplied` hook, `FlipSafeKind`
extended), `coop_net.cpp` (the `WS2_OFF.txt` toggle row, 5 cmd verbs).

## Wire keys (contract)

```
,"wt":[["<hash>|<field>","i",<int>,<live>],["<hash>|<field>|<n>","s","<string>",<live>]]
```
* `<hash>` is `world::Hash(owner)` — the same FNV-1a the `ws` section uses.
* `live` = 1 for a value this client changed within `kLiveSecs`, 0 for the rotating
  window (`kWtWindow` = 24 entries per line). Same semantics as `ws`.
* Arrays are ONE ENTRY PER ELEMENT (G1b's option (b)): the union rule is a set
  difference, no nested payload string, and one late element travels alone.
* Unknown keys are ignored by both sides (the `wt` parser skips an entry it cannot
  parse and never consumes the rest of the line).

## Merge rules as implemented

| Rule | Fields | Who applies |
|---|---|---|
| `max` wins | FC_FinalBossManager.ClensedCores, TavernArena.ArenaCombatLVL, ShellGhost.StateIndex, Egon.GloomDonated | either side, only when the peer value is larger |
| host wins | WoodenElevator.StartingFloor, Tarforge.StoredTarcores, BossDLManager.CurrentBoss | guest only (the host ignores a guest value) |
| set union, never remove | SavedKeys.SavedKeyIDs | either side |
| publish only | SavedDoorIDs, SavedChestIDs | nobody — element is `TSoftClassPtr<USpartaItemDefinition>` (0x20 bytes), publishing a string is free but appending one would need an engine-allocated `FSoftObjectPath`; see "not verified" |

## Census (what actually exists in the test save's hub)

Full file: `native\briefs\R5-WORLD-census.txt` (copied from `coop\ws2_census.txt`).

| class.field | type | merge | live instances | value seen |
|---|---|---|---|---|
| BPSO_FC_FinalBossManager_C.ClensedCores | i | max | 1 | 0 |
| BPSO_WoodenElevator_C.StartingFloor | i | host | 1 | 0 |
| BPSO_Tarforge_C.StoredTarcores | i | host | 1 | 0 |
| BPSO_TavernArena_C.ArenaCombatLVL | i | max | **0** | — |
| BPSO_ShellGhost_C.StateIndex | i | max | **0** | — |
| BPSO_Egon_C.GloomDonated | i | max | **0** | — |
| BPSO_BossDLManager_C.CurrentBoss | i | host | **0** | — |
| BP_SO_SavedKeys_C.SavedKeyIDs | s | union | **0** | — |
| BP_SO_SavedLockedDoors_C.SavedDoorIDs | s | union | **0** | — |
| BPSO_SavedLockedChests_C.SavedChestIDs | s | union | **0** | — |
| BPSO_NPC_C.HasTriggeredInitialGreeting / WasEverKilled | bool | true | 20 | false / false |
| BPSO_CraftingItemPickup_C.ItemInitialized | bool | true | 29 | false |
| BPSO_TarGate_C.PlayerClensed | bool | true | 55 | false |
| BPSO_Ovum_C.OvumCollected | bool | true | 3 | true |
| BPSO_MovingObject_C.MovedToEnd | bool | true | 2 | false |
| BPSO_WoodenElevator_C.HasBrokenOnce (@0xBC) | bool | true | 6 | false |
| BPSO_EridrimUnlockManager_C / EvilStatue_C / ShellRevive_C / PlayerTrapDungeon_C / PetrifiedCultistManager_C / TavernArena_C / SavedLockedChests_C | bool | true | **0** | — |

Every field name/offset/type above was read from a live object, not just the SDK
(`off:184 size:4` = int32 at 0xB8 etc.), which is the "verify against the dump"
requirement: a wrong name shows up as `off:-1 size:-1` in the census rather than
as a silent nil.

Not reachable in this save/area: the three array classes, Egon, ShellGhost,
BossDLManager, TavernArena, ShellRevive, PlayerTrapDungeon, PetrifiedCultistManager,
EridrimUnlockManager, EvilStatue. Their code paths are covered by the codec self
test and the array test; the field-level apply was NOT exercised for them.

## Verdicts (last run: 2026-10-25 10:31, both installs on this build)

```
=== STORE_A2: A kinds:10 typed:3 scans:336 recv:5831 applied:0 str:0 fail:0 leaks:0 ; B kinds:10 typed:3 scans:286 recv:5960 applied:1 str:0 fail:0 leaks:0
=== STORE_A2: PASS store A2: BPSO_NPC_C.HasTriggeredInitialGreeting replayed on BP_Cultist_HUB_C_UAID_..._1464626973_1464626974, BPSO_FC_FinalBossManager_C.ClensedCores=1 (BP_FinalBossManager_C_UAID_..._1195328269_1195328270) applied from wt (kinds=10 typed=3 recv=5953 applied=1 fail=0); no string-array element to union on this client -- see ws2_census.txt
=== STORE_A2SELFTEST: ws2 self test: 20 pass, 0 fail
=== STORE_A2CENSUS: 23 census rows (ws2_census.txt)
RESULT: PASS
```
Every pre-existing verdict in the same run also PASSed: SHELL, WEAPON, LOOK, ANIM,
DAMAGE, DEATH, WORLD, PROGRESS, QUEST, QUEST2, LIFT, CHEST, SPAWN, MIRROR, PEERHIT,
AITARGET, STREAM, NPCFAR, A, B. `LEDGER` printed `-` because the ledgers were
deleted before the run (the brief says that is fine, and the pass line does not
include it).

WORLD is worth a note: the flip fixture this test has always used (BearTrap /
FallingCorpse / FallingStatue) has **0 live instances** in this save, so `wstest`
had nothing to flip and WORLD could not pass (it failed exactly that way in an
earlier run today). `FlipSafeKind` now also accepts an NPC's
`HasTriggeredInitialGreeting` (field 0 only) and a wooden elevator's
`HasBrokenOnce`, and `TestWs` resets its fixtures first, so WORLD replays a real
live flip again (this run: door + pickup, live=2).

## Perf (measured, `COST` table in `ms2coop-native.log`, 30 s window in gameplay)

```
COST   ws2Tick    n=60   avg 0.887 ms  p95<=3.072  max 4.11 ms   total 53.26 ms
COST   wsScan     n=120  avg 1.436 ms  p95<=4.096  max 7.38 ms   total 172.37 ms
COST   worldState n=60   avg 4.665 ms  p95<=12.288 max 16.04 ms  total 279.90 ms
```
`ws2Tick` is 2 Hz, so 0.887 ms × 2/s ≈ **0.03 ms per frame at 60 fps** (budget was
0.15 ms). The typed table costs about half of one `wsScan` (its `FindAllOf` is
index-accelerated, and a class that is absent is a cached miss).

## Unit tests

`native\build.bat test`: `test_api: PASS` (every declared signature matches),
AOB self-check 4/4 PASS (ProcessEvent / AppendString / EngineTick / GObjects resolve
to the recorded RVAs), load test built. The codec has its own test in the module
(`wa2selftest`, no engine calls): **20 pass, 0 fail**
(`native\briefs\R5-WORLD-selftest.txt`).

## Kill switch

`WS2_OFF.txt` in the coop dir (read once at load, like the others) turns off: the
7 appended classes (they are not scanned at all), the 5 extra fields, and the whole
`wt` section. Round-4 behaviour is what remains.

## What does NOT work / was not verified

1. **String-array apply on a live object**: `BP_SO_SavedKeys_C` has no instance in
   this save, so the union never had an element to append live. The union logic and
   the parser are unit-tested (`wa2selftest`), and the engine half —
   `UKismetStringLibrary.ParseIntoArray` + the read-back of its return value — was
   exercised on its own and PASSes (`native\briefs\R5-WORLD-arraytest.txt`):

   ```
   PASS ws2 array: ParseIntoArray built 3 engine FString element(s) [CoopArrayTest_A|CoopArrayTest_B|CoopArrayTest_C]
           (no BP_SO_SavedKeys_C instance loaded: the property write half of the union is unexercised in this save)
   ```

   So the parameter/return convention of that call is proven; what remains unproven
   on a live object is the 16-byte property write of the returned `TArray<FString>`
   (`ue::Set`, the same call the apply path makes) and the leak it leaves behind.
2. `SavedDoorIDs` / `SavedChestIDs` are **publish only** (soft class paths, not
   `FString`): they are read and sent for telemetry, never applied.
3. `BPSO_GrishaCage_C.OpenType` (the `FGameplayTag` at 0xBC) was skipped as the
   brief allows: reading it as an `FName` would be safe, but writing one needs a
   real name-pool entry and there is no GrishaCage instance in the hub to test
   against.
4. `BPSO_MovingObject_C.MovedToEnd` is watched and applied as a FIELD; the actor's
   own action (`BPI_LiftCall:TriggerMovingActor(TopLevelLever)`) is implemented in
   `Ws2OnApplied` but gated behind `WS2_MOVING.txt` and **was not tested** — a
   field-only flip leaves the two worlds visually different, so `wa2test` does not
   set that row.
5. The **ledger** half of `wt` (`wt_ledger_<slot>.txt`) parsed 0 rows in every run
   (the file is deleted before a WORLD/STORE_A2 run, as the brief instructs) — the
   write path is exercised on every apply (`LedgerNote`), the read-back path only
   by the self test. Ledger values are deliberately never offered for host-wins
   rows (a record cannot say which client wrote it).
6. The 9 classes with 0 live instances: their scan/apply code ran (and reported
   `instances=0`), but no value crossed the wire for them.
7. `ws2=` counters: `recv` counts parsed *entries*, not distinct keys (each 15 Hz
   peer line re-sends the section, so it grows by the section's size).

## Open questions

* Whether `BPSO_NPC_C.WasEverKilled` should really be true-wins: the mod now
  mirrors an NPC killed on one client onto the other's save object, which is the
  documented "true wins" rule but is a permanent change to that save.
* Whether the two soft-class arrays should be applied once an engine-allocated
  `FSoftObjectPath` can be produced safely (the blocker is `SubPathString`, an
  engine `FString`).
* Two `0xC0000374` (STATUS_HEAP_CORRUPTION, ntdll) application events appear in the
  Windows log at 10:22:16 and 10:32:29 today. Neither is one of the two clients that
  produced the passing run: that run printed no "only N game client(s) alive"
  warning and both clients were stopped by the harness at 10:31:20, and 10:32:29 is
  after the lock was released to the next agent. Earlier today a lab client of this
  workspace did crash with the same signature while `wa2selftest` ran; the cause
  found and fixed was a stale row index (`RowIndex` returned an index into a
  `gRows` that the self test had cleared, so a later read/write was out of range)
  plus the self test now saving and restoring the tables it touches. The signature
  is identical, so that is the leading hypothesis for the earlier one; the later one
  is not attributable.

