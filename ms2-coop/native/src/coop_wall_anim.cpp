// coop_wall_anim.cpp -- round 8: going THROUGH a broken wall (traversal / squeeze).
//
// ---------------------------------------------------------------------------
// WHAT THE GAME DOES (research\raw\CXXHeaderDump, measured in REPORT.md)
// ---------------------------------------------------------------------------
// A destructible wall is `BP_BreakableWall_C` (BP_BreakableWall.hpp): `WallIsBroken`
// plus a `SqueezeThrough: TArray<TSoftObjectPtr<ABP_Interaction_Traversal_C>>` list
// and `LockTraversals`/`UnlockTraversals`.  Breaking it unlocks the traversal actors
// that sit in the gap, and going through the gap is the game's own squeeze:
//
//   BP_Interaction_Traversal_SqueezeThrough_C   (an ABP_Interaction_Traversal_C)
//     TraversalAbility -> GA_Traversal_SqueezeThrough_C
//     GA_Traversal_SqueezeThrough_C.SqueezeMontage =
//       /Game/Sparta/Characters/Shells/_Shared/Animation/Traversal/SqueezeThrough/
//       A_Shared_Traversal_SqueezeThrough_Montage.A_Shared_Traversal_SqueezeThrough_Montage
//
// i.e. the acting client plays ONE anim montage (with warp targets WT_Entrance /
// WT_Exit at the gap) and its capsule is carried through the gap for that montage's
// duration.  That montage is what has to appear on the other player's puppet.
//
// ---------------------------------------------------------------------------
// WHY THE GENERIC MONTAGE LANE IS NOT ENOUGH
// ---------------------------------------------------------------------------
// coop_local's `ownMontage` already publishes "the montage under the play head"
// (mseq/mpos/mrate/mid) and coop_puppet replays it on the puppet, so a traversal
// montage *should* arrive.  Two things an ability-driven traversal needs and a
// combat swing does not:
//
//  * HOLD THE POSE.  When the segment asset is not loaded on the observer (a
//    traversal sequence is only referenced by its montage, and the observer never
//    plays that montage), coop_puppet's playMontage returns false and
//    updatePuppetAnimation falls through to LOCOMOTION: the puppet walks/slides
//    through the wall in its idle or walk clip.  A traversal must hold the last
//    traversal pose instead of switching to a locomotion loop.
//  * FOLLOW THE GAP.  The move is guided (the capsule is warped through the gap,
//    the nominal velocity does not carry it), so the drive is allowed one straight
//    line across the gap and must not take its 400 uu "snap" in the middle of it.
//
// The wire key is ONE new top-level key, appended last, small and seq-numbered so a
// lost, repeated or reordered packet is harmless:
//
//   ,"wa":["<segment asset tail>",<rate>,<duration>,<seq>]
//
// "wa" also tells the observer that a traversal is happening at all -- the verdict
// WALLANIM reads the lines this file logs on both sides.
//
// Kill switch: WALLANIM_OFF.txt (Toggles::wallAnim): nothing is scanned, no "wa" is
// written or read, and the puppet behaves exactly like round 7.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "coop_state.h"
#include "ms2_ue.h"

