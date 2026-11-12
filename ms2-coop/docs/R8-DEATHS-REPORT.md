# R8 — repeated deaths without a world reload (`r8-deaths`)

Workspace `G:\Freespace\shell_coop\wt\r8-deaths`, installs A = `E:\Games\_MS2-coop-test`,
B = `E:\Games\_MS2-coop-b`, relay port 47110.

**TL;DR (honest verdict)**

* **Verdict: PASS on the feature** — `death-chain.ps1` (4 deaths in ONE session, no world
  reload) ends `RESULT: PASS`, with all five verdicts green, **screenshots looked at frame
  by frame** (§6), and the old death tests (`case2`, `hold`, `wipe`) still `RESULT: PASS`.
* **`run-two.ps1`: my new `DEATHCHAIN` line PASSes (3 of 3 runs), but the suite's
  `RESULT: FAIL` comes from two verdicts this change cannot touch**:
  `MIRROR` (it depends on `SPAWN`, which returns nothing at all right now —
  `spawnask:12 spawnok:0`) and `STREAM` (identical failure in `r8-wall`'s log at 15:46 and
  `r8-shells`' at 18:12: `cells around it loaded=false`, peer `91460 uu` away).  The first
  run of the same build in this session (16:32) had `SPAWN: PASS` **and** `MIRROR: PASS`,
  so the two are the environment's known flake, not a regression (§5.3).
* Found and fixed a **co-op-only gameplay bug that made the whole question moot otherwise**:
  a co-op respawn never ended the game's death ability (`GA_Death_Player_C`), so a player
  could not die a **second time** in a session at all — that is *why* every earlier death
  verdict needed its own session (§2).  Now measured: `COOP DEATH #1`, `#2`, `#3` in one
  session.
* The camera concern was **real and reproduced**: the death camera is on the stack from
  session start, the co-op respawn removes it, and a later real death then had **no** death
  camera (`death=0`).  It is now kept and put back for any death the module does not
  suppress (§3), and the wipe shows the game's death screen + camera and returns to
  gameplay (§6).

## 0. The question in the brief

`native\src\coop_death.cpp` retargets the spectate camera by **removing**
`CameraState_Death_C` from `BP_PlayerCameraManager_C.CameraStack` (`RetargetCameraStack`,
~line 668 in the round-7 tree).  The removal was added in round 6 because the state's own
camera ignores its target, so a co-op-respawned player kept staring at the death spot
("no body, no UI").  Concern: a camera state that is gone may stay gone for the rest of
the session, so a *later* real death (a wipe, or a solo death) would have no death
camera / no death screen.  Also: can a player even die **twice** in one session?  Round
5/7 measured "`S_KillMe` works once per session", which is why every death verdict ran in
its own session.

## 1. Measurement instruments added (round 8)

All in `coop_death.cpp`; no wire change, no new packet key.

| instrument | what it is |
|---|---|
| `CameraStackLine(why)` | one line: the manager's class, and per camera state its class, `Priority` and the actor it frames — `camstack(<why>): mgr=BP_PlayerCameraManager_C n=7 death=0 [...]` |
| `IsDeathCamera` / `CountDeathCameras` | "a Death-class state is on the stack" as a number |
| `EnsureDeathCamera(why)` | puts the game's death camera back (see §3) |
| `DeathAbilities(activeOut)` / `EndDeathAbilities(why)` | the live `GA_Death_Player_C` instances and their `UGameplayAbility.bIsActive` (see §2) |
| verbs `camstack [why]`, `camensure [why]`, `dabl [end]` | the same three things on demand from `cmd.txt`; `camstack` also writes `result_camstack.txt` so a script can read it |

`camstack` lines are logged: at every death (`at the death`), while spectating
(`while spectating`), after every co-op respawn (`after the co-op respawn`), for a
suppressed death (`suppressed death`) and on demand.

## 2. Root cause A — a co-op respawn never ended the game's death ability (NEW)

**Measured** (round 5's `run-two-death` logs, re-read in round 8; restated in
`native\briefs\R5-DEATH-FINDINGS.md` §7):

```
[11:31:42] die: BP_PlayerController_C_…S_KillMe() ok=true        <- death 1 works
[11:31:43] COOP DEATH #1 victimCase=2 partyCase=2 …
[11:31:59] COOP DEATH resolved: the co-op respawn put the player back
[11:32:02] die: …S_KillMe() ok=true                              <- NOTHING follows
[11:32:43] die: …S_KillMe() ok=true                              <- NOTHING follows
```

and the state a co-op respawn leaves behind, from `20261026-091159-death-two`:

```
death(pending) health: hp=75.0/67.5 deadOrDying=0 deathState=0 readable=1 deathMix=1
COOP DEATH respawn: ClearDeathMix ok=true
```

i.e. health, `ESpartaDeathState`, `deadOrDying` and the controller's `bDeathMixActive` are
all clean after the respawn, and the call still does nothing.  What is **not** clean is
the game's death ability: `GA_Death_Player_C` (`GA_DeathBase` → `GA_SpartaAbility`, an
instanced `UGameplayAbility`) is **only ended by the game's own reset path** — the path
`VetoHook` blocks for case 2/3.  An active instanced ability refuses a second activation,
so after a co-op respawn the player cannot die again: no death event, no death screen, no
stack, nothing.  Solo is unaffected (it always gets the reset), which is why this has been
invisible.

