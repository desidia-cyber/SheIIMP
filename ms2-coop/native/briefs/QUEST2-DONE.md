# QUEST2 — DONE (progress lane)

Sub-agent report for the QUEST2 verdict (`run-two.ps1` / guest `result_qs2.txt`).
Files owned and changed: `native\src\coop_progress.cpp`, NEW `native\src\ms2_map.cpp`,
declarations appended to `native\src\ms2_ue.h`. Nothing else was touched.

Build: `cmd /c native\build.bat test` → **0 errors, 0 warnings**, `test_api PASS`, AOB
self-check PASS, export table unchanged. Backups: `native\src\_bak\*.20261025-065608`.

---

## 1. The failure was the READER, not the INSERT

The brief's premise — "inserting a NEW key … the Lua used `TMap:Add`, ue::MapAddStrInt
only overwrites" — is wrong for this map, and the evidence is direct:

* the last run-two log said `MapAddStrInt(NPCRelationships, "Gorf"): key is not in the
  map` and `wq: NPCRelationships path=raw rows=0`;
* a read-only probe of the SAME map on the same save (below) shows **seven keys, and
  `Gorf` is one of them**:

```
mapprobe: qlog.NPCRelationships hdr num=7 max=8 data=0x137F0064900 bitCount=7 bitMax=128 free=-1/0
mapprobe: qlog.NPCRelationships raw 00 49 06 F0 37 01 00 00 07 00 00 00 08 00 00 00 7F ...
mapprobe: qlog.NPCRelationships row 'Sester Genessa' = 0
mapprobe: qlog.NPCRelationships row 'Gorf' = 0
mapprobe: qlog.NPCRelationships row 'Vlas' = 0
mapprobe: qlog.NPCRelationships row 'Collector' = 0
mapprobe: qlog.NPCRelationships row 'Thestus' = 0
mapprobe: qlog.NPCRelationships row 'Blacksmith' = 0
mapprobe: qlog.NPCRelationships row 'Tavern NPC' = 0
mapprobe: qlog.NPCRelationships rows(this reader)=7
```

`NPCRelationships` is seeded from the Blueprint defaults with every hub NPC, so
main.lua's `q.NPCRelationships:Add(npc, rel+2)` (3224) is an **overwrite**, and the
`rel` step failed because the old walker could not see a key that was there.

### Why it could not see it

Two independent defects in the shared walker, both confirmed against live memory:

1. **This build's `TBitArray` keeps its bits INLINE for ≤ 128 elements.** The probe's raw
   header gives the layout: bits at map+0x10 (7 elements → `0x7F`), `NumBits` at +0x28
   (= 7), `MaxBits` at +0x2C (= `0x80` = 4 words), `FirstFreeIndex` at +0x30 (-1),
   `NumFreeIndices` at +0x34 (0). `ms2_ue.cpp` reads a *heap* bit array (ptr +0x10,
   num +0x18, NumBits +0x20), so `NumBits != Num` and the allocation flags are dropped.
2. **The no-flags fallback decodes every slot's key as an FName**, while these maps are
   keyed by **FString** (16-byte key). So the fallback rejected every slot:
   `rows=0`, and `MapFindKey` therefore also said "key is not in the map".

Both are *read* defects, which is why `wqScan` published `r|`/`e|` = 0 and why
`ue::MapAddStrInt` refused a key that existed.

## 2. The fix: `native\src\ms2_map.cpp` (new)

A TMap view built on what the probe measured:

| what | offset in this build |
|---|---|
| `TArray<Element> Data` (ptr,num,max) | 0x00 / 0x08 / 0x0C |
| allocation bits (inline words) | 0x10 (4 words when `MaxBits <= 0x80`) |
| `TBitArray::NumBits` / `MaxBits` | 0x28 / 0x2C |
| `FirstFreeIndex` / `NumFreeIndices` | 0x30 / 0x34 |