namespace coop {
namespace wall {

// ===========================================================================
// constants
// ===========================================================================

namespace {

// The game's own squeeze assets / classes.
const char* const kSqueezeMontage =
    "/Game/Sparta/Characters/Shells/_Shared/Animation/Traversal/SqueezeThrough/"
    "A_Shared_Traversal_SqueezeThrough_Montage.A_Shared_Traversal_SqueezeThrough_Montage";
const char* const kSqueezeAbility = "GA_Traversal_SqueezeThrough_C";
const char* const kTraversalClass = "BP_Interaction_Traversal_C";
const char* const kBreakableWallClass = "BP_BreakableWall_C";

// The traversal montages all live under .../Animation/Traversal/; the squeeze is
// the one a broken wall opens.
const char* const kTraversalToken = "/Traversal/";
const char* const kSqueezeToken = "SqueezeThrough";

// How long a traversal keeps being published after the montage leaves the play
// head (the capture drops a frame before the capsule stops moving), and how long
// the observer keeps treating the peer as "in a traversal".
constexpr double kHoldAfter = 1.5;
constexpr double kPeerStale = 2.0;
// A montage length we could not read: the squeeze runs ~2.5 s.
constexpr double kDefaultLen = 2.5;

inline State& St() { return coop::state(); }
inline const Toggles& Tg() { return coop::toggles(); }
inline bool On() { return Tg().wallAnim; }

double SteadyNow() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// Last path component (the part a log line can afford).
std::string TailOf(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    if (dot != std::string::npos && dot + 1 < path.size()) return path.substr(dot + 1);
    return path;
}

// ---------------------------------------------------------------------------
// reflection-built calls (file-wide rule: never pass a parameter the UFunction does
// not declare, and take a scalar's width from the property)
// ---------------------------------------------------------------------------
bool HasParam(ue::UFunction* f, const char* name) {
    return f && ue::FindProperty(reinterpret_cast<ue::UStruct*>(f), name) != nullptr;
}

int32_t ParamSize(ue::UFunction* f, const char* name) {
    return f ? ue::PropSizeStruct(reinterpret_cast<ue::UStruct*>(f), name) : -1;
}

void SetObj(ue::Params& p, ue::UFunction* f, const char* name, ue::UObject* o) {
    if (ParamSize(f, name) != 8) return;
    p.Set<ue::UObject*>(name, o);
}

void SetNum(ue::Params& p, ue::UFunction* f, const char* name, double v) {
    const int32_t sz = ParamSize(f, name);
    if (sz == 8) {
        p.Set<double>(name, v);
    } else if (sz == 4) {
        p.Set<float>(name, static_cast<float>(v));
    }
}

ue::UObject* PropObj(ue::UObject* o, const char* name) {
    ue::UObject* v = nullptr;
    if (ue::Get(o, name, v)) return v;
    return nullptr;
}

bool ActorLoc(ue::UObject* o, Vec3* out) {
    if (!out) return false;
    *out = Vec3();
    ue::UFunction* f = ue::FindFunction(o, "K2_GetActorLocation");
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(o, f, p.data())) return false;
    *out = ToVec3(p.Get<ue::FVector>("ReturnValue"));
    return true;
}

// PlayAnimMontage(Montage, InPlayRate, StartSectionName): ACharacter's own entry,
// i.e. the call an ability task ends at (UAbilityTask_PlayMontageAndWait ->
// ACharacter::PlayAnimMontage).  Returns the montage length, 0 on failure.
double PlayMontageOn(ue::UObject* pawn, ue::UObject* montage, double rate) {
    if (!net::Valid(pawn) || !net::Valid(montage)) return 0.0;
    ue::UFunction* f = ue::FindFunction(pawn, "PlayAnimMontage");
    if (!f) return 0.0;
    ue::Params p(f);
    SetObj(p, f, "Montage", montage);
    SetNum(p, f, "InPlayRate", rate);
    if (HasParam(f, "StartSectionName")) p.Set<ue::FName>("StartSectionName", ue::FName());
    if (!ue::Call(pawn, f, p.data())) return 0.0;
    return static_cast<double>(p.Get<float>("ReturnValue"));
}

bool StopMontageOn(ue::UObject* pawn, ue::UObject* montage) {
    if (!net::Valid(pawn)) return false;
    ue::UFunction* f = ue::FindFunction(pawn, "StopAnimMontage");
    if (!f) return false;
    ue::Params p(f);
    SetObj(p, f, "AnimMontage", montage);
    return ue::Call(pawn, f, p.data());
}

// Play length of a montage asset (UAnimSequenceBase::GetPlayLength).  A pure read;
// 0 when the function or the asset is unavailable.
double MontageLength(ue::UObject* montage) {
    if (!net::Valid(montage)) return 0.0;
    ue::UFunction* f = ue::FindFunction(montage, "GetPlayLength");
    if (!f) return 0.0;
    ue::Params p(f);
    if (!ue::Call(montage, f, p.data())) return 0.0;
    return static_cast<double>(p.Get<float>("ReturnValue"));
}

// The skeleton an animation asset / anim instance is bound to.  A montage whose
// skeleton is not the mesh's anim instance skeleton is refused by
// UAnimInstance::Montage_Play with a return value of 0 and no log of ours
// (rt 8: exactly the "it plays nothing, silently" case), so it is worth printing.
ue::UObject* SkeletonOf(ue::UObject* o) {
    if (!net::Valid(o)) return nullptr;
    ue::UFunction* f = ue::FindFunction(o, "GetSkeleton");
    if (!f) {
        ue::UFunction* g = ue::FindFunction(o, "GetSkeletalMeshAsset");
        if (!g) return nullptr;
        ue::Params p(g);
        if (!ue::Call(o, g, p.data())) return nullptr;
        ue::UObject* mesh = p.Get<ue::UObject*>("ReturnValue");
        return SkeletonOf(mesh);
    }
    ue::Params p(f);
    if (!ue::Call(o, f, p.data())) return nullptr;
    return p.Get<ue::UObject*>("ReturnValue");
}

// UAnimInstance::Montage_Play(MontageToPlay, InPlayRate) -- the raw entry, used
// only by the probe to tell "the call was refused" from "the capture missed it".
double MontagePlayDirect(ue::UObject* mesh, ue::UObject* montage, double rate) {
    if (!net::Valid(mesh) || !net::Valid(montage)) return -1.0;
    ue::UFunction* gi = ue::FindFunction(mesh, "GetAnimInstance");
    if (!gi) return -1.0;
    ue::Params q(gi);
    if (!ue::Call(mesh, gi, q.data())) return -1.0;
    ue::UObject* inst = q.Get<ue::UObject*>("ReturnValue");
    if (!net::Valid(inst)) return -2.0;
    ue::UFunction* f = ue::FindFunction(inst, "Montage_Play");
    if (!f) return -3.0;
    ue::Params p(f);
    SetObj(p, f, "MontageToPlay", montage);
    SetNum(p, f, "InPlayRate", rate);
    if (!ue::Call(inst, f, p.data())) return -4.0;
    return static_cast<double>(p.Get<float>("ReturnValue"));
}

// Is this segment the traversal / squeeze one?  The capture resolves a montage to
// the AnimSequence of its first slot track, so the test is on the asset path.
bool IsTraversalAsset(const std::string& path) {
    if (path.empty()) return false;
    return path.find(kSqueezeToken) != std::string::npos ||
           path.find("Traversal/SqueezeThrough") != std::string::npos ||
           path.find("Traversal/VaultOver") != std::string::npos;
}

// The montage an ability class plays: its first readable montage property.  The
// squeeze ability calls it SqueezeMontage, the NoWeaponStow variant Montage.
void StartTrace(double secs);   // the 10 Hz trace, defined with the lab verbs below
ue::UObject* AbilityMontage(ue::UClass* abilityClass) {
    if (!abilityClass) return nullptr;
    ue::UObject* cdo = ue::GetCDO(abilityClass);
    if (!net::Valid(cdo)) return nullptr;
    for (const char* prop : {"Montage", "SqueezeMontage"}) {
        ue::UObject* m = PropObj(cdo, prop);
        if (net::Valid(m)) return m;
    }
    return nullptr;
}

// Everything the "why does the montage not play" question needs, in one line.
void LogAnimBinding(const char* tag, ue::UObject* mesh, ue::UObject* montage) {
    ue::UObject* inst = nullptr;
    if (net::Valid(mesh)) {
        ue::UFunction* gi = ue::FindFunction(mesh, "GetAnimInstance");
        if (gi) {
            ue::Params q(gi);
            if (ue::Call(mesh, gi, q.data())) inst = q.Get<ue::UObject*>("ReturnValue");
        }
    }
    ue::UObject* skelInst = SkeletonOf(inst);
    ue::UObject* skelAnim = SkeletonOf(montage);
    net::Log("%s mesh=%s inst=%s skel(inst)=%s montage=%s skel(montage)=%s len=%.2f same=%d", tag,
             ue::GetName(mesh).c_str(), inst ? ue::GetClassName(inst).c_str() : "-",
             skelInst ? ue::GetName(skelInst).c_str() : "-",
             montage ? TailOf(net::AssetPath(montage)).c_str() : "-",
             skelAnim ? ue::GetName(skelAnim).c_str() : "-", MontageLength(montage),
             (skelInst && skelAnim && skelInst == skelAnim) ? 1 : 0);
}

// The montage with a raw UAnimInstance::Montage_Play, then whatever the pawn's own
// PlayAnimMontage does with it: the two answers tell whether the engine refused the
// montage (skeleton/slot) or the capture simply did not see it.
void TryMontage(const char* how, ue::UObject* montage) {
    ue::UObject* pawn = net::Pawn();
    ue::UObject* mesh = PropObj(pawn, "Mesh");
    LogAnimBinding(how, mesh, montage);
    const double direct = MontagePlayDirect(mesh, montage, 1.0);
    net::Log("%s Montage_Play(inst) -> %.2f", how, direct);
    const double len = PlayMontageOn(pawn, montage, 1.0);
    net::Log("%s PlayAnimMontage(pawn) -> %.2f", how, len);
    StartTrace(6.0);
}

// The nearest live BP_Interaction_Traversal_C of this world (any variant: squeeze,
// squeeze-no-weapon-stow, vault).
ue::UObject* NearestTraversal(double* dist) {
    if (dist) *dist = 1.0e30;
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn)) return nullptr;
    Vec3 pl;
    if (!ActorLoc(pawn, &pl)) return nullptr;
    ue::UObject* best = nullptr;
    double bestD = 1.0e30;
    for (ue::UObject* o : ue::FindAllOf(kTraversalClass)) {
        if (!net::Valid(o)) continue;
        Vec3 l;
        if (!ActorLoc(o, &l)) continue;
        const double d = coop::Dist(pl, l);
        if (d < bestD) {
            bestD = d;
            best = o;
        }
    }
    if (dist) *dist = bestD;
    return best;
}

