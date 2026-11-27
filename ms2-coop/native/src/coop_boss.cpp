// ---------------------------------------------------------------------------
// coop_boss.cpp -- Round 7, BOSS lane (gap G7 of docs/HANDOFF-REMAINING-SYNC.md).
//
// THE PROBLEM.  A boss is one actor with two independent copies (one per client),
// and its FIGHT STATE is entirely local:
//   * `UBPC_BossAI_C` (an actor component, `/Game/Sparta/Core/AI/Components/
//     BPC_BossAI.BPC_BossAI_C`) owns `bTriggeredSecondPhase` @0xC0,
//     `bTriggeredThirdPhase` @0xC1, `bIsAggroed` @0xC2, the two thresholds
//     `SecondPhaseHealthPercent` @0xF8 / `ThirdPhaseHealthPercent` @0x100 and the
//     delegates `OnSecondPhaseStarted` @0x118 / `OnThirdPhaseStarted` @0x260
//     (research/mine/SYNC-DATA-MINE.md section 15, BPC_BossAI.hpp);
//   * the transition is DECIDED locally by two blueprints UFunctions,
//     `ShouldStartSecondPhase()` and `ShouldStartThirdPhase()`, which the boss's
//     own damage event calls.  They read the health percent, set the flag and
//     broadcast the delegate, whose bound handlers start `GA_*_PhaseChange_C`,
//     push the phase camera state and swap the boss's move set.
//
// The enemy health mirror (`coop_combat.cpp`: "nhp" + SetHealthNorm) lands the
// NUMBER on the peer but cannot land the DECISION: a health value written with
// SetHealthNorm raises no damage event, so the peer's `ShouldStartSecondPhase`
// is never asked and the peer fights a phase-1 boss with phase-1 abilities while
// the owner is in phase 3.  That is the gap this file closes.
//
// WHAT THIS DOES.
//   1. DETECT, on the owner, the moment the game decides: a POST hook on
//      `BPC_BossAI_C.ShouldStartSecondPhase` / `ShouldStartThirdPhase`.  The hook
//      itself does nothing but raise a flag (it runs inside the ProcessEvent
//      detour); the 15 Hz publish tick then diffs the two flags against what this
//      module last accounted for and turns a change into an event.  A 1 Hz poll of
//      the same flags is the safety net for a build where the decision is inlined
//      into the damage event instead of calling the function.
//   2. PUBLISH, from the owner only, `,"bsp":[["<tail>","<class>",phase,seq],...]`
//      -- its OWN top-level key, appended last, only when there is something to
//      say, repeated for a couple of seconds so one lost packet is harmless and
//      carrying a per-publisher seq so a repeated one is harmless too.
//   3. APPLY, on the peer, with the GAME'S OWN function: the local copy's health
//      has already been lowered by the nhp mirror when this runs, so calling
//      `ShouldStartSecondPhase()` / `ShouldStartThirdPhase()` lets the game's own
//      code set the flag, broadcast the delegate and start the phase ability --
//      no raw byte write, and the phase visuals/moves are the real ones.
//
// WHY IT CANNOT FIRE TWICE (the health/death mirror consistency requirement).
// The transition is guarded by the game's OWN `bTriggered*Phase` flags, i.e. by
// the same state on both sides: after the peer applies phase 2, its own later
// health-driven check early-outs.  The echo of our own call (our call goes through
// ProcessEvent, so our own POST hook sees it) is suppressed by comparing the flip
// against `appliedPhase[key]`: a flip this module caused is never published back.
//
// IDENTITY.  The key is `net::Tail(owner)` -- byte-identical to the key the enemy
// health mirror publishes in "nhp", so the phase event and the health value that
// gates it are about the same local actor.  A runtime-SUMMONED boss has a
// per-client name (that is why spawnReplay has the "SP_<spawner>" alias), so the
// event also carries the class short name and the receiver falls back to it when
// exactly one local boss has that class (logged).
//
// Kill switch: BOSS_OFF.txt (no "bsp" on the wire, no phase call, no hooks).
// Everything here runs on the game thread (the hook, the publish tick and the
// apply dispatch are all on it), so there is no locking and no UObject is ever
// touched from another thread.
// ---------------------------------------------------------------------------

#include "ms2_ue.h"
#include "coop_state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace coop {

