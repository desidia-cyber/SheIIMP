// ms2_ue.h -- UE 5.6 reflection core for Mortal Shell II, no UE4SS.
//
// This is the native replacement for the small part of UE4SS the co-op mod
// actually used: GObjects, FName lookups, property offsets, ProcessEvent, and
// the pre/post hook + game-thread-queue + per-frame-loop trio that the Lua mod
// called RegisterHook / ExecuteInGameThread / LoopAsync.
//
// EVERYTHING here is guarded: a wrong offset returns false and logs, it does not
// fault.  That is a hard rule, because this DLL is loaded into a shipping game
// with no debugger, and a bad read during a ProcessEvent detour would take the
// process down mid-frame with no evidence left behind.
//
// LAYOUT SOURCES (all verified against the shipped build, see README.md):
//   UObject/FName/TArray  -- Dumper-7 SDK generated from THIS exe
//                            (research/raw/Dumper7/SDK/CoreUObject_classes.hpp)
//   FField/FProperty      -- same SDK, and UE4SS 3.0.1's own probe log
//   globals               -- AOB scan, with the build's RVAs as a checked fallback
//
// UE5 notes that bite: FVector/FRotator/FQuat are DOUBLE precision in UE5, and
// UProperty does not exist -- properties are FField/FProperty with
// UStruct::ChildProperties (0x50) as the sibling chain.
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace ue {

using int32  = std::int32_t;
using uint32 = std::uint32_t;
using int64  = std::int64_t;
using uint64 = std::uint64_t;

struct UObject;
struct UField;
struct UStruct;
struct UClass;
struct UFunction;
struct UScriptStruct;
struct FField;
struct FProperty;

// ---------------------------------------------------------------------------
// Minimal UE5.6 value types.  Layouts, not behaviour: nothing here allocates or
// frees game memory.
// ---------------------------------------------------------------------------

// FName is two uint32: the name-pool id and the instance number ("Actor_3").
// Comparing FNames means comparing BOTH halves.
struct FName {
    uint32 ComparisonIndex = 0;
    uint32 Number = 0;

    bool IsNone() const { return ComparisonIndex == 0 && Number == 0; }
    bool operator==(const FName& o) const {
        return ComparisonIndex == o.ComparisonIndex && Number == o.Number;
    }
    bool operator!=(const FName& o) const { return !(*this == o); }
};

// FString is a TArray<wchar_t>: ptr, count, capacity.  We never free one that the
// engine filled in (see NameToString in ms2_ue.cpp, which reuses one buffer).
struct FString {
    wchar_t* Data = nullptr;
    int32    Num = 0;
    int32    Max = 0;
};

template <class T>
struct TArray {
    T*    Data = nullptr;
    int32 Num = 0;
    int32 Max = 0;
};

// UE5 moved the core math types to double.  Getting this wrong silently reads
// half of X as Y, so it is spelled out here rather than typedef'd.
struct FVector  { double X = 0, Y = 0, Z = 0; };
struct FRotator { double Pitch = 0, Yaw = 0, Roll = 0; };
struct FQuat    { double X = 0, Y = 0, Z = 0, W = 0; };
// FTransform is Rotation, Translation, Scale3D in that order, each 16-byte aligned:
// Rotation @0x00, Translation @0x20, Scale3D @0x40, sizeof 0x60 (Dumper-7: every
// FTransform member is 0x60 wide).  Without the two pads the engine read our
// Scale3D.Y/Z as X/Y and a zero as Z -- every component we created with a
// transform was flattened to scale (1,1,0) and invisible (round 5: the puppet's
// weapons).
struct FTransform {
    FQuat   Rotation;
    FVector Translation;
    double  _pad0 = 0;
    FVector Scale3D;
    double  _pad1 = 0;
};
static_assert(sizeof(FTransform) == 0x60, "UE5 FTransform is 0x60 bytes");
static_assert(offsetof(FTransform, Scale3D) == 0x40, "UE5 FTransform.Scale3D is at 0x40");
struct FLinearColor { float R = 0, G = 0, B = 0, A = 0; };
struct FVector2D { double X = 0, Y = 0; };

// ---------------------------------------------------------------------------
// Init -- resolve the globals (AOB scan, then this build's checked RVAs) and
// prove the object array is readable.  Safe to call repeatedly; the second call
// is a no-op returning the first result.  Must be called from the worker thread,
// never from DllMain.
// ---------------------------------------------------------------------------
bool Init();
bool IsReady();
const char* InitReport();          // one-line summary of what was resolved, for the log

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------
std::string GetName(UObject* o);
std::string GetFullName(UObject* o);       // "Class Package.Outer.Name"
std::string GetClassName(UObject* o);

// FName from a string, for the times a call needs an FName rather than a name.
//
// This goes through the game's own UKismetStringLibrary::Conv_StringToName via
// ProcessEvent, so the engine builds the name correctly (including adding it to
// the pool if it is new) and this module never has to implement the pool layout.
// The alternative -- calling the FName constructor at its own address -- was
// rejected: its prologue takes three registers and the calling convention could
// not be established from the binary alone, and an 8-byte write to the wrong
// pointer is not a mistake SEH can undo.
//
// Requires the game to be up (Init() done, ProcessEvent resolved).  Returns an
// empty FName and logs once if the library cannot be reached.
FName MakeFName(const char* ascii);
// Alias: the co-op lanes call this one.  Both spellings resolve to the same code.
FName MakeName(const char* ascii);

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------
UObject* FindObject(const char* fullPathOrName);      // full path or bare name
UClass*  FindClass(const char* name);                 // "BP_PlayerCharacter_C"
UObject* FindFirstOf(const char* className);          // live, non-CDO, IsA
std::vector<UObject*> FindAllOf(const char* className);

bool     IsA(UObject* o, UClass* c);
bool     IsValid(UObject* o);       // in the array, not garbage/pending-kill, not CDO
UObject* GetOuter(UObject* o);
UClass*  GetClass(UObject* o);

// ---------------------------------------------------------------------------
// Guarded memory access (SEH inside).  Exposed because callers walking game
// structures need it as much as this header does.
//
// DECLARED HERE, ABOVE ue::Get/ue::Set, on purpose: a non-dependent name inside a
// template is looked up at the point of DEFINITION, so the Get/Set templates below
// cannot call a function declared later in the file.  Under /permissive- that is
// error C3861, not a warning.
// ---------------------------------------------------------------------------
bool ReadMem(const void* src, void* dst, size_t n);
bool WriteMem(void* dst, const void* src, size_t n);
bool PlausiblePtr(const void* p);

// ---------------------------------------------------------------------------
// Properties
// ---------------------------------------------------------------------------
FProperty* FindProperty(UStruct* st, const char* name);   // walks the super chain
FProperty* FindProperty(UClass* c, const char* name);
int32_t    PropOffset(UObject* o, const char* name);      // cached by (class,name); -1 if absent
int32_t    PropOffsetStruct(UStruct* st, const char* name);
int32_t    PropSize(UObject* o, const char* name);        // ElementSize, -1 if absent
// The same question about a struct/function's own property list (a UFunction's
// parameters and ReturnValue hang off the function, not off its class).  It is how
// a caller decides a parameter's WIDTH: 8 = DoubleProperty, 4 = FloatProperty.
// UE pads the parameter block, so ParmsSize cannot answer that question.
int32_t    PropSizeStruct(UStruct* st, const char* name);

template <class T>
bool Get(UObject* o, const char* prop, T& out) {
    // IsValid first: a cached pointer from an earlier frame may name a destroyed
    // object whose memory is still mapped, so the guarded read below would happily
    // return whatever now lives there.  (It also rejects a stale pointer that a
    // garbage collection has since re-used.)
    if (!IsValid(o)) return false;
    const int32_t off = PropOffset(o, prop);
    if (off < 0) return false;
    return ReadMem(reinterpret_cast<const uint8_t*>(o) + off, &out, sizeof(T));
}

template <class T>
bool Set(UObject* o, const char* prop, const T& v) {
    if (!IsValid(o)) return false;
    const int32_t off = PropOffset(o, prop);
    if (off < 0) return false;
    return WriteMem(reinterpret_cast<uint8_t*>(o) + off, &v, sizeof(T));
}

// Booleans need their own reader.  A UE bool property occupies ONE BIT of a byte
// (or of a 4-byte word, hence FBoolProperty::ByteOffset), so memcpy-ing a bool
// over it reads whatever else shares the byte.  This applies the property's own
// mask, which is what the engine does.
bool GetBool(UObject* o, const char* prop);
bool SetBool(UObject* o, const char* prop, bool v);

// The class default object of a class (its template instance: property offsets are
// the same as an instance's, so it is the cheapest thing to probe).
UObject* GetCDO(UClass* c);

// The current UWorld, from the GWorld global.  This global is NOT AOB-scanned (no
// signature was recovered for it); it comes from this build's RVA and is validated
// before being returned -- a wrong global yields nullptr, never a bad pointer.
UObject* GetWorld();

// ---------------------------------------------------------------------------
// Strings: FName -> "the bytes FName::ToString() gives", FString/FText -> utf8.
// ---------------------------------------------------------------------------
// FName::AppendString through the resolved engine function, with the "_<Number>"
// suffix FName::ToString adds.  Returns "None" for the empty name.
std::string ToString(FName name);
// FString (UTF-16) -> utf8.  Never frees, never validates the pointer's provenance
// beyond a guarded read.
std::string ToUtf8(const FString& s);
// FText -> std::string through UKismetTextLibrary::Conv_TextToString (the only
// sound way: FText's payload layout is engine-private).  `ftext` is the address of
// the FText itself.  Returns "" when unavailable.
std::string TextToString(void* ftext);

// ---------------------------------------------------------------------------
// TMap / TSet iteration (read-only) and the two "Add" shapes the co-op code uses.
//
// UE5.6's TMap<K,V> is a TSet<TPair<K,V>>: a TSparseArray of
// TSparseArrayElementOrFreeListLink<TPair> (element stride = align(sizeof(pair)) +
// 8: HashNextId and HashIndex), plus an allocation-flag bit array that marks the
// live slots.  Key/value offsets and sizes come from the FMapProperty's
// KeyProp/ValueProp (and its FScriptMapLayout when it reads sanely), so the same
// code walks FName-keyed, FString-keyed and struct-valued maps.
//
// `key`/`value` point at the stored key and value.  Return false from fn to stop.
// False from MapForEach means "no such property / not a map / the map is empty".
// ---------------------------------------------------------------------------
bool MapForEach(UObject* o, const char* prop, std::function<bool(void* key, void* value)> fn);

// By address, when the map is not a property of any object (e.g. FSpartaMapInt::Map
// at offset 0 inside the value box of TMap<FName, FSpartaMapInt>).  This overload
// assumes the shape every FName-keyed map in this game has: an 8-byte FName key and
// the value right after it.  Pass `keySize`/`valueSize` explicitly when it does not.
bool MapForEachRaw(const void* mapAddr, std::function<bool(void* key, void* value)> fn);
bool MapForEachRaw(const void* mapAddr, int32 keySize, int32 valueSize,
                   std::function<bool(void* key, void* value)> fn);

// [R7-MAPS] The map's own header counts, WITHOUT walking it: the sparse array's
// allocated-slot count and capacity.  Added because "the walk returned no rows"
// cannot tell an EMPTY map from an UNREADABLE one -- ReadMapInfo() requires a
// non-zero allocation-bit count, so an empty TMap always looks like a failure to
// MapForEachRaw.  `num == 0` here is the definitive "the game stores nothing in
// this map" answer (gap G2's ActorFloats question).  `num` counts allocated slots,
// so it is an upper bound on the pairs; a map that reads num > 0 and still walks
// to zero rows is a walker problem, not an empty map.
bool MapCountRaw(const void* mapAddr, int32* num, int32* max);

// TMap<FName,int32>::Add / TMap<FString,int32>::Add.
//
// HONEST LIMITATION: growing a TMap needs the game's allocator plus a rehash, which
// this layer cannot do safely (there is no reachable, verified Malloc/Free pair).
// What these do instead is FIND the key and overwrite its value -- which is the
// observable part of the Lua's Add for every caller here (the maps are pre-seeded by
// the save/quest system) -- and log once per property and return false when the key
// is genuinely absent, so the caller's failure path runs instead of a silent no-op.
bool MapAddNameInt(UObject* o, const char* prop, const char* key, int32 value);
bool MapAddStrInt(UObject* o, const char* prop, const char* key, int32 value);

// Guarded memory access (SEH inside).  Exposed because callers walking game
// structures need it as much as this header does.
// NOTE: declared near the top of this header (above ue::Get/ue::Set); see there.

// AOB scan over the game exe's .text section (e.g. "48 89 5C ?? ??").
// Returns the absolute address of the first match, or 0 if not found.
uintptr_t ScanTextAob(const char* aob);

// ---------------------------------------------------------------------------
// Functions and ProcessEvent
// ---------------------------------------------------------------------------
UFunction* FindFunction(UObject* o, const char* name);

// Calls obj->fn(params).  `params` must be laid out per the UFunction's
// properties (use Params below, or pass nullptr for no-argument functions).
bool Call(UObject* o, UFunction* fn, void* params);
bool Call(UObject* o, const char* fnName, void* params);

// A parameter block sized from UFunction::ParmsSize, zero-initialised, with
// property-name access.  This is the native equivalent of the Lua mod's
// {param = value} tables passed to obj:CallFunction().
struct Params {
    explicit Params(UFunction* fn);
    ~Params();
    Params(const Params&) = delete;
    Params& operator=(const Params&) = delete;

    template <class T>
    void Set(const char* name, const T& v) {
        const int32_t off = offset_of(name);
        if (off < 0 || off + (int32_t)sizeof(T) > size_) return;
        WriteMem(buf_ + off, &v, sizeof(T));
    }

    template <class T>
    T Get(const char* name) const {
        T out{};
        const int32_t off = offset_of(name);
        if (off < 0 || off + (int32_t)sizeof(T) > size_) return out;
        ReadMem(buf_ + off, &out, sizeof(T));
        return out;
    }

    void*     data() { return buf_; }
    int32_t   size() const { return size_; }
    UFunction* function() const { return fn_; }

private:
    int32_t offset_of(const char* name) const;
    UFunction* fn_ = nullptr;
    uint8_t*   buf_ = nullptr;
    int32_t    size_ = 0;
};

// ---------------------------------------------------------------------------
// Hooks -- the equivalent of UE4SS RegisterHook, implemented as ONE detour on
// ProcessEvent plus a name table.
//
// The name is the UFunction's full path ("/Script/Engine.Actor.ReceiveBeginPlay")
// or "Class.Function"; resolution happens at registration, and a name that does
// not resolve is logged and ignored rather than silently never firing.
// ---------------------------------------------------------------------------
using HookFn = void (*)(UObject* self, UFunction* fn, void* params);

// [R5-DEATH] begin
// A hook that can VETO the original call: returning true from a pre-hook skips
// the UFunction's own body (the ProcessEvent dispatch returns to its caller).
//
// WHY THIS EXISTS.  The co-op death rules need to block the game's death
// reset/reload, and that reset is authored in blueprint: it is reached through
// ProcessEvent, so suppressing it at the dispatch is the one lever that does not
// touch engine code.  A skip hook that faults, or whose target cannot be
// resolved, NEVER blocks the call -- a skip is a behaviour change and must be
// deliberate.
//
// NOTE: this cannot block a UFunction that NATIVE C++ code calls directly (a
// direct call does not go through ProcessEvent).  `coop_death.cpp` documents
// which of the two each hook is.
using HookSkipFn = bool (*)(UObject* self, UFunction* fn, void* params);

bool HookPre(const char* functionFullName, HookFn);
bool HookPreSkip(const char* functionFullName, HookSkipFn);

// The same veto, for a UFunction the CALLER has already resolved -- typically one it
// found on a live object (ue::FindFunction on an instance).  This is the form that
// works for a blueprint class whose NAME does not resolve by itself: find the object,
// ask it for the function, register that.  Returns false for a null function.
bool HookPreSkipFn(UFunction* fn, HookSkipFn f);

// THE EVENT PROBE -- a research instrument, not a feature.  Logs every
// ProcessEvent whose function name contains one of the `|`- or `,`-separated
// substrings (case-insensitive), for `seconds`, into the native log.  It is a
// no-op when off and is meant for lab runs only: it is how the death sequence
// (R5-DEATH-FINDINGS.md) was read.
bool ProbeOn(float seconds, const char* patterns);
void ProbeOff();
bool ProbeActive();
// [R5-DEATH] end
bool HookPost(const char* functionFullName, HookFn);

// Runs `f` on the game thread (drained inside the ProcessEvent detour and the
// engine-tick detour).  Safe to call from any thread.
void EnqueueGameThread(std::function<void()> f);

// Per-frame callback on the game thread.  dt is the engine's delta seconds when
// the tick detour is live, or a measured wall-clock delta otherwise.
void OnTick(std::function<void(float dt)>);

// ---------------------------------------------------------------------------
// Diagnostics.  SelfTest() logs the acceptance checklist: object count, five
// names, FindClass("Actor"), a property offset compared against the SDK, and the
// ProcessEvent call count.  Safe from any thread, but on the game thread it can
// also verify the vtable slot.
// ---------------------------------------------------------------------------
void SelfTest(const char* when);
uint64 ProcessEventCount();
bool   IsGameThread();          // true when called from inside a detour
void   NoteSelfTestTick(float dt);

// Drop every cached lookup (class/object caches and the IsChildOf memo).  The
// co-op module calls this on a world change: a map unload destroys the level's
// objects and blueprint classes, and UObject addresses are recycled, so a cached
// pointer can name a different object that still passes IsValid().
void ResetLookupCaches();

// Resolved addresses, for the log and for other modules that want the raw values.
struct Resolved {
    uintptr_t imageBase = 0;
    uintptr_t processEvent = 0;
    uintptr_t appendString = 0;
    uintptr_t objects = 0;          // &FUObjectArray::ObjObjects (Dumper-7's "GObjects")
    uintptr_t engineTick = 0;
    uint32    processEventIndex = 0;
    const char* objectsSource = "?";
    const char* processEventSource = "?";
    const char* appendStringSource = "?";
    const char* engineTickSource = "?";
    bool      objectArrayLayoutB = false;   // true when the chunk table is at +0x00
    uintptr_t gworld = 0;                   // &GWorld, this build's RVA only
    const char* gworldSource = "?";
};
const Resolved& GetResolved();

// ---------------------------------------------------------------------------
// Instrumentation (see the comment on the profiler in ms2_ue.cpp).
//
// The engine layer records itself, but a co-op STEP is what the log shows as a
// 14 s frame, so the router instruments its steps with the same table.  Usage
// from another module:
//
//   const uint64 t0 = ue::ProfTicks();  const uint64 o0 = ue::ProfObjs();
//   ...do the work...
//   ue::ProfRecord("myStep", arg, ue::ProfTicks() - t0,
//                  ue::ProfObjs() - o0, ue::ProfNames() - n0);
//
// ProfRecord is cheap (it returns immediately unless the call was slow), so it
// is safe on a per-frame path.  ProfDump() prints the worst-first table.
// ---------------------------------------------------------------------------
uint64   ProfTicks();
uint64   ProfSlowThresholdTicks();     // ticks that count as "slow" (30 ms)
void     ProfRecord(const char* what, const char* arg, uint64 ticks, uint32 objs,
                    uint32 names);
uint32   ProfObjs();                   // object-array slots walked by this thread
uint32   ProfNames();                  // name resolutions by this thread
void     ProfCountObject();
void     ProfCountName();
void     ProfDump(const char* when);
bool     ProfEnabled();

// ---------------------------------------------------------------------------
// Cost accounting -- what the module costs PER FRAME.
//
// ProfRecord above answers "which step was slow"; this one answers "how much of
// every frame is ours", which is what a fps difference is made of.  It records
// EVERY call (not only the slow ones) and every frame, and CostDump prints
// avg/p95/max per row plus the per-frame series:
//
//   COST frame    n=3000  avg 3.20 ms p95<=7.10 max 210.00 ms
//   COST   appear n=120   avg 8.31 ms p95<=15.60 max 210.00 ms total 997.20 ms
//
// All of it is a QueryPerformanceCounter pair per step and two 64-bit adds per
// ProcessEvent call: no locks, no allocation, no string work.
// ---------------------------------------------------------------------------
uint64 CostNow();

// RAII timer for one step.  `tag` must be a string literal (it is the row key).
struct CostScope {
    const char* tag;
    uint64      t0;
    explicit CostScope(const char* t);
    ~CostScope();
};

void CostStepRecord(const char* tag, uint64 ticks);

// Per-frame series slots (CostFrameRecord / CostSeriesRecord).
enum {
    CostSlotFrameDt = 0,      // the engine's delta for the last frame
    CostSlotTick = 1,         // our whole per-frame callback (net::Frame)
    CostSlotDetour = 2,       // time in the ProcessEvent detour (all calls)
    CostSlotDetourOwn = 3,    // ...minus the original ProcessEvent call
    CostSlotEngineTick = 4,   // time inside the engine's own UGameEngine::Tick
    CostSlotDrain = 5,        // the game-thread queue
    CostSlotCount = 6
};
void CostSeriesRecord(int slot, const char* name, uint64 ticks);

// Called once per frame by the engine-tick detour: folds the thread-local detour
// accumulators into the series and starts a new frame.
void CostFrameRecord(uint64 dtTicks, uint64 tickTicks, uint64 drainTicks);

// Prints the window (all series and steps), then resets it.
void CostDump(const char* when);

// ---------------------------------------------------------------------------
// TMap read/write for the two shapes the quest log uses (ms2_map.cpp):
// TMap<FString,int32> (NPCRelationships, NarrativeEvents, ...) and
// TMap<FName,int32> (CurrentQuestObjectiveProgress).
//
// WHY THESE EXIST.  The run-two failure `rel Gorf 0 -> 2 ok=false` was not a
// missing insert: NPCRelationships is seeded from the blueprint defaults with every
// hub NPC, and the MAP_PROBE dump shows `key='Gorf'`.  It was this build's TBitArray
// layout: the allocation bits are INLINE for <= 128 elements (NumBits at +0x28,
// MaxBits 0x80 at +0x2C, the bits themselves at +0x10), so MapForEach's flag test
// never applies and its fallback proves a slot by decoding its key as an FName --
// while these maps are keyed by FString.  Every row was invisible: `wqScan` published
// `r|e| = 0` and MapAddStrInt reported "key is not in the map" for a key that was
// there.
//
// The functions below walk those maps with the correct header and the right key
// type; a slot is still only read after its allocation bit AND its key decode agree,
// so a wrong stride yields fewer rows, never a fault.
// ---------------------------------------------------------------------------
bool MapWalkStrInt(UObject* o, const char* prop,
                   const std::function<bool(const char* key, int32 value)>& fn);
bool MapWalkNameInt(UObject* o, const char* prop,
                    const std::function<bool(const char* key, int32 value)>& fn);
// Find one key; `slot` (optional) is the address of its value inside the map.
bool MapFindStrInt(UObject* o, const char* prop, const char* key, int32* value, void** slot);
bool MapFindNameInt(UObject* o, const char* prop, const char* key, int32* value, void** slot);

// Set an EXISTING key (overwrite + read-back).  This is the whole of what main.lua's
// `TMap:Add` needs for a key the game seeded, and it involves no allocator.
bool MapSetStrInt(UObject* o, const char* prop, const char* key, int32 value);
bool MapSetNameInt(UObject* o, const char* prop, const char* key, int32 value);

// Set-or-insert.  Same as MapSet* when the key exists; when it does NOT, the engine's
// own map-add routine is used (the one UBlueprintMapLibrary::Map_Add reaches: proven on
// a throwaway map, prologue-checked, and read back through this module's walk, with
// one failed read-back disabling inserts for the session).  Returns true only when the
// pair reads back with the requested value.
bool MapInsertStrInt(UObject* o, const char* prop, const char* key, int32 value);
bool MapInsertNameInt(UObject* o, const char* prop, const char* key, int32 value);

// Read-only diagnostic (MAP_PROBE.txt in the coop dir): the engine's map-code
// addresses, GMalloc's vtable and the header + rows of every TMap the progress
// module touches.  MAP_INSERT_TEST.txt runs the throwaway-map insert experiment.
// Safe to call every tick: it checks its own throttle/toggle.
void MapProbe();

}  // namespace ue
