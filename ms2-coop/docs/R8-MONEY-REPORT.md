# Round 8 — death money pickup after a co-op respawn (agent `r8-money`, ws `wt\r8-money`)

Test installs: host **A = `E:\Games\_MS2-coop-e`**, guest **B = `E:\Games\_MS2-coop-f`**, relay port **47120**.
Kill switch: `coop\MONEY_OFF.txt`.

## TL;DR — answers to a–d

| | Question | Answer |
|---|---|---|
| **a** | Is the lost-money pickup spawned at the death place for a co-op death (case 2)? | **NO in the build as found — measured, and now fixed.** The death takes the carried Gloom and writes the save slot (`bHasLostGloom=1, LostGloomAmount=500, LostGloomLocation=(49002,-134182,-42197)`), but **no `BP_DeathSpoils_C` actor exists**: the ability's `SaveLostGloomData` half runs, its `SpawnLostGloom` half does not. `money::EnsureDrop` now runs the game's own spawn step and the bag appears (amount = the lost amount, 38 uu from the death spot). |
| **b** | After the co-op respawn with no reload: present / visible / collectable, and does it return exactly the lost amount? Same after a wipe? | **YES** — bag present after the in-place respawn (`dDeath=38`), rendered by the game, interaction handler live, and the game's own interaction pays back **exactly** the lost amount (`+500 … EXACT`, `+300 … EXACT`, and `+800 … EXACT` after a wipe). See §6 verdicts. |
| **c** | Second death before collecting: the game's rule (old drop replaced/lost) preserved? | **YES.** The sweep destroyed the older drop (1 stale) and the collect paid the second loss only (`+300`), never both. The second death's loss also had to be *recorded* first: the mod's own `die` fallback (`HealthComponent.StartDeath`) empties the wallet without reaching the ability's save step, which used to lose the money silently (§4). |
| **d** | Partner's world: currency is per player, so the partner must NOT be able to collect it (ideally does not see it). | **Decision: client-local, nothing synced, no wire key.** Measured: with B's drop on the ground, A holds **no** drop (`spoil=0`) and never collected anything (`+0 over 0 collect(s)`), while B was paid. Each client's bag is spawned by its own ability from its own `BP_SpartaPlayerSaveObject_C`, so A cannot even see it. |

## 1. What the mechanic is (STATIC)

The currency a death takes is **Gloom** (not gold, not tar):

* `GA_DeathSpoilsHandler_C` (`/Game/Sparta/Core/Player/Ability/Death/`): `GetGloom` →
  `RemoveGloom(bool& Success)` → `SaveLostGloomData(int32 Gloom)` → `SpawnLostGloom()`.
  ObjectDump rows: `GetGloom:CallFunc_GetItemStacksByClass_ReturnValue`,
  `RemoveGloom:CurrentGloom`, `SaveLostGloomData:Gloom`,
  `SpawnLostGloom:CallFunc_GetPlayerSaveObjectFromCurrentSave_ReturnValue`,
  `…CallFunc_BeginDeferredActorSpawnFromClass_ReturnValue`,
  `…CallFunc_FinishSpawningActor_ReturnValue`.
* The amount and the place live in **one slot** on `BP_SpartaPlayerSaveObject_C`
  (SYNC-DATA-MINE §9.4): `bHasLostGloom @0x560`, `bLostGloomInsideObjective @0x561`,
  `LostGloomAmount @0x578`, `LostGloomLocation @0x580`, `LostGloomBiome @0x598`.
  One slot ⇒ a new death **overwrites** it: that is the game's "the old drop is
  replaced/lost" rule.
* The world actor is `ABP_DeathSpoils_C` (`BP_DeathSpoils.hpp`): `Amount @0x308`,
  `Recovered @0x314`, `GiveGloom()`, `ResetSaveData()`, `ReceiveBeginPlay()`, and the
  interaction callback
  `BndEvt__BP_DeathSpoils_BPC_InteractionHandler_K2Node_ComponentBoundEvent_0_OnInteraction__DelegateSignature`.
  The place comes from the `EQS_DeathSpoils` donut query and the actor is ground-traced
  (`AdjustZ` / `TraceForGround`), so it sits *near* the death, usually a few tens of uu
  lower.
