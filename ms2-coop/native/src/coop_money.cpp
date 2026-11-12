// coop_money.cpp -- [R8-MONEY] the player's own death drop: the lost-Gloom spoils.
//
// THE MECHANIC, from the data mine and the reflection dump (SYNC-DATA-MINE 9.4,
// topic-08-economy; research\raw\CXXHeaderDump\{GA_DeathSpoilsHandler,BP_DeathSpoils}.hpp):
//
//   * on a death, GA_DeathSpoilsHandler_C runs GetGloom (the item manager's stack
//     count for ID_Gloom_C) and, when it is > 0, RemoveGloom (takes ALL of it),
//     then SaveLostGloomData writes the lost-gloom slot of
//     BP_SpartaPlayerSaveObject_C -- `bHasLostGloom @0x560`, `LostGloomAmount
//     @0x578`, `LostGloomLocation @0x580`, `LostGloomBiome @0x598` (a single slot:
//     a new death OVERWRITES it, which is the game's "old drop replaced" rule);
//   * SpawnLostGloom then spawns ONE ABP_DeathSpoils_C at the spot (the location
//     comes from an EQS query, so it is a short walk from the death, never the
//     exact body position), with `Amount @0x308`;
//   * the bag's interaction handler (BPC_InteractionHandler_C, bound event
//     BndEvt__..._OnInteraction) runs GiveGloom (S_AddGloom(Amount)),
//     ResetSaveData (clears the save slot) and destroys the actor.
//
// WHY THIS MODULE EXISTS.  In vanilla that actor is destroyed by the world RELOAD
// that every death performs, and a fresh one is spawned from the save slot after
// the load.  A co-op case-2 death (coop_death.cpp) deliberately does NOT reload,
// so the answers are not free: is the bag still there after the in-place respawn,
// is it visible, can THAT player collect it, does it pay back exactly the lost
// amount, does a second death still replace the old drop, and does the partner
// stay out of it (currency is per player and the bag is a client-local actor)?
//
// WHAT IT DOES: measures all of that from the game's own numbers (2 samples a
// second: the wallet's Gloom stack count, the save slot, and every
// BP_DeathSpoils_C in the world with class, location and Amount), and enforces
// the ONE rule the missing reload would have enforced -- when a death rewrites the
// lost-gloom slot, every bag that is not the one the slot now describes is
// destroyed (the old drop is replaced/lost, exactly as in vanilla).  Nothing else
// is written.
//
// Kill switch: coop\MONEY_OFF.txt (no scan, no sweep, no "money=" line).
//
// Verbs: moneyprobe [secs] / moneydump / moneyget [radius] / moneyview.

#include "coop_state.h"

