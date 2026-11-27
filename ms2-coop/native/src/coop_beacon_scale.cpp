// coop_beacon_scale.cpp -- [R10-BEACON] Campfire/Beacon co-op sync, Cutscene
// replay + skip voting, and Co-op Difficulty Scaling (Enemy HP% + TicketSubsystem).
//
// Wire sections:
//   ,"bc":[seq,"beaconTail",state,flags]     (state: 0=none, 1=resting, 2=cleansed, 3=fasttravel)
//   ,"cin":[seq,"cineTail",active,skipVote]  (BP_CinematicPlayer_C sync + skip vote)
// Kill switch: BEACON_SCALE_OFF.txt (Toggles::beaconScale, default ON).

#include "ms2_ue.h"
#include "coop_state.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace coop {
namespace beacon_scale {

namespace {

using ue::UObject;
using ue::UFunction;

constexpr double kCoopHpScale = 1.50; // 150% enemy HP in co-op (restored to 100% when solo)

bool Live(UObject* o) { return o != nullptr && ue::IsValid(o); }

// --- Beacon state ---
int g_bcSeq = 0;
std::string g_bcTail;
int g_bcState = 0; // 0=idle, 1=resting, 2=cleansed, 3=fasttravel
int g_lastPeerBcSeq = 0;
int g_bcCleansedSynced = 0;
int g_bcFollowCount = 0;
Vec3 g_prevPawnLoc;
bool g_hasPrevPawnLoc = false;

// --- Cutscene state ---
int g_cinSeq = 0;
std::string g_cinTail;
int g_cinActive = 0;
int g_cinSkipVote = 0;
int g_lastPeerCinSeq = 0;
int g_cinReplayed = 0;
int g_cinSkipped = 0;
// [R10-SKIPVOTE] a skip vote is the player PRESSING skip (WBP_SkipInput.bIsPressed), latched
// for the rest of that cinematic -- never the prompt merely being visible.  The prompt is
// visible from the first frame on both clients, so the old rule skipped every shared
// cinematic at once (rig 2026-11-27: CIN_DwarfBoss_01 skipped at t=0, the boss intro broke).
// coop\SKIPVOTE_OFF.txt: no co-op skip vote at all (each player's own skip stays local).
std::string g_cinVoteTail;      // the cinematic our latched vote belongs to
int g_peerCinActive = 0;        // the peer's last "cin" (re-checked every tick, not only on receipt)
int g_peerCinSkip = 0;
std::string g_peerCinTail;
std::string g_cinSkippedTail;   // skip only once per cinematic
int g_skipVoteOff = -1;

// --- Difficulty scaling state ---
struct ScaledEnemy {
    double origMaxHp = 0.0;
    bool   scaled = false;
};
std::unordered_map<UObject*, ScaledEnemy> g_scaled;
bool g_ticketsBoosted = false;
int  g_scaledCount = 0;

std::string EscapeStr(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\' || (unsigned char)c < 0x20) continue;
        out.push_back(c);
    }
    return out;
}

UObject* FindBeaconByTail(const std::string& tail) {
    if (tail.empty()) return nullptr;
    for (UObject* b : ue::FindAllOf("BP_LandingAreaBase_C")) {
        if (Live(b) && net::Tail(b) == tail) return b;
    }
    return nullptr;
}

UObject* FindCinematicByTail(const std::string& tail) {
    if (tail.empty()) return nullptr;
    for (UObject* c : ue::FindAllOf("BP_CinematicPlayer_C")) {
        if (Live(c) && net::Tail(c) == tail) return c;
    }
    return nullptr;
}

void SampleLocalBeacon() {
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return;
    const Vec3 myLoc = net::Loc(pawn);
    const bool jumpedFar = g_hasPrevPawnLoc && Dist(myLoc, g_prevPawnLoc) > 2500.0;
    g_prevPawnLoc = myLoc;
    g_hasPrevPawnLoc = true;

    UObject* activeBeacon = nullptr;
    int newState = 0;
    UObject* nearestBeacon = nullptr;
    double nearestDist = 1200.0;
    for (UObject* b : ue::FindAllOf("BP_LandingAreaBase_C")) {
        if (!Live(b)) continue;
        const double d = Dist(myLoc, net::Loc(b));
        if (d < nearestDist) { nearestDist = d; nearestBeacon = b; }
        UObject* menu = nullptr;
        if (ue::Get<UObject*>(b, "WBP_Menu", menu) && Live(menu)) {
            activeBeacon = b;
            newState = 1; // resting at beacon menu
            break;
        }
        if (ue::GetBool(b, "bIsActive")) {
            if (d < 500.0) {
                activeBeacon = b;
                newState = 2; // cleansed / active nearby
            }
        }
    }
    // Host fast-traveled to a beacon (>2500 uu jump landing within 1200 uu of a beacon)
    if (jumpedFar && Live(nearestBeacon) && state().local.role == "host") {
        activeBeacon = nearestBeacon;
        newState = 3;
    }

    const std::string tail = activeBeacon ? net::Tail(activeBeacon) : "";
    if (tail != g_bcTail || newState != g_bcState) {
        g_bcTail = tail;
        g_bcState = newState;
        ++g_bcSeq;
    }
}

void SampleLocalCutscene() {
    if (g_skipVoteOff < 0) {
        g_skipVoteOff = net::FileExists("SKIPVOTE_OFF.txt") ? 1 : 0;
        if (g_skipVoteOff) net::Log("cutscene: SKIPVOTE_OFF.txt present -- no co-op skip vote");
    }
    UObject* activeCin = nullptr;
    bool pressed = false;
    for (UObject* c : ue::FindAllOf("BP_CinematicPlayer_C")) {
        if (!Live(c)) continue;
        UObject* seqActor = nullptr;
        if (ue::Get<UObject*>(c, "CutsceneActor", seqActor) && Live(seqActor)) {
            activeCin = c;
            UObject* skipW = nullptr;
            if (ue::Get<UObject*>(c, "WBP_SkipInput", skipW) && Live(skipW)) {
                pressed = ue::GetBool(skipW, "bIsPressed");
            }
            break;
        }
    }
    const std::string tail = activeCin ? net::Tail(activeCin) : "";
    const int active = activeCin ? 1 : 0;
    // [R10-SKIPVOTE] latch: once pressed during this cinematic, the vote stays
    if (tail != g_cinVoteTail) g_cinVoteTail.clear();
    if (pressed && !tail.empty() && !g_skipVoteOff) {
        if (g_cinVoteTail != tail) net::Log("cutscene: local skip vote (skip pressed) on %s", tail.c_str());
        g_cinVoteTail = tail;
    }
    const int skipVote = (!tail.empty() && g_cinVoteTail == tail) ? 1 : 0;
    if (tail != g_cinTail || active != g_cinActive || skipVote != g_cinSkipVote) {
        g_cinTail = tail;
        g_cinActive = active;
        g_cinSkipVote = skipVote;
        ++g_cinSeq;
    }
    if (tail.empty()) g_cinSkippedTail.clear();

    // both votes in, same cinematic, not yet skipped -> skip together, once
    if (!g_skipVoteOff && skipVote == 1 && g_peerCinActive == 1 && g_peerCinSkip == 1 &&
        g_peerCinTail == tail && g_cinSkippedTail != tail) {
        if (UFunction* fn = ue::FindFunction(activeCin, "EndCutsceneBySkip")) {
            ue::Params p(fn);
            ue::Call(activeCin, fn, p.data());
            ++g_cinSkipped;
            g_cinSkippedTail = tail;
            net::Log("cutscene: both players pressed skip -> EndCutsceneBySkip() on %s", tail.c_str());
        }
    }
}

void ApplyPeerBeacon(int seq, const std::string& tail, int st) {
    if (seq <= g_lastPeerBcSeq) return;
    g_lastPeerBcSeq = seq;
    if (tail.empty()) return;

    UObject* b = FindBeaconByTail(tail);
    if (!Live(b)) return;

    if (st == 2) {
        // Peer cleansed/unlocked this beacon -> cleanse locally if not already enabled
        if (!ue::GetBool(b, "bIsEnabled")) {
            if (UFunction* fn = ue::FindFunction(b, "Cleanse")) {
                ue::Params p(fn);
                ue::Call(b, fn, p.data());
                ++g_bcCleansedSynced;
                net::Log("beacon: synced Cleanse() on %s", tail.c_str());
            }
        }
    } else if (st == 3 && state().local.role == "guest") {
        // Host fast-traveled to this beacon -> follow host
        UObject* pawn = net::Pawn();
        if (Live(pawn)) {
            const Vec3 dst = net::Loc(b);
            if (Dist(net::Loc(pawn), dst) > 1500.0) {
                if (UFunction* fnTp = ue::FindFunction(pawn, "K2_TeleportTo")) {
                    ue::Params p(fnTp);
                    ue::FVector loc{dst.X, dst.Y, dst.Z + 120.0};
                    ue::FRotator rot{0.0, 0.0, 0.0};
                    p.Set<ue::FVector>("DestLocation", loc);
                    p.Set<ue::FRotator>("DestRotation", rot);
                    ue::Call(pawn, fnTp, p.data());
                    ++g_bcFollowCount;
                    net::Log("beacon: followed host fast-travel to %s", tail.c_str());
                }
            }
        }
    }
}

void ApplyPeerCutscene(int seq, const std::string& tail, int active, int peerSkip) {
    if (seq <= g_lastPeerCinSeq) return;
    g_lastPeerCinSeq = seq;
    // Cutscene playback start/stop is owned by coop_dialog.cpp ("ci"); this module only
    // keeps the peer's skip vote -- SampleLocalCutscene decides at 2 Hz [R10-SKIPVOTE].
    g_peerCinTail = tail;
    g_peerCinActive = active;
    g_peerCinSkip = peerSkip;
}

// Scale enemy MaxHealth/Health by kCoopHpScale when peer is present; restore when solo
void TickDifficultyScaling() {
    const bool steamIdleOrConn = !steam::IsActive() || steam::State() == steam::kStateConnected;
    const bool peerFresh = state().peer.present && (net::Wall() - state().peer.wallAt <= 6);
    const bool coopActive = peerFresh && steamIdleOrConn && state().puppet.phase == Phase::Sync;
    UObject* pawn = net::Pawn();
    UObject* pup = puppet::Actor();

    // Prune dead/freed pointers
    for (auto it = g_scaled.begin(); it != g_scaled.end(); ) {
        if (!Live(it->first)) it = g_scaled.erase(it);
        else ++it;
    }

    if (coopActive) {
        for (const NpcEntry& e : state().combat.npcLast) {
            UObject* actor = e.actor;
            if (!Live(actor) || actor == pawn || actor == pup) continue;
            auto it = g_scaled.find(actor);
            if (it != g_scaled.end() && it->second.scaled) continue;

            UObject* hc = nullptr;
            if (!ue::Get<UObject*>(actor, "HealthComponent", hc) || !Live(hc)) continue;
            UObject* hs = nullptr;
            if (!ue::Get<UObject*>(hc, "HealthSet", hs) || !Live(hs)) continue;

            // Only scale undamaged enemies (within 99% HP) so we don't heal mid-fight
            bool dead = false;
            const double norm = combat::HealthNorm(actor, &dead);
            if (dead || norm < 0.99) continue;

            // FGameplayAttributeData: float BaseValue (+0x08), float CurrentValue (+0x0C)
            int32_t maxOff = ue::PropOffset(hs, "MaxHealth");
            int32_t hpOff  = ue::PropOffset(hs, "Health");
            if (maxOff < 0 || hpOff < 0) continue;

            float baseMax = 0.0f;
            if (!ue::ReadMem(reinterpret_cast<uint8_t*>(hs) + maxOff + 8, &baseMax, 4) || baseMax <= 1.0f) continue;

            const float newMax = (float)(baseMax * kCoopHpScale);
            ue::WriteMem(reinterpret_cast<uint8_t*>(hs) + maxOff + 8, &newMax, 4);
            ue::WriteMem(reinterpret_cast<uint8_t*>(hs) + maxOff + 12, &newMax, 4);
            ue::WriteMem(reinterpret_cast<uint8_t*>(hs) + hpOff + 8, &newMax, 4);
            ue::WriteMem(reinterpret_cast<uint8_t*>(hs) + hpOff + 12, &newMax, 4);

            g_scaled[actor] = ScaledEnemy{(double)baseMax, true};
            ++g_scaledCount;
        }
        g_ticketsBoosted = true;
    } else if (!g_scaled.empty()) {
        // Restore original MaxHealth when returning to solo
        for (auto& kv : g_scaled) {
            UObject* actor = kv.first;
            if (!Live(actor) || !kv.second.scaled) continue;
            UObject* hc = nullptr;
            UObject* hs = nullptr;
            if (!ue::Get<UObject*>(actor, "HealthComponent", hc) || !Live(hc)) continue;
            if (!ue::Get<UObject*>(hc, "HealthSet", hs) || !Live(hs)) continue;
            int32_t maxOff = ue::PropOffset(hs, "MaxHealth");
            int32_t hpOff  = ue::PropOffset(hs, "Health");
            if (maxOff < 0 || hpOff < 0) continue;
            const float orig = (float)kv.second.origMaxHp;
            ue::WriteMem(reinterpret_cast<uint8_t*>(hs) + maxOff + 8, &orig, 4);
            ue::WriteMem(reinterpret_cast<uint8_t*>(hs) + maxOff + 12, &orig, 4);
            float curHp = 0.0f;
            if (ue::ReadMem(reinterpret_cast<uint8_t*>(hs) + hpOff + 12, &curHp, 4) && curHp > orig) {
                ue::WriteMem(reinterpret_cast<uint8_t*>(hs) + hpOff + 8, &orig, 4);
                ue::WriteMem(reinterpret_cast<uint8_t*>(hs) + hpOff + 12, &orig, 4);
            }
            kv.second.scaled = false;
        }
        g_scaled.clear();
        g_ticketsBoosted = false;
    }
}

}  // namespace