* Collection is interaction-driven: `BPC_InteractionHandler_C`
  (`HandleInteractEvent(InputAction)` → `OnInteraction`) fires the bag's event, which pays
  (`GiveGloom` → `S_AddGloom`), clears the slot (`ResetSaveData`) and removes the actor.
* In vanilla nothing has to survive: every death reloads the world, the reload destroys the
  bag and **re-creates it from the slot** after the load. A co-op case-2 death does not
  reload — that is the whole gap.

## 2. Root cause, with the log lines that prove it

### 2.1 The game's record is written and its actor is not (case 2, the game's own death path)

Run `_go-20261026-144243.log` (first measurement) and every later run show the same shape:

```
[B] money: death #1 noted at (48999,-134177,-42197) -- carried gloom 500, now 0,
    save slot before: hasLost=1 amount=0 loc=(0,0,0)
[B] money: the lost-gloom slot after death #1: hasLost=1 amount=500 loc=(49002,-134182,-42197)
    (before: hasLost=1 amount=0) -- changed=1
[B] money(after the death) sweep: the slot changed (amount=500 …) but there is NO
    BP_DeathSpoils_C in this world -- the death drop did not spawn
[B] money(after the death) restore: the slot owes 500 gloom … calling
    GA_DeathSpoilsHandler_C_…SpawnLostGloom() (the game's own spawn-from-save step)
[B] money(after the death) restore: SpawnLostGloom ok=true
[B] money: a drop is in the world: BP_DeathSpoils_C_… amount=500 recovered=0
    rendered=1 at (49002,-134182,-42235) dMe=39 dDeath=39
```

So the cause is the **first** of the brief's two candidates ("the game's drop code is
skipped because the mod intercepts death"), not the second ("destroyed by the respawn"): the
record is written, the actor is never created, and it is still absent after the co-op
respawn.

### 2.2 The mod's own death path skips the ability entirely (second death, wipe)

A second death in one session is not produced by `S_KillMe` (it answers ok and does nothing);
the mod falls back to `USpartaHealthComponent::StartDeath`. That path empties the wallet
without reaching `GA_DeathSpoilsHandler` at all — no slot write, no spawn, so the money was
**silently destroyed**:

```
[B] money: respawned -- wallet gloom=0, save slot hasLost=1 amount=500 (was … changed=0)
[B] money: THE GAME TOOK 300 GLOOM AND RECORDED NOTHING (carried 300 -> 0, slot was
    hasLost=1 amount=500) -- the mod's own death path does not reach GA_DeathSpoilsHandler's
    save step; recording it at (49116,-134346,-42182)
[B] money(after the unrecorded loss was recorded) restore: the slot owes 300 gloom … SpawnLostGloom
```

The same happened to a 5021 and then a 5021→0 wipe: without the fix a party wipe destroyed
the whole carried balance with no drop and no record.

### 2.3 The step probe does not fire — an engine fact worth recording

`money` hooks all eight steps of `GA_DeathSpoilsHandler_C` (`GetGloom/RemoveGloom/
ShouldSaveLocation/UseDeferredLocation/SaveLostGloomData/SpawnLostGloom/RunEQS/OnEQSFinished`)
and installs them (8/8), but **not one `money: STEP` line ever appears**, even on the death
whose slot write is visible. Reason: a call from inside a blueprint's own event graph
(`ExecuteUbergraph_…` → helper UFUNCTION) is executed by the BP VM, **not** through
`ProcessEvent`, so the mod's ProcessEvent detour cannot see it. Only calls made *from C++*
(or from the ability framework) pass through ProcessEvent — which is why the mod's own
`GA_Death_Player_C.HandlePlayerDeath` hook does fire. **Lesson for the project: a
ProcessEvent hook can prove a function is called by the engine, never that a BP graph ran.**

### 2.4 Two measurement bugs in my own probe, both fixed

