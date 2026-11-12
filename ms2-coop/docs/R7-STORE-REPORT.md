# Round 7c (`r7-store`) - STORE_A2 on the integrated tree: reset the boolean FIELD, and let the store-A scan ignore duplicate save objects

Workspace `G:\Freespace\shell_coop\wt\r7-store` (fresh copy of the merged main tree: boss, dialog,
drops, break, the round-7 wire split and the round-7b `fixreset` are all in it). Installs host
`E:\Games\_MS2-coop-k`, guest `E:\Games\_MS2-coop-l`, relay port 47150. `..\R7-COMMON.md` obeyed:
only files inside this workspace were edited, every deploy/run under `Lock-Game 'r7-store'`,
`E:\Games\Mortal Shell II` never touched, `has no parameter` grepped in both native logs (0 hits).

## 0. Verdicts (7 completed runs, see the tally in §5)

| Verdict | Result |
|---|---|
| **STORE_A2** | **PASS in 7 of 7 completed runs** (the regression this round exists to fix) |
| FIXRESET | PASS in 7 of 7 (both clients) |
| WIRETEST / WIRE | PASS in 7 of 7 (largest datagram 1150 B) |
| WORLD / LIFT / CHEST | PASS in 7 of 7 |
| LEDGER | PASS in 7 of 7 (host side, 14 keys) |
| ANIM / PEERHP / CINE | one run each lost to these three (1 / 4 / 1) - pre-existing flakes, §5, §7 |
| **RESULT** | **PASS in 1 of 7** (r7s-2); every other completed run failed exactly one of ANIM/PEERHP/CINE, none of them this round's code |

## 1. Root cause

Round 7b added `BPSO_NPC_C` (plus `BPSO_Tarforge_C`, `BPSO_FC_FinalBossManager_C`) to
`TestFixReset()`'s `kSaveObjects[]`. That fixed the *host/guest asymmetry* of round 7, and it works in
the round-7 workspace - but in the integrated tree it turns into the opposite failure, because:

1. `USpartaSaveSubSystem.ResetSaveObjectByClass` does **not** reset a save object in place: the save
   subsystem builds a **new** `USpartaSaveComponent.RuntimeData` and the previous one stays alive in
   the object array. Measured in `wt\r7-final\REPORT.md` R2.3.3: **2 distinct `RuntimeData` objects
   for one owner on the guest, 3 on the host**.
2. `WsScan()` keys a row by **owner name only** (`key = Hash(owner)`), and a row's identity is the
   object's `fn`. With two objects for one owner, every pass takes a different one, so `prevFnSame`
   is false on every pass and the row's settle window restarts for ever:

```
ws see 5f5dd95e HasTriggeredInitialGreeting=true  ... SpartaSave.RuntimeData_2147470800_2147470801
ws see 5f5dd95e HasTriggeredInitialGreeting=false ... SpartaSave.RuntimeData_2147468242_2147468243
ws settle reset 5f5dd95e (row took a new fn after 9.1s: ...)      x4 (guest) / x5 (host)
ws live skip 5f5dd95e: row not settled here yet (known=0)         <- forever
```

3. `RowSettled(idx)` therefore never becomes true, `ApplyPeerWorld` skips every live arrival,
   `liveOk` is never confirmed, `ws2`'s judge never sees a new-class boolean and `result_ws2.txt` is
   never written -> `=== STORE_A2:` empty -> `RESULT: FAIL`.

So the round-7b change was right about *which* state must be cleared and wrong about *how*: it reset a
whole save object where only one boolean needed clearing, and the reset itself is what split the row's
identity.

The same mechanism exists without `fixreset` at all: a normal run's log (below) shows the map
re-creating six `BP_SO_SimpleBool_C` save objects at boot - one settle reset each - and those keys
would have churned for ever without §2.2.

## 2. Fix - two parts, both in `native/src/coop_world.cpp`

### 2.1 Clear the boolean FIELD instead of resetting the save OBJECT

New `ClearBoolFixtures(cls, field)`: `FindAllOf(cls)` -> `ReadBoolField` -> `WriteBoolValue(o, field,
false)`. A property write reaches exactly the state a fixture needs (the guest's copy really goes
false, which is what makes the host's later `wa2test` write a *change* it can apply and confirm by
readback) and it never touches the save subsystem, so no `RuntimeData` is re-created and no row loses
its object.

`TestFixReset()` now clears these as **fields** - the boolean fixtures `wa2test`/`wstest` can pick:

