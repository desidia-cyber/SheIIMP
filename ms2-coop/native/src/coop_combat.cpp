// coop_combat.cpp -- native port of the combat half of CoopMod (main.lua).
//
// Ported ranges (READ-ONLY reference: mod\CoopMod\Scripts\main.lua, 4133 lines):
//   1192-1408  hcCache / fillHealthCache / healthCompOf / healthNormOf /
//              setHealthNorm / startDeath / MAX_NPC / NPC_RADIUS / NPC_MARGIN /
//              PEER_CLAIMS / deadTails / hpSeen / collectNPCs / hpString /
//              OWNED_TAILS / NPC_LAST / montagePath / npcJSON / nhpJSON /
//              deadJSON / sampleCombat / killTail
//   1766-1935  MIR / MIR_SKIP / MIR_STATS / mirrorTake / mirrorRelease /
//              mirrorDrive / applyNPCs / applyHealth / applyDead
//   3808-3956  MIRROR_ENABLED / SPN / AIT / PD / FT / spawnerScan / spawnReplay /
//              aiTargetPeer / puppetDamage / applyPeerDamage / frameTime /
//              enemyTick (+ streamStatus' enemy/perf lines, 3957-3966)
//
// Everything here is called from the game thread only (the whole mod runs inside
// ue::OnTick); there is no locking and there must not be any UObject touched from
// another thread.
//
// ---------------------------------------------------------------------------
// CONTRACT-REQUEST (things coop_state.h still cannot express)
// ---------------------------------------------------------------------------
// 1. `CombatState::npcFirst` is `std::vector<NamedActor>` ("tail -> first
//    position seen") but NamedActor has no position field, so the Lua's
//    npcFirst[name] = {x,y,z} (main.lua:1770, 1868-1871) cannot be stored there.
//    The positions live in this file (`g_firstPos`); `npcFirst` is populated with
//    the tails (obj = nullptr) so the names are still visible to other modules.
//    Please make it `std::vector<NpcFirst>` = { std::string tail; Vec3 pos; }.
//    (spawnAlias was already fixed to StrStrMap -- thank you.)
// 3. `ue::UClass` is forward-declared as an unrelated type in ms2_ue.h, so there
//    is no way to ask for a class's full name: ClassFullName() below
//    reinterpret_casts the UClass* to UObject* (a UClass IS a UObject in the
//    engine) for the GetFullName call only. Either derive the declaration
//    (`class UClass : public UObject;`) or add `std::string GetFullName(UClass*)`
//    and this cast goes away.
// 4. `ue::MakeFName` from the common brief is not in ms2_ue.h; PlayAnimMontage's
//    StartSectionName is passed a zero FName, which is exactly the "None" FName
//    the Lua passes (main.lua:1831).
// 2. `CombatState::mirrors` is a bare vector of `MirrorEntry`, which has no name
//    field, while the Lua keys MIR by the peer's enemy name (main.lua:1775-1781).
//    The keys therefore live in this file (`g_mirrorKeys`, parallel to the
//    vector) -- a destroyed actor must still be releasable. A `std::string key`
//    on MirrorEntry would remove that shadow index.
// ---------------------------------------------------------------------------

#include "ms2_ue.h"
#include "coop_state.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace coop {
namespace combat {

namespace {

using ue::UObject;
using ue::UFunction;

// ---------------------------------------------------------------------------
// literals from the Lua that are not in coop_state.h
// ---------------------------------------------------------------------------
constexpr double kInf          = std::numeric_limits<double>::infinity();
constexpr double kDeadHp       = 0.01;   // main.lua:1392, 1849, 1908
constexpr double kNoDamageHp   = 0.05;   // main.lua:3901 (puppetDamage floor)
constexpr double kNearOwnMoved = 200.0;  // main.lua:1871 "the host really moved it"
constexpr double kNearGood     = 30.0;   // main.lua:1878 npc verdict tolerance
// kNpcRadius + 300, spelled out at main.lua:1298 and 1301
constexpr double kClaimSlack   = kNpcFarMargin;
constexpr int    kNpcVerdictN  = 20;     // main.lua:1879

// Proximity spawner: a pawn standing inside a spawner's own trigger sphere for this
// long is treated as "walked in" (the harness teleports with sweep=false, so the
// game never raises the overlap itself).  kSelfTriggerRadius is only the fallback
// when the sphere's SphereRadius cannot be read.
constexpr double kSelfTriggerHold   = 1.5;    // s inside the sphere before firing
constexpr double kSelfTriggerRadius = 300.0;  // fallback sphere radius, uu

// ---------------------------------------------------------------------------
// the clock every interval in this file uses (see patch-combat-coop_net.diff)
// ---------------------------------------------------------------------------
// net::Now() is GameplayStatics.GetRealTimeSeconds, and in the slotfix4 run it is
// NOT a usable timer: diagnostics throttled to one line per 5 s fired ~11 times a
// second on both clients.  That can only happen if the value DEGREASES between
// calls -- probe.log A 03:28:53-58 has all 30 "collect:" lines of its budget in
// 5 s, B 03:29:10-13 all 30 "npcrcv:" lines in 3 s -- and the two
// "spawnscan: clock rewound" lines print stamps no clock can produce
// (A -0.6 -> -160.0, B 243388915243820045087367015432192.0 -> 0.0).
//
// Every gate in this module means "how much real time has passed", so it reads a
// monotonic clock of its own; net::Now() is only logged as evidence of the bug.
// Root cause: UGameplayStatics' ReturnValue is a DOUBLE in this UE5 build (the
// coop_net reader takes the low 4 bytes of it, i.e. the garbage float half).
double Mono() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return duration_cast<duration<double>>(steady_clock::now() - t0).count();
}

// Read "ReturnValue" without betting on its width.  Some of UGameplayStatics'
// accessors return a DOUBLE in this UE5 build and some return a FLOAT, and reading
// the wrong width does not fail -- it yields a denormal that still passes `> 0`.
// The tell is exact: a 4-byte float read through an 8-byte view can only produce a
// DENORMAL double (|v| < 2.3e-308), while every number this module reads is either
// exactly 0 or a real time/count, far above 1e-300.
bool ReturnNumber(ue::Params& p, double* out) {
    if (out) *out = 0.0;
    if (p.size() >= 16) {
        const double d = p.Get<double>("ReturnValue");
        if (std::isfinite(d) && (d == 0.0 || std::fabs(d) > 1.0e-300)) {
            if (out) *out = d;   // the property really is 8 bytes wide
            return true;
        }
    }
    const float f = p.Get<float>("ReturnValue");
    if (!std::isfinite(static_cast<double>(f))) return false;
    if (out) *out = static_cast<double>(f);
    return true;
}

// ---------------------------------------------------------------------------
// spawn / mirror diagnostics
//
// The two-client harness gates SPAWN on result_spawn.txt (SpawnerScan) and
// MIRROR on `enemy=... taken:N` (MirrorTake).  Both stayed at 0 on the first
// native run while the Lua passed, and the ring has five gates none of which
// said anything in either log:
//
//   1. send    collectNPCs  -- a local enemy in range, and owned by this client
//   2. alias   SpawnerScan  -- spawnAlias[enemy tail] = "SP_<spawner tail>"
//   3. wire    npcJSON      -- an "SP_..." name in this client's own npc array
//   4. recv    ApplyNpcs    -- PEER_CLAIMS holds that "SP_" key
//   5. replay  SpawnReplay  -- a local UnitSpawner with that tail + its trigger
//
// Each gate below prints one line, throttled: a net::Log line flushes the whole
// file, and this build's 2 Hz step can be seconds late (sync.txt perf=near), so
// nothing here may log per frame.
// ---------------------------------------------------------------------------
class Diag {
public:
    // true at most maxN times, and never twice within `every` seconds
    bool Due(double now, double every = 5.0, int maxN = 30) {
        if (count_ >= maxN) return false;
        // net::Now() is the world's real-time clock and it restarts on a level
        // load. A rewind must not silence the gate for the rest of the session
        // (that is the "flushed the whole file" trap in the other direction).
        if (have_ && now < last_) have_ = false;
        if (have_ && now - last_ < every) return false;
        have_ = true;
        last_ = now;
        ++count_;
        return true;
    }
    int count() const { return count_; }

private:
    double last_ = 0.0;
    bool   have_ = false;
    int    count_ = 0;
};

Diag g_diagCollect;   // collectNPCs: characters / candidates / owned
Diag g_diagSend;      // npcJSON: published entries, how many were "SP_..."
Diag g_diagRecv;      // applyNPCs: entries / claims / names that resolved
Diag g_diagScan;      // spawnerScan: spawner list, SpawnedEnemy reads, aliases
Diag g_diagReplay;    // spawnReplay: claimed keys, local spawner, trigger result
Diag g_diagTick;      // enemyTick: mirrors / claims / spawners

// Per-name "already said this" set: a per-enemy reason must print once, not once
// per 15 Hz packet (the Lua's mirroredOnce / MIR_SKIP idea, same bound).
StrSet g_said;
int g_aliasLogs = 0;

bool SayOnce(const char* tag, const std::string& name) {
    if (g_said.Size() > 1024) return false;
    const std::string key = std::string(tag) + "|" + name;
    if (g_said.Has(key)) return false;
    g_said.Add(key);
    return true;
}

// ---------------------------------------------------------------------------
// per-translation-unit state (the header cannot hold it, see CONTRACT-REQUEST)
// ---------------------------------------------------------------------------

// SPN_ALIAS: enemy tail -> "SP_<spawner tail>" (main.lua:636). The map itself is
// state().combat.spawnAlias; see AliasOf().

// npcFirst: tail -> first position the peer published for it (main.lua:1770, 1868)
struct FirstPos {
    std::string tail;
    Vec3 pos;
};
std::vector<FirstPos> g_firstPos;

// MIR keys, parallel to state().combat.mirrors (MirrorEntry has no name field).
std::vector<std::string> g_mirrorKeys;

// SPN.list / SPN.at / SPN.listAt (main.lua:3817, 3822-3828). `by` (the tail ->
// spawner map) is mirrored into state().combat.spawners.
std::vector<UObject*> g_spawnList;
bool g_spawnListValid = false;
double g_spawnScanAt = 0.0;

// Cached COO of /Script/Engine.Default__GameplayStatics (main.lua:3927 re-finds
// it every call; a hash lookup, so caching it changes nothing observable).
UObject* g_gameStats = nullptr;

// FGameplayAttributeData slot resolution (see SetHealthNorm).
int  g_healthSlots[2] = {0, 1};
bool g_healthSlotsKnown = false;
bool g_healthVerifyDone = false;

// net::Now() of the last FillHealthCache(). net calls it from its own 2 Hz step
// (the Lua's healthCache step, main.lua:4120) as well as through Tick2(); the
// guard keeps FindAllOf("SpartaHealthComponent") at one GObjects walk per tick.
double g_hcCacheAt = -1.0e9;

// ---------------------------------------------------------------------------
// tiny utilities
// ---------------------------------------------------------------------------
const std::size_t kNpos = std::string::npos;

template <class F>
void Guarded(const char* label, F&& fn) {
    try {
        fn();
    } catch (const std::exception& e) {
        net::Log("ERR %s: %s", label, e.what());
    } catch (...) {
        net::Log("ERR %s: unknown exception", label);
    }
}

bool Live(UObject* o) { return o != nullptr && ue::IsValid(o); }

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool Contains(const std::string& s, const char* needle) {
    return s.find(needle) != kNpos;
}

bool ParseNum(const std::string& s, double* out) {
    if (s.empty() || !out) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || end == nullptr || *end != '\0') return false;
    if (!std::isfinite(v)) return false;   // tonumber("inf") is nil in Lua
    *out = v;
    return true;
}

std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

// Lua's `f[n]`: the n-th (1-based) comma separated field, "" when absent.
std::string Field(const std::vector<std::string>& f, std::size_t n) {
    return n < f.size() ? f[n] : std::string();
}

// Lua: `f[10]:match('^"(.*)"$')`
std::string QuotedField(const std::vector<std::string>& f, std::size_t n) {
    if (n >= f.size()) return std::string();
    const std::string& s = f[n];
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') return std::string();
    return s.substr(1, s.size() - 2);
}

// Split one array entry body ("...",1,2) on commas, keeping quoted strings
// intact. Empty fields are dropped, exactly like the Lua's gmatch('[^,]+').
void SplitFields(const std::string& body, std::vector<std::string>* out) {
    std::string cur;
    bool quoted = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '"') {
            quoted = !quoted;
            cur.push_back(c);
            continue;
        }
        if (c == ',' && !quoted) {
            if (!cur.empty()) out->push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out->push_back(cur);
}

// If `s` is one bracketed group ("[...]"), strip the brackets.
void StripOuterBrackets(std::string* s) {
    if (s->size() < 2 || s->front() != '[') return;
    int depth = 0;
    for (std::size_t i = 0; i < s->size(); ++i) {
        const char c = (*s)[i];
        if (c == '[') {
            ++depth;
        } else if (c == ']') {
            --depth;
            if (depth == 0) {
                if (i == s->size() - 1) *s = s->substr(1, s->size() - 2);
                return;
            }
        }
    }
}

// The "[" ... "]" groups at depth 1 of an array section (works with or without
// the enclosing brackets). This is the Lua's gmatch('%["([%w_]+)",([^%]]*)%]')
// entry list, scanned by depth instead of by pattern.
void SplitEntries(const std::string& section, std::vector<std::string>* out) {
    std::string body = section;
    StripOuterBrackets(&body);
    int depth = 0;
    bool quoted = false;
    std::size_t start = kNpos;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (quoted) continue;
        if (c == '[') {
            if (depth == 0) start = i;
            ++depth;
        } else if (c == ']') {
            if (depth > 0) {
                --depth;
                if (depth == 0 && start != kNpos) {
                    out->push_back(body.substr(start, i - start + 1));
                    start = kNpos;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// reflection calls (every one is "call the UFunction and read ReturnValue")
// ---------------------------------------------------------------------------
bool CallFloat(UObject* o, const char* fn, float* out) {
    if (out) *out = 0.0f;
    if (!o) return false;
    UFunction* f = ue::FindFunction(o, fn);
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(o, f, p.data())) return false;
    if (out) *out = p.Get<float>("ReturnValue");
    return true;
}

bool CallBool(UObject* o, const char* fn, bool* out) {
    if (out) *out = false;
    if (!o) return false;
    UFunction* f = ue::FindFunction(o, fn);
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(o, f, p.data())) return false;
    if (out) *out = p.Get<bool>("ReturnValue");
    return true;
}

UObject* CallObject(UObject* o, const char* fn) {
    if (!o) return nullptr;
    UFunction* f = ue::FindFunction(o, fn);
    if (!f) return nullptr;
    ue::Params p(f);
    if (!ue::Call(o, f, p.data())) return nullptr;
    return p.Get<UObject*>("ReturnValue");
}

bool CallVector(UObject* o, const char* fn, ue::FVector* out) {
    if (out) *out = ue::FVector();
    if (!o) return false;
    UFunction* f = ue::FindFunction(o, fn);
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(o, f, p.data())) return false;
    if (out) *out = p.Get<ue::FVector>("ReturnValue");
    return true;
}

bool CallRotator(UObject* o, const char* fn, ue::FRotator* out) {
    if (out) *out = ue::FRotator();
    if (!o) return false;
    UFunction* f = ue::FindFunction(o, fn);
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(o, f, p.data())) return false;
    if (out) *out = p.Get<ue::FRotator>("ReturnValue");
    return true;
}

// Lua's locOf(): nil on failure (never a fake 0,0,0).
bool LocOf(UObject* o, Vec3* out) {
    if (out) *out = Vec3();
    if (!o) return false;
    ue::FVector v;
    if (!CallVector(o, "K2_GetActorLocation", &v)) return false;
    if (out) {
        out->X = v.X;
        out->Y = v.Y;
        out->Z = v.Z;
    }
    return true;
}

