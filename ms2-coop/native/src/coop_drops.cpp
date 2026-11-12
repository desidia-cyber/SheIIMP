// coop_drops.cpp -- [R7-DROPS] G6: enemy drops, shared between the two players.
//
// THE PROBLEM.  Each client runs its own copy of every enemy, and the loot roll
// is authored in the DEATH ABILITY of the copy that died (GA_DeathBase_C:
// DropCurrency -> DropGold/DropGloom/DropItems, fed by
// BP_AICharacter_C.CharacterDeathData @0xDA8 / AdditionalDropItems @0xE08 /
// UseCharacterDataRewards @0xDA0).  With the round-6 death sync both copies
// "die", so both clients roll loot out of their own RNG: the killer sees one set
// of drops and the partner another, and neither of them is the same number.
//
// THE SHAPE (measured, see REPORT.md and the "dropsprobe" log lines):
//   * the copy that actually died rolls: TotalGold / TotalGloom on the live
//     ability instance hold what the game decided, and the spawned currency
//     actors (BP_CurrencyBase_C.Amount @0x2E0) carry the same numbers;
//   * the owner publishes those numbers as ONE event (seq + enemy key);
//   * the peer VETOES its own drop code for that death (the enemy was killed
//     here by copying the peer's death, so the peer owns the loot) and grants a
//     COPY to its own player -- S_AddGold / S_AddGloom on BP_PlayerController_C
//     and AddItemByClass on the item manager, i.e. exactly the writers the data
//     mine calls the tag-free write path (SYNC-DATA-MINE.md section 10/11).
//
// Currency and inventory stay PER PLAYER: nothing here writes a total, it adds
// the same delta to each player separately.  A repeated packet is harmless (the
// event carries a per-sender seq and the receiver keeps a high-water mark); a
// lost packet is repaired (an event is re-published for kDropRetransmit seconds).
//
// Kill switch: DROPS_OFF.txt (toggles().dropsEnabled).  Off means: no "drp"
// section, no grant, and no veto hook installed -- round 6 behaviour, exactly.
//
// Lab verbs (cmd.txt):
//   dropsprobe [secs]            measure one kill: hooks, totals, the wallet
//   dropswallet                  log the local player's gold/gloom balance
//   dropsgrant <gold> <gloom>    grant to our own player (the receiver path)
//   dropskill [tail|nearest]     kill a local enemy through the game's own death
//   dropsdump                    the module state, one line per fact

#include "coop_state.h"

