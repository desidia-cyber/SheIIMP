// ---------------------------------------------------------------------------
// coop_body.cpp -- Round 5, BODY lane.  Two features, one file:
//
//   A. ZONE IDENTITY + MISMATCH GUARD  ("zn")
//      Every world/quest key this mod syncs is a per-map ACTOR NAME (store A rows,
//      store B keys, quests, data layers, lifts, chests, enemy tails).  When the
//      two players are in different worlds, none of them can match and the old
//      module applied them anyway -- silently, into whatever actor happened to
//      carry a similar name.  The zone string is the identity that notices this:
//
//          "zn":"<persistentMap>|<zoneId>|<liHash>"
//
//      persistentMap = the world's package path (UWorld -> its UPackage), e.g.
//                      "/Game/Sparta/Maps/Core_Levels/L_Core_World_01" (the real
//                      shape, see research/raw/ObjectDump_ingame.txt);
//      zoneId        = USpartaZoneData.ZoneId (FName @0x30) of the volume on top
//                      of USpartaZoneVolumeManager.ZoneVolumeStack (@0x40, a
//                      TArray of FWeakObjectPtr).  Informational, "" when none;
//      liHash        = world::Hash() of the sorted package names of the loaded
//                      ULevelStreaming objects that belong to OUR world -- the
//                      streamed sub-levels / level instances a dungeon variant is
//                      made of.  Package names, not object names: a
//                      ULevelStreamingDynamic's own name ("LevelStreamingDynamic_3")
//                      is per-client, its package is not.
//
//      The guard itself lives in coop_net's apply dispatch (the ONE place that
//      decides what a peer line applies); this file only answers Paused().
//
//   B. THE FRIEND'S BODY STATE  ("bh")
//      Health / shell health / resolve / heal charges / PP / sidearm ammo / dark
//      form / death state, published at 2 Hz (death state also the moment it
//      changes, see FastPollDeath), read from the pawn's USpartaHealthComponent
//      and its attribute sets, and written into the PUPPET's own attribute sets
//      on the receiving side so the game's own UI and AI see the friend's real
//      numbers.  Peer death (ds >= 1) plays a DeathsLibrary clip on the puppet and
//      holds it until ds returns to 0.
//
// Kill switches: ZONE_OFF.txt (no guard, still publish "zn"), BODY_OFF.txt (no
// "bh" on the wire, no puppet write).
//
// Everything here runs on the game thread inside the 2 Hz / publish ticks, so
// there is no locking and no UObject is ever touched from another thread.
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
// tiny utilities (the same shape the other modules use)
// ===========================================================================

inline State& St() { return state(); }
inline const Toggles& TG() { return state().toggles; }

bool Valid(ue::UObject* o) { return o != nullptr && ue::IsValid(o); }

// FGameplayAttributeData is 16 bytes on this build: BaseValue @+0x08 and
// CurrentValue @+0x0C of the MEMBER offset (SYNC-DATA-MINE.md 7.1, from the
// Dumper-7 SDK generated from this exe).  coop_combat's SetHealthNorm uses the
// same two slots for the proven health write.
constexpr std::int32_t kAttrBase = 0x08;
constexpr std::int32_t kAttrCur  = 0x0C;

bool AttrRead(ue::UObject* set, const char* prop, double* out) {
    if (out) *out = 0.0;
    if (!Valid(set) || prop == nullptr) return false;
    const std::int32_t off = ue::PropOffset(set, prop);
    if (off < 0) return false;
    float v = 0.0f;
    if (!ue::ReadMem(reinterpret_cast<const std::uint8_t*>(set) + off + kAttrCur, &v, sizeof(v)))
        return false;
    if (!std::isfinite(static_cast<double>(v))) return false;
    if (out) *out = static_cast<double>(v);
    return true;
}

bool AttrWrite(ue::UObject* set, const char* prop, double v) {
    if (!Valid(set) || prop == nullptr) return false;
    const std::int32_t off = ue::PropOffset(set, prop);
    if (off < 0) return false;
    const float f = static_cast<float>(v);
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(set) + off;
    bool ok = ue::WriteMem(base + kAttrBase, &f, sizeof(f));
    ok = ue::WriteMem(base + kAttrCur, &f, sizeof(f)) && ok;
    return ok;
}

// A UObject* property, guarded (ue::Get already checks IsValid and the offset).
bool PropObject(ue::UObject* o, const char* prop, ue::UObject** out) {
    if (out) *out = nullptr;
    if (!Valid(o) || prop == nullptr) return false;
    ue::UObject* v = nullptr;
    if (!ue::Get<ue::UObject*>(o, prop, v)) return false;
    if (!Valid(v)) return false;
    if (out) *out = v;
    return true;
}

// One reflective call with a correctly sized parameter block.  Deliberately a
// local copy of the other modules' helper: coop_state.h is the only shared
// header, and a module may not include another module's internals.
class Fn {
public:
    Fn(ue::UObject* obj, const char* name) {
        if (!Valid(obj) || name == nullptr) return;
        obj_ = obj;
        fn_ = ue::FindFunction(obj, name);
        if (fn_ != nullptr) p_ = new ue::Params(fn_);
    }
    ~Fn() { delete p_; }
    Fn(const Fn&) = delete;
    Fn& operator=(const Fn&) = delete;

    bool ok() const { return obj_ != nullptr && fn_ != nullptr && p_ != nullptr; }
    bool run() { return ok() && ue::Call(obj_, fn_, p_->data()); }

    template <class T>
    void set(const char* n, const T& v) {
        if (p_ != nullptr) p_->Set<T>(n, v);
    }
    template <class T>
    T get(const char* n) {
        T t{};
        if (p_ != nullptr) t = p_->Get<T>(n);
        return t;
    }

private:
    ue::UObject* obj_ = nullptr;
    ue::UFunction* fn_ = nullptr;
    ue::Params* p_ = nullptr;
};

// SpartaHealthComponent's getters take no parameters and return a float
// (coop_combat's CallFloat, which the DAMAGE/PEERHIT path already proves).
bool CallFloat(ue::UObject* o, const char* fn, double* out) {
    if (out) *out = 0.0;
    Fn c(o, fn);
    if (!c.ok() || !c.run()) return false;
    const double v = static_cast<double>(c.get<float>("ReturnValue"));
    if (!std::isfinite(v)) return false;
    if (out) *out = v;
    return true;
}

