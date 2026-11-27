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

// [R10-INTERDUMP] areacomp <n|nearest|substring> <ComponentProp> [go] -- world location of a
// component property on a cached hit (e.g. BP_CinematicPlayer_C TriggerArea), optionally tp
// our pawn into it.  Lab verb.
void Comp(const std::string& arg) {
    std::vector<std::string> f = Split(arg);
    if (f.size() < 2) {
        net::Log("areacomp: usage areacomp <n|nearest|substring> <ComponentProp> [go]");
        return;
    }
    Hit* h = Pick(f[0]);
    if (!h || !net::Valid(h->obj)) {
        net::Log("areacomp: no cached hit for '%s'", f[0].c_str());
        return;
    }
    ue::UObject* comp = nullptr;
    ue::Get<ue::UObject*>(h->obj, f[1].c_str(), comp);
    if (!net::Valid(comp)) {
        net::Log("areacomp: %s has no live component '%s'", h->name.c_str(), f[1].c_str());
        return;
    }
    Vec3 tl;
    if (ue::UFunction* gl = ue::FindFunction(comp, "K2_GetComponentLocation")) {
        ue::Params gp(gl);
        if (ue::Call(comp, gl, gp.data())) {
            const ue::FVector v = gp.Get<ue::FVector>("ReturnValue");
            tl.X = v.X; tl.Y = v.Y; tl.Z = v.Z;
        }
    }
    ue::FVector ext{};
    const bool hasExt = ue::Get<ue::FVector>(comp, "BoxExtent", ext);
    ue::UObject* pawn = net::Pawn();
    net::Log("areacomp: %s.%s (%s) at (%s) d=%.0f extent=%s UseTriggerToPlay=%d TriggerActivated=%d "
             "PlayCinematicOnSpawn=%d OnlyPlayOnce=%d",
             h->name.c_str(), f[1].c_str(), ue::GetClassName(comp).c_str(), Where(tl).c_str(),
             Dist(net::Loc(pawn), tl),
             hasExt ? net::Fmt("%.0f,%.0f,%.0f", ext.X, ext.Y, ext.Z).c_str() : "-",
             ue::GetBool(h->obj, "UseTriggerToPlay") ? 1 : 0, ue::GetBool(h->obj, "TriggerActivated") ? 1 : 0,
             ue::GetBool(h->obj, "PlayCinematicOnSpawn") ? 1 : 0, ue::GetBool(h->obj, "OnlyPlayOnce") ? 1 : 0);
    if (f.size() > 2 && f[2] == "go" && net::Valid(pawn)) {
        puppet::StartTp(pawn, tl.X, tl.Y, tl.Z + 50.0);
        net::Log("areacomp: go -> (%s)", Where(tl).c_str());
    }
}

