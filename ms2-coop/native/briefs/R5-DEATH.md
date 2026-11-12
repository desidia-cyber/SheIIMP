# Round 5 — DEATH agent (`<you>` = death, copy = `G:\Freespace\shell_coop\wt\death\ms2-coop\`)

Read `native\briefs\R5-COMMON.md` first (in your copy). Then implement **co-op player death** for MS2, modelled
on the working DollsMP mod. The owner wants our logic to stay as close as possible to DollsMP's.

## Reference implementation (read it, it is the spec)
- `G:\Freespace\dollsMP\mod\native\src\coop_policy.h` — the pure decision header (party case, should-suppress,
  should-respawn, recase). `G:\Freespace\dollsMP\mod\native\src\test_coop_policy.cpp` — its unit tests.
- `G:\Freespace\dollsMP\mod\native\src\proxy.cpp`: death detection ~11990-12001 + 12171 (watchdog),
  `CoopTick` 11517, reload suppression 11484/11582, `MaybeRevive` 12191 / fallback 12332 / spawn-point 12058,
  wipe 11702/14508/14620, spectate `DmpCoopSpectateStep` 14645, rest clears stacks 11776.
  `dmp_rules.h` (debuff factor :29, refund :213), `dmp_rules_engine.h` (re-basing 60-105, after-respawn :148),
  `dmp_settings.h` 111-127 (defaults). Port the *logic*, not the game-specific engine calls (different game/UE).

