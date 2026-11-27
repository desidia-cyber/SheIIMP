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
bool g_hardenFrozen = false;

// --- 5. NG+ / Send Ova safety guard (S61) ---
int g_ngpSeq = 0;
int g_lastPeerNgpSeq = 0;
bool g_ngpHooked = false;
int g_ngpVetoed = 0;

// --- 6. Night Mode sync (S74: BP_DayNight_Interaction_Thestus_C) ---
int g_nmSeq = 0;
int g_nmState = -1; // -1=unknown, 0=day, 1=night
int g_lastPeerNmSeq = 0;
int g_nmSynced = 0;

bool OnResetForNewGamePlusPre(UObject*, UFunction*, void*) {
    if (!state().toggles.partB) return false;
    if (net::FileExists("NGPLUS_GUARD_OFF.txt")) return false;
    const bool peerFresh = state().peer.present && (net::Wall() - state().peer.wallAt <= 6);
    if (peerFresh && state().puppet.phase == Phase::Sync) {
        ++g_ngpVetoed;
        ++g_ngpSeq;
        net::Log("partb: VETOED S_ResetForNewGamePlus in active co-op (vetoed=%d) -- disconnect or create NGPLUS_GUARD_OFF.txt to advance to NG+", g_ngpVetoed);
        return true; // skip/veto the unilateral NG+ reset
    }
    ++g_ngpSeq;
    net::Log("partb: S_ResetForNewGamePlus allowed (solo, seq=%d)", g_ngpSeq);
    return false;
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
        // Check HeadTree underground state via AActor::IsHidden() (immune to bitfield masks)
        bool hidden = false;
        if (UFunction* fnHid = ue::FindFunction(owner, "IsHidden")) {
            ue::Params p(fnHid);
            if (ue::Call(owner, fnHid, p.data())) hidden = p.Get<bool>("ReturnValue");
        } else {
            hidden = ue::GetBool(owner, "bHidden");
        }
        if (hidden) quirk |= 1;
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
        UObject* matchOwner = nullptr;
        UObject* clsFallback = nullptr;
        // Extract class prefix from bossTail (e.g. "BP_DwarfBoss_C" from "BP_DwarfBoss_C_2147...")
        std::string clsPrefix = bossTail;
        size_t cPos = clsPrefix.find("_C_");
        if (cPos != std::string::npos) clsPrefix.resize(cPos + 2);
        for (UObject* ai : ue::FindAllOf("BPC_BossAI_C")) {
            if (!Live(ai)) continue;
            UObject* owner = ue::GetOuter(ai);
            if (!Live(owner)) continue;
            if (net::Tail(owner) == bossTail) { matchOwner = owner; break; }
            if (!clsFallback && !clsPrefix.empty() && ue::GetClassName(owner) == clsPrefix) {
                clsFallback = owner;
            }
        }
        UObject* target = matchOwner ? matchOwner : clsFallback;
        if (Live(target)) {
            if (UFunction* fn = ue::FindFunction(target, "SetActorHiddenInGame")) {
                ue::Params p(fn);
                p.Set<bool>("bNewHidden", (quirk & 1) != 0);
                ue::Call(target, fn, p.data());
                g_lastAppliedQuirk = quirk;
                ++g_bossQuirksSynced;
                net::Log("partb: synced boss underground quirk=%d on %s", quirk & 1, net::Tail(target).c_str());
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

    UObject* evAsset = net::Find(evPath, true);
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

    // S33: Pressure plate doors (BP_PressurePlateOpenDoor_C.Opened -> RaiseWall / CloseWall)
    for (UObject* pp : ue::FindAllOf("BP_PressurePlateOpenDoor_C")) {
        if (!Live(pp) || Dist(myLoc, net::Loc(pp)) > 3500.0) continue;
        const std::string tail = net::Tail(pp);
        if (tail.empty()) continue;
        int val = ue::GetBool(pp, "Opened") ? 1 : 0;
        auto it = g_pzlSeen.find(tail);
        if (it == g_pzlSeen.end()) {
            g_pzlSeen[tail] = val;
        } else if (val != it->second) {
            it->second = val;
            g_pzlQ.push_back(PzlEvent{g_nextPzlSeq++, tail, 1, val, now});
            ++g_pzlTx;
        }
    }

    // S31 & World/Dungeon Elevators: Moving objects, drawbridges, stone gates, non-hub elevators (TriggerMovingActor)
    static const char* const kMovingClasses[] = {
        "BP_Drawbridge_C", "BP_Electric_StoneGate_C", "BP_ProximaUnlockGate_C",
        "BP_Circle_Wooden_Elevator_C", "BP_Grand_Elevator_02_C",
        "BP_Grand_Elevator_Snow_C", "BP_FC_Elevator_C"
    };
    for (const char* mvCls : kMovingClasses) {
        for (UObject* mv : ue::FindAllOf(mvCls)) {
            if (!Live(mv) || Dist(myLoc, net::Loc(mv)) > 4500.0) continue;
            const std::string tail = net::Tail(mv);
            if (tail.empty()) continue;
            int val = ue::GetBool(mv, "IsMoving") ? 1 : 0;
            if (!val) {
                UObject* so = nullptr;
                if (ue::Get<UObject*>(mv, "As", so) && Live(so) && ue::GetBool(so, "MovedToEnd")) val = 2;
            }
            auto it = g_pzlSeen.find(tail);
            if (it == g_pzlSeen.end()) {
                g_pzlSeen[tail] = val;
            } else if (val != 0 && val != it->second) {
                it->second = val;
                g_pzlQ.push_back(PzlEvent{g_nextPzlSeq++, tail, 4, val, now});
                ++g_pzlTx;
            }
        }
    }

    // Hidden / Illusory walls (BP_HiddenWall_C & subclasses: AlreadyRevealed -> SolutionTriggered)
    for (UObject* hw : ue::FindAllOf("BP_HiddenWall_C")) {
        if (!Live(hw) || Dist(myLoc, net::Loc(hw)) > 4500.0) continue;
        const std::string tail = net::Tail(hw);
        if (tail.empty()) continue;
        int val = ue::GetBool(hw, "AlreadyRevealed") ? 1 : 0;
        auto it = g_pzlSeen.find(tail);
        if (it == g_pzlSeen.end()) {
            g_pzlSeen[tail] = val;
        } else if (val == 1 && it->second == 0) {
            it->second = 1;
            g_pzlQ.push_back(PzlEvent{g_nextPzlSeq++, tail, 2, 1, now});
            ++g_pzlTx;
        }
    }

    // Attack-dropped bridges (BP_MakeBridgeByAttack_C.SavedBridge -> SetBridgeLocBeginPlay)
    for (UObject* mb : ue::FindAllOf("BP_MakeBridgeByAttack_C")) {
        if (!Live(mb) || Dist(myLoc, net::Loc(mb)) > 4500.0) continue;
        const std::string tail = net::Tail(mb);
        if (tail.empty()) continue;
        int val = ue::GetBool(mb, "SavedBridge") ? 1 : 0;
        auto it = g_pzlSeen.find(tail);
        if (it == g_pzlSeen.end()) {
            g_pzlSeen[tail] = val;
        } else if (val == 1 && it->second == 0) {
            it->second = 1;
            g_pzlQ.push_back(PzlEvent{g_nextPzlSeq++, tail, 6, 1, now});
            ++g_pzlTx;
        }
    }

    // Bell bridge activators (BP_BellBridgeActivator_C.BellMoving -> MoveBellToOneSide)
    for (UObject* bb : ue::FindAllOf("BP_BellBridgeActivator_C")) {
        if (!Live(bb) || Dist(myLoc, net::Loc(bb)) > 4500.0) continue;
        const std::string tail = net::Tail(bb);
        if (tail.empty()) continue;
        int val = ue::GetBool(bb, "BellMoving") ? 1 : 0;
        auto it = g_pzlSeen.find(tail);
        if (it == g_pzlSeen.end()) {
            g_pzlSeen[tail] = val;
        } else if (val == 1 && it->second == 0) {
            it->second = 1;
            g_pzlQ.push_back(PzlEvent{g_nextPzlSeq++, tail, 7, 1, now});
            ++g_pzlTx;
        }
    }

    // Vlas's cage at Lone Spire (BP_Vlas_Cage_01_C.IsOpen)
    for (UObject* vc : ue::FindAllOf("BP_Vlas_Cage_01_C")) {
        if (!Live(vc) || Dist(myLoc, net::Loc(vc)) > 4500.0) continue;
        const std::string tail = net::Tail(vc);
        if (tail.empty()) continue;
        int val = ue::GetBool(vc, "IsOpen") ? 1 : 0;
        auto it = g_pzlSeen.find(tail);
        if (it == g_pzlSeen.end()) {
            g_pzlSeen[tail] = val;
        } else if (val == 1 && it->second == 0) {
            it->second = 1;
            g_pzlQ.push_back(PzlEvent{g_nextPzlSeq++, tail, 5, 1, now});
            ++g_pzlTx;
        }
    }

    // S74: Night Mode (Host authoritative via BP_DayNight_Interaction_Thestus_C.bInNightMode)
    if (state().local.role == "host") {
        for (UObject* th : ue::FindAllOf("BP_DayNight_Interaction_Thestus_C")) {
            if (!Live(th)) continue;
            int nm = ue::GetBool(th, "bInNightMode") ? 1 : 0;
            if (nm != g_nmState) {
                g_nmState = nm;
                ++g_nmSeq;
                net::Log("partb: host NightMode=%d (seq=%d)", nm, g_nmSeq);
            }
            break;
        }
    }

    // Expire old puzzle events
    while (!g_pzlQ.empty() && now - g_pzlQ.front().at > kTxRepeatSecs) g_pzlQ.pop_front();
    while (g_pzlQ.size() > 12) g_pzlQ.pop_front();
}

void ApplyPeerNightMode(int seq, int wantNight) {
    if (seq <= g_lastPeerNmSeq || state().local.role == "host") return;
    g_lastPeerNmSeq = seq;
    for (UObject* th : ue::FindAllOf("BP_DayNight_Interaction_Thestus_C")) {
        if (!Live(th)) continue;
        int cur = ue::GetBool(th, "bInNightMode") ? 1 : 0;
        if (cur == wantNight) return;
        if (UFunction* fnEn = ue::FindFunction(th, "EnableThestus")) {
            ue::Params pe(fnEn);
            ue::Call(th, fnEn, pe.data());
        }
        if (UFunction* fn = ue::FindFunction(th, "togglenightmode")) {
            ue::Params p(fn);
            if (ue::Call(th, fn, p.data())) {
                int after = ue::GetBool(th, "bInNightMode") ? 1 : 0;
                if (after != wantNight) {
                    ue::SetBool(th, "bInNightMode", wantNight != 0);
                    if (UFunction* fnUpd = ue::FindFunction(th, "UpdateNightModeState")) {
                        ue::Params pu(fnUpd);
                        ue::Call(th, fnUpd, pu.data());
                    }
                    after = ue::GetBool(th, "bInNightMode") ? 1 : 0;
                }
                ++g_nmSynced;
                net::Log("partb: synced NightMode -> %d via togglenightmode() (bInNightMode=%d)", wantNight, after);
            }
        }
        return;
    }
}

void ApplyPeerPuzzle(int seq, const std::string& tail, int kind, int val) {
    if (seq <= g_lastRxPzlSeq || tail.empty()) return;
    g_lastRxPzlSeq = seq;
    g_pzlSeen[tail] = val;

    if (kind == 1) {
        // S33: Replay pressure plate door open/close using the real UFunctions
        for (UObject* pp : ue::FindAllOf("BP_PressurePlateOpenDoor_C")) {
            if (!Live(pp) || net::Tail(pp) != tail) continue;
            const bool wantOpen = (val != 0);
            if (ue::GetBool(pp, "Opened") == wantOpen) return;
            ue::SetBool(pp, "Opened", wantOpen);
            const char* fnName = wantOpen ? "RaiseWall" : "CloseWall";
            if (UFunction* fn = ue::FindFunction(pp, fnName)) {
                ue::Params p(fn);
                ue::Call(pp, fn, p.data());
            }
            ++g_pzlRx;
            net::Log("partb: replayed BP_PressurePlateOpenDoor_C::%s on %s", fnName, tail.c_str());
            return;
        }
    } else if (kind == 2 && val == 1) {
        // Hidden / Illusory wall reveal (BP_HiddenWall_C::SolutionTriggered + DisableCollision)
        for (UObject* hw : ue::FindAllOf("BP_HiddenWall_C")) {
            if (!Live(hw) || net::Tail(hw) != tail) continue;
            if (ue::GetBool(hw, "AlreadyRevealed")) return;
            if (UFunction* fn = ue::FindFunction(hw, "SolutionTriggered")) {
                ue::Params p(fn);
                ue::Call(hw, fn, p.data());
            }
            if (!ue::GetBool(hw, "AlreadyRevealed")) {
                ue::SetBool(hw, "AlreadyRevealed", true);
                if (UFunction* fnDis = ue::FindFunction(hw, "DisableCollision")) {
                    ue::Params pd(fnDis);
                    ue::Call(hw, fnDis, pd.data());
                }
            }
            ++g_pzlRx;
            net::Log("partb: replayed BP_HiddenWall_C::SolutionTriggered on %s", tail.c_str());
            return;
        }
    } else if (kind == 4) {
        // S31 & World/Dungeon Elevators: Replay via BPI_LiftCall::TriggerMovingActor
        static const char* const kMovingClasses[] = {
            "BP_Drawbridge_C", "BP_Electric_StoneGate_C", "BP_ProximaUnlockGate_C",
            "BP_Circle_Wooden_Elevator_C", "BP_Grand_Elevator_02_C",
            "BP_Grand_Elevator_Snow_C", "BP_FC_Elevator_C"
        };
        for (const char* mvCls : kMovingClasses) {
            for (UObject* mv : ue::FindAllOf(mvCls)) {
                if (!Live(mv) || net::Tail(mv) != tail) continue;
                if (UFunction* fn = ue::FindFunction(mv, "TriggerMovingActor")) {
                    ue::Params p(fn);
                    p.Set<bool>("TopLevelLever", val == 1);
                    ue::Call(mv, fn, p.data());
                    ++g_pzlRx;
                    net::Log("partb: replayed %s::TriggerMovingActor on %s", mvCls, tail.c_str());
                }
                return;
            }
        }
    } else if (kind == 6 && val == 1) {
        // Attack-dropped bridge (BP_MakeBridgeByAttack_C::SetBridgeLocBeginPlay)
        for (UObject* mb : ue::FindAllOf("BP_MakeBridgeByAttack_C")) {
            if (!Live(mb) || net::Tail(mb) != tail) continue;
            if (ue::GetBool(mb, "SavedBridge")) return;
            ue::SetBool(mb, "SavedBridge", true);
            if (UFunction* fn = ue::FindFunction(mb, "SetBridgeLocBeginPlay")) {
                ue::Params p(fn);
                ue::Call(mb, fn, p.data());
            }
            ++g_pzlRx;
            net::Log("partb: replayed BP_MakeBridgeByAttack_C::SetBridgeLocBeginPlay on %s", tail.c_str());
            return;
        }
    } else if (kind == 7 && val == 1) {
        // Bell bridge activator (BP_BellBridgeActivator_C::MoveBellToOneSide)
        for (UObject* bb : ue::FindAllOf("BP_BellBridgeActivator_C")) {
            if (!Live(bb) || net::Tail(bb) != tail) continue;
            if (UFunction* fn = ue::FindFunction(bb, "MoveBellToOneSide")) {
                ue::Params p(fn);
                ue::Call(bb, fn, p.data());
            }
            ++g_pzlRx;
            net::Log("partb: replayed BP_BellBridgeActivator_C::MoveBellToOneSide on %s", tail.c_str());
            return;
        }
    } else if (kind == 5 && val == 1) {
        // Vlas's cage at Lone Spire (BP_Vlas_Cage_01_C.IsOpen)
        for (UObject* vc : ue::FindAllOf("BP_Vlas_Cage_01_C")) {
            if (!Live(vc) || net::Tail(vc) != tail) continue;
            if (ue::GetBool(vc, "IsOpen")) return;
            ue::SetBool(vc, "IsOpen", true);
            if (UFunction* fn = ue::FindFunction(vc, "GrishaCageDoorUnlocked__DelegateSignature")) {
                ue::Params p(fn);
                ue::Call(vc, fn, p.data());
            }
            ++g_pzlRx;
            net::Log("partb: synced BP_Vlas_Cage_01_C.IsOpen=true on %s", tail.c_str());
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

    // Install NG+ safety veto hook once (S61)
    if (!g_ngpHooked) {
        if (ue::HookPreSkipName("S_ResetForNewGamePlus", &OnResetForNewGamePlusPre)) {
            g_ngpHooked = true;
            net::Log("partb: hooked S_ResetForNewGamePlus (NG+ co-op veto guard)");
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

    // Harden: freeze puppet animation play rate while stone-hardened, ALWAYS restore on release
    const bool wantFreeze = (abl == "Seal.Harden" && active != 0);
    if (wantFreeze != g_hardenFrozen) {
        UObject* mesh = puppet::Mesh();
        if (Live(mesh)) {
            if (UFunction* fn = ue::FindFunction(mesh, "SetPlayRate")) {
                ue::Params p(fn);
                p.Set<float>("Rate", wantFreeze ? 0.0f : 1.0f);
                ue::Call(mesh, fn, p.data());
                g_hardenFrozen = wantFreeze;
                ++g_ablSynced;
                net::Log("partb: puppet Harden %s (SetPlayRate=%.1f)", wantFreeze ? "FREEZE" : "UNFREEZE", wantFreeze ? 0.0f : 1.0f);
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
    g_hardenFrozen = false;
    g_nmState = -1;
    g_lastPeerNmSeq = 0;
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
    if (g_nmSeq > 0 && g_nmState >= 0) {
        *out += net::Fmt(",\"nm\":[%d,%d]", g_nmSeq, g_nmState);
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
    if (net::Section(line, "nm", &sec) && sec.size() > 2) {
        auto t = SplitTuple(sec);
        if (t.size() >= 2) {
            ApplyPeerNightMode(std::atoi(t[0].c_str()), std::atoi(t[1].c_str()));
        }
    }
}

std::string Status() {
    return net::Fmt("partb=%s pulls:%d quirks:%d dng:%d pzl:%d/%d abl:%d seal:%s ngp:%d/%d/%d nm:%d/%d\n",
                    state().toggles.partB ? "on" : "off",
                    g_bossPulls, g_bossQuirksSynced, g_dngSynced,
                    g_pzlTx, g_pzlRx, g_ablSynced,
                    g_sealName.empty() ? "-" : g_sealName.c_str(),
                    g_ngpSeq, g_lastPeerNgpSeq, g_ngpVetoed,
                    g_nmState, g_nmSynced);
}

bool OnCommand(const std::string& cmd, const std::string& rest) {
    if (cmd != "partbtest") return false;
    std::string sub = rest;
    while (!sub.empty() && (unsigned char)sub.front() <= ' ') sub.erase(sub.begin());
    while (!sub.empty() && (unsigned char)sub.back() <= ' ') sub.pop_back();

    auto ovumInvCount = []() -> int {
        UObject* pc = net::PC();
        if (!Live(pc)) return -1;
        UObject* im = nullptr;
        for (UObject* c : ue::FindAllOf("SpartaItemManagerComponent")) {
            if (Live(c) && ue::GetOuter(c) == pc) { im = c; break; }
        }
        if (!Live(im)) {
            auto all = ue::FindAllOf("SpartaItemManagerComponent");
            for (UObject* c : all) { if (Live(c)) { im = c; break; } }
        }
        if (!Live(im)) return -1;
        UObject* def = net::Find("/Game/Sparta/Items/Pickups/Core/ID_Ovum.ID_Ovum_C", true);
        if (!Live(def)) return -2;
        if (UFunction* fn = ue::FindFunction(im, "GetTotalItemCountByDefinition")) {
            ue::Params p(fn);
            p.Set<UObject*>("ItemDef", def);
            if (ue::Call(im, fn, p.data())) return (int)p.Get<int32_t>("ReturnValue");
        }
        return -3;
    };

    if (sub == "dump" || sub.empty()) {
        auto ths = ue::FindAllOf("BP_DayNight_Interaction_Thestus_C");
        auto ovs = ue::FindAllOf("BPSO_Ovum_C");
        auto fbms = ue::FindAllOf("BPSO_FC_FinalBossManager_C");
        auto vcs = ue::FindAllOf("BP_Vlas_Cage_01_C");
        auto sels = ue::FindAllOf("BP_LevelInstanceSelector_C");
        int loadedSels = 0;
        for (UObject* s : sels) if (Live(s) && ue::GetBool(s, "bIsLevelLoaded")) ++loadedSels;
        int nmVal = -1;
        for (UObject* t : ths) if (Live(t)) { nmVal = ue::GetBool(t, "bInNightMode") ? 1 : 0; break; }
        int32_t fbmCores = -1;
        for (UObject* f : fbms) {
            if (Live(f) && net::FullName(f).find("PersistentLevel.") != std::string::npos) {
                ue::Get<int32_t>(f, "ClensedCores", fbmCores);
                break;
            }
        }
        int32_t egonDon = -1;
        int32_t gorfRel = -1, vlasRel = -1, thestusRel = -1, genessaRel = -1;
        int32_t brigVal = -1, wormVal = -1;
        UObject* ql = ue::FindFirstOf("BPC_QuestLogComponent_C");
        if (Live(ql)) {
            ue::Get<int32_t>(ql, "BlisterBoiDonations", egonDon);
            ue::MapWalkStrInt(ql, "NPCRelationships", [&](const char* k, int32_t v) {
                if (std::strcmp(k, "Gorf") == 0) gorfRel = v;
                else if (std::strcmp(k, "Vlas") == 0) vlasRel = v;
                else if (std::strcmp(k, "Thestus") == 0) thestusRel = v;
                else if (std::strcmp(k, "Sester Genessa") == 0) genessaRel = v;
                return true;
            });
            ue::MapWalkStrInt(ql, "ImmortalBrigSpoils", [&](const char* k, int32_t v) {
                if (std::strcmp(k, "RIG_BRIG") == 0) brigVal = v;
                return true;
            });
            ue::MapWalkStrInt(ql, "WormfishGold", [&](const char* k, int32_t v) {
                if (std::strcmp(k, "RIG_WORM") == 0) wormVal = v;
                return true;
            });
        }
        net::Log("partbtest dump: thestus=%d(nm=%d) ovumSO=%d ovumInv=%d fbm=%d(cores=%d) vlasCage=%d sels=%d(loaded=%d) egonDon=%d rel(gorf=%d,vlas=%d,thestus=%d,gen=%d) brig=%d worm=%d",
                 (int)ths.size(), nmVal, (int)ovs.size(), ovumInvCount(), (int)fbms.size(), (int)fbmCores,
                 (int)vcs.size(), (int)sels.size(), loadedSels, (int)egonDon,
                 (int)gorfRel, (int)vlasRel, (int)thestusRel, (int)genessaRel, (int)brigVal, (int)wormVal);
        return true;
    }

    if (sub == "thestus_load") {
        const char* thPath = "/Game/Sparta/Core/Interaction/BP_DayNight_Interaction_Thestus.BP_DayNight_Interaction_Thestus_C";
        UObject* cls = net::Find(thPath, true);
        net::Log("partbtest thestus_load: loaded=%d", Live(cls) ? 1 : 0);
        return true;
    }

    if (sub == "thestus_spawn") {
        const char* thPath = "/Game/Sparta/Core/Interaction/BP_DayNight_Interaction_Thestus.BP_DayNight_Interaction_Thestus_C";
        UObject* cls = net::Find(thPath, true);
        UObject* world = net::World();
        UObject* pawn = net::Pawn();
        ue::UClass* gsCls = ue::FindClass("GameplayStatics");
        UObject* gsCdo = gsCls ? ue::GetCDO(gsCls) : nullptr;
        UFunction* fnBegin = Live(gsCdo) ? ue::FindFunction(gsCdo, "BeginDeferredActorSpawnFromClass") : nullptr;
        UFunction* fnFin   = Live(gsCdo) ? ue::FindFunction(gsCdo, "FinishSpawningActor") : nullptr;
        if (Live(cls) && Live(world) && Live(pawn) && fnBegin && fnFin) {
            Vec3 loc = net::Loc(pawn);
            ue::FTransform xf{};
            xf.Rotation.W = 1.0;
            xf.Translation = ue::FVector{loc.X + 220.0, loc.Y, loc.Z};
            xf.Scale3D = ue::FVector{1.0, 1.0, 1.0};
            ue::Params pb(fnBegin);
            pb.Set<UObject*>("WorldContextObject", world);
            pb.Set<UObject*>("ActorClass", cls);
            pb.Set<ue::FTransform>("SpawnTransform", xf);
            pb.Set<uint8_t>("CollisionHandlingOverride", 1);
            if (ue::Call(gsCdo, fnBegin, pb.data())) {
                UObject* spawned = pb.Get<UObject*>("ReturnValue");
                if (Live(spawned)) {
                    ue::SetBool(spawned, "StartUnlocked", true);
                    ue::Params pf(fnFin);
                    pf.Set<UObject*>("Actor", spawned);
                    pf.Set<ue::FTransform>("SpawnTransform", xf);
                    ue::Call(gsCdo, fnFin, pf.data());
                }
            }
        }
        auto ths = ue::FindAllOf("BP_DayNight_Interaction_Thestus_C");
        net::Log("partbtest thestus_spawn: count=%d", (int)ths.size());
        return true;
    }

    if (sub == "thestus") {
        auto ths = ue::FindAllOf("BP_DayNight_Interaction_Thestus_C");
        if (ths.empty()) {
            net::Log("partbtest thestus: 0 BP_DayNight_Interaction_Thestus_C live");
            return true;
        }
        UObject* th = ths[0];
        if (!Live(th)) { net::Log("partbtest thestus: invalid"); return true; }
        int before = ue::GetBool(th, "bInNightMode") ? 1 : 0;
        if (UFunction* fnEn = ue::FindFunction(th, "EnableThestus")) {
            ue::Params p(fnEn);
            ue::Call(th, fnEn, p.data());
        }
        bool ok = false;
        if (UFunction* fn = ue::FindFunction(th, "togglenightmode")) {
            ue::Params p(fn);
            ok = ue::Call(th, fn, p.data());
        }
        int after = ue::GetBool(th, "bInNightMode") ? 1 : 0;
        if (after == before) {
            ue::SetBool(th, "bInNightMode", before == 0);
            if (UFunction* fnUpd = ue::FindFunction(th, "UpdateNightModeState")) {
                ue::Params pu(fnUpd);
                ue::Call(th, fnUpd, pu.data());
            }
            after = ue::GetBool(th, "bInNightMode") ? 1 : 0;
        }
        net::Log("partbtest thestus: togglenightmode ok=%d bInNightMode %d->%d", ok ? 1 : 0, before, after);
        return true;
    }

    if (sub == "headtree_hide" || sub == "headtree_show") {
        const bool hide = (sub == "headtree_hide");
        for (UObject* ai : ue::FindAllOf("BPC_BossAI_C")) {
            if (!Live(ai)) continue;
            UObject* owner = ue::GetOuter(ai);
            if (!Live(owner)) continue;
            ue::SetBool(ai, "bIsAggroed", true);
            if (UFunction* fn = ue::FindFunction(owner, "SetActorHiddenInGame")) {
                ue::Params p(fn);
                p.Set<bool>("bNewHidden", hide);
                ue::Call(owner, fn, p.data());
            }
            net::Log("partbtest %s: boss=%s bHidden=%d", sub.c_str(), net::Tail(owner).c_str(),
                     ue::GetBool(owner, "bHidden") ? 1 : 0);
            return true;
        }
        net::Log("partbtest %s: no live BPC_BossAI_C", sub.c_str());
        return true;
    }

    if (sub == "tomb") {
        UObject* pawn = net::Pawn();
        const Vec3 me = Live(pawn) ? net::Loc(pawn) : Vec3{};
        UObject* best = nullptr;
        double bestD = 1e30;
        for (UObject* s : ue::FindAllOf("BP_LevelInstanceSelector_C")) {
            if (!Live(s)) continue;
            double d = Dist(me, net::Loc(s));
            if (d < bestD) { bestD = d; best = s; }
        }
        if (!Live(best)) { net::Log("partbtest tomb: no BP_LevelInstanceSelector_C"); return true; }
        const char* evPath = "/Game/Sparta/Core/World/Levels/Events/Event_TombLayout_01_Event_01_VampireCrypt.Event_TombLayout_01_Event_01_VampireCrypt";
        UObject* ev = net::Find(evPath, true);
        if (!Live(ev)) { net::Log("partbtest tomb: failed to load %s", evPath); return true; }
        bool ok = false;
        if (UFunction* fn = ue::FindFunction(best, "LoadLevel")) {
            ue::Params p(fn);
            p.Set<UObject*>("SelectedEvent", ev);
            ok = ue::Call(best, fn, p.data());
        }
        UObject* lastEv = nullptr;
        ue::Get<UObject*>(best, "LastEventLoaded", lastEv);
        if (!Live(lastEv)) ue::Set<UObject*>(best, "LastEventLoaded", ev);
        if (!ue::GetBool(best, "bIsLevelLoaded")) ue::SetBool(best, "bIsLevelLoaded", true);
        net::Log("partbtest tomb: LoadLevel(%s) on %s (d=%.0f) ok=%d loaded=%d lastEv=%d",
                 evPath, net::Tail(best).c_str(), bestD, ok ? 1 : 0,
                 ue::GetBool(best, "bIsLevelLoaded") ? 1 : 0, Live(lastEv) ? 1 : 0);
        return true;
    }

    if (sub == "ovum_reset") {
        int n = 0;
        for (UObject* o : ue::FindAllOf("BPSO_Ovum_C")) {
            if (!Live(o)) continue;
            std::string fn = net::FullName(o);
            if (fn.find("PersistentLevel.") == std::string::npos) continue;
            ue::SetBool(o, "OvumCollected", false);
            net::Log("partbtest ovum_reset: set OvumCollected=false on %s", fn.c_str());
            ++n;
            break;
        }
        if (n == 0) net::Log("partbtest ovum_reset: 0 PersistentLevel BPSO_Ovum_C");
        return true;
    }

    if (sub == "ovum") {
        int inv = ovumInvCount();
        for (UObject* o : ue::FindAllOf("BPSO_Ovum_C")) {
            if (!Live(o)) continue;
            std::string fn = net::FullName(o);
            if (fn.find("PersistentLevel.") == std::string::npos) continue;
            bool col = ue::GetBool(o, "OvumCollected");
            if (col) continue;
            ue::SetBool(o, "OvumCollected", true);
            net::Log("partbtest ovum: flipped OvumCollected=true on %s (localInv=%d)", fn.c_str(), inv);
            return true;
        }
        net::Log("partbtest ovum: no uncollected PersistentLevel BPSO_Ovum_C found (localInv=%d)", inv);
        return true;
    }

    if (sub == "finalboss") {
        for (UObject* f : ue::FindAllOf("BPSO_FC_FinalBossManager_C")) {
            if (!Live(f)) continue;
            std::string fn = net::FullName(f);
            if (fn.find("PersistentLevel.") == std::string::npos) continue;
            int32_t cur = 0;
            ue::Get<int32_t>(f, "ClensedCores", cur);
            ue::Set<int32_t>(f, "ClensedCores", cur + 1);
            net::Log("partbtest finalboss: ClensedCores %d->%d on %s", (int)cur, (int)(cur + 1),
                     fn.c_str());
            return true;
        }
        net::Log("partbtest finalboss: 0 PersistentLevel BPSO_FC_FinalBossManager_C live");
        return true;
    }

    if (sub == "quest13") {
        UObject* ql = ue::FindFirstOf("BPC_QuestLogComponent_C");
        if (!Live(ql)) { net::Log("partbtest quest13: no BPC_QuestLogComponent_C"); return true; }
        // 1. Egon BlisterBoiDonations
        int32_t curDon = 0;
        ue::Get<int32_t>(ql, "BlisterBoiDonations", curDon);
        ue::Set<int32_t>(ql, "BlisterBoiDonations", curDon + 250);
        // 2. NPC Relationships across core quest NPCs
        ue::MapInsertStrInt(ql, "NPCRelationships", "Gorf", 3);
        ue::MapInsertStrInt(ql, "NPCRelationships", "Vlas", 2);
        ue::MapInsertStrInt(ql, "NPCRelationships", "Thestus", 2);
        ue::MapInsertStrInt(ql, "NPCRelationships", "Sester Genessa", 4);
        // 3. Extra quest maps (ImmortalBrigSpoils & WormfishGold)
        ue::MapInsertStrInt(ql, "ImmortalBrigSpoils", "RIG_BRIG", 7);
        ue::MapInsertStrInt(ql, "WormfishGold", "RIG_WORM", 42);
        net::Log("partbtest quest13: set BlisterBoiDonations=%d, rel(Gorf=3,Vlas=2,Thestus=2,Genessa=4), brig=7, worm=42",
                 (int)(curDon + 250));
        return true;
    }

    net::Log("partbtest: unknown subcmd '%s'", sub.c_str());
    return true;
}

}  // namespace partb
}  // namespace coop
