// ===========================================================================
// THE CO-OP DEATH POLICY'S TRUTH TABLE, AS A TEST THAT CAN FAIL.
// A port of DollsMP's `test_coop_policy.cpp` policy half (the reflection half of
// that file is about a different engine layer and does not apply here).
//
// WHY THIS FILE EXISTS, IN ONE PARAGRAPH.  The three-case rule's hardest
// property is that its acceptance test must be PER-CASE with OPPOSITE
// expectations: a wipe must reset and a non-wipe must not.  A test that only
// asserts "the level did not reload on a death" PASSES ON THE BROKEN BUILD,
// because a blanket suppression gets cases 2 and 3 right -- and those are the
// deaths a player has most of the time.  So the case that matters most is the
// one this file asserts LOUDEST, and it is asserted against the exact wrong
// implementation (the MUTANT section below).
//
// Built and run by `native\build.bat test`.
// ===========================================================================
#include <stdio.h>

#include "coop_policy.h"

static int g_fail = 0;
static int g_pass = 0;

static void check(const char* what, int got, int want)
{
    if (got == want) {
        ++g_pass;
        return;
    }
    ++g_fail;
    printf("  FAIL  %-58s got %d, want %d\n", what, got, want);
}

// A float comparison that PRINTS BOTH NUMBERS: "0.9 is not 0.9" with no
// precision is not enough to debug a debuff factor by.
static void checkf(const char* what, float got, float want, float tol)
{
    float d = got - want;
    if (d < 0.0f) d = -d;
    if (d <= tol) {
        ++g_pass;
        return;
    }
    ++g_fail;
    printf("  FAIL  %-58s got %.6f, want %.6f (tol %.6f)\n", what, got, want, tol);
}

// ---------------------------------------------------------------------------
// 1. THE WIPE WINS OVER THE BOSS FIGHT.  (Rule 1, and the case the broken build
//    loses.)  Both players down, inside a live boss fight: the party being
//    entirely dead satisfies BOTH conditions, and the wipe takes precedence --
//    so the reset is ALLOWED and the encounter resets.
// ---------------------------------------------------------------------------
static void test_wipe_beats_boss(void)
{
    printf("wipe precedence over an active boss fight\n");
    int c = CoopPartyCase(0 /*self dead*/, 0 /*peer dead*/, 1 /*boss live*/);
    check("party case is 1, not 3", c, COOP_CASE_WIPE);
    check("the reset IS allowed on a wipe", CoopShouldSuppress(1, 0, 1, c), 0);
    check("a wipe accrues no stack", CoopAccruesStack(1, c), 0);
}

// ---------------------------------------------------------------------------
// 2. THE ORDINARY DEATH.  Some dead, at least one alive, no boss fight:
//    respawn with debuff, and the reset is SUPPRESSED.
// ---------------------------------------------------------------------------
static void test_ordinary_death(void)
{
    printf("ordinary death (case 2)\n");
    int c = CoopPartyCase(0, 1, 0);
    check("party case is 2", c, COOP_CASE_RESPAWN);
    check("the reset is suppressed", CoopShouldSuppress(1, 0, 1, c), 1);
    check("the mod respawns by hand", CoopShouldRespawn(1, 0, c), 1);
    check("a stack is accrued", CoopAccruesStack(1, c), 1);
    check("no stack when the rule is disabled", CoopAccruesStack(0, c), 0);
}

// ---------------------------------------------------------------------------
// 3. THE HOLD.  A death inside a live boss fight is HELD even when the partner
//    is alive -- the one place where "some dead, >=1 alive" does NOT produce a
//    respawn.
// ---------------------------------------------------------------------------
static void test_hold_overrides_respawn(void)
{
    printf("hold overrides respawn (case 3)\n");
    int c = CoopPartyCase(0, 1 /*partner ALIVE*/, 1 /*boss live*/);
    check("party case is 3, not 2", c, COOP_CASE_HOLD);
    check("the reset is suppressed", CoopShouldSuppress(1, 0, 1, c), 1);
    check("the mod does NOT respawn a held player", CoopShouldRespawn(1, 0, c), 0);
    check("no stack yet -- the hold can end in VICTORY, which pays",
          CoopAccruesStack(1, c), 0);
}

