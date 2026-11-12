// _test_coop_combat.cpp -- THROWAWAY test harness for coop_combat.cpp.
// ===========================================================================
// OPT-IN ONLY: this file compiles to nothing unless COOP_TEST_FAKE_ENGINE is
// defined, so it can never collide with the real ue::/coop::net definitions in a
// wildcard build. To run it:
//   cl /std:c++17 /EHsc /DCOOP_TEST_FAKE_ENGINE /I native /I native\src ^
//      native\src\coop_combat.cpp tests\_test_coop_combat.cpp
// It fakes the engine layer and net:: and checks the frozen wire grammar
// (npc/pd/nhp/dead), the peer-line parsers, mirror take/drive/release, the
// health write, pd baselines and the spawner alias/replay against main.lua.
// ===========================================================================
#ifdef COOP_TEST_FAKE_ENGINE
// Not part of the DLL: it fakes the ue:: and coop::net symbols so the module can
// be exercised on this machine. Deleted after the run.
#include "ms2_ue.h"
#include "coop_state.h"

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// ===========================================================================
// fake engine
// ===========================================================================
namespace ue {

struct UClass;

struct UObject {
    // FIRST member on purpose: ms2_ue.h's Get/Set read property bytes at
    // (uint8_t*)o + PropOffset, so the fake property store has to live there.
    unsigned char mem[1024];

    std::string full;
    std::string tag;
    UClass* cls = nullptr;
    bool valid = true;
    std::map<std::string, int> props;

    UObject() { std::memset(mem, 0, sizeof(mem)); }

    FVector loc, vel;
    FRotator rot;
    UObject* owner = nullptr;
    UObject* controller = nullptr;
    UObject* montage = nullptr;
    UObject* overrideTarget = nullptr;
    bool aiStopped = false;
    bool dead = false;
    std::string lastReason;
    int moveCalls = 0;
    int modeSetCount = 0;
    int lastMode = -1;
    int startDeathCalls = 0;
    int restartCalls = 0;
    int triggerCalls = 0;
};

struct UClass : UObject {};

struct UFunction {
    std::string name;
    std::vector<std::pair<std::string, int> > params;
    int size = 1;
};

std::vector<UObject*>& All();
std::map<std::string, UFunction*>& Fns();
std::map<std::string, UObject*>& Objects();

UFunction* DefFn(const char* name, std::vector<std::pair<std::string, int> > sized);

int32_t FnOffset(UFunction* fn, const char* n) {
    if (!fn) return -1;
    for (std::size_t i = 0; i < fn->params.size(); ++i) {
        if (fn->params[i].first == n) return fn->params[i].second;
    }
    return -1;
}

int AddProp(UObject* o, const std::string& name, int off) {
    o->props[name] = off;
    return off;
}

template <class T>
void SetPtr(UObject* o, const std::string& name, T v) {
    std::memcpy(&o->mem[(std::size_t)o->props[name]], &v, sizeof(T));
}

}  // namespace ue

