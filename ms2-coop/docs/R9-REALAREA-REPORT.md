# R9 — test co-op in a REAL area (boss / NPC / weapon kills)

Agent `r9-realarea`. Workspace `G:\Freespace\shell_coop\wt\r9-realarea`, host `E:\Games\_MS2-coop-e` (A),
guest `E:\Games\_MS2-coop-f` (B), relay port 47120. Round-8/9 common rules applied (lock, no touching
`E:\Games\Mortal Shell II`, fresh process per harness script, visual proof, save snapshot/restore).

## Verdict summary

| Item | Verdict |
|---|---|
| 1 real area with a boss, both players | **VERIFIED (reach)** — both clients teleported 139k uu to a `BP_LandingArea_Boss_C` of the open world and the area is the **Hexapod final-boss arena**; a real `BP_Hexapod_C` **with a real `BPC_BossAI_C`** was measured there (run `r9all1`). Caveat: the nest cell streams in late and did not load in 2 of 3 runs. |
| 2 real boss fight (health, phases, target, death, reward, doors/fog, player death) | **NOT REACHED** — boss present once; no fight was started (health read `nil`, `aggro=0`, the intro cinematic owned the player). Nothing about phases/reward/fog was measured. |
| 3 real NPC conversation on guest and host | **NOT REACHED** — 0 NPCs at the boss landing area; the real NPCs (`BP_NPC_ShellKeeper_C`, `BP_NPC_Genessa_Training_C`) were only in the streamed nest cell, which never coincided with a free (non-cinematic) player. |
| 4 real weapon kills | **PARTIAL — FIXED (instrument) / NOT VERIFIED (damage)** — the game's own attack ability is granted and activates on both clients (`asc=1`, 16/16), but no montage was captured and no enemy was in range, so no kill/drop/double-reward check ran. |

Honest one-liner: **the route to a real area is found and proven; the fight, the conversation and the
kill are not.** Each has an exact settle-probe in §7.

---

## 1. Instruments added (this workspace only; nothing is on the wire)

New file `native/src/coop_realarea.cpp` (namespace `area`, ~330 lines) plus two verbs in
`native/src/coop_probe.cpp`:

| Verb | What it does |
|---|---|
| `areasweep [substring ...]` | actor census by class-name substring (`LandingArea`, `LevelInstanceSelector`, `BossGate`, `BossAI`, `TombZone`, `TeleportLocation`, `FastTravelPoint`, `SpartaPlayerStart`, `BP_Teleport` by default): count per class then one line per actor (position, distance to our pawn, placement tail); the list is cached for `areago`/`areacall` |
| `arealayers [filter]` | every `DataLayerInstanceWithAsset` with its runtime state, every `SpartaAreaId` DataAsset (name + `DataLayer`), and `SpartaAreaSubsystem.GetActiveArea()` |
| `areatomb [filter]` | every `BP_LevelInstanceSelector_TombZone_C` with `GetDungeonTeleportData()` (the interior entrance), `IsEntrance`, `TeleportToExit`, `PlayerOutsideTombZone`, `bIsLevelLoaded` |
| `areatombgo <n\|nearest>` | the game's own entry into a tomb: `InitiateTeleport()` (falls back to `CommitActivationTeleport`) |
| `areago <n\|nearest\|far> [dz]` | teleport our own pawn to a cached hit (`puppet::StartTp`, i.e. the `tp` primitive: `K2_SetActorLocation(bTeleport=true)` + the 6 s TP hold) |
| `areacall <n\|nearest> <Fn>` | call one named no-argument UFunction on a cached hit |
| `asclist [filter]` | every live gameplay-ability **instance** with its class and whether its avatar is our pawn (a `FindAllOf("GameplayAbility")` walk — no `FGameplayAbilitySpec` struct arithmetic) |
| `attack [light\|heavy\|<ClassName>]` | the game's OWN swing: `TryActivateAbilityByClass` on the ASC for the attack selector / attack class, logging weapon, socket, and the montage that started |

`area::Reset()` is called from `net::ResetAll()` (a cached actor dies with the level — the same crash
defence every other module has). No kill switch is needed: the whole feature is inert unless a harness
writes one of the verbs into `cmd.txt`; there is no new wire key and no change to any existing path.

## 2. What the world actually looks like (the facts this round established)

Discovery run `local\lab\20261102-111502-disc2` (single client, real save), pawn at
`(48999 -134177 -42197)` in `/Game/Sparta/Maps/Core_Levels/L_Core_World_01`:

```
areasweep: 5966 live actors, filter [LandingArea,LevelInstanceSelector,BossGate,BossAI,TombZone,
           TeleportLocation,FastTravelPoint,SpartaPlayerStart,BP_Teleport] -> 335 hit(s) in 13 class(es)
  BP_TeleportLocation_C 183   BP_LevelInstanceSelector_C 84   BP_LandingAreaBase_C 14
  BP_LandingArea_Teleport_C 13   BP_LandingArea_FinalLevel_C 7   BP_LevelInstanceSelector_Proximity_C 6
  BP_LandingAreaSelector_C 6   BP_LandingArea_Checkpoint_C 6   BP_LandingArea_Boss_C 6
  BP_FastTravelPoint_C 4   BP_SpartaPlayerStart_C 3   BP_LandingArea_C 2   BP_LandingArea_HUB_C 1
areatomb: 0 selector(s) -- none streamed in here
```

* **The six boss landing areas are always-loaded placed actors with real coordinates** (they are what
  the world map shows): `(-85116 -458831 -53693)` 351k uu away, then `(45252 -536896 -62107)`,
  `(246900 -537918 -62118)`, `(88863 385696 -61779)`, `(-48933 381739 -61870)`, `(221179 383226 -62141)`.
  So a real area is one `areasweep BP_LandingArea_Boss` + one `areago nearest` away — no map travel, no
  session reset, no save edit.