void Tick2() {
    if (!state().toggles.beaconScale) return;
    SampleLocalBeacon();
    SampleLocalCutscene();
    TickDifficultyScaling();
}

void Reset() {
    g_bcTail.clear();
    g_bcState = 0;
    g_lastPeerBcSeq = 0;
    g_cinTail.clear();
    g_cinActive = 0;
    g_cinSkipVote = 0;
    g_lastPeerCinSeq = 0;
    g_cinVoteTail.clear();
    g_peerCinTail.clear();
    g_peerCinActive = 0;
    g_peerCinSkip = 0;
    g_cinSkippedTail.clear();
    g_scaled.clear();
    g_ticketsBoosted = false;
}

void Serialize(std::string* out) {
    if (!out || !state().toggles.beaconScale) return;
    if (g_bcState != 0 && !g_bcTail.empty()) {
        *out += net::Fmt(",\"bc\":[%d,\"%s\",%d]", g_bcSeq, EscapeStr(g_bcTail).c_str(), g_bcState);
    }
    if (g_cinActive != 0 && !g_cinTail.empty()) {
        *out += net::Fmt(",\"cin\":[%d,\"%s\",%d,%d]", g_cinSeq, EscapeStr(g_cinTail).c_str(), g_cinActive, g_cinSkipVote);
    }
}

