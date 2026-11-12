// coop_realarea.cpp -- round 9 lab probe: find a REAL area (a boss arena) and get
// the local player into it.  Not part of the sync; only runs when a `cmd` verb asks.
//
//   areasweep [substring ...]   every live Actor whose CLASS NAME contains one of
//                               the substrings (default: the area/boss set below),
//                               with its world position and the distance to our pawn.
//                               The result is cached for `areago` / `areacall`.
//   arealayers [substring]      every DataLayerInstanceWithAsset (name + runtime
//                               state) and every SpartaAreaId DataAsset (the area
//                               list: DisplayName + DataLayer).  An "area" in this
//                               game is a data layer, so this is the map list.
//   areatomb [substring]        every BP_LevelInstanceSelector_TombZone_C and what the
//                               game itself says about it: `GetDungeonTeleportData`
//                               (the dungeon entrance location), IsEntrance /
//                               TeleportToExit / PlayerOutsideTombZone, and whether
//                               the level instance is loaded.
//   areatombgo <n|nearest>      the game's OWN entry into that tomb zone:
//                               `InitiateTeleport()` (fade + streaming + teleport).
//   areago <n|nearest|far> [dz] teleport our own pawn to a cached hit (K2_SetActorLocation
//                               through puppet::StartTp, the `tp` path).
//   areacall <n|nearest> <Fn>   call ONE named UFunction on a cached hit (no parameters).
//
// Why this file exists: every earlier round ran in the hub save, which has no boss
// and no placed NPC to talk to.  The boss/phase lane (coop_boss.cpp) could only
// prove itself on a component synthesized onto a player character.  A real area is
// reached the way the game reaches it -- a TombZone level-instance selector placed
// in the open world -- so the probe only has to FIND that actor and call its own
// teleport entry; it never writes a location by hand unless `areago` is asked to.

#include "coop_state.h"

#include "ms2_ue.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace coop {
namespace area {

namespace {

// One actor the last sweep found.  Cached so `areago`/`areacall` can address it by
// index after the sweep printed the list.
struct Hit {
    std::string cls;
    std::string name;
    ue::UObject* obj = nullptr;
    Vec3 loc;
    double d = 0.0;
};

std::vector<Hit> g_hits;
constexpr size_t kMaxHits = 600;      // a log line each; the hub cell has hundreds
constexpr int kMaxLines = 220;        // what a sweep prints

// The default sweep: everything an area/boss/dungeon entrance can be called.
const char* const kDefaultPatterns[] = {
    "LandingArea", "LevelInstanceSelector", "BossGate", "BossAI", "TombZone",
    "TeleportLocation", "FastTravelPoint", "SpartaPlayerStart", "BP_Teleport",
};

std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (unsigned char)text[i] <= ' ') ++i;
        size_t j = i;
        while (j < text.size() && (unsigned char)text[j] > ' ') ++j;
        if (j > i) out.push_back(text.substr(i, j - i));
        i = j;
    }
    return out;
}

bool Matches(const std::string& cls, const std::vector<std::string>& pats) {
    for (const std::string& p : pats) {
        if (cls.find(p) != std::string::npos) return true;
    }
    return false;
}

std::string Where(const Vec3& v) {
    return net::Fmt("%.0f %.0f %.0f", v.X, v.Y, v.Z);
}

// A no-argument UFunction call (the shape every BP entry point here has).
bool CallNoArgs(ue::UObject* o, const char* fn, bool* exists) {
    ue::UFunction* f = ue::FindFunction(o, fn);
    if (exists) *exists = f != nullptr;
    if (!f) return false;
    ue::Params p(f);
    return ue::Call(o, f, p.data());
}

// `FullName` minus the outer chain: "Class /Game/..Level.Inst" is too long for a
// log line and the placement (level instance) is what tells two selectors apart.
std::string Short(const std::string& full) {
    const size_t sp = full.rfind(' ');
    if (sp == std::string::npos) return full;
    std::string rest = full.substr(sp + 1);
    const size_t dot = rest.rfind('.');
    if (dot != std::string::npos && rest.size() - dot < 64) return rest.substr(dot + 1);
    return rest;
}

// GetDungeonTeleportData(Location, Rotation, ZoneData) -- the game's own answer to
// "where is this tomb's interior entrance".  Read-only.
bool DungeonTeleportData(ue::UObject* sel, Vec3* loc) {
    ue::UFunction* f = ue::FindFunction(sel, "GetDungeonTeleportData");
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(sel, f, p.data())) return false;
    const ue::FVector v = p.Get<ue::FVector>("Location");
    loc->X = v.X;
    loc->Y = v.Y;
    loc->Z = v.Z;
    return true;
}

