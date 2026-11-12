# Round 5 — common brief for every agent (read fully before doing anything)

Project: Mortal Shell II (UE 5.6, Win64 shipping) two-player co-op mod. It is a **native dxgi.dll proxy**
(no UE4SS, never re-add it). Source: `native\src\*.cpp`, shared contract `native\coop_state.h`,
build `native\build.bat` (output `native\build\dxgi.dll`), unit tests `native\build.bat test`.
Background reading, in this order, skim what you don't need:
- `native\README.md` (how the engine layer works; sections "first live run" .. "Fourth pass" = lessons learned)
- `native\src\ms2_ue.h` (the engine API: FindObject/FindAllOf/Get/Set/Call/Params/HookPre/HookPost/maps)
- `native\coop_state.h` (module contract), `native\src\coop_net.cpp` (wire, 30/15/2/1 Hz router, `cmd` verbs ~line 2311)
- `docs\HANDOFF-REMAINING-SYNC.md` (gap register G1..G12; written for the OLD Lua mod, so its `main.lua:NNNN`
  references point at `mod\CoopMod\Scripts\main.lua`, read-only; the native code is a 1:1 port of it)
- `G:\Freespace\shell_coop\research\mine\SYNC-DATA-MINE.md` + `research\mine\sync-map.tsv` (every class /
  offset / function signature with proof lines). `research\raw\` has the full object dumps if you need to grep.

## Your workspace — isolation rules (critical)
- You work ONLY inside your own copy: `G:\Freespace\shell_coop\wt\<you>\ms2-coop\` (`<you>` = body | death | world).
  Never edit `G:\Freespace\shell_coop\ms2-coop\` (the main tree) or another agent's copy. Three agents run in parallel.
- Build in your copy (`<copy>\native\build.bat`, `build.bat test`). Deploy with
  `<copy>\deploy.ps1 -GameDir <inst> -DllPath <copy>\native\build\dxgi.dll` (run your copy's scripts).
- Shared files (`coop_state.h`, `coop_net.cpp`, `ms2_ue.h/.cpp`, `run-two.ps1`) WILL be edited by other agents
  too. Keep your edits to them minimal and wrap every edit in marker comments
  `// [R5-<YOU>] begin` ... `// [R5-<YOU>] end` (PowerShell: `# [R5-<YOU>] begin/end`) so an integrator can
  merge three copies mechanically. Prefer putting your logic in your own new file(s).
- Wire: the publish line is the hand-rolled JSON line built in coop_net.cpp. Add your keys only as specified in
  your task brief (the key names are a contract other agents rely on). Keep unknown keys ignorable.

## The two test game instances — shared, LOCKED
- A = `E:\Games\_MS2-coop-test` (host), B = `E:\Games\_MS2-coop-b` (guest). NEVER touch `E:\Games\Mortal Shell II`
  (the owner's real install) in any way.
- Before ANY deploy / `lab.ps1` / `run-two.ps1` / killing a game process:
  `. G:\Freespace\shell_coop\local\gamelock.ps1; Lock-Game '<you>'` and afterwards `Unlock-Game '<you>'`.
  The lock can be held by another agent for a while; that is normal, keep coding while you wait (run the lock
  wait in the background or retry later). Release it as soon as your run ends. Hold it at most ~20 min at once.
- Never deploy while a game process is running (overwriting a loaded dxgi.dll made a client exit).
- Delete `<inst>\MortalShell2\Binaries\Win64\coop\ws_ledger_*.txt` on both instances before a run that tests WORLD.
- Saves: the harness snapshots/restores `%LOCALAPPDATA%\MortalShell2\Saved\SaveGames` itself; don't break that.
  Kill every game/relay process you start before releasing the lock.
- Harness: `run-two.ps1` = two-client run with verdicts (`=== NAME: PASS|FAIL` lines, `RESULT: PASS|FAIL`).
  `lab.ps1 -Commands "verb args@seconds,..."` = one scripted client. Commands go through `coop\cmd.txt`.
  For your own verdicts make a private copy `run-two-<you>.ps1` in your copy; also write the verdict block as a
  separate file `native\briefs\R5-<YOU>-runtwo-snippet.ps1` so the integrator can merge it.
- The baseline must stay green: every existing verdict in run-two must still PASS with your change
  (LEDGER has no verdict when ledgers were cleared; that is fine).

## Engineering rules
- Crash safety first (this rewrite exists to stop crashes): validate every engine pointer (`ue::IsValid`), all
  engine work on the game thread (the router already is), no unbounded FindAllOf in hot paths (use the index /
  caches as existing modules do), SEH-safe reads (`ue::ReadMem`-style) for raw offsets.
- Perf budget: the whole mod costs ~1 ms/frame on the host today. Your additions: < 0.15 ms average. 2 Hz work
  for slow state. Use the profiler (`ue::CostScope`, ProfDump; see README "Fourth pass").
- Every new feature: a kill switch `<X>_OFF.txt` (read once at load like the others), a `sync.txt` counter line,
  log lines in `probe.log`/native log, a `cmd` test verb, and a run-two verdict written by the RECEIVING client.
- FGameplayAttributeData = 16 bytes: BaseValue @+0x08, CurrentValue @+0x0C of the member offset.
- Prefer the game's own functions over poking bytes when a proven function exists; but calling unknown BP
  functions can crash — test new calls in a lab run first, one at a time, and log before/after.
- C++17/MSVC, match the surrounding code style and comment density. No git here; don't create one.

## Final report (your last message) — keep it factual
Files changed (and which marker blocks in shared files), new wire keys, new cmd verbs, new verdicts and their
last results (paste the run-two verdict lines), unit-test result, measured perf cost, anything that does NOT
work or was not verified, and open questions. Do not claim a verdict passed unless you saw it pass.