// ===========================================================================
// state
// ===========================================================================

// Our own traversal, published as "wa".
struct Own {
    bool active = false;
    int seq = 0;
    int lastId = -1;          // MontageSig.id of the traversal we are in
    std::string path;         // the segment asset the capture resolved
    double rate = 1.0;
    double dur = 0.0;         // montage length, 0 when unreadable
    double heldUntil = -1.0e9; // monotone seconds
    Vec3 from;
};
Own g_own;

// The peer's traversal, applied on the puppet.
struct Peer {
    bool active = false;
    int seq = -1;             // highest seq seen (repeats are harmless)
    std::string path;
    double rate = 1.0;
    double dur = 0.0;
    double seenAt = -1.0e9;   // monotone seconds of the last "wa"
};
Peer g_peer;

int g_ownStarts = 0;      // traversals this client published
int g_peerStarts = 0;     // traversals the peer published
int g_puppetHolds = 0;    // frames the puppet was held in the traversal pose
int g_puppetPoses = 0;    // traversals the puppet actually played

}  // namespace

// ===========================================================================
// the local side: notice our own traversal and publish it
// ===========================================================================

void Tick2() {
    if (!On()) return;
    const double now = SteadyNow();
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn)) {
        g_own.active = false;
        return;
    }
    MontageSig m;
    const bool has = local::CaptureMontage(&m);
    const bool traversal = has && IsTraversalAsset(m.path);
    if (traversal) {
        if (!g_own.active || m.id != g_own.lastId) {
            ++g_own.seq;
            g_own.lastId = m.id;
            g_own.dur = MontageLength(ue::FindObject(m.path.c_str()));
            if (g_own.dur <= 0.01) g_own.dur = kDefaultLen;
            ActorLoc(pawn, &g_own.from);
            ++g_ownStarts;
            net::Log("wall: traversal start #%d seg=%s rate=%.2f dur=%.2f at=(%.0f %.0f %.0f)",
                     g_own.seq, TailOf(m.path).c_str(), m.rate, g_own.dur, g_own.from.X,
                     g_own.from.Y, g_own.from.Z);
        }
        g_own.active = true;
        g_own.path = m.path;
        g_own.rate = m.rate;
        g_own.heldUntil = now + kHoldAfter;
    } else if (g_own.active && now > g_own.heldUntil) {
        g_own.active = false;
        net::Log("wall: traversal end #%d", g_own.seq);
    }
}

// ,"wa":["<tail>",<rate>,<dur>,<seq>] -- appended LAST by coop_net's Publish.
void Serialize(std::string* out) {
    if (!out || !On() || !g_own.active) return;
    net::AppendFmt(out, ",\"wa\":[\"%s\",%.2f,%.2f,%d]", TailOf(g_own.path).c_str(), g_own.rate,
                   g_own.dur, g_own.seq);
}

// ===========================================================================
// the peer side
// ===========================================================================

// Read the flat array: ["<tail>",<rate>,<dur>,<seq>]
void ReadPeer(const std::string& line) {
    std::string sec;
    if (!net::Section(line, "wa", &sec)) return;
    const std::size_t q = sec.find('"');
    if (q == std::string::npos) return;
    const std::size_t q2 = sec.find('"', q + 1);
    if (q2 == std::string::npos) return;
    const std::string part = sec.substr(q + 1, q2 - q - 1);
    std::size_t i = q2 + 1;

    auto nextNum = [&sec, &i](double* out) -> bool {
        while (i < sec.size() && (sec[i] == ',' || sec[i] == ' ')) ++i;
        if (i >= sec.size()) return false;
        char* end = nullptr;
        const double v = std::strtod(sec.c_str() + i, &end);
        if (end == sec.c_str() + i) return false;
        i = static_cast<std::size_t>(end - sec.c_str());
        if (out) *out = v;
        return true;
    };
    double rate = 1.0, dur = 0.0, seq = -1.0;
    nextNum(&rate);
    nextNum(&dur);
    nextNum(&seq);
    if (part.empty()) return;
    const int iseq = static_cast<int>(seq + 0.5);
    if (iseq < g_peer.seq) return;   // an older traversal: harmless repeat
    const bool fresh = (iseq != g_peer.seq);
    g_peer.seq = iseq;
    g_peer.path = part;
    g_peer.rate = rate > 0.01 ? rate : 1.0;
    g_peer.dur = dur > 0.01 ? dur : kDefaultLen;
    g_peer.seenAt = SteadyNow();
    g_peer.active = true;
    if (fresh) {
        ++g_peerStarts;
        net::Log("wall: peer traversal #%d seg=%s rate=%.2f dur=%.2f", iseq, part.c_str(), rate, dur);
    }
}