// ---------------------------------------------------------------------------
// 4. NO DEATH PENDING.  This is the menu's CONTINUE, ordinary travel and the
//    load-in -- all of which go through the SAME call as the death reset.  A
//    suppression not conditioned on a death locks the player out of their save.
// ---------------------------------------------------------------------------
static void test_no_death_never_suppresses(void)
{
    printf("no death pending\n");
    check("case 1, no death -> not suppressed", CoopShouldSuppress(1, 0, 0, 1), 0);
    check("case 2, no death -> not suppressed", CoopShouldSuppress(1, 0, 0, 2), 0);
    check("case 3, no death -> not suppressed", CoopShouldSuppress(1, 0, 0, 3), 0);
    check("case 0, no death -> not suppressed", CoopShouldSuppress(1, 0, 0, 0), 0);
}

// ---------------------------------------------------------------------------
// 5. THE UNKNOWN CASE.  A death whose case has not been computed must NOT
//    suppress.  Letting vanilla handle it is recoverable; suppressing on an
//    unknown state is not.
// ---------------------------------------------------------------------------
static void test_unknown_case_does_not_suppress(void)
{
    printf("unknown case\n");
    check("case 0 -> not suppressed", CoopShouldSuppress(1, 0, 1, 0), 0);
    check("case 0 -> no hand respawn", CoopShouldRespawn(1, 0, 0), 0);
}

// ---------------------------------------------------------------------------
// 6. THE UNKNOWN PEER.  A missing or unreadable peer counts as UNKNOWN, and
//    UNKNOWN IS NOT ALIVE.  The two failure directions are not symmetric:
//    an unknown peer counted as dead makes a real wipe reset (vanilla,
//    recoverable), while counting it as alive suppresses the reset for a party
//    that has actually wiped -- the case-1 defect, arriving through the file
//    system instead of through the branch.
// ---------------------------------------------------------------------------
static void test_unknown_peer_is_not_alive(void)
{
    printf("an unreadable peer counts as NOT alive\n");
    check("UNKNOWN is not alive", CoopPeerAlive(COOP_CL_UNKNOWN), 0);
    check("ALIVE is alive", CoopPeerAlive(COOP_CL_ALIVE), 1);
    check("RESPAWNING is not alive", CoopPeerAlive(COOP_CL_RESPAWNING), 0);
    check("HOLD is not alive", CoopPeerAlive(COOP_CL_HOLD), 0);
    check("WIPED is not alive", CoopPeerAlive(COOP_CL_WIPED), 0);

    // And the consequence, which is the point of the rule.
    int c = CoopPartyCase(0 /*self dead*/, CoopPeerAlive(COOP_CL_UNKNOWN), 0);
    check("self dead + unknown peer -> the WIPE case, so the reset still runs",
          c, COOP_CASE_WIPE);
}

// ---------------------------------------------------------------------------
// 7. THE 3 -> 1 TRANSITION.  The one transition NOT derivable from any
//    individual player's inputs.  Player A dies into a boss fight and is HELD;
//    player B then dies too.  At that instant every player is dead-or-held, and
//    a per-player implementation never sees a new input -- the party stays held
//    FOREVER, silently, because the players who would report it are held.
// ---------------------------------------------------------------------------
static void test_hold_becomes_wipe(void)
{
    printf("the 3 -> 1 transition (the held-forever trap)\n");

    int cA = CoopPartyCase(0, 1, 1);
    check("A is held", cA, COOP_CASE_HOLD);

    int cB = CoopPartyCase(0, 0, 1);
    check("with both down it is a WIPE, not a hold", cB, COOP_CASE_WIPE);
    check("and the reset IS allowed, releasing the held player via the reset",
          CoopShouldSuppress(1, 0, 1, cB), 0);

    // The per-player implementation would still be holding A at case 3.
    int perPlayerWouldSay = COOP_CASE_HOLD;
    check("a per-player implementation is WRONG here", perPlayerWouldSay == cB, 0);
}

