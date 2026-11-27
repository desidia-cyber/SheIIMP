// coop_world_partb.cpp -- [R10-PARTB] Part B long-tail co-op sync:
//   1. Boss arena entry / fog-wall partner pull & per-boss quirks (S45: HeadTree underground, etc.)
//   2. Dungeon variant sync (S73: BP_LevelInstanceSelector_C / TombZone LoadLevel)
//   3. Non-save-flag world puzzles & horde spawners (S28-S39, S47-S48: pressure plates, bells,
//      lanterns, illusory walls, drawbridges, BP_Spawner_C)
//   4. Non-hit Shell abilities & Seal state (S9, S18: Harden / Parry / Guard / Slayer / Shell abilities)
//
// Wire sections:
//   ,"bsa":[seq,"bossTail",x,y,z,quirk]
//   ,"dng":[["selectorTail","eventAssetPath"],...]
//   ,"pzl":[[seq,"actorTail",kind,val],...]
//   ,"abl":[seq,"verbOrAbility",active]
// Kill switch: PARTB_OFF.txt (Toggles::partB, default ON).

#include "ms2_ue.h"
#include "coop_state.h"

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
namespace partb {

namespace {

using ue::UObject;
using ue::UFunction;

constexpr double kTxRepeatSecs = 3.0;

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

// --- 1. Boss arena entry & per-boss quirks (S45) ---
int g_bsaSeq = 0;
std::string g_bsaBossTail;
Vec3 g_bsaPos;
int g_bsaQuirk = 0; // bit0=HeadTreeUnderground
int g_lastPeerBsaSeq = 0;
int g_bossPulls = 0;
int g_bossQuirksSynced = 0;

// --- 2. Dungeon variant sync (S73) ---
struct DngEntry {
    std::string selectorTail;
    std::string eventPath;
};
std::vector<DngEntry> g_dngList;
std::unordered_set<std::string> g_dngApplied;
int g_dngSynced = 0;

// --- 3. Non-save-flag puzzles & horde spawners (S28-S39, S47-S48) ---
struct PzlEvent {
    int seq = 0;
    std::string tail;
    int kind = 0; // 1=puzzle, 2=illusory, 3=spawner
    int val = 0;
    double at = 0;
};
std::unordered_map<std::string, int> g_pzlSeen;
std::deque<PzlEvent> g_pzlQ;
int g_nextPzlSeq = 1, g_lastRxPzlSeq = 0;
int g_pzlTx = 0, g_pzlRx = 0;

// --- 4. Seal verb & non-hit Shell abilities (S9, S18) ---
int g_ablSeq = 0;
std::string g_ablName;
int g_ablActive = 0;
std::string g_sealName;
int g_lastPeerAblSeq = 0;
int g_ablSynced = 0;

// --- 5. NG+ / Send Ova safety notification (S61) ---
int g_ngpSeq = 0;
int g_lastPeerNgpSeq = 0;
bool g_ngpHooked = false;

void OnResetForNewGamePlus(UObject*, UFunction*, void*) {
    if (!state().toggles.partB) return;
    ++g_ngpSeq;
    net::Log("partb: S_ResetForNewGamePlus triggered locally (seq=%d) -- notifying partner", g_ngpSeq);
}

// ---------------------------------------------------------------------------
// 1. Boss arena entry & quirks
// ---------------------------------------------------------------------------
void SampleBossArena() {
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return;

    std::string activeBoss;
    Vec3 bossPos;
    int quirk = 0;

    for (UObject* ai : ue::FindAllOf("BPC_BossAI_C")) {
        if (!Live(ai)) continue;
        if (!ue::GetBool(ai, "bIsAggroed")) continue;
        UObject* owner = ue::GetOuter(ai);
        if (!Live(owner)) continue;
        bool dead = false;
        if (combat::HealthNorm(owner, &dead) <= 0.001 || dead) continue;

        activeBoss = net::Tail(owner);
        bossPos = net::Loc(owner);
        // Check HeadTree underground state (hidden / collision disabled while alive)
        if (ue::GetBool(owner, "bHidden")) quirk |= 1;
        break;
    }

    if (activeBoss != g_bsaBossTail || quirk != g_bsaQuirk) {
        g_bsaBossTail = activeBoss;
        g_bsaPos = bossPos;
        g_bsaQuirk = quirk;
        ++g_bsaSeq;
    }
}

void ApplyPeerBossArena(int seq, const std::string& bossTail, double bx, double by, double bz, int quirk) {
    if (seq <= g_lastPeerBsaSeq) return;
    g_lastPeerBsaSeq = seq;
    if (bossTail.empty()) return;

    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return;

    // If partner is inside a boss arena and we are stuck outside the fog wall (1500..12000 uu),
    // pull us cleanly to the partner's side inside the arena!
    const Vec3 bossLoc{bx, by, bz};
    const double myDist = Dist(net::Loc(pawn), bossLoc);
    const double peerDist = state().peer.present ? Dist(state().peer.pos, bossLoc) : 1e9;
    if (myDist > 1500.0 && myDist < 12000.0 && peerDist < 3000.0) {
        if (UFunction* fnTp = ue::FindFunction(pawn, "K2_TeleportTo")) {
            ue::Params p(fnTp);
            ue::FVector dst{state().peer.pos.X, state().peer.pos.Y, state().peer.pos.Z + 80.0};
            ue::FRotator rot{0.0, state().peer.rot.Yaw, 0.0};
            p.Set<ue::FVector>("DestLocation", dst);
            p.Set<ue::FRotator>("DestRotation", rot);
            if (ue::Call(pawn, fnTp, p.data())) {
                ++g_bossPulls;
                net::Log("partb: pulled local player into boss arena %s (was %.0f uu away)",
                         bossTail.c_str(), myDist);
            }
        }
    }

    // Sync HeadTree underground visibility quirk (hide when underground, unhide when surfacing)
    static int g_lastAppliedQuirk = 0;
    if ((quirk & 1) != (g_lastAppliedQuirk & 1)) {
        for (UObject* ai : ue::FindAllOf("BPC_BossAI_C")) {
            if (!Live(ai)) continue;
            UObject* owner = ue::GetOuter(ai);
            if (Live(owner) && net::Tail(owner) == bossTail) {
                if (UFunction* fn = ue::FindFunction(owner, "SetActorHiddenInGame")) {
                    ue::Params p(fn);
                    p.Set<bool>("bNewHidden", (quirk & 1) != 0);
                    ue::Call(owner, fn, p.data());
                    g_lastAppliedQuirk = quirk;
                    ++g_bossQuirksSynced;
                }
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 2. Dungeon variant sync (S73)
// ---------------------------------------------------------------------------
void SampleDungeonVariants() {
    if (state().local.role != "host") return;
    g_dngList.clear();
    for (UObject* sel : ue::FindAllOf("BP_LevelInstanceSelector_C")) {
        if (!Live(sel)) continue;
        if (!ue::GetBool(sel, "bIsLevelLoaded")) continue;
        UObject* ev = nullptr;
        if (!ue::Get<UObject*>(sel, "LastEventLoaded", ev) || !Live(ev)) continue;
        std::string evPath = ue::GetFullName(ev);
        size_t sp = evPath.find(' ');
        if (sp != std::string::npos) evPath.erase(0, sp + 1);
        if (evPath.size() < 8) continue;
        g_dngList.push_back(DngEntry{net::Tail(sel), evPath});
    }
}

void ApplyPeerDungeonVariant(const std::string& selTail, const std::string& evPath) {
    if (state().local.role == "host" || selTail.empty() || evPath.empty()) return;
    const std::string key = selTail + "|" + evPath;
    if (g_dngApplied.count(key)) return;

    UObject* targetSel = nullptr;
    for (UObject* sel : ue::FindAllOf("BP_LevelInstanceSelector_C")) {
        if (Live(sel) && net::Tail(sel) == selTail) { targetSel = sel; break; }
    }
    if (!Live(targetSel)) return;

    // Check if already loaded with the exact same event asset
    UObject* curEv = nullptr;
    if (ue::Get<UObject*>(targetSel, "LastEventLoaded", curEv) && Live(curEv)) {
        std::string curPath = ue::GetFullName(curEv);
        size_t sp = curPath.find(' ');
        if (sp != std::string::npos) curPath.erase(0, sp + 1);
        if (curPath == evPath) {
            g_dngApplied.insert(key);
            return;
        }
        // Mismatched layout -> unload first
        if (UFunction* fnUnl = ue::FindFunction(targetSel, "UnloadLevel")) {
            ue::Params p(fnUnl);
            ue::Call(targetSel, fnUnl, p.data());
        }
    }

    UObject* evAsset = net::Find(evPath, false);
    if (!Live(evAsset)) return;

    if (UFunction* fnLoad = ue::FindFunction(targetSel, "LoadLevel")) {
        ue::Params p(fnLoad);
        p.Set<UObject*>("SelectedEvent", evAsset);
        if (ue::Call(targetSel, fnLoad, p.data())) {
            g_dngApplied.insert(key);
            ++g_dngSynced;
            net::Log("partb: synced dungeon variant %s -> %s", selTail.c_str(), evPath.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// 3. Non-save-flag puzzles & horde spawners (S28-S39, S47-S48)
// ---------------------------------------------------------------------------
void SamplePuzzlesAndSpawners() {
    if (!state().peer.present) return;
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return;
    const Vec3 myLoc = net::Loc(pawn);
    const double now = Mono();

    // Non-proximity BP_Spawner_C horde packs near local player
    for (UObject* sp : ue::FindAllOf("BP_Spawner_C")) {
        if (!Live(sp)) continue;
        if (Dist(myLoc, net::Loc(sp)) > 3500.0) continue;
        const std::string tail = net::Tail(sp);
        if (tail.empty()) continue;
        // Skip proximity spawners (already handled by coop_combat + coop_farenemy)
        if (ue::GetClassName(sp).find("Proximity") != std::string::npos) continue;

        UObject* spawned = nullptr;
        int val = (ue::Get<UObject*>(sp, "SpawnedEnemy", spawned) && Live(spawned)) ? 1 : 0;
        auto it = g_pzlSeen.find(tail);
        if (it == g_pzlSeen.end()) {
            g_pzlSeen[tail] = val;
        } else if (val == 1 && it->second == 0) {
            it->second = 1;
            g_pzlQ.push_back(PzlEvent{g_nextPzlSeq++, tail, 3, 1, now});
            ++g_pzlTx;
        }
    }

    // S33: Pressure plate doors (BP_PressurePlateOpenDoor_C.Opened)
    for (UObject* pp : ue::FindAllOf("BP_PressurePlateOpenDoor_C")) {
        if (!Live(pp) || Dist(myLoc, net::Loc(pp)) > 3500.0) continue;
        const std::string tail = net::Tail(pp);
        if (tail.empty()) continue;
        int val = ue::GetBool(pp, "Opened") ? 1 : 0;
        auto it = g_pzlSeen.find(tail);
        if (it == g_pzlSeen.end()) {
            g_pzlSeen[tail] = val;
        } else if (val == 1 && it->second == 0) {
            it->second = 1;
            g_pzlQ.push_back(PzlEvent{g_nextPzlSeq++, tail, 1, 1, now});
            ++g_pzlTx;
        }
    }

    // Expire old puzzle events
    while (!g_pzlQ.empty() && now - g_pzlQ.front().at > kTxRepeatSecs) g_pzlQ.pop_front();
    while (g_pzlQ.size() > 12) g_pzlQ.pop_front();
}

void ApplyPeerPuzzle(int seq, const std::string& tail, int kind, int val) {
    if (seq <= g_lastRxPzlSeq || tail.empty()) return;
    g_lastRxPzlSeq = seq;
    g_pzlSeen[tail] = val;

    if (kind == 1 && val == 1) {
        // S33: Replay pressure plate door open
        for (UObject* pp : ue::FindAllOf("BP_PressurePlateOpenDoor_C")) {
            if (!Live(pp) || net::Tail(pp) != tail) continue;
            if (ue::GetBool(pp, "Opened")) return;
            ue::SetBool(pp, "Opened", true);
            for (const char* fnName : {"OpenDoor", "TriggerMovingActor", "OnPressurePlateActivated"}) {
                if (UFunction* fn = ue::FindFunction(pp, fnName)) {
                    ue::Params p(fn);
                    ue::Call(pp, fn, p.data());
                    break;
                }
            }
            ++g_pzlRx;
            net::Log("partb: replayed BP_PressurePlateOpenDoor_C open on %s", tail.c_str());
            return;
        }
    } else if (kind == 3 && val == 1) {
        for (UObject* sp : ue::FindAllOf("BP_Spawner_C")) {
            if (!Live(sp) || net::Tail(sp) != tail) continue;
            UObject* spawned = nullptr;
            if (ue::Get<UObject*>(sp, "SpawnedEnemy", spawned) && Live(spawned)) return;
            if (UFunction* fn = ue::FindFunction(sp, "CustomSpawnMe")) {
                ue::Params p(fn);
                ue::Call(sp, fn, p.data());
                ++g_pzlRx;
                net::Log("partb: replayed BP_Spawner_C::CustomSpawnMe on %s", tail.c_str());
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// 4. Seal verb & non-hit Shell abilities (S9, S18)
// ---------------------------------------------------------------------------
void SampleLocalAbilities() {
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return;

    // Install NG+ safety hook once (S61)
    if (!g_ngpHooked) {
        if (ue::HookPost("BP_PlayerController_C.S_ResetForNewGamePlus", &OnResetForNewGamePlus)) {
            g_ngpHooked = true;
            net::Log("partb: hooked BP_PlayerController_C.S_ResetForNewGamePlus (NG+ guard)");
        }
    }

    // S9: Read equipped Seal item def from PlayerController::GetActiveSealItemDef
    UObject* pc = net::PC();
    if (Live(pc)) {
        if (UFunction* fnSeal = ue::FindFunction(pc, "GetActiveSealItemDef")) {
            ue::Params p(fnSeal);
            if (ue::Call(pc, fnSeal, p.data())) {
                UObject* sealDef = p.Get<UObject*>("ReturnValue");
                if (Live(sealDef)) g_sealName = ue::GetName(sealDef);
            }
        }
    }

    // S18: Detect live Harden stone-form directly from GA_Harden_Original_C::bInStoneForm,
    // plus fallback montage heuristics for Parry / Guard / Slayer / Shell abilities.
    std::string activeAbl;
    for (UObject* ga : ue::FindAllOf("GA_Harden_Original_C")) {
        if (!Live(ga)) continue;
        if (ue::GetBool(ga, "bInStoneForm")) {
            activeAbl = "Seal.Harden";
            break;
        }
    }
    if (activeAbl.empty()) {
        const std::string& mp = state().local.montage.path;
        if (mp.find("Harden") != std::string::npos || mp.find("Stone") != std::string::npos) {
            activeAbl = "Seal.Harden";
        } else if (mp.find("Parry") != std::string::npos) {
            activeAbl = "Seal.Parry";
        } else if (mp.find("Guard") != std::string::npos || mp.find("Block") != std::string::npos) {
            activeAbl = "Seal.Guard";
        } else if (mp.find("Slayer") != std::string::npos) {
            activeAbl = "Seal.Slayer";
        } else if (mp.find("ShellAbility") != std::string::npos) {
            activeAbl = "Shell.Ability";
        }
    }

    const int act = activeAbl.empty() ? 0 : 1;
    if (activeAbl != g_ablName || act != g_ablActive) {
        g_ablName = activeAbl;
        g_ablActive = act;
        ++g_ablSeq;
    }
}

void ApplyPeerAbility(int seq, const std::string& abl, int active) {
    if (seq <= g_lastPeerAblSeq) return;
    g_lastPeerAblSeq = seq;
    UObject* pup = puppet::Actor();
    if (!Live(pup)) return;

    // Harden: freeze puppet animation play rate while stone-hardened, restore when released
    if (abl == "Seal.Harden") {
        UObject* mesh = puppet::Mesh();
        if (Live(mesh)) {
            if (UFunction* fn = ue::FindFunction(mesh, "SetPlayRate")) {
                ue::Params p(fn);
                p.Set<float>("Rate", active ? 0.0f : 1.0f);
                ue::Call(mesh, fn, p.data());
                ++g_ablSynced;
            }
        }
    } else if (active) {
        ++g_ablSynced;
    }
}

}  // namespace

void Tick2() {
    if (!state().toggles.partB) return;
    SampleBossArena();
    SampleDungeonVariants();
    SamplePuzzlesAndSpawners();
    SampleLocalAbilities();
}

void Reset() {
    g_bsaBossTail.clear();
    g_bsaQuirk = 0;
    g_lastPeerBsaSeq = 0;
    g_dngList.clear();
    g_dngApplied.clear();
    g_pzlSeen.clear();
    g_pzlQ.clear();
    g_lastRxPzlSeq = 0;
    g_ablName.clear();
    g_ablActive = 0;
    g_lastPeerAblSeq = 0;
}

void Serialize(std::string* out) {
    if (!out || !state().toggles.partB) return;
    if (!g_bsaBossTail.empty()) {
        *out += net::Fmt(",\"bsa\":[%d,\"%s\",%.0f,%.0f,%.0f,%d]",
                         g_bsaSeq, EscapeStr(g_bsaBossTail).c_str(),
                         g_bsaPos.X, g_bsaPos.Y, g_bsaPos.Z, g_bsaQuirk);
    }
    if (!g_dngList.empty()) {
        *out += ",\"dng\":[";
        for (size_t i = 0; i < g_dngList.size(); ++i) {
            if (i) *out += ',';
            *out += net::Fmt("[\"%s\",\"%s\"]",
                             EscapeStr(g_dngList[i].selectorTail).c_str(),
                             EscapeStr(g_dngList[i].eventPath).c_str());
        }
        *out += "]";
    }
    if (!g_pzlQ.empty()) {
        *out += ",\"pzl\":[";
        bool first = true;
        for (const PzlEvent& e : g_pzlQ) {
            if (!first) *out += ','; first = false;
            *out += net::Fmt("[%d,\"%s\",%d,%d]", e.seq, EscapeStr(e.tail).c_str(), e.kind, e.val);
        }
        *out += "]";
    }
    if (g_ablSeq > 0 || !g_sealName.empty()) {
        *out += net::Fmt(",\"abl\":[%d,\"%s\",%d,\"%s\"]", g_ablSeq, EscapeStr(g_ablName).c_str(), g_ablActive, EscapeStr(g_sealName).c_str());
    }
    if (g_ngpSeq > 0) {
        *out += net::Fmt(",\"ngp\":%d", g_ngpSeq);
    }
}

void OnPeerLine(const std::string& line) {
    if (!state().toggles.partB) return;
    std::string sec;
    double ngpVal = 0.0;
    if (net::KvNum(line, "ngp", &ngpVal)) {
        int seq = (int)ngpVal;
        if (seq > g_lastPeerNgpSeq) {
            g_lastPeerNgpSeq = seq;
            net::Log("partb: WARNING partner triggered New Game Plus (seq=%d) -- local save untouched", seq);
        }
    }
    if (net::Section(line, "bsa", &sec) && sec.size() > 4) {
        auto t = SplitTuple(sec);
        if (t.size() >= 6) {
            ApplyPeerBossArena(std::atoi(t[0].c_str()), t[1],
                               std::strtod(t[2].c_str(), nullptr),
                               std::strtod(t[3].c_str(), nullptr),
                               std::strtod(t[4].c_str(), nullptr),
                               std::atoi(t[5].c_str()));
        }
    }
    if (net::Section(line, "dng", &sec) && sec.size() > 4) {
        size_t pos = 1;
        while (pos < sec.size()) {
            size_t o = sec.find('[', pos), c = sec.find(']', o == std::string::npos ? pos : o);
            if (o == std::string::npos || c == std::string::npos) break;
            auto t = SplitTuple(sec.substr(o, c - o + 1));
            if (t.size() >= 2) ApplyPeerDungeonVariant(t[0], t[1]);
            pos = c + 1;
        }
    }
    if (net::Section(line, "pzl", &sec) && sec.size() > 4) {
        size_t pos = 1;
        while (pos < sec.size()) {
            size_t o = sec.find('[', pos), c = sec.find(']', o == std::string::npos ? pos : o);
            if (o == std::string::npos || c == std::string::npos) break;
            auto t = SplitTuple(sec.substr(o, c - o + 1));
            if (t.size() >= 4) {
                ApplyPeerPuzzle(std::atoi(t[0].c_str()), t[1], std::atoi(t[2].c_str()), std::atoi(t[3].c_str()));
            }
            pos = c + 1;
        }
    }
    if (net::Section(line, "abl", &sec) && sec.size() > 4) {
        auto t = SplitTuple(sec);
        if (t.size() >= 3) {
            ApplyPeerAbility(std::atoi(t[0].c_str()), t[1], std::atoi(t[2].c_str()));
        }
    }
}

std::string Status() {
    return net::Fmt("partb=%s pulls:%d quirks:%d dng:%d pzl:%d/%d abl:%d seal:%s ngp:%d/%d\n",
                    state().toggles.partB ? "on" : "off",
                    g_bossPulls, g_bossQuirksSynced, g_dngSynced,
                    g_pzlTx, g_pzlRx, g_ablSynced,
                    g_sealName.empty() ? "-" : g_sealName.c_str(),
                    g_ngpSeq, g_lastPeerNgpSeq);
}

}  // namespace partb
}  // namespace coop
