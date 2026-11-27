// coop_hithook.cpp -- [R10-HITHOOK] Hit-payload combat sync.
//
// Hooks USpartaHitPayload::ApplyPayloadInstigator dynamically (resolved from
// UFunction::ExecFunction + vtable on any build, no hardcoded RVA) to sync:
//   - real damage, poise damage, break damage, strike direction, and reaction tag
//   - guest parry (Event.Hit.Parried) and flyback (Event.Reaction.Hit.Flyback)
//   - DodgeVeto on phantom enemy->puppet hits while peer is dodging or out of range
//
// Wire section: ,"hit":[[seq,"targetKey",dmg,poise,brk,"react",dir,hx,hy,hz,flags],...]
// Kill switch: HITHOOK_OFF.txt (Toggles::hitHook, default ON).
// Sync status: hithook=on|off hooked:0|1 tx:N rx:N applied:N veto:N parries:N
// Lab verbs:   hitprobe, hittest

#include "ms2_ue.h"
#include "coop_state.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "MinHook.h"

#include <atomic>
#ifdef GetClassName
#undef GetClassName
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace coop {
namespace hithook {

namespace {

using ue::UObject;
using ue::UFunction;
using ue::UClass;

// USpartaHitPayload field offsets (Dumper-7 Sparta_classes.hpp, verified UE5.6)
// Dynamic PropOffset is preferred; these constants are safe fallbacks.
constexpr int32_t kOffHealthDamageOption = 0x0028; // FHealthDamageOption (12B)
constexpr int32_t kOffReactionTag        = 0x0070; // FGameplayTag (8B FName)
constexpr int32_t kOffStrikeDirection    = 0x0078; // uint8
constexpr int32_t kOffPoiseDamage        = 0x007C; // float
constexpr int32_t kOffBreakDamage        = 0x0088; // float
constexpr int32_t kOffChosenReactionTag  = 0x00B0; // FGameplayTag (8B FName)
constexpr int32_t kOffDamageDealt        = 0x00DC; // float
constexpr int32_t kOffCalcDamage         = 0x00E0; // float
constexpr int32_t kOffCalcPoiseDamage    = 0x00E4; // float
constexpr int32_t kOffCalcBreakDamage    = 0x00E8; // float

// Hit flags transmitted on the wire
constexpr int kFlagParried = 1 << 0;
constexpr int kFlagFlyback = 1 << 1;
constexpr int kFlagPeerHit = 1 << 2; // enemy hit the peer's puppet

constexpr double kTxRepeatSecs   = 3.5;
constexpr double kDodgeVetoDist  = 380.0;
constexpr int    kMaxQueuedHits  = 12;

double Mono() {
    using namespace std::chrono;
    static const steady_clock::time_point t0 = steady_clock::now();
    return duration_cast<duration<double>>(steady_clock::now() - t0).count();
}

bool Live(UObject* o) { return o != nullptr && ue::IsValid(o); }

// Raw C++ signature of USpartaHitPayload::ApplyPayloadInstigator on Win64:
//   void ApplyPayloadInstigator(USpartaHitPayload* this,
//                               AActor* Target,
//                               UAbilitySystemComponent* InstigatingASC,
//                               const FHitResult* HitResult,
//                               AActor* DamageCauser,
//                               const FGameplayEffectContextHandle* EffectContext);
using ApplyPayloadInstigatorFn = void (*)(void* self, void* target, void* instigatingASC,
                                          const void* hitResult, void* damageCauser,
                                          const void* effectContext);

ApplyPayloadInstigatorFn g_origApply = nullptr;
void*                    g_hookTarget = nullptr;
uintptr_t                g_hookRva = 0;
uintptr_t                g_vtableRva = 0;
bool                     g_installTried = false;
bool                     g_peFallbackInstalled = false;

thread_local bool tl_inRemoteApply = false;

struct OutHit {
    int         seq = 0;
    std::string targetKey;
    double      dmg = 0.0;
    double      poise = 0.0;
    double      brk = 0.0;
    std::string react;
    int         dir = 0;
    double      hx = 0.0, hy = 0.0, hz = 0.0;
    int         flags = 0;
    double      at = 0.0;
    std::string pcls;   // payload class name (wire field 12, optional)
};

std::deque<OutHit> g_outQueue;
int g_nextSeq = 1;
int g_lastRxSeq = 0;

// Telemetry counters
int g_txCount = 0;
int g_rxCount = 0;
int g_appliedCount = 0;
int g_vetoCount = 0;
int g_parryCount = 0;
int g_failCount = 0;
std::atomic<int> g_callCount{0};   // [R10-HOOKFIX] every entry into the detour (diagnostic)

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

std::string ReadTagName(UObject* payload, const char* propName, int32_t fallbackOff) {
    int32_t off = ue::PropOffset(payload, propName);
    if (off < 0) off = fallbackOff;
    ue::FName fn;
    if (!ue::ReadMem(reinterpret_cast<const uint8_t*>(payload) + off, &fn, sizeof(fn)))
        return "";
    if (fn.ComparisonIndex == 0) return "";
    std::string s = ue::ToString(fn);
    if (s == "None" || s.empty()) return "";
    return s;
}

float ReadFloatProp(UObject* obj, const char* propName, int32_t fallbackOff) {
    int32_t off = ue::PropOffset(obj, propName);
    if (off < 0) off = fallbackOff;
    float v = 0.0f;
    ue::ReadMem(reinterpret_cast<const uint8_t*>(obj) + off, &v, sizeof(v));
    return std::isfinite(v) ? v : 0.0f;
}

// Resolve canonical network key for a target actor:
//   - puppet -> "@peer"
//   - enemy with spawnAlias -> "SP_<spawner tail>"
//   - other enemy -> actor tail
std::string KeyForTarget(UObject* target) {
    if (!Live(target)) return "";
    UObject* pup = puppet::Actor();
    if (pup && target == pup) return "@peer";
    const std::string tail = net::Tail(target);
    if (tail.empty()) return "";
    const std::string* alias = state().combat.spawnAlias.Find(tail);
    if (alias && !alias->empty()) return *alias;
    return tail;
}

// Resolve target key back to a live local UObject*
UObject* FindTargetByKey(const std::string& key) {
    if (key.empty()) return nullptr;
    if (key == "@peer") return net::Pawn();

    const CombatState& c = state().combat;
    // Direct tail match in tickTails
    for (const NamedActor& na : state().local.tickTails) {
        if (na.tail == key && Live(na.obj)) return na.obj;
    }
    // SP_<spawner tail> -> look up via spawnAlias reverse or spawner's SpawnedEnemy
    if (key.size() > 3 && key.compare(0, 3, "SP_") == 0) {
        for (const StrStr& kv : c.spawnAlias.Items()) {
            if (kv.value == key) {
                for (const NamedActor& na : state().local.tickTails) {
                    if (na.tail == kv.key && Live(na.obj)) return na.obj;
                }
            }
        }
        const std::string spTail = key.substr(3);
        for (const NamedActor& sp : c.spawners) {
            if (sp.tail == spTail && Live(sp.obj)) {
                UObject* e = nullptr;
                if (ue::Get<UObject*>(sp.obj, "SpawnedEnemy", e) && Live(e)) return e;
            }
        }
    }
    // Fallback: scan Characters
    for (UObject* ch : ue::FindAllOf("Character")) {
        if (!Live(ch)) continue;
        if (net::Tail(ch) == key) return ch;
    }
    return nullptr;
}

bool PeerIsDodging() {
    const PeerState& p = state().peer;
    if (!p.present || !p.hasMontage) return false;
    const std::string& mp = p.montage.path;
    return ContainsCI(mp, "dodge") || ContainsCI(mp, "roll") ||
           ContainsCI(mp, "evade") || ContainsCI(mp, "dash");
}

bool LocalIsDodging() {
    const LocalState& L = state().local;
    const std::string& mp = L.montage.path;
    return ContainsCI(mp, "dodge") || ContainsCI(mp, "roll") ||
           ContainsCI(mp, "evade") || ContainsCI(mp, "dash");
}

// Check if an actor is the local player or owned by the local player (weapon/projectile)
bool IsLocalPlayerInstigator(UObject* damageCauser, UObject* instigatingASC) {
    UObject* pawn = net::Pawn();
    if (!Live(pawn)) return false;
    if (damageCauser == pawn) return true;
    if (Live(damageCauser)) {
        UObject* owner = nullptr;
        if (ue::Get<UObject*>(damageCauser, "Owner", owner) && owner == pawn) return true;
        UObject* inst = nullptr;
        if (ue::Get<UObject*>(damageCauser, "Instigator", inst) && inst == pawn) return true;
    }
    if (Live(instigatingASC)) {
        UObject* outer = ue::GetOuter(instigatingASC);
        if (outer == pawn) return true;
        UObject* avatar = nullptr;
        if (ue::Get<UObject*>(instigatingASC, "AvatarActor", avatar) && avatar == pawn) return true;
    }
    return false;
}

void EnqueueHit(const std::string& targetKey, double dmg, double poise, double brk,
                const std::string& react, int dir, const Vec3& hitLoc, int flags,
                const std::string& pcls = std::string()) {
    if (targetKey.empty()) return;
    OutHit h;
    h.seq = g_nextSeq++;
    h.targetKey = targetKey;
    h.dmg = dmg;
    h.poise = poise;
    h.brk = brk;
    h.react = react;
    h.dir = dir;
    h.hx = hitLoc.X;
    h.hy = hitLoc.Y;
    h.hz = hitLoc.Z;
    h.flags = flags;
    h.pcls = pcls;
    h.at = Mono();
    g_outQueue.push_back(std::move(h));
    while ((int)g_outQueue.size() > kMaxQueuedHits) g_outQueue.pop_front();
    ++g_txCount;
    if (flags & kFlagParried) ++g_parryCount;
}

void QueueLook(UObject* target, const std::string& key, const char* tag, int mode);
extern int g_localLogged;

// ---------------------------------------------------------------------------
// Native detour on USpartaHitPayload::ApplyPayloadInstigator
// ---------------------------------------------------------------------------
void DetourApplyPayloadInstigator(void* self, void* targetRaw, void* instigatingASCRaw,
                                  const void* hitResult, void* damageCauserRaw,
                                  const void* effectContext) {
    g_callCount.fetch_add(1, std::memory_order_relaxed);
    if (tl_inRemoteApply || !state().toggles.hitHook || !self || !targetRaw) {
        if (g_origApply) g_origApply(self, targetRaw, instigatingASCRaw, hitResult, damageCauserRaw, effectContext);
        return;
    }

    UObject* payload = reinterpret_cast<UObject*>(self);
    UObject* target = reinterpret_cast<UObject*>(targetRaw);
    UObject* instASC = reinterpret_cast<UObject*>(instigatingASCRaw);
    UObject* causer = reinterpret_cast<UObject*>(damageCauserRaw);

    if (!Live(payload) || !Live(target)) {
        if (g_origApply) g_origApply(self, targetRaw, instigatingASCRaw, hitResult, damageCauserRaw, effectContext);
        return;
    }

    UObject* pawn = net::Pawn();
    UObject* pup = puppet::Actor();

    // [R10-LABGOD] lab only: no damage to our own pawn while `labgod 1` is on
    if (pawn && target == pawn && area::LabGodOn()) {
        ++g_vetoCount;
        return;
    }

    // 1. DodgeVeto on enemy -> puppet hits: suppress phantom hits while peer is dodging
    //    or has already moved away from the impact point.
    if (pup && target == pup && state().peer.present) {
        if (PeerIsDodging()) {
            ++g_vetoCount;
            return;
        }
        if (Live(causer)) {
            const double d = Dist(state().peer.pos, net::Loc(causer));
            if (d > kDodgeVetoDist) {
                ++g_vetoCount;
                return;
            }
        }
    }

    // 2. DodgeVeto on mirrored enemy -> local player hits while local player is dodging
    if (pawn && target == pawn && LocalIsDodging() && Live(causer)) {
        const std::string cTail = net::Tail(causer);
        if (state().combat.ownedTails.Size() > 0 && !state().combat.ownedTails.Has(cTail)) {
            const double d = Dist(net::Loc(pawn), net::Loc(causer));
            if (d > kDodgeVetoDist) {
                ++g_vetoCount;
                return;
            }
        }
    }

    // Let the game execute the real hit calculation and application first
    if (g_origApply) {
        g_origApply(self, targetRaw, instigatingASCRaw, hitResult, damageCauserRaw, effectContext);
    }

    // Only transmit hits when we are in an active co-op session
    if (!state().peer.present || state().puppet.phase != Phase::Sync) return;

    const bool localToEnemy = (target != pawn && target != pup && IsLocalPlayerInstigator(causer, instASC));
    const bool enemyToPuppet = (pup && target == pup && !IsLocalPlayerInstigator(causer, instASC));
    if (!localToEnemy && !enemyToPuppet) return;

    float dmg = ReadFloatProp(payload, "DamageDealt", kOffDamageDealt);
    if (dmg <= 0.0f) dmg = ReadFloatProp(payload, "CalculatedDamage", kOffCalcDamage);
    const float poise = ReadFloatProp(payload, "CalculatedPoiseDamage", kOffCalcPoiseDamage);
    const float brk = ReadFloatProp(payload, "CalculatedBreakDamage", kOffCalcBreakDamage);

    std::string react = ReadTagName(payload, "ChosenReactionTag", kOffChosenReactionTag);
    if (react.empty()) react = ReadTagName(payload, "ReactionTag", kOffReactionTag);

    uint8_t dir = 0;
    int32_t dirOff = ue::PropOffset(payload, "StrikeDirection");
    if (dirOff < 0) dirOff = kOffStrikeDirection;
    ue::ReadMem(reinterpret_cast<const uint8_t*>(payload) + dirOff, &dir, 1);

    int flags = 0;
    if (enemyToPuppet) flags |= kFlagPeerHit;
    if (ContainsCI(react, "parr")) flags |= kFlagParried;
    if (ContainsCI(react, "flyback")) flags |= kFlagFlyback;

    if (dmg <= 0.01f && poise <= 0.01f && brk <= 0.01f && flags == 0) return;

    const std::string key = KeyForTarget(target);
    if (key.empty()) return;

    const Vec3 loc = net::Loc(target);
    const std::string pcls = ue::GetClassName(payload);
    if (localToEnemy && g_localLogged < 40) {
        ++g_localLogged;
        std::string reactTag = ReadTagName(payload, "ReactionTag", kOffReactionTag);
        net::Log("hithook: local hit %s cls=%s pcls=%s chosen=%s react=%s dmg=%.1f poise=%.1f brk=%.1f dir=%d owned=%d",
                 key.c_str(), ue::GetClassName(target).c_str(), pcls.c_str(), react.c_str(), reactTag.c_str(),
                 dmg, poise, brk, (int)dir, state().combat.ownedTails.Has(key) ? 1 : 0);
        QueueLook(target, key, "local", 0);
    }
    EnqueueHit(key, (double)dmg, (double)poise, (double)brk, react, (int)dir, loc, flags, pcls);
}

// ---------------------------------------------------------------------------
// Dynamic resolution of USpartaHitPayload::ApplyPayloadInstigator
// ---------------------------------------------------------------------------
// Reads UFunction::ExecFunction (+0xD8) for SpartaHitPayload.ApplyPayloadInstigator
// and scans its machine code for either:
//   - a virtual call `FF 90 <disp32>` / `FF 50 <disp8>` (call [rax + vtbl_off])
//   - a direct call `E8 <rel32>`
// Zero hardcoded RVAs: works across Day1 and Nov 2026+ patches automatically.
bool ResolveAndHook() {
    if (g_hookTarget) return true;
    if (g_installTried) return false;
    g_installTried = true;

    UClass* cls = ue::FindClass("SpartaHitPayload");
    if (!cls) {
        g_installTried = false; // class not loaded yet, retry
        return false;
    }
    UObject* cdo = ue::GetCDO(cls);
    if (!Live(cdo)) {
        g_installTried = false;
        return false;
    }
    UFunction* fn = ue::FindFunction(cdo, "ApplyPayloadInstigator");
    if (!fn) {
        net::Log("hithook: USpartaHitPayload.ApplyPayloadInstigator UFunction not found");
        return false;
    }

    uintptr_t execAddr = 0;
    if (!ue::ReadMem(reinterpret_cast<const uint8_t*>(fn) + 0xD8, &execAddr, sizeof(execAddr)) || !execAddr) {
        net::Log("hithook: ApplyPayloadInstigator ExecFunction unreadable");
        return false;
    }

    uintptr_t vtable = 0;
    ue::ReadMem(cdo, &vtable, sizeof(vtable));
    const uintptr_t base = ue::GetResolved().imageBase;

    // [R10-HOOKFIX] The exec thunk on this build is ~0x260 bytes: six FFrame::Step
    // calls (E8 -> rva 0x1620970) and THEN `mov rax,[r14]; mov rdi,[rax+2D0h]; ...;
    // call rdi`.  The old 320-byte window with an E8 fallback hooked FFrame::Step --
    // the Blueprint VM's hottest function -- so no hit was ever seen (round 10 run 9).
    // Read the whole thunk, accept only a vtable slot that is later called through a
    // register (or FF 90/FF 50), and never fall back to a direct E8 call.
    uint8_t code[1536] = {};
    if (!ue::ReadMem(reinterpret_cast<const void*>(execAddr), code, sizeof(code))) {
        net::Log("hithook: could not read execApplyPayloadInstigator bytes");
        return false;
    }
    {
        int32_t slotDisp = -1;
        int slotAt = -1;
        uint8_t slotReg = 0xFF;
        for (int i = 0; i + 8 < (int)sizeof(code); ++i) {
            // mov r64, [rax + disp32]  ->  REX.W(48|4C) 8B modrm(mod=10, rm=000)
            if ((code[i] == 0x48 || code[i] == 0x4C) && code[i + 1] == 0x8B && (code[i + 2] & 0xC7) == 0x80) {
                int32_t disp = 0;
                std::memcpy(&disp, code + i + 3, 4);
                if (disp >= 0x100 && disp < 0x1000 && (disp & 7) == 0) {
                    slotDisp = disp;
                    slotAt = i;
                    slotReg = (uint8_t)(((code[i + 2] >> 3) & 7) | (code[i] == 0x4C ? 8 : 0));
                }
            }
            // call reg  ->  FF D0+r   or  41 FF D0+r
            if (slotAt >= 0 && i > slotAt) {
                uint8_t r = 0xFF;
                if (code[i] == 0xFF && (code[i + 1] & 0xF8) == 0xD0 && (i == 0 || code[i - 1] != 0x41)) r = code[i + 1] & 7;
                else if (code[i] == 0x41 && code[i + 1] == 0xFF && (code[i + 2] & 0xF8) == 0xD0) r = (uint8_t)(8 | (code[i + 2] & 7));
                if (r == slotReg && vtable) {
                    uintptr_t fnPtr = 0;
                    if (ue::ReadMem(reinterpret_cast<const void*>(vtable + (uintptr_t)slotDisp), &fnPtr, sizeof(fnPtr)) &&
                        fnPtr > base && fnPtr < base + 0x0C000000ULL) {
                        if (MH_CreateHook(reinterpret_cast<LPVOID>(fnPtr),
                                          reinterpret_cast<LPVOID>(&DetourApplyPayloadInstigator),
                                          reinterpret_cast<LPVOID*>(&g_origApply)) != MH_OK ||
                            MH_EnableHook(reinterpret_cast<LPVOID>(fnPtr)) != MH_OK) {
                            net::Log("hithook: MinHook failed at rva 0x%llX", (unsigned long long)(fnPtr - base));
                            g_origApply = nullptr;
                            return false;
                        }
                        g_hookTarget = reinterpret_cast<void*>(fnPtr);
                        g_hookRva = fnPtr - base;
                        g_vtableRva = (vtable > base) ? (vtable - base) : 0;
                        net::Log("hithook: hooked ApplyPayloadInstigator rva=0x%llX (vtable rva=0x%llX slot 0x%X via reg call, exec rva=0x%llX)",
                                 (unsigned long long)g_hookRva, (unsigned long long)g_vtableRva,
                                 (unsigned)slotDisp, (unsigned long long)(execAddr - base));
                        return true;
                    }
                }
            }
        }
    }

    uintptr_t targetAddr = 0;
    // Scan for the call to the real C++ method inside execApplyPayloadInstigator:
    // After stepping the 5 FFrame parameters, MSVC emits either:
    //   FF 90 <disp32>  (call qword ptr [rax + disp32])
    //   FF 50 <disp8>   (call qword ptr [rax + disp8])
    //   E8 <rel32>      (direct call to non-virtual USpartaHitPayload::ApplyPayloadInstigator)
    // Prefer the LAST call before `C3` (ret) / epilogue because earlier calls are FFrame::StepCompiledIn.
    uintptr_t lastDirectCall = 0;
    uintptr_t lastVCallTarget = 0;
    for (int i = 0; i < 290; ++i) {
        // Stop at ret (0xC3) after at least 40 bytes of body
        if (i > 40 && code[i] == 0xC3) break;
        if (vtable && code[i] == 0xFF && code[i + 1] == 0x90) {
            int32_t disp = 0;
            std::memcpy(&disp, code + i + 2, 4);
            if (disp >= 0x20 && disp < 0x1000 && (disp & 7) == 0) {
                uintptr_t fnPtr = 0;
                if (ue::ReadMem(reinterpret_cast<const void*>(vtable + (uintptr_t)disp), &fnPtr, sizeof(fnPtr)) &&
                    fnPtr > base && fnPtr < base + 0x0A000000ULL) {
                    lastVCallTarget = fnPtr;
                }
            }
        } else if (vtable && code[i] == 0xFF && code[i + 1] == 0x50) {
            int8_t disp = (int8_t)code[i + 2];
            if (disp >= 0x20 && (disp & 7) == 0) {
                uintptr_t fnPtr = 0;
                if (ue::ReadMem(reinterpret_cast<const void*>(vtable + (uintptr_t)disp), &fnPtr, sizeof(fnPtr)) &&
                    fnPtr > base && fnPtr < base + 0x0A000000ULL) {
                    lastVCallTarget = fnPtr;
                }
            }
        } else if (code[i] == 0xE8) {
            int32_t rel = 0;
            std::memcpy(&rel, code + i + 1, 4);
            const uintptr_t dest = execAddr + (uintptr_t)i + 5 + (intptr_t)rel;
            if (dest > base && dest < base + 0x0A000000ULL) {
                lastDirectCall = dest;
            }
        }
    }

    // [R10-HOOKFIX] a direct E8 call in the thunk is FFrame::Step, never the method
    (void)lastDirectCall;
    targetAddr = lastVCallTarget;
    if (!targetAddr) {
        net::Log("hithook: could not locate ApplyPayloadInstigator target from exec=0x%llX",
                 (unsigned long long)(execAddr - base));
        return false;
    }

    if (MH_CreateHook(reinterpret_cast<LPVOID>(targetAddr),
                      reinterpret_cast<LPVOID>(&DetourApplyPayloadInstigator),
                      reinterpret_cast<LPVOID*>(&g_origApply)) != MH_OK ||
        MH_EnableHook(reinterpret_cast<LPVOID>(targetAddr)) != MH_OK) {
        net::Log("hithook: MinHook failed at rva 0x%llX", (unsigned long long)(targetAddr - base));
        g_origApply = nullptr;
        return false;
    }

    g_hookTarget = reinterpret_cast<void*>(targetAddr);
    g_hookRva = targetAddr - base;
    g_vtableRva = (vtable > base) ? (vtable - base) : 0;
    net::Log("hithook: hooked ApplyPayloadInstigator rva=0x%llX (vtable rva=0x%llX, %s)",
             (unsigned long long)g_hookRva, (unsigned long long)g_vtableRva,
             lastVCallTarget ? "vtable" : "direct");
    return true;
}

// ---------------------------------------------------------------------------
// Apply a received remote hit onto a local actor
// ---------------------------------------------------------------------------
UObject* GetASC(UObject* actor) {
    if (!Live(actor)) return nullptr;
    UObject* asc = nullptr;
    if (ue::Get<UObject*>(actor, "AbilitySystemComponent", asc) && Live(asc)) return asc;
    if (UFunction* fn = ue::FindFunction(actor, "GetAbilitySystemComponent")) {
        ue::Params p(fn);
        if (ue::Call(actor, fn, p.data())) {
            asc = p.Get<UObject*>("ReturnValue");
            if (Live(asc)) return asc;
        }
    }
    return nullptr;
}

void SendGameplayEventTag(UObject* target, const char* tagName, UObject* instigator) {
    if (!Live(target) || !tagName || !*tagName) return;
    UClass* libCls = ue::FindClass("AbilitySystemBlueprintLibrary");
    if (!libCls) return;
    UObject* libCdo = ue::GetCDO(libCls);
    if (!Live(libCdo)) return;
    UFunction* fn = ue::FindFunction(libCdo, "SendGameplayEventToActor");
    if (!fn) return;
    ue::FName tag = ue::MakeName(tagName);
    if (tag.ComparisonIndex == 0) return;
    ue::Params p(fn);
    p.Set<UObject*>("Actor", target);
    p.Set<ue::FName>("EventTag", tag);
    // Payload is FGameplayEventData; leave zeroed except Instigator if needed
    ue::Call(libCdo, fn, p.data());
    (void)instigator;
}

// ---------------------------------------------------------------------------
// [R10-HITSTUN] Remote hit = reaction only.
//
// Health already travels on the proven "nhp" lane (lowest copy wins), so a
// remote hit must NOT deal damage again.  What the receiver lacks is the hit
// REACTION: the enemy's AI runs here, and a health write raises no stagger.
// R10's first version called the native ApplyPayloadInstigator unguarded with a
// zeroed HitResult and the puppet's (uninitialised) ASC: it faulted on every
// received hit (B log 12:21:52 "tick callback faulted"), left tl_inRemoteApply
// stuck true (the hook went passive for good) and never played a reaction.
//
// Reaction methods (g_reactMode, lab verb `hitmode N`):
//   1  SendGameplayEventToActor(target, reactTag)
//   2  HitReactionAnimSet->GetHitReactionAnimation + PlayAnimMontage (pure anim)
//   3  ApplyPayloadInstigator through ProcessEvent (SEH-guarded) with the LOCAL
//      pawn's ASC and a duplicate of the sender's payload class, zero health dmg
//   0  off
// ---------------------------------------------------------------------------
int g_reactMode = 3;
int g_skipMirror = 0;
int g_reactOk = 0;

struct PendingLook {
    UObject*    target = nullptr;
    std::string key;
    std::string before;
    std::string tag;     // "local" real local hit, "rx" remote, "lab" hitreact verb
    int         mode = 0;
    double      at = 0.0;
    int         stage = 0;
};
std::vector<PendingLook> g_looks;
int g_localLogged = 0;

std::string MontageOf(UObject* a) {
    if (!Live(a)) return "";
    UFunction* fn = ue::FindFunction(a, "GetCurrentMontage");
    if (!fn) return "";
    ue::Params p(fn);
    if (!ue::Call(a, fn, p.data())) return "";
    UObject* m = p.Get<UObject*>("ReturnValue");
    if (!Live(m)) return "-";
    return net::Tail(m);
}

void QueueLook(UObject* target, const std::string& key, const char* tag, int mode) {
    if (g_looks.size() > 24) return;
    PendingLook l;
    l.target = target;
    l.key = key;
    l.before = MontageOf(target);
    l.tag = tag;
    l.mode = mode;
    l.at = Mono();
    g_looks.push_back(l);
}

void TickLooks() {
    const double now = Mono();
    for (size_t i = 0; i < g_looks.size();) {
        PendingLook& l = g_looks[i];
        const double wait = (l.stage == 0) ? 0.12 : 0.6;
        if (now - l.at < wait) { ++i; continue; }
        const std::string m = Live(l.target) ? MontageOf(l.target) : "<gone>";
        const bool reacted = (m != l.before && m != "-" && !m.empty() && m != "<gone>");
        net::Log("hitlook %s mode=%d %s +%.2fs montage %s -> %s%s", l.tag.c_str(), l.mode,
                 l.key.c_str(), now - l.at, l.before.c_str(), m.c_str(), reacted ? "  [REACTED]" : "");
        if (l.stage == 0) { l.stage = 1; ++i; continue; }
        g_looks.erase(g_looks.begin() + (std::ptrdiff_t)i);
    }
}

struct EvData {   // FGameplayEventData, 0xB0
    ue::FName tag;
    UObject*  instigator;
    UObject*  target;
    UObject*  opt1;
    UObject*  opt2;
    uint8_t   rest[0xB0 - 0x28];
};
static_assert(sizeof(EvData) == 0xB0, "FGameplayEventData size");

bool ReactByEvent(UObject* target, const std::string& react) {
    if (react.empty()) return false;
    UClass* libCls = ue::FindClass("AbilitySystemBlueprintLibrary");
    UObject* libCdo = libCls ? ue::GetCDO(libCls) : nullptr;
    if (!Live(libCdo)) return false;
    UFunction* fn = ue::FindFunction(libCdo, "SendGameplayEventToActor");
    if (!fn) return false;
    ue::FName tag = ue::MakeName(react.c_str());
    if (tag.ComparisonIndex == 0) return false;
    EvData ev{};
    ev.tag = tag;
    ev.instigator = net::Pawn();
    ev.target = target;
    ue::Params p(fn);
    p.Set<UObject*>("Actor", target);
    p.Set<ue::FName>("EventTag", tag);
    p.Set<EvData>("Payload", ev);
    return ue::Call(libCdo, fn, p.data());
}

bool ReactByAnimSet(UObject* target, const std::string& react, int dir) {
    if (react.empty()) return false;
    UObject* set = nullptr;
    if (!ue::Get<UObject*>(target, "HitReactionAnimSet", set) || !Live(set)) return false;
    UFunction* fn = ue::FindFunction(set, "GetHitReactionAnimation");
    if (!fn) return false;
    ue::Params p(fn);
    p.Set<ue::FName>("ReactionType", ue::MakeName(react.c_str()));
    p.Set<uint8_t>("Direction", (uint8_t)dir);
    p.Set<UObject*>("ASC", GetASC(target));
    if (!ue::Call(set, fn, p.data())) return false;
    UObject* mont = p.Get<UObject*>("ReturnValue");
    if (!Live(mont)) return false;
    UFunction* play = ue::FindFunction(target, "PlayAnimMontage");
    if (!play) return false;
    ue::Params pp(play);
    pp.Set<UObject*>("AnimMontage", mont);
    pp.Set<float>("InPlayRate", 1.0f);
    return ue::Call(target, play, pp.data());
}

bool ReactByPayload(UObject* target, const OutHit& h) {
    UClass* pCls = nullptr;
    if (!h.pcls.empty()) pCls = ue::FindClass(h.pcls.c_str());
    if (!pCls) pCls = ue::FindClass("SpartaHitPayload");
    UObject* pCdo = pCls ? ue::GetCDO(pCls) : nullptr;
    if (!Live(pCdo)) return false;
    UFunction* fnDup = ue::FindFunction(pCdo, "GetDuplicate");
    if (!fnDup) return false;
    UObject* dup = nullptr;
    {
        ue::Params p(fnDup);
        p.Set<UObject*>("Outer_0", target);
        p.Set<UObject*>("Outer", target);
        if (!ue::Call(pCdo, fnDup, p.data())) return false;
        dup = p.Get<UObject*>("ReturnValue");
    }
    if (!Live(dup)) return false;
    // zero health damage: health stays on the nhp lane
    struct { uint8_t type; uint8_t pad[3]; float val; float mult; } opt = {0, {0, 0, 0}, 0.0f, 0.0f};
    int32_t hdOff = ue::PropOffset(dup, "HealthDamage");
    if (hdOff < 0) hdOff = kOffHealthDamageOption;
    ue::WriteMem(reinterpret_cast<uint8_t*>(dup) + hdOff, &opt, sizeof(opt));
    float poiseF = (float)h.poise;
    int32_t pOff = ue::PropOffset(dup, "PoiseDamage");
    if (pOff < 0) pOff = kOffPoiseDamage;
    ue::WriteMem(reinterpret_cast<uint8_t*>(dup) + pOff, &poiseF, sizeof(poiseF));
    if (!h.react.empty()) {
        ue::FName rn = ue::MakeName(h.react.c_str());
        int32_t rOff = ue::PropOffset(dup, "ReactionTag");
        if (rOff < 0) rOff = kOffReactionTag;
        ue::WriteMem(reinterpret_cast<uint8_t*>(dup) + rOff, &rn, sizeof(rn));
    }
    uint8_t dirB = (uint8_t)h.dir;
    int32_t dOff = ue::PropOffset(dup, "StrikeDirection");
    if (dOff < 0) dOff = kOffStrikeDirection;
    ue::WriteMem(reinterpret_cast<uint8_t*>(dup) + dOff, &dirB, 1);

    UObject* pawn = net::Pawn();
    UObject* asc = GetASC(pawn);
    if (!Live(asc)) return false;
    UFunction* fn = ue::FindFunction(dup, "ApplyPayloadInstigator");
    if (!fn) return false;
    ue::Params p(fn);   // zeroed HitResult and EffectContext
    p.Set<UObject*>("Target", target);
    p.Set<UObject*>("InstigatingASC", asc);
    p.Set<UObject*>("DamageCauser", pawn);
    return ue::Call(dup, fn, p.data());   // ProcessEvent is SEH-guarded
}

struct RemoteApplyScope {
    RemoteApplyScope() { tl_inRemoteApply = true; }
    ~RemoteApplyScope() { tl_inRemoteApply = false; }
};

bool React(UObject* target, const OutHit& h, int mode) {
    RemoteApplyScope scope;
    switch (mode) {
        case 1: return ReactByEvent(target, h.react);
        case 2: return ReactByAnimSet(target, h.react, h.dir);
        case 3: return ReactByPayload(target, h);
        default: return false;
    }
}

// [R10-BOSSDMG] the actor carries a BPC_BossAI_C component (its outer is the actor)
bool IsBossActor(UObject* target) {
    for (UObject* c : ue::FindAllOf("BPC_BossAI_C")) {
        if (Live(c) && ue::GetOuter(c) == target) return true;
    }
    return false;
}

bool ApplyRemoteHit(const OutHit& h) {
    if (h.flags & kFlagPeerHit) return true;   // player hits stay on the puppetDamage lane
    UObject* target = FindTargetByKey(h.targetKey);
    if (!Live(target)) {
        ++g_failCount;
        return false;
    }
    bool dead = false;
    combat::HealthNorm(target, &dead);
    if (dead) return true;
    // the peer drives this enemy here (mirror): its copy already reacted and the
    // montage stream carries that; replaying would fight the mirror
    if (!state().combat.ownedTails.Has(h.targetKey)) {
        ++g_skipMirror;
        if (g_skipMirror <= 5)
            net::Log("hithook: remote hit seq=%d on %s skipped: the peer drives it here",
                     h.seq, h.targetKey.c_str());
        return true;
    }
    bool ok = false;
    const std::string montBefore = MontageOf(target);   // hitlook: sample BEFORE the reaction
    if (h.flags & kFlagParried) {
        RemoteApplyScope scope;
        SendGameplayEventTag(target, "Event.Hit.Parried", net::Pawn());
        ok = true;
    } else if (h.flags & kFlagFlyback) {
        RemoteApplyScope scope;
        SendGameplayEventTag(target, "Event.Reaction.Hit.Flyback", net::Pawn());
        ok = true;
    } else if (g_reactMode > 0) {
        ok = React(target, h, g_reactMode);
    }
    // [R10-BOSSDMG] measured (r10_boss5): a boss copy on the GUEST stays in its pre-intro
    // state (the intro's world event is suppressed there) and takes no damage, so the
    // guest's hits never reached the "nhp" lane and the host's boss kept its health.
    // For a host-owned BOSS the owner applies the hit's raw damage itself.  A copy that
    // did take the damage reports the same value on nhp (lowest wins: no double count).
    // coop\BOSSDMG_OFF.txt disables.
    if (h.dmg > 0.0 && !(h.flags & kFlagParried) && IsBossActor(target)) {
        static int off = -1;
        if (off < 0) off = net::FileExists("BOSSDMG_OFF.txt") ? 1 : 0;
        if (!off) {
            const double next = combat::DamageRaw(target, h.dmg);
            if (next > 0.01) boss::CheckPhaseAfterDamage(target);   // [R10-BOSSFIGHT]
            static int logs = 0;
            if (logs++ < 30)
                net::Log("hithook: [R10-BOSSDMG] peer hit seq=%d on boss %s dmg=%.1f -> hp %.4f",
                         h.seq, h.targetKey.c_str(), h.dmg, next);
            if (next >= 0.0 && next <= 0.01) {
                const bool dk = combat::StartDeath(target);
                net::Log("hithook: [R10-BOSSDMG] the peer's hit killed boss %s: startDeath ok=%d",
                         h.targetKey.c_str(), dk ? 1 : 0);
            }
        }
    }
    if (ok) ++g_reactOk;
    ++g_appliedCount;
    if (g_appliedCount <= 40) {
        net::Log("hithook: remote hit seq=%d target=%s react=%s pcls=%s poise=%.1f dir=%d mode=%d ok=%d",
                 h.seq, h.targetKey.c_str(), h.react.c_str(), h.pcls.c_str(), h.poise, h.dir,
                 g_reactMode, ok ? 1 : 0);
        QueueLook(target, h.targetKey, "rx", g_reactMode);
        if (!g_looks.empty() && g_looks.back().target == target) g_looks.back().before = montBefore;
    }
    return true;
}

// Parse one hit array element: [seq,"key",dmg,poise,brk,"react",dir,hx,hy,hz,flags]
bool ParseHitTuple(const std::string& s, OutHit* out) {
    // Minimal bracket-stripped comma parser respecting quotes
    std::vector<std::string> tok;
    bool inQ = false;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') { inQ = !inQ; continue; }
        if (!inQ && c == ',') { tok.push_back(cur); cur.clear(); continue; }
        if (!inQ && (c == '[' || c == ']' || c == ' ' || c == '\t')) continue;
        cur.push_back(c);
    }
    if (!cur.empty()) tok.push_back(cur);
    if (tok.size() < 11) return false;
    out->seq = std::atoi(tok[0].c_str());
    out->targetKey = tok[1];
    out->dmg = std::strtod(tok[2].c_str(), nullptr);
    out->poise = std::strtod(tok[3].c_str(), nullptr);
    out->brk = std::strtod(tok[4].c_str(), nullptr);
    out->react = tok[5];
    out->dir = std::atoi(tok[6].c_str());
    out->hx = std::strtod(tok[7].c_str(), nullptr);
    out->hy = std::strtod(tok[8].c_str(), nullptr);
    out->hz = std::strtod(tok[9].c_str(), nullptr);
    out->flags = std::atoi(tok[10].c_str());
    if (tok.size() >= 12) out->pcls = tok[11];
    return out->seq > 0 && !out->targetKey.empty();
}

}  // namespace

void Tick2() {
    if (!state().toggles.hitHook) return;
    if (!g_hookTarget) ResolveAndHook();
    TickLooks();
    // Expire old re-transmit entries
    const double now = Mono();
    while (!g_outQueue.empty() && now - g_outQueue.front().at > kTxRepeatSecs) {
        g_outQueue.pop_front();
    }
}

void Reset() {
    g_outQueue.clear();
    g_lastRxSeq = 0;
    g_looks.clear();
}

void Serialize(std::string* out) {
    if (!out || !state().toggles.hitHook || g_outQueue.empty()) return;
    *out += ",\"hit\":[";
    bool first = true;
    for (const OutHit& h : g_outQueue) {
        if (!first) *out += ',';
        first = false;
        *out += net::Fmt("[%d,\"%s\",%.1f,%.1f,%.1f,\"%s\",%d,%.0f,%.0f,%.0f,%d,\"%s\"]",
                         h.seq, EscapeStr(h.targetKey).c_str(), h.dmg, h.poise, h.brk,
                         EscapeStr(h.react).c_str(), h.dir, h.hx, h.hy, h.hz, h.flags,
                         EscapeStr(h.pcls).c_str());
    }
    *out += "]";
}

void OnPeerLine(const std::string& line) {
    if (!state().toggles.hitHook) return;
    std::string sec;
    if (!net::Section(line, "hit", &sec) || sec.size() < 4) return;
    // Walk inner [...] tuples inside sec
    size_t pos = 1;
    while (pos < sec.size()) {
        size_t open = sec.find('[', pos);
        if (open == std::string::npos) break;
        size_t close = sec.find(']', open);
        if (close == std::string::npos) break;
        OutHit h;
        if (ParseHitTuple(sec.substr(open, close - open + 1), &h)) {
            if (h.seq > g_lastRxSeq) {
                g_lastRxSeq = h.seq;
                ++g_rxCount;
                ApplyRemoteHit(h);
            }
        }
        pos = close + 1;
    }
}

std::string Status() {
    return net::Fmt("hithook=%s hooked:%d rva:0x%llx calls:%d tx:%d rx:%d applied:%d react:%d mode:%d skipmir:%d veto:%d parries:%d fail:%d\n",
                    state().toggles.hitHook ? "on" : "off",
                    g_hookTarget ? 1 : 0,
                    (unsigned long long)g_hookRva,
                    g_callCount.load(std::memory_order_relaxed),
                    g_txCount, g_rxCount, g_appliedCount, g_reactOk, g_reactMode, g_skipMirror,
                    g_vetoCount, g_parryCount, g_failCount);
}

bool OnCommand(const std::string& cmd, const std::string& rest) {
    (void)rest;
    if (cmd == "hitprobe") {
        net::Log("hitprobe: enabled=%d hooked=%d rva=0x%llX vtableRva=0x%llX tx=%d rx=%d applied=%d veto=%d parries=%d fail=%d",
                 state().toggles.hitHook ? 1 : 0, g_hookTarget ? 1 : 0,
                 (unsigned long long)g_hookRva, (unsigned long long)g_vtableRva,
                 g_txCount, g_rxCount, g_appliedCount, g_vetoCount, g_parryCount, g_failCount);
        return true;
    }
    if (cmd == "hitmode") {
        g_reactMode = std::atoi(rest.c_str());
        net::Log("hitmode: remote hit reaction mode = %d", g_reactMode);
        return true;
    }
    if (cmd == "hitreact") {
        // hitreact <mode> [reactTag] -- play a reaction on the nearest enemy HERE
        // through one method, and log its montage before/after (hitlook lab ...)
        int mode = std::atoi(rest.c_str());
        std::string tag = "Event.Reaction.Hit.Light";
        const size_t sp = rest.find(' ');
        if (sp != std::string::npos && sp + 1 < rest.size()) tag = rest.substr(sp + 1);
        UObject* best = nullptr;
        double bestD = 1e9;
        for (const NpcEntry& e : state().combat.npcLast) {
            if (Live(e.actor) && e.distSelf < bestD) { best = e.actor; bestD = e.distSelf; }
        }
        if (!best) { net::Log("hitreact: no nearby enemy"); return true; }
        OutHit h;
        h.targetKey = KeyForTarget(best);
        h.react = tag;
        h.poise = 20.0;
        const std::string before = MontageOf(best);
        const bool ok = React(best, h, mode);
        net::Log("hitreact: mode=%d tag=%s on %s d=%.0f ok=%d montage-before=%s", mode, tag.c_str(),
                 h.targetKey.c_str(), bestD, ok ? 1 : 0, before.c_str());
        QueueLook(best, h.targetKey, "lab", mode);
        return true;
    }
    if (cmd == "hittest") {
        // Send a synthetic 15-damage test hit on the nearest enemy
        UObject* best = nullptr;
        double bestD = 1e9;
        const Vec3 me = net::Loc(net::Pawn());
        for (const NpcEntry& e : state().combat.npcLast) {
            if (Live(e.actor) && e.distSelf < bestD) { best = e.actor; bestD = e.distSelf; }
        }
        if (!best) {
            net::Log("hittest: no nearby enemy");
            return true;
        }
        const std::string key = KeyForTarget(best);
        EnqueueHit(key, 15.0, 20.0, 10.0, "Event.Reaction.Hit.Light", 0, net::Loc(best), 0);
        net::Log("hittest: queued 15 dmg hit on %s (seq=%d)", key.c_str(), g_nextSeq - 1);
        return true;
    }
    return false;
}

}  // namespace hithook
}  // namespace coop