UObject* CharacterMovementOf(UObject* actor) {
    UObject* cm = nullptr;
    if (!actor) return nullptr;
    if (!ue::Get<UObject*>(actor, "CharacterMovement", cm)) return nullptr;
    return cm;
}

// main.lua:1790-1793 / 1808 / 1823: SetMovementMode(mode, 0) and Velocity.
bool SetMovementMode(UObject* cm, int mode) {
    if (!cm) return false;
    UFunction* f = ue::FindFunction(cm, "SetMovementMode");
    if (!f) return false;
    ue::Params p(f);
    p.Set<std::uint8_t>("NewMovementMode", static_cast<std::uint8_t>(mode & 0xFF));
    p.Set<std::uint8_t>("NewCustomMode", static_cast<std::uint8_t>(0));
    return ue::Call(cm, f, p.data());
}

bool ReadMovementMode(UObject* cm, int* out) {
    std::uint8_t v = 0;
    if (!cm) return false;
    if (!ue::Get<std::uint8_t>(cm, "MovementMode", v)) return false;
    if (out) *out = static_cast<int>(v);
    return true;
}

bool SetVelocity(UObject* cm, double x, double y, double z) {
    if (!cm) return false;
    ue::FVector v;
    v.X = x;
    v.Y = y;
    v.Z = z;
    return ue::Set<ue::FVector>(cm, "Velocity", v);
}

// UBrainComponent::StopLogic(const FString& Reason) -- the FString is a
// TArray<wchar_t>; hand the engine a valid temporary it can copy from.
struct TempFString {
    wchar_t buf[96];
    ue::FString str;

    explicit TempFString(const char* ascii) {
        std::size_t i = 0;
        if (ascii) {
            for (; ascii[i] && i + 1 < (sizeof(buf) / sizeof(buf[0])); ++i) buf[i] = (wchar_t)ascii[i];
        }
        buf[i] = 0;
        str.Data = buf;
        str.Num = static_cast<std::int32_t>(i + 1);
        str.Max = static_cast<std::int32_t>(i + 1);
    }
};

UObject* BrainComponentOf(UObject* controller) {
    UObject* bc = nullptr;
    if (!controller) return nullptr;
    if (!ue::Get<UObject*>(controller, "BrainComponent", bc)) return nullptr;
    return bc;
}

bool StopLogic(UObject* brain, const char* reason) {
    if (!brain) return false;
    UFunction* f = ue::FindFunction(brain, "StopLogic");
    if (!f) return false;
    TempFString why(reason);
    ue::Params p(f);
    p.Set<ue::FString>("Reason", why.str);
    return ue::Call(brain, f, p.data());
}

bool RestartLogic(UObject* brain) {
    if (!brain) return false;
    UFunction* f = ue::FindFunction(brain, "RestartLogic");
    if (!f) return false;
    ue::Params p(f);
    return ue::Call(brain, f, p.data());
}

bool SetActorLocation(UObject* a, double x, double y, double z) {
    if (!a) return false;
    UFunction* f = ue::FindFunction(a, "K2_SetActorLocation");
    if (!f) return false;
    ue::Params p(f);   // SweepHitResult stays the zeroed FHitResult the Lua passes as {}
    ue::FVector v;
    v.X = x;
    v.Y = y;
    v.Z = z;
    p.Set<ue::FVector>("NewLocation", v);
    p.Set<bool>("bSweep", false);
    p.Set<bool>("bTeleport", true);
    return ue::Call(a, f, p.data());
}

bool SetActorRotationYaw(UObject* a, double yaw) {
    if (!a) return false;
    UFunction* f = ue::FindFunction(a, "K2_SetActorRotation");
    if (!f) return false;
    ue::Params p(f);
    ue::FRotator r;
    r.Pitch = 0.0;
    r.Yaw = yaw;
    r.Roll = 0.0;
    p.Set<ue::FRotator>("NewRotation", r);
    p.Set<bool>("bTeleportPhysics", true);
    return ue::Call(a, f, p.data());
}

bool PlayAnimMontage(UObject* a, UObject* montage, float rate) {
    if (!a || !montage) return false;
    UFunction* f = ue::FindFunction(a, "PlayAnimMontage");
    if (!f) return false;
    ue::Params p(f);
    p.Set<UObject*>("AnimMontage", montage);
    p.Set<float>("InPlayRate", rate);
    // The Lua passes FName("None"); the None FName is (index 0, number 0), which
    // is exactly a zero-initialised FName. (ms2_ue.h has no MakeFName yet --
    // CONTRACT-REQUEST.)
    p.Set<ue::FName>("StartSectionName", ue::FName());
    return ue::Call(a, f, p.data());
}

bool StopAnimMontage(UObject* a) {
    if (!a) return false;
    UFunction* f = ue::FindFunction(a, "StopAnimMontage");
    if (!f) return false;
    ue::Params p(f);
    p.Set<UObject*>("AnimMontage", static_cast<UObject*>(nullptr));
    return ue::Call(a, f, p.data());
}

// ---------------------------------------------------------------------------
// actor tail helpers
// ---------------------------------------------------------------------------
UObject* FindTickTail(const std::string& tail) {
    for (const auto& it : state().local.tickTails) {
        if (it.tail == tail) return it.obj;
    }
    return nullptr;
}

// fullname(actor:GetClass()). ms2_ue.h forward-declares UClass as its own type
// (it IS a UObject in the engine and there is no GetFullName(UClass*) overload),
// so the pointer is viewed as UObject* for the name call only -- see
// CONTRACT-REQUEST.
std::string ClassFullName(UObject* o) {
    if (!o) return std::string();
    ue::UClass* cls = ue::GetClass(o);
    if (!cls) return std::string();
    return net::FullName(reinterpret_cast<UObject*>(cls));
}

// The class's own short name ("BP_NPC_ShellKeeper_C") plus its full name.  The pair
// matters: MirrorTake's friendly-NPC test must look at the CLASS NAME, because this
// build files the hostile HUB cultists under a folder named NPCs --
//   BlueprintGeneratedClass /Game/Sparta/Dialogue/NPCs/BP_Cultist_HUB.BP_Cultist_HUB_C
//   BlueprintGeneratedClass /Game/Sparta/Dialogue/NPCs/BP_NPC_ShellKeeper.BP_NPC_ShellKeeper_C
// so the full name contains "NPC" for enemies too (coop\near.txt, lab dump).
std::string ClassNameAndFull(UObject* o, std::string* fullName) {
    if (fullName) fullName->clear();
    if (!o) return std::string();
    ue::UClass* cls = ue::GetClass(o);
    if (!cls) return std::string();
    if (fullName) *fullName = net::FullName(reinterpret_cast<UObject*>(cls));
    return ue::GetName(reinterpret_cast<UObject*>(cls));
}

// main.lua:1845-1846: `local a = tickTails[name]; if a and not isLive(a) then a = nil end`
UObject* FindTickTailLive(const std::string& tail) {
    UObject* a = FindTickTail(tail);
    if (a && !Live(a)) return nullptr;
    return a;
}

// SPN_ALIAS lives in state().combat.spawnAlias (StrStrMap, enemy tail ->
// "SP_<spawner tail>"); local::RefreshChars() folds it into tickTails when it
// rebuilds the cache (main.lua:646-649). Lookups copy the value: the map is a
// vector underneath and a Set() would invalidate a held pointer.
std::string AliasOf(const std::string& et) {
    const std::string* key = state().combat.spawnAlias.Find(et);
    return key ? *key : std::string();
}

// main.lua:1328-1331
std::string MontagePath(UObject* o) {
    if (!o) return std::string();
    const std::string fn = net::FullName(o);
    const std::size_t p = fn.find("/Game/");
    if (p == kNpos) return std::string();
    std::size_t e = p;
    while (e < fn.size() && !IsSpace(fn[e])) ++e;
    return fn.substr(p, e - p);
}

// main.lua:1319-1323
std::string HpString(const std::string& tail) {
    const double* v = state().combat.hpSeen.Find(tail);
    if (!v) return std::string("1.0");
    return net::Fmt("%.4f", *v);   // [R10-BOSSFIGHT] 1e-4 = ~1 HP on a boss
}

// ---------------------------------------------------------------------------
// health components (main.lua:1192-1248)
// ---------------------------------------------------------------------------
UObject* HealthCompOf(UObject* actor) {
    if (!Live(actor)) return nullptr;
    const std::string key = net::FullName(actor);
    UObject* hc = nullptr;
    for (const auto& it : state().combat.hcCache) {
        if (it.tail == key) {
            hc = it.obj;
            break;
        }
    }
    if (hc && !Live(hc)) return nullptr;
    return hc;
}

// main.lua:1211-1225 -> nil means "not readable yet". Returns false for nil.
bool HealthNormOf(UObject* actor, double* out, bool* dead) {
    if (out) *out = 0.0;
    if (dead) *dead = false;
    UObject* hc = HealthCompOf(actor);
    if (!hc) return false;

    float maxhp = 0.0f;
    if (!CallFloat(hc, "GetMaxHealth", &maxhp)) return false;
    if (!(maxhp > 0.0f)) return false;   // attributes not initialised yet

    float n = 0.0f;
    if (!CallFloat(hc, "GetCurrentHealthNormalized", &n)) return false;

    bool d = false;
    CallBool(hc, "IsDeadOrDying", &d);

    if (n <= 0.0f && !d) {
        // a fresh actor can report 0 before its attributes are set up; never
        // treat that as real damage (main.lua:1219-1223)
        return false;
    }
    if (out) *out = static_cast<double>(n);
    if (dead) *dead = d;
    return true;
}

// Resolve which two floats of FGameplayAttributeData are BaseValue/CurrentValue.
//
// NOT USED ANY MORE -- kept as the record of a wrong assumption.  On this build the
// struct is not {Base,Current} at +0/+4: the Dumper-7 SDK says an 8-byte prefix,
// then BaseValue at +0x08 and CurrentValue at +0x0C.  The resolver below also
// derived its slot indices from the MaxHealth property's window and applied them to
// the Health property, which is why health writes went to the prefix / the next
// attribute and DAMAGE sync never saw a change.
constexpr std::int32_t kAttrBaseValue    = 0x08;
constexpr std::int32_t kAttrCurrentValue = 0x0C;

[[maybe_unused]] void ResolveHealthSlots(UObject* healthSet, UObject* hc) {
    g_healthSlots[0] = 0;
    g_healthSlots[1] = 1;
    g_healthSlotsKnown = true;

    float maxhp = 0.0f;
    if (!CallFloat(hc, "GetMaxHealth", &maxhp)) return;
    if (!(maxhp > 0.0f) || !std::isfinite(static_cast<double>(maxhp))) return;

    const std::int32_t off = ue::PropOffset(healthSet, "MaxHealth");
    if (off < 0) return;

    float slots[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!ue::ReadMem(reinterpret_cast<const std::uint8_t*>(healthSet) + off, slots, sizeof(slots))) return;

    int current = -1;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(static_cast<double>(slots[i]))) continue;
        if (std::fabs(static_cast<double>(slots[i]) - static_cast<double>(maxhp)) <=
            0.01 * static_cast<double>(maxhp)) {
            current = i;
            break;
        }
    }
    if (current < 0) return;

    if (current == 0) {
        g_healthSlots[0] = 0;
        g_healthSlots[1] = 1;
    } else {
        g_healthSlots[0] = current;
        g_healthSlots[1] = current - 1;
    }
}

}  // namespace

// ===========================================================================
// public API (coop_state.h section 12)
// ===========================================================================