#include "ms2_log.h"
#include "ms2_ue.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// winuser.h turns GetClassName into GetClassNameA, which would rename ue::'s
// function at the point of use (the same #undef every other module has).
#undef GetClassName

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace coop {
namespace money {

namespace {

using ue::UObject;
using ue::UFunction;

// The one actor class this lane cares about.  GA_DeathSpoilsHandler is the ONLY
// spawner of it (grep over the reflection dump: BP_DeathSpoils is referenced by
// that ability alone), so every instance in a world is a player's lost drop.
constexpr const char* kSpoilsClass = "BP_DeathSpoils_C";
constexpr const char* kSpoilsEvent =
    "BndEvt__BP_DeathSpoils_BPC_InteractionHandler_K2Node_ComponentBoundEvent_0_"
    "OnInteraction__DelegateSignature";
// The slots of BP_SpartaPlayerSaveObject_C (SYNC-DATA-MINE 9.4), read by NAME so
// the widths come from the reflection rather than from this file.
constexpr const char* kHasLost   = "bHasLostGloom";
constexpr const char* kLostAmt   = "LostGloomAmount";
constexpr const char* kLostLoc   = "LostGloomLocation";

// How long after the death the save slot is allowed to settle before the sweep
// reads it.  The ability writes the slot at the death and spawns the bag right
// after; 1.2 s is several 2 Hz samples past that and far short of the 10 s respawn
// delay, so the sweep always runs while the player is still down.
constexpr double kSweepDelay = 1.2;
// A bag further than this from the save slot's own location is not the drop the
// slot describes.  The EQS picks the spawn point, so the bag is never far from the
// recorded spot; 400 uu is generous against an EQS donut radius and still much
// smaller than the distance between two death spots.
constexpr double kSameSpotUu = 400.0;
// How long a death keeps the lane in its hot (2 Hz scan, 2 Hz wallet) mode.
constexpr double kWatchSecs = 180.0;
// Out of watch mode the world is still scanned, just slowly: a bag that appears
// from another cause (a load restoring the slot) must not be invisible to us.
constexpr int kNtickIdleScanEvery = 10;

MoneyState& M() { return state().money; }

// Defined with the sweep/restore code further down; the sweep needs them first.
int DestroyAllDrops(const char* why);
bool WriteLostGloom(int amount, const Vec3& at, const char* why);

// How many 2 Hz calls this lane has seen: the only clock it needs to decide
// "sample now" deterministically (net::Now() is the game's seconds, which is not a
// tick counter).
long g_ticks = 0;

bool Live(UObject* o) { return o != nullptr && ue::IsValid(o); }

bool On() { return toggles().moneyEnabled; }

// ---------------------------------------------------------------------------
// guarded reflection helpers (this module's own copies: every module keeps its
// own -- coop_net's `uecall` is private to its translation unit)
// ---------------------------------------------------------------------------
struct FuncSlot {
    std::string key;
    UFunction* fn = nullptr;
};

std::vector<FuncSlot> g_fnCache;

UFunction* Func(UObject* o, const char* name) {
    if (!o || !name) return nullptr;
    const std::string key = ue::GetClassName(o) + "|" + name;
    for (const FuncSlot& s : g_fnCache) {
        if (s.key == key) return s.fn;
    }
    UFunction* fn = ue::FindFunction(o, name);
    FuncSlot slot;
    slot.key = key;
    slot.fn = fn;   // a miss is cached too: no MISS storms
    g_fnCache.push_back(slot);
    return fn;
}

bool CallVoid(UObject* o, const char* fnName) {
    if (!Live(o)) return false;
    UFunction* fn = Func(o, fnName);
    if (!fn) return false;
    ue::Params p(fn);
    return ue::Call(o, fn, p.data());
}

bool CallBool(UObject* o, const char* fnName, bool* out) {
    if (out) *out = false;
    if (!Live(o)) return false;
    UFunction* fn = Func(o, fnName);
    if (!fn) return false;
    ue::Params p(fn);
    if (!ue::Call(o, fn, p.data())) return false;
    if (out) *out = p.Get<bool>("ReturnValue");
    return true;
}

UObject* CallObject(UObject* o, const char* fnName) {
    if (!Live(o)) return nullptr;
    UFunction* fn = Func(o, fnName);
    if (!fn) return nullptr;
    ue::Params p(fn);
    if (!ue::Call(o, fn, p.data())) return nullptr;
    UObject* r = p.Get<UObject*>("ReturnValue");
    return Live(r) ? r : nullptr;
}

bool PropObj(UObject* o, const char* prop, UObject** out) {
    if (out) *out = nullptr;
    if (!Live(o)) return false;
    UObject* p = nullptr;
    if (!ue::Get<UObject*>(o, prop, p)) return false;
    if (!Live(p)) return false;
    if (out) *out = p;
    return true;
}

int ReadInt(UObject* o, const char* prop) {
    std::int32_t v = 0;
    if (!Live(o)) return -1;
    if (!ue::Get<std::int32_t>(o, prop, v)) return -1;
    return v;
}

// ---------------------------------------------------------------------------
// the wallet: the carried Gloom
//
// Gloom is an ITEM STACK in this game (the same reading coop_drops.cpp uses for
// gold/gloom), so the balance is GetTotalItemCountByDefinition(ID_Gloom_C) on the
// pawn's SpartaItemManagerComponent -- not a property on the save object (the save
// only carries the cumulative counters).
// ---------------------------------------------------------------------------
struct Wallet {
    int gold = -1;      // item COUNT (stacks) -- GetTotalItemCountByDefinition
    int gloom = -1;
    int goldSum = -1;   // the SUM the death itself uses -- GetItemStacksByClass
    int gloomSum = -1;
};

UObject* CurrencyDef(const char* cls, const char* path, UObject** slot) {
    if (*slot && Live(*slot)) return *slot;
    *slot = nullptr;
    UObject* o = nullptr;
    if (path && *path) o = ue::FindObject(path);
    if (!Live(o) && cls && *cls) o = reinterpret_cast<UObject*>(ue::FindClass(cls));
    if (Live(o)) *slot = o;
    return Live(o) ? o : nullptr;
}

UObject* GloomDef() {
    static UObject* slot = nullptr;
    return CurrencyDef("ID_Gloom_C", "/Game/Sparta/Items/Pickups/ID_Gloom.ID_Gloom_C", &slot);
}

UObject* GoldDef() {
    static UObject* slot = nullptr;
    return CurrencyDef("ID_Gold_C", "/Game/Sparta/Items/Pickups/ID_Gold.ID_Gold_C", &slot);
}

UObject* ItemManager() {
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return nullptr;
    static UObject* mgrClass = nullptr;
    if (!Live(mgrClass)) mgrClass = net::Find("/Script/Sparta.SpartaItemManagerComponent", false);
    if (!Live(mgrClass)) return nullptr;
    UFunction* f = ue::FindFunction(pawn, "GetComponentByClass");
    if (!f) return nullptr;
    ue::Params p(f);
    p.Set<ue::UClass*>("ComponentClass", reinterpret_cast<ue::UClass*>(mgrClass));
    if (!ue::Call(pawn, f, p.data())) return nullptr;
    UObject* im = p.Get<UObject*>("ReturnValue");
    return Live(im) ? im : nullptr;
}

int ItemCount(UObject* im, UObject* def) {
    if (!Live(im) || !Live(def)) return -1;
    UFunction* f = ue::FindFunction(im, "GetTotalItemCountByDefinition");
    if (!f) return -1;
    ue::Params p(f);
    p.Set<ue::UClass*>("ItemDef", reinterpret_cast<ue::UClass*>(def));
    if (!ue::Call(im, f, p.data())) return -1;
    return p.Get<std::int32_t>("ReturnValue");
}

// MEASURED (this round, run 1): GetTotalItemCountByDefinition counts STACKS -- one
// `dropsgrant 0 500` moved it by 1, and a death that emptied the wallet moved it by
// -2.  The number the death itself works with is `GetItemStacksByClass` (that is
// exactly what GA_DeathSpoilsHandler_C:GetGloom calls, see the reflection dump), so
// the lane reads BOTH and compares the death against the sum.
int ItemSum(UObject* im, UObject* def) {
    if (!Live(im) || !Live(def)) return -1;
    UFunction* f = ue::FindFunction(im, "GetItemStacksByClass");
    if (!f) return -1;
    ue::Params p(f);
    p.Set<ue::UClass*>("ItemDef", reinterpret_cast<ue::UClass*>(def));
    if (!ue::Call(im, f, p.data())) return -1;
    return p.Get<std::int32_t>("ReturnValue");
}

Wallet ReadWallet() {
    Wallet w;
    UObject* g = GoldDef();
    UObject* gl = GloomDef();
    UObject* im = ItemManager();
    if (!im) return w;
    if (g) {
        w.gold = ItemCount(im, g);
        w.goldSum = ItemSum(im, g);
    }
    if (gl) {
        w.gloom = ItemCount(im, gl);
        w.gloomSum = ItemSum(im, gl);
    }
    return w;
}

// The number a death is measured in: the carried Gloom, i.e. the SUM
// GetItemStacksByClass reports (the stacks count is only a second opinion).
int CarriedGloom(const Wallet& w) {
    if (w.gloomSum >= 0) return w.gloomSum;
    return w.gloom;
}

// ---------------------------------------------------------------------------
// the save object's lost-gloom slot
// ---------------------------------------------------------------------------
UObject* SaveObject() {
    UObject* pc = net::PC();
    UObject* so = nullptr;
    if (!PropObj(pc, "PlayerSaveGameObject", &so)) return nullptr;
    return so;
}

LostGloomSave ReadSave() {
    LostGloomSave s;
    UObject* so = SaveObject();
    if (!Live(so)) return s;
    s.has = ue::GetBool(so, kHasLost);
    s.amount = ReadInt(so, kLostAmt);
    ue::FVector v;
    if (ue::Get<ue::FVector>(so, kLostLoc, v)) {
        s.loc.X = v.X;
        s.loc.Y = v.Y;
        s.loc.Z = v.Z;
        s.locOk = true;
    }
    return s;
}

double Dist3(const Vec3& a, const Vec3& b) { return Dist(a, b); }

// "did this death rewrite the lost-gloom slot?"  The ability only writes it when the
// death took a non-zero amount of Gloom, so an unchanged slot means "no new drop was
// created" -- and the sweep must then leave whatever bag is on the ground alone.
bool SlotChanged(const LostGloomSave& now, const LostGloomSave& before) {
    if (now.has != before.has) return true;
    if (now.amount != before.amount) return true;
    if (now.locOk && before.locOk && Dist3(now.loc, before.loc) > 1.0) return true;
    return false;
}

// ---------------------------------------------------------------------------
// THE STEP PROBE -- does the game's drop code RUN at all?
//
// The brief's two candidate causes ("the drop code is skipped because the mod
// intercepts the death" and "the drop is destroyed by the respawn") look identical
// from the world afterwards, so this hooks the ability's own steps and logs them as
// they fire.  It logs; it never changes a return value and never skips a call.
// ---------------------------------------------------------------------------
void StepHook(UObject* /*self*/, UFunction* fn, void* parms) {
    if (!On() || !fn) return;
    const std::string name = ue::GetName(reinterpret_cast<UObject*>(fn));
    if (name.empty()) return;
    // The two parameters worth reading, each at the width the reflection gives it.
    std::string extra;
    const char* wanted[] = {"Gloom", "ReturnValue", "Success", "bUsingDeferred"};
    for (const char* p : wanted) {
        if (!parms) break;
        const std::int32_t off = ue::PropOffsetStruct((ue::UStruct*)fn, p);
        if (off < 0) continue;
        const std::int32_t size = ue::PropSizeStruct((ue::UStruct*)fn, p);
        const std::uint8_t* at = reinterpret_cast<const std::uint8_t*>(parms) + off;
        if (size == 1) {
            std::uint8_t v = 0;
            if (ue::ReadMem(at, &v, 1)) net::AppendFmt(&extra, " %s=%d", p, (int)v);
        } else if (size == 4) {
            std::int32_t v = 0;
            if (ue::ReadMem(at, &v, 4)) net::AppendFmt(&extra, " %s=%d", p, v);
        }
    }
    net::Log("money: STEP %s%s", name.c_str(), extra.c_str());
}

// The steps of GA_DeathSpoilsHandler_C, in the order the graph reaches them
// (research\raw\CXXHeaderDump\GA_DeathSpoilsHandler.hpp).  A POST hook so
// ReturnValue carries the step's own answer.
const char* const kStepSpecs[] = {
    "GA_DeathSpoilsHandler_C.GetGloom",
    "GA_DeathSpoilsHandler_C.RemoveGloom",
    "GA_DeathSpoilsHandler_C.ShouldSaveLocation",
    "GA_DeathSpoilsHandler_C.UseDeferredLocation",
    "GA_DeathSpoilsHandler_C.SaveLostGloomData",
    "GA_DeathSpoilsHandler_C.SpawnLostGloom",
    "GA_DeathSpoilsHandler_C.RunEQS",
    "GA_DeathSpoilsHandler_C.OnEQSFinished",
};
bool g_stepHooks = false;
double g_stepRetryAt = 0.0;
int g_stepTries = 0;
bool g_stepInstalled[sizeof(kStepSpecs) / sizeof(kStepSpecs[0])] = {false};

void EnsureStepHooks() {
    if (g_stepHooks) return;
    const double now = net::Now();
    if (now < g_stepRetryAt) return;
    g_stepRetryAt = now + 2.0;
    const int want = (int)(sizeof(kStepSpecs) / sizeof(kStepSpecs[0]));
    int have = 0;
    for (int i = 0; i < want; ++i) {
        // Only the ones that are still missing: RegisterHook logs a failed resolve,
        // and 8 specs x 2 s through a 60 s boot would bury the log.
        if (!g_stepInstalled[i]) g_stepInstalled[i] = ue::HookPost(kStepSpecs[i], &StepHook);
        if (g_stepInstalled[i]) ++have;
    }
    if (have == want) {
        g_stepHooks = true;
        net::Log("money: %d/%d death-spoils step hooks installed on GA_DeathSpoilsHandler_C",
                 have, want);
    } else if (++g_stepTries >= 30) {
        g_stepHooks = true;   // stop retrying, say what is missing
        std::string missing;
        for (int i = 0; i < want; ++i) {
            if (!g_stepInstalled[i]) net::AppendFmt(&missing, "%s%s", missing.empty() ? "" : ",",
                                                    kStepSpecs[i]);
        }
        net::Log("money: STEP probe incomplete (%d/%d) -- not resolved: %s.  The world-side "
                 "measurement continues; only the step log is thinner.",
                 have, want, missing.c_str());
    }
}

// ---------------------------------------------------------------------------
// the bags in this world
// ---------------------------------------------------------------------------
// One FindAllOf per scan, and every question about a bag is answered from that
// one pass (a 2 Hz lane must not walk GObjects more than once per sample).
struct SavedBag {
    UObject* obj = nullptr;
    SpoilsObs obs;
};

void ScanBags(std::vector<SavedBag>* out) {
    out->clear();
    UObject* pawn = net::Pawn();
    const bool haveMe = Live(pawn);
    const Vec3 me = haveMe ? net::Loc(pawn) : Vec3{};
    const DeathState& D = state().death;
    for (UObject* a : ue::FindAllOf(kSpoilsClass)) {
        if (!Live(a)) continue;
        SavedBag b;
        b.obj = a;
        b.obs.name = ue::GetName(a);
        b.obs.pos = net::Loc(a);
        b.obs.amount = ReadInt(a, "Amount");
        b.obs.recovered = ue::GetBool(a, "Recovered");
        b.obs.hidden = ue::GetBool(a, "bHidden");
        // "is it on screen" from the engine, not from a pixel: AActor::
        // WasRecentlyRendered is what a human eye cannot be trusted for on a dark
        // PNG (the gloom pile sits in dark water at the death spot).
        bool rendered = false;
        if (CallBool(a, "WasRecentlyRendered", &rendered)) b.obs.rendered = rendered ? 1 : 0;
        b.obs.dSelf = haveMe ? Dist3(me, b.obs.pos) : -1.0;
        b.obs.dDeath = D.hasDeathPos ? Dist3(D.deathPos, b.obs.pos) : -1.0;
        out->push_back(b);
    }
}

void Scan(std::vector<SpoilsObs>* out) {
    std::vector<SavedBag> bags;
    ScanBags(&bags);
    out->clear();
    for (const SavedBag& b : bags) out->push_back(b.obs);
}

// The bag a name refers to, or nullptr when it is gone.
UObject* FindByName(const std::string& name) {
    for (UObject* a : ue::FindAllOf(kSpoilsClass)) {
        if (Live(a) && ue::GetName(a) == name) return a;
    }
    return nullptr;
}

UObject* NearestSpoils(double* distOut) {
    UObject* best = nullptr;
    double bestD = 0.0;
    UObject* pawn = net::Pawn();
    const Vec3 me = Live(pawn) ? net::Loc(pawn) : Vec3{};
    for (UObject* a : ue::FindAllOf(kSpoilsClass)) {
        if (!Live(a)) continue;
        const double d = Dist3(me, net::Loc(a));
        if (!best || d < bestD) {
            best = a;
            bestD = d;
        }
    }
    if (distOut) *distOut = best ? bestD : -1.0;
    return best;
}

// ---------------------------------------------------------------------------
// logging
// ---------------------------------------------------------------------------
std::string Where(const Vec3& v) {
    return net::Fmt("(%.0f,%.0f,%.0f)", v.X, v.Y, v.Z);
}

void LogWorld(const char* why) {
    Scan(&M().now);
    const Wallet w = ReadWallet();
    std::string list;
    for (const SpoilsObs& s : M().now) {
        net::AppendFmt(&list, "%s[%s amount=%d recovered=%d rendered=%d hidden=%d dMe=%.0f dDeath=%.0f]", list.empty() ? "" : " ",
                       s.name.c_str(), s.amount, s.recovered ? 1 : 0, s.rendered, s.hidden ? 1 : 0, s.dSelf, s.dDeath);
    }
    net::Log("money(%s): spoils=%d %s | wallet gloom=%d (stacks %d) gold=%d (stacks %d) | save "
             "hasLost=%d amount=%d loc=%s",
             why, (int)M().now.size(), list.empty() ? "(none)" : list.c_str(), CarriedGloom(w),
             w.gloom, w.goldSum, w.gold, M().save.has ? 1 : 0, M().save.amount,
             M().save.locOk ? Where(M().save.loc).c_str() : "(none)");
}

// Every bag that the save slot does NOT describe is destroyed.  This is the whole
// point of the lane's ONE write: vanilla gets the same result from the reload that
// a co-op case-2 death does not perform (a new death overwrites the single
// lost-gloom slot, so the previous drop is lost -- and its actor must be too, or
// the player could collect two drops for one loss).
//
// It never runs unless the slot CHANGED at this death (the ability only rewrites it
// when the death took a non-zero amount of Gloom), so a death with an empty wallet
// cannot delete a bag the player is still owed.
// WHICH bag does the slot describe?  The AMOUNT first, the place second.
//
// The place alone is not enough: two deaths in the same room (a player who dies
// twice at the same respawn point) put two bags within a few hundred uu of each
// other, and a circle around the recorded location would keep both -- which is
// exactly the duplication this rule exists to stop.  The amount is what identifies
// the drop: the slot's amount is the sum the death took, and it is written on the
// actor's own `Amount`.  Distance is only the tie-breaker (two losses of the same
// size), and if nothing matches the amount the nearest bag to the slot wins when it
// is close enough.
int SweepStale(const char* why) {
    if (!M().saveChanged) {
        net::Log("money(%s) sweep: the lost-gloom slot did NOT change at this death "
                 "(hasLost=%d amount=%d) -- the old drop stays, nothing is replaced",
                 why, M().save.has ? 1 : 0, M().save.amount);
        return 0;
    }
    if (!M().save.locOk) {
        net::Log("money(%s) sweep: the slot changed but its location is unreadable -- "
                 "refusing to guess which bag is the new one; nothing swept", why);
        return 0;
    }
    std::vector<SavedBag> bags;
    ScanBags(&bags);
    M().now.clear();
    for (const SavedBag& b : bags) M().now.push_back(b.obs);
    if (bags.empty()) {
        net::Log("money(%s) sweep: the slot changed (amount=%d at %s) but there is NO "
                 "BP_DeathSpoils_C in this world -- the death drop did not spawn",
                 why, M().save.amount, Where(M().save.loc).c_str());
        return 0;
    }
    // 1. the bag whose Amount is the slot's amount, the nearest one if several match
    int keep = -1;
    double keepD = 0.0;
    for (std::size_t i = 0; i < bags.size(); ++i) {
        if (bags[i].obs.amount != M().save.amount) continue;
        const double d = Dist3(M().save.loc, bags[i].obs.pos);
        if (keep < 0 || d < keepD) {
            keep = (int)i;
            keepD = d;
        }
    }
    // 2. no amount match: the drop the slot describes does not exist.  Everything on
    // the ground is therefore an older loss, and vanilla's rule is that the older loss
    // is GONE -- so they all go, and the caller's EnsureDrop re-creates the one the
    // slot describes.  (Keeping one of them, as an earlier build did when it was the
    // "only bag near the spot", left the player with the WRONG amount -- run 4 kept a
    // 4021 bag for a 500 slot and the new drop was never spawned.)
    if (keep < 0) {
        net::Log("money(%s) sweep: no bag carries the slot's amount %d (%d on the ground: %s) -- "
                 "the described drop is missing, so every bag here is an older loss; replacing "
                 "them", why, M().save.amount, (int)bags.size(),
                 bags.empty() ? "(none)" : bags[0].obs.name.c_str());
        const int gone = DestroyAllDrops("replacing the old loss");
        ++M().swept;
        net::Log("money(%s) sweep: %d old drop(s) destroyed, the slot's own drop will be spawned",
                 why, gone);
        return gone;
    }
    int removed = 0;
    for (std::size_t i = 0; i < bags.size(); ++i) {
        const double d = Dist3(M().save.loc, bags[i].obs.pos);
        if ((int)i == keep) {
            net::Log("money(%s) sweep: keep %s amount=%d dSlot=%.0f (its Amount is the slot's)",
                     why, bags[i].obs.name.c_str(), bags[i].obs.amount, d);
            continue;
        }
        net::Log("money(%s) sweep: destroying stale drop %s amount=%d dSlot=%.0f at %s "
                 "(the slot now describes %s amount=%d)",
                 why, bags[i].obs.name.c_str(), bags[i].obs.amount, d,
                 Where(bags[i].obs.pos).c_str(), Where(M().save.loc).c_str(), M().save.amount);
        if (CallVoid(bags[i].obj, "K2_DestroyActor")) {
            ++removed;
            ++M().staleRemoved;
        }
    }
    ++M().swept;
    net::Log("money(%s) sweep: %d stale drop(s) destroyed, 1 kept", why, removed);
    return removed;
}

// Destroy EVERY drop in this world.  Used only where it is already established that
// nobody on the ground is the drop the slot describes (the slot has just been written
// for a loss that had no actor at all), so "everything here is stale" is a fact, not
// a guess.
int DestroyAllDrops(const char* why) {
    std::vector<SavedBag> bags;
    ScanBags(&bags);
    int removed = 0;
    for (const SavedBag& b : bags) {
        net::Log("money(%s): destroying drop %s amount=%d at %s -- the slot describes a drop "
                 "that does not exist yet", why, b.obs.name.c_str(), b.obs.amount,
                 Where(b.obs.pos).c_str());
        if (CallVoid(b.obj, "K2_DestroyActor")) {
            ++removed;
            ++M().staleRemoved;
        }
    }
    M().now.clear();
    return removed;
}

// ---------------------------------------------------------------------------
// WRITE THE LOST-GLOOM SLOT OURSELVES -- when the death took Gloom and the game
// recorded nothing.
//
// MEASURED (run 3): a death the mod drives through `USpartaHealthComponent::
// StartDeath` (the `die` fallback after the first `S_KillMe` of a session) empties
// the wallet but does NOT reach GA_DeathSpoilsHandler's save step: the carried 300
// Gloom was gone at the respawn and the slot still held the PREVIOUS death's
// 4521.  The same happens on a party wipe (5021 carried, slot empty, reload, money
// gone).  In vanilla that record is what makes the reload restore the drop, so the
// mod has to make it: these four fields ARE the game's own model of "you are owed N
// Gloom at L" (SYNC-DATA-MINE 9.4), and the spawn that follows is the game's own
// `SpawnLostGloom`, which reads them.
//
// It runs only when the loss is PROVEN (the wallet went down between the death and
// the respawn) and the slot does not already hold at least that much -- so it can
// never pay out money that was not taken.
// ---------------------------------------------------------------------------
bool WriteLostGloom(int amount, const Vec3& at, const char* why) {
    UObject* so = SaveObject();
    if (!Live(so)) {
        net::Log("money(%s): cannot record the %d gloom this death took -- no player save "
                 "object", why, amount);
        return false;
    }
    ue::FVector v;
    v.X = at.X;
    v.Y = at.Y;
    v.Z = at.Z;
    const bool okHas = ue::SetBool(so, kHasLost, true);
    const std::int32_t a = amount;
    const bool okAmt = ue::Set<std::int32_t>(so, kLostAmt, a);
    const bool okLoc = ue::Set<ue::FVector>(so, kLostLoc, v);
    const bool okObj = ue::SetBool(so, "bLostGloomInsideObjective", false);
    const bool rbHas = ue::GetBool(so, kHasLost);
    const int rbAmt = ReadInt(so, kLostAmt);
    net::Log("money(%s): wrote the lost-gloom slot ourselves -- amount=%d at %s "
             "(hasLost=%d/%d amount=%d/%d loc=%d insideObjective=%d) => %s",
             why, amount, Where(at).c_str(), (int)okHas, (int)rbHas, (int)okAmt, rbAmt,
             (int)okLoc, (int)okObj, (rbHas && rbAmt == amount) ? "RECORDED" : "FAILED");
    return rbHas && rbAmt == amount;
}

// ---------------------------------------------------------------------------
// THE OTHER HALF OF THE FIX: put the drop back when the game did not.
//
// The save slot is the game's own record of "this player is owed N Gloom at L".
// When that record exists and the world holds no BP_DeathSpoils_C, the drop is
// lost -- in vanilla the load path would have restored it.  The restoration is the
// ability's OWN function (`SpawnLostGloom` reads the slot and spawns the actor at
// the recorded location, see the reflection rows
// SpawnLostGloom:CallFunc_GetPlayerSaveObjectFromCurrentSave_ReturnValue ->
// CallFunc_BeginDeferredActorSpawnFromClass -> FinishSpawningActor), so nothing is
// re-implemented here: the skipped step is simply run.
//
// It never runs while a bag already exists, so it cannot duplicate a drop.
// ---------------------------------------------------------------------------
int g_restores = 0;

bool EnsureDrop(const char* why) {
    MoneyState& m = M();
    m.save = ReadSave();
    if (!m.save.has || m.save.amount <= 0) {
        net::Log("money(%s) restore: the save slot holds no lost gloom (hasLost=%d amount=%d) "
                 "-- nothing is owed, nothing to restore",
                 why, m.save.has ? 1 : 0, m.save.amount);
        return false;
    }
    std::vector<SavedBag> bags;
    ScanBags(&bags);
    if (!bags.empty()) {
        net::Log("money(%s) restore: the slot says %d lost at %s and the world already holds "
                 "%d drop(s) -- no restore needed",
                 why, m.save.amount, m.save.locOk ? Where(m.save.loc).c_str() : "(no loc)",
                 (int)bags.size());
        return false;
    }
    UObject* ga = ue::FindFirstOf("GA_DeathSpoilsHandler_C");
    if (!Live(ga)) {
        net::Log("money(%s) restore: the slot says %d gloom is owed at %s but there is NO live "
                 "GA_DeathSpoilsHandler_C to spawn it (the ability is not instantiated) -- the "
                 "drop stays lost", why, m.save.amount,
                 m.save.locOk ? Where(m.save.loc).c_str() : "(no loc)");
        return false;
    }
    net::Log("money(%s) restore: the slot owes %d gloom at %s and this world has no drop -- "
             "calling %s.SpawnLostGloom() (the game's own spawn-from-save step)",
             why, m.save.amount, m.save.locOk ? Where(m.save.loc).c_str() : "(no loc)",
             ue::GetName(ga).c_str());
    const bool ok = CallVoid(ga, "SpawnLostGloom");
    std::vector<SavedBag> after;
    ScanBags(&after);
    net::Log("money(%s) restore: SpawnLostGloom ok=%s -> %d drop(s) in the world now",
             why, ok ? "true" : "false", (int)after.size());
    if (ok && !after.empty()) {
        ++g_restores;
        m.now.clear();
        for (const SavedBag& b : after) m.now.push_back(b.obs);
    }
    return ok && !after.empty();
}

// ---------------------------------------------------------------------------
// collection
//
// The game's own collect path, not a re-implementation: the bag's
// BPC_InteractionHandler_C broadcasts OnInteraction when the player presses the
// interact key, and BP_DeathSpoils_C's bound event is what gives the gloom back.
// The handler's own `HandleInteractEvent(InputAction)` is the step just before that
// broadcast (it resolves the InteractID from the input action), so it is tried
// first with the action the handler itself carries -- and the bound event is the
// fallback.  Both are the game's code; neither grants anything by hand.
// ---------------------------------------------------------------------------
// FF_InteractAction: FName InteractID @0x00 ... UInputAction* @0x38, size 0x40
// (research\raw\CXXHeaderDump\F_InteractAction.hpp).
constexpr int kInteractStride = 0x40;
constexpr int kInteractActionOff = 0x38;

struct InteractPick {
    UObject* comp = nullptr;
    UObject* action = nullptr;
    ue::FName id;
    std::string idText;
    int n = 0;
};

InteractPick ReadInteractPick(UObject* bag) {
    InteractPick p;
    if (!PropObj(bag, "BPC_InteractionHandler", &p.comp)) return p;
    ue::TArray<std::uint8_t> arr;
    if (!ue::Get<ue::TArray<std::uint8_t>>(p.comp, "Actions", arr)) return p;
    if (!arr.Data || arr.Num <= 0) return p;
    p.n = arr.Num;
    std::uint8_t first[kInteractStride] = {0};
    if (!ue::ReadMem(arr.Data, first, sizeof(first))) return p;
    std::memcpy(&p.id, first, sizeof(ue::FName));
    std::memcpy(&p.action, first + kInteractActionOff, sizeof(UObject*));
    p.idText = ue::ToString(p.id);
    if (!Live(p.action)) p.action = nullptr;
    return p;
}

bool TeleportPawn(const Vec3& to) {
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return false;
    UFunction* fn = Func(pawn, "K2_SetActorLocation");
    if (!fn) return false;
    ue::FVector v;
    v.X = to.X;
    v.Y = to.Y;
    v.Z = to.Z;
    ue::Params p(fn);
    p.Set<ue::FVector>("NewLocation", v);
    p.Set<bool>("bSweep", false);
    p.Set<bool>("bTeleport", true);
    return ue::Call(pawn, fn, p.data());
}

bool SetPawnYaw(double yaw) {
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return false;
    UFunction* fn = Func(pawn, "K2_SetActorRotation");
    if (!fn) return false;
    ue::FRotator r;
    r.Pitch = 0.0;
    r.Yaw = yaw;
    r.Roll = 0.0;
    ue::Params p(fn);
    p.Set<ue::FRotator>("NewRotation", r);
    p.Set<bool>("bTeleportPhysics", false);
    return ue::Call(pawn, fn, p.data());
}

// The interaction, one stage per call.  Stage 1 = the handler's own entry point
// with the action it carries; stage 2 = the bound event on the bag.  Whatever
// happens, the wallet delta is what the verdict reads.
void PumpPickup() {
    MoneyState& m = M();
    if (m.pickupStage == 0) return;
    if (m.pickupAt > 0.0 && net::Now() < m.pickupAt) return;
    UObject* bag = FindByName(m.pickupName);
    if (!Live(bag)) {
        // gone: the bag was consumed.  The delta was recorded when we started.
        const Wallet w = ReadWallet();
        const int delta = (m.pickupWallet >= 0 && CarriedGloom(w) >= 0) ? CarriedGloom(w) - m.pickupWallet : -1;
        m.lastReturned = delta;
        if (delta > 0) {
            ++m.collected;
            m.returned += delta;
            net::Log("money: collected %s amount=%d -> wallet %d -> %d (+%d) -- %s",
                     m.pickupName.c_str(), m.pickupAmount, m.pickupWallet, CarriedGloom(w), delta,
                     (m.pickupAmount >= 0 && delta == m.pickupAmount) ? "EXACT" : "MISMATCH");
        } else {
            net::Log("money: %s is gone but the wallet did not go up (%d -> %d)", m.pickupName.c_str(),
                     m.pickupWallet, CarriedGloom(w));
        }
        m.pickupStage = 0;
        m.gloomAfterPickup = CarriedGloom(w);
        return;
    }
    const InteractPick pick = ReadInteractPick(bag);
    if (m.pickupStage == 1) {
        net::Log("money: the handler path did not consume %s (handler=%s action=%s id=%s "
                 "actions=%d) -- trying the bag's own OnInteraction event",
                 m.pickupName.c_str(), pick.comp ? ue::GetName(pick.comp).c_str() : "(none)",
                 pick.action ? ue::GetName(pick.action).c_str() : "(none)", pick.idText.c_str(),
                 pick.n);
        UFunction* ev = Func(bag, kSpoilsEvent);
        if (!ev) {
            net::Log("money: %s has no %s -- cannot replay the collect, giving up",
                     m.pickupName.c_str(), kSpoilsEvent);
            m.pickupStage = 0;
            ++m.pickupFail;
            return;
        }
        ue::Params p(ev);
        p.Set<UObject*>("InputAction", pick.action);
        p.Set<UObject*>("Component", pick.comp);
        p.Set<ue::FName>("InteractID", pick.id);
        net::Log("money: calling %s.%s(InputAction=%s, Component=%s, InteractID=%s) -- "
                 "the game's own collect callback",
                 m.pickupName.c_str(), kSpoilsEvent,
                 pick.action ? ue::GetName(pick.action).c_str() : "(none)",
                 pick.comp ? ue::GetName(pick.comp).c_str() : "(none)", pick.idText.c_str());
        const bool ok = ue::Call(bag, ev, p.data());
        net::Log("money: OnInteraction ok=%s, bag still %s", ok ? "true" : "false",
                 Live(FindByName(m.pickupName)) ? "present" : "gone");
        m.pickupStage = 2;
        m.pickupAt = net::Now() + 1.0;
        return;
    }
    // stage 2 did not work either
    const Wallet w = ReadWallet();
    net::Log("money: FAILED to collect %s (wallet gloom=%d) after both the handler path "
             "and the bag's own event", m.pickupName.c_str(), CarriedGloom(w));
    m.pickupStage = 0;
    ++m.pickupFail;
    m.gloomAfterPickup = CarriedGloom(w);
}

void StartPickup(const char* why, double radius) {
    MoneyState& m = M();
    double d = -1.0;
    UObject* bag = NearestSpoils(&d);
    if (!Live(bag) || (radius > 0.0 && d > radius)) {
        net::Log("moneyget(%s): no BP_DeathSpoils_C within %.0f uu (nearest %s)",
                 why, radius, Live(bag) ? net::Fmt("%.0f", d).c_str() : "none");
        return;
    }
    const Vec3 pos = net::Loc(bag);
    Vec3 to = pos;
    to.Z += 90.0;   // stand ON the bag's spot, not inside the floor
    const bool tp = TeleportPawn(to);
    const Wallet before = ReadWallet();
    const int amount = ReadInt(bag, "Amount");
    m.pickupName = ue::GetName(bag);
    m.pickupAmount = amount;
    m.pickupWallet = CarriedGloom(before);
    m.pickupStage = 1;
    m.pickupAt = net::Now() + 0.4;
    LogWorld("before the collect");
    const InteractPick pick = ReadInteractPick(bag);
    UFunction* h = Live(pick.comp) ? Func(pick.comp, "HandleInteractEvent") : nullptr;
    if (!h || !pick.action) {
        net::Log("moneyget(%s): %s at %s d=%.0f amount=%d tp=%s | handler=%s action=%s "
                 "actions=%d -- the handler path is unavailable, the bag event is next",
                 why, m.pickupName.c_str(), Where(pos).c_str(), d, amount,
                 tp ? "true" : "false", pick.comp ? ue::GetName(pick.comp).c_str() : "(none)",
                 pick.action ? ue::GetName(pick.action).c_str() : "(none)", pick.n);
        return;   // PumpPickup will take stage 1 -> 2 on the next tick
    }
    ue::Params p(h);
    p.Set<UObject*>("InputAction", pick.action);
    net::Log("moneyget(%s): %s at %s d=%.0f amount=%d wallet gloom=%d tp=%s -> calling "
             "%s.HandleInteractEvent(%s) (id=%s, actions=%d)",
             why, m.pickupName.c_str(), Where(pos).c_str(), d, amount, CarriedGloom(before),
             tp ? "true" : "false", ue::GetName(pick.comp).c_str(),
             ue::GetName(pick.action).c_str(), pick.idText.c_str(), pick.n);
    const bool ok = ue::Call(pick.comp, h, p.data());
    net::Log("moneyget(%s): HandleInteractEvent ok=%s, bag %s", why, ok ? "true" : "false",
             Live(FindByName(m.pickupName)) ? "present" : "gone");
}

}  // namespace

// ===========================================================================
// the module's entry points
// ===========================================================================

void Tick2() {
    MoneyState& m = M();
    m.off = !On();
    if (m.off) return;
    EnsureStepHooks();
    const double now = net::Now();
    const bool hot = m.watch || (m.probe && now < m.probeUntil);
    ++g_ticks;

    // --- the wallet, 2 Hz in the hot window, 1 Hz otherwise -------------------
    // (2 ProcessEvent calls a sample; the lane is idle most of the time and this is
    // the number a death is measured against, so it must be recent.)
    const bool sampleWallet = hot || (g_ticks % 2 == 0);
    if (sampleWallet) {
        const Wallet w = ReadWallet();
        m.walletAt = now;
        m.gloomNow = CarriedGloom(w);
        m.goldNow = w.gold;
        if (m.lastWallet >= 0 && m.gloomNow != m.lastWallet) {
            net::Log("money: wallet gloom %d -> %d%s", m.lastWallet, m.gloomNow,
                     m.pickupStage ? " (a collect is in flight)" : "");
        }
        m.lastWallet = m.gloomNow;
        m.save = ReadSave();
        // the two-deep ring that makes "before the death" honest (see MoneyState)
        m.ring[m.ringNext] = m.save;
        m.ringAt[m.ringNext] = now;
        m.ringNext = (m.ringNext + 1) % 2;
    }

    // --- the bags ------------------------------------------------------------
    const bool scan = hot || (g_ticks % kNtickIdleScanEvery == 0);
    if (scan) {
        ++m.scans;
        std::vector<SpoilsObs> fresh;
        Scan(&fresh);
        // a new name = a new drop; a name that vanished = collected or destroyed
        for (const SpoilsObs& s : fresh) {
            if (m.seen.Add(s.name)) {
                ++m.appeared;
                net::Log("money: a drop is in the world: %s amount=%d recovered=%d rendered=%d at %s "
                         "dMe=%.0f dDeath=%.0f (this world, not the peer's)",
                         s.name.c_str(), s.amount, s.recovered ? 1 : 0, s.rendered,
                         Where(s.pos).c_str(), s.dSelf, s.dDeath);
            }
        }
        for (const SpoilsObs& old : m.now) {
            bool still = false;
            for (const SpoilsObs& s : fresh) {
                if (s.name == old.name) {
                    still = true;
                    break;
                }
            }
            if (!still) {
                ++m.gone;
                net::Log("money: a drop LEFT the world: %s amount=%d at %s (collected or "
                         "destroyed)", old.name.c_str(), old.amount, Where(old.pos).c_str());
            }
        }
        m.lastCount = (int)fresh.size();
        m.now = fresh;
    }

    // --- the sweep a death owes ---------------------------------------------
    if (m.sweepAt > 0.0 && now >= m.sweepAt) {
        m.sweepAt = 0.0;
        m.save = ReadSave();
        m.saveChanged = SlotChanged(m.save, m.prev);
        net::Log("money: the lost-gloom slot after death #%u: hasLost=%d amount=%d loc=%s "
                 "(before: hasLost=%d amount=%d) -- changed=%d",
                 m.serial, m.save.has ? 1 : 0, m.save.amount,
                 m.save.locOk ? Where(m.save.loc).c_str() : "(none)", m.prev.has ? 1 : 0,
                 m.prev.amount, m.saveChanged ? 1 : 0);
        SweepStale("after the death");
        LogWorld("after the sweep");
        m.now = std::vector<SpoilsObs>();
        Scan(&m.now);
        if (m.now.empty()) EnsureDrop("after the death");
    }

    // --- the post-load restore check ----------------------------------------
    if (m.restoreAt > 0.0 && now >= m.restoreAt) {
        m.restoreAt = 0.0;
        EnsureDrop("after a world change");
    }

    // --- the collection in flight -------------------------------------------
    PumpPickup();

    if (m.watch && now > m.watchUntil) {
        m.watch = false;
        net::Log("money: the death watch window closed (spoils=%d collected=%d returned=%d)",
                 (int)m.now.size(), m.collected, m.returned);
    }
}

void Reset() {
    MoneyState& m = M();
    // Every actor pointer dies with the world; the NAMES in `seen` do not (they are
    // how the sweep tells "still there" from "gone"), so they stay.
    m.now.clear();
    m.pickupStage = 0;
    m.pickupAt = 0.0;
    m.sweepAt = 0.0;
    m.watch = false;
    m.scans = 0;
    m.lastCount = -1;
    m.saveChanged = false;
    // A reload is exactly the moment the game used to re-create the drop from the
    // save slot; check that it did, once the new world has settled.
    if (On()) m.restoreAt = net::Now() + 10.0;
}

void NoteDeath(UObject* pawn) {
    if (!On()) return;
    MoneyState& m = M();
    const double now = net::Now();
    // The slot as it stood BEFORE this death.  The NEWEST of the two ring samples is
    // the one that can have raced the ability (measured in run 1: the tick that
    // noted the death already saw the death's own write), so "before" is the OLDER
    // one whenever there is one.
    const int older = (m.ringNext + 1) % 2;   // ringNext is the NEXT slot to write
    if (m.ringAt[older] > 0.0 && m.ringAt[older] <= now) {
        m.prev = m.ring[older];
    } else {
        m.prev = m.save;
    }
    // A death that happens before the second sample of the session has no history;
    // say so rather than inventing a "before" that may already be the after.
    m.serial = state().death.serial;
    m.deathAt = now;
    // The corpse's spot is where the drop belongs -- and it is the only place we can
    // write into the save slot ourselves if the game's own save step is skipped
    // (note: this survives the wipe's reload, which is exactly when it is needed).
    m.hasDeathSpot = ue::IsValid(pawn);
    if (m.hasDeathSpot) m.deathSpot = net::Loc(pawn);
    m.watch = true;
    m.watchUntil = now + kWatchSecs;
    m.gloomBefore = m.gloomNow;
    m.sweepAt = now + kSweepDelay;
    m.saveChanged = false;
    m.pickupStage = 0;
    const Wallet w = ReadWallet();
    m.gloomAtDeath = CarriedGloom(w);
    net::Log("money: death #%u noted at %s -- carried gloom (last sample) %d, now %d, save slot "
             "before (the older of the last two samples): hasLost=%d amount=%d loc=%s; spoils in "
             "the world now: %d",
             m.serial, m.hasDeathSpot ? Where(m.deathSpot).c_str() : "(unknown)",
             m.gloomBefore, CarriedGloom(w), m.prev.has ? 1 : 0, m.prev.amount,
             m.prev.locOk ? Where(m.prev.loc).c_str() : "(none)", (int)m.now.size());
}

void NoteRespawn() {
    if (!On()) return;
    MoneyState& m = M();
    const Wallet w = ReadWallet();
    m.gloomAtRespawn = CarriedGloom(w);
    m.save = ReadSave();
    m.saveChanged = SlotChanged(m.save, m.prev);
    LogWorld("after the respawn");
    net::Log("money: respawned -- wallet gloom=%d, save slot hasLost=%d amount=%d (was "
             "hasLost=%d amount=%d, changed=%d)",
             CarriedGloom(w), m.save.has ? 1 : 0, m.save.amount, m.prev.has ? 1 : 0, m.prev.amount,
             m.saveChanged ? 1 : 0);

    // THE PROOF OF THE LOSS: carried before the death minus carried now.  This is the
    // one number that says "the game really took it", so it is what a slot we have to
    // write ourselves is allowed to be based on.
    const int taken = (m.gloomBefore >= 0 && m.gloomAtRespawn >= 0)
                          ? m.gloomBefore - m.gloomAtRespawn
                          : -1;
    if (m.saveChanged) {
        // The game recorded this death itself (case-2 death through the game's own
        // death path).  The 1.2 s sweep already kept exactly the drop it describes.
        net::Log("money: this death's loss is the game's own record (%d gloom) -- nothing to "
                 "write", m.save.amount);
        if (m.now.empty()) EnsureDrop("after the respawn");
        return;
    }
    if (taken <= 0) {
        net::Log("money: no gloom was taken at this death (carried %d -> %d) and the slot did "
                 "not change -- no drop is owed", m.gloomBefore, m.gloomAtRespawn);
        if (m.now.empty()) EnsureDrop("after the respawn");
        return;
    }
    // The slot did NOT change at this death, so whatever it still holds belongs to an
    // OLDER loss -- its amount is not a reason to skip this one (run 4: death #2 lost
    // 300 while the slot still held death #1's uncollected 500, and that comparison
    // dropped the 300 on the floor).  This death's record replaces the old one, which
    // IS the game's "old drop replaced/lost" rule.
    // THE GAP: gloom is gone and nothing recorded it.  Everything on the ground is
    // therefore stale (the slot describes a drop that does not exist), so it goes,
    // then the record, then the game's own spawn.
    net::Log("money: THE GAME TOOK %d GLOOM AND RECORDED NOTHING (carried %d -> %d, slot was "
             "hasLost=%d amount=%d) -- the mod's own death path does not reach "
             "GA_DeathSpoilsHandler's save step; recording it at %s",
             taken, m.gloomBefore, m.gloomAtRespawn, m.save.has ? 1 : 0, m.save.amount,
             m.hasDeathSpot ? Where(m.deathSpot).c_str() : "(the last death spot)");
    DestroyAllDrops("before the restore");
    Vec3 at = m.hasDeathSpot ? m.deathSpot : (m.save.locOk ? m.save.loc : net::Loc(net::Pawn()));
    if (!WriteLostGloom(taken, at, "after the respawn")) return;
    m.save = ReadSave();
    m.saveChanged = true;
    EnsureDrop("after the unrecorded loss was recorded");
}

std::string Status() {
    MoneyState& m = M();
    if (!On()) return "money=off\n";
    // The verdict line's inputs, on one line at column 0 like every other block:
    //   spoil   bags in THIS world now
    //   seen    distinct drops this session
    //   lost    the save slot's amount (what this death took)
    //   before/after   the carried gloom around the death
    //   got     what the collects gave back
    int amount = -1;
    if (!m.now.empty()) amount = m.now.front().amount;
    return net::Fmt("money=spoil:%d amount:%d seen:%d appeared:%d gone:%d lost:%d before:%d "
                    "atDeath:%d atRespawn:%d collected:%d got:%d last:%d swept:%d "
                    "stale:%d restores:%d fails:%d v:1\n",
                    (int)m.now.size(), amount, (int)m.seen.Size(), m.appeared, m.gone,
                    m.save.amount, m.gloomBefore, m.gloomAtDeath, m.gloomAtRespawn, m.collected,
                    m.returned, m.lastReturned, m.swept, m.staleRemoved, g_restores,
                    m.pickupFail);
}

bool OnCommand(const std::string& cmd, const std::string& rest) {
    if (cmd == "moneydump") {
        M().save = ReadSave();
        LogWorld("moneydump");
        return true;
    }
    if (cmd == "moneyclear") {
        // THE FIXTURE RESET (the same idea as the WORLD/LIFT/CHEST fixture resets):
        // a seeded save may already owe the player a drop from an earlier session, and
        // a verdict that cannot tell the old bag from this run's bag is worthless.
        // Destroys every drop in this world and clears the lost-gloom slot, i.e. the
        // state a player is in right after collecting.
        const int gone = DestroyAllDrops("moneyclear");
        const bool cleared = WriteLostGloom(0, Vec3(), "moneyclear");
        M().seen.Clear();
        M().now.clear();
        M().save = ReadSave();
        M().prev = M().save;
        M().saveChanged = false;
        net::Log("moneyclear: %d drop(s) destroyed, slot cleared=%s (hasLost=%d amount=%d)",
                 gone, cleared ? "true" : "false", M().save.has ? 1 : 0, M().save.amount);
        LogWorld("moneyclear");
        return true;
    }
    if (cmd == "moneygive") {
        // The fixture: put REAL carried Gloom on this player, the way the game's own
        // pickup does (S_AddGloom on the controller, the item path as a fallback),
        // and prove it landed by reading the number the DEATH reads
        // (GetItemStacksByClass).  Measured in run 1: S_AddGloom alone moved the
        // stack COUNT by 1, so the sum is the number to check.
        const int want = rest.empty() ? 500 : (int)::atoi(rest.c_str());
        const Wallet before = ReadWallet();
        bool ok = false;
        UObject* pc = net::PC();
        if (Live(pc)) {
            if (UFunction* f = Func(pc, "S_AddGloom")) {
                ue::Params p(f);
                p.Set<std::int32_t>("Stacks", want);
                ok = ue::Call(pc, f, p.data());
            }
        }
        Wallet after = ReadWallet();
        bool itemPath = false;
        if (CarriedGloom(after) < want) {
            UObject* im = ItemManager();
            UObject* def = GloomDef();
            if (Live(im) && Live(def)) {
                if (UFunction* f = Func(im, "AddItemByClass")) {
                    ue::Params p(f);
                    p.Set<ue::UClass*>("ItemDef", reinterpret_cast<ue::UClass*>(def));
                    p.Set<std::int32_t>("StackCount", want);
                    itemPath = ue::Call(im, f, p.data());
                }
            }
            after = ReadWallet();
        }
        net::Log("moneygive: asked %d gloom -> S_AddGloom=%s itemPath=%s | carried gloom "
                 "%d -> %d (stacks %d -> %d)",
                 want, ok ? "true" : "false", itemPath ? "true" : "false", CarriedGloom(before),
                 CarriedGloom(after), before.gloom, after.gloom);
        M().gloomNow = CarriedGloom(after);
        M().lastWallet = M().gloomNow;
        return true;
    }
    if (cmd == "moneyprobe") {
        const double secs = rest.empty() ? 120.0 : ::atof(rest.c_str());
        M().probe = true;
        M().probeUntil = net::Now() + (secs > 0.0 ? secs : 120.0);
        M().save = ReadSave();
        LogWorld("moneyprobe");
        return true;
    }
    if (cmd == "moneyget") {
        double radius = rest.empty() ? 4000.0 : ::atof(rest.c_str());
        if (radius <= 0.0) radius = 4000.0;
        StartPickup("cmd", radius);
        return true;
    }
    if (cmd == "moneyrestore") {
        // Force the restore check now (the same call the death/respawn path makes).
        const bool ok = EnsureDrop("cmd");
        net::Log("moneyrestore: restore ok=%s (%d restore(s) this session)", ok ? "true" : "false",
                 g_restores);
        LogWorld("moneyrestore");
        return true;
    }
    if (cmd == "moneyblink") {
        // A/B pixels, not judgement: hide or show every drop without moving anything.
        // Two frames from the SAME camera then differ exactly by the drop, which is how
        // a dark pile in dark water is proven to be on screen (see the harness: it
        // shoots "on" and "off" and the diff localises the bag).
        const bool hide = (rest == "on" || rest == "1" || rest == "true");
        int n = 0;
        for (UObject* a : ue::FindAllOf(kSpoilsClass)) {
            if (!Live(a)) continue;
            UFunction* f = Func(a, "SetActorHiddenInGame");
            if (!f) continue;
            ue::Params p(f);
            p.Set<bool>("bNewHidden", hide);
            if (ue::Call(a, f, p.data())) ++n;
        }
        net::Log("moneyblink: %s -> %d drop(s) %s", rest.c_str(), n,
                 hide ? "hidden (the A/B pair's OFF frame)" : "shown again");
        return true;
    }
    if (cmd == "moneyvis") {
        // THE VISIBILITY VERDICT, from the engine: is the drop actually RENDERED, and
        // is it interactable from here?  A dark gloom pile in a dark pool is nearly
        // impossible to judge by eye on a PNG, so WasRecentlyRendered is the honest
        // answer, and the game's own interaction handler is its statement that the
        // player can collect it from where he is.
        double d = -1.0;
        UObject* bag = NearestSpoils(&d);
        if (!Live(bag)) {
            net::Log("moneyvis: no BP_DeathSpoils_C in this world -- nothing is visible "
                     "because there is nothing (this is the FAIL case of the verdict)");
            return true;
        }
        bool rendered = false;
        const bool rOk = CallBool(bag, "WasRecentlyRendered", &rendered);
        const bool hidden = ue::GetBool(bag, "bHidden");
        const bool hiddenGame = ue::GetBool(bag, "bHiddenInGame");
        const int amount = ReadInt(bag, "Amount");
        const bool rec = ue::GetBool(bag, "Recovered");
        UObject* comp = nullptr;
        const bool handler = PropObj(bag, "BPC_InteractionHandler", &comp);
        const bool locked = Live(comp) ? ue::GetBool(comp, "InteractionLocked") : false;
        // The drop has NO static mesh: its whole look is the `Niagara` component
        // (BP_DeathSpoils has Niagara + Scene_Root, no mesh).  A Niagara that is not
        // active is an INVISIBLE drop -- measured here, because that is exactly what
        // the screenshots suggested (nothing to see but the game's prompt).
        UObject* fx = nullptr;
        const bool hasFx = PropObj(bag, "Niagara", &fx);
        bool fxActive = false;
        const bool fxOk = hasFx ? CallBool(fx, "IsActive", &fxActive) : false;
        const bool fxAuto = hasFx ? ue::GetBool(fx, "bAutoActivate") : false;
        net::Log("moneyvis: %s amount=%d at %s rendered=%d(ok=%d) hidden=%d hiddenInGame=%d "
                 "recovered=%d handler=%d locked=%d dMe=%.0f dDeath=%.0f | niagara=%d active=%d "
                 "autoActivate=%d",
                 ue::GetName(bag).c_str(), amount, Where(net::Loc(bag)).c_str(),
                 rOk ? (rendered ? 1 : 0) : -1, rOk ? 1 : 0, hidden ? 1 : 0,
                 hiddenGame ? 1 : 0, rec ? 1 : 0, handler ? 1 : 0, locked ? 1 : 0, d,
                 state().death.hasDeathPos ? Dist3(state().death.deathPos, net::Loc(bag)) : -1.0,
                 hasFx ? 1 : 0, fxOk ? (fxActive ? 1 : 0) : -1, fxAuto ? 1 : 0);
        LogWorld("moneyvis");
        return true;
    }
    if (cmd == "moneyview") {
        // Put the player where a screenshot can see the bag, and clear the way for it:
        // the third-person camera sits ~200 uu behind the pawn along its yaw, so the
        // pawn is placed <dist> uu from the bag ON THE SIDE AWAY FROM THE PARTNER'S
        // PUPPET (measured in run 3: the puppet was standing exactly between B and his
        // drop, which is how a bag that was there did not appear in the shot) and faces
        // it.  The CONTROL rotation is set too: the drop lands a few tens of uu below
        // the death spot (AdjustZ/TraceForGround put it on the pool bed), so a level
        // camera frames it at the very bottom edge -- measured in run 5, where the bag
        // was only visible as the "F: RECOVER GLOOM" prompt at the frame's bottom.
        double dist = rest.empty() ? 200.0 : ::atof(rest.c_str());
        if (dist < 60.0) dist = 60.0;
        double d = -1.0;
        UObject* bag = NearestSpoils(&d);
        if (!Live(bag)) {
            net::Log("moneyview: no BP_DeathSpoils_C in this world -- nothing to look at "
                     "(that IS the answer when the death drop did not spawn)");
            return true;
        }
        const Vec3 b = net::Loc(bag);
        UObject* pawn = net::Pawn();
        const Vec3 me = Live(pawn) ? net::Loc(pawn) : b;
        // The direction the bag should be approached FROM: away from the puppet when
        // there is one, otherwise away from the player's own side.
        double dx = me.X - b.X, dy = me.Y - b.Y;
        UObject* pup = state().puppet.actor;
        if (Live(pup)) {
            const Vec3 p = net::Loc(pup);
            const double px = b.X - p.X, py = b.Y - p.Y;
            const double pl = std::sqrt(px * px + py * py);
            if (pl > 1.0) {
                dx = px;
                dy = py;
            }
        }
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0) {
            dx = 1.0;
            dy = 0.0;
            len = 1.0;
        }
        Vec3 to;
        to.X = b.X + dx / len * dist;
        to.Y = b.Y + dy / len * dist;
        to.Z = b.Z + 60.0;
        const bool tp = TeleportPawn(to);
        // Face the bag: the yaw that points from the new spot at it.
        const double yaw = std::atan2(b.Y - to.Y, b.X - to.X) * 180.0 / 3.14159265358979323;
        const bool rot = SetPawnYaw(yaw);
        // ... and tilt the camera down at it, or the bag (which sits below the death
        // spot, in the ground trace's own dip) hides under the HUD.
        bool controlled = false;
        UObject* pc = net::PC();
        if (Live(pc)) {
            const char* names[] = {"SetControlRotation", "K2_SetControlRotation",
                                   "ClientSetRotation"};
            for (const char* n : names) {
                UFunction* f = Func(pc, n);
                if (!f) continue;
                ue::FRotator r;
                r.Pitch = (dist <= 150.0) ? -45.0 : (dist <= 320.0 ? -30.0 : -22.0);
                r.Yaw = yaw;
                r.Roll = 0.0;
                ue::Params p(f);
                if (!ue::FindProperty((ue::UStruct*)f, "NewRotation")) continue;
                p.Set<ue::FRotator>("NewRotation", r);
                controlled = ue::Call(pc, f, p.data());
                if (controlled) {
                    net::Log("moneyview: camera tilted down with %s.Pitch=%.0f", n,
                             (double)r.Pitch);
                    break;
                }
            }
        }
        net::Log("moneyview: %s amount=%d at %s; player %s -> %s yaw=%.0f tp=%s rot=%s d=%.0f "
                 "puppet=%s control=%s", ue::GetName(bag).c_str(), ReadInt(bag, "Amount"),
                 Where(b).c_str(), Where(me).c_str(), Where(to).c_str(), yaw,
                 tp ? "true" : "false", rot ? "true" : "false", d,
                 Live(pup) ? Where(net::Loc(pup)).c_str() : "(none)",
                 controlled ? "true" : "false");
        LogWorld("moneyview");
        return true;
    }
    return false;
}

}  // namespace money
}  // namespace coop