* `ReadMapView()` resolves the property (reflection `PropOffset`), the header, the bit
  array (inline vs heap), and the element stride from the pair's own alignment
  (`TPair<FString,int32>` → value@16, pair 20 → align 8 → **stride 32**;
  `TPair<FName,int32>` → value@8, pair 12 → **stride 20**), then verifies the stride by
  decoding keys and falls back to the other candidate if it does not.
* A slot is read only when **its allocation bit is set AND its key decodes as the right
  type** — the same hard rule the module already used, so a wrong stride still yields
  "fewer rows, never a fault".
* Public API (declared in `ms2_ue.h`): `MapWalkStrInt`, `MapWalkNameInt`,
  `MapFindStrInt`, `MapFindNameInt`, `MapSetStrInt`, `MapSetNameInt`,
  `MapInsertStrInt`, `MapInsertNameInt`, `MapProbe`.
* `MapSet*` = find + write + **read back through the same walk**; true only if the value
  is really there.

`coop_progress.cpp` now uses these:
* `WqScan` (NPCRelationships/NarrativeEvents) → `ue::MapWalkStrInt`  (was `WalkMapProp`)
* `WqProgress` (CurrentQuestObjectiveProgress) → `ue::MapWalkNameInt`
* `RelRead` → `ue::MapFindStrInt`
* `Qs2Test` (main.lua:3224) → `ue::MapInsertStrInt`
* `WqApplyQuest` `r|`/`e|` (main.lua:3011) → `ue::MapInsertStrInt`
* `WqApplyQuest` `o|` (main.lua:2919) → `ue::MapInsertNameInt`

`WalkMapProp`/`MapForEach` are untouched (store B still uses them); the old
`ue::MapAddStrInt/NameInt` are untouched too.

## 3. The engine's own TMap insert (a real NEW key), found and proved

For a key that genuinely is absent, the insert is done by the engine, not by this
layer — the only sound way, since it needs the game's allocator, the property's
`GetValueTypeHash` and a rehash.

**How it was located.** `UBlueprintMapLibrary::execMap_Add` in the shipped exe is at
`rva 0x3B04700` (read from the live `UFunction`'s `ExecFunction` pointer, logged by the
probe). Disassembling it shows the ByteCode-frame parameter stepping
(`Stack.StepCompiledIn` at 0x161d870 / 0x161d8a0) and, once map/key/value are in
registers, exactly one call:

```
0x03B04906  mov rcx, [rbp]        ; Map   (MostRecentPropertyAddress of the map param)
0x03B0490D  mov r8,  rdi          ; Key   (initialised temp of the key property)
0x03B04910  mov rdx, r13          ; FMapProperty*
0x03B04903  mov r9,  r14          ; Value (initialised temp of the value property)
0x03B0491E  call 0x3AEEAD0        ; << the engine's "add a pair to a map property"
```

`Map_Find` and `Map_Contains` call the sibling `0x3AEEE30` the same way, which is what
identifies the pair. (This also proves the draft's "call Map_Add with a 0x30 wildcard
frame" idea cannot work: the thunk reads its parameters off the UFunction's bytecode
stream, not off the frame.)

**Why it is trustworthy.** The call site fixes the contract
`(TMap*, FMapProperty*, const Key*, const Value*)` — the same four registers. It is
called with the prologue byte-checked (`48 85 C9 0F 84` = `test rcx,rcx; je`), with SEH
around it, and every insert is read back through our own walk; one bad read-back disables
inserts for the session.

**Throwaway-map proof (the experiment the brief asked for).**
`MAP_INSERT_TEST.txt` asks it to add a NEW key to `WormfishGold` — the map the draft
named:

```
mapprobe: insert test -- about to call the engine add for a NEW key 'MAPPROBE_INSERT' on WormfishGold
mapprobe: insert test -- called=1 num 2 -> 3 found=1 value=1234 PASS
mapprobe: qlog.WormfishGold hdr num=3 max=22 ... bitCount=3 bitMax=128   (the engine grew 2 -> 22)
mapprobe: qlog.WormfishGold row 'GoldToGive' = 0
mapprobe: qlog.WormfishGold row 'TotalGoldGiven' = 0
mapprobe: qlog.WormfishGold row 'MAPPROBE_INSERT' = 1234                (both old pairs intact)
```