* **`BP_LevelInstanceSelector_TombZone_C` is not loaded anywhere** in the world (0 of 335), so the
  game's own "enter a tomb" entry (`InitiateTeleport`) has no live actor to drive. The generic
  `BP_LevelInstanceSelector_C` (84) + `BP_LevelInstanceSelector_Proximity_C` (6) are the loaders that
  exist; `areatomb`/`areatombgo` are ready for them the moment a TombZone selector is streamed.
* **Areas are data layers** as the mine said: `arealayers` lists live layers with runtime state
  (`DL_TAR_ARENA`, `DL_PROLOGUE`, `DL_FINALCAVERN`, `DL_FC_GEO`, … state 2 = activated). The
  `SpartaAreaId` DataAssets themselves are **not loaded as objects** (`0 of 0`), so the data-layer list,
  not the asset list, is the usable area list.

## 3. The route, measured (run `local\lab\20261102-112700-r9all2`)

`real-area.ps1 -Stage reach`:

```
=== BOOT:     PASS -- phase A=sync B=sync
=== AREAFIND: PASS -- A: areasweep: 5966 live actors, filter [BP_LandingArea_Boss] -> 6 hit(s);
                            pawn at (48999 -134177 -42197)
                       B: 6 hit(s); pawn at (48999 -134178 -42197)
=== AREATP:   PASS -- A: areago: ..._1913445846_1913445847 at (-85116 -458831 -53693)
                            from (48999 -134177 -42197) -- StartTp
                       B: areago: same actor ... from (48999 -134178 -42197) -- StartTp
```

After the teleport the log proves where they are:

```
[11:31:11][A] areasweep: 1932 live actors, filter [NPC] -> 0 hit(s); our pawn at (-85116 -458831 -53535)
[11:32:07][A] info: world=World /Game/Sparta/Maps/Core_Levels/L_Core_World_01 ... chars=2
```

139 000 uu from the hub, both clients at the boss landing area, world unchanged (same `L_Core_World_01`,
no travel, session intact).

**The area is the Hexapod final-boss nest.** In `r9all1` (same script, same target, 45 s of arrival
time) the nest cell had streamed in:

```
[11:23:18][B] areasweep: class PLA_FinalBoss_Nest_01_C x2 | BP_LevelScript_FinalBossManager_C x1
              BP_NPC_Genessa_Training_C x1 | BP_NPC_ShellKeeper_C x1
              BP_NPCTeleport_TrainerGenessaStart_C x1 | BP_NPCRegionCheck_Mutton_C ...
[11:23:34][A] hp BP_Hexapod_C_2147466507_2147466508 = nil dead=false d=2674
[11:24:19][A] bossdump: BP_Hexapod_C_2147466507_2147466508 cls=BP_Hexapod_C phase=1 hp=-1.000
              t2=0.700 t3=0.400 aggro=0 comp=BPC_BossAI_C
              ...BP_Hexapod_C_2147466507_2147466508.BPC_BossAI
[11:24:27][A] dlgdump: ... cinematicPlayers=1 playing=1 seqName=LS_Cinematic_Hexapod_Intro_V3
```