namespace ue {

Params::Params(UFunction* fn) : fn_(fn) {
    size_ = fn ? fn->size : 1;
    if (size_ < 1) size_ = 1;
    buf_ = static_cast<uint8_t*>(std::calloc(1, (std::size_t)size_));
}
Params::~Params() { std::free(buf_); }
int32_t Params::offset_of(const char* name) const { return FnOffset(fn_, name); }

bool IsValid(UObject* o) { return o != nullptr && o->valid; }

std::string GetName(UObject* o) {
    if (!o) return "";
    std::size_t p = o->full.find_last_of('.');
    const std::size_t q = o->full.find_last_of(' ');
    if (q != std::string::npos && (p == std::string::npos || q > p)) p = q;
    return (p == std::string::npos) ? o->full : o->full.substr(p + 1);
}
std::string GetFullName(UObject* o) { return o ? o->full : std::string(); }
std::string GetClassName(UObject* o) { return (o && o->cls) ? GetName(o->cls) : std::string(); }
UClass* GetClass(UObject* o) { return o ? o->cls : nullptr; }

std::vector<UObject*> FindAllOf(const char* cls) {
    std::vector<UObject*> out;
    for (UObject* o : All()) {
        if (!o || !o->valid) continue;
        if (std::string(cls) == "Character") {
            if (o->tag == "Character") out.push_back(o);
            continue;
        }
        if (o->cls && GetName(o->cls) == cls) out.push_back(o);
    }
    return out;
}

UObject* FindObject(const char* pathOrName) {
    auto it = Objects().find(pathOrName);
    return it == Objects().end() ? nullptr : it->second;
}

int32_t PropOffset(UObject* o, const char* name) {
    if (!o) return -1;
    auto it = o->props.find(name);
    return it == o->props.end() ? -1 : it->second;
}

bool ReadMem(const void* src, void* dst, size_t n) {
    if (!src || !dst) return false;
    std::memcpy(dst, src, n);
    return true;
}
bool WriteMem(void* dst, const void* src, size_t n) {
    if (!dst || !src) return false;
    std::memcpy(dst, src, n);
    return true;
}

UFunction* FindFunction(UObject* o, const char* name) {
    (void)o;
    auto it = Fns().find(name);
    return it == Fns().end() ? nullptr : it->second;
}

static bool SpartaHook(UObject* o, UFunction* fn, void* params);

static bool Dispatch(UObject* o, UFunction* fn, void* params) {
    if (!o || !fn) return false;
    if (!IsValid(o)) return false;
    std::printf("D %s o=%p\n", fn->name.c_str(), static_cast<void*>(o));
    std::fflush(stdout);
    if (SpartaHook(o, fn, params)) return true;

    uint8_t* b = static_cast<uint8_t*>(params);
    const std::string& n = fn->name;

    auto at = [&](const char* p) { return b + FnOffset(fn, p); };

    if (n == "K2_GetActorLocation") { *reinterpret_cast<FVector*>(b + FnOffset(fn, "ReturnValue")) = o->loc; return true; }
    if (n == "K2_GetActorRotation") { *reinterpret_cast<FRotator*>(b + FnOffset(fn, "ReturnValue")) = o->rot; return true; }
    if (n == "GetVelocity") { *reinterpret_cast<FVector*>(b + FnOffset(fn, "ReturnValue")) = o->vel; return true; }
    if (n == "GetController") { *reinterpret_cast<UObject**>(b + FnOffset(fn, "ReturnValue")) = o->controller; return true; }
    if (n == "GetCurrentMontage") { *reinterpret_cast<UObject**>(b + FnOffset(fn, "ReturnValue")) = o->montage; return true; }
    if (n == "GetOwner") { *reinterpret_cast<UObject**>(b + FnOffset(fn, "ReturnValue")) = o->owner; return true; }
    if (n == "StartDeath") { o->dead = true; o->startDeathCalls++; return true; }
    if (n == "RestartLogic") { o->aiStopped = false; o->restartCalls++; return true; }
    if (n == "StopLogic") {
        o->aiStopped = true;
        FString* s = reinterpret_cast<FString*>(at("Reason"));
        std::string a;
        for (int i = 0; s->Data && i < s->Num && i < 40; ++i) {
            if (!s->Data[i]) break;
            a.push_back(static_cast<char>(s->Data[i]));
        }
        o->lastReason = a;
        return true;
    }
    if (n == "SetMovementMode") {
        o->lastMode = static_cast<int>(*reinterpret_cast<uint8_t*>(at("NewMovementMode")));
        o->modeSetCount++;
        return true;
    }
    if (n == "K2_SetActorLocation") {
        o->loc = *reinterpret_cast<FVector*>(at("NewLocation"));
        o->moveCalls++;
        *reinterpret_cast<bool*>(b + FnOffset(fn, "ReturnValue")) = true;
        return true;
    }
    if (n == "K2_SetActorRotation") {
        o->rot = *reinterpret_cast<FRotator*>(at("NewRotation"));
        *reinterpret_cast<bool*>(b + FnOffset(fn, "ReturnValue")) = true;
        return true;
    }
    if (n == "PlayAnimMontage") {
        o->montage = *reinterpret_cast<UObject**>(at("AnimMontage"));
        return true;
    }
    if (n == "StopAnimMontage") { o->montage = nullptr; return true; }
    if (n == "SetTargetOverride") { o->overrideTarget = *reinterpret_cast<UObject**>(at("NewTarget")); return true; }
    if (n == "GetWorldDeltaSeconds") { *reinterpret_cast<double*>(b + FnOffset(fn, "ReturnValue")) = 0.5; return true; }
    if (n.rfind("BndEvt__", 0) == 0) { o->triggerCalls++; return true; }
    std::printf("D (unknown) %s\n", n.c_str());
    std::fflush(stdout);
    return false;
}

bool Call(UObject* o, UFunction* fn, void* params) { return Dispatch(o, fn, params); }
bool Call(UObject* o, const char* name, void* params) { return Dispatch(o, FindFunction(o, name), params); }

std::vector<UObject*>& All() { static std::vector<UObject*> v; return v; }
std::map<std::string, UFunction*>& Fns() { static std::map<std::string, UFunction*> m; return m; }
std::map<std::string, UObject*>& Objects() { static std::map<std::string, UObject*> m; return m; }

UFunction* DefFn(const char* name, std::vector<std::pair<std::string, int> > sized) {
    UFunction* f = new UFunction();
    f->name = name;
    int off = 0;
    for (std::size_t i = 0; i < sized.size(); ++i) {
        f->params.push_back(std::make_pair(sized[i].first, off));
        off += sized[i].second;
    }
    f->size = (off > 0) ? off : 1;
    Fns()[f->name] = f;
    return f;
}

}  // namespace ue