#include "ms2_ue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace coop {
namespace drops {

// [R7b] defined with the real-hit code further down (it needs the reflection
// helpers there); the death hook above the tick needs the declaration.
struct HealthDamageOption;
bool DeathDataOf(ue::UObject* a, int* gloom, int* gold, float* chance, bool* useRewards);
bool RollFromData(const std::string& key, int gold, int gloom, float chance,
                 const std::vector<DropItem>& items);
bool CollectItemDrops(ue::UObject* a, std::vector<DropItem>* out);
std::vector<DropItem> PickItems(const std::vector<DropItem>& cands);

namespace {

using ue::UObject;

// ---------------------------------------------------------------------------
// tunables
// ---------------------------------------------------------------------------
// How long an event keeps riding the packet.  The relay is a latest-value pipe
// at 15 Hz, so 5 s is ~75 chances to deliver it; the receiver drops the repeats
// by seq, which is why a longer window is free.
constexpr double kDropRetransmit = 5.0;
// Bounded: a packet must not grow without limit when a fight kills ten enemies.
constexpr std::size_t kMaxOutEvents = 4;
constexpr std::size_t kMaxItems = 6;
// A veto is per death, and the ability may run its drop steps a moment after
// StartDeath (a montage notify).  Ten seconds covers the slowest death we saw.
constexpr double kDropVetoSeconds = 10.0;
constexpr std::size_t kMaxVeto = 32;
// [R7c] How long the owner waits for the game's own roll before falling back to the
// death data.  A real weapon kill was never measured, and its ability can fire late
// (a long death animation, a queued ability), so the window is generous and the
// double-grant guard below -- not this number -- is what makes it airtight.
constexpr double kDropFallbackSeconds = 3.0;
// One entry per enemy: three enemies dying inside a second each keep their own
// roll (round 7b's single slot + 1.5 s rate limit lost the second and third).
constexpr std::size_t kMaxPendingRolls = 32;

// The two currency writers, verbatim from SYNC-DATA-MINE.md section 10:
//   S_AddGold(Stacks: int) / S_AddGloom(Stacks: int) on BP_PlayerController_C.
constexpr const char* kAddGold = "S_AddGold";
constexpr const char* kAddGloom = "S_AddGloom";

DropsState& D() { return state().drops; }

bool On() { return toggles().dropsEnabled; }

bool Live(UObject* o) { return o != nullptr && ue::IsValid(o); }

// ---------------------------------------------------------------------------
// the drop code we hook
//
// Anchored on GA_DeathBase_C (the parent of GA_Death_AI_C): DropCurrency is the
// death ability's single loot entry point, and the three Drop* below it are what
// it calls.  All four are hooked so the veto holds whichever one the authored
// graph reaches first.  They are blueprint classes, so they only resolve once
// the game has loaded them -- hence the retry in Tick2 (a hook spec that does
// not resolve yet is not an error, it is "not loaded yet").
// ---------------------------------------------------------------------------
struct HookTarget {
    const char* spec;      // "<Class>.<UFunction>"
    const char* alt;       // the enemy subclass's spelling of the same function
    bool              skip;   // a veto hook (pre-skip) or a post hook (read the roll)
    bool              done;
};

// DropCurrency and friends are DECLARED on GA_DeathBase_C, and a blueprint child
// may re-author (override) one of them -- an override is a NEW UFunction on the
// child class, so a hook on the parent's would never fire for it.  Each row
// therefore carries the child's spelling too: resolving both aliases to the same
// UFunction is harmless (the hook table is keyed by FName and RegisterHook
// replaces with the same entry), and an override is then covered as well.
HookTarget g_targets[] = {
    {"GA_DeathBase_C.DropCurrency", "GA_Death_AI_C.DropCurrency", true,  false},
    {"GA_DeathBase_C.DropGold",     "GA_Death_AI_C.DropGold",     true,  false},
    {"GA_DeathBase_C.DropGloom",    "GA_Death_AI_C.DropGloom",    true,  false},
    {"GA_DeathBase_C.DropItems",    "GA_Death_AI_C.DropItems",    true,  false},
    {"GA_DeathBase_C.DropCurrency", "GA_Death_AI_C.DropCurrency", false, false},
};

// Every local death is announced, whether or not the loot code ever runs: a
// version of the verdict that only hears about rolls cannot tell "the game rolled
// nothing" from "the hook never fired", and the two need different fixes.  A
// provisional "NONE" is written here and OVERWRITTEN by RollHook when a roll does
// come in (a hub dummy has no CharacterDeathData and never reaches DropCurrency).
HookTarget g_deathTarget = {"SpartaHealthComponent.StartDeath", nullptr, false, false};
constexpr int kTargetCount = (int)(sizeof(g_targets) / sizeof(g_targets[0]));

// The ability whose death this is.  GetOwningActorFromActorInfo is a
// BlueprintCallable on UGameplayAbility (GameplayAbilities.hpp), so the detour
// does not need to know a single ability offset.
UObject* AbilityOwner(UObject* self) {
    if (!Live(self)) return nullptr;
    ue::UFunction* f = ue::FindFunction(self, "GetOwningActorFromActorInfo");
    if (!f) return nullptr;
    ue::Params p(f);
    if (!ue::Call(self, f, p.data())) return nullptr;
    UObject* o = p.Get<UObject*>("ReturnValue");
    return Live(o) ? o : nullptr;
}

// A plain int property read that does NOT reject a CDO.  ue::Get calls IsValid,
// which rejects a class default object, and a GameplayAbility CAN be
// non-instanced (its state then lives on the CDO) -- so the drop reader cannot
// use it.  The probe logs which object the totals came from, so a shared-CDO
// surprise is visible instead of silently attributing loot to the wrong enemy.
bool ReadInt(UObject* o, const char* prop, int* out) {
    if (!o || !out) return false;
    const std::int32_t off = ue::PropOffset(o, prop);
    if (off < 0) return false;
    return ue::ReadMem(reinterpret_cast<const std::uint8_t*>(o) + off, out, sizeof(*out));
}

// Is this death a mirrored one (the peer killed it, so the peer rolled the loot)?
// Declared here because the death-start hook below asks it before its body.
bool Vetoed(UObject* actor, std::string* tailOut);

// An enemy tail from an actor: the same key coop_combat publishes in "npc".
std::string KeyOf(UObject* a);

// Queue an OWNER death for its fallback roll.  [R7c] this is a QUEUE with one entry
// per enemy, not a single slot with a global rate limit: three enemies dying inside
// a second must each get their own roll (round 7b's 1.5 s limit dropped the second
// and third death).  Only the provisional result file is rate limited.
bool QueueRoll(UObject* dying) {
    DropsState& d = D();
    if (!Live(dying)) return false;
    const std::string key = KeyOf(dying);
    if (key.empty()) return false;
    for (const PendingRoll& r : d.pendingRolls) {
        if (r.tail == key) return false;   // already queued (a second StartDeath)
    }
    if (d.rolledByFallback.Has(key) || d.rolledByGame.Has(key)) return false;

    PendingRoll r;
    r.actor = dying;
    r.tail = key;
    r.at = net::Now() + kDropFallbackSeconds;
    DeathDataOf(dying, &r.gloom, &r.gold, &r.chance, nullptr);
    CollectItemDrops(dying, &r.items);
    if (d.pendingRolls.size() >= kMaxPendingRolls) d.pendingRolls.erase(d.pendingRolls.begin());
    d.pendingRolls.push_back(r);
    d.queuedRolls += 1;
    net::Log("drops: queued the fallback roll for %s (%.0fs): gold=%d gloom=%d chance=%.2f items=%d",
             key.c_str(), kDropFallbackSeconds, r.gold, r.gloom, static_cast<double>(r.chance),
             (int)r.items.size());
    return true;
}

// A local death starts (an OWNER death -- a mirrored one was vetoed, so it never
// gets here): queue its roll, and say so in the provisional result file.
void DeathStartHook(UObject* self, ue::UFunction* /*fn*/, void* /*parms*/) {
    if (!On()) return;
    if (!Live(self)) return;
    ue::UFunction* f = ue::FindFunction(self, "GetOwner");
    if (!f) return;
    ue::Params p(f);
    if (!ue::Call(self, f, p.data())) return;
    UObject* dying = p.Get<UObject*>("ReturnValue");
    if (!Live(dying)) return;
    const std::string cls = ue::GetClassName(dying);
    if (cls.find("Player") != std::string::npos) return;   // our own body, no loot
    std::string tail;
    if (Vetoed(dying, &tail)) return;                      // the peer's death
    if (!QueueRoll(dying)) return;                         // deduped / already rolled

    // The provisional "NONE" is a HINT for the harness, so it may be rate limited;
    // the roll itself never is.
    DropsState& ds = D();
    const double now = net::Now();
    if (now - ds.lastClaimAt < 0.5) return;
    ds.lastClaimAt = now;
    net::WriteFile("result_drops_a.txt",
                   net::Fmt("NONE enemy %s: a local death started and no loot roll has been "
                            "seen yet\n",
                            tail.c_str()));
}

// An enemy tail from an actor: the same key coop_combat publishes in "npc".
std::string KeyOf(UObject* a) {
    if (!Live(a)) return std::string();
    return net::Tail(a);
}

// Is this death a mirrored one (the peer killed it, so the peer rolled the loot)?
// Expired entries are dropped here, so the table cannot grow.
bool Vetoed(UObject* actor, std::string* tailOut) {
    if (!actor) return false;
    DropsState& d = D();
    const double now = net::Now();
    for (std::size_t i = 0; i < d.veto.size();) {
        if (d.veto[i].until < now || !Live(d.veto[i].actor)) {
            d.veto[i] = d.veto.back();
            d.veto.pop_back();
            continue;
        }
        if (d.veto[i].actor == actor) {
            if (tailOut) *tailOut = d.veto[i].tail;
            return true;
        }
        ++i;
    }
    return false;
}

// The veto.  Returning true skips the original UFunction call, so the game
// spawns no currency bag, no item pickup and no gold/gloom for this death.
bool VetoHook(UObject* self, ue::UFunction* fn, void* /*parms*/) {
    if (!On()) return false;
    UObject* dying = AbilityOwner(self);
    std::string tail;
    DropsState& d = D();
    const std::string fnName = ue::GetName((UObject*)fn);

    if (Vetoed(dying, &tail)) {
        for (auto& v : d.veto) {
            if (v.actor == dying) v.fired += 1;
        }
        d.vetoed += 1;
        net::Log("drops: VETO %s on %s -- the peer rolled this death's loot, so this copy "
                 "must not", fnName.c_str(), tail.empty() ? "(unknown)" : tail.c_str());
        return true;
    }

    // [R7c] The other half of the double-grant guard: the owner already rolled this
    // death through the data fallback, and the game's ability woke up LATE (a long
    // death animation, a queued ability).  Granting its numbers as well would pay the
    // player twice, so the late roll is vetoed exactly like a mirrored one.
    if (Live(dying)) {
        const std::string key = KeyOf(dying);
        if (!key.empty() && d.rolledByFallback.Has(key)) {
            d.alreadyRolledVetoed += 1;
            net::Log("drops: VETO late game roll %s for %s -- the fallback already granted this "
                     "death's loot (once only)", fnName.c_str(), key.c_str());
            return true;
        }
    }
    return false;
}

// The read.  Runs POST: by then the ability has decided, and TotalGold /
// TotalGloom carry what DropGold/DropGloom put in (a currency actor's Amount is
// the same number -- the probe logs both to keep that claim honest).
void RollHook(UObject* self, ue::UFunction* /*fn*/, void* /*parms*/) {
    if (!On()) return;
    UObject* dying = AbilityOwner(self);
    std::string tail;
    if (dying && Vetoed(dying, &tail)) return;   // cannot happen: the veto ran first

    // A player's own death also runs a GA_Death_* ability; it has no loot.
    if (!dying) return;
    const std::string cls = ue::GetClassName(dying);
    if (cls.find("Player") != std::string::npos || cls.find("BP_Player") != std::string::npos) return;

    int gold = 0, gloom = 0;
    if (!ReadInt(self, "TotalGold", &gold) || !ReadInt(self, "TotalGloom", &gloom)) {
        if (D().probe) {
            net::Log("dropsprobe: %s has no TotalGold/TotalGloom (class %s) -- cannot read the roll",
                     ue::GetName(self).c_str(), ue::GetClassName(self).c_str());
        }
        return;
    }
    if (gold < 0) gold = 0;
    if (gloom < 0) gloom = 0;

    const std::string key = KeyOf(dying);
    DropsState& d = D();
    // [R7c] the fallback already granted this death (the game's roll was vetoed at
    // the dispatch, but a POST hook still runs after a skip): never publish twice.
    if (!key.empty() && d.rolledByFallback.Has(key)) {
        net::Log("drops: the game's late roll for %s is ignored -- the fallback already granted "
                 "it", key.c_str());
        return;
    }
    d.calls += 1;
    if (d.probe) {
        d.probeFires += 1;
        d.lastKey = key;
        d.lastGold = gold;
        d.lastGloom = gloom;
        net::Log("dropsprobe: dropCurrency fired ability=%s dying=%s gold=%d gloom=%d",
                 ue::GetName(self).c_str(), key.empty() ? "(none)" : key.c_str(), gold, gloom);
        // What appeared in the world next to the corpse: the currency bag is a
        // world pickup (ABP_CurrencyBase_C.Amount), item drops are pickups too.
        // Sampling it is what tells "grant on pickup" from "direct add".
        d.probePos = net::Loc(dying);
        d.probeScanAt = net::Now() + 1.5;
        d.probeScan = true;
    }
    if (gold <= 0 && gloom <= 0) {
        // Nothing materialised (no gold on this character, or the chance rolled
        // against it).  An empty event is not worth a packet, but the verdict has
        // to be able to tell "nothing to send" from "the hook never ran".
        if (d.probe) net::Log("dropsprobe: nothing to publish for %s", key.c_str());
        net::WriteFile("result_drops_a.txt",
                       net::Fmt("NONE enemy %s: the roll was empty (gold=0 gloom=0), "
                                "nothing published\n",
                                key.empty() ? "(unknown)" : key.c_str()));
        return;
    }

    d.outSeq += 1;
    DropEvent e;
    e.seq = d.outSeq;
    e.key = key;
    e.gold = gold;
    e.gloom = gloom;
    e.born = net::Now();
    d.rolled += 1;
    // The game rolled for this death: the owner's data roll must not fire as well.
    if (!key.empty()) d.rolledByGame.Add(key);
    d.rolledAt.Set(key, net::Now());
    for (std::size_t i = 0; i < d.pendingRolls.size(); ++i) {
        if (d.pendingRolls[i].tail != key) continue;
        d.pendingRolls.erase(d.pendingRolls.begin() + (long long)i);
        d.fallbackCancelled += 1;
        net::Log("drops: the game rolled for %s first -- the fallback stands down", key.c_str());
        break;
    }
    d.out.push_back(e);
    if (d.out.size() > kMaxOutEvents) d.out.erase(d.out.begin());
    net::Log("drops: rolled %d gold / %d gloom for %s (seq %lld) -- the peer will grant "
             "the same to its own player", gold, gloom, key.empty() ? "(unknown)" : key.c_str(),
             e.seq);
    // The killer's own claim, for the harness: what this client rolled and put on
    // the wire.  The receiver writes result_drops.txt with what it granted.
    net::WriteFile("result_drops_a.txt",
                   net::Fmt("PASS enemy %s: rolled gold=%d gloom=%d, published seq %lld\n",
                            key.empty() ? "(unknown)" : key.c_str(), gold, gloom, e.seq));
}

// ---------------------------------------------------------------------------
// our player's wallet
//
// Gold and gloom are ITEM STACKS in this game (the currency bags grant them
// through the item manager), so the balance is read the way every other
// inventory question is: GetTotalItemCountByDefinition on the item definition.
// The item definitions are resolved by bare class name and cached, because the
// engine layer has no asset loader (net::Find with requestLoad is a no-op here).
// ---------------------------------------------------------------------------
struct Wallet {
    int gold = -1;    // -1 = not readable
    int gloom = -1;
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

UObject* ItemManager() {
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return nullptr;
    static UObject* mgrClass = nullptr;
    if (!Live(mgrClass)) {
        mgrClass = net::Find("/Script/Sparta.SpartaItemManagerComponent", false);
    }
    if (!Live(mgrClass)) return nullptr;
    ue::UFunction* f = ue::FindFunction(pawn, "GetComponentByClass");
    if (!f) return nullptr;
    ue::Params p(f);
    p.Set<ue::UClass*>("ComponentClass", reinterpret_cast<ue::UClass*>(mgrClass));
    if (!ue::Call(pawn, f, p.data())) return nullptr;
    UObject* im = p.Get<UObject*>("ReturnValue");
    return Live(im) ? im : nullptr;
}

int ItemCount(UObject* im, UObject* def) {
    if (!Live(im) || !Live(def)) return -1;
    ue::UFunction* f = ue::FindFunction(im, "GetTotalItemCountByDefinition");
    if (!f) return -1;
    ue::Params p(f);
    p.Set<ue::UClass*>("ItemDef", reinterpret_cast<ue::UClass*>(def));
    if (!ue::Call(im, f, p.data())) return -1;
    return p.Get<std::int32_t>("ReturnValue");
}

// The two currency definitions, resolved once and cached (they never unload).
UObject* GoldDef() {
    static UObject* slot = nullptr;
    return CurrencyDef("ID_Gold_C", "/Game/Sparta/Items/Pickups/ID_Gold.ID_Gold_C", &slot);
}

UObject* GloomDef() {
    static UObject* slot = nullptr;
    return CurrencyDef("ID_Gloom_C", "/Game/Sparta/Items/Pickups/ID_Gloom.ID_Gloom_C", &slot);
}

Wallet ReadWallet(UObject** goldDef, UObject** gloomDef) {
    Wallet w;
    UObject* g = GoldDef();
    UObject* gl = GloomDef();
    if (goldDef) *goldDef = g;
    if (gloomDef) *gloomDef = gl;
    UObject* im = ItemManager();
    if (!im) return w;
    if (goldDef && *goldDef) w.gold = ItemCount(im, *goldDef);
    if (gloomDef && *gloomDef) w.gloom = ItemCount(im, *gloomDef);
    return w;
}

// ---------------------------------------------------------------------------
// granting INTO our own player
//
// Two writers, both documented (SYNC-DATA-MINE.md section 10): the controller's
// S_AddGold/S_AddGloom for the currencies, the item manager's AddItemByClass for
// anything else the death dropped.  A grant never writes a total and never
// touches the peer: it is the same delta the owner got.
// ---------------------------------------------------------------------------
bool CallControllerInt(const char* fnName, int n) {
    UObject* pc = net::PC();
    if (!Live(pc)) return false;
    ue::UFunction* f = ue::FindFunction(pc, fnName);
    if (!f) return false;
    ue::Params p(f);
    p.Set<std::int32_t>("Stacks", n);
    return ue::Call(pc, f, p.data());
}

bool AddItemByClass(UObject* def, int count) {
    UObject* im = ItemManager();
    if (!Live(im) || !Live(def) || count <= 0) return false;
    ue::UFunction* f = ue::FindFunction(im, "AddItemByClass");
    if (!f) return false;
    ue::Params p(f);
    p.Set<ue::UClass*>("ItemDef", reinterpret_cast<ue::UClass*>(def));
    p.Set<std::int32_t>("StackCount", count);
    return ue::Call(im, f, p.data());
}

// Grant `gold`/`gloom` to OUR OWN player.  Used by the receiver (a copy of the
// peer's roll) and by the OWNER (its own kill): measured, a co-op kill produces no
// world pickup at all -- the death ability that would spawn the currency bag never
// runs -- so if the owner did not grant itself, the killer would get nothing while
// the partner got the loot.  Immediate on both sides is also the only symmetric
// choice: there is no bag to walk to on either client.
bool GrantCurrency(int gold, int gloom, int* gotGold, int* gotGloom) {
    int g = 0, gl = 0;
    if (gold > 0) {
        if (CallControllerInt(kAddGold, gold)) {
            g = gold;
        } else if (AddItemByClass(GoldDef(), gold)) {
            g = gold;
        } else {
            net::Log("drops: S_AddGold(%d) failed and the item path failed too", gold);
        }
    }
    if (gloom > 0) {
        if (CallControllerInt(kAddGloom, gloom)) {
            gl = gloom;
        } else if (AddItemByClass(GloomDef(), gloom)) {
            gl = gloom;
        } else {
            net::Log("drops: S_AddGloom(%d) failed and the item path failed too", gloom);
        }
    }
    if (gotGold) *gotGold = g;
    if (gotGloom) *gotGloom = gl;
    return (g == gold) && (gl == gloom);
}

// The last item of a full name: "/Game/X/ID_Gold.ID_Gold_C" stays as it is (an
// asset path is what both peers share), but a bare "Class /Game/...Foo_C" is
// normalised to the path so the wire carries one spelling only.
std::string AssetPath(UObject* o) {
    const std::string fn = ue::GetFullName(o);
    const std::string::size_type sp = fn.find_last_of(" \t");
    if (sp == std::string::npos) return std::string();
    const std::string tail = fn.substr(sp + 1);
    return (tail.size() > 1 && tail[0] == '/') ? tail : std::string();
}

// Find an item definition on THIS client from the path the peer sent.
UObject* ResolveItem(const std::string& path) {
    if (path.empty()) return nullptr;
    UObject* o = ue::FindObject(path.c_str());
    if (Live(o)) return o;
    // Fall back to the bare class name of the same path ("...ID_Gold_C").
    const std::string::size_type dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        o = reinterpret_cast<UObject*>(ue::FindClass(path.substr(dot + 1).c_str()));
        if (Live(o)) return o;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// wire helpers
// ---------------------------------------------------------------------------
// The event list is an ARRAY OF ARRAYS of scalars and one quoted item string:
// net::SplitFields (coop_combat) splits on commas outside quotes, so a nested
// array would not parse -- an item list therefore rides as "path@count|path@count".
void AppendItems(const std::vector<DropItem>& items, std::string* out) {
    for (std::size_t i = 0; i < items.size() && i < kMaxItems; ++i) {
        if (i) out->push_back('|');
        *out += items[i].path;
        out->push_back('@');
        *out += net::Fmt("%d", items[i].count);
    }
}

void ParseItems(const std::string& s, std::vector<DropItem>* out) {
    std::size_t start = 0;
    while (start < s.size() && out->size() < kMaxItems) {
        std::size_t bar = s.find('|', start);
        if (bar == std::string::npos) bar = s.size();
        const std::string one = s.substr(start, bar - start);
        const std::size_t at = one.rfind('@');
        if (at != std::string::npos && at > 0) {
            DropItem it;
            it.path = one.substr(0, at);
            it.count = atoi(one.substr(at + 1).c_str());
            if (!it.path.empty() && it.count > 0) out->push_back(it);
        }
        start = bar + 1;
    }
}

// The two SplitFields/SplitEntries helpers live in coop_combat's anonymous
// namespace; these two are the same algorithms, kept local so this file owns no
// other module's internals.
void SplitFields(const std::string& body, std::vector<std::string>* out) {
    std::string cur;
    bool quoted = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '"') {
            quoted = !quoted;
            cur.push_back(c);
            continue;
        }
        if (c == ',' && !quoted) {
            if (!cur.empty()) out->push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out->push_back(cur);
}

void StripOuter(std::string* s) {
    if (s->size() < 2 || s->front() != '[') return;
    int depth = 0;
    bool quoted = false;
    for (std::size_t i = 0; i < s->size(); ++i) {
        const char c = (*s)[i];
        if (c == '"') quoted = !quoted;
        if (quoted) continue;
        if (c == '[') ++depth;
        else if (c == ']') {
            --depth;
            if (depth == 0) {
                if (i == s->size() - 1) *s = s->substr(1, s->size() - 2);
                return;
            }
        }
    }
}

void SplitEntries(const std::string& section, std::vector<std::string>* out) {
    std::string body = section;
    StripOuter(&body);
    int depth = 0;
    bool quoted = false;
    std::size_t start = std::string::npos;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (quoted) continue;
        if (c == '[') {
            if (depth == 0) start = i;
            ++depth;
        } else if (c == ']') {
            if (depth > 0) {
                --depth;
                if (depth == 0 && start != std::string::npos) {
                    out->push_back(body.substr(start, i - start + 1));
                    start = std::string::npos;
                }
            }
        }
    }
}

std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

double NumOf(const std::string& s) { return atof(s.c_str()); }

}  // namespace

// [R7b] the probe-only trace lives further down (it is a probe, not the sync
// path); Tick2 only needs these two.
bool TraceOn();
void DrainTrace();

// ===========================================================================
// the tick
// ===========================================================================
void Tick2() {
    DropsState& d = D();

    // 1. the drop hooks.  They are Blueprint UFunctions, so they only resolve
    // once the game has loaded the ability class; retry every 2 s until the
    // whole set is in, then stop (the count is logged when it changes).
    if (On()) {
        const double now = net::Now();
        if (now >= d.hookRetryAt) {
            d.hookRetryAt = now + 2.0;
            int ok = 0;
            for (int i = 0; i < kTargetCount; ++i) {
                HookTarget& t = g_targets[i];
                if (t.done) {
                    ++ok;
                    continue;
                }
                const bool installed =
                    (t.skip ? ue::HookPreSkip(t.spec, &VetoHook) : ue::HookPost(t.spec, &RollHook)) ||
                    (t.alt && (t.skip ? ue::HookPreSkip(t.alt, &VetoHook)
                                      : ue::HookPost(t.alt, &RollHook)));
                if (installed) {
                    t.done = true;
                    ++ok;
                }
            }
            if (ok != d.hooks) {
                d.hooks = ok;
                net::Log("drops: %d of %d drop hook(s) installed (%s)", ok, kTargetCount,
                         ok == kTargetCount ? "the roll and the veto are live"
                                            : "waiting for GA_DeathBase_C to load");
            }
            if (!g_deathTarget.done) {
                if (ue::HookPost(g_deathTarget.spec, &DeathStartHook)) {
                    g_deathTarget.done = true;
                    net::Log("drops: local-death hook on %s", g_deathTarget.spec);
                }
            }
        }
    }

    // 2. expire the veto table and the retransmit window.
    const double now = net::Now();
    for (std::size_t i = 0; i < d.veto.size();) {
        if (d.veto[i].until < now || !Live(d.veto[i].actor)) {
            d.veto[i] = d.veto.back();
            d.veto.pop_back();
            continue;
        }
        ++i;
    }
    d.out.erase(std::remove_if(d.out.begin(), d.out.end(),
                               [now](const DropEvent& e) { return now - e.born > kDropRetransmit; }),
                d.out.end());

    // 3. the probe's wallet sampler: what the local player's balance DOES over
    // the kill is the measurement the task starts from.
    if (d.probe && now < d.probeUntil) {
        const Wallet w = ReadWallet(nullptr, nullptr);
        const int sum = (w.gold < 0 ? 0 : w.gold) + (w.gloom < 0 ? 0 : w.gloom);
        if (sum != d.lastWallet) {
            d.lastWallet = sum;
            net::Log("dropsprobe: wallet gold=%d gloom=%d (sum %d)", w.gold, w.gloom, sum);
        }
    }

    // 4. what the death spawned next to the corpse: every live actor whose class
    // looks like a currency bag or a pickup within 6 m of the death point, with
    // the amount the bag carries.  This answers "pickup the owner must walk to, or
    // a direct add" for the enemy under test.
    if (d.probeScan && now >= d.probeScanAt) {
        d.probeScan = false;
        d.probeScans += 1;
        int found = 0;
        for (const char* cls : {"BP_CurrencyBase_C", "BP_PickupParticleFX_C", "BP_PickupBase_C"}) {
            for (UObject* o : ue::FindAllOf(cls)) {
                if (!Live(o)) continue;
                const Vec3 p = net::Loc(o);
                const double dx = p.X - d.probePos.X;
                const double dy = p.Y - d.probePos.Y;
                const double dz = p.Z - d.probePos.Z;
                const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (dist > 600.0) continue;
                int amount = 0;
                ReadInt(o, "Amount", &amount);
                net::Log("dropsprobe: spawned %s at %.0f uu from the death, Amount=%d",
                         ue::GetClassName(o).c_str(), dist, amount);
                found += 1;
                if (found >= 12) break;
            }
            if (found >= 12) break;
        }
        net::Log("dropsprobe: corpse scan %d: %d drop actor(s) within 6 m of the death point",
                 d.probeScans, found);
    }

    // 5. [R7b] the outcome of a real hit, read one tick later (a gameplay effect
    // can be what actually applies the damage).
    if (d.pendingHit && now - d.pendingAt > 0.4) {
        d.pendingHit = false;
        bool dead = false;
        const double hp = Live(d.pendingActor) ? combat::HealthNorm(d.pendingActor, &dead) : -1.0;
        net::Log("dropshit: %.1fs later %s hp %.3f -> %.3f dead=%d | local drops=%s",
                 now - d.pendingAt, d.pendingTail.c_str(), d.pendingHp0, hp, dead ? 1 : 0,
                 Status().c_str());
    }

    // 6. [R7b/R7c] the owner's fallback rolls, one queue entry per death, and the
    // death-ability trace.  A death is rolled by EXACTLY ONE of the two paths:
    // the game's own ability (rollsByGame) or this fallback (rollsByFallback), and
    // whichever comes second stands down (see VetoHook/RollHook for the game side).
    for (std::size_t i = 0; i < d.pendingRolls.size();) {
        PendingRoll r = d.pendingRolls[i];
        if (now < r.at) {
            ++i;
            continue;
        }
        d.pendingRolls.erase(d.pendingRolls.begin() + (long long)i);   // take it out first
        if (r.tail.empty()) continue;
        if (d.rolledByGame.Has(r.tail)) {
            d.fallbackCancelled += 1;
            net::Log("drops: %s was already rolled by the game -- the fallback stands down",
                     r.tail.c_str());
            continue;
        }
        if (d.rolledByFallback.Has(r.tail)) {
            net::Log("drops: %s was already rolled by the fallback -- not rolling it twice",
                     r.tail.c_str());
            continue;
        }
        if (Vetoed(r.actor, nullptr)) continue;
        if (RollFromData(r.tail, r.gold, r.gloom, r.chance, PickItems(r.items))) d.rolled += 0;   // the counters above already recorded it
    }
    if (TraceOn()) DrainTrace();
}

void Reset() {
    // Every pointer in here dies with the world; the counters are session facts
    // and stay (the verdict reads them), the events do not survive a map load.
    DropsState& d = D();
    d.out.clear();
    d.veto.clear();
    d.lastKey.clear();
    d.lastGold = 0;
    d.lastGloom = 0;
    d.lastWallet = -1;
    d.probeScan = false;
    // lastPeerSeq is deliberately KEPT: ApplyPeer re-reads the same peer line every
    // tick, so clearing the high-water mark after a map load would let an event
    // from before the load be granted a second time.  A peer that RESTARTS (its own
    // seq begins again at 1) is recognised in OnPeerLine instead.
}

// ===========================================================================
// the wire
// ===========================================================================
void Publish(std::string* line) {
    if (!line) return;
    DropsState& d = D();
    if (!On() || d.out.empty()) return;   // steady state: the packet is unchanged

    std::string body;
    std::size_t n = 0;
    for (const DropEvent& e : d.out) {
        if (n >= kMaxOutEvents) break;
        if (n) body.push_back(',');
        std::string items;
        AppendItems(e.items, &items);
        net::AppendFmt(&body, "[%lld,\"%s\",%d,%d,\"%s\"]", e.seq, e.key.c_str(), e.gold, e.gloom,
                       items.c_str());
        n += 1;
    }
    if (n == 0) return;
    for (DropEvent& e : d.out) {
        if (!e.sent) {
            e.sent = true;
            d.sentEvents += 1;
        }
    }
    *line += ",\"drp\":[";
    *line += body;
    *line += ']';
}

void OnPeerLine(const std::string& line) {
    if (!On()) return;
    std::string sec;
    if (!net::Section(line, "drp", &sec)) return;

    std::vector<std::string> entries;
    SplitEntries(sec, &entries);

    DropsState& d = D();
    for (const std::string& entry : entries) {
        std::vector<std::string> f;
        std::string body = entry;
        StripOuter(&body);
        SplitFields(body, &f);
        if (f.size() < 4) continue;

        const long long seq = (long long)NumOf(f[0]);
        const std::string key = Unquote(f[1]);
        const int gold = (int)NumOf(f[2]);
        const int gloom = (int)NumOf(f[3]);
        std::vector<DropItem> items;
        if (f.size() > 4) ParseItems(Unquote(f[4]), &items);

        // Dedupe: the sender retransmits for a few seconds, and the relay may
        // repeat a line, so the seq is the only thing that decides.  A seq that is
        // FAR behind the mark is not a repeat -- it is the peer having restarted
        // its session (its counter began again), and that must be accepted.
        if (seq != 0 && seq <= d.lastPeerSeq) {
            if (d.lastPeerSeq - seq > 8) {
                net::Log("drops: the peer's seq jumped back %lld -> %lld: new session, "
                         "accepting it", d.lastPeerSeq, seq);
                d.lastPeerSeq = 0;
            } else {
                d.dupSkipped += 1;
                continue;
            }
        }
        if (seq != 0) d.lastPeerSeq = seq;
        if (gold <= 0 && gloom <= 0 && items.empty()) continue;

        // The wallet before, so the verdict can compare what the GAME says our
        // balance did against what the peer says it rolled.  A counter that only
        // we increment would be a log-only claim; this is the game's own getter.
        const Wallet before = ReadWallet(nullptr, nullptr);
        int gotGold = 0, gotGloom = 0, gotItems = 0;
        GrantCurrency(gold, gloom, &gotGold, &gotGloom);
        for (const DropItem& it : items) {
            UObject* def = ResolveItem(it.path);
            if (!def) {
                net::Log("drops: item %s is not loaded on this client -- cannot grant %d",
                         it.path.c_str(), it.count);
                continue;
            }
            if (AddItemByClass(def, it.count)) gotItems += it.count;
            else net::Log("drops: AddItemByClass(%s, %d) failed", it.path.c_str(), it.count);
        }
        const Wallet after = ReadWallet(nullptr, nullptr);

        d.gotEvents += 1;
        d.grantedGold += gotGold;
        d.grantedGloom += gotGloom;
        d.grantedItems += gotItems;
        net::Log("drops: granted gold=%d gloom=%d items=%d from the peer's roll for %s (seq %lld); "
                 "wallet gold %d->%d gloom %d->%d",
                 gotGold, gotGloom, gotItems, key.empty() ? "(unknown)" : key.c_str(), seq,
                 before.gold, after.gold, before.gloom, after.gloom);

        // The verdict file: the receiver's answer to "did I get what the owner
        // rolled, without rolling it myself".  The wallet numbers are INFORMATIONAL:
        // measured, GetTotalItemCountByDefinition(ID_Gold) is the count of gold
        // *stacks* in the bag, not the spendable balance (it reads 1-2 while
        // CumulativeGold is ~1200), so the claim rests on the game accepting the
        // writer call (S_AddGold/S_AddGloom returned true) plus the amounts.
        const bool amountsOk = (gotGold == gold) && (gotGloom == gloom);
        net::WriteFile("result_drops.txt",
                       net::Fmt("%s enemy %s: granted gold=%d gloom=%d items=%d "
                                "(peer rolled gold=%d gloom=%d items=%d, seq %lld); "
                                "stacks gold %d->%d gloom %d->%d; own roll vetoed=%d dup=%d\n",
                                amountsOk ? "PASS" : "FAIL",
                                key.empty() ? "(unknown)" : key.c_str(), gotGold, gotGloom,
                                gotItems, gold, gloom, (int)items.size(), seq,
                                before.gold, after.gold, before.gloom, after.gloom, d.vetoed,
                                d.dupSkipped));
    }
}

// ===========================================================================
// [R7b] a REAL player hit -- so the game's own drop code runs on the owner
//
// The round-7 harness killed through `StartDeath`, which only flips the health
// attribute: the death ability sees no instigator, so its `WasKilledByPlayer`
// gate keeps the loot code closed and NOBODY rolls (round-7 measurement).  A real
// hit has to go through the hittable interface the game itself uses:
//
//   ISpartaHittable::SpartaApplyHit(FHitResult, ASpartaCharacter* Instigator,
//                                   USpartaHitPayload* Payload, AActor* Causer)
//
// with a payload whose `HealthDamage.Type = Float` (EHealthDamageType::Float = 0)
// and `FloatValue = damage`.  A fresh payload comes from the class default
// object's own `GetDuplicate(Outer)` -- the game's own factory, no UObject
// construction from this module.
// ===========================================================================
struct HealthDamageOption {
    std::uint8_t Type;      // EHealthDamageType: 0 = Float
    float FloatValue;       // 0x04
    float Multiplier;       // 0x08
};

// The three scalars of FSpartaCharacterDeathData, read out of the actor's inline
// struct (CharacterDeathData @0xDA8).  Returns false when the class has neither
// the struct nor the fields.
bool DeathDataOf(UObject* a, int* gloom, int* gold, float* chance, bool* useRewards) {
    if (!Live(a)) return false;
    const std::int32_t base = ue::PropOffset(a, "CharacterDeathData");
    if (base < 0) return false;
    std::int32_t g = 0, go = 0;
    float c = 0.0f;
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(a) + base;
    if (!ue::ReadMem(p + 0x0, &g, sizeof(g))) return false;
    ue::ReadMem(p + 0x4, &go, sizeof(go));
    ue::ReadMem(p + 0x8, &c, sizeof(c));
    if (gloom) *gloom = g;
    if (gold) *gold = go;
    if (chance) *chance = c;
    if (useRewards) *useRewards = ue::GetBool(a, "UseCharacterDataRewards");
    return true;
}

// A fresh USpartaHitPayload, made by the game: CDO.GetDuplicate(Outer).
UObject* NewPayload(UObject* outer) {
    static UObject* cdo = nullptr;
    if (!Live(cdo)) {
        cdo = ue::FindObject("/Script/Sparta.Default__SpartaHitPayload");
        if (!cdo || !ue::PlausiblePtr(cdo)) {
            UObject* cls = reinterpret_cast<UObject*>(ue::FindClass("SpartaHitPayload"));
            if (cls) cdo = ue::GetCDO(reinterpret_cast<ue::UClass*>(cls));
        }
    }
    if (!cdo || !ue::PlausiblePtr(cdo)) return nullptr;
    ue::UFunction* f = ue::FindFunction(cdo, "GetDuplicate");
    if (!f) return nullptr;
    ue::Params p(f);
    p.Set<UObject*>("Outer", outer ? outer : net::Pawn());
    if (!ue::Call(cdo, f, p.data())) return nullptr;
    UObject* out = p.Get<UObject*>("ReturnValue");
    return (out && ue::IsValid(out)) ? out : nullptr;
}

// Sets the payload's health damage to "Float, `amount`".  Returns false when the
// property or the setter is missing (then the caller says so in the log).
bool SetPayloadDamage(UObject* payload, double amount) {
    if (!Live(payload)) return false;
    ue::UFunction* f = ue::FindFunction(payload, "SetHealthDamage");
    if (f) {
        HealthDamageOption opt{};
        opt.Type = 0;   // EHealthDamageType::Float
        opt.FloatValue = static_cast<float>(amount);
        opt.Multiplier = 1.0f;
        ue::Params p(f);
        p.Set<HealthDamageOption>("InHealthDamage", opt);
        if (ue::Call(payload, f, p.data())) return true;
    }
    // No setter (or it refused): write the struct in place.
    const std::int32_t off = ue::PropOffset(payload, "HealthDamage");
    if (off < 0) return false;
    HealthDamageOption opt{};
    opt.Type = 0;
    opt.FloatValue = static_cast<float>(amount);
    opt.Multiplier = 1.0f;
    return ue::WriteMem(reinterpret_cast<std::uint8_t*>(payload) + off, &opt, sizeof(opt));
}

// [R7b] The game's own roll, reproduced on the OWNER when its death ability does
// not run.  Measured: `USpartaHealthComponent::StartDeath` sets the death state and
// the enemy's BP plays its death animation, but GA_Death_AI_C -- the ability that
// owns DropCurrency -- is activated from the damage/ASC path, which a health write
// never goes through: 15 traced ability steps, zero of them ran, for both a hub
// cultist and a real BP_Horde_Brigand.  So the owner rolls from the enemy's OWN
// data, with the game's own numbers and the game's own chance:
//     gloom = GloomOnDeath                       (always)
//     gold  = GoldOnDeath, with probability GoldDropChance
// That is the same distribution the ability would produce; the ability's hook above
// still wins whenever the game does roll (real weapon damage), and this fallback
// then cancels itself.
int g_dataRolls = 0;

// A small LCG on the frame clock: this decision is the OWNER's alone (the peer only
// ever grants what it is told), so it does not have to match anything else.
bool RollChance(double p) {
    static std::uint64_t s = 0x9E3779B97F4A7C15ull;
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    s ^= static_cast<std::uint64_t>(net::Now() * 1000.0);
    const double u = static_cast<double>((s >> 33) & 0xFFFFFF) / static_cast<double>(0x1000000);
    return u < p;
}

// Roll once from captured death data and publish it.  Returns true when published.
bool RollFromData(const std::string& key, int gold, int gloom, float chance,
                  const std::vector<DropItem>& items) {
    if (gloom <= 0 && gold <= 0 && items.empty()) return false;
    const int g = (gold > 0 && RollChance(static_cast<double>(chance))) ? gold : 0;
    const int gl = gloom > 0 ? gloom : 0;
    if (g == 0 && gl == 0 && items.empty()) return false;

    DropsState& d = D();
    // The key is now permanently attributed to this roll (until a world change):
    // a LATE game roll for it is vetoed, and no second fallback can fire.
    if (!key.empty()) {
        d.rolledByFallback.Add(key);
        d.rolledAt.Set(key, net::Now());
    }
    d.outSeq += 1;
    DropEvent e;
    e.seq = d.outSeq;
    e.key = key;
    e.gold = g;
    e.gloom = gl;
    e.items = items;
    e.born = net::Now();
    d.rolled += 1;
    g_dataRolls += 1;
    d.grantedGold += (g > 0 ? g : 0);
    d.grantedGloom += (gl > 0 ? gl : 0);
    d.out.push_back(e);
    if (d.out.size() > kMaxOutEvents) d.out.erase(d.out.begin());
    // The killer gets its share immediately as well: measured, a co-op kill spawns
    // no world pickup (the death ability never runs), so there is nothing to walk to
    // and no bag that could grant it later.
    int gotGold = 0, gotGloom = 0;
    GrantCurrency(g, gl, &gotGold, &gotGloom);
    // The item side of the same roll: the owner takes its own drops too, so the two
    // players stay symmetric (see GrantCurrency's comment).
    int gotItems = 0;
    for (DropItem it : items) {
        if (UObject* def = ResolveItem(it.path)) {
            if (AddItemByClass(def, it.count)) gotItems += it.count;
        } else {
            net::Log("drops: item %s is not loaded on this client -- cannot grant it", it.path.c_str());
        }
    }
    net::Log("drops: rolled FROM DEATH DATA for %s: gold=%d gloom=%d items=%d (chance %.2f -> %s, "
             "seq %lld) -- the death ability did not roll for this kill; granted to self %d/%d/%d",
             key.empty() ? "(unknown)" : key.c_str(), g, gl, gotItems,
             static_cast<double>(chance), g > 0 ? "hit" : "miss", e.seq, gotGold, gotGloom,
             gotItems);
    net::WriteFile("result_drops_a.txt",
                   net::Fmt("PASS enemy %s: rolled gold=%d gloom=%d items=%d, published seq %lld "
                            "(from CharacterDeathData: goldOnDeath=%d chance=%.2f "
                            "gloomOnDeath=%d)\n",
                            key.empty() ? "(unknown)" : key.c_str(), g, gl, (int)items.size(),
                            e.seq, gold, static_cast<double>(chance), gloom));
    return true;
}

// --- [R7b] the death-ability trace ----------------------------------------
// Which of the authored death steps actually run?  A hook per step, and the names
// are resolved on the next tick (a detour must not format strings).  Probe only.
bool g_traceOn = false;
bool TraceOn() { return g_traceOn; }
struct TraceCall {
    UObject* fn = nullptr;
    UObject* self = nullptr;
};
constexpr int kTraceRing = 96;
TraceCall g_traceRing[kTraceRing];
int g_traceAt = 0;      // write cursor
int g_traceCount = 0;   // total captured

struct TraceTarget {
    const char* spec;
    bool done;
};
TraceTarget g_traceTargets[] = {
    {"GA_Death_AI_C.InitializeDeath", true},
    {"GA_Death_AI_C.HandleDeathEvent", true},
    {"GA_Death_AI_C.GrantRewardAndHideHealthBar", true},
    {"GA_Death_AI_C.DeathToEnemyOnly", true},
    {"GA_Death_AI_C.WasKilledByPlayer", true},
    {"GA_Death_AI_C.EndDeath", true},
    {"GA_DeathBase_C.DropCurrency", true},
    {"GA_DeathBase_C.DropGold", true},
    {"GA_DeathBase_C.DropGloom", true},
    {"GA_DeathBase_C.DropItems", true},
    {"GA_DeathBase_C.ShouldDropGlobal", true},
    {"GA_DeathBase_C.ShouldDropGold", true},
    {"GA_DeathBase_C.ShouldDropGloom", true},
    {"GA_DeathBase_C.ShouldDropItems", true},
    {"GA_DeathBase_C.GetCharDeathData", true},
};
constexpr int kTraceTargets = (int)(sizeof(g_traceTargets) / sizeof(g_traceTargets[0]));

void TraceHookFn(UObject* self, ue::UFunction* fn, void* /*parms*/) {
    if (!g_traceOn) return;
    g_traceRing[g_traceAt].fn = (UObject*)fn;
    g_traceRing[g_traceAt].self = self;
    g_traceAt = (g_traceAt + 1) % kTraceRing;
    g_traceCount += 1;
}

int InstallTraceHooks() {
    int ok = 0;
    for (int i = 0; i < kTraceTargets; ++i) {
        if (g_traceTargets[i].done) {
            ++ok;
            continue;
        }
        if (ue::HookPost(g_traceTargets[i].spec, &TraceHookFn)) {
            g_traceTargets[i].done = true;
            ++ok;
        }
    }
    return ok;
}

void DrainTrace() {
    static int lastCount = 0;
    if (g_traceCount == lastCount) return;
    lastCount = g_traceCount;
    for (int i = 0; i < kTraceRing; ++i) {
        if (!g_traceRing[i].fn) continue;
        UObject* dying = AbilityOwner(g_traceRing[i].self);
        net::Log("dropstrace: %s on %s owner=%s", ue::GetName(g_traceRing[i].fn).c_str(),
                 ue::GetName(g_traceRing[i].self).c_str(),
                 dying ? KeyOf(dying).c_str() : "(none)");
        g_traceRing[i].fn = nullptr;
        g_traceRing[i].self = nullptr;
    }
}

// --- [R7b] the reward gate -------------------------------------------------
// GA_Death_AI_C asks itself "WasKilledByPlayer()" (a BlueprintPure, so a real
// ProcessEvent call) before it grants a reward; a death produced by writing the
// health attribute has no player instigator, so the answer is No and the whole
// loot path is skipped (round-7 measurement: `calls:0`).  A co-op client's kill IS
// a player kill -- the mod's health push is how our copy of the enemy dies -- so
// while the probe asks for it the answer is forced to Yes and the game runs its own
// roll.  Armed only by `dropsprobe`; a normal run never touches it.
bool g_forcePlayerKill = false;
int g_forcedKills = 0;

void WasKilledHook(UObject* /*self*/, ue::UFunction* fn, void* parms) {
    if (!g_forcePlayerKill || !parms || !fn) return;
    const std::int32_t off = ue::PropOffsetStruct((ue::UStruct*)fn, "ReturnValue");
    if (off < 0) return;
    const std::uint8_t one = 1;
    if (ue::WriteMem(reinterpret_cast<std::uint8_t*>(parms) + off, &one, 1)) g_forcedKills += 1;
}

bool InstallWasKilledHook() {
    static bool done = false;
    if (done) return true;
    done = ue::HookPost("GA_Death_AI_C.WasKilledByPlayer", &WasKilledHook);
    if (done) net::Log("drops: the reward gate WasKilledByPlayer is hooked (probe only)");
    return done;
}

// --- [R7c] items ------------------------------------------------------------
// Two tables can add items to a death:
//   * `BP_AICharacter_C.AdditionalDropItems` @0xE08 -- a real property, so
//     ue::MapForEach reads it with the property's own key/value sizes
//     (key = item definition class, value = a double);
//   * `FSpartaCharacterDeathData.ItemDropTable` @0xDA8+0x10 -- a map INSIDE the
//     struct, so it is read by address (MapForEachRaw, key = class 8 bytes, value
//     = float 4 bytes per the dumper; 8 is tried too in case it is a double).
// The game's exact policy for the table (per-entry chance vs a weighted pick) is
// not documented; the sum decides which it looks like, and the choice is logged.
bool CollectItemDrops(UObject* a, std::vector<DropItem>* out) {
    out->clear();
    if (!Live(a)) return false;

    auto add = [&](UObject* cls, double weight) {
        if (!Live(cls)) return;
        if (weight <= 0.0) return;
        if (out->size() >= kMaxItems) return;
        std::string path;
        const std::string fn = ue::GetFullName(cls);
        const std::string::size_type sp = fn.find_last_of(" \t");
        if (sp != std::string::npos) {
            const std::string tail = fn.substr(sp + 1);
            if (tail.size() > 1 && tail[0] == '/') path = tail;
        }
        if (path.empty()) path = ue::GetName(cls);
        for (const DropItem& it : *out) {
            if (it.path == path) return;   // a class listed twice: keep it once
        }
        DropItem it;
        it.path = path;
        it.count = 1;
        it.weight = weight;
        out->push_back(it);
    };

    // 1. the actor's own AdditionalDropItems (TMap<TSubclassOf<ItemDef>, double>).
    ue::MapForEach(a, "AdditionalDropItems", [&](void* k, void* v) {
        UObject* cls = nullptr;
        double w = 0.0;
        if (k) ue::ReadMem(k, &cls, sizeof(cls));
        if (v) ue::ReadMem(v, &w, sizeof(w));
        add(cls, w);
        return out->size() < kMaxItems;
    });

    // 2. ItemDropTable inside the inline death-data struct.
    const std::int32_t base = ue::PropOffset(a, "CharacterDeathData");
    if (base >= 0 && out->size() < kMaxItems) {
        const std::uint8_t* mapAddr = reinterpret_cast<const std::uint8_t*>(a) + base + 0x10;
        for (int valueSize : {4, 8}) {
            const std::size_t before = out->size();
            ue::MapForEachRaw(mapAddr, 8, valueSize, [&](void* k, void* v) {
                UObject* cls = nullptr;
                if (k) ue::ReadMem(k, &cls, sizeof(cls));
                double w = 0.0;
                if (v) {
                    if (valueSize == 4) {
                        float f = 0.0f;
                        ue::ReadMem(v, &f, sizeof(f));
                        w = static_cast<double>(f);
                    } else {
                        ue::ReadMem(v, &w, sizeof(w));
                    }
                }
                add(cls, w);
                return out->size() < kMaxItems;
            });
            if (out->size() > before) break;   // the size that produced rows is the right one
        }
    }
    if (!out->empty()) {
        double sum = 0.0;
        for (const DropItem& it : *out) sum += it.weight;
        net::Log("drops: %s dropped %d item candidate(s), weights sum %.2f -> %s", KeyOf(a).c_str(),
                 (int)out->size(), sum,
                 (sum > 0.98 && sum < 1.02) ? "weights" : "per-entry chances");
        for (const DropItem& it : *out) {
            net::Log("drops: item candidate %s weight %.2f", it.path.c_str(), it.weight);
        }
    }
    return !out->empty();
}


std::vector<DropItem> PickItems(const std::vector<DropItem>& cands) {
    std::vector<DropItem> picked;
    if (cands.empty()) return picked;
    double sum = 0.0;
    for (const DropItem& it : cands) sum += it.weight;
    if (sum > 0.98 && sum < 1.02) {
        // a weighted pick: one item, chosen by weight
        double u = 0.0;
        {
            static std::uint64_t s = 0x2545F4914F6CDD1Dull;
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            s ^= static_cast<std::uint64_t>(net::Now() * 1000.0);
            u = static_cast<double>((s >> 33) & 0xFFFFFF) / static_cast<double>(0x1000000) * sum;
        }
        for (const DropItem& it : cands) {
            if (u < it.weight) {
                DropItem one = it;
                one.count = 1;
                picked.push_back(one);
                break;
            }
            u -= it.weight;
        }
    } else {
        for (const DropItem& it : cands) {
            const double p = it.weight > 1.0 ? 1.0 : it.weight;
            if (p > 0.0 && RollChance(p)) {
                DropItem one = it;
                one.count = 1;
                picked.push_back(one);
            }
        }
    }
    return picked;
}

// "0.050 dead=0" -- the health text the hit ladder prints around each attempt.
std::string FmtHp(double hp, bool dead) {
    return net::Fmt("%.3f dead=%d", hp, dead ? 1 : 0);
}

// The player's ability system component, from its health component
// (USpartaHealthComponent.AbilitySystemComponent @0x118).
UObject* PlayerASC(UObject* pawn) {
    if (!Live(pawn)) return nullptr;
    static UObject* cached = nullptr;
    if (Live(cached)) return cached;
    cached = nullptr;
    for (UObject* o : ue::FindAllOf("SpartaHealthComponent")) {
        UObject* owner = nullptr;
        if (ue::UFunction* gf = ue::FindFunction(o, "GetOwner")) {
            ue::Params p(gf);
            if (ue::Call(o, gf, p.data())) owner = p.Get<UObject*>("ReturnValue");
        }
        if (owner != pawn) continue;
        UObject* asc = nullptr;
        ue::Get(o, "AbilitySystemComponent", asc);
        if (Live(asc)) {
            cached = asc;
            return asc;
        }
    }
    return nullptr;
}

// One real hit, delivered the way the game delivers one.  `how` says what was
// tried; every attempt is logged with the target's health before and after.
// Ladder, in the order the game itself would use them:
//   1. ISpartaHittable::SpartaApplyHit(hit, Instigator=our pawn, Payload, Causer)
//      with a fresh payload (CDO.GetDuplicate) whose HealthDamage is Float/amount;
//   2. the same call with the payload the enemy already carries, if the fresh one
//      did not move the health;
//   3. BP_PlayerController_C.S_DealDamage(amount) -- the game's own cheat entry;
//   4. ASpartaCharacter::OnHitReceived(hit, payload, instigator ASC) -- the ASC
//      path the payload is finally applied through.
bool ApplyPlayerHit(UObject* target, double amount, std::string* how) {
    UObject* pawn = net::Pawn();
    if (!Live(target) || !Live(pawn)) return false;

    auto hpNow = [&]() {
        bool dead = false;
        const double hp = combat::HealthNorm(target, &dead);
        return FmtHp(hp, dead);
    };

    // --- 0. the payload the game itself would use ---
    UObject* payload = NewPayload(pawn);
    bool payloadOk = false;
    if (Live(payload)) {
        // `GetDuplicate` on this build hands back the class default object, and the
        // apply path refuses a CDO payload (bIsCDO @0x154) -- clear the flag, and say
        // so once, because that is the difference between "the game ignored the hit"
        // and "the damage landed".
        const bool wasCdo = ue::GetBool(payload, "bIsCDO");
        if (wasCdo) ue::SetBool(payload, "bIsCDO", false);
        payloadOk = SetPayloadDamage(payload, amount);
        HealthDamageOption back{};
        const std::int32_t off = ue::PropOffset(payload, "HealthDamage");
        if (off >= 0) {
            ue::ReadMem(reinterpret_cast<const std::uint8_t*>(payload) + off, &back, sizeof(back));
        }
        net::Log("dropshit: payload=%s built=%d healthDamage(type=%d value=%.1f) isCDO %d->%d",
                 ue::GetName(payload).c_str(), payloadOk ? 1 : 0, (int)back.Type,
                 static_cast<double>(back.FloatValue), wasCdo ? 1 : 0,
                 ue::GetBool(payload, "bIsCDO") ? 1 : 0);
    }
    const std::string hp0 = hpNow();

    // --- 1. the hittable interface, exactly as a weapon calls it ---
    if (ue::UFunction* f = ue::FindFunction(target, "SpartaApplyHit")) {
        ue::Params p(f);
        p.Set<UObject*>("Instigator", pawn);
        p.Set<UObject*>("Payload", payload);
        p.Set<UObject*>("DamageCauser", pawn);
        const bool ok = ue::Call(target, f, p.data());
        net::Log("dropshit: 1) SpartaApplyHit ok=%d hp %s -> %s",
                 ok ? 1 : 0, hp0.c_str(), hpNow().c_str());
        if (how) *how += "SpartaApplyHit; ";
    } else if (how) {
        *how += "no SpartaApplyHit; ";
    }
    if (hpNow() != hp0) return true;

    // --- 2. the payload's own apply entry (the ASC path) ---
    if (Live(payload)) {
        UObject* asc = PlayerASC(pawn);
        if (ue::UFunction* f = ue::FindFunction(payload, "ApplyPayload")) {
            ue::Params p(f);
            p.Set<UObject*>("Target", target);
            p.Set<UObject*>("DamageCauser", pawn);
            const bool ok = ue::Call(payload, f, p.data());
            net::Log("dropshit: 2) payload.ApplyPayload ok=%d asc=%s hp %s -> %s", ok ? 1 : 0,
                     Live(asc) ? ue::GetName(asc).c_str() : "(null)", hp0.c_str(),
                     hpNow().c_str());
            if (how) *how += "ApplyPayload; ";
            if (hpNow() != hp0) return true;
        }
        if (ue::UFunction* f = ue::FindFunction(payload, "ApplyPayloadInstigator")) {
            ue::Params p(f);
            p.Set<UObject*>("Target", target);
            p.Set<UObject*>("InstigatingASC", asc);
            p.Set<UObject*>("DamageCauser", pawn);
            const bool ok = ue::Call(payload, f, p.data());
            net::Log("dropshit: 3) payload.ApplyPayloadInstigator ok=%d hp %s -> %s", ok ? 1 : 0,
                     hp0.c_str(), hpNow().c_str());
            if (how) *how += "ApplyPayloadInstigator; ";
            if (hpNow() != hp0) return true;
        }
    }

    // --- 4. the ASC receive path, when the class exposes it ---
    if (Live(payload)) {
        UObject* asc = PlayerASC(pawn);
        if (ue::UFunction* f = ue::FindFunction(target, "OnHitReceived")) {
            ue::Params p(f);
            p.Set<UObject*>("InstigatorActor", pawn);
            p.Set<UObject*>("InstigatorASC", asc);
            p.Set<UObject*>("Payload", payload);
            p.Set<UObject*>("DamageCauser", pawn);
            const bool ok = ue::Call(target, f, p.data());
            net::Log("dropshit: 4) OnHitReceived ok=%d asc=%s hp %s -> %s", ok ? 1 : 0,
                     Live(asc) ? ue::GetName(asc).c_str() : "(null)", hp0.c_str(), hpNow().c_str());
            if (how) *how += "OnHitReceived; ";
            if (hpNow() != hp0) return true;
        } else if (how) {
            *how += "no OnHitReceived; ";
        }
    }

    // --- 4. still nothing: the round-7 kill path, but only with the reward gate
    // forced (<  probe  only).  This is what tells "the hit never landed" apart
    // from "the loot gate is what blocks the roll".
    if (g_forcePlayerKill) {
        const bool started = combat::StartDeath(target);
        net::Log("dropshit: 4) StartDeath (gate forced) ok=%d hp %s -> %s", started ? 1 : 0,
                 hp0.c_str(), hpNow().c_str());
        if (how) *how += "StartDeath(forced); ";
    }
    return false;
}

// The verbs -----------------------------------------------------------------
//   dropsenemies [n]        every BP_AICharacter within n big units: tail, class,
//                            distance, health, and its own death data (gold/gloom/
//                            chance/useRewards) -- this is how a REWARDED enemy is
//                            found instead of guessed
//   dropshit [dmg] [tail]   one real player hit on the nearest (or named) enemy
//   dropsarm                drop that enemy's health to 5% first (so the real hit
//                            is the killing blow without needing a 300 dmg weapon)
void DumpEnemies(double maxDist) {
    UObject* pawn = net::Pawn();
    const Vec3 me = net::Loc(pawn);
    int shown = 0, rewarded = 0, total = 0;
    std::vector<UObject*> list = ue::FindAllOf("BP_AICharacter_C");
    for (UObject* a : list) {
        if (!Live(a)) continue;
        const Vec3 p = net::Loc(a);
        const double dx = p.X - me.X;
        const double dy = p.Y - me.Y;
        const double dz = p.Z - me.Z;
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        int gloom = 0, gold = 0;
        float chance = 0.0f;
        bool useRewards = false;
        const bool has = DeathDataOf(a, &gloom, &gold, &chance, &useRewards);
        if (has && (gold > 0 || gloom > 0)) rewarded += 1;
        total += 1;
        if (d > maxDist || shown >= 24) continue;
        bool dead = false;
        const double hp = combat::HealthNorm(a, &dead);
        net::Log("dropsenemies: %s %.0fuu hp=%.2f dead=%d rewards=%d gold=%d gloom=%d chance=%.2f %s",
                 net::Tail(a).c_str(), d, hp, dead ? 1 : 0, useRewards ? 1 : 0,
                 gold, gloom, static_cast<double>(chance), ue::GetClassName(a).c_str());
        shown += 1;
    }
    net::Log("dropsenemies: %d enemy actor(s) in the world, %d with gold/gloom in their death "
             "data, %d within %.0f uu", total, rewarded, shown, maxDist);
}

// ===========================================================================
// the mirrored death
// ===========================================================================

void MarkMirrorDeath(UObject* actor, const char* why) {
    if (!On() || !actor) return;
    DropsState& d = D();
    const std::string tail = KeyOf(actor);
    for (auto& v : d.veto) {
        if (v.actor == actor) {
            v.until = net::Now() + kDropVetoSeconds;
            return;
        }
    }
    if (d.veto.size() >= kMaxVeto) {
        d.veto.erase(d.veto.begin());
    }
    DropVeto v;
    v.actor = actor;
    v.tail = tail;
    v.until = net::Now() + kDropVetoSeconds;
    d.veto.push_back(v);
    net::Log("drops: %s is a mirrored death (%s) -- its own loot roll is vetoed for %.0fs",
             tail.empty() ? "(unknown)" : tail.c_str(), (why && *why) ? why : "peer",
             kDropVetoSeconds);
}

std::string Status() {
    const DropsState& d = D();
    return net::Fmt("drops=rolled:%d calls:%d sent:%d got:%d gold:%d gloom:%d items:%d "
                    "vetoed:%d dup:%d hooks:%d queued:%d pending:%d lateveto:%d cancelled:%d\n",
                    d.rolled, d.calls, d.sentEvents, d.gotEvents, d.grantedGold, d.grantedGloom,
                    d.grantedItems, d.vetoed, d.dupSkipped, d.hooks, d.queuedRolls,
                    (int)d.pendingRolls.size(), d.alreadyRolledVetoed, d.fallbackCancelled);
}

// ===========================================================================
// the lab verbs
// ===========================================================================
bool OnCommand(const std::string& cmd, const std::string& rest) {
    DropsState& d = D();

    if (cmd == "dropsprobe") {
        const double secs = rest.empty() ? 120.0 : atof(rest.c_str());
        d.probe = true;
        d.probeUntil = net::Now() + (secs > 0.0 ? secs : 120.0);
        d.probeFires = 0;
        d.lastWallet = -1;
        // [R7b] the reward gate: our kills are player kills, so let the game's own
        // roll run while the probe is on.
        g_forcePlayerKill = true;
        InstallWasKilledHook();
        g_traceOn = true;
        const int traced = InstallTraceHooks();
        net::Log("dropsprobe: the WasKilledByPlayer gate is forced for this probe (co-op kills "
                 "count as player kills); %d/%d death-ability steps traced",
                 traced, kTraceTargets);
        UObject* g = nullptr;
        UObject* gl = nullptr;
        const Wallet w = ReadWallet(&g, &gl);
        net::Log("dropsprobe: on for %.0fs. wallet gold=%d (%s) gloom=%d (%s) hooks=%d/%d",
                 secs, w.gold, g ? ue::GetFullName(g).c_str() : "no item def", w.gloom,
                 gl ? ue::GetFullName(gl).c_str() : "no item def", d.hooks, kTargetCount);
        return true;
    }
    if (cmd == "dropswallet") {
        UObject* g = nullptr;
        UObject* gl = nullptr;
        const Wallet w = ReadWallet(&g, &gl);
        int cumGold = 0, cumGloom = 0;
        UObject* save = ue::FindFirstOf("BP_SpartaPlayerSaveObject_C");
        if (save) {
            ue::Get(save, "CumulativeGold", cumGold);
            ue::Get(save, "CumulativeGloom", cumGloom);
        }
        net::Log("dropswallet: gold=%d gloom=%d | CumulativeGold=%d CumulativeGloom=%d | "
                 "goldDef=%s gloomDef=%s pc=%s",
                 w.gold, w.gloom, cumGold, cumGloom,
                 g ? ue::GetName(g).c_str() : "-", gl ? ue::GetName(gl).c_str() : "-",
                 net::FullName(net::PC()).c_str());
        return true;
    }
    if (cmd == "dropsgrant") {
        int gold = 0, gloom = 0;
        sscanf_s(rest.c_str(), "%d %d", &gold, &gloom);
        const Wallet before = ReadWallet(nullptr, nullptr);
        const bool okG = gold > 0 ? CallControllerInt(kAddGold, gold) : false;
        const bool okGl = gloom > 0 ? CallControllerInt(kAddGloom, gloom) : false;
        const Wallet after = ReadWallet(nullptr, nullptr);
        net::Log("dropsgrant: asked gold=%d gloom=%d -> S_AddGold ok=%d S_AddGloom ok=%d | "
                 "wallet gold %d -> %d, gloom %d -> %d",
                 gold, gloom, okG ? 1 : 0, okGl ? 1 : 0, before.gold, after.gold, before.gloom,
                 after.gloom);
        return true;
    }
    if (cmd == "dropskill") {
        // A local kill through the game's own death, so the drop code runs with
        // nobody vetoing it: this is the "measure a real kill" verb.
        std::vector<UObject*> cands;
        const Vec3 me = net::Loc(net::Pawn());
        for (UObject* a : ue::FindAllOf("BP_AICharacter_C")) {
            if (!Live(a)) continue;
            cands.push_back(a);
            if (cands.size() >= 64) break;
        }
        if (cands.empty()) {
            net::Log("dropskill: no BP_AICharacter_C in this world");
            return true;
        }
        // Nearest LIVE enemy to our own pawn -- the enemy the harness's `kill` would
        // take, so the measurement is about the fight the player is actually in.  A
        // corpse is skipped: killing one again would roll for a death that already
        // happened (and would make the second direction of the test meaningless).
        UObject* pick = nullptr;
        double best = 1.0e18;
        for (UObject* a : cands) {
            bool dead = false;
            const double hp = combat::HealthNorm(a, &dead);
            if (dead || (hp >= 0.0 && hp <= 0.05)) continue;
            // Hostile only: BP_AICharacter_C is also the base of the hub's friendly
            // NPCs (vendors, the shell keeper, the training dummy), and killing one of
            // those proves nothing about enemy loot.
            const std::string cn = ue::GetClassName(a);
            if (cn.find("NPC") != std::string::npos || cn.find("Merchant") != std::string::npos ||
                cn.find("Blacksmith") != std::string::npos ||
                cn.find("Shopkeeper") != std::string::npos ||
                cn.find("Genessa") != std::string::npos) {
                continue;
            }
            const Vec3 p = net::Loc(a);
            const double dx = p.X - me.X;
            const double dy = p.Y - me.Y;
            const double dz = p.Z - me.Z;
            const double d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best) {
                best = d2;
                pick = a;
            }
        }
        if (!pick) {
            net::Log("dropskill: every nearby BP_AICharacter is already dead");
            return true;
        }
        std::string tail = net::Tail(pick);
        if (!rest.empty() && rest != "nearest") {
            bool found = false;
            for (UObject* a : cands) {
                if (net::Tail(a) == rest) {
                    pick = a;
                    found = true;
                    break;
                }
            }
            if (!found) {
                net::Log("dropskill: no enemy with tail %s", rest.c_str());
                return true;
            }
        }
        tail = net::Tail(pick);
        net::Log("dropskill: %s (%s) (%d candidate(s))", tail.c_str(),
                 ue::GetClassName(pick).c_str(), (int)cands.size());
        // Go through coop_combat's own path so the health write, the dead-set and
        // the game's StartDeath behave exactly as in a real death.
        if (combat::SetHealthNorm(pick, 0.0)) {
            const bool started = combat::StartDeath(pick);
            net::Log("dropskill: %s -> health 0, startDeath ok=%d", tail.c_str(), started ? 1 : 0);
        } else {
            net::Log("dropskill: could not write health for %s", tail.c_str());
        }
        return true;
    }
    if (cmd == "dropsenemies") {
        DumpEnemies(rest.empty() ? 20000.0 : atof(rest.c_str()));
        return true;
    }
    if (cmd == "dropshit") {
        // dropshit [damage] [tail|nearest]
        double dmg = 200.0;
        std::string tail;
        {
            char buf[128] = {0};
            sscanf_s(rest.c_str(), "%lf %127s", &dmg, buf, (unsigned)sizeof(buf));
            tail = buf;
            if (dmg <= 0.0) dmg = 200.0;
        }
        UObject* pawn = net::Pawn();
        const Vec3 me = net::Loc(pawn);
        UObject* target = nullptr;
        if (!tail.empty() && tail != "nearest") {
            for (UObject* a : ue::FindAllOf("BP_AICharacter_C")) {
                if (Live(a) && net::Tail(a) == tail) {
                    target = a;
                    break;
                }
            }
            if (!target) {
                net::Log("dropshit: no enemy with tail %s", tail.c_str());
                return true;
            }
        } else {
            double best = 1.0e18;
            for (UObject* a : ue::FindAllOf("BP_AICharacter_C")) {
                if (!Live(a)) continue;
                const Vec3 p = net::Loc(a);
                const double dx = p.X - me.X;
                const double dy = p.Y - me.Y;
                const double dz = p.Z - me.Z;
                const double d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best) {
                    best = d2;
                    target = a;
                }
            }
        }
        if (!Live(target)) {
            net::Log("dropshit: no enemy actor at all");
            return true;
        }
        int gloom = 0, gold = 0;
        float chance = 0.0f;
        bool useRewards = false;
        DeathDataOf(target, &gloom, &gold, &chance, &useRewards);
        bool dead = false;
        const double hp0 = combat::HealthNorm(target, &dead);
        net::Log("dropshit: target=%s class=%s hp=%.3f dead=%d | deathData gold=%d gloom=%d "
                 "chance=%.2f useRewards=%d | dmg=%.0f",
                 net::Tail(target).c_str(), ue::GetClassName(target).c_str(), hp0, dead ? 1 : 0,
                 gold, gloom, static_cast<double>(chance), useRewards ? 1 : 0, dmg);