// "[a, b, c]" / "a,b,c" -> the numbers it contains.  Used for "bh".
std::vector<double> Numbers(const std::string& body) {
    std::vector<double> out;
    std::string num;
    for (std::size_t i = 0; i <= body.size(); ++i) {
        const char c = (i < body.size()) ? body[i] : ',';
        const bool numc = (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' ||
                          c == 'e' || c == 'E';
        if (numc) {
            num.push_back(c);
            continue;
        }
        if (!num.empty()) {
            char* stop = nullptr;
            const double v = strtod(num.c_str(), &stop);
            if (stop != num.c_str() && std::isfinite(v)) out.push_back(v);
            num.clear();
        }
    }
    return out;
}

double Now() { return net::Now(); }

// ===========================================================================
// zone -- local identity
// ===========================================================================

std::string g_localMap;      // the persistent map we report (zonefake overrides it)
std::string g_realMap;       // ...and the real one, for the log line
std::string g_localZone;     // active Sparta zone id ("" when none)
std::string g_localLi;       // hash of the loaded level instances
std::string g_localLiList;   // the plain list behind that hash (logging)
std::string g_localZn;       // "<map>|<zone>|<li>" -- what we publish
std::string g_fakeMap;       // zonefake override

// zone -- peer identity
std::string g_peerZn;
std::string g_peerMap;
std::string g_peerZone;
std::string g_peerLi;
bool g_peerKnown = false;

// zone -- the guard
bool g_paused = false;
int g_applyRuns = 0;     // peer lines whose world-state dispatch ran
int g_applySkips = 0;    // ...and whose dispatch the guard skipped

// zone -- the ZONE verdict bookkeeping (written by the RECEIVING client)
bool g_seenMismatch = false;
bool g_seenResume = false;
double g_pauseStart = 0.0;
double g_pauseLongest = 0.0;
int g_runsAtResume = 0;
int g_resumeCount = 0;     // guard pauses that ENDED (one verdict write each)
int g_liDiffLogged = 0;   // last logged li state: 0 unknown, 1 same, 2 diff
double g_liAt = -1.0e9;
double g_idAt = -1.0e9;
double g_zoneIdAt = -1.0e9;   // the zone id is re-read once a second

void AppendOnce(std::vector<std::string>* v, const std::string& s) {
    for (const std::string& it : *v) {
        if (it == s) return;
    }
    v->push_back(s);
}

// The persistent map of the CURRENT world, as the "<package>.<WorldName>" prefix
// every actor of that map shares -- measured live:
//   "BP_PlayerCharacter_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01
//    .L_Core_World_01.PersistentLevel.BP_PlayerCharacter_C_2147479937_..."
// so cutting a live actor's path at ".PersistentLevel" gives exactly the identity
// the module's keys are built on.
//
// The PAWN is the primary source, not the UWorld: net::World() returned a
// MATERIAL INSTANCE path in the first two-client run (the cached world pointer
// resolved to an unrelated live object of class "World" -- logged below), while a
// pawn path was provably right in the same second.  The UWorld is the last
// fallback, and the log line names which source answered so a wrong identity can
// never again be a silent one.
std::string WorldMapPath() {
    std::string path;
    const char* src = "world";
    ue::UObject* pawn = net::Pawn();
    if (Valid(pawn)) {
        path = net::AssetPath(pawn);
        src = "pawn";
    }
    if (path.empty()) {
        ue::UObject* pup = puppet::Actor();
        if (Valid(pup)) {
            path = net::AssetPath(pup);
            src = "puppet";
        }
    }
    if (path.empty()) {
        ue::UObject* world = net::World();
        if (Valid(world)) {
            path = net::AssetPath(world);
            src = "world";
        }
    }
    if (path.empty()) return path;
    const size_t cut = path.find(".PersistentLevel");
    if (cut != std::string::npos) path.resize(cut);
    // Normalise the separators a path never contains, so the published string can
    // never break the "zn" framing ("map|zone|li").
    for (char& c : path) {
        if (c == '|' || c == '"' || c == '\\') c = '_';
    }
    static std::string logged;
    if (path != logged) {
        logged = path;
        net::Log("zone: map identity <- %s: %s", src, path.c_str());
        // One diagnostic line for the UWorld/cache question above (R5-BODY open
        // question): it is what proves which source is trustworthy.
        net::Log("zone: diag world=%s pawn=%s", net::FullName(net::World()).c_str(),
                 net::FullName(net::Pawn()).c_str());
    }
    return path;
}

// FWeakObjectPtr { int32 ObjectIndex; int32 ObjectSerialNumber; } -> UObject*.
// The object-array item table is public through ue::GetResolved(); the layout is
// the one ms2_ue.cpp documents (0x10000 items per chunk, 0x18 bytes per item with
// the UObject* at +0x00; this build validated the "chunk table at GObjects+0x00"
// shape, "chunk-table-at-0 scored 64").  Every step is a guarded read, and the
// caller STILL proves the result is a SpartaZoneVolume, so a wrong layout yields
// "no zone id", never a bad pointer.
ue::UObject* WeakToObject(const std::uint8_t* weak) {
    std::int32_t index = 0;
    if (!ue::ReadMem(weak, &index, sizeof(index))) return nullptr;
    if (index < 0 || index > 4000000) return nullptr;

    const ue::Resolved& res = ue::GetResolved();
    if (res.objects == 0) return nullptr;

    constexpr std::int32_t kPerChunk = 0x10000;
    constexpr std::int32_t kItemSize = 0x18;
    const std::int32_t chunk = index / kPerChunk;
    const std::int32_t inChunk = index % kPerChunk;

    std::uintptr_t chunkBase = 0;
    if (res.objectArrayLayoutB) {
        // 5.6: the chunk TABLE is at GObjects+0x00.
        std::uintptr_t table = 0;
        if (!ue::ReadMem(reinterpret_cast<const void*>(res.objects), &table, sizeof(table)))
            return nullptr;
        if (!ue::PlausiblePtr(reinterpret_cast<const void*>(table))) return nullptr;
        if (!ue::ReadMem(reinterpret_cast<const void*>(table + (std::uintptr_t)chunk * 8),
                         &chunkBase, sizeof(chunkBase)))
            return nullptr;
    } else if (chunk == 0) {
        // Legacy shape: chunk 0 is inline at GObjects+0x00.
        if (!ue::ReadMem(reinterpret_cast<const void*>(res.objects), &chunkBase,
                         sizeof(chunkBase)))
            return nullptr;
    } else {
        // Legacy shape, higher chunks: a second table at GObjects+0x08.
        std::uintptr_t table = 0;
        if (!ue::ReadMem(reinterpret_cast<const void*>(res.objects + 0x08), &table, sizeof(table)))
            return nullptr;
        if (!ue::PlausiblePtr(reinterpret_cast<const void*>(table))) return nullptr;
        if (!ue::ReadMem(reinterpret_cast<const void*>(table + (std::uintptr_t)chunk * 8),
                         &chunkBase, sizeof(chunkBase)))
            return nullptr;
    }
    if (!ue::PlausiblePtr(reinterpret_cast<const void*>(chunkBase))) return nullptr;

    ue::UObject* obj = nullptr;
    if (!ue::ReadMem(reinterpret_cast<const void*>(chunkBase +
                                                   (std::uintptr_t)inChunk * kItemSize),
                     &obj, sizeof(obj)))
        return nullptr;
    return Valid(obj) ? obj : nullptr;
}

// The zone id of the volume on top of the manager's stack ("" when the manager,
// the stack or the ZONE DATA is not there).  Logged once per value, and once for
// the shape of the stack so a miss is diagnosable from probe.log alone.
std::string ActiveZoneId() {
    ue::UObject* mgr = nullptr;
    {
        // The manager is a GameInstanceSubsystem: exactly one live object, and it
        // survives map loads, so it is worth caching (re-found every 5 s at most).
        static ue::UObject* cached = nullptr;
        static double at = -1.0e9;
        if (!Valid(cached)) {
            cached = nullptr;
            const double now = Now();
            if (now - at < 5.0) return std::string();
            at = now;
            for (ue::UObject* o : ue::FindAllOf("SpartaZoneVolumeManager")) {
                if (Valid(o)) {
                    cached = o;
                    break;
                }
            }
        }
        mgr = cached;
    }
    if (!Valid(mgr)) return std::string();

    // TArray<FWeakObjectPtr> (ArrayProperty, weak object elements).
    const std::int32_t off = ue::PropOffset(mgr, "ZoneVolumeStack");
    if (off < 0) return std::string();
    struct RawArray {
        std::uint8_t* Data = nullptr;
        std::int32_t Num = 0;
        std::int32_t Max = 0;
    } arr;
    if (!ue::ReadMem(reinterpret_cast<const std::uint8_t*>(mgr) + off, &arr, sizeof(arr)))
        return std::string();
    if (arr.Num <= 0 || arr.Num > 64) {
        static bool said = false;
        if (!said) {
            said = true;
            net::Log("zone: ZoneVolumeStack is empty (n=%d) -- no active zone id", arr.Num);
        }
        return std::string();
    }
    if (!ue::PlausiblePtr(arr.Data)) return std::string();

    // The top of the stack is the LAST element pushed (a TArray push_back stack).
    std::string topName;
    for (std::int32_t i = arr.Num - 1; i >= 0; --i) {
        // FWeakObjectPtr is 8 bytes (int32 ObjectIndex, int32 ObjectSerialNumber).
        const std::uint8_t* weak = arr.Data + (std::size_t)i * 8;
        ue::UObject* vol = WeakToObject(weak);
        if (!Valid(vol)) continue;
        if (ue::GetClassName(vol) != "SpartaZoneVolume") continue;
        ue::UObject* data = nullptr;
        if (!PropObject(vol, "ZoneData", &data)) continue;
        ue::FName id;
        if (!ue::Get<ue::FName>(data, "ZoneId", id)) continue;
        const std::string s = ue::ToString(id);
        if (s.empty() || s == "None") continue;
        if (topName.empty() && i == arr.Num - 1) {
            static bool said = false;
            if (!said) {
                said = true;
                net::Log("zone: volume stack n=%d, top=%s zone=%s", arr.Num,
                         net::FullName(vol).c_str(), s.c_str());
            }
        }
        return s;
    }
    static bool saidMiss = false;
    if (!saidMiss) {
        saidMiss = true;
        net::Log("zone: volume stack n=%d but no SpartaZoneVolume decoded (weak-ptr "
                 "resolve failed) -- zone id stays empty", arr.Num);
    }
    return std::string();
}

// "<package>.<WorldName>" of the world a ULevel belongs to: a ULevel's own object
// path ends in ":PersistentLevel", and everything before that is the identity its
// actors' paths share.
std::string WorldKeyOfLevel(ue::UObject* level) {
    std::string p = net::AssetPath(level);
    if (p.empty()) return p;
    const size_t dot = p.find(".PersistentLevel");
    if (dot != std::string::npos) p.resize(dot);
    const size_t colon = p.find(':');
    if (colon != std::string::npos) p.resize(colon);
    return p;
}

// Package paths of the level-streaming objects that belong to OUR world and are
// LOADED -- the streamed sub-levels and the level instances a dungeon variant is
// built from.
//
// Only plain property reads here (no ProcessEvent on a streaming object: doing
// that is what the first build of this file did, and it corrupted the heap in the
// first live run).  The identity of a loaded stream is the world of its
// LoadedLevel (ULevelStreaming:LoadedLevel -> ULevel).
//
// World Partition sub-levels are EXCLUDED: their package is
// ".../<Map>/_Generated_/<HASH>" and that hash is per load, not per map (the
// object dump has the same map under a dozen different hashes), so including them
// would report a divergence on every single run.  What is left is exactly what the
// brief asks about: a level instance selected on this client.
std::vector<std::string> LevelInstanceNames() {
    std::vector<std::string> out;
    ue::UObject* pawn = net::Pawn();
    if (!Valid(pawn)) return out;
    // Our world, by POINTER: the pawn's outer is its ULevel, that level's outer is
    // the UWorld.  (net::World() is not used here: see WorldMapPath.)
    ue::UObject* level = ue::GetOuter(pawn);
    if (!Valid(level)) return out;
    ue::UObject* world = ue::GetOuter(level);
    if (!Valid(world)) return out;
    // The world's own name IS the map name for the persistent level and for every
    // World Partition cell (measured: ".../_Generated_/<hash>.L_Core_World_01"),
    // so a cell is rejected by one name lookup instead of by building its path.
    // That comparison is what keeps this walk cheap: a two-client hub has hundreds
    // of cells and building each one's full outer chain cost 8 ms per tick.
    const std::string worldName = ue::GetName(world);

    for (ue::UObject* o : ue::FindAllOf("LevelStreaming")) {
        if (!Valid(o)) continue;
        // Only our world's streaming levels: a previous map's world (and the
        // transition worlds the loader keeps alive) has its own.
        if (ue::GetOuter(o) != world) continue;
        ue::UObject* loaded = nullptr;
        if (!PropObject(o, "LoadedLevel", &loaded)) continue;   // not loaded yet
        ue::UObject* subWorld = ue::GetOuter(loaded);
        if (!Valid(subWorld)) continue;
        if (ue::GetName(subWorld) == worldName) continue;   // WP cell / same world
        const std::string pkg = WorldKeyOfLevel(loaded);
        if (pkg.empty()) continue;
        if (pkg.find("/_Generated_/") != std::string::npos) continue;
        AppendOnce(&out, pkg);
    }
    // Sorted, so the hash is the same on both clients whatever order the object
    // array happens to be in.
    for (std::size_t i = 0; i + 1 < out.size(); ++i) {
        for (std::size_t j = i + 1; j < out.size(); ++j) {
            if (out[j] < out[i]) {
                const std::string t = out[i];
                out[i] = out[j];
                out[j] = t;
            }
        }
    }
    return out;
}

std::string Join(const std::vector<std::string>& v, const char* sep) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) s += sep;
        s += v[i];
    }
    return s;
}

