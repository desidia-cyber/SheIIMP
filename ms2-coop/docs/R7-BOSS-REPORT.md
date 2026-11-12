# R7 — Boss phases (G7): hook the phase decision, publish it, replay it on the peer

Workspace `G:\Freespace\shell_coop\wt\r7-boss` — test installs host `E:\Games\_MS2-coop-e`,
guest `E:\Games\_MS2-coop-f`, relay port 47120. Verdict `BOSSPHASE`. Kill switch `BOSS_OFF.txt`.

Status: **done**. Implemented, built (`coop_policy: 112 checks passed`, `Built:`), proven
end-to-end on E/F (host publishes, peer applies through the game's own function, no echo,
no double fire), and the full suite runs green with `BOSSPHASE: PASS` / `RESULT: PASS`.

---

## 1. Root cause (why the peer's boss stayed in phase 1)

Boss fight state is **entirely local** and is not a value the existing mirror could land:

| Fact | Where |
|---|---|
| `UBPC_BossAI_C` owns `bTriggeredSecondPhase` @0xC0, `bTriggeredThirdPhase` @0xC1, `bIsAggroed` @0xC2, `SecondPhaseHealthPercent` @0xF8, `ThirdPhaseHealthPercent` @0x100, delegates `OnSecondPhaseStarted` @0x118 / `OnThirdPhaseStarted` @0x260 | `research/mine/SYNC-DATA-MINE.md` §15, `research/raw/CXXHeaderDump/BPC_BossAI.hpp` |
| the transition is **decided** by two Blueprint UFunctions, `ShouldStartSecondPhase()` / `ShouldStartThirdPhase()` (no parameters), which the boss's own damage event calls | same header, function list |
| the enemy health mirror (`coop_combat.cpp` `ApplyHealth`) writes the **number** with `SetHealthNorm`; a written attribute raises no damage event, so the peer's `ShouldStartSecondPhase` is never asked | `coop_combat.cpp` `ApplyHealth`, `SetHealthNorm` |

So on the peer the boss kept fighting in phase 1 — phase-1 move set, no `GA_*_PhaseChange_C`,
no phase camera push, no phase HUD — while the owner was in phase 2/3. That is gap G7
("boss phase thresholds"): the *damage* lands, the *decision* does not.

---

## 2. Design (and why each choice)

### 2.1 Detect the decision, not the health

`coop_boss.cpp` installs **POST hooks** on `BPC_BossAI_C.ShouldStartSecondPhase` /
`ShouldStartThirdPhase` through the existing ProcessEvent detour machinery
(`ue::HookPost` → `RegisterHook` → `ResolveHookTarget` "Class.Function", the same API
`coop_death.cpp` uses for its rest/death hooks). The hook body does almost nothing: it raises
a flag and counts (it runs inside the detour, on the game thread).

The 15 Hz publish tick then rescans the live `BPC_BossAI_C` components and diffs
`bTriggeredSecondPhase` / `bTriggeredThirdPhase` against what the module last accounted for.
That diff is what produces an event, which means:

* a transition the hook missed (a blueprint that inlines the decision instead of calling the
  function) is still caught — a **1 Hz flag poll** is the safety net;
* the event carries the phase the game actually entered, never an inference from health.

### 2.2 Publish only changes/events, on its own wire key

```
,"bsp":[["<tail>","<class>",<phase>,<seq>], ...]
```

* own top-level key, appended **last** in `net::Publish` (no existing key moves);
* present only when there is something to say (omitted otherwise) — the key costs 0 bytes in
  the steady state;
* an event is repeated for 2 s (>30 packets) so one lost packet is harmless, and carries a
  **per-key seq** so a repeated packet is harmless (the receiver drops `seq <= seen`);
* key = `net::Tail(owner)`, byte-identical to the key the enemy health mirror uses in `"nhp"`,
  so the phase event and the health value that gates it describe the same local actor.

### 2.3 Apply with the game's own function

On the peer, `boss::OnPeerLine` runs **after** `combat::OnPeerLine` (so the `nhp` health of the
same packet is already applied, and inside the zone guard, because a boss key is a per-map
actor name). It then calls the local component's `ShouldStartSecondPhase()` /
`ShouldStartThirdPhase()` — the game's own code path, which sets the flag, broadcasts the
delegate, starts the phase ability and pushes the camera state. **No raw byte write.**