using ue::AddProp;
using ue::DefFn;
using ue::SetPtr;
using ue::UClass;
using ue::UFunction;
using ue::UObject;

// ===========================================================================
// fake coop state / net / puppet
// ===========================================================================
static double g_now = 1000.0;
static std::map<std::string, std::string> g_files;
static int g_failures = 0;
static int g_warns = 0;

namespace coop {

State& state() { static State s; return s; }
const Toggles& toggles() { return state().toggles; }

namespace net {

void Log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::printf("  log: %s\n", buf);
    std::fflush(stdout);
    if (std::strncmp(buf, "WARN", 4) == 0 || std::strncmp(buf, "ERR", 3) == 0) ++g_warns;
}

std::string Fmt(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

void AppendFmt(std::string* out, const char* fmt, ...) {
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    *out += buf;
}

bool WriteFile(const std::string& name, const std::string& text) {
    g_files[name] = text;
    return true;
}

double Now() { return g_now; }
int64_t Wall() { return static_cast<int64_t>(g_now); }
void SetLocalHold(double secs) { state().localHoldUntil = static_cast<int64_t>(g_now + secs); }

bool Valid(UObject* o) { return ue::IsValid(o); }
std::string FullName(UObject* o) {
    if (!o) return "<nil>";
    if (!ue::IsValid(o)) return "UObject <invalid>";
    return ue::GetFullName(o);
}

std::string Tail(UObject* o) {
    const std::string fn = FullName(o);
    std::size_t end = fn.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(fn[end - 1]))) --end;
    if (end < fn.size() && end >= 3 && fn.compare(end - 3, 3, "_C_") == 0) {
        std::size_t start = end - 3;
        while (start > 0 &&
               (std::isalnum(static_cast<unsigned char>(fn[start - 1])) || fn[start - 1] == '_')) {
            --start;
        }
        return fn.substr(start);
    }
    const std::size_t p = fn.find_last_of(".:");
    return (p == std::string::npos) ? fn : fn.substr(p + 1);
}

bool Section(const std::string& line, const char* key, std::string* out) {
    const std::string needle = std::string("\"") + key + "\":";
    const std::size_t i = line.find(needle);
    if (i == std::string::npos) return false;
    const std::size_t start = line.find('[', i);
    if (start == std::string::npos) return false;
    int depth = 0;
    for (std::size_t j = start; j < line.size(); ++j) {
        if (line[j] == '[') ++depth;
        else if (line[j] == ']') {
            --depth;
            if (depth == 0) {
                *out = line.substr(start, j - start + 1);
                return true;
            }
        }
    }
    return false;
}

bool KvNum(const std::string& line, const char* key, double* out) {
    const std::string needle = std::string("\"") + key + "\":";
    const std::size_t i = line.find(needle);
    if (i == std::string::npos) return false;
    const char* p = line.c_str() + i + needle.size();
    char* end = nullptr;
    const double v = std::strtod(p, &end);
    if (end == p) return false;
    *out = v;
    return true;
}

UObject* Pawn() { return ue::FindObject("__pawn"); }
UObject* World() { return nullptr; }
UObject* PC() { return nullptr; }

}  // namespace net

namespace puppet {
UObject* Actor() { return ue::FindObject("__puppet"); }
}

// coop_local owns the trait caches now (coop_state.h section 9); combat forwards
// here. Same body as main.lua:638-650, alias folding included.
namespace local {
void RefreshChars() {
    state().local.tickChars = ue::FindAllOf("Character");
    state().local.tickTails.clear();
    UObject* pup = puppet::Actor();
    for (UObject* a : state().local.tickChars) {
        if (!a || a == pup) continue;
        const std::string tail = net::Tail(a);
        bool replaced = false;
        for (auto& row : state().local.tickTails) {
            if (row.tail == tail) { row.obj = a; replaced = true; break; }
        }
        if (!replaced) state().local.tickTails.push_back(NamedActor{tail, a});
    }
    for (const auto& al : state().combat.spawnAlias.Items()) {
        UObject* a = nullptr;
        for (const auto& row : state().local.tickTails) {
            if (row.tail == al.key) { a = row.obj; break; }
        }
        if (!a) continue;
        bool replaced = false;
        for (auto& row : state().local.tickTails) {
            if (row.tail == al.value) { row.obj = a; replaced = true; break; }
        }
        if (!replaced) state().local.tickTails.push_back(NamedActor{al.value, a});
    }
}
}  // namespace local

}  // namespace coop