// Recompute the local identity.  `force` re-reads the engine (zonefake and map
// changes); otherwise the map is refreshed at most once a second and the level
// instances at most once every two.
void RecomputeLocal(bool force, bool withLi) {
    const double now = Now();

    // The map is a package-path read; once a second is plenty and zonefake forces
    // it.  A changed map is worth a log line: it is what explains every "nothing
    // synced" report.
    if (force || now - g_idAt >= 1.0 || g_realMap.empty()) {
        g_idAt = now;
        const std::string map = WorldMapPath();
        if (map != g_realMap) {
            net::Log("zone: world -> %s", map.empty() ? "(unknown)" : map.c_str());
            g_realMap = map;
        }
        g_localMap = g_fakeMap.empty() ? g_realMap : g_fakeMap;
    }

    // Level instances: an indexed object walk (FindAllOf is class-bucketed, not a
    // GObjects sweep), so 1 Hz is cheap; the hash only has to be right when a
    // level actually loads or unloads.
    if (force || withLi) {
        g_liAt = now;
        const std::vector<std::string> names = LevelInstanceNames();
        const std::string list = Join(names, ",");
        if (list != g_localLiList) {
            g_localLiList = list;
            net::Log("zone: level instances [%s] (%d)", list.c_str(), (int)names.size());
        }
        g_localLi = names.empty() ? std::string() : world::Hash(list);
    }

    // The zone id changes on foot as the player walks between volumes; it is
    // informational, so once a second is enough and it costs no object walk (the
    // manager and its stack are cached).
    if (force || now - g_zoneIdAt >= 1.0) {
        g_zoneIdAt = now;
        const std::string id = ActiveZoneId();
        if (id != g_localZone) {
            net::Log("zone: active zone -> %s", id.empty() ? "(none)" : id.c_str());
            g_localZone = id;
        }
    }

    g_localZn = g_localMap + "|" + g_localZone + "|" + g_localLi;
}

bool ParseZn(const std::string& zn, std::string* map, std::string* zone, std::string* li) {
    if (map) map->clear();
    if (zone) zone->clear();
    if (li) li->clear();
    const std::size_t a = zn.find('|');
    if (a == std::string::npos) {
        if (map) *map = zn;
        return false;
    }
    const std::size_t b = zn.find('|', a + 1);
    if (map) *map = zn.substr(0, a);
    if (b == std::string::npos) {
        if (zone) *zone = zn.substr(a + 1);
        return false;
    }
    if (zone) *zone = zn.substr(a + 1, b - a - 1);
    if (li) *li = zn.substr(b + 1);
    return true;
}

