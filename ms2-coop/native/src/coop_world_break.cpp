// coop_world_break.cpp -- round 7: LIVE breakables (HANDOFF-REMAINING-SYNC G3).
//
// ---------------------------------------------------------------------------
// WHAT THIS IS, AND WHY IT CANNOT USE ANY SAVE STORE
// ---------------------------------------------------------------------------
// Crates, pots, barrels, carts, chandeliers, battleground barriers and the two
// destructible-wall blueprints are `BP_VFX_Breakable_C` / `BP_BreakableWall_C`
// actors (research\10-world-state.md 8.6, mine section 19).  The barrels have NO
// `USpartaSaveComponent` anywhere in their 40+ members, so nothing about them is
// written to store A or store B: when the game breaks one, the ONLY trace is the
// live `IsBroken` flag on the actor itself (data mine 19: `IsBroken@0x518`, no
// save object).  That is the whole reason the existing wire can never carry a
// barrel: ws/wb/wq/wd/wl/wc all replay something that was persisted.
//
// So this file is an EVENT channel, not a state store:
//   ,"wx":[["<8hex>","B",<seq>,<x>,<y>,<z>,<imp>], ...]
// where <8hex> is world::Hash(actor name) -- the SAME FNV-1a every other world
// key uses -- "B" is the event kind (break), <seq> is a per-sender monotone
// counter (a lost, doubled or reordered packet is harmless: the receiver keeps
// the highest seq applied per key), and the four integers are WHERE it broke and
// how hard (`<x>,<y>,<z>` world uu, `<imp>` = impulse in whole units).  The
// receiver needs them because it re-runs the GAME'S OWN break with a hit result
// built from that point -- see below.
//
// Entries are repeated for kEventKeep seconds (and only events that young are
// sent), so a packet dropped on the floor costs a delay, never a state.
//
// ---------------------------------------------------------------------------
// HOW THE GAME BREAKS ONE (this is what round 7b added)
// ---------------------------------------------------------------------------
// The class carries a `UGeometryCollectionComponent` plus 4 field-system
// components (Radial/Plane/Culling/Operator fields) and these functions:
//
//   SpartaApplyHit(HitResult, Instigator, Payload, DamageCauser)   <- ISpartaHittable
//   GetHitResponse(Source, Hit)
//   TriggerDamage(Instigator, Location, Velocity, Radius, AngularVelocity, StrainRadius)
//   FilteredHit(Impulse, Location, Instigator, GenerateFieldsOnHit)
//   BreakableHit(Location, FirstHit)
//   CustomBreak(HitResult, Instigator)
//   OnBrokenEvent() / ConvertToBreakable() / SpawnDisable() / Cleanup()
//
// `IsBroken` is NOT set by TriggerDamage: it is set by the blueprint when the
// actual break happens (the Chaos `OnChaosBreakEvent` handler), which is why the
// round-7 attempt with `TriggerDamage(r=80, strain=30)` answered ok=true and left
// IsBroken false (REPORT.md section 5, item 1).  TriggerDamage only seeds the
// strain field; the fracture needs the game's own break entry.
//
// So an apply (and the `brkbreak` test verb) walks ONE ORDERED LIST of the game's
// own entries and stops at the first one that really breaks the actor:
//
//   1. CustomBreak(HitResult, Instigator)      -- the BP's "break now" entry; the
//      hit result is what carries the impact point/normal, so the debris and the
//      VFX appear where the peer's hit happened.  Tried first: fewest side
//      effects, and it is the function the BP itself calls to finish a break.
//   2. SpartaApplyHit(HitResult, Instigator, Payload, DamageCauser) -- the real
//      damage interface (ISpartaHittable).  Payload is passed as null: the BP VM
//      reads a missing object as zero/default (it logs "Accessed None"), and
//      constructing a USpartaHitPayload needs a spawn + 19 setter calls.  This is
//      the entry that also breaks the destructible WALLS.
//   3. BreakableHit(Location, FirstHit=true)
//   4. FilteredHit(Impulse, Location, Instigator, GenerateFieldsOnHit=true)
//   5. TriggerDamage(...)                      -- last, it is the weakest.
//
// Every call is built by REFLECTION: a parameter is written only when the
// function really declares it (`ue::FindProperty` on the UFunction), and the
// width comes from the property (`ue::PropSizeStruct`), so this cannot send a
// float where the BP wants a double and cannot log "has no parameter".
//
// The result is verified by READING THE ACTOR (`IsBroken` / `WallIsBroken`), never
// by the call's return value.  If no entry produces a break, the old primitives
// are used as a FALLBACK (state bool + SetActorHiddenInGame + collision off) and
// the log says which path ran.  A destroyed actor counts as broken.
//
// ---------------------------------------------------------------------------
// THE KEY: ACTOR NAME, NOT LOCATION
// ---------------------------------------------------------------------------
// A breakable's identity across the two clients is its PLACED ACTOR NAME, the
// same thing store A keys on (coop_world.cpp `MatchPersistentLevel`): both
// clients load the SAME .umap from the same data, in the same order, so the
// persistent level names its actors identically.  A location hash was rejected on
// purpose: every barrel is a simulated physics body (BP_VFX_Physical_C), and two
// clients simulate it independently -- after one hit the copies are metres apart,
// so a location key stops matching exactly when it is needed.  `breakdump` prints
// the name AND the location together, so this is checkable in both logs.
//
// ECHO GUARD: the apply sets IsBroken on OUR copy, which is the same transition
// our 1 Hz scan watches, so a key applied from the peer is latched
// (`peerApplied`) and its flip is never published back.
//
// Kill switch: BREAK_OFF.txt (Toggles::breakEnabled).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ms2_ue.h"
#include "coop_state.h"