// ---------------------------------------------------------------------------
// 8. THE PENDING DEATH IS RE-DECIDED (CoopRecase).  Each exit of the hold, the
//    wipe reached from a respawn countdown, and the two directions that must NOT
//    move.
// ---------------------------------------------------------------------------
static void test_recase_while_pending(void)
{
    printf("a pending death is re-decided from the state as it is now\n");
    check("held, partner still fighting: still held", CoopRecase(3, 0, 1), 3);
    check("held, the fight is over (won or lost): respawn", CoopRecase(3, 0, 0), 2);
    check("held, the partner goes down: WIPE", CoopRecase(3, 1, 1), 1);
    check("held, partner down AND fight over: still a WIPE", CoopRecase(3, 1, 0), 1);
    check("counting down, partner goes down: WIPE", CoopRecase(2, 1, 0), 1);
    check("counting down, a fight starts: NOT pulled into a hold", CoopRecase(2, 0, 1), 2);
    check("counting down, nothing changes: still a respawn", CoopRecase(2, 0, 0), 2);
    check("a wipe is final even if the partner is back up", CoopRecase(1, 0, 0), 1);

    // and the two halves still agree on the re-decided case
    check("a hold released to 2 is respawned by hand",
          CoopShouldRespawn(1, 0, CoopRecase(3, 0, 0)), 1);
    check("a hold turned wipe lets the reset through",
          CoopShouldSuppress(1, 0, 1, CoopRecase(3, 1, 1)), 0);

    // The stack a hold put off lands when the hold resolves into a respawn --
    // because the partner carried the fight.  Never on a wipe.
    check("3 -> 2 charges the stack the hold put off",
          CoopStackOnRecase(1, 3, 2), 1);
    check("3 -> 2 charges nothing with the rule disabled",
          CoopStackOnRecase(0, 3, 2), 0);
    check("3 -> 1 (a wipe) charges nothing", CoopStackOnRecase(1, 3, 1), 0);
    check("2 -> 2 charges nothing again", CoopStackOnRecase(1, 2, 2), 0);

    // the mutant: compute-once, which keeps whatever it had
    int computeOnce = COOP_CASE_HOLD;
    check("compute-once would keep the player held after a win (must differ)",
          computeOnce == CoopRecase(3, 0, 0), 0);
}

// ---------------------------------------------------------------------------
// 9. A PARTY WIPE IS THE HOST'S RESET: the host's reload passes, a guest's own
//    save is not reloaded, and the guest comes back once the host is up.
// ---------------------------------------------------------------------------
static void test_wipe_is_the_hosts_reset(void)
{
    printf("a wipe is the host's reset, the guest follows\n");
    check("wipe: the host's reload passes", CoopShouldSuppressRole(1, 0, 1, 1, 1), 0);
    check("wipe: an undecided role reloads (vanilla direction)",
          CoopShouldSuppressRole(1, 0, 1, 1, -1), 0);
    check("wipe: a GUEST's own-save reload is blocked",
          CoopShouldSuppressRole(1, 0, 1, 1, 0), 1);
    check("wipe: the guest waits while the host is down",
          CoopShouldRespawnRole(1, 0, 1, 0, 0), 0);
    check("wipe: the guest respawns once the host is up",
          CoopShouldRespawnRole(1, 0, 1, 0, 1), 1);
    check("wipe: the host never respawns by hand",
          CoopShouldRespawnRole(1, 0, 1, 1, 1), 0);
    check("case 2 is untouched by the role", CoopShouldRespawnRole(1, 0, 2, 0, 0), 1);
    check("case 3 is untouched by the role", CoopShouldSuppressRole(1, 0, 1, 3, 0), 1);
    check("solo is untouched by the role", CoopShouldSuppressRole(0, 0, 0, 0, 0), 0);
}

// ---------------------------------------------------------------------------
// 10. SESSIONS.  MS2's solo rule, which is the INVERSE of DollsMP's: with no
//     partner this session, the game's own death handling is left alone -- the
//     harness override is the only way to force the single-machine arm.
// ---------------------------------------------------------------------------
static void test_solo_is_vanilla(void)
{
    printf("solo play is vanilla (the MS2 rule)\n");
    check("solo + a death pending -> NOT suppressed",
          CoopShouldSuppress(0, 0, 1, 1), 0);
    check("solo + a death pending -> no hand respawn",
          CoopShouldRespawn(0, 0, 1), 0);
    check("solo + no death pending -> not suppressed",
          CoopShouldSuppress(0, 0, 0, 0), 0);

    // The manual override, which the lab harness arms to exercise the respawn
    // machinery with ONE client.  It must be blunt on purpose.
    check("solo + override armed -> suppressed (the lab arm)",
          CoopShouldSuppress(0, 1, 1, 1), 1);
    check("solo + override armed -> respawned (the lab arm)",
          CoopShouldRespawn(0, 1, 1), 1);

    // WITH A SESSION the case is the answer and the override is irrelevant.
    check("session, case 2, no override -> suppress anyway",
          CoopShouldSuppress(1, 0, 1, 2), 1);
    check("session, case 1, override armed -> do NOT suppress",
          CoopShouldSuppress(1, 1, 1, 1), 0);
}