### Rules to reproduce (owner-approved)
| Case | Condition | Outcome |
|---|---|---|
| 1 wipe | me down AND partner down | full vanilla death/reset is allowed; host resets first; guest does NOT run its own reset early, it waits for the host to be up again (and in the same map), then follows; no stack added; all stacks cleared |
| 2 respawn+debuff | at least one player up, no live boss fight holds me | suppress the game's death reset/reload, respawn me in the LIVE world (no world reset) after `respawn_delay_s + respawn_delay_per_stack_s * stacks` (10 s + 5 s/stack, cap 600 s) at the nearest respawn point on the same level measured from my frozen death position; add 1 stack |
| 3 hold | I am down inside a live boss fight | no respawn; camera spectates the partner's body (our local puppet of them); no stack yet |
Precedence: wipe beats hold; hold beats respawn. Re-decide 4 Hz: 3→2 when the fight is no longer live (if the
fight was won add the held stack now... DollsMP: "a victory does not charge"— read `DmpCoopRecase` and copy its
exact rule), anything→1 when the partner goes down, never 2→3.
Partner liveness: only ALIVE counts as alive; unknown/stale (no peer packet for 6 s) counts as down.
Solo (partner never seen this session): do nothing — vanilla game behaviour (the owner's game handles solo).
Stacks: max health AND max shell health × (1 − stacks × pct), total cap 90 %; re-base when the game itself changes
the value so we never undo the game's own changes (DollsMP rules engine). Cleared on a wipe or when the player
rests (find MS2's rest/checkpoint interaction). Defaults: death_debuffs=1, debuff_max_stacks=5,
debuff_health_pct=10, debuff_shield_pct=10 (→ shell health), respawn_delay_s=10, respawn_delay_per_stack_s=5,
pay_death_penalty=1 (keep whatever currency penalty the game applies on death; if our suppression skips the game's
penalty, apply the equivalent — find out what MS2 takes on death; with 0, refund it on a case-2 respawn).
PvP: none in this mod — omit, note it. Rules file: `coop\MS2Coop.ini` section `[Rules]` (read at load, defaults
if missing; write a commented default file if absent). Kill switch `DEATH_OFF.txt` = vanilla death everywhere.
The debuff belongs to the HOST's settings: the host publishes its rules values, the guest uses them.

## Deliverables
1. `native\src\coop_policy.h` — pure, engine-free decision functions (port of DollsMP's), and
   `native\tests\test_coop_policy.cpp` hooked into `build.bat test`. Port the relevant DollsMP test cases.
2. `native\src\coop_death.cpp` (+ declarations in a marked block of coop_state.h): detection, decision, suppression,
   respawn, hold/spectate, wipe coordination, stacks/debuff, rules ini, sync.txt line
   `death=me:<liveness> case:<n> serial:<n> stacks:<n> peer:<liveness> boss:<0|1> ...`, log lines
   `COOP DEATH #n victimCase=.. partyCase=..` + a plain-English line, `MISSING REVIVE` watchdog.
3. Wire keys (exact, in the publish line; your contract with the other agents):
   - `"lv":<int>` liveness word: bits 0-2 liveness (0 unknown, 1 ALIVE, 2 RESPAWNING, 3 HOLD, 4 WIPED/DOWN),
     bits 4-5 party case, bits 8-15 death serial (mod 256).
   - `"bs":0|1` this machine sees a live boss fight it is part of.
   - `"dstk":<n>` my current stacks.  `"wr":<n>` host world-reset serial (host only; guest follows it).
   - `"rul":[maxStacks,healthPct,shieldPct,delay,delayPerStack,payPenalty]` host only.
4. Cmd verbs: `die` (kill my own player through the game's own path — `BP_PlayerController_C.S_KillMe()` exists,
   or `USpartaHealthComponent.StartDeath()`), `bossfake 0|1` (force my boss flag, for tests), `dstate` (log state).
5. Verdicts (receiving/observing client writes result file, plus a snippet for run-two):
   `DCASE2` (B dies while A alive, no boss → B respawned alive, no world reset happened, stacks=1, B's max health
   ≈ 90 % of before), `DHOLD` (bossfake 1 on B then die → B held + view target is the puppet; bossfake 0 → B
   respawns), `DWIPE` (both die → host world reset happens, guest follows after host is up, both alive, stacks 0).
   Order the steps in one run so they don't interfere, or use separate runs.

## The research you must do first (lab runs, one client is enough for most of it)
MS2 death flow per the docs: `SpartaHealthComponent:StartDeath()` → `GA_Death_Player_C:HandlePlayerDeath` →
`WBP_DeathScreen_C` (`OnDeathSelection`, `ResetWorld`, `CreateLoadingScreen`) → `SpartaWorldGenSubsystem.BeginWorldReset()`
(see `docs\HANDOFF-REMAINING-SYNC.md` G9 and `research\mine\SYNC-DATA-MINE.md` §7, §8, §17; grep `research\raw`).
Find out, with logging HookPre/HookPost lab runs and `die`:
- the exact sequence of UFunctions on death (log every ProcessEvent function whose name matches
  Death|Reset|Respawn|Revive|Load|Travel|Checkpoint|Restart for ~60 s after `die`), and whether the game does an
  in-place world reset or a level reload/travel;
- a point where the reset/reload can be **blocked** (ProcessEvent pre-hook that skips the original — the engine
  layer has no "skip" yet: add it in `ms2_ue` inside a marked block, e.g. `HookBlock(name, fn) -> bool skip`;
  calls BP→native do not go through ProcessEvent, if you need to block one of those, swap that UFunction's native
  `Func` pointer — find the offset, document it);
- how to respawn in the live world: candidates GameMode `RestartPlayer(Controller)`, the game's checkpoint/respawn
  function, `S_ReviveShell`, or revive-in-place (DeathState→0, health→max, end the death ability, remove the death
  screen widget, restore input) + teleport to the nearest `ASpartaPlayerStart`/`BP_PrologueCheckpoint_C`/rest
  point. DollsMP: game's own restart first, possess+teleport fallback;
- live boss fight detection (`BPC_BossAI_C` aggro & not dead: `IsAlreadyAggroed`, `OnBossAggro`, boss health);
- rest detection (to clear stacks); what currency the game takes on death.
Write findings to `native\briefs\R5-DEATH-FINDINGS.md` as you go (facts with evidence lines).
Do not break the existing `DEATH` verdict (that one is about enemy death mirroring) or `DAMAGE`/`PEERHIT`.
A peer that is down/held should still be shown sensibly: the BODY agent publishes `"bh"` (health/death state) and
plays a death animation on the puppet when the peer's death state ≥ 1; you do not need to do that part.