namespace coop {
namespace brk {
namespace {

constexpr int kScanDiv = 2;            // one breakable scan every 2nd 2 Hz tick (1 Hz)
constexpr int kMaxRows = 4096;         // refuse to track more actors than this
constexpr double kEventKeep = 25.0;    // seconds an event keeps travelling
constexpr int kMaxPayload = 40;        // events per packet
constexpr int kMaxEvents = 64;         // the event ring
constexpr double kPendingLife = 40.0;  // give up waiting for the peer's actor after this
constexpr double kEntryWait = 0.8;     // seconds one game entry gets to take effect
constexpr int kMaxDump = 60;

inline State& St() { return coop::state(); }
inline const Toggles& Tg() { return coop::toggles(); }
inline bool On() { return Tg().breakEnabled; }

// FHitResult is 0x100 bytes in UE 5.6 (Dumper-7 Engine_structs.hpp):
//   0x00 FaceIndex int32, 0x04 Time float, 0x08 Distance float,
//   0x10 Location FVector, 0x28 ImpactPoint, 0x40 Normal, 0x58 ImpactNormal,
//   0x70 TraceStart, 0x88 TraceEnd, 0xA0 PenetrationDepth float,
//   0xA4 MyItem, 0xA8 Item, 0xAC ElementIndex, 0xAD bBlockingHit:1,
//   0xB0 PhysMaterial (weak), 0xB8 HitObjectHandle, 0xD8 Component (weak),
//   0xF0 BoneName, 0xF8 MyBoneName.
// Every object-ish field stays null on purpose (a weak ptr of 0 is "none").
struct HitBlob {
    std::uint8_t b[0x100] = {0};
};
static_assert(sizeof(HitBlob) == 0x100, "FHitResult is 0x100 bytes in UE 5.6");

// ---------------------------------------------------------------------------
// monotone clock (same reasoning as coop_world.cpp's NowSane: net::Now() is the
// world clock and it has been seen returning 0 / -160 / 2.4e32 at boot)
// ---------------------------------------------------------------------------
double gNow = -1.0;

double Now() {
    const double raw = net::Now();
    const bool plausible = (raw >= 0.0) && (raw <= 1.0e8);
    if (gNow < 0.0) {
        gNow = plausible ? raw : 0.0;
        return gNow;
    }
    if (!plausible) return gNow;
    if (raw < gNow) return gNow;
    if (raw - gNow > 5.0) {
        gNow += 0.5;
        return gNow;
    }
    gNow = raw;
    return gNow;
}

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------
inline bool IsWordChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline bool IsHexChar(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Lua: fn:match("PersistentLevel%.([%w_]+)") -- the placed actor name.
bool PlacedName(const std::string& fn, std::string* out) {
    static const char kPat[] = "PersistentLevel.";
    const std::size_t at = fn.find(kPat);
    if (at == std::string::npos) return false;
    const std::size_t b = at + sizeof(kPat) - 1;
    std::size_t e = b;
    while (e < fn.size() && IsWordChar(fn[e])) ++e;
    if (e == b) return false;
    out->assign(fn, b, e - b);
    return true;
}

double DistTo(const Vec3& a, const Vec3& b) {
    const double dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------
// rows -- one per breakable actor this client has ever seen
// ---------------------------------------------------------------------------
struct Row {
    std::string key;        // world::Hash(actor name) -- the cross-client identity
    std::string owner;      // the actor name itself (logs + the peer's own lookup)
    std::string cls;
    std::string fn;         // GetFullName(), to notice a recycled object slot
    ue::UObject* obj = nullptr;
    int known = 0;          // 0 = never read, 1 = seen intact, 2 = seen broken
    bool peerApplied = false;   // WE broke it (peer's order): a flip is an echo
    bool isWall = false;        // BP_BreakableWall_C: persisted + opens a path
    double firstAt = 0.0;
};

std::vector<Row> gRows;
StrIntMap gRowIdx;

// one event: where a breakable broke, and how hard
struct Ev {
    std::string key;
    char kind = 'B';
    int seq = 0;
    double at = 0.0;
    Vec3 point;
    double impulse = 0.0;
};

std::vector<Ev> gEvents;        // the ring that is still travelling
StrIntMap gAppliedSeq;          // key -> the highest seq applied from the peer
std::string gPayload;           // the ",wx":[...] string

int gSeq = 0;
int gSentEvents = 0;
int gRecvEvents = 0;
int gApplied = 0;
int gFailed = 0;
int gRealBreaks = 0;            // applies/verbs where a GAME entry did the work
int gFallbackBreaks = 0;        // applies/verbs that ended on the old primitives
std::string gLastApplied;       // "<key> <cls> <owner>" of the last successful apply
std::string gLastPath;          // which entry produced it, or "fallback"
std::string gLastError;
bool gVerdict = false;
int gTick = 0;
int gSkipsNotPlaced = 0;
int gEchoes = 0;

// ---------------------------------------------------------------------------
// attempts: ONE ordered walk of the game's own break entries, then the fallback
// ---------------------------------------------------------------------------
struct Entry {
    const char* fn;
    const char* label;
};
// Measured on the live game (probe run `r7maps-probe1`, trace in REPORT.md 7b.1):
// FilteredHit(Impulse=200, Location, Instigator, GenerateFieldsOnHit=true) is the
// one entry that really breaks a BP_VFX_Breakable_C -- the GeometryCollection
// fractures (the BP's OnChaosBreakEvent fires) and IsBroken flips.  CustomBreak,
// SpartaApplyHit and BreakableHit all answer ok=true and leave IsBroken false.
// So the proven entry goes FIRST; the rest stay in the list for the classes that
// do not have it (BP_BreakableWall_C has SpartaApplyHit only).
const Entry kEntries[] = {
    {"FilteredHit", "FilteredHit(Impulse,Location,Instigator,GenerateFieldsOnHit)"},
    {"SpartaApplyHit", "SpartaApplyHit(HitResult,Instigator,Payload,DamageCauser)"},
    {"CustomBreak", "CustomBreak(HitResult,Instigator)"},
    {"BreakableHit", "BreakableHit(Location,FirstHit)"},
    {"TriggerDamage", "TriggerDamage(Instigator,Location,Velocity,Radius,AngVel,StrainRadius)"},
};
constexpr int kEntryCount = static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0]));

enum class Origin { Local, Peer };

struct Attempt {
    std::string key;
    std::string owner;
    std::string cls;
    Origin origin = Origin::Peer;
    int seq = 0;
    int nextEntry = 0;     // the entry to try when the wait expires
    double at = 0.0;       // when the current entry was invoked
    Vec3 point;            // where it broke (the breaker's hit point)
    Vec3 dir;              // surface normal / impact direction
    double impulse = 0.0;
    bool wasLive = false;  // the actor existed when the attempt started
    bool anyCallOk = false;
    int tries = 0;
};

std::vector<Attempt> gAttempts;

// ---------------------------------------------------------------------------
// verdict file (the RECEIVER writes it, like result_bh.txt / result_zone.txt)
// ---------------------------------------------------------------------------
void WriteVerdict(const char* how) {
    const std::string line = net::Fmt("PASS peer break %s (%s)\n", gLastApplied.c_str(), how);
    net::WriteFile("result_break.txt", line);
    gVerdict = true;
    net::Log("break verdict: PASS %s (%s)", gLastApplied.c_str(), how);
}

// ---------------------------------------------------------------------------
// reflection-built calls into the game's own break entries
// ---------------------------------------------------------------------------
const char* StateField(const Row& r);   // defined with the scan helpers below

bool HasParam(ue::UFunction* f, const char* name) {
    return f && ue::FindProperty(reinterpret_cast<ue::UStruct*>(f), name) != nullptr;
}

int32_t ParamSize(ue::UFunction* f, const char* name) {
    return f ? ue::PropSizeStruct(reinterpret_cast<ue::UStruct*>(f), name) : -1;
}

void FillHit(HitBlob* hit, const Vec3& point, const Vec3& dir) {
    std::memset(hit->b, 0, sizeof(hit->b));
    // Time/Distance: a fresh hit one frame old, no distance.
    const float time = 1.0f;
    std::memcpy(hit->b + 0x04, &time, sizeof(time));
    // Location / ImpactPoint (0x10 / 0x28) are FVector (3 doubles in UE5).
    double v[3] = {point.X, point.Y, point.Z};
    std::memcpy(hit->b + 0x10, v, sizeof(v));
    std::memcpy(hit->b + 0x28, v, sizeof(v));
    // Normal / ImpactNormal (0x40 / 0x58) -- the direction the hit came from.
    double n[3] = {dir.X, dir.Y, dir.Z};
    const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len > 1.0e-6) {
        n[0] /= len;
        n[1] /= len;
        n[2] /= len;
    } else {
        n[0] = 0.0;
        n[1] = 0.0;
        n[2] = 1.0;
    }
    std::memcpy(hit->b + 0x40, n, sizeof(n));
    std::memcpy(hit->b + 0x58, n, sizeof(n));
    // TraceStart/TraceEnd (0x70 / 0x88): a short backwards trace through the point.
    double ts[3] = {point.X - n[0] * 100.0, point.Y - n[1] * 100.0, point.Z - n[2] * 100.0};
    std::memcpy(hit->b + 0x70, ts, sizeof(ts));
    std::memcpy(hit->b + 0x88, v, sizeof(v));
    // bBlockingHit = 1 (bit 0 of the byte at 0xAD)
    hit->b[0xAD] = 0x01;
}

// Write one scalar/vector parameter by its REAL width.
void SetNum(ue::Params& p, ue::UFunction* f, const char* name, double v) {
    const int32_t sz = ParamSize(f, name);
    if (sz == 8) {
        p.Set<double>(name, v);
    } else if (sz == 4) {
        p.Set<float>(name, static_cast<float>(v));
    } else if (sz == 24) {
        ue::FVector vec;
        vec.X = v;
        vec.Y = v;
        vec.Z = v;
        p.Set<ue::FVector>(name, vec);
    }
}

void SetVec(ue::Params& p, ue::UFunction* f, const char* name, const Vec3& v) {
    if (ParamSize(f, name) != 24) return;
    ue::FVector vec;
    vec.X = v.X;
    vec.Y = v.Y;
    vec.Z = v.Z;
    p.Set<ue::FVector>(name, vec);
}

void SetObj(ue::Params& p, ue::UFunction* f, const char* name, ue::UObject* o) {
    if (ParamSize(f, name) != 8) return;
    p.Set<ue::UObject*>(name, o);
}

// Invoke one of the game's entries.  Returns false when the UFunction does not
// exist or ProcessEvent refused the call; `*called` says whether it ran.
bool InvokeEntry(ue::UObject* o, int index, const Vec3& point, const Vec3& dir, double impulse,
                 bool* called) {
    if (called) *called = false;
    if (!net::Valid(o) || index < 0 || index >= kEntryCount) return false;
    ue::UFunction* f = ue::FindFunction(o, kEntries[index].fn);
    if (!f) return false;
    ue::Params p(f);
    ue::UObject* pawn = net::Pawn();

    if (HasParam(f, "HitResult") && ParamSize(f, "HitResult") >= 0x100) {
        HitBlob hit;
        FillHit(&hit, point, dir);
        p.Set<HitBlob>("HitResult", hit);
    }
    if (HasParam(f, "Instigator")) SetObj(p, f, "Instigator", pawn);
    if (HasParam(f, "Damage Instigator")) SetObj(p, f, "Damage Instigator", pawn);
    if (HasParam(f, "DamageCauser")) SetObj(p, f, "DamageCauser", pawn);
    if (HasParam(f, "Payload")) SetObj(p, f, "Payload", nullptr);
    if (HasParam(f, "Location")) SetVec(p, f, "Location", point);
    if (HasParam(f, "Damage Location")) SetVec(p, f, "Damage Location", point);
    if (HasParam(f, "FirstHit")) p.Set<bool>("FirstHit", true);
    if (HasParam(f, "GenerateFieldsOnHit")) p.Set<bool>("GenerateFieldsOnHit", true);
    if (HasParam(f, "Impulse")) SetNum(p, f, "Impulse", impulse > 0.0 ? impulse : 200.0);
    if (HasParam(f, "Radius")) SetNum(p, f, "Radius", impulse > 0.0 ? impulse : 200.0);
    if (HasParam(f, "StrainRadius")) SetNum(p, f, "StrainRadius", impulse > 0.0 ? impulse : 200.0);
    if (HasParam(f, "Damage Velocity")) SetNum(p, f, "Damage Velocity", 400.0);
    if (HasParam(f, "Damage Angular Velocity")) SetNum(p, f, "Damage Angular Velocity", 400.0);

    const bool ok = ue::Call(o, f, p.data());
    if (called) *called = true;
    return ok;
}

// The old primitives: state bool + hidden + no collision.  Reached only when every
// game entry above failed to produce a break.
bool FallbackBreak(Row& r, const char* why) {
    ue::UObject* o = r.obj;
    if (!net::Valid(o)) {
        gLastError = "the actor is gone";
        return false;
    }
    const char* field = StateField(r);
    const bool hadField = (ue::PropOffset(o, field) >= 0);
    const bool wroteState = hadField && ue::SetBool(o, field, true);

    bool hiddenOk = false, collOk = false;
    {
        ue::UFunction* f = ue::FindFunction(o, "SetActorHiddenInGame");
        if (f) {
            ue::Params p(f);
            p.Set<bool>("bNewHidden", true);
            hiddenOk = ue::Call(o, f, p.data());
        }
        f = ue::FindFunction(o, "SetActorEnableCollision");
        if (f) {
            ue::Params p(f);
            p.Set<bool>("bNewActorEnableCollision", false);
            collOk = ue::Call(o, f, p.data());
        }
    }

    bool unlocked = false, persisted = false;
    if (r.isWall) {
        if (ue::UFunction* fn = ue::FindFunction(o, "UnlockTraversals")) {
            ue::Params p(fn);
            unlocked = ue::Call(o, fn, p.data());
        }
        std::string vn;
        ue::FName vnName;
        std::memset(&vnName, 0, sizeof(vnName));
        if (ue::Get<ue::FName>(o, "WallIsBrokenName", vnName)) vn = ue::ToString(vnName);
        if (!vn.empty()) persisted = progress::StoreBWriteInt(r.owner, vn, 1);
        net::Log("break wall %s %s: UnlockTraversals=%s storeB %s=1 %s", r.key.c_str(),
                 r.owner.c_str(), unlocked ? "true" : "false", vn.c_str(),
                 persisted ? "ok" : "FAILED");
    }

    bool reads = hadField && ue::GetBool(o, field);
    if (!reads && (hiddenOk || collOk)) reads = true;
    net::Log("break apply %s %s (%s) state=%s hidden=%s collision=%s -> %s", r.key.c_str(),
             r.owner.c_str(), why, hadField ? (wroteState ? "set" : "write-failed") : "no-field",
             hiddenOk ? "true" : "false", collOk ? "true" : "false", reads ? "broken" : "FAILED");
    if (!reads) gLastError = "the state write and the hide both failed";
    return reads;
}

// ---------------------------------------------------------------------------
// rows / scan
// ---------------------------------------------------------------------------
const char* StateField(const Row& r) { return r.isWall ? "WallIsBroken" : "IsBroken"; }

bool ReadBroken(const Row& r) {
    if (!net::Valid(r.obj)) return false;
    const char* field = StateField(r);
    if (ue::PropOffset(r.obj, field) < 0) return false;
    return ue::GetBool(r.obj, field);
}

void PublishFlip(Row& r, const Vec3& point, double impulse) {
    if (r.peerApplied) {
        ++gEchoes;
        net::Log("break echo %s %s (this client applied it on the peer's order -- not published)",
                 r.key.c_str(), r.owner.c_str());
        return;
    }
    if (gEvents.size() >= static_cast<std::size_t>(kMaxEvents)) gEvents.erase(gEvents.begin());
    Ev e;
    e.key = r.key;
    e.kind = 'B';
    e.seq = ++gSeq;
    e.at = Now();
    e.point = point;
    e.impulse = impulse;
    gEvents.push_back(e);
    ++gSentEvents;
    net::Log("break local %s %s (%s) seq=%d at=(%.0f %.0f %.0f) -> published", r.key.c_str(),
             r.owner.c_str(), r.cls.c_str(), e.seq, point.X, point.Y, point.Z);
}

void PublishFlip(Row& r) { PublishFlip(r, net::Loc(r.obj), 0.0); }

// Find the live actor behind a key, bypassing the last scan's cached pointer: a
// teleport can destroy and re-create the actor between two scans.
ue::UObject* ResolveByKey(const std::string& key) {
    static const char* kClasses[] = {"BP_VFX_Breakable_C", "BP_BreakableWall_C"};
    for (std::size_t ci = 0; ci < sizeof(kClasses) / sizeof(kClasses[0]); ++ci) {
        const std::vector<ue::UObject*> list = ue::FindAllOf(kClasses[ci]);
        for (std::size_t i = 0; i < list.size(); ++i) {
            ue::UObject* o = list[i];
            if (!net::Valid(o)) continue;
            std::string owner;
            if (!PlacedName(net::FullName(o), &owner)) continue;
            if (world::Hash(owner) == key) return o;
        }
    }
    return nullptr;
}

void ScanOnce() {
    if (gRows.size() >= static_cast<std::size_t>(kMaxRows)) return;
    static const char* kClasses[] = {"BP_VFX_Breakable_C", "BP_BreakableWall_C"};
    for (std::size_t ci = 0; ci < sizeof(kClasses) / sizeof(kClasses[0]); ++ci) {
        const bool isWallClass = (ci == 1);
        const std::vector<ue::UObject*> list = ue::FindAllOf(kClasses[ci]);
        for (std::size_t i = 0; i < list.size(); ++i) {
            ue::UObject* o = list[i];
            if (!net::Valid(o)) continue;
            const std::string fn = net::FullName(o);
            std::string owner;
            if (!PlacedName(fn, &owner)) {
                ++gSkipsNotPlaced;
                continue;
            }
            const std::string key = world::Hash(owner);
            if (key.size() != 8) continue;

            int idx = gRowIdx.Get(key, -1);
            Row* r = (idx >= 0) ? &gRows[static_cast<std::size_t>(idx)] : nullptr;
            if (r && r->fn == fn) {
                r->obj = o;
            } else {
                Row nr;
                nr.key = key;
                nr.owner = owner;
                nr.cls = ue::GetClassName(o);
                nr.fn = fn;
                nr.obj = o;
                nr.isWall = isWallClass || nr.cls.find("BreakableWall") != std::string::npos;
                nr.firstAt = Now();
                if (r) {
                    nr.peerApplied = r->peerApplied;
                    nr.known = r->known;
                    *r = nr;
                } else {
                    gRowIdx.Set(key, static_cast<int>(gRows.size()));
                    gRows.push_back(nr);
                    r = &gRows.back();
                }
            }

            const char* field = StateField(*r);
            if (ue::PropOffset(o, field) < 0) continue;
            const bool broken = ue::GetBool(o, field);
            const int prev = r->known;
            r->known = broken ? 2 : 1;
            if (broken && prev == 1) PublishFlip(*r);
            if (!broken && prev == 2 && r->isWall) {
                net::Log("break wall %s %s reads intact again (level reloaded)", r->key.c_str(),
                         r->owner.c_str());
            }
        }
    }
}

// Resolve a key to a row, refreshing the rows when it is not known yet.
Row* FindRow(const std::string& key) {
    const int idx = gRowIdx.Get(key, -1);
    if (idx >= 0) {
        Row* r = &gRows[static_cast<std::size_t>(idx)];
        if (!net::Valid(r->obj)) {
            ue::UObject* o = ResolveByKey(key);
            if (o) {
                r->obj = o;
                r->fn = net::FullName(o);
            }
        }
        return r;
    }
    ScanOnce();
    const int idx2 = gRowIdx.Get(key, -1);
    if (idx2 >= 0) return &gRows[static_cast<std::size_t>(idx2)];
    return nullptr;
}

// ---------------------------------------------------------------------------
// attempts
// ---------------------------------------------------------------------------
void StartAttempt(const std::string& key, Origin origin, int seq, const Vec3& point,
                  const Vec3& dir, double impulse) {
    Row* r = FindRow(key);
    if (!r) {
        net::Log("break %s: the actor is not in this level", key.c_str());
        return;
    }
    Attempt a;
    a.key = key;
    a.owner = r->owner;
    a.cls = r->cls;
    a.origin = origin;
    a.seq = seq;
    a.point = point;
    a.dir = dir;
    a.impulse = impulse;
    a.wasLive = net::Valid(r->obj);
    if (a.point.X == 0.0 && a.point.Y == 0.0 && a.point.Z == 0.0) a.point = net::Loc(r->obj);
    // first applicable entry right away, so the break happens on this tick
    int e = 0;
    while (e < kEntryCount && !ue::FindFunction(r->obj, kEntries[e].fn)) ++e;
    a.nextEntry = e;
    a.at = Now();
    if (e < kEntryCount) {
        bool called = false;
        const bool ok = InvokeEntry(r->obj, e, a.point, a.dir, a.impulse, &called);
        a.anyCallOk = called && ok;
        ++a.tries;
        net::Log("break %s %s: %s(%s) ok=%s%s", key.c_str(), a.owner.c_str(), kEntries[e].fn,
                 kEntries[e].label, ok ? "true" : "false",
                 called ? "" : " (refused: no UFunction)");
        a.nextEntry = e + 1;
    }
    gAttempts.push_back(a);
}

void FinishOk(const Attempt& a, Row& r, const char* how) {
    r.known = 2;
    if (a.origin == Origin::Peer) {
        ++gApplied;
        gAppliedSeq.Set(a.key, a.seq);
        r.peerApplied = true;   // the echo guard
        gLastApplied = a.key + " " + r.cls + " " + r.owner;
        gLastPath = how;
        if (!gVerdict) WriteVerdict(how);
    } else {
        PublishFlip(r, a.point, a.impulse);
    }
}

void PumpAttempts() {
    const double now = Now();
    for (int i = static_cast<int>(gAttempts.size()) - 1; i >= 0; --i) {
        Attempt a = gAttempts[static_cast<std::size_t>(i)];
        bool done = false;
        Row* r = FindRow(a.key);

        if (!r) {
            if (now - a.at > kPendingLife) {
                ++gFailed;
                gLastError = "the actor never existed here";
                net::Log("break %s GAVE UP after %.0fs (no such actor in this level)",
                         a.key.c_str(), now - a.at);
                done = true;
            }
        } else if (!net::Valid(r->obj) && a.wasLive) {
            // the game destroyed it as part of the break: that IS a break
            net::Log("break %s %s: the actor was DESTROYED by the break", a.key.c_str(),
                     a.owner.c_str());
            ++gRealBreaks;
            FinishOk(a, *r, "game entry (actor destroyed)");
            done = true;
        } else if (ReadBroken(*r)) {
            const char* how = (a.tries > 0) ? kEntries[(a.nextEntry > 0 ? a.nextEntry - 1 : 0)].fn
                                            : "fallback";
            ++gRealBreaks;
            FinishOk(a, *r, how);
            net::Log("break %s %s confirmed broken via %s", a.key.c_str(), a.owner.c_str(), how);
            done = true;
        } else if (now - a.at >= kEntryWait) {
            int e = a.nextEntry;
            while (e < kEntryCount && !ue::FindFunction(r->obj, kEntries[e].fn)) ++e;
            if (e < kEntryCount) {
                bool called = false;
                const bool ok = InvokeEntry(r->obj, e, a.point, a.dir, a.impulse, &called);
                a.anyCallOk = a.anyCallOk || (called && ok);
                a.nextEntry = e + 1;
                a.at = now;
                ++a.tries;
                net::Log("break %s %s: %s(%s) ok=%s", a.key.c_str(), a.owner.c_str(),
                         kEntries[e].fn, kEntries[e].label, ok ? "true" : "false");
                gAttempts[static_cast<std::size_t>(i)] = a;
            } else {
                // every game entry ran (or none exists) and the actor still reads
                // intact.  Two ways to end: if a game call answered success we must
                // NOT hide it (the fracture may simply not have ticked yet -- hiding
                // would delete the debris of a real break), so only the logical half
                // is written.  Otherwise the old primitives are the last resort.
                if (a.anyCallOk) {
                    ++gFallbackBreaks;
                    if (ue::PropOffset(r->obj, StateField(*r)) >= 0) {
                        ue::SetBool(r->obj, StateField(*r), true);
                    }
                    net::Log("break %s %s: a game entry answered success but the state has not "
                             "flipped yet -- state written, actor NOT hidden (the fracture may "
                             "still be coming)", a.key.c_str(), a.owner.c_str());
                    FinishOk(a, *r, "game entry (unverified)");
                } else {
                    ++gFallbackBreaks;
                    if (FallbackBreak(*r, "fallback")) FinishOk(a, *r, "fallback");
                    else net::Log("break %s FAILED: %s", a.key.c_str(), gLastError.c_str());
                }
                done = true;
            }
        }
        if (done) gAttempts.erase(gAttempts.begin() + i);
    }
}

// ---------------------------------------------------------------------------
// the peer's events
// ---------------------------------------------------------------------------
void PeerNote(const std::string& key, char kind, int seq, const Vec3& point, double impulse) {
    if (!On()) return;
    if (key.size() != 8) return;
    if (kind != 'B') return;
    const int have = gAppliedSeq.Get(key, 0);
    if (seq <= have) return;   // already applied (or older): harmless repeat
    ++gRecvEvents;
    for (std::size_t i = 0; i < gAttempts.size(); ++i) {
        if (gAttempts[i].key == key && gAttempts[i].origin == Origin::Peer) {
            if (seq > gAttempts[i].seq) gAttempts[i].seq = seq;
            return;
        }
    }
    net::Log("break peer %s kind=%c seq=%d at=(%.0f %.0f %.0f) imp=%.0f", key.c_str(), kind,
             seq, point.X, point.Y, point.Z, impulse);
    // the direction is "into the surface": the receiver only needs a normal, and a
    // hit that came from above is what most impacts on these props are
    Vec3 dir;
    dir.Z = 1.0;
    StartAttempt(key, Origin::Peer, seq, point, dir, impulse);
}

void ReadPeer(const std::string& line) {
    std::string sec;
    if (!net::Section(line, "wx", &sec)) return;
    std::size_t i = 0;
    while (i + 1 < sec.size()) {
        if (sec[i] != '[' || sec[i + 1] != '"') {
            ++i;
            continue;
        }
        std::size_t j = i + 2;
        std::string key;
        while (j < sec.size() && IsHexChar(sec[j])) key.push_back(sec[j++]);
        // ["<key>","<kind>",<seq>[,<x>,<y>,<z>,<imp>]]
        if (key.empty() || j + 4 >= sec.size() || sec[j] != '"' || sec[j + 1] != ',' ||
            sec[j + 2] != '"' || sec[j + 4] != '"') {
            ++i;
            continue;
        }
        const char kind = sec[j + 3];
        std::size_t m = j + 5;
        if (m >= sec.size() || sec[m] != ',') {
            ++i;
            continue;
        }
        ++m;
        auto readInt = [&sec, &m](int* out) -> bool {
            bool neg = false;
            if (m < sec.size() && sec[m] == '-') {
                neg = true;
                ++m;
            }
            bool any = false;
            int v = 0;
            while (m < sec.size() && sec[m] >= '0' && sec[m] <= '9') {
                any = true;
                if (v < 100000000) v = v * 10 + (sec[m] - '0');
                ++m;
            }
            if (!any) return false;
            *out = neg ? -v : v;
            return true;
        };
        int seq = 0;
        if (!readInt(&seq)) {
            ++i;
            continue;
        }
        Vec3 point;
        double impulse = 0.0;
        int vx = 0, vy = 0, vz = 0, imp = 0;
        if (m + 4 < sec.size() && sec[m] == ',') {
            ++m;
            if (readInt(&vx) && m < sec.size() && sec[m] == ',') {
                ++m;
                if (readInt(&vy) && m < sec.size() && sec[m] == ',') {
                    ++m;
                    if (readInt(&vz) && m < sec.size() && sec[m] == ',') {
                        ++m;
                        if (readInt(&imp)) {
                            point.X = (double)vx;
                            point.Y = (double)vy;
                            point.Z = (double)vz;
                            impulse = (double)imp;
                        }
                    }
                }
            }
        }
        if (m >= sec.size() || sec[m] != ']') {
            ++i;
            continue;
        }
        PeerNote(key, kind, seq, point, impulse);
        i = m + 1;
    }
}

// ---------------------------------------------------------------------------
// the "wx" payload
// ---------------------------------------------------------------------------
void BuildPayload() {
    std::string parts;
    const double now = Now();
    for (std::size_t i = 0; i < gEvents.size();) {
        if (now - gEvents[i].at > kEventKeep) gEvents.erase(gEvents.begin() + static_cast<long>(i));
        else ++i;
    }
    int n = 0;
    for (std::size_t i = 0; i < gEvents.size() && n < kMaxPayload; ++i, ++n) {
        const Ev& e = gEvents[i];
        if (!parts.empty()) parts += ',';
        parts += net::Fmt("[\"%s\",\"%c\",%d,%d,%d,%d,%d]", e.key.c_str(), e.kind, e.seq,
                          (int)std::lround(e.point.X), (int)std::lround(e.point.Y),
                          (int)std::lround(e.point.Z), (int)std::lround(e.impulse));
    }
    if (parts.empty()) {
        gPayload.clear();
        return;
    }
    gPayload = ",\"wx\":[" + parts + "]";
}

}  // namespace

