// ===========================================================================
// CO-OP DEATH POLICY -- THE PURE DECISION FUNCTIONS (port of DollsMP's
// coop_policy.h + the death half of dmp_rules.h, decision for decision).
//
// WHY THIS IS A SEPARATE HEADER, AND WHY THAT IS NOT TIDINESS.
//
// The owner's rule is a three-case rule, and the whole difficulty is that the
// acceptance test must be PER-CASE with the two expectations OPPOSITE:
//
//     case 1 (party wipe)  -> the reset/reload MUST HAPPEN
//     cases 2 and 3        -> the reset/reload MUST NOT happen
//
// A test that only asserts "the level did not reload on a death" PASSES ON THE
// BROKEN BUILD, because cases 2 and 3 are the deaths a player has most of the
// time and a blanket suppression gets them right. The failure is a party wipe,
// which is the RARER path and is found last.
//
// So the decision lives here as pure functions over explicit inputs, and
// `native\tests\test_coop_policy.cpp` asserts the truth table INCLUDING the case
// the broken build fails.  The header is included by both `coop_death.cpp` and
// the test, so the tested code and the shipped code are THE SAME CODE.
//
// WHAT THIS FILE DELIBERATELY DOES NOT CONTAIN: PvP.  DollsMP's policy has a
// PvP arm (a partner's doll can kill you); this mod has no PvP at all, so the
// `pvpDeath` inputs of DollsMP's rules are dropped rather than passed as a
// constant zero -- a parameter that is always zero documents nothing.
//
// MS2 DIFFERENCE FROM DOLLSMP, STATED HERE BECAUSE IT IS A REAL RULE CHANGE:
//   DollsMP in NO SESSION suppresses the reload and respawns by hand (its
//   single-machine arm).  MS2's owner rule is the opposite for solo play: with
//   no partner this session the game's own death handling is left ALONE --
//   "the owner's game handles solo".  So `CoopShouldSuppress(0, ...)` returns 0
//   by default here; the manual override (`gateArmed`, the harness file) is the
//   only way to force the single-machine arm, and it exists so the respawn
//   machinery can be exercised with ONE client in a lab run.
// ===========================================================================
#ifndef MS2_COOP_POLICY_H
#define MS2_COOP_POLICY_H

// Liveness values, matching the wire vocabulary of the "lv" key (bits 0-2).
// UNKNOWN is a real value and is NOT the same as ALIVE -- see CoopPartyCase.
enum CoopLiveness {
    COOP_CL_UNKNOWN    = 0,
    COOP_CL_ALIVE      = 1,
    COOP_CL_RESPAWNING = 2,   // down, counting down to the co-op respawn (case 2)
    COOP_CL_HOLD       = 3,   // down, held for the partner's boss fight (case 3)
    COOP_CL_WIPED      = 4    // down, party wipe (case 1) or down with the partner gone
};

// Case numbers.  0 = not computed yet (never suppressed on).
enum {
    COOP_CASE_UNKNOWN = 0,
    COOP_CASE_WIPE    = 1,
    COOP_CASE_RESPAWN = 2,
    COOP_CASE_HOLD    = 3
};

// Is this liveness value "the player is up and playing"?
//
// ONLY ALIVE IS ALIVE.  Every other value -- including UNKNOWN -- counts as not
// alive, and that direction is chosen deliberately: a peer we cannot read might
// be dead, and treating it as alive would suppress the full reset for a party
// that has actually wiped.  The two failure directions are not symmetric:
//
//   unknown counted as DEAD  -> a real wipe resets. A two-player session with a
//                               broken peer file behaves like a wipe, which is
//                               vanilla behaviour and recoverable.
//   unknown counted as ALIVE -> a real wipe does NOT reset, which is the exact
//                               case-1 defect, and it is invisible.
inline int CoopPeerAlive(int liveness)
{
    return liveness == COOP_CL_ALIVE;
}

