# Round 9 — r9-quest: multi-stage quest sync on a real quest

Workspace: `G:\Freespace\shell_coop\wt\r9-quest`. Host **A** = `E:\Games\_MS2-coop-g`,
guest **B** = `E:\Games\_MS2-coop-h`, relay port **47130**. Windows.

**VERDICT: `QUESTSTAGE: PASS`** (walk A to B and B to A, plus the late join), and
**`run-two.ps1 RESULT: PASS`** on g/h with the fix in place.

| item | label |
|---|---|
| per-stage sync of a multi-stage quest, A driving | **VERIFIED** |
| per-stage sync, B driving (host follows the guest) | **VERIFIED** |
| late join / reconnect catches up (stage + objectives + turned) | **FIXED + VERIFIED** |
| stage/objective TEXT from the game's own source, compared in the log | **VERIFIED** (6c) |
| the journal's own widget rows read live (`WBP_QuestLogEntry_Objective_C`) | **VERIFIED on both clients** — `Retrieve Vlas's Wares 1/3, 2/3, 3/3` (6c/6d) |
| the journal PAGE photographed on screen | **PARTIAL** — the game's own menu opens and is photographed, but three real-input attempts did not switch to the journal tab (6d: exact reason + next probe) |
| `QUESTSTAGE_OFF.txt` kill switch | implemented in code, **UNTESTED** (not exercised in a run) |
| regression suite | **VERIFIED** (`RESULT: PASS`) |

---

## 0. Starting point

At the previous hand-over the workspace had no Round-9 code at all (`native\src` identical
to `local\r9-base`), so the first pass was a baseline test of the **merged** quest sync. It
found the defect below and stopped at FAIL. This pass fixes it and verifies it.

## 1. The defect (as measured before the fix)

A late joiner applies the peer's whole quest snapshot in map order. `WqApplyQuest` treated
a turned-in quest as history:

- kind `'s'`: `if (ue::Get<bool>(o, "IsTurnedIn", turned) && turned) return false;`
- kind `'o'`: `if (turned || (int)cur > stage) return true;`

When `c|`/`u|` landed before `s|`, B created the quest with `CompleteQuest`/`TurnInQuest`
at **stage 1**, and from then on both guards discarded the stage and objective entries for
the rest of the session:

```
A: stage=2/0 name='Chapter 1: Ill-Gotten Gains' objs=[VlasQuest_Item_Ch_01 3/3] turned=1
B: stage=1/0 name='Chapter 1: Ill-Gotten Gains' objs=[VlasQuest_Item_Ch_01 0/3] turned=1   (45 s, unchanged)
B log: `wq apply base add c|VlasQuest = 1` and NO `s|`/`o|` line, ever
```

## 2. The fix (targeted; apply order unchanged)

`coop_progress.cpp`, `WqApplyQuest`:

- **kind `'s'`**: the turned-in early return is now
  `if (turned && Qs9CatchUpOff()) return false;`. When this side is turned in **and
  behind**, the stage INDEX is adopted directly —
  `Set<int32_t>(o, "CurrentQuestStage", peerV-1)` + `InitializeQuest(o)`, read back — and
  **no objective is heard**, so the game's own completion/reward path does not run a second
  time on this side. Logged as
  `wq stage <id> -> N catch-up on a turned-in quest (name 'x' -> 'y')`.
- **kind `'o'`**: `if ((int)cur > stage) return true;` (left stages are still history),
  then `if (turned && Qs9CatchUpOff()) return true;`. A turned-in quest whose counter is
  behind gets the counter written through `ue::MapInsertNameInt` + read-back (the same
  writer the normal path already uses), **without** re-entering `OnObjectiveIDHeard`.
  Logged as `wq apply count <id> = N catch-up on a turned-in quest`.

**Kill switch:** `coop\QUESTSTAGE_OFF.txt` makes `Qs9CatchUpOff()` true, and both paths then
behave exactly as merged (a turned-in quest is history). Same shape as the other `*_OFF.txt`
switches (checked once per session). It is **UNTESTED** — I did not spend a run on it.

**Not done on purpose:** the global apply-order sort (`a` `s` `o` `c` `u`) tried in the first
pass is gone. It starved the 4-per-tick apply budget and made a normal stage take >25 s (was
0.6-5.5 s). The fix above keeps the order and only changes what "turned in" means.
The stale "OPEN (not fixed)" comment at `WqApply`'s `ents` build was rewritten to describe
the fix, so the source and the shipped dll agree.

## 3. Instruments (my only native changes)

`[R9-QUEST]` block in `coop_progress.cpp`, dispatch in `coop_net.cpp`, declarations in
`coop_state.h`. No wire key.

| verb | what it does |
|---|---|
| `qlist` | DT_QuestData1 row names to `coop\result_qlist.txt` (6 rows here) |
| `qsid <QuestID>` | `AddNewQuest` + `CurrentTrackedQuest` + `TrackQuests` + `InitializeQuest` |
| `qnext <QuestID>` | one stage: hears the current stage's objectives (the game's own `OnObjectiveIDHeard`), 2.5 s settle, and only if the stage did not move the direct write + `InitializeQuest`; stops instead of writing when `IsComplete` |
| `qstage <QuestID>` | one-line snapshot, including the stage **Description** and each objective's own **FText** |
| `qlog <SubTabIndex>` | opens the game's own menu: `BPC_UserInterfaceHandler::HandleGameMenu(SubTabIndex, AllowClose)` |
| `qwidget` | dumps the live journal rows (`WBP_QuestLogEntry_Objective_C`) and the `Text` blocks they bind |

`qstage` line (compared byte for byte between clients):

```
<id> stage=<cur+1>/<count> name='<CurrentStageDetails.StageName>' desc='<...Description>'
     journal='<QuestDetails.LogDescription>' track='<TrackingDescription>' qdoff=384 qn=''
     stageNames=[...] objs=[<id> <have>/<qty> txt='<ObjectiveName FText>' od='<Description FText>']
     turned=<0|1>