namespace {

// ===========================================================================
// small utilities (the same shape the other modules use)
// ===========================================================================

inline State& St() { return state(); }
inline const Toggles& TG() { return state().toggles; }
inline BossState& BS() { return state().boss; }

bool Valid(ue::UObject* o) { return o != nullptr && ue::IsValid(o); }

// The boss component is a UBPC_BossAI_C on a boss character.  coop_death's
// ScanBoss uses the same class name, so the class object is already known good.
const char* const kBossCompClass = "BPC_BossAI_C";

// How long one event keeps being re-published (15 Hz x 2 s = 30 sends).  The
// receiver dedupes by (key, seq), so this is pure loss insurance -- it also
// covers the peer's boss still spawning, or a load hitch on its side.
constexpr double kBossEventHold = 2.0;
constexpr std::size_t kBossEventMax = 8;

// The retry budget for "the gate said no": the phase function refuses while the
// local health percent is still above the threshold, and the health mirror lands
// on its own 15 Hz schedule.  0.5 s x 12 = 6 s is comfortably longer than that.
constexpr int kRetryMax = 12;
constexpr double kRetryEvery = 0.5;

// Phase numbers.  1 = the fight's first phase, 2/3 = the two triggered phases.
constexpr int kPhaseMax = 3;

// second -> the normalised 0..1 threshold the health percent is compared with.
// The authored value is 0..1 in this build (a double property), but a project
// that authors 60 is handled too: the value is logged either way by `bossdump`.
double NormThreshold(double v) {
    if (!std::isfinite(v) || v <= 0.0) return 0.0;
    return (v > 1.5) ? (v / 100.0) : v;
}

// ---------------------------------------------------------------------------
// component readers
// ---------------------------------------------------------------------------

bool CompBool(ue::UObject* comp, const char* prop, bool* out) {
    if (out) *out = false;
    if (!Valid(comp)) return false;
    // A missing property is a failure; a present BoolProperty carries its own
    // ByteMask/FieldMask, which ue::GetBool applies (memcpy-ing a bool over a
    // shared byte would read every neighbour bit as well).
    if (ue::PropOffset(comp, prop) < 0) return false;
    const bool v = ue::GetBool(comp, prop);
    if (out) *out = v;
    return true;
}

bool CompDouble(ue::UObject* comp, const char* prop, double* out) {
    if (out) *out = 0.0;
    if (!Valid(comp)) return false;
    const int32_t sz = ue::PropSize(comp, prop);
    if (sz == 8) return ue::Get<double>(comp, prop, *out);
    if (sz == 4) {
        float f = 0.0f;
        if (!ue::Get<float>(comp, prop, f)) return false;
        if (out) *out = (double)f;
        return true;
    }
    return false;
}

// 3 if the third phase fired, 2 if the second, 1 otherwise.  A jump straight to
// phase 3 (a scripted phase change) reports 3, which is what the peer applies.
int PhaseOf(ue::UObject* comp) {
    bool second = false;
    bool third = false;
    const bool ok2 = CompBool(comp, "bTriggeredSecondPhase", &second);
    const bool ok3 = CompBool(comp, "bTriggeredThirdPhase", &third);
    if (!ok2 && !ok3) return 0;      // neither readable: not a boss AI component
    if (ok3 && third) return 3;
    if (ok2 && second) return 2;
    return 1;
}

bool CallVoid(ue::UObject* o, const char* fnName) {
    if (!Valid(o)) return false;
    ue::UFunction* fn = ue::FindFunction(o, fnName);
    if (!fn) return false;
    ue::Params p(fn);
    return ue::Call(o, fn, p.data());
}

ue::UObject* CallObject(ue::UObject* o, const char* fnName) {
    if (!Valid(o)) return nullptr;
    ue::UFunction* fn = ue::FindFunction(o, fnName);
    if (!fn) return nullptr;
    ue::Params p(fn);
    if (!ue::Call(o, fn, p.data())) return nullptr;
    ue::UObject* r = p.Get<ue::UObject*>("ReturnValue");
    return Valid(r) ? r : nullptr;
}

// The owning character of a BPC_BossAI_C.  GetOwner() first (what coop_death's
// ScanBoss uses); the component's own BossAICharacter property @0xC8 is the
// fallback for a component that is not attached the usual way.
ue::UObject* OwnerOf(ue::UObject* comp) {
    ue::UObject* owner = CallObject(comp, "GetOwner");
    if (Valid(owner)) return owner;
    ue::UObject* ch = nullptr;
    if (ue::Get<ue::UObject*>(comp, "BossAICharacter", ch) && Valid(ch)) return ch;
    return nullptr;
}

// "BP_DwarfBoss_C" from the owner's class full name (or "" when unreadable).
std::string ClassShortOf(ue::UObject* actor) {
    if (!Valid(actor)) return std::string();
    ue::UClass* c = ue::GetClass(actor);
    if (!c) return std::string();
    const std::string full = ue::GetFullName((ue::UObject*)c);
    const std::string name = ue::GetName((ue::UObject*)c);
    return name.empty() ? full : name;
}

// ---------------------------------------------------------------------------
// THE SCAN -- the one place that reads engine state for this module
// ---------------------------------------------------------------------------

// Rebuilds BS().bosses from the live BPC_BossAI_C components.  Keeps `lastPhase`
// of an entry whose key survives (it is this module's accounting of a transition,
// not a copy of the game's flag), so a rescan never re-announces a phase.
void Scan() {
    BossState& B = BS();
    std::vector<BossEntry> next;

    for (ue::UObject* comp : ue::FindAllOf(kBossCompClass)) {
        if (!Valid(comp)) continue;
        ue::UObject* actor = OwnerOf(comp);
        if (!Valid(actor)) continue;
        const std::string key = net::Tail(actor);
        if (key.empty() || key == "<nil>") continue;

        BossEntry e;
        e.comp = comp;
        e.actor = actor;
        e.key = key;
        e.cls = ClassShortOf(actor);
        e.phase = PhaseOf(comp);
        if (e.phase == 0) continue;             // not a boss AI component after all
        e.hp = combat::HealthNorm(actor, nullptr);
        CompDouble(comp, "SecondPhaseHealthPercent", &e.t2);
        CompDouble(comp, "ThirdPhaseHealthPercent", &e.t3);
        e.t2 = NormThreshold(e.t2);
        e.t3 = NormThreshold(e.t3);
        CompBool(comp, "bIsAggroed", &e.aggro);
        e.lastPhase = e.phase;

        // inherit this module's accounting from the previous pass (same key)
        for (const BossEntry& old : B.bosses) {
            if (old.key == e.key) {
                e.lastPhase = old.lastPhase;
                break;
            }
        }
        next.push_back(e);
    }

    B.bosses.swap(next);
    B.scanned += 1;

    // A key that left the world (the boss died and was destroyed, or the map was
    // unloaded) must not keep its "already applied" value: the next copy of that
    // boss is a new fight.  The kill switch side of the house does not care.
    if (!B.bosses.empty()) return;
    B.appliedPhase.Clear();
    B.retries.clear();
}

// Finds the local boss a peer event is about: the exact key first, then the class
// short name when exactly one local boss carries it (a runtime-summoned boss has
// a per-client name, see the file header).  Ambiguous classes are refused.
BossEntry* FindBoss(const std::string& key, const std::string& cls, bool* byClass) {
    BossState& B = BS();
    if (byClass) *byClass = false;
    for (BossEntry& e : B.bosses) {
        if (e.key == key) return &e;
    }
    if (cls.empty()) return nullptr;
    BossEntry* hit = nullptr;
    for (BossEntry& e : B.bosses) {
        if (e.cls != cls) continue;
        if (hit) return nullptr;               // two candidates: do not guess
        hit = &e;
    }
    if (hit && byClass) *byClass = true;
    return hit;
}

// ---------------------------------------------------------------------------
// DETECT / PUBLISH
// ---------------------------------------------------------------------------

// Appends one event, bumping the local seq for that key.  Ignored (a no-op) when
// the same phase is already pending, which is what a hook + poll race looks like.
void QueueEvent(const BossEntry& e, int phase) {
    BossState& B = BS();
    for (const BossEvent& ev : B.pending) {
        if (ev.key == e.key && ev.phase == phase) return;
    }
    BossEvent ev;
    ev.key = e.key;
    ev.cls = e.cls;
    ev.phase = phase;
    // A per-key monotonic seq: the receiver only needs "is this newer than the last
    // one I took from you", and it must not collide with the seq WE take from the
    // peer, so the two counters live in two maps (localSeq / seenSeq).
    ev.seq = B.localSeq.Get(e.key, 0) + 1;
    B.localSeq.Set(e.key, ev.seq);
    ev.at = net::Now();
    if (B.pending.size() >= kBossEventMax) B.pending.erase(B.pending.begin());
    B.pending.push_back(ev);
    B.published += 1;
    net::Log("bossphase: publish %s (%s) phase %d seq %d hp=%.2f%s%s", e.key.c_str(),
             e.cls.empty() ? "?" : e.cls.c_str(), phase, ev.seq,
             e.hp < 0.0 ? -1.0 : e.hp, e.aggro ? " aggro" : "",
             phase >= 3 ? " (third)" : " (second)");
}

// Drops the events that have been on the wire long enough.
void ExpireEvents() {
    BossState& B = BS();
    const double now = net::Now();
    for (std::size_t i = 0; i < B.pending.size();) {
        if (now - B.pending[i].at > kBossEventHold) {
            B.pending.erase(B.pending.begin() + (std::ptrdiff_t)i);
        } else {
            ++i;
        }
    }
}

// ---------------------------------------------------------------------------
// APPLY
// ---------------------------------------------------------------------------

// The two decisions, by phase.  Third uses its own function; a phase-3 event that
// arrives without phase 2 having been applied also gets phase 2 first when its
// flag is still clear, because the third-phase handler assumes the second ran.
const char* PhaseFunction(int phase) {
    return (phase >= 3) ? "ShouldStartThirdPhase" : "ShouldStartSecondPhase";
}

bool CallPhase(BossEntry* e, int phase) {
    if (!e || !Valid(e->comp)) return false;
    if (!CallVoid(e->comp, PhaseFunction(phase))) {
        net::Log("WARN bossphase: %s has no %s function", e->key.c_str(), PhaseFunction(phase));
        return false;
    }
    return true;
}

// The peer said phase N for this boss.  Marks it applied BEFORE the call so the
// echo our own POST hook is about to observe is never published back, then calls
// the game's own function.  Returns true when the local copy reached that phase.
bool ApplyPhase(const std::string& key, const std::string& cls, int phase, int seq) {
    BossState& B = BS();
    if (phase < 2 || phase > kPhaseMax) return false;

    bool byClass = false;
    BossEntry* e = FindBoss(key, cls, &byClass);
    if (!e) {
        B.missed += 1;
        // Once per key, not once per packet: the event is re-sent 30 times.
        static StrSet logged;
        if (!logged.Has(key)) {
            logged.Add(key);
            net::Log("bossphase: no local boss for %s (%s) -- phase %d kept unapplied",
                     key.c_str(), cls.empty() ? "?" : cls.c_str(), phase);
        }
        return false;
    }
    if (byClass) {
        net::Log("bossphase: %s matched by class %s (local key %s)", key.c_str(), cls.c_str(),
                 e->key.c_str());
    }

    e->lastPhase = std::max(e->lastPhase, phase);
    if (e->phase >= phase) {
        B.appliedPhase.Set(key, phase);
        if (e->key != key) B.appliedPhase.Set(e->key, phase);
        net::Log("bossphase: peer phase %d for %s already local (phase %d) -- nothing to do",
                 phase, e->key.c_str(), e->phase);
        return true;
    }

    // Suppress the echo: this flip is ours.  Both keys are marked when the class
    // fallback matched a differently-named local copy -- the echo check runs on the
    // LOCAL key, and a missed mark would publish our own apply back to the owner.
    B.appliedPhase.Set(key, phase);
    if (e->key != key) B.appliedPhase.Set(e->key, phase);

    // THE GATE.  Both functions refuse while the local health percent is above the
    // threshold they compare against, and the health mirror lands on its own
    // schedule (it lowers the value only when the owner's is lower).  A phase-2
    // event therefore means "the owner took this boss under SecondPhaseHealthPercent",
    // so put the local copy there with the SAME writer the mirror uses -- a phase
    // change at full health would be a lie the peer's own UI would show.  Nothing is
    // written when the value is already below the threshold.
    const double thr = (phase >= 3) ? e->t3 : e->t2;
    if (thr > 0.0 && e->hp >= 0.0 && e->hp > thr) {
        const double target = std::max(0.01, thr - 0.02);
        const bool ok = combat::SetHealthNorm(e->actor, target);
        net::Log("bossphase: %s hp %.3f -> %.3f for the phase-%d gate %.3f (%s)", e->key.c_str(),
                 e->hp, target, phase, thr, ok ? "ok" : "FAILED");
        e->hp = combat::HealthNorm(e->actor, nullptr);
    } else if (thr <= 0.0) {
        net::Log("bossphase: %s phase-%d threshold unread (t2=%.3f t3=%.3f) -- asking the "
                 "function anyway", e->key.c_str(), phase, e->t2, e->t3);
    }

    const bool called = CallPhase(e, phase);
    const int nowPhase = PhaseOf(e->comp);
    e->phase = nowPhase;
    if (called && nowPhase >= phase) {
        B.applied += 1;
        e->lastPhase = nowPhase;
        net::Log("bossphase: APPLIED phase %d on %s (%s) via %s -- flags second=%d third=%d",
                 phase, e->key.c_str(), e->cls.c_str(), PhaseFunction(phase),
                 (nowPhase >= 2) ? 1 : 0, (nowPhase >= 3) ? 1 : 0);
        return true;
    }

    // Keep trying (the health mirror is the likely missing half).
    BossRetry r;
    r.key = key;
    r.cls = cls;
    r.phase = phase;
    r.tries = 1;
    r.nextAt = net::Now() + kRetryEvery;
    for (BossRetry& old : B.retries) {
        if (old.key == key && old.phase == phase) {
            old.nextAt = r.nextAt;
            old.tries += 1;
            return false;
        }
    }
    B.retries.push_back(r);
    net::Log("WARN bossphase: %s did not reach phase %d (hp=%.2f thr=%.2f) -- retrying",
             e->key.c_str(), phase, e->hp, (phase >= 3) ? e->t3 : e->t2);
    return false;
}

// Drains the retry list.  Runs on the publish tick, so it keeps going even when
// the peer stopped sending (the event is already parsed at that point).
void TickRetries() {
    BossState& B = BS();
    if (B.retries.empty()) return;
    const double now = net::Now();
    for (std::size_t i = 0; i < B.retries.size();) {
        BossRetry& r = B.retries[i];
        if (now < r.nextAt) {
            ++i;
            continue;
        }
        BossEntry* e = FindBoss(r.key, r.cls, nullptr);
        if (!e) {
            B.retries.erase(B.retries.begin() + (std::ptrdiff_t)i);
            continue;
        }
        r.tries += 1;
        r.nextAt = now + kRetryEvery;
        const bool called = CallPhase(e, r.phase);
        const int nowPhase = PhaseOf(e->comp);
        e->phase = nowPhase;
        if (called && nowPhase >= r.phase) {
            B.applied += 1;
            e->lastPhase = nowPhase;
            net::Log("bossphase: APPLIED phase %d on %s (%s) on retry %d", r.phase,
                     e->key.c_str(), e->cls.c_str(), r.tries);
            B.retries.erase(B.retries.begin() + (std::ptrdiff_t)i);
            continue;
        }
        if (r.tries >= kRetryMax) {
            B.refused += 1;
            net::Log("WARN bossphase: GAVE UP on phase %d for %s after %d tries (hp=%.2f) -- "
                     "the game's gate never opened", r.phase, e->key.c_str(), r.tries, e->hp);
            B.retries.erase(B.retries.begin() + (std::ptrdiff_t)i);
            continue;
        }
        ++i;
    }
}

// ---------------------------------------------------------------------------
// THE HOOKS
// ---------------------------------------------------------------------------

// A POST hook on the two decision functions.  It runs inside the ProcessEvent
// detour on the game thread, so it must do almost nothing: raise the flag and
// count.  The 15 Hz publish tick does the diffing (and can therefore also see a
// transition the hook missed, e.g. one performed by an inlined blueprint node).
void PhaseHook(ue::UObject* self, ue::UFunction* fn, void* /*params*/) {
    BossState& B = BS();
    B.flipSeen = true;
    B.hookFires += 1;
    if (B.hookFires <= 6) {
        const std::string comp = Valid(self) ? ue::GetName(self) : std::string("?");
        const std::string name = fn ? ue::GetName((ue::UObject*)fn) : std::string("?");
        net::Log("bossphase: hook fired %s on %s (fire #%d)", name.c_str(), comp.c_str(),
                 B.hookFires);
    }
}

// Installs the two POST hooks.  The blueprint class is loaded with the level, so
// this is retried from the publish tick until it resolves (the same pattern the
// death module uses for its hooks).  A build that resolves neither still works
// through the 1 Hz flag poll.
bool g_hooksTried = false;

void EnsureHooks() {
    BossState& B = BS();
    if (!TG().bossSync) return;
    if (g_hooksTried) return;
    g_hooksTried = true;
    int ok = 0;
    if (ue::HookPost("BPC_BossAI_C.ShouldStartSecondPhase", &PhaseHook)) ++ok;
    if (ue::HookPost("BPC_BossAI_C.ShouldStartThirdPhase", &PhaseHook)) ++ok;
    if (ok != 2) {
        B.hookFail = 1;
        g_hooksTried = false;      // the class may not be loaded yet: retry
        net::Log("bossphase: phase hooks %d of 2 resolved (retrying; the flag poll still covers it)",
                 ok);
    } else {
        net::Log("bossphase: phase hooks installed (BPC_BossAI_C.ShouldStartSecondPhase/"
                 "ShouldStartThirdPhase)");
    }
}

// ---------------------------------------------------------------------------
// the 1 Hz flag poll and the diff that turns a flip into an event
// ---------------------------------------------------------------------------

// True when this local flip is the ECHO of a phase this module applied from the
// peer.  `appliedPhase[key]` is set before the call that causes the flip, so an
// equal phase there is ours by construction.
bool IsEcho(const BossEntry& e, int phase) {
    BossState& B = BS();
    const int applied = B.appliedPhase.Get(e.key, 0);
    if (applied >= phase) {
        B.echoes += 1;
        return true;
    }
    return false;
}

void DiffAndPublish() {
    BossState& B = BS();
    for (BossEntry& e : B.bosses) {
        if (e.phase == e.lastPhase) continue;
        const int from = e.lastPhase;
        if (e.phase < from) {
            // The flags went backwards: a respawned boss copy, or a world reset
            // that reused the name.  Not a transition this module publishes.
            e.lastPhase = e.phase;
            B.appliedPhase.Erase(e.key);
            continue;
        }
        const int to = e.phase;
        e.lastPhase = to;
        if (IsEcho(e, to)) {
            net::Log("bossphase: %s phase %d->%d is our own apply (echo suppressed)",
                     e.key.c_str(), from, to);
            continue;
        }
        QueueEvent(e, to);
    }
}

// ===========================================================================
// the commands (cmd.txt): bossphase dump|force|sim|spawn
// ===========================================================================

std::string TrimArg(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ') ++b;
    while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
    return s.substr(b, e - b);
}

// "dump" / "dump self" / "dump <tail>": one line per boss, plus the counters.
void CmdDump(const std::string& arg) {
    const std::string a = TrimArg(arg);
    BossState& B = BS();
    Scan();
    if (B.bosses.empty()) {
        net::Log("bossdump: no BPC_BossAI_C in this world (%d scan(s)); `bossphase spawn` "
                 "summons one, `bossphase dump` re-reads", B.scanned);
        return;
    }
    for (const BossEntry& e : B.bosses) {
        if (!a.empty() && a != "all" && e.key.find(a) == std::string::npos) continue;
        // [R10-BOSSLOC] where the boss stands and how far from us (lab diagnostics)
        ue::UObject* actor = OwnerOf(e.comp);
        const auto bl = net::Loc(actor);
        const auto ml = net::Loc(net::Pawn());
        const double dx = bl.X - ml.X, dy = bl.Y - ml.Y, dz = bl.Z - ml.Z;
        net::Log("bossdump: %s cls=%s phase=%d hp=%.3f t2=%.3f t3=%.3f aggro=%d at (%.0f %.0f %.0f) d=%.0f comp=%s",
                 e.key.c_str(), e.cls.c_str(), e.phase, e.hp, e.t2, e.t3, e.aggro ? 1 : 0,
                 bl.X, bl.Y, bl.Z, std::sqrt(dx * dx + dy * dy + dz * dz),
                 net::FullName(e.comp).c_str());
    }
    net::Log("bossdump: n=%d scans=%d hook=%d pub=%d applied=%d fail=%d missed=%d echoes=%d "
             "pending=%d retries=%d",
             (int)B.bosses.size(), B.scanned, B.hookFires, B.published, B.applied, B.refused,
             B.missed, B.echoes, (int)B.pending.size(), (int)B.retries.size());
}

// "force <2|3> [tail]": make the LOCAL boss change phase through the game's own
// function -- the test verb the task asks for when no boss is reachable.  The
// health is lowered to just under the threshold first, because that is the gate
// the function checks; the POST hook then fires and the event goes out.
void CmdForce(const std::string& arg) {
    const std::string a = TrimArg(arg);
    const int phase = (int)::strtol(a.c_str(), nullptr, 10);
    if (phase < 2 || phase > kPhaseMax) {
        net::Log("bossphase force: phase must be 2 or 3 (got '%s')", a.c_str());
        return;
    }
    Scan();
    BossState& B = BS();
    if (B.bosses.empty()) {
        net::Log("bossphase force: no local boss to change (use `bossphase spawn` first)");
        return;
    }
    BossEntry& e = B.bosses.front();
    const double thr = (phase >= 3) ? e.t3 : e.t2;
    if (thr > 0.0 && e.hp > thr) {
        // The engine's own gate: put the copy under the threshold with the SAME
        // writer the health mirror uses (proven: it is what "nhp" applies), then let
        // the game's function do the transition.
        const double target = std::max(0.01, thr - 0.02);
        const bool hurt = combat::SetHealthNorm(e.actor, target);
        net::Log("bossphase force: lowered %s hp %.3f -> %.3f via SetHealthNorm (%s) to clear "
                 "the phase-%d gate %.3f", e.key.c_str(), e.hp, target, hurt ? "ok" : "failed",
                 phase, thr);
        e.hp = combat::HealthNorm(e.actor, nullptr);
    }
    const bool called = CallPhase(&e, phase);
    const int nowPhase = PhaseOf(e.comp);
    net::Log("bossphase force: %s -> %s (%s) called=%d phase now %d (flags second=%d third=%d) "
             "hookfire=%d", e.key.c_str(), PhaseFunction(phase), e.cls.c_str(), called ? 1 : 0,
             nowPhase, (nowPhase >= 2) ? 1 : 0, (nowPhase >= 3) ? 1 : 0, B.hookFires);
    e.phase = nowPhase;
    // The scan on the next publish tick turns this into an event; do it now too so
    // the verb's own log line is followed immediately by the publish line.
    DiffAndPublish();
}

// "sim <2|3> [tail]": publish the event WITHOUT touching the local game (the wire
// + apply half on its own).  Used when the local copy must not be disturbed.
void CmdSim(const std::string& arg) {
    const int phase = (int)::strtol(TrimArg(arg).c_str(), nullptr, 10);
    if (phase < 2 || phase > kPhaseMax) {
        net::Log("bossphase sim: phase must be 2 or 3");
        return;
    }
    Scan();
    BossState& B = BS();
    if (B.bosses.empty()) {
        net::Log("bossphase sim: no local boss to name");
        return;
    }
    const BossEntry& e = B.bosses.front();
    QueueEvent(e, phase);
    net::Log("bossphase sim: queued a synthetic phase-%d event for %s (local game NOT touched)",
             phase, e.key.c_str());
}

// "spawn [class]": the only proven way to make an actor is the console's `summon`
// (coop_puppet.cpp kSummonCmd).  A LIST is tried in order and the first one that
// produces a live BPC_BossAI_C wins -- the boss packages are not all streamed in
// every area, and a class whose package is not mounted cannot be summoned.
//
// Note what `summon` can and cannot do here: the puppet works because
// BP_PlayerCharacter is always loaded; a boss class has to be LOADED by the game
// first, and this build exposes no asset loader to the mod (net::Find's
// CONTRACT-REQUEST 4).  So the verb reports, per candidate, whether the class
// object exists in memory before and after the console call -- "class loaded but
// no actor" (a spawn failure) and "class not loaded" (a load failure) are two
// completely different findings and the report must not confuse them.
const char* const kBossSpawnList[] = {
    "/Game/Sparta/Characters/Bosses/Dwarf/BP_DwarfBoss.BP_DwarfBoss_C",
    "/Game/Sparta/Characters/Enemies/CannibalKnight/BP_Cannibal_Knight_MiniBoss.BP_Cannibal_Knight_MiniBoss_C",
    "/Game/Sparta/Characters/Enemies/Batushka/BP_Batushka_MiniBoss.BP_Batushka_MiniBoss_C",
};

double g_spawnScanAt = -1.0e9;   // the delayed re-scan after a summon
std::string g_spawnTried;
std::string g_spawnShortCls;
std::vector<ue::UObject*> g_rigSpawned;

std::string ShortClassFromPath(const std::string& path) {
    const size_t dot = path.rfind('.');
    return (dot != std::string::npos) ? path.substr(dot + 1) : path;
}

void RegisterRigAliases() {
    if (g_spawnShortCls.empty()) return;
    const std::string alias = "RIGBOSS_" + g_spawnShortCls;
    for (ue::UObject* a : ue::FindAllOf(g_spawnShortCls.c_str())) {
        if (!Valid(a)) continue;
        const std::string tail = net::Tail(a);
        if (tail.empty()) continue;
        state().combat.spawnAlias.Set(tail, alias);
        bool seen = false;
        for (ue::UObject* x : g_rigSpawned) if (x == a) { seen = true; break; }
        if (!seen) g_rigSpawned.push_back(a);
    }
}

void CmdDespawn() {
    int n = 0;
    Scan();
    for (const BossEntry& e : BS().bosses) {
        if (Valid(e.actor)) {
            bool seen = false;
            for (ue::UObject* x : g_rigSpawned) if (x == e.actor) { seen = true; break; }
            if (!seen) g_rigSpawned.push_back(e.actor);
        }
    }
    for (ue::UObject* a : g_rigSpawned) {
        if (!Valid(a)) continue;
        if (ue::UFunction* fn = ue::FindFunction(a, "K2_DestroyActor")) {
            ue::Params p(fn);
            ue::Call(a, fn, p.data());
            ++n;
        }
    }
    g_rigSpawned.clear();
    g_spawnShortCls.clear();
    BS().bosses.clear();
    BS().pending.clear();
    BS().retries.clear();
    net::Log("bossdespawn: destroyed %d rig-spawned boss(es), live bosses=%d", n, (int)BS().bosses.size());
}

void CmdSpawn(const std::string& arg) {
    std::string cls = TrimArg(arg);
    std::vector<std::string> tryList;
    if (!cls.empty()) {
        tryList.push_back(cls);
    } else {
        for (const char* c : kBossSpawnList) tryList.push_back(c);
    }

    Scan();
    const std::size_t before = BS().bosses.size();
    net::Log("bossphase spawn: bosses before %d, trying %d class(es)",
             (int)before, (int)tryList.size());

    for (const std::string& path : tryList) {
        ue::UObject* pre = ue::FindObject(path.c_str());
        if (!Valid(pre)) {
            pre = load::Asset(path);
        }
        const bool preOk = Valid(pre);
        net::Log("bossphase spawn: %s -- class loaded: %s", path.c_str(),
                 preOk ? "yes" : "no");
        net::Console(std::string("summon ") + path);
        if (preOk) {
            g_spawnTried = path;
            g_spawnShortCls = ShortClassFromPath(path);
            RegisterRigAliases();
            break;
        }
    }

    g_spawnScanAt = net::Now() + 1.5;
    Scan();
    net::Log("bossphase spawn: bosses after (same tick) %d%s", (int)BS().bosses.size(),
             BS().bosses.size() > before ? " -- APPEARED" : "");
}

// The delayed re-scan a `spawn` asked for (driven by the publish tick).
void TickSpawnScan() {
    if (g_spawnScanAt < 0.0) return;
    if (net::Now() < g_spawnScanAt) return;
    g_spawnScanAt = -1.0e9;
    RegisterRigAliases();
    Scan();
    BossState& B = BS();
    net::Log("bossphase spawn: after 1.5 s bosses=%d (ai characters=%d) tried=%s",
             (int)B.bosses.size(), (int)ue::FindAllOf("BP_AICharacter_C").size(),
             g_spawnTried.empty() ? "-" : g_spawnTried.c_str());
    for (const BossEntry& e : B.bosses) {
        const Vec3 p = net::Loc(e.actor);
        net::Log("bossphase spawn: live %s cls=%s phase=%d hp=%.2f t2=%.2f t3=%.2f pos=%.0f %.0f %.0f", e.key.c_str(),
                 e.cls.c_str(), e.phase, e.hp, e.t2, e.t3, p.X, p.Y, p.Z);
    }
    if (!g_spawnShortCls.empty() && B.bosses.empty()) {
        for (ue::UObject* a : ue::FindAllOf(g_spawnShortCls.c_str())) {
            if (!Valid(a)) continue;
            bool dead = false;
            const double hp = combat::HealthNorm(a, &dead);
            net::Log("bossphase spawn: live-nonboss %s cls=%s hp=%.2f dead=%d",
                     net::Tail(a).c_str(), g_spawnShortCls.c_str(), hp, dead ? 1 : 0);
        }
    }
}

// "mkcomp [self|enemy]": attach a real BPC_BossAI_C to a LIVE actor, through
// AActor::AddComponentByClass -- the call coop_appear.cpp already uses (and proved)
// to hang an equipment component on an actor.  This is the only way to get a boss
// AI component on a map that has no boss in it: the boss ACTOR classes are not
// loaded in the hub (measured: `class in memory after: no` even after `summon`),
// but the COMPONENT class is (the two phase hooks resolve), and this call needs
// nothing but a UClass.
//
// It is a lab tool, not a game feature: the component is a real one, so its own
// BeginPlay runs and `ShouldStartSecondPhase` is the real function -- which is
// exactly what makes the phase replay provable in a save with no boss.
ue::FTransform IdentityXform() {
    ue::FTransform xf;
    xf.Rotation.X = 0.0;
    xf.Rotation.Y = 0.0;
    xf.Rotation.Z = 0.0;
    xf.Rotation.W = 1.0;
    xf.Translation.X = 0.0;
    xf.Translation.Y = 0.0;
    xf.Translation.Z = 0.0;
    xf.Scale3D.X = 1.0;
    xf.Scale3D.Y = 1.0;
    xf.Scale3D.Z = 1.0;
    return xf;
}

ue::UObject* AddBossComponent(ue::UObject* actor) {
    if (!Valid(actor)) return nullptr;
    ue::UClass* cls = ue::FindClass(kBossCompClass);
    if (!cls) {
        net::Log("bossphase mkcomp: %s is not in the class registry", kBossCompClass);
        return nullptr;
    }
    ue::UFunction* f = ue::FindFunction(actor, "AddComponentByClass");
    if (!f) {
        net::Log("bossphase mkcomp: %s has no AddComponentByClass", ue::GetName(actor).c_str());
        return nullptr;
    }
    ue::Params p(f);
    // TSubclassOf param + the dumper's "Class_0" rename fallback, the size check
    // inside Params::Set being what keeps a wrong name harmless.
    p.Set<ue::UObject*>("Class", (ue::UObject*)cls);
    {
        const int32_t off = 0x00;
        if (off + (int32_t)sizeof(ue::UObject*) <= p.size()) {
            ue::UObject* c = (ue::UObject*)cls;
            ue::WriteMem(reinterpret_cast<uint8_t*>(p.data()) + off, &c, sizeof(c));
        }
    }
    p.Set<bool>("bManualAttachment", true);
    p.Set<ue::FTransform>("RelativeTransform", IdentityXform());
    p.Set<bool>("bDeferredFinish", false);
    if (!ue::Call(actor, f, p.data())) {
        net::Log("bossphase mkcomp: AddComponentByClass faulted on %s",
                 ue::GetName(actor).c_str());
        return nullptr;
    }
    ue::UObject* comp = p.Get<ue::UObject*>("ReturnValue");
    return Valid(comp) ? comp : nullptr;
}

// Best effort, one line of truth: does the component know its character/controller?
void CacheBossRefs(ue::UObject* comp) {
    if (!Valid(comp)) return;
    bool okChar = false;
    bool okCtrl = false;
    if (ue::UFunction* f = ue::FindFunction(comp, "CacheCharacter")) {
        ue::Params p(f);
        okChar = ue::Call(comp, f, p.data()) && p.Get<bool>("Success");
    }
    if (ue::UFunction* f = ue::FindFunction(comp, "CacheController")) {
        ue::Params p(f);
        okCtrl = ue::Call(comp, f, p.data()) && p.Get<bool>("Success");
    }
    ue::UObject* ch = nullptr;
    ue::Get<ue::UObject*>(comp, "BossAICharacter", ch);
    net::Log("bossphase mkcomp: CacheCharacter=%d CacheController=%d BossAICharacter=%s",
             okChar ? 1 : 0, okCtrl ? 1 : 0,
             Valid(ch) ? net::FullName(ch).c_str() : "(none)");
}

// The nearest live AI character (an enemy), for `mkcomp enemy`.
ue::UObject* NearestEnemy() {
    ue::UObject* pawn = net::Pawn();
    if (!Valid(pawn)) return nullptr;
    const Vec3 me = net::Loc(pawn);
    ue::UObject* best = nullptr;
    double bestD = 1.0e30;
    for (ue::UObject* a : ue::FindAllOf("BP_AICharacter_C")) {
        if (!Valid(a) || a == pawn) continue;
        const Vec3 p = net::Loc(a);
        const double dx = p.X - me.X;
        const double dy = p.Y - me.Y;
        const double dz = p.Z - me.Z;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < bestD) {
            bestD = d;
            best = a;
        }
    }
    return best;
}

