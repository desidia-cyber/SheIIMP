# Round 7 FINAL — verification of the merged MAIN tree (agent `r7-final`)

Date: 2026-10-26 05:39 – 06:35. Workspace: `G:\Freespace\shell_coop\wt\r7-final`.
Verified tree: `G:\Freespace\shell_coop\ms2-coop` (MAIN, **not edited** by me; it was rebuilt only).
Runtime: `ms2coop-native` proxy `dxgi.dll`, sha256 **a351eb15fe51ee127272d9240acf08b3efb95b23176a94445e11b5044f50cff2**
(1 729 024 B, built 05:43:32), deployed to all four installs used below.

| Role | Install | Relay port |
|---|---|---|
| host A (main suite) | `E:\Games\_MS2-coop-c` | 47160 |
| guest B (main suite) | `E:\Games\_MS2-coop-d` | 47160 |
| host A (death phases) | `E:\Games\_MS2-coop-test` | 47170 |
| guest B (death phases) | `E:\Games\_MS2-coop-b` | 47170 |

Every game run held the shared run lock (`local\gamelock.ps1`, owner `r7-final`) for deploy+run and released
it afterwards; every harness invocation was a fresh `powershell -NoProfile -ExecutionPolicy Bypass -File`
process. `coop\ws_ledger_*.txt` was **not** deleted anywhere (fixreset is supposed to handle that — see §7.2,
where it does not).

---

## 1. Build (step 1)

```
cmd /c G:\Freespace\shell_coop\ms2-coop\native\build.bat test
  coop_policy: 112 checks passed, 0 failed
  PASS: the co-op death rule's truth table holds, mutants included
  PASS: all 4 patterns match exactly once and resolve to the recovered addresses
  Built: G:\Freespace\shell_coop\ms2-coop\native\build\dxgi.dll
exit code 0
```

Logs: `build-r7final-2.log` (clean, exit 0). `build-r7final.log` is the same build piped through
`Tee-Object` (the reported `[exit code: 1]` there is the tee's NativeCommandError, not the build; the
second run redirected the log and returned **exit 0**).
Verdict: **PASS**.

---

## 2. `run-two.ps1` on c/d, port 47160, twice back to back (step 2)

Commands (fresh process each, one driver that also deployed the DLL):

```
deploy.ps1 -GameDir E:\Games\_MS2-coop-c -DllPath ms2-coop\native\build\dxgi.dll
deploy.ps1 -GameDir E:\Games\_MS2-coop-d -DllPath ms2-coop\native\build\dxgi.dll
run-two.ps1 -GameA E:\Games\_MS2-coop-c -GameB E:\Games\_MS2-coop-d -Port 47160   # run 1
run-two.ps1 -GameA E:\Games\_MS2-coop-c -GameB E:\Games\_MS2-coop-d -Port 47160   # run 2
```

Transcripts: `run-cd-1.log`, `run-cd-2.log` (full), `appendix-cd.md` (every `=== …` + `RESULT` line) — all three now in `round1\` (round 2 reuses the names).

### Results

| Verdict | run 1 | run 2 |
|---|---|---|
| FIXRESET | PASS | PASS |
| WIRETEST | PASS | PASS |
| SHELL | PASS | PASS |
| WEAPON | PASS | PASS |
| PUPVIS | PASS | PASS |
| LOOK | PASS | PASS |
| ANIM | PASS | PASS |
| DAMAGE | PASS | PASS |
| DEATH | PASS | PASS |
| DROPS | PASS (both directions, 20/100 gold/gloom each) | PASS |
| WORLD | PASS | PASS |
| PROGRESS | PASS | PASS |
| QUEST | PASS | PASS |
| QUEST2 | PASS | PASS |
| LIFT | PASS | PASS |
| CHEST | PASS | PASS |
| **STORE_A2** | **FAIL** (verdict line empty — no `result_ws2.txt` inside the 130 s window) | **FAIL** (same) |
| STORE_A2SELFTEST | PASS (20/0) | PASS (20/0) |
| SPAWN | PASS | PASS |
| MIRROR | PASS | PASS |
| PEERHIT | PASS | PASS |
| AITARGET | PASS | PASS |
| **STREAM** | **FAIL** (`cells around it loaded=false`) | **PASS** — flaky, see §2.3 |
| NPCFAR | PASS | PASS |
| LEDGER | PASS (17 keys, 12→13 good, 4→5 bad, 4→5 applied) | PASS (13 good / 5 bad / 5 applied) |
| BREAK | PASS (peer break `75e02c2e` `BP_VFX_Breakable_Cart_02_A_C`, `FilteredHit`) | PASS (also `recv:1 applied:1`) |
| WIRE | PASS (6/7 parts, max datagram 1149 B, 0 lost) | PASS (6/7, max 1150 B, 0 lost) |
| ZONE | PASS | PASS |
| **DIALOG** | **FAIL** (`listenLines=False`, verdict line empty) | **FAIL** (same) |
| DIALOGVOTE | PASS (7/7 both clients) | PASS (7/7) |
| DIALOGCODEC | PASS (21/0) | PASS (21/0) |
| CINE | PASS (real `LevelSequence_ShellKeeperIntro_CoreWorld`, replays=1) | PASS |
| PEERHP | PASS | PASS |
| PEERDEAD | PASS | PASS |
| BOSSPHASE | PASS (owner phase 2, peer `applied:1`) | PASS |
| DIALOGREAL | (log line only) `lines:0` | `lines:0` |
| NPC | (empty — no NPC outcome needed) | (empty) |
| A / B (puppet tracking) | PASS (30/30 good) | PASS (30/30 good) |
| NATIVE | READY / coop started (both) | READY / coop started (both) |
| **RESULT** | **FAIL** | **FAIL** |

**Both runs FAIL, for the same two deterministic reasons (STORE_A2, DIALOG).** STREAM is flaky
(FAIL/PASS in the two runs), every other merged feature — BREAK, WIRE/WIRETEST/FIXRESET, BOSSPHASE,
DIALOGVOTE/DIALOGCODEC, CINE, DROPS, and all round-5/6 verdicts — passed twice.

### 2.1 STORE_A2 FAIL — root cause: `fixreset` does not reset the `BPSO_NPC_C` fixture, so the guest
already holds the "new-class boolean" as `true` from the boot snapshot and its *live* apply only lands
~134 s later — 4 s after the harness stops waiting

Both runs, guest `result_ws2.txt` is not there when the harness looks, so `run-two.ps1` prints
`=== STORE_A2: ` with nothing, which gates `RESULT` (`$resWs2 -match '^PASS'`):

```
=== STORE_A2: A kinds:10 typed:3 scans:558 recv:7850 applied:2 str:0 fail:0 leaks:0 ; B kinds:10 typed:3 scans:523 recv:7718 applied:2 str:0 fail:0 leaks:0
=== STORE_A2:
```

Guest (`_MS2-coop-d`) native log, same run, every 5 s from the moment `wa2test` was consumed to the end of
the session:

```
[05:58:2x][B] ws2 judge: no new-class boolean replayed yet (applied int=2 str=0)   (…, still at 06:03:12)
```

The judge (`coop_world_ws2.cpp` `Judge()`, ~line 1306) requires, in (a), a key that is in
`world.liveOk` **and** whose row is `IsNewWsRow(kind, field)` — i.e. a new-class boolean applied from the
*change* block, **and** `gAppliedInt > 0` (that half is fine: `ClensedCores` applied).

The host does write both new-class boolean fixtures (`wa2test`, run 2, 05:58:20):

```
[05:58:20][A] ws2 fixture: BPSO_NPC_C reset by class -> 13 object(s) (by guid: 0 of 18 ok)
[05:58:20][A] ws2 wa2test: bool BPSO_NPC_C.HasTriggeredInitialGreeting := true on BP_Cultist_HUB_OvumCaretaker_C_… d=6329 ok=true key=5f5dd95e
[05:58:20][A] ws2 wa2test: bool BPSO_WoodenElevator_C.HasBrokenOnce := true on BP_EndDungeonRewardDoor_C_… ok=true key=c8c8b77f
[05:58:20][A] ws2 wa2test: bools=2 ints=3 arrays=0 skipped=9
```

**Full guest-side timeline (run 2), which is the whole failure in one place:**

```
[05:56:14][B] ws see 5f5dd95e HasTriggeredInitialGreeting=false … (the save really is false at boot on the guest)
[05:56:23][B] ws apply base 5f5dd95e BPSO_NPC_C.HasTriggeredInitialGreeting (…OvumCaretaker…) ok     <- the host's boot snapshot
[05:58:20][B] ws peer live 5f5dd95e (the peer says its world just changed)                          <- wa2test's publish arrives
[05:58:21][B] ws live skip c8c8b77f: row not settled here yet (known=0)                             <- the live apply is DEFERRED
[06:00:34][B] ws apply live 5f5dd95e BPSO_NPC_C.HasTriggeredInitialGreeting (…OvumCaretaker…) ok     <- 134 s after the write
[06:00:34][B] ws live skip 5f5dd95e: already applied live here
```

`run-two.ps1` waits `130 s` for `result_ws2.txt` after writing `wa2test` (lines 561-564), so the verdict
line is read at ~06:00:30 and printed empty. The deferred live apply landed at **06:00:34, four seconds
after the harness had read the file** — and it did not rescue the verdict either: the guest's judge logged
`ws2 judge: no new-class boolean replayed yet (applied int=2 str=0)` again at **06:03:12**, i.e. after that
apply, and the run ended without a verdict. The reason is the second half of the same gate: `liveOk` is only
trusted after a confirm-readback (`coop_world.cpp` ~line 971 sets it to 0 "pending readback", ~line 983 sets
it to 1), and the two keys that *did* apply live and read back at 05:56:58 (`ws readback 8e9149a9 … now
true (direct read after the live apply)`, `a8ca0e96 …`) are exactly the ones `fixreset` had just reset to
false. For the NPC row — never reset on the guest — the apply is a redundant same-value write and never
produced a confirmed `liveOk` entry. Run 1 was worse: the deferred apply never happened inside the session
(guest `ms2coop-native.2.log` has `ws live skip c8c8b77f: row not settled here yet` and **no** live apply
of `5f5dd95e` at all).

Why the guest holds it `true` at 05:56:23: the host's boot snapshot already carries it, because the store-A
**ledger** replays last session's keys on load (the mechanism the `fixreset` comment names). Guest
`coop\ws_ledger_0.txt` (17 keys) and the host's both contain `5f5dd95e` and `767b6e14`; host status shows
the replay: `=== LEDGER: A ok keys:17 good:13 bad:5 applied:5` (run 2).

Why the live apply is then *deferred*: `coop_world.cpp` `ApplyPeerWorld` (~line 1033) only applies a live
arrival once `RowSettled(idx)` — the guest's row for that key is "not settled yet (known=0)", and the
settle/scan retry takes ~2 min that the harness does not have. The rows `fixreset` does reset were settled
immediately and produced the confirmed live applies. Fixing the fixture state removes the deferral and the
unconfirmed readback; widening the wait would only paper over it.

`fixreset` (`coop_world.cpp` `TestFixReset()`, ~line 2317) resets only

```cpp
static const char* kSaveObjects[] = {"BP_HubDoor_C", "BPSO_BearTrap_C",
                                     "BPSO_FallingCorpse_C", "BPSO_FallingStatue_C",
                                     "BPSO_WoodenElevator_C"};