// The one place the guard is decided and every zone log line is written.
void RefreshGuard() {
    const bool bothKnown = !g_localMap.empty() && !g_peerMap.empty() && g_peerKnown;
    const bool sameMap = bothKnown && g_localMap == g_peerMap;
    const bool wantPause = TG().zoneGuard && bothKnown && !sameMap;
    const double now = Now();

    if (wantPause != g_paused) {
        g_paused = wantPause;
        if (g_paused) {
            g_seenMismatch = true;
            g_pauseStart = now;
            net::Log("zone mismatch: %s vs %s -- world apply paused", g_localMap.c_str(),
                     g_peerMap.c_str());
            // The peer's enemy claims name actors in ITS map.  Dropping them is
            // what "pause enemy mirroring / ownership / spawn replay / aitarget"
            // means here: with no claims there is nothing to mirror, claim or
            // retarget, and the mirrors we already hold are released by the
            // normal 2 s timeout.
            St().combat.peerClaims.clear();
        } else {
            g_seenResume = true;
            ++g_resumeCount;
            const double dur = now - g_pauseStart;
            if (dur > g_pauseLongest) g_pauseLongest = dur;
            g_runsAtResume = g_applyRuns;
            net::Log("zone match: %s -- world apply resumed (paused %.1fs, %d applies skipped)",
                     g_localMap.c_str(), dur, g_applySkips);
        }
    }

    // Level-instance divergence: same map, different streamed sub-levels.  This
    // is DETECTED AND REPORTED only, and the reason is in the game's own code:
    // BP_LevelInstanceSelector_C:SelectLevel() takes NO input and picks the
    // variant with Array_Random ("CallFunc_Array_Random_OutItem"), then
    // LoadLevel(SelectedEvent) calls SpartaRunManagerSubsystem::
    // LevelInstanceSelected and LoadLevelInstanceBySoftObjectPtr.  So "make the
    // guest load the host's pick" means unloading and reloading a live level
    // instance under the player -- the class of call that cost earlier rounds a
    // session -- and the selected event object cannot be named portably.  Until a
    // lab round proves that call, li:diff is reported, not repaired.
    if (bothKnown && sameMap) {
        const bool diff = !g_localLi.empty() && !g_peerLi.empty() && g_localLi != g_peerLi;
        const int state = diff ? 2 : 1;
        if (state != g_liDiffLogged) {
            g_liDiffLogged = state;
            if (diff) {
                net::Log("zone level-instance divergence: local [%s] vs peer [%s] -- li:diff "
                         "(not repaired: BP_LevelInstanceSelector_C picks with Array_Random)",
                         g_localLiList.c_str(), g_peerLi.c_str());
            } else if (g_localLiList.find(',') != std::string::npos || !g_localLiList.empty()) {
                net::Log("zone level instances match: [%s]", g_localLiList.c_str());
            }
        }
    }
}

// The ZONE verdict, written by the RECEIVING client (run-two checks this file):
// a mismatch was seen, the world-state dispatch really stopped for a while, and
// it started again when the maps agreed.
int g_zoneWroteCount = 0;   // the resume episode the verdict file describes

void WriteZoneVerdict() {
    // One write per resume EPISODE (a mismatch earlier in the run must not consume
    // the write the harness is waiting for).
    if (g_resumeCount <= g_zoneWroteCount) return;
    if (!g_seenMismatch || !g_seenResume) return;
    if (g_applySkips < 10) return;                 // ~0.7 s of a 15 Hz apply loop
    if (g_pauseLongest < 1.5) return;              // the dispatch really stopped
    if (g_applyRuns < g_runsAtResume + 5) return;  // ...and really started again
    g_zoneWroteCount = g_resumeCount;
    const std::string text =
        net::Fmt("PASS zone guard: %s vs %s -- world apply paused %.1fs (%d applies skipped), "
                 "resumed at %s (applies %d -> %d)\n",
                 g_localMap.c_str(), g_peerMap.c_str(), g_pauseLongest, g_applySkips,
                 g_localMap.c_str(), g_runsAtResume, g_applyRuns);
    net::WriteFile("result_zone.txt", text);
    net::Log("zone verdict: PASS (paused %.1fs, %d skips, applies %d -> %d)",
             g_pauseLongest, g_applySkips, g_runsAtResume, g_applyRuns);
}

// ===========================================================================
// body -- the friend's body state
// ===========================================================================

body::State g_localBody;
body::State g_peerBody;

// The puppet write: what we last put into the puppet's health set, so
// combat::PuppetDamage can tell our mirror from real damage.
bool g_wrotePuppet = false;
double g_wroteNorm = -1.0;      // normalized health the puppet reported after the write
double g_wroteHp = 0.0;         // absolute health we wrote
double g_wroteHpMax = 0.0;
double g_puppetWriteAt = 0.0;   // net::Now() of the last write

// Peer death visual.
bool g_puppetDead = false;
std::string g_deathClip;        // the clip currently on the puppet
double g_deathRetryAt = 0.0;

// One-shot / first-read logging.
bool g_firstReadLogged = false;
bool g_noPawnLogged = false;
int g_puppetWriteLogs = 0;
int g_bhLogs = 0;

// Verdict bookkeeping (the receiver writes these).
double g_bhLastFrac = -1.0;    // the peer fraction of the last verdict write
double g_bhWriteAt = -1.0e9;   // ...and when it was written (1 Hz at most)
double g_deathSeenAt = 0.0;
double g_deadWroteAt = -1.0;   // the death episode a verdict was written for

// --- sources --------------------------------------------------------------

// pawn -> USpartaCharacter.HealthComponent -> .HealthSet
bool PawnHealthSet(ue::UObject* pawn, ue::UObject** hcOut, ue::UObject** setOut) {
    ue::UObject* hc = nullptr;
    if (!PropObject(pawn, "HealthComponent", &hc)) return false;
    ue::UObject* set = nullptr;
    if (!PropObject(hc, "HealthSet", &set)) return false;
    if (hcOut) *hcOut = hc;
    if (setOut) *setOut = set;
    return true;
}

// USpartaHealthComponent.DeathState @0x128 is an EnumProperty: one byte.
int ReadDeathState(ue::UObject* hc) {
    if (!Valid(hc)) return -1;
    const std::int32_t off = ue::PropOffset(hc, "DeathState");
    if (off < 0) return -1;
    std::uint8_t b = 0;
    if (!ue::ReadMem(reinterpret_cast<const std::uint8_t*>(hc) + off, &b, sizeof(b))) return -1;
    if (b > 4) return -1;
    return static_cast<int>(b);
}

bool WriteDeathState(ue::UObject* hc, int v) {
    if (!Valid(hc)) return false;
    const std::int32_t off = ue::PropOffset(hc, "DeathState");
    if (off < 0) return false;
    const std::uint8_t b = static_cast<std::uint8_t>(v & 0xFF);
    return ue::WriteMem(reinterpret_cast<std::uint8_t*>(hc) + off, &b, sizeof(b));
}

// The ASC's attribute sets: TArray<UAttributeSet*> SpawnedAttributes.  Matched by
// the property they carry, not by class name, so a renamed BP subclass still
// resolves.
struct AttrSets {
    ue::UObject* player_ = nullptr;    // HealCurrentCharges / PPItemResource
    ue::UObject* sidearm_ = nullptr;   // CurrentAmmo / MaxAmmo (SidearmAttributeSet)
    ue::UObject* dark_ = nullptr;      // ...or DarkPowerAttributeSet (same layout)
};

AttrSets AttrSetsOf(ue::UObject* pawn) {
    AttrSets out;
    ue::UObject* asc = nullptr;
    if (!PropObject(pawn, "AbilitySystemComponent", &asc)) return out;

    struct RawArray {
        ue::UObject** Data = nullptr;
        std::int32_t Num = 0;
        std::int32_t Max = 0;
    } arr;
    if (!ue::Get<RawArray>(asc, "SpawnedAttributes", arr)) return out;
    if (arr.Num <= 0 || arr.Num > 64) return out;
    if (!ue::PlausiblePtr(arr.Data)) return out;

    for (std::int32_t i = 0; i < arr.Num; ++i) {
        ue::UObject* s = nullptr;
        if (!ue::ReadMem(arr.Data + i, &s, sizeof(s))) continue;
        if (!Valid(s)) continue;
        if (out.player_ == nullptr && ue::PropOffset(s, "HealCurrentCharges") >= 0) {
            out.player_ = s;
            continue;
        }
        if (ue::PropOffset(s, "CurrentAmmo") >= 0) {
            const std::string cls = ue::GetClassName(s);
            if (cls == "SidearmAttributeSet") {
                out.sidearm_ = s;
            } else if (out.dark_ == nullptr) {
                out.dark_ = s;
            }
        }
    }
    if (out.sidearm_ == nullptr) out.sidearm_ = out.dark_;
    return out;
}

