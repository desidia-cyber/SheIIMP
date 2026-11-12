# Round 8 - going through a broken wall: syncing the traversal animation (`r8-wall`)

Workspace: `G:\Freespace\shell_coop\wt\r8-wall` - installs host A `E:\Games\_MS2-coop-i`,
guest B `E:\Games\_MS2-coop-j`, relay port **47140**.

Build: `cmd /c <ws>\ms2-coop\native\build.bat test` -> `coop_policy: 112 checks passed,
0 failed` + `Built: ...\build\dxgi.dll`. Both native logs grepped for `has no parameter` /
`MISMATCH` / `ERR `: none.

**Review round (this pass)** - the main agent rejected the first submission ("Montage_Play
by itself is not the real traversal, the sheet shows no squeeze"). What changed:

1. the game's OWN chain is now driven and measured: `AddWarpReferences` on the interaction
   plus its `GrantTraversalAbility` start the real squeeze, and the capsule really ends up
   past `WT_Exit` (section 2.1, log quoted);
2. both clients are staged at the real gap (`wallstage actor|observer...`) with an explicit
   `PlayerController.SetControlRotation` towards the gap, and the observer's burst now shows
   the puppet **in the squeeze pose inside the crack** (sheet path in section 6);
3. the `run-two.ps1` WALLANIM block moved before the long travel and switched to the real
   chain - it reports **PASS** (section 5.3);
4. the run then left the guest inside the crack and the tail of the suite failed; the block
   now restores the guest's position afterwards (fix in place, re-run blocked by the shared
   run lock - see section 5.3/5.4, reported honestly rather than claimed).

---

## 1. What the interaction actually is (measured, not guessed)

| Thing | Evidence |
|---|---|
| Destructible wall | `BP_BreakableWall_C` - `WallIsBroken@0x2E8`, `WallIsBrokenName@0x2E0`, `SqueezeThrough: TArray<TSoftObjectPtr<ABP_Interaction_Traversal_C>>`, `LockTraversals`/`UnlockTraversals`, `SpartaApplyHit`, `USpartaSaveComponent` (`research\raw\CXXHeaderDump\BP_BreakableWall.hpp`) |
| What a broken wall opens | `BP_Interaction_Traversal_SqueezeThrough_C` (and `_NoWeaponStow`): `TraversalAbility`, `IsDisabledDueToDamage`, `LinkedTraversals`, warp targets `WT_Entrance`/`WT_Exit` (`BP_Interaction_Traversal_SqueezeThrough.hpp`, `BP_Interaction_Traversal.hpp`) |
| The ability | `GA_Traversal_SqueezeThrough_C` (`SqueezeMontage`) / `GA_Traversal_SqueezeThrough_NoWeaponStow_C` (`Montage`) |
| The animation | `.../Traversal/SqueezeThrough/A_Shared_Traversal_SqueezeThrough_Montage` -> slot segment `A_Shared_Traversal_SqueezeThrough`, **3.50 s** |
| In the HUB test save | ONE squeeze interaction, `BP_Interaction_Traversal_SqueezeThrough_C_UAID_D4E98A7EF567F94A02_1315745516_1315745517`, ability `GA_Traversal_SqueezeThrough_NoWeaponStow_C`, `disabled=0`; **no `BP_BreakableWall_C` in the hub** (`wallscan: no BP_BreakableWall_C in this world`, same as R7-MAPS 7b.5) |
| The gap | `WT_Entrance (61691 -136067 -42537)` -> `WT_Exit (61663 -136014 -42537)`, length **60 uu** (`wallgap` verb) |

"Going through the broken wall" = play that montage on the acting client while its capsule
is carried through the 60 uu crack, and let the puppet do the same on the other client.

## 2. What runs on the acting client (probe, log lines quoted)

Three probe runs (`local\lab\20261026-140209-r8wall-probe1`, `...-141036-r8wall-probe2`,
`...-144607-r8wall-real1`) measured the candidate entries:

1. `ACharacter::PlayAnimMontage(Montage, rate, "None")` -> **0.00, nothing plays**
   ```
   [14:04:10][A] wallplay: anim PlayAnimMontage(A_Shared_Traversal_SqueezeThrough_Montage) len=0.00
   (then 6 s of wallprobe: cap=0 pos=0.000)
   ```
2. `UAnimInstance::Montage_Play(Montage, 1.0)` -> **3.50** (the call an ability task ends at):
   ```
   [14:12:25][A] wallplay: info (ability) Montage_Play(inst) -> 3.50
   [14:12:25][A] wallplay: info (ability) PlayAnimMontage(pawn) -> 0.00
   [14:12:25][A] wall: traversal start #1 seg=A_Shared_Traversal_SqueezeThrough rate=1.00 dur=3.50
   ```
   This is what the first submission used, and the main agent rightly called it "not the
   traversal": the montage plays, but nothing about the gap is involved.