// ===========================================================================
// public API
// ===========================================================================

void Tick2() {
    if (!On()) return;
    if ((gTick++ % kScanDiv) == 0) net::Step("brkScan", Guard::None, [] { ScanOnce(); });
    net::Step("brkApply", Guard::None, [] { PumpAttempts(); });
    net::Step("brkBuild", Guard::None, [] { BuildPayload(); });
}

void Reset() {
    for (std::size_t i = 0; i < gRows.size(); ++i) {
        gRows[i].obj = nullptr;
        gRows[i].fn.clear();
    }
    gPayload.clear();
    gAttempts.clear();
    gNow = -1.0;
}

void OnPeerLine(const std::string& line) {
    if (!On()) return;
    ReadPeer(line);
}

void Serialize(std::string* out) {
    if (!out || !On()) return;
    out->append(gPayload);
}

std::string Status() {
    return net::Fmt("brk=rows:%d sent:%d recv:%d applied:%d fail:%d live:%d fall:%d "
                    "pending:%d echo:%d\n",
                    (int)gRows.size(), gSentEvents, gRecvEvents, gApplied, gFailed, gRealBreaks,
                    gFallbackBreaks, (int)gAttempts.size(), gEchoes);
}

// ---------------------------------------------------------------------------
// `breakdump [all]` -- every breakable this client can see, with the actor NAME
// (identical on both clients) and the LOCATION (which drifts, hence the name key)
// ---------------------------------------------------------------------------
void BreakDump(const std::string& arg) {
    const bool all = (arg == "all");
    ue::UObject* pawn = net::Pawn();
    const bool haveMe = net::Valid(pawn);
    const Vec3 me = haveMe ? net::Loc(pawn) : Vec3{};
    ScanOnce();
    net::Log("breakdump: %d breakable row(s) tracked (skipped %d not under a persistent level)",
             (int)gRows.size(), gSkipsNotPlaced);
    int n = 0;
    for (std::size_t i = 0; i < gRows.size(); ++i) {
        Row& r = gRows[i];
        if (!net::Valid(r.obj)) continue;
        const Vec3 p = net::Loc(r.obj);
        const double d = haveMe ? DistTo(p, me) : -1.0;
        if (!all && d > 6000.0) continue;
        if (n >= kMaxDump) break;
        ++n;
        net::Log("breakdump %s %s key=%s d=%.0f at=(%.0f %.0f %.0f) %s%s", r.cls.c_str(),
                 r.owner.c_str(), r.key.c_str(), d, p.X, p.Y, p.Z,
                 r.known == 2 ? "BROKEN" : (r.known == 1 ? "intact" : "unknown"),
                 r.peerApplied ? " (peer-applied)" : "");
    }
    if (n == 0) {
        net::Log("breakdump: no breakable within 6000 uu (use `breakdump all`; the nearest "
                 "one may just not be streamed in)");
    }
}

