// coop_death.cpp -- co-op player death for Mortal Shell II (R5-DEATH).
//
// This is the engine half of the owner's three-case rule; the DECISIONS are in
// coop_policy.h (pure, unit-tested, a port of DollsMP's coop_policy.h +
// dmp_rules.h) and are never re-implemented here.  Read the banner of
// coop_policy.h first: it explains why the case must be computed over the PARTY
// and why a blanket suppression is the failure mode that hides.
//
// WHAT THIS FILE DOES
//   1. DETECTION     -- the local player's ESpartaDeathState, read off the pawn's
//                       USpartaHealthComponent (a reflection offset, one byte, no
//                       engine call on the hot path).
//   2. THE CASE      -- CoopPartyCase(selfAlive, peerAlive, bossLive) over the
//                       party, re-decided 4 Hz while a death is pending.
//   3. SUPPRESSION   -- the game's death reset/reload, vetoed at the ProcessEvent
//                       dispatch when the case says the live world must be kept.
//   4. RESPAWN       -- the game's own revive, then its restart, then a teleport to
//                       the nearest respawn point, all inside the LIVE world.
//   5. HOLD          -- a death inside a live boss fight: no respawn, the camera
//                       watches the partner's body (our local puppet of them).
//   6. WIPE          -- the full reset is the host's; a guest blocks its own and
//                       comes back once the host is up.
//   7. STACKS        -- max health and max SHELL health x (1 - stacks*pct), capped,
//                       re-based when the game changes the value itself.
//
// Every engine call that could be the first of its kind is logged BEFORE it is
// made ("RESPAWN step %d ..."), because calling an unknown blueprint function is
// the one thing that can take this game down without a trace.

#include "coop_state.h"
#include "coop_policy.h"

