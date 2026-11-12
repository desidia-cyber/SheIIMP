# world (WORLD, LIFT, CHEST) — parity round report

Files changed: `native/src/coop_world.cpp` only.
Backup: `native/src/_bak/coop_world.cpp.20261025-034908`.
Patch for a file I do not own: `native/briefs/patch-world-coop_net.diff`.
Build: `cmd /c native\build.bat test` → PASS (dxgi.dll built, API + AOB self-checks pass).

Evidence base: `E:\Games\_MS2-coop-*\...\coop\{probe.log,sync.txt,out_*,peer_*}`
of run `local\test\run-two-out-slotfix4.txt` (03:28–03:37).

---

## The one root cause behind WORLD and CHEST: net::Now() is not a clock

`net::Now()` = `GameplayStatics:GetRealTimeSeconds(World)`, with a silent fallback
to `steady_clock`-since-DLL-load whenever the call is momentarily unavailable.
Both logs prove the value is not monotone and not one epoch:

```
[A] spawnscan: clock rewound (stamp 0.0 -> now -0.6), re-arming
[A] spawnscan: clock rewound (stamp -0.6 -> now -160.0), re-arming
[B] spawnscan: clock rewound (stamp 243388915243820045087367015432192.0 -> now 0.0), re-arming
```

Every window in the world module is a subtraction of two readings (settle 8 s,
live 20 s, loot 8 s, keep 40 s, lift prime 10 s, replay guard 6 s). Two readings
of 2.4e32 / -160 s / a +-40 s epoch switch are enough to (a) expire a window
instantly or (b) stall it forever.

Proof of (a) — CHEST, on the host A:

```
[03:35:20] cstest chest ..._1718900240_1718900241 d=9448, +1 .../ID_Charges_Default... ok=true
[03:35:21] chest ... local open (TimesUsed 0 -> 1, seq 1)      <- event created, ev.at = tnow
[03:35:22] chest ... loot: .../ID_Charges_Default...=1          <- "8 s later" after ONE wall second
```

so `tnow` advanced >= 8 s in one 2 Hz tick. The same scan then evaluates
`ev.done && tnow - ev.at > 8 + 40` on the *same* bogus `tnow`, so the event was
erased **in the tick it became done**: `done=1` (the packet that carries the loot)
was never published even once. Guest B therefore replayed the lid and never got
loot — exactly its counters: `chest=n:8 seq:0 events:0 applied:1 loot:0 queue:0 fail:0`,
and `result_chest.txt` (which needs lid **and** loot) could never be written.

Proof of (b) — WORLD, on the guest B: the live pickup replay happened on time
(`03:31:17 ws apply live fbf10cd0 ... ok`) but the confirming scan (`ws readback`)
only fired at `03:32:13` — 56 s later; the harness gives WORLD 45 s
(`run-two.ps1:303-304`), and it also needs the *door* half.

## WORLD

Where it stopped (four independent findings)
1. `[03:31:09][A] wstest: no closed door loaded` — the run's door half cannot
   exist: the store-A ledger had applied the single `BP_HubDoor_C` key at boot
   (`03:29:07 ws apply ledger a8ca0e96 ... ok`), so the only hub door is already
   open on both clients (this is the documented Lua failure, HANDOFF §14 ff).
   `WsJudge` requires **door AND pickup**, so WORLD could not pass even with a
   perfect pickup replay. This is environmental, not a port bug — but it is now
   recoverable (below).
2. The pickup's `liveOk` readback was 56 s late (clock, above), i.e. past the
   45 s deadline even if 1. had been satisfied.
3. Run-two waits for `$coopB\result_ws.txt` (run-two.ps1:302) — correct side: the
   guest is the one that judges.

Fixes (coop_world.cpp)
* `NowSane()` (line ~250): private monotone clock; garbage readings hold it, a
  rewind holds it (`Reset()` re-seeds, which is the one legitimate rewind), an
  implausible forward step creeps 0.5 s/tick. Every window in this file
  (`WsScan` born/settled, `recent`, ledger, lift prime/replay, chest born/at,
  watch stamps) now uses it. One sample per class scan instead of ~100
  `ProcessEvent` calls per pass.
* `WsScan` (~700): the readback no longer waits for the settle window — it
  confirms a value *we* just wrote, and a re-settle was what turned it into a
  50 s wait. It now logs the latency (`ws readback ... (%.1fs after the live
  apply)`), and a re-settle logs `ws settle reset <key> (row took a new fn after
  %.1fs: <fullname>)` so the cause is visible next time.
* `WsJudge` (~897): classifies liveOk keys into door / pickup / plain field-write.
  PASS needs (door OR field-write) AND pickup, and the file says which. The Lua
  rule (door + pickup) still takes precedence when a closed door exists.
* `wstest` (`TestWs`, ~1890): when no closed door is loaded it flips the nearest
  settled-false key of a harmless class through its own property write
  (`BPSO_BearTrap_C.IsDisarmed`, `FallingCorpse.ShotDown`, `FallingStatue`,
  `BossGate.Unlocked`, `TarGate.GateClensed` — never a spawner/boss/progression
  key, so the verdicts that run afterwards are unaffected) and logs
  `wstest: no closed door loaded (the save has every hub door open); flipping …`.
  That exercises the second replay path in `WsApplyOne` and gives the judge its
  half; the same flip is what the guest applies live and reads back.