Hit* Pick(const std::string& arg) {
    if (g_hits.empty()) return nullptr;
    if (arg.empty() || arg == "nearest") return &g_hits.front();   // sorted by distance
    if (arg == "far") return &g_hits.back();
    char* stop = nullptr;
    const long i = strtol(arg.c_str(), &stop, 10);
    if (stop != arg.c_str() && i >= 0 && (size_t)i < g_hits.size()) return &g_hits[(size_t)i];
    for (Hit& h : g_hits) {
        if (h.cls.find(arg) != std::string::npos || h.name.find(arg) != std::string::npos) return &h;
    }
    return nullptr;
}

}  // namespace

// ===========================================================================
// areasweep -- the actor census, by class-name substring
// ===========================================================================
void Sweep(const std::string& arg) {
    std::vector<std::string> pats = Split(arg);
    if (pats.empty()) {
        for (const char* p : kDefaultPatterns) pats.push_back(p);
    }

    ue::UObject* pawn = net::Pawn();
    const Vec3 me = net::Loc(pawn);
    const std::vector<ue::UObject*> all = ue::FindAllOf("Actor");

    g_hits.clear();
    std::vector<std::pair<int, std::string>> counts;   // class -> n, biggest first
    int actors = 0;
    for (ue::UObject* a : all) {
        if (!net::Valid(a)) continue;
        ++actors;
        const std::string cls = ue::GetClassName(a);
        if (!Matches(cls, pats)) continue;

        bool seen = false;
        for (auto& c : counts) {
            if (c.second == cls) { ++c.first; seen = true; break; }
        }
        if (!seen) counts.push_back({1, cls});

        if (g_hits.size() < kMaxHits) {
            Hit h;
            h.cls = cls;
            h.name = ue::GetName(a);
            h.obj = a;
            h.loc = net::Loc(a);
            h.d = Dist(me, h.loc);
            g_hits.push_back(h);
        }
    }
    std::sort(counts.begin(), counts.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::sort(g_hits.begin(), g_hits.end(),
              [](const Hit& a, const Hit& b) { return a.d < b.d; });

    std::string filt;
    for (const std::string& p : pats) { if (!filt.empty()) filt += ","; filt += p; }
    net::Log("areasweep: %d live actors, filter [%s] -> %d hit(s) in %d class(es); our pawn at (%s)",
             actors, filt.c_str(), (int)g_hits.size(), (int)counts.size(), Where(me).c_str());
    for (const auto& c : counts) {
        net::Log("areasweep: class %s x%d", c.second.c_str(), c.first);
    }

    const int lines = (int)std::min<size_t>(g_hits.size(), (size_t)kMaxLines);
    for (int i = 0; i < lines; ++i) {
        // the PLACEMENT tail tells two selectors of one class apart ("...Level.PersistentLevel"
        // vs a streamed level instance), so it is printed, shortened.
        const std::string full = net::FullName(g_hits[(size_t)i].obj);
        net::Log("areasweep: [%d] %s %s at (%s) d=%.0f in=%s", i,
                 g_hits[(size_t)i].cls.c_str(), g_hits[(size_t)i].name.c_str(),
                 Where(g_hits[(size_t)i].loc).c_str(), g_hits[(size_t)i].d,
                 Short(full).c_str());
    }
    if ((size_t)lines < g_hits.size()) {
        net::Log("areasweep: %d more hit(s) not printed", (int)(g_hits.size() - (size_t)lines));
    }
}

// ===========================================================================
// arealayers -- the area list (a data layer per area) and the live layer states
// ===========================================================================
void Layers(const std::string& arg) {
    const std::string filt = arg;
    const std::string want = filt.empty() ? std::string() : filt;

    // 1. every area DataAsset the game has loaded: SpartaAreaId.DisplayName + .DataLayer
    const std::vector<ue::UObject*> ids = ue::FindAllOf("SpartaAreaId");
    int shown = 0;
    for (ue::UObject* a : ids) {
        if (!net::Valid(a)) continue;
        const std::string name = ue::GetName(a);
        std::string layer;
        if (!ue::Get(a, "DataLayer", layer)) layer = "-";
        if (!want.empty() && name.find(want) == std::string::npos &&
            layer.find(want) == std::string::npos) {
            continue;
        }
        net::Log("arealayers: area %s layer=%s", name.c_str(), layer.c_str());
        ++shown;
        if (shown >= 120) break;
    }
    net::Log("arealayers: %d of %d SpartaAreaId asset(s) listed (filter '%s')", shown,
             (int)ids.size(), want.c_str());

    // 2. the live data-layer instances + their runtime state (what is loaded NOW)
    ue::UObject* dlm = ue::FindFirstOf("DataLayerManager");
    const std::vector<ue::UObject*> insts = ue::FindAllOf("DataLayerInstanceWithAsset");
    int live = 0;
    for (ue::UObject* inst : insts) {
        if (!net::Valid(inst)) continue;
        ue::UObject* asset = nullptr;
        ue::Get(inst, "DataLayerAsset", asset);
        const std::string aname = net::Valid(asset) ? ue::GetName(asset) : ue::GetName(inst);
        if (!want.empty() && aname.find(want) == std::string::npos) continue;
        int state = -1;
        if (dlm) {
            if (ue::UFunction* f = ue::FindFunction(dlm, "GetDataLayerInstanceRuntimeState")) {
                ue::Params p(f);
                p.Set<ue::UObject*>("InDataLayerInstance", inst);
                if (ue::Call(dlm, f, p.data())) state = (int)p.Get<uint8_t>("ReturnValue");
            }
        }
        net::Log("arealayers: live %s state=%d", aname.c_str(), state);
        ++live;
        if (live >= 200) break;
    }
    net::Log("arealayers: %d live DataLayerInstanceWithAsset (filter '%s') manager=%s", live,
             want.c_str(), dlm ? "yes" : "no");

    // 3. the active area, from the subsystem the game itself uses
    ue::UObject* sub = ue::FindFirstOf("SpartaAreaSubsystem");
    if (sub) {
        if (ue::UFunction* f = ue::FindFunction(sub, "GetActiveArea")) {
            ue::Params p(f);
            if (ue::Call(sub, f, p.data())) {
                ue::UObject* act = p.Get<ue::UObject*>("ReturnValue");
                std::string layer;
                if (net::Valid(act)) ue::Get(act, "DataLayer", layer);
                net::Log("arealayers: active area=%s layer=%s",
                         net::Valid(act) ? ue::GetName(act).c_str() : "none", layer.c_str());
            }
        }
    } else {
        net::Log("arealayers: no SpartaAreaSubsystem in this world");
    }
}

// ===========================================================================
// areatomb -- every TombZone level-instance selector, and where it leads
// ===========================================================================
void Tombs(const std::string& arg) {
    const std::vector<ue::UObject*> all = ue::FindAllOf("BP_LevelInstanceSelector_TombZone_C");
    const Vec3 me = net::Loc(net::Pawn());
    int n = 0;
    for (ue::UObject* s : all) {
        if (!net::Valid(s)) continue;
        const std::string name = ue::GetName(s);
        if (!arg.empty() && name.find(arg) == std::string::npos) continue;
        const Vec3 here = net::Loc(s);
        Vec3 dest;
        const bool haveDest = DungeonTeleportData(s, &dest);
        const bool entrance = ue::GetBool(s, "IsEntrance");
        const bool toExit = ue::GetBool(s, "TeleportToExit");
        const bool outside = ue::GetBool(s, "PlayerOutsideTombZone");
        const bool loaded = ue::GetBool(s, "bIsLevelLoaded");
        net::Log("areatomb: %s at (%s) d=%.0f entrance=%d toExit=%d outside=%d loaded=%d dest=%s "
                 "(%s) in=%s",
                 name.c_str(), Where(here).c_str(), Dist(me, here), entrance ? 1 : 0,
                 toExit ? 1 : 0, outside ? 1 : 0, loaded ? 1 : 0,
                 haveDest ? Where(dest).c_str() : "-", haveDest ? net::Fmt("%.0f", Dist(me, dest)).c_str() : "-",
                 Short(net::FullName(s)).c_str());

        // cache it so `areatombgo` / `areago` can address it by index
        Hit h;
        h.cls = ue::GetClassName(s);
        h.name = name;
        h.obj = s;
        h.loc = here;
        h.d = Dist(me, here);
        g_hits.push_back(h);
        ++n;
    }
    std::sort(g_hits.begin(), g_hits.end(), [](const Hit& a, const Hit& b) { return a.d < b.d; });
    net::Log("areatomb: %d selector(s)%s", n, n == 0 ? " -- none streamed in here" : "");
}

// ===========================================================================
// areatombgo -- the game's OWN way in (InitiateTeleport)
// ===========================================================================
void TombGo(const std::string& arg) {
    Hit* h = Pick(arg);
    if (!h) {
        net::Log("areatombgo: no cached selector (run areatomb/areasweep first)");
        return;
    }
    ue::UObject* s = h->obj;
    net::Log("areatombgo: %s (%s) -- InitiateTeleport ...", h->name.c_str(), h->cls.c_str());
    bool exists = false;
    const bool ok = CallNoArgs(s, "InitiateTeleport", &exists);
    net::Log("areatombgo: InitiateTeleport fn=%d call=%d", exists ? 1 : 0, ok ? 1 : 0);
    if (!exists) {
        // the entrance path the game's own overlap uses
        bool e2 = false;
        const bool ok2 = CallNoArgs(s, "CommitActivationTeleport", &e2);
        net::Log("areatombgo: CommitActivationTeleport fn=%d call=%d", e2 ? 1 : 0, ok2 ? 1 : 0);
    }
}

// ===========================================================================
// areago -- put OUR pawn on a cached hit (the `tp` primitive, no game function)
// ===========================================================================
void Go(const std::string& arg) {
    std::vector<std::string> f = Split(arg);
    std::string which = f.empty() ? std::string("nearest") : f[0];
    double dz = 50.0;
    if (f.size() > 1) dz = strtod(f[1].c_str(), nullptr);
    Hit* h = Pick(which);
    ue::UObject* pawn = net::Pawn();
    if (!h) {
        net::Log("areago: no cached hit for '%s' (run areasweep/areatomb first)", which.c_str());
        return;
    }
    if (!net::Valid(pawn)) {
        net::Log("areago: no pawn");
        return;
    }
    const Vec3 me = net::Loc(pawn);
    net::Log("areago: %s %s at (%s) from (%s) dz=%.0f -- StartTp", h->cls.c_str(), h->name.c_str(),
             Where(h->loc).c_str(), Where(me).c_str(), dz);
    puppet::StartTp(pawn, h->loc.X, h->loc.Y, h->loc.Z + dz);

    // face the target: an NPC interaction only appears when the player LOOKS at the
    // NPC, and the `tp` primitive keeps the old rotation (round 9c: the interact press
    // did nothing because the ShellKeeper was behind the camera).
    const double yaw = std::atan2(h->loc.Y - me.Y, h->loc.X - me.X) * 180.0 / 3.14159265358979323846;
    if (ue::UFunction* fr = ue::FindFunction(pawn, "K2_SetActorRotation")) {
        ue::FRotator r{};
        r.Pitch = 0.0;
        r.Yaw = yaw;
        r.Roll = 0.0;
        ue::Params pr(fr);
        pr.Set<ue::FRotator>("NewRotation", r);
        pr.Set<bool>("bTeleportPhysics", true);
        net::Log("areago: facing yaw %.0f (set=%d)", yaw, ue::Call(pawn, fr, pr.data()) ? 1 : 0);
    }
}

// ===========================================================================
// areacall -- call one named UFunction on a cached hit
// ===========================================================================
void Call(const std::string& arg) {
    std::vector<std::string> f = Split(arg);
    if (f.size() < 2) {
        net::Log("areacall: usage areacall <n|nearest|substring> <FunctionName>");
        return;
    }
    Hit* h = Pick(f[0]);
    if (!h) {
        net::Log("areacall: no cached hit for '%s'", f[0].c_str());
        return;
    }
    bool exists = false;
    const bool ok = CallNoArgs(h->obj, f[1].c_str(), &exists);
    net::Log("areacall: %s %s . %s fn=%d call=%d", h->cls.c_str(), h->name.c_str(), f[1].c_str(),
             exists ? 1 : 0, ok ? 1 : 0);
}

void Reset() { g_hits.clear(); }

// ===========================================================================
// attack -- the game's OWN melee swing, and the watch that proves it ran
// ===========================================================================
//
// Round 9 first try: `TryActivateAbilityByClass` returned 1 on both clients but
// `GetCurrentMontage` on the pawn was EMPTY at the moment of the call, so nothing
// proved a swing.  ACharacter::GetCurrentMontage reads the montage under the play
// head of the character's OWN anim instance, so an empty answer means either the
// ability never really ran or the montage starts on a later frame.  This pair of
// verbs separates the two: `attack` activates and arms a 4 s watch, and the watch
// samples (a) the montage, (b) the attack ability instance's own IsActive(), and
// (c) the nearest enemy's health, 4x/s -- so "the ability reported active but no
// montage ever played" and "the montage played and the enemy lost health" are
// different, visible outcomes.
namespace {

ue::UObject* AscOf(ue::UObject* pawn) {
    if (!net::Valid(pawn)) return nullptr;
    if (ue::UFunction* f = ue::FindFunction(pawn, "GetAbilitySystemComponent")) {
        ue::Params p(f);
        if (ue::Call(pawn, f, p.data())) {
            ue::UObject* asc = p.Get<ue::UObject*>("ReturnValue");
            if (net::Valid(asc)) return asc;
        }
    }
    ue::UObject* asc = nullptr;
    if (ue::Get(pawn, "AbilitySystemComponent", asc) && net::Valid(asc)) return asc;
    return nullptr;
}

bool ActivateByClass(ue::UObject* asc, ue::UObject* cls) {
    ue::UFunction* f = ue::FindFunction(asc, "TryActivateAbilityByClass");
    if (!f) return false;
    ue::Params p(f);
    if (ue::PropOffsetStruct((ue::UStruct*)f, "AbilityToActivate") >= 0)
        p.Set<ue::UObject*>("AbilityToActivate", cls);
    else if (ue::PropOffsetStruct((ue::UStruct*)f, "AbilityClass") >= 0)
        p.Set<ue::UObject*>("AbilityClass", cls);
    if (ue::PropOffsetStruct((ue::UStruct*)f, "bAllowRemoteActivation") >= 0)
        p.Set<bool>("bAllowRemoteActivation", true);
    return ue::Call(asc, f, p.data());
}

std::string MontageNow(double* pos) {
    MontageSig m;
    if (!local::CaptureMontage(&m) || m.path.empty()) {
        if (pos) *pos = 0.0;
        return "-";
    }
    if (pos) *pos = m.pos;
    return m.path;
}

// the granted instance of an ability class: FindAllOf("GameplayAbility") once, then
// a class-name compare (no FGameplayAbilitySpec arithmetic)
ue::UObject* AbilityInstance(const std::string& cls) {
    for (ue::UObject* a : ue::FindAllOf("GameplayAbility")) {
        if (!net::Valid(a)) continue;
        if (ue::GetClassName(a) == cls) return a;
    }
    return nullptr;
}

bool AbilityActive(ue::UObject* a) {
    if (!net::Valid(a)) return false;
    ue::UFunction* f = ue::FindFunction(a, "IsActive");
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(a, f, p.data())) return false;
    return p.Get<bool>("ReturnValue");
}

// the nearest live enemy and its health (the same SpartaHealthComponent the damage
// mirror reads -- combat::HealthNorm is the proven accessor)
ue::UObject* NearestEnemyActor(double maxD, double* dOut) {
    ue::UObject* pawn = net::Pawn();
    const Vec3 me = net::Loc(pawn);
    ue::UObject* best = nullptr;
    double bestD = maxD;
    for (ue::UObject* a : ue::FindAllOf("BP_AICharacter_C")) {
        if (!net::Valid(a)) continue;
        const double dd = Dist(me, net::Loc(a));
        if (dd > bestD) continue;
        bool dead = false;
        combat::HealthNorm(a, &dead);
        if (dead) continue;
        bestD = dd;
        best = a;
    }
    if (dOut) *dOut = best ? bestD : -1.0;
    return best;
}

bool NearestEnemy(double maxD, std::string* tail, double* hp, double* d) {
    double dd = -1.0;
    ue::UObject* a = NearestEnemyActor(maxD, &dd);
    if (!a) return false;
    if (tail) *tail = ue::GetName(a);
    if (d) *d = dd;
    if (hp) {
        bool dead = false;
        *hp = combat::HealthNorm(a, &dead);
    }
    return true;
}

}  // namespace