void OnPeerLine(const std::string& line) {
    if (!On()) return;
    ReadPeer(line);
}

// The observer side of the verdict: while the peer is squeezing, the puppet must be
// playing the traversal segment (NOT a locomotion loop) -- log the first frames of
// each traversal so run-two can assert it.
void Tick30(double) {
    if (!On()) return;
    const bool trav = PeerTraversal();
    if (trav) {
        const std::string& anim = St().puppet.animState;
        // "m:<id>:<path>" is the generic replayer, "w:<path>" the traversal hold
        // (coop_puppet's [R8-WALL] block); a locomotion state ("idle"/"walk"...) is
        // exactly what must NOT be on the body here.
        const bool pose = (anim.find("m:") == 0 || anim.find("w:") == 0) &&
                          (IsTraversalAsset(St().peer.montage.path) ||
                           IsTraversalAsset(St().puppet.animState));
        if (pose) ++g_puppetHolds;
        static int loggedSeq = -1;
        if (pose && loggedSeq != g_peer.seq) {
            loggedSeq = g_peer.seq;
            ++g_puppetPoses;
            net::Log("wall: puppet traversal #%d pose=%s gap=%d", g_peer.seq,
                     St().puppet.animState.c_str(), g_puppetHolds > 0 ? 1 : 0);
        }
    } else if (g_peer.active) {
        g_peer.active = false;
    }
}

bool PeerTraversal() {
    if (!On() || !g_peer.active) return false;
    if (SteadyNow() - g_peer.seenAt > kPeerStale) return false;
    return true;
}

const std::string& PeerAsset() { return g_peer.path; }

bool OwnTraversal() { return On() && g_own.active; }

int PeerSeq() { return g_peer.seq; }

std::string Status() {
    return net::Fmt("wall=%s own=%d/%s peer=%d/%s poses=%d holds=%d\n", On() ? "on" : "off",
                    g_ownStarts, g_own.active ? "in" : "-", g_peerStarts,
                    PeerTraversal() ? "in" : "-", g_puppetPoses, g_puppetHolds);
}

void Reset() {
    g_own = Own();
    g_peer = Peer();
}

// ===========================================================================
// lab verbs (`cmd.txt`): wallscan / wallplay / wallprobe
// ===========================================================================

namespace {

struct Trace {
    bool active = false;
    bool registered = false;
    double t0 = 0.0;
    double dur = 6.0;
    double nextAt = 0.0;
    double lastT = 0.0;
    Vec3 lastPos;
    bool hasLast = false;
    int samples = 0;
};
Trace g_trace;

void TraceTick(float) {
    if (!g_trace.active) return;
    const double now = SteadyNow();
    if (now - g_trace.t0 > g_trace.dur) {
        g_trace.active = false;
        net::Log("wallprobe: done (%d samples)", g_trace.samples);
        return;
    }
    if (now < g_trace.nextAt) return;
    g_trace.nextAt = now + 0.1;

    // what the local montage capture sees (the thing the wire carries)
    MontageSig m;
    const bool has = local::CaptureMontage(&m);

    // where our own pawn is, and how fast it moves between samples
    ue::UObject* pawn = net::Pawn();
    Vec3 pos;
    double speed = 0.0;
    if (ActorLoc(pawn, &pos)) {
        if (g_trace.hasLast && g_trace.lastT > 0.0) {
            const double dt = now - g_trace.lastT;
            if (dt > 0.0005) speed = coop::Dist(pos, g_trace.lastPos) / dt;
        }
        g_trace.lastPos = pos;
        g_trace.lastT = now;
        g_trace.hasLast = true;
    }
    ++g_trace.samples;
    net::Log("wallprobe: t=%.2f cap=%d pos=%.3f rate=%.2f id=%d seg=%s own=(%.0f %.0f %.0f) "
             "spd=%.0f pupAnim=%s/%s peerM=%s",
             now - g_trace.t0, has ? 1 : 0, m.pos, m.rate, m.id, TailOf(m.path).c_str(), pos.X,
             pos.Y, pos.Z, speed, St().puppet.animState.c_str(), St().puppet.animKind.c_str(),
             TailOf(St().peer.montage.path).c_str());
}

void EnsureTraceTick() {
    if (g_trace.registered) return;
    g_trace.registered = true;
    ue::OnTick(&TraceTick);
}

void StartTrace(double secs) {
    g_trace.t0 = SteadyNow();
    g_trace.dur = secs;
    g_trace.nextAt = 0.0;
    g_trace.hasLast = false;
    g_trace.samples = 0;
    g_trace.active = true;
}

}  // namespace

// ===========================================================================
// [R8-WALL-REAL] the gap, the staging and the game's own activation chain
//
// Round 8 first measured only `Montage_Play` on the local anim instance, which is
// NOT the traversal (the main agent rejected that as proof).  The real chain is the
// interaction's own: BP_Interaction_Traversal_C carries the two warp targets
// (WT_Entrance / WT_Exit), `AddWarpReferences(Character)` puts them on the player's
// motion-warping component, `GrantTraversalAbility()` gives the ability, and the
// interact input lets it run.  The steps below are tried in that order and each one
// is VERIFIED by whether the montage started (the capture) and where the pawn
// ended up, so the log says which step is the real entry on this build.
// ===========================================================================

namespace {

// A component's world location (K2_GetComponentLocation), or false.
bool CompLoc(ue::UObject* comp, Vec3* out) {
    if (out) *out = Vec3();
    if (!net::Valid(comp)) return false;
    ue::UFunction* f = ue::FindFunction(comp, "K2_GetComponentLocation");
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(comp, f, p.data())) return false;
    if (out) *out = ToVec3(p.Get<ue::FVector>("ReturnValue"));
    return true;
}