// ===========================================================================
// attribute sets (real FGameplayAttributeData layout: base then current)
// ===========================================================================
namespace ue {
static float AttrAt(UObject* hs, int off, int slot) {
    float v = 0.0f;
    std::memcpy(&v, &hs->mem[(std::size_t)off + 4 * slot], 4);
    return v;
}
static void AttrWrite(UObject* hs, int off, float v) {
    std::memcpy(&hs->mem[(std::size_t)off], &v, 4);
    std::memcpy(&hs->mem[(std::size_t)off + 4], &v, 4);
}

static bool SpartaHook(UObject* o, UFunction* fn, void* params) {
    const std::string& n = fn->name;
    if (n != "GetMaxHealth" && n != "GetCurrentHealthNormalized" && n != "GetHealth" &&
        n != "IsDeadOrDying") {
        return false;
    }
    UObject* hs = nullptr;
    if (auto it = o->props.find("HealthSet"); it != o->props.end()) {
        std::memcpy(&hs, &o->mem[(std::size_t)it->second], 8);
    }
    uint8_t* ret = static_cast<uint8_t*>(params) + FnOffset(fn, "ReturnValue");
    if (n == "IsDeadOrDying") { *reinterpret_cast<bool*>(ret) = o->dead; return true; }
    if (!hs) return false;
    const float mx = AttrAt(hs, 0x70, 1);
    if (n == "GetMaxHealth") { *reinterpret_cast<float*>(ret) = mx; return true; }
    if (n == "GetHealth") { *reinterpret_cast<float*>(ret) = AttrAt(hs, 0x60, 1); return true; }
    *reinterpret_cast<float*>(ret) = (mx > 0.0f) ? (AttrAt(hs, 0x60, 1) / mx) : 0.0f;
    return true;
}
}  // namespace ue

// ===========================================================================
// helpers
// ===========================================================================
static UObject* NewObj(const char* cls, const char* full, const char* tag) {
    UObject* o = new UObject();
    o->full = full;
    o->tag = tag ? tag : "";
    const std::string key = std::string("__cls_") + cls;
    auto it = ue::Objects().find(key);
    if (it == ue::Objects().end()) {
        UClass* c = new UClass();
        c->full = std::string("BlueprintGeneratedClass ") + cls;
        ue::Objects()[key] = c;
        it = ue::Objects().find(key);
    }
    o->cls = static_cast<UClass*>(it->second);
    ue::All().push_back(o);
    ue::Objects()[full] = o;
    return o;
}

static UObject* ActorObj(const char* tail, const char* cls, double x, double y, double z) {
    const std::string full = std::string(cls) + " /Game/Test/" + cls + "." + tail;
    UObject* o = NewObj(cls, full.c_str(), "Character");
    o->loc.X = x;
    o->loc.Y = y;
    o->loc.Z = z;
    o->rot.Yaw = 90.0;
    return o;
}

static void ExpectStr(const char* what, const std::string& got, const std::string& want) {
    if (got == want) {
        std::printf("PASS %s\n", what);
        std::fflush(stdout);
        return;
    }
    std::printf("FAIL %s\n  got : %s\n  want: %s\n", what, got.c_str(), want.c_str());
    std::fflush(stdout);
    g_failures++;
}

static void Expect(const char* what, bool cond) {
    if (cond) {
        std::printf("PASS %s\n", what);
        std::fflush(stdout);
        return;
    }
    std::printf("FAIL %s\n", what);
    std::fflush(stdout);
    g_failures++;
}