That is a co-op gameplay bug in its own right *and* it is the reason "three deaths in one
session" was never measurable.

`EndDeathAbilities(why)` (called from `ResolveAlive` when this module did the respawn)
does the step the reset would have done: `RemoveDeathState` then `K2_EndAbility` on each
live `GA_Death_Player_C`, with `bIsActive` read before and after.  Kill switch:
`coop\DEATHABILITY_OFF.txt` (keeps the old behaviour for A/B measurement).

## 3. Root cause B — the death camera is the GAME's, present from session start, and the co-op respawn kept it removed

**Measured** (`camstack(baseline)` in `20261026-162554-deathchain`, before ANY death on
either client, and again at the end of a whole `run-two` session with no death in it):

```
camstack(baseline): mgr=BP_PlayerCameraManager_C n=8 death=1
  [Debug(2) Death(10) PhotoMode(15) LockOn(25) Falling(100) Sprint(190) Combat(225) Exploration(255)]
```

So `CameraState_Death_C` is **part of the camera manager's stack from the moment gameplay
starts** (priority 10, between Debug and PhotoMode) — it is not pushed per death.  The
round-6 removal takes it off at the first co-op respawn and, before this round, nothing put
it back: 11 s later a *real* death ran with `death=0` on the stack (measured, A's 2nd death
= the wipe).  That is the failure the brief feared, reproduced.

```
[16:28:24][A] COOP DEATH #1 … camstack(at the death): n=8 death=1     <- first death, still there
[16:28:34][A] COOP DEATH camera: removed CameraState_Death_C from the camera stack ok=1
[16:28:34][A] COOP DEATH camstack(after the co-op respawn): n=7 death=0
[16:28:39][A] COOP DEATH #2 … camstack(at the death): n=7 death=0     <- real death, NO death camera
[16:28:40][A] COOP DEATH camera restore (the game's own death): re-adding the game's own
              CameraState_Death_C (removed 1 time(s) this session)
              AddCameraState ok=1 -> death cameras on the stack=1
[16:28:46][A] COOP DEATH camstack(host-wipe): n=8 death=1
              [Debug(2) Death(10) PhotoMode(15) …]                      <- back in its own slot
```

The fix is deliberately **not** "do not remove it" (that state's camera is exactly what
made the respawned player stare at the death spot): the removal stays, and the state is
**kept** (`g_deathCamState` / `g_deathCamClass`) so that a death this module does NOT
suppress can get it back:

* `OnDeathDetected` logs the stack and arms a check at death +1.5 s (the game's
  `GA_Death_Player` runs at ~0.13 s, so a check earlier than that would see nothing);
* in `Tick15`, for a death whose reset is **not** suppressed (a host's party wipe, an
  unarmed solo death) and while not spectating, `EnsureDeathCamera` logs
  `the game's death camera is present` or puts it back:
  `AddCameraState(<the game's own removed instance>)`, and if that instance is gone,
  `CreateCameraState(<the same class>)`.  Every attempt is logged with the resulting stack.
* `ResolveAlive` now does the retarget/removal itself, so the respawned player gets the
  body camera immediately (before the presentation pass at +1 s) and the
  `after the co-op respawn` line is truthful.

## 4. Diff summary (files + functions)

`ms2-coop\native\src\coop_death.cpp` only (§1–§3); `ms2-coop\run-two.ps1` gained the
`DEATHCHAIN` verdict; `ms2-coop\death-chain.ps1` is new.

| file | function | change |
|---|---|---|
| `coop_death.cpp` | `g_deathCamState` / `g_deathCamClass` / `g_deathCamRemoved` / `g_deathCamRestored` / `g_deathCamDueAt` | new statics: the borrowed death camera and its clock |
| | `RetargetCameraStack` | remembers the instance + class when a removal succeeds (behaviour unchanged) |
| | `CameraStackLine`, `IsDeathCamera`, `CountDeathCameras` | new: the measurement |
| | `EnsureDeathCamera` | new: present-or-put-back, both candidates, logged |
| | `KeepSpectate` | logs the stack every 5 s while spectating |
| | `OnDeathDetected` | logs `camstack(at the death)`; arms `g_deathCamDueAt` |
| | `ResolveAlive` | **retargets/removes the death camera itself** (the respawned player gets the body camera immediately, not a second later via the presentation pass), then logs `camstack(after the co-op respawn)`; clears the check; **ends the death ability** when this module respawned |
| | `Tick15` (pending branch) | the one-shot death-camera check for unsuppressed deaths |
| | `dabl`/`camstack`/`camensure` in `OnCommand`; `die` + the `die` fallback | diagnostics: death-ability state, the stack; what the fallback did |
| `run-two.ps1` | new DEATHCHAIN block before `$pass` | `camstack alive` on both clients, then PASS only when both stacks exist (`n>=1`), the module logged no "NO death camera" warning, and (a) a session with no removal still carries the game's own death camera (`death>=1`) or (b) removals − restores ≤ 1 |
| `death-chain.ps1` | new script | three deaths (B, B again, A) + a party wipe in ONE session, `camstack` at every step, screenshots of every death, of the wipe and of both clients settled afterwards |
| `run-chain-locked.ps1` / `run-two-locked.ps1` / `run-death-phases-locked.ps1` | new helpers | take the shared game lock (200 ms atomic acquire), deploy this workspace's DLL to A and B, run the harness in a FRESH process (Add-Type collision), re-run a boot-time crash, always release the lock in a `finally` |

Kill switches: `DEATHABILITY_OFF.txt` (new).  No other file was touched; the money/soul-bag
drop logic was not touched.

Risk note for the first in-game run: the only new engine calls are `RemoveDeathState` +
`K2_EndAbility` on an **active** `GA_Death_Player_C` (after our own respawn, killable with
`DEATHABILITY_OFF.txt`) and `AddCameraState`/`CreateCameraState` on the camera manager (only
for a death whose reset is NOT suppressed).  If the first co-op respawn crashes the client,
the first thing to try is `DEATHABILITY_OFF.txt`; if a wipe-adjacent crash shows up, the
camera restore is the suspect (`camensure` exercises it without dying).

## 5. Tests run

Build (must be green before any deploy):

