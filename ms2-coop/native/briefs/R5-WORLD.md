# Round 5 — WORLD agent (`<you>` = world, copy = `G:\Freespace\shell_coop\wt\world\ms2-coop\`)

Read `native\briefs\R5-COMMON.md` first (in your copy). Task: **gap G1 — the missing store-A save-object classes
and fields** (`docs\HANDOFF-REMAINING-SYNC.md` §2.3 matrix + G1 + G1b; data `research\mine\SYNC-DATA-MINE.md` §4
and `research\mine\saveobjects-matrix.tsv` for exact field names/offsets/types). Your file is
`native\src\coop_world.cpp` (the store-A `WS_KINDS` table, ledger, apply); declarations in marked blocks of
coop_state.h, cmd verbs in marked blocks of coop_net.cpp.

## 1. Pure table additions (true-wins bools) on classes already watched
`BPSO_CraftingItemPickup_C.ItemInitialized`, `BPSO_EridrimUnlockManager_C.ShellClaimed`,
`BPSO_EvilStatue_C.RewardGiven`, `BPSO_Ovum_C.OvumCollected`, `BPSO_TarGate_C.PlayerClensed`. Plus the non-bool
fields `BPSO_FC_FinalBossManager_C.ClensedCores` (int, max wins) and `BPSO_GrishaCage_C.OpenType` (tag — only if you
can read/write the FGameplayTag safely as its FName; else skip with a note).
Verify every field name/type against the dump before shipping (a wrong name must fail loudly in the log, not
silently).

## 2. New classes
| Class | Fields | Merge |
|---|---|---|
| `BPSO_NPC_C` | `HasTriggeredInitialGreeting`, `WasEverKilled` | true wins |
| `BPSO_ShellRevive_C` | `StationUsed` | true wins |
| `BPSO_PlayerTrapDungeon_C` | `TrappedOnce` | true wins |
| `BPSO_PetrifiedCultistManager_C` | `Harros` (check type) | true wins |
| `BPSO_MovingObject_C` | `MovedToEnd` | true wins; live flip should also move the actor via its own action (`BPI_LiftCall:TriggerMovingActor(bool)` pattern; see the lift code) — lab-verify, else field-only + note |
| `BPSO_WoodenElevator_C` | `StartingFloor` (int), `HasBrokenOnce` (bool) | host wins / true wins |
| `BPSO_ShellGhost_C` | `StateIndex` (int) | max wins (monotonic progress — check in-game semantics, else host wins) |
| `BPSO_Egon_C` | `GloomDonated` (int) | max wins |
| `BPSO_Tarforge_C` | `StoredTarcores` (int) | host wins |
| `BPSO_TavernArena_C` | `WagerPlaced`, `RewardGiven` (bool true wins), `ArenaCombatLVL` (int max wins) | |
| `BP_SO_SavedKeys_C` / `BP_SO_SavedLockedDoors_C` / `BPSO_SavedLockedChests_C` | `SavedKeyIDs` / `SavedDoorIDs` / `SavedChestIDs` (TArray<FString>), `HasBeenOpened` | set union / true wins |
| `BPSO_BossDLManager_C` | `CurrentBoss` (enum) | host wins — publish + apply field only; do NOT touch `RelevantDL` (object ref) |
Out of scope (per-player or later): `BPSO_ShopData_C`, `BPSO_Progression_C`, `BPSO_PlayerSession_C`,
`BP_SpartaPlayerSaveObject_C`, `BPSO_ShellMemories_C`, `BPSO_WordMapObjective_C`/`BPSO_WorldMap_C` (per-player map
reveal — leave), `BPSO_NarrativeEventsTracker_C`, `BP_SpawnerSessionSaveObject_C`. The owner decided inventory,
gold and levels are per player — never sync currency counts.

## 3. Typed-value codec (G1b)
Today the `ws` section carries `[hash,0|1]` bools. Add a typed section `"wt":[["<hash>|<field>","i",<int>,live],
["<hash>|<field>|<n>","s","<string>",live], ...]` (or the one-key-per-element design G1b recommends) with the same
rotating-window + live-flip + ledger semantics as `ws`, so late-streamed actors and reloads converge. Arrays:
one entry per element, union on apply (append missing strings, never remove). Write FString arrays through the
engine-safe path (GMalloc / engine helper used elsewhere in the native code — never hand-allocate engine memory
with the CRT).

## 4. Fixtures (do this first — it also fixes the old perennial WORLD red)
`USpartaSaveSubSystem.ResetSaveObjectByGuid(GuidString)` / `ResetSaveObjectByClass(SaveObjectClass)` exist. Make
the store-A test verbs reset their fixtures at test start so WORLD/LIFT/CHEST and your new verdict are re-runnable
without deleting `ws_ledger_*.txt` by hand (keep the ledger otherwise unchanged). Lab-verify the reset call is safe
(never iterate `SaveObjects` — that crashed under UE4SS; if you need the map, use ms2_map.cpp's reader carefully).

## 5. Test + verdict
First a census lab run: which of the new classes have live instances in the test save's hub/start area (log count +
first names per class). Test only what exists; for classes that are not reachable, verify the code path with a
unit test or a forced field write and say so. Verb `wa2test` (sets one fixture per reachable new class/field on A,
incl. one int and one string-array element) → verdict `STORE_A2` on B (all received and applied, through ws/wt
and through the ledger after B reloads if you can test that). Counters in sync.txt (`ws2=kinds:<n> typed:<n> ...`).
Kill switch `WS2_OFF.txt` (only the new kinds + wt; old behaviour intact).