// THE PARTY'S CASE, computed over the whole liveness set.
//
//   selfAlive  -- is THIS machine's player up (i.e. no unresolved death here)?
//   peerAlive  -- is the peer known to be up?  (CoopPeerAlive of its liveness)
//   bossActive -- is a boss encounter live for this party, from its own state?
//
// Returns 1, 2 or 3.
//
// THIS IS THE ONE QUANTITY IN THE DESIGN THAT IS NOT DERIVABLE FROM ANY
// INDIVIDUAL PLAYER'S INPUTS.  Everything else is computed from one player's own
// state and then replicated; the case is computed from the SET.  A design that
// implements `case` per player never fires the 3 -> 1 transition, and the
// failure is a party held forever after a wipe -- silent, because the players
// who would have reported it are the ones being held.
//
// THE TWO PRECEDENCE RULES ARE BOTH ENCODED HERE, and their order is the rules:
//
//   1. A WIPE DURING A BOSS FIGHT IS CASE 1, NOT CASE 3.  The party being
//      entirely dead satisfies both conditions and the wipe wins -- so the
//      encounter's fate is the full reset.  This is why the all-dead test comes
//      FIRST and does not mention the boss at all.
//
//   2. CASE 3 OVERRIDES CASE 2.  A death in a boss fight is held even when a
//      partner is alive -- the one place where "some dead, >=1 alive" does NOT
//      produce a respawn.  Respawning here would put the player back in the
//      arena, which is the exact exploit the hold exists to prevent.
inline int CoopPartyCase(int selfAlive, int peerAlive, int bossActive)
{
    // Rule 1: the wipe wins, unconditionally and regardless of the boss.
    if (!selfAlive && !peerAlive) return COOP_CASE_WIPE;

    // Rule 2: held, because this machine's player is down inside a live fight.
    if (!selfAlive && bossActive) return COOP_CASE_HOLD;

    // Otherwise: at least one player is up and no fight is holding anyone.
    return COOP_CASE_RESPAWN;
}

// THE PREDICATE THE RESET/RELOAD SUPPRESSION ASKS.
//
// Returns true when the death reset must be BLOCKED.  The session gate comes
// first, and it is what stops solo play from being treated as a party of one
// (see the banner: in MS2 solo is vanilla, the inverse of DollsMP's arm).
//
//   no session      -> vanilla, unless the manual override is armed
//   no death pending-> NEVER suppress: the menu's CONTINUE, ordinary travel and
//                      the load-in go through the same call, so a suppression
//                      not conditioned on a pending death locks the player out
//                      of their own save
//   case 1          -> DO NOT suppress: the full reset IS the wipe's design
//   cases 2 and 3   -> suppress: the live world is kept on purpose
//   case 0 (unknown)-> DO NOT suppress: recoverable beats unrecoverable
inline int CoopShouldSuppress(int sessionSeen, int gateArmed, int deathPending, int partyCase)
{
    // NO SESSION: single-machine play.  MS2 leaves it to the game; the override
    // exists only for the harness (see the banner).
    if (!sessionSeen) return gateArmed ? 1 : 0;

    if (!deathPending) return 0;

    // CASE 1 -- ALL PLAYERS DEAD.  DO NOT SUPPRESS.
    if (partyCase == COOP_CASE_WIPE) return 0;

    // Cases 2 and 3 -- respawn-with-debuff, and hold/spectate.  Both want the
    // live world kept, and neither wants the reload.
    if (partyCase == COOP_CASE_RESPAWN || partyCase == COOP_CASE_HOLD) return 1;

    // Case not yet computed.  Err toward NOT suppressing.
    return 0;
}

// SHOULD THIS MACHINE RESPAWN THE PLAYER BY HAND?
//
// The other half of `CoopShouldSuppress`, and the two MUST agree, because they
// are two halves of one operation. A case that suppresses the reset and then
// does NOT respawn leaves the player dead in a world that will never reload --
// the worst state this mod can produce.
//
//   no session -> vanilla, unless the override is armed (which respawns)
//   case 2     -> YES.  There is no reload coming, so the mod is the only thing
//                 that will put the player back on their feet.
//   case 3     -> NO.  The player is HELD; a respawn is what the hold forbids.
//   case 1     -> NO.  The full reset governs and is how the player comes back.
inline int CoopShouldRespawn(int sessionSeen, int gateArmed, int partyCase)
{
    if (!sessionSeen) return gateArmed ? 1 : 0;
    return partyCase == COOP_CASE_RESPAWN;
}

// Does this death accrue a stack of the death cost?
//
// NO on a party wipe: the wipe is already the punishment, and it is the one
// death that is not also fined (DollsMP v1.4.3: "You no longer accumulate rot
// essence when wiping out to a boss").
//
// NO on a hold either -- but for a different reason: a held player has not
// finished dying in the sense that matters, because the hold can end in
// VICTORY.  The stack lands when the hold resolves into a respawn instead
// (CoopStackOnRecase).
//
// `enabled` is the HOST's setting, not the local player's.
inline int CoopAccruesStack(int enabled, int partyCase)
{
    if (!enabled) return 0;
    return partyCase == COOP_CASE_RESPAWN ? 1 : 0;
}