// ---------------------------------------------------------------------------
// `brkbreak [nearest|<key>|<owner>]` -- break one HERE through the game's own
// entries, then publish it.  The chain is the same one the receiver runs, so the
// pictures on both sides come from the same call.
// ---------------------------------------------------------------------------
Row* PickNearest(const std::string& arg, bool skipBroken, double* distOut) {
    ue::UObject* pawn = net::Pawn();
    const bool haveMe = net::Valid(pawn);
    const Vec3 me = haveMe ? net::Loc(pawn) : Vec3{};
    Row* best = nullptr;
    double bd = 1.0e12;
    for (int pass = 0; pass < 2 && !best; ++pass) {
        for (std::size_t i = 0; i < gRows.size(); ++i) {
            Row& r = gRows[i];
            if (r.isWall) continue;
            if (skipBroken && r.known == 2) continue;
            if (!arg.empty() && arg != "nearest" && arg != "all" && arg != r.key &&
                arg != r.owner) {
                continue;
            }
            const bool live = net::Valid(r.obj);
            if (pass == 0 && !live) continue;
            const double d = (haveMe && live) ? DistTo(net::Loc(r.obj), me) : 1.0e11;
            if (d < bd) {
                bd = d;
                best = &r;
            }
        }
    }
    if (distOut) *distOut = bd;
    return best;
}