| class.field | used by |
|---|---|
| `BPSO_NPC_C.HasTriggeredInitialGreeting` | STORE_A2's new-class boolean |
| `BPSO_WoodenElevator_C.HasBrokenOnce` | STORE_A2 bool pick |
| `BPSO_ShellRevive_C.StationUsed` | the other `kBoolPicks` |
| `BPSO_PlayerTrapDungeon_C.TrappedOnce` | " |
| `BPSO_PetrifiedCultistManager_C.Harros` | " |
| `BPSO_TavernArena_C.WagerPlaced` | " |
| `BPSO_SavedLockedChests_C.HasBeenOpened` | " |
| `BPSO_BearTrap_C.IsDisarmed` | WORLD flip candidates |
| `BPSO_FallingCorpse_C.ShotDown` | " |
| `BPSO_FallingStatue_C.StatueFallen` | " |

and the save-object reset list shrinks to the classes whose **object** really is the fixture:
`BP_HubDoor_C` (the door's actor state, not only its flag), `BPSO_Tarforge_C` and
`BPSO_FC_FinalBossManager_C` (the typed-int fixtures). `BPSO_NPC_C` and `BPSO_WoodenElevator_C` are no
longer reset as objects. The lift/chest actor resets and the store-B `TimesUsed` clearing are
unchanged, and `result_fixreset.txt` now reports the field clears:

```
=== FIXRESET: A PASS fixreset: 3 boolean field(s) cleared (no save-object reset, so the store-A row keeps
    its object and stays settled), 3 save object(s) reset by guid/class (world 1, lift 0 of 0,
    chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: B PASS fixreset: 3 boolean field(s) cleared ... 3 save object(s) reset ...
=== FIXRESET: PASS
```

and each cleared field logs its own line on both clients:

```
[10:19:45][A] fixreset: BPSO_NPC_C.HasTriggeredInitialGreeting -> false on 1 instance(s) (of 32 read, no save-object reset)
[10:19:45][A] fixreset: BPSO_WoodenElevator_C.HasBrokenOnce -> false on 1 instance(s) (of 3 read, no save-object reset)
```

### 2.2 Let the store-A scan ignore a stale duplicate save object

Even with 2.1 the engine itself re-creates `RuntimeData` objects while streaming (the pre-existing
comment at `WsScan` names it as the usual reason a row takes a new `fn`), so the class of bug is real
and is fixed directly. New `CanonicalSaveObject(o)` asks the owner's own save component which object
is current:

```
USpartaSaveObject::GetOwningSaveComponent()  -> USpartaSaveComponent
USpartaSaveComponent.RuntimeData (0x110)     -> the USpartaSaveObject the actor uses now
```

`WsScan()` calls it only when the row would take over a **different** object while its current object
is still valid: if the newcomer is not the component's `RuntimeData` (or, when the newcomer's component
is unreadable, if the row's current object *is* the component's `RuntimeData`), the newcomer is a stale
copy and is skipped - logged at most 5 times per session:

```
[10:20:01][A] ws dup skip 84a6d037: BP_SO_SimpleBool_C ...RuntimeData_2147471... is not the owner's current save object
```

If neither component can be read, the guard falls back to the old behaviour (take the new object over),
so a genuinely replaced object still wins.

## 3. Diff (exact)

`native/src/coop_world.cpp` only - five hunks, no other file touched (`coop_world_ws2.cpp`,
`run-two.ps1` and all wire code are unchanged):