// A pending death whose case CHANGED.  A player held in a boss fight (3) that
// the partner then won is released into a respawn (2): that gains the stack the
// hold put off.  A wipe (-> 1) gains none.
inline int CoopStackOnRecase(int enabled, int from, int to)
{
    return (enabled && from == COOP_CASE_HOLD && to == COOP_CASE_RESPAWN) ? 1 : 0;
}

// Stacks after one more, capped.
inline int CoopStackAdd(int stacks, int maxStacks)
{
    if (maxStacks < 0) maxStacks = 0;
    if (stacks < 0) stacks = 0;
    return stacks + 1 > maxStacks ? maxStacks : stacks + 1;
}

// ---------------------------------------------------------------------------
// A PENDING DEATH IS RE-DECIDED WHILE IT IS PENDING.
//
// The case used to be computed once, at the moment of the death, and never
// again.  That is an event-list design, and it has two silent failures:
//   * a player held in a boss fight (3) whose partner then wins stays held for
//     good: nothing ever turns the 3 into a 2;
//   * a player held (3) or counting down (2) whose partner then dies is a party
//     WIPE, but this machine never learns it: its own reset was blocked at its
//     own death and nothing asks again.
// So while a death is pending the case is re-asked from the state as it is NOW:
//
//   cur            the case the death has now (1, 2 or 3)
//   peerDown       the partner is KNOWN to be down (its published liveness says
//                  RESPAWNING / HOLD / WIPED), or the session has been lost
//   encounterLive  a boss fight is live for this party: on this machine OR on
//                  the partner's (the dying player's own game may end its boss
//                  mode on its own death, so this machine alone is not enough)
//
// The order is the rules:
//   1. a wipe is final -- the reset is under way and nothing undoes it;
//   2. the partner going down makes it a wipe, from 2 as well as from 3;
//   3. a hold whose encounter is no longer live becomes a respawn.  Never the
//      other way: a player already counting down is not pulled into a hold by a
//      fight that starts afterwards.
inline int CoopRecase(int cur, int peerDown, int encounterLive)
{
    if (cur == COOP_CASE_WIPE) return COOP_CASE_WIPE;
    if (peerDown) return COOP_CASE_WIPE;
    if (cur == COOP_CASE_HOLD && !encounterLive) return COOP_CASE_RESPAWN;
    return cur;
}

// ---------------------------------------------------------------------------
// A PARTY WIPE IS THE HOST'S RESET.
//
// Both machines reloading their OWN save comes back to different places: each
// save answers "where was THIS save last rested", and only one of the two saves
// is the world's (the host's).  So on a wipe:
//   * the HOST resets/reloads (its own death reset passes, or it is asked again);
//   * a GUEST does not reload its own save: the reload is blocked, and the guest
//     is put back on its feet in the live world once the host is up again -- by
//     then the host's world is the reset one, and the guest's copies follow the
//     host's table (enemies revived, the dead list cleared), which is the reset
//     arriving the host-authoritative way.
// `isHost` < 0 (the role has not been decided) counts as the host: the vanilla
// reload is the recoverable direction.
inline int CoopShouldSuppressRole(int sessionSeen, int gateArmed, int deathPending, int partyCase,
                                  int isHost)
{
    if (sessionSeen && deathPending && partyCase == COOP_CASE_WIPE && isHost == 0) return 1;
    return CoopShouldSuppress(sessionSeen, gateArmed, deathPending, partyCase);
}

inline int CoopShouldRespawnRole(int sessionSeen, int gateArmed, int partyCase, int isHost,
                                 int hostIsUp)
{
    if (sessionSeen && partyCase == COOP_CASE_WIPE && isHost == 0) return hostIsUp;
    return CoopShouldRespawn(sessionSeen, gateArmed, partyCase);
}

// ---------------------------------------------------------------------------
// THE DEATH STACK ARITHMETIC (DollsMP dmp_rules.h section 1).
// ---------------------------------------------------------------------------
// The share of max health (or shell health) a player keeps with `stacks` stacks
// of `pct` % each.  The stacks are capped at `maxStacks`, the total at 90 % (a
// player is never left with nothing).
inline float CoopDebuffFactor(int stacks, int maxStacks, int pct)
{
    if (stacks <= 0 || pct <= 0) return 1.0f;
    if (maxStacks < 0) maxStacks = 0;
    if (stacks > maxStacks) stacks = maxStacks;
    int total = stacks * pct;
    if (total > 90) total = 90;
    if (total < 0) total = 0;
    return 1.0f - (float)total / 100.0f;
}