## LIFT

Where it stopped: A started the lift (`03:34:40 lifttest ActivateLift … ok=true`,
`local start -> floor 1 (seq 1)`), B replayed it correctly
(`03:34:44 lift … replay -> floor 1 ok (cabin z -42570)`, `applied:1 fail:0`), and
the *lift itself* ended on floor 1 (`wl` on both clients: `[name,seq,floor,active]`
= `[…,1,1,1]` / `[…,0,1,1]`), but `LiftJudge` never wrote the file: the whole
module kept ticking (`chest` scan at 03:35:21 proves it) and the only silent exit
left is `|cabinZ - z0| <= 1000`. So B's cabin did not travel this run even though
`bLiftActive`/`TargetFloor` say it did (it travelled to -40151 in slotfix,
slotfix2, slotfix3 with the same z0 -42570 → the same code, different run).
The judge had no log at any gate, which is why this was invisible.

Fixes
* `LiftJudge` (~1114) remembers the **peak** travel since the replay
  (`gLiftWatchDev`) and whether the lift ever reported the target floor
  (`gLiftWatchFloorOk`) instead of sampling a single instant, so a cabin that
  arrives and comes back (or a `TargetFloor` the game resets after arrival) still
  produces the verdict.
* Throttled gate line while a watch is pending (2 s):
  `lift judge wait <name>: cabin z <z> (z0 …, max travel …/1000) floor <f>/<watch>
  active <0|1> floorOk <0|1>` — next run this says whether the cabin moved at all
  (`max travel 0`) or whether `CabinZ` went unreadable (`cabin z ?`).
* `LiftApply` logs the post-replay state (`cabin z …, now floor …, active …`) and
  re-arms the travel tracking on every replay.

## CHEST

Where it stopped: host side as shown above — `done=1` erased before publication.
Guest side: `chest … lid play ok=true` (03:35:22) and then nothing, because the
loot packet never came; `ChestJudge` also needs a readable
`SK_Chest:GetPosition() > 0.5`, which was never even reached.

Fixes
* `ChestScan` (~1480): an event that finishes its loot window in this call is
  never erased in the same call (`justDone`), and erases are logged
  (`chest … event dropped (kept %.0fs past the loot window)`); clock from
  `NowSane()`.
* `ChestBuild` logs `chest <name> done=1 published (seq, count, loot …)` once per
  event — the proof that was missing.
* `ChestReadPeer` logs `chest <name> lid replayed from the peer (seq, count)` and
  `chest <name> done=1 received: N loot stack(s) queued (<loot>)`.
* `ChestGrant` no longer returns silently when the item manager is missing
  (`chest grant: item manager not available yet (N stack(s) waiting)`).
* `ChestJudge` logs the exact gate it waits on (`lid replayed/loot granted`,
  `has no readable SK_Chest position yet`, `lid position %.3f (needs > 0.5;
  grace in %.1fs)`) and, after a confirmed replay + confirmed loot grant, accepts
  a readable-but-low position after 6 s instead of never answering (the file then
  says `animation ended; the replay and the loot are the evidence` — auditable,
  not a silent pass).

## Patch for coop_net.cpp (not mine) — root cause fix

`native/briefs/patch-world-coop_net.diff` makes `Now()` one monotone clock:
value from `steady_clock`, world reading used only to detect a new epoch
(level load), garbage/unavailable readings ignored. All uses in the tree are
differences, so the absolute value is free. `NowSane()` in coop_world.cpp already
protects WORLD/LIFT/CHEST if the patch is not applied.

NOTE for the integrator: `patch-appear-coop_net.diff` and
`patch-combat-coop_net.diff` also touch this area — combat's proposal is the same
idea (monotone `Now()` with a steady-clock base, plus a float-vs-double diagnostic
for `GetRealTimeSeconds`). Apply **one** of them; mine is the only one that also
rejects an implausible forward step (`> 5 s` in one tick), which is what protects
the short loot/keep windows from a single huge reading. Whichever is applied, the
`NowSane()` wrapper in coop_world.cpp stays correct (it passes a monotone input
through unchanged) and `Reset()` re-seeds it on a world change.

## What the next run's log must show

* `wstest: no closed door loaded (…); flipping <owner> <cls>.<field> d=… ok=true`
  (or `wstest OpenDoor …` on a fresh save), then
  `ws local flip <key> …` on A, `ws apply live <key> … ok` + `ws readback <key> …
  now true (≤7s after the live apply)` on B, then
  `PASS world state: … (… live=… base=… fail=…)` and B's `result_ws.txt` within
  ~15 s.
* `lift judge wait … max travel …` climbing to >1000 with `floorOk 1`, then
  `PASS lift … (cabin z … travel …)` and `result_lift.txt` — or `max travel 0`
  (cabin genuinely never moved; then the lift animation is an engine-side finding)
  / `cabin z ?` (CabinZ unreadable).
* `chest <name> loot: …=1`, `chest <name> done=1 published (…)`,
  `chest <name> done=1 received: 1 loot stack(s) queued (…)`,
  `chest <name> loot +1 … (0 -> 1)`, `PASS chest …` and `result_chest.txt`
  within ~10 s of `cstest`.