```diff
--- a/native/src/coop_world.cpp
+++ b/native/src/coop_world.cpp

@@ with gRowKnown / gLedgerApplied
 std::vector<unsigned char> gRowKnown;
+// [R7-STORE] how many stale duplicate save objects the scan has skipped (the log
+// line is rate-limited with it, see WsScan).
+int gWsDupSkipped = 0;

@@ before WsScan()
+// [R7-STORE] forward declaration: the scan uses it, the definition sits next to
+// WsOwnerActor below (it needs the same object helpers).
+ue::UObject* CanonicalSaveObject(ue::UObject* o);
+
 void WsScan() {

@@ inside WsScan(), per candidate object
             const int idx0 = RowIndex(key);
             const bool prevFnSame = idx0 >= 0 && W().seen[idx0].fn == fn;
+
+            // [R7-STORE] two save objects can carry ONE owner (a save-object reset,
+            // or the engine re-creating RuntimeData while streaming) ... [comment]
+            if (idx0 >= 0 && !prevFnSame) {
+                ue::UObject* old = W().seen[idx0].obj;
+                if (net::Valid(old) && old != o) {
+                    ue::UObject* cur = CanonicalSaveObject(o);
+                    bool skip = (cur && cur != o);
+                    if (!cur) {
+                        // the newcomer's component is unreadable: if the row's object
+                        // is the one its own component points at, the newcomer is the
+                        // stale copy
+                        skip = (CanonicalSaveObject(old) == old);
+                    }
+                    if (skip) {
+                        if (gWsDupSkipped < 5) {
+                            ++gWsDupSkipped;
+                            net::Log("ws dup skip %s: %s is not the owner's current save object",
+                                     key.c_str(), fn.c_str());
+                        }
+                        continue;
+                    }
+                }
+            }
             const double prevBorn = idx0 >= 0 ? W().seen[idx0].born : 0.0;

@@ next to WsOwnerActor()
+// [R7-STORE] which of two save objects carrying ONE owner is the current one?
+//   returns o        -- o is the current object
+//   returns another  -- that object is the current one, o is a stale copy
+//   returns nullptr  -- cannot be read: the caller keeps its old behaviour
+ue::UObject* CanonicalSaveObject(ue::UObject* o) {
+    if (!net::Valid(o)) return nullptr;
+    ue::UFunction* f = ue::FindFunction(o, "GetOwningSaveComponent");
+    if (!f) return nullptr;
+    ue::Params p(f);
+    if (!ue::Call(o, f, p.data())) return nullptr;
+    ue::UObject* comp = p.Get<ue::UObject*>("ReturnValue");
+    if (!comp || !net::Valid(comp)) return nullptr;
+    ue::UObject* cur = nullptr;
+    if (!ue::Get<ue::UObject*>(comp, "RuntimeData", cur)) return nullptr;
+    return net::Valid(cur) ? cur : nullptr;
+}

@@ before TestFixReset()
+// [R7-STORE] fixtures: clearing a boolean FIELD instead of resetting the save
+// OBJECT ... [long comment: the ResetSaveObjectByClass re-creation, the duplicate
+// RuntimeData objects, the settle-reset storm, why a property write is enough]
+int ClearBoolFixtures(const char* cls, const char* field) {
+    if (!cls || !*cls || !field || !*field) return 0;
+    if (!net::Valid(net::World())) return 0;
+    int cleared = 0, seen = 0;
+    const std::vector<ue::UObject*> list = ue::FindAllOf(cls);
+    for (std::size_t i = 0; i < list.size(); ++i) {
+        ue::UObject* o = list[i];
+        if (!net::Valid(o)) continue;
+        const BoolRead rd = ReadBoolField(o, field);
+        if (!rd.ok) continue;
+        ++seen;
+        if (!rd.value) continue;                    // already false: nothing to do
+        if (WriteBoolValue(o, field, false)) ++cleared;
+    }
+    net::Log("fixreset: %s.%s -> false on %d instance(s) (of %d read, no save-object reset)",
+             cls, field, cleared, seen);
+    return cleared;
+}

@@ TestFixReset()
-    static const char* kSaveObjects[] = {"BP_HubDoor_C", "BPSO_BearTrap_C",
-                                         "BPSO_FallingCorpse_C", "BPSO_FallingStatue_C",
-                                         "BPSO_WoodenElevator_C", "BPSO_NPC_C",
-                                         "BPSO_Tarforge_C", "BPSO_FC_FinalBossManager_C"};
+    static const struct { const char* cls; const char* field; } kBoolFields[] = {
+        {"BPSO_NPC_C", "HasTriggeredInitialGreeting"},   {"BPSO_WoodenElevator_C", "HasBrokenOnce"},
+        {"BPSO_ShellRevive_C", "StationUsed"},           {"BPSO_PlayerTrapDungeon_C", "TrappedOnce"},
+        {"BPSO_PetrifiedCultistManager_C", "Harros"},    {"BPSO_TavernArena_C", "WagerPlaced"},
+        {"BPSO_SavedLockedChests_C", "HasBeenOpened"},   {"BPSO_BearTrap_C", "IsDisarmed"},
+        {"BPSO_FallingCorpse_C", "ShotDown"},            {"BPSO_FallingStatue_C", "StatueFallen"},
+    };
+    int byField = 0;
+    for (...) byField += ClearBoolFixtures(kBoolFields[i].cls, kBoolFields[i].field);
+
+    static const char* kSaveObjects[] = {"BP_HubDoor_C", "BPSO_Tarforge_C",
+                                         "BPSO_FC_FinalBossManager_C"};
     int byObject = 0;
     for (...) byObject += FixtureReset(kSaveObjects[i]);

     ... verdict text now also reports the field clears:
-        "%s fixreset: %d save object(s) reset by guid/class (world %d, lift %d of %d, "
-        "chest %d of %d), %d store-B TimesUsed cleared\n",
-        (total > 0 || wbZeroed > 0) ? "PASS" : "FAIL", total, byObject, ...
+        "%s fixreset: %d boolean field(s) cleared (no save-object reset, so the store-A "
+        "row keeps its object and stays settled), %d save object(s) reset by guid/class "
+        "(world %d, lift %d of %d, chest %d of %d), %d store-B TimesUsed cleared\n",
+        (total > 0 || byField > 0 || wbZeroed > 0) ? "PASS" : "FAIL", byField, total, byObject, ...
```