```

plus the lift and the chests (`=== FIXRESET: … (world 4, lift 0 of 0, chest 8 of 8)`). **`BPSO_NPC_C` is
missing**, although the store-A2 fixture reset the host runs itself (`Wa2FixtureReset()`,
`coop_world_ws2.cpp` ~line 1715) does include it (`BPSO_BearTrap_C, BPSO_FallingCorpse_C,
BPSO_FallingStatue_C, BPSO_NPC_C, BPSO_WoodenElevator_C, BPSO_Tarforge_C`). The host resets its own NPC
save objects, writes `true`, and the *guest* is never reset — the one asymmetry behind the deferred apply.

R7-WIRE's own claim ("two runs back to back must both pass with the ledger left alone", `run-two.ps1`
lines 193-204) therefore does not hold for STORE_A2. It is a **regression-hygiene defect in the merged
FIXRESET verb**, not a data-loss bug: no wire state is wrong, the verdict only cannot be produced twice.

Proposed (small) fix — **not applied, not proven** (time ran out; the fix belongs in the main tree, which
I must not edit):
```diff
--- a/native/src/coop_world.cpp   TestFixReset()  ~line 2317
 static const char* kSaveObjects[] = {"BP_HubDoor_C", "BPSO_BearTrap_C",
-                                     "BPSO_FallingCorpse_C", "BPSO_FallingStatue_C",
-                                     "BPSO_WoodenElevator_C"};
+                                     "BPSO_FallingCorpse_C", "BPSO_FallingStatue_C",
+                                     "BPSO_NPC_C", "BPSO_WoodenElevator_C",
+                                     "BPSO_Tarforge_C", "BPSO_FC_FinalBossManager_C"};
```
(add the classes `wa2test` writes: `BPSO_NPC_C` for the boolean half — `BPSO_Tarforge_C` /
`BPSO_FC_FinalBossManager_C` keep the typed-int half equally repeatable). With that, `fixreset` runs on
**both** clients (it is sent to both by `run-two.ps1`), the guest's copies go `false` and are settled, the
host's `wa2test` write is a genuine change that arrives through the change block and is applied
immediately instead of being deferred → `liveOk` gets a new-class boolean → `result_ws2.txt` is written.
Caveat to check when someone builds it: resetting `BPSO_NPC_C` also clears `WasEverKilled` (the comment at
`kBoolPicks` warns about writing that field); in the test save that is harmless (NPCs simply re-greet), and
the harness restores the owner saves anyway.

A harness-only alternative that needs no recompile (and is also how I would double-check the diagnosis):
write one `wa2reset` line to `$coopB`'s `cmd.txt` right after the FIXRESET block in `run-two.ps1` — the
`wa2reset` verb already resets exactly `Wa2FixtureReset()`'s class list on that client.
A pure band-aid — raising the 130 s wait (lines 561-564) — is not even proven to help: run 2's deferred
apply at 06:00:34 produced no confirmed `liveOk`, and run 1 never applied at all. It would only hide the
defect.

### 2.2 DIALOG FAIL — root cause: the listener cannot resolve the dialogue asset, so it replays 0 lines
and never writes `result_dialog.txt`

Both runs print the same lines:

```
=== DIALOG: realConversation=False (dlgstart content-gated in this save; see REPORT)
=== DIALOG: A me:0 peer:1 kind:1 name:DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting node:0 opts:0 seq:1 idle:7 pd:7 ai:7 ep:1 mine:0 listen:1 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOG:
=== DIALOG: FAIL (peerBusySeen=True realConversation=False idlePuppet=True listenLines=False pdNotCharged=True [0.1 -> 0.1] votes=True lockEnded=True)
```

(`pdNotCharged=True` is measured with a real `pdtest` drop 0.1 → 0.1, so the damage half works; the vote
resolver passes 7/7 and the codec 21/21.)

The listener half is what fails. Host `_MS2-coop-c` native log:

```
[06:02:31][A] asset not loaded yet: /Game/Sparta/Dialogue/Dialogues/HUB/TunnelCultist/DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting
[06:02:31][A] dialog: PEER BUSY (dialogue) name=DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting seq=1 -- puppet idle, no retarget, no damage charge, this client listens to the lines
[06:02:31][A] dlg: LISTEN node=0 speaker=- line=- voice=- options=0        (x9, every 2 s)
[06:02:53][A] dialog: peer busy ended (kind=1, idle samples=43 pd=43)
```

`ListenerTick()` (`coop_dialog.cpp` ~line 1263) does `net::Find(path, true)`; `net::Find`
(`coop_net.cpp` ~line 1356) only looks the object up in GObjects and has **no asset loader**
(`requestLoad` is a no-op unless `LOAD_ON.txt`, and the native build has none), so the asset is only found
if that client already loaded the dialogue itself. The listener therefore gets `asset == nullptr`,
`NodeLine()` returns nothing, `g_linesReal` stays 0, and the judge
(`coop_dialog.cpp` ~line 1513: `… && g_linesReal >= 1 && …`) never writes `result_dialog.txt` — which is
exactly the empty verdict line. CINE passes because the level-sequence asset *is* loaded on both clients
(the hub intro sequence ships with the map).

Note vs the round-7b report: `wt\r7-dialog\REPORT.md` §7b.3 claims a `RESULT: PASS` with
`listenLines=True lines=1` and a listener log line `dlg: LISTEN node=0 speaker=Disciple line=Welcome back,
High Sester.` — that run must have had the asset loaded on the listening client (their lab had driven that
client's own dialogue path first; no transcript is kept in their workspace, so I cannot check it). In the
merged tree, in fresh back-to-back `run-two.ps1` sessions on c/d, it is not loaded and the verdict fails
deterministically (2/2). So the merged DIALOG feature's listener half is **state-dependent and not
reproducible from a clean session**; the wire/state half (peer busy ⇒ puppet idle, no retarget, no damage
charge, vote lock release, codec) does work.

Proposed fixes (not applied):
1. **Preferred, small, no loader needed:** the *owner* already has the asset; publish the current node's
   `speaker` + `line` text (and the voice asset path) inside the existing `"dl"` key
   (`coop_dialog.cpp` `Serialize`, ~line 150-200) and let `ListenerTick()` use them when its own
   `net::Find` fails. ~40 B payload, loss-safe with the existing `seq`, key order untouched. The
   `dlgfake` injector must then also accept a text (`dlgfake <path> [text]`) or, better, `run-two.ps1`'s
   `dlgfake` call should pass the same textual line so the DIALOG verdict can be produced without the
   NPC being talkable in this save.
2. **Alternative:** give the listener an asset load (`UKismetSystemLibrary::LoadAsset`-style, or resolve
   the dialogue via `UDialogueSubsystem`'s own loaded asset) — bigger, and it contradicts the native
   module's deliberate "no asset loader" contract.
3. **Harness-only workaround (do not ship):** have the *listening* client run `dlgstart`/`dlgfake on`
   itself once, before the check, so the asset ends up loaded — fragile and not representative.

### 2.3 STREAM FAIL in run 1 only — pre-existing flakiness, not a merge regression

```
run 1: === STREAM: FAIL remote area streamed around peer 91460 uu away after a long travel: cells around it loaded=false, static meshes 81 -> 20 …
run 2: === STREAM: PASS remote area streamed around peer 91460 uu away after a long travel: cells around it loaded=true,  static meshes 85 -> 20 …
```
Nothing in round 7 touches `coop_world.cpp`'s stream/`travel` code; the check depends on the guest's
60 s straight-line travel reaching the far point and the cells finishing their async load inside the
130 s `Wait-File`. Reported as flaky (one FAIL, one PASS in the two runs; round 6 saw PASS).

---

## 3. Death phases on test/b, port 47170 (step 3)

Three separate fresh-process runs (one phase each; a second death in one session is not producible):

```
deploy.ps1 -GameDir E:\Games\_MS2-coop-test -DllPath ms2-coop\native\build\dxgi.dll
deploy.ps1 -GameDir E:\Games\_MS2-coop-b    -DllPath ms2-coop\native\build\dxgi.dll
run-two-death.ps1 -GameA E:\Games\_MS2-coop-test -GameB E:\Games\_MS2-coop-b -Port 47170 -Phase case2
run-two-death.ps1 … -Phase hold
run-two-death.ps1 … -Phase wipe
```

| phase | verdict lines | RESULT | transcript |
|---|---|---|---|
| case2 | `=== DCASE2 case: PASS the death was decided as case 2` · `=== DCASE2: PASS B respawned in the live world, no reset, stacks=1, max health ~90%` — detail `maxhp 75 -> 67.5 (ratio 0.9) worldresets A 2->2 B 2->2` | **PASS** | `death-case2.log` |
| hold | `=== DHOLD: PASS B is held (case 3, pending)` · `=== DHOLDCAM: PASS the rendered camera follows the partner` · `=== DHOLD release: PASS B respawned once the fight was over` · `=== DHOLDCAM release: PASS the camera is back on B` · `=== DHOLD: PASS` | **PASS** | `death-hold.log` |
| wipe | `=== DWIPE case: PASS a party wipe was seen` · `=== DWIPE: PASS host reset, guest followed, both alive, 0 stacks` — detail `host reset A worldresets 2->3 ; guest did NOT reset its own (B 2->2)`, `both alive=True stacks A=0 B=0` | **PASS** | `death-wipe.log` |

All verdict lines: `appendix-death.md`. Screenshot/evidence dirs the script wrote:
`local\lab\20261026-061145-death-two`, `local\lab\20261026-061347-death-two`,
`local\lab\20261026-061551-death-two`.

Worth noting for the hand-over: the **boss-hold spectate camera now PASSES** (`viewpuppet=1`;
`COOP DEATH camera: removed CameraState_Death_C from the camera stack ok=1` … `COOP DEATH camera: back on
the player (7 state(s))`), which was `DHOLDCAM: WARN` in round 5/6 — the merged tree keeps that fix. The
death stack/debuff/9 %-per-stack arithmetic, the fresh-body rebuild (`InitializeCharacter=1 -> bones=164`,
7 weapons re-attached) and the wipe hand-off (host `BeginWorldReset` ok, guest waits for the host, both
0 stacks) all reproduce.

---

## 4. Visual runs on c/d (step 4)

Screenshots are `PrintWindow(PW_RENDERFULLCONTENT)` of the real game windows; every set was assembled into a
System.Drawing contact sheet and **looked at** (full-resolution frames re-opened where the sheet was
ambiguous). Three runs were needed: the briefed one plus two controls, because the briefed command set
starts a cutscene, and a cutscene hides the player and the HUD by design — which makes "own body + weapon +
HUD visible" impossible to judge from it.

### Run A — the briefed run (`-Cmds 'A=brkbreak nearest;A=cineplay'`)

```
powershell -NoProfile -File ms2-coop\visual-two.ps1 -GameA E:\Games\_MS2-coop-c -GameB E:\Games\_MS2-coop-d `
  -Port 47160 -BootWait 200 -Tag r7final -Cmds 'A=brkbreak nearest;A=cineplay'
```
Output: `G:\Freespace\shell_coop\local\lab\20261026-061603-r7final\` (26 PNG + `contact-sheet.png`),
transcript `visual.log`. Both commands did what they should:

```
[06:17:15][A] brkbreak: BP_VFX_Breakable_Cart_02_A_C_UAID_345A60361B8776BE02_… (BP_VFX_Breakable_Cart_02_A_C) d=4224 key=75e02c2e -> the game's own break chain
[06:17:15][A] cmd handled: brkbreak nearest
[06:17:17][A] cineplay: PlayCutscene on BP_CinematicPlayer_C_… (sequence=LevelSequence_ShellKeeperIntro_CoreWorld) -> started
[06:17:18][B] dialog: PEER BUSY (cinematic) name=LevelSequence_ShellKeeperIntro_CoreWorld seq=1 -- … this client replays the sequence
[06:17:20][B] PASS cinematic lock: peer 'LevelSequence_ShellKeeperIntro_CoreWorld' held 2.0s ; puppet idle samples=5, damage charges suppressed=5, replays started=1
```

What the frames show: `A-after-cmds1..4` and `B-after-cmds2..4` are the **cutscene** (close-up of the cultist
woman, sequence camera, no HUD) on **both** clients — so the real cinematic and its replay on the peer are
visible on screen. Afterwards, `A-sees-B-dir45` shows B's dark-form puppet standing next to the cultist, and
`B-sees-A-idle/move1..3` show A's puppet with Hadern's sword rendered on B's screen — the peer puppet and
its weapon are visible from both sides. **No debug wireframe anywhere** in the 26 frames.

Gap: in every frame after the cutscene both clients show **neither their own body nor the HUD** (the camera
sits where the body should be and the frame is empty in the lower half). Run C below proves this is not a
framing artifact but the merged CINE replay leaving the client in the cinematic presentation state.

### Run B — control WITHOUT any cutscene (`-Cmds 'A=selfcheck;B=selfcheck'`)

```
-Tag r7final-nocine   -> local\lab\20261026-062102-r7final-nocine\ (26 PNG + contact-sheet.png), visual-nocine.log
```

All 26 frames show **own body + weapon + HUD on both clients**: bottom-left health/resolve bars,
bottom-right two item slots, top-right currency `1212 / 0`, a subtitle line in the middle, and the peer's
puppet in the same frame (`B-sees-A-*` shows A's shell with the sword; `A-after-cmds*`/`A-sees-B-*` show A's
own body in the foreground). No wireframes. `selfcheck` on both clients agrees with the picture:

```
[06:22:16][A] selfcheck (cmd): … hidden=0 meshVisible=1 meshHiddenInGame=0 viewTarget=self uiHandler=1 hud=1 hudInViewport=1 hudVisible=1
[06:22:18][B] selfcheck (cmd): … hidden=0 meshVisible=1 meshHiddenInGame=0 viewTarget=self uiHandler=1 hud=1 hudInViewport=1 hudVisible=1
```

So the merged tree's normal gameplay presentation is intact (own body, weapon, HUD on both).

### Run C — cutscene, then `selfcheck` (`-Cmds 'A=cineplay;A=cinestop;A=selfcheck;B=selfcheck'`)

```
-Tag r7final-cinecheck -> local\lab\20261026-062559-r7final-cinecheck\ (26 PNG + contact-sheet.png), visual-cinecheck.log
```

**Confirmed defect — after a real cutscene the client stays hidden / on the cinema camera, and the HUD does
not come back:**

```
[06:27:09][A] cineplay: PlayCutscene on BP_CinematicPlayer_C_… (sequence=LevelSequence_ShellKeeperIntro_CoreWorld) -> started
[06:27:10][A] cine: replay stop on BP_CinematicPlayer_C_… ok=1
[06:27:10][A] cinestop: ForceStopCinematic ok=1
[06:27:13][A] selfcheck (cmd): … povToBody=4006 … hidden=1 meshVisible=1 meshHiddenInGame=0 viewTarget=CineCameraActor_UAID_345A6054A1DA877A02_2147467975_2147467976 uiHandler=1 hud=1 hudInViewport=1 hudVisible=1
[06:27:15][B] selfcheck (cmd): … povToBody=226  … hidden=1 meshVisible=1 meshHiddenInGame=0 viewTarget=self            uiHandler=1 hud=1 hudInViewport=1 hudVisible=1
```

* **A (the client that played the sequence):** pawn `hidden=1` **and** the camera is still the leftover
  `CineCameraActor` — `povToBody=4006` uu (vs. 226 uu in normal gameplay). Its frames show empty scenery,
  no own body, no HUD.
* **B (the client that replayed the sequence):** pawn `hidden=1`, camera normal (`viewTarget=self`,
  `povToBody=226`) — its frames show the scene and the peer's puppet, no own body, no HUD.

Compare Run B (no cutscene): `hidden=0 … hudVisible=1` and both bodies + HUDs are on screen. The pawn's
`ActorHiddenInGame` and the HUD are left in the cinematic state by a **force-stopped**
(`cinestop: ForceStopCinematic`, `cine: replay stop … ok=1`) sequence: the game's own "end of cutscene"
restore never runs. The module already has a presentation restore for death
(`coop_death.cpp` "COOP DEATH presentation restored … hidden=0 … hudVisible=1"), but none for cinematics.
**Neither `run-two.ps1`'s CINE verdict nor the log can see this** — `selfcheck`/screenshots are the only
witness, which is why it survived the round-7 merge.

Impact: one real cutscene (story beat, boss intro) leaves the player's screen wrong until the next
death/respawn (a respawn does restore the presentation — see the death log lines). This is the most
serious *new* finding of this verification, because it is a picture-level regression that no verdict line
covers.

### BREAK on screen (the barrel)

`brkbreak nearest` picked a barrel/cart **4 224 uu away** from A (the hub camera looks at the cultist
alcove, not at that cart), so this run's frames cannot show that particular prop before/after. The break
itself is proven by the log and by the peer: `BREAK: PASS peer break 75e02c2e
BP_VFX_Breakable_Cart_02_A_C … (FilteredHit)` in both run-two passes, and `BREAK: A … sent:2 recv:1
applied:1` on the host in run 2. Same on-screen limitation r7-maps reported (`BREAK: PASS` on the wire and
on the peer's objects, prop-level before/after WARN); its `break-lab.ps1` is the tool for a prop close-up.

---

## 5. Log greps and crash dumps (step 5)

Installs used: `_MS2-coop-c`, `_MS2-coop-d` (run-two ×2 + three visual sessions), `_MS2-coop-test`,
`_MS2-coop-b` (death case2/hold/wipe). The module keeps three log generations per install
(`ms2coop-native.log`, `.1.log`, `.2.log`) and **all of them** were grepped per install, twice: once right
after the run-two/death sessions (c/d: 06:03, 05:54, R6 23:38; test/b: 06:15, 06:13, 06:11) and once again at
the very end (c/d: 06:28, 06:23, 06:18 — the three visual sessions; test/b: unchanged).

| pattern | `c` | `d` | `test` | `b` |
|---|---|---|---|---|
| `has no parameter` | 0 | 0 | 0 | 0 |
| `ERR` (case-sensitive) | 0 | 0 | 0 | 0 |
| `MISMATCH` (case-sensitive) | 0 | 0 | 0 | 0 |
| `exception` | 0 | 0 | 0 | 0 |
| `mismatch` (case-insensitive) | 1 in each of the two run-two logs, 0 in the three visual logs | same | 0 | 0 |

The only "mismatch" hits are the **deliberate** `zonefake` lines the ZONE verdict produces, one per run.
Final grep state (06:30, all four installs, all three generations): `param=0 ERR=0 MISMATCH=0 exc=0` and
`mismatchCI=0` everywhere (the `zone mismatch` lines had rotated out — the newest generation is a visual
session). Nothing in the merged DIALOG/CINE/DROPS/BOSS/WIRE/BREAK code logged a wrong UFunction parameter
name or an engine error in any of my 8 game sessions.

**Log-rotation caveat (honesty):** the module keeps only three generations, and the three *extra* visual
sessions I ran afterwards rotated the run-two native logs out (c/d: `.log` is now visual C, `.1` visual B,
`.2` visual A). The run-two native lines quoted in §2.1/§2.2 were read **while those logs existed**; the
verbatim `===`/`RESULT` transcript of every run is kept (`run-cd-*.log`, `death-*.log`, `visual*.log`), and a
copy of one mid-run native-log pair is in `run1-coop\`. If a re-check of the run-two engine log is needed,
re-run `run-two.ps1` once and grep `.log` before any visual session.

**Crash dumps created during my runs: none.**

* `%LOCALAPPDATA%\CrashDumps` — newest file before my first run: `MortalShell2-Win64-Shipping.exe.49248.dmp`
  2026-10-26 **05:31:49**, 140 963 977 B; after all runs (re-checked 06:32) the newest is still that one. No dump was
  written during 05:46-06:24, so there is no stack to inspect (nothing with or without a `dxgi.dll` frame).
* `<install>\MortalShell2\Saved\Crashes` — does not exist in any of the four installs.
* WER `ReportArchive` — no `MortalShell` report after 05:40.
* The known `0xC0000374` `steam_api64` double-free (round-5/6 dumps, no dxgi frame) did **not** reproduce in
  any of my 8 sessions (run-two ×2, death ×3, visual ×3) — no client died, every
  transcript ends `alive=2` / `RESULT: PASS|FAIL` with both processes up.

---

## 6. Summary / what is broken

I edited **nothing** in `ms2-coop` (main): it was rebuilt (step 1), its DLL was deployed to the four installs,
and its scripts were run from the main tree in fresh processes. The only files I wrote are in this workspace
(`REPORT.md`, the drivers `run-cd.ps1` / `run-death.ps1` / `run-visual*.ps1`, `contact-sheet.ps1`, the
transcripts `run-cd-*.log`, `death-*.log`, `visual*.log`, `build-r7final*.log`, `appendix-*.md`).

### Verified PASS

* **Build**: `112 checks passed`, `Built:`, exit 0.
* **All three death phases** on test/b: `DCASE2 PASS`, `DHOLD PASS` + `DHOLDCAM PASS` + release/camera-back
  PASS, `DWIPE PASS` — and the boss-hold spectate camera now really follows the partner (`viewpuppet=1`),
  closing the round-5 `DHOLDCAM: WARN`.
* **Merged round-7 features that pass twice in a row**: BREAK (peer break applied), WIRE + WIRETEST +
  FIXRESET (datagrams ≤ 1150 B, 0 parts lost, 12 fixture objects reset by guid/class), BOSSPHASE (owner
  phase 2 → peer `applied:1`), CINE verdict (sequence + replay + idle puppet + no `pd` charge),
  DIALOGVOTE (7/7) and DIALOGCODEC (21/21), DROPS (both directions, no double roll), plus every round-5/6
  verdict (SHELL, WEAPON, PUPVIS, LOOK, ANIM, DAMAGE, DEATH, WORLD, PROGRESS, QUEST, QUEST2, LIFT, CHEST,
  STORE_A2SELFTEST, SPAWN, MIRROR, PEERHIT, AITARGET, NPCFAR, LEDGER, ZONE, PEERHP, PEERDEAD, puppet
  tracking A/B).
* **Normal gameplay rendering**: own body + weapon + HUD on both clients, peer puppet with its weapon, no
  debug wireframes (26-frame control run).
* **Logs**: 0 × `has no parameter`, 0 × `ERR`, 0 × `MISMATCH` (only the deliberate `zonefake` lines) in all
  four installs' three log generations.
* **No crash**: no dump, no `Saved\Crashes`, no WER entry; the old `steam_api64` `0xC0000374` did not
  reproduce in 8 sessions.

### FAIL — 2 deterministic + 1 flaky (and 1 picture-level defect no verdict covers)

| # | Symptom | Which merged feature | Root cause (proof in §) | Fix |
|---|---|---|---|---|
| 1 | `STORE_A2` empty every run (gates `RESULT`) | **WIRE** (`fixreset`, r7-wire) + round-5 ledger | `fixreset` resets only 5 save-object classes; `BPSO_NPC_C` (the `wa2test` boolean fixture) is reset on the host only, so the guest still holds it `true` from the boot snapshot, its *live* apply is deferred to 06:00:34 (4 s after the harness gave up) and never gets a confirmed `liveOk`, so the judge never writes the verdict (§2.1) | add `BPSO_NPC_C` (+ `BPSO_Tarforge_C`, `BPSO_FC_FinalBossManager_C`) to `TestFixReset()`'s `kSaveObjects`, or send `wa2reset` to the guest from `run-two.ps1`; do **not** just widen the wait |
| 2 | `DIALOG` FAIL every run (gates `RESULT`): `listenLines=False`, empty verdict line | **DIALOG** (r7-dialog) | the listener resolves the dialogue asset with `net::Find(path,true)`, which has no asset loader; a client that never dialogued itself logs `asset not loaded yet: /Game/…/DLG_HubCultist02_Greeting…`, so `g_linesReal` stays 0 and `result_dialog.txt` is never written (§2.2) | owner publishes the current node's speaker+line (+ voice path) inside the existing `dl` key; or give the listener a real asset load. The r7b report's PASS required the asset already loaded on the listener |
| 3 | `STREAM` FAIL once, PASS once | not round 7 (stream/travel) | 60 s travel + async cell load vs a 130 s wait; no round-7 code touches it (§2.3) | none needed; known flake |
| 4 | **After a real cutscene both clients keep `hidden=1`; the sequence client's camera stays on the leftover `CineCameraActor` (`povToBody=4006`); own body and HUD never come back** | **CINE** (r7-dialog) | `cinestop: ForceStopCinematic` / `cine: replay stop` skip the game's own end-of-cutscene restore; `selfcheck` proves `hidden=1` while normal gameplay shows `hidden=0` (§4 run C). No verdict checks presentation | restore the presentation after a cinematic the way `coop_death.cpp` does after death (`SetActorHiddenInGame(false)` on the pawn, `SetViewTarget` back to the pawn, re-show the HUD), and add a log/verdict line for it (`selfcheck`-style `hidden=`/`viewTarget=`) |

**RESULT lines of this verification:**

```
build  : coop_policy: 112 checks passed, 0 failed / Built: …\native\build\dxgi.dll          -> build PASS (exit 0)
run-two #1 (c/d, 47160): RESULT: FAIL      (STORE_A2, DIALOG; STREAM)
run-two #2 (c/d, 47160): RESULT: FAIL      (STORE_A2, DIALOG)
death case2 (test/b, 47170): RESULT: PASS
death hold  (test/b, 47170): RESULT: PASS
death wipe  (test/b, 47170): RESULT: PASS
visual  (c/d, 47160): alive=2, 3 sessions, 78 screenshots                          -> see §4
```

### Left open / recommendations

1. Fix #1 and #2 above, then re-run `run-two.ps1` twice on C/D — with those two lines green, `RESULT` should
   be `PASS` (every other verdict already passes twice).
2. Fix #4 (presentation restore after CINE) and add a `selfcheck`-based line to `run-two.ps1`'s CINE block so
   a log-only run can catch it; `visual-two.ps1 -Cmds 'A=cineplay;A=cinestop;A=selfcheck;B=selfcheck'` is the
   ready-made reproducer.
3. The BREAK prop-level on-screen check still needs `break-lab.ps1` (the hub camera does not frame the cart
   that `brkbreak nearest` picks, 4 224 uu away).
4. `STREAM` remains flaky at ~50 % in this environment (1 of 2 here, PASS in round 6) — it should not gate
   `RESULT` on a single sample.
5. Environment note (not a mod issue): every harness script calls `Get-CimInstance Win32_Process` in its
   `Stop-All` (to kill stale `cooprelay.py`), and this agent's file sandbox denies WMI with `Access denied`.
   Under `$ErrorActionPreference = 'Stop'` that aborts the run before the first verdict. I ran the drivers
   with the sandbox lifted for those commands; if a future agent sees an immediate `Access denied` from
   `run-two*.ps1`/`visual-two.ps1`, that is the cause, not the DLL.

---

## Appendix A — every verdict line of the two c/d runs

(see below: every `===`/`RESULT` line of the two runs, copied verbatim from run-cd-1.log / run-cd-2.log)

### run-two.ps1 run 1 (port 47160, host c, guest d) -- full transcript: round1\run-cd-1.log
```
=== FIXRESET: phase A=sync B=sync after the boot wait
=== FIXRESET: A PASS fixreset: 12 save object(s) reset by guid/class (world 4, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: B PASS fixreset: 12 save object(s) reset by guid/class (world 4, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: PASS
=== WIRETEST: A PASS wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back byte-identical (2675 B); with one part dropped the hot block is intact and 4 of 5 part(s) arrived
=== WIRETEST: B PASS wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back byte-identical (2675 B); with one part dropped the hot block is intact and 4 of 5 part(s) arrived
=== WIRETEST: PASS
=== SHELL: A body=/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk ; B sees '/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk' ; B body=/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk ; A sees '/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk'
=== SHELL: PASS
=== WEAPON: A wpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 ; B wpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 ; A sees peerwpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 applied=[["/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01","S","Socket_Prop_R",0.00,0.00,0.00,0.00,0.00,0.00],["/Game/Sparta/Weapons/Player/Nail_Shotgun/Mesh/Nail_Shotgun_StaticMesh.Nail_Shotgun_StaticMesh","S","Socket_NailShotgun_Stowed",0.00,0.00,0.00,0.00,0.00,-90.00],["/Game/Sparta/Weapons/Player/VatraSeal/StaticMesh/SM_VatraSeal_Model.SM_VatraSeal_Model","S","Socket_Prop_Stowed_InfiniteSeal_Right",0.00,0.00,0.00,0.00,0.00,0.00]] ; B applied=[["/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01","S","Socket_Prop_R",0.00,0.00,0.00,0.00,0.00,0.00],["/Game/Sparta/Weapons/Player/Nail_Shotgun/Mesh/Nail_Shotgun_StaticMesh.Nail_Shotgun_StaticMesh","S","Socket_NailShotgun_Stowed",0.00,0.00,0.00,0.00,0.00,-90.00],["/Game/Sparta/Weapons/Player/VatraSeal/StaticMesh/SM_VatraSeal_Model.SM_VatraSeal_Model","S","Socket_Prop_Stowed_InfiniteSeal_Right",0.00,0.00,0.00,0.00,0.00,0.00]]
=== WEAPON: PASS
=== PUPVIS: A PASS puppet render: visible=1 animmode=1 animinstance=AnimSingleNodeInstance weapons=3/3 flat=0 ; B PASS puppet render: visible=1 animmode=1 animinstance=AnimSingleNodeInstance weapons=3/3 flat=0
=== PUPVIS: PASS
=== LOOK on: B look=038dd00f parts=1 (was 0) ; A peerlook=038dd00f applied=038dd00f parts=1 missing=0 loads=0
=== LOOK off: B parts=0 ; A applied=2465c27f parts=0
=== LOOK: PASS (on=True off=True)
=== ANIM: PASS puppet locomotion: sequence states idle=419 walk=20 sprint=0, moving-while-animated=20 ticks
=== DAMAGE: PASS
=== DEATH: PASS
=== DROPS: A rolled:2 calls:0 sent:2 got:1 gold:60 gloom:300 items:0 vetoed:0 dup:78 hooks:5 queued:2 pending:0 lateveto:0 cancelled:0
=== DROPS: B rolled:1 calls:0 sent:1 got:2 gold:60 gloom:300 items:0 vetoed:0 dup:155 hooks:5 queued:1 pending:0 lateveto:0 cancelled:0
=== DROPS: dir1 (A owns) A rolled [BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334] 20/100 -> B granted [BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334] 20/100
=== DROPS: dir2 (B owns) B rolled [BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134] 20/100 -> A granted [BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134] 20/100
=== DROPS: A claim: PASS enemy BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334: rolled gold=20 gloom=100 items=0, published seq 1 (from CharacterDeathData: goldOnDeath=20 chance=1.00 gloomOnDeath=100)
=== DROPS: B grant: PASS enemy BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334: granted gold=20 gloom=100 items=0 (peer rolled gold=20 gloom=100 items=0, seq 1); stacks gold -1->-1 gloom -1->-1; own roll vetoed=0 dup=0
=== DROPS: B claim: PASS enemy BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134: rolled gold=20 gloom=100 items=0, published seq 1 (from CharacterDeathData: goldOnDeath=20 chance=1.00 gloomOnDeath=100)
=== DROPS: A grant: PASS enemy BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134: granted gold=20 gloom=100 items=0 (peer rolled gold=20 gloom=100 items=0, seq 1); stacks gold -1->-1 gloom -1->-1; own roll vetoed=0 dup=0
=== DROPS: PASS both directions: each owner rolled and the other client granted the same (20/100 and 20/100), no double roll, hooks 5/5
=== WORLD: A seen:170 true:16 peer:16 live:0 base:0 fail:0 scans:266 ; B seen:170 true:12 peer:16 live:2 base:4 fail:0 scans:196
=== WORLD: PASS world state: door BP_HubDoubleDoor_C_UAID_D4E98A7EF567D94802_83a1dc45e2a01438_1808979936_1808979937 opened, BP_CurrencyBag_Pickup_C_UAID_D4E98A7EF567C9C102_83a1dc45e2a01438_1764479806_1764479807 collected (live=2 base=4 fail=0)
=== PROGRESS: A b:4/4+0!0 q:15/8+0!0 d:104/0+0!0 ; B b:4/4+1!0 q:15/8+2!0 d:104/104+2!0
=== PROGRESS: PASS world progression: i|BP_Interactable_Chest_General_C_UAID_D4E98A7EF56741D002_83a1dc45e2a01438_1718900240_1718900241|TimesUsed, tag t|Prologue.FlowerCrownRecieved removed+restored, layer DL_AU loaded+reactivated (wb=1/0 wq=2/0 wd=2/0)
=== QUEST: PASS quest LocationQuest added then completed (active now=false, wq=6/0)
=== QUEST2: PASS quest state GorfQuest: count heard 1 objective(s), stage s=3, tracked, rel r|Gorf=2, turned in (wq=13/0)
=== LIFT: A n:1 seq:1 peer:1 applied:0 fail:0 ; B n:1 seq:0 peer:1 applied:1 fail:0
=== LIFT: PASS lift BP_HubLift_C_UAID_D4E98A7EF5672F1B02_83a1dc45e2a01438_1653632767_1653632768 followed the peer to floor 1 (cabin z -42570 -> -40282, travel 2288, applied 1, fail 0)
=== CHEST: A n:8 seq:1 events:1 applied:0 loot:0 queue:0 fail:0 ; B n:8 seq:0 events:0 applied:1 loot:1 queue:0 fail:0
=== CHEST: PASS chest BP_Interactable_Chest_General_C_UAID_D4E98A7EF5673FD002_83a1dc45e2a01438_2008351691_2008351692: lid replayed (anim pos 5.07), loot ID_Charges_Default.ID_Charges_Default_C 1 -> 2 (applied 1, loot 1, fail 0)
=== STORE_A2: A kinds:10 typed:3 scans:554 recv:7577 applied:2 str:0 fail:0 leaks:0 ; B kinds:10 typed:3 scans:513 recv:7499 applied:2 str:0 fail:0 leaks:0
=== STORE_A2: 
=== STORE_A2SELFTEST: ws2 self test: 20 pass, 0 fail
=== STORE_A2CENSUS: 23 census rows (ws2_census.txt)
=== SPAWN: PASS enemy spawned by the peer appeared here too: SP_BP_Spawner_Proximity_C_UAID_D4E98A7EF5671DF601_1555938298_1555938299 -> BP_Horde_Brigand_C_2147452298_2147452299 (A driven:0 taken:0 released:0 montages:0 spawnask:0 spawnok:0 retarget:0 pdsent:0.000 pdapplied:0)
=== MIRROR: PASS host copy driven by the guest (AI paused, owner motion/facing/montage) (A driven:4 taken:4 released:0 montages:0 spawnask:4 spawnok:4 retarget:0 pdsent:0.000 pdapplied:0 ; B driven:0 taken:2 released:2 montages:0 spawnask:0 spawnok:0 retarget:0 pdsent:0.000 pdapplied:0)
=== PEERHIT: PASS peer-side hit applied to own body: 0.480 -> 0.380
=== AITARGET: PASS enemy BP_NPC_ShellKeeper_C_UAID_D4E98A7EF56747A302_1167224404_1167224405 targets the peer's body after SetTargetOverride (current BP_PlayerCharacter_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01.PersistentLevel.BP_PlayerCharacter_C_2147469548_2147469549)
=== STREAM: A src:blk=false/nb=true n0:81 n1:20 npcown:1 npcmirror:262 ; B src:blk=false/nb=true n0:101 n1:91 npcown:0 npcmirror:6213
=== STREAM: FAIL remote area streamed around peer 91460 uu away after a long travel: cells around it loaded=false, static meshes 81 -> 20 (sources blk=false/nb=true); enemies there: own 1 mirrored 262
=== NPCFAR: PASS host hands BP_NPC_ShellKeeper_C_UAID_D4E98A7EF56747A302_1167224404_1167224405 to the far guest and moves its copy there
=== PERF: host working set 6509 MB -> 6892 MB with the far area loaded; A near:50.72ms(605) far:54.14ms(145) ; B near:64.01ms(557) far:61.36ms(146)
=== LEDGER: A ok keys:17 good:12 bad:4 applied:4 ; B ok keys:17 good:0 bad:0 applied:0
=== LEDGER: A PASS ledger: BPSO_NPC_C.HasTriggeredInitialGreeting (BP_Cultist_HUB_C_UAID_D8BBC1D053266E4902_83a1dc45e2a01438_1464626973_1464626974) re-applied from the ledger on load (ledger 16 keys, applied 3) ; B -
=== BREAK: A rows:81 sent:2 recv:0 applied:0 fail:0 live:1 fall:0 pending:0 echo:0
=== BREAK: B rows:132 sent:1 recv:6 applied:1 fail:0 live:1 fall:0 pending:0 echo:0
=== BREAK: PASS peer break 75e02c2e BP_VFX_Breakable_Cart_02_A_C BP_VFX_Breakable_Cart_02_A_C_UAID_345A60361B8776BE02_83a1dc45e2a01438_2105289101_2105289102 (FilteredHit)
=== BREAK: PASS
=== WIRE: A txp:6 txmax:1149 txb:4917 hot:948 rxp:7 rxmax:1148 rxb:5346 lost:0 v:2
=== WIRE: B txp:7 txmax:1148 txb:5346 hot:947 rxp:6 rxmax:1149 rxb:4917 lost:0 v:2
=== WIRE: relay [05:53:44] wire: tx sent=8502 recv=0 max=1149 parts=1345 dropped=0 parts_rx=7228 parts_lost=0 legacy=25 oversize=0 torn=0 loss=0 | [05:53:47] wire: tx sent=7324 recv=0 max=1149 parts=1451 dropped=0 parts_rx=8549 parts_lost=0 legacy=29 oversize=0 torn=0 loss=0 (sent=15826 parts_lost=0)
=== WIRE: PASS (packet split into 6/7 part(s), largest datagram 1149 B <= 1200, reassembled 7/6, parts lost 0)
=== ZONE: before 'me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5363 sk:0 guard:1'
=== ZONE: mismatch=True (applies 5376 -> 5376 and skips 10 -> 70 while paused) resumed=True (applies -> 5425)
=== ZONE: A me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5425 sk:81 guard:1
=== ZONE: B me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5342 sk:82 guard:1
=== ZONE: PASS zone guard: /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 vs /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 -- world apply paused 5.4s (81 applies skipped), resumed at /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 (applies 5376 -> 5387)
=== ZONE: PASS
=== DIALOG: realConversation=False (dlgstart content-gated in this save; see REPORT)
=== DIALOG: A me:0 peer:1 kind:1 name:DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting node:0 opts:0 seq:1 idle:7 pd:7 ai:7 ep:1 mine:0 listen:1 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOG: B me:0 peer:0 kind:0 name:- node:0 opts:0 seq:0 idle:0 pd:0 ai:0 ep:0 mine:1 listen:0 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOG: 
=== DIALOGVOTE: A PASS dialogue votes: 7 checks, 0 failed ; B PASS dialogue votes: 7 checks, 0 failed
=== DIALOG: FAIL (peerBusySeen=True realConversation=False idlePuppet=True listenLines=False pdNotCharged=True [0.1 -> 0.1] votes=True lockEnded=True)
=== CINE: A me:0 peer:0 name:- ep:2 replay:2/1/2 on:1
=== CINE: B me:0 peer:0 name:- ep:1 replay:1/0/1 on:1
=== CINE: PASS cinematic lock: peer 'LevelSequence_ShellKeeperIntro_CoreWorld' held 1.5s ; puppet idle samples=4, damage charges suppressed=4, replays started=1 (jumps=0, stops=0)
=== CINE: PASS (aPlaying=True bReplays=False replayCounter=True idlePuppet=True pdNotCharged=True [0 -> 0] endedBoth=True)
=== PEERHP: A me:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:0.0 pup:0.380 pdf:0 ok:1
=== PEERHP: B me:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:0.1 pup:1.000 pdf:0 ok:1
=== PEERHP: PASS peer body: hp=28.500/75.000 (0.380) shp=0.000/0.000 res=100.000/100.000 heal=3/3 pp=1.000/1.000 ammo=0/0 dark=0 ds=0 ; puppet health set reads back 0.380
=== PEERHP: PASS (received=True puppetMatches=True)
=== PEERDEAD: A me:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds1 age:0.0 pup:0.380 pdf:1 ok:1
=== PEERDEAD: B me:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds1 peer:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:0.1 pup:1.000 pdf:0 ok:1
=== PEERDEAD: PASS peer death: ds=1 ; the puppet plays /Game/MS1/Characters/DeathsLibrary/death_093_front.death_093_front (held 0.5s on the mesh)
=== PEERDEAD: PASS (clipOnPuppet=True)
=== BOSSPHASE: A before 'n:0 phase:- scanned:408 hook:0 pub:0 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0'
=== BOSSPHASE: spawn h=True g=True ; A n:1 phase:BP_PlayerCharacter_C_2147479937_2147479938:2 scanned:414 hook:1 pub:1 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0
=== BOSSPHASE: B n:1 phase:BP_PlayerCharacter_C_2147479937_2147479938:2 scanned:395 hook:1 pub:0 applied:1 seen:1 fail:0 missed:0 echoes:0 peerkeys:1 off:0
=== BOSSPHASE: PASS (owner phase 2 + peer applied>=1)
=== DIALOGREAL: A me:0 peer:0 kind:0 name:- node:0 opts:0 seq:4 idle:51 pd:51 ai:51 ep:2 mine:1 listen:1 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOGREAL: B me:0 peer:0 kind:0 name:- node:0 opts:0 seq:2 idle:9 pd:9 ai:9 ep:1 mine:2 listen:0 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOGCODEC: PASS dialog wire codec: 21 checks, 0 failed
=== NPC: 
=== A: PASS puppet tracks peer (dist 0.4, 30/30 good; moves 12, snaps 0)
=== B: PASS puppet tracks peer (dist 0.4, 30/30 good; moves 13, snaps 0)
=== NATIVE: E:\Games\_MS2-coop-c -> READY ; coop started
=== NATIVE: E:\Games\_MS2-coop-d -> READY ; coop started
RESULT: FAIL
```

### run-two.ps1 run 2 (port 47160, host c, guest d) -- full transcript: round1\run-cd-2.log
```
=== FIXRESET: phase A=sync B=sync after the boot wait
=== FIXRESET: A PASS fixreset: 12 save object(s) reset by guid/class (world 4, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: B PASS fixreset: 12 save object(s) reset by guid/class (world 4, lift 0 of 0, chest 8 of 8), 0 store-B TimesUsed cleared
=== FIXRESET: PASS
=== WIRETEST: A PASS wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back byte-identical (2675 B); with one part dropped the hot block is intact and 4 of 5 part(s) arrived
=== WIRETEST: B PASS wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back byte-identical (2675 B); with one part dropped the hot block is intact and 4 of 5 part(s) arrived
=== WIRETEST: PASS
=== SHELL: A body=/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk ; B sees '/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk' ; B body=/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk ; A sees '/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk'
=== SHELL: PASS
=== WEAPON: A wpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 ; B wpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 ; A sees peerwpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 applied=[["/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01","S","Socket_Prop_R",0.00,0.00,0.00,0.00,0.00,0.00],["/Game/Sparta/Weapons/Player/Nail_Shotgun/Mesh/Nail_Shotgun_StaticMesh.Nail_Shotgun_StaticMesh","S","Socket_NailShotgun_Stowed",0.00,0.00,0.00,0.00,0.00,-90.00],["/Game/Sparta/Weapons/Player/VatraSeal/StaticMesh/SM_VatraSeal_Model.SM_VatraSeal_Model","S","Socket_Prop_Stowed_InfiniteSeal_Right",0.00,0.00,0.00,0.00,0.00,0.00]] ; B applied=[["/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01","S","Socket_Prop_R",0.00,0.00,0.00,0.00,0.00,0.00],["/Game/Sparta/Weapons/Player/Nail_Shotgun/Mesh/Nail_Shotgun_StaticMesh.Nail_Shotgun_StaticMesh","S","Socket_NailShotgun_Stowed",0.00,0.00,0.00,0.00,0.00,-90.00],["/Game/Sparta/Weapons/Player/VatraSeal/StaticMesh/SM_VatraSeal_Model.SM_VatraSeal_Model","S","Socket_Prop_Stowed_InfiniteSeal_Right",0.00,0.00,0.00,0.00,0.00,0.00]]
=== WEAPON: PASS
=== PUPVIS: A PASS puppet render: visible=1 animmode=1 animinstance=AnimSingleNodeInstance weapons=3/3 flat=0 ; B PASS puppet render: visible=1 animmode=1 animinstance=AnimSingleNodeInstance weapons=3/3 flat=0
=== PUPVIS: PASS
=== LOOK on: B look=038dd00f parts=1 (was 0) ; A peerlook=038dd00f applied=038dd00f parts=1 missing=0 loads=0
=== LOOK off: B parts=0 ; A applied=2465c27f parts=0
=== LOOK: PASS (on=True off=True)
=== ANIM: PASS puppet locomotion: sequence states idle=469 walk=20 sprint=0, moving-while-animated=20 ticks
=== DAMAGE: PASS
=== DEATH: PASS
=== DROPS: A rolled:2 calls:0 sent:2 got:1 gold:60 gloom:300 items:0 vetoed:0 dup:81 hooks:5 queued:2 pending:0 lateveto:0 cancelled:0
=== DROPS: B rolled:1 calls:0 sent:1 got:2 gold:60 gloom:300 items:0 vetoed:0 dup:153 hooks:5 queued:1 pending:0 lateveto:0 cancelled:0
=== DROPS: dir1 (A owns) A rolled [BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334] 20/100 -> B granted [BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334] 20/100
=== DROPS: dir2 (B owns) B rolled [BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134] 20/100 -> A granted [BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134] 20/100
=== DROPS: A claim: PASS enemy BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334: rolled gold=20 gloom=100 items=0, published seq 1 (from CharacterDeathData: goldOnDeath=20 chance=1.00 gloomOnDeath=100)
=== DROPS: B grant: PASS enemy BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334: granted gold=20 gloom=100 items=0 (peer rolled gold=20 gloom=100 items=0, seq 1); stacks gold -1->-1 gloom -1->-1; own roll vetoed=0 dup=0
=== DROPS: B claim: PASS enemy BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134: rolled gold=20 gloom=100 items=0, published seq 1 (from CharacterDeathData: goldOnDeath=20 chance=1.00 gloomOnDeath=100)
=== DROPS: A grant: PASS enemy BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134: granted gold=20 gloom=100 items=0 (peer rolled gold=20 gloom=100 items=0, seq 1); stacks gold -1->-1 gloom -1->-1; own roll vetoed=0 dup=0
=== DROPS: PASS both directions: each owner rolled and the other client granted the same (20/100 and 20/100), no double roll, hooks 5/5
=== WORLD: A seen:170 true:17 peer:17 live:0 base:0 fail:0 scans:268 ; B seen:170 true:17 peer:17 live:2 base:5 fail:0 scans:208
=== WORLD: PASS world state: door BP_HubDoubleDoor_C_UAID_D4E98A7EF567D94802_83a1dc45e2a01438_1808979936_1808979937 opened, BP_CurrencyBag_Pickup_C_UAID_D4E98A7EF567C9C102_83a1dc45e2a01438_1764479806_1764479807 collected (live=2 base=5 fail=0)
=== PROGRESS: A b:4/4+0!0 q:15/8+0!0 d:104/0+0!0 ; B b:4/4+2!0 q:15/8+2!0 d:104/104+2!0
=== PROGRESS: PASS world progression: i|BP_Interactable_Chest_General_C_UAID_D4E98A7EF56741D002_83a1dc45e2a01438_1718900240_1718900241|TimesUsed, tag t|Prologue.FlowerCrownRecieved removed+restored, layer DL_AU loaded+reactivated (wb=2/0 wq=2/0 wd=2/0)
=== QUEST: PASS quest LocationQuest added then completed (active now=false, wq=6/0)
=== QUEST2: PASS quest state GorfQuest: count heard 1 objective(s), stage s=3, tracked, rel r|Gorf=2, turned in (wq=13/0)
=== LIFT: A n:1 seq:1 peer:1 applied:0 fail:0 ; B n:1 seq:0 peer:1 applied:1 fail:0
=== LIFT: PASS lift BP_HubLift_C_UAID_D4E98A7EF5672F1B02_83a1dc45e2a01438_1653632767_1653632768 followed the peer to floor 1 (cabin z -42570 -> -40038, travel 2532, applied 1, fail 0)
=== CHEST: A n:8 seq:1 events:1 applied:0 loot:0 queue:0 fail:0 ; B n:8 seq:0 events:0 applied:1 loot:1 queue:0 fail:0
=== CHEST: PASS chest BP_Interactable_Chest_General_C_UAID_D4E98A7EF5673FD002_83a1dc45e2a01438_2008351691_2008351692: lid replayed (anim pos 5.07), loot ID_Charges_Default.ID_Charges_Default_C 1 -> 2 (applied 1, loot 1, fail 0)
=== STORE_A2: A kinds:10 typed:3 scans:558 recv:7850 applied:2 str:0 fail:0 leaks:0 ; B kinds:10 typed:3 scans:523 recv:7718 applied:2 str:0 fail:0 leaks:0
=== STORE_A2: 
=== STORE_A2SELFTEST: ws2 self test: 20 pass, 0 fail
=== STORE_A2CENSUS: 23 census rows (ws2_census.txt)
=== SPAWN: PASS enemy spawned by the peer appeared here too: SP_BP_Spawner_Proximity_C_UAID_D4E98A7EF567C67402_1440305372_1440305373 -> BP_Horde_Brigand_C_2147452434_2147452435 (A driven:0 taken:0 released:0 montages:0 spawnask:3 spawnok:0 retarget:0 pdsent:0.000 pdapplied:0)
=== MIRROR: PASS host copy driven by the guest (AI paused, owner motion/facing/montage) (A driven:3 taken:3 released:0 montages:0 spawnask:3 spawnok:3 retarget:0 pdsent:0.000 pdapplied:0 ; B driven:0 taken:2 released:2 montages:0 spawnask:0 spawnok:0 retarget:0 pdsent:0.000 pdapplied:0)
=== PEERHIT: PASS peer-side hit applied to own body: 1.000 -> 0.900
=== AITARGET: PASS enemy BP_NPC_ShellKeeper_C_UAID_D4E98A7EF56747A302_1167224404_1167224405 targets the peer's body after SetTargetOverride (current BP_PlayerCharacter_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01.PersistentLevel.BP_PlayerCharacter_C_2147469550_2147469551)
=== STREAM: A src:blk=false/nb=true n0:85 n1:20 npcown:1 npcmirror:181 ; B src:blk=false/nb=true n0:101 n1:91 npcown:0 npcmirror:6375
=== STREAM: PASS remote area streamed around peer 91460 uu away after a long travel: cells around it loaded=true, static meshes 85 -> 20 (sources blk=false/nb=true); enemies there: own 1 mirrored 181
=== NPCFAR: PASS host hands BP_NPC_ShellKeeper_C_UAID_D4E98A7EF56747A302_1167224404_1167224405 to the far guest and moves its copy there
=== PERF: host working set 6592 MB -> 6917 MB with the far area loaded; A near:44.90ms(605) far:37.19ms(147) ; B near:52.14ms(565) far:39.10ms(148)
=== LEDGER: A ok keys:17 good:13 bad:5 applied:5 ; B ok keys:17 good:0 bad:0 applied:0
=== LEDGER: A PASS ledger: BPSO_NPC_C.HasTriggeredInitialGreeting (BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134) re-applied from the ledger on load (ledger 17 keys, applied 4) ; B -
=== BREAK: A rows:82 sent:2 recv:1 applied:1 fail:0 live:2 fall:0 pending:0 echo:0
=== BREAK: B rows:137 sent:1 recv:4 applied:1 fail:0 live:1 fall:0 pending:0 echo:0
=== BREAK: PASS peer break 75e02c2e BP_VFX_Breakable_Cart_02_A_C BP_VFX_Breakable_Cart_02_A_C_UAID_345A60361B8776BE02_83a1dc45e2a01438_2105289101_2105289102 (FilteredHit)
=== BREAK: PASS
=== WIRE: A txp:6 txmax:1149 txb:4875 hot:948 rxp:7 rxmax:1148 rxb:5260 lost:0 v:2
=== WIRE: B txp:7 txmax:1148 txb:5260 hot:947 rxp:6 rxmax:1149 rxb:4875 lost:0 v:2
=== WIRE: relay [06:02:06] wire: tx sent=8490 recv=0 max=1149 parts=1364 dropped=0 parts_rx=7651 parts_lost=0 legacy=27 oversize=0 torn=0 loss=0 | [06:02:08] wire: tx sent=7722 recv=0 max=1150 parts=1468 dropped=0 parts_rx=8509 parts_lost=0 legacy=28 oversize=0 torn=0 loss=0 (sent=16212 parts_lost=0)
=== WIRE: PASS (packet split into 6/7 part(s), largest datagram 1150 B <= 1200, reassembled 7/6, parts lost 0)
=== ZONE: before 'me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5393 sk:0 guard:1'
=== ZONE: mismatch=True (applies 5406 -> 5406 and skips 10 -> 69 while paused) resumed=True (applies -> 5452)
=== ZONE: A me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5452 sk:83 guard:1
=== ZONE: B me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5421 sk:82 guard:1
=== ZONE: PASS zone guard: /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 vs /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 -- world apply paused 5.5s (83 applies skipped), resumed at /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 (applies 5406 -> 5415)
=== ZONE: PASS
=== DIALOG: realConversation=False (dlgstart content-gated in this save; see REPORT)
=== DIALOG: A me:0 peer:1 kind:1 name:DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting node:0 opts:0 seq:1 idle:8 pd:8 ai:8 ep:1 mine:0 listen:1 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOG: B me:0 peer:0 kind:0 name:- node:0 opts:0 seq:0 idle:0 pd:0 ai:0 ep:0 mine:1 listen:0 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOG: 
=== DIALOGVOTE: A PASS dialogue votes: 7 checks, 0 failed ; B PASS dialogue votes: 7 checks, 0 failed
=== DIALOG: FAIL (peerBusySeen=True realConversation=False idlePuppet=True listenLines=False pdNotCharged=True [0.1 -> 0.1] votes=True lockEnded=True)
=== CINE: A me:0 peer:1 name:LevelSequence_ShellKeeperIntro_CoreWorld ep:2 replay:2/1/1 on:1
=== CINE: B me:0 peer:0 name:- ep:1 replay:1/0/1 on:1
=== CINE: PASS cinematic lock: peer 'LevelSequence_ShellKeeperIntro_CoreWorld' held 2.0s ; puppet idle samples=5, damage charges suppressed=5, replays started=1 (jumps=0, stops=0)
=== CINE: PASS (aPlaying=True bReplays=False replayCounter=True idlePuppet=True pdNotCharged=True [0 -> 0] endedBoth=True)
=== PEERHP: A me:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:0.0 pup:0.380 pdf:0 ok:1
=== PEERHP: B me:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:0.1 pup:1.000 pdf:0 ok:1
=== PEERHP: PASS peer body: hp=28.500/75.000 (0.380) shp=0.000/0.000 res=100.000/100.000 heal=3/3 pp=1.000/1.000 ammo=0/0 dark=0 ds=0 ; puppet health set reads back 0.380
=== PEERHP: PASS (received=True puppetMatches=True)
=== PEERDEAD: A me:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds1 age:0.0 pup:0.380 pdf:1 ok:1
=== PEERDEAD: B me:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds1 peer:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:0.1 pup:1.000 pdf:0 ok:1
=== PEERDEAD: PASS peer death: ds=1 ; the puppet plays /Game/MS1/Characters/DeathsLibrary/death_093_front.death_093_front (held 0.5s on the mesh)
=== PEERDEAD: PASS (clipOnPuppet=True)
=== BOSSPHASE: A before 'n:0 phase:- scanned:410 hook:0 pub:0 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0'
=== BOSSPHASE: spawn h=True g=True ; A n:1 phase:BP_PlayerCharacter_C_2147479937_2147479938:2 scanned:416 hook:1 pub:1 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0
=== BOSSPHASE: B n:1 phase:BP_PlayerCharacter_C_2147479937_2147479938:2 scanned:396 hook:1 pub:0 applied:1 seen:1 fail:0 missed:0 echoes:0 peerkeys:1 off:0
=== BOSSPHASE: PASS (owner phase 2 + peer applied>=1)
=== DIALOGREAL: A me:0 peer:0 kind:0 name:- node:0 opts:0 seq:4 idle:52 pd:52 ai:52 ep:2 mine:1 listen:1 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOGREAL: B me:0 peer:0 kind:0 name:- node:0 opts:0 seq:2 idle:9 pd:9 ai:9 ep:1 mine:2 listen:0 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOGCODEC: PASS dialog wire codec: 21 checks, 0 failed
=== NPC: 
=== A: PASS puppet tracks peer (dist 0.4, 30/30 good; moves 12, snaps 0)
=== B: PASS puppet tracks peer (dist 0.4, 30/30 good; moves 13, snaps 0)
=== NATIVE: E:\Games\_MS2-coop-c -> READY ; coop started
=== NATIVE: E:\Games\_MS2-coop-d -> READY ; coop started
RESULT: FAIL
```


---

## Appendix B - every verdict line of the three death phases

### run-two-death.ps1 -Phase case2 (port 47170, host test, guest b) -- full transcript: round1\death-case2.log
```
=== LINK: PASS both clients in gameplay, peer seen
--- A death: me:1(alive) case:0 victim:0 serial:0 stacks:0 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:1 rules:[5,10,10,10,5,1,1]
--- A       0 deathmix=0 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:2 maxhp=
--- B death: me:1(alive) case:0 victim:0 serial:0 stacks:0 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1]
--- B       0 deathmix=0 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:2 maxhp=
=== DCASE2 baseline: B maxhp=75 worldresets A=2 B=2
=== DCASE2 case: PASS the death was decided as case 2
=== DCASE2: B me:1(alive) case:2 victim:2 serial:1 stacks:1 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1] maxhp 75 -> 67.5 (ratio 0.9) worldresets A 2->2 B 2->2
=== DCASE2: PASS B respawned in the live world, no reset, stacks=1, max health ~90%
--- E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
--- E:\Games\_MS2-coop-b\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
RESULT: PASS
```

### run-two-death.ps1 -Phase hold (port 47170, host test, guest b) -- full transcript: round1\death-hold.log
```
=== LINK: PASS both clients in gameplay, peer seen
--- B(hold) death: me:3(down) case:3 victim:3 serial:1 stacks:0 peer:1(seen) boss:1 pending:1 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1]
--- B(hold)       0 deathmix=1 maxhp=75.0 maxshell=0.0 viewpuppet=1 spectating:1 missrevive:0 hooks:2 maxhp=
=== DHOLD: PASS B is held (case 3, pending)
=== DHOLDCAM: PASS the rendered camera follows the partner
--- B(after hold) death: me:1(alive) case:2 victim:3 serial:1 stacks:1 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1]
--- B(after hold)       0 deathmix=0 maxhp=67.5 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:4 maxhp=
=== DHOLD release: PASS B respawned once the fight was over
=== DHOLDCAM release: PASS the camera is back on B
=== DHOLD: PASS
--- E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
--- E:\Games\_MS2-coop-b\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
RESULT: PASS
```

### run-two-death.ps1 -Phase wipe (port 47170, host test, guest b) -- full transcript: round1\death-wipe.log
```
=== LINK: PASS both clients in gameplay, peer seen
=== DWIPE baseline: worldresets A=2 B=2 stacks B=0
--- A(wipe) death: me:2(down) case:2 victim:2 serial:1 stacks:1 peer:1(seen) boss:0 pending:1 suppressed:0 worldresets:2 wr:0 peerwr:0 host:1 rules:[5,10,10,10,5,1,1]
--- A(wipe)       0 deathmix=1 maxhp=67.5 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:2 maxhp=
--- B(wipe) death: me:4(down) case:1 victim:2 serial:1 stacks:0 peer:2(seen) boss:0 pending:1 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1]
--- B(wipe)       0 deathmix=1 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:2 maxhp=
=== DWIPE case: PASS a party wipe was seen
--- A(after wipe) death: me:1(alive) case:0 victim:2 serial:1 stacks:0 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:3 wr:1 peerwr:0 host:1 rules:[5,10,10,10,5,1,1]
--- A(after wipe)       0 deathmix=0 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:4 maxhp=
--- B(after wipe) death: me:1(alive) case:1 victim:2 serial:1 stacks:0 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:1 host:0 rules:[5,10,10,10,5,1,1]
--- B(after wipe)       0 deathmix=0 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:4 maxhp=
=== DWIPE: host reset A worldresets 2->3 ; guest did NOT reset its own (B 2->2)
=== DWIPE: both alive=True stacks A=0 B=0
=== DWIPE: PASS host reset, guest followed, both alive, 0 stacks
--- E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
--- E:\Games\_MS2-coop-b\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
RESULT: PASS
```


---

## Appendix C — visual runs: paths and what each frame set contains

All three runs: host `E:\Games\_MS2-coop-c`, guest `E:\Games\_MS2-coop-d`, port 47160,
`visual-two.ps1 -BootWait 200`, screenshots `PrintWindow(PW_RENDERFULLCONTENT)` of the real windows
(1040x615), contact sheets built with System.Drawing (`wt\r7-final\contact-sheet.ps1`, 4 columns, 470 px
tiles) and looked at.

| run | command | output dir | sheet |
|---|---|---|---|
| A (briefed) | `-Tag r7final -Cmds 'A=brkbreak nearest;A=cineplay'` | `local\lab\20261026-061603-r7final\` | `contact-sheet.png` (26 tiles) |
| B (control, no cutscene) | `-Tag r7final-nocine -Cmds 'A=selfcheck;B=selfcheck'` | `local\lab\20261026-062102-r7final-nocine\` | `contact-sheet.png` (26 tiles) |
| C (cutscene + selfcheck) | `-Tag r7final-cinecheck -Cmds 'A=cineplay;A=cinestop;A=selfcheck;B=selfcheck'` | `local\lab\20261026-062559-r7final-cinecheck\` | `contact-sheet.png` (26 tiles) |

Frame names in every dir: `A-after-cmds1..4`, `B-after-cmds1..4` (both screens right after the commands),
`A-sees-B-dir{0,45,…,315}` (8 directions, B teleported 380 uu around A), `A-sees-B-move1..6` (B patrolling),
`B-sees-A-idle`, `B-sees-A-move1..3` (A brought in front of B), plus `probe_A.log`, `probe_B.log`,
`sync_A.txt`, `sync_B.txt`.

What I see, per checklist item:

* **Own body + weapon** — Run B: yes on both (`A-after-cmds*` and `A-sees-B-*` show A's own shell in the
  foreground with the sword; `B-after-cmds*` show B's own shell + sword). Run A/C after the cutscene: **no**
  (pawn `hidden=1`, see §4 run C).
* **HUD** — Run B: yes on both frames (health/resolve bottom-left, two item slots bottom-right, currency
  `1212 / 0` top-right, subtitle line mid-screen). Run A/C: **no** after the cutscene.
* **Peer puppet visible** — yes in all runs, from both sides: `A-sees-B-dir45` (B's dark form next to the
  cultist), `B-sees-A-idle` / `B-sees-A-move*` (A's shell with Hadern's sword on its back), and the same
  puppet in run B's `A-sees-B-*` / `B-after-cmds*` frames.
* **No debug wireframes** — none in any of the 78 frames (no bone/socket/collision draws, no text overlays
  other than the game's own subtitle).
* **Barrel break (BREAK)** — the host's `brkbreak nearest` broke `BP_VFX_Breakable_Cart_02_A_C` at d=4224 uu
  (log), which is outside every frame of this run; the peer-side replay is proven by `BREAK: PASS` and
  `BREAK: A … sent:2 recv:1 applied:1` in run 2 (§2). Use `break-lab.ps1` for the prop close-up.
* **Cutscene** — real: `cineplay` → `LevelSequence_ShellKeeperIntro_CoreWorld` on A and its replay on B are
  visible in `A-after-cmds2..4` / `B-after-cmds2..4` (the cultist close-up), and the state/lock in the logs
  (`CINE: PASS`, `PASS cinematic lock … held 2.0s`).

---

## Appendix D — session list (all 8 game sessions of this verification)

| # | session | install pair | ports | window | outcome |
|---|---|---|---|---|---|
| 1 | run-two #1 | c / d | 47160 | 05:46-05:54 | RESULT: FAIL (STORE_A2, DIALOG, STREAM) |
| 2 | run-two #2 | c / d | 47160 | 05:54-06:03 | RESULT: FAIL (STORE_A2, DIALOG) |
| 3 | death case2 | test / b | 47170 | 06:09-06:11 | RESULT: PASS |
| 4 | death hold | test / b | 47170 | 06:11-06:13 | RESULT: PASS |
| 5 | death wipe | test / b | 47170 | 06:13-06:15 | RESULT: PASS |
| 6 | visual A (briefed) | c / d | 47160 | 06:16-06:18 | alive=2, cutscene defect seen |
| 7 | visual B (control) | c / d | 47160 | 06:21-06:23 | alive=2, own body+HUD OK |
| 8 | visual C (cinecheck) | c / d | 47160 | 06:25-06:28 | alive=2, `hidden=1` / cine camera confirmed |

No client crashed; no dump was written; every transcript ends with both clients alive.

---
---

# Round 2 — re-verification of the merged MAIN tree with the three fixes

Date: 2026-10-26 07:49 – 09:30. Same installs/ports as round 1 (c/d 47160, test/b 47170),
same procedure, main tree **not edited** (rebuilt + deployed only). Round-1 artefacts were moved to
`wt\r7-final\round1\` so nothing from round 1 was overwritten.

## R2.1 Build (recorded DLL hash)

```
cmd /c G:\Freespace\shell_coop\ms2-coop\native\build.bat test > build-r7final-round2.log 2>&1
  coop_policy: 112 checks passed, 0 failed
  Built: G:\Freespace\shell_coop\ms2-coop\native\build\dxgi.dll
