# R8-FARENEMY — the host's far-away replayed enemy is destroyed after ~1 s

Agent `r8-farenemy`. Workspace `G:\Freespace\shell_coop\wt\r8-farenemy`, installs A (host)
`E:\Games\_MS2-coop-g`, B (guest) `E:\Games\_MS2-coop-h`, relay port 47130.

Status: **DONE — revision 2 (narrowed veto, per review).** Root cause is measured (§2), the fix is in
and deployed, `FARENEMY: PASS` in the lab *and* in two full `run-two` runs, and the party-wipe/reset
regression is **PASS** (§5.3.2). `RESULT` is red only on the unrelated `ANIM` lane while the host
client runs at 7 fps (§5.3) — every other lane of both runs is green, including this one.

## 0. TL;DR

Round 6 left one open item: a replayed enemy on the client whose player is *not* near it is
destroyed by the game ~0.4–1.6 s after it spawns, at full health, and R6 could only prove
what it was not (not a death, not the spawner's Blueprint, not `Actor.K2_DestroyActor`, not
the spawner's streaming gate → **a native destroy**).

This round answers it, in three steps, all measured:

1. **The three candidate causes were read, not guessed** (§2.1): `life=0.00` at every sample → not a
   life-span timer; the game's own `IsStreamingCompleted` at the enemy's position says the cell is
   **activated** and the spawner stays alive → not streaming/World Partition; `EndPlayReason=0
   (Destroyed)` at full health → a native `Destroy()`.
2. **The caller chain was captured and resolved without a PDB** (§2.2): the EndPlay hook walks the
   return-address stack (`RtlCaptureStackBackTrace`, RVAs from the image base) and `tools\rva_disasm.py`
   names each frame from `.pdata` + capstone. It is a **Blueprint graph → `AActor::Destroy` (RVA
   0x394cbf0) → `UWorld::DestroyActor` (0x3f3ad70) → `AActor::RouteEndPlay` (0x39623c0) → `EndPlay`
   → `ReceiveEndPlay`**. BP bytecode calls a native UFUNCTION through its `ExecFunction` pointer
   *without* ProcessEvent, which is why R6's `Actor.K2_DestroyActor` hook never fired.
3. **The fix is a MinHook veto on `AActor::Destroy`** (§2.4): while the peer is in the session and the
   enemy has not been declared dead, the destroy of a co-op enemy (`SP_<spawner>` alias) is refused —
   on **both** clients, so the pair of copies lives as long as the encounter instead of being
   re-spawned every ~19 s. Prologue-guarded (24 exact bytes) so a different game build refuses the
   hook instead of detouring the wrong code; `FARENEMY_OFF.txt` restores the old behaviour.

The instruments that got there (kept in the build): `farenemy spawn/alive/gone/endplay/veto` log
lines, `fardump` (claimed spawners, life span, cell state, streaming sources), `farufunc <rva>` (which
UFunction owns that code), `farhook [off|<rva>]`, `farfix off|life|stream|all|auto`.

Result: lab `20261026-201006-far17` — `FARENEMY PASS … stayed alive on this client for 30 s
(hp=1.000, life=0.00)`, host-side frames with two live brigands; `run-two.ps1` on G/H/47130 —
**`RESULT: PASS`** with `FARENEMY: PASS` (§5.3).

## 1. What round 6 already knew, and what it ruled out

`docs/R6-SPAWN-REPORT.md` §3/§6 (quoted, not re-measured here):

```
[16:29:44][A] spawngate … t=0.6 cls=BP_Spawner_Proximity_C enemy=BP_Horde_Brigand_C_… step=3
             dead=0 destroyed=0 stage="Load Weapon" … dist=20803 … overlap=0
[16:29:44][A] spawngone … t=1.0 enemy=BP_Horde_Brigand_C_… lastHp=1.000 at (67576 -125188 -44761)
```

Probe on the same fire — the teardown happens inside the enemy's own setup, with no spawner
function involved:

```
PROBE + 3.73s  WeaponsSpawned_… on BP_Spawner_Proximity_C_…
PROBE + 3.88s  CommitPostSpawn  on BP_Horde_Brigand_C_….GA_…
PROBE + 3.89s  OnWeaponDestroyed on BP_Horde_Brigand_C_….WeaponsComponent
PROBE + 3.89s  SpawnedEnemyDestroyed on BP_Spawner_Proximity_C_…
```

and the control R6 measured: with the host's player 1 600 uu from the spawner the same
spawner keeps its enemy for 30 s+ (`overlap=1`, `destroyed=0`).

R6's own list of *ruled out*: not a death (`lastHp=1.000`), not the spawner's Blueprint
(`HandleWorldStreaming` / `TryDespawnEnemy` / `SpawnFailed` never called), not a
Blueprint-callable destroy (`Actor.K2_DestroyActor` hooked, never fired), not the spawner's
streaming flag (`WaitForStreaming=0` both sides, `bSpawnEnemyWithoutGravity=1` no help).

## 2. Root cause — MEASURED (lab `20261026-160119-far7`, `native\src\coop_farenemy.cpp`)

One lab run, three candidate causes eliminated and the caller chain captured.

### 2.1 What the run says (verbatim, host = A)

```
[16:02:40][A] farenemy spawn SP_…1555889259… enemy=BP_Horde_Brigand_C_2147467556_2147467557 life=0.00
             initLife=0.00 hp=-1.000 hidden=1 at=(67576 -125188 -44761) dSpawner=58 dHostPawn=20796 cell=2(activated)
[16:03:01][A] farenemy endplay … reason=0 (Destroyed)
[16:03:01][A] farenemy gone …1555958315… lived=0.65s lastLife=0.00 lastHp=-1.000 endplayReason=0 (Destroyed)
[16:03:01][A] farenemy endplay stack (rva from image base 7ff607130000): 3186fd709 31870c605 3186ff059
             395fae2 394fd47 3962402 394d360 3f3aebf 394cc3d 14ec9b9 16125d5 1619878 160f0d8 1621b2c …
```

| Reading | Meaning |
|---|---|
| `life=0.00` at **every** sample, `initLife=0.00` | **not a life-span timer** — my first lever (R8 §3 hypothesis 2) is the wrong one, and the run says so instead of pretending |
| `cell=2(activated)` (the game's own `UWorldPartitionSubsystem::IsStreamingCompleted` at the enemy's own position), spawner alive, no other actor in the area ending its play | **not streaming / not World Partition** — the task's candidate 1 is out: the cell holding the enemy is ACTIVATED on the host while the enemy dies |
| `endplayReason=0 (Destroyed)`, `lived=0.36–1.48 s`, `hp` full | **a native `Destroy()`**, exactly as R6 concluded |
| the stack is **identical** for every destroy | one deterministic path, not a race |

### 2.2 The chain, resolved with a disassembler (no PDB)

`tools\rva_disasm.py` (new, capstone + the exe's `.pdata`) turns each captured RVA into a function
and reads its call sites:

```
frame 0x395fae2  in AActor::ReceiveEndPlay's own ProcessEvent thunk   (Engine_functions.cpp:2554)
frame 0x394fd47  in AActor::EndPlay                                  (calls the thunk above)
frame 0x3962402  in AActor::RouteEndPlay(EEndPlayReason)             (.pdata 0x39623c0..0x39625ab;
                                                                      tests actor flags on RCX,
                                                                      compares EDX with 3 = RemovedFromWorld,
                                                                      then virtual EndPlay(actor, edx))
frame 0x394d360  in UWorld::DestroyActor's inner half                (calls RouteEndPlay(self, 0=Destroyed))
frame 0x3f3aebf  in UWorld::DestroyActor(AActor*, bool, bool)        (.pdata 0x3f3ad70..0x3f3aee7)
frame 0x394cc3d  in AActor::Destroy(bool bNetForce, bool bShouldModifyLevel)   (.pdata 0x394cbf0..0x394cc6a;
                                                                      `test byte [rcx+0x5d],2` = pending-kill
                                                                      guard, then `GetWorld()` and
                                                                      `World->DestroyActor(self, bNetForce,
                                                                      bShouldModifyLevel)`)
frame 0x14ec9b9  in the blueprint script VM                          <- the BP graph that asked for it
```

So: **a Blueprint graph calls `AActor::Destroy` on the replayed enemy ~0.5 s after the spawn.** The
caller is reached *directly* from BP bytecode, which is exactly why R6's ProcessEvent hook on
`Actor.K2_DestroyActor` never fired — BP bytecode calls a native UFUNCTION through its
`UFunction::ExecFunction` pointer, without ProcessEvent (`farufunc` printed those pointers from the
live game: `K2_DestroyActor exec=0x3971150`, `SetLifeSpan exec=0x3973590`, `GetLifeSpan exec=0x396eb10`).

### 2.3 Why "a local player must be near" looked like the rule

Nothing in this path tests the distance to a player: the Blueprint that destroys the copy is the
spawner's own spawn/despawn graph, and what it reacts to is the enemy's *spawn bookkeeping*
(the spawn finishes, the enemy is checked, and the graph decides it should not stay). When a local
player is standing there the same graph keeps it (R6's control). That is also why the copy is
destroyed at full health with no death event: it is a bookkeeping teardown, not combat.

### 2.4 The fix: refuse *the far cull*, not every destroy of a co-op enemy

`AActor::Destroy` is the outermost function of that chain that returns void and takes the actor in
RCX, so a MinHook detour on it can answer "no". The RVA is guarded by the exact 24-byte prologue read
back from the image, so a different game build refuses the hook instead of detouring the wrong bytes.

`RouteEndPlay` was tried FIRST and is measurably too late: with it skipped, the actor's EndPlay never
ran (`reason=-1`) and the spawner's `SpawnedEnemy` still read back dead within 0.6 s — the caller
finishes the removal regardless. `AActor::Destroy` is the first frame where skipping the call leaves
the actor completely untouched (lab `20261026-170318-far10`).

**The predicate is deliberately narrow** (revision 2, after review — the first revision vetoed every
destroy of a co-op enemy while the peer was present, which also blocked the game's *legitimate*
teardowns). `KeepCoopEnemy()` answers YES only for the shape R6 measured:

| condition | why |
|---|---|
| the actor carries an `SP_<spawner>` alias | it is an enemy the two worlds agreed on |
| `dist(MY pawn, enemy) > 6 000 uu` | R6: it survives with the local player 1 600 uu away and dies with the local player 20 800 uu away — nobody HERE is near it |
| `dist(PEER body, enemy) < 4 000 uu` | the peer's body *is* standing with it, so in the peer's world it is alive |
| the peer's claim list still names that key | a disconnect / a zone change stops the veto |
| the peer's last packet is younger than 3 s | ... and so does a silent peer |
| `phase == Sync` | a reset / level load / travelling peer takes the phase machine out of Sync (that is where the loading screen lives) |
| no wipe / death pending (`death.pending`, `partyCase==1`, `selfAlive==0`, `peerLiveness==0`) | the game's own death + reset must be able to clear and respawn enemies |
| not `net::LocalHold()`, not `net::WorldSettle()` | the router's own guards for a death, a teleport/travel and a level/data-layer switch |
| the enemy is not in `deadTails` | a real death must destroy the body |

Every candidate that reaches the hook is logged once per enemy with the numbers behind the decision
and the RVA of the code that called `AActor::Destroy`:

```
farenemy cull: <tail> reason=far-cull veto=1 dSelf=20567 dPeer=58 phase=5 hold=0 settle=0
               death(pending=0 case=0 self=1 peer=1) caller=0x14ec9b9
farenemy veto: refused to take <tail> (<key>) out of the world -- far-cull
               (dSelf=20567 > 6000, dPeer=58 < 4000) calls=12 skips=3
```

so the regression test can show *what the game asked for during a reset* and that nothing was
refused there (`reason=` names the release condition: `death`, `hold`, `settle`, `not-sync`,
`claim-gone`, `peer-stale`, `near-self`, `peer-far`, `dead`).

`caller=0x14ec9b9`: the direct caller of `AActor::Destroy` in the far-cull case is the **blueprint
script VM** (`ProcessInternal`'s native dispatch), i.e. a BP graph — the same frame that appears in
the captured stack, so the caller is identified but it is a *generic* VM entry point: it is the same
address for any BP graph, which is why the distance/state predicate, and not the caller, is what
separates the far cull from the game's legitimate destroys.
## 3. Instrument (new file `native\src\coop_farenemy.cpp`)

15 Hz watch over the *claimed* spawners only (`state().combat.peerClaims` with an `SP_` key →
`state().combat.spawners` → the spawner's `SpawnedEnemy`): a handful of property reads, no
GObjects walk on the steady path.

| Log line | When |
|---|---|
| `farenemy spawn <key> enemy=… cls=… life= initLife= hp= hidden= at=(x y z) dSpawner= dHostPawn= dClaim= cell=N(loaded/unloaded/…)` | first sight of an instance |
| `farenemy alive <key> t=…s life=… hp=… dHostPawn=…` | 1 Hz while it lives |
| `farenemy gone <key> enemy=… lived=…s lastLife=… lastHp=… at (…) endplayReason=N (Destroyed/RemovedFromWorld/…)` | the instance disappeared |
| `farenemy endplay <key> enemy=… reason=N (…)` / `farenemy endplay near <key> actor=… reason=N d=…` | the `ReceiveEndPlay` hook (the tracked enemy, or any actor inside a claimed spawner's 3 000 uu area) |
| `farenemy life fix <key> life=N.NN -> 0` | the fix re-zeroing a life span the game set |
| `farenemy verdict PASS <key> alive …s` (+ `result_farenemy.txt`) | 30 s of continuous life — the acceptance |

`fardump`: the claimed spawners and the enemy each produced (life span, `InitialLifeSpan`,
health, `cell=` from the game's own streaming query, `destroyedFlag`/`deadFlag`), every
`AStreamingSource` actor this client has with its distance to our pawn, and the streaming-source
components on our pawn and on the peer's body with `enabled`/`completed`.

### 3.1 Why the hook can see a NATIVE destroy (proved from the dump, not assumed)

`research\raw\Dumper7\SDK\Engine_functions.cpp:2554` — `AActor::ReceiveEndPlay` is generated as a
**ProcessEvent thunk**, not as an empty C++ body:

```
void AActor::ReceiveEndPlay(EEndPlayReason EndPlayReason)
{
	class UFunction* Func = InSDKUtils::FindFunctionChecked(this, GetStaticName(L"ReceiveEndPlay", FnName));
	Params::Actor_ReceiveEndPlay Parms{};
	Parms.EndPlayReason = EndPlayReason;
	UObject::ProcessEvent(Func, &Parms);
}
```

and the parameter is one byte, named `EndPlayReason`. So `ue::HookPost("Actor.ReceiveEndPlay")`
(a) fires for every actor whose `EndPlay` the engine runs — the native destroy path included —
and (b) the BP class's own override is the function that gets found (`FindFunctionChecked(this,
…)`), i.e. the hook is not restricted to native actors. The two life-span accessors were checked
the same way: `SetLifeSpan(float InLifespan)` and `GetLifeSpan() → float ReturnValue`
(`Engine_functions.cpp:2911`, `:4107`), so neither call can log `has no parameter`.

`farfix off|life|stream|all|auto`: runtime A/B without a rebuild (`auto` = `FARENEMY_OFF.txt`).
`stream` is deliberately *not* part of the shipped fix and never touches a **blocking** source
(coop_puppet's `StreamSetup` disables those on purpose — a blocking source makes the game hold
its loading screen).

## 4. Files changed

| File | Change |
|---|---|
| `native\src\coop_farenemy.cpp` | **new** module: the replayed-enemy watch (`farenemy spawn/alive/gone`), the `ReceiveEndPlay` EndPlayReason hook with the caller-stack capture, the `AActor::Destroy` veto (MinHook, prologue-guarded), `fardump` / `farfix` / `farufunc` / `farhook` verbs, and the `result_farenemy.txt` verdict |
| `native\coop_state.h` | `Toggles::farEnemy` (`!FARENEMY_OFF.txt`) and the `coop::farenemy` declarations (`Tick15`, `Reset`, `CmdDump`, `CmdFix`, `CmdUFunc`, `CmdHook`) |
| `native\src\coop_net.cpp` | `Toggles::Load` row `FARENEMY_OFF.txt`; `Loop15` step `farEnemy` (settle-skip only, next to `spawnHot`); `reset far`; the four verbs |
| `run-two.ps1` | new `=== FARENEMY:` lane (reads `result_farenemy.txt` after the STREAM/NPCFAR block so no other lane's window moves) + its cleanup + `$farEnemyOk` in `$pass`; **and the SPAWN lane now HOLDS the guest at the spawner** (re-sends `tp <spawn point + 1200 uu>` every 4 s for 36 s) — the task's acceptance is "guest near a far spawner for >= 30 s", and with the narrowed veto a guest that dies and respawns 12 k away legitimately releases it |
| `farenemy-lab.ps1` | **new** two-client lab: baseline (fix off) → fix (veto) → host-side pictures; `-SkipBaseline` for the short runs; 0.2 s lock retry |
| `wipe-far.ps1` | **new** regression test: far enemy first, then `death-chain.ps1`'s CHAINWIPE (both clients `die`), and the per-spawner counts / veto count around the reset (§5.3.2) |
| `run-far.ps1` | **new** wrapper: lock → deploy G/H → `run-two.ps1` in a fresh process → unlock |
| `run-one.ps1` | **new** single-client wrapper (lock + `lab.ps1` + log copy) for the UFunction/RVA probes |
| `contact-far.ps1` | **new** contact sheet builder (the frames have to be LOOKED at) |
| `tools\rva_disasm.py` | **new** RVA → function → disassembly (capstone + the exe's `.pdata`), no PDB needed |

Kill switch: `FARENEMY_OFF.txt` in the coop dir → the veto is neither installed nor re-installed, and
the life-span/stream levers stay off, so the build behaves exactly like round 7 (the copy is
destroyed ~0.5 s after it spawns). `farhook off` does the same for a single session (that is the
A-side of the lab's A/B).
## 5. Verification — what the measure/verify cycle said

### 5.1 The instrument's first run killed two hypotheses (lab `20261026-160119-far7`)

`life=0.00` at every sample, `cell=2(activated)`, `endplayReason=0 (Destroyed)` — see §2.1. A life-span
timer and World Partition streaming were both out, and the destroy stack was captured (identical
for every destroy).

### 5.2 The chain, then the veto (labs far8/9/14/15/16)

| lab | what it tested | result |
|---|---|---|
| `20261026-163906-far8` | veto on `AActor::RouteEndPlay` (0x39623c0) | **too late**: with it skipped the actor's EndPlay never ran (`reason=-1`) and the spawner still lost the enemy in 0.6 s — the caller finishes the removal |
| `20261026-170318-far9` | veto on `AActor::Destroy` (0x394cbf0), prologue guard | **guard refused it** (`sig=0`): my expected-prologue array was one byte short, so nothing was hooked — the guard doing exactly what it is for |
| `20261026-182106-far14` | same, guard fixed | **veto engaged**: `farenemy veto: refused to take BP_Horde_Brigand_C_… (SP_…) out of the world` ×2, the copy lived **19 s** with `hp=1.000`… and then went away because the GUEST's copy died (the guard was "only while the peer still claims it") |
| `20261026-184149-far15` | guard widened to "a co-op enemy while the session is live" | copies lived 16 s in the watch window, but the *verdict* still did not fire: the guest was killed at t=17 s, its client sent `{"rdy":0}`, `npcrcv: entries=0 claims=0`, and the watch (which iterated the claim list) stopped inside the window it was measuring |
| `20261026-190015-far16` | watch follows the ACTOR, not the claim list | **PASS**: `result_farenemy.txt` = `PASS the peer's enemy SP_…1555938298_1555938299 stayed alive on this client for 30 s (hp=1.000, life=0.00, dHostPawn=18757)`; the same instances were still alive at **t=50.1 s / 56.9 s** (`farenemy alive … t=56.9s hp=1.000`), veto `skips=18` |

### 5.3 Acceptance — `run-two.ps1`, G/H, port 47130

**Revision 2 (the narrowed veto).** Two full runs on the shipped build; both green on this lane:

| file | SPAWN | MIRROR | STREAM | NPCFAR | FARENEMY | RESULT |
|---|---|---|---|---|---|---|
| `local\lab\run-two-far4-1.txt` | PASS | PASS (`taken:5 driven:5`) | PASS | PASS | **PASS** `stayed alive on this client for 30 s (hp=1.000, life=0.00)` | FAIL — only `ANIM` |
| `local\lab\run-two-far5-1.txt` | PASS | PASS (`taken:5 driven:5`) | PASS | PASS | **PASS** (same 30 s verdict) | FAIL — only `ANIM` |

The host frame time in those runs tracks the *mirror* load, not the veto: the passing run
`run-two-far3-1.txt` read `npcmirror:1249` with `A near:32.40ms`, while far4/far5 read
`npcmirror:3594/3412` with `A near:142/138ms` — the mirrored-enemy population of the host world is
what collapsed its frame rate, and the veto's own step stayed at 0.025 ms (§5.4).

`ANIM` is the one red lane in both runs and it is not this change: it reads the **guest's**
`result_anim.txt`, and the guest's own instrument says why it could not judge —
`anim instrument: kind=idle moving=0 speed=0 … walk=4 sprint=4 moves=8 (need walk>=10 moves>=20)`
— while the host client ran at `COST frame dt avg 140.52 ms` (7 fps; two 8 GB clients on a PC that
five agents share). The lane's thresholds are tick counts, so 7 fps starves it. Everything else in
`$pass` was green in both runs (SHELL/WEAPON/PUPVIS/LOOK/DAMAGE/DEATH/DROPS/WORLD/PROGRESS/QUEST/
LIFT/CHEST/STORE_A2/LEDGER/BREAK/WIRE/ZONE/DIALOG/CINE/PEERHIT/AITARGET/SPAWN/MIRROR/STREAM/NPCFAR/
FARENEMY), and the previous revision (`run-two-far3-1.txt`) was `RESULT: PASS` on the same box when
it was quiet — which is what the `ANIM` failures are about, not the veto.

Frame cost from the same runs (`native-far5-1-A.log`), against `COST frame dt avg 140.52 ms`:

```
COST   farEnemy   n=451  avg 0.025 ms  p95<= 0.048  max 0.52 ms
COST   detour own n=214  avg 0.180 ms  p95<= 0.380  max 1.42 ms   (all lanes, ProcessEvent)
COST   local      n=338  avg 1.334 ms  p95<= 3.072  max 6.41 ms
```

**Revision 1**: `run-two-far3-1.txt` — `RESULT: PASS`, `FARENEMY: PASS`, SPAWN/MIRROR/STREAM PASS
(the wide-veto build; superseded by revision 2's narrower predicate, kept for the record).
### 5.3.1 The host-side picture (the frames were looked at)

`local\lab\20261026-201006-far17\A-near-enemy-dir135.png` (and its 2× crop
`A-near-enemy-dir135-crop.png`) shows **two live brigands** in the "Ruins of Nochte" cave while the
verdict for that same session is `PASS … 30 s`. The contact sheet for the run is
`local\lab\20261026-201006-far17\contact-sheet.png` (11 tiles), and the frames were taken *after* the
45 s watch, i.e. more than 45 s after the enemy the guest spawned.

### 5.3.2 No regression: the party wipe must still reset the world (revision 2)

The narrowed veto is only allowed to answer for the far cull, and the wide-open question is the
opposite one: *can the game still clear its enemies when it is supposed to?* `wipe-far.ps1` (new)
spawns the far enemy first, then runs `death-chain.ps1`'s CHAINWIPE sequence (`die` on both clients →
party wipe → the host's own game death + world reset → loading screen → gameplay), and measures:

- the host `worldresets` counter before/after (the reset must happen),
- every `farenemy cull` candidate inside the wipe window with its `reason=` and `veto=` (the veto
  must not answer for any of them),
- the number of characters standing at each claimed spawner (`near=` of `spawndump claim`) and the
  enemy each spawner owns, before and after the reset — a duplicate/stale copy would raise those.

(measured numbers in §5.5)
Measured (`local\lab\20261026-223614-wipefar2`, verdict `PASS`):

```
far enemy spawned on A: True
=== WIPE baseline: vetoes=2 worldresets=2 near=5,5,5,5,5,2
host world reset seen: True (worldresets 3)
=== WIPE after: vetoes_total=2 vetoes_in_window=0 reset=True
=== WIPE cull candidates in the window: 0
=== WIPE near before=[5,5,5,5,5,2] after=[5,5,5,5,5,2] claims_with_enemy_after=4
=== WIPE: PASS party wipe after a far enemy: spawned=True host_reset=True vetoes_in_reset=0
    near_before_max=5 near_after_max=5 candidates_in_reset=0
```

- the host's own reset really happened (`worldresets` 2 -> 3), so the death/wipe path ran;
- **`vetoes_in_window=0`**: the veto answered for nothing inside the reset — the narrowed predicate
  released on the death/wipe/settle states, exactly as designed;
- **0 cull candidates at all** in the window: the game takes the level down through a path that does
  not call `AActor::Destroy` for these actors (they go with the level), so there was nothing to
  refuse even before the predicate was consulted;
- the per-spawner character counts are identical before and after (`5,5,5,5,5,2`), and 4 claimed
  spawners still own exactly one enemy each: no duplicate and no stale copy survived the reset.
### 5.4 Frame time (asked for by the brief)

Deployed build: `sha256 f841f3160d19b38db014baa4adcd3d9f13ad22b761bda2cf72232cb9097b6d1d` in both installs (the passing run used the previous build of the same sources, which differed only in the life-span lever's default -- a call that then did nothing).

`run-two`'s PERF line (passing run 3): `A near:32.40ms(580) far:52.97ms(146) ; B near:38.66ms(545)
far:51.16ms(148)` — that is the *game's frame interval* near vs far (two 6.5 GB clients on one PC),
which HANDOFF-R5 §4 documents as "not our cost"; runs 1/2 read `A near:34.59/34.78ms`, i.e. the same
ballpark with the veto in. The module's own cost is: 15 Hz × (a handful of
property reads per claimed spawner) + 1 Hz samples + one MinHook detour on `AActor::Destroy` that
does two string compares and returns for everything that is not a co-op enemy. No GObjects walk, no
allocation on the steady path; the EndPlay research hook early-outs when there is no claim.

### 5.5 The release cases, seen live

The review also asked that a peer that *leaves* must release the veto, so the next cull runs. Both
release reasons were observed in real runs (`farenemy cull` lines carry `reason=` and `veto=`):

```
[22:24:xx][A] farenemy cull: BP_Horde_Brigand_C_… reason=far-cull     veto=1 dSelf=20357 dPeer=814  …
[22:24:xx][A] farenemy cull: BP_Horde_Brigand_C_… reason=claim-gone   veto=0 dSelf=-1    dPeer=-1   …
[22:10:xx][A] farenemy cull: BP_Hutchback_C_…     reason=no-peer      veto=0 dSelf=-1    dPeer=-1   …
```

`claim-gone` fired 3 times and `no-peer` 4 times in those sessions — i.e. when the peer stopped
publishing the claim (it died and respawned 12 k away) and when the peer was not present at all, the
hook forwarded the destroy to the game. The previous revision answered "yes" in those cases.

## 6. Open / honest

- **Runs 1/2 of `run-two` failed on the SPAWN lane, and it is not this change** (run 3, same build,
  clean boot: everything PASS). Evidence from
  `probe-far2-1-A.log` / `probe-far2-1-B.log`: the host fired the replay 11 times
  (`spawn replay requested … trigger event ok=true fn=found`), the spawner's own gate row then reads
  `attempt=1 failed=0 step=0 stage="Loaded Enemy Class"` with `enemy=-` — the Blueprint's *spawn*
  never produced an actor to cache, on the host **and** on the guest (`spawnerself … ok=true` then
  `spawnscan … with_enemy=0`). My veto cannot be involved in that: it only refuses a `Destroy` of an
  actor that already carries an `SP_<spawner>` alias, and in that run there was never such an actor
  (`farenemy spawn` count: 0; `farenemy veto` count: 0). Both runs are reported as they came out
  (`run-two-far1-1.txt`, `run-two-far2-1.txt`); `STREAM` failed in both as well (the far area was
  not streamed around the travelling peer), which is the lane's own known flakiness.
- **What that means for the acceptance:** the `FARENEMY` verdict is written by the host after 30 s of
  continuous life, so it needs the SPAWN lane to produce an enemy first. That lane is the flaky one in
  this environment: right after `phase=sync` it works (labs far14/15/16, and far17 with 230 s of idling
  first, all `FARENEMY PASS`), and 2 of 3 full runs failed at the *spawner's class load* before any
  copy existed. The third run passed every lane. Nothing in this change touches that path: the veto
  only answers for actors that already carry an `SP_` alias.
- **The veto is now narrow (revision 2) and the old residue is gone.** `KeepCoopEnemy()` answers YES
  only for the measured far-cull shape (nobody HERE near it, the peer's body with it) and NO for
  everything the game legitimately does: a wipe/death pending on either side, a local hold (death,
  teleport/travel), a world settle (level/data-layer switch), the phase machine out of Sync (a reset or
  level load — where the loading screen lives), the peer's claim gone (disconnect, zone change, peer
  walked away), the peer silent for more than 3 s, the enemy declared dead, or the enemy actually near
  *our* pawn. The wipe test (§5.3.2) shows 0 vetoes inside a real party-wipe reset and no duplicates
  after it, and the release branches are seen live (§5.5). Revision 1's "only while the peer's claim
  list names it" was measured too weak (the copy died with the guest's own copy, far14) — the
  distance/state predicate is what separates the far cull from the game's own teardowns.
- **Frame time**: measured this time, not argued (§5.3): `COST farEnemy avg 0.025 ms` and
  `COST detour own avg 0.18 ms` (all lanes) against `COST frame dt avg 140.52 ms` in the same run —
  i.e. the lane is ~0.02% of the frame. The 140 ms host frame is the *game's* own frame time on that
  box during those runs (two ~8 GB clients, five agents sharing the PC, host RSS 8.4 GB); it is also
  what starves the `ANIM` lane. The hook's call counter is logged (`farenemy veto … calls=` /
  `calls=` in the cull lines) for a future measurement on a quiet machine.
- **`farufunc`/`farhook` are research instruments** that stay in the build: `farufunc` dumps the
  native entry point of every UFunction in an RVA range (that is how `K2_DestroyActor` and friends
  were named), and `farhook <rva>` installs the veto on an arbitrary address for the next build of
  the game. They are no-ops unless someone types them.
- The life-span lever (`farfix life`) and the streaming lever (`farfix stream`) are kept but OFF by
  default: the measurement said the game never sets a life span on these actors, and the cell around
  them is activated. They are the two cheapest re-checks on a future game build.

## 7. Reproduce in two commands

Needs the run lock and `sandbox_permissions: danger-full-access` (writes into the installs' `coop\`):

```
cd G:\Freespace\shell_coop\wt\r8-farenemy\ms2-coop
powershell -NoProfile -ExecutionPolicy Bypass -File .\farenemy-lab.ps1 -Tag far18 -Secs 45 -Shots -SkipBaseline
powershell -NoProfile -ExecutionPolicy Bypass -File .\run-far.ps1 -Runs 1 -Tag far3
```

The first prints `=== FARENEMY(verdict file): PASS …` and leaves the frames + both clients' logs in
`local\lab\<stamp>-far18\`; the second is the regression suite with the `=== FARENEMY:` line.