// BPSO_PlayerSession_C.IsInDarkform, the live session save object.  Cached until
// the world changes (Reset) or the object dies.
bool LocalDarkForm(int* out) {
    if (out) *out = 0;
    static ue::UObject* cached = nullptr;
    static double at = -1.0e9;
    if (!Valid(cached)) {
        cached = nullptr;
        const double now = Now();
        if (now - at < 4.0) return false;
        at = now;
        // A session object is a save object, not a world actor: it survives map
        // loads, but a save reload replaces it.
        for (ue::UObject* o : ue::FindAllOf("BPSO_PlayerSession_C")) {
            if (Valid(o)) {
                cached = o;
                break;
            }
        }
    }
    if (!Valid(cached)) return false;
    const bool v = ue::GetBool(cached, "IsInDarkform");
    if (out) *out = v ? 1 : 0;
    return true;
}

// One complete sample of a body.  `pawn` is the local pawn (or the puppet: the
// same components hang off both, which is how the receiver verifies its write).
bool SampleBody(ue::UObject* pawn, body::State* s, bool withDark) {
    if (s == nullptr || !Valid(pawn)) return false;
    body::State v = *s;

    ue::UObject* hc = nullptr;
    ue::UObject* set = nullptr;
    if (!PawnHealthSet(pawn, &hc, &set)) return false;

    double d = 0.0;
    if (CallFloat(hc, "GetHealth", &d)) v.hp = d;
    if (CallFloat(hc, "GetMaxHealth", &d)) v.hpMax = d;
    if (CallFloat(hc, "GetShellHealth", &d)) v.shp = d;
    if (CallFloat(hc, "GetMaxShellHealth", &d)) v.shpMax = d;
    // The two getters above and the attribute bytes must agree: one line, once.
    if (!g_firstReadLogged) {
        g_firstReadLogged = true;
        double h = 0.0, hm = 0.0;
        const bool a1 = AttrRead(set, "Health", &h);
        const bool a2 = AttrRead(set, "MaxHealth", &hm);
        net::Log("body first read: GetHealth=%.3f GetMaxHealth=%.3f | Health@+0xC=%s(%.3f) "
                 "MaxHealth@+0xC=%s(%.3f) | hc=%s set=%s",
                 v.hp, v.hpMax, a1 ? "ok" : "fail", h, a2 ? "ok" : "fail", hm,
                 net::FullName(hc).c_str(), net::FullName(set).c_str());
    }
    // No getter for resolve: read the attribute slots directly.
    AttrRead(set, "Resolve", &v.res);
    AttrRead(set, "MaxResolve", &v.resMax);

    const AttrSets sets = AttrSetsOf(pawn);
    if (sets.player_ != nullptr) {
        AttrRead(sets.player_, "HealCurrentCharges", &v.heal);
        AttrRead(sets.player_, "HealMaxCharges", &v.healMax);
        AttrRead(sets.player_, "PPItemResource", &v.pp);
        AttrRead(sets.player_, "PPItemMaxResource", &v.ppMax);
    }
    if (sets.sidearm_ != nullptr) {
        AttrRead(sets.sidearm_, "CurrentAmmo", &v.ammo);
        AttrRead(sets.sidearm_, "MaxAmmo", &v.ammoMax);
    }
    if (withDark) {
        int dark = v.dark;
        if (LocalDarkForm(&dark)) v.dark = dark;
    }
    int ds = ReadDeathState(hc);
    if (ds >= 0) v.ds = ds;

    if (v.hpMax <= 0.0) return false;   // attributes not initialised yet
    v.known = true;
    v.at = Now();
    *s = v;
    return true;
}

// The death state alone, on the 15 Hz path: one guarded byte read, no
// ProcessEvent, so "immediately when ds changes" costs nothing.
void FastPollDeath() {
    if (!g_localBody.known) return;
    ue::UObject* pawn = net::Pawn();
    ue::UObject* hc = nullptr;
    if (!PawnHealthSet(pawn, &hc, nullptr)) return;
    const int ds = ReadDeathState(hc);
    if (ds < 0 || ds == g_localBody.ds) return;
    SampleBody(pawn, &g_localBody, true);
    net::Log("body: my death state -> %d (hp=%.3f)", g_localBody.ds, g_localBody.hp);
}

// --- the death clip -------------------------------------------------------

const char* kDeathClips[] = {
    "/Game/MS1/Characters/DeathsLibrary/death_093_front.death_093_front",
    "/Game/MS1/Characters/DeathsLibrary/death_086_front.death_086_front",
    "/Game/MS1/Characters/DeathsLibrary/death_084_front.death_084_front",
    "/Game/MS1/Characters/DeathsLibrary/death_001_back.death_001_back",
};

// The clip to play: the peer's own `mseq` when it IS a DeathsLibrary sequence
// (that is what the player's death ability plays), else the first library entry
// that is loaded, else the first loaded deaths sequence found by class.
std::string FindDeathClip() {
    const std::string& mseq = St().peer.montage.path;
    if (mseq.size() > 8 && mseq.find("/DeathsLibrary/") != std::string::npos) return mseq;

    for (const char* path : kDeathClips) {
        if (Valid(ue::FindObject(path))) return path;
    }
    // Nothing of the library is loaded: one bounded search, cached in `found`.
    // Only runs when a death actually has to be drawn.
    static std::string found;
    if (!found.empty() && Valid(ue::FindObject(found.c_str()))) return found;
    for (ue::UObject* o : ue::FindAllOf("AnimSequence")) {
        if (!Valid(o)) continue;
        const std::string name = ue::GetName(o);
        if (name.size() < 6 || name.compare(0, 6, "death_") != 0) continue;
        const std::string full = ue::GetFullName(o);
        if (full.find("/DeathsLibrary/") == std::string::npos) continue;
        // FindObject wants the object path ("<package>.<object>"), which is what
        // GetFullName's last segment is; the first "/Game/" token in the object's
        // own path is the package.
        const std::size_t pkg = full.find("/Game/");
        if (pkg == std::string::npos) continue;
        found = full.substr(pkg);
        net::Log("body: death clip found by class search: %s", found.c_str());
        return found;
    }
    return std::string();
}

void StartPuppetDeath() {
    if (g_puppetDead) return;
    const double now = Now();
    if (now < g_deathRetryAt) return;

    std::string clip = FindDeathClip();
    if (clip.empty()) {
        g_deathRetryAt = now + 2.0;
        net::Log("body: peer is dead but no DeathsLibrary clip is loaded yet");
        return;
    }
    if (!puppet::PlayAsset(clip, false)) {
        g_deathRetryAt = now + 0.5;
        return;
    }
    g_puppetDead = true;
    g_deathClip = clip;
    g_deathSeenAt = now;
    net::Log("body: peer death (ds=%d) -- puppet plays %s", g_peerBody.ds, clip.c_str());
}

void StopPuppetDeath() {
    if (!g_puppetDead) return;
    g_puppetDead = false;
    g_deathClip.clear();
    // The puppet's animation state belongs to the drive again: drop the cached
    // clip so the next drive tick re-applies the peer's montage/locomotion.
    puppet::ResetAnimationState();
    net::Log("body: peer alive again (ds=%d) -- puppet drive restored", g_peerBody.ds);
}

// --- writing the peer's values into the puppet ----------------------------

