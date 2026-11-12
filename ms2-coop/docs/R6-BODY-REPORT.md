# R6-BODY — the player's own body after a co-op respawn

Workspace: `G:\Freespace\shell_coop\wt\r6-body`. Installs: A (host) `E:\Games\_MS2-coop-test`,
B (guest) `E:\Games\_MS2-coop-b`, port 47010, lock `r6body`.
Code touched: `ms2-coop\native\src\coop_death.cpp` only (+778/−4 vs the main tree file, eight
hunks; the main tree's copy was read, never edited).

## 1. Root cause

The game never revives a dead player pawn. Our ladder therefore does what the game's own
death reset does: `UnPossess` the corpse and let `BP_SpartaGameMode_C.RestartPlayerAtPlayerStart`
spawn and possess a NEW `BP_PlayerCharacter_C` (that part works and is logged `NEW`). That new
pawn is complete — every component is attached to the capsule — but the game fills a player
pawn in from its own possession/save path, and a bare `RestartPlayerAtPlayerStart` never runs it:

```
[18:19:55][A] bodyprobe (cmd): mesh=CharacterMesh0 bones=164 ai=ABP_Player_C animClass=ABP_Player_C
              mode=0 weapons=7[WP_Kick_L_C,… WP_StoneSeal_C, WP_NailShotgun_C, WP_HadernsSword_C]
[18:20:08][A] RESPAWN fresh body: UnPossess=1 RestartPlayerAtPlayerStart(PlayerStart_UAID_… d=102)=1
              -> pawn BP_PlayerCharacter_C_2147466573_2147466574 (NEW)
[18:20:08][A] RESPAWN fresh body: before[mesh=CharacterMesh0 bones=164 ai=- animClass=ABP_Player_C
              mode=2 weapons=0[] dbgMove=0 dbgKeys=0]
[18:20:19][A] (the bare pawn, debugscan) … nothing but the standard components …
[18:20:24][A] bodyinit(5): before[mesh=CharacterMesh0 bones=0 ai=- animClass=ABP_Player_C mode=0
              weapons=4[WP_Kick_L/R, WP_Fist_L/R] …]
```

So two things are missing on the fresh pawn, and both are needed:

1. **the body itself** — `CharacterMesh0` has *no skeletal mesh asset* (`bones=0`) and therefore
   *no anim instance* (`ai=-`). Its `AnimClass` is already `ABP_Player_C`, but a class without a
   mesh never instantiates, and swapping the asset in later does not re-run the anim-script
   init. That is why the screen shows nothing where the player stands.
2. **the equipped weapon actors** — only the four unarmed defaults (`WP_Kick_L/R`, `WP_Fist_L/R`)
   are spawned; the three the player actually carries (`WP_HadernsSword`, `WP_NailShotgun`,
   `WP_StoneSeal`) live in `UWeaponsComponent.EquippedWeapons` and are spawned by the game's
   equip path, not by class defaults. This is also why `pupdump self` after the respawn showed
   `ai=-` and only Kick/Fist.

...and, as it turned out while fixing it, a third thing that only shows up on the HUD:

3. **the attributes** — the fresh pawn keeps the pawn-default max health (50), not the dark
   form's (75). With the copy-only repair the one death stack produced `MaxHealth 50 -> 45`
   (`45/75 = 0.6`) and `run-two-death -Phase case2` failed on exactly that:
   `=== DCASE2: B … maxhp 75 -> 45 (ratio 0.6) … === DCASE2: FAIL`
   (lab `20261025-193753-death-two`). The controller's `InitializeCharacter` is what puts the
   dark form's 75 there.

The camera and HUD were already restored by the existing presentation work: after the respawn
`selfcheck` reports `simulating=0 meshParent=CollisionCylinder deathCam=0 povToBody=226 hud=1
hudVisible=1 viewTarget=self`, and the death camera state is removed
(`COOP DEATH camera: removed CameraState_Death_C from the camera stack ok=1`).

## 2. The fix (diff summary, by function)

All in `ms2-coop\native\src\coop_death.cpp` — one new section,
**`PUTTING THE BODY BACK ON THE FRESH PAWN`** (line 838 onwards), plus small edits in four
existing functions and two forward declarations at namespace scope (four of the eight diff
hunks are the section, the rest are the edits below).

New in the section:

| function | what it does |
|---|---|
| `BodySnapshot g_bodyShot` | what the dead pawn carried: skeletal mesh asset, AnimBP class, weapon classes, and their names for the log |
| `CharMesh` | `CharacterMesh0`, else the pawn's first skinned component |
| `MeshAnimInstance` | `GetAnimInstance()` → the thing whose absence IS the invisible body |
| `WeaponsComp`, `EquippedWeaponClasses` | `ASpartaCharacter.WeaponsComponent` → `UWeaponsComponent.EquippedWeapons` as weapon **classes** |
| `BodyLine` | one line of facts: bones, anim instance, AnimClass, anim mode, weapon actors, debug flags |
| `BodyOk` | the verdict the ladder is steered by: an anim instance **and** more than the four unarmed weapon actors |
| `PropByte`, `DebugFlags` | read-only probe of the debug switches (they are logged around every repair) |
| `SnapshotBody` | read the three things above from the dead pawn **before** it is destroyed |
| `CallVoid1`, `CallBool1`, `GameInitStep` | one-argument calls, and the controller's own init verbs (`ReceivePossess`, `ActivateShellOrDarkform`, `InitializeCharacter`, `ActivateDarkForm`, `Load*`) |
| `ApplyBodySnapshot` | **the copy**: clear `AnimClass` → `SetSkeletalMeshAsset` → `SetAnimInstanceClass` (that is what *creates* the anim instance) → `SetVisibility(true)`/`SetHiddenInGame(false)` → `SpawnAndEquipWeaponOfClass` for every missing weapon class → `CreateMatInstances` |
| `ArmBodyFix`, `BodyFixDisabled` (`coop\BODYFIX_OFF.txt`), `RebuildBody` | the repair itself and its research switch |
| `ControllerItemName`, `BodyProbe` | `bodyprobe`: equipment definitions from the controller (read with the parameter names their signatures declare) + body facts |
| `DebugScan`, `DebugHide`, `BodyInit` | research verbs: enumerate every component with its visibility, hide shape-looking ones, and run one repair step by hand (`bodyinit 1..13`) |

Edits in existing functions:

| existing function | change |
|---|---|
| `StepFreshBody` | `SnapshotBody(oldPawn)` before destroying the corpse; the dead flag (`ClearDeathMix`) is cleared **before** `UnPossess` too (the controller's possession path does nothing while it is set); logs `before[BodyLine]`; and after the new pawn exists it repairs it **in this step** (`RebuildBody`) and re-runs the debuff tick (`Tick2`), because the stack is a percentage of the body the player ends up with and the death is published right after this returns; if the repair did not take, `ArmBodyFix(now + 1.0)` leaves it to `Tick15` |
| `Reset` | forget the snapshot, the pending repair and the pending flag read when the world changes |
| `Tick15` | the bounded repair driver: six tries 1.5 s apart, then it logs `giving up…` instead of hammering; it re-runs `Tick2` on success and logs the debug flags before the repair and again 20 s after it |
| `OnCommand` | `bodyprobe`, `debugscan`, `dbghide`, `bodyinit` |
| (declarations) | `ClearDeathMix`/`g_freshBody*` moved above `StepFreshBody`; `void Tick2();` declared at `namespace death` scope so the repair can call it |

The repair (measured, and the order is the whole story):

1. **`BP_PlayerController_C.InitializeCharacter`** — the call the controller makes for itself
   when it takes a pawn over. It does the *whole* job in one go: `bones 0 -> 164`,
   `ai=ABP_Player_C`, weapon actors `4 -> 7`, `CharacterId.Player` →
   `CharacterId.Player.Darkform.StrongOne`, and max health 75 (so one death stack reads 67.5).
   Its price, measured side by side on the same bare pawn, is that it also switches the game's
   **wireframe debug draw** on — see §5.
2. **the copy** (`ApplyBodySnapshot`) as the fallback: the same body (and a *clean* picture) but
   without the attributes, so it is only used when the game's own init does not produce a body.

## 3. Commands run and verdicts

Build (after every edit):

```
cmd /c G:\Freespace\shell_coop\wt\r6-body\ms2-coop\native\build.bat test
  coop_policy: 112 checks passed, 0 failed
  Built: G:\Freespace\shell_coop\wt\r6-body\ms2-coop\native\build\dxgi.dll
```

Each harness run below is one fresh process, under lock `r6body`, deploying the built
`dxgi.dll` to both installs:

| command | lab folder | verdict |
|---|---|---|
| `accept.ps1 -Run case2` (run-two-death `-Phase case2`) | `local\lab\20261025-202644-death-two` | `=== DCASE2: PASS B respawned in the live world, no reset, stacks=1, max health ~90%` · `RESULT: PASS` |
| `accept.ps1 -Run held` (run-two-death `-Phase hold`) | `local\lab\20261025-210006-death-two` | `=== DHOLD: PASS` (+ its three sub-verdicts) · `RESULT: PASS` |
| `accept.ps1 -Run wipe` (run-two-death `-Phase wipe`) | `local\lab\20261025-211908-death-two` | `=== DWIPE: PASS host reset, guest followed, both alive, 0 stacks` · `RESULT: PASS` |
| `accept.ps1 -Run visual` (the task's case-2 cmd list) | `local\lab\20261025-212023-r6body` | B's own body with its weapon and the HUD in `B-after-cmds1..4.png`; `pupdump self` = `ai=ABP_Player_C`, 8 attached actors (7 weapons + light rig) · **PASS** |
| `accept.ps1 -Run hold` (the task's boss-hold cmd list) | `local\lab\20261025-221620-r6body-hold` | the list ran end to end (`bossfake 1` → `die` → **victimCase=3 partyCase=3 boss=1** → held → `bossfake 0` → `hold released: respawn in 15.0s` → respawn) and the body is back in `B-after-cmds1..4.png` · **PASS** |

`accept.ps1` was extended for this (as instructed): `-Run hold` is the **visual** boss-hold
run, so the `run-two-death -Phase hold` verdict is spelled **`-Run held`**:

```powershell
'held'   { & (Join-Path $ws 'run-two-death.ps1') -GameA $A -GameB $B -Port $Port -Phase hold }
```

Two earlier case-2 attempts are kept because they are what the diagnosis is based on:
`20261025-193753-death-two` (copy-only repair: `maxhp 75 -> 45 (ratio 0.6)` → FAIL, which is
how the attributes problem was found) and `20261025-181816-m6` (the both-providers
measurement, §5).

`visual-two.ps1` had to be fixed for that last row: it wrote one command into `coop\cmd.txt`
every 4 s, and the mod **reads then deletes** that file (`RunCommands`, `coop_net.cpp`), so a
command written before the previous one was consumed is silently lost. That bit twice —
`20261025-212756-r6body-hold` (`bossfake 1` and `die` dropped, no death at all) and
`20261025-190316-r6body-hold` (`bossfake 1` dropped, the death ran as case 2) — and the loop now
writes a command and waits for the file to disappear (the consume acknowledgement) before the
next one. `run-two-death` was never affected: it drives its commands through its own waits.

## 4. Screenshots

All from `G:\Freespace\shell_coop\local\lab\`; each folder also has the two clients' full
`probe_A.log` / `probe_B.log` and `sync_*.txt` for the run.

**Case 2 (the task's cmd list) — final build, `20261025-212023-r6body`**

| file | what it shows |
|---|---|
| `B-after-cmds1.png` … `B-after-cmds4.png` | B's OWN body — dark form, `WP_HadernsSword` in both hands — right in front of B's camera, HUD with the four weapon slots, health bar at 90 % (the one death stack). Identical to the pre-death look apart from the wireframes of §5. |
| `contact-B.png` | those four in one sheet |
| `B-sees-A-idle.png`, `B-sees-A-move1..3.png` | the partner (A) visible from B |
| `A-after-cmds1..4.png`, `A-sees-B-dir0..315.png`, `A-sees-B-move1..6.png` | A's view of B's puppet: normal body, moving, no debug shapes on A's screen |

**Boss-hold release (the task's hold cmd list) — final build, `20261025-221620-r6body-hold`**

| file | what it shows |
|---|---|
| `B-after-cmds1.png` … `B-after-cmds4.png` | the same result as case 2: B's own body, dark form + `WP_HadernsSword`, in front of B's camera, HUD present, after the hold was released by `bossfake 0` |
| `contact-B.png` | those four in one sheet |
| `A-sees-B-*`, `B-sees-A-*` | the two puppets, both normal |

The repair at that release, from the same log:

```
[22:18:10][B] COOP DEATH hold released: respawn in 15.0s
[22:18:25][B] RESPAWN fresh body: UnPossess=1 RestartPlayerAtPlayerStart(… d=96)=1 -> pawn … (NEW)
[22:18:25][B] COOP DEATH body fix (fresh body): before[… bones=0 ai=- … weapons=4[Kick/Fist]]
[22:18:25][B] COOP DEATH body fix (fresh body): InitializeCharacter=1 -> mesh=CharacterMesh0
              bones=164 ai=ABP_Player_C animClass=ABP_Player_C mode=0 weapons=7[…]
[22:18:25][B] COOP DEATH debuff: MaxHealth 75.0 -> 67.5 (stacks 1, factor 0.90) ok=true
```

(The list's own `pupdump self` lands 15 s *before* the release respawn, so it dumps the corpse —
that is the cmd list's timing, not the repair: `attached actors=1`, `mode=2`. The body it
describes afterwards is in `B-after-cmds*.png` and in `run-two-death -Phase hold`'s log.)

**Diagnosis runs**

| folder | what it proves |
|---|---|
| `20261025-181816-m6\step01..06.png` + `contact-steps.png` | step01 the body before death; step02/03 the bare post-respawn pawn (no body, no wireframes); step04 after the **copy** (body, clean); step05 after **InitializeCharacter** (body + wireframes); step06 bodyprobe |
| `20261025-185215-r6body` | case 2 with the copy-only repair: body visible, `contact-B.png` |
| `20261025-190316-r6body-hold` | first boss-hold visual attempt (`bossfake 1` was lost to the harness's cmd-file race, so the death ran as case 2) |
| `20261025-202644-death-two`, `20261025-210006-death-two`, `20261025-211908-death-two` | the three `run-two-death` phases' evidence |
| `20261025-193753-death-two` | the copy-only FAIL that found the attributes/health problem |

## 5. Open items

**RESOLVED in round 2 — the wireframe debug draw.** It was our own visibility call: `SetVisibility(true, bPropagateToChildren=true)` / `SetHiddenInGame(false, bPropagateToChildren=true)` on `CharacterMesh0` propagated to the weapon actors attached to its sockets and **un-hid their normally-hidden-in-game components** (the hit-shape `CapsuleComponent DefaultCollisionShape`, `ArrowComponent WeaponForward`, `SphereComponent TrailLocation`). Four calls now pass `bPropagateToChildren=false` and the respawned body is clean. Full before/after in §7.

The measurements below are kept because they are what made the earlier rounds point at the game's
own init instead of at our own call — the shapes only appeared *after* a pass that propagated to
already-attached children, and the debug *switches* (targeting/ledge/weapon enums) were genuinely
off, so nothing there looked guilty.

* `20261025-181816-m6` ran the two providers on the same bare pawn and photographed each:
  `step04.png` (after `bodyinit 5`, the copy) is **clean**; `step05.png` (after `bodyinit 3`,
  `InitializeCharacter`) has the shapes — because `InitializeCharacter` spawns the weapon actors
  first and our presentation pass ran afterwards on a later session.
* they are **not components**: `debugscan` (`20261025-181816-m6`) lists every component of the
  pawn and of each weapon actor and there is nothing debug-shaped to hide — so they are
  `DrawDebug*` calls from BP code, driven by a switch.
* the switches that were checked are **off**, both on the bare pawn and 20 s after the
  repair: `COOP DEATH body debug flags (20 s after the fix): DebugMovement=0
  bEnableDebugKeys=0 targeting:CurrentDebugDuration=-1.00 targeting:CameraAbsoluteRadius=5.00
  targeting:Camera Absolute Length=3500.00 strafe:LedgeDetection_Debug=0`
  (`20261025-212023-r6body`). No weapon actor in this loadout has a
  `TraceDebug`/`DrawDebugType`/`DebugTrace` property at all.

So the lead the earlier rounds reported was wrong, and the round-2 measurement says why: the
shapes were the weapon actors' own hidden-in-game components, whose `vis`/`hiddenInGame` were
flipped **by our propagation**, not by a game debug switch.

Nothing else is open in the code. One thing is worth the owner's attention and is **not**
caused by this change: the death penalty takes B's gloom (4021 → 0) because the host's rules
carry `payPenalty=1` — see §6/§7 for the evidence.

The harness bug found on the way (a `cmd.txt` command written before the mod consumed the
previous one is lost) is fixed in this workspace's `visual-two.ps1`; `run-two-death.ps1` and
`lab.ps1` were not affected.

## 6. Verdict lines

### run-two-death — `-Phase case2` (`local\lab\20261025-202644-death-two`)

```
=== LINK: PASS both clients in gameplay, peer seen
=== DCASE2 baseline: B maxhp=75 worldresets A=2 B=2
=== DCASE2 case: PASS the death was decided as case 2
=== DCASE2: B me:1(alive) case:2 victim:2 serial:1 stacks:1 peer:1(seen) boss:0 pending:0
    suppressed:0 worldresets:2 … maxhp 75 -> 67.5 (ratio 0.9) worldresets A 2->2 B 2->2
=== DCASE2: PASS B respawned in the live world, no reset, stacks=1, max health ~90%
RESULT: PASS
```

and the repair that produced it, in the same second as the respawn:

```
[20:26:43][B] RESPAWN fresh body: UnPossess=1 RestartPlayerAtPlayerStart(… d=102)=1
              -> pawn BP_PlayerCharacter_C_2147468340_2147468341 (NEW)
[20:26:43][B] COOP DEATH body fix (fresh body): before[… bones=0 ai=- … weapons=4[Kick/Fist]]
[20:26:43][B] COOP DEATH body fix (fresh body): InitializeCharacter=1 ->
              mesh=CharacterMesh0 bones=164 ai=ABP_Player_C animClass=ABP_Player_C mode=0
              weapons=7[WP_StoneSeal_C, WP_NailShotgun_C, WP_HadernsSword_C, Kick/Fist]
[20:26:43][B] COOP DEATH debuff: MaxHealth 75.0 -> 67.5 (stacks 1, factor 0.90) ok=true
[20:26:43][B] COOP DEATH resolved: the co-op respawn put the player back (case 2, stacks 1)
```

(The earlier copy-only build, `20261025-193753-death-two`, is the FAIL that found the
attributes problem: `maxhp 75 -> 45 (ratio 0.6)` … `=== DCASE2: FAIL`.)

### run-two-death — `-Phase hold` (`local\lab\20261025-210006-death-two`)

```
=== DHOLD: PASS B is held (case 3, pending)
=== DHOLDCAM: PASS the rendered camera follows the partner
=== DHOLD release: PASS B respawned once the fight was over
=== DHOLDCAM release: PASS the camera is back on B
=== DHOLD: PASS
RESULT: PASS
```

`B(after hold) … maxhp=67.5` and the same `body fix (fresh body): InitializeCharacter=1 ->
bones=164 ai=ABP_Player_C weapons=7` at the release, i.e. the hold release goes through the
same repair (`AdvanceRespawn` calls `StepFreshBody` for case 2 and case 3 alike).

### run-two-death — `-Phase wipe` (`local\lab\20261025-211908-death-two`)

```
=== DWIPE case: PASS a party wipe was seen
=== DWIPE: host reset A worldresets 2->4 ; guest did NOT reset its own (B 2->2)
=== DWIPE: both alive=True stacks A=0 B=0
=== DWIPE: PASS host reset, guest followed, both alive, 0 stacks
RESULT: PASS
```

### The visual list — `local\lab\20261025-212023-r6body`

Taken by looking at `contact-B.png` (all four `B-after-cmds*.png` in one sheet): B's own body,
dark form with `WP_HadernsSword` in both hands, filling the frame in front of B's camera, HUD
present, health bar at 90 %. From the same run's log:

```
[21:21:53][B] COOP DEATH body fix (fresh body): InitializeCharacter=1 -> mesh=CharacterMesh0
              bones=164 ai=ABP_Player_C animClass=ABP_Player_C mode=0 weapons=7[…]
[21:22:07][B] pupdump: [1] SpartaSkeletalMeshComponent CharacterMesh0 asset=- vis=1
              parent=CollisionCylinder mode=0 ai=ABP_Player_C animClass=ABP_Player_C
[21:22:07][B] pupdump: attached actors=8
```

`ai=ABP_Player_C` and 8 attached actors (7 weapons + the light rig) are exactly the pre-death
facts, which is the `pupdump self` acceptance item. `A-sees-B-*` show B's puppet normally from
A's side (no wireframes there — they are drawn per-client, on the repaired pawn).

### Inventory / gold, and no parameter warnings

**Inventory is unchanged and no item is duplicated or lost by the respawn**: `sync_B.txt` vs
`sync_B_after_cmds.txt` of the case-2 visual run differ only in position, speed and scan
counters, `ledger=ok keys:8` before and after, `bh=me: … heal3/3 pp1.000/1.000 ammo0/0 dark0`
identical, and the HUD keeps the same four weapon slots (`B-after-cmds*.png`).

**One number does change, and it is the game's own death penalty, not the respawn**: B's gloom
reads 4021 before the death (`20261025-181816-m6\step01.png`, same account) and 0 after
(`hud-currency-before-after.png` captures the HUD strip of `20261025-212023-r6body`), because
the host's rules carry `payPenalty=1` and the log says so at the moment of death:

```
[21:20:38][B] death: rules debuffs=1 maxStacks=5 hp=10% shell=10% delay=10s+5s/stack payPenalty=1 bossHold=1
[21:21:41][B] death(at death) penalty: bHasLostGloom=1 LostGloomAmount=4021 payPenalty=1
```

That is a solo death's penalty applied to a death that really happened; the body repair itself
never touches currency or inventory (it only writes the mesh, the AnimBP, the weapon actors and
`CreateMatInstances`). Tar (1212) is unchanged.

`grep -c 'has no parameter'` is **0** in both clients' logs in every run of this round (the only
ones ever seen came from `bodyprobe` reading the controller's `GetCurrent*` *events* as if they
returned a value — fixed by `ControllerItemName`, which checks the parameter's name against the
function before reading it).

## 7. Round 2 — the wireframe debug draw: cause, fix, verdicts

Owner's diagnosis, confirmed by before/after: the shapes were **our own visibility call
propagating to the weapon actors' normally hidden-in-game components**. `SetVisibility(true,
bPropagateToChildren=true)` and `SetHiddenInGame(false, bPropagateToChildren=true)` on
`CharacterMesh0` walk down to every component of every actor attached to its sockets, and after
`InitializeCharacter` the weapon actors are attached — so their hit-shape `CapsuleComponent
DefaultCollisionShape`, `ArrowComponent WeaponForward` and `SphereComponent TrailLocation`, which
are hidden in game on a normally spawned weapon, were drawn: a capsule round the player, arrows,
ellipses along the blade, exactly what the screenshots showed.

That also explains the measurements that pointed the wrong way in round 1: the calls only exist
on the respawn path (the pre-death pawn never gets them, hence a clean picture before death), and
in `20261025-181816-m6` the copy (`step04`) was clean only because the weapon actors were spawned
*after* the propagation had already run, while `InitializeCharacter` (`step05`) spawned them
before the next presentation pass.

**The fix** (four calls, `bPropagateToChildren=false`, in `ApplyBodySnapshot` and in
`Presentation`; the pre-change file is kept as `wt\r6-body\coop_death.cpp.pre-noprop`):

```cpp
CallBool1(mesh, "SetVisibility", "bNewVisibility", true, "bPropagateToChildren", false);
CallBool1(mesh, "SetHiddenInGame", "NewHidden", false, "bPropagateToChildren", false);
```

`dbghide` deliberately keeps propagation on: it is the research verb that hides *every*
shape-looking component, and it is exactly the blanket action that was not wanted as the fix.

### Build and verdicts (all fresh processes, lock `r6body`, deployed to both installs)

```
cmd /c …\native\build.bat test
  coop_policy: 112 checks passed, 0 failed
  PASS: all 4 patterns match exactly once and resolve to the recovered addresses
  Built: G:\Freespace\shell_coop\wt\r6-body\ms2-coop\native\build\dxgi.dll
```

| command | lab folder | verdict |
|---|---|---|
| `accept.ps1 -Run visual` | `local\lab\20261025-224834-r6body` | **PASS** — wireframes gone; `B-after-cmds1..4.png` show B's own body, sword in hand, four-slot HUD, health bar at 90 %; `pupdump self` = `ai=ABP_Player_C`, `attached actors=8`; 0 `has no parameter` |
| `accept.ps1 -Run hold` | `local\lab\20261025-225943-r6body-hold` | **PASS** — `victimCase=3 partyCase=3 boss=1` (held) → `hold released: respawn in 15.0s` → `InitializeCharacter=1 -> bones=164 ai=ABP_Player_C weapons=7`; clean body in `B-after-cmds1..4.png`; 0 `has no parameter` |
| `accept.ps1 -Run case2` | `local\lab\20261025-230433-death-two` | `=== DCASE2: PASS B respawned in the live world, no reset, stacks=1, max health ~90%` (`maxhp 75 -> 67.5 (ratio 0.9)`) · `RESULT: PASS` |

Screenshots to look at, in this order:

* `20261025-224834-r6body\contact-B.png` and `B-after-cmds1.png` — case 2, clean;
* `20261025-225943-r6body-hold\contact-B.png` — boss-hold release, clean;
* `20261025-224834-r6body\A-sees-B-*.png` — A's view of B's puppet (unchanged).

### Gloom on death — stated from evidence, not changed

* The **game** takes the carried Gloom and leaves it at the death spot; the mod only **reads** it.
  `grep` over `native\src\*.cpp` finds no write to `bHasLostGloom`/`LostGloomAmount`/`GiveGloom`
  anywhere; `LogDeathPenalty` is the only user, and it prints the game's own save-object fields at
  the moment of death: `death(at death) penalty: bHasLostGloom=1 LostGloomAmount=4021 payPenalty=1`
  (identical in all three round-2 runs).
* The HUD follows: 4021 before the death (`20261025-181816-m6\step01.png`) and 0 after
  (`20261025-212023-r6body\hud-currency-before-after.png`). It is the souls-like drop, not a
  destruction: `BP_SpartaPlayerSaveObject_C` records `LostGloomAmount`, `LostGloomLocation` and
  `LostGloomBiome`, and the spoils are a `BP_DeathSpoils_C` actor with `GiveGloom` that gives it
  back when picked up (the note above `LogDeathPenalty` records this from SYNC-DATA-MINE 9.1).
* `pay_death_penalty=1` (the host's rule here) means exactly "let the game's loss stand"; with
  `0` the build only WARNs that it cannot refund — pre-existing behaviour, untouched.

### Files touched in round 2

* `ms2-coop\native\src\coop_death.cpp` — four `bPropagateToChildren` flags (`false`), no other
  change; rebuild only.
* `ms2-coop\visual-two.ps1` — the command loop now waits for `cmd.txt` to be consumed before the
  next command (round 1; kept).
* `accept.ps1` — the `-Run held` mapping (round 1; kept).
* `contact.ps1` — contact-sheet helper (new).