// `enemytp` -- stand next to an enemy so a swing can reach it: the mod's own `tp`
// primitive, a little ABOVE the enemy (the TP hold hovers for 6 s) and FACING it.
// `enemytp <name substring>` picks THAT enemy (the kill test has to keep hitting one
// actor, not whatever happens to be nearest: two cultists 1 200 uu apart made the
// "nearest" flip between them and hid a real kill).
void EnemyTp(const std::string& arg) {
    std::string want = arg;
    while (!want.empty() && (unsigned char)want.back() <= ' ') want.pop_back();
    while (!want.empty() && (unsigned char)want.front() <= ' ') want.erase(want.begin());

    ue::UObject* a = nullptr;
    double dd = -1.0;
    if (want.empty()) {
        a = NearestEnemyActor(8000.0, &dd);
    } else {
        ue::UObject* pawn = net::Pawn();
        const Vec3 me = net::Loc(pawn);
        for (ue::UObject* e : ue::FindAllOf("BP_AICharacter_C")) {
            if (!net::Valid(e)) continue;
            if (ue::GetName(e).find(want) == std::string::npos) continue;
            a = e;
            dd = Dist(me, net::Loc(e));
            break;
        }
    }
    if (!a) {
        net::Log("enemytp: no live enemy %s", want.empty() ? "within 8000 uu" : want.c_str());
        return;
    }
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn)) {
        net::Log("enemytp: no pawn");
        return;
    }
    const Vec3 el = net::Loc(a);
    const Vec3 me = net::Loc(pawn);
    // 160 uu from the enemy along the line from us, 40 uu up: inside a melee
    // swing's reach (the mod's NPC collect radius is 4000 uu, the swing ~250).
    // k is "stay 160 uu short of the enemy", NOT "(len-160)/len" -- that lands
    // (len-160) away from it, which is where it started (round-9 first try left the
    // host 1164 uu from the target because of exactly this).
    double dx = me.X - el.X, dy = me.Y - el.Y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0) { dx = 1.0; dy = 0.0; len = 1.0; }
    const double k = (len > 160.0) ? (160.0 / len) : 1.0;
    Vec3 to;
    to.X = el.X + dx * k;
    to.Y = el.Y + dy * k;
    to.Z = el.Z + 40.0;
    bool dead = false;
    const double hp = combat::HealthNorm(a, &dead);
    net::Log("enemytp: %s d=%.0f hp=%.3f at (%s) -> tp to (%s)", ue::GetName(a).c_str(), dd, hp,
             Where(el).c_str(), Where(to).c_str());
    puppet::StartTp(pawn, to.X, to.Y, to.Z);

    // FACE it: the swing's trace goes along the pawn's yaw and `tp` keeps the old
    // rotation, so a kill test that stands beside the target swings into the wall
    // (round 9c: the montage played and the enemy at d=160 never lost health).
    const double yaw = std::atan2(el.Y - to.Y, el.X - to.X) * 180.0 / 3.14159265358979323846;
    if (ue::UFunction* fr = ue::FindFunction(pawn, "K2_SetActorRotation")) {
        ue::FRotator r{};
        r.Pitch = 0.0;
        r.Yaw = yaw;
        r.Roll = 0.0;
        ue::Params pr(fr);
        pr.Set<ue::FRotator>("NewRotation", r);
        pr.Set<bool>("bTeleportPhysics", true);
        const bool ok = ue::Call(pawn, fr, pr.data());
        net::Log("enemytp: facing yaw %.0f (set=%d)", yaw, ok ? 1 : 0);
    }
}