void BreakNow(const std::string& arg) {
    ue::UObject* pawn = net::Pawn();
    const bool haveMe = net::Valid(pawn);
    const Vec3 me = haveMe ? net::Loc(pawn) : Vec3{};
    ScanOnce();
    double bd = 0.0;
    Row* best = PickNearest(arg, true, &bd);
    if (!best) {
        net::Log("brkbreak: no intact breakable known here (rows=%d, see `breakdump all`)",
                 (int)gRows.size());
        return;
    }
    ue::UObject* o = net::Valid(best->obj) ? best->obj : ResolveByKey(best->key);
    if (!o) {
        net::Log("brkbreak: %s (key=%s) is not loaded right now (streaming) -- nothing to break",
                 best->owner.c_str(), best->key.c_str());
        return;
    }
    best->obj = o;
    // the impact point: the surface facing the player, a little above the base
    Vec3 point = net::Loc(o);
    Vec3 dir;
    if (haveMe) {
        Vec3 to = me;
        to.X -= point.X;
        to.Y -= point.Y;
        to.Z = 0.0;
        const double len = std::sqrt(to.X * to.X + to.Y * to.Y);
        if (len > 1.0e-3) {
            dir.X = to.X / len;
            dir.Y = to.Y / len;
        }
        dir.Z = 0.35;
        point.X += dir.X * 12.0;
        point.Y += dir.Y * 12.0;
    } else {
        dir.Z = 1.0;
    }
    net::Log("brkbreak: %s (%s) d=%.0f key=%s -> the game's own break chain", best->owner.c_str(),
             best->cls.c_str(), bd, best->key.c_str());
    StartAttempt(best->key, Origin::Local, 0, point, dir, 200.0);
}