        // Remember the attempt so Tick2 can report the outcome a second later
        // (damage may be applied by a deferred gameplay effect).
        d.pendingHit = true;
        d.pendingTail = net::Tail(target);
        d.pendingHp0 = hp0;
        d.pendingAt = net::Now();
        d.pendingActor = target;

        std::string how;
        const bool ok = ApplyPlayerHit(target, dmg, &how);
        bool dead2 = false;
        const double hp1 = combat::HealthNorm(target, &dead2);
        net::Log("dropshit: SpartaApplyHit ok=%d (%s) hp %.3f -> %.3f dead=%d",
                 ok ? 1 : 0, how.empty() ? "direct" : how.c_str(), hp0, hp1, dead2 ? 1 : 0);
        return true;
    }
    if (cmd == "dropsarm") {
        // Take the target down to 5% through the attribute (NOT a death), so the
        // following real hit is the killing blow and WasKilledByPlayer sees a hit.
        std::string tail;
        {
            char buf[128] = {0};
            sscanf_s(rest.c_str(), "%127s", buf, (unsigned)sizeof(buf));
            tail = buf;
        }
        UObject* pawn = net::Pawn();
        const Vec3 me = net::Loc(pawn);
        UObject* target = nullptr;
        double best = 1.0e18;
        for (UObject* a : ue::FindAllOf("BP_AICharacter_C")) {
            if (!Live(a)) continue;
            if (!tail.empty() && tail != "nearest") {
                if (net::Tail(a) == tail) {
                    target = a;
                    break;
                }
                continue;
            }
            const Vec3 p = net::Loc(a);
            const double dx = p.X - me.X;
            const double dy = p.Y - me.Y;
            const double dz = p.Z - me.Z;
            const double d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best) {
                best = d2;
                target = a;
            }
        }
        if (!Live(target)) {
            net::Log("dropsarm: no enemy actor");
            return true;
        }
        const bool ok = combat::SetHealthNorm(target, 0.05);
        bool dead = false;
        const double hp = combat::HealthNorm(target, &dead);
        net::Log("dropsarm: %s health -> 0.05 (ok=%d), now %.3f dead=%d", net::Tail(target).c_str(),
                 ok ? 1 : 0, hp, dead ? 1 : 0);
        return true;
    }
    if (cmd == "dropsforce") {
        // A synthetic event: proves the transport + the grant + the dedupe even
        // when the enemy under test happens to roll nothing.  It is NOT a kill --
        // the verdict says which of the two it used.
        int gold = 0, gloom = 0;
        sscanf_s(rest.c_str(), "%d %d", &gold, &gloom);
        if (gold <= 0 && gloom <= 0) {
            net::Log("dropsforce: give a gold and/or gloom amount");
            return true;
        }
        d.outSeq += 1;
        DropEvent e;
        e.seq = d.outSeq;
        e.key = "FORCED";
        e.gold = gold;
        e.gloom = gloom;
        e.born = net::Now();
        d.out.push_back(e);
        if (d.out.size() > kMaxOutEvents) d.out.erase(d.out.begin());
        net::Log("dropsforce: publishing a synthetic event gold=%d gloom=%d seq=%lld",
                 gold, gloom, e.seq);
        return true;
    }
    if (cmd == "dropskilln") {
        // [R7c] kill N hostile enemies back to back inside one tick: the multi-kill
        // case (three deaths in a second must give three rolls, one each).
        int n = 3;
        if (!rest.empty()) { n = atoi(rest.c_str()); }
        if (n < 1) n = 1;
        if (n > 8) n = 8;
        std::vector<UObject*> killed;
        UObject* pawn = net::Pawn();
        const Vec3 me = net::Loc(pawn);
        for (int k = 0; k < n; ++k) {
            UObject* pick = nullptr;
            double best = 1.0e18;
            for (UObject* a : ue::FindAllOf("BP_AICharacter_C")) {
                if (!Live(a)) continue;
                bool used = false;
                for (UObject* u : killed) {
                    if (u == a) used = true;
                }
                if (used) continue;
                bool dead = false;
                const double hp = combat::HealthNorm(a, &dead);
                if (dead || (hp >= 0.0 && hp <= 0.05)) continue;
                const std::string cn = ue::GetClassName(a);
                if (cn.find("NPC") != std::string::npos ||
                    cn.find("Merchant") != std::string::npos ||
                    cn.find("Blacksmith") != std::string::npos ||
                    cn.find("Shopkeeper") != std::string::npos ||
                    cn.find("Genessa") != std::string::npos) {
                    continue;
                }
                const Vec3 p = net::Loc(a);
                const double dx = p.X - me.X;
                const double dy = p.Y - me.Y;
                const double dz = p.Z - me.Z;
                const double d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best) {
                    best = d2;
                    pick = a;
                }
            }
            if (!pick) break;
            killed.push_back(pick);
            combat::SetHealthNorm(pick, 0.0);
            const bool started = combat::StartDeath(pick);
            net::Log("dropskilln: %d/%d %s (%s) startDeath=%d", k + 1, n, net::Tail(pick).c_str(),
                     ue::GetClassName(pick).c_str(), started ? 1 : 0);
        }
        net::Log("dropskilln: killed %d enemy(ies), queue=%d rolled=%d", (int)killed.size(),
                 (int)d.pendingRolls.size(), d.rolled);
        return true;
    }
    if (cmd == "dropsdump") {
        net::Log("dropsdump: %s", Status().c_str());
        net::Log("dropsdump: pending=%d fallbackRolled=%d gameRolled=%d", (int)d.pendingRolls.size(),
                 (int)d.rolledByFallback.Size(), (int)d.rolledByGame.Size());
        net::Log("dropsdump: out=%d seq=%lld peerSeq=%lld veto=%d hooks=%d probe=%d last=%s g=%d gl=%d",
                 (int)d.out.size(), d.outSeq, d.lastPeerSeq, (int)d.veto.size(), d.hooks,
                 d.probe ? 1 : 0, d.lastKey.c_str(), d.lastGold, d.lastGloom);
        for (const DropEvent& e : d.out) {
            net::Log("dropsdump: pending seq=%lld key=%s gold=%d gloom=%d sent=%d",
                     e.seq, e.key.c_str(), e.gold, e.gloom, e.sent ? 1 : 0);
        }
        return true;
    }
    return false;
}

}  // namespace drops
}  // namespace coop
