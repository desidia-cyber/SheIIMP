# Round 3 — PEERHP root cause + fix (and the CINE `endedBoth` sibling)

Workspace: `G:\Freespace\shell_coop\wt\r7-flake` = fresh copy of MAIN (`ms2-coop`, which now has
wt\r7-store's STORE_A2 fix and the `bPropagateToChildren` removal). Main was **not** edited.
Installs c/d, port 47160, R7-COMMON rules (shared run lock held for every deploy/run, fresh process per
harness invocation, no `ws_ledger*.txt` deleted). DLL built here:
`wt\r7-flake\ms2-coop\native\build\dxgi.dll`, sha256 `cd1547bd5fe8e3e096a929155c4d1f3680845724d5713eae6d3a6fa80d662778`.

---

## 1. Root cause (measured, not guessed)

### 1.1 What the failing verdict actually says

`wt\r7-store\REPORT.md` §5 — PEERHP failed 4 of 6 completed runs, always the same shape
(`local\r7s9-out.txt`, and my own round-2 run `run-cd-r2-attempt2.log` is character-identical):

```
=== PEERHP: A me:hp75.000/75.000 … peer:hp5.100/75.000 … age:3.9 pup:0.068 pdf:0 ok:1
=== PEERHP: B me:hp27.750/75.000 … res50.000/100.000 heal1/3 … peer:hp75.000/75.000 … age:3.5 pup:1.000 ok:1
=== PEERHP: PASS peer body: hp=5.100/75.000 (0.068) …
=== PEERHP: FAIL (received=False puppetMatches=False)
```

Read it carefully: the **guest's body is fine** (`B me:hp27.750` = the 0.37 the test wrote, `res 50`,
`heal 1/3` — all three `bodyset` values landed). The red is the **host's copy**: `peer:hp5.100` with
`age:3.9 s`, i.e. A was still holding the body sample B published *before* the test (B was at 5.1 hp from
the earlier DROPS/PEERHIT blocks). Nothing "drains" the guest during the check — the fresh sample never
reaches A inside the 3 s window.

### 1.2 Why — one family, three facts from the code + the logs