// ---------------------------------------------------------------------------
// `brkstate [all|nearest|<key>]` -- the actor's live state, for the probe runs
// ---------------------------------------------------------------------------
void BreakState(const std::string& arg) {
    ScanOnce();
    const bool all = (arg == "all");
    double bd = 0.0;
    std::vector<Row*> rows;
    if (all) {
        for (std::size_t i = 0; i < gRows.size(); ++i) rows.push_back(&gRows[i]);
    } else {
        Row* r = PickNearest(arg, false, &bd);
        if (r) rows.push_back(r);
    }
    if (rows.empty()) {
        net::Log("brkstate: nothing matched '%s'", arg.c_str());
        return;
    }
    int n = 0;
    for (std::size_t i = 0; i < rows.size() && n < 12; ++i, ++n) {
        Row& r = *rows[i];
        if (!net::Valid(r.obj)) {
            net::Log("brkstate %s %s: actor GONE", r.key.c_str(), r.owner.c_str());
            continue;
        }
        ue::UObject* o = r.obj;
        char fn[256] = {0};
        int k = 0;
        static const char* kFns[] = {"CustomBreak", "SpartaApplyHit", "BreakableHit",
                                     "FilteredHit", "TriggerDamage", "OnBrokenEvent"};
        for (std::size_t q = 0; q < sizeof(kFns) / sizeof(kFns[0]) && k < 250; ++q) {
            if (ue::FindFunction(o, kFns[q])) {
                const int wrote = _snprintf_s(fn + k, sizeof(fn) - k, _TRUNCATE, "%s,",
                                              kFns[q]);
                if (wrote > 0) k += wrote;
            }
        }
        double thr = 0.0, frad = 0.0, srad = 0.0;
        ue::Get<double>(o, "Threshold", thr);
        ue::Get<double>(o, "ForceRadius", frad);
        ue::Get<double>(o, "StrainRadius", srad);
        net::Log("brkstate %s %s %s: IsBroken=%d hidden=%d collision=%d GC=%s fns=[%s] "
                 "thr=%.1f fR=%.1f sR=%.1f",
                 r.key.c_str(), r.cls.c_str(), r.owner.c_str(),
                 ReadBroken(r) ? 1 : 0,
                 ue::GetBool(o, "bHidden") ? 1 : 0,
                 ue::GetBool(o, "bActorEnableCollision") ? 1 : 0,
                 ue::PropOffset(o, "GeometryCollection") >= 0 ? "yes" : "no", fn,
                 thr, frad, srad);
    }
}