void Reset() { g_hits.clear(); InjectReset(); }

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
        // [R10-AIM] nearest LIVE match, so `enemytp Brigand` on both clients lands on
        // the same brigand (the guest's mirror of the host's), not the first in the list
        for (ue::UObject* e : ue::FindAllOf("BP_AICharacter_C")) {
            if (!net::Valid(e)) continue;
            if (ue::GetName(e).find(want) == std::string::npos) continue;
            bool dead = false;
            combat::HealthNorm(e, &dead);
            if (dead) continue;
            const double d = Dist(me, net::Loc(e));   // [R10-BOSSLOC] a named pick has no range cap
            if (!a || d < dd) { a = e; dd = d; }
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
        // [R10-AIM] The player pawn turns to the CONTROL rotation (camera), so a pawn-only
        // rotation snaps back and the swing whiffs (round 10 run 8: host baseline target
        // stayed hp=1.000 after 3 swings).  Turn the controller too.
        bool okc = false;
        ue::UObject* pc = net::PC();
        if (pc) {
            if (ue::UFunction* fc = ue::FindFunction(pc, "SetControlRotation")) {
                ue::Params pp(fc);
                pp.Set<ue::FRotator>("NewRotation", r);
                okc = ue::Call(pc, fc, pp.data());
            }
        }
        net::Log("enemytp: facing yaw %.0f (set=%d ctrl=%d)", yaw, ok ? 1 : 0, okc ? 1 : 0);
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
    InjectTick();
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


// ===========================================================================
// [R10-INJECT] ialist / inject -- the game's OWN input, without the desktop.
//
// Real OS input (real-input.ps1) needs the game window in the foreground, and
// Windows refuses SetForegroundWindow to a background script while someone is
// using the PC: the R10 rig clicks went to the owner's window, not the game.
// Enhanced Input's StartContinuousInputInjectionForAction feeds an InputAction
// through the same mapping/trigger pipeline a key press uses, every frame until
// StopContinuousInputInjectionForAction -- so a tap and a hold both work.
//   ialist [substr]            list live UInputAction assets (name, value type)
//   inject <substr> [seconds]  hold that action for `seconds` (default 0.12)
// ===========================================================================
namespace {

struct InjectJob {
    ue::UObject* sub = nullptr;
    ue::UObject* action = nullptr;
    std::string  name;
    double       until = 0.0;
};
std::vector<InjectJob> g_inject;

ue::UObject* InputSubsystem() {
    for (ue::UObject* s : ue::FindAllOf("EnhancedInputLocalPlayerSubsystem")) {
        if (net::Valid(s)) return s;
    }
    return nullptr;
}

ue::UObject* FindInputAction(const std::string& want, std::string* exact) {
    ue::UObject* best = nullptr;
    for (ue::UObject* a : ue::FindAllOf("InputAction")) {
        if (!net::Valid(a)) continue;
        const std::string n = ue::GetName(a);
        if (n.find("Default__") == 0) continue;
        if (n == want) { if (exact) *exact = n; return a; }
        if (!best && n.find(want) != std::string::npos) { best = a; if (exact) *exact = n; }
    }
    return best;
}

ue::UFunction* InjectFn(ue::UObject* sub, const char* name) {
    if (ue::UFunction* f = ue::FindFunction(sub, name)) return f;
    const std::string path = std::string("/Script/EnhancedInput.EnhancedInputSubsystemInterface:") + name;
    if (ue::UObject* o = ue::FindObject(path.c_str())) return reinterpret_cast<ue::UFunction*>(o);
    const std::string path2 = std::string("/Script/EnhancedInput.EnhancedInputSubsystemInterface.") + name;
    if (ue::UObject* o = ue::FindObject(path2.c_str())) return reinterpret_cast<ue::UFunction*>(o);
    return nullptr;
}

struct ActionValue {   // FInputActionValue, 0x20
    double  x, y, z;
    uint8_t type;
    uint8_t pad[7];
};
static_assert(sizeof(ActionValue) == 0x20, "FInputActionValue size");

bool StartInject(ue::UObject* sub, ue::UObject* action) {
    ue::UFunction* fn = InjectFn(sub, "StartContinuousInputInjectionForAction");
    if (!fn) return false;
    uint8_t vt = 0;
    ue::Get<uint8_t>(action, "ValueType", vt);
    ActionValue v{};
    v.x = 1.0;
    v.type = vt;
    ue::Params p(fn);
    p.Set<ue::UObject*>("Action", action);
    p.Set<ActionValue>("RawValue", v);
    return ue::Call(sub, fn, p.data());
}

bool StopInject(ue::UObject* sub, ue::UObject* action) {
    ue::UFunction* fn = InjectFn(sub, "StopContinuousInputInjectionForAction");
    if (!fn || !net::Valid(sub)) return false;
    ue::Params p(fn);
    p.Set<ue::UObject*>("Action", action);
    return ue::Call(sub, fn, p.data());
}

}  // namespace

void InputList(const std::string& arg) {
    std::string want = arg;
    while (!want.empty() && (unsigned char)want.back() <= ' ') want.pop_back();
    int n = 0;
    std::string line;
    for (ue::UObject* a : ue::FindAllOf("InputAction")) {
        if (!net::Valid(a)) continue;
        const std::string nm = ue::GetName(a);
        if (nm.find("Default__") == 0) continue;
        if (!want.empty() && nm.find(want) == std::string::npos) continue;
        uint8_t vt = 0;
        ue::Get<uint8_t>(a, "ValueType", vt);
        line += net::Fmt("%s(%d) ", nm.c_str(), (int)vt);
        if (++n % 12 == 0) {
            net::Log("ialist: %s", line.c_str());
            line.clear();
        }
    }
    if (!line.empty()) net::Log("ialist: %s", line.c_str());
    net::Log("ialist: %d input action(s) match '%s' ; subsystem=%s", n, want.c_str(),
             net::Valid(InputSubsystem()) ? "yes" : "NO");
}

void Inject(const std::string& arg) {
    std::vector<std::string> f = Split(arg);
    if (f.empty()) {
        net::Log("inject: usage inject <InputAction substring> [seconds]");
        return;
    }
    double secs = 0.12;
    if (f.size() >= 2) secs = std::max(0.05, std::atof(f[1].c_str()));
    std::string exact;
    ue::UObject* action = FindInputAction(f[0], &exact);
    ue::UObject* sub = InputSubsystem();
    if (!action || !sub) {
        net::Log("inject: action '%s' %s, subsystem %s", f[0].c_str(), action ? "found" : "NOT FOUND",
                 sub ? "found" : "NOT FOUND");
        return;
    }
    const bool ok = StartInject(sub, action);
    net::Log("inject: %s for %.2fs start=%d montage=%s", exact.c_str(), secs, ok ? 1 : 0,
             MontageNow(nullptr).c_str());
    if (!ok) return;
    InjectJob j;
    j.sub = sub;
    j.action = action;
    j.name = exact;
    j.until = net::Now() + secs;
    g_inject.push_back(j);
}

bool g_labGod = false;

// labgod 0|1 -- LAB ONLY: keep our own health topped up at 15 Hz so a combat test
// is not ended by a wipe (the 150% co-op scaling kills a test pawn in seconds)
void LabGod(const std::string& arg) {
    g_labGod = std::atoi(arg.c_str()) != 0;
    net::Log("labgod: %d", g_labGod ? 1 : 0);
}

// [R10-LABGOD] coop_hithook drops every hit payload aimed at our own pawn while this is
// on: the 15 Hz top-up alone lost to a brigand burst (run 11: guest died, party wipe).
bool LabGodOn() { return g_labGod; }

void InjectTick() {
    if (g_labGod) {
        ue::UObject* pawn = net::Pawn();
        bool dead = false;
        const double hp = net::Valid(pawn) ? combat::HealthNorm(pawn, &dead) : -1.0;
        if (!dead && hp >= 0.0 && hp < 0.9) combat::SetHealthNorm(pawn, 1.0);
    }
    if (g_inject.empty()) return;
    const double now = net::Now();
    for (size_t i = 0; i < g_inject.size();) {
        if (now < g_inject[i].until) { ++i; continue; }
        const bool ok = StopInject(g_inject[i].sub, g_inject[i].action);
        net::Log("inject: %s released stop=%d montage=%s", g_inject[i].name.c_str(), ok ? 1 : 0,
                 MontageNow(nullptr).c_str());
        g_inject.erase(g_inject.begin() + (std::ptrdiff_t)i);
    }
}

void InjectReset() { g_inject.clear(); }

// ===========================================================================
// [R10-INTERDUMP] interdump [owner-substr] [call <IA-substr>]
//   The NPC interaction handlers near us: which InputAction each one expects, whether it
//   is locked / has input bound, and the game's OWN CanFocus / IsRelevant verdict with its
//   failure text.  `call` hands that InputAction to HandleInteractEvent on the nearest one
//   (the path the bound input takes).  Read-only otherwise.  Lab verb.
// ===========================================================================
void InterDump(const std::string& arg) {
    std::vector<std::string> f = Split(arg);
    std::string want, callIA;
    bool goTrigger = false;
    bool haveTrig = false;
    Vec3 trigLoc;
    for (size_t i = 0; i < f.size(); ++i) {
        if (f[i] == "call" && i + 1 < f.size()) { callIA = f[i + 1]; ++i; }
        else if (f[i] == "go") goTrigger = true;
        else want = f[i];
    }
    ue::UObject* pawn = net::Pawn();
    const Vec3 me = net::Loc(pawn);
    struct Cand { ue::UObject* c; ue::UObject* owner; double d; };
    std::vector<Cand> cs;
    for (ue::UObject* c : ue::FindAllOf("BPC_InteractionHandler_C")) {
        if (!net::Valid(c)) continue;
        ue::UObject* owner = ue::GetOuter(c);
        if (!net::Valid(owner)) continue;
        if (!want.empty() && ue::GetName(owner).find(want) == std::string::npos) continue;
        cs.push_back({c, owner, Dist(me, net::Loc(owner))});
    }
    std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    net::Log("interdump: %d handler(s) match '%s'", (int)cs.size(), want.c_str());

    auto verdict = [](ue::UObject* c, const char* fn, std::string* why) -> int {
        ue::UFunction* f = ue::FindFunction(c, fn);
        if (!f) return -1;
        ue::Params p(f);
        if (!ue::Call(c, f, p.data())) return -2;
        *why = ue::ToUtf8(p.Get<ue::FString>("FailureReasons"));
        return p.Get<bool>("ReturnValue") ? 1 : 0;
    };

    const size_t shown = std::min<size_t>(cs.size(), 4);
    for (size_t i = 0; i < shown; ++i) {
        ue::UObject* c = cs[i].c;
        std::string acts;
        const int32_t offA = ue::PropOffset(c, "Actions");
        if (offA >= 0) {
            ue::TArray<uint8_t> arr{};
            ue::ReadMem(reinterpret_cast<uint8_t*>(c) + offA, &arr, sizeof(arr));
            for (int32_t k = 0; k < arr.Num && k < 8 && arr.Data; ++k) {
                ue::FName id{};
                ue::UObject* ia = nullptr;
                ue::ReadMem(arr.Data + k * 0x40, &id, sizeof(id));
                ue::ReadMem(arr.Data + k * 0x40 + 0x38, &ia, sizeof(ia));
                acts += net::Fmt("%s->%s ", ue::ToString(id).c_str(),
                                 net::Valid(ia) ? ue::GetName(ia).c_str() : "-");
            }
        }
        int32_t handles = -1;
        const int32_t offH = ue::PropOffset(c, "InputHandles");
        if (offH >= 0) {
            ue::TArray<uint8_t> h{};
            ue::ReadMem(reinterpret_cast<uint8_t*>(c) + offH, &h, sizeof(h));
            handles = h.Num;
        }
        std::string whyF, whyR;
        const int canF = verdict(c, "CanFocus", &whyF);
        const int rel = verdict(c, "IsRelevant", &whyR);
        net::Log("interdump: [%d] %s d=%.0f locked=%d handles=%d relevant=%d(%s) focus=%d(%s) actions: %s",
                 (int)i, ue::GetName(cs[i].owner).c_str(), cs[i].d,
                 ue::GetBool(c, "InteractionLocked") ? 1 : 0, handles, rel, whyR.c_str(), canF,
                 whyF.c_str(), acts.c_str());

        // the focus conditions: a TriggerOverlap one names the box the player must stand in
        const int32_t offC = ue::PropOffset(c, "FocusConditions");
        if (offC >= 0) {
            ue::TArray<ue::UObject*> conds{};
            ue::ReadMem(reinterpret_cast<uint8_t*>(c) + offC, &conds, sizeof(conds));
            for (int32_t k = 0; k < conds.Num && k < 6 && conds.Data; ++k) {
                ue::UObject* cond = nullptr;
                ue::ReadMem(conds.Data + k, &cond, sizeof(cond));
                if (!net::Valid(cond)) continue;
                ue::UObject* trig = nullptr;
                ue::Get<ue::UObject*>(cond, "Trigger", trig);
                Vec3 tl;
                bool got = false;
                if (net::Valid(trig)) {
                    if (ue::UFunction* gl = ue::FindFunction(trig, "K2_GetComponentLocation")) {
                        ue::Params gp(gl);
                        if (ue::Call(trig, gl, gp.data())) {
                            const ue::FVector v = gp.Get<ue::FVector>("ReturnValue");
                            tl.X = v.X; tl.Y = v.Y; tl.Z = v.Z;
                            got = true;
                        }
                    }
                }
                net::Log("interdump:     cond %s trigger=%s at (%s) d=%.0f", ue::GetClassName(cond).c_str(),
                         net::Valid(trig) ? ue::GetName(trig).c_str() : "-",
                         got ? Where(tl).c_str() : "-", got ? Dist(me, tl) : -1.0);
                if (i == 0 && got && !haveTrig) { trigLoc = tl; haveTrig = true; }
            }
        }
    }

    if (goTrigger && haveTrig && net::Valid(pawn)) {
        net::Log("interdump: go -> trigger (%s)", Where(trigLoc).c_str());
        puppet::StartTp(pawn, trigLoc.X, trigLoc.Y, trigLoc.Z + 50.0);
        if (!cs.empty()) {
            const Vec3 o = net::Loc(cs[0].owner);
            const double yaw = std::atan2(o.Y - trigLoc.Y, o.X - trigLoc.X) * 180.0 / 3.14159265358979323846;
            ue::FRotator r{};
            r.Yaw = yaw;
            if (ue::UFunction* fr = ue::FindFunction(pawn, "K2_SetActorRotation")) {
                ue::Params pr(fr);
                pr.Set<ue::FRotator>("NewRotation", r);
                pr.Set<bool>("bTeleportPhysics", true);
                ue::Call(pawn, fr, pr.data());
            }
            if (ue::UObject* pc = net::PC()) {
                if (ue::UFunction* fc = ue::FindFunction(pc, "SetControlRotation")) {
                    ue::Params pc2(fc);
                    pc2.Set<ue::FRotator>("NewRotation", r);
                    ue::Call(pc, fc, pc2.data());
                }
            }
        }
    }

    if (!callIA.empty() && !cs.empty()) {
        std::string exact;
        ue::UObject* ia = FindInputAction(callIA, &exact);
        ue::UFunction* fn = ue::FindFunction(cs[0].c, "HandleInteractEvent");
        bool ok = false;
        if (ia && fn) {
            ue::Params p(fn);
            p.Set<ue::UObject*>("InputAction", ia);
            ok = ue::Call(cs[0].c, fn, p.data());
        }
        net::Log("interdump: HandleInteractEvent(%s) on %s fn=%d ia=%d call=%d", exact.c_str(),
                 ue::GetName(cs[0].owner).c_str(), fn ? 1 : 0, ia ? 1 : 0, ok ? 1 : 0);
    }
}

}  // namespace area
}  // namespace coop