```

`QuestDetails` on the live object is still **empty** in this build (`stages=0 qn=''`), while
`CurrentStageDetails` is populated — that is why the stage row and the objectives are the
text that can be compared.

## 4. `quest-stages.ps1` (repeatable)

`<ws>\ms2-coop\quest-stages.ps1 [-Driver A|B] [-QuestID ...] [-MaxStages n] [-NoDeploy]`:
lock, save backup, deploy, both clients + relay, wait `phase=sync` **and**
`wqScan ... primed=1` (wqApply is gated for 10 s), `qlist`, for each candidate `qsid` +
`qnext` once and see whether the stage moved, walk the chosen quest stage by stage
**polling the follower until it matches** (a single sample measures latency, not
correctness), then the late join: stop the **follower**, advance a second quest twice on the
driver, restart it, wait for `primed`, poll. Then the journal phase (`qlog 0..5` +
`qwidget`) and a grep of both native logs for `has no parameter` (fail if any). Boot is
retried up to 3 times (known init race). `-Driver B` swaps the roles, so the guest drives
and the host follows.

`journal-shot.ps1` is the on-screen UI probe (game menu + real synthetic input +
screenshots).

## 5. Files changed (ws only)

| file | change |
|---|---|
| `native\src\coop_progress.cpp` | the fix in section 2 + `Qs9CatchUpOff()`, journal text in `Qs9State` (`Qs9CurStageDesc`, `Qs9ObjText`), the 6 probe verbs, updated comment at `WqApply` |
| `native\src\coop_net.cpp` | dispatch: `qlist`, `qsid`, `qnext`, `qstage`, `qlog`, `qwidget` |
| `native\coop_state.h` | 6 declarations |
| `ms2-coop\quest-stages.ps1` | `-Driver`, journal phase, driver-aware labels |
| `ms2-coop\journal-shot.ps1` | new: menu + real key/mouse input + screenshots |
| `REPORT.md` | this file |

Build: `build.bat test` gives `coop_policy: 112 checks passed, 0 failed`, AOB self-check
PASS, `Built: ...\dxgi.dll` — before and after the fix; the final tree is the fixed one and
was rebuilt after the comment change.

## 6. Results

### 6a. Per-stage walk — VERIFIED (both directions)

Driver **A** (`lab\20261102-121119-queststages`), GorfQuest, one comparison per stage, the
follower polled until it matched:

```
stage 1 match=True after 0.6 s   A/B: stage=2/0 name='' objs=[] turned=0
stage 2 match=True after 5.2 s   A/B: stage=3/0
stage 3 match=True after 5.3 s   A/B: stage=4/0
stage 4 match=True after 5.2 s   A/B: stage=5/0
```

Driver **B** (`lab\20261102-121640-queststages`) — guest drives, **host follows**:

```
stage 1 match=True after 0.6 s
stage 2 match=True after 5.1 s
stage 3 match=True after 5.2 s
stage 4 match=True after 3.0 s
```

Same convergence band as the merged code (0.6-5.5 s), so the fix did not slow the normal
path. `GorfQuest`'s stage name is `''` in the row, so this leg proves the **index**; the
text leg is 6c.

### 6b. Late join / reconnect — FIXED + VERIFIED

Run with **A driving** (B killed, A advanced VlasQuest twice, B restarted):

```
=== QUESTSTAGE latejoin match=True after 0.2 s (quest VlasQuest)
  A: stage=2/0 name='Chapter 1: Ill-Gotten Gains'
     desc='First you must find Vlas's wares so he can reest...'
     objs=[VlasQuest_Item_Ch_01 3/3 txt='Retrieve Vlas's Wares' od='Retrieve Vlas's Wares'] turned=1
  B: stage=2/0 name='Chapter 1: Ill-Gotten Gains'
     desc='First you must find Vlas's wares so he can reest...'
     objs=[VlasQuest_Item_Ch_01 3/3 txt='Retrieve Vlas's Wares' od='Retrieve Vlas's Wares'] turned=1