// How long after the death the co-op respawn comes: respawn_delay_s plus
// respawn_delay_per_stack_s for every stack the player already held when they
// died.  Capped at 600 s.
inline unsigned CoopRespawnDelayMs(int baseS, int perStackS, int stacksHeld)
{
    if (baseS < 0) baseS = 0;
    if (perStackS < 0) perStackS = 0;
    if (stacksHeld < 0) stacksHeld = 0;
    long long ms = ((long long)baseS + (long long)perStackS * stacksHeld) * 1000ll;
    if (ms > 600000ll) ms = 600000ll;
    return (unsigned)ms;
}

// How much of what the game took at a death to give back.  pay_death_penalty=1
// keeps the game's own penalty (0 refunded); 0 gives it all back on a case-2
// respawn.  A wipe refunds nothing even with the setting off: the reset is the
// wipe's punishment and the currency loss is part of it.
inline int CoopRefund(int took, int payPenalty, int partyCase)
{
    if (took <= 0) return 0;
    if (partyCase == COOP_CASE_RESPAWN && !payPenalty) return took;
    return 0;
}

// ---------------------------------------------------------------------------
// ONE ATTRIBUTE HELD AT ITS NATURAL VALUE TIMES A FACTOR.
//
// What the mod writes is remembered.  When the value read is not what was
// written, the GAME set it (a new shell, an upgrade, a refresh, a boss's next
// form): that value is the new natural one and the factor is applied to it. So
// the mod never multiplies its own result again, and never undoes something the
// game did.
// ---------------------------------------------------------------------------
struct CoopDebuffVal {
    float base = 0.0f;      // the natural value (what the game set)
    float written = 0.0f;   // what the mod last wrote (== base * factor)
    float factor = 1.0f;    // the factor `written` was made with
    int   have = 0;
};

inline void CoopDebuffForget(CoopDebuffVal* v)
{
    v->base = v->written = 0.0f;
    v->factor = 1.0f;
    v->have = 0;
}

inline int CoopNear(float a, float b)
{
    float d = a - b; if (d < 0.0f) d = -d;
    float m = (a < 0.0f ? -a : a); if (m < 1.0f) m = 1.0f;
    return d <= 0.5f || d <= m * 0.0001f;
}

inline int CoopSameFactor(float a, float b)
{
    float d = a - b; if (d < 0.0f) d = -d;
    return d < 0.0001f;
}

// One look at the value `cur`.  `want` is the factor it should carry.  Returns 1
// and the value to write in *out when a write is due.
inline int CoopDebuffStep(CoopDebuffVal* v, float cur, float want, float* out)
{
    if (!(cur > 0.0f) || cur > 1.0e9f) return 0;              // nothing usable to hold
    if (!v->have || !CoopNear(cur, v->written)) {
        // first look, or the game set it: this is the natural value
        v->base = cur; v->written = cur; v->factor = 1.0f; v->have = 1;
    }
    if (CoopSameFactor(want, v->factor)) return 0;
    float target = v->base * want;
    if (target < 1.0f) target = 1.0f;
    v->factor = want;
    v->written = target;
    *out = target;
    return 1;
}

// ---------------------------------------------------------------------------
// THE "lv" STATUS WORD (the wire contract in R5-DEATH.md):
//   bits 0-2  liveness   bits 4-5  party case   bits 8-15  death serial (mod 256)
// An empty word is UNKNOWN and case 0 -- never ALIVE.
// ---------------------------------------------------------------------------
inline unsigned CoopStatusPack(int liveness, int partyCase, unsigned deathSerial)
{
    unsigned w = 0;
    w |= (unsigned)(liveness & 0x7);
    w |= (unsigned)((partyCase & 0x3) << 4);
    w |= (unsigned)((deathSerial & 0xFF) << 8);
    return w;
}

inline int CoopStatusLiveness(unsigned w) { return (int)(w & 0x7); }
inline int CoopStatusCase(unsigned w)     { return (int)((w >> 4) & 0x3); }
inline unsigned CoopStatusDeathSerial(unsigned w) { return (unsigned)((w >> 8) & 0xFF); }

#endif  // MS2_COOP_POLICY_H
