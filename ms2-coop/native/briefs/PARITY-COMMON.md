# Parity round — common brief (read fully)

Mortal Shell II co-op, G:\Freespace\shell_coop\ms2-coop. The UE4SS Lua mod (mod\CoopMod\Scripts\main.lua = REFERENCE
behaviour) was ported to a native proxy DLL (native\src; build `cmd /c native\build.bat test`). It now boots, syncs and
runs without crashes or stalls on both test clients. But run-two.ps1 verdicts lag the Lua. Lua baseline (docs\HANDOFF.md
§16-17): SHELL, WEAPON, LOOK, ANIM, DAMAGE, DEATH, PROGRESS, QUEST, QUEST2, LIFT, CHEST, LEDGER, STREAM, NPCFAR, A, B all PASS
(WORLD fails only because the test door/pickup is already open in the save). Native latest run (local\test\run-two-out-slotfix4.txt):
LOOK FAIL (on=True off=False); ANIM no verdict; WORLD/PROGRESS/QUEST/QUEST2/LIFT/CHEST/SPAWN no verdict;
MIRROR FAIL (A driven:0 taken:0; B driven:1 taken:419 released:418 — take/release thrash); STREAM FAIL (remote area 91732 uu
away: cells loaded=false, static meshes 2 -> 2). SHELL WEAPON DAMAGE DEATH PEERHIT AITARGET NPCFAR LEDGER A B PASS.

Read: native\README.md (all "pass" sections — they list bug classes already found in this port: Lua `break` inside
`repeat…until true` = continue; Lua patterns without backtracking; FString NUL; full path vs class name; clock rewinds on
level load; GetRealTimeSeconds vs os.time; FName index mismatch between Conv_StringToName and NamePrivate), docs\NATIVE-PORT.md,
run-two.ps1 (exactly which cmd.txt commands it issues for your verdicts, which result_*.txt file + text it waits for, and
its deadlines), and the Lua for your features.

Evidence on disk from the last run (do NOT launch the game — another agent will run it after you):
E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\coop\ (A) and E:\Games\_MS2-coop-b\...\coop\ (B): probe.log, sync.txt,
result_*.txt, out_*.txt, peer_*.txt; ms2coop-native.log next to each exe; plus local\test\* run folders. Use them to prove
where each verdict path stops (e.g. command received? test started? follow-up ran? judge ran? file written?).

Method: for each of your verdicts trace harness command -> coop_net RunCommands -> your module's test/apply/judge ->
result file, line-by-line against the Lua. Fix every divergence in YOUR files only. If a fix is needed in a file you don't own
(esp. coop_net.cpp or ms2_ue.cpp), write a unified diff to native\briefs\patch-<yourname>-<file>.diff and describe it — do
not edit. Before first edit copy the file to native\src\_bak\<name>.<timestamp>. Add throttled diagnostic log lines at each
gate so the next run proves where it stops. Build must pass (`cmd /c native\build.bat test`); if a concurrent edit by another
agent breaks the full build, compile your files alone with cl /c and the flags in build.bat. Work fast.
Final report: per verdict — where it stopped (with log evidence), cause, fix (file:line), patches for other files, and what
the next run's log should show.