Vec3 Sub(const Vec3& a, const Vec3& b) {
    Vec3 v;
    v.X = a.X - b.X;
    v.Y = a.Y - b.Y;
    v.Z = a.Z - b.Z;
    return v;
}
Vec3 Mul(const Vec3& a, double k) {
    Vec3 v;
    v.X = a.X * k;
    v.Y = a.Y * k;
    v.Z = a.Z * k;
    return v;
}
Vec3 Add(const Vec3& a, const Vec3& b) {
    Vec3 v;
    v.X = a.X + b.X;
    v.Y = a.Y + b.Y;
    v.Z = a.Z + b.Z;
    return v;
}
Vec3 Up(double dz) {
    Vec3 v;
    v.Z = dz;
    return v;
}
double Len(const Vec3& a) { return std::sqrt(a.X * a.X + a.Y * a.Y + a.Z * a.Z); }
Vec3 Norm(const Vec3& a) {
    const double l = Len(a);
    if (l < 1.0e-6) return Vec3();
    return Mul(a, 1.0 / l);
}

struct Gap {
    ue::UObject* inter = nullptr;
    ue::UObject* abil = nullptr;      // TraversalAbility (the class the game grants)
    Vec3 ent;
    Vec3 exi;
    Vec3 center;
    Vec3 fwd;                          // entrance -> exit
    Vec3 right;                        // perpendicular, for the observer's stand
    bool ok = false;
};

// The last successfully read gap.  WORLD PARTITION unloads the interaction actor when
// the local client is far away, so a later `wallstage actor` (after the harness has
// travelled the guest 90k uu) would find nothing -- the geometry it already knows is
// still the right place to stand, and once the area streams in the next call reads the
// live actor again.
Gap g_gapCache;
bool g_gapCached = false;

// The squeeze interaction's own warp targets define the gap geometry.
bool ReadGap(Gap* g) {
    if (!g) return false;
    *g = Gap();
    ue::UObject* best = nullptr;
    double bestD = 1.0e30;
    ue::UObject* pawn = net::Pawn();
    Vec3 pl;
    const bool havePawn = ActorLoc(pawn, &pl);
    for (ue::UObject* o : ue::FindAllOf(kTraversalClass)) {
        if (!net::Valid(o)) continue;
        const std::string cls = ue::GetClassName(o);
        const bool squeeze = cls.find("SqueezeThrough") != std::string::npos;
        Vec3 ent, exi;
        if (!CompLoc(PropObj(o, "WT_Entrance"), &ent)) continue;
        if (!CompLoc(PropObj(o, "WT_Exit"), &exi)) continue;
        Vec3 l;
        ActorLoc(o, &l);
        const double d = havePawn ? coop::Dist(pl, l) : 0.0;
        // a squeeze beats anything else; within that, the closest one
        const double rank = d - (squeeze ? 1.0e7 : 0.0);
        if (rank < bestD) {
            bestD = rank;
            best = o;
            g->ent = ent;
            g->exi = exi;
        }
    }
    if (!best) {
        if (g_gapCached) {
            *g = g_gapCache;
            // NEVER hand out a cached actor pointer: the actor was unloaded, so it may
            // be freed.  Only the geometry is remembered; the callers that need the
            // live object (AddWarpReferences / GrantTraversalAbility) then see none.
            g->inter = nullptr;
            g->abil = nullptr;
            net::Log("wall: the traversal actor is not streamed in here, using the gap "
                     "geometry remembered from (%.0f %.0f %.0f)",
                     g->ent.X, g->ent.Y, g->ent.Z);
            return g->ok;
        }
        return false;
    }
    g->inter = best;
    g->abil = PropObj(best, "TraversalAbility");
    g->center = Mul(Add(g->ent, g->exi), 0.5);
    g->fwd = Norm(Sub(g->exi, g->ent));
    g->right.X = -g->fwd.Y;
    g->right.Y = g->fwd.X;
    g->right.Z = 0.0;
    g->ok = Len(Sub(g->exi, g->ent)) > 1.0;
    if (g->ok) {
        g_gapCache = *g;
        g_gapCached = true;
    }
    return g->ok;
}

// APlayerController::SetControlRotation + the pawn's own yaw: the third-person
// camera follows the control rotation, so this is what "face the gap" means.
bool LookAt(ue::UObject* pawn, const Vec3& target) {
    if (!net::Valid(pawn)) return false;
    Vec3 from;
    if (!ActorLoc(pawn, &from)) return false;
    const Vec3 d = Sub(target, from);
    const double flat = std::sqrt(d.X * d.X + d.Y * d.Y);
    ue::FRotator rot;
    rot.Yaw = std::atan2(d.Y, d.X) * 57.29577951308232;
    rot.Pitch = -std::atan2(d.Z, flat > 1.0 ? flat : 1.0) * 57.29577951308232;
    rot.Roll = 0.0;
    bool yawOk = false;
    if (ue::UFunction* f = ue::FindFunction(pawn, "SetActorRotation")) {
        ue::Params p(f);
        p.Set<ue::FRotator>("NewRotation", rot);
        p.Set<bool>("bTeleportPhysics", true);
        yawOk = ue::Call(pawn, f, p.data());
    }
    bool camOk = false;
    ue::UObject* pc = net::PC();
    if (net::Valid(pc)) {
        if (ue::UFunction* f = ue::FindFunction(pc, "SetControlRotation")) {
            ue::Params p(f);
            p.Set<ue::FRotator>("NewRotation", rot);
            camOk = ue::Call(pc, f, p.data());
        }
    }
    net::Log("wallcam: look at (%.0f %.0f %.0f) from (%.0f %.0f %.0f) yaw=%.1f pitch=%.1f "
             "actor=%d cam=%d",
             target.X, target.Y, target.Z, from.X, from.Y, from.Z, rot.Yaw, rot.Pitch,
             yawOk ? 1 : 0, camOk ? 1 : 0);
    return camOk || yawOk;
}

// Teleport our own pawn (K2_SetActorLocation), the same primitive the `tp` verb uses.
bool PlacePawn(ue::UObject* pawn, const Vec3& to) {
    if (!net::Valid(pawn)) return false;
    ue::UFunction* f = ue::FindFunction(pawn, "K2_SetActorLocation");
    if (!f) return false;
    ue::FVector v;
    v.X = to.X;
    v.Y = to.Y;
    v.Z = to.Z;
    ue::Params p(f);
    p.Set<ue::FVector>("NewLocation", v);
    p.Set<bool>("bSweep", false);
    p.Set<bool>("bTeleport", true);
    return ue::Call(pawn, f, p.data());
}