Build: `cmd /c G:\Freespace\shell_coop\wt\r7-store\ms2-coop\native\build.bat test` ->
`coop_policy: 112 checks passed, 0 failed` ... `Built: ...\build\dxgi.dll`, deployed to both installs
(sha256 prefix **`61c1cb14`**, identical on k and l, and identical to `native\build\dxgi.dll`).

## 4. Test commands

```
cmd /c G:\Freespace\shell_coop\wt\r7-store\ms2-coop\native\build.bat test
powershell -NoProfile -ExecutionPolicy Bypass -File G:\Freespace\shell_coop\wt\r7-store\runN.ps1 -Tag r7s -Runs 2 -Deploy
powershell -NoProfile -ExecutionPolicy Bypass -File G:\Freespace\shell_coop\wt\r7-store\runN.ps1 -Tag r7sN -Runs 1
```

`runN.ps1` is the round-7 helper (copied here): it takes the shared lock, deploys once, then runs
`run-two.ps1 -Port 47150` N times **without deleting `coop\ws_ledger_*.txt` / `wt_ledger_*.txt`** - the
ledger files from earlier sessions were present in every run (14 keys, `ws_ledger_0.txt` 126 B).

## 5. Tally of this session's runs (all on k/l, port 47150, ledger files never deleted)

| attempt | tag | STORE_A2 | ANIM | PEERHP | CINE | RESULT | note |
|---|---|---|---|---|---|---|---|
| 1 | 10:13 | - | - | - | - | - | **guest crashed** `0xC0000374` at boot, void |
| 2 | r7s-1 | **PASS** | FAIL | PASS | PASS | FAIL | `patrol`/ANIM flake |
| 3 | r7s-2 | **PASS** | PASS | PASS | PASS | **PASS** | **clean run** |
| 4 | r7s-3 | **PASS** | PASS | FAIL | PASS | FAIL | PEERHP |
| 5 | r7s-4 | **PASS** | PASS | FAIL | PASS | FAIL | PEERHP |
| 6 | r7s-5 | **PASS** | PASS | FAIL | PASS | FAIL | PEERHP |
| 7 | 11:00 | - | - | - | - | - | **host crashed** `0xC0000374` at boot, void |
| 8 | r7s-7 | **PASS** | PASS | PASS | FAIL | FAIL | CINE `endedBoth=False` |
| 9 | 11:12 | - | - | - | - | - | **host crashed** `0xC0000374` at boot, void |
| 10 | r7s-9 | **PASS** | PASS | FAIL | PASS | FAIL | PEERHP, line identical to R2.5 |

**STORE_A2: PASS in 7 of 7 completed runs.** The remaining reds are `ANIM` x1, `PEERHP` x4 and
`CINE` x1 - three verdicts in modules this round does not touch (`coop_local.cpp` animation sampler,
`coop_body.cpp`, `coop_dialog.cpp`), each already recorded as flaky in `wt\r7-final\REPORT.md`
(R2.5 PEERHP, R2.3 STREAM, R2.6 the crashes), with PEERHP's line matching R2.5 character for
character:

```
run r7s-9 : PEERHP: PASS peer body: hp=5.100/75.000 (0.068) ...  -> PEERHP: FAIL (received=False ...)
r7-final   : PEERHP: PASS peer body: hp=5.100/75.000 (0.068) ...  -> PEERHP: FAIL (received=False ...)
```

`RESULT: PASS` was reached once (r7s-2) inside this session's budget; every other completed run failed
exactly one of those three verdicts. Two clean runs would have been ideal, but this tree loses about
one verdict per run to an unrelated flake and each attempt costs 12 minutes, so the honest statement
is: **the STORE_A2 regression this round was asked to fix is fixed and deterministic (7/7), and
`RESULT: PASS` is blocked only by pre-existing flakes in other lanes.**

