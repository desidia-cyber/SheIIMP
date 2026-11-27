// coop_load.cpp -- [R8-APPEAR] load a /Game/ asset the peer is wearing that this
// client never loaded (round-7 gap: "a peer mask whose mesh asset was never
// loaded on this PC stays missing").
//
// WHY A NEW FILE.  Round 13's Lua had `requestGameLoad` (docs/HANDOFF.md 20) whose
// whole body was `ExecuteInGameThread(function() FindObject(path) end)`: it never
// loaded anything, it only re-asked the object array, so a mesh that was not in
// memory stayed missing forever (sync.txt `lookmissing` never reached 0 for a
// shell/mask the local save never equipped).  HANDOFF-R5 recorded the suspicion
// that this path crashed the guest; that was ruled out there, and the honest
// statement in the port was CONTRACT-REQUEST 4/5 in coop_net.cpp ("the native
// build has no asset loader").
//
// THE LOADER.  The engine's own synchronous loader is reachable through
// reflection, exactly as the game's own blueprints do it (net::Log evidence for
// the same trio is in research/raw/CXXHeaderDump/BPFL_Player_Equipment.hpp: every
// ChangeShell*/ChangeWeapon* node body starts with
// CallFunc_LoadClassAsset_Blocking_ReturnValue).  /Script/Engine.KismetSystemLibrary
// is a BlueprintFunctionLibrary, so its functions are callable on the class
// default object like BPFL_Player_Equipment (proven in this project, docs/
// HANDOFF-R5.md 5):
//
//   FString                 -> FSoftObjectPath        MakeSoftObjectPath(PathString)
//   FSoftObjectPath         -> TSoftObjectPtr<UObject> Conv_SoftObjPathToSoftObjRef()
//   TSoftObjectPtr<UObject> -> UObject*                LoadAsset_Blocking(Asset)
//
// The two struct hops exist so this file never has to guess a struct layout: each
// value is read out of one parameter block and written into the next one verbatim,
// with the width the engine itself reports for that parameter (PropSizeStruct).
// Conv_SoftObjectReferenceToObject is the fallback for the last hop (it returns the
// object without loading, i.e. only useful when something else loaded it first).
//
// WHEN IT RUNS.  Only from net::Find(path, true) -- the one place the look/body/
// weapon code asks "give me this asset" -- and only for a path that is not in
// memory yet, at most once per second and three tries per path, on the game thread
// (net::Find is called from the tick and from the 2 Hz steps, both game thread).
// Loading is synchronous by construction: LoadAsset_Blocking is what the game
// itself calls during a shell change, and the look apply already tolerates a
// hitch (it is behind net::Step).
//
// KILL SWITCH: APPEAR_OFF.txt (Toggles::assetLoad) -- with it present net::Find
// behaves exactly like round 7: log "asset not loaded yet" and retry forever.

#include "coop_state.h"

#include "ms2_ue.h"

#include <string>
#include <vector>