// The ASC: GetAbilitySystemComponent() first (IAbilitySystemInterface), then the
// property.
ue::UObject* AscOf(ue::UObject* pawn) {
    if (!net::Valid(pawn)) return nullptr;
    if (ue::UFunction* f = ue::FindFunction(pawn, "GetAbilitySystemComponent")) {
        ue::Params p(f);
        if (ue::Call(pawn, f, p.data())) {
            ue::UObject* asc = p.Get<ue::UObject*>("ReturnValue");
            if (net::Valid(asc)) return asc;
        }
    }
    ue::UObject* asc = PropObj(pawn, "AbilitySystemComponent");
    if (net::Valid(asc)) return asc;
    return nullptr;
}

bool CallNoArgs(ue::UObject* o, const char* fn) {
    ue::UFunction* f = ue::FindFunction(o, fn);
    if (!f) return false;
    ue::Params p(f);
    return ue::Call(o, f, p.data());
}

// The interact entry the game itself uses when the player presses the key: the
// interaction's own grant/focus/start, then (last) an explicit
// TryActivateAbilityByClass on the ASC.  Every step is verified by the montage
// capture, so the log names the step that really runs the traversal.
int RealTraversal(const Gap& g, bool verbose) {
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn) || !g.ok) return 0;
    Vec3 before;
    ActorLoc(pawn, &before);
    net::Log("wallreal: gap ent=(%.0f %.0f %.0f) exi=(%.0f %.0f %.0f) len=%.0f pawn before="
             "(%.0f %.0f %.0f)",
             g.ent.X, g.ent.Y, g.ent.Z, g.exi.X, g.exi.Y, g.exi.Z, Len(Sub(g.exi, g.ent)),
             before.X, before.Y, before.Z);

    MontageSig m;
    auto started = [&m]() {
        m = MontageSig();
        return local::CaptureMontage(&m) && IsTraversalAsset(m.path);
    };

    // 1. the interaction's warp targets on OUR pawn (the montage warps to them)
    if (ue::UFunction* f = ue::FindFunction(g.inter, "AddWarpReferences")) {
        ue::Params p(f);
        if (HasParam(f, "Character")) p.Set<ue::UObject*>("Character", pawn);
        const bool ok = ue::Call(g.inter, f, p.data());
        if (verbose) net::Log("wallreal: AddWarpReferences -> %d", ok ? 1 : 0);
    } else {
        net::Log("wallreal: the interaction has no AddWarpReferences");
    }

    // 2. the game's own grant + focus + interact steps
    const char* steps[] = {"GrantTraversalAbility", "OnGainActiveFocus", "OnInteractStart",
                           "Event_Trigger", "OnInteractComplete", "InitTraversalInteraction"};
    for (const char* step : steps) {
        if (started()) break;
        const bool ok = CallNoArgs(g.inter, step);
        const bool now = started();
        if (verbose) net::Log("wallreal: %s -> call=%d montage=%d", step, ok ? 1 : 0, now ? 1 : 0);
        if (now) break;
    }

    // 3. the ability on the ASC (what an ability task actually drives)
    if (!started()) {
        ue::UObject* asc = AscOf(pawn);
        net::Log("wallreal: asc=%s", asc ? ue::GetClassName(asc).c_str() : "-");
        if (asc && g.abil) {
            if (ue::UFunction* f = ue::FindFunction(asc, "TryActivateAbilityByClass")) {
                ue::Params p(f);
                if (HasParam(f, "AbilityToActivate"))
                    p.Set<ue::UObject*>("AbilityToActivate", g.abil);
                if (HasParam(f, "AbilityClass")) p.Set<ue::UObject*>("AbilityClass", g.abil);
                if (HasParam(f, "bAllowRemoteActivation"))
                    p.Set<bool>("bAllowRemoteActivation", true);
                const bool ok = ue::Call(asc, f, p.data());
                net::Log("wallreal: TryActivateAbilityByClass(%s) -> %d montage=%d",
                         ue::GetName(g.abil).c_str(), ok ? 1 : 0, started() ? 1 : 0);
            } else {
                net::Log("wallreal: the ASC has no TryActivateAbilityByClass");
            }
        }
    }

    if (!started()) {
        net::Log("wallreal: NOTHING started the traversal (no montage under the play head)");
        return 0;
    }

    // 4. run the trace so the capsule's real path through the gap is on the log
    StartTrace(6.0);
    Vec3 after;
    ActorLoc(pawn, &after);
    net::Log("wallreal: traversal RUNNING (%s) pos now (%.0f %.0f %.0f)", m.path.c_str(), after.X,
             after.Y, after.Z);
    return 1;
}