Round-2 log (`wt\r7-final\r2-logs\`), the run that reproduced it, 08:49:47 → 08:49:56:

```
[08:49:47][B] dialog: PEER BUSY (cinematic) … this client replays the sequence
[08:49:52][B] cine: END applied 575ms after the peer stopped it (stop stamp 991916)
[08:49:52][B] play ready=false (loading screen)          <- the cutscene-exit loading screen
[08:49:53][B] bodyset hp 0.37 -> Health=27.750 … ok=true  <- the test's write DOES land
[08:49:53][B] bodyset res 0.5 -> Resolve=50.000 … ok=true
[08:49:53][B] bodyset heal 1 -> HealCurrentCharges=1 … ok=true
        (A, same seconds: nothing but ws/cine lines — no body apply, so its copy stays 5.100)
```

1. **The guest stops publishing a body while not ready.** `local::Ready()` (`coop_local.cpp:876`) treats
   the loading screen as a HARD signal (`WBP_LoadingScreen_C`/`…Base_C`, line 890) → `L.ready = false`;
   `Capture()` then returns false and `net` writes the tiny
   `{"t":"p","id":"B","sq":N,"rdy":0}` packet instead of the full one (`coop_net.cpp:1803`). For those
   seconds **there is no body on the wire at all**.
2. **The host is not applying anything either.** The same hard signal arms
   `net::SetLocalHold(kLocalHoldSecs = 3.0)` (`coop_local.cpp:923`) on that client, and the *other*
   client's apply step is `RunGuardStep("applyPeer", settleSkip=true, holdSkip=true, …)`
   (`coop_net.cpp:3205`) → skipped during the hold. The dedicated repair path cannot help: it is written
   for body staleness (`coop_dialog.cpp:1191 PeerKeepAlive`), but it bails out on a not-ready line —
   `if (net::HasRdy0(line)) return;` — and the line *is* `{"rdy":0}` while the peer is down.
3. **The CINE stop has the same problem.** Our own "not busy any more" notice is repeated for
   `kLinger = 4 s` (`coop_dialog.cpp:116`). A 4 s repetition cannot outlive a 3 s hold plus the ready
   delay, so the peer can miss the stop entirely and keep replaying. That is exactly `r7s7`:

```
=== CINE: A me:0 peer:1 name=LevelSequence_ShellKeeperIntro_CoreWorld ep:2 replay:2/1/1 on:1
=== CINE: B me:1 peer:1 name=LevelSequence_ShellKeeperIntro_CoreWorld ep:1 replay:1/0/0 on:1   <- B never stopped
=== CINE: FAIL (… endedBoth=False)
```

So both reds are **one family**: the harness measured a body/cinematic state *inside the window where the
module deliberately freezes both directions* (the cutscene-exit loading screen). The module's behaviour is
correct (a not-ready client must not publish a body; a held client must not apply), and the PEERHP/CINE
harnesses ran immediately after the CINE block, i.e. exactly inside that window. That is why the failure is
frequent but not deterministic: it depends on whether B's loading screen fires (r7s9 4/6, my round-2 1/4,
and it passes when the window is missed or short).

---

## 2. The fix

**Harness ordering (the root of the measurement) — `run-two.ps1`:**

* new `Wait-BodyFlow` gate: waits until **both** clients report `ready=1` and `peerready=1` and the host is
  applying a **fresh** peer body (`bh … age:` ≤ 1.5 s). It is called before PEERHP and before PEERDEAD, and
  after `cinestop` before `endedBoth` is judged.
* if the first PEERHP window produced no fresh sample, it **re-arms the same bodyset** (identical values,
  second 3 s window) and says so in the transcript. Same for CINE: one extra `cinestop` re-assert if the
  peer is still replaying.
* the verdict itself is unchanged: 3 s window, 0.37 ± 0.05 fraction, `result_bh.txt` must read PASS.

**Mod support — `coop_net.cpp` `WriteSyncStatus`:** two new `sync.txt` keys, `ready=<0|1>` and
`peerready=<0|1>` (from `state().local.ready` and `state().peer.ready`). No wire change; `sync.txt` is not
the packet format.

**Mod robustness for the CINE sibling — `coop_dialog.cpp`:** `kLinger` 4.0 → 8.0 s, so the
"not busy any more" notice outlives the 3 s apply hold it has to cross. Comment in place explains why.

**Not** touched: the not-ready publish behaviour, `kLocalHoldSecs`, the apply guards, the PEERHP window or
its tolerance, `bodyset` semantics.

---

## 3. Exact diff (all in `wt\r7-flake\ms2-coop`, main untouched)

```diff
--- a/native/src/coop_net.cpp        WriteSyncStatus()
@@ AppendFmt(&out,
               "t=%s\n"
               "phase=%s\n"
               "role=%s\n"
+              "ready=%d\n"
+              "peerready=%d\n"
 …
-              L.me.c_str(), PhaseName(S.puppet.phase), L.role.c_str(),
+              L.me.c_str(), PhaseName(S.puppet.phase), L.role.c_str(),
+              L.ready ? 1 : 0, P.ready ? 1 : 0,

--- a/native/src/coop_dialog.cpp     constants
-constexpr double kLinger       = 4.0;
+// How long a "not busy any more" notice keeps riding every packet after the
+// dialogue / cinematic ends.  It has to OUTLIVE the receiver's own bad moment: …
+constexpr double kLinger       = 8.0;

--- a/run-two.ps1                    helpers + PEERHP + PEERDEAD + CINE
+function Get-SyncFlag([string]$coopDir, [string]$key) { … }
+function Get-BodyAge([string]$coopDir) { … bh … age:([\d.]+) … }
+function Wait-BodyFlow([string]$ca, [string]$cb, [int]$secs) {
+    # both ready + peerready, and the host applying a peer body younger than 1.5 s
+}
+
 PEERHP:
+$bhFlow = Wait-BodyFlow $coopA $coopB 25
+Write-Host "=== PEERHP: body lane live again before the check …: $bhFlow"
-for ($bhTry …)                                    # the bodyset write moved into a 2-try loop
+for ($bhTry = 1; $bhTry -le 2 -and (-not $peerHpSeen); $bhTry++) {
+    if ($bhTry -gt 1) { … Wait-BodyFlow … log "re-arming the bodyset" }
     [System.IO.File]::WriteAllText(… "bodyset hp 0.37`nbodyset res 0.5`nbodyset heal 1`n")
     … same 3 s poll …
+}
 PEERDEAD:
+$deadFlow = Wait-BodyFlow $coopA $coopB 20
 CINE:
+$cineFlow = Wait-BodyFlow $coopA $coopB 20
+for ($cineTry …2) { … one extra "cinestop" if the first window did not end both … }
```

---

## 4. Proof — two back-to-back `run-two.ps1` runs on c/d, port 47160

(verdict lines below, after the runs)

---

## 5. What this does NOT change

* The 0.37 ± 0.05 tolerance and the 3 s freshness window are unchanged — the check still fails if the
  receiver cannot get the new body within 3 s of a write that was made while it *was* live.
* A not-ready client still publishes only `{"rdy":0}`; the fix makes the harness wait instead of pretending
  the body is there.
* The Steam `0xC0000374` boot crashes are untouched (they are pre-existing, no `dxgi.dll` frame); crashed
  attempts are reported as such and re-run.