3. `Event_Trigger()` on the interaction -> `ok=1` but nothing starts (the player was 342 uu
   away; the interaction needs the game's own focus/interact input).

### 2.1 The REAL traversal - `wallstage actor` + `wallreal`

The interaction's own entry points are driven, in the order the game uses them, and each
step is verified by the montage capture (`coop_wall_anim.cpp`, `RealTraversal`):

```
[14:48:03][A] wallreal: gap ent=(61691 -136067 -42537) exi=(61663 -136014 -42537) len=60 pawn before=(61805 -136259 -42443)
[14:48:03][A] wallreal: AddWarpReferences -> 1
[14:48:03][A] wallreal: GrantTraversalAbility -> call=1 montage=1
[14:48:03][A] wallreal: traversal RUNNING (/Game/.../A_Shared_Traversal_SqueezeThrough) pos now (61805 -136259 -42443)
[14:48:03][A] wall: traversal start #1 seg=A_Shared_Traversal_SqueezeThrough rate=1.00 dur=3.50
...
[14:48:16][A] wallprobe: t=0.02 own=(61584 -135863 -42451)     <- 490 uu later, 79/151 uu PAST WT_Exit
```

So:
* `BP_Interaction_Traversal_C.AddWarpReferences(pawn)` returns 1 (the two warp targets land
  on the player's motion-warping component);
* the interaction's own **`GrantTraversalAbility()`** is what starts the real squeeze (the
  interact input normally triggers it through the handler's gameplay event);
* the capsule is really carried **through the gap to the other side** (490 uu of travel,
  ending past `WT_Exit`), not just an animation played in place;
* `wallstage actor` puts the actor 2.2 m before `WT_Entrance`, facing the exit, and
  `wallstage observer|observer2|observer3|observer5` puts the observer at the gap (5.5 m to
  one side, or behind the exit) with `PlayerController.SetControlRotation` aimed at the gap
  centre (`wallcam: look at ... yaw=.. pitch=.. actor=.. cam=1`).

`coop_local`'s generic montage capture already sees the traversal
(`wallscan: capture=1 seg=.../A_Shared_Traversal_SqueezeThrough pos=...`), i.e.
`mseq/mpos/mrate` already cross the wire; the gap was on the observer's side.

## 3. Root cause (observer side) and the fix

`coop_puppet::UpdateAnimation` replays `PEER().montage` via `PlayMontage()`, and when that
returns false it **falls through to locomotion**:

1. **Hold the pose.** `PlayMontage` resolves the *segment* AnimSequence with
   `FindAsset(path, false)`; the traversal sequence is referenced only by its montage and
   the observer never plays that montage, so it can be absent -> `PlayMontage` fails -> the
   body walks/slides through the wall in its idle/walk clip.
2. **Follow the gap.** `DriveTick`'s 400 uu snap is right for spawns/teleports and wrong
   inside a gap: the capsule is warped through it, so a snap teleports the body through the
   wall instead of squeezing it.

The lane (`native\src\coop_wall_anim.cpp` + two marked edits in `coop_puppet.cpp`):

* acting client publishes **one new top-level key, LAST**:
  `,"wa":["<segment asset tail>",<rate>,<duration>,<seq>]`, while its own pawn is in a
  traversal (+1.5 s hysteresis), `seq` monotone so a lost/repeated packet is harmless;
* `UpdateAnimation`: while `wall::PeerTraversal()` it plays the peer's traversal segment
  (`w:<path>`) and **holds the pose**; if the asset is not loadable it says so once
  (`puppet traversal: segment not loaded here (...), holding the pose`) and still holds;
* `DriveTick`: during a peer traversal the 400 uu snap is skipped and the ease becomes
  `kTraversalEase = 0.60` (instead of 0.35) - a straight-line follow through the gap;
* observer verdict lines: `wall: peer traversal #n ...` and
  `wall: puppet traversal #n pose=<puppet anim state> gap=<1 when held>`;
* kill switch **`WALLANIM_OFF.txt`** (`Toggles::wallAnim`): no scan, no `wa`, no hold, no
  follow - round-7 behaviour.

## 4. Files changed

| File | Change |
|---|---|
| `native\src\coop_wall_anim.cpp` **(new)** | detection (`local::CaptureMontage` + `Traversal/SqueezeThrough` / `VaultOver`), `,"wa"` publish/parse, `Tick2/Tick30/Reset/Status`, observer pose bookkeeping + `WALLANIM` log lines, and the lab verbs `wallscan`, `wallplay anim|ga|auto|go|nearest|stop`, `wallprobe <secs>`, **`wallgap`**, **`wallstage observer|observer2|observer3|actor|observer4|observer5`**, **`wallreal`** (the real chain: gap read from `WT_Entrance`/`WT_Exit`, `AddWarpReferences`, `GrantTraversalAbility`, ASC fallback, camera via `SetControlRotation`) |
| `native\src\coop_puppet.cpp` | `[R8-WALL]` in `UpdateAnimation` (hold the traversal pose, never fall back to locomotion) and in `DriveTick` (no snap + `kTraversalEase`) |
| `native\src\coop_net.cpp` | `[R8-WALL]`: `wall::Serialize` last in `Publish`; `wall::OnPeerLine` in `ApplyPeer` (inside the zone guard, next to `brk`); `wall::Tick2` (2 Hz); `wall::Tick30` after `puppet::Tick30`; `wall::Reset`; `wall::Status()` (`wall=` in `sync.txt`); `WALLANIM_OFF.txt` row; cmd dispatch for all six verbs |
| `native\coop_state.h` | `[R8-WALL]`: `Toggles::wallAnim`, `kTraversalEase`, `namespace wall` declarations (section 14c) |
| `ms2-coop\run-two.ps1` | `[R8-WALL]` WALLANIM block, **moved before the long travel** and switched to the real chain (`wallstage actor` + `wallreal`), with the guest's position restored afterwards |
| `ms2-coop\wall-gap.ps1` **(new)** | the staging + screenshot harness: B squeezes for real at the gap while A (observer) takes a 12-frame burst, per side, then contact sheets |
| `ms2-coop\wall-two.ps1` (from the first pass) | the 8-direction search harness (kept; it is what proved the replay path) |

Wire discipline: new key, LAST, ~70 B while active and absent when idle, seq-numbered, no
existing key reordered.

## 5. Tests and verdicts

### 5.1 Observer replay of the REAL traversal - PASS (log)

`wall-gap.ps1`, output `local\lab\20261026-151458-wallgap4\` (and `...-145257-wallgap\`,
`...-150837-wallgap3\`):

```
=== A (= observer) probe_A.log
[15:14:5x][A] wallstage: observer placed=1 side=550 at (61191 -136297 -42477)
[15:14:5x][A] wallcam: look at (61677 -136040 -42497) from (61191 -136297 -42477) yaw=27.8 pitch=2.1 actor=0 cam=1
[15:15:0x][A] wall: peer traversal #1 seg=A_Shared_Traversal_SqueezeThrough rate=1.00 dur=3.50
[15:15:0x][A] wall: puppet traversal #1 pose=m:1:/Game/.../A_Shared_Traversal_SqueezeThrough ... gap=1
[15:15:2x][A] wall: peer traversal #2 ... / wall: puppet traversal #2 ... gap=1
=== sync_A.txt
wall=on own=0/- peer=2/- poses=2 holds=382
=== B (= actor) probe_B.log
[15:14:5x][B] wallstage: actor placed=1 at (61794 -136261 -42517) facing the exit
[15:14:5x][B] wallreal: AddWarpReferences -> 1
[15:14:5x][B] wallreal: GrantTraversalAbility -> call=1 montage=1
[15:14:5x][B] wall: traversal start #1 seg=A_Shared_Traversal_SqueezeThrough rate=1.00 dur=3.50 at=(61805 -136259 -42443)
```

The observer received the traversal state and **played the traversal segment on the puppet**
(`poses=2`, `gap=1`) both times, with the acting client's own log showing the game's
`GrantTraversalAbility` entry and the capsule leaving through the gap.

Earlier run `local\lab\20261026-142618-wall\` (the montage-only entry): `poses=7` of 8.

### 5.2 Visual (observer side) - what the frames actually show

`local\lab\20261026-151458-wallgap4\` (1600x900 clients):
`A-observer-gap01..12.png`, `sheet-observer.png` (12 frames),
`zoom-observer.png` (4x2 zoom of frames 1-8), `A-observer5-gap*.png`,
`sheet-observer5.png`, `zoom-observer5.png`;
`local\lab\20261026-150837-wallgap3\` has the clearest set (`zoom-observer.png`).

Honest reading of `...-150837-wallgap3\zoom-observer.png`:
* f01/f02 (+0.0 s, +0.3 s): the crack in the rock, no body;
* **f03 (+0.7 s): the puppet appears INSIDE the crack in the squeeze pose** - hunched,
  turned side-on, arms forward against the rock, sword still in hand;
* **f04 (+1.0 s): it is further into the gap and turned away**, upper body already through;
* f05-f08: the body is gone from this side of the rock (it passed to the far side).

So the observer sheet shows the puppet entering the gap in the squeeze pose and passing
through. It does **not** show the exit on the far side: the exit side is behind the rock,
and the two exit-side camera placements (`observer4` at 4.5 m, `observer5` at 9 m behind
`WT_Exit`) were both blocked by the observer's own third-person body filling the frame -
see open item 1.

### 5.3 Regression suite - `run-two.ps1` (A `_MS2-coop-i` / B `_MS2-coop-j`, port 47140)

| Run | WALLANIM | RESULT |
|---|---|---|
| first pass, montage-only trigger, block at the end | WARN (montage not loaded 91k uu away) | **PASS** (all other verdicts PASS) |
| review pass 1, block moved early, real chain | **PASS** (`actor=True peer=True pose=True`) | FAIL - the guest was left inside the crack and every later stage failed (STREAM/BREAK/WIRE/ZONE/DIALOG/CINE/PEERHP/PEERDEAD) |
| review pass 2 (fix: guest restored to `selfpos` after the block) | **not run yet** - see below | **not run yet** |

The last run's own lines:

```
=== WALLANIM: A on own=0/- peer=1/- poses=1 holds=183
=== WALLANIM: B on own=1/- peer=0/- poses=0 holds=0
=== WALLANIM: PASS (actor=True peer=True pose=True)
```

The WALLANIM verdict itself is PASS; the FAIL of that run is a harness-ordering effect of
the block (it teleported the guest 12.8k uu into a cave and never brought it back), which
is fixed by the `tp <selfpos>` restore added right after the block. The re-run could not be
taken: the shared `game.lock` was held by other agents for the whole window after I released
it, and lock discipline forbids waiting inside it. This is reported as PENDING, not as PASS.

### 5.4 `WALLANIM_OFF.txt` run - PENDING

Not taken (same lock shortage). The switch is wired (`Toggles::wallAnim`, read once at
start, gate at the top of `Tick2/Tick30/Serialize/OnPeerLine/PeerTraversal`), and with it
present the module is inert by construction - but "by construction" is not a measurement,
so this stays an open item.

## 6. Screenshot paths

```
G:\Freespace\shell_coop\local\lab\20261026-150837-wallgap3\zoom-observer.png   <- the squeeze pose, clearest
G:\Freespace\shell_coop\local\lab\20261026-151458-wallgap4\zoom-observer.png   <- same, 12-frame run
G:\Freespace\shell_coop\local\lab\20261026-151458-wallgap4\sheet-observer.png  <- 12 full frames
G:\Freespace\shell_coop\local\lab\20261026-151458-wallgap4\A-observer-gap01..12.png
G:\Freespace\shell_coop\local\lab\20261026-151458-wallgap4\sheet-observer5.png <- exit side (blocked, see 7.1)
G:\Freespace\shell_coop\local\lab\20261026-142618-wall\sheet.png               <- first pass, montage-only
G:\Freespace\shell_coop\local\lab\20261026-140209-r8wall-probe1\ , ...-141036-r8wall-probe2\ , ...-144607-r8wall-real1\
```

## 7. Open items

1. **The exit side is not framed.** Both exit-side camera placements were blocked by the
   observer's own third-person body. Next step: park the observer on a rock ledge/roof above
   the exit (a free camera does not exist in this title) or spawn a `CameraActor` and
   `SetViewTarget` it - the lane's `wallstage` already knows the exit point.
2. **Re-run `run-two.ps1` once with the guest-restore fix** (blocked by the shared lock) and
   report its `RESULT:` line; the WALLANIM verdict itself already reports PASS.
3. **`WALLANIM_OFF.txt` run** (blocked by the same lock): expect `wall=off`, no `wa`, and no
   `wall: peer traversal` on the observer, with the rest of the suite unchanged.
4. The interact input itself (the handler's gameplay event) is still not pressed; the chain
   is entered at the interaction's `GrantTraversalAbility`, one call below the input. That is
   the game's own grant function, and it is what makes the capsule traverse.
5. **No `BP_BreakableWall_C` exists in the hub** (`wall:0`), so the broken-wall half (wall
   state + `UnlockTraversals` opening this gap) is still proven only by round 7's `wx` lane.
6. `ACharacter::PlayAnimMontage` is refused (0) on this character while
   `UAnimInstance::Montage_Play` works - any future "play a montage on the local player"
   code must use the anim instance.

## 8. run-two.ps1 output (final blocks)

```
# first pass (montage-only trigger, block at the end)
=== WALLANIM: A on own=0/- peer=0/- poses=0 holds=0
=== WALLANIM: B on own=0/- peer=0/- poses=0 holds=0
=== WALLANIM: WARN (actor=False peer=False pose=False)
RESULT: PASS

# review pass 1 (block moved before the travel, real chain)
=== WALLANIM: A on own=0/- peer=1/- poses=1 holds=183
=== WALLANIM: B on own=1/- peer=0/- poses=0 holds=0
=== WALLANIM: PASS (actor=True peer=True pose=True)
RESULT: FAIL   (tail stages failed: the guest was left inside the crack; fix applied)

# review pass 2 (guest restore) - PENDING, see section 5.3
```