// `enemydump` -- the closest live enemies with class, health and distance (the
// target list a kill test needs; `hp` is the Lua-era version of the same read)
void EnemyDump(const std::string& arg) {
    ue::UObject* pawn = net::Pawn();
    const Vec3 me = net::Loc(pawn);
    int n = 0;
    for (ue::UObject* a : ue::FindAllOf("BP_AICharacter_C")) {
        if (!net::Valid(a)) continue;
        const double dd = Dist(me, net::Loc(a));
        bool dead = false;
        const double hp = combat::HealthNorm(a, &dead);
        if (dead) continue;
        if (dd > 6000.0) continue;
        const Vec3 l = net::Loc(a);
        net::Log("enemydump: %s %s hp=%.3f d=%.0f at (%s)", ue::GetClassName(a).c_str(),
                 ue::GetName(a).c_str(), hp, dd, Where(l).c_str());
        if (++n >= 10) break;
    }
    net::Log("enemydump: %d live enemy(s) within 6000 uu%s", n, n ? "" : " (none -- this cell has no brigands)");
}

namespace {

struct AtkWatch {
    bool on = false;
    double t0 = 0.0;
    double nextSample = 0.0;
    std::string cls;        // the ability class that reported activation
    std::string enemy;
    double hp0 = -1.0;
    double d0 = 0.0;
    int samples = 0;
    bool sawMontage = false;
    bool sawActive = false;
    double lastHp = -1.0;
};

AtkWatch g_atk;

}  // namespace