// wallstage observer|actor -- put the two clients at the gap:
//   observer: 5.5 m to the SIDE of the gap, looking at its centre (the gap then sits
//             in the middle of the screen and the squeeze is seen from outside)
//   actor:    2.2 m before the entrance, facing along entrance->exit
void Stage(const std::string& which) {
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn)) {
        net::Log("wallstage: no pawn");
        return;
    }
    Gap g;
    if (!ReadGap(&g)) {
        net::Log("wallstage: no traversal interaction with WT_Entrance/WT_Exit here");
        return;
    }
    net::Log("wallstage: %s interaction=%s ability=%s ent=(%.0f %.0f %.0f) exi=(%.0f %.0f %.0f) "
             "len=%.0f",
             which.c_str(), g.inter ? ue::GetName(g.inter).c_str() : "-",
             g.abil && net::Valid(g.abil) ? ue::GetName(g.abil).c_str() : "-", g.ent.X, g.ent.Y,
             g.ent.Z, g.exi.X, g.exi.Y, g.exi.Z, Len(Sub(g.exi, g.ent)));
    if (which == "cache") {
        // remember the gap while it is streamed in; no teleport, no side effects
        net::Log("wallstage: cache -> ok=%d", g.ok ? 1 : 0);
        return;
    }
    if (which == "actor") {
        const Vec3 stand = Add(Sub(g.ent, Mul(g.fwd, 220.0)), Up(20.0));
        const bool placed = PlacePawn(pawn, stand);
        net::Log("wallstage: actor placed=%d at (%.0f %.0f %.0f) facing the exit", placed ? 1 : 0,
                 stand.X, stand.Y, stand.Z);
        LookAt(pawn, Add(g.exi, Up(40.0)));
        return;
    }
    // observer: to the side of the gap, looking at its centre; the exact distance
    // decides how big the squeezing body is on screen (550 uu was too far for a
    // readable frame, measured).
    //   observer  : 5.5 m to one side      observer2 : 5.5 m to the other side
    //   observer3 : 3.0 m to one side (closer, for the pose)
    //   observer4 : 4.5 m BEHIND the exit, looking back at the gap (the body comes
    //               out of the gap towards the camera)
    if (which == "observer4" || which == "observer5") {
        // behind the exit, looking back at the gap: the body comes OUT of the gap
        // towards the camera.  observer5 stands further back where there is room for
        // the third-person camera (observer4 at 4.5 m ended up inside the rock).
        const double back = (which == "observer5") ? 900.0 : 450.0;
        const Vec3 stand = Add(Add(g.exi, Mul(g.fwd, back)), Up(which == "observer5" ? 220.0 : 70.0));
        const bool placed = PlacePawn(pawn, stand);
        net::Log("wallstage: %s placed=%d behind the exit at (%.0f %.0f %.0f)", which.c_str(),
                 placed ? 1 : 0, stand.X, stand.Y, stand.Z);
        LookAt(pawn, Add(g.exi, Up(20.0)));
        return;
    }
    const double side = (which == "observer2") ? -550.0 : (which == "observer3" ? 300.0 : 550.0);
    const Vec3 stand = Add(Add(g.center, Mul(g.right, side)), Up(which == "observer3" ? 90.0 : 60.0));
    const bool placed = PlacePawn(pawn, stand);
    net::Log("wallstage: observer placed=%d side=%.0f at (%.0f %.0f %.0f)", placed ? 1 : 0, side,
             stand.X, stand.Y, stand.Z);
    LookAt(pawn, Add(g.center, Up(45.0)));
}

// wallreal -- the whole real chain (see RealTraversal).
void Real() {
    Gap g;
    if (!ReadGap(&g)) {
        net::Log("wallreal: no traversal interaction with WT_Entrance/WT_Exit here");
        return;
    }
    RealTraversal(g, true);
}

// wallgap -- print the gap geometry and both ends.
void GapDump() {
    Gap g;
    if (!ReadGap(&g)) {
        net::Log("wallgap: no traversal interaction with WT_Entrance/WT_Exit here");
        return;
    }
    net::Log("wallgap: interaction=%s ability=%s ent=(%.0f %.0f %.0f) exi=(%.0f %.0f %.0f) "
             "center=(%.0f %.0f %.0f) fwd=(%.2f %.2f %.2f) len=%.0f",
             ue::GetName(g.inter).c_str(),
             g.abil && net::Valid(g.abil) ? ue::GetName(g.abil).c_str() : "-", g.ent.X, g.ent.Y,
             g.ent.Z, g.exi.X, g.exi.Y, g.exi.Z, g.center.X, g.center.Y, g.center.Z, g.fwd.X,
             g.fwd.Y, g.fwd.Z, Len(Sub(g.exi, g.ent)));
}

}  // namespace

// wallscan -- what this client has to work with: the traversal interactions and
// destructible walls near the pawn, plus the montage capture's own view.
void Scan(const std::string& arg) {
    (void)arg;
    ue::UObject* pawn = net::Pawn();
    Vec3 pl;
    const bool havePawn = ActorLoc(pawn, &pl);
    net::Log("wallscan: pawn=%s at (%.0f %.0f %.0f) wallAnim=%d", ue::GetName(pawn).c_str(), pl.X,
             pl.Y, pl.Z, On() ? 1 : 0);

    int n = 0;
    for (ue::UObject* o : ue::FindAllOf(kTraversalClass)) {
        if (!net::Valid(o)) continue;
        Vec3 l;
        ActorLoc(o, &l);
        const double d = havePawn ? coop::Dist(pl, l) : -1.0;
        ue::UObject* abil = PropObj(o, "TraversalAbility");
        net::Log("wallscan: traversal[%d] %s %s at (%.0f %.0f %.0f) d=%.0f ability=%s disabled=%d",
                 n, ue::GetClassName(o).c_str(), ue::GetName(o).c_str(), l.X, l.Y, l.Z, d,
                 abil && net::Valid(abil) ? ue::GetName(abil).c_str() : "-",
                 ue::GetBool(o, "IsDisabledDueToDamage") ? 1 : 0);
        if (++n >= 12) break;
    }
    if (n == 0) net::Log("wallscan: no %s in this world", kTraversalClass);

    int w = 0;
    for (ue::UObject* o : ue::FindAllOf(kBreakableWallClass)) {
        if (!net::Valid(o)) continue;
        Vec3 l;
        ActorLoc(o, &l);
        const double d = havePawn ? coop::Dist(pl, l) : -1.0;
        net::Log("wallscan: wall[%d] %s at (%.0f %.0f %.0f) d=%.0f broken=%d unlock=%d", w,
                 ue::GetName(o).c_str(), l.X, l.Y, l.Z, d, ue::GetBool(o, "WallIsBroken") ? 1 : 0,
                 ue::FindFunction(o, "UnlockTraversals") ? 1 : 0);
        if (++w >= 12) break;
    }
    if (w == 0) net::Log("wallscan: no %s in this world", kBreakableWallClass);

    MontageSig m;
    const bool has = local::CaptureMontage(&m);
    net::Log("wallscan: capture=%d seg=%s pos=%.3f rate=%.2f id=%d traversal=%d", has ? 1 : 0,
             m.path.c_str(), m.pos, m.rate, m.id, IsTraversalAsset(m.path) ? 1 : 0);
    // the anim binding of the squeeze ability that is actually placed here
    {
        double dist = 0.0;
        ue::UObject* inter = NearestTraversal(&dist);
        ue::UClass* abil = reinterpret_cast<ue::UClass*>(inter ? PropObj(inter, "TraversalAbility") : nullptr);
        ue::UObject* mo = AbilityMontage(abil);
        if (!mo) mo = AbilityMontage(ue::FindClass(kSqueezeAbility));
        LogAnimBinding("wallscan: ability", PropObj(pawn, "Mesh"), mo);
    }
    net::Log("wallscan: puppet anim=%s kind=%s peerM=%s", St().puppet.animState.c_str(),
             St().puppet.animKind.c_str(), St().peer.montage.path.c_str());
}