The gate both functions use is the health percent, so when the mirrored health has not landed
yet the module writes it with the **proven** writer the mirror itself uses
(`combat::SetHealthNorm`, the same call `ApplyHealth` makes) down to just under the threshold,
and logs that write. If the function still refuses, the event goes on a retry list
(0.5 s × 12) instead of being retried per packet.

### 2.4 Why it cannot fire twice (the health/death-mirror consistency requirement)

* The transition is guarded by the game's **own** `bTriggered*Phase` flags: after the peer
  applies phase 2, its own later health-driven check early-outs. Two clients cannot disagree
  about "phase 2 already ran" because both went through the same function.
* The echo of our own apply (our call goes through ProcessEvent, so our own POST hook sees it)
  is suppressed by `appliedPhase[key]`, which is set **before** the call; a flip this module
  caused is never published back. The counter `echoes:` in sync.txt counts those suppressions,
  so the log can prove the mechanism ran rather than assuming it.
* The peer's own copy has no real damage events (its AI is paused / it is mirrored), so it has
  no independent reason to flip.

### 2.5 Identity, kill switch, world change

* A runtime-**summoned** boss has a per-client name (that is why `spawnReplay` needs the
  `SP_<spawner>` alias), so the event also carries the class short name and the receiver falls
  back to it **when exactly one local boss carries that class** (ambiguous → refused, logged).
  For a placed boss the tail matches and the fallback never runs.
* `BOSS_OFF.txt` (`Toggles::bossSync`) turns the lane off: no scan, no `"bsp"` on the wire, no
  phase call, no hooks.
* `net::ResetAll` → `boss::Reset()` drops every cached component and every per-key map on a
  world change (a boss that comes back after a reset is a new fight).

---

## 3. Diff summary

| File | Change |
|---|---|
| `native/src/coop_boss.cpp` | **new** module (~640 lines): the scan, the two POST hooks, the event queue + `"bsp"` codec, the apply path with the health-gate write and the retry list, `boss::Status()`, and the `bossphase` verbs |
| `native/coop_state.h` | `Toggles::bossSync` (`BOSS_OFF.txt`); `BossEntry` / `BossEvent` / `BossRetry` / `BossState`; `State::boss`; `namespace boss` API |
| `native/src/coop_net.cpp` | `Toggles::Load` row for `BOSS_OFF.txt`; `boss::Serialize` in `Publish` (last key); `boss::OnPeerLine` in the apply dispatch (after combat, inside the zone guard); `boss::Status()` in `sync.txt`; `boss::OnCommand` in the verb dispatch; `boss::Reset()` in `ResetAll` |
| `ms2-coop/run-two.ps1` | `BOSSPHASE` verdict block (spawn both → owner forces phase 2 → peer must report phase 2 + `applied>=1`); gates `RESULT` only when a boss existed on both sides (a `SKIP` cannot hide a defect and cannot redden a healthy run) |
| `ms2-coop/boss-lab.ps1` | **new** lab wrapper: lock → deploy to E/F → delete `ws_ledger_*.txt` → one `visual-two.ps1` session with the boss commands → unlock |

Functions touched: `net::Publish`, `net::ApplyPeer`, `net::WriteSyncStatus`, `net::RunCommands`,
`net::ResetAll`, `Toggles::Load` (+ the new `boss::*`).

## 4. The measured proof (boss lab, E/F, `bosslab4`)

The hub save this harness enters **has no boss in it** — measured, not assumed:

```
[01:33:42][A] bossdump: no BPC_BossAI_C in this world (2 scan(s)); `bossphase spawn` summons one, `bossphase dump` re-reads
```

`summon` cannot fix that in this build, and the log says *why* rather than "nothing happened":

```
[01:33:44][A] bossphase spawn: /Game/Sparta/Characters/Bosses/Dwarf/BP_DwarfBoss.BP_DwarfBoss_C -- class in memory before: no
[01:33:44][A] console [summon /Game/Sparta/Characters/Bosses/Dwarf/BP_DwarfBoss.BP_DwarfBoss_C]
[01:33:44][A] bossphase spawn: /Game/Sparta/Characters/Bosses/Dwarf/BP_DwarfBoss.BP_DwarfBoss_C -- class in memory after: no
... (same for BP_Cannibal_Knight_MiniBoss and BP_Batushka_MiniBoss)
[01:33:46][A] bossphase spawn: after 1.5 s bosses=0 (ai characters=12) tried=-
```

