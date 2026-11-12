// coop_farenemy.cpp -- round 8: a REPLAYED enemy that no local player is near is
// destroyed by the game ~0.4-1.6 s after it spawns, at FULL health.
//
// ---------------------------------------------------------------------------
// WHAT ROUND 6 ALREADY ESTABLISHED
// ---------------------------------------------------------------------------
// docs/R6-SPAWN-REPORT.md section 3: on the client that replays a proximity spawner
// whose area no local player is standing in, the spawner runs, the Blueprint creates
// the enemy, loads its weapon, runs its abilities -- and about a second later the
// actor is gone, at hp = 1.000, while the same spawner keeps its enemy for the whole
// session as soon as the local player is 1 600 uu from it.  Round 6 could only prove
// what it was NOT:
//   - not a death: the last health read before the actor vanished was 1.000;
//   - not the spawner's Blueprint: a ProcessEvent probe on the spawner showed no
//     TryDespawnEnemy / HandleWorldStreaming / SpawnFailed call at all;
//   - not a Blueprint-callable destroy: Actor.K2_DestroyActor was hooked and never
//     fired for those actors, so the destroy is NATIVE (a C++ call, no ProcessEvent);
//   - not the spawner's streaming gate: WaitForStreaming=0 on both clients.
//
// So the question this file answers is "who destroys it, and can it be made to
// stay".  The destroy being native rules out every ProcessEvent veto, so the
// instruments here are the three things that ARE readable from a mod:
//
//   1. AActor::ReceiveEndPlay's EndPlayReason -- the engine's own answer to "how did
//      this actor leave the world".  Destroyed (0) is a Destroy() call;
//      RemovedFromWorld (3) / LevelTransition (1) is streaming/level teardown.
//      ReceiveEndPlay is a BlueprintImplementableEvent, i.e. its C++ body is a
//      ProcessEvent thunk, so an ordinary ue::HookPost on it fires for every actor
//      (BP or native) -- and it is the ONE thing that runs for a native destroy too.
//   2. AActor's own life span: SetLifeSpan(N) is how a native subsystem asks the
//      engine to destroy an actor N seconds from now WITHOUT calling anything a mod
//      can hook -- the timer calls AActor::Destroy natively.  A ~1 s lifetime at full
//      health is exactly that shape, and GetLifeSpan() (reflected) reads the
//      remaining time, so it can be watched counting down.
//   3. the world's streaming state around the enemy, through the game's own
//      UWorldPartitionSubsystem::IsStreamingCompleted query, the streaming sources
//      this client has (`fardump`) and the spawner's own survival.
//
// ...and the fix is what the measurement says it is (REPORT.md section 2).  A
// replayed copy the peer is responsible for must live as long as the peer's copy
// does, so the mod keeps that actor's engine life span at zero while it is watched.
//
// ---------------------------------------------------------------------------
// TEST HOOKS (cmd.txt, 2 Hz: net::RunCommands)
// ---------------------------------------------------------------------------
//   fardump        -- what this client sees: claimed spawners and the enemy each
//                     produced, its life span/health, the cell state at the enemy,
//                     and every streaming source this client has.
//   farfix <mode>  -- runtime A/B of the fix: off | life | stream | all | auto.
//                     `auto` follows FARENEMY_OFF.txt (the shipped kill switch).
//
// Always-on log lines (bounded, cheap):
//   farenemy spawn <key> ...    once per replayed enemy instance
//   farenemy alive <key> ...    1 Hz while it lives (life span, hp, distance)
//   farenemy gone  <key> ...    once when the instance disappears, with the last
//                               sample and the EndPlayReason the hook captured
//   farenemy endplay ...        the hook firing on a tracked actor (or on one inside
//                               a claimed spawner's area)
//   farenemy verdict ...        30 s of continuous life = the acceptance; it writes
//                               result_farenemy.txt for the harness.

#include "ms2_ue.h"
#include "coop_state.h"

// CaptureStackBackTrace: the destroy is NATIVE, so the return-address stack captured
// inside the EndPlay hook is the only way to name the code that asked for it (the
// EndPlay thunk sits below AActor::Destroy, and below that whatever called it).
// WIN32_LEAN_AND_MEAN/NOMINMAX are set by the build; this header is used only for
// that one kernel32 entry point, and nothing here is named `near`/`far`.
#include <windows.h>
// MinHook is already linked into this DLL (ms2_ue.cpp detours ProcessEvent with it);
// the veto in section 4 needs it for one more target.
#include "MinHook.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <intrin.h>   // _ReturnAddress(), for the destroy caller's RVA
#include <cstring>
#include <string>
#include <vector>