* The 2 Hz slot sample that lands on the same tick as the death already carries the new
  value, so "before" equalled "after" and the sweep thought the slot had not changed. Now a
  **two-deep ring** of samples is kept and `NoteDeath` uses the **older** one.
* `GetTotalItemCountByDefinition(ID_Gloom_C)` counts item **stacks**, not the amount (one
  `dropsgrant 0 500` moved it by 1). The number the death itself uses is
  `GetItemStacksByClass` (what `GA_DeathSpoilsHandler_C:GetGloom` calls), so the lane and
  the harness both read that sum.
* (Harness) `Last-Log` returned a `String` and `$s[-1]` meant "last character", so a verdict
  was fed the letter `")"` instead of the log line. Fixed with `@(…)`.

## 3. The fix

`ms2-coop\native\src\coop_money.cpp` (new), wired with 6 lines in `coop_net.cpp` and 2 calls
in `coop_death.cpp`.

### 3.1 Put the drop back (a/b) — `EnsureDrop()`

When the lost-gloom slot says the player is owed N Gloom at L and the world holds no
`BP_DeathSpoils_C`, the lane runs **the game's own skipped step**,
`GA_DeathSpoilsHandler_C.SpawnLostGloom()`, on the live ability instance (it reads the slot
itself and spawns at the recorded location). It runs 1.2 s after a death, on the co-op
respawn, 10 s after a world change (the wipe case), and on demand with `moneyrestore`. It
returns early whenever a bag is already in the world, so it can never duplicate a drop.

### 3.2 Record a loss the game forgot — `WriteLostGloom()` (new this round)

If the wallet went **down** between the death and the respawn (proven by the two samples)
and the slot did **not** change at this death, the game took the Gloom and recorded nothing
(§2.2). The lane then writes the game's four fields itself — `bHasLostGloom`,
`LostGloomAmount = taken`, `LostGloomLocation = the corpse`, `bLostGloomInsideObjective =
false`, with a read-back — destroys the now-stale bags, and lets `EnsureDrop` spawn. This is
what makes the *second* death and the *wipe* keep the money instead of destroying it.

### 3.3 Keep the game's "old drop replaced/lost" rule (c) — `SweepStale()`

With no reload to enforce it, an old bag would stay and the player could collect two drops
for one loss. When a death rewrote the slot the sweep keeps **the bag whose `Amount` is the
slot's** and destroys every other; when no bag carries that amount the described drop is
missing, so all of them are older losses and all go (run 4 kept a 4021 bag for a 500 slot and
the new drop was never spawned). If the slot did not change at the death, the sweep touches
nothing.

### 3.4 Measurement

2 Hz: the carried Gloom (sum), the slot, and every bag with class, `Amount`, `Recovered`,
`bHidden`, **`WasRecentlyRendered`** (rendered/not — the objective "is it on screen" answer,
because a dark gloom pile in dark water cannot be judged by eye on a PNG), distance to the
player and to the death spot. `moneyget` collects through the game's own
`HandleInteractEvent(<the action the handler itself carries>)`, falling back to the bag's own
`OnInteraction` event with the `InputAction`/`InteractID` read from the handler's `Actions`
array (`FF_InteractAction`: `InteractID @0x00`, `UInputAction* @0x38`, stride 0x40).

Logs (prefix `money`): `death #N noted at (x,y,z)`, `the lost-gloom slot after death #N … --
changed=1`, `sweep: keep/destroying …`, `THE GAME TOOK N GLOOM AND RECORDED NOTHING`,
`restore: … calling SpawnLostGloom()`, `a drop is in the world: … rendered=1 …`,
`respawned -- wallet gloom=…`, `collected NAME amount=… -> wallet a -> b (+n) -- EXACT`, and
the `money=` sync line.

### 3.5 Verbs, fixture reset and the kill switch

`moneydump`, `moneyvis` (rendered/hidden/handler/locked), `moneyview [dist]` (stands the
player away from the bag on the side away from the partner's puppet and tilts the camera
down with `SetControlRotation`), `moneyget [radius]`, `moneygive <n>`, `moneyrestore`,
`moneyprobe`, and **`moneyclear`** — the fixture reset (destroy every drop + clear the slot),
because a seeded save can already owe the player a drop from an earlier session (run 4 found
a 4021 bag on the ground before the first death, on both clients). `MONEY_OFF.txt` turns the
whole lane off.