#include "ms2_log.h"
#include "ms2_ue.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// winuser.h turns GetClassName into GetClassNameA, which would rename ue::'s
// function at the point of use (the same #undef every other module has).
#undef GetClassName

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace coop {
namespace death {

// the ticks below this file's helpers: the body repair re-runs the debuff one when the
// respawned pawn finally has the attributes the percentage is measured on
void Tick2();

namespace {

using ue::UObject;
using ue::UFunction;

// ---------------------------------------------------------------------------
// guarded reflection helpers (this module's own copies: coop_net's `uecall` is
// private to its translation unit, and coop_combat's are private to its)
// ---------------------------------------------------------------------------

struct FuncSlot {
    std::string key;
    UFunction* fn = nullptr;
};

std::vector<FuncSlot> g_fnCache;

UFunction* Func(UObject* o, const char* name) {
    if (!o || !name) return nullptr;
    const std::string key = ue::GetClassName(o) + "|" + name;
    for (const FuncSlot& s : g_fnCache) {
        if (s.key == key) return s.fn;
    }
    UFunction* fn = ue::FindFunction(o, name);
    FuncSlot slot;
    slot.key = key;
    slot.fn = fn;   // a miss is cached too: no MISS storms
    g_fnCache.push_back(slot);
    return fn;
}

bool CallVoid(UObject* o, const char* fnName) {
    if (!ue::IsValid(o)) return false;
    UFunction* fn = Func(o, fnName);
    if (!fn) return false;
    ue::Params p(fn);
    return ue::Call(o, fn, p.data());
}

bool CallFloat(UObject* o, const char* fnName, float* out) {
    if (out) *out = 0.0f;
    if (!ue::IsValid(o)) return false;
    UFunction* fn = Func(o, fnName);
    if (!fn) return false;
    ue::Params p(fn);
    if (!ue::Call(o, fn, p.data())) return false;
    if (out) *out = p.Get<float>("ReturnValue");
    return true;
}

bool CallBool(UObject* o, const char* fnName, bool* out) {
    if (out) *out = false;
    if (!ue::IsValid(o)) return false;
    UFunction* fn = Func(o, fnName);
    if (!fn) return false;
    ue::Params p(fn);
    if (!ue::Call(o, fn, p.data())) return false;
    if (out) *out = p.Get<bool>("ReturnValue");
    return true;
}

UObject* CallObject(UObject* o, const char* fnName) {
    if (!ue::IsValid(o)) return nullptr;
    UFunction* fn = Func(o, fnName);
    if (!fn) return nullptr;
    ue::Params p(fn);
    if (!ue::Call(o, fn, p.data())) return nullptr;
    UObject* r = p.Get<UObject*>("ReturnValue");
    return ue::IsValid(r) ? r : nullptr;
}

bool PropObj(UObject* o, const char* prop, UObject** out) {
    if (out) *out = nullptr;
    if (!ue::IsValid(o)) return false;
    UObject* p = nullptr;
    if (!ue::Get<UObject*>(o, prop, p)) return false;
    if (!ue::IsValid(p)) return false;
    if (out) *out = p;
    return true;
}

// An integer property whose width the reflection knows (enums are 1 byte).
bool PropInt(UObject* o, const char* prop, int* out) {
    if (!out) return false;
    *out = 0;
    if (!ue::IsValid(o)) return false;
    const std::int32_t sz = ue::PropSize(o, prop);
    if (sz == 1) {
        std::uint8_t v = 0;
        if (!ue::Get<std::uint8_t>(o, prop, v)) return false;
        *out = (int)v;
        return true;
    }
    if (sz == 2) {
        std::int16_t v = 0;
        if (!ue::Get<std::int16_t>(o, prop, v)) return false;
        *out = (int)v;
        return true;
    }
    if (sz == 4) {
        std::int32_t v = 0;
        if (!ue::Get<std::int32_t>(o, prop, v)) return false;
        *out = (int)v;
        return true;
    }
    if (sz == 8) {
        std::int64_t v = 0;
        if (!ue::Get<std::int64_t>(o, prop, v)) return false;
        *out = (int)v;
        return true;
    }
    return false;
}

// FGameplayAttributeData: BaseValue at +0x08, CurrentValue at +0x0C of the member
// offset (measured; see coop_combat.cpp's banner and SYNC-DATA-MINE 7.1).
constexpr std::int32_t kAttrBase = 0x08;
constexpr std::int32_t kAttrCur  = 0x0C;

bool WriteAttr(UObject* set, const char* prop, float v) {
    if (!ue::IsValid(set)) return false;
    const std::int32_t off = ue::PropOffset(set, prop);
    if (off < 0) return false;
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(set) + off;
    const bool a = ue::WriteMem(base + kAttrBase, &v, sizeof(v));
    const bool b = ue::WriteMem(base + kAttrCur, &v, sizeof(v));
    return a && b;
}

bool ReadAttr(UObject* set, const char* prop, float* out) {
    if (!out) return false;
    *out = 0.0f;
    if (!ue::IsValid(set)) return false;
    const std::int32_t off = ue::PropOffset(set, prop);
    if (off < 0) return false;
    float v = 0.0f;
    if (!ue::ReadMem(reinterpret_cast<const std::uint8_t*>(set) + off + kAttrCur, &v, sizeof(v)))
        return false;
    *out = v;
    return true;
}

// IS THIS PLAYER DEAD RIGHT NOW?
//
// Not from ESpartaDeathState: run 4 measured it reading 0 through the whole death AND
// the reset, on the player's health component.  The signal the game itself keeps is
// BP_PlayerController_C.bDeathMixActive (bool, offset 0x9A0 in
// research\mine\topic-18-player-controller.props.tsv): ApplyDeathMix sets it at the
// death and ClearDeathMix clears it when the player is back.  Both are real
// UFunctions on the same object, and this module hooks them.
//
// A build without the flag falls back to the health component's DeathState, which
// costs a property read and works for enemies (it is simply not what the PLAYER uses).
bool ReadDeathMix(UObject* pc, bool* ok) {
    if (ok) *ok = false;
    if (!ue::IsValid(pc)) return false;
    const std::int32_t off = ue::PropOffset(pc, "bDeathMixActive");
    if (off < 0) return false;
    std::uint8_t v = 0;
    if (!ue::ReadMem(reinterpret_cast<const std::uint8_t*>(pc) + off, &v, sizeof(v))) return false;
    if (ok) *ok = true;
    return (v & 1) != 0;
}

// The health component of a pawn: the reflection property, not FindAllOf.
UObject* HealthCompOf(UObject* pawn) {
    UObject* hc = nullptr;
    if (!PropObj(pawn, "HealthComponent", &hc)) return nullptr;
    return hc;
}

// ESpartaDeathState: NotDead 0, DeathStarted 1, DeathFinished 2.
int ReadDeathState(UObject* hc, bool* readable) {
    if (readable) *readable = false;
    if (!hc) return 0;
    int v = 0;
    if (PropInt(hc, "DeathState", &v)) {
        if (readable) *readable = true;
        return v;
    }
    // A build without the property falls back to the game's own predicate, which
    // is a call -- still only once per tick.
    bool dead = false;
    if (CallBool(hc, "IsDeadOrDying", &dead)) {
        if (readable) *readable = true;
        return dead ? 1 : 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// utilities
// ---------------------------------------------------------------------------

bool Contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

std::string Lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return s;
}

double Dist3(const Vec3& a, const Vec3& b) {
    const double dx = a.X - b.X;
    const double dy = a.Y - b.Y;
    const double dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool IsHost() { return state().local.role != "guest"; }

// THE MANUAL OVERRIDE (coop_policy.h's `gateArmed`).  With no partner this session
// the mod leaves the game's death handling alone -- MS2's solo rule.  This file is
// the one way to force the single-machine arm, and it exists so the respawn
// machinery can be exercised with ONE client in a lab run:
//
//     coop\SOLO_DEATH.txt  ->  treat a solo death as case 2 (suppress + respawn)
//
// It is read every tick (a file test, ~2 us) so a run can drop it in mid-session,
// and it is logged when it changes state.
// A REST CLEARS THE STACKS (the owner's rule: "cleared on a wipe or when the player
// rests").  This game has no single "rest" UFunction worth hooking, but it does have
// BPFL_Player_C.RestorePlayerHealth / RestoreResolve / RestoreHealingCharges -- the
// restore trio a checkpoint/pillar runs -- so "the game restored the player" is the
// rest signal.  It is APPROXIMATE, so it is narrowed twice: only when no death is
// pending, and only within 600 uu of a respawn point (the act of resting happens at
// one).  A false positive costs the player an early reprieve; a false negative keeps
// a debuff one rest longer.  Neither can corrupt a save.
int g_restSeen = 0;

void RestHook(UObject* /*self*/, UFunction* fn, void* /*parms*/) {
    if (state().death.off) return;
    g_restSeen = 1;
    (void)fn;
}

int GateArmed() {
    static int last = -1;
    static double checkedAt = -1.0e9;
    // Cached for 2 s: this is a FILE OPEN, and the caller is a 15 Hz game-thread tick.
    if (last < 0 || net::Now() - checkedAt > 2.0) {
        checkedAt = net::Now();
        const int v = net::FileExists("SOLO_DEATH.txt") ? 1 : 0;
        if (v != last) {
            last = v;
            if (v) net::Log("death: SOLO_DEATH.txt present -- a solo death is treated as case 2 "
                            "(the harness override; a two-client session ignores it)");
        }
    }
    return last < 0 ? 0 : last;
}

constexpr double kPeerStaleS = 6.0;   // no liveness word for this long = down

// The liveness word this machine publishes: ALIVE, or the pending death's case.
int MyLiveness() {
    const DeathState& D = state().death;
    if (D.selfAlive) return COOP_CL_ALIVE;
    switch (D.partyCase) {
        case COOP_CASE_WIPE:    return COOP_CL_WIPED;
        case COOP_CASE_RESPAWN: return COOP_CL_RESPAWNING;
        case COOP_CASE_HOLD:    return COOP_CL_HOLD;
        default:                return COOP_CL_WIPED;
    }
}

// The partner's liveness as of NOW.  A stale word is UNKNOWN, and CoopPeerAlive
// says UNKNOWN is not alive -- the deliberate asymmetry from coop_policy.h.
int PeerLivenessNow() {
    const DeathState& D = state().death;
    if (!D.peerSeen) return COOP_CL_UNKNOWN;
    if (net::Now() - D.peerAt > kPeerStaleS) return COOP_CL_UNKNOWN;
    return D.peerLiveness;
}

int PeerAliveNow() { return CoopPeerAlive(PeerLivenessNow()); }

// The host's boss flag, from the wire (the dying player's own game may end its
// boss mode on its own death, so this machine alone is not enough to keep a hold).
int PeerBossNow() {
    const DeathState& D = state().death;
    if (!D.peerSeen) return 0;
    if (net::Now() - D.peerAt > kPeerStaleS) return 0;
    return D.peerBoss;
}

// ---------------------------------------------------------------------------
// boss fight detection (case 3)
// ---------------------------------------------------------------------------

int ScanBoss() {
    DeathState& D = state().death;
    const double now = net::Now();
    if (now - D.lastBossAt < 1.0) return D.bossFound;
    D.lastBossAt = now;

    for (UObject* c : ue::FindAllOf("BPC_BossAI_C")) {
        if (!ue::IsValid(c)) continue;
        bool aggro = false;
        if (!CallBool(c, "IsAlreadyAggroed", &aggro)) {
            if (!CallBool(c, "IsAggroed", &aggro)) {
                int v = 0;
                if (PropInt(c, "bIsAggroed", &v)) aggro = v != 0;
            }
        }
        if (!aggro) continue;
        UObject* owner = CallObject(c, "GetOwner");
        if (owner) {
            UObject* hc = HealthCompOf(owner);
            if (hc) {
                bool readable = false;
                if (ReadDeathState(hc, &readable) && readable) continue;   // a dead boss is no fight
            }
        }
        D.bossFound = 1;
        return 1;
    }
    D.bossFound = 0;
    return 0;
}

int BossLive() { return (state().death.bossFake || ScanBoss()) ? 1 : 0; }

// ---------------------------------------------------------------------------
// the death-screen widget.  It is the reset's front-end (CreateLoadingScreen ->
// ResetWorld -> BeginWorldReset), so when a veto is in force the widget is the
// visible half of what we are blocking.  Found by class, never by a cached
// pointer (a widget is rebuilt on every world change).
// ---------------------------------------------------------------------------

double g_screenScanAt = -1.0e9;
bool   g_screenLogged = false;

void DismissDeathScreen(const char* why) {
    DeathState& D = state().death;
    const double now = net::Now();
    if (now - g_screenScanAt < 1.0) return;
    g_screenScanAt = now;

    for (UObject* w : ue::FindAllOf("WBP_DeathScreen_C")) {
        if (!ue::IsValid(w)) continue;
        // UUserWidget::RemoveFromParent -- the widget leaves the viewport, which is
        // all "do not show a death screen" means.  It does not touch the actor.
        const bool ok = CallVoid(w, "RemoveFromParent");
        if (!g_screenLogged || !ok) {
            g_screenLogged = ok;
            net::Log("COOP DEATH death screen dismissed (%s) ok=%s", why, ok ? "true" : "false");
        }
    }
    // Measured (holdcam2): the dismissal can lose the race with CreateLoadingScreen
    // (~8 s after death); the loading screen then stays up for the whole hold because
    // the reset it fronts is vetoed.  Take any loading screen that is on screen down too.
    for (const char* cls : {"WBP_LoadingScreen_C", "WBP_LoadingScreenBase_C"}) {
        for (UObject* w : ue::FindAllOf(cls)) {
            if (!ue::IsValid(w)) continue;
            bool inVp = false;
            if (!CallBool(w, "IsInViewport", &inVp) || !inVp) continue;
            const bool ok = CallVoid(w, "RemoveFromParent");
            net::Log("COOP DEATH loading screen %s removed (%s) ok=%s", ue::GetName(w).c_str(), why,
                     ok ? "true" : "false");
        }
    }
    (void)D;
}

// ===========================================================================
// The reset veto.
//
// The game's death handling is authored in blueprint, so the one lever that does
// not patch engine code is skipping the UFunction at the ProcessEvent dispatch
// (ue::HookPreSkip).  Which functions are on the list is MEASURED -- see
// native\briefs\R5-DEATH-FINDINGS.md, which records the lab run that read the
// sequence after `die`.
//
// A veto is only taken when the policy says so AT THE MOMENT OF THE CALL, so a
// hook can never block an ordinary travel or a load-in: there is no pending death
// then.  That is the property the whole design rests on.
// ===========================================================================

struct VetoTarget {
    const char* baseClass;   // a class FindAllOf CAN resolve (native base classes do)
    const char* fn;          // the UFunction on an instance of it
    bool        done;
};

// Run 2/4 measured the order (R5-DEATH-FINDINGS.md section 2): after `die` the game
// runs GA_Death_Player, then (about 7 s later) WBP_DeathScreen.CreateLoadingScreen ->
// OnLoadingScreenCompleted -> the world-gen subsystem's HandleResetReady ->
// SpartaDataLayerSubsystem.SessionReset -> a save/data-layer reload and ClientRestart
// on a NEW controller.  The level never travels: "the death reset" is a data-layer /
// save reload IN PLACE.
//
// Targets are anchored on OBJECTS found through their NATIVE base class, because a
// blueprint class name does not always resolve (run 4: only 1 of 4 name-based specs
// installed).  FindAllOf(SpartaWorldGenSubsystem) finds the BP subclass instance, and
// FindFunction on the instance finds HandleResetReady.
VetoTarget g_vetoTargets[] = {
    {"SpartaDataLayerSubsystem", "SessionReset", false},      // the reload itself
    {"SpartaWorldGenSubsystem", "HandleResetReady", false},   // the last BP step before it
    {"SpartaWorldGenSubsystem", "BeginWorldReset", false},    // the documented entry point
    {"WBP_DeathScreen_C", "ResetWorld", false},               // the death screen's own
    {"WBP_DeathScreen_C", "CreateLoadingScreen", false},      // the screen in front of it
};
constexpr int kVetoCount = (int)(sizeof(g_vetoTargets) / sizeof(g_vetoTargets[0]));

bool VetoHook(UObject* /*self*/, UFunction* fn, void* /*parms*/) {
    DeathState& D = state().death;
    if (D.off || !D.pending || D.selfAlive) return false;
    const int isHost = IsHost() ? 1 : 0;
    if (!CoopShouldSuppressRole(D.peerSeen, GateArmed(), 1, D.partyCase, isHost)) return false;
    ++D.suppressed;
    net::Log("COOP DEATH veto: %s BLOCKED (case %d host=%d peer=%d) -- the live world is kept",
             ue::GetFullName((UObject*)fn).c_str(), D.partyCase, isHost, (int)D.peerSeen);
    return true;
}

// THE DEATH EVENT.  Run 2 established that ESpartaDeathState on the player's
// health component does NOT move when the player dies (it read 0 through the whole
// death and the reset), so the death is detected from the game's own event instead:
// BP_PlayerController_C.ApplyDeathMix (fired 0.0 s after `die`) and
// GA_Death_Player_C.HandlePlayerDeath (fired 0.13 s after).  Both are blueprint
// functions, so both are real ProcessEvent calls.
//
// The hook does NOTHING but raise a flag and log: everything expensive (the party
// case, the boss scan, the engine reads) happens on the next 15 Hz tick, never
// inside a ProcessEvent detour.
void OnDeathDetected(UObject* pawn);   // defined below

void DeathEventHook(UObject* self, UFunction* fn, void* /*parms*/) {
    DeathState& D = state().death;
    if (D.off) return;
    if (D.pending || D.pendingSignal) return;
    D.pendingSignal = 1;
    net::Log("death: the game's death event fired: %s on %s",
             ue::GetName((UObject*)fn).c_str(),
             ue::IsValid(self) ? ue::GetName(self).c_str() : "(nil)");
}

bool g_hooksInstalled = false;

// How many of each group resolved.  A group counts as done only when all of its
// targets resolve, which for blueprint classes means "after the level that loads
// them": at coop::Start() the game is in the MENU and almost nothing exists (run 4
// measured 6742 objects and 5 indexed classes; every spec failed).
int g_restHooks = 0;
int g_deathHooks = 0;
constexpr int kRestHookCount = 2;
constexpr int kDeathHookCount = 2;
int g_vetoCount = -1;   // last logged value, so the retry does not spam the log

void InstallVetoHooks() {
    g_hooksInstalled = true;
    int ok = 0;
    for (VetoTarget& t : g_vetoTargets) {
        if (t.done) {
            ++ok;
            continue;
        }
        for (UObject* o : ue::FindAllOf(t.baseClass)) {
            if (!ue::IsValid(o)) continue;
            UFunction* fn = Func(o, t.fn);
            if (!fn) continue;
            if (ue::HookPreSkipFn(fn, &VetoHook)) {
                t.done = true;
                ++ok;
                net::Log("death: veto hook on %s.%s (%s)", t.baseClass, t.fn,
                         ue::GetName(o).c_str());
                break;
            }
        }
    }
    state().death.hookResolved = ok;
    if (ok != g_vetoCount) {
        g_vetoCount = ok;
        net::Log("death: %d of %d reset-veto hook(s) installed", ok, kVetoCount);
    }

    // The rest signal (see RestHook) -- resolved by name, which works for a
    // BlueprintFunctionLibrary because its CDO is loaded with the module.
    if (g_restHooks < kRestHookCount) {
        int r = 0;
        if (ue::HookPre("BPFL_Player_C.RestorePlayerHealth", &RestHook)) ++r;
        if (ue::HookPre("BPFL_Player_C.RestoreResolve", &RestHook)) ++r;
        if (r != g_restHooks) {
            g_restHooks = r;
            net::Log("death: rest hooks %d of %d", r, kRestHookCount);
        }
    }
    // The death signal itself (see DeathEventHook).
    if (g_deathHooks < kDeathHookCount) {
        int d = 0;
        if (ue::HookPre("BP_PlayerController_C.ApplyDeathMix", &DeathEventHook)) ++d;
        if (ue::HookPre("GA_Death_Player_C.HandlePlayerDeath", &DeathEventHook)) ++d;
        if (d != g_deathHooks) {
            g_deathHooks = d;
            net::Log("death: death-event hooks %d of %d", d, kDeathHookCount);
        }
    }
}

// ---- spectate camera -------------------------------------------------------
// The engine asks APlayerCameraManager::BlueprintUpdateCamera(CameraTarget, out
// NewCameraLocation, out NewCameraRotation, out NewCameraFOV) -> bool through
// ProcessEvent EVERY frame (a BlueprintImplementableEvent's stub always calls it).
// While the player is held, a post-hook answers it: camera behind and above the
// partner's puppet, looking where the puppet looks, ReturnValue = true so the
// manager uses it.  Measured: SetViewTargetWithBlend runs but this game's manager
// keeps the dead player's fixed death camera; writing ViewTarget.Target changed
// what GetViewTarget answers but not what was RENDERED (round 5 screenshots).
bool g_wantSpectate = false;
double g_camAppliedAt = -1.0e9;
int g_camHooked = 0;

void CamHook(UObject* self, UFunction* fn, void* params) {
    static int seen = 0;
    if (seen++ == 0)
        net::Log("death: BlueprintUpdateCamera is called (first on %s)", ue::GetClassName(self).c_str());
    if (!g_wantSpectate || !params) return;
    DeathState& D = state().death;
    UObject* cam = nullptr;
    if (!ue::IsValid(D.pc) || !ue::Get(D.pc, "PlayerCameraManager", cam) || cam != self) return;
    UObject* pup = puppet::Actor();
    if (!ue::IsValid(pup)) return;
    ue::FVector loc{};
    ue::FRotator rot{};
    {
        UFunction* f = Func(pup, "K2_GetActorLocation");
        if (!f) return;
        ue::Params p(f);
        if (!ue::Call(pup, f, p.data())) return;
        loc = p.Get<ue::FVector>("ReturnValue");
    }
    {
        UFunction* f = Func(pup, "K2_GetActorRotation");
        if (!f) return;
        ue::Params p(f);
        if (!ue::Call(pup, f, p.data())) return;
        rot = p.Get<ue::FRotator>("ReturnValue");
    }
    const double yaw = rot.Yaw * 3.14159265358979 / 180.0;
    ue::FVector camLoc{loc.X - std::cos(yaw) * 420.0, loc.Y - std::sin(yaw) * 420.0, loc.Z + 170.0};
    ue::FRotator camRot{-15.0, rot.Yaw, 0.0};
    const auto put = [&](const char* name, const void* v, size_t n) {
        const std::int32_t off = ue::PropOffsetStruct((ue::UStruct*)fn, name);
        if (off >= 0) ue::WriteMem((std::uint8_t*)params + off, v, n);
    };
    put("NewCameraLocation", &camLoc, sizeof(camLoc));
    put("NewCameraRotation", &camRot, sizeof(camRot));
    float fov = 0.0f;
    const std::int32_t fovOff = ue::PropOffsetStruct((ue::UStruct*)fn, "NewCameraFOV");
    if (fovOff >= 0) ue::ReadMem((std::uint8_t*)params + fovOff, &fov, sizeof(fov));
    if (!(fov > 10.0f && fov < 170.0f)) {
        fov = 90.0f;
        put("NewCameraFOV", &fov, sizeof(fov));
    }
    const bool yes = true;
    put("ReturnValue", &yes, sizeof(yes));
    static bool first = true;
    if (first) {
        first = false;
        net::Log("COOP DEATH spectate camera: first frame applied (behind the puppet)");
    }
    g_camAppliedAt = net::Now();
}

void EnsureCamHook() {
    if (g_camHooked) return;
    if (ue::HookPost("PlayerCameraManager.BlueprintUpdateCamera", &CamHook)) {
        g_camHooked = 1;
        net::Log("death: spectate camera hook on PlayerCameraManager.BlueprintUpdateCamera");
    }
}

// ---- spectate camera, the mechanism that RENDERS ------------------------------
// Measured (round 5): BlueprintUpdateCamera is never called, the manager snaps its
// view target back to our own (dead) pawn, and the player pawn has NO CameraComponent
// (pupdump self).  The camera is this game's CameraStateFramework: BP_PlayerCameraManager_C
// derives ACSCameraManager, whose CameraStack holds UCSCameraState objects; each state
// computes the view from its own target actor (UCSCameraState::SetCameraTargetActor /
// GetCameraTargetActor).  So: retarget every state on the stack to the partner's
// puppet while held, and back to our pawn on release.  Re-checked every tick (a state
// pushed later -- e.g. by the death sequence -- gets retargeted too).  Verified by the
// manager's last rendered POV (GetCameraLocation) being near the puppet.
bool g_camRetargeted = false;

// ---- the game's death camera is BORROWED, never spent ------------------------
// `RetargetCameraStack` takes CameraState_Death_C off the stack, because otherwise a
// respawned player keeps staring at the death spot (measured in R5/R6).  That state is
// the GAME's, and a LATER real death in the same session -- a party wipe, or a solo
// death this module does not suppress -- must still have its death camera and the
// death screen in front of it.  So the instance and its class are kept, and every
// death the module does NOT suppress gets the state checked for, and put back if the
// game did not push a new one of its own.  The `deathchain` run (round 8) is what
// reads the `camstack(...)` lines below.
UObject* g_deathCamState = nullptr;   // the last instance this module took off the stack
UObject* g_deathCamClass = nullptr;   // ... its class (UClass as an opaque UObject*)
int      g_deathCamRemoved = 0;       // how many times a death camera was taken off
int      g_deathCamRestored = 0;      // how many times one had to be put back
double   g_deathCamDueAt = 0.0;       // when to check an unsuppressed death's camera

std::vector<UObject*> CameraStack(UObject* pc, UObject** mgrOut) {
    std::vector<UObject*> out;
    UObject* mgr = nullptr;
    if (!ue::IsValid(pc) || !ue::Get(pc, "PlayerCameraManager", mgr) || !ue::IsValid(mgr)) return out;
    if (mgrOut) *mgrOut = mgr;
    ue::TArray<UObject*> arr{};
    if (!ue::Get(mgr, "CameraStack", arr) || arr.Num <= 0 || arr.Num > 64) return out;
    for (std::int32_t i = 0; i < arr.Num; ++i) {
        UObject* st = nullptr;
        if (ue::ReadMem(arr.Data + i, &st, sizeof(st)) && ue::IsValid(st)) out.push_back(st);
    }
    return out;
}

// Returns how many states now target `target`; logs each change once.
int RetargetCameraStack(UObject* pc, UObject* target) {
    if (!ue::IsValid(target)) return 0;
    UObject* mgr = nullptr;
    const std::vector<UObject*> stack = CameraStack(pc, &mgr);
    int on = 0;
    for (UObject* st : stack) {
        // CameraState_Death_C frames the death spot from a fixed place and ignores its
        // target (measured, holdcam2).  It is taken off the stack both while held and
        // after a respawn this module ran -- the game only pops it on its own reset path,
        // so without this a respawned player kept staring at the death marker
        // (respawnvis, owner report "no body, no UI").
        if (ue::GetClassName(st).find("Death") != std::string::npos) {
            UFunction* rf = Func(mgr, "RemoveCameraState");
            if (rf) {
                ue::Params rp(rf);
                rp.Set<UObject*>("CameraState", st);
                const bool ok = ue::Call(mgr, rf, rp.data());
                if (ok) {
                    // Keep it: the game's own later death may need it back (see the
                    // banner above g_deathCamState).
                    g_deathCamState = st;
                    g_deathCamClass = reinterpret_cast<UObject*>(ue::GetClass(st));
                    ++g_deathCamRemoved;
                }
                net::Log("COOP DEATH camera: removed %s from the camera stack ok=%d",
                         ue::GetClassName(st).c_str(), ok ? 1 : 0);
                continue;
            }
        }
        UObject* cur = CallObject(st, "GetCameraTargetActor");
        if (cur == target) { ++on; continue; }
        const std::string was = cur ? ue::GetName(cur) : std::string("-");
        UFunction* f = Func(st, "SetCameraTargetActor");
        if (!f) continue;
        ue::Params p(f);
        p.Set<UObject*>("Actor", target);
        const bool ok = ue::Call(st, f, p.data());
        cur = CallObject(st, "GetCameraTargetActor");
        if (cur == target) ++on;
        net::Log("COOP DEATH camera: state %s target %s -> %s (call=%d now=%s)", ue::GetClassName(st).c_str(),
                 was.c_str(), ue::GetName(target).c_str(), ok ? 1 : 0, cur ? ue::GetName(cur).c_str() : "-");
    }
    static int said = 0;
    if (stack.empty() && said++ < 3)
        net::Log("COOP DEATH camera: no CameraStack on %s", mgr ? ue::GetClassName(mgr).c_str() : "(no manager)");
    return on;
}

bool CamPov(UObject* pc, ue::FVector* loc) {
    UObject* mgr = nullptr;
    if (!ue::IsValid(pc) || !ue::Get(pc, "PlayerCameraManager", mgr) || !ue::IsValid(mgr)) return false;
    UFunction* f = Func(mgr, "GetCameraLocation");
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(mgr, f, p.data())) return false;
    *loc = p.Get<ue::FVector>("ReturnValue");
    return true;
}

// The picture really follows the partner: the manager's last POV is within 800 uu
// of the puppet (a third-person camera sits a few hundred uu away).
bool CamRenderedNearPuppet(UObject* pc, double* distOut) {
    UObject* pup = puppet::Actor();
    ue::FVector pov{};
    if (!ue::IsValid(pup) || !CamPov(pc, &pov)) return false;
    const auto dist = [&](const Vec3& a) {
        const double dx = pov.X - a.X, dy = pov.Y - a.Y, dz = pov.Z - a.Z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    const double d = dist(net::Loc(pup));
    if (distOut) *distOut = d;
    // ...and closer to the puppet than to our own body: in a test both stand together.
    UObject* self = net::Pawn();
    const double dSelf = ue::IsValid(self) ? dist(net::Loc(self)) : 1.0e12;
    return d < 800.0 && d < dSelf;
}

// ---- measuring the camera stack ---------------------------------------------
// One line with the whole stack: the manager's class, and per state its class, its
// Priority and the actor it frames.  "Is the death camera there?" and "did the game
// push a new one for this death?" are answered by this line and nothing else, so it
// is logged before and after every death, every removal and every restore.
bool IsDeathCamera(UObject* st) {
    return ue::IsValid(st) && ue::GetClassName(st).find("Death") != std::string::npos;
}

int CountDeathCameras(const std::vector<UObject*>& stack) {
    int n = 0;
    for (UObject* st : stack) {
        if (IsDeathCamera(st)) ++n;
    }
    return n;
}

std::string CameraStackLine(const char* why) {
    DeathState& D = state().death;
    UObject* pc = ue::IsValid(D.pc) ? D.pc : net::PC();
    UObject* mgr = nullptr;
    const std::vector<UObject*> stack = CameraStack(pc, &mgr);
    std::string line = net::Fmt("camstack(%s): mgr=%s n=%d death=%d [", why,
                                mgr ? ue::GetClassName(mgr).c_str() : "-", (int)stack.size(),
                                CountDeathCameras(stack));
    for (size_t i = 0; i < stack.size(); ++i) {
        UObject* st = stack[i];
        int prio = 0;
        PropInt(st, "Priority", &prio);
        UObject* tgt = CallObject(st, "GetCameraTargetActor");
        net::AppendFmt(&line, "%s%s(prio=%d,target=%s)", i ? " " : "", ue::GetClassName(st).c_str(),
                       prio, tgt ? ue::GetName(tgt).c_str() : "-");
    }
    net::AppendFmt(&line, "]");
    return line;
}

// Put the game's death camera back when a death this module does NOT suppress needs
// it and the stack has none.  Two candidates, in order: the game's own instance that
// this module took off (it is initialized -- PlayerController/CameraManager/
// CameraInstance are set, the game only popped it), then a fresh one of the same
// class.  Returns 1 when the stack has a death camera afterwards.
int EnsureDeathCamera(const char* why) {
    DeathState& D = state().death;
    UObject* pc = ue::IsValid(D.pc) ? D.pc : net::PC();
    UObject* mgr = nullptr;
    std::vector<UObject*> stack = CameraStack(pc, &mgr);
    if (!ue::IsValid(mgr)) {
        net::Log("COOP DEATH camera check (%s): %s -- no camera manager to check", why,
                 CameraStackLine(why).c_str());
        return 0;
    }
    if (CountDeathCameras(stack) > 0) {
        net::Log("COOP DEATH camera check (%s): the game's death camera is present (%s)", why,
                 CameraStackLine(why).c_str());
        return 1;
    }
    // The manager can only be told to add a state; say which object before calling.
    UFunction* add = Func(mgr, "AddCameraState");
    if (add && ue::IsValid(g_deathCamState)) {
        net::Log("COOP DEATH camera restore (%s): re-adding the game's own %s (removed %d time(s) "
                 "this session)", why, ue::GetClassName(g_deathCamState).c_str(), g_deathCamRemoved);
        ue::Params p(add);
        p.Set<UObject*>("CameraState", g_deathCamState);
        const bool ok = ue::Call(mgr, add, p.data());
        stack = CameraStack(pc, nullptr);
        const int now = CountDeathCameras(stack);
        if (now > 0) ++g_deathCamRestored;
        net::Log("COOP DEATH camera restore (%s): AddCameraState ok=%d -> death cameras on the "
                 "stack=%d (%s)", why, ok ? 1 : 0, now, CameraStackLine(why).c_str());
        if (now > 0) return 1;
    }
    UFunction* create = Func(mgr, "CreateCameraState");
    if (create && ue::IsValid(g_deathCamClass)) {
        net::Log("COOP DEATH camera restore (%s): creating a fresh %s (the old instance is gone)",
                 why, ue::GetName(g_deathCamClass).c_str());
        ue::Params p(create);
        p.Set<UObject*>("CameraState", g_deathCamClass);   // TSubclassOf = a UClass*
        const bool ok = ue::Call(mgr, create, p.data());
        stack = CameraStack(pc, nullptr);
        const int now = CountDeathCameras(stack);
        if (now > 0) ++g_deathCamRestored;
        net::Log("COOP DEATH camera restore (%s): CreateCameraState ok=%d -> death cameras on the "
                 "stack=%d (%s)", why, ok ? 1 : 0, now, CameraStackLine(why).c_str());
        if (now > 0) return 1;
    }
    net::Log("COOP DEATH WARN camera check (%s): NO death camera on the stack and it could not be "
             "put back; a real death in this session will have no death camera (%s)", why,
             CameraStackLine(why).c_str());
    return 0;
}

bool CamToPuppet(UObject* pc) {
    const int on = RetargetCameraStack(pc, puppet::Actor());
    if (on > 0) g_camRetargeted = true;
    return on > 0;
}

void CamRestore(UObject* pc) {
    if (!g_camRetargeted) return;
    UObject* self = net::Pawn();
    const int on = RetargetCameraStack(pc, self);
    net::Log("COOP DEATH camera: back on the player (%d state(s))", on);
    g_camRetargeted = false;
}

// Called from the 15 Hz tick, unconditionally and cheaply: while ANY spec is still
// unresolved, resolve again.  This is what makes the whole module work at all --
// the classes it hooks are loaded with the level, minutes after coop::Start().
double g_lastHookRetryAt = -1.0e9;

void MaintainHooks() {
    if (state().death.hookResolved >= kVetoCount && g_restHooks >= kRestHookCount &&
        g_deathHooks >= kDeathHookCount) {
        return;   // everything is in: nothing to do, not even a clock read
    }
    if (!g_hooksInstalled) return;   // not started yet
    // ONLY IN GAMEPLAY.  Resolving a class is a full object-array walk, and during a
    // load that array is being rebuilt -- a scan there is the one place this module
    // could hurt the engine for no benefit, because the player cannot die before
    // gameplay starts anyway.
    if (state().puppet.phase != Phase::Sync) return;
    if (!ue::IsValid(net::Pawn())) return;
    const double now = net::Now();
    if (now - g_lastHookRetryAt < 3.0) return;
    g_lastHookRetryAt = now;
    InstallVetoHooks();
}



// ---------------------------------------------------------------------------
// the respawn ladder
//
// ORDER MATTERS AND IT IS THE REFERENCE MOD'S ORDER: the game's own restart
// first, and a hand-made respawn only if it does nothing.  Every step is logged
// before it is called and verified by the NEXT tick's death-state read, so a step
// that silently does nothing is visible in the log rather than assumed to work.
// ---------------------------------------------------------------------------

void ClearDebuffState() {
    DeathState& D = state().death;
    CoopDebuffForget(&D.maxHealth);
    CoopDebuffForget(&D.maxShell);
}

void AddStack(const char* why) {
    DeathState& D = state().death;
    const int before = D.stacks;
    D.stacks = CoopStackAdd(D.stacks, D.rules.maxStacks);
    net::Log("COOP DEATH stack %d -> %d (%s; factor %.3f)", before, D.stacks, why,
             (double)CoopDebuffFactor(D.stacks, D.rules.maxStacks, D.rules.healthPct));
}

// The nearest respawn point on this level, measured from the frozen death
// position.  DollsMP's "nearest respawn point" falls out of the engine's own
// player-start list: every ASpartaPlayerStart is a respawn point, and the nearest
// one is the same choice the game makes on a restart.
UObject* NearestPlayerStart(const Vec3& from, double* distOut) {
    UObject* best = nullptr;
    double bestD = 0.0;
    for (UObject* a : ue::FindAllOf("SpartaPlayerStart")) {
        if (!ue::IsValid(a)) continue;
        const Vec3 loc = net::Loc(a);
        const double d = Dist3(loc, from);
        if (!best || d < bestD) {
            best = a;
            bestD = d;
        }
    }
    if (distOut) *distOut = best ? bestD : 0.0;
    return best;
}

bool TeleportPawnTo(UObject* pawn, const Vec3& to) {
    if (!ue::IsValid(pawn)) return false;
    UFunction* fn = Func(pawn, "K2_SetActorLocation");
    if (!fn) return false;
    ue::FVector v;
    v.X = to.X;
    v.Y = to.Y;
    v.Z = to.Z;
    ue::Params p(fn);
    p.Set<ue::FVector>("NewLocation", v);
    return ue::Call(pawn, fn, p.data());
}

// step 1: the game's own in-place revive (BP_PlayerController_C.S_ReviveShell).
bool StepRevive() {
    DeathState& D = state().death;
    UObject* pc = D.pc;
    if (!ue::IsValid(pc)) return false;
    const bool ok = CallVoid(pc, "S_ReviveShell");
    net::Log("RESPAWN step 1: %s.S_ReviveShell() ok=%s", ue::GetName(pc).c_str(),
             ok ? "true" : "false");
    return ok;
}

// ===========================================================================
// PUTTING THE BODY BACK ON THE FRESH PAWN
//
// MEASURED (probe_B.log of local\lab\20261025-152913-freshdump): the pawn the game
// mode spawns for us is a whole BP_PlayerCharacter_C -- every component is there and
// attached to the capsule -- but it renders nothing:
//
//   before the death  [1] CharacterMesh0 mode=0 ai=ABP_Player_C animClass=ABP_Player_C
//                     attached: light rig + WP_Kick_L/R + WP_Fist_L/R +
//                     WP_StoneSeal_C + WP_NailShotgun_C + WP_HadernsSword_C
//   after  the respawn [1] CharacterMesh0 mode=0 ai=-            animClass=ABP_Player_C
//                     attached: light rig + WP_Kick_L/R + WP_Fist_L/R
//
// So the body is attached and visible-flagged and its AnimClass is right; what the
// new pawn does not have is (a) an ANIM INSTANCE on CharacterMesh0 -- the AnimBP is
// never instantiated because the skeletal mesh asset is null when the component is
// initialised and swapping the asset later does not re-init the anim script -- and
// (b) the EQUIPPED WEAPON ACTORS, which the game spawns through
// UWeaponsComponent::SpawnAndEquipWeaponOfClass, not through the class defaults.
// The CONTROLLER's equipment state is intact (the HUD still draws the same four
// slot icons after the respawn), which is why nothing here re-publishes items: the
// mesh asset, the AnimBP and the weapon classes are copied from the pawn that just
// died, snapshotted before it is destroyed.
//
// Two providers, and each one is missing something the other has (20261025-181816-m6
// ran both on the same bare pawn):
//   1. the game's own path -- BP_PlayerController_C.InitializeCharacter, the call its
//      possession path makes for itself.  It is the whole init: bones 0 -> 164,
//      ai=ABP_Player_C, weapons 4 -> 7, CharacterId -> the dark form, and the dark
//      form's max health (75) that the death stack is a percentage OF.  It also turns
//      the game's WIREFRAME DEBUG DRAW on (red capsule round the player, arrows,
//      hit-shape ellipses along the blade) -- the open cosmetic item, see RebuildBody.
//   2. the copy -- SetSkeletalMeshAsset + SetAnimInstanceClass on CharacterMesh0
//      (which is what CREATES the anim instance) and SpawnAndEquipWeaponOfClass for
//      every weapon class the dead pawn carried.  Same body, clean picture, but the
//      pawn keeps its pawn-default attributes (max health 50), so it is the fallback.
// A step is done when BodyOk() says the pawn has an anim instance and more than the
// four unarmed weapon actors, and every attempt logs before/after facts, so a silent
// no-op is visible in the log instead of assumed.
// ===========================================================================
void ClearDeathMix();   // defined below
double g_freshBodyAt = -1.0e9;
bool   g_freshBody = false;

// what the dead pawn carried, read before it is destroyed
struct BodySnapshot {
    bool have = false;
    UObject* meshAsset = nullptr;
    UObject* animClass = nullptr;
    std::vector<UObject*> weaponClasses;
    std::string meshName;
    std::string animName;
    std::string weapons;
};
BodySnapshot g_bodyShot;

// The character mesh: CharacterMesh0 by name, else the first skinned component with
// bones (the same rule coop_local's ComputeBody uses to publish the body).
UObject* CharMesh(UObject* pawn) {
    UObject* mesh = nullptr;
    if (!ue::IsValid(pawn)) return nullptr;
    if (ue::Get(pawn, "Mesh", mesh) && ue::IsValid(mesh)) return mesh;
    for (UObject* c : ue::FindAllOf("SpartaSkeletalMeshComponent")) {
        if (!ue::IsValid(c)) continue;
        if (ue::GetOuter(c) != pawn) continue;
        return c;
    }
    return nullptr;
}

bool MeshAnimInstance(UObject* mesh, UObject** out) {
    if (out) *out = nullptr;
    if (!ue::IsValid(mesh)) return false;
    UFunction* fn = Func(mesh, "GetAnimInstance");
    if (!fn) return false;
    ue::Params p(fn);
    if (!ue::Call(mesh, fn, p.data())) return false;
    UObject* ai = p.Get<UObject*>("ReturnValue");
    if (!ue::IsValid(ai)) return false;
    if (out) *out = ai;
    return true;
}

// Every weapon ACTOR the pawn's WeaponsComponent holds, as CLASSES: that is what
// SpawnAndEquipWeaponOfClass takes, and the class survives the actor's destruction.
// ASpartaCharacter.WeaponsComponent @0x6B8, UWeaponsComponent.EquippedWeapons
// (TMap<FGameplayTag, ASpartaWeapon*>) @0xF8 -- research\mine\sync-map.md.
UObject* WeaponsComp(UObject* pawn) {
    UObject* wc = nullptr;
    if (!PropObj(pawn, "WeaponsComponent", &wc)) return nullptr;
    return wc;
}

std::vector<UObject*> EquippedWeaponClasses(UObject* wc, std::string* names) {
    std::vector<UObject*> out;
    std::string joined;
    if (!ue::IsValid(wc)) return out;
    ue::MapForEach(wc, "EquippedWeapons", [&](void* /*key*/, void* value) {
        UObject* w = nullptr;
        if (!ue::ReadMem(value, &w, sizeof(w)) || !ue::IsValid(w)) return true;
        UObject* cls = reinterpret_cast<UObject*>(ue::GetClass(w));
        if (!ue::IsValid(cls)) return true;
        const std::string n = ue::GetName(cls);
        for (UObject* seen : out) {
            if (ue::GetName(seen) == n) return true;
        }
        out.push_back(cls);
        if (!joined.empty()) joined += ",";
        joined += n;
        return true;
    });
    if (names) *names = joined;
    return out;
}

// One line of the facts that decide whether B's own body is on screen: the mesh's
// bones, its anim instance, its AnimClass, the animation mode, and the weapon actors
// (by class) attached to the pawn.
std::string BodyLine(UObject* pawn) {
    if (!ue::IsValid(pawn)) return "pawn=-";
    UObject* mesh = CharMesh(pawn);
    int bones = 0;
    UObject* ai = nullptr;
    UObject* animClass = nullptr;
    int mode = -1;
    if (ue::IsValid(mesh)) {
        int32_t n = 0;
        if (UFunction* f = Func(mesh, "GetNumBones")) {
            ue::Params p(f);
            if (ue::Call(mesh, f, p.data())) n = p.Get<std::int32_t>("ReturnValue");
        }
        bones = (int)n;
        MeshAnimInstance(mesh, &ai);
        if (UFunction* f = Func(mesh, "GetAnimClass")) {
            ue::Params p(f);
            if (ue::Call(mesh, f, p.data())) {
                UObject* c = p.Get<UObject*>("ReturnValue");
                if (ue::IsValid(c)) animClass = c;
            }
        }
        if (UFunction* f = Func(mesh, "GetAnimationMode")) {
            ue::Params p(f);
            if (ue::Call(mesh, f, p.data())) mode = (int)p.Get<std::uint8_t>("ReturnValue");
        }
    }
    int wp = 0;
    std::string acts;
    if (UFunction* fa = Func(pawn, "GetAttachedActors")) {
        ue::Params q(fa);
        q.Set<bool>("bResetArray", true);
        q.Set<bool>("bRecursivelyIncludeAttachedActors", true);
        if (ue::Call(pawn, fa, q.data())) {
            const auto arr = q.Get<ue::TArray<UObject*>>("OutActors");
            for (int32_t i = 0; i < arr.Num && i < 32; ++i) {
                UObject* a = nullptr;
                if (!ue::ReadMem(arr.Data + i, &a, sizeof(a)) || !ue::IsValid(a)) continue;
                const std::string cn = ue::GetClassName(a);
                if (cn.compare(0, 3, "WP_") != 0) continue;
                ++wp;
                if (!acts.empty()) acts += ",";
                acts += ue::GetName(a);
            }
        }
    }
    return net::Fmt("mesh=%s bones=%d ai=%s animClass=%s mode=%d weapons=%d[%s] dbgMove=%d dbgKeys=%d",
                    ue::IsValid(mesh) ? ue::GetName(mesh).c_str() : "-", bones,
                    ai ? ue::GetClassName(ai).c_str() : "-",
                    animClass ? ue::GetName(animClass).c_str() : "-", mode, wp, acts.c_str(),
                    ue::GetBool(pawn, "DebugMovement") ? 1 : 0,
                    ue::GetBool(pawn, "bEnableDebugKeys") ? 1 : 0);
}

// The verdict the ladder is steered by: an anim instance on the character mesh and
// more weapon actors than the four unarmed ones every pawn is born with.
bool BodyOk(UObject* pawn) {
    UObject* mesh = CharMesh(pawn);
    UObject* ai = nullptr;
    if (!MeshAnimInstance(mesh, &ai)) return false;
    int wp = 0;
    if (UFunction* fa = Func(pawn, "GetAttachedActors")) {
        ue::Params q(fa);
        q.Set<bool>("bResetArray", true);
        q.Set<bool>("bRecursivelyIncludeAttachedActors", true);
        if (ue::Call(pawn, fa, q.data())) {
            const auto arr = q.Get<ue::TArray<UObject*>>("OutActors");
            for (int32_t i = 0; i < arr.Num && i < 32; ++i) {
                UObject* a = nullptr;
                if (!ue::ReadMem(arr.Data + i, &a, sizeof(a)) || !ue::IsValid(a)) continue;
                if (ue::GetClassName(a).compare(0, 3, "WP_") == 0) ++wp;
            }
        }
    }
    return wp > 4;
}

// One small integer property of the object, -1 when the class does not have it.
// The debug switches are bools and one-byte enums whose exact type varies by BP, and a
// name a class does not have is NOT an error here -- it is simply not that switch --
// so a miss stays silent (and must: `has no parameter` is a real warning we chase).
int PropByte(UObject* o, const char* name, bool isBool) {
    if (!ue::IsValid(o)) return -1;
    ue::UClass* c = ue::GetClass(o);
    if (!c || !ue::FindProperty(c, name)) return -1;
    if (isBool) return ue::GetBool(o, name) ? 1 : 0;
    uint8_t b = 0;
    if (!ue::Get<uint8_t>(o, name, b)) return -1;
    return (int)b;
}

// THE WIREFRAMES on the respawned body (measured: a red capsule round the player,
// arrows up and down, hit-shape ellipses along the blade -- 20261025-185215-r6body
// B-after-cmds*.png) are not components: `debugscan` lists every component of the pawn
// and of the weapon actors and there is nothing debug-shaped hiding there to turn off.
// They are DrawDebug* calls from BP code that branches on a flag, and these are the
// flags the reflection dump shows on the player's own components.  READ-ONLY.
std::string DebugFlags(UObject* pawn) {
    if (!ue::IsValid(pawn)) return "pawn=-";
    std::string s;
    auto add = [&](const char* where, UObject* o, const char* name, bool isBool) {
        const int v = PropByte(o, name, isBool);
        if (v < 0) return;
        s += net::Fmt("%s%s=%d ", where, name, v);
    };
    add("", pawn, "DebugMovement", true);
    add("", pawn, "bEnableDebugKeys", true);
    UObject* tgt = nullptr;
    if (!PropObj(pawn, "Targeting Component", &tgt)) PropObj(pawn, "TargetingComponent", &tgt);
    if (ue::IsValid(tgt)) {
        add("targeting:", tgt, "bDebug", true);
        // the soft-targeting trace settings: a non-zero debug duration is the camera
        // capsule that is drawn every frame while it lasts
        if (UFunction* f = Func(tgt, "GetCameraAbsoluteTraceSettings")) {
            ue::Params p(f);
            if (ue::Call(tgt, f, p.data())) {
                const char* names[] = {"CurrentDebugDuration", "CameraAbsoluteRadius",
                                       "Camera Absolute Length"};
                for (const char* n : names) {
                    if (!ue::FindProperty((ue::UStruct*)f, n)) continue;
                    s += net::Fmt("targeting:%s=%.2f ", n, p.Get<double>(n));
                }
            }
        }
    }
    UObject* strafe = nullptr;
    if (!PropObj(pawn, "Strafing Component", &strafe)) PropObj(pawn, "StrafingComponent", &strafe);
    add("strafe:", strafe, "LedgeDetection_Debug", false);
    // and the weapons: an EDrawDebugTrace != None on any of them draws its trace every
    // time it swings, which is exactly the shape of the ellipses along the blade
    if (UFunction* fa = Func(pawn, "GetAttachedActors")) {
        ue::Params q(fa);
        q.Set<bool>("bResetArray", true);
        q.Set<bool>("bRecursivelyIncludeAttachedActors", true);
        if (ue::Call(pawn, fa, q.data())) {
            const auto arr = q.Get<ue::TArray<UObject*>>("OutActors");
            for (int32_t i = 0; i < arr.Num && i < 24; ++i) {
                UObject* a = nullptr;
                if (!ue::ReadMem(arr.Data + i, &a, sizeof(a)) || !ue::IsValid(a)) continue;
                const std::string cn = ue::GetClassName(a);
                if (cn.compare(0, 3, "WP_") != 0) continue;
                std::string one;
                const char* props[] = {"TraceDebug", "DrawDebugType", "DebugTrace", "DrawDebug",
                                       "ShowDebugHitTraces", "bShowDebugHitTraces"};
                for (const char* n : props) {
                    const int v = PropByte(a, n, false);
                    if (v < 0) continue;
                    one += net::Fmt("%s=%d ", n, v);
                }
                if (!one.empty()) s += cn + ":" + one;
            }
        }
    }
    return s.empty() ? "-" : s;
}

void SnapshotBody(UObject* pawn) {
    if (!ue::IsValid(pawn)) return;
    UObject* mesh = CharMesh(pawn);
    if (ue::IsValid(mesh)) {
        if (UFunction* f = Func(mesh, "GetSkeletalMeshAsset")) {
            ue::Params p(f);
            if (ue::Call(mesh, f, p.data())) {
                UObject* a = p.Get<UObject*>("ReturnValue");
                if (ue::IsValid(a)) g_bodyShot.meshAsset = a;
            }
        }
        if (UFunction* f = Func(mesh, "GetAnimClass")) {
            ue::Params p(f);
            if (ue::Call(mesh, f, p.data())) {
                UObject* c = p.Get<UObject*>("ReturnValue");
                if (ue::IsValid(c)) g_bodyShot.animClass = c;
            }
        }
    }
    std::string names;
    const std::vector<UObject*> classes = EquippedWeaponClasses(WeaponsComp(pawn), &names);
    if (!classes.empty()) {
        g_bodyShot.weaponClasses = classes;
        g_bodyShot.weapons = names;
    }
    g_bodyShot.meshName = g_bodyShot.meshAsset ? ue::GetName(g_bodyShot.meshAsset) : "-";
    g_bodyShot.animName = g_bodyShot.animClass ? ue::GetName(g_bodyShot.animClass) : "-";
    g_bodyShot.have = g_bodyShot.meshAsset || g_bodyShot.animClass || !classes.empty();
    net::Log("COOP DEATH body snapshot: mesh=%s animClass=%s weapons=%s",
             g_bodyShot.meshName.c_str(), g_bodyShot.animName.c_str(),
             g_bodyShot.weapons.c_str());
}

bool CallVoid1(UObject* o, const char* fnName, const char* arg, UObject* v) {
    if (!ue::IsValid(o)) return false;
    UFunction* f = Func(o, fnName);
    if (!f) return false;
    ue::Params p(f);
    p.Set<UObject*>(arg, v);
    return ue::Call(o, f, p.data());
}

// A step of the game's own path: the controller-side calls that dress a possessed
// pawn.  Only the ones whose parameters are known are called with parameters.
bool GameInitStep(int step, UObject* pc, UObject* pawn) {
    if (!ue::IsValid(pc)) return false;
    switch (step) {
        case 1:
            return CallVoid1(pc, "ReceivePossess", "PossessedPawn", pawn);
        case 2:
            return CallVoid(pc, "ActivateShellOrDarkform");
        case 3:
            return CallVoid(pc, "InitializeCharacter");
        case 4: {
            UFunction* f = Func(pc, "ActivateDarkForm");
            if (!f) return false;
            ue::Params p(f);
            p.Set<bool>("Pause", false);
            p.Set<bool>("bReinitPose", true);
            return ue::Call(pc, f, p.data());
        }
        case 8:
            return CallVoid(pc, "LoadCharacter");
        case 9:
            return CallVoid(pc, "LoadDarkFormFromSave");
        case 10:
            return CallVoid(pc, "LoadWeaponFromSave");
        case 11:
            return CallVoid(pc, "LoadShellFromSave");
        case 12:
            return CallVoid(pc, "OnPostPlayerSaveLoaded");
        case 13: {
            UFunction* f = Func(pc, "LoadItems");
            if (!f) return false;
            ue::Params p(f);
            p.Set<std::int32_t>("L_Stack", 0);
            p.Set<UObject*>("L_Item", nullptr);
            return ue::Call(pc, f, p.data());
        }
        default:
            return false;
    }
}

// defined with the presentation below (one bool, or two with the same value)
bool CallBool1(UObject* o, const char* fn, const char* arg, bool v, const char* arg2, bool v2);

// THE COPY: the mesh asset, the AnimBP and the weapon classes of the pawn that died.
bool ApplyBodySnapshot(UObject* pawn, const char* why) {
    if (!ue::IsValid(pawn) || !g_bodyShot.have) return false;
    bool ok = false;
    UObject* mesh = CharMesh(pawn);
    if (ue::IsValid(mesh) && (g_bodyShot.meshAsset || g_bodyShot.animClass)) {
        UObject* ai = nullptr;
        if (!MeshAnimInstance(mesh, &ai)) {
            // Clearing the AnimClass first is what the puppet's body swap does and
            // needs: swapping the mesh under a live AnimBP crashed before
            // (HANDOFF 5.5).  Setting it again after the asset is what CREATEs the
            // anim instance -- a plain asset swap does not.
            CallVoid1(mesh, "SetAnimInstanceClass", "NewClass", nullptr);
            if (g_bodyShot.meshAsset)
                CallVoid1(mesh, "SetSkeletalMeshAsset", "NewMesh", g_bodyShot.meshAsset);
            if (g_bodyShot.animClass) {
                const bool set =
                    CallVoid1(mesh, "SetAnimInstanceClass", "NewClass", g_bodyShot.animClass);
                net::Log("COOP DEATH body fix (%s): SetAnimInstanceClass(%s)=%d", why,
                         g_bodyShot.animName.c_str(), set ? 1 : 0);
            }
        }
        CallBool1(mesh, "SetVisibility", "bNewVisibility", true, "bPropagateToChildren", false);
        CallBool1(mesh, "SetHiddenInGame", "NewHidden", false, "bPropagateToChildren", false);
        ok = true;
    }
    // The weapon actors: spawn and equip every class the dead pawn carried that this
    // pawn does not already have (Kick/Fist are its own defaults).
    UObject* wc = WeaponsComp(pawn);
    std::string have;
    const std::vector<UObject*> now = EquippedWeaponClasses(wc, &have);
    int spawned = 0;
    for (UObject* cls : g_bodyShot.weaponClasses) {
        const std::string n = ue::GetName(cls);
        bool there = false;
        for (UObject* c : now) {
            if (ue::GetName(c) == n) {
                there = true;
                break;
            }
        }
        if (there) continue;
        if (UFunction* f = Func(wc, "SpawnAndEquipWeaponOfClass")) {
            ue::Params p(f);
            p.Set<UObject*>("WeaponClass", cls);
            if (ue::Call(wc, f, p.data())) ++spawned;
        }
    }
    if (wc) {
        std::string after;
        EquippedWeaponClasses(wc, &after);
        net::Log("COOP DEATH body fix (%s): weapons had[%s] spawned=%d now[%s]", why, have.c_str(),
                 spawned, after.c_str());
    } else {
        net::Log("COOP DEATH body fix (%s): no WeaponsComponent on %s", why,
                 ue::GetName(pawn).c_str());
    }
    // Material instances: BP_PlayerCharacter_C builds them once at BeginPlay, and a
    // fresh pawn's are built before it has a mesh.
    CallVoid(pawn, "CreateMatInstances");
    return ok;
}

// When to run the fix (net::Now() based; -1 = nothing pending).
double g_bodyFixAt = -1.0;
int    g_bodyFixTries = 0;
// when to re-read the debug flags after a successful repair (-1 = nothing pending)
double g_flagsAt = -1.0;

void ArmBodyFix(double when) {
    g_bodyFixAt = when;
    g_bodyFixTries = 0;
}

// `coop\BODYFIX_OFF.txt`: the research switch, same shape as the other *_OFF.txt
// files -- with it present the respawn does not repair the body, so `bodyinit` can
// measure one step at a time in a real post-respawn pawn.
int BodyFixDisabled() {
    static int last = -1;
    static double at = -1.0e9;
    if (last < 0 || net::Now() - at > 2.0) {
        at = net::Now();
        const int v = net::FileExists("BODYFIX_OFF.txt") ? 1 : 0;
        if (v != last && v)
            net::Log("death: BODYFIX_OFF.txt present -- the respawn leaves the body to "
                     "`bodyinit`");
        last = v;
    }
    return last < 0 ? 0 : last;
}

// ---------------------------------------------------------------------------
// THE DEATH ABILITY, WHICH THE GAME'S RESET ENDS AND A CO-OP RESPAWN DID NOT.
//
// MEASURED (round 5's `run-two-death` logs, re-read in round 8): `S_KillMe` answers
// ok=true for the FIRST death of a session and then does nothing at all -- the second and
// third calls log ok=true and no death event follows, on both clients, with the
// controller's death-mix flag already cleared by the co-op respawn and the health
// component back at `deathState=0 deadOrDying=0` (lab 20261025-113306 / -114029 and
// 20261026-091159).  What has NOT been undone is the game's death ability:
// `GA_Death_Player_C` (`GA_DeathBase` -> `GA_SpartaAbility`) is an instanced
// GameplayAbility, and it only ends on the game's own reset path -- the very path this
// module blocks.  An active instanced ability refuses a second activation, so after a
// co-op respawn the player cannot die again: no death screen, no death camera, no
// stacks, nothing.  That is a co-op-only bug (solo always gets the reset), and it is
// also what makes "three deaths in one session" impossible to test.
//
// The fix is the step the reset would have taken: end the death ability once the
// co-op respawn has put the player back on their feet, and clear the death state it
// granted.  Both are logged before and after, and `coop\DEATHABILITY_OFF.txt` keeps
// the old (broken) behaviour for A/B measurement.
int DeathAbilityDisabled() {
    static int last = -1;
    static double at = -1.0e9;
    if (last < 0 || net::Now() - at > 2.0) {
        at = net::Now();
        const int v = net::FileExists("DEATHABILITY_OFF.txt") ? 1 : 0;
        if (v != last && v)
            net::Log("death: DEATHABILITY_OFF.txt present -- a co-op respawn leaves the game's "
                     "death ability ACTIVE (the pre-round-8 behaviour: no second death)");
        last = v;
    }
    return last < 0 ? 0 : last;
}

// Every live player death ability on this machine, with the flag that decides whether
// it can be activated again.  `bIsActive` is UGameplayAbility's own (reflection offset,
// never a guessed byte).
std::vector<UObject*> DeathAbilities(int* activeOut) {
    std::vector<UObject*> out;
    int active = 0;
    for (UObject* o : ue::FindAllOf("GA_Death_Player_C")) {
        if (!ue::IsValid(o)) continue;
        out.push_back(o);
        int a = 0;
        if (PropInt(o, "bIsActive", &a) && a) ++active;
    }
    if (activeOut) *activeOut = active;
    return out;
}

// The step the game's own reset performs: end the death ability (which runs its
// K2_OnEndAbility cleanup), then drop the death state it granted.  Returns the number
// of abilities that were still active before the call.
int EndDeathAbilities(const char* why) {
    int active = 0;
    const std::vector<UObject*> ab = DeathAbilities(&active);
    net::Log("COOP DEATH death ability (%s): %d instance(s), %d still ACTIVE", why,
             (int)ab.size(), active);
    if (ab.empty()) return 0;
    for (size_t i = 0; i < ab.size(); ++i) {
        UObject* a = ab[i];
        int wasActive = 0;
        PropInt(a, "bIsActive", &wasActive);
        // An instance that already ended is left alone: K2_EndAbility on a finished
        // ability is not a no-op worth risking, and there is nothing to clean up.
        if (!wasActive) continue;
        // The state it granted while the player was down (GA_DeathBase.GrantDeathState /
        // RemoveDeathState); safe to ask for even when it was not granted.
        const bool rm = CallVoid(a, "RemoveDeathState");
        const bool ended = CallVoid(a, "K2_EndAbility");
        int nowActive = 0;
        PropInt(a, "bIsActive", &nowActive);
        net::Log("COOP DEATH death ability (%s): %s active %d -> %d (RemoveDeathState=%d "
                 "K2_EndAbility=%d)", why, ue::GetName(a).c_str(), wasActive, nowActive,
                 rm ? 1 : 0, ended ? 1 : 0);
    }
    int activeAfter = 0;
    DeathAbilities(&activeAfter);
    if (activeAfter > 0)
        net::Log("WARN death: the death ability is STILL active after K2_EndAbility (%d of %d) "
                 "-- a later real death may be refused", activeAfter, (int)ab.size());
    else
        net::Log("COOP DEATH death ability ended: a later real death can run again");
    return active;
}

// The whole repair for one attempt.  Returns true when the body is now complete.
//
// MEASURED (lab runs 20261025-161824-m2 and -160740-m1, `bodyinit <n>` = one
// controller call each, same session, on the bare post-respawn pawn):
//
//   ActivateShellOrDarkform()   ok=1, and NOTHING changes
//   ReceivePossess(pawn)        ok=1, and NOTHING changes
//   LoadDarkFormFromSave()      ok=1, and NOTHING changes
//   LoadWeaponFromSave()        ok=1, and NOTHING changes
//   LoadCharacter()             ok=1, and NOTHING changes
//   InitializeCharacter()       bones 0 -> 164, anim instance created, weapon actors
//                               4 -> 7, CharacterId "CharacterId.Player" ->
//                               "CharacterId.Player.Darkform.StrongOne"
//
// The game's own InitializeCharacter is therefore the provider that does the WHOLE job
// -- the body AND the attributes the debuff is measured on: with the copy alone the
// fresh pawn keeps its pawn-default max health (50, so one stack reads 45 where the
// rule wants 90 % of 75 = 67.5 -- that is exactly how dcASE2 failed on
// 20261025-193753-death-two), and only the game's init puts the dark form's 75 there.
//
// Its price is the debug draw: 20261025-181816-m6 ran both providers on the SAME bare
// pawn, and `bodyinit 5` (the copy -- mesh asset + AnimBP + the three weapon classes)
// gave the same bones=164 ai=ABP_Player_C weapons=7 with a CLEAN picture, while the
// `bodyinit 3` that followed put the game's wireframes back on screen (a red capsule
// round the player, arrows, hit-shape ellipses along the blade: step04.png vs
// step05.png of that run).  So the copy is the fallback -- a body the player can see,
// without the right attributes -- and the wireframes are the open cosmetic item, with
// `DebugFlags()` logging the switches around every repair.
bool RebuildBody(UObject* pawn, const char* why) {
    if (!ue::IsValid(pawn)) return false;
    net::Log("COOP DEATH body fix (%s): before[%s]", why, BodyLine(pawn).c_str());
    if (BodyOk(pawn)) {
        net::Log("COOP DEATH body fix (%s): nothing to do", why);
        return true;
    }
    // 1. the game's own character setup, once per respawn: it is the whole init (body
    //    AND attributes) and it re-spawns every weapon actor.
    UObject* pc = ue::IsValid(state().death.pc) ? state().death.pc : net::PC();
    if (g_bodyFixTries == 0) {
        const bool ok = GameInitStep(3, pc, pawn);
        net::Log("COOP DEATH body fix (%s): InitializeCharacter=%d -> %s", why, ok ? 1 : 0,
                 BodyLine(pawn).c_str());
        if (BodyOk(pawn)) return true;
    }
    // 2. the copy from the pawn that died: the guaranteed provider, but it cannot
    //    bring the attributes the controller's init sets up.
    if (g_bodyShot.have) {
        ApplyBodySnapshot(pawn, why);
        net::Log("COOP DEATH body fix (%s): copy -> %s", why, BodyLine(pawn).c_str());
    } else {
        net::Log("COOP DEATH body fix (%s): no snapshot of the dead pawn to copy", why);
    }
    net::Log("COOP DEATH body fix (%s): after[%s]", why, BodyLine(pawn).c_str());
    return BodyOk(pawn);
}

// One item definition from the controller, named the way its signature declares it.
// BPI_PlayerController's accessors are not uniform -- GetCurrentShell/Weapon/Sidearm
// are EVENTS with a `TSubclassOf<...>&` out-parameter and no return value, while
// GetCurrentDarkFormDef/GetActiveSealItemDef return theirs.  Asking the wrong one for
// "ReturnValue" is a silent nothing plus a "has no parameter" line in the log, so each
// name is checked against the function before it is read.
std::string ControllerItemName(UObject* pc, const char* fnName, const char* outParam) {
    if (!ue::IsValid(pc)) return "-";
    UFunction* fn = Func(pc, fnName);
    if (!fn) return "-";
    ue::Params p(fn);
    if (!ue::Call(pc, fn, p.data())) return "-";
    const char* read = outParam ? outParam : "ReturnValue";
    if (!ue::FindProperty((ue::UStruct*)fn, read)) return "-?";
    UObject* o = p.Get<UObject*>(read);
    return ue::IsValid(o) ? ue::GetName(o) : "-";
}

// `bodyprobe`: the equipment the CONTROLLER still has (it is why the HUD draws the
// right slots after a respawn), the pawn's body facts, and the snapshot the fix will
// rebuild from.  Read-only: the same facts selfcheck logs, plus the item definitions.
void BodyProbe(const char* why) {
    DeathState& D = state().death;
    UObject* pc = ue::IsValid(D.pc) ? D.pc : net::PC();
    UObject* pawn = net::Pawn();
    ue::FName charId{};
    if (ue::IsValid(pawn)) ue::Get(pawn, "CharacterId", charId);
    // name, function, out-parameter (nullptr = the function returns its value)
    struct Probe { const char* label; const char* fn; const char* out; };
    const Probe defs[] = {{"Shell", "GetCurrentShell", "ShellId"},
                          {"DarkForm", "GetCurrentDarkFormDef", nullptr},
                          {"Weapon", "GetCurrentWeapon", "WeaponID"},
                          {"Sidearm", "GetCurrentSidearm", "SidearmID"},
                          {"Seal", "GetActiveSealItemDef", nullptr}};
    std::string eq;
    for (const Probe& d : defs) {
        eq += d.label;
        eq += "=";
        eq += ControllerItemName(pc, d.fn, d.out);
        eq += " ";
    }
    bool loaded = false;
    CallBool(pc, "GetIsPlayerSaveLoaded", &loaded);
    net::Log("bodyprobe (%s): pc=%s saveLoaded=%d deathmix=%d charId=%s", why,
             ue::IsValid(pc) ? ue::GetName(pc).c_str() : "-", loaded ? 1 : 0,
             ReadDeathMix(pc, nullptr) ? 1 : 0, ue::ToString(charId).c_str());
    net::Log("bodyprobe (%s): %s", why, BodyLine(pawn).c_str());
    std::string have;
    EquippedWeaponClasses(WeaponsComp(pawn), &have);
    net::Log("bodyprobe (%s): equip %s| equipped[%s] snapshot[mesh=%s anim=%s weapons=%s]", why,
             eq.c_str(), have.c_str(), g_bodyShot.meshName.c_str(), g_bodyShot.animName.c_str(),
             g_bodyShot.weapons.c_str());
}

// `debugscan`: every component of the pawn AND of the actors attached to it, with
// its visibility.  Round 6 left one thing open: after a respawn the body is right but
// wire shapes are drawn around it (a capsule outline, ellipses along the blade, two
// arrows) that are NOT there before the death.  They are world-space and they follow
// the character, so they are components -- the weapon actors are where the hit-trace
// shapes live, and `pupdump self` never looks inside them.  Read-only.
void DebugScan() {
    UObject* pawn = net::Pawn();
    if (!ue::IsValid(pawn)) {
        net::Log("debugscan: no pawn");
        return;
    }
    UFunction* byClass = Func(pawn, "K2_GetComponentsByClass");
    UObject* sceneCls = ue::FindObject("/Script/Engine.SceneComponent");
    if (!byClass || !sceneCls) {
        net::Log("debugscan: no K2_GetComponentsByClass/SceneComponent");
        return;
    }
    std::vector<UObject*> actors;
    actors.push_back(pawn);
    if (UFunction* fa = Func(pawn, "GetAttachedActors")) {
        ue::Params q(fa);
        q.Set<bool>("bResetArray", true);
        q.Set<bool>("bRecursivelyIncludeAttachedActors", true);
        if (ue::Call(pawn, fa, q.data())) {
            const auto arr = q.Get<ue::TArray<UObject*>>("OutActors");
            for (int32_t i = 0; i < arr.Num && i < 24; ++i) {
                UObject* a = nullptr;
                if (ue::ReadMem(arr.Data + i, &a, sizeof(a)) && ue::IsValid(a)) actors.push_back(a);
            }
        }
    }
    for (UObject* a : actors) {
        net::Log("debugscan: actor %s %s hidden=%d", ue::GetClassName(a).c_str(),
                 ue::GetName(a).c_str(), ue::GetBool(a, "bHidden") ? 1 : 0);
        ue::Params p(byClass);
        p.Set<UObject*>("ComponentClass", sceneCls);
        if (!ue::Call(a, byClass, p.data())) continue;
        const auto comps = p.Get<ue::TArray<UObject*>>("ReturnValue");
        for (int32_t i = 0; i < comps.Num && i < 32; ++i) {
            UObject* c = nullptr;
            if (!ue::ReadMem(comps.Data + i, &c, sizeof(c)) || !ue::IsValid(c)) continue;
            bool vis = false;
            CallBool(c, "IsVisible", &vis);
            net::Log("debugscan:   %s %s vis=%d hiddenInGame=%d tick=%d", ue::GetClassName(c).c_str(),
                     ue::GetName(c).c_str(), vis ? 1 : 0,
                     ue::GetBool(c, "bHiddenInGame") ? 1 : 0,
                     ue::GetBool(c, "bComponentTickEnabled") ? 1 : 0);
        }
    }
}

// `dbghide`: the ACTION for what `debugscan` finds -- every component of the pawn and
// of its attached actors whose class name looks like a shape/visualiser
// (Debug|HitTrace|Capsule|Trace|Shape) is hidden from the viewport.  Rendering only:
// SetVisibility/SetHiddenInGame do not touch collision, so a real capsule keeps
// working, and this is reversible by hiding nothing else.
int DebugHide() {
    UObject* pawn = net::Pawn();
    if (!ue::IsValid(pawn)) return 0;
    UFunction* byClass = Func(pawn, "K2_GetComponentsByClass");
    UObject* sceneCls = ue::FindObject("/Script/Engine.SceneComponent");
    if (!byClass || !sceneCls) return 0;
    std::vector<UObject*> actors;
    actors.push_back(pawn);
    if (UFunction* fa = Func(pawn, "GetAttachedActors")) {
        ue::Params q(fa);
        q.Set<bool>("bResetArray", true);
        q.Set<bool>("bRecursivelyIncludeAttachedActors", true);
        if (ue::Call(pawn, fa, q.data())) {
            const auto arr = q.Get<ue::TArray<UObject*>>("OutActors");
            for (int32_t i = 0; i < arr.Num && i < 24; ++i) {
                UObject* a = nullptr;
                if (ue::ReadMem(arr.Data + i, &a, sizeof(a)) && ue::IsValid(a)) actors.push_back(a);
            }
        }
    }
    int hidden = 0;
    for (UObject* a : actors) {
        ue::Params p(byClass);
        p.Set<UObject*>("ComponentClass", sceneCls);
        if (!ue::Call(a, byClass, p.data())) continue;
        const auto comps = p.Get<ue::TArray<UObject*>>("ReturnValue");
        for (int32_t i = 0; i < comps.Num && i < 32; ++i) {
            UObject* c = nullptr;
            if (!ue::ReadMem(comps.Data + i, &c, sizeof(c)) || !ue::IsValid(c)) continue;
            const std::string cn = ue::GetClassName(c);
            if (cn.find("Debug") == std::string::npos && cn.find("HitTrace") == std::string::npos &&
                cn.find("Capsule") == std::string::npos && cn.find("Trace") == std::string::npos &&
                cn.find("Shape") == std::string::npos)
                continue;
            const bool a1 =
                CallBool1(c, "SetVisibility", "bNewVisibility", false, "bPropagateToChildren", true);
            const bool a2 =
                CallBool1(c, "SetHiddenInGame", "NewHidden", true, "bPropagateToChildren", true);
            net::Log("dbghide: %s %s (vis=%d hidden=%d)", cn.c_str(), ue::GetName(c).c_str(),
                     a1 ? 1 : 0, a2 ? 1 : 0);
            ++hidden;
        }
    }
    net::Log("dbghide: %d component(s) hidden", hidden);
    return hidden;
}

// `bodyinit [n]`: run ONE repair step by hand (the research verb; the respawn runs
// RebuildBody itself).  1 ReceivePossess, 2 ActivateShellOrDarkform, 3
// InitializeCharacter, 4 ActivateDarkForm, 5 the mesh copy, 6 the weapon copy,
// 7 CreateMatInstances, 8 LoadCharacter, 0/empty the whole repair.
void BodyInit(int step) {
    DeathState& D = state().death;
    UObject* pc = ue::IsValid(D.pc) ? D.pc : net::PC();
    UObject* pawn = net::Pawn();
    if (!ue::IsValid(pawn)) {
        net::Log("bodyinit: no pawn");
        return;
    }
    net::Log("bodyinit(%d): before[%s]", step, BodyLine(pawn).c_str());
    bool ok = false;
    const char* what = "whole repair";
    switch (step) {
        case 1: what = "ReceivePossess"; ok = GameInitStep(1, pc, pawn); break;
        case 2: what = "ActivateShellOrDarkform"; ok = GameInitStep(2, pc, pawn); break;
        case 3: what = "InitializeCharacter"; ok = GameInitStep(3, pc, pawn); break;
        case 4: what = "ActivateDarkForm"; ok = GameInitStep(4, pc, pawn); break;
        case 5: what = "mesh copy"; ok = ApplyBodySnapshot(pawn, "bodyinit 5"); break;
        case 6: what = "weapon copy"; ok = ApplyBodySnapshot(pawn, "bodyinit 6"); break;
        case 7: what = "CreateMatInstances"; ok = CallVoid(pawn, "CreateMatInstances"); break;
        case 8: what = "LoadCharacter"; ok = GameInitStep(8, pc, pawn); break;
        case 9: what = "LoadDarkFormFromSave"; ok = GameInitStep(9, pc, pawn); break;
        case 10: what = "LoadWeaponFromSave"; ok = GameInitStep(10, pc, pawn); break;
        case 11: what = "LoadShellFromSave"; ok = GameInitStep(11, pc, pawn); break;
        case 12: what = "OnPostPlayerSaveLoaded"; ok = GameInitStep(12, pc, pawn); break;
        case 13: what = "LoadItems"; ok = GameInitStep(13, pc, pawn); break;
        default:
            ok = RebuildBody(pawn, "bodyinit");
            net::Log("bodyinit: %s -> after[%s]", ok ? "body complete" : "still bare",
                     BodyLine(pawn).c_str());
            return;
    }
    net::Log("bodyinit(%d) %s=%d -> %s", step, what, ok ? 1 : 0, BodyLine(pawn).c_str());
}

// step 1 (owner, 2026-10-25): A FRESH BODY.  The game never revives a dead player
// pawn -- its reset spawns a new one (ClientRestart).  Reviving the corpse in place
// left the mesh ragdolled (measured: physics turned back on 3 s after we cleared it),
// i.e. "no body after death".  So: unpossess the corpse, have the game mode spawn and
// possess a new pawn at the nearest player start, destroy the corpse.  If no new pawn
// appears, the old one is possessed again and the old ladder continues.

bool StepFreshBody(UObject* oldPawn) {
    DeathState& D = state().death;
    UObject* pc = D.pc;
    UObject* gm = ue::FindFirstOf("BP_SpartaGameMode_C");
    if (!ue::IsValid(pc) || !ue::IsValid(gm) || !ue::IsValid(oldPawn)) {
        net::Log("RESPAWN fresh body: missing pc/gamemode/pawn");
        return false;
    }
    const Vec3 from = D.hasDeathPos ? D.deathPos : net::Loc(oldPawn);
    double dist = 0.0;
    UObject* start = NearestPlayerStart(from, &dist);
    UFunction* rf = Func(gm, "RestartPlayerAtPlayerStart");
    if (!start || !rf) {
        net::Log("RESPAWN fresh body: no player start (%d) or RestartPlayerAtPlayerStart (%d)",
                 start ? 1 : 0, rf ? 1 : 0);
        return false;
    }
    // WHAT THE DEAD BODY CARRIES must be read while it is still there: the mesh
    // asset, the AnimBP and the equipped weapon classes are what the new pawn is
    // rebuilt from (see RebuildBody).
    SnapshotBody(oldPawn);
    // THE DEAD FLAG GOES BEFORE THE POSSESS, not after.  The controller's own
    // possession path (ReceivePossess -> InitializeCharacter -> ActivateX) is what
    // dresses a pawn in this game, and the new pawn arrived undressed with the
    // death mix still on -- the one state in which that path does nothing.
    ClearDeathMix();
    const bool un = CallVoid(pc, "UnPossess");
    ue::Params p(rf);
    p.Set<UObject*>("NewPlayer", pc);
    p.Set<UObject*>("StartSpot", start);
    const bool ok = ue::Call(gm, rf, p.data());
    UObject* now = CallObject(pc, "K2_GetPawn");
    const bool fresh = ue::IsValid(now) && now != oldPawn;
    net::Log("RESPAWN fresh body: UnPossess=%d RestartPlayerAtPlayerStart(%s d=%.0f)=%d -> pawn %s (%s)",
             un ? 1 : 0, ue::GetName(start).c_str(), dist, ok ? 1 : 0,
             now ? ue::GetName(now).c_str() : "-", fresh ? "NEW" : "not new");
    if (!fresh) {
        if (UFunction* pf = Func(pc, "Possess")) {
            ue::Params pp(pf);
            pp.Set<UObject*>("InPawn", oldPawn);
            ue::Call(pc, pf, pp.data());
        }
        return false;
    }
    net::Log("RESPAWN fresh body: before[%s]", BodyLine(oldPawn).c_str());
    CallVoid(oldPawn, "K2_DestroyActor");
    g_freshBody = true;
    g_freshBodyAt = net::Now();
    ClearDeathMix();   // ... and again after: the possess path may re-apply it
    // THE BODY GOES ON IN THIS STEP, not on a later tick: the new pawn's max health is
    // the pawn default (50) until the controller's InitializeCharacter runs, and the
    // death stack is a PERCENTAGE of that number, so a repair that lands after the
    // death has been published leaves the player on 45/75 = 60 % (dcASE2 measured
    // exactly that).  Repair, then put the stack back on the body he ends up with.
    const bool fixed = RebuildBody(now, "fresh body");
    if (fixed) {
        g_flagsAt = net::Now() + 20.0;   // re-read the debug flags once it has all settled
        Tick2();
    }
    if (!fixed || !BodyOk(now)) ArmBodyFix(net::Now() + 1.0);   // the tick retries it
    return true;
}

// step 2: the game mode's own restart, at the nearest player start if one is
// found (RestartPlayerAtPlayerStart), otherwise the plain restart.
bool StepRestart(UObject* pawn) {
    DeathState& D = state().death;
    UObject* gm = ue::FindFirstOf("BP_SpartaGameMode_C");
    if (!ue::IsValid(gm)) {
        net::Log("RESPAWN step 2: no BP_SpartaGameMode_C in this world");
        return false;
    }
    const Vec3 from = D.hasDeathPos ? D.deathPos : net::Loc(pawn);
    double dist = 0.0;
    UObject* start = NearestPlayerStart(from, &dist);
    if (start && ue::IsValid(D.pc)) {
        UFunction* fn = Func(gm, "RestartPlayerAtPlayerStart");
        if (fn) {
            const char* who = nullptr;
            if (ue::FindProperty((ue::UStruct*)fn, "NewPlayer")) who = "NewPlayer";
            if (!who && ue::FindProperty((ue::UStruct*)fn, "Player")) who = "Player";
            ue::Params p(fn);
            if (who) p.Set<UObject*>(who, D.pc);
            const char* spot = nullptr;
            if (ue::FindProperty((ue::UStruct*)fn, "StartSpot")) spot = "StartSpot";
            if (!spot && ue::FindProperty((ue::UStruct*)fn, "StartPoint")) spot = "StartPoint";
            if (spot) p.Set<UObject*>(spot, start);
            const bool ok = (who && spot) ? ue::Call(gm, fn, p.data()) : false;
            net::Log("RESPAWN step 2: GameMode.RestartPlayerAtPlayerStart(%s,%s) start=%s "
                     "d=%.0f ok=%s",
                     who ? who : "?", spot ? spot : "?", ue::GetName(start).c_str(), dist,
                     ok ? "true" : "false");
            if (ok) return true;
        }
    }
    const bool ok = CallVoid(gm, "RestartPlayer");
    net::Log("RESPAWN step 2b: GameMode.RestartPlayer() ok=%s", ok ? "true" : "false");
    return ok;
}

// step 3: put the pawn back on its feet by hand, at the nearest respawn point.
// This is the fallback the reference mod also keeps, and it is the one step that
// touches the death state directly -- so it says so, loudly, and only runs after
// the two engine paths above have done nothing.
bool StepHandRespawn(UObject* pawn) {
    DeathState& D = state().death;
    if (!ue::IsValid(pawn)) return false;
    const Vec3 from = D.hasDeathPos ? D.deathPos : net::Loc(pawn);
    double dist = 0.0;
    UObject* start = NearestPlayerStart(from, &dist);
    bool moved = false;
    if (start) moved = TeleportPawnTo(pawn, net::Loc(start));
    net::Log("RESPAWN step 3: teleport pawn to nearest player start %s d=%.0f ok=%s",
             start ? ue::GetName(start).c_str() : "(none)", dist, moved ? "true" : "false");

    UObject* hc = HealthCompOf(pawn);
    if (!hc) return false;
    int st = 0;
    if (PropInt(hc, "DeathState", &st)) {
        if (st != 0) {
            std::uint8_t zero = 0;
            const bool ok = ue::Set<std::uint8_t>(hc, "DeathState", zero);
            net::Log("RESPAWN step 3: DeathState %d -> 0 ok=%s (the aggressive path)", st,
                     ok ? "true" : "false");
        }
    }
    // Full health at the CURRENT max (a stack may have lowered it), base and
    // current, on both the shell bar and the pool the bar draws from.
    UObject* set = nullptr;
    if (PropObj(hc, "HealthSet", &set)) {
        float maxHp = 0.0f, maxShell = 0.0f;
        ReadAttr(set, "MaxHealth", &maxHp);
        ReadAttr(set, "MaxShellHealth", &maxShell);
        if (maxHp > 0.0f) WriteAttr(set, "Health", maxHp);
        if (maxShell > 0.0f) WriteAttr(set, "ShellHealth", maxShell);
        net::Log("RESPAWN step 3: health -> max (%.1f / shell %.1f)", (double)maxHp,
                 (double)maxShell);
    }
    return true;
}

// THE HOST ASKS FOR THE WIPE RESET ITSELF.
//
// The death screen's reset is a SELECTION in this game (WBP_DeathScreen_C has
// `OnDeathSelection` and `HasSelection`), so a wipe can leave the host sitting on
// that screen instead of resetting.  The reference mod's rule is explicit about
// this: "the HOST reloads (its own death reload passes, **or it is asked for
// again**)".  This is that second arm.
//
// It only ever runs on the host, only for a case-1 wipe, and only after a grace
// period in which the game could have done it by itself -- and it verifies the
// outcome by waiting for the world change, escalating to the subsystem only if the
// widget's own call did nothing.
double g_lastHostResetAt = -1.0e9;
int    g_hostResetStage = 0;   // 0 none, 1 asked the widget, 2 asked the subsystem

void HostAskForReset() {
    DeathState& D = state().death;
    if (D.off || !D.pending || D.partyCase != COOP_CASE_WIPE) return;
    if (!IsHost()) return;
    const double now = net::Now();
    if (now - D.deathAt < 3.0) return;              // grace: let the game do it
    if (now - g_lastHostResetAt < 4.0) return;
    g_lastHostResetAt = now;

    if (g_hostResetStage == 0) {
        g_hostResetStage = 1;
        for (UObject* w : ue::FindAllOf("WBP_DeathScreen_C")) {
            if (!ue::IsValid(w)) continue;
            const bool ok = CallVoid(w, "ResetWorld");
            net::Log("COOP DEATH host: asked the death screen to reset the world ok=%s "
                     "(the wipe's reset is this machine's)",
                     ok ? "true" : "false");
            return;
        }
        net::Log("COOP DEATH host: no death screen widget to ask; will ask the subsystem");
    }
    // The widget's call did nothing (or there was no widget): ask the world-gen
    // subsystem directly -- the function the whole flow ends in.
    if (g_hostResetStage >= 1) {
        g_hostResetStage = 2;
        UObject* wg = ue::FindFirstOf("SpartaWorldGenSubsystem");
        const bool ok = ue::IsValid(wg) ? CallVoid(wg, "BeginWorldReset") : false;
        net::Log("COOP DEATH host: %s.BeginWorldReset() ok=%s (stage 2)",
                 ue::IsValid(wg) ? ue::GetName(wg).c_str() : "(no subsystem)",
                 ok ? "true" : "false");
    }
}

// THE DEATH MIX IS THE GAME'S OWN "dead" FLAG, so the last step of every respawn is the
// game's own clear -- the same function the game's reset path calls.  Without it the
// controller keeps the death post-process and this module keeps believing the player
// is dead, which would leave a hold that never ends.
void ClearDeathMix() {
    DeathState& D = state().death;
    if (!ue::IsValid(D.pc)) return;
    bool readable = false;
    const bool on = ReadDeathMix(D.pc, &readable);
    if (!readable || !on) return;
    const bool ok = CallVoid(D.pc, "ClearDeathMix");
    net::Log("COOP DEATH respawn: ClearDeathMix ok=%s", ok ? "true" : "false");
}

// A respawn puts the PAWN back, but the controller may still be ignoring input
// (the death screen's effect).  Both calls are Engine's own and take no arguments.
void RestoreInput() {
    DeathState& D = state().death;
    if (!ue::IsValid(D.pc)) return;
    const bool a = CallVoid(D.pc, "ResetIgnoreMoveInput");
    const bool b = CallVoid(D.pc, "ResetIgnoreLookInput");
    net::Log("COOP DEATH respawn: input restored (move=%s look=%s)", a ? "true" : "false",
             b ? "true" : "false");
}

// THE PLAYER'S PRESENTATION after a respawn this module ran.  The game shows the
// body and the HUD again on its own reset path (ClientRestart on a new controller),
// which is exactly the path a suppressed death blocks -- owner report 2026-10-25:
// "the second player doesn't show the player body even to themselves, also no UI".
// So the respawn puts both back explicitly: the pawn and every mesh un-hidden, and
// the UI handler's HUD created if missing and shown.  `selfcheck` logs the same
// facts without changing anything (fix=false).
bool SetViewTargetLocal(UObject* pc, UObject* target);   // defined with the camera below
double g_presentAt = -1.0;   // when to (re)apply Presentation(true) after a respawn
int    g_presentPass = 0;

bool CallBool1(UObject* o, const char* fn, const char* arg, bool v, const char* arg2 = nullptr,
               bool v2 = false) {
    if (!ue::IsValid(o)) return false;
    UFunction* f = Func(o, fn);
    if (!f) return false;
    ue::Params p(f);
    p.Set<bool>(arg, v);
    if (arg2) p.Set<bool>(arg2, v2);
    return ue::Call(o, f, p.data());
}

UObject* UiHandler(UObject* pc) {
    UObject* h = nullptr;
    if (ue::IsValid(pc) && ue::Get(pc, "User Interface Handler Component", h) && ue::IsValid(h)) return h;
    for (UObject* o : ue::FindAllOf("BPC_UserInterfaceHandler_C")) {
        if (ue::IsValid(o)) return o;
    }
    return nullptr;
}

std::string Presentation(bool fix, const char* why) {
    DeathState& D = state().death;
    UObject* pc = ue::IsValid(D.pc) ? D.pc : net::PC();
    UObject* pawn = net::Pawn();
    UObject* mesh = nullptr;
    if (ue::IsValid(pawn)) ue::Get(pawn, "Mesh", mesh);
    int chars = 0;
    for (UObject* o : ue::FindAllOf("BP_PlayerCharacter_C")) {
        if (ue::IsValid(o) && o != puppet::Actor()) ++chars;
    }
    const auto facts = [&]() {
        bool meshVis = false;
        if (ue::IsValid(mesh)) CallBool(mesh, "IsVisible", &meshVis);
        UObject* h = UiHandler(pc);
        UObject* hud = nullptr;
        if (h) ue::Get(h, "WBP_Player_HUD", hud);
        bool inVp = false, hudVis = false;
        if (ue::IsValid(hud)) {
            CallBool(hud, "IsInViewport", &inVp);
            CallBool(hud, "IsVisible", &hudVis);
        }
        UObject* vt = ue::IsValid(pc) ? CallObject(pc, "GetViewTarget") : nullptr;
        int deathCam = 0;
        for (UObject* st : CameraStack(pc, nullptr))
            if (ue::GetClassName(st).find("Death") != std::string::npos) ++deathCam;
        ue::FVector pov{};
        double povToBody = -1.0;
        if (ue::IsValid(pawn) && CamPov(pc, &pov)) {
            const Vec3 b = net::Loc(pawn);
            povToBody = std::sqrt((pov.X - b.X) * (pov.X - b.X) + (pov.Y - b.Y) * (pov.Y - b.Y) +
                                  (pov.Z - b.Z) * (pov.Z - b.Z));
        }
        double op0 = -1.0;
        if (ue::IsValid(hud)) {
            if (UFunction* gf = Func(hud, "GetHUDOpacity")) {
                ue::Params gp(gf);
                gp.Set<std::uint8_t>("Layer", 0);
                if (ue::Call(hud, gf, gp.data())) op0 = gp.Get<double>("ReturnValue");
            }
        }
        double meshToActor = -1.0;
        bool simulating = false;
        if (ue::IsValid(mesh) && ue::IsValid(pawn)) {
            UFunction* lf = Func(mesh, "K2_GetComponentLocation");
            if (lf) {
                ue::Params lp(lf);
                if (ue::Call(mesh, lf, lp.data())) {
                    const ue::FVector m = lp.Get<ue::FVector>("ReturnValue");
                    const Vec3 a = net::Loc(pawn);
                    meshToActor = std::sqrt((m.X - a.X) * (m.X - a.X) + (m.Y - a.Y) * (m.Y - a.Y) +
                                            (m.Z - a.Z) * (m.Z - a.Z));
                }
            }
            if (UFunction* sf = Func(mesh, "IsSimulatingPhysics")) {
                ue::Params sp(sf);
                sp.Set<ue::FName>("BoneName", ue::FName());
                if (ue::Call(mesh, sf, sp.data())) simulating = sp.Get<bool>("ReturnValue");
            }
        }
        UObject* meshParent = nullptr;
        if (ue::IsValid(mesh)) ue::Get(mesh, "AttachParent", meshParent);
        return net::Fmt("meshToActor=%.0f simulating=%d meshParent=%s deathCam=%d povToBody=%.0f hudOpacity0=%.2f "
                        "pawn=%s chars=%d hidden=%d meshVisible=%d meshHiddenInGame=%d viewTarget=%s "
                        "uiHandler=%d hud=%d hudInViewport=%d hudVisible=%d", meshToActor, simulating ? 1 : 0,
                        meshParent ? ue::GetName(meshParent).c_str() : "-", deathCam, povToBody, op0,
                        ue::IsValid(pawn) ? ue::GetName(pawn).c_str() : "-", chars,
                        ue::IsValid(pawn) && ue::GetBool(pawn, "bHidden") ? 1 : 0, meshVis ? 1 : 0,
                        ue::IsValid(mesh) && ue::GetBool(mesh, "bHiddenInGame") ? 1 : 0,
                        vt == pawn ? "self" : (vt ? ue::GetName(vt).c_str() : "-"), h ? 1 : 0,
                        ue::IsValid(hud) ? 1 : 0, inVp ? 1 : 0, hudVis ? 1 : 0);
    };
    const std::string before = facts();
    if (!fix) {
        net::Log("selfcheck (%s): %s", why, before.c_str());
        return before;
    }
    if (ue::IsValid(pawn) && ue::IsValid(mesh)) {
        // The death sequence ragdolls the mesh (physics on, detached from the capsule);
        // only the reset path rebuilds it.  Physics off, back on the capsule, at the
        // relative transform the puppet's mesh (same class, never died) has.
        UObject* capsule = nullptr;
        ue::Get(pawn, "CapsuleComponent", capsule);
        UObject* meshParent = nullptr;
        ue::Get(mesh, "AttachParent", meshParent);
        bool sim = false;
        if (UFunction* sf = Func(mesh, "IsSimulatingPhysics")) {
            ue::Params sp(sf);
            sp.Set<ue::FName>("BoneName", ue::FName());
            if (ue::Call(mesh, sf, sp.data())) sim = sp.Get<bool>("ReturnValue");
        }
        const bool broken = sim || (ue::IsValid(capsule) && meshParent != capsule);
        if (broken) {
        net::Log("COOP DEATH presentation: the mesh is ragdolled/detached (sim=%d parent=%s) -- rebuilding",
                 sim ? 1 : 0, meshParent ? ue::GetName(meshParent).c_str() : "-");
        CallBool1(mesh, "SetSimulatePhysics", "bSimulate", false);
        CallBool1(mesh, "SetAllBodiesSimulatePhysics", "bNewSimulate", false);
        ue::FVector rl{0.0, 0.0, -90.0};
        ue::FRotator rr{0.0, -90.0, 0.0};
        UObject* pupMesh = puppet::Mesh();
        if (ue::IsValid(pupMesh)) {
            ue::Get(pupMesh, "RelativeLocation", rl);
            ue::Get(pupMesh, "RelativeRotation", rr);
        }
        if (ue::IsValid(capsule)) {
            UFunction* af = Func(mesh, "K2_AttachToComponent");
            if (af) {
                ue::Params ap(af);
                ap.Set<UObject*>("Parent", capsule);
                ap.Set<ue::FName>("SocketName", ue::FName());
                ap.Set<std::uint8_t>("LocationRule", 0);
                ap.Set<std::uint8_t>("RotationRule", 0);
                ap.Set<std::uint8_t>("ScaleRule", 0);
                ap.Set<bool>("bWeldSimulatedBodies", false);
                ue::Call(mesh, af, ap.data());
            }
            if (UFunction* rf = Func(mesh, "K2_SetRelativeLocationAndRotation")) {
                ue::Params rp(rf);
                rp.Set<ue::FVector>("NewLocation", rl);
                rp.Set<ue::FRotator>("NewRotation", rr);
                rp.Set<bool>("bSweep", false);
                rp.Set<bool>("bTeleport", true);
                ue::Call(mesh, rf, rp.data());
            }
        }
        }   // broken
    }
    if (ue::IsValid(pawn)) {
        CallBool1(pawn, "SetActorHiddenInGame", "bNewHidden", false);
        if (ue::IsValid(mesh)) {
            CallBool1(mesh, "SetHiddenInGame", "NewHidden", false, "bPropagateToChildren", false);
            CallBool1(mesh, "SetVisibility", "bNewVisibility", true, "bPropagateToChildren", false);
        }
    }
    if (ue::IsValid(pc) && ue::IsValid(pawn) && CallObject(pc, "GetViewTarget") != pawn)
        SetViewTargetLocal(pc, pawn);
    // The camera: off the death state, back on our body.
    if (ue::IsValid(pawn)) RetargetCameraStack(pc, pawn);
    // The HUD: the death flow fades its layers out (the widget stays "visible" with
    // opacity 0), and only the reset path fades them back.  Every layer on, opacity reset.
    if (UObject* h = UiHandler(pc)) {
        UObject* hud = nullptr;
        ue::Get(h, "WBP_Player_HUD", hud);
        if (!ue::IsValid(hud)) {
            CallVoid(h, "CreatePlayerHUD");
            ue::Get(h, "WBP_Player_HUD", hud);
        }
        CallVoid(h, "ShowHUD");
        if (ue::IsValid(hud)) {
            for (std::uint8_t layer = 0; layer < 4; ++layer) {
                if (UFunction* f = Func(hud, "SetHUDVisibility")) {
                    ue::Params p(f);
                    p.Set<std::uint8_t>("Layer", layer);
                    p.Set<bool>("Show", true);
                    ue::Call(hud, f, p.data());
                }
                if (UFunction* f = Func(hud, "ResetHUDOpacity")) {
                    ue::Params p(f);
                    p.Set<std::uint8_t>("Layer", layer);
                    ue::Call(hud, f, p.data());
                }
            }
        }
    }
    const std::string after = facts();
    net::Log("COOP DEATH presentation restored (%s): before[%s] after[%s]", why, before.c_str(), after.c_str());
    return after;
}

// The hold's camera: the partner's body is our puppet of them.
// APlayerController::SetViewTargetWithBlend(NewViewTarget, BlendTime, BlendFunc,
// BlendExp, bLockOutgoing) -- the LOCAL setter (it drives the PlayerCameraManager
// directly).  ClientSetViewTarget is a Client RPC that this netless build drops, and
// SetViewTarget is not a UFunction here; the first DHOLD build picked the RPC
// because it resolved first, so the camera never moved.
bool SetViewTargetLocal(UObject* pc, UObject* target) {
    UFunction* fn = Func(pc, "SetViewTargetWithBlend");
    static int logs = 0;
    if (!fn) {
        if (logs++ < 3) net::Log("COOP DEATH view: no SetViewTargetWithBlend on %s", ue::GetClassName(pc).c_str());
        return false;
    }
    ue::Params p(fn);
    p.Set<UObject*>("NewViewTarget", target);
    const bool ok = ue::Call(pc, fn, p.data());
    UObject* now = CallObject(pc, "GetViewTarget");
    UObject* cam = nullptr;
    ue::Get(pc, "PlayerCameraManager", cam);
    if (logs++ < 6)
        net::Log("COOP DEATH view: SetViewTargetWithBlend(%s) ok=%d -> GetViewTarget=%s camMgr=%s",
                 ue::GetName(target).c_str(), ok ? 1 : 0, now ? ue::GetName(now).c_str() : "-",
                 cam ? ue::GetClassName(cam).c_str() : "-");
    if (ok && now == target) return true;
    // The setter ran and the target did not change (measured: BP_PlayerCameraManager_C
    // keeps the owner's pawn while the player is dead).  APlayerCameraManager.ViewTarget
    // is a reflected FTViewTarget whose FIRST member is Target: write it, and drop a
    // pending blend so it cannot swap back.  The manager's own per-frame update then
    // computes the view from the target (the puppet is a BP_PlayerCharacter_C with
    // the same camera rig as ours).
    if (cam && ue::PlausiblePtr(cam)) {
        const std::int32_t off = ue::PropOffset(cam, "ViewTarget");
        const std::int32_t poff = ue::PropOffset(cam, "PendingViewTarget");
        bool w = false;
        if (off >= 0) w = ue::WriteMem((std::uint8_t*)cam + off, &target, sizeof(target));
        if (poff >= 0) {
            UObject* none = nullptr;
            ue::WriteMem((std::uint8_t*)cam + poff, &none, sizeof(none));
        }
        now = CallObject(pc, "GetViewTarget");
        if (logs++ < 8)
            net::Log("COOP DEATH view: wrote %s.ViewTarget.Target (off 0x%X, write=%d) -> GetViewTarget=%s",
                     ue::GetClassName(cam).c_str(), (unsigned)off, w ? 1 : 0,
                     now ? ue::GetName(now).c_str() : "-");
    }
    return now == target;
}

void KeepSpectate(const char* why) {
    DeathState& D = state().death;
    UObject* puppetActor = puppet::Actor();
    if (!ue::IsValid(puppetActor) || !ue::IsValid(D.pc)) {
        static bool said = false;
        if (!said) {
            said = true;
            net::Log("COOP DEATH hold: nothing to spectate yet (puppet=%s pc=%s)", 
                     ue::IsValid(puppetActor) ? "ok" : "none", ue::IsValid(D.pc) ? "ok" : "none");
        }
        return;
    }

    // The camera-stack retarget is what is RENDERED (see CamToPuppet): every camera
    // state follows the puppet.  The view-target paths of the first builds never moved the picture
    // (the manager snaps back to our pawn every frame) and are gone; the BP camera
    // hook stays as a harmless extra in case a manager does call it.
    EnsureCamHook();
    g_wantSpectate = true;
    const bool rigged = CamToPuppet(D.pc);
    double dist = -1.0;
    const bool povNear = CamRenderedNearPuppet(D.pc, &dist);
    D.spectating = (rigged && povNear) || (net::Now() - g_camAppliedAt < 0.5) ? 1 : 0;
    D.viewTarget = D.spectating ? puppetActor : nullptr;
    static double lastLog = -1.0e9;
    if (net::Now() - lastLog > 5.0) {
        lastLog = net::Now();
        net::Log("COOP DEATH spectate (%s): rig=%d pov-to-puppet=%.0f uu -> %s", why, rigged ? 1 : 0, dist,
                 D.spectating ? "FOLLOWING" : "not following");
        net::Log("COOP DEATH %s", CameraStackLine("while spectating").c_str());
    }
}

void LogState(const char* why);
void LogDeathPenalty(const char* why);   // defined with the save-object helpers below

}  // namespace

// ===========================================================================
// 4. public API
// ===========================================================================

void Start() {
    DeathState& D = state().death;
    if (D.started) return;
    D.started = true;

    D.off = net::FileExists("DEATH_OFF.txt");
    D.rules.Read(true);         // the defaults, then the ini (written if absent)
    D.liveness = COOP_CL_ALIVE;
    D.selfAlive = 1;

    if (D.off) {
        net::Log("DEATH_OFF.txt present: vanilla death everywhere (no suppression, no stacks)");
        return;
    }

    InstallVetoHooks();

    net::Log("death: rules debuffs=%d maxStacks=%d hp=%d%% shell=%d%% delay=%ds+%ds/stack "
             "payPenalty=%d bossHold=%d",
             D.rules.debuffs, D.rules.maxStacks, D.rules.healthPct, D.rules.shieldPct,
             D.rules.delayS, D.rules.delayPerStackS, D.rules.payPenalty, D.rules.bossHold);
}

void Reset() {
    DeathState& D = state().death;
    // A world change destroys every object this module holds.  The STACKS and the
    // rules survive: they are the mod's own numbers, not the world's.
    D.pawn = nullptr;
    D.healthComp = nullptr;
    D.pc = nullptr;
    D.viewTarget = nullptr;
    g_camRetargeted = false;   // a world change destroyed the camera stack with it
    D.spectating = 0;
    D.hasDeathPos = false;
    D.deathPos = Vec3();
    D.respawnStep = 0;
    D.respawnTries = 0;
    D.bossFound = 0;
    D.lastBossAt = 0.0;
    ClearDebuffState();
    // the world went away: anything a respawn was going to rebuild with it is gone
    g_bodyFixAt = -1.0;
    g_bodyFixTries = 0;
    g_flagsAt = -1.0;
    g_bodyShot = BodySnapshot();
    g_fnCache.clear();
    g_screenScanAt = -1.0e9;
    g_screenLogged = false;

    ++D.worldChanges;
    // A level reload is what the game's own death reset does.  If a death was
    // pending, the reload RESOLVED it -- the player is back in a fresh world.
    if (D.pending) {
        net::Log("death: world changed while a death was pending (case %d) -- the reset "
                 "resolved it; stacks cleared, as the wipe rule says", D.partyCase);
        D.pending = false;
        D.partyCase = 0;
        D.selfAlive = 1;
        D.stacks = 0;
        D.liveness = COOP_CL_ALIVE;
        if (IsHost()) ++D.hostResetSerial;
        net::Log("COOP DEATH WIPE resolved by the world reset: stacks=0 host=%d",
                 IsHost() ? 1 : 0);
    }
}

namespace {

// --- the one detection path ------------------------------------------------

void OnDeathDetected(UObject* pawn) {
    DeathState& D = state().death;
    const double now = net::Now();
    D.pending = true;
    D.dieFallbackAt = 0.0;
    D.selfAlive = 0;
    D.deathAt = now;
    D.hasDeathPos = true;
    D.deathPos = net::Loc(pawn);
    D.respawnStep = 0;
    D.respawnTries = 0;
    D.missReviveAt = 0.0;
    ++D.serial;

    const int boss = BossLive();
    D.partyCase = CoopPartyCase(0, PeerAliveNow(), boss);
    // THE HARNESS OVERRIDE, stated once: "a solo death is treated as case 2".  Without
    // this the case is 1 (no partner && myself dead == a party of one that wiped),
    // which is arithmetically right and semantically wrong -- solo play is not a party
    // (coop_policy.h's banner).  Only reachable with coop\SOLO_DEATH.txt present.
    if (!D.peerSeen && GateArmed()) {
        D.partyCase = COOP_CASE_RESPAWN;
        net::Log("death: no partner this session and SOLO_DEATH.txt is armed -- the death is "
                 "case 2, not a wipe");
    }
    D.victimCase = D.partyCase;

    const int stacksHeld = D.stacks;
    const unsigned delayMs = CoopRespawnDelayMs(D.rules.delayS, D.rules.delayPerStackS, stacksHeld);
    D.respawnAt = now + (double)delayMs / 1000.0;

    D.liveness = MyLiveness();
    net::Log("COOP DEATH #%u victimCase=%d partyCase=%d peer=%s boss=%d stacksHeld=%d "
             "delay=%.1fs",
             D.serial, D.victimCase, D.partyCase,
             D.peerSeen ? (PeerAliveNow() ? "alive" : "down") : "solo", boss, stacksHeld,
             (double)delayMs / 1000.0);

    const char* plain = "nothing happens to the world: the mod puts you back on your feet";
    switch (D.partyCase) {
        case COOP_CASE_WIPE:
            plain = "PARTY WIPE: the game's own death reset is allowed (the host resets first, "
                    "a guest follows once the host is up)";
            D.stacks = 0;   // the wipe clears every stack, and adds none
            if (!IsHost()) {
                D.followerRespawn = 1;
                net::Log("COOP DEATH guest: this machine waits for the host, it does NOT reset "
                         "its own save");
            }
            break;
        case COOP_CASE_HOLD:
            plain = "BOSS HOLD: held, no respawn, the camera watches the partner; the stack is "
                    "charged only if the fight is lost";
            break;
        case COOP_CASE_RESPAWN:
            plain = "respawn in the live world after the delay, plus one death stack";
            if (CoopAccruesStack(D.rules.debuffs, D.partyCase)) AddStack("death");
            break;
        default:
            break;
    }
    net::Log("COOP DEATH %s", plain);
    LogDeathPenalty("at death");
    // Measure the camera stack at the death itself, and -- when the game's own death
    // is allowed to run on THIS machine (a wipe's host, a solo death) -- arm a check
    // 1.5 s later, after the game's death ability has had its turn, so a death camera
    // this module removed at an EARLIER death is put back if the game did not push a
    // new one.  (See the banner above g_deathCamState.)
    g_deathCamDueAt = now + 1.5;
    net::Log("COOP DEATH %s", CameraStackLine("at the death").c_str());
    // [R8-MONEY] the lost-Gloom spoil this death leaves on the ground is measured
    // (and, when a second death replaces it, swept) by its own lane.  A call, not a
    // change: nothing here writes currency or a save field.
    money::NoteDeath(pawn);
    // [R8-MONEY] end
}

void ResolveAlive(const char* reason) {
    DeathState& D = state().death;
    const bool weRespawned = (D.respawnStep != 0) || g_freshBody;
    g_freshBody = false;
    net::Log("COOP DEATH resolved: %s (case %d, stacks %d)", reason, D.partyCase, D.stacks);
    D.pending = false;
    D.selfAlive = 1;
    D.respawnStep = 0;
    D.respawnTries = 0;
    D.followerRespawn = 0;
    g_hostResetStage = 0;
    D.liveness = COOP_CL_ALIVE;
    if (weRespawned) {
        ClearDeathMix();
        RestoreInput();
        // The reset the game would have run ends the death ability; ours must too, or
        // the player cannot die a second time (see EndDeathAbilities).
        if (!DeathAbilityDisabled()) EndDeathAbilities("after the co-op respawn");
    }
    g_presentAt = net::Now() + 1.0;   // once now-ish, and again after the game settles
    g_presentPass = 0;
    // The camera must come back to the player, and the death screen must be gone.
    if (ue::IsValid(D.pc)) {
        UObject* self = net::Pawn();
        if (ue::IsValid(self) && CallObject(D.pc, "GetViewTarget") != self) {
            const bool ok = SetViewTargetLocal(D.pc, self);
            net::Log("COOP DEATH camera back to the player ok=%s", ok ? "true" : "false");
        }
    }
    CamRestore(D.pc);
    g_wantSpectate = false;
    D.spectating = 0;
    // The camera comes back to the player NOW, not one second later: the game's death
    // camera is taken off the stack here (measured 20261026-161545: without this the
    // `after the co-op respawn` stack still had CameraState_Death_C on it, because the
    // removal used to wait for the presentation pass at +1 s -- so the respawned player
    // sat on the death framing for a second).  The presentation pass still retargets and
    // verifies on its own.
    if (ue::IsValid(D.pc) && ue::IsValid(net::Pawn())) RetargetCameraStack(D.pc, net::Pawn());
    // What the co-op respawn leaves on the camera stack: this line is the measurement
    // for "the death camera was taken off and the game will need it again".
    g_deathCamDueAt = 0.0;
    net::Log("COOP DEATH %s", CameraStackLine("after the co-op respawn").c_str());
    // [R8-MONEY] what the death drop looks like now that the player is back on his
    // feet in this world (the case-2 respawn does not reload, so nothing has
    // re-spawned the bag -- it either survived or it did not).
    money::NoteRespawn();
    // [R8-MONEY] end
}

// The pended case is re-asked from the state as it is NOW (doc 41 / coop_policy's
// CoopRecase).  Without this a player held in a boss fight the partner then wins
// stays held for good, and a case-2 death whose partner dies never becomes a wipe.
void ReDecide() {
    DeathState& D = state().death;
    D.lastRecaseAt = net::Now();
    if (!D.peerSeen) return;   // solo: nothing to re-decide against

    const int peerDown = PeerAliveNow() ? 0 : 1;
    const int encounter = (BossLive() || PeerBossNow()) ? 1 : 0;
    const int now = CoopRecase(D.partyCase, peerDown, encounter);
    if (now == D.partyCase) return;

    net::Log("COOP DEATH recase %d -> %d (peerDown=%d encounterLive=%d)",
             D.partyCase, now, peerDown, encounter);
    const int was = D.partyCase;
    D.partyCase = now;
    if (CoopStackOnRecase(D.rules.debuffs, was, now)) AddStack("hold released by a win");

    if (now == COOP_CASE_WIPE) {
        D.stacks = 0;
        if (!IsHost()) D.followerRespawn = 1;
        net::Log("COOP DEATH the held/counting-down death became a WIPE: stacks cleared");
    } else if (now == COOP_CASE_RESPAWN) {
        // The countdown starts when the hold is released, not at the death.
        const unsigned ms = CoopRespawnDelayMs(D.rules.delayS, D.rules.delayPerStackS, D.stacks);
        D.respawnAt = net::Now() + (double)ms / 1000.0;
        D.respawnStep = 0;
        net::Log("COOP DEATH hold released: respawn in %.1fs", (double)ms / 1000.0);
    }
    D.liveness = MyLiveness();
}

// The respawn ladder, one step per call, each verified by the next tick.
void AdvanceRespawn(UObject* pawn) {
    DeathState& D = state().death;
    const double now = net::Now();

    const int isHost = IsHost() ? 1 : 0;
    const int want = CoopShouldRespawnRole(D.peerSeen, GateArmed(), D.partyCase, isHost, D.hostUp);
    if (!want) {
        if (D.partyCase == COOP_CASE_WIPE && !isHost) {
            if (now - D.lastRespawnTry > 5.0) {
                D.lastRespawnTry = now;
                net::Log("COOP DEATH guest: waiting for the host to be up (host=%s peer=%s)",
                         D.hostUp ? "up" : "down", D.peerSeen ? "seen" : "none");
            }
        }
        return;
    }
    if (now < D.respawnAt) return;

    if (g_freshBody && now - g_freshBodyAt < 6.0) return;   // the new body is settling
    if (D.respawnStep == 0) {
        D.respawnStep = 1;
        D.lastRespawnTry = now;
        ++D.respawnTries;
        if (!g_freshBody && StepFreshBody(pawn)) return;
        StepRevive();
        return;
    }
    const double since = now - D.lastRespawnTry;
    if (D.respawnStep == 1 && since >= 1.5) {
        D.respawnStep = 2;
        D.lastRespawnTry = now;
        ++D.respawnTries;
        StepRestart(pawn);
        return;
    }
    if (D.respawnStep == 2 && since >= 2.0) {
        D.respawnStep = 3;
        D.lastRespawnTry = now;
        ++D.respawnTries;
        StepHandRespawn(pawn);
        return;
    }
    if (D.respawnStep >= 3 && since >= 3.0) {
        D.lastRespawnTry = now;
        ++D.respawnTries;
        // Step 4, every attempt: the game's own clear.  The controller's death-mix flag
        // is what "the player is dead" MEANS here, and nothing clears it on a suppressed
        // death (run 6: revive ok, restart ok, and the flag still on) -- the game clears
        // it on its own reset path, which is exactly the path we blocked.
        ClearDeathMix();
        DismissDeathScreen("respawn complete");
        if (D.respawnTries % 3 == 0) StepHandRespawn(pawn);
    }
}

// THE MISSING REVIVE WATCHDOG: a death that should have ended and has not.  The
// one state this mod must never leave a player in is "dead in a world that will
// never reload", so it says so repeatedly rather than once.
void MissingReviveWatchdog() {
    DeathState& D = state().death;
    const double now = net::Now();
    if (!D.pending) return;
    if (now < D.respawnAt + 20.0) return;
    if (now - D.missReviveAt < 10.0) return;
    D.missReviveAt = now;
    net::Log("MISSING REVIVE: case %d was due %.1fs ago and the player is still down "
             "(tries=%d step=%d); the reset is still suppressed -- this needs a look",
             D.partyCase, now - D.respawnAt, D.respawnTries, D.respawnStep);
}

// WHAT A DEAD AND AN ALIVE PLAYER LOOK LIKE.  Written while a death is pending and
// kept afterwards as the diagnostic that answers "is this player up?" from the
// game's own numbers rather than from an enum that does not move (run 2).
void LogHealthTable(const char* why) {
    DeathState& D = state().death;
    UObject* hc = ue::IsValid(D.healthComp) ? D.healthComp : HealthCompOf(net::Pawn());
    if (!ue::IsValid(hc)) return;
    float hp = 0.0f, maxHp = 0.0f, shell = 0.0f, maxShell = 0.0f, norm = 0.0f, shellNorm = 0.0f;
    bool dead = false;
    CallFloat(hc, "GetHealth", &hp);
    CallFloat(hc, "GetMaxHealth", &maxHp);
    CallFloat(hc, "GetShellHealth", &shell);
    CallFloat(hc, "GetMaxShellHealth", &maxShell);
    CallFloat(hc, "GetCurrentHealthNormalized", &norm);
    CallFloat(hc, "GetShellHealthNormalized", &shellNorm);
    CallBool(hc, "IsDeadOrDying", &dead);
    int st = 0;
    bool readable = false;
    st = ReadDeathState(hc, &readable);
    net::Log("death(%s) health: hp=%.1f/%.1f shell=%.1f/%.1f norm=%.3f shellnorm=%.3f "
             "deadOrDying=%d deathState=%d readable=%d deathMix=%d pawn=%s",
             why, (double)hp, (double)maxHp, (double)shell, (double)maxShell, (double)norm,
             (double)shellNorm, (int)dead, st, (int)readable, (int)D.deathMix,
             ue::IsValid(D.pawn) ? ue::GetName(D.pawn).c_str() : "(no pawn)");
}

// THE DEATH PENALTY.  This game takes the player's carried Gloom and leaves it at
// the death spot: BP_SpartaPlayerSaveObject_C carries `bHasLostGloom @0x560`,
// `LostGloomAmount @0x578`, `LostGloomLocation @0x580`, `LostGloomBiome @0x598`, and
// the spoils are a BP_DeathSpoils_C actor with `GiveGloom` (SYNC-DATA-MINE 9.1 +
// topic-08-economy.props.tsv).  So `pay_death_penalty=1` needs NOTHING from the mod:
// the game's own loss stands, and the mod's job is only to record it.
//
// With `pay_death_penalty=0` the loss is supposed to be refunded on a case-2
// respawn.  What this build CAN do honestly is named below; what it cannot is said
// out loud rather than faked, because silently keeping the loss while the setting
// says "refund" is the kind of quiet lie this project has been burned by.
UObject* SaveObject() {
    DeathState& D = state().death;
    UObject* pc = ue::IsValid(D.pc) ? D.pc : net::PC();
    UObject* so = nullptr;
    if (!PropObj(pc, "PlayerSaveGameObject", &so)) return nullptr;
    return so;
}

void LogDeathPenalty(const char* why) {
    DeathState& D = state().death;
    UObject* so = SaveObject();
    if (!ue::IsValid(so)) {
        net::Log("death(%s) penalty: no player save object reached", why);
        return;
    }
    int lost = 0;
    const bool haveLost = PropInt(so, "LostGloomAmount", &lost);
    const bool hasLost = ue::GetBool(so, "bHasLostGloom");
    net::Log("death(%s) penalty: bHasLostGloom=%d LostGloomAmount=%s%d payPenalty=%d",
             why, (int)hasLost, haveLost ? "" : "(unread) ", lost, D.rules.payPenalty);
    if (!D.rules.payPenalty) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            net::Log("WARN death: pay_death_penalty=0 asks for a refund of the Gloom this death "
                     "took, and THIS BUILD DOES NOT IMPLEMENT IT: the spendable balance's owner "
                     "is not identified (the loss is recorded as LostGloomAmount and recovered by "
                     "picking up the BP_DeathSpoils_C actor).  The loss therefore stands; set "
                     "pay_death_penalty=1 to say so explicitly.");
        }
    }
}

void LogState(const char* why) {
    DeathState& D = state().death;
    net::Log("death(%s): me=%s case=%d victim=%d serial=%u stacks=%d pending=%d boss=%d/%d "
             "peer=%s/%d stacks=%d wr=%d/%d rules=%s",
             why, D.selfAlive ? "alive" : "down", D.partyCase, D.victimCase, D.serial, D.stacks,
             (int)D.pending, D.bossFake, D.bossFound,
             D.peerSeen ? (PeerAliveNow() ? "alive" : "down") : "solo", D.peerLiveness,
             D.peerStacks, D.hostResetSerial, D.peerResetSerial, D.rules.Serialize().c_str());
}

}  // namespace

void Tick15() {
    DeathState& D = state().death;
    if (D.off) return;
    MaintainHooks();

    // After a respawn: body + HUD back, twice (the game may re-hide on its next frames).
    if (g_presentAt > 0.0 && net::Now() >= g_presentAt && !D.pending) {
        Presentation(true, g_presentPass == 0 ? "after respawn" : "after respawn, settled");
        g_presentAt = (++g_presentPass < 2) ? net::Now() + 3.0 : -1.0;
        if (g_presentAt < 0.0) g_presentPass = 0;
    }

    // A freshly spawned body is not dressed by the game on its own (see RebuildBody):
    // an anim instance and the equipped weapon actors are what it is missing.  Bounded
    // -- six tries 1.5 s apart -- and then it says so instead of hammering every tick.
    // The debug flags are read around it because the respawn also puts the game's
    // wireframes on the body (see DebugFlags) and they are not always on at once.
    if (g_bodyFixAt > 0.0 && !BodyFixDisabled() && net::Now() >= g_bodyFixAt) {
        UObject* fresh = net::Pawn();
        if (g_bodyFixTries == 0)
            net::Log("COOP DEATH body debug flags (before the fix): %s", DebugFlags(fresh).c_str());
        if (RebuildBody(fresh, g_bodyFixTries == 0 ? "fresh body" : "fresh body, retry")) {
            g_bodyFixAt = -1.0;
            g_flagsAt = net::Now() + 20.0;
            Tick2();   // the attributes exist now: the stack goes on THIS body at once
        } else if (++g_bodyFixTries >= 6) {
            net::Log("COOP DEATH body fix: giving up after %d attempts, the body is still bare: %s",
                     g_bodyFixTries, BodyLine(fresh).c_str());
            g_bodyFixAt = -1.0;
        } else {
            g_bodyFixAt = net::Now() + 1.5;
        }
    }
    if (g_flagsAt > 0.0 && net::Now() >= g_flagsAt) {
        g_flagsAt = -1.0;
        net::Log("COOP DEATH body debug flags (20 s after the fix): %s",
                 DebugFlags(net::Pawn()).c_str());
    }

    // --- who am I watching -------------------------------------------------
    // A pending death must keep running even if the CONTROLLER's Pawn is null for a
    // moment (a reload in progress, a husk being swapped): the ladder and the
    // re-decision below are guarded on their own pointers, so this only decides
    // whether the death-state READING is available.
    UObject* pawn = net::Pawn();
    if (ue::IsValid(pawn) && pawn != D.pawn) {
        D.pawn = pawn;
        D.healthComp = nullptr;
        D.pc = nullptr;
        D.respawnStep = 0;
    }
    if (!ue::IsValid(D.pawn)) {
        if (!D.pending) return;
        pawn = D.pawn;
    }
    if (!ue::IsValid(D.healthComp)) D.healthComp = HealthCompOf(D.pawn);
    if (!ue::IsValid(D.pc)) D.pc = net::PC();
    if (!ue::IsValid(D.healthComp)) {
        if (!D.pending) return;
        // Nothing to read: keep the peer-facing liveness and the ladder alive.
        if (net::Now() - D.lastRecaseAt >= 0.25) ReDecide();
        AdvanceRespawn(D.pawn);
        D.liveness = MyLiveness();
        return;
    }

    const double now = net::Now();
    bool readable = false;
    const int st = ReadDeathState(D.healthComp, &readable);
    D.deathStateRaw = st;
    bool mixReadable = false;
    const bool mix = ReadDeathMix(D.pc, &mixReadable);
    D.deathMix = mix ? 1 : 0;
    // THE PLAYER'S OWN DEAD FLAG when it is readable; the health component's enum only
    // as a fallback (it does not move for a player -- run 4).
    const bool deadNow = mixReadable ? mix : (readable && st != 0);

    if (!D.pending) {
        if (D.dieFallbackAt > 0.0 && now >= D.dieFallbackAt) {
            D.dieFallbackAt = 0.0;
            net::Log("die: S_KillMe produced no death in 2 s -- trying "
                     "HealthComponent.StartDeath()");
            const bool ok = combat::StartDeath(D.pawn);
            net::Log("die: StartDeath ok=%s", ok ? "true" : "false");
            // Round 8: measure what that fallback actually did -- the death state the
            // hook would set, the health, and the ability that refuses to run again.
            LogHealthTable("after die fallback");
            int active = 0;
            DeathAbilities(&active);
            net::Log("die: after StartDeath the death ability is %d instance(s) ACTIVE", active);
        }
        if (g_restSeen) {
            g_restSeen = 0;
            if (D.stacks > 0) {
                double dist = 0.0;
                UObject* start = NearestPlayerStart(net::Loc(D.pawn), &dist);
                if (start && dist < 600.0) {
                    net::Log("COOP DEATH rest at %s (d=%.0f): stacks %d -> 0",
                             ue::GetName(start).c_str(), dist, D.stacks);
                    D.stacks = 0;
                    ClearDebuffState();
                    D.debuffVerified = 0;   // verify again on the next stack
                } else {
                    net::Log("death: a restore fired %.0f away from the nearest respawn point -- "
                             "not counted as a rest, stacks stay %d", dist, D.stacks);
                }
            }
        }
        // The death itself is a game EVENT, not this enum (run 2).  Either the hook
        // raised the flag last frame or the controller's own death-mix flag is on; the
        // decision and every engine read happen here.
        if (D.pendingSignal || mix) {
            D.pendingSignal = 0;
            OnDeathDetected(pawn);
            D.liveness = MyLiveness();
            return;
        }
        if (deadNow) OnDeathDetected(pawn);
        else {
            D.selfAlive = 1;
        }
        D.liveness = MyLiveness();
        return;
    }

    // --- a pending death ---------------------------------------------------
    if (!deadNow) {
        // The game itself put the player back (or our own respawn did).
        ResolveAlive((D.respawnStep != 0 || g_freshBody) ? "the co-op respawn put the player back"
                                        : "the player is up again");
        D.liveness = MyLiveness();
        return;
    }

    if (now - D.lastRecaseAt >= 0.25) ReDecide();   // 4 Hz, as the brief asks
    if (now - D.lastHealthLogAt >= 2.0) {
        D.lastHealthLogAt = now;
        LogHealthTable("pending");
    }

    // The wipe's reset is the HOST's (and the host has to ask for it when the death
    // screen waits for a selection); a guest never asks, it waits.
    if (D.partyCase == COOP_CASE_WIPE) HostAskForReset();

    // Is the reset being kept out of this machine's way?  The death-screen dismissal is
    // part of that answer: removing the widget stops the CreateLoadingScreen ->
    // HandleResetReady chain (run 6), so it must NEVER run on a case-1 HOST, whose reset
    // is how the wipe resolves.
    const int suppressing = CoopShouldSuppressRole(D.peerSeen, GateArmed(), 1, D.partyCase,
                                                   IsHost() ? 1 : 0);

    if (D.partyCase == COOP_CASE_HOLD && D.rules.bossHold) {
        KeepSpectate("boss hold");
        if (suppressing) DismissDeathScreen("hold");
    } else {
        if (D.partyCase == COOP_CASE_HOLD) {
            // boss_hold=0: the owner asked for a respawn even inside a fight.
            net::Log("COOP DEATH boss_hold=0: the held death respawns instead");
            D.partyCase = COOP_CASE_RESPAWN;
            const unsigned ms = CoopRespawnDelayMs(D.rules.delayS, D.rules.delayPerStackS,
                                                   D.stacks);
            D.respawnAt = now + (double)ms / 1000.0;
        }
        AdvanceRespawn(pawn);
        if (suppressing) DismissDeathScreen("suppressed death");
    }

    // THE GAME'S OWN DEATH MUST STILL HAVE ITS DEATH CAMERA.  This module takes
    // CameraState_Death_C off the stack after a co-op respawn; a death it does NOT
    // suppress (a host's party wipe, an unarmed solo death) is the game's death and
    // needs that state back if the game did not push a new one of its own.  One check
    // per death, 1.5 s in (the game's death ability runs at ~0.13 s), and never while
    // this machine is spectating -- the hold wants the death camera GONE.
    if (g_deathCamDueAt > 0.0 && now >= g_deathCamDueAt) {
        if (!suppressing && !g_wantSpectate) {
            g_deathCamDueAt = 0.0;
            EnsureDeathCamera("the game's own death");
        } else if (suppressing) {
            g_deathCamDueAt = 0.0;   // our own respawn/hold: the camera stays off
            net::Log("COOP DEATH %s", CameraStackLine("suppressed death").c_str());
        }
    }

    MissingReviveWatchdog();

    // A plain-English state line every 5 s while a death is in flight: the log is
    // what the findings file cites, and a pending death is the state worth seeing.
    if (now - D.lastLoggedAt > 5.0) {
        D.lastLoggedAt = now;
        net::Log("COOP DEATH pending case=%d: %s (due in %.1fs, step %d, peer=%s, boss=%d)",
                 D.partyCase,
                 D.partyCase == COOP_CASE_RESPAWN
                     ? (D.respawnAt > now ? "counting down to the co-op respawn"
                                          : "respawn due, running the ladder")
                     : (D.partyCase == COOP_CASE_HOLD ? "held for the partner's fight"
                                                      : "party wipe: waiting on the reset/ host"),
                 D.respawnAt - now, D.respawnStep,
                 D.peerSeen ? (PeerAliveNow() ? "alive" : "down") : "solo", BossLive());
    }

    D.liveness = MyLiveness();
}

void Tick2() {
    DeathState& D = state().death;
    if (D.off) return;
    if (!D.rules.debuffs) return;
    // Zero stacks must still be PROCESSED when a debuff was applied earlier: the
    // factor is 1.0 then, and CoopDebuffStep writes the natural value back.  (Run 8
    // caught the old early-return leaving a wiped player at 90 % max health for good.)
    if (D.stacks <= 0 && !D.maxHealth.have && !D.maxShell.have) return;

    UObject* hc = ue::IsValid(D.healthComp) ? D.healthComp : HealthCompOf(net::Pawn());
    if (!ue::IsValid(hc)) return;
    UObject* set = nullptr;
    if (!PropObj(hc, "HealthSet", &set)) return;

    const double now = net::Now();
    D.lastDebuffAt = now;

    const float fh = CoopDebuffFactor(D.stacks, D.rules.maxStacks, D.rules.healthPct);
    const float fs = CoopDebuffFactor(D.stacks, D.rules.maxStacks, D.rules.shieldPct);

    float cur = 0.0f, want = 0.0f;
    if (ReadAttr(set, "MaxHealth", &cur) && CoopDebuffStep(&D.maxHealth, cur, fh, &want)) {
        const bool ok = WriteAttr(set, "MaxHealth", want);
        net::Log("COOP DEATH debuff: MaxHealth %.1f -> %.1f (stacks %d, factor %.2f) ok=%s",
                 (double)cur, (double)want, D.stacks, (double)fh, ok ? "true" : "false");
    }
    float curS = 0.0f;
    if (ReadAttr(set, "MaxShellHealth", &curS) && CoopDebuffStep(&D.maxShell, curS, fs, &want)) {
        const bool ok = WriteAttr(set, "MaxShellHealth", want);
        net::Log("COOP DEATH debuff: MaxShellHealth %.1f -> %.1f (stacks %d, factor %.2f) ok=%s",
                 (double)curS, (double)want, D.stacks, (double)fs, ok ? "true" : "false");
    }

    // One verification of the readback, so a write that lands in the wrong slot is
    // visible instead of assumed: GetMaxHealth is the game's own reading.
    if (!D.debuffVerified && D.maxHealth.have && D.maxHealth.written > 0.0f) {
        float got = 0.0f;
        if (CallFloat(hc, "GetMaxHealth", &got)) {
            D.debuffVerified = 1;
            const double err = std::fabs((double)got - (double)D.maxHealth.written);
            net::Log("COOP DEATH debuff verify: GetMaxHealth=%.2f wrote %.2f (delta %.2f)%s",
                     (double)got, (double)D.maxHealth.written, err,
                     err > std::max(1.0, (double)D.maxHealth.written * 0.02)
                         ? "  <-- MISMATCH, the write did not land where the game reads"
                         : "");
        }
    }
}

void Publish(std::string* line) {
    if (!line) return;
    DeathState& D = state().death;
    if (D.off) return;

    const unsigned word = CoopStatusPack(MyLiveness(), D.partyCase, D.serial);
    if (word != D.publishedWord) {
        D.publishedWord = word;
        net::Log("death: publishing lv=%u (liveness %d, case %d, serial %u)", word,
                 MyLiveness(), D.partyCase, D.serial);
    }
    net::AppendFmt(line, ",\"lv\":%u,\"bs\":%d,\"dstk\":%d", word,
                   (D.bossFake || D.bossFound) ? 1 : 0, D.stacks);
    if (IsHost()) {
        // The host's world-reset serial and its RULES: a guest follows the first
        // and applies the second (the debuff belongs to the host's settings).
        net::AppendFmt(line, ",\"wr\":%d,\"rul\":%s", D.hostResetSerial,
                       D.rules.Serialize().c_str());
    }
}

void OnPeerLine(const std::string& line) {
    DeathState& D = state().death;
    if (D.off) return;

    int64_t v = 0;
    if (net::KvInt(line, "lv", &v)) {
        const int wasLiveness = D.peerLiveness;
        const int wasCase = D.peerCase;
        D.peerLiveness = (int)(v & 0x7);
        D.peerCase = (int)((v >> 4) & 0x3);
        D.peerSerial = (unsigned)((v >> 8) & 0xFF);
        D.peerAt = net::Now();
        if (!D.peerSeen) {
            D.peerSeen = 1;
            net::Log("death: the peer's liveness is now readable (lv=%lld -> %d, case %d)",
                     (long long)v, D.peerLiveness, D.peerCase);
        } else if (D.peerLiveness != wasLiveness || D.peerCase != wasCase) {
            net::Log("death: peer lv %d(case %d) -> %d(case %d) serial %u", wasLiveness, wasCase,
                     D.peerLiveness, D.peerCase, D.peerSerial);
        }
    }
    if (net::KvInt(line, "bs", &v)) D.peerBoss = (int)(v ? 1 : 0);
    if (net::KvInt(line, "dstk", &v)) D.peerStacks = (int)v;
    if (net::KvInt(line, "wr", &v)) D.peerResetSerial = (int)v;

    // The host's rules.  A guest must NOT use its own file's values (the debuff is
    // the host's setting), and the host ignores this key entirely.
    std::string body;
    if (!IsHost() && net::Section(line, "rul", &body)) {
        DeathRules r;
        if (r.Parse(body)) {
            const bool changed = (r.maxStacks != D.rules.maxStacks ||
                                  r.healthPct != D.rules.healthPct ||
                                  r.shieldPct != D.rules.shieldPct ||
                                  r.delayS != D.rules.delayS ||
                                  r.delayPerStackS != D.rules.delayPerStackS ||
                                  r.payPenalty != D.rules.payPenalty ||
                                  r.bossHold != D.rules.bossHold ||
                                  r.debuffs != D.rules.debuffs);
            if (changed || !D.rulesFromHost) {
                r.debuffs = D.rules.debuffs;   // the guest's own kill switch still wins
                D.rules = r;
                D.rulesFromHost = 1;
                net::Log("death: rules from the host %s", r.Serialize().c_str());
            }
        }
    }

    // The host is "up" when its liveness word says ALIVE (and is fresh).
    D.hostUp = (PeerLivenessNow() == COOP_CL_ALIVE) ? 1 : 0;
}

std::string Status() {
    const DeathState& D = state().death;
    if (!D.started) return std::string();

    // The numbers a verdict reads that are not this module's own bookkeeping: the
    // player's max health / max shell health (the debuff's effect, from the GAME's
    // getter) and the raw death-state byte.  One call each, at 2 Hz.
    float maxHp = 0.0f;
    float maxShell = 0.0f;
    UObject* hc = ue::IsValid(D.healthComp) ? D.healthComp : HealthCompOf(net::Pawn());
    if (ue::IsValid(hc)) {
        CallFloat(hc, "GetMaxHealth", &maxHp);
        CallFloat(hc, "GetMaxShellHealth", &maxShell);
    }
    UObject* puppetActor = puppet::Actor();
    // LIVE view target (the game's own getter), not the value we last stored: a camera
    // manager that reverts our write must show up as viewpuppet=0.
    UObject* liveView = ue::IsValid(D.pc) ? CallObject(D.pc, "GetViewTarget") : nullptr;
    // RENDERED camera only: the camera hook applied a frame recently.  GetViewTarget
    // answering "puppet" after the ViewTarget write did NOT move the picture.
    (void)liveView;
    // ...or the manager's last rendered POV is next to the puppet (the camera rig).
    const int viewIsPuppet = (ue::IsValid(puppetActor) &&
                              (net::Now() - g_camAppliedAt < 0.5 ||
                               (g_camRetargeted && CamRenderedNearPuppet(D.pc, nullptr)))) ? 1 : 0;

    return net::Fmt("death=me:%d(%s) case:%d victim:%d serial:%u stacks:%d peer:%d(%s) boss:%d "
                    "pending:%d suppressed:%d worldresets:%d wr:%d peerwr:%d host:%d rules:%s\n"
                    "deathstate=%d deathmix=%d maxhp=%.1f maxshell=%.1f viewpuppet=%d "
                    "spectating:%d missrevive:%d hooks:%d\n",
                    MyLiveness(), D.selfAlive ? "alive" : "down", D.partyCase, D.victimCase,
                    D.serial, D.stacks, PeerLivenessNow(),
                    D.peerSeen ? "seen" : "none", (D.bossFake || D.bossFound) ? 1 : 0,
                    (int)D.pending, D.suppressed, D.worldChanges, D.hostResetSerial,
                    D.peerResetSerial, IsHost() ? 1 : 0, D.rules.Serialize().c_str(),
                    D.deathStateRaw, D.deathMix, (double)maxHp, (double)maxShell, viewIsPuppet,
                    D.spectating,
                    (D.missReviveAt != 0.0) ? 1 : 0, D.hookResolved);
}

bool OnCommand(const std::string& cmd, const std::string& rest) {
    DeathState& D = state().death;

    if (cmd == "die") {
        // The game's own kill path: BP_PlayerController_C.S_KillMe().  The fallback
        // is the health component's own StartDeath, which is what an enemy death
        // uses and what this module already calls for mirrored enemies.
        UObject* pc = net::PC();
        UObject* pawn = net::Pawn();
        bool ok = false;
        if (ue::IsValid(pc)) {
            ok = CallVoid(pc, "S_KillMe");
            net::Log("die: %s.S_KillMe() ok=%s", ue::GetName(pc).c_str(), ok ? "true" : "false");
        }
        if (!ok) {
            ok = combat::StartDeath(pawn);
            net::Log("die: fallback HealthComponent.StartDeath() ok=%s", ok ? "true" : "false");
        }
        if (!ok) net::Log("die: FAILED -- this build exposes neither path");
        // S_KillMe answers true and then sometimes does NOTHING (measured twice: the
        // first death of a session works, the second does not).  So arm a fallback: if
        // no death event arrives within 2 s, drive the health component's own
        // StartDeath -- the same call an enemy death uses.
        {
            // Round 8: say what the game's death ability looks like at the moment the
            // kill is asked for.  An instanced ability that is still ACTIVE is what
            // makes S_KillMe a no-op, and this line is the measurement for it.
            int active = 0;
            DeathAbilities(&active);
            net::Log("die: the game's death ability: %d instance(s) ACTIVE (0 = a death can "
                     "run, >0 = the ability never ended)", active);
        }
        D.dieFallbackAt = net::Now() + 2.0;
        LogState("after die");
        return true;
    }

    if (cmd == "bossfake") {
        const int v = (int)::strtol(rest.c_str(), nullptr, 10);
        D.bossFake = v ? 1 : 0;
        D.lastBossAt = -1.0e9;   // rescan on the next tick
        net::Log("bossfake: boss flag forced to %d (test switch, not a game state)", D.bossFake);
        LogState("bossfake");
        return true;
    }

    if (cmd == "dprobe") {
        // The research verb: the death-state + health table, now, on demand.
        LogHealthTable("dprobe");
        LogState("dprobe");
        net::Log("dprobe: hooks=%d/4 pending=%d case=%d suppressed=%d worldresets=%d",
                 D.hookResolved, (int)D.pending, D.partyCase, D.suppressed, D.worldChanges);
        return true;
    }
    if (cmd == "dabl") {
        // dabl [end] -- the game's player death ability: how many instances exist, how
        // many are still ACTIVE (a second activation is refused while one is), and with
        // `end` the same call the co-op respawn makes.
        if (rest.find("end") != std::string::npos) {
            EndDeathAbilities("cmd");
        } else {
            int active = 0;
            const std::vector<UObject*> ab = DeathAbilities(&active);
            std::string names;
            for (size_t i = 0; i < ab.size(); ++i) {
                int a = 0;
                PropInt(ab[i], "bIsActive", &a);
                net::AppendFmt(&names, "%s%s(active=%d)", i ? " " : "", ue::GetName(ab[i]).c_str(), a);
            }
            net::Log("dabl: %d instance(s), %d active [%s]", (int)ab.size(), active, names.c_str());
        }
        return true;
    }

    if (cmd == "selfcheck") {
        Presentation(false, "cmd");
        return true;
    }
    if (cmd == "camstack") {
        // camstack [why] -- the research verb of round 8: the whole camera stack (the
        // manager's class, and each state's class/priority/target) in the native log,
        // plus the same line in result_camstack.txt so a script can read it.
        const std::string line = CameraStackLine(rest.empty() ? "cmd" : rest.c_str());
        net::Log("COOP DEATH %s", line.c_str());
        net::WriteFile("result_camstack.txt", line + "\n");
        return true;
    }
    if (cmd == "camensure") {
        // camensure [why] -- force the death-camera check (the restore path a wipe
        // takes), so the mechanism can be exercised without dying.
        const int ok = EnsureDeathCamera(rest.empty() ? "cmd" : rest.c_str());
        const std::string line = net::Fmt("ensure=%d %s", ok, CameraStackLine("cmd").c_str());
        net::Log("COOP DEATH %s", line.c_str());
        net::WriteFile("result_camstack.txt", line + "\n");
        return true;
    }
    if (cmd == "bodyprobe") {
        BodyProbe("cmd");
        return true;
    }
    if (cmd == "debugscan") {
        DebugScan();
        return true;
    }
    if (cmd == "dbghide") {
        DebugHide();
        return true;
    }
    if (cmd == "bodyinit") {
        BodyInit(rest.empty() ? 0 : (int)::strtol(rest.c_str(), nullptr, 10));
        return true;
    }
    if (cmd == "selffix") {
        Presentation(true, "cmd");
        return true;
    }
    if (cmd == "dstate") {
        LogState("dstate");
        // Also say what the engine looks like for the two things the policy needs.
        UObject* hc = ue::IsValid(D.healthComp) ? D.healthComp : HealthCompOf(net::Pawn());
        int st = 0;
        bool readable = false;
        if (hc) st = ReadDeathState(hc, &readable);
        UObject* gm = ue::FindFirstOf("BP_SpartaGameMode_C");
        double dist = 0.0;
        UObject* start = NearestPlayerStart(D.deathPos, &dist);
        net::Log("dstate: pawn=%s hc=%s deathState=%d readable=%d pc=%s gamemode=%s "
                 "start=%s d=%.0f viewtarget=%s",
                 net::FullName(D.pawn).c_str(), net::FullName(hc).c_str(), st, (int)readable,
                 net::FullName(D.pc).c_str(), net::FullName(gm).c_str(),
                 ue::IsValid(start) ? ue::GetName(start).c_str() : "(none)", dist,
                 net::FullName(D.viewTarget).c_str());
        return true;
    }

    if (cmd == "deathprobe") {
        // deathprobe 60 [patterns]  -- the research instrument (R5-DEATH-FINDINGS).
        char* end = nullptr;
        const double secs = ::strtod(rest.c_str(), &end);
        std::string pats = "death|reset|respawn|revive|load|travel|checkpoint|restart|"
                           "worldreset|deathscreen";
        if (end && *end && (*end == ' ' || *end == '|' || *end == ',')) {
            pats = end + 1;
        }
        const bool ok = ue::ProbeOn((float)(secs > 0.0 ? secs : 60.0), pats.c_str());
        net::Log("deathprobe: %.0fs on '%s' ok=%s", secs > 0.0 ? secs : 60.0, pats.c_str(),
                 ok ? "true" : "false");
        return true;
    }

    if (cmd == "dstk") {
        // dstk [n] -- read, or SET, the stack count (a test switch: the debuff is
        // applied on the next 2 Hz tick, and 0 clears it).
        if (rest.empty()) {
            net::Log("dstk: stacks=%d factor=%.3f", D.stacks,
                     (double)CoopDebuffFactor(D.stacks, D.rules.maxStacks, D.rules.healthPct));
        } else {
            const int v = std::max(0, (int)::strtol(rest.c_str(), nullptr, 10));
            D.stacks = std::min(v, D.rules.maxStacks);
            if (D.stacks == 0) ClearDebuffState();
            net::Log("dstk: stacks forced to %d", D.stacks);
        }
        LogState("dstk");
        return true;
    }

    return false;
}

}  // namespace death
}  // namespace coop