namespace coop {
namespace farenemy {

namespace {

using ue::UObject;
using ue::UFunction;

// ---------------------------------------------------------------------------
// clocks, locality: the same primitives every module keeps its own copy of (the
// contract forbids one module including another's header)
// ---------------------------------------------------------------------------
// The monotonic clock coop_combat uses, and for the same measured reason: net::Now()
// (GameplayStatics.GetRealTimeSeconds read as a float) has been seen to run
// backwards, and every interval here means "how much real time has passed".
double Mono() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return duration_cast<duration<double>>(steady_clock::now() - t0).count();
}

bool Live(UObject* o) { return o != nullptr && ue::IsValid(o); }

Vec3 LocOf(UObject* o) { return Live(o) ? net::Loc(o) : Vec3(); }

double Dist3(const Vec3& a, const Vec3& b) {
    const double dx = a.X - b.X, dy = a.Y - b.Y, dz = a.Z - b.Z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The engine's EEndPlayReason, spelled out (Engine_enums.hpp).  This is what
// separates "the game destroyed an actor" from "the cell that owned it went away".
const char* EndPlayReasonText(int r) {
    switch (r) {
        case 0: return "Destroyed";
        case 1: return "LevelTransition";
        case 2: return "EndPlayInEditor";
        case 3: return "RemovedFromWorld";
        case 4: return "Quit";
        default: return "not-recorded";
    }
}

const char* CellText(int cell) {
    switch (cell) {
        case -1: return "no-query";
        case 0:  return "unloaded";
        case 1:  return "loaded";
        case 2:  return "activated";
        default: return "?";
    }
}

// normalised health through combat's own reader (1.0 = full)
double HealthOf(UObject* e) {
    if (!e) return -1.0;
    bool dead = false;
    return combat::HealthNorm(e, &dead);
}

// ---------------------------------------------------------------------------
// reflected accessors -- every one optional, because a class may not have them
// ---------------------------------------------------------------------------
double LifeSpanOf(UObject* e) {
    UFunction* f = e ? ue::FindFunction(e, "GetLifeSpan") : nullptr;
    if (!f) return -1.0;
    ue::Params p(f);
    if (!ue::Call(e, f, p.data())) return -1.0;
    return static_cast<double>(p.Get<float>("ReturnValue"));
}

bool ZeroLifeSpan(UObject* e) {
    UFunction* f = e ? ue::FindFunction(e, "SetLifeSpan") : nullptr;
    if (!f) return false;
    ue::Params p(f);
    p.Set<float>("InLifespan", 0.0f);
    return ue::Call(e, f, p.data());
}

bool BoolProp(UObject* o, const char* name) {
    if (!o || ue::PropOffset(o, name) < 0) return false;
    return ue::GetBool(o, name);
}

double FloatProp(UObject* o, const char* name) {
    if (!o || ue::PropOffset(o, name) < 0) return -1.0;
    float v = 0.0f;
    if (!ue::Get<float>(o, name, v)) return -1.0;
    return static_cast<double>(v);
}

std::string ClassOf(UObject* o) {
    ue::UClass* c = o ? ue::GetClass(o) : nullptr;
    return c ? ue::GetFullName(reinterpret_cast<UObject*>(c)) : std::string("-");
}

// ---------------------------------------------------------------------------
// 3. the world's streaming state at a point
// ---------------------------------------------------------------------------
// UWorldPartitionSubsystem::IsStreamingCompleted(QueryState, QuerySources,
// bExactState) is the game's own answer to "is the cell at this location at least
// Loaded".  EWorldPartitionRuntimeCellState: 0 Unloaded / 1 Loaded / 2 Activated.
// FWorldPartitionStreamingQuerySource is 0x108 bytes (SDK: Location 0x00, Radius
// 0x18, bUseGridLoadingRange 0x1C, DataLayers 0x20, bDataLayersOnly 0x30,
// bSpatialQuery 0x31) and the TArray parameter is passed by POINTER.
UObject* WorldPartitionSubsystem() {
    static UObject* cached = nullptr;
    static double at = -1.0e9;
    if (Live(cached)) return cached;
    if (Mono() - at < 5.0) return nullptr;
    at = Mono();
    cached = nullptr;
    for (UObject* o : ue::FindAllOf("WorldPartitionSubsystem")) {
        if (!Live(o)) continue;   // IsValid inside Live() rejects the CDO
        cached = o;
        break;
    }
    return cached;
}

struct RawArray {
    void*  data;
    std::int32_t num;
    std::int32_t max;
};

// -1 = the query could not be made, 0 = unloaded, 1 = loaded, 2 = activated.
int CellStateAt(const Vec3& p) {
    UObject* sub = WorldPartitionSubsystem();
    if (!sub) return -1;
    UFunction* f = ue::FindFunction(sub, "IsStreamingCompleted");
    if (!f) return -1;

    // one query source on this thread's stack for the duration of the call: the
    // parameter is a const reference, so the engine only reads it
    alignas(16) unsigned char src[0x108];
    std::memset(src, 0, sizeof(src));
    const double xyz[3] = {p.X, p.Y, p.Z};
    std::memcpy(src + 0x00, xyz, sizeof(xyz));
    const float radius = 200.0f;
    std::memcpy(src + 0x18, &radius, sizeof(radius));
    const unsigned char grid = 1;       // let the grid's own loading range decide
    std::memcpy(src + 0x1C, &grid, sizeof(grid));
    const unsigned char spatial = 1;    // bSpatialQuery
    std::memcpy(src + 0x31, &spatial, sizeof(spatial));

    RawArray arr;
    arr.data = src;
    arr.num = 1;
    arr.max = 1;

    for (int want = 2; want >= 1; --want) {
        ue::Params prm(f);
        prm.Set<unsigned char>("QueryState", static_cast<unsigned char>(want));
        prm.Set<RawArray*>("QuerySources", &arr);
        prm.Set<unsigned char>("bExactState", 1);
        if (!ue::Call(sub, f, prm.data())) return -1;
        if (prm.Get<unsigned char>("ReturnValue") & 1) return want;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// the acceptance window and the log budget
// ---------------------------------------------------------------------------
// 30 s is the task's own acceptance ("guest near a far spawner for >= 30 s -> the
// host's enemy exists the whole time"); the per-instance sample is 1 Hz because
// the thing being measured lasts about a second when it fails.
constexpr double kAliveVerdictSecs = 30.0;
constexpr double kSampleSecs       = 1.0;
constexpr int    kMaxRows          = 24;
constexpr int    kMaxLogsPerRow    = 60;
constexpr double kNearDist         = 3000.0;   // "an actor inside a claimed spawner's area"

// ---------------------------------------------------------------------------
// one replayed enemy instance
// ---------------------------------------------------------------------------
struct Row {
    std::string key;          // "SP_<spawner tail>" -- the wire key both clients agree on
    std::string spawner;      // spawner tail
    UObject* spawnerObj = nullptr;
    UObject* enemy = nullptr; // the instance currently being watched (nullptr = none)
    std::string enemyName;    // cached: a destroyed actor's name is no longer readable
    std::string enemyClass;
    double firstSeen = 0.0;
    double lastLog = -1.0e9;
    double lastLife = -1.0;   // AActor::GetLifeSpan() at the last sample
    double lastHp = -1.0;     // the last health read while the actor was alive
    Vec3   lastPos;
    int    logs = 0;
    int    endplayReason = -1;   // captured by the hook for THIS instance
};

std::vector<Row> g_rows;
bool   g_hooksInstalled = false;
bool   g_hookFailed = false;
int    g_lifeFixOn = -1;        // -1 = follow Toggles::farEnemy, 0/1 = `farfix` override
int    g_streamFixOn = -1;
int    g_spawns = 0, g_gone = 0;
double g_bestAlive = 0.0;       // longest continuous life seen this session
bool   g_verdictWritten = false;
int    g_logBudget = 400;       // global cap so a stuck claim cannot flood the log
bool   g_vetoAutoBlocked = false;   // `farhook off` also stops the automatic install
double g_vetoTryAt = -1.0e9;
bool   g_firstVetoReport = true;    // the base/anchor/target line, once per session
bool   g_vetoFailLogged = false;    // the "wrong build" line, once per session


// The life-span lever is NOT the fix either (measured: the game never sets one), so it is
// off unless `farfix life|all` asks for it in a lab.  Kept because it is the cheapest way to
// re-check the "an engine timer removes it" reading on a future game build.
bool FixLife() {
    return g_lifeFixOn > 0;
}

// The streaming lever is NOT part of the shipped fix: it only exists so the "the cell
// is unloaded" hypothesis can be A/B'd from cmd.txt.  It is deliberately off unless
// `farfix stream|all` asked for it -- and it never touches a BLOCKING source, because
// coop_puppet's StreamSetup turns those off on purpose (a blocking source makes the
// game show its loading screen while it waits).
bool FixStream() {
    return g_streamFixOn > 0;
}

Row* RowFor(const std::string& key, const std::string& spawner, UObject* spawnerObj) {
    for (Row& r : g_rows) {
        if (r.key == key) {
            r.spawnerObj = spawnerObj;
            return &r;
        }
    }
    if (g_rows.size() >= static_cast<std::size_t>(kMaxRows)) g_rows.erase(g_rows.begin());
    Row row;
    row.key = key;
    row.spawner = spawner;
    row.spawnerObj = spawnerObj;
    g_rows.push_back(row);
    return &g_rows.back();
}

// ---------------------------------------------------------------------------
// 1. the destroy's EndPlayReason -- the probe the native path cannot hide from
// ---------------------------------------------------------------------------
// [R8-FARENEMY] the return-address stack at the moment a tracked actor ends its play.
// The destroy is native, so this is the only evidence of WHICH code asked for it: the
// frames below AActor::Destroy are the caller chain.  Each frame is printed as an RVA
// from the main module's base, which is what a follow-up session needs to identify it
// without a PDB (the project's AOB/RVA method); a frame outside the game's image shows
// up as a huge number, which itself says "another module asked for this".
void LogCallerStack() {
    void* frames[24] = {};
    const unsigned short n = CaptureStackBackTrace(1, 24, frames, nullptr);
    const uintptr_t base = ue::GetResolved().imageBase;
    std::string out;
    for (unsigned short i = 0; i < n && i < 20; ++i) {
        const uintptr_t a = reinterpret_cast<uintptr_t>(frames[i]);
        const unsigned long long rva =
            (base != 0 && a >= base) ? static_cast<unsigned long long>(a - base) : 0ull;
        net::AppendFmt(&out, "%s%llx", i == 0 ? "" : " ", rva);
    }
    net::Log("farenemy endplay stack (rva from image base %llx): %s",
             static_cast<unsigned long long>(base), out.c_str());
}

void NoteEndPlay(UObject* self, UFunction* fn, void* params) {
    if (!self || !params) return;
    // This runs inside the ProcessEvent detour, and the "is it near a claimed
    // spawner" branch below asks the actor for its location -- which is itself a
    // ProcessEvent call.  One guard, so a nested entry can never recurse back into
    // this hook (the log line is worth nothing if it costs the frame).
    static thread_local bool inHook = false;
    if (inHook) return;
    inHook = true;
    struct Guard {
        bool* flag;
        ~Guard() { *flag = false; }
    } guard{&inHook};

    // the parameter is the function's own property; reading it needs its offset,
    // not a Params block (this runs inside the detour, so it must not allocate)
    const std::int32_t off =
        ue::PropOffsetStruct(reinterpret_cast<ue::UStruct*>(fn), "EndPlayReason");
    unsigned char reason = 0xFF;
    if (off < 0 || !ue::ReadMem(reinterpret_cast<unsigned char*>(params) + off, &reason, 1)) {
        reason = 0xFF;
    }
    const int r = static_cast<int>(reason);

    // the tracked instances first: this is the answer this module exists for
    for (Row& row : g_rows) {
        if (row.enemy != self) continue;
        row.endplayReason = r;
        if (g_logBudget > 0) {
            --g_logBudget;
            net::Log("farenemy endplay %s enemy=%s reason=%d (%s)", row.key.c_str(),
                     row.enemyName.c_str(), r, EndPlayReasonText(r));
            LogCallerStack();
        }
        return;
    }

    // then anything else that ends its play INSIDE a claimed spawner's area: an
    // enemy whose spawner lost it from `SpawnedEnemy` before we sampled it, or a
    // helper actor (weapon, AI controller, session object) going down with it.
    //
    // The claim list gates this deliberately: EndPlay runs for EVERY actor, and on a
    // cell unload that is hundreds of them in one frame.  With no claim on this
    // client there is nothing to explain, so the hook returns here and costs two
    // compares -- the frame cost of this whole lane stays at "a few property reads
    // per claimed spawner" (measured cost is in the report).
    if (state().combat.peerClaims.empty()) return;
    const Vec3 p = LocOf(self);
    for (const PeerClaim& claim : state().combat.peerClaims) {
        if (claim.key.size() < 4 || claim.key.compare(0, 3, "SP_") != 0) continue;
        if (Dist3(p, claim.pos) > kNearDist) continue;
        if (g_logBudget > 0) {
            --g_logBudget;
            net::Log("farenemy endplay near %s actor=%s reason=%d (%s) d=%.0f",
                     claim.key.c_str(), ue::GetName(self).c_str(), r, EndPlayReasonText(r),
                     Dist3(p, claim.pos));
        }
        return;
    }
}

void InstallHooks() {
    if (g_hooksInstalled || g_hookFailed) return;
    g_hooksInstalled = true;
    // "Class.Function" resolves through FindClass + the CDO's function list (the
    // spelling the other lanes use); the full object path is the fallback for the
    // case where the short class name does not resolve on this build.
    if (!ue::HookPost("Actor.ReceiveEndPlay", NoteEndPlay) &&
        !ue::HookPost("/Script/Engine.Actor.ReceiveEndPlay", NoteEndPlay)) {
        g_hookFailed = true;
        net::Log("farenemy: the ReceiveEndPlay hook did NOT install -- the destroy's "
                 "EndPlayReason will not be recorded");
        return;
    }
    net::Log("farenemy: hook on Actor.ReceiveEndPlay installed (the native destroy's reason)");
}

// ---------------------------------------------------------------------------
// 2. the fix: a replayed copy lives as long as the peer's does
// ---------------------------------------------------------------------------
// THE HYPOTHESIS THIS LEVER TESTS (REPORT.md section 2 -- the reading that selects it):
// the game asks the engine to remove these actors with a LIFE SPAN rather than with a
// call a mod can veto.  An engine timer destroys the actor natively, which is exactly
// why Actor.K2_DestroyActor never fires and why the spawner's Blueprint shows nothing,
// and a life span is also the one part of that path that is a plain reflected property
// -- so the copy the peer is responsible for is kept alive by keeping it at zero.
//
// The watch above prints `life=` on every 1 Hz sample, so the lab run either shows a
// countdown (this is the cause and the fix is the answer) or `life=0.00` for the whole
// life (it is not, and the `endplayReason` line then says which family it is).
//
// Only the actors this module tracks are touched (the enemies a claimed spawner
// produced on THIS client), and only while FARENEMY_OFF.txt is absent.
void ApplyLifeFix() {
    if (!FixLife()) return;
    for (Row& row : g_rows) {
        if (!Live(row.enemy)) continue;
        const double life = LifeSpanOf(row.enemy);
        if (!(life > 0.0)) continue;
        if (g_logBudget > 0 && row.logs < kMaxLogsPerRow) {
            --g_logBudget;
            row.logs += 1;
            net::Log("farenemy life fix: %s enemy=%s life=%.2f -> 0 (the engine timer would "
                     "destroy it)", row.key.c_str(), row.enemyName.c_str(), life);
        }
        ZeroLifeSpan(row.enemy);
    }
}

// The second hypothesis (the cell around a far enemy is unloaded because no local
// player stands there) has its lever here so the A/B can be run from cmd.txt without
// a rebuild: the peer's body already carries the game's own "Streaming Source (Non
// Blocking)" component, so all this can do is make sure it is enabled while the peer
// is alive -- what the game's BPFL_WorldStreaming does for a player.
void ApplyStreamFix() {
    if (!FixStream()) return;
    UObject* pup = puppet::Actor();
    if (!Live(pup)) return;
    static double lastTry = -1.0e9;
    const double mono = Mono();
    if (mono - lastTry < 2.0) return;
    lastTry = mono;

    UObject* cls = net::EngineClass("WorldPartitionStreamingSourceComponent");
    UFunction* f = ue::FindFunction(pup, "K2_GetComponentsByClass");
    if (!Live(cls) || !f) return;
    ue::Params p(f);
    p.Set<UObject*>("ComponentClass", cls);
    if (!ue::Call(pup, f, p.data())) return;
    ue::TArray<UObject*> arr = p.Get<ue::TArray<UObject*>>("ReturnValue");
    if (!arr.Data || arr.Num <= 0) return;
    for (int i = 0; i < arr.Num && i < 16; ++i) {
        UObject* comp = nullptr;
        if (!ue::ReadMem(arr.Data + i, &comp, sizeof(comp))) break;
        if (!Live(comp)) continue;
        // NEVER the blocking source: coop_puppet's StreamSetup disables it on purpose
        // (a blocking source makes the game hold its loading screen until the cell is
        // in), and only the non-blocking one is the "stream this area in the
        // background" lever the fix wants.
        if (net::FullName(comp).find("Non Blocking") == std::string::npos) continue;
        // the accessors are the game's own: bStreamingSourceEnabled is a bitfield, so
        // writing the property directly is not safe (coop_puppet reads it only).
        UFunction* en = ue::FindFunction(comp, "EnableStreamingSource");
        UFunction* isOn = ue::FindFunction(comp, "IsStreamingSourceEnabled");
        if (!en || !isOn) continue;
        ue::Params q(isOn);
        bool enabled = false;
        if (ue::Call(comp, isOn, q.data())) enabled = (q.Get<unsigned char>("ReturnValue") & 1) != 0;
        if (enabled) continue;
        ue::Params e(en);
        ue::Call(comp, en, e.data());
        if (g_logBudget > 0) {
            --g_logBudget;
            net::Log("farenemy stream fix: enabled %s on the peer's body",
                     ue::GetName(comp).c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// the per-tick watch
// ---------------------------------------------------------------------------
// MEASURED (lab 20261026-184149-far15): the rows were followed through `peerClaims`, and
// that made the watch stop inside the window it was measuring -- the brigands killed the
// GUEST at t=17 s, the guest's client answered `{"rdy":0}` (no "npc" section, so
// `npcClaims` came back empty: `npcrcv: entries=0 claims=0`), and this loop simply had
// nothing to iterate.  The actors themselves were still alive on the host (the veto did
// its job); only the bookkeeping that named them was gone.
//
// So the watch follows the ACTOR, not the claim list: a row keeps its enemy pointer and
// is sampled for as long as the actor is alive, and the claim list is only used to learn
// about NEW instances.  That is also the honest reading of the acceptance -- "the host's
// copy of the enemy the guest spawned stays alive" does not stop being true because the
// guest's client stopped publishing.
void Watch(double mono) {
    CombatState& c = state().combat;
    if (g_rows.empty() && c.peerClaims.empty()) return;

    // 1. learn about new instances from the claims (spawners are combat's own 2 Hz index)
    for (const PeerClaim& claim : c.peerClaims) {
        if (claim.key.size() < 4 || claim.key.compare(0, 3, "SP_") != 0) continue;
        const std::string tail = claim.key.substr(3);
        UObject* sp = nullptr;
        for (const NamedActor& row : c.spawners) {
            if (row.tail == tail) { sp = row.obj; break; }
        }
        if (!sp || !Live(sp)) continue;
        UObject* e = nullptr;
        if (!ue::Get<UObject*>(sp, "SpawnedEnemy", e)) e = nullptr;
        if (e && !Live(e)) e = nullptr;   // the property can hold a pending-kill pointer
        if (e) RowFor(claim.key, tail, sp);
    }

    // 2. sample every row: a row lives as long as its actor does
    for (Row& r : g_rows) {
        // refresh the spawner when this client still has it (its SpawnedEnemy may name a
        // newer instance, which is how a re-spawn is noticed)
        UObject* sp = Live(r.spawnerObj) ? r.spawnerObj : nullptr;
        if (!sp) {
            for (const NamedActor& row : c.spawners) {
                if (row.tail == r.spawner) { sp = row.obj; break; }
            }
        }
        UObject* fromSpawner = nullptr;
        if (sp && Live(sp)) {
            r.spawnerObj = sp;
            if (ue::Get<UObject*>(sp, "SpawnedEnemy", fromSpawner) && fromSpawner &&
                !Live(fromSpawner)) {
                fromSpawner = nullptr;
            }
        }

        const bool actorAlive = r.enemy != nullptr && Live(r.enemy);
        UObject* e = fromSpawner ? fromSpawner : (actorAlive ? r.enemy : nullptr);

        if (!e) {
            if (!r.enemy) continue;
            // the instance this row was watching is gone (really gone: not merely
            // unreadable -- the pointer no longer resolves, and the endplay hook has
            // already said why if it was destroyed)
            const double lived = mono - r.firstSeen;
            if (lived > g_bestAlive) g_bestAlive = lived;
            ++g_gone;
            if (g_logBudget > 0) {
                --g_logBudget;
                net::Log("farenemy gone %s enemy=%s lived=%.2fs lastLife=%.2f lastHp=%.3f at "
                         "(%.0f %.0f %.0f) endplayReason=%d (%s)", r.key.c_str(),
                         r.enemyName.c_str(), lived, r.lastLife, r.lastHp, r.lastPos.X,
                         r.lastPos.Y, r.lastPos.Z, r.endplayReason,
                         EndPlayReasonText(r.endplayReason));
            }
            r.enemy = nullptr;
            r.enemyName.clear();
            r.lastLife = -1.0;
            continue;
        }

        if (r.enemy != e) {
            // a NEW instance (the first one, or the next replay after a destroy)
            const double life = LifeSpanOf(e);
            r.enemy = e;
            r.enemyName = ue::GetName(e);
            r.enemyClass = ClassOf(e);
            r.firstSeen = mono;
            r.lastLog = -1.0e9;
            r.lastLife = life;
            r.lastHp = HealthOf(e);
            r.lastPos = LocOf(e);
            r.logs = 0;
            r.endplayReason = -1;
            ++g_spawns;

            const Vec3 sl = LocOf(sp);
            const int cell = CellStateAt(r.lastPos);
            Vec3 claimPos = sl;
            for (const PeerClaim& claim : c.peerClaims) {
                if (claim.key == r.key) { claimPos = claim.pos; break; }
            }
            if (g_logBudget > 0) {
                --g_logBudget;
                net::Log("farenemy spawn %s enemy=%s cls=%s life=%.2f initLife=%.2f hp=%.3f "
                         "hidden=%d at=(%.0f %.0f %.0f) dSpawner=%.0f dHostPawn=%.0f dClaim=%.0f "
                         "cell=%d(%s)",
                         r.key.c_str(), r.enemyName.c_str(), r.enemyClass.c_str(), life,
                         FloatProp(e, "InitialLifeSpan"), r.lastHp, BoolProp(e, "bHidden") ? 1 : 0,
                         r.lastPos.X, r.lastPos.Y, r.lastPos.Z, Dist3(r.lastPos, sl),
                         Dist3(r.lastPos, LocOf(net::Pawn())), Dist3(r.lastPos, claimPos), cell,
                         CellText(cell));
            }
        } else if (mono - r.lastLog >= kSampleSecs) {
            // 1 Hz while it lives: the life span is the tell (a countdown means the
            // engine has already been asked to destroy it)
            r.lastLog = mono;
            r.lastPos = LocOf(e);
            r.lastLife = LifeSpanOf(e);
            const double hp = HealthOf(e);
            r.lastHp = hp;
            const double lived = mono - r.firstSeen;
            if (lived > g_bestAlive) g_bestAlive = lived;
            if (g_logBudget > 0 && r.logs < kMaxLogsPerRow) {
                --g_logBudget;
                r.logs += 1;
                net::Log("farenemy alive %s t=%.1fs life=%.2f hp=%.3f dHostPawn=%.0f",
                         r.key.c_str(), lived, r.lastLife, hp,
                         Dist3(r.lastPos, LocOf(net::Pawn())));
            }
            // the acceptance: one instance of the peer's enemy alive on this client
            // for the whole 30 s window
            if (!g_verdictWritten && lived >= kAliveVerdictSecs) {
                g_verdictWritten = true;
                net::WriteFile("result_farenemy.txt",
                               net::Fmt("PASS the peer's enemy %s stayed alive on this client for "
                                        "%.0f s (hp=%.3f, life=%.2f, dHostPawn=%.0f)\n",
                                        r.key.c_str(), lived, hp, r.lastLife,
                                        Dist3(r.lastPos, LocOf(net::Pawn()))));
                net::Log("farenemy verdict PASS %s alive %.1fs (hp=%.3f life=%.2f)",
                         r.key.c_str(), lived, hp, r.lastLife);
            }
        }
    }
}

}  // namespace

// section 4 (the veto) is defined further down; Tick15 installs it once, and these two
// declarations are what let it do that from here.
void InstallVeto(std::uint64_t explicitRva, bool reportOnly);
extern void* g_destroyTarget;

// ---------------------------------------------------------------------------
// ticks
// ---------------------------------------------------------------------------
// 15 Hz: the watch.  A handful of property reads per claimed spawner (the enemy
// pointer, plus its life span and health at 1 Hz); it walks nothing and allocates
// nothing on the steady path.  It runs OUTSIDE the Sync gate like combat's spawn
// watch: a replayed enemy can exist before this client has settled.
void Tick15() {
    InstallHooks();
    const double mono = Mono();
    // THE FIX is installed once, automatically, while FARENEMY_OFF.txt is absent: the
    // veto on the game's end-play routing (section 4).  `farhook off` latches it off for
    // this session -- that is the A-side of the lab's A/B.
    if (!g_vetoAutoBlocked && state().toggles.farEnemy && !g_destroyTarget &&
        mono - g_vetoTryAt > 2.0) {
        g_vetoTryAt = mono;
        InstallVeto(0, false);
    }
    Watch(mono);
    ApplyLifeFix();
    ApplyStreamFix();
}

// A world change frees every actor this module holds.  The counters and the verdict
// latch stay: the verdict is about the session, not about one map.
void Reset() {
    g_rows.clear();
}

// ---------------------------------------------------------------------------
// `fardump`: what this client sees, in one block
// ---------------------------------------------------------------------------
void CmdDump(const std::string& arg) {
    const double mono = Mono();
    net::Log("fardump: arg=\"%s\" mono=%.1f claims=%d spawners=%d rows=%d spawns=%d gone=%d "
             "bestAlive=%.1fs fixLife=%d fixStream=%d wpSubsystem=%d", arg.c_str(), mono,
             (int)state().combat.peerClaims.size(), (int)state().combat.spawners.size(),
             (int)g_rows.size(), g_spawns, g_gone, g_bestAlive, FixLife() ? 1 : 0,
             FixStream() ? 1 : 0, WorldPartitionSubsystem() ? 1 : 0);

    // every claimed spawner and the enemy it produced here
    int n = 0;
    for (const PeerClaim& claim : state().combat.peerClaims) {
        if (claim.key.size() < 4 || claim.key.compare(0, 3, "SP_") != 0) continue;
        UObject* sp = nullptr;
        const std::string tail = claim.key.substr(3);
        for (const NamedActor& row : state().combat.spawners) {
            if (row.tail == tail) { sp = row.obj; break; }
        }
        ++n;
        if (!sp || !Live(sp)) {
            net::Log("fardump: claim %s -> NO LOCAL SPAWNER (its cell is not loaded)",
                     claim.key.c_str());
            continue;
        }
        UObject* e = nullptr;
        if (!ue::Get<UObject*>(sp, "SpawnedEnemy", e) || !Live(e)) e = nullptr;
        const Vec3 sl = LocOf(sp);
        if (!e) {
            net::Log("fardump: claim %s spawner dClaim=%.0f -> no live enemy", claim.key.c_str(),
                     Dist3(sl, claim.pos));
            continue;
        }
        const Vec3 ep = LocOf(e);
        const int cell = CellStateAt(ep);
        net::Log("fardump: claim %s spawner dClaim=%.0f enemy=%s life=%.2f initLife=%.2f hp=%.3f "
                 "dSpawner=%.0f cell=%d(%s) destroyedFlag=%d deadFlag=%d", claim.key.c_str(),
                 Dist3(sl, claim.pos), ue::GetName(e).c_str(), LifeSpanOf(e),
                 FloatProp(e, "InitialLifeSpan"), HealthOf(e), Dist3(ep, sl), cell, CellText(cell),
                 BoolProp(sp, "IsSpawnedEnemyDestroyed") ? 1 : 0,
                 BoolProp(sp, "IsSpawnedEnemyDead") ? 1 : 0);
    }
    net::Log("fardump: claimed spawners=%d", n);

    // the streaming sources this client has: the game's own AStreamingSource actors
    // (BP_StreamingSource_STH derives from it) and the components on our pawn and on
    // the peer's body
    int srcActors = 0;
    for (UObject* o : ue::FindAllOf("StreamingSource")) {
        if (!Live(o)) continue;
        const Vec3 p = LocOf(o);
        ++srcActors;
        net::Log("fardump: source actor %s at (%.0f %.0f %.0f) dHostPawn=%.0f",
                 ue::GetName(o).c_str(), p.X, p.Y, p.Z, Dist3(p, LocOf(net::Pawn())));
    }
    net::Log("fardump: StreamingSource actors=%d", srcActors);

    for (int who = 0; who < 2; ++who) {
        UObject* actor = who == 0 ? net::Pawn() : puppet::Actor();
        const char* label = who == 0 ? "pawn" : "puppet";
        if (!Live(actor)) {
            net::Log("fardump: %s=not live", label);
            continue;
        }
        UObject* cls = net::EngineClass("WorldPartitionStreamingSourceComponent");
        UFunction* f = ue::FindFunction(actor, "K2_GetComponentsByClass");
        if (!Live(cls) || !f) {
            net::Log("fardump: %s: no K2_GetComponentsByClass / component class", label);
            continue;
        }
        ue::Params p(f);
        p.Set<UObject*>("ComponentClass", cls);
        if (!ue::Call(actor, f, p.data())) {
            net::Log("fardump: %s: K2_GetComponentsByClass failed", label);
            continue;
        }
        ue::TArray<UObject*> arr = p.Get<ue::TArray<UObject*>>("ReturnValue");
        const int got = (arr.Data && arr.Num > 0) ? arr.Num : 0;
        net::Log("fardump: %s %s streaming components=%d", label, ue::GetName(actor).c_str(), got);
        for (int i = 0; i < got && i < 8; ++i) {
            UObject* comp = nullptr;
            if (!ue::ReadMem(arr.Data + i, &comp, sizeof(comp))) break;
            if (!Live(comp)) continue;
            bool enabled = false, completed = false;
            if (UFunction* isOn = ue::FindFunction(comp, "IsStreamingSourceEnabled")) {
                ue::Params q(isOn);
                if (ue::Call(comp, isOn, q.data())) {
                    enabled = (q.Get<unsigned char>("ReturnValue") & 1) != 0;
                }
            }
            if (UFunction* isDone = ue::FindFunction(comp, "IsStreamingCompleted")) {
                ue::Params q(isDone);
                if (ue::Call(comp, isDone, q.data())) {
                    completed = (q.Get<unsigned char>("ReturnValue") & 1) != 0;
                }
            }
            net::Log("fardump:   %s name=%s enabled=%d completed=%d", label,
                     ue::GetName(comp).c_str(), enabled ? 1 : 0, completed ? 1 : 0);
        }
    }
}

// `farufunc <loRva> <hiRva>`: WHICH UFunction owns the native code at a given RVA.
//
// Why this exists: the destroy is native, so the only record of who did it is the
// return-address stack (captured by the EndPlay hook).  A stack of RVAs is anonymous
// without a PDB -- but every UFunction carries its native entry point at +0xD8
// (`UFunction::ExecFunction`, Dumper7 CoreUObject_classes.hpp), so dumping the
// functions whose entry point falls in the captured range names them exactly.
// `K2_DestroyActor` (the BP "Destroy Actor" node) is the one that matters: BP bytecode
// calls a native UFUNCTION through that pointer DIRECTLY, i.e. WITHOUT ProcessEvent --
// which is why R6's ProcessEvent hook on Actor.K2_DestroyActor never fired even if the
// spawner's graph is exactly what destroys the copy.
void CmdUFunc(const std::string& arg) {
    // "<lo> <hi>" (hex, 0x optional).  A single number means "0x10000 around it", which
    // is what a captured frame needs.
    std::vector<std::string> tok;
    {
        std::string cur;
        for (char ch : arg) {
            if (ch == ' ' || ch == ',' || ch == '\t') {
                if (!cur.empty()) { tok.push_back(cur); cur.clear(); }
            } else {
                cur.push_back(ch);
            }
        }
        if (!cur.empty()) tok.push_back(cur);
    }
    std::uint64_t lo = 0, hi = ~0ull;
    if (!tok.empty()) {
        lo = std::strtoull(tok[0].c_str(), nullptr, 16);
        hi = (tok.size() > 1) ? std::strtoull(tok[1].c_str(), nullptr, 16) : lo + 0x10000ull;
    }
    const std::uintptr_t base = ue::GetResolved().imageBase;

    // the anchor first: the functions a destroy path is made of, always named
    struct Named { const char* cls; const char* fn; };
    const Named anchors[] = {
        {"Actor", "ReceiveEndPlay"}, {"Actor", "ReceiveDestroyed"}, {"Actor", "K2_DestroyActor"},
        {"Actor", "SetLifeSpan"},    {"Actor", "GetLifeSpan"},      {"Actor", "Destroyed"},
        {"Actor", "ReceiveBeginPlay"},
    };
    for (const Named& a : anchors) {
        ue::UClass* cls = ue::FindClass(a.cls);
        if (!cls) continue;
        ue::UObject* cdo = ue::GetCDO(cls);
        if (!cdo) continue;
        UFunction* fn = ue::FindFunction(cdo, a.fn);
        if (!fn) continue;
        void* exec = nullptr;
        if (!ue::ReadMem(reinterpret_cast<const unsigned char*>(fn) + 0xD8, &exec, sizeof(exec))) continue;
        const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(exec);
        net::Log("farufunc anchor %s.%s exec=0x%llx%s", a.cls, a.fn,
                 static_cast<unsigned long long>(base && p >= base ? p - base : 0),
                 (base && p >= base && (p - base) >= lo && (p - base) <= hi) ? "  <== IN RANGE" : "");
    }

    int scanned = 0, hit = 0;
    for (UObject* o : ue::FindAllOf("Function")) {
        if (!o) continue;
        void* exec = nullptr;
        if (!ue::ReadMem(reinterpret_cast<const unsigned char*>(o) + 0xD8, &exec, sizeof(exec)) ||
            !exec) {
            continue;
        }
        ++scanned;
        const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(exec);
        if (!base || p < base) continue;
        const std::uint64_t rva = static_cast<std::uint64_t>(p - base);
        if (rva < lo || rva > hi) continue;
        UObject* outer = ue::GetOuter(o);
        net::Log("farufunc %s.%s exec=0x%llx", ue::GetName(outer).c_str(), ue::GetName(o).c_str(),
                 static_cast<unsigned long long>(rva));
        if (++hit >= 300) break;
    }
    net::Log("farufunc: range 0x%llx..0x%llx, %d functions with an ExecFunction, %d inside",
             static_cast<unsigned long long>(lo), static_cast<unsigned long long>(hi), scanned, hit);
}

// ---------------------------------------------------------------------------
// 4. THE VETO -- the fix, and why it is a native hook and not a ProcessEvent one
// ---------------------------------------------------------------------------
// MEASURED (lab 20261026-160119-far7, probe_A.log):
//   farenemy spawn ... life=0.00 initLife=0.00 hidden=1 ... cell=2(activated)
//   farenemy endplay ... reason=0 (Destroyed)
//   farenemy endplay stack (rva from image base ...): 395fae2 394fd47 3962402 394d360
//                                                    3f3aebf 394cc3d 14ec9b9 16125d5 ...
// so, in one run, three of the four candidate causes are gone:
//   - NOT a life-span timer: `life=0.00` at every sample (this is why the life-span
//     lever of section 2 does nothing and is not the fix);
//   - NOT streaming: the game's own query says the cell at the enemy is ACTIVATED while
//     the spawner stays alive and nothing else in the area ends its play;
//   - NOT a death: the reason is Destroyed at full health.
// The stack is the caller chain, and it is identical for every one of these destroys:
//   ... BP script VM (0x14ec9b9) -> 0x394cc3d -> 0x3f3aebf -> 0x394d360
//       -> 0x39623c0 -> 0x394fd14 -> [AActor::EndPlay/ReceiveEndPlay thunk] -> ProcessEvent
// i.e. a BLUEPRINT graph asks for a native destroy.  That is also why R6's
// ProcessEvent hook on Actor.K2_DestroyActor never fired: blueprint bytecode calls a
// native UFUNCTION through its UFunction::ExecFunction pointer DIRECTLY, without
// ProcessEvent, so a ProcessEvent-level "veto" can never see it.
//
// The one place that every destroy path funnels through is the function that ROUTES the
// end-play, and its address is read straight off the captured stack: frame 0x3962402 is
// inside the function at 0x39623c0 (.pdata: 0x39623c0..0x39625ab), whose body tests the
// actor flags on RCX, compares EDX with 3 (= EEndPlayReason::RemovedFromWorld) and then
// calls virtual EndPlay(actor, edx).  That is AActor::RouteEndPlay -- the single gate in
// front of EndPlay for a destroyed actor, a level transition and a level unload alike.
// Skipping it for a replayed enemy is the fix: in the peer's world that actor is alive,
// so our copy must not be routed out of the world on the peer's behalf.
//
// The hook takes no chances with the signature: the registers are forwarded verbatim
// (two integer-sized slots cover both an enum reason and a pointer argument), and "skip"
// means "return without calling the trampoline", which is sound because the entry
// returns void.  Only actors the peer claims (the "SP_<spawner tail>" aliases) are ever
// skipped; everything else -- including our own code's destroys -- goes through.
using VetoFn = void (*)(void* self, std::int64_t a, std::int64_t b);

VetoFn g_destroyTrampoline = nullptr;
void*  g_destroyTarget = nullptr;
int    g_vetoCalls = 0;
int    g_vetoSkips = 0;
double g_lastVetoLog = -1.0e9;

// WHERE EXACTLY the veto applies (main-agent review: a session-wide veto is too broad -- it
// would also refuse the game's LEGITIMATE teardowns: the world reset after a wipe, the
// checkpoint rest that respawns enemies, a despawn on a level change, the corpse cleanup).
//
// The measured far-cull is much narrower than "any co-op enemy":
//   * R6's control: with the local player 1 600 uu from the spawner the enemy SURVIVES; with
//     the local player 20 800 uu away it is destroyed in ~1 s.  So the case is "an enemy that
//     only exists because the peer is with it, being torn down because NOBODY HERE is with
//     it".  Both halves are checked per call:
//         dist(MY pawn  , enemy) > kFarCullDist     <- no local player anywhere near it
//         dist(PEER body, enemy) < kPeerWithDist    <- the peer's body IS standing with it
//   * and every state in which the game is *supposed* to remove enemies releases the veto:
//         - a wipe / a death pending on either side (coop_death's own state),
//         - a local hold (death, teleport/travel) and a world settle (level/ data-layer
//           switch) -- the two guards the whole router obeys,
//         - the phase machine out of Sync: a reset / level load takes it out of Sync, and the
//           loading screen that fronts it lives exactly there,
//         - the peer's claim list empty or its last packet stale (disconnect, zone change,
//           peer walked away) -> the veto is released, so the game's next cull runs.
//           That is the open item of the previous revision, closed.
constexpr double kFarCullDist   = 6000.0;  // my pawn -> enemy: outside R6's near case (1.6k)
constexpr double kPeerWithDist  = 4000.0;  // peer's body -> enemy: it is WITH the peer
constexpr double kPeerFreshSecs = 3.0;     // no peer news for this long: release

// `why` receives a short reason string for the decision log (one line per enemy, see
// LogCullCandidate), so the wipe test can show exactly which candidates appeared during a
// reset and that none of them was refused.
bool KeepCoopEnemy(UObject* actor, const std::string& tail, const std::string& key,
                   const char** why, double* dSelf, double* dPeer) {
    if (why) *why = "no-actor";
    if (dSelf) *dSelf = -1.0;
    if (dPeer) *dPeer = -1.0;
    if (!actor || tail.empty() || key.empty()) return false;
    const CombatState& c = state().combat;
    if (c.deadTails.Has(tail)) { if (why) *why = "dead"; return false; }

    // --- 2. release conditions: the peer, and every legitimising state --------------------
    if (!state().peer.present) { if (why) *why = "no-peer"; return false; }
    // the peer is still publishing (a disconnect or a zone change empties/ages this)
    bool claimed = false;
    for (const PeerClaim& claim : c.peerClaims) {
        if (claim.key == key) { claimed = true; break; }
    }
    if (!claimed) { if (why) *why = "claim-gone"; return false; }
    if (state().puppet.phase != Phase::Sync) { if (why) *why = "not-sync"; return false; }
    if (state().death.pending || state().death.partyCase == 1 || state().death.selfAlive == 0 ||
        state().death.peerLiveness == 0) {
        if (why) *why = "death";          // wipe / respawn / the game's own death+reset
        return false;
    }
    if (net::LocalHold()) { if (why) *why = "hold"; return false; }       // death, tp, travel
    if (net::WorldSettle()) { if (why) *why = "settle"; return false; }   // level/data layer
    const double now = net::Now();
    if (state().peer.at > 0.0 && now - state().peer.at > kPeerFreshSecs) {
        if (why) *why = "peer-stale";
        return false;
    }

    // --- 1. the far-cull shape: nobody here is near it, the peer's body is ---------------
    UObject* mine = net::Pawn();
    UObject* peerBody = puppet::Actor();
    if (!Live(mine) || !Live(peerBody)) { if (why) *why = "no-bodies"; return false; }
    const Vec3 e = LocOf(actor);
    const double ds = Dist3(LocOf(mine), e);
    const double dp = Dist3(LocOf(peerBody), e);
    if (dSelf) *dSelf = ds;
    if (dPeer) *dPeer = dp;
    if (!(ds > kFarCullDist)) { if (why) *why = "near-self"; return false; }
    if (!(dp < kPeerWithDist)) { if (why) *why = "peer-far"; return false; }
    if (why) *why = "far-cull";
    return true;
}

// One line per enemy that reaches the decision (first time only, so a repeat-culling
// spawner cannot flood): the numbers behind the decision, plus the RVA of the code that
// called AActor::Destroy -- the handle for "which caller is this" (BP script VM 0x14exxx /
// engine level code / our own DLL), which is what the wipe test compares.
void LogCullCandidate(UObject* actor, const std::string& tail, bool veto, const char* why,
                      double ds, double dp) {
    static StrSet logged;
    if (logged.Has(tail)) return;
    if (g_logBudget <= 0) return;
    logged.Add(tail);
    --g_logBudget;
    const std::uintptr_t caller =
        reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const std::uintptr_t base = ue::GetResolved().imageBase;
    const unsigned long long rva =
        (base && caller >= base) ? static_cast<unsigned long long>(caller - base) : 0ull;
    net::Log("farenemy cull: %s reason=%s veto=%d dSelf=%.0f dPeer=%.0f phase=%d hold=%d "
             "settle=%d death(pending=%d case=%d self=%d peer=%d) caller=0x%llx",
             tail.c_str(), why ? why : "?", veto ? 1 : 0, ds, dp,
             static_cast<int>(state().puppet.phase), net::LocalHold() ? 1 : 0,
             net::WorldSettle() ? 1 : 0, state().death.pending ? 1 : 0, state().death.partyCase,
             state().death.selfAlive, state().death.peerLiveness, rva);
}

void DestroyHook(void* self, std::int64_t a, std::int64_t b) {
    if (self) {
        UObject* actor = reinterpret_cast<UObject*>(self);
        const std::string tail = net::Tail(actor);
        const std::string* key = state().combat.spawnAlias.Find(tail);
        if (key != nullptr && key->size() > 3 && key->compare(0, 3, "SP_") == 0) {
            const char* why = "?";
            double ds = -1.0, dp = -1.0;
            const bool keep = KeepCoopEnemy(actor, tail, *key, &why, &ds, &dp);
            LogCullCandidate(actor, tail, keep, why, ds, dp);
            if (keep) {
                ++g_vetoSkips;
                const double mono = Mono();
                if (g_logBudget > 0 && mono - g_lastVetoLog > 1.0) {
                    --g_logBudget;
                    g_lastVetoLog = mono;
                    net::Log("farenemy veto: refused to take %s (%s) out of the world -- "
                             "far-cull (dSelf=%.0f > %.0f, dPeer=%.0f < %.0f) calls=%d skips=%d",
                             tail.c_str(), key->c_str(), ds, kFarCullDist, dp, kPeerWithDist,
                             g_vetoCalls, g_vetoSkips);
                }
                return;   // <- the whole fix
            }
        }
    }
    ++g_vetoCalls;
    if (g_destroyTrampoline) g_destroyTrampoline(self, a, b);
}

std::uintptr_t UFunctionEntry(UObject* fnObject) {
    void* exec = nullptr;
    if (!fnObject) return 0;
    if (!ue::ReadMem(reinterpret_cast<const unsigned char*>(fnObject) + 0xD8, &exec,
                     sizeof(exec))) {
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(exec);
}

// Install (or just report) the veto.  `explicitRva` 0 = AActor::Destroy (see the comment
// above, which is where the chain was resolved frame by frame); the named anchor is
// logged next to it as the cross-check that the build is the one the stack came from.
//
// THE RVA IS BUILD-SPECIFIC ON PURPOSE, and it is guarded: the install only happens after
// the function's first 24 bytes were read back and matched the prologue captured from the
// image, so a different game build refuses the hook instead of detouring whatever now
// lives at that address.
//
//   0x394cbf0  AActor::Destroy(bool bNetForce, bool bShouldModifyLevel)   <- the veto
//   0x3f3ad70  UWorld::DestroyActor(AActor*, bool, bool)                <- its callee
//   0x39623c0  AActor::RouteEndPlay(EEndPlayReason)                     <- the funnel
//
// Skipping Destroy is the only one of the three that leaves the actor COMPLETELY
// untouched: RouteEndPlay is already too late (the actor has been flagged and the caller
// finishes the removal either way -- measured: with RouteEndPlay skipped the actor's
// EndPlay never ran, and the spawner's SpawnedEnemy still read back dead within 0.6 s).
constexpr std::uint64_t kActorDestroyRva = 0x394cbf0;

void InstallVeto(std::uint64_t explicitRva, bool reportOnly) {
    const std::uintptr_t base = ue::GetResolved().imageBase;
    if (!base) {
        net::Log("farhook: no image base -- cannot resolve anything");
        return;
    }
    // 1. the anchor, for the log only: Actor.K2_DestroyActor's native entry point
    std::uintptr_t anchor = 0;
    if (ue::UClass* cls = ue::FindClass("Actor")) {
        if (UObject* cdo = ue::GetCDO(cls)) {
            anchor = UFunctionEntry(
                reinterpret_cast<UObject*>(ue::FindFunction(cdo, "K2_DestroyActor")));
        }
    }
    // 2. the target
    const std::uint64_t rva = explicitRva ? explicitRva : kActorDestroyRva;
    const std::uintptr_t target = base + static_cast<std::uintptr_t>(rva);

    // 3. the guard: the EXACT first 24 bytes of AActor::Destroy on this build, read back
    // from the image before anything is hooked.  A different game build (or a wrong RVA)
    // therefore refuses the hook instead of detouring whatever now lives there -- the one
    // mistake that would take the process down mid-frame.
    static const unsigned char kDestroyPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,        // mov [rsp+8], rbx
        0x48, 0x89, 0x6C, 0x24, 0x10,        // mov [rsp+0x10], rbp
        0x48, 0x89, 0x74, 0x24, 0x18,        // mov [rsp+0x18], rsi
        0x57,                                // push rdi
        0x48, 0x83, 0xEC, 0x20,              // sub rsp, 0x20
        0xF6, 0x41, 0x5D, 0x02,              // test byte [rcx+0x5d], 2
    };
    unsigned char buf[sizeof(kDestroyPrologue)] = {};
    const bool readable = ue::ReadMem(reinterpret_cast<const void*>(target), buf, sizeof(buf));
    const bool sig = readable && std::memcmp(buf, kDestroyPrologue, sizeof(buf)) == 0;
    if (g_firstVetoReport) {
        g_firstVetoReport = false;
        net::Log("farhook: base=0x%llx anchor(K2_DestroyActor)=0x%llx target=rva 0x%llx sig=%d",
                 static_cast<unsigned long long>(base), static_cast<unsigned long long>(anchor),
                 static_cast<unsigned long long>(rva), sig ? 1 : 0);
    }

    if (reportOnly) return;
    if (!sig) {
        // once per session: the automatic retry must not flood the log at 2 s intervals
        if (!g_vetoFailLogged) {
            g_vetoFailLogged = true;
            net::Log("farhook: NOT hooking rva 0x%llx -- its first bytes are not the prologue the "
                     "destroy stack came from (a different game build?)",
                     static_cast<unsigned long long>(rva));
        }
        return;
    }
    if (g_destroyTarget == reinterpret_cast<void*>(target)) {
        net::Log("farhook: already hooked at rva 0x%llx (skips=%d)",
                 static_cast<unsigned long long>(rva), g_vetoSkips);
        return;
    }
    if (g_destroyTarget) {
        MH_DisableHook(g_destroyTarget);
        MH_RemoveHook(g_destroyTarget);
        g_destroyTarget = nullptr;
        g_destroyTrampoline = nullptr;
    }
    if (MH_CreateHook(reinterpret_cast<LPVOID>(target), reinterpret_cast<LPVOID>(&DestroyHook),
                      reinterpret_cast<LPVOID*>(&g_destroyTrampoline)) != MH_OK) {
        net::Log("farhook: MH_CreateHook FAILED at 0x%llx -- no veto",
                 static_cast<unsigned long long>(target - base));
        return;
    }
    if (MH_EnableHook(reinterpret_cast<LPVOID>(target)) != MH_OK) {
        MH_RemoveHook(reinterpret_cast<LPVOID>(target));
        g_destroyTrampoline = nullptr;
        net::Log("farhook: MH_EnableHook FAILED at 0x%llx -- no veto",
                 static_cast<unsigned long long>(target - base));
        return;
    }
    g_destroyTarget = reinterpret_cast<void*>(target);
    net::Log("farhook: VETO installed at rva 0x%llx (trampoline=%p) -- a replayed enemy "
             "cannot be routed out of the world any more", static_cast<unsigned long long>(target - base),
             reinterpret_cast<void*>(g_destroyTrampoline));
}

void CmdHook(const std::string& arg) {
    std::string a;
    for (char ch : arg) {
        if ((unsigned char)ch > ' ') a.push_back(static_cast<char>(ch | 0x20));
    }
    if (a == "off") {
        g_vetoAutoBlocked = true;   // also stops the automatic install for this session
        if (g_destroyTarget) {
            MH_DisableHook(g_destroyTarget);
            MH_RemoveHook(g_destroyTarget);
            g_destroyTarget = nullptr;
            g_destroyTrampoline = nullptr;
            net::Log("farhook: veto removed (skips=%d)", g_vetoSkips);
        }
        return;
    }
    const std::uint64_t rva = a.empty() ? 0ull : std::strtoull(a.c_str(), nullptr, 16);
    InstallVeto(rva, false);
}

// `farfix <off|life|stream|all|auto>`: the runtime A/B.  `auto` is what a normal run
// uses (FARENEMY_OFF.txt decides); the override exists so one lab boot can measure
// both sides of the same spawner without a rebuild.
void CmdFix(const std::string& arg) {
    std::string a;
    for (char ch : arg) {
        if ((unsigned char)ch > ' ') a.push_back(static_cast<char>(ch | 0x20));
    }
    if (a == "off") {
        g_lifeFixOn = 0;
        g_streamFixOn = 0;
    } else if (a == "life") {
        g_lifeFixOn = 1;
        g_streamFixOn = 0;
    } else if (a == "stream") {
        g_lifeFixOn = 0;
        g_streamFixOn = 1;
    } else if (a == "all") {
        g_lifeFixOn = 1;
        g_streamFixOn = 1;
    } else if (a == "auto") {
        g_lifeFixOn = -1;
        g_streamFixOn = -1;
    } else {
        net::Log("farfix: usage: farfix <off|life|stream|all|auto> (got \"%s\")", a.c_str());
        return;
    }
    net::Log("farfix: mode=%s (lifeFix=%d streamFix=%d; FARENEMY_OFF.txt is %s)",
             a.c_str(), FixLife() ? 1 : 0, FixStream() ? 1 : 0,
             state().toggles.farEnemy ? "absent" : "present");
}

}  // namespace farenemy
}  // namespace coop
