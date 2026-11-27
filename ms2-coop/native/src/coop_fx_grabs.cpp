// coop_fx_grabs.cpp -- [R10-FX] Ranged Projectiles, Animal/Object Transformations,
// Player Clones & Summons, Paired Animations (Grabs/Ripostes), and Niagara VFX sync.
//
// Wire sections:
//   ,"prj":[[seq,"clsPath",x,y,z,pitch,yaw,roll,vx,vy,vz],...]
//   ,"grb":[seq,"instKey","partKey","instMontage","partMontage"]
//   ,"vfx":[[seq,"sysPath","attachKey","sock",x,y,z],...]
//   ,"xform":[active,"tag","staticMeshPath",bodyHidden]
//   ,"cln":[[id,"clsPath",x,y,z,yaw,"meshPath"],...]
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
    double at = 0;
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
};

struct PeerCloneMirror {
    int id = 0;
    UObject* actor = nullptr;
    std::string clsPath;
    std::string appliedMesh;
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
int g_cloneTx = 0, g_cloneRx = 0;

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

    UObject* pawn = net::Pawn();
    if (inst != pawn && part != pawn) return;

    OutGrab g;
    g.seq = g_nextGrabSeq++;
    g.instKey = KeyForActor(inst);
    g.partKey = KeyForActor(part);
    g.instMontage = Live(iMon) ? StripClassPrefix(ue::GetFullName(iMon)) : "";
    g.partMontage = Live(pMon) ? StripClassPrefix(ue::GetFullName(pMon)) : "";
    g.at = Mono();
    g_grabQ.push_back(std::move(g));
    while (g_grabQ.size() > 4) g_grabQ.pop_front();
    ++g_grabTx;
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

    // Only replicate VFX attached to local pawn (e.g. weapon buffs, shell VFX, healing)
    UObject* outer = ue::GetOuter(comp);
    if (outer != net::Pawn()) return;

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
            if (!ce.meshPath.empty() && m.appliedMesh != ce.meshPath) {
                UObject* mesh = nullptr;
                if (ue::Get<UObject*>(m.actor, "Mesh", mesh) && Live(mesh)) {
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

void ReplayGrab(const OutGrab& g) {
    UObject* inst = ActorForKey(g.instKey);
    UObject* part = ActorForKey(g.partKey);
    if (!Live(inst) || !Live(part)) return;
    UObject* iMon = !g.instMontage.empty() ? net::Find(g.instMontage, false) : nullptr;
    UObject* pMon = !g.partMontage.empty() ? net::Find(g.partMontage, false) : nullptr;

    ue::UClass* libCls = ue::FindClass("BPFL_Utility_C");
    UObject* libCdo = libCls ? ue::GetCDO(libCls) : nullptr;
    if (!Live(libCdo)) return;
    UFunction* fn = ue::FindFunction(libCdo, "TriggerPairedAnimation");
    if (!fn) return;

    tl_replaying = true;
    ue::Params p(fn);
    p.Set<UObject*>("Instigator", inst);
    p.Set<UObject*>("Participant", part);
    p.Set<UObject*>("InstigatorMontage", iMon);
    p.Set<UObject*>("ParticipantMontage", pMon);
    p.Set<UObject*>("__WorldContext", net::World());
    ue::Call(libCdo, fn, p.data());
    tl_replaying = false;
    ++g_grabRx;
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

void Tick15() {
    if (!state().toggles.fxGrabs) return;
    ScanLocalProjectiles();
    ScanLocalClones();

    const double now = Mono();
    while (!g_projQ.empty() && now - g_projQ.front().at > kTxRepeatSecs) g_projQ.pop_front();
    while (!g_grabQ.empty() && now - g_grabQ.front().at > kTxRepeatSecs) g_grabQ.pop_front();
    while (!g_vfxQ.empty()  && now - g_vfxQ.front().at  > kTxRepeatSecs) g_vfxQ.pop_front();

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
        *out += net::Fmt(",\"grb\":[%d,\"%s\",\"%s\",\"%s\",\"%s\"]",
                         g.seq, EscapeStr(g.instKey).c_str(), EscapeStr(g.partKey).c_str(),
                         EscapeStr(g.instMontage).c_str(), EscapeStr(g.partMontage).c_str());
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
            *out += net::Fmt("[%d,\"%s\",%.0f,%.0f,%.0f,%.1f,\"%s\"]",
                             c.id, EscapeStr(c.clsPath).c_str(), c.x, c.y, c.z, c.yaw,
                             EscapeStr(c.meshPath).c_str());
        }
        *out += "]";
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
                ReplayGrab(OutGrab{seq, t[1], t[2], t[3], t[4], 0});
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
                if (ce.id > 0) parsed.push_back(std::move(ce));
            }
            pos = c + 1;
        }
        ApplyPeerClones(parsed);
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
        net::Log("fxtest: prj=%d/%d grb=%d/%d vfx=%d/%d xform=%d/%d(peerActive=%d,tag=%s,nonHum=%d,hideBody=%d,smComp=%d) cln=%d/%d(local=%d,peer=%d)",
                 g_projTx, g_projRx, g_grabTx, g_grabRx, g_vfxTx, g_vfxRx,
                 g_xformTx, g_xformRx, g_peerXform.active ? 1 : 0, g_peerXform.tag.c_str(),
                 g_peerNonHumanoid ? 1 : 0, g_peerHideBody ? 1 : 0, Live(g_peerXformStaticComp) ? 1 : 0,
                 g_cloneTx, g_cloneRx, (int)g_localClones.size(), (int)g_peerClones.size());
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

    if (sub == "clone_clear") {
        for (UObject* c : g_testClones) DestroyActorSafe(c);
        g_testClones.clear();
        ScanLocalClones();
        net::Log("fxtest: clone_clear localClones=%d", (int)g_localClones.size());
        return true;
    }

    net::Log("fxtest: unknown sub '%s' (status|proj|xform_sheep|xform_bush|xform_off|clone_tiel|clone_astral|clone_clear)", sub.c_str());
    return true;
}

std::string Status() {
    return net::Fmt("fx=%s prj:%d/%d grb:%d/%d vfx:%d/%d xform:%d/%d cln:%d/%d\n",
                    state().toggles.fxGrabs ? "on" : "off",
                    g_projTx, g_projRx, g_grabTx, g_grabRx, g_vfxTx, g_vfxRx,
                    g_xformTx, g_xformRx, g_cloneTx, g_cloneRx);
}

}  // namespace fx_grabs
}  // namespace coop