exit code 0
```

DLL: **sha256 `4e2fd6bbf6e266d2c57e844038445e2b7729ca4046b3b062c2379c467be240b7`**, 1 757 184 B,
built 07:52:51 (round 1 was `a351eb15…`, 1 729 024 B). Deployed to c, d, test, b (deploy.ps1 prints the same
hash on every install). Verdict: **PASS**.

## R2.2 The three fixes, as seen in the code (spot-check before running)

| finding | fix in main | where |
|---|---|---|
| STORE_A2 | `TestFixReset()`'s `kSaveObjects[]` now also resets `BPSO_NPC_C`, `BPSO_Tarforge_C`, `BPSO_FC_FinalBossManager_C` (and the comment explains the host/guest asymmetry) | `native/src/coop_world.cpp` ~2314-2345 |
| DIALOG | the line now **travels on the wire**: `"dl":[active,seq,node,opts,"name","path","speaker","line","voice"]`, and `ListenerTick()` uses `peerSpeaker/peerLine/peerVoice` first, asset only as fallback | `native/src/coop_dialog.cpp` `Serialize`, `OnPeerLine` (~2204), `ListenerTick` (~1878) |
| CINE | `RestoreCinematicPresentation(why)` (blueprint end-path fns `UnhidePlayerAndWeapons`/`ResetPlayerMovement`/`ShowHUD`/`RemoveCutsceneState`/…, pawn unhide + mesh visibility, camera-stack removal + view back) called from every end path | `native/src/coop_dialog.cpp` ~1052, called at ~1161, ~1776, ~1815 |

## R2.3 run-two on c/d, port 47160 (two complete runs, no ledger deletion)

Driver: `run-cd2.ps1` (lock -> run-two; abandons a run whose guest process disappears, so a crash does not
cost the 20-minute dead-guest tail). First pair (07:54) hit the **known steam crash** on the guest at boot
twice — see R2.6 — and was re-run. Transcripts: `run-cd-r2-attempt1.log` (complete run 1),
`run-cd-r2-attempt2.log` (complete run 2); the abandoned first pair is `run-cd-1.log` (07:54) and
`run-cd-2.log` (08:21), and `run-cd-r2-attempt3.log` is the third attempt I stopped on purpose once two
complete runs were in.

| Verdict | run 1 (attempt 1) | run 2 (attempt 2) |
|---|---|---|
| FIXRESET | PASS (A and B: 21 save objects, world 13) | PASS |
| WIRETEST | PASS | PASS |
| SHELL / WEAPON / PUPVIS / LOOK / ANIM | PASS | PASS |
| DAMAGE / DEATH | PASS | PASS |
| DROPS | PASS (both directions) | PASS |
| WORLD / PROGRESS / QUEST / QUEST2 / LIFT / CHEST | PASS | PASS |
| **STORE_A2** | **FAIL** (verdict line empty) | **FAIL** (verdict line empty) |
| STORE_A2SELFTEST / CENSUS | PASS (20/0) / 23 rows | PASS / 23 rows |
| SPAWN / MIRROR / PEERHIT / AITARGET | PASS | PASS |
| STREAM | PASS | PASS |
| NPCFAR / LEDGER | PASS | PASS |
| BREAK | PASS | PASS |
| WIRE | PASS | PASS |
| ZONE | PASS | PASS |
| **DIALOG** | **PASS** (`listenLines=True`, `lines:2`) | **PASS** (`listenLines=True`, `lines:1`) |
| DIALOGVOTE / DIALOGCODEC | PASS (7/7) / PASS (21/21) | PASS / PASS |
| CINE | PASS | PASS |
| PEERHP | PASS | **FAIL** (`received=False`, `age:3.9 s`, peer hp 5.100 vs B 27.750) — flake, see R2.5 |
| PEERDEAD | PASS | PASS |
| BOSSPHASE | PASS | PASS |
| A / B (puppet tracking) | PASS | PASS |
| **RESULT** | **FAIL** | **FAIL** |

**Two of the three fixes are verified green: DIALOG and CINE.** All other verdicts passed twice; the only
deterministic failure left is STORE_A2.

### R2.3.1 DIALOG now passes on a COLD listener (the round-1 failure is fixed)

```
=== DIALOG: A … peer:1 kind:1 name:DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting node:0 opts:0 seq:1 … listen:1 lines:2 …
=== DIALOG: PASS dialogue: peer 'DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting' held 1.8s ; puppet idle samples=5, replayed lines=2, damage charges suppressed=5, votes sent=0, resolutions seen=0
=== DIALOG: PASS (peerBusySeen=True realConversation=False idlePuppet=True listenLines=True pdNotCharged=True [0.1 -> 0.1] votes=True lockEnded=True)
```

and the host's own log shows the asset is still **not** loaded (exactly the round-1 failure) while the line
now comes from the wire and is even drawn as the game's subtitle:

```
[08:41:19][A] asset not loaded yet: /Game/Sparta/Dialogue/Dialogues/HUB/TunnelCultist/DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting
[08:41:19][A] dlg: subtitle via WBP_Subtitle_Base_C -> shown (1 instance(s), created=1, inViewport=1, update=1) [Disciple: Welcome back, High Sester.]
[08:41:19][A] dlg: LISTEN node=0 speaker=Disciple line=Welcome back, High Sester. voice=AE_VO_HubCultist_GreetGenessa_10_11 options=0
```
(both runs produced the same lines; run 2's log is `r2-logs\c-attempt2.log`)

### R2.3.2 CINE restore runs on every end path (visible in the log)

```
[08:41:29][A] cine: RESTORED presentation (sequence stop): blueprint fns=7, unhide=2, camera=1
[08:41:30][B] cine: RESTORED presentation (sequence stop): blueprint fns=7, unhide=2, camera=1
```
Screen-level confirmation is in R2.7 (the reproducer run).

### R2.3.3 STORE_A2 still FAILS — and the round-2 fix is what now blocks it

`=== STORE_A2: ` is empty in both runs; the guest never writes `result_ws2.txt`:

```
[08:40:52][B] ws2 judge: no new-class boolean replayed yet (applied int=3 str=0)
… (every 5 s until the end of the run)
```

The new `fixreset` list **does** run on both clients this time:

```
=== FIXRESET: B PASS fixreset: 21 save object(s) reset by guid/class (world 13, lift 0 of 0, chest 8 of 8), 1 store-B TimesUsed cleared
[08:35:15][B] ws2 fixture: BPSO_NPC_C reset by class -> 13 object(s) (by guid: 0 of 32 ok)
```

but resetting `BPSO_NPC_C` **re-creates the NPC's save component**, and the map then holds two (guest) /
three (host) distinct `SpartaSave.RuntimeData_*` objects for the same owner. The `ws` scan sees the key with
conflicting values and keeps "settle-resetting" it, so `RowSettled()` never becomes true and the host's live
publish is deferred forever:

```
[08:35:26][B] ws see 5f5dd95e HasTriggeredInitialGreeting=true  raw=true(boolean)  BPSO_NPC_C … SpartaSave.RuntimeData_2147470800_2147470801
[08:35:26][B] ws see 5f5dd95e HasTriggeredInitialGreeting=false raw=false(boolean) BPSO_NPC_C … SpartaSave.RuntimeData_2147468242_2147468243
[08:35:17][B] ws settle reset 5f5dd95e (row took a new fn after 18.0s: … RuntimeData_2147468242_2147468243)
[08:35:26][B] ws settle reset 5f5dd95e (row took a new fn after 9.1s: … RuntimeData_2147470800_2147470801)
[08:35:35][B] ws settle reset 5f5dd95e (row took a new fn after 8.9s: …)      (4 × on the guest, 5 × on the host)
[08:37:07][B] ws peer live 5f5dd95e (the peer says its world just changed)   <- wa2test's publish
[08:37:08][B] ws live skip 5f5dd95e: row not settled here yet (known=0)      <- deferred, never applied
```

Distinct `RuntimeData` objects seen for that one key: **guest 2**, **host 3** (`c-attempt1.log`). So the
round-2 change removed the host/guest asymmetry but replaced it with "the row can never settle".

Proposed fixes (in the order I would try them, all in main):
1. **Reset the field, not the save object.** In `TestFixReset()` use `WriteBoolOn(obj, field)=false` for the
   boolean fixtures (`BPSO_NPC_C.HasTriggeredInitialGreeting`, `BPSO_WoodenElevator_C.HasBrokenOnce`)
   instead of `ResetSaveObjectByGuid` — no `RuntimeData` is re-created, the row stays settled, and the
   guest's copy really goes false. Keep `ResetSaveObjectByGuid` for the actor-typed ones (lift, chests).
2. **Make the row identity follow the live component.** The scan keys a row by owner name and can pick any
   matching `RuntimeData`; resolve the object through the owner actor's `SpartaSave`/`Save` component (the
   helpers `GuidOfSaveObject`/`GetOwningSaveComponent` already exist), so a re-created component replaces
   the stale one instead of competing with it. This also fixes the round-1 symptom (134 s deferral).
3. Harness-only fallback: let the STORE_A2 judge accept a **base** apply of a new-class row
   (`liveOk` OR `baseOk`) — it still proves "replayed on the peer", but weakens the per-change claim.

## R2.4 Death phases on test/b, port 47170

| phase | verdict lines | RESULT |
|---|---|---|
| case2 (1st try) | `=== LINK: FAIL (no link)` → `DCASE2 case: FAIL`, `DCASE2: FAIL` | FAIL — the pair never met because **both clients crashed at boot** with the steam `0xC0000374` (dumps 09:01:28 / 09:02:04, R2.6); re-run |
| case2 (retry, 09:10) | `=== LINK: PASS both clients in gameplay, peer seen` · `=== DCASE2 case: PASS the death was decided as case 2` · `=== DCASE2: PASS B respawned in the live world, no reset, stacks=1, max health ~90%` (maxhp 75 -> 67.5, worldresets A 2->2 B 2->2) | **PASS** |
| hold | `DHOLD: PASS B is held (case 3, pending)`, `DHOLDCAM: PASS the rendered camera follows the partner`, `DHOLD release: PASS`, `DHOLDCAM release: PASS`, `DHOLD: PASS` | **PASS** |
| wipe | `DWIPE case: PASS a party wipe was seen`, `DWIPE: PASS host reset, guest followed, both alive, 0 stacks` (A worldresets 2→4, B 2→2) | **PASS** |

The case2 first try never linked (`peer:0(none)` on both clients, relay logs show `no peer traffic for 3 s`),
so the death rule was never exercised; the identical run two minutes later (hold) linked normally, i.e. a
peer-discovery flake, not a death-logic defect.

## R2.5 PEERHP flake in run 2

```
=== PEERHP: A me:… peer:hp5.100/75.000 … age:3.9 … pup:0.068 …
=== PEERHP: B me:hp27.750/75.000 … peer:hp75.000/75.000 … age:3.5 …
=== PEERHP: PASS peer body: hp=5.100/75.000 (0.068) … puppet health set reads back 0.068
=== PEERHP: FAIL (received=False puppetMatches=False)
```

B held the freshly written 0.37 body (27.750) while A still had the previous sample (5.100 = 0.068, 3.9 s
old) when the harness read it — the `bh` sample simply arrived later than the 3 s window. The same check
passed in run 1 and in both round-1 runs; `coop_body.cpp` is untouched by round 2. Reported as a **flake**
(timing), not a regression.

## R2.6 Crash dumps (four, all the known `steam_api64` double free)

| dump | time | size | process start delta | code | bucket | dxgi frame? |
|---|---|---|---|---|---|---|
| `MortalShell2-Win64-Shipping.exe.50692.dmp` | 2026-10-26 **07:55:41** | 134 MB | 64 s | `0xC0000374` | `HEAP_CORRUPTION_ACTIONABLE_BlockNotBusy_DOUBLE_FREE_c0000374_ntdll!RtlpFreeHeapInternal` | **no** |
| `MortalShell2-Win64-Shipping.exe.26128.dmp` | **08:22:41** | 134 MB | 62 s | `0xC0000374` | same | **no** |
| `MortalShell2-Win64-Shipping.exe.50296.dmp` | **09:01:28** | 134 MB | 71 s | `0xC0000374` | same | **no** |
| `MortalShell2-Win64-Shipping.exe.3832.dmp` | **09:02:04** | 131 MB | (same session) | `0xC0000374` | same | **no** |

The first two killed the guest of the first run-two pair; the last two killed **both** clients of the first
death case2 attempt (which is why that run printed `LINK: FAIL (no link)`). All four were analysed with cdb
(`!analyze -v`) and share the stack:

```
ntdll!RtlpFreeHeapInternal → ntdll!RtlFreeHeap
  → steam_api64!SteamInternal_FindOrCreateUserInterface+0x19b570
  → steam_api64!SteamAPI_ISteamClient_GetIPCCallCount+0x76d0/0x6625
  → MortalShell2_Win64_Shipping!… (game code)