// ---------------------------------------------------------------------------
// 11. SUPPRESSION AND RESPAWN MUST AGREE, over EVERY case rather than spot
//     checked.  If the reset is suppressed, the mod must be the thing that puts
//     the player back -- otherwise the player is dead in a world that will never
//     reload, which is the worst state this mod can produce.
// ---------------------------------------------------------------------------
static void test_suppress_and_respawn_agree(void)
{
    printf("suppression and respawn agree for every case\n");
    for (int c = 0; c <= 3; ++c) {
        int suppress = CoopShouldSuppress(1, 0, 1, c);
        int respawn  = CoopShouldRespawn(1, 0, c);

        if (c == 1) {
            check("case 1: reload allowed AND no hand respawn",
                  suppress == 0 && respawn == 0, 1);
            continue;
        }
        if (suppress) {
            // case 3 is the one legitimate exception: the player is HELD, which
            // is a deliberate no-respawn state with its own exits.
            check("suppressed implies the mod owns the outcome (respawn or hold)",
                  respawn || c == 3, 1);
        }
    }

    check("case 2 respawns by hand",  CoopShouldRespawn(1, 0, 2), 1);
    check("case 3 does NOT respawn",  CoopShouldRespawn(1, 0, 3), 0);
    check("case 1 does NOT respawn",  CoopShouldRespawn(1, 0, 1), 0);
    check("case 0 does NOT respawn",  CoopShouldRespawn(1, 0, 0), 0);
}

// ---------------------------------------------------------------------------
// 12. THE MUTANT CHECK.  A check that cannot fail is not a check.  Each mutant
//     is a deliberately broken version of the rule, and the assertion is that
//     the mutant DISAGREES with the real rule on the case that matters.  If a
//     mutant agrees everywhere, the rule is not being tested.
// ---------------------------------------------------------------------------

// MUTANT 1: the blanket suppression -- the actual historical implementation.
static int MutantBlanket(int deathPending, int partyCase)
{
    (void)partyCase;
    return deathPending ? 1 : 0;
}

// MUTANT 2: the boss check FIRST, so a wipe inside a boss fight reads as a hold
// and never becomes the reset.
static int MutantBossFirst(int selfAlive, int peerAlive, int bossActive)
{
    if (!selfAlive && bossActive) return 3;
    if (!selfAlive && !peerAlive) return 1;
    return 2;
}

// MUTANT 3: the peer's liveness ignored -- a one-player rule applied to two.
static int MutantIgnoresPeer(int selfAlive, int peerAlive, int bossActive)
{
    (void)peerAlive;
    if (!selfAlive && bossActive) return 3;
    return 2;
}

static void test_mutants_are_caught(void)
{
    printf("mutant checks -- each broken rule must be DETECTED\n");

    check("blanket agrees on case 2 (this is the trap)",
          MutantBlanket(1, 2), CoopShouldSuppress(1, 0, 1, 2));
    check("blanket agrees on case 3 (this is the trap)",
          MutantBlanket(1, 3), CoopShouldSuppress(1, 0, 1, 3));
    check("blanket is CAUGHT on case 1",
          MutantBlanket(1, 1) == CoopShouldSuppress(1, 0, 1, 1), 0);

    check("boss-first agrees on a plain wipe",
          MutantBossFirst(0, 0, 0), CoopPartyCase(0, 0, 0));
    check("boss-first is CAUGHT on a wipe inside a boss fight",
          MutantBossFirst(0, 0, 1) == CoopPartyCase(0, 0, 1), 0);

    check("peer-ignoring agrees on an ordinary death",
          MutantIgnoresPeer(0, 1, 0), CoopPartyCase(0, 1, 0));
    check("peer-ignoring is CAUGHT on a wipe",
          MutantIgnoresPeer(0, 0, 0) == CoopPartyCase(0, 0, 0), 0);
}

