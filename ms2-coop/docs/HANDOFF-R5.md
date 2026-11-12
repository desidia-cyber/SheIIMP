# HANDOFF — Round 5 (2026-10-25), for the next session

Read this file first, then `C:\Users\ALVIBO\.claude\projects\G--Freespace-shell-coop\memory\ms2-native-port.md`.
Project: Mortal Shell II co-op mod, **native dxgi.dll proxy** (no UE4SS), source `ms2-coop\native\`.
Build `native\build.bat` → `native\build\dxgi.dll`; tests `native\build.bat test` (test_api + 112 death-policy
checks + AOB self-check — all PASS at hand-off). Last built DLL sha256 `0501C108…85E3` (NOT yet run in game: see §6).

## 0. Owner's standing rules (keep them)
- Never touch `E:\Games\Mortal Shell II` (owner's real install). No git repo; never commit.
- Back up before changing anything (small files + saves; NOT whole game folders — too large).
- Delegate bulk work to cheap subagents: `delegate_task`, provider `acpRegistry_deepseek`, model
  `["goat3","deepseek/deepseek-v4.1-flash"]`, `reasoning_effort: high`. Main agent reviews/decides. Time matters.
- Inventory, gold and levels are **per player** (never sync currency counts / progression).
- Death logic must stay close to DollsMP (`G:\Freespace\dollsMP\mod\native\src\coop_policy.h`) — see §3.
- Owner asked: main agent and subagents must NOT share DLL deploy targets (see §1).
- After the round-5 task list the owner said: "After that stop for now and ask me what to do next." —
  **the next session must finish §6, report, then ASK the owner what to do next.**

## 1. Test installs, lock, harness
| Install | Path | Who uses it |
|---|---|---|
| A (host) / B (guest) | `E:\Games\_MS2-coop-test`, `E:\Games\_MS2-coop-b` | subagents |
| C (host) / D (guest) | `E:\Games\_MS2-coop-c`, `E:\Games\_MS2-coop-d` | main agent. Hardlink mirrors of A (227 links, ~0 extra disk); only `Binaries\Win64\dxgi.dll`, `coop\`, logs, `ms2coop-native.ini` are real files |
- **Run lock (shared, still required)**: `. G:\Freespace\shell_coop\local\gamelock.ps1; Lock-Game '<name>'` …
  `Unlock-Game '<name>'`. All scripts kill every MortalShell2 process and the save folder
  (`%LOCALAPPDATA%\MortalShell2\Saved\SaveGames`, snapshot/restored by the scripts) is shared, so only one run at a time.
- Deploy: `ms2-coop\deploy.ps1 -GameDir <inst> -DllPath ms2-coop\native\build\dxgi.dll`. Never while a game runs.
- Before WORLD tests: delete `<inst>\MortalShell2\Binaries\Win64\coop\ws_ledger_*.txt`.
- Harness (all in `ms2-coop\`):
  - `run-two.ps1 -GameA E:\Games\_MS2-coop-c -GameB E:\Games\_MS2-coop-d -Port 47020` — full verdict run.
    Now also prints ZONE / PEERHP / PEERDEAD / STORE_A2 / **PUPVIS** (new, renders-based puppet check).
  - `run-two-death.ps1 -GameA … -GameB … -Port 47020 -Phase case2|hold|wipe` — death verdicts, ONE phase per
    session (`S_KillMe` only kills once per session). Prints DCASE2 / DHOLD / DHOLDCAM(warn) / DWIPE.
  - `visual-two.ps1 [-Tag x] [-Cmds 'B=bossfake 1;B=die'] [-BootWait 200]` — defaults to C/D, port 47020;
    takes SCREENSHOTS (A sees B from 8 directions, B sees A), runs `pupdump` at the end, copies probe/sync logs
    to `G:\Freespace\shell_coop\local\lab\<stamp>-<tag>\`. Build contact sheets with System.Drawing to look at them.
  - `lab.ps1 -GameDir <inst> -Commands "verb args@sec,..."` — one client.
- Use `-BootWait 200` with visual-two (boot to `phase=sync` is ~60–90 s).

## 2. What round 5 delivered (all merged into the main tree `ms2-coop\native\`)
Briefs/reports: `native\briefs\R5-COMMON.md`, `R5-BODY.md`, `R5-DEATH.md`, `R5-WORLD.md`, `R5-DEATH-FINDINGS.md`
(measured death flow — read it), `R5-WORLD-report.md`, `R5-EQUIP-PROBE.md`, `R5-*-runtwo-snippet.ps1`.
Agent workspaces (frozen, for reference only): `G:\Freespace\shell_coop\wt\{body,death,world}\ms2-coop\`.

| Feature | Files | Wire keys / verbs | Verified |
|---|---|---|---|
| Zone id + mismatch guard (G9 v1) | `coop_body.cpp` (zone part), guard in `coop_net.cpp` ApplyPeer `[R5-BODY]` | `zn`; `zonefake`; `ZONE_OFF.txt` | ZONE PASS (C/D). zoneId is always empty in the hub; level-instance divergence only detected (`li:diff`), not repaired |
| Friend's body state (G4) | `coop_body.cpp` | `bh=[hp,hpMax,shp,shpMax,res,resMax,heal,healMax,pp,ppMax,ammo,ammoMax,dark,ds]`; `bodyset`, `bodydump`; `BODY_OFF.txt` | PEERHP, PEERDEAD PASS. pp/ammo/dark never changed in test save |
| Store-A save types + fields (G1/G1b) | `coop_world.cpp` + new `coop_world_ws2.cpp` | `wt` typed section; `wa2test/wa2census/wa2reset/wa2selftest/wa2arraytest`; `WS2_OFF.txt` | STORE_A2 PASS. String-array write never ran live (no instances in save). See R5-WORLD-report |
| Co-op death (DollsMP policy) | `coop_policy.h`, `coop_death.cpp`, `tests\test_coop_policy.cpp`, `HookPreSkip` in ms2_ue | `lv`,`bs`,`dstk`,`wr`,`rul`; `die`,`bossfake`,`dstate`,`dprobe`,`deathprobe`,`dstk`; `DEATH_OFF.txt`; rules `coop\MS2Coop.ini [Rules]` | DCASE2 PASS, DWIPE PASS, DHOLD hold+release PASS, **camera does not follow (§5)** |
| Equipment probe (#5) | `coop_probe.cpp` | `equipprobe [apply|revert|dfapply|dfrevert]` | `BPFL_Player_Equipment_C` CDO works for reads AND writes (`ChangeDarkFormMask` round-trip). Test char is dark form, no shell. Visual not confirmed |

Key death facts (from R5-DEATH-FINDINGS): player death flag = `BP_PlayerController_C.bDeathMixActive`
(ESpartaDeathState never moves for the player); reset is IN PLACE ~9 s after death
(`WBP_DeathScreen.CreateLoadingScreen → OnLoadingScreenCompleted → BP_WorldGenSubsystem.HandleResetReady → SessionReset → ClientRestart`);
dismissing the death-screen widget is what stops the reset; Gloom is taken at death (penalty needs no code);
`pay_death_penalty=0` refund NOT implemented; rest-clears-stacks approximate/unexercised; solo = vanilla.

## 3. Bugs found & fixed by the main agent this round (engine layer — important)
All were silently breaking things while log-based verdicts passed:
1. **`RefreshArrayCounts` throttle (100 ms)** made objects created in the last 100 ms look invalid → puppet got
   0 weapon meshes. Fix: `IsValid` forces a refresh when index ≥ cached count (`ms2_ue.cpp`).
2. **`SetAnimationMode` param name** `"AnimationMode"` → must be `"InAnimationMode"` (`coop_puppet.cpp`). Puppet never
   animated (reference pose) since the native port. Also `bTeleportRotation` → `bTeleportPhysics`.
3. **`ue::FTransform` layout**: must be 0x60 (Translation @0x20, Scale3D @0x40, two pads) — was 0x50 → every
   component created with a transform had scale (1,1,0) (weapons flat/invisible). Fixed + static_asserts; test_api updated.
4. **`MakeName` permanent failure latch**: one failed `Conv_StringToName` during boot disabled FName construction
   for the whole session (intermittent!) → sockets/tags/hook names all `None`. Now retries every 2 s; `"None"`
   short-circuits. Check: native log line `self-test: MakeFName("Actor") … MATCH`.
5. **`net::World()` poisoned by a recycled object slot**: `FindAllOf` now verifies each slot's CURRENT class is in
   the wanted family; `ResolveWorldAndPc` re-checks IsA World; `WorldOk` verifies class once.
6. **Death liveness staleness**: `ApplyPeer` is skipped during local hold/settle, so a living partner went stale
   (>6 s) and a boss hold became a wipe. New always-run step `DeathPeerKeepAlive` (`coop_net.cpp [R5-MAIN]`).
   **Built, NOT yet run.**
7. **Puppet gear pins** (`coop_appear.cpp` RecordPins/VerifyPins, 2 Hz, outside WEAPON_OFF): records parent/socket/
   leader-pose of every weapon AND look part after apply and re-attaches if moved. (The "detached after death"
   symptom was actually bug 4, but the pins stay as a safety net.)
8. **Params name check**: `Params::offset_of` logs `Params: <fn> has no parameter '<name>'` once per fn+name — grep
   native logs for it after every change.
9. Death camera code: helper `SetViewTargetLocal` (SetViewTargetWithBlend), fallback writes
   `PlayerCameraManager.ViewTarget.Target`, per-frame hook `CamHook` on `PlayerCameraManager.BlueprintUpdateCamera`.

New verification tools: `pupdump [self]` (components/sockets/scale/anim mode/attached actors),
`pupcheck` → `result_pupvis.txt` (PUPVIS verdict in run-two), `visual-two.ps1`.

## 4. Last measured results
- `run-two.ps1` on C/D (merged build before bugs 4/6): ALL PASS incl. PUPVIS (3/3 weapons, animmode=1), ZONE,
  PEERHP, PEERDEAD, STORE_A2, QUEST2, LIFT, CHEST… except **WORLD** (no verdict: host `PickUp` on the currency bag
  returned ok but its SimpleBool never flipped → nothing to publish; looked flaky — recheck with names fixed (bug 4
  may be the cause)). Output: `local\lab\run-two-r5-merged-cd.txt`.
- `run-two-death.ps1` on C/D: DCASE2 PASS, DWIPE PASS, DHOLD hold/release PASS.
- Visual (screenshots, `local\lab\*-xformfix`, `*-holdview5`): puppet animates and carries sword/shotgun/seal on
  the right sockets; gear (hood) was a misread — test char has no gear equipped.

## 5. Open problems (in priority order)
1. **Run the current build** (§6) — bugs 4 and 6 fixes + honest camera status have not been exercised in game.
2. **Hold spectate camera does not render.** `SetViewTargetWithBlend` runs (ok=1) but the dead player's
   `BP_PlayerCameraManager_C` keeps a fixed death camera; writing `ViewTarget.Target` changes `GetViewTarget` but not
   the picture; `BlueprintUpdateCamera` is never called while dead (hook installs, never fires). Status
   `viewpuppet` now = camera-hook frames only; harness reports `DHOLDCAM: WARN`. Ideas: hook `UpdateCamera`/
   `BlueprintUpdateCamera` while ALIVE to confirm the hook works at all; find what the death camera is (CameraStyle?
   a CameraActor? `GA_Death_Player` timeline); `SetViewTargetWithBlend` with a spawned CameraActor attached behind
   the puppet; or move the dead pawn's camera boom. Not owner-critical; DollsMP-parity nice-to-have.
3. **WORLD verdict** pickup flip (see §4) — re-run after bug-4 fix before investigating.
4. **Heap-corruption crash 0xC0000374** (DOUBLE_FREE inside `steam_api64.dll`, no dxgi frame; seen 22.10 under UE4SS
   too, often at session end on instance A). Possibly the owner's real crash. Dumps in
   `%LOCALAPPDATA%\CrashDumps`. Not investigated beyond the death agent's cdb analysis.
5. Engine-layer: `FindClass` fails for some BP classes (`WBP_DeathScreen_C`, `SpartaDataLayerSubsystem.SessionReset`
   veto unresolved) — may also be bug 4; re-check logs `death: N of 4 reset-veto hook(s)`.
6. `pay_death_penalty=0` refund, rest-clears-stacks verification, `die` works once per session.
7. Equipment sync proper (CDO works) — publish equipment tags, render on puppet; not started.
8. Wire size ~4 KB/packet > MTU (split hot/warm) — before two-machine play.

## 6. Exact next steps for the new session
1. Build is current (`native\build.bat`; tests pass). Deploy to **C and D** under the lock.
2. Run `run-two.ps1` (C/D, port 47020) → expect all PASS incl. PUPVIS; check WORLD.
3. Run `run-two-death.ps1` for `case2`, `hold`, `wipe` (C/D) → expect DCASE2/DWIPE/DHOLD PASS, DHOLDCAM WARN.
4. Run `visual-two.ps1 -Cmds 'B=bossfake 1;B=die'` and look at B's shots: partner keeps weapons; hold should NOT turn
   into a wipe any more (B log: no `recase 3 -> 1`).
5. Grep both native logs for `has no parameter` and `MISMATCH`.
6. Update `native\README.md` (add a "Round 5" section summarising §2–§3) and the memory file.
7. Report results to the owner and **ask what to do next** (owner's instruction). Candidate next items to offer:
   spectate camera, equipment sync via BPFL CDO, crash 0xC0000374 investigation, enemy drops (G6), boss phases (G7),
   wire split (G12).

## 7. Backups
- Source snapshots: `G:\Freespace\shell_coop\local\snapshots\native-20261025-082802-before-death-zone-body.tgz`
  (round-5 start), `…-104349-pre-r5-merge.tgz`, `…-120842-pre-death-merge.tgz`.
- Saves: `G:\Freespace\shell_coop\local\save-backups\20261025-082802-before-death\localappdata-SaveGames\`.
- Pre-native full backup: `G:\Freespace\shell_coop_backup_20261024-224846`.
- Merge base used for 3-way merges (`git merge-file`, mind CRLF/LF — normalize first): `local\merge-base\`.