No pair was lost, `Num` went 2 → 3, the new pair reads back with the right value, and the
process kept running (no fault, no crash dump). The element array was reallocated by the
engine itself (`max` 2 → 22), so the allocator/hash/rehash all stayed on the engine's
side. The key the engine stored is its own copy: `EngineInsert` passes a stack `FString`
and the engine's `CopyCompleteValue` duplicates it (the pair still reads correctly tens
of seconds later, and after the game's own ticks).

The `MapInsert*` API is therefore: overwrite when the key exists, engine insert when it
does not, read back either way.

## 4. Also fixed: the judge's arming deadline (why the turn-in was never counted)

`Qs2Judge` used to arm on the FIRST applied quest-state entry and give up 25 s later. The
host's quest ladder turns the quest in at **30 s** — so the guest wrote
`FAIL ... the turn-in never did` before the turn-in could arrive. The previous run's own
logs are unambiguous:

```
[04:50:56][B] qs2Judge: armed on k|GorfQuest (first quest-state entry applied)
[04:51:21][B] FAIL quest state GorfQuest: ... the turn-in never did ...
[04:51:27][B] wq apply live turnin u|GorfQuest = 1        <-- six seconds later, it arrived
```

Main.lua only starts its 15 s window after `u|<quest>` was applied as `turnin`
(main.lua:3261-3273), i.e. after the host's 30 s rung. The FAIL deadline is now **42 s**
from the first applied entry: later than the 30 s rung, still inside run-two's 60 s
window for `result_qs2.txt`. The PASS path is unchanged (and now reached in ~32 s).

## 5. Evidence from the single-client runs (instance B only)

All evidence runs used one build: `native\build\dxgi.dll`, sha256 `45223e38…`,
built 07:21:40 from this tree (insert enabled), deployed to `E:\Games\_MS2-coop-b`.

All runs: `local\test\probe-run.ps1` (a PID-scoped runner — `smoke.ps1` kills *every*
MortalShell2 process, which would have killed the perf agent's client on instance A; the
runner also snapshots/restores the save dir the client is actually redirected to).
Saves were restored after every run (`saves restored to ...\local-appdata\B\...`), no new
crash dumps, process alive at teardown.

**Host ladder** (`local\test\20261025-072309-hostqs2`), the exact lines the old failure
was about:

```
[07:25:06][A] qs2test GorfQuest added+tracked, rel Gorf 0 -> 2 ok=true
[07:25:06][A] qs2test steps: add=true track=true trackQuests=true rel=true (rel read back from the live map)
[07:25:12][A] qs2test heard GorfQuest_Item_01 ok=true now=0 (quest object=yes, stage objectives=0)
[07:25:18][A] qs2test stage -> 2 ok=true  (name '' -> '')
[07:25:18][A] qs2test after InitializeQuest: r|Gorf = 2 (present; wanted 2)
[07:25:36][A] qs2test completed+turned in GorfQuest ok=true  turnedin=true (quest object=yes)
```

and the scan that publishes the value to the peer, every tick from then on:

```
[07:24:39][A] wqScan: tags=8 a=0 c=0 k=0 s=0 o=0 r=7 e=0 ActiveQuests=0 primed=1 ...
```

(before the fix the same line was `r=0 e=0`, with `rows=0` in the map gate).

**Guest side** (`local\test\20261025-071555-mapread`), the receive/apply direction with
the same code:

```
[07:17:51][B] qs2test GorfQuest added+tracked, rel Gorf 0 -> 2 ok=true
[07:17:51][B] qs2test steps: add=true track=true trackQuests=true rel=true (rel read back from the live map)
```

**Guest apply path, exercised end to end without a second client.** A synthetic peer
publish line (`local\test\peer_A_*.txt`, written into `coop\peer_A.txt` after boot by
`local\test\probe-run.ps1 -PeerQueue`) drives the same `WpSet`/`WqApplyQuest` code the
guest runs in a real session. With `a|`, `k|`, `r|`, `o|` entries:

```
[07:28:27][B] wq apply live add a|GorfQuest = 1
[07:28:27][B] wq apply live add k|GorfQuest = 1
[07:28:27][B] wq apply live set r|Gorf = 2                      <-- the rel step, applied
[07:28:27][B] fn: no real FName for 'TestObj' -- rebuilt via Conv_StringToName
[07:28:28] map CurrentQuestObjectiveProgress: engine insert 'TestObj' = 1 ok (slots 1 -> 2, read back through our walk)
[07:28:27][B] wq apply live countset o|GorfQuest|0|TestObj = 1  <-- a NEW FName key, inserted
[07:28:27][B] wq apply live turnin u|GorfQuest = 1
[07:28:43][B] FAIL quest state GorfQuest: count o|GorfQuest|0|TestObj=countset, stage nil,
              tracked, rel r|Gorf=2, turned in (wq=5/0)
```

Three of the four verdict components (count, tracked, rel) plus the turn-in came
through; `stage nil` is an artefact of that synthetic payload delivering `s|` and `u|` in
the same tick (the turn-in wins, and `wqApplyQuest` then correctly refuses to move the
stage of a turned-in quest). The real host separates them by 18 s, which is why the next
run should be a PASS — and why the `Qs2Judge` deadline had to move past 30 s (§4).

**The timed variant of that harness** (`probe-run.ps1 -PeerQueue`, three publishes:
`a|k|r|` → `o|s|` → `u|`, exactly the host's ladder) was run to capture a full PASS line.
It did not complete: instance B's `dxgi.dll` was replaced at **07:30:06** by the parallel
performance lane's own build (same file deployed to A *and* B, sha256 `381f22c0…`, which
is not the build these runs used, `45223e38…`), the client then died in the main menu
after 3–15 s with **no crash dump**, and the retry could not even deploy because B's
`dxgi.dll` was in use (another client running). The evidence above was produced with the
progress lane's own build and is unaffected; the final verdict needs the integrator's
two-client run, which also removes the synthetic-peer variable entirely.

Saves: every run snapshotted and restored `local-appdata\B\MortalShell2\Saved\SaveGames`
(`saves restored to …` in each runner summary); no new `.dmp` on either run.

## 6. What the next run-two should show

* A and B: `wqScan: ... r=7 ...` (the seven seeded NPCs are visible on both clients).
* A: `qs2test ... rel Gorf 0 -> 2 ok=true` (was `ok=false`).
* B: `wq apply live set r|Gorf = 2`, then `apply` of `s|`, `k|`, `u|` as before.
* B: `result_qs2.txt` = **PASS** … `rel r|Gorf=2`, `turned in`, at ≈32 s after the
  command (was a FAIL at ≈26 s with `rel=no turnin=no`).
* Nothing new in either native log; no crash dump.

## 7. Residual notes (honest limits)

* `MapProbe` (`MAP_PROBE.txt`) and `MapInsertSelfTest` (`MAP_INSERT_TEST.txt`) are
  read-only diagnostics driven by file toggles in the coop dir; they are not part of the
  normal path and cost nothing when the files are absent (one `GetFileAttributesW` per
  5 s until the probe has run once).
* The insert is called with the four-register contract read off the shipped binary and
  guarded by a prologue check + read-back. A future game build that moves that routine
  will fail the prologue check (logged, inserts skipped) instead of calling something
  else.
* The walker's heap-bit-array branch (`MaxBits > 0x80`, i.e. >128 slots) has no live
  sample in this save; if its offsets were wrong the walk would fall back to key
  validation, i.e. fewer rows rather than a fault.
* `TMap<FString,int32>`/`TMap<FName,int32>` are the only shapes covered (that is all the
  progress module touches). Store B (`TMap<FName, FSpartaMapInt>` and friends) still uses
  `WalkMapProp`/`MapForEachRaw`, which work for it.