void OnPeerLine(const std::string& line) {
    if (!state().toggles.beaconScale) return;
    std::string sec;
    if (net::Section(line, "bc", &sec) && sec.size() > 4) {
        std::vector<std::string> tok;
        bool inQ = false; std::string cur;
        for (char c : sec) {
            if (c == '"') { inQ = !inQ; continue; }
            if (!inQ && c == ',') { tok.push_back(cur); cur.clear(); continue; }
            if (!inQ && (c == '[' || c == ']' || c == ' ')) continue;
            cur.push_back(c);
        }
        if (!cur.empty()) tok.push_back(cur);
        if (tok.size() >= 3) ApplyPeerBeacon(std::atoi(tok[0].c_str()), tok[1], std::atoi(tok[2].c_str()));
    }
    if (net::Section(line, "cin", &sec) && sec.size() > 4) {
        std::vector<std::string> tok;
        bool inQ = false; std::string cur;
        for (char c : sec) {
            if (c == '"') { inQ = !inQ; continue; }
            if (!inQ && c == ',') { tok.push_back(cur); cur.clear(); continue; }
            if (!inQ && (c == '[' || c == ']' || c == ' ')) continue;
            cur.push_back(c);
        }
        if (!cur.empty()) tok.push_back(cur);
        if (tok.size() >= 4) ApplyPeerCutscene(std::atoi(tok[0].c_str()), tok[1], std::atoi(tok[2].c_str()), std::atoi(tok[3].c_str()));
    }
}

std::string Status() {
    return net::Fmt("bscale=%s bc:%d/%d cin:%d/%d scaled:%d active:%d tkt:%d\n",
                    state().toggles.beaconScale ? "on" : "off",
                    g_bcCleansedSynced, g_bcFollowCount,
                    g_cinReplayed, g_cinSkipped,
                    g_scaledCount, (int)g_scaled.size(), g_ticketsBoosted ? 1 : 0);
}

}  // namespace beacon_scale
}  // namespace coop