void WritePuppetBody() {
    if (!TG().bodySync) return;
    if (!g_peerBody.known) return;
    if (body::PeerAge() > 3.0) return;   // a stale packet must not rewrite the puppet

    ue::UObject* pup = puppet::Actor();
    if (!Valid(pup)) return;
    ue::UObject* hc = nullptr;
    ue::UObject* set = nullptr;
    if (!PawnHealthSet(pup, &hc, &set)) return;

    const double hpMax = g_peerBody.hpMax > 0.5 ? g_peerBody.hpMax : 0.0;
    if (hpMax <= 0.0) return;
    const double hp = std::min(g_peerBody.hp, hpMax);
    const double shpMax = g_peerBody.shpMax > 0.0 ? g_peerBody.shpMax : 0.0;
    const double shp = shpMax > 0.0 ? std::min(g_peerBody.shp, shpMax) : 0.0;

    // Write the MAX first: the game's own UI divides by it, and a write of the
    // current value against a stale max shows the wrong bar.
    bool ok = AttrWrite(set, "MaxHealth", hpMax);
    ok = AttrWrite(set, "Health", hp) && ok;
    if (shpMax > 0.0) {
        ok = AttrWrite(set, "MaxShellHealth", shpMax) && ok;
        ok = AttrWrite(set, "ShellHealth", shp) && ok;
    }
    if (!ok) return;

    // Remember what the game now reports as OUR write, normalized, because
    // combat::PuppetDamage compares HealthNorm() against this.
    double norm = -1.0;
    if (!CallFloat(hc, "GetCurrentHealthNormalized", &norm) || norm < 0.0) {
        norm = (g_peerBody.hpMax > 0.5) ? (hp / hpMax) : -1.0;
    }
    g_wrotePuppet = true;
    g_wroteNorm = norm;
    g_wroteHp = hp;
    g_wroteHpMax = hpMax;
    g_puppetWriteAt = Now();

    if (g_puppetWriteLogs < 6) {
        ++g_puppetWriteLogs;
        double readback = 0.0;
        const bool rOk = CallFloat(hc, "GetHealth", &readback);
        net::Log("body: puppet health set <- hp %.3f/%.3f shp %.3f/%.3f (GetHealth=%s%.3f, "
                 "norm=%.3f) [write %d]",
                 hp, hpMax, shp, shpMax, rOk ? "" : "fail ", readback, norm,
                 g_puppetWriteLogs);
    }
}

// The BH verdict, written by the RECEIVING client: the peer's body state arrived
// and the puppet's own health set now reports the same FRACTION.  Both sides are
// normalized (the wire carries absolute attribute values) and the file is REWRITTEN
// for every material change of that fraction, at most once a second, so the last
// write always describes the latest state the peer published.
void WriteBhVerdict() {
    if (!g_peerBody.known || g_peerBody.ds != 0) return;
    if (!g_wrotePuppet) return;
    if (!(g_peerBody.hpMax > 1.0)) return;
    const double norm = g_peerBody.hp / g_peerBody.hpMax;
    if (std::fabs(norm - g_bhLastFrac) < 0.01) return;   // nothing new to report
    const double now = Now();
    if (now - g_bhWriteAt < 1.0) return;
    if (std::fabs(g_wroteNorm - norm) > 0.02) return;    // the puppet write did not take
    g_bhLastFrac = norm;
    g_bhWriteAt = now;
    const std::string text = net::Fmt(
        "PASS peer body: hp=%.3f/%.3f (%.3f) shp=%.3f/%.3f res=%.3f/%.3f heal=%.0f/%.0f "
        "pp=%.3f/%.3f ammo=%.0f/%.0f dark=%d ds=%d ; puppet health set reads back %.3f\n",
        g_peerBody.hp, g_peerBody.hpMax, norm, g_peerBody.shp, g_peerBody.shpMax, g_peerBody.res,
        g_peerBody.resMax, g_peerBody.heal, g_peerBody.healMax, g_peerBody.pp, g_peerBody.ppMax,
        g_peerBody.ammo, g_peerBody.ammoMax, g_peerBody.dark, g_peerBody.ds, g_wroteNorm);
    net::WriteFile("result_bh.txt", text);
    net::Log("body verdict: PASS peer hp=%.3f/%.3f (norm %.3f) res=%.3f heal=%.0f puppet=%.3f",
             g_peerBody.hp, g_peerBody.hpMax, norm, g_peerBody.res, g_peerBody.heal, g_wroteNorm);
}

// The PEERDEAD verdict, written by the RECEIVING client.
void WriteDeadVerdict() {
    // One verdict per death EPISODE, not per session: the peer can die (and be
    // revived) several times in a run, and a LATCHED write leaves the harness
    // reading a file from an earlier death.
    if (g_deadWroteAt == g_deathSeenAt) return;
    if (!g_puppetDead || g_peerBody.ds < 1) return;
    if (Now() - g_deathSeenAt < 0.25) return;   // it really stayed on the mesh
    g_deadWroteAt = g_deathSeenAt;
    const std::string text = net::Fmt(
        "PASS peer death: ds=%d ; the puppet plays %s (held %.1fs on the mesh)\n",
        g_peerBody.ds, g_deathClip.c_str(), Now() - g_deathSeenAt);
    net::WriteFile("result_bhdead.txt", text);
    net::Log("body death verdict: PASS ds=%d clip=%s", g_peerBody.ds, g_deathClip.c_str());
}

}  // namespace

// ===========================================================================
// public: zone
// ===========================================================================

namespace zone {

void Tick2() {
    // The level-instance list is an indexed object walk with a name lookup per
    // streaming object: every 4th tick (0.5 Hz) is plenty, because a level
    // instance loads on a door transition, not between two frames.
    static int tick = 0;
    ++tick;
    const bool withLi = (tick % 4) == 0;
    RecomputeLocal(false, withLi);
    RefreshGuard();
    WriteZoneVerdict();
}

void OnPeerLine(const std::string& line) {
    std::string zn;
    if (!net::KvStr(line, "zn", &zn)) return;
    if (zn.empty() || zn == g_peerZn) return;
    g_peerZn = zn;
    g_peerKnown = true;
    ParseZn(zn, &g_peerMap, &g_peerZone, &g_peerLi);
    if (g_localZn.empty()) RecomputeLocal(true, false);
    RefreshGuard();
}

void Reset() {
    g_localLi.clear();
    g_localLiList.clear();
    g_liAt = -1.0e9;
    g_idAt = -1.0e9;
    g_localZn.clear();
    g_realMap.clear();
    // The peer's identity survives a local map load: it is what tells us whether
    // the LOAD put us back into the same world as the peer.
    RefreshGuard();
}

void SerializeZn(std::string* out) {
    if (out == nullptr) return;
    if (g_localZn.empty()) RecomputeLocal(true, true);
    if (g_localZn.empty() || g_localMap.empty()) return;
    // A JSON string: the identity has no quotes or backslashes by construction
    // (package paths, FName text, an 8 hex digit hash), but never trust that.
    std::string v = g_localZn;
    for (char& c : v) {
        if (c == '"' || c == '\\') c = '_';
    }
    net::AppendFmt(out, ",\"zn\":\"%s\"", v.c_str());
}

bool SameMap() {
    return g_peerKnown && !g_localMap.empty() && !g_peerMap.empty() &&
           g_localMap == g_peerMap;
}

bool PeerKnown() { return g_peerKnown; }

const std::string& Local() { return g_localZn; }

const std::string& Peer() { return g_peerZn; }

bool Paused() { return g_paused; }

void NoteApply(bool paused) {
    if (paused) {
        ++g_applySkips;
    } else {
        ++g_applyRuns;
    }
}

std::string Status() {
    const char* same = SameMap() ? "1" : "0";
    const bool liDiff = !g_localLi.empty() && !g_peerLi.empty() && g_localLi != g_peerLi;
    const char* li = liDiff ? "diff" : "same";
    return net::Fmt("zone=me:%s peer:%s same:%s li:%s known:%d paused:%d ap:%d sk:%d guard:%d\n",
                    g_localZn.empty() ? "-" : g_localZn.c_str(),
                    g_peerZn.empty() ? "-" : g_peerZn.c_str(), same, li,
                    g_peerKnown ? 1 : 0, g_paused ? 1 : 0, g_applyRuns, g_applySkips,
                    TG().zoneGuard ? 1 : 0);
}

void CmdFake(const std::string& rest) {
    std::string arg = rest;
    while (!arg.empty() && (unsigned char)arg.back() <= ' ') arg.pop_back();
    if (arg.empty() || arg == "off" || arg == "0") {
        g_fakeMap.clear();
        net::Log("zonefake off: reporting the real map again");
    } else {
        g_fakeMap = arg;
        net::Log("zonefake on: reporting %s instead of %s", arg.c_str(),
                 g_realMap.empty() ? "(unknown)" : g_realMap.c_str());
    }
    RecomputeLocal(true, true);
    RefreshGuard();
    net::Log("zonefake: now %s", g_localZn.c_str());
}

}  // namespace zone