void NoteNearestEnemy(std::string* tail, double* hp, double* d) {
    if (NearestEnemy(4000.0, tail, hp, d)) return;
    if (tail) *tail = "-";
    if (hp) *hp = -1.0;
    if (d) *d = -1.0;
}

void Attack(const std::string& arg) {
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn)) {
        net::Log("attack: no pawn");
        return;
    }
    ue::UObject* asc = AscOf(pawn);
    if (!net::Valid(asc)) {
        net::Log("attack: no ASC on %s", ue::GetClassName(pawn).c_str());
        return;
    }
    std::vector<std::string> cands;
    if (arg.empty() || arg == "light") {
        cands = {"GA_Player_AttackSelector_Light_C",
                 "GA_Player_AttackSelector_Light_HadernsSword_C",
                 "GA_Player_Attack_Light_C",
                 "GA_Attack_Primary_C"};
    } else if (arg == "heavy") {
        cands = {"GA_Player_AttackSelector_Heavy_C",
                 "GA_Player_AttackSelector_Heavy_HadernsSword_C",
                 "GA_Player_Attack_Heavy_C",
                 "GA_Attack_Secondary_C"};
    } else {
        cands.push_back(arg);
    }

    std::string wpn, wsock, wpns;
    local::WeaponInfo(&wpn, &wsock, &wpns);
    std::string etail;
    double ehp = -1.0, ed = -1.0;
    NoteNearestEnemy(&etail, &ehp, &ed);
    // WHY an attack ability refuses to activate: the player's OWN weapon actor
    // carries `EquipState` (WP_WeaponBase, 0x4F8) and its socket says whether it is
    // in hand or stowed (round 9: socket Socket_Prop_R = on the back).  The weapon
    // hangs off the pawn as an ATTACHED ACTOR, so it is found by walking those --
    // a radius scan finds the enemy's fists and kicks instead (measured).
    int weapons = 0;
    if (ue::UFunction* fa = ue::FindFunction(pawn, "GetAttachedActors")) {
        ue::Params q(fa);
        q.Set<bool>("bResetArray", true);
        q.Set<bool>("bRecursivelyIncludeAttachedActors", true);
        if (ue::Call(pawn, fa, q.data())) {
            const auto acts = q.Get<ue::TArray<ue::UObject*>>("OutActors");
            for (int32_t i = 0; i < acts.Num && i < 64; ++i) {
                ue::UObject* a = nullptr;
                if (!ue::ReadMem(acts.Data + i, &a, sizeof(a)) || !net::Valid(a)) continue;
                const std::string cls = ue::GetClassName(a);
                if (cls.find("WP_") == std::string::npos) continue;
                uint8_t state = 255;
                ue::Get(a, "EquipState", state);
                ue::FName sock{};
                ue::Get(a, "AttachSocketName", sock);
                net::Log("attack: our weapon %s (%s) equipState=%d socket=%s", ue::GetName(a).c_str(),
                         cls.c_str(), (int)state, ue::ToString(sock).c_str());
                ++weapons;
            }
        }
    }
    net::Log("attack: pawn=%s asc=%s weapon=%s sock=%s montage=%s enemy=%s hp=%.3f d=%.0f "
             "weaponActors=%d",
             ue::GetClassName(pawn).c_str(), ue::GetClassName(asc).c_str(), wpn.c_str(),
             wsock.c_str(), MontageNow(nullptr).c_str(), etail.c_str(), ehp, ed, weapons);

    g_atk = AtkWatch();
    g_atk.enemy = etail;
    g_atk.hp0 = ehp;
    g_atk.d0 = ed;

    for (const std::string& name : cands) {
        ue::UObject* cls = (ue::UObject*)ue::FindClass(name.c_str());
        if (!cls) {
            net::Log("attack: class %s not in memory", name.c_str());
            continue;
        }
        ue::UObject* inst = AbilityInstance(name);
        const bool wasActive = AbilityActive(inst);
        const bool ok = ActivateByClass(asc, cls);
        const bool nowActive = AbilityActive(inst);
        net::Log("attack: TryActivateAbilityByClass(%s) asc=%d instance=%s active %d->%d montage=%s",
                 name.c_str(), ok ? 1 : 0, inst ? "yes" : "no", wasActive ? 1 : 0, nowActive ? 1 : 0,
                 MontageNow(nullptr).c_str());
        if (nowActive) {
            g_atk.cls = name;
            break;
        }
    }
    // the watch samples for 4 s: a montage that starts on a later frame, and an
    // enemy whose health only moves once the swing's trace lands, both show up here
    g_atk.on = true;
    g_atk.t0 = net::Now();
    g_atk.nextSample = g_atk.t0;
    net::Log("attack: watch armed (armed ability=%s)", g_atk.cls.empty() ? "none" : g_atk.cls.c_str());
}