```

Run with **B driving** (A killed, B advanced twice, A restarted):

```
=== QUESTSTAGE latejoin match=True after 0.5 s (quest VlasQuest)
  B(driver):   stage=2/0 ... objs=[VlasQuest_Item_Ch_01 3/3 txt='Retrieve Vlas's Wares'] turned=1
  A(follower): stage=2/0 ... objs=[VlasQuest_Item_Ch_01 3/3 txt='Retrieve Vlas's Wares'] turned=1
```

Stage, objectives and turned state all match, in both directions — the exact state that
stayed `1 / 0-of-3` for 45 s before the fix.

### 6c. The stage TEXT, read from the game's own source — VERIFIED

Two independent reads, both compared in the log across the two clients:

1. **`qstage`** reads the live `UQuestObject` structs: `CurrentStageDetails.StageName` and
   `.Description` (FText at +0x00/+0x10) and each
   `CurrentStageDetails.Objectives[i].ObjectiveName`/`.Description` (FText at +0x00/+0x10 of
   the 0x38-byte element). The strings above — `Chapter 1: Ill-Gotten Gains`,
   `First you must find Vlas's wares so he can reest...`, `Retrieve Vlas's Wares` — are the
   game's own text, identical on A and B in every line of 6a/6b.
2. **`qwidget`** reads the journal's own widget rows (`WBP_QuestLogEntry_Objective_C`, the
   class that builds the journal page) and the `UTextBlock.Text` they bind:

```
[12:27:34][A] qwidget: 3 live WBP_QuestLogEntry_Objective_C row(s) -- the journal's own objective lines
[12:27:34][A] qwidget row: None|None text='Retrieve Vlas's Wares 1/3' desc=''
[12:27:34][A] qwidget row: None|None text='Retrieve Vlas's Wares 2/3' desc=''
[12:27:34][A] qwidget row: None|None text='Retrieve Vlas's Wares 3/3' desc=''
```

So the text the journal shows and the text the quest object carries agree, and both agree
between host and guest (A=3 and B=3 rows in the `E` run). `QuestID`/`ObjectiveID` read back
as `None` on the widget instance — a gap in my dump, not in the game.

### 6d. The journal ON SCREEN — PARTIAL, with the exact reason

`qlog <index>` really opens the game's own menu: screenshot
`lab\20261102-121119-queststages\journal-A-tab3.png` shows the INVENTORY page with its tab
strip (INVENTORY, MAP, two icons), the item grid, the item detail panel and the
`[Q] Close  [E] Navigate` hint. `HandleGameMenu` returns `ok=true` for every `SubTabIndex`
0..5, but **the visible tab never changes** — all six screenshots are the inventory page.

Three real-input attempts, each followed by a screenshot that I looked at
(`journal-shot.ps1`: focused window, `keybd_event`/`mouse_event`, the SendInput path):

| input | what happened |
|---|---|
| `Right` x5 (`lab\20261102-122554-journalshot`) | moves the **item cursor** (TROLLWEED to WELTCAP); tab strip unchanged |
| `E` x4 (`lab\20261102-124008-journalshot`) | page unchanged; **the journal's own rows appeared on BOTH clients (A=3, B=3)** |
| left-click at `(310,50)`, `(580,50)`, `(640,50)` — MAP text and the two icon tabs (`lab\20261102-124857-journalshot`) | the pointer moves to the click point (visible in `A-click2-580_50.png`); page stays INVENTORY |

So the journal **page** is UNTESTED, and the reason is concrete: `SubTabIndex` is not the
visible-tab selector; `Right`/`Left` are item navigation here; `E` and mouse clicks on the
strip do not switch it either — the menu drives a **virtual cursor**
(`IsVirtualCursorEnabled` / `SnapVirtualCursor` / `UpdateVirtualCursor` exist on the same UI
handler), so a raw `SetCursorPos` + click at a screenshot coordinate is not the same as
moving that cursor. Next probe, cheap and specific: with the menu open, call
`SnapVirtualCursor` toward the tab strip and click, then read `GetActiveDisplayMenu`; and
take the tab binding from `IA_Menu_*` / `IMC_*` instead of guessing coordinates.

**Retraction:** the previous revision flagged "A=3 journal rows, B=0" as a possible
guest-side UI gap. The `E` run shows **A=3, B=3**, so that was my synthetic input landing on
one window, not a sync or UI defect. Withdrawn.

What IS verified about the journal without the page image: its own row widgets exist on both
clients and carry the same text as the quest object (6c).

### 6e. Regression suite — VERIFIED

`run-two.ps1 -GameA E:\Games\_MS2-coop-g -GameB E:\Games\_MS2-coop-h -Port 47130` with the
fixed dll: `RESULT: PASS`, including `QUEST: PASS`, `QUEST2: PASS`, `PROGRESS: PASS`,
`STORE_A2: PASS`, `WIRE: PASS`, `STREAM: PASS`, plus the death/camera/dialog/zone lanes.

## 7. Real play vs forced — stated plainly

- **Forced, not played.** No walking, no NPC, no dialogue: quests are started with the
  game's own `AddNewQuest` / `CurrentTrackedQuest` / `TrackQuests` / `InitializeQuest`, and
  each stage is completed through the game's own `OnObjectiveIDHeard`.
- **The engine's own transition did run** where the row has objectives: for `VlasQuest` the
  log says `qnext ... the game advanced 1 -> 2 on its own`. `GorfQuest`'s row has no
  objectives, so each of its stages used the direct-write fallback — the same write the
  sync uses.
- **Real:** the 2 Hz wire path and the real relay; the late join is a real process kill and
  reconnect of the game client (relay left running); the menu is the game's own menu and the
  key/mouse presses are real synthetic input.
- **Not done:** reaching the journal tab on screen (6d); the kill switch was not exercised.

## 8. Verdict

`QUESTSTAGE: PASS` — the per-stage sync of a multi-stage quest is correct in both directions
(GorfQuest stages 2 to 5 identical on A and B, converging in 0.6-5.3 s), and the late join
now catches up in 0.2-0.5 s with the same stage, the same `3/3` objective counters, the same
objective FText and the same turned-in flag; `run-two.ps1` is `RESULT: PASS`. The remaining
gap is the on-screen journal page (6d), reported as UNTESTED with the exact reason and the
next probe, not glossed over.
