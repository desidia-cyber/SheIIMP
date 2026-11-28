// coop_fx_grabs.cpp -- [R10-FX] Ranged Projectiles, Animal/Object Transformations,
// Player Clones & Summons, Paired Animations (Grabs/Ripostes/Executions/Finishers),
// Niagara VFX, Musical Instruments & Sitting, Co-op Photomode, and Scripted/Slide Movement sync.
//
// Wire sections:
//   ,"prj":[[seq,"clsPath",x,y,z,pitch,yaw,roll,vx,vy,vz],...]
//   ,"grb":[seq,"instKey","partKey","instMontage","partMontage",px,py,pz,pyaw]
//   ,"vfx":[[seq,"sysPath","attachKey","sock",x,y,z],...]
//   ,"xform":[active,"tag","staticMeshPath",bodyHidden]
//   ,"cln":[[id,"clsPath",x,y,z,yaw,"meshPath","monPath",monPos],...]
//   ,"lute":[playing,sitState,duet,trackIdx,"soundPath",trackTime]
//   ,"pm":[active,paused,dilation]
//   ,"srot":[scripted,pitch,roll]
// Kill switch: FX_GRABS_OFF.txt (Toggles::fxGrabs, default ON).

#include "ms2_ue.h"
#include "coop_state.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace coop {
namespace fx_grabs {

namespace {

using ue::UObject;
using ue::UFunction;

constexpr double kTxRepeatSecs = 2.0;
constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
constexpr double kRad2Deg = 180.0 / 3.14159265358979323846;

double Mono() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return duration_cast<duration<double>>(steady_clock::now() - t0).count();
}

bool Live(UObject* o) { return o != nullptr && ue::IsValid(o); }

std::string EscapeStr(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\' || (unsigned char)c < 0x20) continue;
        out.push_back(c);
    }
    return out;
}

