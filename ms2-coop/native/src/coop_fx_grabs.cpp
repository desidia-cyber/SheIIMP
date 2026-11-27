// coop_fx_grabs.cpp -- [R10-FX] Projectiles, Paired Animations (Grabs/Ripostes),
// and Niagara VFX sync.
//
// Wire sections:
//   ,"prj":[[seq,"clsPath",x,y,z,pitch,yaw,roll,vx,vy,vz],...]
//   ,"grb":[seq,"instKey","partKey","instMontage","partMontage"]
//   ,"vfx":[[seq,"sysPath","attachKey","sock",x,y,z],...]
// Kill switch: FX_GRABS_OFF.txt (Toggles::fxGrabs, default ON).

#include "ms2_ue.h"
#include "coop_state.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

namespace coop {
namespace fx_grabs {

namespace {

using ue::UObject;
using ue::UFunction;

constexpr double kTxRepeatSecs = 2.0;

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

std::deque<OutProj> g_projQ;
std::deque<OutGrab> g_grabQ;
std::deque<OutVfx>  g_vfxQ;

std::unordered_set<UObject*> g_seenProjs;
int g_nextProjSeq = 1, g_lastRxProjSeq = 0;
int g_nextGrabSeq = 1, g_lastRxGrabSeq = 0;
int g_nextVfxSeq  = 1, g_lastRxVfxSeq  = 0;

bool g_hooksInstalled = false;
thread_local bool tl_replaying = false;

int g_projTx = 0, g_projRx = 0;
int g_grabTx = 0, g_grabRx = 0;
int g_vfxTx  = 0, g_vfxRx  = 0;

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
    g.instMontage = Live(iMon) ? ue::GetFullName(iMon) : "";
    g.partMontage = Live(pMon) ? ue::GetFullName(pMon) : "";
    // Strip class prefix "AnimMontage /Game/..." -> "/Game/..."
    auto stripPrefix = [](std::string& s) {
        size_t sp = s.find(' ');
        if (sp != std::string::npos) s.erase(0, sp + 1);
    };
    stripPrefix(g.instMontage);
    stripPrefix(g.partMontage);
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

    std::string spath = ue::GetFullName(sys);
    size_t sp = spath.find(' ');
    if (sp != std::string::npos) spath.erase(0, sp + 1);
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

    // Prune gone pointers
    for (auto it = g_seenProjs.begin(); it != g_seenProjs.end(); ) {
        if (!Live(*it)) it = g_seenProjs.erase(it);
        else ++it;
    }