// 15 Hz, always-run (a local hold must not hide a swing): 4 samples/s for 4 s.
void AttackTick() {
    if (!g_atk.on) return;
    const double now = net::Now();
    if (now < g_atk.nextSample) return;
    g_atk.nextSample = now + 0.25;
    const double t = now - g_atk.t0;

    double pos = 0.0;
    const std::string mon = MontageNow(&pos);
    if (mon != "-") g_atk.sawMontage = true;
    const bool act = AbilityActive(AbilityInstance(g_atk.cls));
    if (act) g_atk.sawActive = true;
    std::string etail;
    double ehp = -1.0, ed = -1.0;
    NoteNearestEnemy(&etail, &ehp, &ed);
    g_atk.lastHp = ehp;
    ++g_atk.samples;
    if (t < 1.5 || mon != "-" || g_atk.samples % 4 == 0) {
        net::Log("attackwatch: t=%.2f montage=%s pos=%.2f active=%d enemy=%s hp=%.3f", t, mon.c_str(), pos,
                 act ? 1 : 0, etail.c_str(), ehp);
    }
    if (t >= 3.75) {
        g_atk.on = false;
        net::Log("attackwatch: done montage_seen=%d active_seen=%d hp %.3f -> %.3f enemy=%s",
                 g_atk.sawMontage ? 1 : 0, g_atk.sawActive ? 1 : 0, g_atk.hp0, g_atk.lastHp,
                 g_atk.enemy.c_str());
    }
}