bool ContainsCI(const std::string& hay, const char* needle) {
    if (!needle || !*needle) return false;
    const size_t nlen = std::strlen(needle);
    if (hay.size() < nlen) return false;
    for (size_t i = 0; i + nlen <= hay.size(); ++i) {
        size_t j = 0;
        for (; j < nlen; ++j) {
            char a = hay[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (j == nlen) return true;
    }
    return false;
}

std::string StripClassPrefix(std::string s) {
    size_t sp = s.find(' ');
    if (sp != std::string::npos) s.erase(0, sp + 1);
    return s;
}

// Convert UE FRotator (degrees: Pitch=Y, Yaw=Z, Roll=X) to unit FQuat
ue::FQuat RotToQuat(double pitchDeg, double yawDeg, double rollDeg) {
    const double sp = std::sin(pitchDeg * kDeg2Rad * 0.5);
    const double cp = std::cos(pitchDeg * kDeg2Rad * 0.5);
    const double sy = std::sin(yawDeg   * kDeg2Rad * 0.5);
    const double cy = std::cos(yawDeg   * kDeg2Rad * 0.5);
    const double sr = std::sin(rollDeg  * kDeg2Rad * 0.5);
    const double cr = std::cos(rollDeg  * kDeg2Rad * 0.5);
    ue::FQuat q{};
    q.X =  cr * sp * sy - sr * cp * cy;
    q.Y = -cr * sp * cy - sr * cp * sy;
    q.Z =  cr * cp * sy - sr * sp * cy;
    q.W =  cr * cp * cy + sr * sp * sy;
    return q;
}

void GetActorRot(UObject* a, double* pitch, double* yaw, double* roll) {
    *pitch = *yaw = *roll = 0.0;
    if (!Live(a)) return;
    if (UFunction* fn = ue::FindFunction(a, "K2_GetActorRotation")) {
        ue::Params p(fn);
        if (ue::Call(a, fn, p.data())) {
            ue::FRotator r = p.Get<ue::FRotator>("ReturnValue");
            *pitch = r.Pitch;
            *yaw   = r.Yaw;
            *roll  = r.Roll;
        }
    }
}

void SetActorLocRot(UObject* a, double x, double y, double z, double pitch, double yaw, double roll) {
    if (!Live(a)) return;
    if (UFunction* fn = ue::FindFunction(a, "K2_SetActorLocation")) {
        ue::Params p(fn);
        p.Set<ue::FVector>("NewLocation", ue::FVector{x, y, z});
        p.Set<bool>("bSweep", false);
        p.Set<bool>("bTeleport", true);
        ue::Call(a, fn, p.data());
    }
    if (UFunction* fn = ue::FindFunction(a, "K2_SetActorRotation")) {
        ue::Params p(fn);
        p.Set<ue::FRotator>("NewRotation", ue::FRotator{pitch, yaw, roll});
        p.Set<bool>("bTeleportPhysics", true);
        ue::Call(a, fn, p.data());
    }
}

void DestroyActorSafe(UObject* a) {
    if (!Live(a)) return;
    if (UFunction* fn = ue::FindFunction(a, "K2_DestroyActor")) {
        ue::Params p(fn);
        ue::Call(a, fn, p.data());
    }
}

void DisableActorCollisionAndAI(UObject* a) {
    if (!Live(a)) return;
    if (UFunction* fn = ue::FindFunction(a, "SetActorEnableCollision")) {
        ue::Params p(fn);
        p.Set<bool>("bNewActorEnableCollision", false);
        ue::Call(a, fn, p.data());
    }
    // Detach AI controller if this is a Pawn/Character so the visual mirror never fights or moves on its own
    UObject* ctrl = nullptr;
    if (ue::Get<UObject*>(a, "Controller", ctrl) && Live(ctrl)) {
        if (UFunction* fn = ue::FindFunction(ctrl, "UnPossess")) {
            ue::Params p(fn);
            ue::Call(ctrl, fn, p.data());
        }
    }
}

// --- Outgoing queues ---
struct OutProj {
    int seq = 0;
    std::string clsPath;
    double x = 0, y = 0, z = 0;
    double pitch = 0, yaw = 0, roll = 0;
    double vx = 0, vy = 0, vz = 0;
    double at = 0;
};

struct OutGrab {
    int seq = 0;
    std::string instKey;
    std::string partKey;
    std::string instMontage;
    std::string partMontage;
    bool hasEnemyPos = false;
    double px = 0, py = 0, pz = 0, pyaw = 0;
    double at = 0;
};

struct PairedActorLock {
    UObject* actor = nullptr;
    UObject* brain = nullptr;
    bool pausedAi = false;
    double until = 0.0;
};

struct LuteState {
    bool playing = false;
    int sitState = 0;     // 0=none, 1=sitDown, 2=sitPlay/sitting, 3=standUp
    bool duet = false;
    int trackIdx = 0;
    std::string soundPath;
    double trackTime = 0.0;
};

struct PhotoModeState {
    bool active = false;
    bool paused = false;
    double dilation = 1.0;
};

struct ScriptedRotState {
    bool scripted = false;
    double pitch = 0.0;
    double roll = 0.0;
};

struct OutVfx {
    int seq = 0;
    std::string sysPath;
    std::string attachKey;
    std::string sock;
    double x = 0, y = 0, z = 0;
    double at = 0;
};

struct XformState {
    bool active = false;
    std::string tag;
    std::string staticMesh;
    bool bodyHidden = false;
};

struct CloneEntry {
    int id = 0;
    std::string clsPath;
    double x = 0, y = 0, z = 0;
    double yaw = 0;
    std::string meshPath;
    std::string monPath;
    double monPos = 0.0;
};

struct PeerCloneMirror {
    int id = 0;
    UObject* actor = nullptr;
    std::string clsPath;
    std::string appliedMesh;
    std::string appliedMon;
    double lastSeen = 0.0;
};

std::deque<OutProj> g_projQ;
std::deque<OutGrab> g_grabQ;
std::deque<OutVfx>  g_vfxQ;

// For fallback scanning of non-hooked projectiles: track last seen time & pos (handles pooled reuse)
struct SeenProjInfo {
    double lastAt = 0.0;
    double x = 0.0, y = 0.0, z = 0.0;
};
std::unordered_map<UObject*, SeenProjInfo> g_seenProjs;
std::unordered_set<UObject*> g_remoteProjs; // visual mirrors we spawned; never re-broadcast

int g_nextProjSeq = 1, g_lastRxProjSeq = 0;
int g_nextGrabSeq = 1, g_lastRxGrabSeq = 0;
int g_nextVfxSeq  = 1, g_lastRxVfxSeq  = 0;

bool g_hooksInstalled = false;
bool g_projHooksInstalled = false;
thread_local bool tl_replaying = false;

// Transformation state (local & peer)
XformState g_localXform;
XformState g_localXformOverride;
bool       g_useXformOverride = false;
double     g_xformClearUntil = 0.0; // keep sending active=0 for 2s after detransform

XformState g_peerXform;
bool       g_peerNonHumanoid = false;
bool       g_peerHideBody = false;
UObject*   g_peerXformStaticComp = nullptr;
std::string g_peerXformStaticApplied;

// Player Clones & Summons (local & peer)
int g_nextCloneId = 1;
std::unordered_map<UObject*, int> g_localCloneIds;
std::vector<CloneEntry> g_localClones;
double g_clonesClearUntil = 0.0;
std::vector<UObject*> g_testClones; // spawned by fxtest clone_*

std::unordered_map<int, PeerCloneMirror> g_peerClones;

// Telemetry counters
int g_projTx = 0, g_projRx = 0;
int g_grabTx = 0, g_grabRx = 0;
int g_vfxTx  = 0, g_vfxRx  = 0;
int g_xformTx = 0, g_xformRx = 0;
int g_cloneTx = 0, g_cloneRx = 0, g_cloneAnimRx = 0;
int g_luteTx = 0, g_luteRx = 0;
int g_photoTx = 0, g_photoRx = 0, g_photoVeto = 0;
int g_srotTx = 0, g_srotRx = 0;

// Active paired-animation locks on enemies (prevents MirrorDrive / AI from clobbering finishers)
std::vector<PairedActorLock> g_pairedLocks;

// Musical instrument & sitting state (local & peer)
LuteState g_localLute;
LuteState g_localLuteOverride;
bool      g_useLuteOverride = false;
LuteState g_peerLute;
double g_luteClearUntil = 0.0;
double g_peerLuteSeenAt = 0.0;
UObject* g_peerLuteAudioComp = nullptr;
std::string g_peerLuteAudioPath;
std::vector<UObject*> g_peerGroovingDetectors;

// Photomode state (local & peer)
PhotoModeState g_localPhoto;
PhotoModeState g_peerPhoto;
double g_photoClearUntil = 0.0;
double g_peerPhotoSeenAt = 0.0;
double g_photoDeactivatingUntil = 0.0;
bool g_photoHooksInstalled = false;
bool g_execHooksInstalled = false;
bool g_coopPhotoPausedUs = false;

// Sliding / skydive / spline scripted movement + 3D rotation state (local & peer)
ScriptedRotState g_localSrot;
ScriptedRotState g_localSrotOverride;
bool             g_useSrotOverride = false;
ScriptedRotState g_peerSrot;
double g_srotClearUntil = 0.0;
double g_peerSrotSeenAt = 0.0;

std::string KeyForActor(UObject* a) {
    if (!Live(a)) return "";
    if (a == net::Pawn()) return "@peer"; // from receiver's POV, our pawn is their puppet
    if (a == puppet::Actor()) return "@self";
    const std::string tail = net::Tail(a);
    const std::string* alias = state().combat.spawnAlias.Find(tail);
    return (alias && !alias->empty()) ? *alias : tail;
}

UObject* ActorForKey(const std::string& key) {
    if (key.empty()) return nullptr;
    if (key == "@peer") return puppet::Actor();
    if (key == "@self") return net::Pawn();
    for (const NamedActor& na : state().local.tickTails) {
        if (na.tail == key && Live(na.obj)) return na.obj;
    }
    if (key.size() > 3 && key.compare(0, 3, "SP_") == 0) {
        const std::string spTail = key.substr(3);
        for (const NamedActor& sp : state().combat.spawners) {
            if (sp.tail == spTail && Live(sp.obj)) {
                UObject* e = nullptr;
                if (ue::Get<UObject*>(sp.obj, "SpawnedEnemy", e) && Live(e)) return e;
            }
        }
    }
    return nullptr;
}

bool IsOwnedByLocalPawn(UObject* a, UObject* pawn) {
    if (!Live(a) || !Live(pawn)) return false;
    UObject* inst = nullptr;
    if (ue::Get<UObject*>(a, "Instigator", inst) && inst == pawn) return true;
    UObject* own = nullptr;
    if (ue::Get<UObject*>(a, "Owner", own) && own == pawn) return true;
    UObject* ownChar = nullptr;
    if (ue::Get<UObject*>(a, "OwnerCharacter", ownChar) && ownChar == pawn) return true;
    UObject* origWpn = nullptr;
    if (ue::Get<UObject*>(a, "OriginalWeapon", origWpn) && Live(origWpn)) {
        UObject* wo = nullptr;
        if (ue::Get<UObject*>(origWpn, "Owner", wo) && wo == pawn) return true;
    }
    UObject* astralComp = nullptr;
    if (ue::Get<UObject*>(a, "Astral_AI_Component", astralComp) && Live(astralComp)) {
        UObject* aOwn = nullptr;
        if (ue::Get<UObject*>(astralComp, "OwnerSpartaCharacter", aOwn) && aOwn == pawn) return true;
    }
    return false;
}

void RecordLocalProjectile(UObject* prj) {
    if (tl_replaying || !state().toggles.fxGrabs || !state().peer.present) return;
    if (!Live(prj) || g_remoteProjs.count(prj)) return;
    UObject* pawn = net::Pawn();
    if (!Live(pawn) || !IsOwnedByLocalPawn(prj, pawn)) return;

    const double now = Mono();
    const Vec3 loc = net::Loc(prj);
    auto it = g_seenProjs.find(prj);
    if (it != g_seenProjs.end()) {
        // Deduplicate if the same projectile was already recorded < 0.05s ago at nearly the same spot
        const double dx = loc.X - it->second.x;
        const double dy = loc.Y - it->second.y;
        const double dz = loc.Z - it->second.z;
        if ((now - it->second.lastAt) < 0.05 && (dx*dx + dy*dy + dz*dz) < 100.0) return;
    }
    g_seenProjs[prj] = SeenProjInfo{now, loc.X, loc.Y, loc.Z};

    ue::UClass* cls = ue::GetClass(prj);
    if (!cls) return;
    std::string cpath = StripClassPrefix(ue::GetFullName(reinterpret_cast<UObject*>(cls)));
    if (cpath.size() < 8) return;

    OutProj op;
    op.seq = g_nextProjSeq++;
    op.clsPath = cpath;
    op.x = loc.X; op.y = loc.Y; op.z = loc.Z;
    GetActorRot(prj, &op.pitch, &op.yaw, &op.roll);

    // Read velocity from ProjectileMovement first, then GetVelocity(), then SuggestedVelocity (throwables)
    bool gotVel = false;
    UObject* pmc = nullptr;
    if (ue::Get<UObject*>(prj, "ProjectileMovement", pmc) && Live(pmc)) {
        ue::FVector v{};
        if (ue::Get<ue::FVector>(pmc, "Velocity", v) && (v.X*v.X + v.Y*v.Y + v.Z*v.Z) > 1.0) {
            op.vx = v.X; op.vy = v.Y; op.vz = v.Z;
            gotVel = true;
        }
    }
    if (!gotVel) {
        if (UFunction* fnVel = ue::FindFunction(prj, "GetVelocity")) {
            ue::Params p(fnVel);
            if (ue::Call(prj, fnVel, p.data())) {
                ue::FVector v = p.Get<ue::FVector>("ReturnValue");
                if ((v.X*v.X + v.Y*v.Y + v.Z*v.Z) > 1.0) {
                    op.vx = v.X; op.vy = v.Y; op.vz = v.Z;
                    gotVel = true;
                }
            }
        }
    }
    if (!gotVel) {
        ue::FVector sv{};
        if (ue::Get<ue::FVector>(prj, "SuggestedVelocity", sv) && (sv.X*sv.X + sv.Y*sv.Y + sv.Z*sv.Z) > 1.0) {
            op.vx = sv.X; op.vy = sv.Y; op.vz = sv.Z;
            gotVel = true;
        }
    }
    if (!gotVel) {
        // Fallback: forward vector from actor rotation at 1500 uu/s
        const double cp = std::cos(op.pitch * kDeg2Rad);
        op.vx = cp * std::cos(op.yaw * kDeg2Rad) * 1500.0;
        op.vy = cp * std::sin(op.yaw * kDeg2Rad) * 1500.0;
        op.vz = std::sin(op.pitch * kDeg2Rad) * 1500.0;
    }

    op.at = now;
    g_projQ.push_back(std::move(op));
    while (g_projQ.size() > 16) g_projQ.pop_front();
    ++g_projTx;
}

// HookPost on BP_ProjectileBase_C.ReceiveBeginPlay / ReBeginPlay and WP_ThrowableWeaponBase_C.ReceiveBeginPlay
void OnProjectileSpawnOrReInit(UObject* self, UFunction*, void*) {
    if (!Live(self)) return;
    // ReceiveBeginPlay is inherited from AActor; filter strictly to projectiles & throwables
    const std::string cn = ue::GetClassName(self);
    if (!ContainsCI(cn, "Projectile") && !ContainsCI(cn, "Throwable")) return;
    RecordLocalProjectile(self);
}

void QueueGrabEvent(UObject* inst, UObject* part, UObject* iMon, UObject* pMon) {
    UObject* pawn = net::Pawn();
    if (!Live(pawn) || (inst != pawn && part != pawn)) return;

    OutGrab g;
    g.seq = g_nextGrabSeq++;
    g.instKey = KeyForActor(inst);
    g.partKey = KeyForActor(part);
    g.instMontage = Live(iMon) ? StripClassPrefix(ue::GetFullName(iMon)) : "";
    g.partMontage = Live(pMon) ? StripClassPrefix(ue::GetFullName(pMon)) : "";
    // Capture the enemy's world location + yaw so the observer can align the finisher cleanly
    UObject* enemy = (inst == pawn) ? part : inst;
    if (Live(enemy) && enemy != pawn) {
        const Vec3 loc = net::Loc(enemy);
        double ep = 0, ey = 0, er = 0;
        GetActorRot(enemy, &ep, &ey, &er);
        g.hasEnemyPos = true;
        g.px = loc.X; g.py = loc.Y; g.pz = loc.Z; g.pyaw = ey;
    }
    g.at = Mono();
    g_grabQ.push_back(std::move(g));
    while (g_grabQ.size() > 4) g_grabQ.pop_front();
    ++g_grabTx;
}

UObject* GetCurrentMontageOf(UObject* actor) {
    if (!Live(actor)) return nullptr;
    if (UFunction* fn = ue::FindFunction(actor, "GetCurrentMontage")) {
        ue::Params p(fn);
        if (ue::Call(actor, fn, p.data())) {
            UObject* m = p.Get<UObject*>("ReturnValue");
            if (Live(m)) return m;
        }
    }
    return nullptr;
}

// HookPost on BPFL_Utility_C.TriggerPairedAnimation
void OnTriggerPairedAnimation(UObject*, UFunction* fn, void* parms) {
    if (tl_replaying || !state().toggles.fxGrabs || !state().peer.present || !fn || !parms) return;
    UObject* inst = nullptr;
    UObject* part = nullptr;
    UObject* iMon = nullptr;
    UObject* pMon = nullptr;
    const ue::UStruct* fs = reinterpret_cast<const ue::UStruct*>(fn);
    auto readPtr = [&](const char* name, UObject** dst) {
        int32_t off = ue::PropOffsetStruct(const_cast<ue::UStruct*>(fs), name);
        if (off >= 0) ue::ReadMem(reinterpret_cast<const uint8_t*>(parms) + off, dst, sizeof(*dst));
    };
    readPtr("Instigator", &inst);
    readPtr("Participant", &part);
    readPtr("InstigatorMontage", &iMon);
    readPtr("ParticipantMontage", &pMon);
    QueueGrabEvent(inst, part, iMon, pMon);
}

// HookPost on GA_Player_ExecutionBase_C.K2_ActivateAbilityFromEvent
// Catches enemy executions that may bypass BPFL_Utility_C.TriggerPairedAnimation.
void OnPlayerExecutionActivate(UObject* self, UFunction*, void*) {
    if (tl_replaying || !state().toggles.fxGrabs || !state().peer.present || !Live(self)) return;
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return;
    // EventData (FGameplayEventData) at property "Event Data" contains Target @0x18 or Instigator @0x10
    int32_t edOff = ue::PropOffset(self, "Event Data");
    if (edOff < 0) return;
    UObject* target = nullptr;
    // In FGameplayEventData: EventTag(0x00,8), Instigator(0x08,8), Target(0x10,8)
    ue::ReadMem(reinterpret_cast<const uint8_t*>(self) + edOff + 0x10, &target, sizeof(target));
    if (!Live(target) || target == pawn) {
        // Fallback: nearest enemy within 350 uu
        const Vec3 ploc = net::Loc(pawn);
        double bestD = 350.0;
        for (const NamedActor& na : state().local.tickTails) {
            if (!Live(na.obj) || na.obj == pawn || na.obj == puppet::Actor()) continue;
            const double d = coop::Dist(ploc, net::Loc(na.obj));
            if (d < bestD) { bestD = d; target = na.obj; }
        }
    }
    if (!Live(target)) return;
    UObject* iMon = GetCurrentMontageOf(pawn);
    UObject* pMon = GetCurrentMontageOf(target);
    if (Live(iMon) || Live(pMon)) {
        QueueGrabEvent(pawn, target, iMon, pMon);
    }
}

// HookPost on NiagaraFunctionLibrary.SpawnSystemAttached
void OnSpawnSystemAttached(UObject*, UFunction* fn, void* parms) {
    if (tl_replaying || !state().toggles.fxGrabs || !state().peer.present || !fn || !parms) return;
    static double lastVfx = 0.0;
    const double now = Mono();
    if (now - lastVfx < 0.08) return; // cap at ~12 Hz

    const ue::UStruct* fs = reinterpret_cast<const ue::UStruct*>(fn);
    UObject* sys = nullptr;
    UObject* comp = nullptr;
    int32_t sysOff = ue::PropOffsetStruct(const_cast<ue::UStruct*>(fs), "SystemTemplate");
    int32_t compOff = ue::PropOffsetStruct(const_cast<ue::UStruct*>(fs), "AttachToComponent");
    if (sysOff < 0 || compOff < 0) return;
    ue::ReadMem(reinterpret_cast<const uint8_t*>(parms) + sysOff, &sys, sizeof(sys));
    ue::ReadMem(reinterpret_cast<const uint8_t*>(parms) + compOff, &comp, sizeof(comp));
    if (!Live(sys) || !Live(comp)) return;

    // Replicate VFX attached to local pawn OR to a weapon/instrument held by local pawn (e.g. lute notes, weapon buffs)
    UObject* pawn = net::Pawn();
    UObject* outer = ue::GetOuter(comp);
    bool ownedByPawn = (outer == pawn);
    if (!ownedByPawn && Live(outer) && Live(pawn)) {
        UObject* holder = nullptr;
        if (ue::Get<UObject*>(outer, "HoldingCharacter", holder) && holder == pawn) {
            ownedByPawn = true;
        } else if (IsOwnedByLocalPawn(outer, pawn)) {
            ownedByPawn = true;
        }
    }
    if (!ownedByPawn) return;

    std::string spath = StripClassPrefix(ue::GetFullName(sys));
    if (spath.size() < 8) return;

    ue::FName sockFn{};
    int32_t sockOff = ue::PropOffsetStruct(const_cast<ue::UStruct*>(fs), "AttachPointName");
    if (sockOff >= 0) ue::ReadMem(reinterpret_cast<const uint8_t*>(parms) + sockOff, &sockFn, sizeof(sockFn));
    std::string sock = sockFn.ComparisonIndex ? ue::ToString(sockFn) : "";
    if (sock == "None") sock.clear();

    OutVfx v;
    v.seq = g_nextVfxSeq++;
    v.sysPath = spath;
    v.attachKey = "@peer";
    v.sock = sock;
    const Vec3 loc = net::Loc(net::Pawn());
    v.x = loc.X; v.y = loc.Y; v.z = loc.Z;
    v.at = now;
    g_vfxQ.push_back(std::move(v));
    while (g_vfxQ.size() > 8) g_vfxQ.pop_front();
    lastVfx = now;
    ++g_vfxTx;
}

void ScanLocalProjectiles() {
    if (!state().peer.present) return;
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return;

    for (auto it = g_seenProjs.begin(); it != g_seenProjs.end(); ) {
        if (!Live(it->first)) it = g_seenProjs.erase(it);
        else ++it;
    }
    for (auto it = g_remoteProjs.begin(); it != g_remoteProjs.end(); ) {
        if (!Live(*it)) it = g_remoteProjs.erase(it);
        else ++it;
    }

    // Fallback scan for any SpartaProjectile or WP_ThrowableWeaponBase not caught by hooks
    for (UObject* prj : ue::FindAllOf("SpartaProjectile")) {
        if (!Live(prj) || g_remoteProjs.count(prj)) continue;
        if (g_seenProjs.count(prj)) continue;
        RecordLocalProjectile(prj);
    }
    for (UObject* thr : ue::FindAllOf("WP_ThrowableWeaponBase_C")) {
        if (!Live(thr) || g_remoteProjs.count(thr)) continue;
        if (g_seenProjs.count(thr)) continue;
        RecordLocalProjectile(thr);
    }
}

// ===========================================================================
// Animal & Object Transformations (Sheep, Bush, Mango, Tarred Corpse)
// ===========================================================================
void SampleLocalTransform() {
    XformState next{};
    if (g_useXformOverride) {
        next = g_localXformOverride;
    } else {
        UObject* pawn = net::Pawn();
        UObject* world = net::World();
        if (Live(pawn) && Live(world)) {
            ue::UClass* libCls = ue::FindClass("BPFL_Transformation_C");
            UObject* libCdo = libCls ? ue::GetCDO(libCls) : nullptr;
            if (Live(libCdo)) {
                bool isX = false;
                if (UFunction* fnIs = ue::FindFunction(libCdo, "IsCharacterTransformed")) {
                    ue::Params p(fnIs);
                    p.Set<UObject*>("Character", pawn);
                    p.Set<UObject*>("__WorldContext", world);
                    if (ue::Call(libCdo, fnIs, p.data())) isX = p.Get<bool>("ReturnValue");
                }
                if (isX) {
                    next.active = true;
                    if (UFunction* fnFind = ue::FindFunction(libCdo, "FindCurrentTransformation")) {
                        ue::Params p(fnFind);
                        p.Set<UObject*>("Character", pawn);
                        p.Set<UObject*>("__WorldContext", world);
                        if (ue::Call(libCdo, fnFind, p.data())) {
                            ue::FName tagName = p.Get<ue::FName>("ReturnValue");
                            if (tagName.ComparisonIndex) next.tag = ue::ToString(tagName);
                        }
                    }
                    // Check if the player's main SkeletalMesh is hidden (e.g., Bush / Mango)
                    UObject* mesh = nullptr;
                    if (ue::Get<UObject*>(pawn, "Mesh", mesh) && Live(mesh)) {
                        if (UFunction* fnVis = ue::FindFunction(mesh, "IsVisible")) {
                            ue::Params p(fnVis);
                            if (ue::Call(mesh, fnVis, p.data())) {
                                next.bodyHidden = !p.Get<bool>("ReturnValue");
                            }
                        }
                        if (!next.bodyHidden) {
                            uint8_t b = 0;
                            int32_t off = ue::PropOffset(mesh, "bHiddenInGame");
                            if (off >= 0 && ue::ReadMem(reinterpret_cast<const uint8_t*>(mesh) + off, &b, 1)) {
                                if (b & 0x80) next.bodyHidden = true;
                            }
                        }
                    }
                    // If Bush/Mango, grab LoadedStaticMesh from active GA_TransformationHandlerBase_C
                    if (next.bodyHidden || ContainsCI(next.tag, "Bush") || ContainsCI(next.tag, "Mango")) {
                        next.bodyHidden = true;
                        for (UObject* ga : ue::FindAllOf("GA_TransformationHandlerBase_C")) {
                            if (!Live(ga)) continue;
                            UObject* sm = nullptr;
                            if (ue::Get<UObject*>(ga, "LoadedStaticMesh", sm) && Live(sm)) {
                                next.staticMesh = StripClassPrefix(ue::GetFullName(sm));
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    if (next.active != g_localXform.active || next.tag != g_localXform.tag ||
        next.staticMesh != g_localXform.staticMesh || next.bodyHidden != g_localXform.bodyHidden) {
        if (g_localXform.active && !next.active) {
            g_xformClearUntil = Mono() + 2.5;
        }
        g_localXform = next;
        ++g_xformTx;
        net::Log("fx_grabs: local xform -> active=%d tag=%s hidden=%d sm=%s",
                 next.active ? 1 : 0, next.tag.c_str(), next.bodyHidden ? 1 : 0, next.staticMesh.c_str());
    }
}

void DestroyPeerXformStatic() {
    if (Live(g_peerXformStaticComp)) {
        if (UFunction* fn = ue::FindFunction(g_peerXformStaticComp, "K2_DestroyComponent")) {
            ue::Params p(fn);
            p.Set<UObject*>("Object", g_peerXformStaticComp);
            ue::Call(g_peerXformStaticComp, fn, p.data());
        }
    }
    g_peerXformStaticComp = nullptr;
    g_peerXformStaticApplied.clear();
}

void ApplyPeerXform(const XformState& xf) {
    const bool changed = (xf.active != g_peerXform.active || xf.tag != g_peerXform.tag ||
                          xf.staticMesh != g_peerXform.staticMesh || xf.bodyHidden != g_peerXform.bodyHidden);
    g_peerXform = xf;
    g_peerNonHumanoid = xf.active && (ContainsCI(xf.tag, "Sheep") || ContainsCI(xf.tag, "Animal"));
    g_peerHideBody = xf.active && xf.bodyHidden;

    UObject* pup = puppet::Actor();
    UObject* mesh = puppet::Mesh();
    if (Live(mesh)) {
        if (UFunction* fnVis = ue::FindFunction(mesh, "SetVisibility")) {
            ue::Params p(fnVis);
            p.Set<bool>("bNewVisibility", !g_peerHideBody);
            p.Set<bool>("bPropagateToChildren", false);
            ue::Call(mesh, fnVis, p.data());
        }
    }

    if (g_peerHideBody && !xf.staticMesh.empty() && Live(pup)) {
        if (g_peerXformStaticApplied != xf.staticMesh || !Live(g_peerXformStaticComp)) {
            DestroyPeerXformStatic();
            UObject* smAsset = net::Find(xf.staticMesh, true);
            UObject* smCls = net::EngineClass("StaticMeshComponent");
            if (Live(smAsset) && Live(smCls)) {
                if (UFunction* fnAdd = ue::FindFunction(pup, "AddComponentByClass")) {
                    ue::FTransform id{};
                    id.Rotation.W = 1.0;
                    id.Scale3D = ue::FVector{1.0, 1.0, 1.0};
                    ue::Params p(fnAdd);
                    p.Set<UObject*>("Class", smCls);
                    p.Set<bool>("bManualAttachment", false);
                    p.Set<ue::FTransform>("RelativeTransform", id);
                    p.Set<bool>("bDeferredFinish", false);
                    if (ue::Call(pup, fnAdd, p.data())) {
                        UObject* comp = p.Get<UObject*>("ReturnValue");
                        if (Live(comp)) {
                            if (UFunction* fnSet = ue::FindFunction(comp, "SetStaticMesh")) {
                                ue::Params ps(fnSet);
                                ps.Set<UObject*>("NewMesh", smAsset);
                                ue::Call(comp, fnSet, ps.data());
                            }
                            if (UFunction* fnCol = ue::FindFunction(comp, "SetCollisionEnabled")) {
                                ue::Params pc(fnCol);
                                pc.Set<uint8_t>("NewType", 0); // NoCollision
                                ue::Call(comp, fnCol, pc.data());
                            }
                            g_peerXformStaticComp = comp;
                            g_peerXformStaticApplied = xf.staticMesh;
                        }
                    }
                }
            }
        }
    } else if (!g_peerHideBody) {
        DestroyPeerXformStatic();
    }

    if (changed) {
        ++g_xformRx;
        net::Log("fx_grabs: peer xform <- active=%d tag=%s nonHum=%d hideBody=%d sm=%s",
                 xf.active ? 1 : 0, xf.tag.c_str(), g_peerNonHumanoid ? 1 : 0,
                 g_peerHideBody ? 1 : 0, xf.staticMesh.c_str());
    }
}

// ===========================================================================
// Player Clones & Summons (Tiel ShadowClone, Genessa AstralClones, Decoy)
// ===========================================================================
void ScanLocalClones() {
    if (!state().peer.present) return;
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return;

    const bool hadClones = !g_localClones.empty();
    g_localClones.clear();

    // Prune dead clone IDs
    for (auto it = g_localCloneIds.begin(); it != g_localCloneIds.end(); ) {
        if (!Live(it->first)) it = g_localCloneIds.erase(it);
        else ++it;
    }

    static const char* const kCloneClasses[] = {
        "BP_Shell_Tiel_ShadowClone_C",
        "BP_Shell_Genessa_AstralBase_C",
        "BP_Shell_Decoy_C",
        "BP_Decoy_Bait_C",
        "BP_Shell_Summon_C"
    };

    std::unordered_set<UObject*> seenThisTick;
    auto pushLocalClone = [&](UObject* c) {
        if (!Live(c) || (int)g_localClones.size() >= 8) return;
        if (seenThisTick.count(c)) return;
        // Skip any peer clone mirror we spawned
        for (const auto& kv : g_peerClones) {
            if (kv.second.actor == c) return;
        }
        seenThisTick.insert(c);

        int& id = g_localCloneIds[c];
        if (id == 0) {
            id = g_nextCloneId++;
            ++g_cloneTx;
            net::Log("fx_grabs: local clone #%d spawned cls=%s", id, ue::GetClassName(c).c_str());
        }

        CloneEntry ce;
        ce.id = id;
        ue::UClass* cls = ue::GetClass(c);
        ce.clsPath = cls ? StripClassPrefix(ue::GetFullName(reinterpret_cast<UObject*>(cls))) : "";
        const Vec3 loc = net::Loc(c);
        ce.x = loc.X; ce.y = loc.Y; ce.z = loc.Z;
        double pitch = 0, yaw = 0, roll = 0;
        GetActorRot(c, &pitch, &yaw, &roll);
        ce.yaw = yaw;

        UObject* mesh = nullptr;
        if (ue::Get<UObject*>(c, "Mesh", mesh) && Live(mesh)) {
            UObject* asset = nullptr;
            if (UFunction* fn = ue::FindFunction(mesh, "GetSkeletalMeshAsset")) {
                ue::Params p(fn);
                if (ue::Call(mesh, fn, p.data())) asset = p.Get<UObject*>("ReturnValue");
            }
            if (Live(asset)) ce.meshPath = StripClassPrefix(ue::GetFullName(asset));

            // Sample active animation montage + playback position for full visual attack sync
            if (UFunction* fnAi = ue::FindFunction(mesh, "GetAnimInstance")) {
                ue::Params pa(fnAi);
                if (ue::Call(mesh, fnAi, pa.data())) {
                    UObject* ai = pa.Get<UObject*>("ReturnValue");
                    if (Live(ai)) {
                        if (UFunction* fnCur = ue::FindFunction(ai, "GetCurrentActiveMontage")) {
                            ue::Params pc(fnCur);
                            if (ue::Call(ai, fnCur, pc.data())) {
                                UObject* mon = pc.Get<UObject*>("ReturnValue");
                                if (Live(mon)) {
                                    ce.monPath = StripClassPrefix(ue::GetFullName(mon));
                                    if (UFunction* fnPos = ue::FindFunction(ai, "Montage_GetPosition")) {
                                        ue::Params pp(fnPos);
                                        pp.Set<UObject*>("Montage", mon);
                                        if (ue::Call(ai, fnPos, pp.data())) {
                                            ce.monPos = (double)pp.Get<float>("ReturnValue");
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        g_localClones.push_back(std::move(ce));
    };

    for (const char* clsName : kCloneClasses) {
        for (UObject* c : ue::FindAllOf(clsName)) {
            if (!Live(c)) continue;
            if (!IsOwnedByLocalPawn(c, pawn)) continue;
            pushLocalClone(c);
        }
    }

    // Also catch ALL Sidearm Ally Summons (GA_Sidearm_AllySummoner_C / GA_SummonedAlly_Handler_C)
    // and Grisha Aid Summons (GA_Summon_Aid_Grisha_C / GA_SummonedGrisha_Handler_C), which spawn
    // standard enemy classes carrying a BPC_AstralAI_C component owned by the local player.
    for (UObject* astral : ue::FindAllOf("BPC_AstralAI_C")) {
        if (!Live(astral)) continue;
        if (ue::GetBool(astral, "bIsCached")) continue;
        if (!ue::GetBool(astral, "bCharacterEnabled")) continue;
        UObject* aOwn = nullptr;
        if (!ue::Get<UObject*>(astral, "OwnerSpartaCharacter", aOwn) || aOwn != pawn) continue;
        UObject* ownerActor = ue::GetOuter(astral);
        if (!Live(ownerActor) || ownerActor == pawn || ownerActor == puppet::Actor()) continue;
        pushLocalClone(ownerActor);
    }

    if (hadClones && g_localClones.empty()) {
        g_clonesClearUntil = Mono() + 2.0;
    }
}

void ClearPeerClones() {
    for (auto& kv : g_peerClones) {
        DestroyActorSafe(kv.second.actor);
    }
    g_peerClones.clear();
}

void ApplyPeerClones(const std::vector<CloneEntry>& list) {
    const double now = Mono();
    std::unordered_set<int> activeIds;

    ue::UClass* gsCls = ue::FindClass("GameplayStatics");
    UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
    UObject* world = net::World();
    UFunction* fnBegin = Live(gsCdo) ? ue::FindFunction(gsCdo, "BeginDeferredActorSpawnFromClass") : nullptr;
    UFunction* fnFin   = Live(gsCdo) ? ue::FindFunction(gsCdo, "FinishSpawningActor") : nullptr;

    for (const CloneEntry& ce : list) {
        activeIds.insert(ce.id);
        PeerCloneMirror& m = g_peerClones[ce.id];
        m.id = ce.id;
        m.lastSeen = now;

        if (!Live(m.actor) && fnBegin && fnFin && Live(world) && ce.clsPath.size() > 8) {
            UObject* cls = net::Find(ce.clsPath, true);
            if (Live(cls)) {
                ue::FTransform xf{};
                xf.Rotation = RotToQuat(0.0, ce.yaw, 0.0);
                xf.Translation = ue::FVector{ce.x, ce.y, ce.z};
                xf.Scale3D = ue::FVector{1.0, 1.0, 1.0};

                tl_replaying = true;
                ue::Params pb(fnBegin);
                pb.Set<UObject*>("WorldContextObject", world);
                pb.Set<UObject*>("ActorClass", cls);
                pb.Set<ue::FTransform>("SpawnTransform", xf);
                pb.Set<uint8_t>("CollisionHandlingOverride", 1); // AlwaysSpawn
                pb.Set<UObject*>("Owner", puppet::Actor());
                if (ue::Call(gsCdo, fnBegin, pb.data())) {
                    UObject* spawned = pb.Get<UObject*>("ReturnValue");
                    if (Live(spawned)) {
                        // Set OwnerCharacter to puppet so clone init doesn't bind to local player
                        ue::Set<UObject*>(spawned, "OwnerCharacter", puppet::Actor());
                        ue::Params pf(fnFin);
                        pf.Set<UObject*>("Actor", spawned);
                        pf.Set<ue::FTransform>("SpawnTransform", xf);
                        ue::Call(gsCdo, fnFin, pf.data());
                        DisableActorCollisionAndAI(spawned);
                        m.actor = spawned;
                        m.clsPath = ce.clsPath;
                        m.appliedMesh.clear();
                        ++g_cloneRx;
                        net::Log("fx_grabs: peer clone #%d mirrored cls=%s", ce.id, ue::GetClassName(spawned).c_str());
                    }
                }
                tl_replaying = false;
            }
        }

        if (Live(m.actor)) {
            SetActorLocRot(m.actor, ce.x, ce.y, ce.z, 0.0, ce.yaw, 0.0);
            UObject* mesh = nullptr;
            if (ue::Get<UObject*>(m.actor, "Mesh", mesh) && Live(mesh)) {
                if (!ce.meshPath.empty() && m.appliedMesh != ce.meshPath) {
                    UObject* asset = net::Find(ce.meshPath, true);
                    if (Live(asset)) {
                        if (UFunction* fnSet = ue::FindFunction(mesh, "SetSkeletalMeshAsset")) {
                            ue::Params p(fnSet);
                            p.Set<UObject*>("NewMesh", asset);
                            ue::Call(mesh, fnSet, p.data());
                            m.appliedMesh = ce.meshPath;
                        }
                    }
                }

                // Full visual attack/ability montage sync on the clone/summon mirror
                UObject* ai = nullptr;
                if (UFunction* fnAi = ue::FindFunction(mesh, "GetAnimInstance")) {
                    ue::Params pa(fnAi);
                    if (ue::Call(mesh, fnAi, pa.data())) ai = pa.Get<UObject*>("ReturnValue");
                }
                if (!Live(ai) && !ce.monPath.empty()) {
                    // Ensure mirror mesh has an AnimInstance (e.g. BP_Shell_Summon_C before InitDefaultMesh)
                    if (UFunction* fnInit = ue::FindFunction(m.actor, "InitDefaultMesh")) {
                        ue::Params pi(fnInit);
                        ue::Call(m.actor, fnInit, pi.data());
                    }
                    if (UFunction* fnAi = ue::FindFunction(mesh, "GetAnimInstance")) {
                        ue::Params pa(fnAi);
                        if (ue::Call(mesh, fnAi, pa.data())) ai = pa.Get<UObject*>("ReturnValue");
                    }
                    if (!Live(ai)) {
                        UObject* pm = puppet::Mesh();
                        UObject* animCls = nullptr;
                        if (Live(pm) && ue::Get<UObject*>(pm, "AnimClass", animCls) && Live(animCls)) {
                            if (UFunction* fnMode = ue::FindFunction(mesh, "SetAnimationMode")) {
                                ue::Params pm2(fnMode);
                                pm2.Set<uint8_t>("InAnimationMode", 0); // UseAnimationBlueprint
                                ue::Call(mesh, fnMode, pm2.data());
                            }
                            if (UFunction* fnSetAi = ue::FindFunction(mesh, "SetAnimInstanceClass")) {
                                ue::Params ps(fnSetAi);
                                ps.Set<UObject*>("NewClass", animCls);
                                ue::Call(mesh, fnSetAi, ps.data());
                            }
                            if (UFunction* fnAi = ue::FindFunction(mesh, "GetAnimInstance")) {
                                ue::Params pa(fnAi);
                                if (ue::Call(mesh, fnAi, pa.data())) ai = pa.Get<UObject*>("ReturnValue");
                            }
                        }
                    }
                }
                if (Live(ai)) {
                    if (!ce.monPath.empty()) {
                        UObject* mon = net::Find(ce.monPath, false);
                        if (!Live(mon)) mon = net::Find(ce.monPath, true);
                        if (Live(mon)) {
                            bool playing = false;
                            if (UFunction* fnIs = ue::FindFunction(ai, "Montage_IsPlaying")) {
                                ue::Params pi(fnIs);
                                pi.Set<UObject*>("Montage", mon);
                                if (ue::Call(ai, fnIs, pi.data())) playing = pi.Get<bool>("ReturnValue");
                            }
                            if (m.appliedMon != ce.monPath || !playing) {
                                if (UFunction* fnPlay = ue::FindFunction(ai, "Montage_Play")) {
                                    ue::Params pp(fnPlay);
                                    pp.Set<UObject*>("MontageToPlay", mon);
                                    pp.Set<float>("InPlayRate", 1.0f);
                                    pp.Set<uint8_t>("ReturnValueType", 0);
                                    pp.Set<float>("InTimeToStartMontageAt", (float)ce.monPos);
                                    pp.Set<bool>("bStopAllMontages", true);
                                    if (ue::Call(ai, fnPlay, pp.data())) {
                                        m.appliedMon = ce.monPath;
                                        ++g_cloneAnimRx;
                                        net::Log("fx_grabs: peer clone #%d montage %s @%.2fs",
                                                 ce.id, ue::GetName(mon).c_str(), ce.monPos);
                                    }
                                }
                            } else {
                                // Phase-lock playback position if drifted > 0.25s
                                if (UFunction* fnGet = ue::FindFunction(ai, "Montage_GetPosition")) {
                                    ue::Params pg(fnGet);
                                    pg.Set<UObject*>("Montage", mon);
                                    if (ue::Call(ai, fnGet, pg.data())) {
                                        float curPos = pg.Get<float>("ReturnValue");
                                        if (std::fabs((double)curPos - ce.monPos) > 0.25) {
                                            if (UFunction* fnSetP = ue::FindFunction(ai, "Montage_SetPosition")) {
                                                ue::Params ps(fnSetP);
                                                ps.Set<UObject*>("Montage", mon);
                                                ps.Set<float>("NewPosition", (float)ce.monPos);
                                                ue::Call(ai, fnSetP, ps.data());
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else if (!m.appliedMon.empty()) {
                        if (UFunction* fnStop = ue::FindFunction(ai, "Montage_Stop")) {
                            ue::Params ps(fnStop);
                            ps.Set<float>("InBlendOutTime", 0.15f);
                            ps.Set<UObject*>("Montage", static_cast<UObject*>(nullptr));
                            ue::Call(ai, fnStop, ps.data());
                        }
                        m.appliedMon.clear();
                    }
                }
            }
        }
    }

    // Destroy any peer clone mirrors no longer present in the peer's list
    for (auto it = g_peerClones.begin(); it != g_peerClones.end(); ) {
        if (!activeIds.count(it->first)) {
            net::Log("fx_grabs: peer clone #%d despawned", it->first);
            DestroyActorSafe(it->second.actor);
            it = g_peerClones.erase(it);
        } else {
            ++it;
        }
    }
}

void TickPairedLocks() {
    const double now = Mono();
    for (auto it = g_pairedLocks.begin(); it != g_pairedLocks.end(); ) {
        bool expired = (now >= it->until) || !Live(it->actor);
        if (!expired && Live(it->actor)) {
            // Release early once the montage finishes (after a 0.35s grace period)
            if (it->until - now < 3.65 && !Live(GetCurrentMontageOf(it->actor))) {
                expired = true;
            }
        }
        if (expired) {
            if (it->pausedAi && Live(it->brain)) {
                if (UFunction* fnR = ue::FindFunction(it->brain, "RestartLogic")) {
                    ue::Params pr(fnR);
                    ue::Call(it->brain, fnR, pr.data());
                }
            }
            it = g_pairedLocks.erase(it);
        } else {
            ++it;
        }
    }
}

void LockActorForPairedAnim(UObject* enemy, double durSecs) {
    if (!Live(enemy) || enemy == net::Pawn() || enemy == puppet::Actor()) return;
    const double until = Mono() + std::max(1.5, std::min(6.0, durSecs));
    for (auto& lk : g_pairedLocks) {
        if (lk.actor == enemy) {
            lk.until = std::max(lk.until, until);
            return;
        }
    }
    PairedActorLock lk;
    lk.actor = enemy;
    lk.until = until;
    // Pause local AI brain while the partner's finisher/riposte/execution plays
    UObject* ctrl = nullptr;
    if (UFunction* fnC = ue::FindFunction(enemy, "GetController")) {
        ue::Params pc(fnC);
        if (ue::Call(enemy, fnC, pc.data())) ctrl = pc.Get<UObject*>("ReturnValue");
    }
    if (Live(ctrl)) {
        UObject* brain = nullptr;
        if (ue::Get<UObject*>(ctrl, "BrainComponent", brain) && Live(brain)) {
            if (UFunction* fnS = ue::FindFunction(brain, "StopLogic")) {
                ue::Params ps(fnS);
                if (ue::Call(brain, fnS, ps.data())) {
                    lk.brain = brain;
                    lk.pausedAi = true;
                }
            }
        }
    }
    g_pairedLocks.push_back(lk);
}

void ReplayGrab(const OutGrab& g) {
    UObject* inst = ActorForKey(g.instKey);
    UObject* part = ActorForKey(g.partKey);
    if (!Live(inst) || !Live(part)) return;
    UObject* iMon = !g.instMontage.empty() ? net::Find(g.instMontage, true) : nullptr;
    UObject* pMon = !g.partMontage.empty() ? net::Find(g.partMontage, true) : nullptr;

    UObject* pup = puppet::Actor();
    UObject* enemy = (inst == pup) ? part : (part == pup ? inst : nullptr);
    UObject* enemyMon = (enemy == part) ? pMon : (enemy == inst ? iMon : nullptr);

    // Align enemy position & facing to match the finisher on the peer's screen
    if (Live(enemy) && g.hasEnemyPos) {
        SetActorLocRot(enemy, g.px, g.py, g.pz, 0.0, g.pyaw, 0.0);
    }

    // Lock the enemy so MirrorDrive / AI does not interrupt the finisher montage
    if (Live(enemy)) {
        double dur = 3.5;
        if (Live(enemyMon)) {
            if (UFunction* fnLen = ue::FindFunction(enemyMon, "GetPlayLength")) {
                ue::Params pl(fnLen);
                if (ue::Call(enemyMon, fnLen, pl.data())) {
                    float len = pl.Get<float>("ReturnValue");
                    if (len > 0.3f && len < 10.0f) dur = (double)len + 0.3;
                }
            }
        }
        LockActorForPairedAnim(enemy, dur);
    }

    ue::UClass* libCls = ue::FindClass("BPFL_Utility_C");
    UObject* libCdo = libCls ? ue::GetCDO(libCls) : nullptr;
    if (Live(libCdo)) {
        if (UFunction* fn = ue::FindFunction(libCdo, "TriggerPairedAnimation")) {
            tl_replaying = true;
            ue::Params p(fn);
            p.Set<UObject*>("Instigator", inst);
            p.Set<UObject*>("Participant", part);
            p.Set<UObject*>("InstigatorMontage", iMon);
            p.Set<UObject*>("ParticipantMontage", pMon);
            p.Set<UObject*>("__WorldContext", net::World());
            ue::Call(libCdo, fn, p.data());
            tl_replaying = false;
        }
    }

    // Because the puppet has no AnimBP/ASC, TriggerPairedAnimation can abort before
    // starting the enemy's victim montage. Guarantee the enemy plays enemyMon directly.
    if (Live(enemy) && Live(enemyMon) && GetCurrentMontageOf(enemy) != enemyMon) {
        if (UFunction* fnPlay = ue::FindFunction(enemy, "PlayAnimMontage")) {
            ue::Params pp(fnPlay);
            pp.Set<UObject*>("AnimMontage", enemyMon);
            pp.Set<float>("InPlayRate", 1.0f);
            ue::Call(enemy, fnPlay, pp.data());
        }
    }
    ++g_grabRx;
    net::Log("fx_grabs: replayed finisher/grab #%d inst=%s part=%s emon=%s",
             g.seq, g.instKey.c_str(), g.partKey.c_str(),
             Live(enemyMon) ? ue::GetName(enemyMon).c_str() : "-");
}

// ===========================================================================
// Musical Instruments & Sitting Sync (Lute, Duet, SitDown/SitLoop)
// ===========================================================================
void StopPeerLuteAudio() {
    if (Live(g_peerLuteAudioComp)) {
        if (UFunction* fnStop = ue::FindFunction(g_peerLuteAudioComp, "Stop")) {
            ue::Params p(fnStop);
            ue::Call(g_peerLuteAudioComp, fnStop, p.data());
        }
    }
    g_peerLuteAudioComp = nullptr;
    g_peerLuteAudioPath.clear();
}

void ClearPeerGroovingDetectors() {
    for (UObject* det : g_peerGroovingDetectors) {
        if (!Live(det)) continue;
        if (UFunction* fnRem = ue::FindFunction(det, "RemoveGroovingState")) {
            ue::Params p(fnRem);
            ue::Call(det, fnRem, p.data());
        }
    }
    g_peerGroovingDetectors.clear();
}

void UpdatePeerGroovingNPCs(bool playing) {
    if (!playing) {
        if (!g_peerGroovingDetectors.empty()) ClearPeerGroovingDetectors();
        return;
    }
    UObject* pup = puppet::Actor();
    if (!Live(pup)) return;
    const Vec3 ploc = net::Loc(pup);
    for (UObject* det : ue::FindAllOf("BPC_LutePlayDetector_C")) {
        if (!Live(det)) continue;
        UObject* owner = nullptr;
        ue::Get<UObject*>(det, "OwnerCharacter", owner);
        if (!Live(owner)) owner = ue::GetOuter(det);
        if (!Live(owner)) continue;
        float rad = 800.0f;
        ue::Get<float>(det, "DetectionRadius", rad);
        if (rad < 100.0f) rad = 800.0f;
        const bool inRange = coop::Dist(ploc, net::Loc(owner)) <= (double)rad;
        auto it = std::find(g_peerGroovingDetectors.begin(), g_peerGroovingDetectors.end(), det);
        if (inRange && it == g_peerGroovingDetectors.end()) {
            if (UFunction* fnAdd = ue::FindFunction(det, "AddGroovingState")) {
                ue::Params p(fnAdd);
                if (ue::Call(det, fnAdd, p.data())) {
                    g_peerGroovingDetectors.push_back(det);
                }
            }
        } else if (!inRange && it != g_peerGroovingDetectors.end()) {
            if (UFunction* fnRem = ue::FindFunction(det, "RemoveGroovingState")) {
                ue::Params p(fnRem);
                ue::Call(det, fnRem, p.data());
            }
            g_peerGroovingDetectors.erase(it);
        }
    }
}

void SampleLocalLute() {
    LuteState next{};
    if (g_useLuteOverride) {
        next = g_localLuteOverride;
    } else {
    UObject* pawn = net::Pawn();
    if (Live(pawn)) {
        for (UObject* ga : ue::FindAllOf("GA_SimpleLuteMusicHandler_C")) {
            if (!Live(ga)) continue;
            bool playing = ue::GetBool(ga, "bIsPlaying");
            uint8_t sitSt = 0;
            ue::Get<uint8_t>(ga, "SitState", sitSt);
            UObject* duetInst = nullptr;
            ue::Get<UObject*>(ga, "DuetInstigator", duetInst);
            if (!playing && sitSt == 0 && !Live(duetInst)) continue;

            next.playing = playing;
            next.sitState = (int)sitSt;
            next.duet = Live(duetInst);
            ue::Get<int32_t>(ga, "TrackIndex", next.trackIdx);
            ue::Get<double>(ga, "MusicTrackTime", next.trackTime);

            UObject* snd = nullptr;
            if (next.duet) {
                ue::Get<UObject*>(ga, "DuetTrack", snd);
                if (!Live(snd)) {
                    UObject* dc = nullptr;
                    if (ue::Get<UObject*>(ga, "DuetMusicComponent", dc) && Live(dc)) {
                        ue::Get<UObject*>(dc, "Sound", snd);
                    }
                }
            }
            if (!Live(snd)) {
                UObject* mc = nullptr;
                if (ue::Get<UObject*>(ga, "MyMusicComponent", mc) && Live(mc)) {
                    ue::Get<UObject*>(mc, "Sound", snd);
                }
            }
            if (!Live(snd)) {
                UObject* luteWpn = nullptr;
                if (ue::Get<UObject*>(ga, "LuteSidearm", luteWpn) && Live(luteWpn)) {
                    UObject* am = nullptr;
                    if (ue::Get<UObject*>(luteWpn, "Audio_Music", am) && Live(am)) {
                        ue::Get<UObject*>(am, "Sound", snd);
                    }
                }
            }
            if (Live(snd)) {
                next.soundPath = StripClassPrefix(ue::GetFullName(snd));
            }
            break;
        }
    }
    }

    if (next.playing != g_localLute.playing || next.sitState != g_localLute.sitState ||
        next.duet != g_localLute.duet || next.trackIdx != g_localLute.trackIdx ||
        next.soundPath != g_localLute.soundPath) {
        if ((g_localLute.playing || g_localLute.sitState != 0) &&
            !next.playing && next.sitState == 0) {
            g_luteClearUntil = Mono() + 2.5;
        }
        g_localLute = next;
        ++g_luteTx;
        net::Log("fx_grabs: local lute -> play=%d sit=%d duet=%d track=%d snd=%s",
                 next.playing ? 1 : 0, next.sitState, next.duet ? 1 : 0,
                 next.trackIdx, next.soundPath.c_str());
    } else {
        g_localLute.trackTime = next.trackTime;
    }
}

void ApplyPeerLute(const LuteState& ls) {
    const bool changed = (ls.playing != g_peerLute.playing || ls.sitState != g_peerLute.sitState ||
                          ls.duet != g_peerLute.duet || ls.trackIdx != g_peerLute.trackIdx ||
                          ls.soundPath != g_peerLute.soundPath);
    g_peerLute = ls;
    g_peerLuteSeenAt = Mono();

    if (ls.playing && !ls.soundPath.empty()) {
        if (g_peerLuteAudioPath != ls.soundPath || !Live(g_peerLuteAudioComp)) {
            StopPeerLuteAudio();
            UObject* snd = net::Find(ls.soundPath, true);
            UObject* attachComp = puppet::Mesh();
            if (!Live(attachComp) && Live(puppet::Actor())) {
                if (UFunction* fnR = ue::FindFunction(puppet::Actor(), "K2_GetRootComponent")) {
                    ue::Params pr(fnR);
                    if (ue::Call(puppet::Actor(), fnR, pr.data())) attachComp = pr.Get<UObject*>("ReturnValue");
                }
            }
            ue::UClass* gsCls = ue::FindClass("GameplayStatics");
            UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
            if (Live(snd) && Live(attachComp) && Live(gsCdo)) {
                if (UFunction* fnSpawn = ue::FindFunction(gsCdo, "SpawnSoundAttached")) {
                    ue::Params p(fnSpawn);
                    p.Set<UObject*>("Sound", snd);
                    p.Set<UObject*>("AttachToComponent", attachComp);
                    p.Set<uint8_t>("LocationType", 0); // KeepRelativeOffset
                    p.Set<bool>("bStopWhenAttachedToDestroyed", true);
                    p.Set<float>("VolumeMultiplier", 1.0f);
                    p.Set<float>("PitchMultiplier", 1.0f);
                    p.Set<float>("StartTime", (float)std::max(0.0, ls.trackTime));
                    p.Set<bool>("bAutoDestroy", true);
                    if (ue::Call(gsCdo, fnSpawn, p.data())) {
                        g_peerLuteAudioComp = p.Get<UObject*>("ReturnValue");
                        g_peerLuteAudioPath = ls.soundPath;
                    }
                }
            }
        }
    } else if (!ls.playing) {
        StopPeerLuteAudio();
    }

    UpdatePeerGroovingNPCs(ls.playing);

    if (changed) {
        ++g_luteRx;
        net::Log("fx_grabs: peer lute <- play=%d sit=%d duet=%d track=%d snd=%s",
                 ls.playing ? 1 : 0, ls.sitState, ls.duet ? 1 : 0,
                 ls.trackIdx, ls.soundPath.c_str());
    }
}

// ===========================================================================
// Co-op Photomode Support (prevents single-client freeze desync; supports mutual freeze)
// ===========================================================================
bool IsGamePausedNow() {
    UObject* world = net::World();
    ue::UClass* gsCls = ue::FindClass("GameplayStatics");
    UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
    if (!Live(world) || !Live(gsCdo)) return false;
    if (UFunction* fn = ue::FindFunction(gsCdo, "IsGamePaused")) {
        ue::Params p(fn);
        p.Set<UObject*>("WorldContextObject", world);
        if (ue::Call(gsCdo, fn, p.data())) return p.Get<bool>("ReturnValue");
    }
    return false;
}

void SetGamePausedSafe(bool paused) {
    UObject* world = net::World();
    ue::UClass* gsCls = ue::FindClass("GameplayStatics");
    UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
    if (!Live(world) || !Live(gsCdo)) return;
    if (UFunction* fn = ue::FindFunction(gsCdo, "SetGamePaused")) {
        ue::Params p(fn);
        p.Set<UObject*>("WorldContextObject", world);
        p.Set<bool>("bPaused", paused);
        ue::Call(gsCdo, fn, p.data());
    }
}

float GetGlobalTimeDilationNow() {
    UObject* world = net::World();
    ue::UClass* gsCls = ue::FindClass("GameplayStatics");
    UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
    if (!Live(world) || !Live(gsCdo)) return 1.0f;
    if (UFunction* fn = ue::FindFunction(gsCdo, "GetGlobalTimeDilation")) {
        ue::Params p(fn);
        p.Set<UObject*>("WorldContextObject", world);
        if (ue::Call(gsCdo, fn, p.data())) return p.Get<float>("ReturnValue");
    }
    return 1.0f;
}

void SetGlobalTimeDilationSafe(float dil) {
    UObject* world = net::World();
    ue::UClass* gsCls = ue::FindClass("GameplayStatics");
    UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
    if (!Live(world) || !Live(gsCdo)) return;
    if (UFunction* fn = ue::FindFunction(gsCdo, "SetGlobalTimeDilation")) {
        ue::Params p(fn);
        p.Set<UObject*>("WorldContextObject", world);
        p.Set<float>("TimeDilation", dil);
        ue::Call(gsCdo, fn, p.data());
    }
}

bool BothInPhotoMode() {
    return g_localPhoto.active && g_peerPhoto.active && (Mono() - g_peerPhotoSeenAt) < 3.0;
}

// HookPreSkip on BPC_PhotographyMode_C.Pause / TogglePause / SetTimeDilationPreset
bool OnPhotoModePauseOrSlomoVeto(UObject*, UFunction* fn, void*) {
    if (!state().toggles.fxGrabs || !state().peer.present) return false; // solo: untouched
    if (BothInPhotoMode()) return false; // mutual co-op photomode: allow pause/slomo!
    ++g_photoVeto;
    static double lastLog = -1e9;
    const double now = Mono();
    if (now - lastLog > 3.0) {
        lastLog = now;
        net::Log("photomode: %s suppressed in co-op (peer active; both players enter photomode to freeze together)",
                 fn ? ue::GetName(reinterpret_cast<UObject*>(fn)).c_str() : "pause");
    }
    return true; // skip original pause/slomo call
}

// HookPre on BPC_PhotographyMode_C.DeactivatePhotoMode
// Immediately clear our active flag and unpause so CameraBlendOutTime cannot be
// frozen by a mutual pause or ping-ponged back into paused=1.
void OnPhotoModeDeactivate(UObject*, UFunction*, void*) {
    g_photoDeactivatingUntil = Mono() + 2.5;
    if (IsGamePausedNow()) SetGamePausedSafe(false);
    if (std::fabs((double)GetGlobalTimeDilationNow() - 1.0) > 0.02) SetGlobalTimeDilationSafe(1.0f);
    g_coopPhotoPausedUs = false;
}

void SampleLocalPhotoMode() {
    PhotoModeState next{};
    const bool deactivating = (Mono() < g_photoDeactivatingUntil);
    if (!deactivating) {
        for (UObject* pm : ue::FindAllOf("BPC_PhotographyMode_C")) {
            if (!Live(pm)) continue;
            if (ue::GetBool(pm, "bIsPhotoModeActive")) {
                next.active = true;
                break;
            }
        }
    }
    if (next.active) {
        next.paused = IsGamePausedNow();
        next.dilation = (double)GetGlobalTimeDilationNow();
    }

    // In co-op when only WE are in photomode (peer is still playing live),
    // enforce unpaused + 1.0 time dilation so native C++ inside ActivatePhotoMode
    // can never silently freeze our client out of sync with the peer.
    if (state().peer.present) {
        if (next.active && !BothInPhotoMode()) {
            if (next.paused) {
                SetGamePausedSafe(false);
                next.paused = false;
            }
            if (std::fabs(next.dilation - 1.0) > 0.02) {
                SetGlobalTimeDilationSafe(1.0f);
                next.dilation = 1.0;
            }
        }
        // If we just exited photomode while a mutual pause was active, resume cleanly
        if (!next.active && (g_coopPhotoPausedUs || IsGamePausedNow())) {
            SetGamePausedSafe(false);
            SetGlobalTimeDilationSafe(1.0f);
            g_coopPhotoPausedUs = false;
        }
    }

    if (next.active != g_localPhoto.active || next.paused != g_localPhoto.paused ||
        std::fabs(next.dilation - g_localPhoto.dilation) > 0.02) {
        if (g_localPhoto.active && !next.active) {
            g_photoClearUntil = Mono() + 2.5;
        }
        g_localPhoto = next;
        ++g_photoTx;
        net::Log("fx_grabs: local photomode -> active=%d paused=%d dil=%.2f",
                 next.active ? 1 : 0, next.paused ? 1 : 0, next.dilation);
    }
}

void ApplyPeerPhotoMode(const PhotoModeState& pm) {
    const bool changed = (pm.active != g_peerPhoto.active || pm.paused != g_peerPhoto.paused ||
                          std::fabs(pm.dilation - g_peerPhoto.dilation) > 0.02);
    g_peerPhoto = pm;
    g_peerPhotoSeenAt = Mono();

    // Mutual co-op Photomode: Host is authoritative for pause/dilation while BOTH
    // are in Photomode (prevents symmetric ping-pong). If either exits Photomode,
    // both resume immediately.
    const bool isHost = (state().local.role == "host");
    if (g_localPhoto.active && pm.active && Mono() >= g_photoDeactivatingUntil) {
        if (!isHost) {
            if (pm.paused && !IsGamePausedNow()) {
                SetGamePausedSafe(true);
                g_coopPhotoPausedUs = true;
            } else if (!pm.paused && g_coopPhotoPausedUs) {
                SetGamePausedSafe(false);
                g_coopPhotoPausedUs = false;
            }
            const float curDil = GetGlobalTimeDilationNow();
            if (std::fabs((double)curDil - pm.dilation) > 0.05 && pm.dilation > 0.01 && pm.dilation <= 2.0) {
                SetGlobalTimeDilationSafe((float)pm.dilation);
                g_coopPhotoPausedUs = true;
            }
        }
    } else if (!pm.active) {
        // Peer exited Photomode -> resume live co-op cleanly on both sides
        if (g_coopPhotoPausedUs || (g_localPhoto.active && IsGamePausedNow())) {
            SetGamePausedSafe(false);
            SetGlobalTimeDilationSafe(1.0f);
            g_coopPhotoPausedUs = false;
        }
    }

    if (changed) {
        ++g_photoRx;
        net::Log("fx_grabs: peer photomode <- active=%d paused=%d dil=%.2f",
                 pm.active ? 1 : 0, pm.paused ? 1 : 0, pm.dilation);
    }
}

// ===========================================================================
// Sliding / Skydive / Spline-Based Scripted Movement + Full 3D Rotation Sync
// ===========================================================================
void SampleLocalScriptedMove() {
    ScriptedRotState next{};
    if (g_useSrotOverride) {
        next = g_localSrotOverride;
    } else {
    UObject* pawn = net::Pawn();
    if (Live(pawn)) {
        double pitch = 0.0, yaw = 0.0, roll = 0.0;
        GetActorRot(pawn, &pitch, &yaw, &roll);
        next.pitch = pitch;
        next.roll = roll;

        // Detect active BP_SplineBasedMovement_C on local pawn
        for (UObject* sp : ue::FindAllOf("BP_SplineBasedMovement_C")) {
            if (!Live(sp)) continue;
            UObject* cached = nullptr;
            uint8_t st = 0;
            ue::Get<UObject*>(sp, "CachedPlayer", cached);
            ue::Get<uint8_t>(sp, "State", st);
            if (cached == pawn && st != 0) {
                next.scripted = true;
                break;
            }
        }
        // Detect active BP_SkydiveHandler_C on local pawn
        if (!next.scripted) {
            for (UObject* sd : ue::FindAllOf("BP_SkydiveHandler_C")) {
                if (!Live(sd)) continue;
                UObject* owner = nullptr;
                ue::Get<UObject*>(sd, "OwnerCharacter", owner);
                if (owner == pawn) {
                    next.scripted = true;
                    break;
                }
            }
        }
        // Also mark scripted if pawn is steeply pitched/rolled (slope sliding / tumbling)
        if (!next.scripted && (std::fabs(pitch) > 10.0 || std::fabs(roll) > 10.0)) {
            next.scripted = true;
        }
    }
    }

    const bool wasActive = g_localSrot.scripted || std::fabs(g_localSrot.pitch) > 2.0 || std::fabs(g_localSrot.roll) > 2.0;
    const bool nowActive = next.scripted || std::fabs(next.pitch) > 2.0 || std::fabs(next.roll) > 2.0;
    if (wasActive && !nowActive) {
        g_srotClearUntil = Mono() + 2.0;
    }
    if (next.scripted != g_localSrot.scripted) {
        ++g_srotTx;
        net::Log("fx_grabs: local scripted/slide -> active=%d pitch=%.1f roll=%.1f",
                 next.scripted ? 1 : 0, next.pitch, next.roll);
    }
    g_localSrot = next;
}

void ReplayVfx(const OutVfx& v) {
    UObject* sys = net::Find(v.sysPath, false);
    UObject* attachActor = ActorForKey(v.attachKey);
    if (!Live(sys) || !Live(attachActor)) return;
    UObject* mesh = (attachActor == puppet::Actor()) ? puppet::Mesh() : nullptr;
    if (!Live(mesh)) ue::Get<UObject*>(attachActor, "Mesh", mesh);
    if (!Live(mesh)) return;

    ue::UClass* libCls = ue::FindClass("NiagaraFunctionLibrary");
    UObject* libCdo = libCls ? ue::GetCDO(libCls) : nullptr;
    if (!Live(libCdo)) return;
    UFunction* fn = ue::FindFunction(libCdo, "SpawnSystemAttached");
    if (!fn) return;

    tl_replaying = true;
    ue::Params p(fn);
    p.Set<UObject*>("SystemTemplate", sys);
    p.Set<UObject*>("AttachToComponent", mesh);
    if (!v.sock.empty()) p.Set<ue::FName>("AttachPointName", ue::MakeName(v.sock.c_str()));
    p.Set<uint8_t>("LocationType", 0);
    p.Set<bool>("bAutoDestroy", true);
    p.Set<bool>("bAutoActivate", true);
    ue::Call(libCdo, fn, p.data());
    tl_replaying = false;
    ++g_vfxRx;
}

void ReplayProj(const OutProj& op) {
    // Visual-only spawn of the peer's projectile with exact rotation + velocity and neutralized damage
    UObject* cls = net::Find(op.clsPath, true);
    UObject* world = net::World();
    if (!Live(cls) || !Live(world)) return;
    ue::UClass* gsCls = ue::FindClass("GameplayStatics");
    UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
    if (!Live(gsCdo)) return;
    UFunction* fnBegin = ue::FindFunction(gsCdo, "BeginDeferredActorSpawnFromClass");
    UFunction* fnFin   = ue::FindFunction(gsCdo, "FinishSpawningActor");
    if (!fnBegin || !fnFin) return;

    double pitch = op.pitch, yaw = op.yaw, roll = op.roll;
    const double spd = std::sqrt(op.vx*op.vx + op.vy*op.vy + op.vz*op.vz);
    if (std::fabs(pitch) < 0.01 && std::fabs(yaw) < 0.01 && std::fabs(roll) < 0.01 && spd > 1.0) {
        yaw = std::atan2(op.vy, op.vx) * kRad2Deg;
        pitch = std::asin(std::max(-1.0, std::min(1.0, op.vz / spd))) * kRad2Deg;
    }

    ue::FTransform xf{};
    xf.Rotation = RotToQuat(pitch, yaw, roll);
    xf.Translation = ue::FVector{op.x, op.y, op.z};
    xf.Scale3D = ue::FVector{1.0, 1.0, 1.0};

    tl_replaying = true;
    ue::Params pb(fnBegin);
    pb.Set<UObject*>("WorldContextObject", world);
    pb.Set<UObject*>("ActorClass", cls);
    pb.Set<ue::FTransform>("SpawnTransform", xf);
    pb.Set<uint8_t>("CollisionHandlingOverride", 1); // AlwaysSpawn
    pb.Set<UObject*>("Owner", puppet::Actor());
    if (!ue::Call(gsCdo, fnBegin, pb.data())) { tl_replaying = false; return; }
    UObject* spawned = pb.Get<UObject*>("ReturnValue");
    if (!Live(spawned)) { tl_replaying = false; return; }

    g_remoteProjs.insert(spawned);
    // Neutralize hit payload & friendly fire BEFORE FinishSpawningActor so it's purely visual
    ue::Set<UObject*>(spawned, "ProjectileHitPayload", static_cast<UObject*>(nullptr));
    ue::Set<UObject*>(spawned, "Projectile_HIt_Payload_Data_Modified", static_cast<UObject*>(nullptr));
    ue::SetBool(spawned, "EnableFriendlyFire", false);
    ue::SetBool(spawned, "StopTraceResultAfterParry", true);
    ue::Set<ue::FVector>(spawned, "SuggestedVelocity", ue::FVector{op.vx, op.vy, op.vz});

    ue::Params pf(fnFin);
    pf.Set<UObject*>("Actor", spawned);
    pf.Set<ue::FTransform>("SpawnTransform", xf);
    ue::Call(gsCdo, fnFin, pf.data());
    tl_replaying = false;

    // Apply exact velocity & rotation to ProjectileMovementComponent
    UObject* pmc = nullptr;
    if (ue::Get<UObject*>(spawned, "ProjectileMovement", pmc) && Live(pmc)) {
        const float fspd = (spd > 1.0) ? (float)spd : 1500.0f;
        ue::Set<float>(pmc, "InitialSpeed", fspd);
        ue::Set<float>(pmc, "MaxSpeed", std::max(fspd, 5000.0f));
        ue::Set<ue::FVector>(pmc, "Velocity", ue::FVector{op.vx, op.vy, op.vz});
        ue::SetBool(pmc, "bRotationFollowsVelocity", true);
    }
    if (UFunction* fnCol = ue::FindFunction(spawned, "SetActorEnableCollision")) {
        ue::Params pc(fnCol);
        pc.Set<bool>("bNewActorEnableCollision", false);
        ue::Call(spawned, fnCol, pc.data());
    }
    if (UFunction* fnLife = ue::FindFunction(spawned, "SetLifeSpan")) {
        ue::Params pl(fnLife);
        pl.Set<float>("InLifespan", 4.0f);
        ue::Call(spawned, fnLife, pl.data());
    }

    ++g_projRx;
    net::Log("fx_grabs: replayed proj #%d cls=%s vel=(%.0f,%.0f,%.0f) rot=(%.0f,%.0f,%.0f)",
             op.seq, ue::GetClassName(spawned).c_str(), op.vx, op.vy, op.vz, pitch, yaw, roll);
}

std::vector<std::string> SplitTuple(const std::string& s) {
    std::vector<std::string> tok;
    bool inQ = false; std::string cur;
    for (char c : s) {
        if (c == '"') { inQ = !inQ; continue; }
        if (!inQ && c == ',') { tok.push_back(cur); cur.clear(); continue; }
        if (!inQ && (c == '[' || c == ']' || c == ' ')) continue;
        cur.push_back(c);
    }
    if (!cur.empty() || (!s.empty() && s.back() == ',')) tok.push_back(cur);
    return tok;
}

}  // namespace

bool PeerTransformNonHumanoid() { return g_peerNonHumanoid; }
bool PeerTransformHideBody() { return g_peerHideBody; }
bool IsPeerCloneMirror(ue::UObject* a) {
    if (!a) return false;
    for (const auto& kv : g_peerClones) {
        if (kv.second.actor == a) return true;
    }
    return false;
}
bool PeerLutePlaying() {
    return g_peerLute.playing && (Mono() - g_peerLuteSeenAt) < 3.0;
}
bool PeerLuteSitting() {
    return (g_peerLute.sitState == 2) && (Mono() - g_peerLuteSeenAt) < 3.0;
}
bool LocalPhotoModeActive() {
    return g_localPhoto.active;
}
bool IsActorInPairedAnim(ue::UObject* a) {
    if (!a) return false;
    const double now = Mono();
    for (const auto& lk : g_pairedLocks) {
        if (lk.actor == a && now < lk.until) return true;
    }
    return false;
}
bool PeerHasFullRot(double* pitchOut, double* rollOut) {
    if ((Mono() - g_peerSrotSeenAt) > 2.5) return false;
    if (!g_peerSrot.scripted && std::fabs(g_peerSrot.pitch) <= 2.0 && std::fabs(g_peerSrot.roll) <= 2.0) {
        return false;
    }
    if (pitchOut) *pitchOut = g_peerSrot.pitch;
    if (rollOut)  *rollOut  = g_peerSrot.roll;
    return true;
}
bool PeerInScriptedMove() {
    return g_peerSrot.scripted && (Mono() - g_peerSrotSeenAt) < 2.5;
}

void Tick15() {
    if (!state().toggles.fxGrabs) return;
    ScanLocalProjectiles();
    ScanLocalClones();
    TickPairedLocks();
    SampleLocalScriptedMove();
    SampleLocalPhotoMode();

    const double now = Mono();
    while (!g_projQ.empty() && now - g_projQ.front().at > kTxRepeatSecs) g_projQ.pop_front();
    while (!g_grabQ.empty() && now - g_grabQ.front().at > kTxRepeatSecs) g_grabQ.pop_front();
    while (!g_vfxQ.empty()  && now - g_vfxQ.front().at  > kTxRepeatSecs) g_vfxQ.pop_front();

    // Stop peer lute audio/grooving if peer stops sending for > 3.5s
    if ((g_peerLute.playing || !g_peerGroovingDetectors.empty()) && (now - g_peerLuteSeenAt) > 3.5) {
        g_peerLute = LuteState{};
        StopPeerLuteAudio();
        ClearPeerGroovingDetectors();
    }

    // Expire stale peer clones if peer stopped sending "cln" for > 2.5s
    for (auto it = g_peerClones.begin(); it != g_peerClones.end(); ) {
        if ((now - it->second.lastSeen) > 2.5) {
            DestroyActorSafe(it->second.actor);
            it = g_peerClones.erase(it);
        } else {
            ++it;
        }
    }
}

void Tick2() {
    if (!state().toggles.fxGrabs) return;
    if (!g_hooksInstalled) {
        bool a = ue::HookPost("BPFL_Utility_C.TriggerPairedAnimation", &OnTriggerPairedAnimation);
        bool b = ue::HookPost("NiagaraFunctionLibrary.SpawnSystemAttached", &OnSpawnSystemAttached);
        if (a || b) {
            g_hooksInstalled = true;
            net::Log("fx_grabs: hooks installed (paired=%d niagara=%d)", a ? 1 : 0, b ? 1 : 0);
        }
    }
    if (!g_execHooksInstalled) {
        bool e1 = ue::HookPost("GA_Player_ExecutionBase_C.K2_ActivateAbilityFromEvent", &OnPlayerExecutionActivate);
        if (e1) {
            g_execHooksInstalled = true;
            net::Log("fx_grabs: execution hook installed");
        }
    }
    if (!g_photoHooksInstalled) {
        bool pm1 = ue::HookPreSkip("BPC_PhotographyMode_C.Pause", &OnPhotoModePauseOrSlomoVeto);
        bool pm2 = ue::HookPreSkip("BPC_PhotographyMode_C.TogglePause", &OnPhotoModePauseOrSlomoVeto);
        bool pm3 = ue::HookPreSkip("BPC_PhotographyMode_C.SetTimeDilationPreset", &OnPhotoModePauseOrSlomoVeto);
        bool pm4 = ue::HookPre("BPC_PhotographyMode_C.DeactivatePhotoMode", &OnPhotoModeDeactivate);
        if (pm1 || pm2 || pm3 || pm4) {
            g_photoHooksInstalled = true;
            net::Log("fx_grabs: photomode hooks installed (pause=%d toggle=%d slomo=%d deact=%d)",
                     pm1 ? 1 : 0, pm2 ? 1 : 0, pm3 ? 1 : 0, pm4 ? 1 : 0);
        }
    }
    if (!g_projHooksInstalled) {
        bool p1 = ue::HookPost("BP_ProjectileBase_C.ReceiveBeginPlay", &OnProjectileSpawnOrReInit);
        bool p2 = ue::HookPost("BP_ProjectileBase_C.ReBeginPlay", &OnProjectileSpawnOrReInit);
        bool p3 = ue::HookPost("WP_ThrowableWeaponBase_C.ReceiveBeginPlay", &OnProjectileSpawnOrReInit);
        if (p1 || p2 || p3) {
            g_projHooksInstalled = true;
            net::Log("fx_grabs: proj hooks installed (begin=%d rebegin=%d throw=%d)", p1 ? 1 : 0, p2 ? 1 : 0, p3 ? 1 : 0);
        }
    }
    SampleLocalTransform();
    SampleLocalLute();
}

void Reset() {
    g_projQ.clear(); g_grabQ.clear(); g_vfxQ.clear();
    g_seenProjs.clear();
    g_remoteProjs.clear();
    g_lastRxProjSeq = 0; g_lastRxGrabSeq = 0; g_lastRxVfxSeq = 0;

    g_localXform = XformState{};
    g_useXformOverride = false;
    g_peerXform = XformState{};
    g_peerNonHumanoid = false;
    g_peerHideBody = false;
    DestroyPeerXformStatic();

    g_localCloneIds.clear();
    g_localClones.clear();
    ClearPeerClones();

    g_pairedLocks.clear();
    g_localLute = LuteState{};
    g_peerLute = LuteState{};
    StopPeerLuteAudio();
    ClearPeerGroovingDetectors();

    g_localPhoto = PhotoModeState{};
    g_peerPhoto = PhotoModeState{};
    g_coopPhotoPausedUs = false;

    g_localSrot = ScriptedRotState{};
    g_peerSrot = ScriptedRotState{};
}

void Serialize(std::string* out) {
    if (!out || !state().toggles.fxGrabs) return;
    if (!g_projQ.empty()) {
        *out += ",\"prj\":[";
        bool first = true;
        for (const OutProj& p : g_projQ) {
            if (!first) *out += ','; first = false;
            *out += net::Fmt("[%d,\"%s\",%.0f,%.0f,%.0f,%.1f,%.1f,%.1f,%.0f,%.0f,%.0f]",
                             p.seq, EscapeStr(p.clsPath).c_str(),
                             p.x, p.y, p.z, p.pitch, p.yaw, p.roll, p.vx, p.vy, p.vz);
        }
        *out += "]";
    }
    if (!g_grabQ.empty()) {
        const OutGrab& g = g_grabQ.back();
        if (g.hasEnemyPos) {
            *out += net::Fmt(",\"grb\":[%d,\"%s\",\"%s\",\"%s\",\"%s\",%.0f,%.0f,%.0f,%.1f]",
                             g.seq, EscapeStr(g.instKey).c_str(), EscapeStr(g.partKey).c_str(),
                             EscapeStr(g.instMontage).c_str(), EscapeStr(g.partMontage).c_str(),
                             g.px, g.py, g.pz, g.pyaw);
        } else {
            *out += net::Fmt(",\"grb\":[%d,\"%s\",\"%s\",\"%s\",\"%s\"]",
                             g.seq, EscapeStr(g.instKey).c_str(), EscapeStr(g.partKey).c_str(),
                             EscapeStr(g.instMontage).c_str(), EscapeStr(g.partMontage).c_str());
        }
    }
    if (!g_vfxQ.empty()) {
        *out += ",\"vfx\":[";
        bool first = true;
        for (const OutVfx& v : g_vfxQ) {
            if (!first) *out += ','; first = false;
            *out += net::Fmt("[%d,\"%s\",\"%s\",\"%s\",%.0f,%.0f,%.0f]",
                             v.seq, EscapeStr(v.sysPath).c_str(), EscapeStr(v.attachKey).c_str(),
                             EscapeStr(v.sock).c_str(), v.x, v.y, v.z);
        }
        *out += "]";
    }
    const double now = Mono();
    if (g_localXform.active || now < g_xformClearUntil) {
        *out += net::Fmt(",\"xform\":[%d,\"%s\",\"%s\",%d]",
                         g_localXform.active ? 1 : 0,
                         EscapeStr(g_localXform.tag).c_str(),
                         EscapeStr(g_localXform.staticMesh).c_str(),
                         g_localXform.bodyHidden ? 1 : 0);
    }
    if (!g_localClones.empty() || now < g_clonesClearUntil) {
        *out += ",\"cln\":[";
        bool first = true;
        for (const CloneEntry& c : g_localClones) {
            if (!first) *out += ','; first = false;
            *out += net::Fmt("[%d,\"%s\",%.0f,%.0f,%.0f,%.1f,\"%s\",\"%s\",%.2f]",
                             c.id, EscapeStr(c.clsPath).c_str(), c.x, c.y, c.z, c.yaw,
                             EscapeStr(c.meshPath).c_str(), EscapeStr(c.monPath).c_str(), c.monPos);
        }
        *out += "]";
    }
    if (g_localLute.playing || g_localLute.sitState != 0 || now < g_luteClearUntil) {
        *out += net::Fmt(",\"lute\":[%d,%d,%d,%d,\"%s\",%.2f]",
                         g_localLute.playing ? 1 : 0, g_localLute.sitState,
                         g_localLute.duet ? 1 : 0, g_localLute.trackIdx,
                         EscapeStr(g_localLute.soundPath).c_str(), g_localLute.trackTime);
    }
    if (g_localPhoto.active || now < g_photoClearUntil) {
        *out += net::Fmt(",\"pm\":[%d,%d,%.2f]",
                         g_localPhoto.active ? 1 : 0, g_localPhoto.paused ? 1 : 0,
                         g_localPhoto.dilation);
    }
    if (g_localSrot.scripted || std::fabs(g_localSrot.pitch) > 2.0 || std::fabs(g_localSrot.roll) > 2.0 ||
        now < g_srotClearUntil) {
        *out += net::Fmt(",\"srot\":[%d,%.1f,%.1f]",
                         g_localSrot.scripted ? 1 : 0, g_localSrot.pitch, g_localSrot.roll);
    }
}

void OnPeerLine(const std::string& line) {
    if (!state().toggles.fxGrabs) return;
    std::string sec;
    if (net::Section(line, "prj", &sec) && sec.size() > 4) {
        size_t pos = 1;
        while (pos < sec.size()) {
            size_t o = sec.find('[', pos), c = sec.find(']', o == std::string::npos ? pos : o);
            if (o == std::string::npos || c == std::string::npos) break;
            auto t = SplitTuple(sec.substr(o, c - o + 1));
            if (t.size() >= 11) {
                int seq = std::atoi(t[0].c_str());
                if (seq > g_lastRxProjSeq) {
                    g_lastRxProjSeq = seq;
                    OutProj op{seq, t[1],
                               std::strtod(t[2].c_str(), nullptr), std::strtod(t[3].c_str(), nullptr), std::strtod(t[4].c_str(), nullptr),
                               std::strtod(t[5].c_str(), nullptr), std::strtod(t[6].c_str(), nullptr), std::strtod(t[7].c_str(), nullptr),
                               std::strtod(t[8].c_str(), nullptr), std::strtod(t[9].c_str(), nullptr), std::strtod(t[10].c_str(), nullptr), 0};
                    ReplayProj(op);
                }
            } else if (t.size() >= 8) {
                // Backwards-compatible 8-field format
                int seq = std::atoi(t[0].c_str());
                if (seq > g_lastRxProjSeq) {
                    g_lastRxProjSeq = seq;
                    OutProj op{seq, t[1],
                               std::strtod(t[2].c_str(), nullptr), std::strtod(t[3].c_str(), nullptr), std::strtod(t[4].c_str(), nullptr),
                               0, 0, 0,
                               std::strtod(t[5].c_str(), nullptr), std::strtod(t[6].c_str(), nullptr), std::strtod(t[7].c_str(), nullptr), 0};
                    ReplayProj(op);
                }
            }
            pos = c + 1;
        }
    }
    if (net::Section(line, "grb", &sec) && sec.size() > 4) {
        auto t = SplitTuple(sec);
        if (t.size() >= 5) {
            int seq = std::atoi(t[0].c_str());
            if (seq > g_lastRxGrabSeq) {
                g_lastRxGrabSeq = seq;
                OutGrab g;
                g.seq = seq;
                g.instKey = t[1];
                g.partKey = t[2];
                g.instMontage = t[3];
                g.partMontage = t[4];
                if (t.size() >= 9) {
                    g.hasEnemyPos = true;
                    g.px = std::strtod(t[5].c_str(), nullptr);
                    g.py = std::strtod(t[6].c_str(), nullptr);
                    g.pz = std::strtod(t[7].c_str(), nullptr);
                    g.pyaw = std::strtod(t[8].c_str(), nullptr);
                }
                ReplayGrab(g);
            }
        }
    }
    if (net::Section(line, "vfx", &sec) && sec.size() > 4) {
        size_t pos = 1;
        while (pos < sec.size()) {
            size_t o = sec.find('[', pos), c = sec.find(']', o == std::string::npos ? pos : o);
            if (o == std::string::npos || c == std::string::npos) break;
            auto t = SplitTuple(sec.substr(o, c - o + 1));
            if (t.size() >= 7) {
                int seq = std::atoi(t[0].c_str());
                if (seq > g_lastRxVfxSeq) {
                    g_lastRxVfxSeq = seq;
                    ReplayVfx(OutVfx{seq, t[1], t[2], t[3],
                                     std::strtod(t[4].c_str(), nullptr),
                                     std::strtod(t[5].c_str(), nullptr),
                                     std::strtod(t[6].c_str(), nullptr), 0});
                }
            }
            pos = c + 1;
        }
    }
    if (net::Section(line, "xform", &sec) && sec.size() >= 4) {
        auto t = SplitTuple(sec);
        if (t.size() >= 4) {
            XformState xf;
            xf.active = (std::atoi(t[0].c_str()) != 0);
            xf.tag = t[1];
            xf.staticMesh = t[2];
            xf.bodyHidden = (std::atoi(t[3].c_str()) != 0);
            ApplyPeerXform(xf);
        }
    }
    if (net::Section(line, "cln", &sec) && sec.size() >= 2) {
        std::vector<CloneEntry> parsed;
        size_t pos = 1;
        while (pos < sec.size()) {
            size_t o = sec.find('[', pos), c = sec.find(']', o == std::string::npos ? pos : o);
            if (o == std::string::npos || c == std::string::npos) break;
            auto t = SplitTuple(sec.substr(o, c - o + 1));
            if (t.size() >= 6) {
                CloneEntry ce;
                ce.id = std::atoi(t[0].c_str());
                ce.clsPath = t[1];
                ce.x = std::strtod(t[2].c_str(), nullptr);
                ce.y = std::strtod(t[3].c_str(), nullptr);
                ce.z = std::strtod(t[4].c_str(), nullptr);
                ce.yaw = std::strtod(t[5].c_str(), nullptr);
                if (t.size() >= 7) ce.meshPath = t[6];
                if (t.size() >= 8) ce.monPath = t[7];
                if (t.size() >= 9) ce.monPos = std::strtod(t[8].c_str(), nullptr);
                if (ce.id > 0) parsed.push_back(std::move(ce));
            }
            pos = c + 1;
        }
        ApplyPeerClones(parsed);
    }
    if (net::Section(line, "lute", &sec) && sec.size() >= 4) {
        auto t = SplitTuple(sec);
        if (t.size() >= 6) {
            LuteState ls;
            ls.playing = (std::atoi(t[0].c_str()) != 0);
            ls.sitState = std::atoi(t[1].c_str());
            ls.duet = (std::atoi(t[2].c_str()) != 0);
            ls.trackIdx = std::atoi(t[3].c_str());
            ls.soundPath = t[4];
            ls.trackTime = std::strtod(t[5].c_str(), nullptr);
            ApplyPeerLute(ls);
        }
    }
    if (net::Section(line, "pm", &sec) && sec.size() >= 4) {
        auto t = SplitTuple(sec);
        if (t.size() >= 3) {
            PhotoModeState pm;
            pm.active = (std::atoi(t[0].c_str()) != 0);
            pm.paused = (std::atoi(t[1].c_str()) != 0);
            pm.dilation = std::strtod(t[2].c_str(), nullptr);
            ApplyPeerPhotoMode(pm);
        }
    }
    if (net::Section(line, "srot", &sec) && sec.size() >= 4) {
        auto t = SplitTuple(sec);
        if (t.size() >= 3) {
            const bool prevS = g_peerSrot.scripted;
            g_peerSrot.scripted = (std::atoi(t[0].c_str()) != 0);
            g_peerSrot.pitch = std::strtod(t[1].c_str(), nullptr);
            g_peerSrot.roll = std::strtod(t[2].c_str(), nullptr);
            g_peerSrotSeenAt = Mono();
            if (g_peerSrot.scripted != prevS) {
                ++g_srotRx;
                net::Log("fx_grabs: peer scripted/slide <- active=%d pitch=%.1f roll=%.1f",
                         g_peerSrot.scripted ? 1 : 0, g_peerSrot.pitch, g_peerSrot.roll);
            }
        }
    }
}

bool OnCommand(const std::string& cmd, const std::string& rest) {
    if (cmd != "fxtest") return false;
    std::string sub = rest;
    while (!sub.empty() && sub.front() == ' ') sub.erase(sub.begin());
    std::string arg;
    size_t sp = sub.find(' ');
    if (sp != std::string::npos) {
        arg = sub.substr(sp + 1);
        sub = sub.substr(0, sp);
        while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
    }

    if (sub.empty() || sub == "status") {
        net::Log("fxtest: prj=%d/%d grb=%d/%d vfx=%d/%d xform=%d/%d(peerActive=%d,tag=%s,nonHum=%d,hideBody=%d,smComp=%d) cln=%d/%d(local=%d,peer=%d,animRx=%d)",
                 g_projTx, g_projRx, g_grabTx, g_grabRx, g_vfxTx, g_vfxRx,
                 g_xformTx, g_xformRx, g_peerXform.active ? 1 : 0, g_peerXform.tag.c_str(),
                 g_peerNonHumanoid ? 1 : 0, g_peerHideBody ? 1 : 0, Live(g_peerXformStaticComp) ? 1 : 0,
                 g_cloneTx, g_cloneRx, (int)g_localClones.size(), (int)g_peerClones.size(), g_cloneAnimRx);
        return true;
    }

    if (sub == "proj") {
        // Spawn a real sidearm projectile owned by local pawn and fire it forward (and test pooled ReBeginPlay if arg == "reinit")
        UObject* pawn = net::Pawn();
        UObject* world = net::World();
        if (!Live(pawn) || !Live(world)) { net::Log("fxtest: proj failed (no pawn/world)"); return true; }
        std::string clsPath = arg.empty()
            ? "/Game/Sparta/Core/Projectiles/Crossbow/BP_Projectile_Crossbow_Primary.BP_Projectile_Crossbow_Primary_C"
            : arg;
        UObject* cls = net::Find(clsPath, true);
        if (!Live(cls)) {
            clsPath = "/Game/Sparta/Core/Projectiles/NailShotgun/BP_Projectile_NailShotgun_Primary.BP_Projectile_NailShotgun_Primary_C";
            cls = net::Find(clsPath, true);
        }
        ue::UClass* gsCls = ue::FindClass("GameplayStatics");
        UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
        UFunction* fnBegin = Live(gsCdo) ? ue::FindFunction(gsCdo, "BeginDeferredActorSpawnFromClass") : nullptr;
        UFunction* fnFin   = Live(gsCdo) ? ue::FindFunction(gsCdo, "FinishSpawningActor") : nullptr;
        if (!Live(cls) || !fnBegin || !fnFin) {
            net::Log("fxtest: proj class/gs missing cls=%s", clsPath.c_str());
            return true;
        }
        Vec3 loc = net::Loc(pawn);
        double pitch = 0, yaw = 0, roll = 0;
        GetActorRot(pawn, &pitch, &yaw, &roll);
        ue::FTransform xf{};
        xf.Rotation = RotToQuat(pitch, yaw, roll);
        xf.Translation = ue::FVector{loc.X + std::cos(yaw * kDeg2Rad) * 120.0,
                                     loc.Y + std::sin(yaw * kDeg2Rad) * 120.0,
                                     loc.Z + 80.0};
        xf.Scale3D = ue::FVector{1.0, 1.0, 1.0};

        ue::Params pb(fnBegin);
        pb.Set<UObject*>("WorldContextObject", world);
        pb.Set<UObject*>("ActorClass", cls);
        pb.Set<ue::FTransform>("SpawnTransform", xf);
        pb.Set<uint8_t>("CollisionHandlingOverride", 1);
        pb.Set<UObject*>("Owner", pawn);
        if (ue::Call(gsCdo, fnBegin, pb.data())) {
            UObject* spawned = pb.Get<UObject*>("ReturnValue");
            if (Live(spawned)) {
                ue::Set<UObject*>(spawned, "Instigator", pawn);
                UObject* pmc = nullptr;
                if (ue::Get<UObject*>(spawned, "ProjectileMovement", pmc) && Live(pmc)) {
                    ue::Set<ue::FVector>(pmc, "Velocity", ue::FVector{
                        std::cos(yaw * kDeg2Rad) * 1800.0,
                        std::sin(yaw * kDeg2Rad) * 1800.0, 0.0});
                }
                ue::Params pf(fnFin);
                pf.Set<UObject*>("Actor", spawned);
                pf.Set<ue::FTransform>("SpawnTransform", xf);
                ue::Call(gsCdo, fnFin, pf.data());
                // Also explicitly test ReBeginPlay pooled reuse on the same actor after 0.1s via RecordLocalProjectile
                RecordLocalProjectile(spawned);
                net::Log("fxtest: proj spawned %s prjTx=%d", ue::GetClassName(spawned).c_str(), g_projTx);
                return true;
            }
        }
        net::Log("fxtest: proj spawn call failed");
        return true;
    }

    if (sub == "xform_sheep") {
        g_useXformOverride = true;
        g_localXformOverride = XformState{true, "State.Transformation.Sheep", "", false};
        SampleLocalTransform();
        net::Log("fxtest: xform_sheep active xformTx=%d", g_xformTx);
        return true;
    }
    if (sub == "xform_bush") {
        // Find any loaded StaticMesh in the world to use as the bush/object static mesh
        std::string smPath;
        for (UObject* sm : ue::FindAllOf("StaticMesh")) {
            if (!Live(sm)) continue;
            std::string fn = StripClassPrefix(ue::GetFullName(sm));
            if (fn.find("/Game/") == 0) { smPath = fn; break; }
        }
        g_useXformOverride = true;
        g_localXformOverride = XformState{true, "State.Transformation.Bush", smPath, true};
        SampleLocalTransform();
        net::Log("fxtest: xform_bush active sm=%s xformTx=%d", smPath.c_str(), g_xformTx);
        return true;
    }
    if (sub == "xform_off") {
        g_useXformOverride = false;
        g_localXformOverride = XformState{};
        SampleLocalTransform();
        net::Log("fxtest: xform_off xformTx=%d", g_xformTx);
        return true;
    }

    if (sub == "clone_tiel" || sub == "clone_astral" || sub == "clone_shell") {
        UObject* pawn = net::Pawn();
        UObject* world = net::World();
        if (!Live(pawn) || !Live(world)) { net::Log("fxtest: clone failed (no pawn/world)"); return true; }
        const char* clsPath = (sub == "clone_tiel")
            ? "/Game/Sparta/Core/Characters/Player/Tiel/BP_Shell_Tiel_ShadowClone.BP_Shell_Tiel_ShadowClone_C"
            : (sub == "clone_shell")
                ? "/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Summon.BP_Shell_Summon_C"
                : "/Game/Sparta/Core/Characters/Player/AstralGenessa/BP_Shell_Genessa_AstralClone.BP_Shell_Genessa_AstralClone_C";
        UObject* cls = net::Find(clsPath, true);
        if (!Live(cls)) {
            clsPath = "/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Decoy.BP_Shell_Decoy_C";
            cls = net::Find(clsPath, true);
        }
        ue::UClass* gsCls = ue::FindClass("GameplayStatics");
        UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
        UFunction* fnBegin = Live(gsCdo) ? ue::FindFunction(gsCdo, "BeginDeferredActorSpawnFromClass") : nullptr;
        UFunction* fnFin   = Live(gsCdo) ? ue::FindFunction(gsCdo, "FinishSpawningActor") : nullptr;
        if (!Live(cls) || !fnBegin || !fnFin) {
            net::Log("fxtest: clone class missing %s", clsPath);
            return true;
        }
        Vec3 loc = net::Loc(pawn);
        ue::FTransform xf{};
        xf.Rotation.W = 1.0;
        xf.Translation = ue::FVector{loc.X + 180.0, loc.Y + 60.0, loc.Z};
        xf.Scale3D = ue::FVector{1.0, 1.0, 1.0};

        ue::Params pb(fnBegin);
        pb.Set<UObject*>("WorldContextObject", world);
        pb.Set<UObject*>("ActorClass", cls);
        pb.Set<ue::FTransform>("SpawnTransform", xf);
        pb.Set<uint8_t>("CollisionHandlingOverride", 1);
        pb.Set<UObject*>("Owner", pawn);
        if (ue::Call(gsCdo, fnBegin, pb.data())) {
            UObject* spawned = pb.Get<UObject*>("ReturnValue");
            if (Live(spawned)) {
                ue::Set<UObject*>(spawned, "OwnerCharacter", pawn);
                ue::Set<UObject*>(spawned, "Instigator", pawn);
                ue::Params pf(fnFin);
                pf.Set<UObject*>("Actor", spawned);
                pf.Set<ue::FTransform>("SpawnTransform", xf);
                ue::Call(gsCdo, fnFin, pf.data());
                DisableActorCollisionAndAI(spawned);
                if (sub == "clone_shell") {
                    if (UFunction* fnInit = ue::FindFunction(spawned, "InitDefaultMesh")) {
                        ue::Params pi(fnInit);
                        ue::Call(spawned, fnInit, pi.data());
                    }
                }
                g_testClones.push_back(spawned);
                ScanLocalClones();
                net::Log("fxtest: %s spawned %s localClones=%d",
                         sub.c_str(), ue::GetClassName(spawned).c_str(), (int)g_localClones.size());
                return true;
            }
        }
        net::Log("fxtest: %s spawn failed", sub.c_str());
        return true;
    }

    if (sub == "clone_anim") {
        if (g_testClones.empty() || !Live(g_testClones[0])) {
            net::Log("fxtest: clone_anim failed (no active test clone)");
            return true;
        }
        UObject* c = g_testClones[0];
        UObject* mesh = nullptr;
        if (!ue::Get<UObject*>(c, "Mesh", mesh) || !Live(mesh)) {
            net::Log("fxtest: clone_anim failed (no Mesh)");
            return true;
        }
        UObject* ai = nullptr;
        if (UFunction* fnAi = ue::FindFunction(mesh, "GetAnimInstance")) {
            ue::Params pa(fnAi);
            if (ue::Call(mesh, fnAi, pa.data())) ai = pa.Get<UObject*>("ReturnValue");
        }
        if (!Live(ai)) {
            UObject* pawn = net::Pawn();
            UObject* pm = nullptr;
            if (Live(pawn) && ue::Get<UObject*>(pawn, "Mesh", pm) && Live(pm)) {
                // Ensure clone mesh has a SkeletalMeshAsset first (required before AnimInstance can initialize)
                UObject* curSk = nullptr;
                if (UFunction* fnGetSk = ue::FindFunction(mesh, "GetSkeletalMeshAsset")) {
                    ue::Params pg(fnGetSk);
                    if (ue::Call(mesh, fnGetSk, pg.data())) curSk = pg.Get<UObject*>("ReturnValue");
                }
                if (!Live(curSk)) {
                    UObject* pawnSk = nullptr;
                    if (UFunction* fnGetSk = ue::FindFunction(pm, "GetSkeletalMeshAsset")) {
                        ue::Params pg(fnGetSk);
                        if (ue::Call(pm, fnGetSk, pg.data())) pawnSk = pg.Get<UObject*>("ReturnValue");
                    }
                    if (Live(pawnSk)) {
                        if (UFunction* fnSetSk = ue::FindFunction(mesh, "SetSkeletalMeshAsset")) {
                            ue::Params ps(fnSetSk);
                            ps.Set<UObject*>("NewMesh", pawnSk);
                            ue::Call(mesh, fnSetSk, ps.data());
                        }
                    }
                }
                UObject* animCls = nullptr;
                if (ue::Get<UObject*>(pm, "AnimClass", animCls) && Live(animCls)) {
                    if (UFunction* fnMode = ue::FindFunction(mesh, "SetAnimationMode")) {
                        ue::Params pm2(fnMode);
                        pm2.Set<uint8_t>("InAnimationMode", 0); // UseAnimationBlueprint
                        ue::Call(mesh, fnMode, pm2.data());
                    }
                    if (UFunction* fnSetAi = ue::FindFunction(mesh, "SetAnimInstanceClass")) {
                        ue::Params ps(fnSetAi);
                        ps.Set<UObject*>("NewClass", animCls);
                        ue::Call(mesh, fnSetAi, ps.data());
                    }
                    if (UFunction* fnAi = ue::FindFunction(mesh, "GetAnimInstance")) {
                        ue::Params pa(fnAi);
                        if (ue::Call(mesh, fnAi, pa.data())) ai = pa.Get<UObject*>("ReturnValue");
                    }
                }
            }
        }
        // Pick a real loaded humanoid AnimMontage (e.g. attack/dodge/shell montage)
        UObject* mon = nullptr;
        for (UObject* m : ue::FindAllOf("AnimMontage")) {
            if (!Live(m)) continue;
            std::string fn = ue::GetFullName(m);
            if (fn.find("/Game/Sparta/") != std::string::npos &&
                (ContainsCI(fn, "Attack") || ContainsCI(fn, "Dodge") || ContainsCI(fn, "Shell"))) {
                mon = m;
                break;
            }
        }
        if (!Live(ai) || !Live(mon)) {
            net::Log("fxtest: clone_anim failed (ai=%d mon=%d)", Live(ai) ? 1 : 0, Live(mon) ? 1 : 0);
            return true;
        }
        bool ok = false;
        if (UFunction* fnPlay = ue::FindFunction(ai, "Montage_Play")) {
            ue::Params pp(fnPlay);
            pp.Set<UObject*>("MontageToPlay", mon);
            pp.Set<float>("InPlayRate", 1.0f);
            pp.Set<uint8_t>("ReturnValueType", 0);
            pp.Set<float>("InTimeToStartMontageAt", 0.0f);
            pp.Set<bool>("bStopAllMontages", true);
            ok = ue::Call(ai, fnPlay, pp.data());
        }
        ScanLocalClones();
        net::Log("fxtest: clone_anim ok=%d mon=%s", ok ? 1 : 0, ue::GetName(mon).c_str());
        return true;
    }

    if (sub == "clone_clear") {
        for (UObject* c : g_testClones) DestroyActorSafe(c);
        g_testClones.clear();
        ScanLocalClones();
        net::Log("fxtest: clone_clear localClones=%d", (int)g_localClones.size());
        return true;
    }

    if (sub == "lute_stand" || sub == "lute_sit" || sub == "lute_off") {
        if (sub == "lute_off") {
            g_useLuteOverride = false;
            g_localLuteOverride = LuteState{};
        } else {
            std::string sndPath;
            for (UObject* s : ue::FindAllOf("SoundCue")) {
                if (!Live(s)) continue;
                std::string fn = StripClassPrefix(ue::GetFullName(s));
                if (ContainsCI(fn, "Lute") || ContainsCI(fn, "Guitar")) { sndPath = fn; break; }
            }
            if (sndPath.empty()) {
                for (UObject* s : ue::FindAllOf("SoundCue")) {
                    if (!Live(s)) continue;
                    std::string fn = StripClassPrefix(ue::GetFullName(s));
                    if (fn.find("/Game/") == 0) { sndPath = fn; break; }
                }
            }
            g_useLuteOverride = true;
            g_localLuteOverride = LuteState{true, (sub == "lute_sit") ? 2 : 0, (sub == "lute_stand"), 0, sndPath, 0.0};
        }
        SampleLocalLute();
        net::Log("fxtest: %s luteTx=%d play=%d sit=%d snd=%s",
                 sub.c_str(), g_luteTx, g_localLute.playing ? 1 : 0, g_localLute.sitState, g_localLute.soundPath.c_str());
        return true;
    }

    if (sub == "photo_on" || sub == "photo_off" || sub == "photo_pause" || sub == "photo_slomo") {
        UObject* pm = nullptr;
        for (UObject* o : ue::FindAllOf("BPC_PhotographyMode_C")) {
            if (Live(o)) { pm = o; break; }
        }
        if (!Live(pm)) {
            net::Log("fxtest: %s failed (no BPC_PhotographyMode_C)", sub.c_str());
            return true;
        }
        bool ok = false;
        if (sub == "photo_on") {
            if (UFunction* f = ue::FindFunction(pm, "ActivatePhotoMode")) { ue::Params p(f); ok = ue::Call(pm, f, p.data()); }
        } else if (sub == "photo_off") {
            if (UFunction* f = ue::FindFunction(pm, "DeactivatePhotoMode")) { ue::Params p(f); ok = ue::Call(pm, f, p.data()); }
        } else if (sub == "photo_pause") {
            if (UFunction* f = ue::FindFunction(pm, "Pause")) { ue::Params p(f); ok = ue::Call(pm, f, p.data()); }
        } else if (sub == "photo_slomo") {
            if (UFunction* f = ue::FindFunction(pm, "SetTimeDilationPreset")) {
                ue::Params p(f);
                p.Set<bool>("Next", true);
                ok = ue::Call(pm, f, p.data());
            }
        }
        SampleLocalPhotoMode();
        net::Log("fxtest: %s ok=%d active=%d paused=%d dil=%.2f veto=%d pmTx=%d pmRx=%d",
                 sub.c_str(), ok ? 1 : 0, g_localPhoto.active ? 1 : 0,
                 IsGamePausedNow() ? 1 : 0, (double)GetGlobalTimeDilationNow(),
                 g_photoVeto, g_photoTx, g_photoRx);
        return true;
    }

    if (sub == "grab_real") {
        UObject* pawn = net::Pawn();
        UObject* world = net::World();
        if (!Live(pawn) || !Live(world)) { net::Log("fxtest: grab_real failed (no pawn/world)"); return true; }
        UObject* enemy = nullptr;
        double bestD = 1e9;
        const Vec3 ploc = net::Loc(pawn);
        for (const NamedActor& na : state().local.tickTails) {
            if (!Live(na.obj) || na.obj == pawn || na.obj == puppet::Actor()) continue;
            std::string cn = ue::GetClassName(na.obj);
            if (ContainsCI(cn, "Player") || ContainsCI(cn, "NPC")) continue;
            const double d = coop::Dist(ploc, net::Loc(na.obj));
            if (d < bestD) { bestD = d; enemy = na.obj; }
        }
        if (!Live(enemy)) {
            for (const NamedActor& na : state().local.tickTails) {
                if (!Live(na.obj) || na.obj == pawn || na.obj == puppet::Actor()) continue;
                const double d = coop::Dist(ploc, net::Loc(na.obj));
                if (d < bestD) { bestD = d; enemy = na.obj; }
            }
        }
        UObject* iMon = nullptr;
        UObject* pMon = nullptr;
        for (UObject* m : ue::FindAllOf("AnimMontage")) {
            if (!Live(m)) continue;
            std::string fn = ue::GetFullName(m);
            if (!Live(iMon) && ContainsCI(fn, "Riposte") && (ContainsCI(fn, "Player") || ContainsCI(fn, "Shell"))) iMon = m;
            if (!Live(pMon) && ContainsCI(fn, "Riposte") && ContainsCI(fn, "Enemy")) pMon = m;
            if (Live(iMon) && Live(pMon)) break;
        }
        if (!Live(pMon)) {
            for (UObject* m : ue::FindAllOf("AnimMontage")) {
                if (!Live(m)) continue;
                std::string fn = ue::GetFullName(m);
                if (fn.find("/Game/Sparta/Characters/Enemies/") != std::string::npos) { pMon = m; break; }
            }
        }
        ue::UClass* libCls = ue::FindClass("BPFL_Utility_C");
        UObject* libCdo = libCls ? ue::GetCDO(libCls) : nullptr;
        UFunction* fn = Live(libCdo) ? ue::FindFunction(libCdo, "TriggerPairedAnimation") : nullptr;
        if (!Live(enemy) || !fn) {
            net::Log("fxtest: grab_real failed (enemy=%d fn=%d)", Live(enemy) ? 1 : 0, fn ? 1 : 0);
            return true;
        }
        ue::Params p(fn);
        p.Set<UObject*>("Instigator", pawn);
        p.Set<UObject*>("Participant", enemy);
        p.Set<UObject*>("InstigatorMontage", iMon);
        p.Set<UObject*>("ParticipantMontage", pMon);
        p.Set<UObject*>("__WorldContext", world);
        ue::Call(libCdo, fn, p.data());
        net::Log("fxtest: grab_real enemy=%s dist=%.0f iMon=%s pMon=%s grbTx=%d",
                 KeyForActor(enemy).c_str(), bestD,
                 Live(iMon) ? ue::GetName(iMon).c_str() : "-",
                 Live(pMon) ? ue::GetName(pMon).c_str() : "-", g_grabTx);
        return true;
    }

    if (sub == "slide_on" || sub == "slide_off") {
        if (sub == "slide_off") {
            g_useSrotOverride = false;
            g_localSrotOverride = ScriptedRotState{};
        } else {
            g_useSrotOverride = true;
            g_localSrotOverride = ScriptedRotState{true, -28.5, 14.0};
        }
        SampleLocalScriptedMove();
        net::Log("fxtest: %s srotTx=%d scripted=%d pitch=%.1f roll=%.1f",
                 sub.c_str(), g_srotTx, g_localSrot.scripted ? 1 : 0, g_localSrot.pitch, g_localSrot.roll);
        return true;
    }

    if (sub == "probe_new") {
        double pp = 0, py = 0, pr = 0;
        if (Live(puppet::Actor())) GetActorRot(puppet::Actor(), &pp, &py, &pr);
        net::Log("fxtest: probe_new lute=%d/%d(peerPlay=%d,peerSit=%d,audio=%d,grooves=%d) pm=%d/%d(locAct=%d,peerAct=%d,paused=%d,dil=%.2f,veto=%d) grb=%d/%d(locks=%d) srot=%d/%d(peerScr=%d,pupRot=%.1f/%.1f/%.1f) pupAnim=%s",
                 g_luteTx, g_luteRx, PeerLutePlaying() ? 1 : 0, PeerLuteSitting() ? 1 : 0,
                 Live(g_peerLuteAudioComp) ? 1 : 0, (int)g_peerGroovingDetectors.size(),
                 g_photoTx, g_photoRx, g_localPhoto.active ? 1 : 0, g_peerPhoto.active ? 1 : 0,
                 IsGamePausedNow() ? 1 : 0, (double)GetGlobalTimeDilationNow(), g_photoVeto,
                 g_grabTx, g_grabRx, (int)g_pairedLocks.size(),
                 g_srotTx, g_srotRx, PeerInScriptedMove() ? 1 : 0, pp, py, pr,
                 state().puppet.animState.c_str());
        return true;
    }

    net::Log("fxtest: unknown sub '%s' (status|proj|xform_sheep|xform_bush|xform_off|clone_tiel|clone_astral|clone_clear|lute_stand|lute_sit|lute_off|photo_on|photo_pause|photo_slomo|photo_off|grab_real|slide_on|slide_off|probe_new)", sub.c_str());
    return true;
}

std::string Status() {
    return net::Fmt("fx=%s prj:%d/%d grb:%d/%d vfx:%d/%d xform:%d/%d cln:%d/%d lute:%d/%d pm:%d/%d(v:%d) srot:%d/%d\n",
                    state().toggles.fxGrabs ? "on" : "off",
                    g_projTx, g_projRx, g_grabTx, g_grabRx, g_vfxTx, g_vfxRx,
                    g_xformTx, g_xformRx, g_cloneTx, g_cloneRx,
                    g_luteTx, g_luteRx, g_photoTx, g_photoRx, g_photoVeto,
                    g_srotTx, g_srotRx);
}

}  // namespace fx_grabs
}  // namespace coop