So: **no boss is reachable in the test save** (the puppet's `summon` works only because
`BP_PlayerCharacter_C` is always loaded; this build gives the mod no asset loader —
`net::Find`'s CONTRACT-REQUEST 4). What *is* loaded is the component class
`BPC_BossAI_C` (the two hooks resolve), so the lab puts a **real** `BPC_BossAI_C` on a live
actor with `AActor::AddComponentByClass` — the engine call `coop_appear.cpp` already uses for
equipment. The component is genuine: real class, real BeginPlay, real
`ShouldStartSecondPhase` / `ShouldStartThirdPhase`, real thresholds (`t2=0.500`, `t3=0.300`).

Owner (A) — the phase change is made through the game's own function, the hook sees it, the
event goes out:

```
[01:49:03][A] bossphase mkcomp: attaching BPC_BossAI_C to BP_PlayerCharacter_C ...BP_PlayerCharacter_C_2147479937_2147479938
[01:49:03][A] bossphase mkcomp: AddComponentByClass -> BPC_BossAI_C ...BP_PlayerCharacter_C_2147479937_2147479938.BPC_BossAI_C_2147468357_2147468358
[01:49:08][A] bossdump: BP_PlayerCharacter_C_2147479937_2147479938 cls=BP_PlayerCharacter_C phase=1 hp=1.000 t2=0.500 t3=0.300 aggro=0
[01:49:13][A] bossphase force: lowered ... hp 1.000 -> 0.480 via SetHealthNorm (ok) to clear the phase-2 gate 0.500
[01:49:13][A] bossphase: hook fired ShouldStartSecondPhase on BPC_BossAI_C_2147468357_2147468358 (fire #1)
[01:49:13][A] bossphase force: ... -> ShouldStartSecondPhase (BP_PlayerCharacter_C) called=1 phase now 2 (flags second=1 third=0) hookfire=1
[01:49:13][A] bossphase: publish BP_PlayerCharacter_C_2147479937_2147479938 (BP_PlayerCharacter_C) phase 2 seq 1 hp=0.48 (second)
[01:49:18][A] bossphase: hook fired ShouldStartThirdPhase on BPC_BossAI_C_2147468357_2147468358 (fire #2)
[01:49:18][A] bossphase: publish BP_PlayerCharacter_C_2147479937_2147479938 (BP_PlayerCharacter_C) phase 3 seq 2 hp=0.28 (third)
```

Peer (B) — the event is received, the gate is cleared with the proven health writer, the
**game's own** function is called, the flags flip, and nothing is published back:

```
[01:49:13][B] bossphase: BP_PlayerCharacter_C_2147479937_2147479938 hp 1.000 -> 0.480 for the phase-2 gate 0.500 (ok)
[01:49:13][B] bossphase: hook fired ShouldStartSecondPhase on BPC_BossAI_C_2147468937_2147468938 (fire #1)
[01:49:13][B] bossphase: APPLIED phase 2 on BP_PlayerCharacter_C_2147479937_2147479938 (BP_PlayerCharacter_C) via ShouldStartSecondPhase -- flags second=1 third=0
[01:49:18][B] bossphase: APPLIED phase 3 on BP_PlayerCharacter_C_2147479937_2147479938 (BP_PlayerCharacter_C) via ShouldStartThirdPhase -- flags second=1 third=1
```

Both `sync.txt` tails, at the end of the run (copied by the lab as `sync_A.txt`/`sync_B.txt`):

```
=== sync_A.txt
boss=n:1 phase:BP_PlayerCharacter_C_2147479937_2147479938:3 scanned:122 hook:2 pub:2 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0
=== sync_B.txt
boss=n:1 phase:BP_PlayerCharacter_C_2147479937_2147479938:3 scanned:106 hook:2 pub:0 applied:2 seen:2 fail:0 missed:0 echoes:0 peerkeys:1 off:0
```

Read it as: A published 2 events (`pub:2`), B received both (`seen:2`) and applied both
(`applied:2`), and **B published nothing** (`pub:0`) — the peer did not echo the phase it was
told about. `hook:2` on both sides with `applied:2` on B is the **no-double-fire** evidence:
each phase transition ran the game's function exactly once per client, because the second one
would have been a third hook fire. `fail:0 missed:0` = nothing was refused or dropped.

Screenshots (the same run, while the phase replay happened — both clients alive and rendering,
no visual breakage from the added component):
`G:\Freespace\shell_coop\local\lab\20261026-014747-boss4\contact-sheet.png` (built from
`A-after-cmds1.png`, `B-after-cmds1.png`, `A-after-cmds2.png`, `B-after-cmds2.png`,
`A-sees-B-dir0.png`, `B-sees-A-idle.png`).

Honest limit of the visual half: with a **synthesized** component on a player character there
is no boss model, so a phase change cannot show on screen. The phase *is* the `bTriggered*Phase`
flags + the delegate broadcast, and both are measured above; on a real boss the same call path
runs `GA_*_PhaseChange_C` and the phase camera state.

## 5. Test commands run

```
# build (must print coop_policy: 112 checks passed and Built:)
cmd /c G:\Freespace\shell_coop\wt\r7-boss\ms2-coop\native\build.bat test

# boss lab: lock -> deploy E/F -> delete ws_ledger_* -> one visual-two session -> unlock
powershell -NoProfile -ExecutionPolicy Bypass -File <ws>\ms2-coop\boss-lab.ps1 -Tag boss4 `
  -Cmds "A=bossphase mkcomp;B=bossphase mkcomp;A=bossphase dump;B=bossphase dump;A=bossphase force 2;B=bossphase dump;A=bossphase force 3;B=bossphase dump"

# the regression suite (BOSSPHASE included) -- run while holding the shared lock
powershell ... run-two.ps1 -GameA E:\Games\_MS2-coop-e -GameB E:\Games\_MS2-coop-f -Port 47120
```

### 5.1 The suite's verdict lines (`run-two-r7boss-2.txt`, full log kept in the workspace)

```
=== SHELL: PASS          === WEAPON: PASS        === PUPVIS: PASS      === LOOK: PASS
=== ANIM: PASS           === DAMAGE: PASS        === DEATH: PASS       === WORLD: PASS
=== PROGRESS: PASS       === QUEST: PASS         === QUEST2: PASS      === LIFT: PASS
=== CHEST: PASS          === STORE_A2: PASS      === STORE_A2SELFTEST: ws2 self test: 20 pass, 0 fail
=== SPAWN: PASS          === MIRROR: PASS        === PEERHIT: PASS     === AITARGET: PASS
=== STREAM: PASS         === NPCFAR: PASS        === ZONE: PASS        === PEERHP: PASS
=== PEERDEAD: PASS
=== BOSSPHASE: A before 'n:0 phase:- scanned:220 hook:0 pub:0 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0'
=== BOSSPHASE: spawn h=True g=True ; A n:1 phase:BP_PlayerCharacter_C_2147479937_2147479938:2 scanned:227 hook:1 pub:1 applied:0 seen:0 fail:0 missed:0 echoes:0 peerkeys:0 off:0
=== BOSSPHASE: B n:1 phase:BP_PlayerCharacter_C_2147479937_2147479938:2 scanned:205 hook:1 pub:0 applied:1 seen:1 fail:0 missed:0 echoes:0 peerkeys:1 off:0
=== BOSSPHASE: PASS (owner phase 2 + peer applied>=1)
=== NATIVE: E:\Games\_MS2-coop-e -> READY ; coop started
=== NATIVE: E:\Games\_MS2-coop-f -> READY ; coop started
RESULT: PASS
```

(One run, no retry needed: `RESULT: PASS` on the first attempt. SPAWN/MIRROR, which the brief
calls flaky, both passed here.)

Verbs added (cmd.txt): `bossphase dump [filter]`, `bossphase force <2|3>`,
`bossphase sim <2|3>`, `bossphase spawn [class]`, `bossphase mkcomp [self|enemy]`,
`bossdump`.

Kill switch: `<coop dir>\BOSS_OFF.txt` → `Toggles::bossSync` = false: no scan, no `"bsp"` on
the wire, no phase call, no hooks (`boss=... off:1` in sync.txt shows it).

## 6. Verdict line (run-two.ps1)

`=== BOSSPHASE: PASS|FAIL|SKIP` — appended after the PEERDEAD block (it is the last thing the
suite does, because it damages a player's health to clear the phase gate).

* the block attaches a real component on **both** clients (`bossphase mkcomp`, the pawn, so
  the key matches exactly), waits for `boss=n:1` on both, then `bossphase force 2` on the host;
* PASS requires the host's own `boss=...phase:<key>:2` **and** the guest's
  `phase:<key>:2` with `applied>=1` (i.e. the event arrived and the game's function ran);
* it gates `RESULT` only when a component existed on **both** sides. A `SKIP` is printed when
  the component class is absent from the build — a check that cannot run must never be
  reported as a pass, and it must not redden a healthy run either.

## 7. What is left open (honest list)

1. **No boss in the test save, and no way to load one.** `summon` cannot load an unloaded boss
   package (`class in memory after: no`), and the mod has no asset loader. So the replay was
   proven on a *real* `BPC_BossAI_C` attached to a live actor, not on a real boss. The next
   probe that would close it: a save inside a boss arena (TombZone / `BP_LevelInstanceSelector_*Boss`),
   where the boss actor is streamed and the component exists naturally. Expected extra work:
   none — the same hook/apply path drives it (`bossphase dump` would show the boss key).
2. **The phase *abilities* were not observed.** With a synthesized component the bound
   handlers are the boss BP's, and this component has no boss character
   (`CacheCharacter=0 BossAICharacter=(none)`), so `GA_*_PhaseChange_C` cannot start. The
   delegate broadcast is the game's own code, so on a real boss it will run; that is
   *unverified*, not *verified*.
3. **Encounter side of G7 is untouched**: arena/fog walls (`ApproachingGate`), `BPSO_BossDLManager`
   data layers, `SpartaHordeEncounter` spawner replay, `BP_SpawnerSessionSaveObject`. This
   round did the phase lane only.
4. **The full suite runs green** — `RESULT: PASS` with `BOSSPHASE: PASS` (see §5.1). The run was
   delayed ~35 min by the shared run lock (other agents), not by the change; it passed on the
   first attempt once the lock was free.
5. **`BOSS_OFF.txt` was not exercised in a game run** (the machine's run lock was taken by
   another agent as soon as the suite finished). It goes through the same `Toggles::Load` table
   and the same `!present` logic as every other switch, and `sync.txt` reports `off:1` when it
   is present, so `grep 'boss=.*off:1' sync.txt` in both coop dirs is the one-line check that
   would close it. Everything in `boss::Serialize` / `boss::OnPeerLine` / `EnsureHooks` is
   behind that flag.

## 8. Why each failure mode is visible in the log

| Symptom | Log line |
|---|---|
| a wrong UFunction parameter name (silent no-op) | `Params: <fn> has no parameter '<name>'` in `ms2coop-native.log` — grepped after every run, none produced by this change |
| the hook never resolves | `bossphase: phase hooks 0 of 2 resolved (retrying; the flag poll still covers it)` + `hook:` line in the native log |
| the game's gate refuses the phase | `WARN bossphase: <key> did not reach phase N (hp=.. thr=..) -- retrying`, then `GAVE UP ... after 12 tries` (`fail:` in sync.txt) |
| the peer has no copy of that boss | `bossphase: no local boss for <key> (<cls>) -- phase N kept unapplied` (`missed:` in sync.txt) |
| the peer echoes the phase back | `pub:` on the receiving side would be non-zero; `echoes:` counts suppressed flips |

Cosmetic quirk worth knowing: `bossphase mkcomp` logs `bosses 0 -> 0` when it reports the scan
taken in the **same** call — `GetOwner()` on a component created by `AddComponentByClass` is not
resolvable until the next tick, so the scan skips it there. The component is real (the log line
right after it, and the following tick's scan, show `n:1`); the line is a diagnostic, not a
result. Do not read `0 -> 0` as a failure.

## 9. Files produced

* `native/build/dxgi.dll` — the DLL tested here (deployed to E and F).
* `boss-lab-boss4.out.txt` — the lab wrapper's output; the screenshots it took are in
  `local\lab\20261026-014747-boss4\` (with `contact-sheet.png`).
* `run-two-r7boss-2.txt` — the full suite log behind §5.1.
* `build.log` — the build that produced the tested DLL.