namespace coop {
namespace load {

namespace {

// How many different paths one session may try to load.  A peer swapping through
// a menu of skins is the only way to reach this; past it the loader goes inert and
// says so, so a pathological wire cannot turn into a load storm.
constexpr int kMaxPaths = 160;
// One load per this many wall seconds: a shell mesh is a ~10-40 ms synchronous
// read, and three of them in one look apply is already a visible hitch.
constexpr int64_t kMinSpacing = 1;
// Tries per path before it is declared unavailable (a wrong path never loads).
constexpr int kMaxTries = 3;

struct Entry {
    std::string path;
    int tries = 0;
};

std::vector<Entry> g_entries;      // module state; small and linear on purpose
int g_done = 0;                    // successful loads (published as gameloads)
int g_failed = 0;                  // failed attempts
int64_t g_nextAt = 0;              // wall seconds of the next allowed load
bool g_noLibrary = false;          // KismetSystemLibrary unreachable: log once
bool g_noFunction = false;         // the trio is gone: log once

Entry* Slot(const std::string& path) {
    for (Entry& e : g_entries) {
        if (e.path == path) return &e;
    }
    if ((int)g_entries.size() >= kMaxPaths) return nullptr;
    Entry e;
    e.path = path;
    g_entries.push_back(e);
    return &g_entries.back();
}

// The parameter's offset and its byte width, straight from the UFunction.  False
// when the function does not have that parameter (a wrong name is a silent no-op
// in the engine, so this file never guesses: it logs and gives up).
bool Param(ue::UFunction* fn, const char* name, int32_t* size) {
    const int32_t off = ue::PropOffsetStruct((ue::UStruct*)fn, name);
    const int32_t n = ue::PropSizeStruct((ue::UStruct*)fn, name);
    if (off < 0 || n <= 0) return false;
    *size = n;
    return true;
}

// Copies one parameter out of `from` and into the same-named parameter of `to`.
// The widths must agree: a mismatch means the two functions disagree about the
// value (different engine version / different parameter) and is refused.
bool MoveParam(ue::Params& from, const char* fromName, ue::Params& to, const char* toName,
               std::string* err) {
    int32_t a = 0, b = 0;
    if (!Param(from.function(), fromName, &a) || !Param(to.function(), toName, &b)) {
        *err = std::string("no parameter ") + fromName + "/" + toName;
        return false;
    }
    if (a != b) {
        *err = std::string("width mismatch on ") + fromName;
        return false;
    }
    const int32_t offA = ue::PropOffsetStruct((ue::UStruct*)from.function(), fromName);
    const int32_t offB = ue::PropOffsetStruct((ue::UStruct*)to.function(), toName);
    if (offA < 0 || offB < 0 || offA + a > from.size() || offB + a > to.size()) {
        *err = "parameter outside the block";
        return false;
    }
    return ue::ReadMem((uint8_t*)from.data() + offA, (uint8_t*)to.data() + offB, (size_t)a);
}

ue::UObject* Library() {
    static ue::UObject* lib = nullptr;
    if (lib && ue::IsValid(lib)) return lib;
    lib = ue::FindObject("/Script/Engine.Default__KismetSystemLibrary");
    if (!lib || !ue::PlausiblePtr(lib)) {
        lib = nullptr;
        if (!g_noLibrary) {
            g_noLibrary = true;
            net::Log("load: no /Script/Engine.Default__KismetSystemLibrary -- peer assets that"
                     " are not in memory cannot be loaded");
        }
        return nullptr;
    }
    return lib;
}

}  // namespace

int Done() { return g_done; }
int Failed() { return g_failed; }

// The whole loader.  Returns the object when it is in memory after this call.
ue::UObject* Asset(const std::string& path) {
    if (path.size() < 4) return nullptr;
    // Already there?  Then this is not a load, it is a lookup (the caller does its
    // own FindObject first, so this only catches the case where something else
    // loaded it in between).
    if (ue::UObject* o = ue::FindObject(path.c_str())) {
        if (ue::IsValid(o)) return o;
    }
    if (!toggles().assetLoad) return nullptr;   // APPEAR_OFF.txt

    Entry* e = Slot(path);
    if (!e) return nullptr;                     // capped: inert for the session
    if (e->tries >= kMaxTries) return nullptr;
    const int64_t now = net::Wall();
    if (now < g_nextAt) return nullptr;
    g_nextAt = now + kMinSpacing;
    ++e->tries;

    ue::UObject* lib = Library();
    if (!lib) return nullptr;
    ue::UFunction* fPath = ue::FindFunction(lib, "MakeSoftObjectPath");
    ue::UFunction* fPtr = ue::FindFunction(lib, "Conv_SoftObjPathToSoftObjRef");
    ue::UFunction* fLoad = ue::FindFunction(lib, "LoadAsset_Blocking");
    if (!fPath || !fPtr || !fLoad) {
        if (!g_noFunction) {
            g_noFunction = true;
            net::Log("load: KismetSystemLibrary is missing MakeSoftObjectPath=%d"
                     " Conv_SoftObjPathToSoftObjRef=%d LoadAsset_Blocking=%d -- loader off",
                     fPath ? 1 : 0, fPtr ? 1 : 0, fLoad ? 1 : 0);
        }
        return nullptr;
    }

    // 1. FString -> FSoftObjectPath.  The engine only reads the FString, so the
    //    buffer may be this frame's stack: it is copied into the FSoftObjectPath
    //    (its SubPathString is an FString the engine allocates).
    std::wstring wide;
    wide.reserve(path.size());
    for (char c : path) wide.push_back((wchar_t)(unsigned char)c);
    ue::FString fs{};
    fs.Data = const_cast<wchar_t*>(wide.c_str());
    fs.Num = (int32_t)wide.size() + 1;         // Num includes the terminator
    fs.Max = fs.Num;
    ue::Params p1(fPath);
    p1.Set<ue::FString>("PathString", fs);
    if (!ue::Call(lib, fPath, p1.data())) {
        ++g_failed;
        net::Log("load: MakeSoftObjectPath faulted for %s", path.c_str());
        return nullptr;
    }

    // 2. FSoftObjectPath -> TSoftObjectPtr<UObject> (verbatim struct copy)
    ue::Params p2(fPtr);
    std::string err;
    if (!MoveParam(p1, "ReturnValue", p2, "SoftObjectPath", &err)) {
        ++g_failed;
        net::Log("load: %s (Conv_SoftObjPathToSoftObjRef), so %s", err.c_str(), path.c_str());
        return nullptr;
    }
    if (!ue::Call(lib, fPtr, p2.data())) {
        ++g_failed;
        net::Log("load: Conv_SoftObjPathToSoftObjRef faulted for %s", path.c_str());
        return nullptr;
    }

    // 3. TSoftObjectPtr<UObject> -> UObject*  (this is the blocking load)
    ue::Params p3(fLoad);
    if (!MoveParam(p2, "ReturnValue", p3, "Asset", &err)) {
        ++g_failed;
        net::Log("load: %s (LoadAsset_Blocking), so %s", err.c_str(), path.c_str());
        return nullptr;
    }
    const bool called = ue::Call(lib, fLoad, p3.data());
    ue::UObject* got = p3.Get<ue::UObject*>("ReturnValue");
    if (!got || !ue::IsValid(got)) {
        // The blueprint node pair also exists as "Get the object a soft reference
        // points at"; it cannot load, but it reports what the load did.
        if (ue::UFunction* fObj = ue::FindFunction(lib, "Conv_SoftObjectReferenceToObject")) {
            ue::Params p4(fObj);
            if (MoveParam(p2, "ReturnValue", p4, "SoftObject", &err) &&
                ue::Call(lib, fObj, p4.data())) {
                got = p4.Get<ue::UObject*>("ReturnValue");
            }
        }
    }
    if (!got || !ue::IsValid(got)) {
        ++g_failed;
        net::Log("load: %s -> still not loaded (call=%s, try %d/%d)", path.c_str(),
                 called ? "ok" : "faulted", e->tries, kMaxTries);
        return nullptr;
    }
    ++g_done;
    net::Log("load: %s -> %s (%d loaded, %d failed)", path.c_str(),
             ue::GetClassName(got).c_str(), g_done, g_failed);
    return got;
}

// World change: the loaded objects (and the library CDO) belong to the old world.
// The per-path attempt counters go too, so a transient failure is retried after a
// reload rather than being remembered forever.
void Reset() {
    g_entries.clear();
    g_nextAt = 0;
}

}  // namespace load
}  // namespace coop
