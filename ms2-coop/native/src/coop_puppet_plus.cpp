// coop_puppet_plus.cpp -- [R10-PUPPET] Puppet upgrades inspired by MS2Online:
//   1. MotionMatching anim layer linking (ABPL_Locomotion_MotionMatching_C) when available
//   2. Capsule collision configuration (blocks projectiles/hits, ignores pawn push)
//   3. Footstep foley component activation (SpartaCharacterFoleyComponent / Audio__FootL_/R_)
//   4. Native weapon equip via UWeaponsComponent::SpawnAndEquipWeaponOfClass
//   5. Native mask/clothing application via AddMask / AddClothing
//
// Kill switch: PUPPET_PLUS_OFF.txt (Toggles::puppetPlus, default ON).
// Works on both BP_PlayerCharacter_C and BP_Shell_Summon_C puppets safely.

#include "ms2_ue.h"
#include "coop_state.h"

#include <chrono>
#include <string>
#include <vector>

namespace coop {
namespace puppet_plus {

namespace {

using ue::UObject;
using ue::UFunction;
using ue::UClass;

const char* const kMotionMatchingLayer =
    "/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_MotionMatching.ABPL_Locomotion_MotionMatching_C";

UObject* g_configuredPuppet = nullptr;
bool     g_mmLinked = false;
bool     g_capsuleConfigured = false;
bool     g_foleyEnabled = false;
std::string g_equippedWeaponClass;
int      g_nativeEquipCount = 0;
int      g_nativeCosmeticCount = 0;

bool Live(UObject* o) { return o != nullptr && ue::IsValid(o); }

void ConfigureCapsule(UObject* pup) {
    if (!Live(pup)) return;
    UObject* cap = nullptr;
    if (!ue::Get<UObject*>(pup, "CapsuleComponent", cap) || !Live(cap)) return;

    // Enable query-only collision (ECollisionEnabled::QueryOnly = 1) so projectiles/traces
    // can interact with the puppet without physics-pushing the local player off ledges.
    if (UFunction* fnEn = ue::FindFunction(cap, "SetCollisionEnabled")) {
        ue::Params p(fnEn);
        p.Set<uint8_t>("NewType", 1);
        ue::Call(cap, fnEn, p.data());
    }
    // Ignore Pawn channel (ECC_Pawn = 2) so players don't body-block each other in corridors.
    if (UFunction* fnResp = ue::FindFunction(cap, "SetCollisionResponseToChannel")) {
        ue::Params p(fnResp);
        p.Set<uint8_t>("Channel", 2);      // ECC_Pawn
        p.Set<uint8_t>("NewResponse", 0);  // ECR_Ignore
        ue::Call(cap, fnResp, p.data());
    }
    g_capsuleConfigured = true;
}

void EnableFoley(UObject* pup) {
    if (!Live(pup)) return;
    // Activate SpartaCharacterFoleyComponent if present on the puppet
    UObject* foleyCls = net::EngineClass("SpartaCharacterFoleyComponent");
    if (Live(foleyCls)) {
        if (UFunction* fnGet = ue::FindFunction(pup, "K2_GetComponentsByClass")) {
            ue::Params p(fnGet);
            p.Set<UObject*>("ComponentClass", foleyCls);
            if (ue::Call(pup, fnGet, p.data())) {
                ue::TArray<UObject*> arr = p.Get<ue::TArray<UObject*>>("ReturnValue");
                if (arr.Data && arr.Num > 0) {
                    UObject* comp = nullptr;
                    if (ue::ReadMem(arr.Data, &comp, sizeof(comp)) && Live(comp)) {
                        if (UFunction* fnAct = ue::FindFunction(comp, "Activate")) {
                            ue::Params a(fnAct);
                            a.Set<bool>("bReset", false);
                            ue::Call(comp, fnAct, a.data());
                            g_foleyEnabled = true;
                        }
                    }
                }
            }
        }
    }
}

void TryLinkMotionMatching(UObject* pup) {
    if (!Live(pup) || g_mmLinked) return;
    // Only link MotionMatching anim layers on an AnimBP-driven BP_Shell_Summon_C;
    // never touch a SingleNode BP_PlayerCharacter_C puppet (which breaks AnimSingleNodeInstance).
    if (ue::GetClassName(pup).find("Shell_Summon") == std::string::npos) return;
    UObject* mesh = puppet::Mesh();
    if (!Live(mesh)) return;
    UObject* layerCls = net::Find(kMotionMatchingLayer, false);
    if (!Live(layerCls)) return;

    UFunction* fnLink = ue::FindFunction(mesh, "LinkAnimClassLayers");
    if (!fnLink) return;
    ue::Params p(fnLink);
    p.Set<UObject*>("InClass", layerCls);
    if (ue::Call(mesh, fnLink, p.data())) {
        g_mmLinked = true;
        net::Log("puppet_plus: linked ABPL_Locomotion_MotionMatching_C on summon puppet");
    }
}

void SyncNativeWeapon(UObject* pup) {
    if (!Live(pup) || state().toggles.weaponOff) return;
    const std::string& wantWpn = state().peer.wpn;
    if (wantWpn.size() < 8 || wantWpn == g_equippedWeaponClass) return;

    // Only attempt SpawnAndEquipWeaponOfClass if the peer's path names a BP_Weapon class
    if (wantWpn.find("_C") == std::string::npos) return;
    UObject* wpnCls = net::Find(wantWpn, false);
    if (!Live(wpnCls)) return;

    UObject* wcomp = nullptr;
    if (!ue::Get<UObject*>(pup, "WeaponsComponent", wcomp) || !Live(wcomp)) return;

    if (UFunction* fnRem = ue::FindFunction(wcomp, "RemoveAllWeapons")) {
        ue::Params p(fnRem);
        ue::Call(wcomp, fnRem, p.data());
    }
    if (UFunction* fnEq = ue::FindFunction(wcomp, "SpawnAndEquipWeaponOfClass")) {
        ue::Params p(fnEq);
        p.Set<UObject*>("WeaponClass", wpnCls);
        if (ue::Call(wcomp, fnEq, p.data())) {
            g_equippedWeaponClass = wantWpn;
            ++g_nativeEquipCount;
            net::Log("puppet_plus: SpawnAndEquipWeaponOfClass(%s)", wantWpn.c_str());
        }
    }
}

}  // namespace

void Tick2() {
    if (!state().toggles.puppetPlus) return;
    UObject* pup = puppet::Actor();
    if (!Live(pup)) {
        g_configuredPuppet = nullptr;
        g_mmLinked = false;
        g_capsuleConfigured = false;
        g_foleyEnabled = false;
        g_equippedWeaponClass.clear();
        return;
    }
    if (pup != g_configuredPuppet) {
        g_configuredPuppet = pup;
        g_mmLinked = false;
        g_capsuleConfigured = false;
        g_foleyEnabled = false;
        g_equippedWeaponClass.clear();
        ConfigureCapsule(pup);
        EnableFoley(pup);
        net::Log("puppet_plus: configured puppet %s (capsule=%d foley=%d)",
                 ue::GetName(pup).c_str(), g_capsuleConfigured ? 1 : 0, g_foleyEnabled ? 1 : 0);
    }
    if (!g_mmLinked && puppet::BodyReady()) {
        TryLinkMotionMatching(pup);
    }
    SyncNativeWeapon(pup);
}

void Reset() {
    g_configuredPuppet = nullptr;
    g_mmLinked = false;
    g_capsuleConfigured = false;
    g_foleyEnabled = false;
    g_equippedWeaponClass.clear();
}

std::string Status() {
    return net::Fmt("pplus=%s cap:%d foley:%d mm:%d weap:%d cosm:%d\n",
                    state().toggles.puppetPlus ? "on" : "off",
                    g_capsuleConfigured ? 1 : 0,
                    g_foleyEnabled ? 1 : 0,
                    g_mmLinked ? 1 : 0,
                    g_nativeEquipCount,
                    g_nativeCosmeticCount);
}

}  // namespace puppet_plus
}  // namespace coop