// ===========================================================================
int main() {
    using namespace coop;

    DefFn("K2_GetActorLocation", std::vector<std::pair<std::string, int> >{{"ReturnValue", 24}});
    DefFn("K2_GetActorRotation", std::vector<std::pair<std::string, int> >{{"ReturnValue", 24}});
    DefFn("GetVelocity", std::vector<std::pair<std::string, int> >{{"ReturnValue", 24}});
    DefFn("GetController", std::vector<std::pair<std::string, int> >{{"ReturnValue", 8}});
    DefFn("GetCurrentMontage", std::vector<std::pair<std::string, int> >{{"ReturnValue", 8}});
    DefFn("GetOwner", std::vector<std::pair<std::string, int> >{{"ReturnValue", 8}});
    DefFn("IsDeadOrDying", std::vector<std::pair<std::string, int> >{{"ReturnValue", 1}});
    DefFn("StartDeath", std::vector<std::pair<std::string, int> >());
    DefFn("RestartLogic", std::vector<std::pair<std::string, int> >());
    DefFn("StopLogic", std::vector<std::pair<std::string, int> >{{"Reason", 16}});
    DefFn("SetMovementMode", std::vector<std::pair<std::string, int> >{{"NewMovementMode", 1}, {"NewCustomMode", 1}});
    DefFn("K2_SetActorLocation", std::vector<std::pair<std::string, int> >{
        {"NewLocation", 24}, {"bSweep", 1}, {"SweepHitResult", 256}, {"bTeleport", 1}, {"ReturnValue", 1}});
    DefFn("K2_SetActorRotation", std::vector<std::pair<std::string, int> >{
        {"NewRotation", 24}, {"bTeleportPhysics", 1}, {"ReturnValue", 1}});
    DefFn("PlayAnimMontage", std::vector<std::pair<std::string, int> >{
        {"AnimMontage", 8}, {"InPlayRate", 4}, {"StartSectionName", 8}, {"ReturnValue", 4}});
    DefFn("StopAnimMontage", std::vector<std::pair<std::string, int> >{{"AnimMontage", 8}});
    DefFn("SetTargetOverride", std::vector<std::pair<std::string, int> >{
        {"NewTarget", 8}, {"OverrideTime", 4}, {"ReturnValue", 32}});
    DefFn("GetWorldDeltaSeconds", std::vector<std::pair<std::string, int> >{
        {"WorldContextObject", 8}, {"ReturnValue", 8}});
    DefFn("GetMaxHealth", std::vector<std::pair<std::string, int> >{{"ReturnValue", 4}});
    DefFn("GetCurrentHealthNormalized", std::vector<std::pair<std::string, int> >{{"ReturnValue", 4}});
    DefFn("GetHealth", std::vector<std::pair<std::string, int> >{{"ReturnValue", 4}});
    DefFn("BndEvt__BP_Spawner_Proximity_Sphere_K2Node_ComponentBoundEvent_1_ComponentBeginOverlapSignature__DelegateSignature",
          std::vector<std::pair<std::string, int> >{{"OverlappedComponent", 8}, {"OtherActor", 8},
                                                    {"OtherComp", 8}, {"OtherBodyIndex", 4},
                                                    {"bFromSweep", 1}, {"SweepResult", 256}});

    // --- the world --------------------------------------------------------
    UObject* pawn = NewObj("BP_PlayerCharacter_C",
                           "BP_PlayerCharacter_C /Game/P/BP_PlayerCharacter.BP_PlayerCharacter_C_0", "Character");
    ue::Objects()["__pawn"] = pawn;
    pawn->loc.X = 100.0;
    pawn->loc.Y = 200.0;
    pawn->loc.Z = 300.0;
    AddProp(pawn, "CapsuleComponent", 0x00);

    UObject* e1 = ActorObj("BP_Enemy_C_1", "BP_Enemy_C", 150.0, 200.0, 300.0);
    UObject* e2 = ActorObj("BP_NPC_Vendor_C_2", "BP_NPC_Vendor_C", 120.0, 200.0, 300.0);
    UObject* e3 = ActorObj("BP_Enemy_C_3", "BP_Enemy_C", 9000.0, 0.0, 0.0);

    UObject* brain = NewObj("BrainComponent", "BrainComponent /Game/Test/AIController.Brain", nullptr);
    UObject* ctrl = NewObj("AIController", "AIController /Game/Test/AIController_C_1", nullptr);
    AddProp(ctrl, "BrainComponent", 0x00);
    AddProp(ctrl, "TargetSelectionComponent", 0x08);
    SetPtr(ctrl, "BrainComponent", brain);
    e1->controller = ctrl;

    UObject* cm = NewObj("CharacterMovementComponent", "CharacterMovementComponent /Game/Test/CMC", nullptr);
    AddProp(cm, "MovementMode", 0x00);
    cm->mem[0] = 1;
    AddProp(e1, "CharacterMovement", 0x00);
    SetPtr(e1, "CharacterMovement", cm);

    UObject* hsEnemy = NewObj("SpartaHealthSet", "SpartaHealthSet /Game/Test/HS_Enemy", nullptr);
    AddProp(hsEnemy, "Health", 0x60);
    AddProp(hsEnemy, "MaxHealth", 0x70);
    ue::AttrWrite(hsEnemy, 0x60, 100.0f);
    ue::AttrWrite(hsEnemy, 0x70, 100.0f);

    UObject* hsPawn = NewObj("SpartaHealthSet", "SpartaHealthSet /Game/Test/HS_Pawn", nullptr);
    AddProp(hsPawn, "Health", 0x60);
    AddProp(hsPawn, "MaxHealth", 0x70);
    ue::AttrWrite(hsPawn, 0x60, 100.0f);
    ue::AttrWrite(hsPawn, 0x70, 100.0f);

    UObject* hc = NewObj("SpartaHealthComponent", "SpartaHealthComponent /Game/Test/HC_Enemy", nullptr);
    AddProp(hc, "HealthSet", 0x00);
    SetPtr(hc, "HealthSet", hsEnemy);
    hc->owner = e1;

    UObject* hcPawn = NewObj("SpartaHealthComponent", "SpartaHealthComponent /Game/Test/HC_Pawn", nullptr);
    AddProp(hcPawn, "HealthSet", 0x00);
    SetPtr(hcPawn, "HealthSet", hsPawn);
    hcPawn->owner = pawn;

    NewObj("GameplayStatics", "/Script/Engine.Default__GameplayStatics", nullptr);

    state().local.role = "host";
    state().local.tickChars = {pawn, e1, e2, e3};

    combat::RefreshChars();
    Expect("refreshChars built 4 tails", state().local.tickTails.size() == 4);

    // --- 1. npc / pd / nhp / dead emitters --------------------------------
    std::printf("\n== SerializeNpc/Pd/Nhp/Dead ==\n");
    std::fflush(stdout);
    combat::FillHealthCache();
    Expect("health cache has 2 owners", state().combat.hcCache.size() == 2);
    std::string out;
    combat::SerializeNpc(&out);
    combat::SerializePd(&out);
    combat::SerializeNhp(&out);
    combat::SerializeDead(&out);
    ExpectStr("wire sections",
              out,
              ",\"npc\":[[\"BP_NPC_Vendor_C_2\",120.0,200.0,300.0,1.0,0,0.0,0.0,0.0,90.0,\"\"],"
              "[\"BP_Enemy_C_1\",150.0,200.0,300.0,1.0,0,0.0,0.0,0.0,90.0,\"\"]]"
              ",\"pd\":0.000"
              ",\"nhp\":[[\"BP_NPC_Vendor_C_2\",1.0],[\"BP_Enemy_C_1\",1.0]]"
              ",\"dead\":[]");
    Expect("npcOwned == 2", state().combat.npcOwned == 2);
    Expect("out-of-radius enemy excluded", state().combat.npcLast.size() == 2);
    Expect("ownedTails filled", state().combat.ownedTails.Has("BP_Enemy_C_1") &&
                                    state().combat.ownedTails.Has("BP_NPC_Vendor_C_2"));

    // --- 2. sampleCombat lowers hpSeen -----------------------------------
    std::printf("\n== sampleCombat ==\n");
    std::fflush(stdout);
    ue::AttrWrite(hsEnemy, 0x60, 40.0f);
    combat::Tick2();
    Expect("hpSeen adopted 0.400", std::fabs(state().combat.hpSeen.Get("BP_Enemy_C_1", -1.0) - 0.4) < 1e-6);
    out.clear();
    combat::SerializeNpc(&out);
    Expect("npc carries hp 0.400", out.find("[\"BP_Enemy_C_1\",150.0,200.0,300.0,0.400,0") != std::string::npos);

    // --- 3. peer line: mirror take + health adoption ----------------------
    std::printf("\n== OnPeerLine npc / nhp ==\n");
    std::fflush(stdout);
    state().combat.ownedTails.Clear();
    combat::OnPeerLine(
        "{\"t\":\"p\",\"id\":\"B\",\"sq\":5,\"x\":1.0,"
        "\"npc\":[[\"BP_Enemy_C_1\",160.0,210.0,300.0,0.250,0,1.0,2.0,3.0,45.0,\"\"],"
        "[\"BP_NPC_Vendor_C_2\",120.0,200.0,300.0,1.0,0,0.0,0.0,0.0,0.0,\"\"]],"
        "\"nhp\":[[\"BP_Enemy_C_1\",0.250]],\"dead\":[],\"pd\":0.000}");

    Expect("peerClaims replaced", state().combat.peerClaims.size() == 2);
    Expect("enemy moved to the peer position",
           std::fabs(e1->loc.X - 160.0) < 1e-9 && std::fabs(e1->loc.Y - 210.0) < 1e-9);
    Expect("mirror taken", state().combat.mirrors.size() == 1);
    Expect("saved mode 1", !state().combat.mirrors.empty() && state().combat.mirrors[0].savedMode == 1);
    Expect("AI stopped", brain->aiStopped && brain->lastReason == "coop mirror");
    Expect("movement set to 0", cm->lastMode == 0);
    Expect("npcMirrored counted (enemy + friendly NPC row)", state().combat.npcMirrored == 2);
    Expect("friendly NPC skipped", state().combat.mirrorSkip.Has("BP_NPC_Vendor_C_2"));
    Expect("nhp adopted 0.250", std::fabs(state().combat.hpSeen.Get("BP_Enemy_C_1", -1.0) - 0.25) < 1e-6);
    Expect("health attribute lowered", std::fabs(ue::AttrAt(hsEnemy, 0x60, 1) - 25.0f) < 0.01f);
    Expect("peer claim holds the position", std::fabs(state().combat.peerClaims[0].pos.X - 160.0) < 1e-9);

    // --- 4. dead section --------------------------------------------------
    std::printf("\n== OnPeerLine dead ==\n");
    std::fflush(stdout);
    combat::OnPeerLine("{\"t\":\"p\",\"id\":\"B\",\"sq\":6,\"x\":1.0,\"npc\":[],\"nhp\":[],"
                       "\"dead\":[\"BP_Enemy_C_1\"],\"pd\":0.000}");
    Expect("deadTails recorded", state().combat.deadTails.Has("BP_Enemy_C_1"));
    Expect("StartDeath called once on the health component", hc->startDeathCalls == 1);
    out.clear();
    combat::SerializeDead(&out);
    ExpectStr("dead section", out, ",\"dead\":[\"BP_Enemy_C_1\"]");
    Expect("IsDeadTail", combat::IsDeadTail("BP_Enemy_C_1"));

    // --- 5. mirror release after the timeout -----------------------------
    std::printf("\n== mirror release ==\n");
    std::fflush(stdout);
    Expect("still mirrored", state().combat.mirrors.size() == 1);
    g_now += 3.0;
    combat::Tick2();
    Expect("released after 3 s", state().combat.mirrors.empty());
    Expect("release counted", state().combat.mirReleased == 1);
    Expect("mode restored", cm->lastMode == 1);
    Expect("no AI restart for a dead enemy", brain->restartCalls == 0);

    // --- 6. the fresh-actor guard ----------------------------------------
    std::printf("\n== fresh actor guard ==\n");
    std::fflush(stdout);
    ue::AttrWrite(hsEnemy, 0x70, 0.0f);
    Expect("HealthNorm < 0 while MaxHealth is 0", combat::HealthNorm(e1, nullptr) < 0.0);
    ue::AttrWrite(hsEnemy, 0x70, 100.0f);
    hc->dead = false;   // the fake's StartDeath above marked it; the real one sets DeathState
    ue::AttrWrite(hsEnemy, 0x60, 0.0f);
    Expect("HealthNorm < 0 for 0 hp that is not a death", combat::HealthNorm(e1, nullptr) < 0.0);

    // --- 7. setHealthNorm direct attribute write -------------------------
    std::printf("\n== setHealthNorm ==\n");
    std::fflush(stdout);
    ue::AttrWrite(hsEnemy, 0x60, 90.0f);
    Expect("SetHealthNorm ok", combat::SetHealthNorm(e1, 0.5));
    Expect("current value written", std::fabs(ue::AttrAt(hsEnemy, 0x60, 1) - 50.0f) < 0.01f);
    Expect("base value written", std::fabs(ue::AttrAt(hsEnemy, 0x60, 0) - 50.0f) < 0.01f);
    Expect("MaxHealth untouched", std::fabs(ue::AttrAt(hsEnemy, 0x70, 1) - 100.0f) < 0.01f);
    Expect("no layout warning", g_warns == 0);

    // --- 8. peer damage (pd) ---------------------------------------------
    std::printf("\n== applyPeerDamage ==\n");
    std::fflush(stdout);
    ue::AttrWrite(hsPawn, 0x60, 100.0f);
    state().combat.hasPdGot = false;   // a fresh session: the first packet is the baseline
    state().combat.pdGot = 0.0;
    combat::OnPeerLine("{\"t\":\"p\",\"id\":\"B\",\"sq\":7,\"x\":1.0,\"pd\":0.200}");
    Expect("baseline recorded", state().combat.hasPdGot && std::fabs(state().combat.pdGot - 0.2) < 1e-9);
    Expect("baseline applies no damage", std::fabs(ue::AttrAt(hsPawn, 0x60, 1) - 100.0f) < 0.01f);
    combat::OnPeerLine("{\"t\":\"p\",\"id\":\"B\",\"sq\":8,\"x\":1.0,\"pd\":0.450}");
    Expect("damage applied (100 -> 75)", std::fabs(ue::AttrAt(hsPawn, 0x60, 1) - 75.0f) < 0.01f);
    Expect("pdApplied counted", state().combat.pdApplied == 1);
    Expect("local hold not taken", state().localHoldUntil == 0);
    if (g_files.count("result_pd.txt")) {
        ExpectStr("result_pd.txt", g_files["result_pd.txt"],
                  "PASS peer-side hit applied to own body: 1.000 -> 0.750\n");
    } else {
        Expect("result_pd.txt written", false);
    }

    // --- 9. spawner alias + spawn replay ---------------------------------
    std::printf("\n== spawnerScan / spawnReplay ==\n");
    std::fflush(stdout);
    UObject* sp = NewObj("UnitSpawner", "BP_Spawner_Proximity_C /Game/Test/SP.BP_Spawner_Proximity_C_5", nullptr);
    AddProp(sp, "SpawnedEnemy", 0x00);
    AddProp(sp, "PlayerTrigger", 0x08);
    UObject* trigger = NewObj("SphereComponent", "SphereComponent /Game/Test/SP.Trigger", nullptr);
    SetPtr(sp, "PlayerTrigger", trigger);
    UObject* spawned = ActorObj("BP_Spawned_C_77", "BP_Spawned_C", 500.0, 200.0, 300.0);
    SetPtr(sp, "SpawnedEnemy", spawned);

    g_now += 12.0;   // the spawner list itself is only re-scanned every 10 s
    combat::Tick2();
    Expect("alias registered", state().combat.spawnAlias.Find("BP_Spawned_C_77") != nullptr);
    combat::RefreshChars();
    out.clear();
    combat::SerializeNpc(&out);
    Expect("aliased tail on the wire", out.find("\"SP_BP_Spawner_Proximity_C_5\"") != std::string::npos);

    UObject* sp2 = NewObj("UnitSpawner", "BP_Spawner_Proximity_C /Game/Test/SP.BP_Spawner_Proximity_C_9", nullptr);
    AddProp(sp2, "SpawnedEnemy", 0x00);
    AddProp(sp2, "PlayerTrigger", 0x08);
    SetPtr(sp2, "PlayerTrigger", trigger);

    g_now += 12.0;
    combat::Tick2();
    combat::OnPeerLine("{\"t\":\"p\",\"id\":\"B\",\"sq\":9,\"x\":1.0,"
                       "\"npc\":[[\"SP_BP_Spawner_Proximity_C_9\",700.0,200.0,300.0,1.0,0,0.0,0.0,0.0,0.0,\"\"]]}");
    g_now += 0.2;
    combat::Tick2();
    Expect("spawn replay asked", state().combat.spawnAsked >= 1);
    Expect("spawner trigger fired", sp2->triggerCalls >= 1);

    // --- 10. status -------------------------------------------------------
    std::printf("\n== Status ==\n");
    std::fflush(stdout);
    const std::string st = combat::Status();
    Expect("stream line", st.rfind("stream=src:none n0:nil n1:nil npcown:", 0) == 0);
    Expect("enemy line", st.find("\nenemy=driven:") != std::string::npos);
    Expect("perf line", st.find("\nperf=near:") != std::string::npos);

    // --- 11. reset drops pointers, keeps tail bookkeeping ----------------
    std::printf("\n== Reset ==\n");
    std::fflush(stdout);
    combat::Reset();
    Expect("hcCache dropped", state().combat.hcCache.empty());
    Expect("mirrors dropped", state().combat.mirrors.empty());
    Expect("spawners dropped", state().combat.spawners.empty());
    Expect("deadTails kept", state().combat.deadTails.Has("BP_Enemy_C_1"));
    Expect("aliases kept", state().combat.spawnAlias.Find("BP_Spawned_C_77") != nullptr);

    std::printf("\n%s (%d failure(s), %d WARN/ERR line(s))\n",
                g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures, g_warns);
    return g_failures ? 1 : 0;
}


#endif  // COOP_TEST_FAKE_ENGINE