// ===========================================================================
// The rules file.  `DeathRules` is a member of coop::state(), so its members are
// defined in namespace coop rather than in coop::death.
//
// coop\MS2Coop.ini, section [Rules].  Missing file -> the defaults, and a
// commented default file is written so the owner can see what exists.  A missing
// KEY is the default too: the file only has to list what the owner changed.
// ===========================================================================

namespace coop {

namespace {
std::string LowerLocal(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return s;
}
}  // namespace

void DeathRules::Read(bool fromIni) {
    *this = DeathRules();   // the brief's defaults
    if (!fromIni) return;

    std::string text;
    if (!net::ReadText("MS2Coop.ini", &text)) {
        static const char* kDefault =
            "; MS2Coop.ini -- the co-op rules (every key is optional; the values the\n"
            "; comments describe ARE the defaults, so a missing file changes nothing).\n"
            ";\n"
            "; The HOST's values are the ones a session uses: the host publishes them\n"
            "; (wire key \"rul\") and a guest applies what arrives, so editing this file on\n"
            "; the guest changes nothing.\n"
            "\n"
            "[Rules]\n"
            "; Death stacks: each co-op respawn adds one stack, and every stack costs\n"
            "; debuff_health_pct of MAX HEALTH and debuff_shield_pct of MAX SHELL HEALTH.\n"
            "; The total is capped at 90 % whatever these say. 0 disables the debuff.\n"
            "death_debuffs=1\n"
            "debuff_max_stacks=5\n"
            "debuff_health_pct=10\n"
            "debuff_shield_pct=10\n"
            ";\n"
            "; How long after a death the co-op respawn comes, and how much longer for\n"
            "; each stack the player already held when they died.  Capped at 600 s.\n"
            "respawn_delay_s=10\n"
            "respawn_delay_per_stack_s=5\n"
            ";\n"
            "; 1 = keep whatever currency penalty the game applies on a death.\n"
            "; 0 = refund it when the co-op respawn puts the player back.\n"
            "pay_death_penalty=1\n"
            ";\n"
            "; 1 = a death inside a live boss fight is HELD (no respawn) until the fight\n"
            "; ends. 0 = respawn anyway (NOT recommended: it re-enters the arena, which is\n"
            "; the exploit the hold exists to prevent).\n"
            "boss_hold=1\n";
        net::WriteFile("MS2Coop.ini", kDefault);
        net::Log("death: wrote the default coop\\MS2Coop.ini ([Rules])");
        return;
    }

    std::string section;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        std::string line =
            text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size()) continue;
        if (line[i] == ';' || line[i] == '#') continue;
        if (line[i] == '[') {
            const size_t close = line.find(']', i);
            section = LowerLocal(line.substr(
                i + 1, close == std::string::npos ? std::string::npos : close - i - 1));
            continue;
        }
        if (section != "rules") continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = LowerLocal(line.substr(i, eq - i));
        const int v = (int)::strtol(line.c_str() + eq + 1, nullptr, 10);
        if (key == "death_debuffs") debuffs = v ? 1 : 0;
        else if (key == "debuff_max_stacks") maxStacks = std::max(0, v);
        else if (key == "debuff_health_pct") healthPct = std::max(0, v);
        else if (key == "debuff_shield_pct") shieldPct = std::max(0, v);
        else if (key == "respawn_delay_s") delayS = std::max(0, v);
        else if (key == "respawn_delay_per_stack_s") delayPerStackS = std::max(0, v);
        else if (key == "pay_death_penalty") payPenalty = v ? 1 : 0;
        else if (key == "boss_hold") bossHold = v ? 1 : 0;
    }
}

std::string DeathRules::Serialize() const {
    return net::Fmt("[%d,%d,%d,%d,%d,%d,%d]", maxStacks, healthPct, shieldPct, delayS,
                    delayPerStackS, payPenalty, bossHold);
}

bool DeathRules::Parse(const std::string& body) {
    int v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int n = 0;
    const char* p = body.c_str();
    while (*p && n < 8) {
        while (*p && (*p == '[' || *p == ',' || *p == ' ' || *p == '\t')) ++p;
        if (!*p || *p == ']') break;
        char* end = nullptr;
        const long x = ::strtol(p, &end, 10);
        if (end == p) break;
        v[n++] = (int)x;
        p = end;
    }
    if (n < 6) return false;
    maxStacks = std::max(0, v[0]);
    healthPct = std::max(0, v[1]);
    shieldPct = std::max(0, v[2]);
    delayS = std::max(0, v[3]);
    delayPerStackS = std::max(0, v[4]);
    payPenalty = v[5] ? 1 : 0;
    if (n >= 7) bossHold = v[6] ? 1 : 0;
    return true;
}

}  // namespace coop