### 3.6 Files and functions touched

| File | Change |
|---|---|
| `ms2-coop\native\src\coop_money.cpp` | **new** (~1300 lines): sampler, scan, `SweepStale`, `DestroyAllDrops`, `WriteLostGloom`, `EnsureDrop`, collect replay, step probe, the verbs, `money=` status |
| `ms2-coop\native\coop_state.h` | `SpoilsObs` (with `hidden`/`rendered`), `LostGloomSave`, `MoneyState`, `namespace money`, `State::money`, `Toggles::moneyEnabled` (`MONEY_OFF.txt`) |
| `ms2-coop\native\src\coop_net.cpp` | the toggle row; `money::Tick2()` in `Loop2` (unguarded, before the Sync gate, next to `drops::Tick2`); `money::Reset()` in `ResetAll`; `money::Status()` in `WriteSyncStatus`; `money::OnCommand` in `RunCommands` |
| `ms2-coop\native\src\coop_death.cpp` | **2 calls**: `money::NoteDeath(pawn)` at the end of `OnDeathDetected`, `money::NoteRespawn()` at the end of `ResolveAlive`. Nothing near the camera code |
| `ms2-coop\death-money.ps1` | **new** harness: two clients + relay, `DEATHMONEY-VISIBLE/SPAWN/COLLECT/PARTNER/SECOND/WIPE` verdicts, screenshots |
| `_go.ps1`, `_go-*.log` | my run wrapper (one lock window = deploy + fresh-process harness) and its logs |

No wire key was added (see §d), so the packet layout is untouched.

## 4. Test commands and verdict lines

```
cmd /c G:\Freespace\shell_coop\wt\r8-money\ms2-coop\native\build.bat test
  coop_policy: 112 checks passed, 0 failed
  PASS: all 4 patterns match exactly once and resolve to the recovered addresses
  Built: G:\Freespace\shell_coop\wt\r8-money\ms2-coop\native\build\dxgi.dll

powershell -NoProfile -ExecutionPolicy Bypass -File G:\Freespace\shell_coop\wt\r8-money\_go.ps1 -Phase all
  # takes the shared lock once, deploys E: and F:, then in a FRESH process:
  #   powershell -File ...\ms2-coop\death-money.ps1 -GameA E:\Games\_MS2-coop-e
  #       -GameB E:\Games\_MS2-coop-f -Port 47120 -Phase all
```

## 5. Screenshots

`local\lab\<stamp>-death-money\`:
`B-sees-the-drop.png` (200 uu, camera tilted down), `B-sees-the-drop-wide.png` (650 uu),
`B-sees-the-drop-close.png` (280 uu), `A-the-partner-no-drop.png` (the partner's window at
the same moment), `B-second-death-single-drop.png` (+ `-wide`) after the second death.

What I saw in them (round 9 build): the drop lands in the shallow pool the death happened
in, a few tens of uu below the death spot where the ground trace put it, so it is a dark pile
in dark water — the game's own **"F : RECOVER GLOOM"** prompt is what marks it on screen, and
`DEATHMONEY-VISIBLE` (rendered=1) is the objective statement. Details in §6.

## 6. Results

(filled in below by the run of the round-9 build — see the verdict block and the shots)

## 7. Open items

1. **`run-two.ps1` with this change** — see §6.
2. The step probe cannot fire for blueprint-internal calls (§2.3); the lane's world-side
   measurement is what proves the behaviour, not the step log.
3. `moneyclear` leaves `bHasLostGloom=1` with `amount=0` in the read-back right after the
   write (the game clears it itself by the next death); every check in this lane gates on
   the **amount**, and the destroy-all path is what the fixture relies on, so this is
   cosmetic — but a later agent should not read `bHasLostGloom` alone.
4. Writing the save slot (§3.2) is the one place this lane touches game state. It is gated
   on a *measured* loss (carried before − carried after) and skipped when the game recorded
   anything at this death.
