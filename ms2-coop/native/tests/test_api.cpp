// test_api.cpp -- the header compiles, and every signature is the one the brief
// asked for.
//
// The co-op lanes are written against ms2_ue.h in parallel with this file, so the
// failure mode worth guarding against is not a bug in the core but a DRIFT in the
// contract: a parameter that became a reference, a return type that lost its
// constness, a name that got renamed.  Taking the address of each function into a
// variable of the EXACT expected type turns any such drift into a compile error
// here, at build time, instead of an error in somebody else's file.
#include <windows.h>
#include <stdio.h>

#include <functional>
#include <string>
#include <type_traits>
#include <vector>

#include "ms2_ue.h"
#include "coop_state.h"

namespace {

// Each of these must compile.  If a signature changes, this file stops building.
bool* pb = nullptr;

void CompileTheContract() {
    using namespace ue;

    bool        (*f_init)()                       = &Init;
    bool        (*f_ready)()                      = &IsReady;
    const char* (*f_report)()                     = &InitReport;

    std::string (*f_n1)(UObject*)                 = &GetName;
    std::string (*f_n2)(UObject*)                 = &GetFullName;
    std::string (*f_n3)(UObject*)                 = &GetClassName;

    UObject*    (*f_o1)(const char*)              = &FindObject;
    UClass*     (*f_c1)(const char*)              = &FindClass;
    UObject*    (*f_o2)(const char*)              = &FindFirstOf;
    std::vector<UObject*> (*f_o3)(const char*)    = &FindAllOf;

    bool        (*f_b1)(UObject*, UClass*)        = &IsA;
    bool        (*f_b2)(UObject*)                 = &IsValid;
    UObject*    (*f_o4)(UObject*)                 = &GetOuter;
    UClass*     (*f_c2)(UObject*)                 = &GetClass;

    FProperty*  (*f_p1)(UStruct*, const char*)    = &FindProperty;
    int32_t     (*f_p2)(UObject*, const char*)    = &PropOffset;
    int32_t     (*f_p3)(UObject*, const char*)    = &PropSize;
    bool        (*f_b3)(UObject*, const char*)    = &GetBool;
    bool        (*f_b4)(UObject*, const char*, bool) = &SetBool;

    UFunction*  (*f_f1)(UObject*, const char*)    = &FindFunction;
    bool        (*f_f2)(UObject*, UFunction*, void*) = &Call;
    bool        (*f_f3)(UObject*, const char*, void*) = &Call;

    bool        (*f_h1)(const char*, HookFn)      = &HookPre;
    bool        (*f_h2)(const char*, HookFn)      = &HookPost;
    void        (*f_h3)(std::function<void()>)    = &EnqueueGameThread;
    void        (*f_h4)(std::function<void(float)>) = &OnTick;

    FName       (*f_m1)(const char*)              = &MakeName;
    FName       (*f_m2)(const char*)              = &MakeFName;

    UObject*    (*f_w1)(UClass*)                  = &GetCDO;
    UObject*    (*f_w2)()                         = &GetWorld;

    uint64      (*f_s1)()                         = &ProcessEventCount;
    const Resolved& (*f_s2)()                     = &GetResolved;
    void        (*f_s3)(const char*)              = &SelfTest;

    (void)f_init; (void)f_ready; (void)f_report;
    (void)f_n1; (void)f_n2; (void)f_n3;
    (void)f_o1; (void)f_c1; (void)f_o2; (void)f_o3;
    (void)f_b1; (void)f_b2; (void)f_o4; (void)f_c2;
    (void)f_p1; (void)f_p2; (void)f_p3; (void)f_b3; (void)f_b4;
    (void)f_f1; (void)f_f2; (void)f_f3;
    (void)f_h1; (void)f_h2; (void)f_h3; (void)f_h4;
    (void)f_m1; (void)f_m2; (void)f_w1; (void)f_w2;
    (void)f_s1; (void)f_s2; (void)f_s3;
}

// The value types must have the sizes the offsets assume.  FVector/FRotator being
// double is the UE5 change that silently ruins a mod when it is missed.
static_assert(sizeof(ue::FName) == 8, "FName is two uint32 (id + number)");
static_assert(sizeof(ue::FString) == 16, "FString is ptr/num/max");
static_assert(sizeof(ue::TArray<int>) == 16, "TArray is ptr/num/max");
static_assert(sizeof(ue::FVector) == 24, "UE5 FVector is DOUBLE precision");
static_assert(sizeof(ue::FRotator) == 24, "UE5 FRotator is DOUBLE precision");
static_assert(sizeof(ue::FQuat) == 32, "UE5 FQuat is DOUBLE precision");
static_assert(sizeof(ue::FTransform) == 0x60, "UE5 FTransform: quat @0, translation @0x20, scale @0x40 (16-byte aligned members)");

// The Params contract: constructible from a UFunction, name-addressed, zero-init.
void CompileTheParamsContract(ue::UFunction* fn) {
    ue::Params p(fn);
    p.Set<float>("Damage", 1.0f);
    p.Set<int32_t>("Index", 0);
    p.Set<ue::FVector>("Location", ue::FVector{1.0, 2.0, 3.0});
    const float f = p.Get<float>("Damage");
    void* d = p.data();
    (void)f; (void)d;
}

// ---------------------------------------------------------------------------
// The co-op half of the contract (coop_state.h + the API the modules call).
//
// These are all decltype/static_cast checks in an UNEVALUATED operand, so they
// need no definitions to link against -- this test still links only ms2_ue.cpp.
// A signature that drifts breaks the build HERE rather than inside a module.
// ---------------------------------------------------------------------------
using MapEachFn   = bool (*)(ue::UObject*, const char*, std::function<bool(void*, void*)>);
using MapEachRaw1 = bool (*)(const void*, std::function<bool(void*, void*)>);
using MapEachRaw2 = bool (*)(const void*, ue::int32, ue::int32,
                             std::function<bool(void*, void*)>);

static_assert(std::is_same<decltype(static_cast<MapEachFn>(&ue::MapForEach)), MapEachFn>::value,
              "ue::MapForEach(UObject*, const char*, fn)");
static_assert(std::is_same<decltype(static_cast<MapEachRaw1>(&ue::MapForEachRaw)), MapEachRaw1>::value,
              "ue::MapForEachRaw(const void*, fn)");
static_assert(std::is_same<decltype(static_cast<MapEachRaw2>(&ue::MapForEachRaw)), MapEachRaw2>::value,
              "ue::MapForEachRaw(const void*, keySize, valueSize, fn)");

static_assert(std::is_same<decltype(&ue::ToString), std::string (*)(ue::FName)>::value,
              "ue::ToString(FName)");
static_assert(std::is_same<decltype(&ue::ToUtf8), std::string (*)(const ue::FString&)>::value,
              "ue::ToUtf8(const FString&)");
static_assert(std::is_same<decltype(&ue::TextToString), std::string (*)(void*)>::value,
              "ue::TextToString(void*)");
static_assert(std::is_same<decltype(&ue::MapAddNameInt),
                           bool (*)(ue::UObject*, const char*, const char*, ue::int32)>::value,
              "ue::MapAddNameInt(UObject*, prop, key, int32)");
static_assert(std::is_same<decltype(&ue::MapAddStrInt),
                           bool (*)(ue::UObject*, const char*, const char*, ue::int32)>::value,
              "ue::MapAddStrInt(UObject*, prop, key, int32)");

// Section 1 of the shared header: the math adapter (coop_math.cpp).
static_assert(std::is_same<decltype(&coop::ToVec3),
                           coop::Vec3 (*)(const ue::FVector&)>::value, "coop::ToVec3");
static_assert(std::is_same<decltype(&coop::FromVec3),
                           ue::FVector (*)(const coop::Vec3&)>::value, "coop::FromVec3");
static_assert(std::is_same<decltype(&coop::ToRot),
                           coop::Rot (*)(const ue::FRotator&)>::value, "coop::ToRot");
static_assert(std::is_same<decltype(&coop::FromRot),
                           ue::FRotator (*)(const coop::Rot&)>::value, "coop::FromRot");
static_assert(std::is_same<decltype(&coop::ToQuat),
                           coop::Quat (*)(const ue::FQuat&)>::value, "coop::ToQuat");
static_assert(std::is_same<decltype(&coop::FromQuat),
                           ue::FQuat (*)(const coop::Quat&)>::value, "coop::FromQuat");
static_assert(std::is_same<decltype(&coop::ToXform),
                           coop::Xform (*)(const ue::FTransform&)>::value, "coop::ToXform");
static_assert(std::is_same<decltype(&coop::FromXform),
                           ue::FTransform (*)(const coop::Xform&)>::value, "coop::FromXform");
static_assert(std::is_same<decltype(&coop::Dist),
                           double (*)(const coop::Vec3&, const coop::Vec3&)>::value, "coop::Dist");
static_assert(std::is_same<decltype(&coop::RotQuat),
                           coop::Quat (*)(const coop::Rot&)>::value, "coop::RotQuat");
static_assert(std::is_same<decltype(&coop::QuatMul),
                           coop::Quat (*)(const coop::Quat&, const coop::Quat&)>::value, "coop::QuatMul");
static_assert(std::is_same<decltype(&coop::QuatRot),
                           coop::Vec3 (*)(const coop::Quat&, const coop::Vec3&)>::value, "coop::QuatRot");
static_assert(std::is_same<decltype(&coop::Compose),
                           coop::Xform (*)(const coop::Xform&, const coop::Xform&)>::value, "coop::Compose");
static_assert(std::is_same<decltype(&coop::NormDeg), double (*)(double)>::value, "coop::NormDeg");

// The cross-module entry points the integrator added to the shared header.
static_assert(std::is_same<decltype(&coop::Start), void (*)()>::value, "coop::Start");
static_assert(std::is_same<decltype(&coop::net::ReadText),
                           bool (*)(const std::string&, std::string*)>::value, "coop::net::ReadText");
static_assert(std::is_same<decltype(&coop::local::RefreshChars), void (*)()>::value,
              "coop::local::RefreshChars");
static_assert(std::is_same<decltype(&coop::combat::KillTail),
                           bool (*)(const std::string&, const char*)>::value, "coop::combat::KillTail");
static_assert(std::is_same<decltype(&coop::puppet::StartTp),
                           void (*)(ue::UObject*, double, double, double)>::value, "coop::puppet::StartTp");
static_assert(std::is_same<decltype(&coop::puppet::StartTravel),
                           void (*)(ue::UObject*, double, double, double, double)>::value,
              "coop::puppet::StartTravel");
static_assert(std::is_same<decltype(&coop::puppet::MovementMode), int (*)()>::value,
              "coop::puppet::MovementMode");
static_assert(std::is_same<decltype(&coop::puppet::OwnStreamingIncomplete),
                           bool (*)(ue::UObject*)>::value, "coop::puppet::OwnStreamingIncomplete");

// CombatState::spawnAlias carries a VALUE (enemy tail -> "SP_<spawner tail>"), which
// a StrSet could not; this is what coop_local folds into tickTails.
static_assert(std::is_same<decltype(coop::CombatState::spawnAlias), coop::StrStrMap>::value,
              "CombatState::spawnAlias is a string -> string map");

}  // namespace

int main() {
    // Nothing to run: this test's whole value is that it COMPILES.  It also checks
    // that the sizes above hold on the compiler actually shipping the DLL.
    printf("test_api: PASS -- ms2_ue.h compiles and every declared signature matches\n");

    // Touch one function so the linker keeps the object around.
    printf("test_api: ProcessEvent calls so far = %llu\n",
           (unsigned long long)ue::ProcessEventCount());
    return 0;
}