void CmdMkComp(const std::string& arg) {
    const std::string a = TrimArg(arg);
    Scan();
    const std::size_t before = BS().bosses.size();
    ue::UObject* actor = nullptr;
    if (a == "enemy") {
        actor = NearestEnemy();
    } else {
        actor = net::Pawn();
    }
    if (!Valid(actor)) {
        net::Log("bossphase mkcomp: no local actor to attach to (arg='%s')", a.c_str());
        return;
    }
    net::Log("bossphase mkcomp: attaching %s to %s (tail %s)", kBossCompClass,
             net::FullName(actor).c_str(), net::Tail(actor).c_str());
    ue::UObject* comp = AddBossComponent(actor);
    net::Log("bossphase mkcomp: AddComponentByClass -> %s",
             Valid(comp) ? net::FullName(comp).c_str() : "(null)");
    if (Valid(comp)) CacheBossRefs(comp);
    Scan();
    net::Log("bossphase mkcomp: bosses %d -> %d", (int)before, (int)BS().bosses.size());
    for (const BossEntry& e : BS().bosses) {
        net::Log("bossphase mkcomp: live %s cls=%s phase=%d hp=%.2f", e.key.c_str(),
                 e.cls.c_str(), e.phase, e.hp);
    }
}

}  // namespace