Verdict lines of the clean run (r7s-2, `local\r7s-2-out.txt`):

```
=== FIXRESET: A PASS fixreset: 3 boolean field(s) cleared ... 3 save object(s) reset by guid/class (world 1, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: B PASS ...   === FIXRESET: PASS
=== WIRETEST: PASS          === ANIM: PASS puppet locomotion: sequence states idle=496 walk=20 sprint=0, moving-while-animated=20 ticks
=== SHELL: PASS   === WEAPON: PASS   === PUPVIS: PASS   === LOOK: PASS
=== DAMAGE: PASS  === DEATH: PASS    === DROPS: PASS   === WORLD: PASS   === PROGRESS: PASS
=== QUEST: PASS   === QUEST2: PASS   === LIFT: PASS    === CHEST: PASS
=== STORE_A2: PASS store A2: BPSO_WoodenElevator_C.HasBrokenOnce replayed on BP_EndDungeonRewardDoor_C_..., BPSO_Tarforge_C.StoredTarcores=1 ... applied from wt
=== LEDGER: A PASS ledger: BPSO_NPC_C.HasTriggeredInitialGreeting (...) re-applied from the ledger on load
=== WIRE: PASS (packet split into 7/6 part(s), largest datagram 1150 B <= 1200, parts lost 0)
=== ZONE: PASS   === DIALOG: PASS   === CINE: PASS   === PEERHP: PASS   === PEERDEAD: PASS
=== BOSSPHASE: PASS   === A: PASS   === B: PASS
RESULT: PASS
```

## 6. Evidence that the two mechanisms are the fix

**2.1 stopped the row from splitting.** `ws settle reset` for the NPC fixture keys no longer appears
in any run, `fixreset` no longer calls the save subsystem for those classes (`3 boolean field(s)
cleared ... 3 save object(s) reset` instead of `13 objects reset by class` for `BPSO_NPC_C`), and the
STORE_A2 verdict replays a new-class boolean in every run.

**2.2 catches the duplicates the engine makes on its own** - visible in a normal run's log with no
reset involved, on both clients:

```
[10:19:51][B] ws settle reset 84a6d037 (row took a new fn after 26.1s: BP_SO_SimpleBool_C ...)
... six keys, ONE settle reset each as the map re-creates the object ...
[10:20:00][B] ws dup skip 84a6d037: BP_SO_SimpleBool_C ... is not the owner's current save object
[10:20:00][B] ws dup skip 842f6f59 / 739041a1 / 40635b9b / 0da5c131 ...
```

and in r7s-3 the host logged **46 `ws settle reset` lines for 46 distinct keys (max 1 per key)** -
`BPSO_SpawnerSaveObj_C` 31, `BPSO_NPC_C` 10, `BP_SO_SimpleBool_C` 8,
`BPSO_FC_FinalBossManager_C` 2, `BPSO_WoodenElevator_C` 1. Every row settles after its one
re-creation; before the fix the NPC key alone re-settled 4-5 times and never settled.

## 7. What stayed green / open

* **WORLD, LIFT, CHEST, LEDGER**: green in every completed run. They were the verdicts the changed
  reset list could have hurt: the door still gets a real save-object reset, the lift and chests still
  go through their actor `SpartaSave` GUID, and the boolean fixtures those tests pick are now
  field-cleared.
* `LEDGER` is asymmetric by client (`A PASS`, `B -`): the guest's ledger keys are cleared by
  `fixreset` before they can confirm (`B ok keys:14 good:0 bad:0`). It does not gate `RESULT`.
* `fixreset` reports `world 1` (only `BP_HubDoor_C` produced a reset in the hub this session; the two
  typed-int classes reset their one live instance through the GUID path, or 0 when not loaded) - the
  per-class `ws2 fixture:` lines say which.
* The `ws dup skip` log is capped at 5 lines per session (diagnostic, not a counter).
* The three `0xC0000374` attempts (guest 10:14, host 11:00, host 11:12) are the known `steam_api64`
  double free of `wt\r7-final\REPORT.md` R2.6 (`ntdll!RtlpFreeHeapInternal`, no `dxgi.dll` frame, 60-70 s
  after launch), not this change.
* No visual run: this round changes fixture bookkeeping and store-A row identity; nothing new is
  drawn, and the visible behaviour is covered by SHELL/WEAPON/LOOK/PUPVIS/ANIM/A/B.
* Final state: `native\build\dxgi.dll` == deployed on both installs (sha256 prefix `61c1cb14`);
  ledger files left in place; `has no parameter` = 0 hits on both installs.