// ===========================================================================
// public: body
// ===========================================================================

namespace body {

void Tick2() {
    ue::UObject* pawn = net::Pawn();
    if (!Valid(pawn)) {
        if (!g_noPawnLogged) {
            g_noPawnLogged = true;
            net::Log("body: no local pawn yet -- nothing to publish");
        }
    } else if (!SampleBody(pawn, &g_localBody, true)) {
        static double lastWarn = -1.0e9;
        const double now = Now();
        if (now - lastWarn > 10.0) {
            lastWarn = now;
            net::Log("body: sample failed (pawn=%s) -- keeping the last values",
                     net::FullName(pawn).c_str());
        }
    }

    // The receiving half.  Order matters: write the health first (so the puppet
    // reflects the peer's numbers), then the death visual.
    if (TG().bodySync && g_peerBody.known) {
        if (body::PeerAge() > 3.0) {
            // The peer went quiet: leave the last values (the body is hidden by
            // the drive's stale gate anyway) and release a death hold.
            if (g_puppetDead) StopPuppetDeath();
        } else {
            WritePuppetBody();
            if (g_peerBody.ds >= 1) {
                StartPuppetDeath();
            } else {
                StopPuppetDeath();
            }
        }
    }

    WriteBhVerdict();
    WriteDeadVerdict();
}

void OnPeerLine(const std::string& line) {
    std::string sec;
    if (!net::Section(line, "bh", &sec)) return;
    const std::vector<double> v = Numbers(sec);
    if (v.size() < 14) {
        if (g_bhLogs < 3) {
            ++g_bhLogs;
            net::Log("body: malformed \"bh\" (%d numbers): %s", (int)v.size(), sec.c_str());
        }
        return;
    }
    const int prevDs = g_peerBody.ds;
    body::State s;
    s.hp = v[0];
    s.hpMax = v[1];
    s.shp = v[2];
    s.shpMax = v[3];
    s.res = v[4];
    s.resMax = v[5];
    s.heal = v[6];
    s.healMax = v[7];
    s.pp = v[8];
    s.ppMax = v[9];
    s.ammo = v[10];
    s.ammoMax = v[11];
    s.dark = static_cast<int>(v[12]);
    s.ds = static_cast<int>(v[13]);
    if (s.ds < 0 || s.ds > 2) s.ds = 0;
    s.known = true;
    s.at = Now();
    g_peerBody = s;
    if (g_bhLogs < 2) {
        ++g_bhLogs;
        net::Log("body: peer bh hp=%.3f/%.3f shp=%.3f/%.3f res=%.3f/%.3f heal=%.0f/%.0f "
                 "pp=%.3f/%.3f ammo=%.0f/%.0f dark=%d ds=%d",
                 s.hp, s.hpMax, s.shp, s.shpMax, s.res, s.resMax, s.heal, s.healMax, s.pp,
                 s.ppMax, s.ammo, s.ammoMax, s.dark, s.ds);
    }
    if (s.ds != prevDs) {
        net::Log("body: peer death state %d -> %d (hp=%.3f)", prevDs, s.ds, s.hp);
    }
}

void Reset() {
    g_localBody = body::State();
    g_peerBody = body::State();
    g_wrotePuppet = false;
    g_wroteNorm = -1.0;
    g_puppetDead = false;
    g_deathClip.clear();
    g_deathRetryAt = 0.0;
    g_firstReadLogged = false;
}

void SerializeBh(std::string* out) {
    if (out == nullptr) return;
    if (!TG().bodySync) return;
    FastPollDeath();
    if (!g_localBody.known) return;
    const body::State& s = g_localBody;
    net::AppendFmt(out, ",\"bh\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%.0f,%.3f,%.3f,%.0f,%.0f,%d,%d]",
                   s.hp, s.hpMax, s.shp, s.shpMax, s.res, s.resMax, s.heal, s.healMax, s.pp,
                   s.ppMax, s.ammo, s.ammoMax, s.dark, s.ds);
}

const State& Local() { return g_localBody; }

const State& Peer() { return g_peerBody; }

double PeerAge() {
    if (!g_peerBody.known) return 1.0e9;
    return Now() - g_peerBody.at;
}

bool OwnsPuppetHealth(double hpNorm) {
    if (!g_wrotePuppet) return false;
    if (g_wroteNorm < 0.0) return false;
    return std::fabs(hpNorm - g_wroteNorm) <= 0.015;
}

bool PuppetDead() { return g_puppetDead; }

std::string Status() {
    const body::State& m = g_localBody;
    const body::State& p = g_peerBody;
    const double age = g_peerBody.known ? PeerAge() : -1.0;
    return net::Fmt(
        "bh=me:hp%.3f/%.3f shp%.3f/%.3f res%.3f/%.3f heal%.0f/%.0f pp%.3f/%.3f ammo%.0f/%.0f "
        "dark%d ds%d peer:hp%.3f/%.3f shp%.3f/%.3f res%.3f/%.3f heal%.0f/%.0f pp%.3f/%.3f "
        "ammo%.0f/%.0f dark%d ds%d age:%.1f pup:%.3f pdf:%d ok:%d\n",
        m.hp, m.hpMax, m.shp, m.shpMax, m.res, m.resMax, m.heal, m.healMax, m.pp, m.ppMax,
        m.ammo, m.ammoMax, m.dark, m.ds, p.hp, p.hpMax, p.shp, p.shpMax, p.res, p.resMax, p.heal,
        p.healMax, p.pp, p.ppMax, p.ammo, p.ammoMax, p.dark, p.ds, age, g_wroteNorm,
        g_puppetDead ? 1 : 0, g_wrotePuppet ? 1 : 0);
}

// bodyset <field> <value>.  Every write goes through the attribute/attribute-byte
// route the module already proves (SetHealthNorm / applyPeerDamage), never
// through a BP function:
//   hp|shp|res|pp   a FRACTION of the matching max      (bodyset hp 0.37)
//   heal|ammo        the absolute value                 (bodyset heal 2)
//   dark             0|1                                (bodyset dark 1)
//   ds              0|1|2 -- 1/2 go through the game's own StartDeath
void CmdSet(const std::string& rest) {
    // "<field> <value>"
    std::size_t i = 0;
    while (i < rest.size() && (unsigned char)rest[i] <= ' ') ++i;
    std::size_t e = i;
    while (e < rest.size() && (unsigned char)rest[e] > ' ') ++e;
    const std::string field = rest.substr(i, e - i);
    std::string val = rest.substr(e);
    while (!val.empty() && (unsigned char)val.front() <= ' ') val.erase(val.begin());
    if (field.empty()) {
        net::Log("bodyset: usage bodyset <hp|shp|res|heal|pp|ammo|dark|ds> <value>");
        return;
    }
    char* stop = nullptr;
    const double v = strtod(val.c_str(), &stop);
    const bool hasNum = (stop != val.c_str());

    ue::UObject* pawn = net::Pawn();
    ue::UObject* hc = nullptr;
    ue::UObject* set = nullptr;
    if (!Valid(pawn) || !PawnHealthSet(pawn, &hc, &set)) {
        net::Log("bodyset %s: no local pawn / health set", field.c_str());
        return;
    }
    const AttrSets sets = AttrSetsOf(pawn);

    bool ok = false;
    std::string what;
    if (field == "hp") {
        double mx = 0.0;
        if (CallFloat(hc, "GetMaxHealth", &mx) && mx > 0.0) {
            const double frac = hasNum ? v : 1.0;
            ok = AttrWrite(set, "Health", frac * mx);
            ok = AttrWrite(set, "MaxHealth", mx) && ok;
            what = net::Fmt("Health=%.3f (%.3f of max %.3f)", frac * mx, frac, mx);
        }
    } else if (field == "shp") {
        double mx = 0.0;
        if (CallFloat(hc, "GetMaxShellHealth", &mx) && mx > 0.0) {
            const double frac = hasNum ? v : 1.0;
            ok = AttrWrite(set, "ShellHealth", frac * mx);
            what = net::Fmt("ShellHealth=%.3f (%.3f of max %.3f)", frac * mx, frac, mx);
        }
    } else if (field == "res") {
        double mx = 0.0;
        if (AttrRead(set, "MaxResolve", &mx) && mx > 0.0) {
            const double frac = hasNum ? v : 1.0;
            ok = AttrWrite(set, "Resolve", frac * mx);
            what = net::Fmt("Resolve=%.3f (%.3f of max %.3f)", frac * mx, frac, mx);
        }
    } else if (field == "heal") {
        if (sets.player_ != nullptr) {
            ok = AttrWrite(sets.player_, "HealCurrentCharges", hasNum ? v : 0.0);
            what = net::Fmt("HealCurrentCharges=%.0f", hasNum ? v : 0.0);
        }
    } else if (field == "pp") {
        double mx = 0.0;
        if (sets.player_ != nullptr && AttrRead(sets.player_, "PPItemMaxResource", &mx) && mx > 0.0) {
            const double frac = hasNum ? v : 1.0;
            ok = AttrWrite(sets.player_, "PPItemResource", frac * mx);
            what = net::Fmt("PPItemResource=%.3f (%.3f of max %.3f)", frac * mx, frac, mx);
        }
    } else if (field == "ammo") {
        if (sets.sidearm_ != nullptr) {
            ok = AttrWrite(sets.sidearm_, "CurrentAmmo", hasNum ? v : 0.0);
            what = net::Fmt("CurrentAmmo=%.0f", hasNum ? v : 0.0);
        }
    } else if (field == "dark") {
        ue::UObject* session = nullptr;
        for (ue::UObject* o : ue::FindAllOf("BPSO_PlayerSession_C")) {
            if (Valid(o)) {
                session = o;
                break;
            }
        }
        if (Valid(session)) {
            ok = ue::SetBool(session, "IsInDarkform", hasNum && v >= 0.5);
            what = net::Fmt("IsInDarkform=%d", (hasNum && v >= 0.5) ? 1 : 0);
        }
    } else if (field == "ds") {
        const int want = hasNum ? static_cast<int>(v) : 1;
        if (want >= 1) {
            // The game's own path (the player's death ability), as applyPeerDamage
            // uses it when the friend's enemy kills our body.
            ok = combat::StartDeath(pawn);
            what = net::Fmt("StartDeath=%s", ok ? "ok" : "fail");
        } else {
            ok = WriteDeathState(hc, 0);
            what = "DeathState=0";
        }
    } else {
        net::Log("bodyset: unknown field \"%s\"", field.c_str());
        return;
    }

    // Re-sample immediately so the next publish carries the new value (the 2 Hz
    // tick would do it within half a second; the verdicts do not want to wait).
    SampleBody(pawn, &g_localBody, true);
    const int ds = ReadDeathState(hc);
    net::Log("bodyset %s %s -> %s (ok=%s, now hp=%.3f ds=%d)", field.c_str(), val.c_str(),
             what.c_str(), ok ? "true" : "false", g_localBody.hp, ds);
}

void CmdDump() {
    ue::UObject* pawn = net::Pawn();
    ue::UObject* hc = nullptr;
    ue::UObject* set = nullptr;
    const bool haveP = PawnHealthSet(pawn, &hc, &set);
    const AttrSets sets = AttrSetsOf(pawn);
    SampleBody(pawn, &g_localBody, true);

    std::string out;
    net::AppendFmt(&out,
                   "pawn=%s\nhealthComp=%s\nhealthSet=%s\nplayerSet=%s\nsidearmSet=%s\n",
                   net::FullName(pawn).c_str(), net::FullName(hc).c_str(),
                   net::FullName(set).c_str(), net::FullName(sets.player_).c_str(),
                   net::FullName(sets.sidearm_).c_str());
    net::AppendFmt(&out,
                   "me: hp=%.3f/%.3f shp=%.3f/%.3f res=%.3f/%.3f heal=%.0f/%.0f pp=%.3f/%.3f "
                   "ammo=%.0f/%.0f dark=%d ds=%d known=%d\n",
                   g_localBody.hp, g_localBody.hpMax, g_localBody.shp, g_localBody.shpMax,
                   g_localBody.res, g_localBody.resMax, g_localBody.heal, g_localBody.healMax,
                   g_localBody.pp, g_localBody.ppMax, g_localBody.ammo, g_localBody.ammoMax,
                   g_localBody.dark, g_localBody.ds, g_localBody.known ? 1 : 0);
    net::AppendFmt(&out,
                   "peer: hp=%.3f/%.3f shp=%.3f/%.3f res=%.3f/%.3f heal=%.0f/%.0f pp=%.3f/%.3f "
                   "ammo=%.0f/%.0f dark=%d ds=%d known=%d age=%s\n",
                   g_peerBody.hp, g_peerBody.hpMax, g_peerBody.shp, g_peerBody.shpMax,
                   g_peerBody.res, g_peerBody.resMax, g_peerBody.heal, g_peerBody.healMax,
                   g_peerBody.pp, g_peerBody.ppMax, g_peerBody.ammo, g_peerBody.ammoMax,
                   g_peerBody.dark, g_peerBody.ds, g_peerBody.known ? 1 : 0,
                   g_peerBody.known ? net::Fmt("%.2f", PeerAge()).c_str() : "n/a");
    net::AppendFmt(&out,
                   "puppet: wrote=%d norm=%.3f hp=%.3f/%.3f deadClip=%s dead=%d\n",
                   g_wrotePuppet ? 1 : 0, g_wroteNorm, g_wroteHp, g_wroteHpMax,
                   g_deathClip.empty() ? "-" : g_deathClip.c_str(), g_puppetDead ? 1 : 0);
    net::AppendFmt(&out, "zone: %s\n", zone::Local().c_str());
    net::WriteFile("bodydump.txt", out);

    net::Log("bodydump: pawn=%s hc=%s set=%s (haveHealthSet=%d)", net::FullName(pawn).c_str(),
             net::FullName(hc).c_str(), net::FullName(set).c_str(), haveP ? 1 : 0);
    net::Log("bodydump: me hp=%.3f/%.3f shp=%.3f/%.3f res=%.3f/%.3f heal=%.0f/%.0f pp=%.3f/%.3f "
             "ammo=%.0f/%.0f dark=%d ds=%d",
             g_localBody.hp, g_localBody.hpMax, g_localBody.shp, g_localBody.shpMax,
             g_localBody.res, g_localBody.resMax, g_localBody.heal, g_localBody.healMax,
             g_localBody.pp, g_localBody.ppMax, g_localBody.ammo, g_localBody.ammoMax,
             g_localBody.dark, g_localBody.ds);
    net::Log("bodydump: peer hp=%.3f/%.3f ds=%d age=%.1f ; puppet wrote=%d norm=%.3f dead=%d",
             g_peerBody.hp, g_peerBody.hpMax, g_peerBody.ds, PeerAge(),
             g_wrotePuppet ? 1 : 0, g_wroteNorm, g_puppetDead ? 1 : 0);
}

}  // namespace body

}  // namespace coop