```

`dxgi.dll` appears only in the "unable to verify checksum" warning of dump 1 — never on a stack frame. So
this is the **known pre-existing `steam_api64` double free** (round-5/6 notes: 47-62 s after launch, two
copies of the game under one Steam account), not mod code and not the new build; the sessions after it
(08:33, 08:42, 09:10, 09:12, 09:14 — same DLL, same installs) booted clean. It fired 4 times in this round
against 0 in round 1's 8 sessions: timing sensitive, worth watching, but not attributable to the mod.

## R2.7 Visual runs on c/d (the reproducer + a barrel break in frame)

Two sessions, run after the death phases, both on c/d port 47160, both with `-BootWait 200`. Screenshots are
`PrintWindow(PW_RENDERFULLCONTENT)`; every set was assembled into a System.Drawing contact sheet and
**looked at** (full-resolution frames re-opened for the HUD/body check).

### A. Barrel break in frame — `break-lab.ps1 -Port 47160 -Tag r7final2-break`

`local\lab\20261026-091201-r7final2-break\` (23 PNG + `contact-sheet.png`), transcript `visual2-break.log`:
the script put both pawns in the barrel cluster, aimed each at the nearest intact breakable along its own
yaw and broke it on A through the game's own `FilteredHit` chain:

```
barrel at 45151 -131190 -41966 (d=632)
A -> 44893 -130821 -41962 (450 uu from the barrel, along A's yaw -55)
B -> 44893 -130821 -41911 (450 uu from the barrel, along B's yaw -55)
=== brk A rows:5 sent:3 recv:6 applied:3 fail:0 live:4 fall:0 pending:0 echo:0
=== brk B rows:5 sent:3 recv:8 applied:3 fail:0 live:3 fall:0 pending:0 echo:0
```

Frames: `A-before1/2` and `B-before1/2` show the **intact wooden barrel in frame on both clients**;
`A-break01..08` / `B-break01..08` show the fracture (the barrel collapsing into staves/planks with dust);
`A-after` / `B-after` show the debris field where it stood. Own HUD on both (health/resolve 3/3 bottom-left,
item slots bottom-right, currency `1462 / 4021` top-right). **No debug wireframes in any of the 23 frames.**

### B. The round-1 CINE reproducer — `visual-two.ps1 … -Tag r7final2-cinecheck`

`local\lab\20261026-091427-r7final2-cinecheck\` (27 PNG + `contact-sheet.png`), transcript
`visual2-cinecheck.log`. Commands (one run, in this order):

```
A=tp 45205 -130560 -41900 ; B=tp 45396 -131050 -41900 ; A=brkbreak nearest ;
A=cineplay ; A=cinestop ; A=selfcheck ; B=selfcheck
```

Log (the whole round-1 defect in a few lines):

```
[09:15:37][A] brkbreak: BP_VFX_Breakable_Wooden_Barrel_a_Closed_01_C_… d=635 key=8f4d91cb -> the game's own break chain
[09:15:40][A] cineplay: PlayCutscene on BP_CinematicPlayer_C_… (sequence=LevelSequence_ShellKeeperIntro_CoreWorld) -> started
[09:15:42][A] cine: RESTORED presentation (sequence stop): blueprint fns=7, unhide=2, camera=1
[09:15:42][A] cinestop: ForceStopCinematic ok=1
[09:15:45][A] selfcheck: … povToBody=226 hidden=0 meshVisible=1 meshHiddenInGame=0 viewTarget=self uiHandler=1 hud=1 hudInViewport=1 hudVisible=1
[09:15:47][B] selfcheck: … povToBody=8585 hidden=1 meshVisible=1 … viewTarget=CineCameraActor_UAID_345A6054A1DA877A02_2147468874_2147468875 uiHandler=1 hud=1 hudInViewport=1 hudVisible=1
[09:16:10][A] cine: RESTORED presentation (sequence stop): blueprint fns=7, unhide=2, camera=1
[09:16:10][B] cine: RESTORED presentation (15 Hz poll): blueprint fns=7, unhide=2, camera=1
```

**The round-1 defect is fixed:** the originator (A) is restored at the stop (`hidden=0`, `viewTarget=self`,
`povToBody=226` — round 1 measured `hidden=1`, `viewTarget=CineCameraActor`, `povToBody=4006`). B's selfcheck
5 s later still reads `hidden=1` + the cine camera **because B was still playing its own replayed sequence**
(the ShellKeeper intro is ~28 s; B's replay started at ~09:15:42), and B restores when that playback ends
(`RESTORED presentation (15 Hz poll)` at 09:16:10). That is the harness's command order, not a stuck state.

Frames (looked at):

* `A-after-cmds1..4` / `A-sees-B-*`: A's **own body** (dark shell, Hadern's sword across the shoulder) in
  third person, full **HUD** (health/resolve 3/3 bottom-left, item slots bottom-right, currency
  `1462 / 4021`), the game's subtitle `Franz (coughs)`, and the peer's puppet in frame
  (`A-after-cmds3`: the pale figure at the left, standing in the barrel debris).
* `B-after-cmds1..4`: B's screen is still the **replayed cutscene** (the cultist's face, the slab) — the
  replay had not finished yet, consistent with the 09:15:47 selfcheck.
* `B-sees-A-idle` / `B-sees-A-move1..3` (taken after B's 09:16:10 restore): B's **own body + weapon + HUD**,
  with A's puppet in frame.
* **No debug wireframes** in any of the 27 frames; the broken barrel of the `brkbreak` is in frame in these
  shots too (both pawns were teleported into that cluster, the debris is visible).

## R2.8 Log greps and dumps (all four installs, three generations each)

| pattern | `c` | `d` | `test` | `b` |
|---|---|---|---|---|
| `has no parameter` | **1 per generation that ran the CINE restore** | same | 0 | 0 |
| `ERR` (case-sensitive) | 0 | 0 | 0 | 0 |
| `MISMATCH` (case-sensitive) | 0 | 0 | 0 | 0 |
| `exception` | 0 | 0 | 0 | 0 |
| `mismatch` (case-insensitive) | 1 (`zone mismatch` of the ZONE verdict) | 1 | 0 | 0 |

The single `has no parameter` hit is **new code from the round-2 CINE fix**:

```
Params: SetActorHiddenInGame has no parameter 'bPropagateToChildren' (Set/Get ignored)
```

`AActor::SetActorHiddenInGame` takes only `bNewHidden`; the module's params layer reports the extra name and
ignores it, so the call still unhides the pawn (selfcheck `hidden=0`, and the frames show the body). The
author's "never propagate to children" intent is therefore not carried by the ACTOR call — only the mesh's
`SetVisibility(bNewVisibility, bPropagateToChildren)`, where that parameter does exist. Benign, but if the
project keeps "zero `has no parameter`" as a rule, delete that one `hide.set<bool>("bPropagateToChildren",
false);` (or move it to the mesh call).

**Crash dumps of round 2:** four — `…exe.50692.dmp` (07:55:41), `…exe.26128.dmp` (08:22:41),
`…exe.50296.dmp` (09:01:28), `…exe.3832.dmp` (09:02:04) — all `0xC0000374`
`HEAP_CORRUPTION_ACTIONABLE_BlockNotBusy_DOUBLE_FREE` with `steam_api64` frames and **no `dxgi.dll` frame**;
the full table, timings and stacks are in **R2.6**. The two 09:0x dumps are the pair behind the first case2
attempt's `LINK: FAIL (no link)` (both clients died at boot, so they never met).

## R2.9 Round-2 summary

```
build            : coop_policy: 112 checks passed, 0 failed / Built: …\dxgi.dll (sha256 4e2fd6bb…) -> PASS (exit 0)
run-two #1 (c/d): RESULT: FAIL      (STORE_A2; everything else PASS incl. DIALOG + CINE)
run-two #2 (c/d): RESULT: FAIL      (STORE_A2 + one PEERHP timing flake; DIALOG + CINE PASS)
death case2 (1st): RESULT: FAIL     (LINK: FAIL -- both clients died with the steam 0xC0000374)
death case2 (retry): RESULT: PASS
death hold       : RESULT: PASS
death wipe       : RESULT: PASS
visual break-lab : alive=2, 23 shots, barrel broken in frame on both
visual cinecheck : alive=2, 27 shots, own body + weapon + HUD back on both after the cutscene
```

* **DIALOG — fixed and verified twice.** The line comes from the wire; a cold listener (`asset not loaded
  yet: …DLG_HubCultist02_Greeting…`, exactly the round-1 failure) shows `dlg: LISTEN … line=Welcome back,
  High Sester.` and the game's subtitle, and the verdict is `DIALOG: PASS (listenLines=True)`.
* **CINE — fixed and verified.** `RESTORED presentation (…)`: blueprint fns=7, unhide=2, camera=1 on every
  end path; the originator is `hidden=0 / viewTarget=self` right after `cinestop`; the frames show both
  clients' own body + weapon + HUD afterwards, and the peer's puppet, with no wireframes.
* **STORE_A2 — still FAILS, deterministic (2/2).** The new fixreset class list runs on both clients, but
  resetting `BPSO_NPC_C` makes the save subsystem re-create the NPC's `RuntimeData`, so the map holds 2
  (guest) / 3 (host) objects for the same key; the `ws` row never settles (`ws settle reset 5f5dd95e` ×4/×5,
  `ws live skip …: row not settled here yet`), so no confirmed `liveOk` and no `result_ws2.txt`. Proposed:
  reset the *field* (`WriteBoolOn false`) instead of the save object for the boolean fixtures, or resolve the
  row through the owner actor's current `SpartaSave` component (details in R2.3.3).
* Two **flakes** (both re-run green or explained): `PEERHP` once (`age:3.9 s`, A still held the previous
  body sample) and the death `case2` first try (no link because both clients crashed with the steam bug).
* One **cosmetic defect** in the new code: `SetActorHiddenInGame` gets a nonexistent
  `bPropagateToChildren` parameter (ignored; the unhide still works).
* No other regression: every round-1 verdict that passed before still passes, and the two fixed lanes
  (DIALOG, CINE) are green in both runs.

---

## Appendix E — every verdict/RESULT line of the Round 2 runs

### run-two #1 (c/d, 47160) — transcript: run-cd-r2-attempt1.log
```
=== FIXRESET: phase A=sync B=sync after the boot wait
=== FIXRESET: A PASS fixreset: 21 save object(s) reset by guid/class (world 13, lift 0 of 0, chest 8 of 8), 1 store-B TimesUsed cleared
=== FIXRESET: B PASS fixreset: 21 save object(s) reset by guid/class (world 13, lift 0 of 0, chest 8 of 8), 1 store-B TimesUsed cleared
=== FIXRESET: PASS
=== WIRETEST: A PASS wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back byte-identical (2675 B); with one part dropped the hot block is intact and 4 of 5 part(s) arrived
=== WIRETEST: B PASS wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back byte-identical (2675 B); with one part dropped the hot block is intact and 4 of 5 part(s) arrived
=== WIRETEST: PASS
=== SHELL: A body=/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk ; B sees '/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk' ; B body=/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk ; A sees '/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk'
=== SHELL: PASS
=== WEAPON: A wpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 ; B wpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 ; A sees peerwpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 applied=[["/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01","S","Socket_Prop_R",0.00,0.00,0.00,0.00,0.00,0.00],["/Game/Sparta/Weapons/Player/Nail_Shotgun/Mesh/Nail_Shotgun_StaticMesh.Nail_Shotgun_StaticMesh","S","Socket_NailShotgun_Stowed",0.00,0.00,0.00,0.00,0.00,-90.00],["/Game/Sparta/Weapons/Player/VatraSeal/StaticMesh/SM_VatraSeal_Model.SM_VatraSeal_Model","S","Socket_Prop_Stowed_InfiniteSeal_Right",0.00,0.00,0.00,0.00,0.00,0.00]] ; B applied=[["/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01","S","Socket_Prop_R",0.00,0.00,0.00,0.00,0.00,0.00],["/Game/Sparta/Weapons/Player/Nail_Shotgun/Mesh/Nail_Shotgun_StaticMesh.Nail_Shotgun_StaticMesh","S","Socket_NailShotgun_Stowed",0.00,0.00,0.00,0.00,0.00,-90.00],["/Game/Sparta/Weapons/Player/VatraSeal/StaticMesh/SM_VatraSeal_Model.SM_VatraSeal_Model","S","Socket_Prop_Stowed_InfiniteSeal_Right",0.00,0.00,0.00,0.00,0.00,0.00]]
=== WEAPON: PASS
=== PUPVIS: A PASS puppet render: visible=1 animmode=1 animinstance=AnimSingleNodeInstance weapons=3/3 flat=0 ; B PASS puppet render: visible=1 animmode=1 animinstance=AnimSingleNodeInstance weapons=3/3 flat=0
=== PUPVIS: PASS
=== LOOK on: B look=038dd00f parts=1 (was 0) ; A peerlook=038dd00f applied=038dd00f parts=1 missing=0 loads=0
=== LOOK off: B parts=0 ; A applied=2465c27f parts=0
=== LOOK: PASS (on=True off=True)
=== ANIM: PASS puppet locomotion: sequence states idle=479 walk=20 sprint=0, moving-while-animated=20 ticks
=== DAMAGE: PASS
=== DEATH: PASS
=== DROPS: A rolled:2 calls:0 sent:2 got:1 gold:60 gloom:300 items:0 vetoed:0 dup:73 hooks:5 queued:2 pending:0 lateveto:0 cancelled:0
=== DROPS: B rolled:1 calls:0 sent:1 got:2 gold:60 gloom:300 items:0 vetoed:0 dup:162 hooks:5 queued:1 pending:0 lateveto:0 cancelled:0
=== DROPS: dir1 (A owns) A rolled [BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334] 20/100 -> B granted [BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334] 20/100
=== DROPS: dir2 (B owns) B rolled [BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134] 20/100 -> A granted [BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134] 20/100
=== DROPS: A claim: PASS enemy BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334: rolled gold=20 gloom=100 items=0, published seq 1 (from CharacterDeathData: goldOnDeath=20 chance=1.00 gloomOnDeath=100)
=== DROPS: B grant: PASS enemy BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334: granted gold=20 gloom=100 items=0 (peer rolled gold=20 gloom=100 items=0, seq 1); stacks gold -1->-1 gloom -1->-1; own roll vetoed=0 dup=0
=== DROPS: B claim: PASS enemy BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134: rolled gold=20 gloom=100 items=0, published seq 1 (from CharacterDeathData: goldOnDeath=20 chance=1.00 gloomOnDeath=100)
=== DROPS: A grant: PASS enemy BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134: granted gold=20 gloom=100 items=0 (peer rolled gold=20 gloom=100 items=0, seq 1); stacks gold -1->-1 gloom -1->-1; own roll vetoed=0 dup=0
=== DROPS: PASS both directions: each owner rolled and the other client granted the same (20/100 and 20/100), no double roll, hooks 5/5
=== WORLD: A seen:170 true:18 peer:18 live:0 base:2 fail:0 scans:286 ; B seen:172 true:14 peer:18 live:2 base:5 fail:0 scans:216
=== WORLD: PASS world state: door BP_HubDoubleDoor_C_UAID_D4E98A7EF567D94802_83a1dc45e2a01438_1808979936_1808979937 opened, BP_CraftingItemPickup_C_UAID_D4E98A7EF567518202_83a1dc45e2a01438_1478318884_1478318885 collected (live=2 base=5 fail=0)
=== PROGRESS: A b:7/7+0!0 q:15/8+0!0 d:104/0+0!0 ; B b:7/7+1!0 q:15/8+2!0 d:104/104+2!0
=== PROGRESS: PASS world progression: i|BP_Interactable_Chest_General_C_UAID_D4E98A7EF5673FD002_83a1dc45e2a01438_2008351691_2008351692|TimesUsed, tag t|Prologue.FlowerCrownRecieved removed+restored, layer DL_AU loaded+reactivated (wb=1/0 wq=2/0 wd=2/0)
=== QUEST: PASS quest LocationQuest added then completed (active now=false, wq=6/0)
=== QUEST2: PASS quest state GorfQuest: count heard 1 objective(s), stage s=3, tracked, rel r|Gorf=2, turned in (wq=13/0)
=== LIFT: A n:1 seq:1 peer:1 applied:0 fail:0 ; B n:1 seq:0 peer:1 applied:1 fail:0
=== LIFT: PASS lift BP_HubLift_C_UAID_D4E98A7EF5672F1B02_83a1dc45e2a01438_1653632767_1653632768 followed the peer to floor 1 (cabin z -42570 -> -40133, travel 2437, applied 1, fail 0)
=== CHEST: A n:8 seq:1 events:1 applied:0 loot:0 queue:0 fail:0 ; B n:8 seq:0 events:0 applied:1 loot:1 queue:0 fail:0
=== CHEST: PASS chest BP_Interactable_Chest_General_C_UAID_D4E98A7EF56741D002_83a1dc45e2a01438_1737023242_1737023243: lid replayed (anim pos 5.07), loot ID_Charges_Default.ID_Charges_Default_C 1 -> 2 (applied 1, loot 1, fail 0)
=== STORE_A2: A kinds:10 typed:3 scans:566 recv:3767 applied:1 str:0 fail:0 leaks:0 ; B kinds:10 typed:3 scans:525 recv:3928 applied:3 str:0 fail:0 leaks:0
=== STORE_A2: 
=== STORE_A2SELFTEST: ws2 self test: 20 pass, 0 fail
=== STORE_A2CENSUS: 23 census rows (ws2_census.txt)
=== SPAWN: PASS enemy spawned by the peer appeared here too: SP_BP_Spawner_Proximity_C_UAID_D4E98A7EF567C67402_1440305372_1440305373 -> BP_Horde_Brigand_C_2147452231_2147452232 (A driven:0 taken:0 released:0 montages:0 spawnask:0 spawnok:0 retarget:0 pdsent:0.000 pdapplied:0)
=== MIRROR: PASS host copy driven by the guest (AI paused, owner motion/facing/montage) (A driven:3 taken:3 released:0 montages:0 spawnask:3 spawnok:3 retarget:0 pdsent:0.000 pdapplied:0 ; B driven:0 taken:2 released:2 montages:0 spawnask:0 spawnok:0 retarget:0 pdsent:0.000 pdapplied:0)
=== PEERHIT: PASS peer-side hit applied to own body: 0.480 -> 0.380
=== AITARGET: PASS enemy BP_NPC_ShellKeeper_C_UAID_D4E98A7EF56747A302_1167224404_1167224405 targets the peer's body after SetTargetOverride (current BP_PlayerCharacter_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01.PersistentLevel.BP_PlayerCharacter_C_2147469565_2147469566)
=== STREAM: A src:blk=false/nb=true n0:89 n1:20 npcown:1 npcmirror:341 ; B src:blk=false/nb=true n0:101 n1:91 npcown:0 npcmirror:6405
=== STREAM: PASS remote area streamed around peer 91461 uu away after a long travel: cells around it loaded=true, static meshes 89 -> 20 (sources blk=false/nb=true); enemies there: own 1 mirrored 341
=== NPCFAR: PASS host hands BP_NPC_ShellKeeper_C_UAID_D4E98A7EF56747A302_1167224404_1167224405 to the far guest and moves its copy there
=== PERF: host working set 7344 MB -> 8014 MB with the far area loaded; A near:67.29ms(613) far:60.23ms(148) ; B near:22.57ms(566) far:20.93ms(149)
=== LEDGER: A ok keys:18 good:14 bad:5 applied:5 ; B ok keys:18 good:0 bad:0 applied:0
=== LEDGER: A PASS ledger: BPSO_NPC_C.HasTriggeredInitialGreeting (BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134) re-applied from the ledger on load (ledger 18 keys, applied 4) ; B -
=== BREAK: A rows:78 sent:1 recv:0 applied:0 fail:0 live:1 fall:0 pending:0 echo:0
=== BREAK: B rows:141 sent:1 recv:4 applied:1 fail:0 live:1 fall:0 pending:0 echo:0
=== BREAK: PASS peer break 75e02c2e BP_VFX_Breakable_Cart_02_A_C BP_VFX_Breakable_Cart_02_A_C_UAID_345A60361B8776BE02_83a1dc45e2a01438_2105289101_2105289102 (FilteredHit)
=== BREAK: PASS
=== WIRE: A txp:7 txmax:1148 txb:5665 hot:948 rxp:6 rxmax:1148 rxb:4357 lost:0 v:2
=== WIRE: B txp:7 txmax:1148 txb:5504 hot:947 rxp:7 rxmax:1148 rxb:5665 lost:0 v:2
=== WIRE: relay [08:40:57] wire: tx sent=8554 recv=0 max=1149 parts=1367 dropped=0 parts_rx=7791 parts_lost=0 legacy=20 oversize=0 torn=0 loss=0 | [08:40:55] wire: tx sent=7753 recv=0 max=1150 parts=1458 dropped=0 parts_rx=8470 parts_lost=0 legacy=28 oversize=0 torn=0 loss=0 (sent=16307 parts_lost=0)
=== WIRE: PASS (packet split into 7/7 part(s), largest datagram 1150 B <= 1200, reassembled 6/7, parts lost 0)
=== ZONE: before 'me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5429 sk:0 guard:1'
=== ZONE: mismatch=True (applies 5442 -> 5442 and skips 2 -> 62 while paused) resumed=True (applies -> 5494)
=== ZONE: A me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5494 sk:78 guard:1
=== ZONE: B me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5438 sk:75 guard:1
=== ZONE: PASS zone guard: /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 vs /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 -- world apply paused 5.2s (78 applies skipped), resumed at /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 (applies 5442 -> 5449)
=== ZONE: PASS
=== DIALOG: realConversation=False (dlgstart content-gated in this save; see REPORT)
=== DIALOG: A me:0 peer:1 kind:1 name:DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting node:0 opts:0 seq:1 idle:8 pd:8 ai:8 ep:1 mine:0 listen:1 lines:2 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOG: B me:0 peer:0 kind:0 name:- node:0 opts:0 seq:0 idle:0 pd:0 ai:0 ep:0 mine:1 listen:0 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOG: PASS dialogue: peer 'DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting' held 1.8s ; puppet idle samples=5, replayed lines=2, damage charges suppressed=5, votes sent=0, resolutions seen=0
=== DIALOGVOTE: A PASS dialogue votes: 7 checks, 0 failed ; B PASS dialogue votes: 7 checks, 0 failed
=== DIALOG: PASS (peerBusySeen=True realConversation=False idlePuppet=True listenLines=True pdNotCharged=True [0.1 -> 0.1] votes=True lockEnded=True)
=== CINE: A me:0 peer:1 name:LevelSequence_ShellKeeperIntro_CoreWorld ep:2 replay:2/1/1 on:1
=== CINE: B me:0 peer:0 name:- ep:1 replay:1/0/1 on:1
=== CINE: PASS cinematic lock: peer 'LevelSequence_ShellKeeperIntro_CoreWorld' held 1.6s ; puppet idle samples=4, damage charges suppressed=4, replays started=1 (jumps=0, stops=0)
=== CINE: PASS (aPlaying=True bReplays=False replayCounter=True idlePuppet=True pdNotCharged=True [0 -> 0] endedBoth=True)
=== PEERHP: A me:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:0.4 pup:0.380 pdf:0 ok:1
=== PEERHP: B me:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:0.1 pup:1.000 pdf:0 ok:1
=== PEERHP: PASS peer body: hp=28.500/75.000 (0.380) shp=0.000/0.000 res=100.000/100.000 heal=3/3 pp=1.000/1.000 ammo=0/0 dark=0 ds=0 ; puppet health set reads back 0.380
=== PEERHP: PASS (received=True puppetMatches=True)
=== PEERDEAD: A me:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds1 age:0.0 pup:0.380 pdf:1 ok:1
=== PEERDEAD: B me:hp28.500/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds1 peer:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:0.0 pup:1.000 pdf:0 ok:1
=== PEERDEAD: PASS peer death: ds=1 ; the puppet plays /Game/MS1/Characters/DeathsLibrary/death_093_front.death_093_front (held 0.4s on the mesh)
=== PEERDEAD: PASS (clipOnPuppet=True)
=== BOSSPHASE: A before 'n:0 phase:- scanned:397 hook:0 pub:0 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0'
=== BOSSPHASE: spawn h=True g=True ; A n:1 phase:BP_PlayerCharacter_C_2147479948_2147479949:2 scanned:402 hook:1 pub:1 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0
=== BOSSPHASE: B n:1 phase:BP_PlayerCharacter_C_2147479948_2147479949:2 scanned:386 hook:1 pub:0 applied:1 seen:1 fail:0 missed:0 echoes:0 peerkeys:1 off:0
=== BOSSPHASE: PASS (owner phase 2 + peer applied>=1)
=== DIALOGREAL: A me:0 peer:0 kind:0 name:- node:0 opts:0 seq:5 idle:25 pd:17 ai:17 ep:2 mine:1 listen:1 lines:3 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOGREAL: B me:0 peer:0 kind:0 name:- node:0 opts:0 seq:3 idle:8 pd:8 ai:8 ep:1 mine:2 listen:0 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOGCODEC: PASS dialog wire codec: 21 checks, 0 failed
=== NPC: 
=== A: PASS puppet tracks peer (dist 0.4, 30/30 good; moves 12, snaps 0)
=== B: PASS puppet tracks peer (dist 0.4, 30/30 good; moves 12, snaps 0)
--- E:\Games\_MS2-coop-c\MortalShell2\Binaries\Win64\coop ---
--- E:\Games\_MS2-coop-d\MortalShell2\Binaries\Win64\coop ---
--- E:\Games\_MS2-coop-c\MortalShell2\Binaries\Win64\ms2coop-native.log ---
=== NATIVE: E:\Games\_MS2-coop-c -> READY ; coop started
--- E:\Games\_MS2-coop-d\MortalShell2\Binaries\Win64\ms2coop-native.log ---
=== NATIVE: E:\Games\_MS2-coop-d -> READY ; coop started
RESULT: FAIL
```

### run-two #2 (c/d, 47160) — transcript: run-cd-r2-attempt2.log
```
=== FIXRESET: phase A=sync B=sync after the boot wait
=== FIXRESET: A PASS fixreset: 21 save object(s) reset by guid/class (world 13, lift 0 of 0, chest 8 of 8), 1 store-B TimesUsed cleared
=== FIXRESET: B PASS fixreset: 21 save object(s) reset by guid/class (world 13, lift 0 of 0, chest 8 of 8), 1 store-B TimesUsed cleared
=== FIXRESET: PASS
=== WIRETEST: A PASS wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back byte-identical (2675 B); with one part dropped the hot block is intact and 4 of 5 part(s) arrived
=== WIRETEST: B PASS wire: packet 2675 B -> 5 part(s), max 1148 B (budget 1150); merged back byte-identical (2675 B); with one part dropped the hot block is intact and 4 of 5 part(s) arrived
=== WIRETEST: PASS
=== SHELL: A body=/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk ; B sees '/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk' ; B body=/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk ; A sees '/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk'
=== SHELL: PASS
=== WEAPON: A wpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 ; B wpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 ; A sees peerwpn=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01 applied=[["/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01","S","Socket_Prop_R",0.00,0.00,0.00,0.00,0.00,0.00],["/Game/Sparta/Weapons/Player/Nail_Shotgun/Mesh/Nail_Shotgun_StaticMesh.Nail_Shotgun_StaticMesh","S","Socket_NailShotgun_Stowed",0.00,0.00,0.00,0.00,0.00,-90.00],["/Game/Sparta/Weapons/Player/VatraSeal/StaticMesh/SM_VatraSeal_Model.SM_VatraSeal_Model","S","Socket_Prop_Stowed_InfiniteSeal_Right",0.00,0.00,0.00,0.00,0.00,0.00]] ; B applied=[["/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01","S","Socket_Prop_R",0.00,0.00,0.00,0.00,0.00,0.00],["/Game/Sparta/Weapons/Player/Nail_Shotgun/Mesh/Nail_Shotgun_StaticMesh.Nail_Shotgun_StaticMesh","S","Socket_NailShotgun_Stowed",0.00,0.00,0.00,0.00,0.00,-90.00],["/Game/Sparta/Weapons/Player/VatraSeal/StaticMesh/SM_VatraSeal_Model.SM_VatraSeal_Model","S","Socket_Prop_Stowed_InfiniteSeal_Right",0.00,0.00,0.00,0.00,0.00,0.00]]
=== WEAPON: PASS
=== PUPVIS: A PASS puppet render: visible=1 animmode=1 animinstance=AnimSingleNodeInstance weapons=3/3 flat=0 ; B PASS puppet render: visible=1 animmode=1 animinstance=AnimSingleNodeInstance weapons=3/3 flat=0
=== PUPVIS: PASS
=== LOOK on: B look=038dd00f parts=1 (was 0) ; A peerlook=038dd00f applied=038dd00f parts=1 missing=0 loads=0
=== LOOK off: B parts=0 ; A applied=2465c27f parts=0
=== LOOK: PASS (on=True off=True)
=== ANIM: PASS puppet locomotion: sequence states idle=510 walk=20 sprint=0, moving-while-animated=20 ticks
=== DAMAGE: PASS
=== DEATH: PASS
=== DROPS: A rolled:2 calls:0 sent:2 got:1 gold:60 gloom:300 items:0 vetoed:0 dup:82 hooks:5 queued:2 pending:0 lateveto:0 cancelled:0
=== DROPS: B rolled:1 calls:0 sent:1 got:2 gold:60 gloom:300 items:0 vetoed:0 dup:154 hooks:5 queued:1 pending:0 lateveto:0 cancelled:0
=== DROPS: dir1 (A owns) A rolled [BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334] 20/100 -> B granted [BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334] 20/100
=== DROPS: dir2 (B owns) B rolled [BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134] 20/100 -> A granted [BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134] 20/100
=== DROPS: A claim: PASS enemy BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334: rolled gold=20 gloom=100 items=0, published seq 1 (from CharacterDeathData: goldOnDeath=20 chance=1.00 gloomOnDeath=100)
=== DROPS: B grant: PASS enemy BP_Cultist_HUB_C_UAID_D8BBC1D05326704902_83a1dc45e2a01438_1417419333_1417419334: granted gold=20 gloom=100 items=0 (peer rolled gold=20 gloom=100 items=0, seq 1); stacks gold -1->-1 gloom -1->-1; own roll vetoed=0 dup=0
=== DROPS: B claim: PASS enemy BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134: rolled gold=20 gloom=100 items=0, published seq 1 (from CharacterDeathData: goldOnDeath=20 chance=1.00 gloomOnDeath=100)
=== DROPS: A grant: PASS enemy BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134: granted gold=20 gloom=100 items=0 (peer rolled gold=20 gloom=100 items=0, seq 1); stacks gold -1->-1 gloom -1->-1; own roll vetoed=0 dup=0
=== DROPS: PASS both directions: each owner rolled and the other client granted the same (20/100 and 20/100), no double roll, hooks 5/5
=== WORLD: A seen:170 true:18 peer:18 live:0 base:1 fail:0 scans:252 ; B seen:170 true:18 peer:18 live:2 base:5 fail:0 scans:222
=== WORLD: PASS world state: door BP_HubDoubleDoor_C_UAID_D4E98A7EF567D94802_83a1dc45e2a01438_1808979936_1808979937 opened, BP_CraftingItemPickup_C_UAID_D4E98A7EF567518202_83a1dc45e2a01438_1478318884_1478318885 collected (live=2 base=5 fail=0)
=== PROGRESS: A b:7/7+0!0 q:15/8+0!0 d:104/0+0!0 ; B b:7/7+1!0 q:15/8+2!0 d:104/104+2!0
=== PROGRESS: PASS world progression: i|BP_Interactable_Chest_General_C_UAID_D4E98A7EF5673FD002_83a1dc45e2a01438_2008351691_2008351692|TimesUsed, tag t|Prologue.FlowerCrownRecieved removed+restored, layer DL_AU loaded+reactivated (wb=1/0 wq=2/0 wd=2/0)
=== QUEST: PASS quest LocationQuest added then completed (active now=false, wq=6/0)
=== QUEST2: PASS quest state GorfQuest: count heard 1 objective(s), stage s=3, tracked, rel r|Gorf=2, turned in (wq=13/0)
=== LIFT: A n:1 seq:1 peer:1 applied:0 fail:0 ; B n:1 seq:0 peer:1 applied:1 fail:0
=== LIFT: PASS lift BP_HubLift_C_UAID_D4E98A7EF5672F1B02_83a1dc45e2a01438_1653632767_1653632768 followed the peer to floor 1 (cabin z -42570 -> -40118, travel 2452, applied 1, fail 0)
=== CHEST: A n:8 seq:1 events:1 applied:0 loot:0 queue:0 fail:0 ; B n:8 seq:0 events:0 applied:1 loot:1 queue:0 fail:0
=== CHEST: PASS chest BP_Interactable_Chest_General_C_UAID_D4E98A7EF56741D002_83a1dc45e2a01438_1737023242_1737023243: lid replayed (anim pos 5.07), loot ID_Charges_Default.ID_Charges_Default_C 1 -> 2 (applied 1, loot 1, fail 0)
=== STORE_A2: A kinds:10 typed:3 scans:548 recv:3711 applied:1 str:0 fail:0 leaks:0 ; B kinds:10 typed:3 scans:527 recv:3569 applied:1 str:0 fail:0 leaks:0
=== STORE_A2: 
=== STORE_A2SELFTEST: ws2 self test: 20 pass, 0 fail
=== STORE_A2CENSUS: 23 census rows (ws2_census.txt)
=== SPAWN: PASS enemy spawned by the peer appeared here too: SP_BP_Spawner_Proximity_C_UAID_D4E98A7EF5671DF601_1555889259_1555889260 -> BP_Horde_Brigand_C_2147452620_2147452621 (A driven:0 taken:0 released:0 montages:0 spawnask:1 spawnok:0 retarget:0 pdsent:0.000 pdapplied:0)
=== MIRROR: PASS host copy driven by the guest (AI paused, owner motion/facing/montage) (A driven:1 taken:1 released:0 montages:0 spawnask:4 spawnok:1 retarget:0 pdsent:0.000 pdapplied:0 ; B driven:0 taken:2 released:2 montages:0 spawnask:0 spawnok:0 retarget:0 pdsent:0.000 pdapplied:0)
=== PEERHIT: PASS peer-side hit applied to own body: 0.480 -> 0.380
=== AITARGET: PASS enemy BP_NPC_ShellKeeper_C_UAID_D4E98A7EF56747A302_1167224404_1167224405 targets the peer's body after SetTargetOverride (current BP_PlayerCharacter_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01.PersistentLevel.BP_PlayerCharacter_C_2147479407_2147479408)
=== STREAM: A src:blk=false/nb=true n0:74 n1:18 npcown:1 npcmirror:511 ; B src:blk=false/nb=true n0:101 n1:91 npcown:0 npcmirror:6489
=== STREAM: PASS remote area streamed around peer 91462 uu away after a long travel: cells around it loaded=true, static meshes 74 -> 18 (sources blk=false/nb=true); enemies there: own 1 mirrored 511
=== NPCFAR: PASS host hands BP_NPC_ShellKeeper_C_UAID_D4E98A7EF56747A302_1167224404_1167224405 to the far guest and moves its copy there
=== PERF: host working set 7299 MB -> 7892 MB with the far area loaded; A near:65.40ms(600) far:133.84ms(147) ; B near:21.92ms(569) far:19.42ms(149)
=== LEDGER: A ok keys:18 good:9 bad:5 applied:5 ; B ok keys:18 good:0 bad:0 applied:0
=== LEDGER: A PASS ledger: BPSO_NPC_C.HasTriggeredInitialGreeting (BP_Cultist_HUB_OvumCaretaker_C_UAID_D8BBC1D05326084B02_83a1dc45e2a01438_1739522133_1739522134) re-applied from the ledger on load (ledger 18 keys, applied 4) ; B -
=== BREAK: A rows:80 sent:1 recv:0 applied:0 fail:0 live:1 fall:0 pending:0 echo:0
=== BREAK: B rows:138 sent:1 recv:1 applied:1 fail:0 live:1 fall:0 pending:0 echo:0
=== BREAK: PASS peer break 75e02c2e BP_VFX_Breakable_Cart_02_A_C BP_VFX_Breakable_Cart_02_A_C_UAID_345A60361B8776BE02_83a1dc45e2a01438_2105289101_2105289102 (FilteredHit)
=== BREAK: PASS
=== WIRE: A txp:6 txmax:1146 txb:4626 hot:948 rxp:6 rxmax:1148 rxb:4928 lost:0 v:2
=== WIRE: B txp:6 txmax:1148 txb:4928 hot:947 rxp:6 rxmax:1146 rxb:4626 lost:0 v:2
=== WIRE: relay [08:49:17] wire: tx sent=8465 recv=0 max=1149 parts=1359 dropped=0 parts_rx=7726 parts_lost=0 legacy=25 oversize=0 torn=0 loss=0 | [08:49:19] wire: tx sent=7789 recv=0 max=1150 parts=1453 dropped=0 parts_rx=8479 parts_lost=0 legacy=26 oversize=0 torn=0 loss=0 (sent=16254 parts_lost=0)
=== WIRE: PASS (packet split into 6/6 part(s), largest datagram 1150 B <= 1200, reassembled 6/6, parts lost 0)
=== ZONE: before 'me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5461 sk:0 guard:1'
=== ZONE: mismatch=True (applies 5474 -> 5474 and skips 10 -> 70 while paused) resumed=True (applies -> 5528)
=== ZONE: A me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5528 sk:83 guard:1
=== ZONE: B me:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| peer:/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01|| same:1 li:same known:1 paused:0 ap:5460 sk:83 guard:1
=== ZONE: PASS zone guard: /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 vs /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 -- world apply paused 5.5s (83 applies skipped), resumed at /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 (applies 5474 -> 5483)
=== ZONE: PASS
=== DIALOG: realConversation=False (dlgstart content-gated in this save; see REPORT)
=== DIALOG: A me:0 peer:1 kind:1 name:DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting node:0 opts:0 seq:1 idle:8 pd:8 ai:8 ep:1 mine:0 listen:1 lines:2 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOG: B me:0 peer:0 kind:0 name:- node:0 opts:0 seq:0 idle:0 pd:0 ai:0 ep:0 mine:1 listen:0 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOG: PASS dialogue: peer 'DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting' held 1.7s ; puppet idle samples=5, replayed lines=1, damage charges suppressed=5, votes sent=0, resolutions seen=0
=== DIALOGVOTE: A PASS dialogue votes: 7 checks, 0 failed ; B PASS dialogue votes: 7 checks, 0 failed
=== DIALOG: PASS (peerBusySeen=True realConversation=False idlePuppet=True listenLines=True pdNotCharged=True [0.1 -> 0.1] votes=True lockEnded=True)
=== CINE: A me:0 peer:1 name:LevelSequence_ShellKeeperIntro_CoreWorld ep:2 replay:2/1/1 on:1
=== CINE: B me:0 peer:0 name:- ep:1 replay:1/0/1 on:1
=== CINE: PASS cinematic lock: peer 'LevelSequence_ShellKeeperIntro_CoreWorld' held 2.0s ; puppet idle samples=5, damage charges suppressed=5, replays started=1 (jumps=0, stops=0)
=== CINE: PASS (aPlaying=True bReplays=False replayCounter=True idlePuppet=True pdNotCharged=True [0 -> 0] endedBoth=True)
=== PEERHP: A me:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp5.100/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:3.9 pup:0.068 pdf:0 ok:1
=== PEERHP: B me:hp27.750/75.000 shp0.000/0.000 res50.000/100.000 heal1/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:3.5 pup:1.000 pdf:0 ok:1
=== PEERHP: PASS peer body: hp=5.100/75.000 (0.068) shp=0.000/0.000 res=100.000/100.000 heal=3/3 pp=1.000/1.000 ammo=0/0 dark=0 ds=0 ; puppet health set reads back 0.068
=== PEERHP: FAIL (received=False puppetMatches=False)
=== PEERDEAD: A me:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 peer:hp27.750/75.000 shp0.000/0.000 res50.000/100.000 heal1/3 pp1.000/1.000 ammo0/0 dark0 ds1 age:0.0 pup:0.370 pdf:1 ok:1
=== PEERDEAD: B me:hp27.750/75.000 shp0.000/0.000 res50.000/100.000 heal1/3 pp1.000/1.000 ammo0/0 dark0 ds1 peer:hp75.000/75.000 shp0.000/0.000 res100.000/100.000 heal3/3 pp1.000/1.000 ammo0/0 dark0 ds0 age:0.0 pup:1.000 pdf:0 ok:1
=== PEERDEAD: PASS peer death: ds=1 ; the puppet plays /Game/MS1/Characters/DeathsLibrary/death_093_front.death_093_front (held 0.5s on the mesh)
=== PEERDEAD: PASS (clipOnPuppet=True)
=== BOSSPHASE: A before 'n:0 phase:- scanned:391 hook:0 pub:0 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0'
=== BOSSPHASE: spawn h=True g=True ; A n:1 phase:BP_PlayerCharacter_C_2147479948_2147479949:2 scanned:396 hook:1 pub:1 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0
=== BOSSPHASE: B n:1 phase:BP_PlayerCharacter_C_2147479948_2147479949:2 scanned:390 hook:1 pub:0 applied:1 seen:1 fail:0 missed:0 echoes:0 peerkeys:1 off:0
=== BOSSPHASE: PASS (owner phase 2 + peer applied>=1)
=== DIALOGREAL: A me:0 peer:0 kind:0 name:- node:0 opts:0 seq:4 idle:29 pd:19 ai:19 ep:2 mine:1 listen:1 lines:3 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOGREAL: B me:0 peer:0 kind:0 name:- node:0 opts:0 seq:3 idle:10 pd:10 ai:10 ep:1 mine:2 listen:0 lines:0 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOGCODEC: PASS dialog wire codec: 21 checks, 0 failed
=== NPC: 
=== A: PASS puppet tracks peer (dist 0.4, 30/30 good; moves 13, snaps 0)
=== B: PASS puppet tracks peer (dist 0.4, 30/30 good; moves 13, snaps 0)
--- E:\Games\_MS2-coop-c\MortalShell2\Binaries\Win64\coop ---
--- E:\Games\_MS2-coop-d\MortalShell2\Binaries\Win64\coop ---
--- E:\Games\_MS2-coop-c\MortalShell2\Binaries\Win64\ms2coop-native.log ---
=== NATIVE: E:\Games\_MS2-coop-c -> READY ; coop started
--- E:\Games\_MS2-coop-d\MortalShell2\Binaries\Win64\ms2coop-native.log ---
=== NATIVE: E:\Games\_MS2-coop-d -> READY ; coop started
RESULT: FAIL
```

### death case2, first try (test/b, 47170) — transcript: death-case2.log
```
=== LINK: FAIL (no link)
--- A death: me:1(alive) case:0 victim:0 serial:0 stacks:0 peer:0(none) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:1 rules:[5,10,10,10,5,1,1]
--- A       0 deathmix=0 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:2 maxhp=
--- B death: me:1(alive) case:0 victim:0 serial:0 stacks:0 peer:0(none) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1]
--- B       0 deathmix=0 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:2 maxhp=
=== DCASE2 baseline: B maxhp=75 worldresets A=2 B=2
=== DCASE2 case: FAIL (me:1(alive) case:0 victim:0 serial:0 stacks:0 peer:0(none) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1])
=== DCASE2: B me:1(alive) case:0 victim:0 serial:0 stacks:0 peer:0(none) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1] maxhp 75 -> 75 (ratio 1) worldresets A 2->2 B 2->2
=== DCASE2: FAIL
--- E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
--- E:\Games\_MS2-coop-b\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
RESULT: FAIL
```

### death case2, retry (test/b, 47170) — transcript: death-case2-retry.log
```
=== LINK: PASS both clients in gameplay, peer seen
--- A death: me:1(alive) case:0 victim:0 serial:0 stacks:0 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:1 rules:[5,10,10,10,5,1,1]
--- A       0 deathmix=0 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:2 maxhp=
--- B death: me:1(alive) case:0 victim:0 serial:0 stacks:0 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1]
--- B       0 deathmix=0 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:2 maxhp=
=== DCASE2 baseline: B maxhp=75 worldresets A=2 B=2
=== DCASE2 case: PASS the death was decided as case 2
=== DCASE2: B me:1(alive) case:2 victim:2 serial:1 stacks:1 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1] maxhp 75 -> 67.5 (ratio 0.9) worldresets A 2->2 B 2->2
=== DCASE2: PASS B respawned in the live world, no reset, stacks=1, max health ~90%
--- E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
--- E:\Games\_MS2-coop-b\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
RESULT: PASS
```

### death hold (test/b, 47170) — transcript: death-hold.log
```
=== LINK: PASS both clients in gameplay, peer seen
--- B(hold) death: me:3(down) case:3 victim:3 serial:1 stacks:0 peer:1(seen) boss:1 pending:1 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1]
--- B(hold)       0 deathmix=1 maxhp=75.0 maxshell=0.0 viewpuppet=1 spectating:1 missrevive:0 hooks:2 maxhp=
=== DHOLD: PASS B is held (case 3, pending)
=== DHOLDCAM: PASS the rendered camera follows the partner
--- B(after hold) death: me:1(alive) case:2 victim:3 serial:1 stacks:1 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1]
--- B(after hold)       0 deathmix=0 maxhp=67.5 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:4 maxhp=
=== DHOLD release: PASS B respawned once the fight was over
=== DHOLDCAM release: PASS the camera is back on B
=== DHOLD: PASS
--- E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
--- E:\Games\_MS2-coop-b\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
RESULT: PASS
```

### death wipe (test/b, 47170) — transcript: death-wipe.log
```
=== LINK: PASS both clients in gameplay, peer seen
=== DWIPE baseline: worldresets A=2 B=2 stacks B=0
--- A(wipe) death: me:4(down) case:1 victim:2 serial:1 stacks:0 peer:4(seen) boss:0 pending:1 suppressed:0 worldresets:2 wr:0 peerwr:0 host:1 rules:[5,10,10,10,5,1,1]
--- A(wipe)       0 deathmix=1 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:4 maxhp=
--- B(wipe) death: me:2(down) case:2 victim:2 serial:1 stacks:1 peer:1(seen) boss:0 pending:1 suppressed:0 worldresets:2 wr:0 peerwr:0 host:0 rules:[5,10,10,10,5,1,1]
--- B(wipe)       0 deathmix=1 maxhp=67.5 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:2 maxhp=
=== DWIPE case: PASS a party wipe was seen
--- A(after wipe) death: me:1(alive) case:0 victim:2 serial:1 stacks:0 peer:1(seen) boss:0 pending:0 suppressed:0 worldresets:4 wr:1 peerwr:0 host:1 rules:[5,10,10,10,5,1,1]
--- A(after wipe)       0 deathmix=0 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:4 maxhp=
--- B(after wipe) death: me:1(alive) case:1 victim:2 serial:1 stacks:0 peer:1(seen) boss:0 pending:0 suppressed:1 worldresets:2 wr:0 peerwr:1 host:0 rules:[5,10,10,10,5,1,1]
--- B(after wipe)       0 deathmix=0 maxhp=75.0 maxshell=0.0 viewpuppet=0 spectating:0 missrevive:0 hooks:4 maxhp=
=== DWIPE: host reset A worldresets 2->4 ; guest did NOT reset its own (B 2->2)
=== DWIPE: both alive=True stacks A=0 B=0
=== DWIPE: PASS host reset, guest followed, both alive, 0 stacks
--- E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
--- E:\Games\_MS2-coop-b\MortalShell2\Binaries\Win64\coop probe.log (death lines) ---
RESULT: PASS
```