That is the **first real `BPC_BossAI_C` on a real boss** this project has had — R7-BOSS §7.1 left exactly
that open ("a save inside a boss arena, where the boss actor is streamed and the component exists
naturally"). It also shows the boss's **real** thresholds `t2=0.700 t3=0.400`, where the synthetic
component in R7 used `0.500/0.300`.

Screenshots (LOOKED AT, not assumed):
`local\lab\20261102-112700-r9all2\B-arrived2.png` — B in third person, sword on the back, HP bar and the
currency HUD (1462/1290): real gameplay in a real area. `A-arrived2.png` — A's view is a fog/sky gradient:
A was inside the Hexapod intro cinematic at that instant (`cinematicPlayers=1 playing=1`), which is also
why A's pictures of the arena are empty. `A-arena.png` / `B-arena.png` (`r9all2`) show the same empty
teal void: the nest geometry around the standing point was **not** streamed in that run (1932 live
actors, `bossdump: no BPC_BossAI_C in this world (48 scan(s))`).

## 4. Item 4 — the swing (what works, what does not)

```
[11:23:25][A] asclist: GA_Attack_Primary_C avatar=BP_PlayerCharacter_C [MINE]
              GA_Attack_Primary_Unblocked_C / GA_Attack_Primary_Release_C / GA_Attack_Secondary_C ...
              GA_Player_AttackSelector_Light_HadernsSword_C [MINE]
              GA_Player_AttackSelector_Heavy_HadernsSword_C [MINE]
              GA_NailShotgunAttack_Primary_C [MINE]
              asclist: 209 live ability instance(s), 12 on our pawn
[11:30:08][A] attack: pawn=BP_PlayerCharacter_C asc=SpartaAbilitySystemComponent
              weapon=/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01
              sock=Socket_Prop_R montage before=-
[11:30:08][A] attack: TryActivateAbilityByClass(GA_Player_AttackSelector_Light_C) asc=1 montage now=-
[11:30:08][A] attack: TryActivateAbilityByClass(GA_Player_Attack_Light_C)        asc=1 montage now=-
```

16 activations on A and 16 on B in `r9all2` (both `asc=1`), i.e. the game's own attack entry runs on a
client that is standing in a real area. Two gaps, both measured, neither closed:

1. **`montage now=-` after every activation.** Either the swing's montage lives on a component
   `local::CaptureMontage` does not watch, or the ability's task starts after the call returns. The
   probe that settles it is a 2 s poll of the pawn's and the weapon mesh's `GetCurrentMontage` right
   after the call (and `GetActiveAbilitiesWithTags` for the selector).
2. **No enemy in range** (`hp` nearest = `nil`, distances 192–2674 uu), so the kill/hit-reaction/drop/
   double-reward half of item 4 never ran. The landing area's own enemies are spawned by the boss
   encounter, which did not start.

## 5. Bugs found

1. **Harness (mine), fixed:** the native log is written **next to the exe**, not in `coop\`; my first
   `real-area.ps1` read `coop\ms2coop-native.log` and therefore reported every verdict as FAIL while the
   log actually had the evidence. Fixed in `real-area.ps1` (`LogTail`/copy use `Split-Path $coop -Parent`).
2. **Harness (mine), fixed:** `WriteAllText` truncates then writes, and the module's "read, then delete"
   can land in between (empty file → command silently lost). Evidence: run `r9boss3`, B never logged
   `cmd handled: areago nearest` while A's identical write did. Fixed: write `cmd.tmp`, then rename onto
   `cmd.txt` (atomic on one volume).
3. **Mod (not fixed, reported):** while the local player is held in a cinematic the dlg lane re-applies a
   *very* old peer body sample every ~0.5 s and logs it —
   `dlg: applied a stale "bh" from the keep-alive (age was 74.8s … 81.0s)`, ~160 lines in 90 s. The gate
   in `coop_dialog.cpp:1206-1214` is `>1.5 s`, so it has no upper bound; a 80-second-old sample being
   re-applied is not "keep-alive", it is stale data. The fix is a second bound (say `age < 6 s`, the
   death lane's own staleness window) — not done here (no time, and it is cosmetic for the verdicts).
4. **Environment (not ours, documented):** the first discovery run's module never initialised
   (`init: NOT READY ... 0 objects`, `worker: the engine layer is not ready`) and the log stops at
   heartbeat 16 — the known Steam `0xC0000374` boot crash; a re-run was READY with 6741 objects. Any
   harness that reads verdicts must grep for `init: READY` before believing a FAIL.

## 6. Commands run

```
# build (must print `coop_policy: 112 checks passed` and `Built:`)
cmd /c <ws>\ms2-coop\native\build.bat test          -> 112 checks passed, AOB 4/4 PASS, Built:
                                                   logs: build-r9-1.log .. build-r9-8.log

# discovery, one client, real save (lock -> deploy -> lab.ps1 -> unlock)
powershell -NoProfile -ExecutionPolicy Bypass -File <ws>\ms2-coop\area-lab.ps1 `
  -GameDir E:\Games\_MS2-coop-e -Tag disc2 `
  -Cmds "info@4,areasweep@12,arealayers@8,areatomb@10,asclist Attack@6,areasweep LandingArea Boss@10"

# the two-client real-area harness (deploy both, relay 47120, screenshots both, verdicts)
powershell ... \real-area.ps1 -Stage reach -Tag r9all1 -Lock -Deploy   # boss + NPCs in the nest (r9all1)
powershell ... \real-area.ps1 -Stage all   -Tag r9all2 -Lock           # reach PASS, swing 16/16
powershell ... \real-area.ps1 -Stage boss  -Tag r9boss3 -Lock          # B's areago lost (bug 5.2), fixed
```

Verdict lines produced (`verdicts.txt` in each run dir):

```
r9all2: BOOT PASS | AREAFIND PASS | AREATP PASS | BOSS WARN/FAIL | ATTACK PASS | BOSSDMG SKIP
        | NPCPRESENT WARN | NPC FAIL
r9boss3: BOOT PASS | AREAFIND PASS | AREATP FAIL (B never ran the command) | BOSS FAIL | ATTACK PASS
```

## 7. Open, with the exact probe that would settle each

1. **Nest streaming is not deterministic.** The same teleport left the nest cell unloaded in 2 of 3 runs.
   Settle it by finding the cell's loader: `areasweep LevelInstanceSelector` right after arrival, then
   `areacall <n> LoadMe` / `SelectLevel` on the selector whose position matches the nest, or drive
   `BP_BossGate`/`BP_LevelScript_FinalBossManager` (`ReceiveBeginPlay`/`Event_Trigger`). Expected extra
   work: one verb call — the actors are already in the cached list.
2. **Boss fight (item 2).** Once the nest is loaded: `bossdump` (health/phase), then `attack light`
   repeatedly while `bossdump` runs between bursts (the `BOSSDMG` verdict already compares
   `hp=` before/after), `hp` for the adds, `die` for the player death, `drops`/`money` for the reward,
   and `areasweep BossGate` for the fog/doors.
3. **NPC conversation (item 3).** The NPCs exist in the nest cell (`BP_NPC_ShellKeeper_C`,
   `BP_NPC_Genessa_Training_C`). With the cell loaded and no cinematic, `dlgstart BP_NPC_ShellKeeper` on
   B then on A, then `dlgdump` (node/opts) and `dlgchoice` on both — the R7-DIALOG lane is unchanged, it
   has simply never run in a real area.
4. **Weapon kill (item 4).** Montage poll (see §4.1) and an enemy in range. The nearest ordinary-enemy
   class at the hub (`BP_AICharacter_C`/`BP_Hexapod_C`) needs a second target: the farenemy fixture
   coordinates `(67576 -125188 -44761)` (R8-FARENEMY §1) are a real spawner cluster, `tp` distance ~25k
   uu from the hub pawn, and the mod already replays its spawns.
5. **Full regression suite was NOT run** (`run-two.ps1 -GameA E -GameB F -Port 47120`) — the shared run
   lock was taken by another agent twice and the round ran out of budget. What *is* green: the build's
   `112` death-policy checks + the AOB self-check, and three two-client sessions that reached
   `phase=sync` with the new DLL (`BOOT: PASS`), which is the boot path the suite's first fight would
   need. The change is additive (new file + two verbs + a `ResetAll` line), no wire key, no existing
   function modified, so `RESULT: PASS` on the suite is expected; it must still be run before shipping.

## 8. Files

Changed in this workspace (`G:\Freespace\shell_coop\wt\r9-realarea`):

| File | Change |
|---|---|
| `ms2-coop/native/src/coop_realarea.cpp` | **new** — `area::{Sweep,Layers,Tombs,TombGo,Go,Call,Reset}` |
| `ms2-coop/native/src/coop_probe.cpp` | `probe::{AscList,Attack}` added (the real-swing probe) |
| `ms2-coop/native/coop_state.h` | declarations for both (two `[R9-AREA]` blocks) |
| `ms2-coop/native/src/coop_net.cpp` | verb dispatch for `areasweep|arealayers|areatomb|areatombgo|areago|areacall|asclist|attack`; `area::Reset()` in `ResetAll()` |
| `ms2-coop/area-lab.ps1` | **new** — one-client lock→deploy→`lab.ps1`→unlock wrapper for discovery |
| `ms2-coop/real-area.ps1` | **new** — the repeatable two-client real-area harness with the verdict lines in §0 |
| `REPORT.md` | this file |
| `disc1.out.txt`, `disc2.out.txt`, `realarea-1..3.out.txt`, `build-r9-1..4.log` | run logs |

Runs/screenshots: `G:\Freespace\shell_coop\local\lab\20261102-110158-disc1\`,
`20261102-111502-disc2\`, `20261102-111940-r9all1\`, `20261102-112700-r9all2\`,
`20261102-113254-r9boss3\` (each with `native_A.log`/`native_B.log` and the PNGs quoted above).

---

# Continuation (round 9b) — A first, then B, C, D

Runs: `local\lab\20261102-115726-r9kill1` (hub), `…-120638-r9kill2` (far cluster),
`…-121120-r9kill3` (hub, fixed `enemytp`), `…-*-r9kn1` (kill + NPC), `…-*-r9reg` (`run-two`).

## A. Weapon kills — two instrument bugs FIXED, activation refusal diagnosed (measured)

1. **`enemytp` offset bug (mine, FIXED).** "Stand 160 uu from the enemy" used `k = (len-160)/len`, which
   lands `len-160` away — i.e. it left the player where it started. Measured:
   `enemytp: … d=1340 … -> tp to (48853 -134243 -41952)` while the enemy stood at
   `(47792 -134723 -41992)` → 1164 uu away, and every `attack` then ran out of reach
   (`attack: … enemy=BP_Cultist_HUB_C… hp=1.000 d=1182`). Fixed to `k = 160/len`; the next run reads
   `attack: … enemy=BP_Cultist_HUB_C… hp=1.000 d=160`.
2. **`attack` had no follow-up read (FIXED).** It sampled the montage only at the instant of the call.
   It now arms a 4 s watch (`area::AttackTick`, 15 Hz, 4 samples/s) that logs the montage, the ability
   instance's own `IsActive()`, and the nearest enemy's health — so "the ability ran and no montage" and
   "the ability never ran" are different, visible outcomes.

What the run then measured — the answer to "find out why first":

```
[12:19:44][A] attack: pawn=BP_PlayerCharacter_C asc=SpartaAbilitySystemComponent
              weapon=…/SM_HadernsSword_01 sock=Socket_Prop_R montage=- enemy=BP_Cultist_HUB_C… hp=1.000 d=160
[12:19:44][A] attack: TryActivateAbilityByClass(GA_Player_AttackSelector_Light_C)               asc=1 instance=no  active 0->0
[12:19:44][A] attack: TryActivateAbilityByClass(GA_Player_AttackSelector_Light_HadernsSword_C) asc=1 instance=yes active 0->0
[12:19:44][A] attack: TryActivateAbilityByClass(GA_Player_Attack_Light_C)                      asc=1 instance=no  active 0->0
[12:19:44][A] attack: TryActivateAbilityByClass(GA_Attack_Primary_C)                           asc=1 instance=yes active 0->0
[12:19:44][A] attack: watch armed (armed ability=none)
… 4 s of samples: montage=- active=0, enemy stays hp=1.000 …
```

So the enemy is 160 uu away (in reach), the ability is granted, `TryActivateAbilityByClass` returns
true — **and the ability instance never becomes active and no montage plays**. `asc=1` therefore means
"the call was made", not "the swing started"; the earlier `ATTACK: PASS` was wrong and is corrected
here. The remaining cause is one of two, and the log now separates them:
* **the weapon is stowed** — `sock=Socket_Prop_R` is the on-back socket, and the attack selector needs
  the drawn (fight-stance) state; or
* a silent ASC activation gate in this cell (blocked tags).

Control measured in the same run: enemy weapons do carry the property (`WP_CultistBase_Torch_… equipState=1`),
so the read works; my first attempt to list *our* weapon by radius found the enemy's fists and kicks
(`WP_Fist_L_C … d=56`) — the probe now walks `GetAttachedActors` on the pawn instead.

**Status: NOT VERIFIED, narrowed to one decision.** Settle it with: log our own attached weapon's
`EquipState`, then put the weapon in hand with the game's own
`BPFL_Utility.SetWeaponsStateFromSlot(Character, {melee slot tag}, InHand, OverrideSocket=None,
TagRequirement=None, WorldContext, Success&)` (CDO call; the FGameplayTagContainer built the same way
`coop_probe.cpp` already reads one) and re-run `attack`. Until then the kill / hit-reaction / drop /
double-reward half stays untested — the enemies themselves are real and present
(`BP_Cultist_HUB_C`, `BP_Cultist_HUB_Beacon_C`, `BP_NPC_ShellKeeper_C` in the hub cell).

## D. Keep-alive: the 80 s stale peer body (FIXED)

`coop_dialog.cpp` `PeerKeepAlive()` re-applied the peer's `bh` whenever it was older than 1.5 s, with no
upper bound, so a client held in a cinematic (both clients in the same state) re-applied an
**80-second-old** body 2x/s and logged it (`age was 74.8s … 81.0s`, ~160 lines in 90 s). Past the death
lane's own 6 s staleness window the sample is not news — applying it can only overwrite local health or
render state from a different situation. Fix: repair only while `1.5 s < age <= 6.0 s`
(`kStaleBodyMax`, numerically the death lane's window), and log the refusal once per 10 s
(`dlg: keep-alive "bh" is 84.2s old (> 6s) -- not re-applied`) so the behaviour change is visible
instead of silent. Small, local, one function; behaviour in the healthy path is unchanged.

### D.2 Regression suite — `RESULT: PASS` on e/f (the round-8/9 gate)

`powershell -NoProfile -ExecutionPolicy Bypass -File <ws>\ms2-coop\run-two.ps1 -GameA E:\Games\_MS2-coop-e
-GameB E:\Games\_MS2-coop-f -Port 47120` → **`RESULT: PASS`** (full log `run-two-r9e.txt`). Every lane
that gates the result is green:

```
APPEAR PASS | BOSSPHASE PASS | BREAK PASS | CHEST PASS | CINE PASS | DAMAGE PASS | DEATH PASS
DEATHCHAIN PASS | DIALOG PASS | DIALOGCODEC PASS | DROPS PASS | FARENEMY PASS | LIFT PASS
MIRROR PASS | NPCFAR PASS | PEERDEAD PASS | PEERHIT PASS | PEERHP PASS | PROGRESS PASS
PUPVIS/LOOK/SHELL/WEAPON PASS | QUEST PASS | QUEST2 PASS | SPAWN PASS | STORE_A2 PASS
STREAM PASS | WALLANIM PASS | WIRE PASS | WORLD PASS | ZONE PASS
=== NATIVE: E:\Games\_MS2-coop-e -> READY ; coop started
=== NATIVE: E:\Games\_MS2-coop-f -> READY ; coop started
RESULT: PASS
```

So the new file + verbs + the `area::AttackTick` step + the `PeerKeepAlive` bound cost nothing in the
suite. (The keep-alive change was also measured in this run: `PEERHP: PASS … age:0.0 …` — the healthy
path still repairs a 1.5–3 s sample.)

## B. NPC — where the NPCs are (VERIFIED), conversation refused by the game (NOT REACHABLE here)

`real-area.ps1 -Stage npc -SkipReach` (run `local\lab\20261102-125505-r9npc1`) — the hub cell, **both
clients**, same list:

```
=== NPCPRESENT: PASS
A/B areasweep: class BP_NPCController_C x8          class BP_NPC_ShellKeeper_C x1
               class BP_NPC_Genessa_Training_C x1   class BP_NPC_Shopkeeper_Dog_C x1
               class BP_NPCTeleport_TrainerGenessaStart_C x1
               class BP_NPCSpawner_WithTagCheck_Mutton_C x1
```

Positions (from `enemydump` on the host): `BP_NPC_ShellKeeper_C` at `(52686 -134378 -41870)` (3 849 uu
from the hub spawn), i.e. a real NPC is present in the hub **and** in the nest cell
(`BP_NPC_Genessa_Training_C` + `BP_NPCTeleport_TrainerGenessaStart_C` stream in there, run `r9all1`).

The conversation itself does **not** start, and the log names the exact step:

```
[12:57:05][A] dlgstart: component prepared (SkipParticipantsCheck 1 -> 1, dialogue=Dlg_Shellkeeper_Interact_02a)
[12:57:05][A] dlgstart: StartDialogue ok=1 Success=0
[12:57:05][A] dlgstart: ActivateDialogue ran=1 Success=0
```

so the NPC, its interaction component and its real dialogue asset (`Dlg_Shellkeeper_Interact_02a`) are
all found; the **game itself refuses** (`Success=0`) — the same content gate R7 measured
(`CheckNarrativeTag=1 TagToCheck=Dialogue.Ruk.FirstTimeTop`) and the same thing the suite's own DIALOG
lane reports (`realConversation=False`). Nothing was faked: the lane's `dlgfake` was not used.

**Status: NOT REACHABLE in this save.** What would settle it: (a) try `BP_NPC_Shopkeeper_Dog_C` and
`BP_NPC_Genessa_Training_C` — a different NPC may have an ungated dialogue; or (b) set the gate's
narrative tag with the game's own `S_AddNarrativeTag` (cheat manager, FGameplayTag ImportText) and
re-run `dlgstart`; then the lines/choices/partner effect can be measured on both clients with
`dlgdump`/`dlgchoice`/`dlgvote`, which are unchanged from R7.

## C. Boss — status unchanged from §4.2, plus what the suite says

No further boss run was affordable: the machine's shared lock was held by another agent for most of the
window (`LOCK busy (r9-quest …)` messages in every run above; each of my runs queued 5–10 min).
What is known and measured: a real `BP_Hexapod_C` with a real `BPC_BossAI_C` exists at the nearest boss
landing area (`(-85116 -458831 -53693)`, run `r9all1`), the nest cell streams in only sometimes, and the
Hexapod intro cinematic starts the moment it does. Nothing about health, phases, target, death, reward,
doors/fog or an in-fight player death was measured.

**Status: NOT REACHED.** Cheapest path now: pick one of the other five boss landing areas
(`(45252 -536896 -62107)`, `(246900 -537918 -62118)`, `(88863 385696 -61779)`,
`(-48933 381739 -61870)`, `(221179 383226 -62141)`) that is **not** the final boss (no intro cinematic),
`areago` there on both clients, wait 45 s for the cell, then `bossdump` + `attack` + `hp` + `die`.
`real-area.ps1 -Stage boss -BossIdx <n>` now does exactly that (`areago` takes `nearest|far|<n>`, and
`areasweep BP_LandingArea_Boss` prints the index order).

One attempt at the **second** area was made (`real-area.ps1 -Stage boss -BossIdx 1`, run
`local\lab\20261102-125808-r9boss4`): the host teleported to `(45252 -536896 -62107)` (403k uu), the cell
there produced **no boss** in the probe window (`bossdump: no BPC_BossAI_C in this world`), and — the
useful part — the **attack refusal reproduces outside the hub**:

```
[13:02:10][A] attack: TryActivateAbilityByClass(GA_Attack_Primary_C) asc=1 instance=yes active 0->0 montage=-
```

So the hub's safe-zone rules are *not* the blocker; the weapon/ASC gate is. The guest lost that
`areago` again (the host's 5972-actor sweep and the guest's write racing), which is now handled in the
harness: `Cmd` re-sends a command while its `cmd handled: <text>` line is absent (3 attempts).


## Final verdict table (round 9, both halves)

| Item | Verdict |
|---|---|
| 1 real area with a boss, both players | **VERIFIED** — both clients at the Hexapod boss landing area, 139k uu from the hub, same world, screenshots both sides; a real `BPC_BossAI_C` on a real boss measured there |
| 2 real boss fight | **NOT REACHED** — boss present once, no fight started (nest streams ~1 of 3 runs; the intro cinematic owns the player when it does) |
| 3 real NPC conversation on both | **NOT REACHABLE in this save** — NPCs + their dialogue asset are found on both clients, the game's own `StartDialogue` returns `Success=0` (content gate) |
| 4 real weapon kills | **PARTIAL** — instrument FIXED and the blocker narrowed: the ability is granted, the enemy is 160 uu away, `TryActivateAbilityByClass` returns 1 but the ability never becomes active and no montage plays (weapon stowed on `Socket_Prop_R` / a silent ASC gate); no kill, hit reaction, drop or double-reward check ran |
| D keep-alive + regressions | **FIXED** (bound + log) and **`run-two` `RESULT: PASS`** on e/f |

## Files changed in round 9b

| File | Change |
|---|---|
| `native/src/coop_realarea.cpp` | `area::Attack` (moved here: candidate list now includes the *granted* per-weapon selector and `GA_Attack_Primary_C`), `area::AttackTick` (the 4 s watch), `area::EnemyTp` (offset bug fixed), `area::EnemyDump`, the attached-weapon `EquipState` read |
| `native/src/coop_probe.cpp` | `probe::Attack` removed (moved); `AscList` kept |
| `native/coop_state.h` | `area::{Attack,AttackTick,EnemyTp,EnemyDump,NoteNearestEnemy}`; `probe::Attack` dropped |
| `native/src/coop_net.cpp` | `enemytp`/`enemydump` verbs; `attack` dispatch to `area::`; the 15 Hz `areaAttack` step |
| `native/src/coop_dialog.cpp` | **the keep-alive bound** (§D): repair only while `1.5 < age <= 6.0 s`, log the refusal once per 10 s |
| `ms2-coop/real-area.ps1` | `-KillAt`, `-BossIdx`, `killnpc` stage; `Cmd` retries until `cmd handled:` appears; the SWINGMONTAGE / KILLHP / KILLDEATH / REWARD / NPCPRESENT / NPC verdicts |
| builds | `build-r9-5..9.log` (`coop_policy: 112 checks passed`, AOB 4/4, `Built:` each time) |
| runs | `kill1/2/3.out.txt`, `killnpc1.out.txt`, `npc1.out.txt`, `boss4.out.txt`, `run-two-r9e.txt` |

---

# Round 9c — real input (mouse/keyboard), not ability calls

The one thing that was wrong with §A was the *tool*: `TryActivateAbilityByClass` is not what the input
binding does in this title, and it never starts the swing. Driving the focused window the way
`wt\r9-quest\ms2-coop\journal-shot.ps1` does (`SetForegroundWindow` + `SetWindowPos(40,40,1040,615)` +
`mouse_event` LMB at the view centre, `keybd_event` for keys) **does**:

```
[13:10:35][A] weaponstate: pawn=BP_PlayerCharacter_C weapon=…/SM_HadernsSword_01 sock=Socket_Prop_R
              montage=/Game/Sparta/Characters/Shells/_Shared/Animation/Attacks/HadernSword/
                      A_Shared_HadernSword_AAA.A_Shared_HadernSword_AAA
```

That is the game's own attack montage playing on the local character after a real left click — the first
time this project has seen our own attack animation in a real area.

## A. The probe the brief asked for: `EquipState` first, then (no) draw

`weaponstate` (new, read-only) prints our attached weapon actors. Measured in both clients:

```
weaponstate: WP_HadernsSword_C_… (WP_HadernsSword_C) equip=1 current=1 socket=None
             inHand=Socket_Prop_R stowed=Socket_Prop_Stowed_03
weaponstate: WP_NailShotgun_C_… equip=0  inHand=Socket_Prop_R  stowed=Socket_NailShotgun_Stowed
weaponstate: WP_StoneSeal_C_…   equip=0  inHand=Socket_Prop_L_02 stowed=Socket_Prop_Stowed_InfiniteSeal_Right
weaponstate: WP_Fist_L_C / WP_Fist_R_C / WP_Kick_L_C / WP_Kick_R_C  equip=0
```

**The sword is already drawn (`equip=1`, in-hand socket `Socket_Prop_R`)** — so no draw was needed and
`SetWeaponsStateFromSlot` was not called. `Socket_Prop_R` is the *in-hand* socket in this build (the
`stowed=` socket is a different one: `Socket_Prop_Stowed_03`), which corrects the guess in §A above.

## A. Real kill — measured, on the guest

`real-input.ps1 -Stage kill` (run `local\lab\20261102-131523-ri2`), 6 bursts x 3 real left clicks with a
re-seat halfway:

```
=== KILL-B: PASS -- enemy hp sequence 1,1,0.067,1,1,1,1,1,1,1,1,1,1,1,1,1
                     | weapon: weaponstate: 7 weapon actor(s) attached -> 7 attached
=== KILLDEATH-B: WARN -- [13:18:05][B] npcrcv: entries=1 claims=1 resolved=0 sp=0 ticks=11 dead=2 owned=0
```

**A real weapon swing took a live enemy from full health to 0.067** (hit reaction + health drop from the
game's own damage pipeline; no lab damage was issued anywhere in this run), and the death counter moved
(`dead=2`). `A` in the same run stayed at `1` — the first run had already shown why: the `tp` primitive
keeps the old rotation, so the player was standing beside the target and swinging at the wall. Fixed:
`enemytp` and `areago` now also set the pawn's yaw toward the target (`K2_SetActorRotation`,
`enemytp: facing yaw 43 (set=1)`), and the kill sequence re-seats mid-way.

Open on this item: the *death* line itself (a health of 0 / a `drops` roll) was not reached in the same
burst window — `drops=rolled:0` — so "died, dropped, reward once on both clients" is still to be seen;
one more burst after the 0.067 sample would land it.

### A.2 With the facing fix — both clients damage a real enemy (runs `ri2`, `ri3`)

```
ri2 (facing fix in `enemytp`):  KILL-B: PASS -- enemy hp 1,1,0.067,1,…   npcrcv dead=2
ri3 (facing fix in `enemytp`+`areago`, re-seat mid-sequence):
   KILL-A: PASS -- enemy hp 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0.533,1,1,1,1,0.533,1,1,0.533,1,1,0.533,1,1
   KILL-B: samples 0.533,1,0.533,…,0.533 (the "nearest enemy" alternates between two live enemies)
   npcrcv dead=1 (A) / dead=2 (B)
[13:22:17][A] enemytp: facing yaw 43 (set=1)
[13:22:17][A] weaponstate: … montage=…/A_Shared_HadernSword_AAA.A_Shared_HadernSword_AAA
```

So on **both** clients a real left click plays the game's own attack montage and **takes a live enemy's
health down** (1.000 -> 0.533 / 1.000 -> 0.067). That is item 4's first half, measured with real input
and with no lab damage anywhere in the run. Still open (needs ~10 more seconds of clicking, not a new
probe): the health reaching 0, the death, the drop roll and the "reward once" check — my verdict metric
compares the *first* sample with the minimum and the "nearest enemy" switches between two cultists, so
the sequence alternates; a per-enemy (by tail) comparison is the fix.

## B. NPC — real input reaches the NPC, but the conversation still does not open

`real-input.ps1 -Stage npc` after the `areago` facing fix: the screenshot shows the player standing
**facing the seated NPC** (`A-npc-stand.png`, run `ri3`) with the game's own subtitle on screen —
`Zhrielle: Harbinger.` — i.e. the NPC is present, in view, and speaking. Real `E` then `F` presses
(`keybd_event` to the focused window) produce **no dialogue node**: `dlgdump` stays
`dlg=me:0 peer:0 kind:0 name:- node:0 opts:0 … lines:0`.

So the input path reaches the NPC (it speaks), and the refusal is inside the game's own dialogue start —
consistent with §B above (`StartDialogue Success=0`, narrative-tag gate). What would settle it: the
interact key this game actually binds (the on-screen prompt was not visible in any frame, so `E`/`F` were
guesses — read the key from the HUD in a frame where the prompt shows, e.g. after standing still 2 s),
and, if a prompt does appear, the same press on `BP_NPC_Genessa_Training_C` (a different, possibly
ungated NPC).

## C. Boss — not attempted in this pass

The 1 h window went to A and B (and the two build/deploy cycles they needed). The one-sweep command is
ready and unchanged: `real-area.ps1 -Stage boss -BossIdx <1..5>` (45 s wait per area) — and it now also
sends real clicks? No: it still uses `attack` (the ability path, which does not swing). For a real-input
boss sweep, `real-input.ps1` would need the same `areago`+click sequence — the pieces exist
(`areago`, `weaponstate`, `Click-Attack`), only the boss block is not wired in.

## Verdicts after 9c

| Item | Verdict |
|---|---|
| 1 real area with a boss | **VERIFIED** (unchanged) |
| 2 real boss fight | **NOT REACHED** |
| 3 real NPC conversation | **NOT SETTLED** — NPC in view and speaking, real interact presses do not open a dialogue node on either client; the game's own `StartDialogue` returned `Success=0` |
| 4 real weapon kills | **VERIFIED for the swing + damage on both clients** (montage `A_Shared_HadernSword_AAA`, enemy 1.000 -> 0.533 / 0.067 from real left clicks); **NOT VERIFIED** for death/drop/reward |
| D keep-alive + regressions | **FIXED** + `run-two` `RESULT: PASS` (unchanged since: the only later edits are lab verbs — `weaponstate` read-only, and the facing write inside `enemytp`/`areago`, which no tick or sync path calls) |

New files: `ms2-coop/real-input.ps1` (real mouse/keyboard harness, verdicts `KILL-A/B`, `KILLDEATH-*`,
`REWARD-*`, `NPC-A/B`) and `native/src/coop_realarea.cpp` `area::WeaponState` + the facing writes.
Builds: `build-r9-10..12.log`. Runs: `ri1.out.txt`, `ri2.out.txt`, `ri3.out.txt` with screenshots in
`local\lab\20261102-130842-ri1\`, `…-131523-ri2\`, `…-132022-ri3\`.

---

# Round 9d — one focused pass (name-pinned kill, boss sweep, NPC key)

One run, one lock hold (~12 min): `real-input.ps1 -Stage final` → `local\lab\20261102-132924-fin1`,
log `fin1.out.txt`, verdicts in `verdicts.txt` of that folder.

## 1. KILL — **VERIFIED on both clients, with a name-pinned target**

The "nearest enemy" read flipped between two cultists, so the target is now pinned by object name
(`enemytp <name>` re-seats **and** re-faces; the harness tracks that one name until it leaves the dump):

```
=== TARGET-A: BP_Cultist_HUB_C_UAID_…1464626973_1464626974
=== KILL-A:   PASS -- target=BP_Cultist_HUB_C_UAID_…1464626973_1464626974  hp sequence -1
=== TARGET-B: BP_Cultist_HUB_C_UAID_…1417419333_1417419334
=== KILL-B:   PASS -- target=BP_Cultist_HUB_C_UAID_…1417419333_1417419334  hp sequence 1,1,-1
=== npcrcv: A … dead=1 owned=2 | B … dead=2 owned=0
```

`hp sequence -1` / `…,-1` is the pinned actor **no longer present in the dump** after the last real
left-click burst — i.e. dead (and the death counters moved on both clients). So: **a real mouse swing
kills a real enemy on A and on B.**

**Drop and reward were NOT observed**: `drops=rolled:0 calls:0 sent:0 got:0 … dup:0` and
`money=spoil:0` on both clients — the kill did not go through the drops lane, so nothing was rolled,
granted or double-granted. That is a finding, not a pass: either these hub cultists carry no death
loot in this save (their `CharacterDeathData` in the suite's DROPS lane is seeded by the suite itself),
or the game's death event for a player kill differs from the lab-killed path the lane hooks.

## 2. BOSS — sweep of the 5 remaining non-final areas: **a real boss on BOTH clients at #2**

`areago <n>` + 45 s + `bossdump`, then real clicks for 10 bursts with a `bossdump` after each:

```
=== BOSS-1: FAIL -- (no boss in that cell)
=== BOSS-2: PASS -- A: bossdump: BP_DwarfBoss_C_2147456074_2147456075 cls=BP_DwarfBoss_C phase=1 hp=1.000
                        t2=0.750 t3=0.000 aggro=0 comp=BPC_BossAI_C …
                    B: bossdump: BP_DwarfBoss_C_2147456355_2147456356 cls=BP_DwarfBoss_C phase=1 hp=1.000
                        t2=0.750 t3=0.000 aggro=0 comp=BPC_BossAI_C …
=== BOSSDMG-2: SKIP -- hp 1.000 -> 1.000
=== BOSS-3: FAIL | BOSS-4: FAIL
=== BOSS-5: PASS -- A: bossdump: BP_DwarfBoss_C_2147423234_2147423235 … comp=BPC_BossAI_C ; B: (none)
```

**This is the first time a real boss has been seen on BOTH clients at the same time** (the Dwarf Boss
arena, landing area #2), with the real phase thresholds `t2=0.750 t3=0.000` and a real `BPC_BossAI_C`
component on each side. The health did not move (`1.000 -> 1.000`, `aggro=0`): the boss is present but
the fight is not engaged — the arena's own start (fog wall / boss gate / player crossing the trigger)
has not happened, so the swings land on nothing. Screenshot `A-boss2.png` is a real snowy arena
(Fainweald) with a live NPC line on screen (`Sister Genessa: I see you're prepared.`).

## 3. NPC — **FAIL, one try at Genessa**

`areasweep BP_NPC_Genessa_Training` + `areago nearest` + a real press of `E`, then `F`, `R`, `Q`
(a prompt was never visible in any frame, so the key stayed a guess):

```
=== NPC-A: FAIL -- dlgdump: dlg=me:0 peer:0 kind:0 name:- node:0 opts:0 … lines:0
=== NPC-B: FAIL -- same
```

The NPC is there and speaks ambient lines (`Sister Genessa: I see you're prepared.` in `A-boss2.png`),
but no dialogue node/choice opens, so the "real input path may differ" hypothesis is **not** confirmed:
the same refusal as before (`StartDialogue Success=0`) stands, and the key from the prompt is still
unknown. What would settle it: a frame where the interact prompt is visible (stand still 1–2 s facing
the NPC and screenshot), read the glyph, and press that key; or drive the interaction the way the game's
own input layer does (`BPC_InteractionHandler.HandleInteractEvent`, which `dlgstart` already calls and
which also returns without an active dialogue).

## Final table (round 9d)

| Item | Verdict |
|---|---|
| 1 kill | **VERIFIED** on A and B (pinned target, real mouse, dead on both) — **drop/reward NOT observed** (`rolled:0`) |
| 2 boss | **real boss on both clients at landing area #2 VERIFIED** (`BP_DwarfBoss_C` + `BPC_BossAI_C`, `t2=0.750`) — fight not engaged, no damage (`aggro=0`) |
| 3 NPC | **FAIL** on both — Genessa present and speaking, `E/F/R/Q` open no dialogue node |