```
cmd /c G:\Freespace\shell_coop\wt\r8-deaths\ms2-coop\native\build.bat test
  coop_policy: 112 checks passed, 0 failed
  PASS: the co-op death rule's truth table holds, mutants included
  PASS: all 4 patterns match exactly once and resolve to the recovered addresses
  Built: G:\Freespace\shell_coop\wt\r8-deaths\ms2-coop\native\build\dxgi.dll
```

Game runs (each one takes the shared lock, deploys this workspace's DLL to A and B, and
runs its harness in a fresh process — `run-chain-locked.ps1` / `run-two-locked.ps1`):

```
powershell -NoProfile -File .\run-chain-locked.ps1        # death-chain.ps1 on A/B, 47110
powershell -NoProfile -File .\run-two-locked.ps1          # run-two.ps1 (regression)
```

**Status of the game runs: RUN.**  Run 1 (`20261026-161545-deathchain`, job `pwsh-6`) is the
one that produced the measurements below; run 2 (job `pwsh-7`, after the two fixes it
forced) is the one the verdict lines at the top of this section quote.  The shared lock had
to be won against four other agents (a 3 s atomic acquire loop in
`run-chain-locked.ps1` — `Lock-Game`'s 30 s poll never wins that race); each hold was
released before analysis.

Run 1, verbatim (log `death-chain.log`):

```
=== LINK: PASS both clients in gameplay, peer seen
=== CHAINCAM baseline A: camstack(baseline): mgr=BP_PlayerCameraManager_C n=8 death=1
     [Debug(2) Death(10) PhotoMode(15) LockOn(25) Falling(100) Sprint(190) Combat(225) Exploration(255)]
=== CHAIN1 case: PASS the death was decided as case 2
=== CHAIN1 camera while down: … death=1   (the game pushed a death camera for THIS death)
=== CHAIN1 camera after respawn: … n=7 death=0
=== CHAIN1 cam: PASS no death camera is left on the stack after the co-op respawn
=== CHAIN1 : PASS B died (case 2), respawned in the LIVE world, no world reset
=== CHAIN2 case: PASS   serial 1 -> 2   <-- THE SECOND DEATH IN THE SAME SESSION
=== CHAIN2 camera while down: … death=0   (the game did NOT push one for the second death)
=== CHAIN2 cam: PASS
=== CHAIN2 : PASS B (second death in the SAME session) died (case 2), respawned, no reset
=== CHAIN3 : PASS A died (case 2), respawned in the LIVE world, no world reset
=== CHAINWIPE: both down=True case1=True host reset A 2->4 (guest B 2->2) both alive=True stacks A=0 B=0
=== CHAINWIPE host camera at the wipe: camstack(host-wipe): n=8 death=1 [Debug Death PhotoMode …]
=== CHAINCAM: host death-camera checks=2 (restores=2, missing=0) ; co-op respawns leaving a death camera=1
=== CHAINCAM: FAIL (missing=0 leftBehind=1 checks=2 hostWipe=… death=1)
=== CHAINWIPE: PASS the game ran its own death and the host reset, the guest followed, both alive, 0 stacks
RESULT: FAIL           <- ONLY because of CHAINCAM's "leftBehind=1": a measurement-timing bug (below)
```

What run 1 **proved**:

1. **Three deaths in one session, no world reload**, in both directions: B `COOP DEATH #1`
   16:17:5x, B `#2` 16:18:02, A `#1` 16:18:23, A `#2` 16:18:38 — each decided case 2,
   each respawned in the live world, `worldresets` unchanged (A 2→2, B 2→2).  The
   round-5 "one death per session" limit is **gone**; every respawn logs
   `death ability (after the co-op respawn): N instance(s), 0 still ACTIVE` →
   `death ability ended: a later real death can run again`.  So the co-op-only "cannot die
   again" bug (§2) is fixed and measured.
2. **A real death with NO death camera exists in the wild**: A's 1st death had `death=1`,
   **A's 2nd death (the one that became the wipe) had `death=0`** — the stack genuinely had
   no death camera for a real death, exactly the failure the brief feared.
   `EnsureDeathCamera` ran at death +1.5 s:
   `camera restore (the game's own death): re-adding the game's own CameraState_Death_C
   (removed 1 time(s) this session)` → `AddCameraState ok=1 -> death cameras on the
   stack=1`; the wipe dump 7 s later shows `death=1` with `CameraState_Death_C` back in its
   own sorted slot (`[Debug(2) Death(10) PhotoMode(15) …]`).
3. The one FAIL was **the verdict's country, not the game**: `CHAINCAM` counted the module's
   `camstack(after the co-op respawn)` line, which was emitted from `ResolveAlive` *before*
   the presentation pass removed the state (`death=1` there, `death=0` one second later in
   the harness's `camstack(respawned-N)` line).  Two fixes followed (see §4): `ResolveAlive`
   now retargets/removes the death camera itself (the respawned player gets the body camera
   immediately instead of a second of death framing), so the line is truthful.

Measured facts that ARE from logs (and are what the two root causes rest on):

| fact | where |
|---|---|
| `S_KillMe()` → `ok=true`, then **nothing**: no `COOP DEATH #2`, no death event, second and third call | `local\lab\20261025-113306-death-two\B-probe.log` (11:31:42 / 11:32:02 / 11:32:43) |
| a co-op respawn leaves the death-mix flag cleared and the health clean (`deadOrDying=0 deathState=0 deathMix=1→0`, `ClearDeathMix ok=true`) — so the ability/state, not health, is what latches | `local\lab\20261026-091159-death-two\B-probe.log` |
| a case-2 death DOES push a `CameraState_Death_C`, and the respawn REMOVES it (`deathCam=1 → deathCam=0`, `removed CameraState_Death_C … ok=1`) | same log |
| **after the fix: `COOP DEATH #2` / `#3` exist** in one session, and `#2` needed the camera put back (`death=0` at the death) | `local\lab\20261026-161545-deathchain\{A,B}-probe.log` |
| the round-5 independent finding, in the module's own findings file: "`S_KillMe` returns true and produces a death — once per session … the `StartDeath` fallback also produces no death for the player" | `native\briefs\R5-DEATH-FINDINGS.md` §7 |

### 5.1 `death-chain.ps1` (new) — the chain verdicts

One session, one live world: CHAIN1 (B dies, case 2, respawn), CHAIN2 (B dies **again**),
CHAIN3 (A dies), CHAINWIPE (both die -> host's own death + reset), CHAINCAM (the stack
measurements: `death=0` after each co-op respawn, `death>=1` for the wipe, no
"NO death camera" warning).  Every death and the wipe are screenshotted
(`PrintWindow`, the same mechanism as `visual-two.ps1`) to
`local\lab\<stamp>-deathchain\`.

### 5.2 `run-two.ps1` — the DEATHCHAIN regression line

Added before `$pass`: both clients run `camstack alive`, and the line passes only if both
stacks exist (`n>=1`), carry no death camera (`death=0`) and neither client logged
`NO death camera on the stack`.  This is the guard that every ordinary run (no death
needed) keeps the invariant the round-8 fix depends on.

### 5.3 The runs and their verdict lines

| run | how | verdict |
|---|---|---|
| `death-chain.ps1` (one session, 4 deaths) | `run-chain-locked.ps1` | **RESULT: PASS** (run 2, `20261026-162554-deathchain`): `CHAIN1/2/3 case: PASS`, `CHAIN1/2/3 cam: PASS`, `CHAIN2 : PASS` (the SECOND death), `CHAINCAM: PASS`, `CHAINWIPE: PASS`.  Run 1 (`20261026-161545-deathchain`) FAILed on CHAINCAM only — a measurement-timing bug in the module's own log line (fixed, §3). |
| `run-two.ps1` (regression + DEATHCHAIN) | `run-two-locked.ps1` | `=== DEATHCHAIN: PASS both camera stacks intact, the game keeps its death camera for a later real death, no un-restorable removal` in **3 of 3** runs.  Everything else PASS except two verdicts this change cannot reach: `MIRROR` (which needs `SPAWN`; `SPAWN:` came back empty — `spawnok:0`) and `STREAM` (`cells around it loaded=false`, peer `91460 uu` away).  The 16:32 run of the **same DLL** had `SPAWN: PASS` + `MIRROR: PASS`; `r8-wall` (15:46) and `r8-shells` (18:12) show the identical `SPAWN`/`STREAM` failures **on their own builds at the same time**, so this is the shared environment's flake, not a regression.  Transcripts: `run-two-r8deaths.log`, `-2.log`, `-3.log`. |
| `run-two-death.ps1 -Phase case2/hold/wipe` (the old death verdicts) | `run-death-phases-locked.ps1` | **all three RESULT: PASS** — `DCASE2: PASS B respawned in the live world, no reset, stacks=1, max health ~90%` (maxhp 75→67.5), `DHOLD: PASS` incl. `DHOLDCAM: PASS the rendered camera follows the partner` + `DHOLDCAM release: PASS the camera is back on B`, `DWIPE: PASS host reset, guest followed, both alive, 0 stacks` (A 2→3, B 2→2).  Transcripts `death-case2.log`, `death-hold.log`, `death-wipe.log`. |

### 5.4 Log hygiene (the round-8 checklist)

* `has no parameter` in either install's `ms2coop-native.log`: **0** occurrences (the new
  param names `CameraState` for `AddCameraState`/`RemoveCameraState`/`CreateCameraState` and
  `Actor` for `SetCameraTargetActor` are all real — the restores/removals log `ok=1`).
* `MISMATCH`: **0**.  `NO death camera on the stack` (the module's own WARN): **0**.
* `death ability (after the co-op respawn): … 0 still ACTIVE` on every respawn, and
  `death ability ended: a later real death can run again`.
* No client crash in any `` run after the first attempt (that one died during **boot**, the
  known Steam `0xC0000374`, and the launcher now re-runs a boot abort automatically).

## 6. Screenshots — what they actually show

Run 2: `G:\Freespace\shell_coop\local\lab\20261026-162554-deathchain\` (run 1's
`20261026-161545-deathchain\` says the same thing).  1040x615 `PrintWindow` captures of
both real game windows; the contact sheet is `contact-png.png` in the same folder, and I
looked at it **and at four frames full size**.  Frame by frame:

| frame | what is on screen |
|---|---|
| `0-baseline-A/B` | normal gameplay: own dark-form body with the sword on the back, HUD (health `3/3` bottom-left, `1462` gold top-right, weapon icons bottom-right), partner visible in B's window |
| `1-death-B` | **B's death**: no HUD, the camera sits on the death spot — the body is a heap on the floor with the orange light column over it; this is the game's death camera framing, not the player's shoulder view |
| `1-respawned-B` | **B is back**: own body standing with the sword, HUD fully back, in the same live world (no loading screen) |
| `2-death-B` | **B's SECOND death, same session**: the death framing again (HUD gone, the body centred, no shoulder camera) — the second death is real, not a no-op |
| `2-respawned-B` | **B is back again**: body + sword + HUD |
| `3-death-A` | **A's death**: full-screen death overlay — the black `MORTAL SHELL II` logo screen over the death spot with the corpse and the light beam under it, HUD gone |
| `3-respawned-A` | **A is back**: body + sword + HUD, live world |
| `4-wipe-2s-A` | **the wipe, host, +2 s**: the `MORTAL SHELL II` death/loading screen fills A's window (no HUD) — the game's own death is running on the host, which is the case whose death camera this module must not have broken |
| `4-wipe-2s-B` | the guest's window: the same death overlay with the burning figure (B was already dead; its own screen is suppressed but the game's fade is visible) |
| `4-wipe-4s-A`, `4-wipe-7s-A`, `4-wipe-9s-A` | A stays on the game's loading screen (blurred scene + logo), then the loading art — this is the host's in-place death reset, and it owns the screen for ~15 s |
| `4-wipe-7s-B` | B's window on the burning-figure death overlay |
| `4-wipe-after-B` | B is back in gameplay (body + sword + HUD) |
| `4-wipe-after-A` | A is still on the loading art (reset in progress) — this is why the log's "both alive" line alone is not enough |
| **`4-wipe-settled-A`** | **A is back in gameplay**: own body with the sword, the partner's body standing next to him, HUD complete (health `3/3`, `1462` gold, weapon icons, interaction prompts) — and A's log at the same second says `play ready=true (ready)` |
| `4-wipe-settled-B` | B in gameplay with A's body next to him, HUD complete |

So the picture agrees with the log: four deaths in one session; every one shows the game's
death framing; every co-op respawn puts body + weapon + HUD back in the live world; the
wipe's death screen is the game's own, and it is gone once the reset's loading screen ends
(host) / immediately (guest).

Two notes on reading these frames:

* this game's death screen has no "YOU DIED" text — it is the black `MORTAL SHELL II` logo
  screen (`WBP_DeathScreen_C`) over the death spot, followed by the loading screen; that is
  what `4-wipe-2s-A` / `3-death-A` show and what the round-5/7 findings call the death
  screen;
* `4-wipe-after-A` is still the loading art on purpose: the host's in-place reset owns the
  screen for ~15 s, which is why the chain script takes the extra `4-wipe-settled` pair
  (and reads `play ready=true`) instead of trusting the sync's "both alive".

## 7. Open items

1. **`run-two.ps1`'s `RESULT:` is FAIL on `MIRROR` + `STREAM`, not on anything this change
   touches.**  `SPAWN` (`spawnask:12 spawnok:0`) and `STREAM` (`cells around it loaded=false`
   with the peer `91460 uu` away) fail identically in `r8-wall`'s and `r8-shells'` runs at
   the same hour, and both PASSed in this session's own 16:32 run of the same DLL.  The
   re-run before blaming (the round-8 rule) was done twice: `run-two-r8deaths-2.log`,
   `-3.log`.  What would settle it: one run when `SPAWN` produces an enemy again (the first
   of the three did, and then `MIRROR` passed).
2. **Second death / ability fix — verified on the happy path only.**  It works for `die`
   (S_KillMe) in a live world, three times in a row, on both clients.  Not exercised: a
   *real* death (enemy damage) after a co-op respawn — `dabl` now prints the ability's
   `bIsActive` before every death, so one wall-clock run in real combat answers it.  The
   "1 instance(s) ACTIVE" line printed before each `die` shows the ability object is still
   around (instanced abilities are reused), while its *active* flag at respawn time is 0 —
   that is the difference that matters.
3. **The death camera restore assumes the manager still owns the state.**  `AddCameraState`
   with the game's own removed instance worked (`ok=1`, `death cameras on the stack=1`); the
   `CreateCameraState(class)` fallback (for when the instance is gone, e.g. after a level
   change) has **not** been exercised — `camensure` after a level travel would.
4. `RemoveDeathState` is called before `K2_EndAbility`; if the game's own cleanup needs the
   state in place, the order is one line to swap (both calls are logged separately).
5. The round-6 "no body / no UI" symptom was re-checked visually in this run: every
   `*-respawned` frame shows body + sword + HUD, so the removal still behaves as intended.
6. `run-two.ps1` was edited (one block + one `$pass` conjunct) while other agents edit the
   same file in their own workspaces: expect a merge in the DEATHCHAIN block only.
7. Lock etiquette for the next round: the shared lock was held ~2 h over runs that take
   ~8 min each, and `Lock-Game`'s 30 s poll never wins a hand-over.  `run-chain-locked.ps1`,
   `run-two-locked.ps1` and `run-death-phases-locked.ps1` now take it with a 200 ms atomic
   acquire and always release it in a `finally`.