// ---------------------------------------------------------------------------
// 13. THE DEATH STACK ARITHMETIC.
// ---------------------------------------------------------------------------
static void test_death_stacks(void)
{
    printf("death stacks: the factor, the cap, the delay, the refund\n");

    checkf("no stacks -> factor 1", CoopDebuffFactor(0, 5, 10), 1.0f, 0.0001f);
    checkf("1 stack of 10% -> 0.9", CoopDebuffFactor(1, 5, 10), 0.9f, 0.0001f);
    checkf("2 stacks of 10% -> 0.8", CoopDebuffFactor(2, 5, 10), 0.8f, 0.0001f);
    checkf("5 stacks of 10% -> 0.5", CoopDebuffFactor(5, 5, 10), 0.5f, 0.0001f);
    // beyond the stack cap nothing more is taken...
    checkf("9 stacks with max 5 -> still 0.5", CoopDebuffFactor(9, 5, 10), 0.5f, 0.0001f);
    // ...and the total is capped at 90 % whatever the settings say.
    checkf("10 stacks of 20% -> the 90% total cap", CoopDebuffFactor(10, 10, 20), 0.1f, 0.0001f);
    checkf("a zero percentage does nothing", CoopDebuffFactor(3, 5, 0), 1.0f, 0.0001f);
    checkf("a negative stack count does nothing", CoopDebuffFactor(-2, 5, 10), 1.0f, 0.0001f);

    check("one stack added", CoopStackAdd(0, 5), 1);
    check("the stack cap holds", CoopStackAdd(5, 5), 5);
    check("a negative count starts at zero", CoopStackAdd(-3, 5), 1);

    check("10 s with no stacks", (int)CoopRespawnDelayMs(10, 5, 0), 10000);
    check("10 + 5 s with one stack", (int)CoopRespawnDelayMs(10, 5, 1), 15000);
    check("10 + 20 s with four stacks", (int)CoopRespawnDelayMs(10, 5, 4), 30000);
    check("the delay is capped at 600 s", (int)CoopRespawnDelayMs(10, 5, 999), 600000);
    check("negative settings cannot rewind the clock", (int)CoopRespawnDelayMs(-5, -5, -1), 0);

    check("pay_death_penalty=1 refunds nothing", CoopRefund(120, 1, 2), 0);
    check("pay_death_penalty=0 refunds on a case-2 respawn", CoopRefund(120, 0, 2), 120);
    check("pay_death_penalty=0 refunds nothing on a wipe", CoopRefund(120, 0, 1), 0);
    check("nothing taken, nothing refunded", CoopRefund(0, 0, 2), 0);
}

// ---------------------------------------------------------------------------
// 14. THE VALUE THAT IS HELD AT A FACTOR, AND IS RE-BASED WHEN THE GAME MOVES
//     IT.  The failure this prevents: multiplying our own result again, so a
//     player at 90 % becomes 81 % -> 73 % without ever dying.
// ---------------------------------------------------------------------------
static void test_debuff_rebasing(void)
{
    printf("the debuffed value is re-based when the GAME changes it\n");
    CoopDebuffVal v;
    CoopDebuffForget(&v);

    float out = 0.0f;
    // first look at 100, factor 1: nothing to write (it is already right)
    check("first look writes nothing", CoopDebuffStep(&v, 100.0f, 1.0f, &out), 0);
    check("  and remembers the natural value", (int)v.base, 100);

    // the factor drops to 0.9 -> write 90
    check("a new factor writes", CoopDebuffStep(&v, 100.0f, 0.9f, &out), 1);
    checkf("  written 90", out, 90.0f, 0.01f);

    // the game reports 90 next frame: that is OUR value, so nothing is written
    check("our own value is not re-multiplied", CoopDebuffStep(&v, 90.0f, 0.9f, &out), 0);

    // the GAME raises the natural value to 200 (an upgrade): re-base and write 180
    check("the game's own change is adopted", CoopDebuffStep(&v, 200.0f, 0.9f, &out), 1);
    checkf("  written 180, not 162", out, 180.0f, 0.01f);
    check("  base is the game's 200", (int)v.base, 200);

    // a factor back to 1 restores the natural value
    check("removing the debuff writes the natural value", CoopDebuffStep(&v, 180.0f, 1.0f, &out), 1);
    checkf("  written 200", out, 200.0f, 0.01f);

    // an unreadable value is left alone
    check("zero is not a value to hold", CoopDebuffStep(&v, 0.0f, 0.9f, &out), 0);
}