// `weaponstate` -- READ-ONLY: our own weapon actors, their EquipState (WP_WeaponBase
// 0x4F8: in hand vs stowed), their socket and the montage under the play head.  With
// real input this is the one probe needed to tell "the input never reached the game"
// (state unchanged, no montage) from "the swing ran" (state 1 / montage playing).
void WeaponState(const std::string& arg) {
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn)) {
        net::Log("weaponstate: no pawn");
        return;
    }
    std::string wpn, wsock, wpns;
    local::WeaponInfo(&wpn, &wsock, &wpns);
    net::Log("weaponstate: pawn=%s weapon=%s sock=%s montage=%s", ue::GetClassName(pawn).c_str(),
             wpn.c_str(), wsock.c_str(), MontageNow(nullptr).c_str());
    int n = 0;
    if (ue::UFunction* fa = ue::FindFunction(pawn, "GetAttachedActors")) {
        ue::Params q(fa);
        q.Set<bool>("bResetArray", true);
        q.Set<bool>("bRecursivelyIncludeAttachedActors", true);
        if (ue::Call(pawn, fa, q.data())) {
            const auto acts = q.Get<ue::TArray<ue::UObject*>>("OutActors");
            for (int32_t i = 0; i < acts.Num && i < 64; ++i) {
                ue::UObject* a = nullptr;
                if (!ue::ReadMem(acts.Data + i, &a, sizeof(a)) || !net::Valid(a)) continue;
                const std::string cls = ue::GetClassName(a);
                if (cls.find("WP_") == std::string::npos) continue;
                uint8_t equip = 255, cur = 255;
                ue::Get(a, "EquipState", equip);
                ue::Get(a, "CurrentEquipState", cur);
                ue::FName sock{};
                ue::Get(a, "AttachSocketName", sock);
                ue::FName inHand{}, stowed{};
                ue::Get(a, "InHandSocket", inHand);
                ue::Get(a, "StowedSocket", stowed);
                net::Log("weaponstate: %s (%s) equip=%d current=%d socket=%s inHand=%s stowed=%s",
                         ue::GetName(a).c_str(), cls.c_str(), (int)equip, (int)cur,
                         ue::ToString(sock).c_str(), ue::ToString(inHand).c_str(),
                         ue::ToString(stowed).c_str());
                ++n;
            }
        }
    }
    net::Log("weaponstate: %d weapon actor(s) attached", n);
}

}  // namespace area
}  // namespace coop