// wallplay anim|ga|nearest|stop -- the ways into the game's squeeze montage:
//   anim     play the SqueezeThrough montage asset on our own pawn
//   ga       read it off GA_Traversal_SqueezeThrough_C's CDO (proves the asset)
//   nearest  call Event_Trigger() on the nearest traversal interaction (the game's
//            own entry: it grants and activates TraversalAbility)
//   stop     stop the montage again
void Play(const std::string& arg) {
    EnsureTraceTick();
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn)) {
        net::Log("wallplay: no pawn");
        return;
    }
    if (arg == "stop") {
        ue::UObject* mo = ue::FindObject(kSqueezeMontage);
        const bool ok = StopMontageOn(pawn, mo);
        net::Log("wallplay: stop -> %d", ok ? 1 : 0);
        return;
    }
    if (arg == "go" || arg == "info" || arg == "auto") {
        // The ability the world's own interaction uses -- and, crucially, the montage
        // THAT ability plays (the NoWeaponStow variant has `Montage`, not
        // `SqueezeMontage`).  `go` enters exactly where an ability task ends
        // (UAnimInstance::Montage_Play), which is measured to work on this character
        // while ACharacter::PlayAnimMontage is refused (len=0, REPORT.md).
        double dist = 0.0;
        ue::UObject* inter = NearestTraversal(&dist);
        ue::UClass* abil = reinterpret_cast<ue::UClass*>(inter ? PropObj(inter, "TraversalAbility") : nullptr);
        ue::UClass* dflt = ue::FindClass(kSqueezeAbility);
        net::Log("wallplay: %s nearest=%s d=%.0f ability=%s default=%s", arg.c_str(),
                 inter ? ue::GetName(inter).c_str() : "-", dist,
                 abil ? ue::GetName((ue::UObject*)abil).c_str() : "-",
                 dflt && ue::IsValid((ue::UObject*)dflt) ? ue::GetName((ue::UObject*)dflt) : "-");
        ue::UObject* montage = AbilityMontage(abil);
        if (!montage) montage = AbilityMontage(dflt);
        if (!montage) montage = ue::FindObject(kSqueezeMontage);
        if (arg == "go") {
            ue::UObject* mesh = PropObj(pawn, "Mesh");
            LogAnimBinding("wallplay: go", mesh, montage);
            const double direct = MontagePlayDirect(mesh, montage, 1.0);
            net::Log("wallplay: go Montage_Play(inst) -> %.2f", direct);
            StartTrace(6.0);
            return;
        }
        if (arg == "info") {
            TryMontage("wallplay: info (ability)", montage);
            return;
        }
        TryMontage("wallplay: auto", montage);
        return;
    }
    ue::UObject* montage = nullptr;
    const char* how = "anim";
    if (arg == "ga") {
        ue::UClass* c = ue::FindClass(kSqueezeAbility);
        ue::UObject* cdo = c ? ue::GetCDO(c) : nullptr;
        montage = PropObj(cdo, "SqueezeMontage");
        how = "ga";
        net::Log("wallplay: %s cdo=%s montage=%s", kSqueezeAbility,
                 cdo ? ue::GetName(cdo).c_str() : "-",
                 montage ? net::AssetPath(montage).c_str() : "-");
    } else if (arg == "nearest" || arg.empty()) {
        double dist = 0.0;
        ue::UObject* inter = NearestTraversal(&dist);
        how = "nearest";
        if (!inter) {
            net::Log("wallplay: no traversal interaction in this world");
            return;
        }
        ue::UObject* abil = PropObj(inter, "TraversalAbility");
        net::Log("wallplay: nearest %s %s d=%.0f ability=%s", ue::GetClassName(inter).c_str(),
                 ue::GetName(inter).c_str(), dist,
                 abil && net::Valid(abil) ? ue::GetName(abil).c_str() : "-");
        ue::UFunction* f = ue::FindFunction(inter, "Event_Trigger");
        if (!f) {
            net::Log("wallplay: the interaction has no Event_Trigger");
            return;
        }
        ue::Params p(f);
        const bool ok = ue::Call(inter, f, p.data());
        net::Log("wallplay: Event_Trigger ok=%d (trace follows)", ok ? 1 : 0);
        StartTrace(6.0);
        return;
    }
    if (!montage) montage = ue::FindObject(kSqueezeMontage);
    if (!net::Valid(montage)) {
        net::Log("wallplay: the squeeze montage asset is not loaded (%s)", kSqueezeMontage);
        return;
    }
    const double len = PlayMontageOn(pawn, montage, 1.0);
    net::Log("wallplay: %s PlayAnimMontage(%s) len=%.2f path=%s", how,
             TailOf(net::AssetPath(montage)).c_str(), len, net::AssetPath(montage).c_str());
    StartTrace(6.0);
}

// wallprobe <secs> -- 10 Hz trace of the montage capture, our pawn's position and
// what the puppet is doing.
void Probe(const std::string& arg) {
    EnsureTraceTick();
    double secs = 6.0;
    if (!arg.empty()) {
        const double v = std::atof(arg.c_str());
        if (v > 0.0 && v < 120.0) secs = v;
    }
    StartTrace(secs);
    net::Log("wallprobe: start %.1fs", secs);
}

void Cmd(const std::string& cmd, const std::string& rest) {
    if (cmd == "wallscan") {
        Scan(rest);
    } else if (cmd == "wallplay") {
        Play(rest);
    } else if (cmd == "wallprobe") {
        Probe(rest);
    } else if (cmd == "wallgap") {
        GapDump();
    } else if (cmd == "wallstage") {
        Stage(rest);
    } else if (cmd == "wallreal") {
        Real();
    }
}

}  // namespace wall
}  // namespace coop