// ---------------------------------------------------------------------------
// 15. THE "lv" STATUS WORD.  An empty word is UNKNOWN, never ALIVE: a peer that
//     sends nothing must count as down (see the unknown-peer rule).
// ---------------------------------------------------------------------------
static void test_status_word(void)
{
    printf("the lv status word\n");
    const unsigned w = CoopStatusPack(COOP_CL_HOLD, 3, 0x1234u);
    check("liveness survives the word", CoopStatusLiveness(w), COOP_CL_HOLD);
    check("case survives the word", CoopStatusCase(w), 3);
    check("the death serial keeps its low byte", (int)CoopStatusDeathSerial(w), 0x34);
    check("an empty word is UNKNOWN", CoopStatusLiveness(0u), COOP_CL_UNKNOWN);
    check("an empty word's case is 0", CoopStatusCase(0u), 0);
    // A word that carries only a serial (an older/partial sender) has liveness 0.
    check("a serial-only word is UNKNOWN, not ALIVE",
          CoopStatusLiveness(0x0700u), COOP_CL_UNKNOWN);
    check("and an UNKNOWN peer is not believed alive",
          CoopPeerAlive(CoopStatusLiveness(0x0700u)), 0);

    // A packed word must round-trip for every value the sender can produce.
    int bad = 0;
    for (int l = 0; l <= 7; ++l)
        for (int c = 0; c <= 3; ++c)
            for (unsigned s = 0; s < 256; s += 17)
                if (CoopStatusLiveness(CoopStatusPack(l, c, s)) != l ||
                    CoopStatusCase(CoopStatusPack(l, c, s)) != c ||
                    CoopStatusDeathSerial(CoopStatusPack(l, c, s)) != (s & 0xFF))
                    ++bad;
    check("every liveness/case/serial round-trips", bad, 0);
}

// ---------------------------------------------------------------------------
// 16. THE FULL TRUTH TABLE, printed so a reader can check it by eye against the
//     owner's rule.  Not an assertion -- a witness.  A column that lists an
//     unreachable combination as though it were an answer misleads, so
//     reachability is marked: a pending death on this machine implies self == 0.
// ---------------------------------------------------------------------------
static void print_table(void)
{
    printf("\nthe truth table, for the record\n");
    printf("  self  peer  boss | case | suppress | stack | reachable\n");
    printf("  -----------------+------+----------+-------+----------\n");
    for (int s = 0; s <= 1; ++s)
        for (int p = 0; p <= 1; ++p)
            for (int b = 0; b <= 1; ++b) {
                int c = CoopPartyCase(0 + s, p, b);
                int reachable = (s == 0);
                printf("   %d     %d     %d   |  %d   |    %d     |   %d   | %s\n",
                       s, p, b, c, CoopShouldSuppress(1, 0, 1, c), CoopAccruesStack(1, c),
                       reachable ? "yes" : "NO -- self is alive, so no death pends");
            }
    printf("\n  (self/peer = 1 means that player is ALIVE; boss = 1 means a boss\n");
    printf("   encounter is live for the party.  `suppress` is asked with a death\n");
    printf("   pending, which is the only time the question is meaningful -- and a\n");
    printf("   pending death implies self = 0, which is why the self = 1 rows are\n");
    printf("   marked unreachable rather than presented as answers.)\n\n");
}

int main(void)
{
    printf("== coop_policy: the co-op death rule ==\n");
    test_wipe_beats_boss();
    test_ordinary_death();
    test_hold_overrides_respawn();
    test_no_death_never_suppresses();
    test_unknown_case_does_not_suppress();
    test_unknown_peer_is_not_alive();
    test_hold_becomes_wipe();
    test_recase_while_pending();
    test_wipe_is_the_hosts_reset();
    test_solo_is_vanilla();
    test_suppress_and_respawn_agree();
    test_mutants_are_caught();
    test_death_stacks();
    test_debuff_rebasing();
    test_status_word();
    print_table();

    printf("coop_policy: %d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail) {
        printf("FAIL: the co-op death rule is broken (see the lines above)\n");
        return 1;
    }
    printf("PASS: the co-op death rule's truth table holds, mutants included\n");
    return 0;
}