// ===========================================================================
// public API
// ===========================================================================

namespace boss {

void Serialize(std::string* out) {
    if (out == nullptr) return;
    if (!TG().bossSync) return;

    EnsureHooks();

    BossState& B = BS();
    const double now = net::Now();
    // The hook asked for an immediate pass; the 1 Hz poll is the safety net.
    if (B.flipSeen || now - B.lastScanAt >= 1.0) {
        B.flipSeen = false;
        B.lastScanAt = now;
        Scan();
        DiffAndPublish();
    }
    TickRetries();
    TickSpawnScan();
    ExpireEvents();

    if (B.pending.empty()) return;

    std::string parts;
    int n = 0;
    for (const BossEvent& ev : B.pending) {
        // One JSON string per text field; the tails/class names carry no quotes by
        // construction, but never trust that.
        std::string key = ev.key;
        std::string cls = ev.cls;
        for (char& c : key) {
            if (c == '"' || c == '\\') c = '_';
        }
        for (char& c : cls) {
            if (c == '"' || c == '\\') c = '_';
        }
        if (n++) parts.push_back(',');
        net::AppendFmt(&parts, "[\"%s\",\"%s\",%d,%d]", key.c_str(), cls.c_str(), ev.phase, ev.seq);
    }
    *out += ",\"bsp\":[";
    *out += parts;
    *out += ']';
}

void OnPeerLine(const std::string& line) {
    if (!TG().bossSync) return;
    std::string sec;
    if (!net::Section(line, "bsp", &sec)) return;

    BossState& B = BS();
    // sec is "[[...],[...]]": walk it entry by entry.  net::Section guarantees the
    // brackets balance, so a simple depth walk is enough (and it cannot be fooled
    // by a tail: none contains '[' or ']').
    std::size_t i = 1;                        // past the opening '['
    while (i < sec.size()) {
        while (i < sec.size() && (sec[i] == ',' || (unsigned char)sec[i] <= ' ')) ++i;
        if (i >= sec.size() || sec[i] == ']') break;
        if (sec[i] != '[') {
            ++i;
            continue;
        }
        const std::size_t start = i;
        int depth = 0;
        std::size_t end = std::string::npos;
        for (std::size_t j = start; j < sec.size(); ++j) {
            if (sec[j] == '[') {
                ++depth;
            } else if (sec[j] == ']') {
                --depth;
                if (depth == 0) {
                    end = j;
                    break;
                }
            }
        }
        if (end == std::string::npos) break;
        const std::string entry = sec.substr(start, end - start + 1);

        // fields: "key","class",phase,seq
        std::string key;
        std::string cls;
        int phase = 0;
        int seq = 0;
        {
            // the two quoted strings, in order
            std::size_t q1 = entry.find('"');
            std::size_t q2 = (q1 == std::string::npos) ? q1 : entry.find('"', q1 + 1);
            std::size_t q3 = (q2 == std::string::npos) ? q2 : entry.find('"', q2 + 1);
            std::size_t q4 = (q3 == std::string::npos) ? q3 : entry.find('"', q3 + 1);
            if (q1 == std::string::npos || q4 == std::string::npos) {
                i = end + 1;
                continue;
            }
            key = entry.substr(q1 + 1, q2 - q1 - 1);
            cls = entry.substr(q3 + 1, q4 - q3 - 1);
            const std::size_t comma = entry.find(',', q4);
            if (comma != std::string::npos) {
                phase = (int)::strtol(entry.c_str() + comma + 1, nullptr, 10);
                const std::size_t comma2 = entry.find(',', comma + 1);
                if (comma2 != std::string::npos) seq = (int)::strtol(entry.c_str() + comma2 + 1, nullptr, 10);
            }
        }
        i = end + 1;

        if (key.empty() || phase < 2) continue;
        // The seq makes a repeated packet harmless: anything not newer is dropped.
        if (seq > 0 && seq <= B.seenSeq.Get(key, 0)) continue;
        if (seq > 0) B.seenSeq.Set(key, seq);
        B.peerEvents += 1;
        ApplyPhase(key, cls, phase, seq);
    }
}

void Reset() {
    BossState& B = BS();
    B.bosses.clear();
    B.pending.clear();
    B.retries.clear();
    B.appliedPhase.Clear();
    B.seenSeq.Clear();
    B.localSeq.Clear();
    B.lastScanAt = -1.0e9;
    B.flipSeen = false;
    g_spawnScanAt = -1.0e9;
}

std::string Status() {
    BossState& B = BS();
    std::string who = "-";
    if (!B.bosses.empty()) {
        const BossEntry& e = B.bosses.front();
        who = net::Fmt("%s:%d", e.key.c_str(), e.phase);
    }
    return net::Fmt("boss=n:%d phase:%s scanned:%d hook:%d pub:%d applied:%d seen:%d fail:%d "
                    "missed:%d echoes:%d peerkeys:%d off:%d\n",
                    (int)B.bosses.size(), who.c_str(), B.scanned, B.hookFires, B.published,
                    B.applied, B.peerEvents, B.refused, B.missed, B.echoes, B.appliedPhase.Size(),
                    TG().bossSync ? 0 : 1);
}

// [R10-BOSSFIGHT]
static bool BossFightOff() {
    static int v = -1;
    if (v < 0) {
        v = net::FileExists("BOSSFIGHT_OFF.txt") ? 1 : 0;
        if (v) net::Log("BOSSFIGHT_OFF.txt present -- the R10 boss-fight fixes are off");
    }
    return v != 0;
}
void CheckPhaseAfterDamage(ue::UObject* actor) {
    if (!Valid(actor) || BossFightOff()) return;
    BossState& B = BS();
    BossEntry* e = nullptr;
    for (int pass = 0; pass < 2 && !e; ++pass) {
        if (pass == 1) Scan();
        for (BossEntry& x : B.bosses)
            if (x.actor == actor) { e = &x; break; }
    }
    if (!e || !Valid(e->comp)) return;
    const double hp = combat::HealthNorm(actor, nullptr);
    if (hp < 0.0) return;
    e->hp = hp;
    int phase = PhaseOf(e->comp);
    for (int want = 2; want <= kPhaseMax; ++want) {
        const double thr = (want >= 3) ? e->t3 : e->t2;
        if (!(thr > 0.0) || hp > thr || phase >= want) continue;
        const bool called = CallPhase(e, want);
        phase = PhaseOf(e->comp);
        net::Log("bossphase: [R10-BOSSFIGHT] %s hp %.4f <= %.3f after a peer hit -> %s called=%d phase now %d",
                 e->key.c_str(), hp, thr, PhaseFunction(want), called ? 1 : 0, phase);
    }
}

// [R10-BOSSFIGHT] "bosshp <norm> [tail]" -- LAB ONLY: put the local boss at a health so a
// test can reach the phase gate / the kill with a few real hits.
void CmdHp(const std::string& arg) {
    const double v = std::atof(TrimArg(arg).c_str());
    Scan();
    BossState& B = BS();
    for (BossEntry& e : B.bosses) {
        if (!Valid(e.actor)) continue;
        const bool ok = combat::SetHealthNorm(e.actor, v);
        net::Log("bosshp: %s hp %.4f -> %.4f ok=%d", e.key.c_str(), combat::HealthNorm(e.actor, nullptr) , v, ok ? 1 : 0);
        return;
    }
    net::Log("bosshp: no local boss");
}

bool OnCommand(const std::string& cmd, const std::string& rest) {
    if (cmd == "bosshp") { CmdHp(rest); return true; }
    if (cmd == "bossdespawn") { CmdDespawn(); return true; }
    if (cmd != "bossphase" && cmd != "bossdump" && cmd != "bossspawn") return false;

    if (cmd == "bossdump") {
        CmdDump(rest);
        return true;
    }

    std::string sub = rest;
    std::string arg;
    const std::size_t sp = sub.find_first_of(" \t");
    if (sp != std::string::npos) {
        arg = sub.substr(sp + 1);
        sub = sub.substr(0, sp);
    }
    if (cmd == "bossspawn") {
        CmdSpawn(rest);
        return true;
    }

    if (sub.empty() || sub == "dump") {
        CmdDump(arg);
    } else if (sub == "force") {
        CmdForce(arg);
    } else if (sub == "sim") {
        CmdSim(arg);
    } else if (sub == "spawn") {
        CmdSpawn(arg);
    } else if (sub == "mkcomp") {
        CmdMkComp(arg);
    } else {
        net::Log("bossphase: unknown subcommand '%s' (dump|force <2|3>|sim <2|3>|spawn [class]|"
                 "mkcomp [self|enemy])", sub.c_str());
    }
    return true;
}

}  // namespace boss

}  // namespace coop