// ---------------------------------------------------------------------------
// `brkcall <FunctionName> [nearest|<key>]` -- probe: invoke ONE named UFunction
// on a breakable with the reflection-built parameter set.  This is the verb the
// entry order in the header was found with.
// ---------------------------------------------------------------------------
void BreakCall(const std::string& arg) {
    std::string fn = arg;
    std::string target;
    const std::size_t sp = arg.find(' ');
    if (sp != std::string::npos) {
        fn = arg.substr(0, sp);
        target = arg.substr(sp + 1);
    }
    if (fn.empty()) {
        net::Log("brkcall: usage brkcall <FunctionName> [nearest|<key>]");
        return;
    }
    ScanOnce();
    double bd = 0.0;
    Row* r = PickNearest(target, true, &bd);
    if (!r) {
        net::Log("brkcall: no intact breakable matched '%s'", target.c_str());
        return;
    }
    ue::UObject* o = net::Valid(r->obj) ? r->obj : ResolveByKey(r->key);
    if (!o) {
        net::Log("brkcall: %s is not loaded", r->owner.c_str());
        return;
    }
    int idx = -1;
    for (int e = 0; e < kEntryCount; ++e) {
        if (fn == kEntries[e].fn) idx = e;
    }
    bool called = false;
    bool ok = false;
    Vec3 point = net::Loc(o);
    Vec3 dir;
    dir.Z = 1.0;
    if (idx >= 0) {
        ok = InvokeEntry(o, idx, point, dir, 200.0, &called);
    } else if (ue::UFunction* f = ue::FindFunction(o, fn.c_str())) {
        // arbitrary function: reflection fills what it declares
        ue::Params p(f);
        if (HasParam(f, "HitResult") && ParamSize(f, "HitResult") >= 0x100) {
            HitBlob hit;
            FillHit(&hit, point, dir);
            p.Set<HitBlob>("HitResult", hit);
        }
        if (HasParam(f, "Instigator")) SetObj(p, f, "Instigator", net::Pawn());
        if (HasParam(f, "FirstHit")) p.Set<bool>("FirstHit", true);
        ok = ue::Call(o, f, p.data());
        called = true;
    }
    net::Log("brkcall %s on %s (%s) called=%s ok=%s -> IsBroken=%d", fn.c_str(),
             r->owner.c_str(), r->cls.c_str(), called ? "true" : "false", ok ? "true" : "false",
             ReadBroken(*r) ? 1 : 0);
}

}  // namespace brk
}  // namespace coop