// main.lua:1194-1202
void FillHealthCache() {
    g_hcCacheAt = Mono();
    std::vector<NamedActor> fresh;
    std::vector<UObject*> list = ue::FindAllOf("SpartaHealthComponent");
    for (UObject* hc : list) {
        if (!hc) continue;
        UObject* owner = CallObject(hc, "GetOwner");
        if (!owner) continue;
        const std::string key = net::FullName(owner);
        bool replaced = false;
        for (auto& row : fresh) {
            if (row.tail == key) {   // Lua's table assignment: last one wins
                row.obj = hc;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            NamedActor row;
            row.tail = key;
            row.obj = hc;
            fresh.push_back(row);
        }
    }
    state().combat.hcCache.swap(fresh);
}

// main.lua:638-650. local::RefreshChars() owns the caches (it also folds
// state().combat.spawnAlias into tickTails); this is the forward a combat caller
// uses so the SPN_ALIAS write side and the read side stay one-way (5 -> 2).
void RefreshChars() {
    local::RefreshChars();
}

// main.lua:1211 via the shared signature: < 0 when the health is not readable.
double HealthNorm(UObject* actor, bool* dead) {
    double n = 0.0;
    bool d = false;
    if (!HealthNormOf(actor, &n, &d)) {
        if (dead) *dead = false;
        return -1.0;
    }
    if (dead) *dead = d;
    return n;
}

// main.lua:1227-1241, with the layout MEASURED on this build instead of guessed.
//
// The Dumper-7 SDK generated from this exact exe (GameplayAbilities_structs.hpp,
// "ScriptStruct GameplayAbilities.GameplayAttributeData", size 0x10) says:
//
//   0x00  uint8 Pad_0[0x8]     (an 8-byte prefix, NOT BaseValue)
//   0x08  float BaseValue
//   0x0C  float CurrentValue
//
// The port used to assume {BaseValue +0x00, CurrentValue +0x04} and then "resolve"
// the slots by looking for a value equal to GetMaxHealth() -- but it resolved them
// inside the *MaxHealth* property's window and applied the resulting indices to the
// *Health* property, so the write landed on the attribute's prefix and on the next
// attribute. GetHealth stayed 75 after "writing" 37.5, which is exactly what the
// two-client DAMAGE verdict saw.  Writing +8/+12 needs no resolution at all, and it
// is what the Lua did (ImportText set BaseValue and CurrentValue together).
// [R10-BOSSDMG]
double DamageRaw(UObject* actor, double dmg) {
    UObject* hc = HealthCompOf(actor);
    if (!hc || !(dmg > 0.0)) return -1.0;
    float maxhp = 0.0f;
    if (!CallFloat(hc, "GetMaxHealth", &maxhp) || !(maxhp > 0.0f)) return -1.0;
    bool dead = false;
    const double cur = HealthNorm(actor, &dead);
    if (cur < 0.0 || dead) return -1.0;
    double next = cur - dmg / (double)maxhp;
    if (next < 0.0) next = 0.0;
    if (!SetHealthNorm(actor, next)) return -1.0;
    return next;
}

bool SetHealthNorm(UObject* actor, double norm) {
    UObject* hc = HealthCompOf(actor);
    if (!hc) return false;

    UObject* healthSet = nullptr;
    if (!ue::Get<UObject*>(hc, "HealthSet", healthSet) || !healthSet) return false;
    if (!Live(healthSet)) return false;

    float maxhp = 0.0f;
    if (!CallFloat(hc, "GetMaxHealth", &maxhp)) return false;
    if (!(maxhp > 0.0f)) return false;

    const double v = norm * static_cast<double>(maxhp);
    const std::int32_t off = ue::PropOffset(healthSet, "Health");
    if (off < 0) return false;

    const float f = static_cast<float>(v);
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(healthSet) + off;
    bool ok = ue::WriteMem(base + kAttrBaseValue, &f, sizeof(f));
    ok = ue::WriteMem(base + kAttrCurrentValue, &f, sizeof(f)) && ok;

    // One-shot diagnostic: confirm the two slots really are the attribute. A
    // wrong layout would silently break DAMAGE sync, so say so once.
    if (!g_healthVerifyDone) {
        g_healthVerifyDone = true;
        float readback = 0.0f;
        if (CallFloat(hc, "GetHealth", &readback)) {
            const double eps = std::max(1.0, std::fabs(v) * 0.02);
            if (std::fabs(static_cast<double>(readback) - v) > eps) {
                net::Log("WARN health write: GetHealth %.3f after writing %.3f (offsets 0x%X,0x%X)",
                         static_cast<double>(readback), v, (unsigned)(off + kAttrBaseValue),
                         (unsigned)(off + kAttrCurrentValue));
            }
        }
    }
    // Say what the slot looks like right after a write for the first few calls:
    // "wrote 0.5*max, memory reads back 0.5*max, but GetCurrentHealthNormalized says
    // 1.0" is a completely different bug from "the write went nowhere".
    {
        static std::atomic<int> rawLogs{0};
        if (rawLogs.fetch_add(1) < 4) {
            float raw[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            ue::ReadMem(base, raw, sizeof(raw));
            float norm = 0.0f;
            const bool okNorm = CallFloat(hc, "GetCurrentHealthNormalized", &norm);
            float gh = 0.0f;
            const bool okGh = CallFloat(hc, "GetHealth", &gh);
            net::Log("health write: off=0x%X (base@0x%X cur@0x%X) wrote=%.3f max=%.3f "
                     "raw=[%.3f %.3f %.3f %.3f] GetHealth=%s(%.3f) normalized=%s(%.3f)",
                     (unsigned)off, (unsigned)(off + kAttrBaseValue),
                     (unsigned)(off + kAttrCurrentValue), v, static_cast<double>(maxhp),
                     static_cast<double>(raw[0]), static_cast<double>(raw[1]),
                     static_cast<double>(raw[2]), static_cast<double>(raw[3]),
                     okGh ? "ok" : "fail", static_cast<double>(gh),
                     okNorm ? "ok" : "fail", static_cast<double>(norm));
        }
    }
    return ok;
}

// [R10-BOSSFIGHT] Read GA_Death_AI_C / GA_DeathBase_C's GameplayEvent trigger tag
// from its CDO's AbilityTriggers array once, and send that event to the actor so the
// real death ability (DisableAILogic, Deaggro, PlayDeathMontage, DropCurrency,
// EndDeath) runs instead of leaving the AI alive and swinging after StartDeath().
namespace {

bool SendAiDeathEvent(UObject* actor) {
    static int s_probed = 0;
    static ue::FName s_tag{};
    static ue::UClass* s_deathCls = nullptr;
    if (!s_probed) {
        s_probed = 1;
        const char* names[] = {"GA_Death_AI_C", "GA_DeathBase_C"};
        for (const char* cn : names) {
            ue::UClass* cls = ue::FindClass(cn);
            if (!cls) continue;
            if (!s_deathCls) s_deathCls = cls;
            UObject* cdo = ue::GetCDO(cls);
            if (!Live(cdo)) continue;
            const std::int32_t off = ue::PropOffset(cdo, "AbilityTriggers");
            if (off < 0) continue;
            struct RawArray { const std::uint8_t* data; std::int32_t num; std::int32_t max; };
            RawArray arr{};
            if (!ue::ReadMem(reinterpret_cast<const std::uint8_t*>(cdo) + off, &arr, sizeof(arr)))
                continue;
            if (!arr.data || arr.num <= 0 || arr.num > 32) continue;
            // FAbilityTriggerData: FGameplayTag TriggerTag @0x00 (8B), uint8 TriggerSource @0x08, size 0x0C
            for (std::int32_t i = 0; i < arr.num; ++i) {
                const auto* elem = arr.data + (std::size_t)i * 0x0C;
                ue::FName tag{};
                std::uint8_t src = 255;
                if (ue::ReadMem(elem, &tag, sizeof(tag)) &&
                    ue::ReadMem(elem + 8, &src, sizeof(src)) &&
                    tag.ComparisonIndex != 0 && src == 0) {
                    s_tag = tag;
                    net::Log("combat: [R10-BOSSFIGHT] %s AbilityTriggers[%d] tag=%s",
                             cn, i, ue::ToString(tag).c_str());
                    break;
                }
            }
            if (s_tag.ComparisonIndex != 0) break;
        }
        if (s_tag.ComparisonIndex == 0)
            net::Log("combat: [R10-BOSSFIGHT] no AbilityTriggers GameplayEvent tag on GA_Death_AI_C");
    }

    bool sent = false;
    if (s_tag.ComparisonIndex != 0) {
        if (ue::UClass* libCls = ue::FindClass("AbilitySystemBlueprintLibrary")) {
            if (UObject* libCdo = ue::GetCDO(libCls); Live(libCdo)) {
                if (UFunction* fn = ue::FindFunction(libCdo, "SendGameplayEventToActor")) {
                    ue::Params p(fn);
                    p.Set<UObject*>("Actor", actor);
                    p.Set<ue::FName>("EventTag", s_tag);
                    sent = ue::Call(libCdo, fn, p.data());
                }
            }
        }
    }
    if (!sent && s_deathCls) {
        UObject* asc = nullptr;
        if (ue::Get<UObject*>(actor, "AbilitySystemComponent", asc) && Live(asc)) {
            if (UFunction* fn = ue::FindFunction(asc, "TryActivateAbilityByClass")) {
                ue::Params p(fn);
                p.Set<UObject*>("InAbilityToActivate", reinterpret_cast<UObject*>(s_deathCls));
                p.Set<bool>("bAllowRemoteActivation", false);
                sent = ue::Call(asc, fn, p.data());
            }
        }
    }
    return sent;
}

}  // namespace

// main.lua:1243-1248
bool StartDeath(UObject* actor) {
    UObject* hc = HealthCompOf(actor);
    if (!hc) return false;

    const bool isAi = Live(actor) && actor != net::Pawn() && actor != puppet::Actor();
    if (isAi && !net::FileExists("BOSSFIGHT_OFF.txt")) {
        // Zero health so the boss/enemy HP bar reads 0.000 (not 0.010) and cancel
        // any in-flight attack montage before activating the death ability.
        SetHealthNorm(actor, 0.0);
        StopAnimMontage(actor);
        const bool evOk = SendAiDeathEvent(actor);
        if (UObject* ctrl = CallObject(actor, "GetController"); Live(ctrl)) {
            if (UObject* bc = BrainComponentOf(ctrl)) StopLogic(bc, "coop death");
            if (UFunction* fm = ue::FindFunction(ctrl, "StopMovement")) {
                ue::Params pm(fm);
                ue::Call(ctrl, fm, pm.data());
            }
        }
        bool deadNow = false;
        if (CallBool(hc, "IsDeadOrDying", &deadNow) && deadNow) return true;
        (void)evOk;
    }

    UFunction* f = ue::FindFunction(hc, "StartDeath");
    if (!f) return false;
    ue::Params p(f);
    return ue::Call(hc, f, p.data());
}

bool IsDeadTail(const std::string& tail) {
    return state().combat.deadTails.Has(tail);
}

// ===========================================================================
// enemies (main.lua:1250-1408)
// ===========================================================================
namespace {

struct NpcRow {
    UObject* a = nullptr;
    Vec3 loc;
    double d = 0.0;
    double dp = kInf;
    bool own = false;
    std::string cls;
    std::string tail;
};

const PeerClaim* FindClaim(const std::string& tail) {
    for (const auto& c : state().combat.peerClaims) {
        if (c.key == tail) return &c;
    }
    return nullptr;
}

double Dist3(const Vec3& a, const Vec3& b) {
    const double dx = a.X - b.X;
    const double dy = a.Y - b.Y;
    const double dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// main.lua:1265-1317
std::vector<NpcRow> CollectNpcs() {
    std::vector<NpcRow> list;
    UObject* pawn = net::Pawn();
    if (!pawn) {
        if (g_diagCollect.Due(Mono(), 10.0, 8))
            net::Log("collect: no local pawn -- this client publishes no npc list at all");
        return list;
    }
    Vec3 me;
    if (!LocOf(pawn, &me)) {
        if (g_diagCollect.Due(Mono(), 10.0, 8))
            net::Log("collect: K2_GetActorLocation failed on the pawn -- no npc list");
        return list;
    }

    UObject* pup = puppet::Actor();
    Vec3 pl;
    bool hasPl = false;
    if (pup) hasPl = LocOf(pup, &pl);

    const bool host = (state().local.role == "host");
    const std::size_t nchars = state().local.tickChars.size();
    int nNear = 0, nOwn = 0;

    for (UObject* a : state().local.tickChars) {
        if (!a || a == pawn || a == pup) continue;

        const std::string cfn = ClassFullName(a);
        if (Contains(cfn, "BP_PlayerCharacter") || Contains(cfn, "BP_MainMenuCharacter")) continue;

        Vec3 loc;
        if (!LocOf(a, &loc)) continue;

        std::string tl = net::Tail(a);
        {
            const std::string alias = AliasOf(tl);   // tl = SPN_ALIAS[tl] or tl
            if (!alias.empty()) tl = alias;
        }

        const double d = Dist3(loc, me);
        double dp = kInf;
        if (hasPl) dp = Dist3(loc, pl);

        if (d <= kNpcRadius || dp <= kNpcRadius) {
            bool own = false;
            if (host) {
                own = (d <= kNpcRadius) || (d <= dp + kNpcMargin);
                // the guest claims it and its copy stands clearly on the guest's
                // side: judge by that copy (ours may lag behind)
                const PeerClaim* c = FindClaim(tl);
                if (own && c && hasPl) {
                    Vec3 cp;
                    cp.X = c->pos.X;
                    cp.Y = c->pos.Y;
                    cp.Z = c->pos.Z;
                    const double ch = Dist3(cp, me);
                    const double cg = Dist3(cp, pl);
                    if (ch > kNpcRadius + kClaimSlack && ch > cg + kNpcMargin) own = false;
                }
            } else {
                own = (d <= kNpcRadius) && (dp > kNpcRadius + kClaimSlack) && (dp > d + kNpcMargin);
            }
            NpcRow row;
            row.a = a;
            row.loc = loc;
            row.d = d;
            row.dp = dp;
            row.own = own;
            row.cls = cfn;
            row.tail = tl;
            list.push_back(row);
            ++nNear;
            if (own) ++nOwn;
        }
    }

    // main.lua:1312-1315. Same comparator, same (unstable) ordering: the harness
    // only relies on list[1] being the nearest enemy of this client.
    std::sort(list.begin(), list.end(), [](const NpcRow& x, const NpcRow& y) {
        const bool xi = x.d <= kNpcRadius;
        const bool yi = y.d <= kNpcRadius;
        if (xi != yi) return xi;
        return std::min(x.d, x.dp) < std::min(y.d, y.dp);
    });

    // GATE 1 (send): every number the ring needs before anything can be published.
    if (g_diagCollect.Due(Mono(), 5.0, 200)) {
        net::Log("collect: chars=%d near=%d own=%d list=%d host=%d peerpos=%d nearest=%s d=%.0f dp=%.0f",
                 (int)nchars, nNear, nOwn, (int)list.size(), host ? 1 : 0, hasPl ? 1 : 0,
                 list.empty() ? "-" : list.front().tail.c_str(),
                 list.empty() ? 0.0 : list.front().d,
                 list.empty() ? 0.0 : list.front().dp);
    }
    return list;
}

// main.lua:1397-1408
void KillTailLogged(const std::string& tail, const char* reason) {
    if (state().combat.deadTails.Has(tail)) return;
    state().combat.deadTails.Add(tail);
    UObject* target = nullptr;
    for (const auto& it : state().local.tickTails) {
        if (it.tail == tail) {
            target = it.obj;
            break;
        }
    }
    const char* why = (reason && *reason) ? reason : "sync";
    if (target) {
        // [R7-DROPS] G6: this death happened because the PEER's copy died, so the
        // peer's client is the one that rolls the loot.  Tell the drops module
        // before the game's death ability gets a chance to roll a second set here.
        drops::MarkMirrorDeath(target, why);
        const bool ok = StartDeath(target);
        net::Log("killTail %s (%s): startDeath ok=%s %s", tail.c_str(), why, ok ? "true" : "false", "");
    } else {
        net::Log("killTail %s (%s): no local actor with that tail", tail.c_str(), why);
    }
}

// MIR is keyed by the peer's enemy name (the tail the packet carried with the
// spawner alias applied). MirrorEntry has no name field, so the keys are kept in
// a parallel vector owned by this file -- a destroyed actor still has to be
// releasable, which a recomputed GetFullName could not promise.
std::string MirrorKeyOf(UObject* a) {
    if (!a) return std::string();
    std::string tail = net::Tail(a);
    const std::string alias = AliasOf(tail);
    if (!alias.empty()) tail = alias;
    return tail;
}

// Self-heal if the two vectors ever disagree (nothing else may touch `mirrors`).
void SyncMirrorKeys() {
    std::vector<MirrorEntry>& v = state().combat.mirrors;
    if (g_mirrorKeys.size() == v.size()) return;
    g_mirrorKeys.clear();
    for (const auto& m : v) g_mirrorKeys.push_back(MirrorKeyOf(m.actor));
}

int MirrorIndex(const std::string& name) {
    SyncMirrorKeys();
    for (std::size_t i = 0; i < g_mirrorKeys.size(); ++i) {
        if (g_mirrorKeys[i] == name) return static_cast<int>(i);
    }
    return -1;
}

MirrorEntry* FindMirror(const std::string& name) {
    const int i = MirrorIndex(name);
    return (i < 0) ? nullptr : &state().combat.mirrors[static_cast<std::size_t>(i)];
}

// Snapshot of the keys, for the release loop in enemyTick (which mutates MIR).
std::vector<std::string> MirrorNames() {
    SyncMirrorKeys();
    return g_mirrorKeys;
}

// main.lua:1779-1799
MirrorEntry* MirrorTake(UObject* a, const std::string& name) {
    CombatState& c = state().combat;

    int idx = MirrorIndex(name);
    if (idx >= 0 && c.mirrors[static_cast<std::size_t>(idx)].actor == a) {
        return &c.mirrors[static_cast<std::size_t>(idx)];   // MIR[name] already drives it
    }
    if (c.mirrorSkip.Has(name)) return nullptr;

    std::string cfn;
    const std::string cn = ClassNameAndFull(a, &cfn);
    // main.lua:1785 `cfn:find("NPC", 1, true)` on the CLASS FULL NAME.  On this build
    // that is a false positive for most of the HUB's hostile enemies: they live in
    //   BlueprintGeneratedClass /Game/Sparta/Dialogue/NPCs/BP_Cultist_HUB.BP_Cultist_HUB_C
    // (lab dump, coop\near.txt) -- "NPC" in the PATH, not in the class.  The Lua's
    // own comment says the rule is about FRIENDLY NPCs (vendors, quest givers):
    //   /Game/Sparta/Dialogue/NPCs/BP_NPC_ShellKeeper.BP_NPC_ShellKeeper_C
    // so judge by the class NAME, which separates the two exactly (BP_NPC_* vs
    // BP_Cultist_*), and say it once when the old full-name test would have skipped
    // a real enemy.  Without this MirrorTake never reached its log for the cultist
    // the harness pulls across the map, which is why `taken` stayed at 0.
    if (Contains(cn, "NPC")) {
        // friendly NPCs (vendors, quest givers) keep their own AI: only their
        // position is shared (main.lua:1783-1786)
        if (SayOnce("mir-npc-skip", name)) {
            net::Log("mir skip %s: class \"%s\" is a friendly NPC (position only)", name.c_str(),
                     cfn.c_str());
        }
        c.mirrorSkip.Add(name);
        return nullptr;
    }
    if (Contains(cfn, "NPC") && SayOnce("mir-npc-path", name)) {
        net::Log("mir keep %s: class \"%s\" sits under an NPCs/ asset path but is \"%s\" "
                 "(the Lua's full-name test skipped it)", name.c_str(), cfn.c_str(), cn.c_str());
    }

    net::Log("mir take %s (%s)", name.c_str(), cfn.c_str());

    UObject* ctrl = CallObject(a, "GetController");
    // a freshly spawned enemy gets its controller a moment later: retried in mirrorDrive
    bool aiOff = false;
    if (ctrl) {
        UObject* bc = BrainComponentOf(ctrl);
        if (bc) aiOff = StopLogic(bc, "coop mirror");
    }

    int mode = 1;
    UObject* cm = CharacterMovementOf(a);
    if (!ReadMovementMode(cm, &mode)) mode = 1;
    // MOVE_None: no gravity or braking fights the positions we write, and the
    // velocity we set stays put for the anim BP's locomotion blend
    SetMovementMode(cm, 0);

    MirrorEntry m;
    m.actor = a;
    m.savedMode = mode;
    m.mont.clear();
    m.seen = Mono();
    m.aiOff = aiOff;

    if (idx >= 0) {
        c.mirrors[static_cast<std::size_t>(idx)] = m;      // MIR[name] = m (replaces)
        g_mirrorKeys[static_cast<std::size_t>(idx)] = name;
    } else {
        c.mirrors.push_back(m);
        g_mirrorKeys.push_back(name);
        idx = static_cast<int>(c.mirrors.size()) - 1;
    }
    c.mirTaken += 1;

    net::Log("enemy %s now driven by the peer (AI paused, mode %d -> 0)", name.c_str(), mode);
    return &c.mirrors[static_cast<std::size_t>(idx)];
}

// main.lua:1801-1813
void MirrorRelease(const std::string& name, const char* why) {
    CombatState& c = state().combat;
    const int idx = MirrorIndex(name);
    if (idx < 0) return;

    const std::size_t i = static_cast<std::size_t>(idx);
    const MirrorEntry m = c.mirrors[i];
    c.mirrors[i] = c.mirrors.back();
    c.mirrors.pop_back();
    g_mirrorKeys[i] = g_mirrorKeys.back();
    g_mirrorKeys.pop_back();
    c.mirReleased += 1;

    UObject* a = m.actor;
    if (!Live(a)) return;

    SetMovementMode(CharacterMovementOf(a), m.savedMode);
    if (!c.deadTails.Has(name)) {
        // dead / no controller: nothing to restart
        UObject* ctrl = CallObject(a, "GetController");
        UObject* bc = BrainComponentOf(ctrl);
        if (bc) RestartLogic(bc);
    }
    net::Log("enemy %s back under local AI (%s)", name.c_str(), (why && *why) ? why : "");
}

// MIRROR thrash detector.  The harness reads `taken:` from this module, and 400 of
// each for two enemies (slotfix4: B taken:419 released:418) means the take/release
// loop never settles -- the Lua had taken:33 released:28 over a whole run.  Name the
// enemy and the interval it churns in, so the next run says which gate released it
// instead of only showing the total.
struct ThrashRow {
    std::string tail;
    double firstAt = 0.0;
    double lastLog = -1.0e9;
    int n = 0;
};
std::vector<ThrashRow> g_thrash;

void ThrashNote(const std::string& tail) {
    const double mono = Mono();
    for (auto& r : g_thrash) {
        if (r.tail != tail) continue;
        if (mono - r.firstAt > 10.0) {
            r.firstAt = mono;
            r.n = 0;
        }
        r.n += 1;
        if (r.n >= 4 && mono - r.lastLog > 10.0) {
            r.lastLog = mono;
            const double span = mono - r.firstAt;
            net::Log("mir thrash %s: %d releases in %.1fs -- one every %.2fs, the peer is still "
                     "publishing it (clock or ownership gate, not the peer)",
                     tail.c_str(), r.n, span, (r.n > 0) ? span / static_cast<double>(r.n) : 0.0);
        }
        return;
    }
    if (g_thrash.size() > 512) g_thrash.clear();
    ThrashRow row;
    row.tail = tail;
    row.firstAt = mono;
    row.n = 1;
    g_thrash.push_back(row);
}

// main.lua:1815-1836
void MirrorDrive(MirrorEntry* m, double vx, double vy, double vz, double yaw, const std::string& mont) {
    if (!m) return;
    UObject* a = m->actor;
    if (!Live(a)) return;
    m->seen = Mono();

    if (!m->aiOff) {
        UObject* ctrl = CallObject(a, "GetController");
        UObject* bc = BrainComponentOf(ctrl);
        if (bc) m->aiOff = StopLogic(bc, "coop mirror");
    }

    SetActorRotationYaw(a, yaw);
    SetVelocity(CharacterMovementOf(a), vx, vy, vz);

    if (mont != m->mont) {
        m->mont = mont;
        if (mont.empty()) {
            StopAnimMontage(a);
        } else {
            UObject* mo = ue::FindObject(mont.c_str());
            if (mo && Live(mo)) {
                PlayAnimMontage(a, mo, 1.0f);
                state().combat.mirMontages += 1;
            }
        }
    }
}

// main.lua:1838-1891
void ApplyNpcs(const std::string& line) {
    std::string sec;
    if (!net::Section(line, "npc", &sec)) {
        if (g_diagRecv.Due(Mono(), 10.0, 6))
            net::Log("npcrcv: the peer line carries no \"npc\" section");
        return;
    }

    std::vector<std::string> entries;
    SplitEntries(sec, &entries);

    std::vector<PeerClaim> claims;
    CombatState& c = state().combat;

    int nEntries = 0, nClaims = 0, nResolved = 0, nSp = 0;

    for (const std::string& entry : entries) {
        std::vector<std::string> f;
        std::string body = entry;
        StripOuterBrackets(&body);
        SplitFields(body, &f);
        if (f.empty()) continue;
        ++nEntries;

        const std::string name = Unquote(f[0]);
        UObject* a = FindTickTailLive(name);

        double tx = 0.0, ty = 0.0, tz = 0.0;
        const bool hasPos = ParseNum(Field(f, 1), &tx) && ParseNum(Field(f, 2), &ty) &&
                            ParseNum(Field(f, 3), &tz);

        const std::string hpTok = Field(f, 4);
        const std::string deadTok = Field(f, 5);
        bool isDead = (deadTok == "1");
        if (!isDead) {
            double hpv = 0.0;
            if (!ParseNum(hpTok, &hpv)) hpv = 1.0;   // (tonumber(hp) or 1)
            if (hpv <= kDeadHp) isDead = true;
        }

        double vx = 0.0, vy = 0.0, vz = 0.0, yaw = 0.0;
        if (!ParseNum(Field(f, 6), &vx)) vx = 0.0;
        if (!ParseNum(Field(f, 7), &vy)) vy = 0.0;
        if (!ParseNum(Field(f, 8), &vz)) vz = 0.0;
        const bool hasYaw = ParseNum(Field(f, 9), &yaw);
        const std::string mont = QuotedField(f, 10);

        if (!hasPos) continue;

        PeerClaim claim;
        claim.key = name;
        claim.pos.X = tx;
        claim.pos.Y = ty;
        claim.pos.Z = tz;
        claims.push_back(claim);
        ++nClaims;
        if (name.size() > 3 && name.compare(0, 3, "SP_") == 0) ++nSp;

        if (a) {
            ++nResolved;
        } else if (SayOnce("npc-miss", name)) {
            // GATE 4a (recv): the peer owns this enemy but there is no local actor
            // with that tail, so it can neither be mirrored nor replayed.
            net::Log("npcrcv: no local actor for %s (tickTails=%d) -- claim kept, nothing to drive",
                     name.c_str(), (int)state().local.tickTails.size());
        }

        if (a && !c.ownedTails.Has(name) && !c.mirroredOnce.Has(name)) {
            c.mirroredOnce.Add(name);
            // The Lua logs exactly this line and run-two.ps1 greps it verbatim
            // (SimpleMatch "mirroring peer-owned enemy <tail>"), so the name stays
            // first and unchanged.  A spawner-spawned enemy travels under its
            // "SP_<spawner tail>" alias, which is NOT what either world calls the
            // actor, so append this world's own name after the position when the
            // two differ -- the harness match is unaffected, the log is readable.
            const std::string local = net::Tail(a);
            if (!local.empty() && local != name) {
                net::Log("mirroring peer-owned enemy %s at %.0f %.0f %.0f (local tail %s)",
                         name.c_str(), tx, ty, tz, local.c_str());
            } else {
                net::Log("mirroring peer-owned enemy %s at %.0f %.0f %.0f", name.c_str(), tx, ty, tz);
            }
        }

        if (a && !c.deadTails.Has(name) && !isDead && !c.ownedTails.Has(name)) {
            c.npcMirrored += 1;
            MirrorEntry* m = ::coop::toggles().mirrorEnabled ? MirrorTake(a, name) : nullptr;

            SetActorLocation(a, tx, ty, tz);
            if (m && hasYaw) MirrorDrive(m, vx, vy, vz, yaw, mont);

            // a pass only counts after the host really moved this NPC: with
            // identical saves two copies match trivially without any sync
            Vec3 first;
            bool haveFirst = false;
            for (const auto& fp : g_firstPos) {
                if (fp.tail == name) {
                    first = fp.pos;
                    haveFirst = true;
                    break;
                }
            }
            if (!haveFirst) {
                FirstPos fp;
                fp.tail = name;
                fp.pos.X = tx;
                fp.pos.Y = ty;
                fp.pos.Z = tz;
                g_firstPos.push_back(fp);
                first = fp.pos;
                haveFirst = true;
            }
            bool firstListed = false;
            for (const auto& row : c.npcFirst) {
                if (row.tail == name) {
                    firstListed = true;
                    break;
                }
            }
            if (!firstListed) {
                NamedActor row;
                row.tail = name;
                row.obj = nullptr;   // the position lives in g_firstPos
                c.npcFirst.push_back(row);
            }

            const double mx = tx - first.X;
            const double my = ty - first.Y;
            const double mz = tz - first.Z;
            if (std::sqrt(mx * mx + my * my + mz * mz) > kNearOwnMoved) c.npcMoved = true;

            Vec3 got;
            if (LocOf(a, &got) && c.npcMoved && state().local.role != "host") {
                const double dd = Dist3(got, Vec3{tx, ty, tz});
                c.npcTicks += 1;
                if (dd < kNearGood) c.npcGood += 1;
                if (c.npcTicks >= kNpcVerdictN && !c.npcVerdictDone) {
                    c.npcVerdictDone = true;
                    net::WriteFile("result_npc.txt",
                                   net::Fmt("%s npc mirror after host move %d/20 within 30u (last %.1f, %s)\n",
                                            (c.npcGood >= 18) ? "PASS" : "FAIL", c.npcGood, dd, name.c_str()));
                    net::Log("npc verdict: %d/20 good", c.npcGood);
                }
            }
        } else if (a && SayOnce("mir-block", name)) {
            // The one silent path that keeps `taken` at 0 while "mirroring ..." is
            // logged: everything a, deadTails / peer-dead / our-own filtered above.
            net::Log("mir block %s: local_dead=%d peer_dead=%d hp=%s ours=%d mirror_enabled=%d",
                     name.c_str(), c.deadTails.Has(name) ? 1 : 0, isDead ? 1 : 0, hpTok.c_str(),
                     c.ownedTails.Has(name) ? 1 : 0,
                     ::coop::toggles().mirrorEnabled ? 1 : 0);
        }
    }

    c.peerClaims.swap(claims);

    // GATE 4 (recv): what arrived and how much of it this world can act on.
    if (g_diagRecv.Due(Mono(), 5.0, 200)) {
        net::Log("npcrcv: entries=%d claims=%d resolved=%d sp=%d ticks=%d dead=%d owned=%d",
                 nEntries, nClaims, nResolved, nSp, (int)state().local.tickTails.size(),
                 (int)c.deadTails.Size(), (int)c.ownedTails.Size());
    }
}

// [R10-BOSSFIGHT] measured (r10_boss6): the fixed epsilons are in NORMALISED health, so on
// a ~10 000 HP boss a hit (35 HP = 0.0034) was below the local damage epsilon (0.004) and
// the guest adopted only gaps over 0.02 (~200 HP): its boss bar ran >10 s behind.  The
// epsilon is now the smaller of the old value and a fixed number of HP points.
static bool BossFightOff() {
    static int v = -1;
    if (v < 0) {
        v = net::FileExists("BOSSFIGHT_OFF.txt") ? 1 : 0;
        if (v) net::Log("BOSSFIGHT_OFF.txt present -- the R10 boss-fight fixes are off");
    }
    return v != 0;
}
double ScaledEps(UObject* a, double base, double hpPoints) {
    if (BossFightOff()) return base;
    UObject* hc = HealthCompOf(a);
    float mx = 0.0f;
    if (!hc || !CallFloat(hc, "GetMaxHealth", &mx) || !(mx > 0.0f)) return base;
    double e = hpPoints / (double)mx;
    if (e < 0.001) e = 0.001;
    return e < base ? e : base;
}

// main.lua:1895-1924
void ApplyHealth(const std::string& line) {
    std::string sec;
    if (!net::Section(line, "nhp", &sec)) return;

    std::vector<std::string> entries;
    SplitEntries(sec, &entries);
    CombatState& c = state().combat;

    for (const std::string& entry : entries) {
        std::vector<std::string> f;
        std::string body = entry;
        StripOuterBrackets(&body);
        SplitFields(body, &f);
        if (f.size() < 2) continue;

        const std::string tail = Unquote(f[0]);
        double peerHp = 0.0;
        if (!ParseNum(f[1], &peerHp)) continue;
        if (peerHp < 0.0) continue;

        UObject* a = FindTickTailLive(tail);
        if (!a) continue;

        double mine = 0.0;
        bool dead = false;
        if (!HealthNormOf(a, &mine, &dead)) continue;

        const double gap = mine - peerHp;
        if (gap > kHpAdoptEps || (gap > 0.001 && gap > ScaledEps(a, kHpAdoptEps, 25.0))) {
            SetHealthNorm(a, peerHp);
            c.hpSeen.Set(tail, peerHp);
            if (peerHp <= kDeadHp && !c.deadTails.Has(tail)) {
                c.deadTails.Add(tail);
                // [R7-DROPS] G6: same rule as killTail -- the peer killed it, the
                // peer rolls the loot, this copy's own roll is vetoed.
                drops::MarkMirrorDeath(a, "peer damage");
                const bool ok = StartDeath(a);
                net::Log("peer damage killed %s: startDeath ok=%s %s", tail.c_str(),
                         ok ? "true" : "false", "");
            }
        } else if (c.deadTails.Has(tail) && peerHp >= kHpRespawn && !dead) {
            // peer says this enemy is alive again (respawn after a wipe)
            c.deadTails.Erase(tail);
            SetHealthNorm(a, peerHp);
            c.hpSeen.Set(tail, peerHp);
            net::Log("peer respawn adopted: %s (%.2f)", tail.c_str(), peerHp);
        }
    }
}

// main.lua:1926-1934
void ApplyDead(const std::string& line) {
    std::string sec;
    if (!net::Section(line, "dead", &sec)) return;

    // sec:gmatch('"([%w_]+)"') -- quoted word tokens only
    std::size_t i = 0;
    while (i < sec.size()) {
        if (sec[i] != '"') {
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        std::string tok;
        while (j < sec.size() && sec[j] != '"') {
            const unsigned char ch = static_cast<unsigned char>(sec[j]);
            const bool word = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
                              (ch >= 'a' && ch <= 'z') || ch == '_';
            if (!word) {
                tok.clear();
                break;
            }
            tok.push_back(static_cast<char>(ch));
            ++j;
        }
        if (j < sec.size() && sec[j] == '"' && !tok.empty()) {
            if (!state().combat.deadTails.Has(tok)) KillTailLogged(tok, "peer");
            i = j + 1;
            continue;
        }
        i = (j > i) ? j + 1 : i + 1;
    }
}

// main.lua:3909-3924
void ApplyPeerDamage(double total) {
    CombatState& c = state().combat;
    if (!c.hasPdGot) {
        c.hasPdGot = true;
        c.pdGot = total;   // baseline, never replay old hits
        return;
    }
    if (total <= c.pdGot + kPdEps) return;
    const double delta = total - c.pdGot;
    c.pdGot = total;

    UObject* pawn = net::Pawn();
    if (!pawn) return;
    const double hp = HealthNorm(pawn, nullptr);
    if (hp < 0.0) return;

    const double now = std::max(0.0, hp - delta);
    SetHealthNorm(pawn, now);
    c.pdApplied += 1;
    net::Log("hit on us by the peer's enemy: %.3f -> %.3f", hp, now);
    net::WriteFile("result_pd.txt",
                   net::Fmt("PASS peer-side hit applied to own body: %.3f -> %.3f\n", hp, now));
    if (now <= 0.0) {
        net::SetLocalHold(3.0);   // LOCAL_HOLD = os.time() + 3
        const bool ok = StartDeath(pawn);
        net::Log("killed by the peer's enemy: startDeath ok=%s", ok ? "true" : "false");
    }
}

// main.lua:3926-3935
void FrameTime() {
    if (!g_gameStats) g_gameStats = ue::FindObject("/Script/Engine.Default__GameplayStatics");
    UObject* pawn = net::Pawn();
    if (!g_gameStats || !pawn) return;

    if (!Live(g_gameStats)) {
        g_gameStats = nullptr;
        return;
    }
    UFunction* f = ue::FindFunction(g_gameStats, "GetWorldDeltaSeconds");
    if (!f) return;
    ue::Params p(f);
    p.Set<UObject*>("WorldContextObject", pawn);
    if (!ue::Call(g_gameStats, f, p.data())) return;
    double dt = 0.0;
    if (!ReturnNumber(p, &dt)) return;
    if (!(dt > 0.0) || dt > 1.0) return;

    Vec3 me;
    const bool hasMe = LocOf(pawn, &me);
    Vec3 pl;
    bool hasPl = false;
    UObject* pup = puppet::Actor();
    if (pup) hasPl = LocOf(pup, &pl);

    const bool far = hasMe && hasPl &&
                     std::sqrt((me.X - pl.X) * (me.X - pl.X) + (me.Y - pl.Y) * (me.Y - pl.Y)) > 30000.0;
    CombatState& c = state().combat;
    if (far) {
        c.ftFarSum += dt;
        c.ftFarN += 1;
    } else {
        c.ftNearSum += dt;
        c.ftNearN += 1;
    }
}

// ---------------------------------------------------------------------------
// the proximity spawner's trigger
// ---------------------------------------------------------------------------
// The Lua calls ONE mangled Blueprint function (main.lua:3868).  The number in
// that name follows the blueprint's node order, so a port that bets the whole
// SPAWN verdict on it fails silently when the number is 0 or 2: try the variants.
const char* const kSpawnerTriggerNames[] = {
    "BndEvt__BP_Spawner_Proximity_Sphere_K2Node_ComponentBoundEvent_1_ComponentBeginOverlapSignature__DelegateSignature",
    "BndEvt__BP_Spawner_Proximity_Sphere_K2Node_ComponentBoundEvent_0_ComponentBeginOverlapSignature__DelegateSignature",
    "BndEvt__BP_Spawner_Proximity_Sphere_K2Node_ComponentBoundEvent_2_ComponentBeginOverlapSignature__DelegateSignature",
    "BndEvt__BP_Spawner_Proximity_Sphere_K2Node_ComponentBoundEvent_3_ComponentBeginOverlapSignature__DelegateSignature",
    "BndEvt__BP_Spawner_Proximity_Capsule_K2Node_ComponentBoundEvent_1_ComponentBeginOverlapSignature__DelegateSignature",
    "BndEvt__BP_Spawner_Proximity_Capsule_K2Node_ComponentBoundEvent_0_ComponentBeginOverlapSignature__DelegateSignature",
};

UFunction* FindSpawnerTrigger(UObject* sp) {
    if (!Live(sp)) return nullptr;
    for (const char* n : kSpawnerTriggerNames) {
        if (UFunction* f = ue::FindFunction(sp, n)) return f;
    }
    return nullptr;
}

// main.lua:3863-3871: fire that event with OUR pawn as the one who walked in (the
// puppet does not count as a player; lab sp15 spawned 20k away from the real
// player).  `detail` gets the component state so a failure can be read off the log.
bool FireSpawnerTrigger(UObject* sp, UFunction* f, std::string* detail) {
    if (detail) detail->clear();
    if (!Live(sp) || !f) {
        if (detail) *detail = "no trigger function";
        return false;
    }
    UObject* pawn = net::Pawn();
    if (!pawn) {
        if (detail) *detail = "no local pawn";
        return false;
    }
    UObject* trigger = nullptr;
    UObject* capsule = nullptr;
    ue::Get<UObject*>(sp, "PlayerTrigger", trigger);
    ue::Get<UObject*>(pawn, "CapsuleComponent", capsule);

    ue::Params p(f);
    p.Set<UObject*>("OverlappedComponent", trigger);
    p.Set<UObject*>("OtherActor", pawn);
    p.Set<UObject*>("OtherComp", capsule);
    p.Set<std::int32_t>("OtherBodyIndex", 0);
    p.Set<bool>("bFromSweep", false);
    // SweepResult stays the zeroed FHitResult the Lua passes as {}
    const bool ok = ue::Call(sp, f, p.data());
    if (detail) *detail = net::Fmt("pawn=%d trigger=%d capsule=%d", pawn ? 1 : 0, trigger ? 1 : 0,
                                   capsule ? 1 : 0);
    return ok;
}

// Radius of the spawner's own player trigger, so "the pawn is inside the sphere"
// can be decided from the game's own geometry instead of a guessed constant.
// SphereRadius is a float property; reading it as a double takes 4 bytes of the
// neighbouring field with it, so the width matters.
double SpawnerTriggerRadius(UObject* sp) {
    UObject* trigger = nullptr;
    if (!ue::Get<UObject*>(sp, "PlayerTrigger", trigger) || !trigger) return 0.0;
    if (!Live(trigger)) return 0.0;
    float r = 0.0f;
    if (!ue::Get<float>(trigger, "SphereRadius", r)) return 0.0;
    const double d = static_cast<double>(r);
    if (!(d > 1.0) || d > 5000.0) return 0.0;
    return d;
}

// ---------------------------------------------------------------------------
// [R6-SPAWN] why a replayed spawn does not produce an enemy
// ---------------------------------------------------------------------------
// Firing the proximity trigger is NECESSARY but, measured, not SUFFICIENT: in
// run-two fixall-2 the host fired the same event every 10 s for a spawner the
// guest had already spawned from, and no enemy appeared for minutes while other
// spawners did spawn.  The trigger call itself returns true, so the block is
// inside the Blueprint -- and every gate the Blueprint uses is a property on the
// spawner (BP_Spawner.hpp / BP_Spawner_Proximity.hpp), which can be READ instead
// of guessed:
//   FinishedSpawn / DebugAttemptedEnemySpawn / DebugFailedToSpawnEnemy /
//   DebugSpawnStage  -- the BP's own record of the attempt;
//   WaitForStreaming / SpawnWhenLoaded / DoCustomSpawnWhenLoaded -- the streaming
//   gates (a spawner whose cell is loaded but whose neighbours are not waits);
//   AllowSpawnIfInSafeZone / PlayerReqArtifactsToSpawn / RequirePath /
//   UsesTerritorySystem -- the eligibility gates;
//   PendingSpawnedEnemies / SpawnedAICharacter / SpawnedEnemy -- how far it got;
//   CachedClass / AssociatedSpawnTriggers (proximity) -- the async class load and
//   the spawn triggers the proximity subtype works through.
// The probe prints that state next to the trigger result, and a short watcher
// after each fire prints the first changes so the field that never advances is
// visible in the log instead of being inferred from a timeout.
struct SpawnGateRow {
    std::string key;         // "SP_<spawner tail>"
    UObject* sp = nullptr;
    double at = 0.0;         // Mono() of the fire
    int  n = 0;              // state lines printed for this fire
    std::string last;        // last state line (only a CHANGE is logged)
    // the enemy this spawner produced, watched separately: "the spawner's enemy
    // pointer went stale" and "the enemy died" are different failures, and the
    // last health/location before it went away says which one it was.
    UObject* enemy = nullptr;
    std::string enemyName;   // cached while it is alive: a destroyed actor's name is not readable
    double lastHp = -1.0;
    Vec3  lastPos;
};

std::vector<SpawnGateRow> g_spawnGates;

// FString property -> utf8 (empty when the property is absent or unreadable).
std::string PropText(UObject* o, const char* name) {
    if (!o || ue::PropOffset(o, name) < 0) return std::string();
    ue::FString s{};
    if (!ue::Get<ue::FString>(o, name, s)) return std::string();
    return ue::ToUtf8(s);
}

// "0/1" of a bool property, "-" when the class does not have it (a plain
// AUnitSpawner has none of the BP's fields, so every read must be optional).
std::string PropBit(UObject* o, const char* name) {
    if (!o || ue::PropOffset(o, name) < 0) return "-";
    return ue::GetBool(o, name) ? "1" : "0";
}

std::string PropInt(UObject* o, const char* name) {
    if (!o || ue::PropOffset(o, name) < 0) return "-";
    std::int32_t v = 0;
    if (!ue::Get<std::int32_t>(o, name, v)) return "-";
    return net::Fmt("%d", v);
}

// name of an object-valued property ("-" null, "!stale" not live any more).
std::string PropObj(UObject* o, const char* name) {
    if (!o || ue::PropOffset(o, name) < 0) return "-";
    UObject* v = nullptr;
    if (!ue::Get<UObject*>(o, name, v) || !v) return "-";
    if (!Live(v)) return "!stale";
    return ue::GetName(v);
}

// UClass-valued property (TSubclassOf / UClass*) -> the class's name.
std::string PropClass(UObject* o, const char* name) {
    UObject* v = nullptr;
    if (!o || ue::PropOffset(o, name) < 0) return "-";
    if (!ue::Get<UObject*>(o, name, v) || !v) return "-";
    return ClassNameAndFull(v, nullptr);
}

int PropArrayNum(UObject* o, const char* name) {
    if (!o || ue::PropOffset(o, name) < 0) return -1;
    ue::TArray<UObject*> arr{};
    if (!ue::Get<ue::TArray<UObject*>>(o, name, arr)) return -1;
    return arr.Num;
}

double g_nearScanAt = -1.0e9;
std::vector<UObject*> g_nearChars;

// The spawner's own gate state, one line.  Everything is optional so the same
// probe works on a plain UnitSpawner and on the proximity subtype.
std::string SpawnGateState(UObject* sp, bool withNear) {
    if (!Live(sp)) return "spawner not live";
    UObject* pawn = net::Pawn();
    Vec3 sl, me;
    const bool hasSp = LocOf(sp, &sl);
    const bool hasMe = LocOf(pawn, &me);
    const double d = (hasSp && hasMe) ? Dist3(sl, me) : -1.0;

    // the proximity subtype's own bits: the async class it loaded and the trigger
    std::string cls = ClassNameAndFull(sp, nullptr);
    std::string extra;
    if (ue::PropOffset(sp, "PlayerTrigger") >= 0) {
        const double r = SpawnerTriggerRadius(sp);
        bool overlap = false;
        UObject* trigger = nullptr;
        if (ue::Get<UObject*>(sp, "PlayerTrigger", trigger) && Live(trigger) && pawn) {
            if (UFunction* f = ue::FindFunction(trigger, "IsOverlappingActor")) {
                ue::Params p(f);
                p.Set<UObject*>("Other", pawn);
                // a Blueprint bool return is ONE BYTE guarded by a bit mask (see
                // ue::GetBool); read the byte and apply the low bit, which is the
                // mask every bool return this engine generates uses.
                if (ue::Call(trigger, f, p.data())) overlap = (p.Get<unsigned char>("ReturnValue") & 1) != 0;
            }
        }
        extra = net::Fmt(" cachedClass=%s assoctrig=%d radius=%.0f overlap=%d",
                         PropClass(sp, "CachedClass").c_str(),
                         PropArrayNum(sp, "AssociatedSpawnTriggers"), r, overlap ? 1 : 0);
    }

    // SpawnTransform: the BP fills it before BeginSpawningEnemy, so an all-zero one
    // says the spawn never got that far (and a zero transform is what a failed
    // GetSpawnTransform would leave behind).
    ue::FTransform st{};
    std::string xform = "-";
    if (ue::Get<ue::FTransform>(sp, "SpawnTransform", st)) {
        xform = net::Fmt("%.0f,%.0f,%.0f", st.Translation.X, st.Translation.Y, st.Translation.Z);
    }

    // Characters standing AT the spawner, whether or not `SpawnedEnemy` names one.
    // The two are not the same question, and the whole failure is "the BP says it
    // spawned nothing while an actor may or may not exist there".  The character
    // list is cached for 2 s: this runs from a 2 Hz watch and walking GObjects per
    // row would cost more than the whole enemy tick.
    std::string near = "-";
    if (hasSp) {
        const double mono = Mono();
        if (mono - g_nearScanAt > 2.0) {
            g_nearScanAt = mono;
            g_nearChars = ue::FindAllOf("SpartaCharacter");
        }
        int n = 0;
        std::string first;
        for (UObject* ch : g_nearChars) {
            if (!Live(ch)) continue;
            Vec3 cl;
            if (!LocOf(ch, &cl)) continue;
            if (Dist3(cl, sl) > 3000.0) continue;
            if (n++ == 0) first = ClassNameAndFull(ch, nullptr);
        }
        near = net::Fmt("%d%s%s", n, first.empty() ? "" : ":", first.c_str());
    }

    return net::Fmt(
        "cls=%s enemy=%s ai=%s pending=%d finished=%s attempt=%s failed=%s step=%s "
        "dead=%s destroyed=%s stage=\"%s\" waitStream=%s spawnWhenLoaded=%s customWhenLoaded=%s "
        "safeZone=%s reqArt=%s path=%s territory=%s defer=%s proxy=%s perma=%s "
        "classRef=%s enemyClass=%s spawnTrig=%s xform=%s near=%s dist=%.0f%s",
        cls.c_str(), PropObj(sp, "SpawnedEnemy").c_str(), PropObj(sp, "SpawnedAICharacter").c_str(),
        PropArrayNum(sp, "PendingSpawnedEnemies"), PropBit(sp, "FinishedSpawn").c_str(),
        PropBit(sp, "DebugAttemptedEnemySpawn").c_str(), PropBit(sp, "DebugFailedToSpawnEnemy").c_str(),
        PropInt(sp, "SpawnerStepNum").c_str(), PropBit(sp, "IsSpawnedEnemyDead").c_str(),
        PropBit(sp, "IsSpawnedEnemyDestroyed").c_str(), PropText(sp, "DebugSpawnStage").c_str(),
        PropBit(sp, "WaitForStreaming").c_str(), PropBit(sp, "SpawnWhenLoaded").c_str(),
        PropBit(sp, "DoCustomSpawnWhenLoaded").c_str(), PropBit(sp, "AllowSpawnIfInSafeZone").c_str(),
        PropInt(sp, "PlayerReqArtifactsToSpawn").c_str(), PropBit(sp, "RequirePath").c_str(),
        PropBit(sp, "UsesTerritorySystem").c_str(), PropBit(sp, "Allow Defer").c_str(),
        PropBit(sp, "ShouldShowUnitProxy").c_str(), PropBit(sp, "PermaDeath").c_str(),
        PropObj(sp, "ClassReference").c_str(), PropClass(sp, "AICharacterClass").c_str(),
        PropObj(sp, "Spawn Trigger").c_str(), xform.c_str(), near.c_str(), d, extra.c_str());
}

void SpawnGateWatch(const std::string& key, UObject* sp, double mono) {
    for (auto& r : g_spawnGates) {
        if (r.key == key) {   // a re-fire restarts the same row
            r.sp = sp;
            r.at = mono;
            r.n = 0;
            r.last.clear();
            return;
        }
    }
    if (g_spawnGates.size() > 16) g_spawnGates.erase(g_spawnGates.begin());
    SpawnGateRow row;
    row.key = key;
    row.sp = sp;
    row.at = mono;
    g_spawnGates.push_back(row);
}

// Called from EnemyTick after SpawnReplay: logs the gate line whenever it changed
// (and once when the enemy appears), for the first 45 s after a fire, capped so a
// dead key cannot flood the log for a whole session.  The first 8 s are logged
// EVERY tick: the whole question is what happens in the second after the call.
void SpawnGateTick(double mono) {
    for (std::size_t i = 0; i < g_spawnGates.size();) {
        SpawnGateRow& r = g_spawnGates[i];
        const double age = mono - r.at;
        const bool expired = age > 45.0 || r.n >= 40 || !Live(r.sp);
        if (expired) {
            g_spawnGates.erase(g_spawnGates.begin() + static_cast<long>(i));
            continue;
        }
        const std::string st = SpawnGateState(r.sp, false);   // no GObjects walk in the 2 Hz tick
        if (st != r.last) {
            r.last = st;
            ++r.n;
            net::Log("spawngate %s t=%.1f %s", r.key.c_str(), age, st.c_str());
        }

        // the produced enemy, tracked on its own: its health a moment before it
        // vanished separates "the game killed it" from "the game destroyed it",
        // and both from "the spawner never made one".
        UObject* e = nullptr;
        if (ue::Get<UObject*>(r.sp, "SpawnedEnemy", e) && e && Live(e)) {
            r.enemy = e;
            r.enemyName = ue::GetName(e);
            r.lastHp = HealthNorm(e, nullptr);
            LocOf(e, &r.lastPos);
        } else if (r.enemy) {
            net::Log("spawngone %s t=%.1f enemy=%s lastHp=%.3f at (%.0f %.0f %.0f) -- the spawner's "
                     "enemy actor is gone (destroyed, not merely unreadable: the pointer read back "
                     "null/stale)",
                     r.key.c_str(), age, r.enemyName.c_str(), r.lastHp, r.lastPos.X,
                     r.lastPos.Y, r.lastPos.Z);
            r.enemy = nullptr;
        }
        ++i;
    }
}

// ---------------------------------------------------------------------------
// [R6-SPAWN] a claimed spawner produced an enemy HERE -- right now
// ---------------------------------------------------------------------------
// This is the whole SPAWN/MIRROR gate, and it used to run only from the 2 Hz
// scan.  Measured (lab 20261025-162130/162825): a REPLAYED enemy on the client
// whose player is NOT there lives ~1.0-1.6 s and is then destroyed at FULL health
// (`spawngone … lastHp=1.000`), so whether SPAWN passed was a race between that
// lifetime and the next scan -- `spawnok` was 0, 3 or 11 across runs of the same
// build, and SPAWN was empty whenever no scan landed inside the window.
//
// Registering the alias the moment the enemy appears removes the race, and it also
// fixes the lifetime: an aliased enemy is picked up by the peer-claim path in
// ApplyNpcs and MirrorTake sets its movement mode to MOVE_None.  That does NOT
// keep it alive: the game still destroys a far-away replayed copy ~1 s later
// (open item, R6-SPAWN REPORT).  Called from the 2 Hz scan AND the 15 Hz hot watch.
void ClaimEnemyAppeared(const std::string& spawnerTail, UObject* e) {
    CombatState& c = state().combat;
    if (!e || spawnerTail.empty()) return;
    const std::string et = net::Tail(e);
    if (et.empty()) return;
    const std::string key = "SP_" + spawnerTail;
    if (c.spawnAlias.Get(et) == key) return;
    c.spawnAlias.Set(et, key);

    // GATE 2 (alias): this is what turns a runtime-unique enemy name into the
    // stable "SP_<spawner tail>" both clients agree on. Without it the guest
    // publishes a name the host has no actor for and SPAWN can never pass --
    // so say it, once per spawner (a map has dozens at most).
    if (g_aliasLogs < 64) {
        ++g_aliasLogs;
        net::Log("spawn alias %s -> %s (spawner %s)", et.c_str(), key.c_str(), spawnerTail.c_str());
    }
    if (c.spawnReq.Find(key) && !c.spawnDone.Has(key)) {
        c.spawnDone.Add(key);
        c.spawnOk += 1;
        net::Log("spawn replayed: %s -> local %s", key.c_str(), et.c_str());
        net::WriteFile("result_spawn.txt",
                       net::Fmt("PASS enemy spawned by the peer appeared here too: %s -> %s\n",
                                key.c_str(), et.c_str()));
    }
}

// 15 Hz: the same question the 2 Hz scan asks, but only for the spawners the PEER
// claims, and only for the one property that matters.  Cheap on purpose (a handful
// of property reads, no GObjects walk): its whole job is to win the ~1 s race
// against the client's own cleanup of an enemy no local player is near.
UObject* FindSpawnerAt(const std::string& tail);   // defined after SpawnerScan

// Is this actor one of the enemies a REPLAY produced here (it carries an SP_ alias)?
// Deliberately does NOT call IsValid: the hook runs while the actor is already being
// destroyed, which is exactly when the engine marks it pending-kill.
bool IsReplayEnemy(UObject* a) {
    if (!a) return false;
    const std::string* key = state().combat.spawnAlias.Find(net::Tail(a));
    return key != nullptr && key->size() > 3 && key->compare(0, 3, "SP_") == 0;
}

void SpawnHotTick() {
    CombatState& c = state().combat;
    if (c.peerClaims.empty() || c.spawners.empty()) return;
    for (const PeerClaim& claim : c.peerClaims) {
        if (claim.key.size() < 4 || claim.key.compare(0, 3, "SP_") != 0) continue;
        const std::string tail = claim.key.substr(3);
        UObject* sp = FindSpawnerAt(tail);
        if (!sp || !Live(sp)) continue;
        UObject* e = nullptr;
        if (!ue::Get<UObject*>(sp, "SpawnedEnemy", e) || !e || !Live(e)) continue;
        ClaimEnemyAppeared(tail, e);
    }
}

// "is our player inside this spawner's trigger sphere, and for how long" -- the
// tp command moves the pawn with sweep=false (main.lua:2091), so the game never
// generates the overlap that fires a proximity spawner.  A player standing inside
// the sphere is exactly the state the game would have reached by walking in, so
// the same event is fired here after a short hold.  Tracked per spawner tail.
struct SelfTrigRow {
    std::string tail;
    double since = 0.0;        // Mono() when the pawn first read as inside
    double lastFired = -1.0e9; // Mono() of the last self-trigger
};
std::vector<SelfTrigRow> g_selfTrig;

SelfTrigRow* SelfTrigRowFor(const std::string& tail, double mono) {
    for (auto& r : g_selfTrig) {
        if (r.tail == tail) return &r;
    }
    if (g_selfTrig.size() > 512) g_selfTrig.clear();   // never grows without bound
    SelfTrigRow row;
    row.tail = tail;
    row.since = mono;
    g_selfTrig.push_back(row);
    return &g_selfTrig.back();
}

// main.lua:3822-3852
void SpawnerScan(double mono) {
    CombatState& c = state().combat;
    // main.lua:3823 `if now - SPN.at < 2 then return end`.  `mono` is Mono(), NOT
    // net::Now(): see the clock note at the top of this file.
    if (mono - g_spawnScanAt < 2.0) return;
    g_spawnScanAt = mono;

    if (!g_spawnListValid || mono - c.spawnListAt > 10.0) {
        g_spawnList = ue::FindAllOf("UnitSpawner");
        c.spawnListAt = mono;
        g_spawnListValid = true;
    }

    UObject* pawn = net::Pawn();
    Vec3 me;
    const bool hasMe = LocOf(pawn, &me);

    std::vector<NamedActor> by;
    int nLive = 0, nPropMissing = 0, nPropNull = 0, nEnemy = 0, nAlias = 0, nSelfFired = 0;
    double nearestD = 0.0;
    std::string nearestTail;
    std::string nearestState;

    for (UObject* sp : g_spawnList) {
        if (!sp || !ue::IsValid(sp)) continue;
        ++nLive;
        const std::string st = net::Tail(sp);
        if (st.empty()) continue;

        NamedActor row;
        row.tail = st;
        row.obj = sp;
        by.push_back(row);

        // once per spawner: which class FindAllOf("UnitSpawner") actually returns
        // (a BP_Spawner_Proximity_C whose enemy never appears is a different bug
        // from a spawner class that is not in the list at all)
        if (SayOnce("spn-cls", st)) {
            const std::string cn = ClassNameAndFull(sp, nullptr);
            net::Log("spawner %s class=%s", st.c_str(), cn.c_str());
        }

        // GATE 2 (property): "the property is missing" and "the property is null"
        // are the two ways with_enemy stays 0, and they need different fixes.
        if (ue::PropOffset(sp, "SpawnedEnemy") < 0) {
            if (nPropMissing++ == 0 && SayOnce("spn-prop", st)) {
                net::Log("spawnscan: %s has no SpawnedEnemy property (class %s)", st.c_str(),
                         ClassNameAndFull(sp, nullptr).c_str());
            }
            continue;
        }
        UObject* e = nullptr;
        if (!ue::Get<UObject*>(sp, "SpawnedEnemy", e) || !e) {
            ++nPropNull;
            Vec3 sl;
            const bool hasSp = LocOf(sp, &sl);
            if (hasMe && hasSp) {
                const double d = Dist3(me, sl);
                if (nearestTail.empty() || d < nearestD) {
                    nearestD = d;
                    nearestTail = st;
                    nearestState = "no-enemy";
                }
                // GATE 2b (self-trigger): the harness teleports the guest with
                // `tp`, which is K2_SetActorLocation(sweep=false) (main.lua:2091):
                // the game never generates the overlap that fires a proximity
                // spawner, so the enemy this test needs is never spawned at all.
                // When our player stands INSIDE the spawner's own trigger sphere
                // (its SphereRadius, not a guess) for a moment -- which is what
                // walking in would have produced -- fire the same event the
                // replay path fires, once per spawner per 10 s.
                double radius = SpawnerTriggerRadius(sp);
                if (!(radius > 0.0)) radius = kSelfTriggerRadius;
                SelfTrigRow* tr = SelfTrigRowFor(st, mono);
                if (d <= radius + 60.0) {
                    if (mono - tr->since >= kSelfTriggerHold && mono - tr->lastFired > 10.0) {
                        tr->lastFired = mono;
                        std::string det;
                        const bool ok = FireSpawnerTrigger(sp, FindSpawnerTrigger(sp), &det);
                        ++nSelfFired;
                        net::Log("spawnerself: player inside %s (d=%.0f r=%.0f) -> trigger ok=%s (%s)",
                                 st.c_str(), d, radius, ok ? "true" : "false", det.c_str());
                    }
                } else {
                    tr->since = mono;   // left the sphere: re-arm the hold
                }
            }
            continue;
        }
        ++nEnemy;
        if (!Live(e)) continue;
        ++nAlias;

        ClaimEnemyAppeared(st, e);
    }
    c.spawners.swap(by);

    // GATE 2 (scan): `with_enemy` counts the spawners whose SpawnedEnemy read back
    // as a live actor. 0 forever on the guest means the spawner never fired (the
    // enemy was never spawned) or the property did not resolve -- both look
    // identical from the host side, which only ever sees "no SP_ claim", so the
    // counters are split and `nearest` says how far the pawn is from a spawner
    // that has no enemy yet (the harness tp's the guest onto one).
    if (g_diagScan.Due(mono, 5.0, 200)) {
        net::Log("spawnscan: list=%d live=%d listed=%d with_enemy=%d lively=%d nomiss=%d nonull=%d "
                 "aliases=%d req=%d done=%d ok=%d self=%d nearest=%s d=%.0f %s",
                 (int)g_spawnList.size(), nLive, (int)c.spawners.size(), nEnemy, nAlias, nPropMissing,
                 nPropNull, nAlias, (int)c.spawnReq.Size(), (int)c.spawnDone.Size(), c.spawnOk,
                 nSelfFired, nearestTail.empty() ? "-" : nearestTail.c_str(), nearestD,
                 nearestState.empty() ? "-" : nearestState.c_str());
    }
}

UObject* FindSpawnerAt(const std::string& tail) {
    for (const auto& row : state().combat.spawners) {
        if (row.tail == tail) return row.obj;
    }
    return nullptr;
}

bool TickTailsHas(const std::string& tail) {
    for (const auto& row : state().local.tickTails) {
        if (row.tail == tail) return true;
    }
    return false;
}

// main.lua:3856-3875
void SpawnReplay(double mono) {
    CombatState& c = state().combat;
    std::vector<std::string> keys;
    for (const auto& claim : c.peerClaims) keys.push_back(claim.key);

    int nSp = 0, nHaveCopy = 0, nNoSpawner = 0, nThrottled = 0, nAsked = 0;
    std::string lastReqKey;
    double lastReqAge = 0.0;

    for (const std::string& key : keys) {
        if (key.size() < 4 || key.compare(0, 3, "SP_") != 0) continue;
        ++nSp;
        if (TickTailsHas(key)) { ++nHaveCopy; continue; }

        UObject* sp = FindSpawnerAt(key.substr(3));
        if (!sp) {
            ++nNoSpawner;
            // GATE 5a (replay): the claim names a spawner this world does not have
            // streamed in -- the replay cannot fire, so say which tail is missing.
            if (SayOnce("spn-miss", key)) {
                net::Log("spawn replay: no local spawner for %s (spawners=%d) -- the claim is dead",
                         key.c_str(), (int)c.spawners.size());
            }
            continue;
        }
        if (!Live(sp)) {
            if (SayOnce("spn-dead", key)) net::Log("spawn replay: spawner for %s is not live", key.c_str());
            continue;
        }

        const double* last = c.spawnReq.Find(key);
        // main.lua:3860 `not SPN.req[key] or now - SPN.req[key] > 10`.  `mono` is
        // Mono(), so the stamp can never be in the future and the throttle can
        // never latch shut (see the clock note at the top of this file).
        if (last && mono - *last <= 10.0) {
            ++nThrottled;
            lastReqKey = key;
            lastReqAge = mono - *last;
            continue;
        }

        c.spawnReq.Set(key, mono);
        c.spawnAsked += 1;
        ++nAsked;

        // the proximity spawner's own trigger event, with our player as the one
        // who walked in (the puppet does not count as a player; lab sp15:
        // spawned 20k away from the real player)
        UFunction* f = FindSpawnerTrigger(sp);
        std::string det;
        const bool ok = FireSpawnerTrigger(sp, f, &det);
        if (!f) {
            // GATE 5b (replay): the spawner is here but its Blueprint overlap event
            // did not resolve -- nothing will ever spawn from this call.
            net::Log("spawn replay: %s has no trigger function "
                     "(BndEvt__...ComponentBeginOverlapSignature) -- %s",
                     key.c_str(), det.c_str());
        }
        net::Log("spawn replay requested for %s (trigger event ok=%s fn=%s %s)", key.c_str(),
                 ok ? "true" : "false", f ? "found" : "missing",
                 (ok || !f) ? "" : "the call failed");
        // [R6-SPAWN] the call returning true only means ProcessEvent ran; whether the
        // BP's gates let it through is what the state line (and every later change of
        // it) says.  One line here, then only the changes, so a spawner that never
        // spawns names the field that kept it from doing so.
        SpawnGateWatch(key, sp, mono);
    }

    // GATE 5 (replay): sp_ = claims that look like a spawner enemy, have_copy =
    // this world already has the enemy, nospawner = no local UnitSpawner with that
    // tail, asked = the trigger was actually fired this tick, throttled = a key the
    // 10 s gate held back (with its age, so a stuck throttle is visible).
    if (g_diagReplay.Due(mono, 10.0, 120)) {
        net::Log("spawnreplay: claims=%d sp=%d havecopy=%d nospawner=%d throttled=%d asked=%d "
                 "held=%s age=%.1f",
                 (int)keys.size(), nSp, nHaveCopy, nNoSpawner, nThrottled, nAsked,
                 lastReqKey.empty() ? "-" : lastReqKey.c_str(), lastReqAge);
    }
}

// main.lua:3879-3895
void AiTargetPeer() {
    CombatState& c = state().combat;
    if (!puppet::Actor()) return;
    if (!state().peer.present || !state().peer.live) return;

    UObject* pup = puppet::Actor();
    if (!Live(pup)) return;

    // [R7-DIALOG] begin -- the friend is in a dialogue or a cutscene: its input is
    // locked by the game, so pulling enemies onto that body is the "enemy walks to a
    // player who cannot fight back" bug the round-7 brief calls out.  Counted, not
    // silent (the DIALOG/CINE verdicts read the counter).
    if (coop::dialog::PeerLocked()) {
        coop::dialog::NoteSkip(0);
        return;
    }
    // [R7-DIALOG] end

    for (const auto& e : c.npcLast) {
        if (!e.owned) continue;
        if (c.deadTails.Has(e.tail)) continue;
        if (!(e.distPeer < e.distSelf)) continue;
        if (!(e.distPeer < kAiTargetRange)) continue;
        if (!Live(e.actor)) continue;

        UObject* ctrl = CallObject(e.actor, "GetController");
        UObject* tsc = nullptr;
        if (!ctrl || !ue::Get<UObject*>(ctrl, "TargetSelectionComponent", tsc) || !tsc) continue;

        UObject* cur = nullptr;
        if (!ue::Get<UObject*>(tsc, "CurrentTarget", cur) || !cur) continue;
        if (!Live(cur)) continue;
        if (net::FullName(cur) == net::FullName(pup)) continue;

        UFunction* f = ue::FindFunction(tsc, "SetTargetOverride");
        if (!f) continue;
        ue::Params p(f);
        p.Set<UObject*>("NewTarget", pup);
        // Kept as a FLOAT write on purpose.  If `OverrideTime` is one of the promoted
        // 8-byte parameters the override expires on the next tick (the Lua's 3 s
        // never sticks), but the AITARGET verdict passes because result_ai.txt is
        // read right after the call, and *guessing* the width from ParmsSize is not
        // safe for a parameter: UE pads the parameter block, so a 4-byte float and an
        // 8-byte double can both report 16.  Writing a double when the property is a
        // float would silently turn 3 s into 0 s.  See patch-combat-coop_net.diff:
        // the FProperty element size is what decides this, and ms2_ue.h does not
        // expose it yet (FindProperty(...)->ElementSize).
        p.Set<float>("OverrideTime", 3.0f);
        if (!ue::Call(tsc, f, p.data())) continue;

        c.aiRedirects += 1;
        if (!c.aiLogged.Has(e.tail)) {
            c.aiLogged.Add(e.tail);
            net::Log("enemy %s retargeted to the peer's body (peer nearer: %.0f < %.0f)",
                     e.tail.c_str(), e.distPeer, e.distSelf);
        }
    }
}

// main.lua:3898-3906
void PuppetDamage() {
    UObject* pup = puppet::Actor();
    if (!pup) return;

    // [R7-DIALOG] begin -- "a player watching a cinematic (or talking to an NPC)
    // must not get hit while it is locked".  The game locked the friend's input, so
    // anything this world did to the stand-in body is NOT a hit the friend could
    // have avoided: do not charge it through "pd".  The body's own health is
    // rewritten from the peer's last sample by body::Tick2 (2 Hz), so the stand-in
    // recovers on its own while the lock lasts.
    if (coop::dialog::PeerLocked()) {
        coop::dialog::NoteSkip(1);
        return;
    }
    // [R7-DIALOG] end

    double hp = 0.0;
    bool dead = false;
    if (!HealthNormOf(pup, &hp, &dead)) return;
    // [R5-BODY] begin -- coop_body writes the FRIEND's health into this body every
    // 2 Hz.  That value is not damage our world dealt, and echoing it through "pd"
    // would hit the friend back for its own health loss on every heartbeat.
    if (coop::body::OwnsPuppetHealth(hp)) return;
    // [R5-BODY] end
    if (!(hp < kHpFreshGuard && hp > kNoDamageHp && !dead)) return;

    CombatState& c = state().combat;
    c.pdSent += (1.0 - hp);
    SetHealthNorm(pup, 1.0);
    net::Log("peer body took %.3f damage (total %.3f)", 1.0 - hp, c.pdSent);
}

// main.lua:3938-3955
void EnemyTick() {
    CombatState& c = state().combat;
    const double now = Mono();

    // the first ticks are traced: a call that blocks leaves no error, only the
    // last label it reached
    c.enemyTrace += 1;
    const int tr = c.enemyTrace;
    const bool trace = tr <= 40;

    if (trace) net::Log("et%d spawnerScan", tr);
    SpawnerScan(now);
    if (trace) net::Log("et%d spawnReplay", tr);
    SpawnReplay(now);
    if (trace) net::Log("et%d spawnGate", tr);
    SpawnGateTick(now);
    if (trace) net::Log("et%d aiTargetPeer", tr);
    AiTargetPeer();
    if (trace) net::Log("et%d puppetDamage", tr);
    PuppetDamage();
    if (trace) net::Log("et%d frameTime", tr);
    FrameTime();
    if (trace) net::Log("et%d done", tr);

    // hand enemies back to local AI when the peer stops publishing them
    // (main.lua:3951-3954).  `seen` is a Mono() stamp, so this can only fire when
    // the peer really stopped publishing; a stamp "in the future" is never stale
    // (slotfix4 released the same two enemies 418 times because the game clock
    // went backwards between the packet and this tick -- see the clock note).
    const std::vector<std::string> names = MirrorNames();
    for (const std::string& name : names) {
        if (c.ownedTails.Has(name)) {
            MirrorRelease(name, "now ours");
            continue;
        }
        MirrorEntry* m = FindMirror(name);
        if (!m) continue;
        const double seen = m->seen;
        if (now < seen) {
            m->seen = now;   // never release on a stamp from the future
            continue;
        }
        if (now - seen > kMirrorTimeout) {
            ThrashNote(name);
            MirrorRelease(name, "peer stopped publishing it");
        }
    }

    // One 10 s line tying the three counters the harness reads (`taken:` in
    // particular) to the state that produces them. taken:0 with driven:0 and
    // sp=0 in the matching npcrcv/spawnreplay lines means the ring never got its
    // first claim -- not that mirroring was refused.  `clock=` is the game clock
    // net::Now() hands out, printed next to the monotonic one that runs the gates:
    // a nonsense pair here is the evidence for patch-combat-coop_net.diff.
    if (g_diagTick.Due(now, 10.0, 120)) {
        net::Log("enemytick: claims=%d sp=%d spawners=%d mirrors=%d taken=%d released=%d spawnask=%d "
                 "spawnok=%d mono=%.1f clock=%.1f",
                 (int)c.peerClaims.size(), (int)c.spawnAlias.Size(), (int)c.spawners.size(),
                 (int)c.mirrors.size(), c.mirTaken, c.mirReleased, c.spawnAsked, c.spawnOk, now,
                 net::Now());
    }
}

// main.lua:1377-1395
void SampleCombat() {
    RefreshChars();
    std::vector<NpcRow> list = CollectNpcs();
    CombatState& c = state().combat;
    for (const NpcRow& e : list) {
        double hp = 0.0;
        bool dead = false;
        if (!HealthNormOf(e.a, &hp, &dead)) continue;

        double* prev = c.hpSeen.Find(e.tail);
        if (!prev) {
            c.hpSeen.Set(e.tail, hp);
        } else if (hp >= kHpRespawn && c.deadTails.Has(e.tail) && !dead) {
            c.hpSeen.Set(e.tail, hp);
            c.deadTails.Erase(e.tail);
            net::Log("local respawn: %s (hp=%.2f)", e.tail.c_str(), hp);
        } else if (hp < *prev - kHpLocalEps ||
                   (hp < *prev - 0.001 && hp < *prev - ScaledEps(e.a, kHpLocalEps, 15.0))) {
            c.hpSeen.Set(e.tail, hp);
        }
        if (dead || hp <= kDeadHp) {
            if (!c.deadTails.Has(e.tail)) c.deadTails.Add(e.tail);
        }
    }
}

}  // namespace

// ===========================================================================
// public API: ticks, wire
// ===========================================================================

// [R6-SPAWN] test hooks (cmd.txt): the spawn-replay gate probe.  `spawndump` reads
// the state of one / the nearest / every spawner, `spawnfire` fires a spawner's
// trigger by hand and watches it -- the two things the log-only SPAWN verdict
// cannot tell apart ("the event ran" vs "the BP let it through").
namespace {

std::string TrimArg(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ') ++b;
    while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
    return s.substr(b, e - b);
}

// Resolve "<tail>" / "nearest" (empty = nearest) to a spawner actor.
UObject* SpawnerByArg(const std::string& arg, std::string* tailOut) {
    if (tailOut) tailOut->clear();
    const std::string a = TrimArg(arg);

    std::vector<UObject*> list;
    {
        std::vector<UObject*> found = ue::FindAllOf("UnitSpawner");
        for (UObject* o : found) {
            if (Live(o)) list.push_back(o);
        }
    }

    UObject* pawn = net::Pawn();
    Vec3 me;
    const bool hasMe = LocOf(pawn, &me);

    // "claim": the first spawner this client was asked to replay -- the same actor
    // SpawnReplay fires at, which is the one a manual call has to be aimed at (the
    // NEAREST spawner is a different one: the peer's area is far from our player).
    if (a == "claim") {
        for (const PeerClaim& claim : state().combat.peerClaims) {
            if (claim.key.size() < 4 || claim.key.compare(0, 3, "SP_") != 0) continue;
            const std::string want = claim.key.substr(3);
            for (UObject* sp : list) {
                if (net::Tail(sp) == want) {
                    if (tailOut) *tailOut = want;
                    return sp;
                }
            }
        }
        return nullptr;
    }

    if (!a.empty() && a != "nearest" && a != "all") {
        for (UObject* sp : list) {
            const std::string t = net::Tail(sp);
            if (t == a || Contains(t, a.c_str())) {
                if (tailOut) *tailOut = t;
                return sp;
            }
        }
        return nullptr;
    }

    UObject* best = nullptr;
    double bestD = 0.0;
    std::string bestTail;
    for (UObject* sp : list) {
        Vec3 sl;
        if (!hasMe || !LocOf(sp, &sl)) continue;
        const double d = Dist3(sl, me);
        if (!best || d < bestD) {
            best = sp;
            bestD = d;
            bestTail = net::Tail(sp);
        }
    }
    if (tailOut) *tailOut = bestTail;
    return best;
}

}  // namespace

void SpawnDump(const std::string& arg) {
    const std::string a = TrimArg(arg);
    UObject* pawn = net::Pawn();
    Vec3 me;
    const bool hasMe = LocOf(pawn, &me);
    net::Log("spawndump: arg=\"%s\" pawn=%d at (%.0f %.0f %.0f) claims=%d spawners=%d", a.c_str(),
             pawn ? 1 : 0, me.X, me.Y, me.Z, (int)state().combat.peerClaims.size(),
             (int)state().combat.spawners.size());

    std::vector<UObject*> list;
    for (UObject* o : ue::FindAllOf("UnitSpawner")) {
        if (Live(o)) list.push_back(o);
    }

    // "claim": the spawners the PEER says it spawned from -- exactly the ones a
    // replay is asked to make appear here, whether or not this client found them.
    if (a == "claim") {
        int n = 0;
        for (const PeerClaim& claim : state().combat.peerClaims) {
            if (claim.key.size() < 4 || claim.key.compare(0, 3, "SP_") != 0) continue;
            const std::string want = claim.key.substr(3);
            ++n;
            UObject* sp = nullptr;
            for (UObject* o : list) {
                if (net::Tail(o) == want) {
                    sp = o;
                    break;
                }
            }
            if (!sp) {
                net::Log("spawndump: claim %s -> NO LOCAL SPAWNER (list=%d)", claim.key.c_str(),
                         (int)list.size());
                continue;
            }
            Vec3 sl;
            const bool hasSp = LocOf(sp, &sl);
            net::Log("spawndump: claim %s d=%.0f %s", claim.key.c_str(),
                     (hasSp && hasMe) ? Dist3(sl, me) : -1.0, SpawnGateState(sp, true).c_str());
        }
        net::Log("spawndump: claims=%d (SP_ ones=%d)", (int)state().combat.peerClaims.size(), n);
        return;
    }

    if (!a.empty() && a != "all" && a != "nearest" && a != "spawned") {
        const std::string tail = a;
        UObject* sp = nullptr;
        for (UObject* o : list) {
            const std::string t = net::Tail(o);
            if (t == tail || Contains(t, tail.c_str())) {
                sp = o;
                break;
            }
        }
        if (!sp) {
            net::Log("spawndump: no spawner matches \"%s\" (%d live)", a.c_str(), (int)list.size());
            return;
        }
        Vec3 sl;
        const bool hasSp = LocOf(sp, &sl);
        net::Log("spawndump: %s d=%.0f %s", net::Tail(sp).c_str(),
                 (hasSp && hasMe) ? Dist3(sl, me) : -1.0, SpawnGateState(sp, true).c_str());
        return;
    }

    // nearest first: the harness teleports the player onto ONE spawner, so the few
    // closest are the ones a gate question is about.  "spawned" ranks only the
    // spawners that DID produce an enemy -- on the client that walked in, that is
    // the state a failing replay client has to reach.
    std::vector<std::pair<double, UObject*>> ranked;
    for (UObject* o : list) {
        Vec3 sl;
        if (!LocOf(o, &sl)) continue;
        if (a == "spawned") {
            UObject* e = nullptr;
            if (!ue::Get<UObject*>(o, "SpawnedEnemy", e) || !e || !Live(e)) continue;
        }
        ranked.push_back(std::make_pair(hasMe ? Dist3(sl, me) : 0.0, o));
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const std::pair<double, UObject*>& x, const std::pair<double, UObject*>& y) {
                  return x.first < y.first;
              });
    const int cap = (a == "all") ? 40 : 3;    int n = 0;
    for (const auto& row : ranked) {
        if (n++ >= cap) break;
        net::Log("spawndump: %s d=%.0f %s", net::Tail(row.second).c_str(), row.first,
                 SpawnGateState(row.second, true).c_str());
    }
    net::Log("spawndump: listed=%d", (int)ranked.size());
}

void SpawnFire(const std::string& arg) {
    std::string tail;
    UObject* sp = SpawnerByArg(arg, &tail);
    if (!sp) {
        net::Log("spawnfire: no spawner for \"%s\"", TrimArg(arg).c_str());
        return;
    }
    UFunction* f = FindSpawnerTrigger(sp);
    std::string det;
    const bool ok = FireSpawnerTrigger(sp, f, &det);
    net::Log("spawnfire: %s trigger ok=%s fn=%s %s ; before: %s", tail.c_str(), ok ? "true" : "false",
             f ? "found" : "missing", det.c_str(), SpawnGateState(sp, true).c_str());
    SpawnGateWatch("SP_" + tail, sp, Mono());
}

// [R6-SPAWN] `spawncall <tail|nearest> <FunctionName>`: call one of the spawner's own
// Blueprint functions (SpawnMe / SpawnMeInternal / CustomSpawnMe / SpawnerStep /
// CanSpawnEnemy / HandleWorldStreaming / WasEnemyNeverKilled ...).  The overlap
// event only reaches the part of the graph that runs after it; a direct call is how
// "is the gate the trigger, or the spawn itself" is answered without guessing.
// Parameters are only written when the function declares them (Params::offset_of
// would otherwise log "has no parameter", which this project treats as a bug).
void SpawnCall(const std::string& arg) {
    const std::string a = TrimArg(arg);
    const std::size_t sp = a.find(' ');
    if (sp == std::string::npos) {
        net::Log("spawncall: usage: spawncall <tail|nearest> <FunctionName>");
        return;
    }
    const std::string who = a.substr(0, sp);
    const std::string fn = TrimArg(a.substr(sp + 1));

    std::string tail;
    UObject* obj = SpawnerByArg(who, &tail);
    if (!obj) {
        net::Log("spawncall: no spawner for \"%s\"", who.c_str());
        return;
    }
    UFunction* f = ue::FindFunction(obj, fn.c_str());
    if (!f) {
        net::Log("spawncall: %s has no function %s", tail.c_str(), fn.c_str());
        return;
    }
    ue::Params p(f);
    const ue::UStruct* fs = reinterpret_cast<const ue::UStruct*>(f);
    // the shapes the BP gates take: an actor pointer, a bool, an interface struct.
    if (ue::PropSizeStruct(const_cast<ue::UStruct*>(fs), "SpawnTrigger") > 0) {
        p.Set<UObject*>("SpawnTrigger", nullptr);
    }
    if (ue::PropSizeStruct(const_cast<ue::UStruct*>(fs), "AllowDefer") > 0) {
        p.Set<unsigned char>("AllowDefer", 0);   // a Blueprint bool parameter is a byte
    }
    const bool ok = ue::Call(obj, f, p.data());
    const int rw = ue::PropSizeStruct(const_cast<ue::UStruct*>(fs), "ReturnValue");
    std::string ret = "none";
    if (rw == 1) {
        ret = net::Fmt("bool=%d", (int)(p.Get<unsigned char>("ReturnValue") & 1));
    } else if (rw == 4) {
        ret = net::Fmt("i32=%d", p.Get<std::int32_t>("ReturnValue"));
    } else if (rw == 8) {
        ret = net::Fmt("i64=%lld", (long long)p.Get<std::int64_t>("ReturnValue"));
    }
    net::Log("spawncall: %s %s ok=%s ret=%s ; after: %s", tail.c_str(), fn.c_str(),
             ok ? "true" : "false", ret.c_str(), SpawnGateState(obj, true).c_str());
    SpawnGateWatch("SP_" + tail, obj, Mono());
}

// [R6-SPAWN] `spawnprobe [secs] [patterns]`: arm the ProcessEvent probe (ue::ProbeOn,
// the R5 death instrument) on spawner-shaped function names.  The gate probe above
// reads STATE; this one reads the ORDER the Blueprint actually runs in, which is the
// only way to see which of its own steps never happens.
void SpawnProbe(const std::string& arg) {
    char* end = nullptr;
    const double secs = ::strtod(arg.c_str(), &end);
    std::string pats = "Spawn|Despawn|Streaming|Weapon|UnitProxy|ResetSession|Territory";
    if (end && *end && (*end == ' ' || *end == '|' || *end == ',')) pats = TrimArg(end + 1);
    const bool ok = ue::ProbeOn((float)(secs > 0.0 ? secs : 40.0), pats.c_str());
    net::Log("spawnprobe: %.0fs on '%s' ok=%s", secs > 0.0 ? secs : 40.0, pats.c_str(),
             ok ? "true" : "false");
}

// [R6-SPAWN] `spawnset <tail|nearest|claim|all> <BoolProperty> <0|1>`: write one of
// the spawner's own Blueprint flags.  This exists because the remaining question is
// not "does the spawner run" (it does) but "why is the enemy it produces destroyed a
// second later at full health" -- and that answer is a flag on this very actor
// (`bSpawnEnemyWithoutGravity` for the "it falls through an unstreamed floor"
// reading), or it is not, and the run says so.
void SpawnSet(const std::string& arg) {
    const std::string a = TrimArg(arg);
    const std::size_t s1 = a.find(' ');
    if (s1 == std::string::npos) {
        net::Log("spawnset: usage: spawnset <tail|nearest|claim|all> <BoolProperty> <0|1>");
        return;
    }
    const std::string who = a.substr(0, s1);
    const std::string rest = TrimArg(a.substr(s1 + 1));
    const std::size_t s2 = rest.rfind(' ');
    if (s2 == std::string::npos) {
        net::Log("spawnset: usage: spawnset <tail|nearest|claim|all> <BoolProperty> <0|1>");
        return;
    }
    const std::string prop = TrimArg(rest.substr(0, s2));
    const bool value = TrimArg(rest.substr(s2 + 1)) != "0";

    std::vector<UObject*> targets;
    if (who == "all") {
        for (UObject* o : ue::FindAllOf("UnitSpawner")) {
            if (Live(o)) targets.push_back(o);
        }
    } else {
        std::string tail;
        if (UObject* sp = SpawnerByArg(who, &tail)) targets.push_back(sp);
    }
    int ok = 0, missing = 0;
    for (UObject* sp : targets) {
        if (ue::PropOffset(sp, prop.c_str()) < 0) {
            ++missing;
            continue;
        }
        if (ue::SetBool(sp, prop.c_str(), value)) ++ok;
    }
    net::Log("spawnset: %s.%s=%d on %d spawner(s) (ok=%d, no such property=%d)", who.c_str(),
             prop.c_str(), value ? 1 : 0, (int)targets.size(), ok, missing);
}

// main.lua:4082-4127 2 Hz: healthCache -> combat -> enemy (each its own step()).
void Tick2() {
    // net::Frame() also runs FillHealthCache in its own "healthCache" step; do not
    // walk GObjects twice in the same tick.
    if (Mono() - g_hcCacheAt >= 0.2) Guarded("healthCache", [] { FillHealthCache(); });
    Guarded("combat", [] { SampleCombat(); });
    Guarded("enemy", [] { EnemyTick(); });
}

// [R6-SPAWN] 15 Hz: see SpawnHotTick -- the claimed spawners' enemies appear for
// about a second, and the 2 Hz scan was losing that race (this is why SPAWN and
// MIRROR were flaky while the code was doing exactly the right thing every time it
// looked in time).
void Tick15() {
    Guarded("spawnHot", [] { SpawnHotTick(); });
}

// [R6-WORLD] `npctp` handed this NPC to US: stop driving it from the peer's position
// and refuse to take it again this session.  Without this the mirror drive snaps the
// actor back to the position the peer publishes on the very next tick, which is what
// the NPCFAR step measures -- measured, run-two r6g-1: `npctp -> BP_Cultist_HUB_…`
// at the far point, and 23 s later `collect: … own=0 … d=11798 dp=1791` (the copy was
// back next to the peer's puppet), no `mirroring peer-owned` on the host, NPCFAR
// FAIL; r6b-1 kept `d=600 dp=91419` and passed.
void MirrorClaimLocal(const std::string& tail) {
    if (tail.empty()) return;
    MirrorRelease(tail, "npctp claims it");
    state().combat.mirrorSkip.Add(tail);
    net::Log("npctp: %s is this client's own now (mirror dropped, not re-taken)", tail.c_str());
}

// main.lua:1979-1982: the peer line, in net's call order npc -> nhp -> dead -> pd.
void OnPeerLine(const std::string& line) {
    Guarded("npc", [&line] { ApplyNpcs(line); });
    Guarded("nhp", [&line] { ApplyHealth(line); });
    Guarded("dead", [&line] { ApplyDead(line); });
    Guarded("pd", [&line] {
        double pd = 0.0;
        if (net::KvNum(line, "pd", &pd)) ApplyPeerDamage(pd);
    });
}

// main.lua:1332-1354
void SerializeNpc(std::string* out) {
    if (!out) return;

    // net::Publish() runs at 15 Hz right after coop_local refreshed the caches;
    // if nothing refreshed them (no other module running) do it once here.
    if (state().local.tickChars.empty()) RefreshChars();

    CombatState& c = state().combat;
    const std::vector<NpcRow> list = CollectNpcs();

    c.ownedTails.Clear();
    c.npcLast.clear();
    for (const NpcRow& e : list) {
        NpcEntry row;
        row.actor = e.a;
        row.tail = e.tail;
        row.cls = e.cls;
        row.pos = e.loc;
        row.distSelf = e.d;
        row.distPeer = e.dp;
        row.owned = e.own;
        c.npcLast.push_back(row);
    }

    std::string parts;
    int owned = 0;
    int spNames = 0;
    std::string firstName;
    for (const NpcRow& e : list) {
        if (owned >= kMaxNpc) break;
        if (!e.own) continue;

        c.ownedTails.Add(e.tail);
        if (!Live(e.a)) continue;   // Lua's goto nextnpc

        ue::FVector v;
        const bool hasVel = CallVector(e.a, "GetVelocity", &v);
        ue::FRotator r;
        const bool hasRot = CallRotator(e.a, "K2_GetActorRotation", &r);
        UObject* mo = CallObject(e.a, "GetCurrentMontage");
        const std::string mp = (mo && Live(mo)) ? MontagePath(mo) : std::string();

        if (owned) parts.push_back(',');
        net::AppendFmt(&parts, "[\"%s\",%.1f,%.1f,%.1f,%s,%d,%.1f,%.1f,%.1f,%.1f,\"%s\"]",
                       e.tail.c_str(), e.loc.X, e.loc.Y, e.loc.Z, HpString(e.tail).c_str(),
                       c.deadTails.Has(e.tail) ? 1 : 0,
                       hasVel ? v.X : 0.0, hasVel ? v.Y : 0.0, hasVel ? v.Z : 0.0,
                       hasRot ? r.Yaw : 0.0, mp.c_str());
        if (e.tail.size() > 3 && e.tail.compare(0, 3, "SP_") == 0) ++spNames;
        if (firstName.empty()) firstName = e.tail;
        owned += 1;
    }

    c.npcOwned = owned;

    // GATE 3 (wire): what this client is about to put on the wire. `own` is the
    // number of enemies this client claims (the peer can only mirror those), `sp`
    // is how many of them carry the "SP_<spawner tail>" alias (the only names the
    // other side can act on for a runtime-spawned enemy).
    if (g_diagSend.Due(Mono(), 5.0, 200)) {
        net::Log("npcsend: chars=%d list=%d own=%d sp=%d aliases=%d first=%s",
                 (int)state().local.tickChars.size(), (int)list.size(), owned, spNames,
                 (int)c.spawnAlias.Size(), firstName.empty() ? "-" : firstName.c_str());
    }

    // Concatenated, not passed through one %s: a full 16-entry list is ~1.5 kB and
    // net::AppendFmt is a vsnprintf into a fixed buffer.
    *out += ",\"npc\":[";
    *out += parts;
    *out += ']';
}

// main.lua:1604  `,"pd":%.3f` from PD.sent (cumulative)
void SerializePd(std::string* out) {
    if (!out) return;
    net::AppendFmt(out, ",\"pd\":%.3f", state().combat.pdSent);
}

// main.lua:1356-1363
void SerializeNhp(std::string* out) {
    if (!out) return;
    const std::vector<NpcEntry>& list = state().combat.npcLast;
    const std::size_t n = std::min<std::size_t>(list.size(), static_cast<std::size_t>(kMaxNpc));

    std::string parts;
    for (std::size_t i = 0; i < n; ++i) {
        if (i) parts.push_back(',');
        net::AppendFmt(&parts, "[\"%s\",%s]", list[i].tail.c_str(), HpString(list[i].tail).c_str());
    }
    *out += ",\"nhp\":[";
    *out += parts;
    *out += ']';
}

// main.lua:1365-1373 (order is a hash walk in the Lua -- the harness counts only;
// cap 40 emitted entries)
void SerializeDead(std::string* out) {
    if (!out) return;
    std::string parts;
    int n = 0;
    for (const std::string& tail : state().combat.deadTails.Items()) {
        n += 1;
        if (n > kDeadListCap) break;
        if (!parts.empty()) parts.push_back(',');
        parts.push_back('"');
        parts += tail;
        parts.push_back('"');
    }
    *out += ",\"dead\":[";
    *out += parts;
    *out += ']';
}

// main.lua:3957-3966 (the stream= line belongs to coop_puppet's state)
std::string Status() {
    const PuppetState& p = state().puppet;
    const CombatState& c = state().combat;

    const std::string n0 = (p.streamN0 < 0) ? std::string("nil") : net::Fmt("%d", p.streamN0);
    const std::string n1 = (p.streamN1 < 0) ? std::string("nil") : net::Fmt("%d", p.streamN1);

    std::string out = net::Fmt("stream=src:%s n0:%s n1:%s npcown:%d npcmirror:%d\n",
                               p.streamSrc.c_str(), n0.c_str(), n1.c_str(), c.npcOwned, c.npcMirrored);

    net::AppendFmt(&out,
                   "enemy=driven:%d taken:%d released:%d montages:%d spawnask:%d spawnok:%d "
                   "retarget:%d pdsent:%.3f pdapplied:%d\n",
                   static_cast<int>(c.mirrors.size()), c.mirTaken, c.mirReleased, c.mirMontages,
                   c.spawnAsked, c.spawnOk, c.aiRedirects, c.pdSent, c.pdApplied);

    const double nearMs = (c.ftNearN > 0) ? (c.ftNearSum / static_cast<double>(c.ftNearN) * 1000.0) : 0.0;
    const double farMs = (c.ftFarN > 0) ? (c.ftFarSum / static_cast<double>(c.ftFarN) * 1000.0) : 0.0;
    net::AppendFmt(&out, "perf=near:%.2fms(%d) far:%.2fms(%d)\n", nearMs, c.ftNearN, farMs, c.ftFarN);
    return out;
}

// main.lua:2120-2122 (`kill`) and the tail of applyDead: record the tail and run
// StartDeath on the local actor only once.
bool KillTail(const std::string& tail, const char* reason) {
    if (state().combat.deadTails.Has(tail)) return false;
    KillTailLogged(tail, reason);
    return true;
}

// ===========================================================================
// Reset -- a world change frees every cached actor (HANDOFF.md 18)
// ===========================================================================
void Reset() {
    CombatState& c = state().combat;

    // pointers: never touched again without a fresh FindAllOf
    c.hcCache.clear();
    c.mirrors.clear();
    c.npcLast.clear();
    c.npcFirst.clear();
    c.peerClaims.clear();
    c.spawners.clear();
    c.ownedTails.Clear();

    g_firstPos.clear();
    g_mirrorKeys.clear();
    g_spawnList.clear();
    g_spawnListValid = false;
    g_spawnScanAt = 0.0;
    g_selfTrig.clear();   // Mono() stamps taken in the old world mean nothing now
    g_thrash.clear();
    g_gameStats = nullptr;
    g_healthSlots[0] = 0;
    g_healthSlots[1] = 1;
    g_healthSlotsKnown = false;
    g_hcCacheAt = -1.0e9;   // the next tick refills immediately after a map load

    c.spawnListAt = 0.0;
    c.npcOwned = 0;
    c.npcMirrored = 0;

    // tail-keyed bookkeeping survives, exactly like the Lua's globals: after a
    // death reload the peers' enemies come back with the same tails.
    // (hpSeen, deadTails, spawnAlias, spawnReq, spawnDone, mirroredOnce,
    //  mirrorSkip, aiLogged and every counter stay.)
}

}  // namespace combat
}  // namespace coop