    for (UObject* prj : ue::FindAllOf("SpartaProjectile")) {
        if (!Live(prj) || g_seenProjs.count(prj)) continue;
        g_seenProjs.insert(prj);

        // Check if owned by local player
        bool ours = false;
        UObject* inst = nullptr;
        if (ue::Get<UObject*>(prj, "Instigator", inst) && inst == pawn) ours = true;
        UObject* own = nullptr;
        if (!ours && ue::Get<UObject*>(prj, "Owner", own) && own == pawn) ours = true;
        if (!ours) continue;

        ue::UClass* cls = ue::GetClass(prj);
        if (!cls) continue;
        std::string cpath = ue::GetFullName(reinterpret_cast<UObject*>(cls));
        size_t sp = cpath.find(' ');
        if (sp != std::string::npos) cpath.erase(0, sp + 1);
        if (cpath.size() < 8) continue;

        OutProj op;
        op.seq = g_nextProjSeq++;
        op.clsPath = cpath;
        const Vec3 loc = net::Loc(prj);
        op.x = loc.X; op.y = loc.Y; op.z = loc.Z;
        if (UFunction* fnVel = ue::FindFunction(prj, "GetVelocity")) {
            ue::Params p(fnVel);
            if (ue::Call(prj, fnVel, p.data())) {
                ue::FVector v = p.Get<ue::FVector>("ReturnValue");
                op.vx = v.X; op.vy = v.Y; op.vz = v.Z;
            }
        }
        op.at = Mono();
        g_projQ.push_back(std::move(op));
        while (g_projQ.size() > 8) g_projQ.pop_front();
        ++g_projTx;
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
    // Visual-only spawn of the peer's projectile via GameplayStatics::BeginDeferredActorSpawnFromClass
    UObject* cls = net::Find(op.clsPath, false);
    UObject* world = net::World();
    if (!Live(cls) || !Live(world)) return;
    ue::UClass* gsCls = ue::FindClass("GameplayStatics");
    UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
    if (!Live(gsCdo)) return;
    UFunction* fnBegin = ue::FindFunction(gsCdo, "BeginDeferredActorSpawnFromClass");
    UFunction* fnFin   = ue::FindFunction(gsCdo, "FinishSpawningActor");
    if (!fnBegin || !fnFin) return;

    ue::FTransform xf{};
    xf.Rotation = ue::FQuat{0.0, 0.0, 0.0, 1.0};
    xf.Translation = ue::FVector{op.x, op.y, op.z};
    xf.Scale3D = ue::FVector{1.0, 1.0, 1.0};

    ue::Params pb(fnBegin);
    pb.Set<UObject*>("WorldContextObject", world);
    pb.Set<UObject*>("ActorClass", cls);
    pb.Set<ue::FTransform>("SpawnTransform", xf);
    pb.Set<uint8_t>("CollisionHandlingOverride", 1); // AlwaysSpawn
    pb.Set<UObject*>("Owner", puppet::Actor());
    if (!ue::Call(gsCdo, fnBegin, pb.data())) return;
    UObject* spawned = pb.Get<UObject*>("ReturnValue");
    if (!Live(spawned)) return;

    g_seenProjs.insert(spawned); // do not re-broadcast
    ue::Params pf(fnFin);
    pf.Set<UObject*>("Actor", spawned);
    pf.Set<ue::FTransform>("SpawnTransform", xf);
    ue::Call(gsCdo, fnFin, pf.data());
    ++g_projRx;
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
    if (!cur.empty()) tok.push_back(cur);
    return tok;
}

}  // namespace

void Tick15() {
    if (!state().toggles.fxGrabs) return;
    ScanLocalProjectiles();
    const double now = Mono();
    while (!g_projQ.empty() && now - g_projQ.front().at > kTxRepeatSecs) g_projQ.pop_front();
    while (!g_grabQ.empty() && now - g_grabQ.front().at > kTxRepeatSecs) g_grabQ.pop_front();
    while (!g_vfxQ.empty()  && now - g_vfxQ.front().at  > kTxRepeatSecs) g_vfxQ.pop_front();
}

void Tick2() {
    if (!state().toggles.fxGrabs || g_hooksInstalled) return;
    bool a = ue::HookPost("BPFL_Utility_C.TriggerPairedAnimation", &OnTriggerPairedAnimation);
    bool b = ue::HookPost("NiagaraFunctionLibrary.SpawnSystemAttached", &OnSpawnSystemAttached);
    if (a || b) {
        g_hooksInstalled = true;
        net::Log("fx_grabs: hooks installed (paired=%d niagara=%d)", a ? 1 : 0, b ? 1 : 0);
    }
}

void Reset() {
    g_projQ.clear(); g_grabQ.clear(); g_vfxQ.clear();
    g_seenProjs.clear();
    g_lastRxProjSeq = 0; g_lastRxGrabSeq = 0; g_lastRxVfxSeq = 0;
}

void Serialize(std::string* out) {
    if (!out || !state().toggles.fxGrabs) return;
    if (!g_projQ.empty()) {
        *out += ",\"prj\":[";
        bool first = true;
        for (const OutProj& p : g_projQ) {
            if (!first) *out += ','; first = false;
            *out += net::Fmt("[%d,\"%s\",%.0f,%.0f,%.0f,%.0f,%.0f,%.0f]",
                             p.seq, EscapeStr(p.clsPath).c_str(), p.x, p.y, p.z, p.vx, p.vy, p.vz);
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
            if (t.size() >= 8) {
                int seq = std::atoi(t[0].c_str());
                if (seq > g_lastRxProjSeq) {
                    g_lastRxProjSeq = seq;
                    OutProj op{seq, t[1], std::strtod(t[2].c_str(),nullptr), std::strtod(t[3].c_str(),nullptr),
                               std::strtod(t[4].c_str(),nullptr), 0,0,0,
                               std::strtod(t[5].c_str(),nullptr), std::strtod(t[6].c_str(),nullptr),
                               std::strtod(t[7].c_str(),nullptr), 0};
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
                                     std::strtod(t[4].c_str(),nullptr),
                                     std::strtod(t[5].c_str(),nullptr),
                                     std::strtod(t[6].c_str(),nullptr), 0});
                }
            }
            pos = c + 1;
        }
    }
}

std::string Status() {
    return net::Fmt("fx=%s prj:%d/%d grb:%d/%d vfx:%d/%d\n",
                    state().toggles.fxGrabs ? "on" : "off",
                    g_projTx, g_projRx, g_grabTx, g_grabRx, g_vfxTx, g_vfxRx);
}

}  // namespace fx_grabs
}  // namespace coop
