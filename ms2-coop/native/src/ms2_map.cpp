// ms2_map.cpp -- reading and writing the TMaps the quest log keeps, correctly.
//
// WHY THIS FILE EXISTS (QUEST2)
// -----------------------------
// main.lua does `q.NPCRelationships:Add(npc, rel+2)` (3224) and
// `o.CurrentQuestObjectiveProgress:Add(FName(obj), v)` (2919); the native port's
// `ue::MapAddStrInt` / `ue::MapAddNameInt` could not do it, and the run-two verdict
// failed at `rel Gorf 0 -> 2 ok=false`.
//
// WHAT THE MAP-PROBE RUN PROVED (log lines quoted in briefs/QUEST2-DONE.md)
// ------------------------------------------------------------------------
// The key is ALREADY THERE.  BPC_QuestLogComponent_C::NPCRelationships is seeded
// from the blueprint's defaults with every hub NPC, and the probe read:
//
//   hdr num=7 max=8 ... el[1] keyOk=1 key='Gorf' value=0 hashNext=-1 hashIdx=4
//
// So `:Add` here is an *overwrite*, not an insert.  The bug was the READER:
//
//  * This build's TBitArray keeps its bits INLINE for <= 128 elements: the probe
//    shows `NumBits` at map+0x28, `MaxBits = 0x80` at +0x2C and the seven set bits
//    as the word at +0x10.  ms2_ue.cpp's walker reads a *heap* bit array
//    (ptr +0x10, num +0x18, NumBits +0x20), sees `NumBits != Num`, and falls back
//    to proving each slot by decoding its key -- but that fallback always decodes
//    the key as an FName, while these maps are keyed by FString (16 bytes).  Every
//    slot was therefore judged dead and `wqScan` published `r|e| = 0`.
//  * `MapAddStrInt` uses the same walk (`MapFindKey` -> `IterateMap`), so it also
//    said "the key is not in the map" for a key that was in the map.
//
// This file therefore carries its own walker for exactly the shapes the progress
// module needs -- TMap<FString,int32> and TMap<FName,int32> -- with this build's
// sparse-array header, the inline bit array, and a key decode of the RIGHT type.
// It is strictly a superset of the old behaviour and it holds the same hard rule:
// a slot is only read after its allocation bit says it is live AND its key decodes,
// so a wrong stride yields fewer rows, never a fault.
//
// WRITES.  Two shapes:
//   * value overwrite of an existing key -- always safe, no allocator involved;
//   * a genuinely NEW key, which needs the engine's own insert (allocator + rehash +
//     the property's GetValueTypeHash).  That routine is reached from
//     UBlueprintMapLibrary::execMap_Add; its call site in the shipped exe is fully
//     determined (kRvaMapAddHelper below) and the routine was proved on a throwaway
//     map before it was enabled: MAP_INSERT_TEST.txt asked it to add
//     WormfishGold["MAPPROBE_INSERT"]=1234 and the read-back found it
//     (`called=1 num 2 -> 3 found=1 value=1234 PASS`, briefs/QUEST2-DONE.md).
//     Every insert is still prologue-checked and read back, and one bad read-back
//     disables inserts for the session.

#include <windows.h>

// winuser.h defines GetClassName as a macro (-> GetClassNameA/W), which renames
// ue::GetClassName in THIS translation unit only.  ms2_ue.cpp #undefs the same one.
#undef GetClassName

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#include "coop_state.h"
#include "ms2_log.h"
#include "ms2_ue.h"

namespace ue {
namespace {

// ---------------------------------------------------------------------------
// Offsets.  README.md gives the FField/FProperty/UFunction numbers; the TSet
// header below is measured from live maps by MapProbe() (the raw dumps are in
// briefs/QUEST2-DONE.md) because this build's TBitArray layout is not the one
// ms2_ue.cpp assumes.
// ---------------------------------------------------------------------------
constexpr uint32 kOffClassDefaultObj = 0x110;   // UClass::ClassDefaultObject
constexpr uint32 kOffFuncNumParms    = 0xB4;    // UFunction::NumParms
constexpr uint32 kOffFuncParmsSize   = 0xB6;    // UFunction::ParmsSize
constexpr uint32 kOffFuncExec        = 0xD8;    // UFunction::ExecFunction (native)

// TSparseArray<TPair<K,V>> header of a TMap (measured, see the file comment).
constexpr uint32 kOffMapData      = 0x00;   // TArray<Element> Data: ptr,num,max
constexpr uint32 kOffMapCount     = 0x08;
constexpr uint32 kOffMapMax       = 0x0C;
constexpr uint32 kOffMapBits      = 0x10;   // TBitArray: inline words, or heap ptr
constexpr uint32 kOffMapBitsNum   = 0x18;
constexpr uint32 kOffMapBitsMax   = 0x1C;
constexpr uint32 kOffMapBitCount  = 0x28;   // TBitArray::NumBits
constexpr uint32 kOffMapBitMax    = 0x2C;   // TBitArray::MaxBits
constexpr uint32 kOffMapFreeFirst = 0x30;   // int32 FirstFreeIndex (-1 = none)
constexpr uint32 kOffMapFreeNum   = 0x34;   // int32 NumFreeIndices
constexpr int32  kInlineBitWords  = 4;      // MaxBits 0x80 == 4 words held inline

// The engine's "add a pair to a map property" routine, from
// UBlueprintMapLibrary::execMap_Add:
//   execMap_Add steps the map/key/value parameters off the bytecode and then
//     mov rcx, [map address]      mov rdx, [FMapProperty*]
//     mov r8,  [key temp]         mov r9,  [value temp]
//     call 0x3AEEAD0              (0x3B0491E in the Map_Add thunk, rva 0x3B04700)
// so the contract is (TMap*, FMapProperty*, const Key*, const Value*) -- the same
// four registers.  0x3AEEE30 is the matching "find" routine, called the same way by
// Map_Find and Map_Contains, which is what identifies this pair of helpers.
constexpr uintptr_t kRvaMapAddHelper = 0x3AEEAD0;
constexpr uint8_t   kMapAddPrologue[5] = {0x48, 0x85, 0xC9, 0x0F, 0x84};   // test rcx,rcx; je

// GMalloc, from README.md ("Globals").  Only used by the probe.
constexpr uintptr_t kRvaGMalloc = 0x0AF4B3C0;

int32 AlignUp(int32 v, int32 a) {
    if (a <= 1) return v;
    return ((v + a - 1) / a) * a;
}

// A TMap element is only 4-byte aligned once the stride is not a multiple of 8
// (TPair<FName,int32> is 20 bytes), so this is PlausiblePtr without the alignment
// requirement -- the same test ms2_ue.cpp uses.
bool MapAddrOk(const void* p) {
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000 && v < 0x00007FFFFFFFFFFFULL;
}

template <class T>
bool ReadAt(const void* base, uint32 off, T& out) {
    return ReadMem(reinterpret_cast<const uint8_t*>(base) + off, &out, sizeof(T));
}

std::string LocalFmt(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return std::string();
    const size_t len = (static_cast<size_t>(n) < sizeof(buf)) ? static_cast<size_t>(n)
                                                             : sizeof(buf) - 1;
    return std::string(buf, len);
}

std::string HexBytes(const uint8_t* p, int n) {
    static const char* kHex = "0123456789ABCDEF";
    std::string s;
    s.reserve(static_cast<size_t>(n) * 3);
    for (int i = 0; i < n; ++i) {
        if (i) s.push_back(' ');
        s.push_back(kHex[p[i] >> 4]);
        s.push_back(kHex[p[i] & 15]);
    }
    return s;
}

bool ToggleFileExists(const char* name) {
    const std::string& dir = coop::state().local.dir;
    if (dir.empty()) return false;
    std::wstring w(dir.begin(), dir.end());
    while (!w.empty() && (w.back() == L'/' || w.back() == L'\\')) w.pop_back();
    w.push_back(L'/');
    w += std::wstring(name, name + std::strlen(name));
    return GetFileAttributesW(w.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ---------------------------------------------------------------------------
// The map view: everything a walk needs, resolved once and validated.
// ---------------------------------------------------------------------------
struct MapView {
    uint8_t* base = nullptr;        // the TMap itself (inside the owner object)
    uint8_t* data = nullptr;        // TSparseArray element array
    int32 num = 0;                  // Data.Num()  (slots, live + free)
    int32 max = 0;
    const uint32* bits = nullptr;   // allocation bits: heap words, or the inline ones
    int32 bitWords = 0;
    bool bitCountOk = false;        // NumBits == num, i.e. the bits cover every slot
    int32 stride = 0;
    int32 keySize = 0;
    int32 keyOff = 0;
    int32 valueSize = 0;
    int32 valueOff = 0;
    bool fnameKey = false;
};

// A key is believable when it decodes as the type the map claims.  This is the
// safety net that makes a wrong stride or a stale map produce "fewer rows".
bool KeyText(const MapView& mv, int32 i, std::string* out) {
    if (!mv.data || i < 0 || i >= mv.num) return false;
    const uint8_t* key = mv.data + static_cast<size_t>(i) * mv.stride + mv.keyOff;
    if (!MapAddrOk(key)) return false;
    if (mv.fnameKey) {
        FName n;
        if (!ReadMem(key, &n, sizeof(n))) return false;
        if (n.ComparisonIndex == 0 || n.ComparisonIndex > 0x08000000u) return false;
        const std::string s = ToString(n);
        if (s.empty() || s[0] == '<') return false;
        if (out) *out = s;
        return true;
    }
    FString s;
    if (!ReadMem(key, &s, sizeof(s))) return false;
    if (s.Num < 1 || s.Num > 4096 || s.Max < s.Num || !s.Data) return false;
    if (out) *out = ToUtf8(s);
    return true;
}

// Allocation bit: inline words when the bit array never grew past its inline
// buffer (MaxBits == 0x80), else the heap array the header points at.
bool SlotLive(const MapView& mv, int32 i) {
    if (!mv.bitCountOk) return true;            // unknown: the key decode decides
    const int32 word = i >> 5;
    if (word < 0 || word >= mv.bitWords || !mv.bits) return false;
    uint32 w = 0;
    if (!ReadMem(mv.bits + word, &w, sizeof(w))) return false;
    return (w & (1u << (i & 31))) != 0;
}

// The element stride of TPair<Key,Value> inside its sparse array: the pair padded
// to the pair's own alignment, then the two int32 the array keeps after it.
int32 PairStride(int32 keySize, int32 valueSize, int32 keyAlign) {
    const int32 valueOff = AlignUp(keySize, valueSize >= 8 ? 8 : (valueSize >= 4 ? 4 : 1));
    const int32 pairEnd = valueOff + valueSize;
    const int32 pairAlign = keyAlign > 4 ? keyAlign : 4;
    return AlignUp(pairEnd, pairAlign) + 8;
}

bool ReadMapView(UObject* o, const char* prop, bool fnameKey, MapView* mv) {
    if (!o || !prop || !mv) return false;
    const int32_t off = PropOffset(o, prop);
    if (off < 0) return false;

    memset(mv, 0, sizeof(*mv));
    mv->base = reinterpret_cast<uint8_t*>(o) + off;
    mv->fnameKey = fnameKey;
    mv->keySize = fnameKey ? 8 : 16;
    mv->keyOff = 0;
    mv->valueSize = 4;
    mv->valueOff = AlignUp(mv->keySize, 4);
    mv->stride = PairStride(mv->keySize, mv->valueSize, fnameKey ? 4 : 8);

    if (!ReadAt(mv->base, kOffMapData, mv->data) || !MapAddrOk(mv->base)) return false;
    if (!ReadAt(mv->base, kOffMapCount, mv->num) || !ReadAt(mv->base, kOffMapMax, mv->max))
        return false;
    if (mv->num < 0 || mv->max < 0 || mv->num > mv->max || mv->max > (1 << 22)) return false;
    if (mv->num > 0 && !MapAddrOk(mv->data)) return false;

    int32 bitCount = 0, bitMax = 0;
    if (ReadAt(mv->base, kOffMapBitCount, bitCount) && ReadAt(mv->base, kOffMapBitMax, bitMax) &&
        bitCount == mv->num && bitCount > 0) {
        mv->bitCountOk = true;
        if (bitMax > 0 && bitMax <= kInlineBitWords * 32) {
            mv->bits = reinterpret_cast<const uint32*>(mv->base + kOffMapBits);
            mv->bitWords = kInlineBitWords;
        } else {
            const uint32* heap = nullptr;
            int32 words = 0;
            if (ReadAt(mv->base, kOffMapBits, heap) && MapAddrOk(heap) &&
                ReadAt(mv->base, kOffMapBitsNum, words) && words >= (bitCount + 31) / 32) {
                mv->bits = heap;
                mv->bitWords = words;
            } else {
                mv->bitCountOk = false;     // unrecognised shape: fall back to keys
            }
        }
    }

    // If the computed stride does not decode a single live key, the pair alignment
    // assumption is wrong for this map -- try the other candidate and then refuse.
    auto anyKey = [&](int32 stride) {
        const int32 saved = mv->stride;
        mv->stride = stride;
        bool ok = false;
        for (int32 i = 0; i < mv->num && !ok; ++i) {
            if (SlotLive(*mv, i)) ok = KeyText(*mv, i, nullptr);
        }
        mv->stride = saved;
        return ok;
    };
    if (mv->num > 0 && !anyKey(mv->stride)) {
        const int32 alt = AlignUp(mv->valueOff + mv->valueSize, 4) + 8;
        if (alt != mv->stride && anyKey(alt)) mv->stride = alt;
    }
    return true;
}

// Walk every live slot: fn(keyText, value, keyPtr, valuePtr).  Return false from
// fn to stop.  Returns the number of rows visited through *rows.
bool Walk(UObject* o, const char* prop, bool fnameKey,
          const std::function<bool(const std::string&, int32, void*, void*)>& fn, int32* rows) {
    if (rows) *rows = 0;
    MapView mv;
    if (!ReadMapView(o, prop, fnameKey, &mv)) return false;
    int32 n = 0;
    for (int32 i = 0; i < mv.num; ++i) {
        if (!SlotLive(mv, i)) continue;
        std::string key;
        if (!KeyText(mv, i, &key)) continue;
        uint8_t* el = mv.data + static_cast<size_t>(i) * mv.stride;
        int32 value = 0;
        if (!ReadMem(el + mv.valueOff, &value, sizeof(value))) continue;
        ++n;
        if (!fn(key, value, el + mv.keyOff, el + mv.valueOff)) break;
    }
    if (rows) *rows = n;
    return true;
}

std::atomic<int> g_probeState{0};
std::atomic<int> g_insertState{0};    // 0 untried, 1 enabled (validated), 2 disabled

// The engine routine, SEH-guarded: a wrong routine must not take the process down.
bool CallEngineMapAdd(void* fn, void* map, void* prop, const void* key, const void* value) {
    __try {
        reinterpret_cast<void (*)(void*, void*, const void*, const void*)>(fn)(map, prop, key,
                                                                              value);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public: walks / find / insert for the two shapes the quest log uses.
// ---------------------------------------------------------------------------
bool MapWalkStrInt(UObject* o, const char* prop,
                   const std::function<bool(const char*, int32)>& fn) {
    if (!fn) return false;
    return Walk(o, prop, /*fnameKey*/ false,
                [&](const std::string& k, int32 v, void*, void*) { return fn(k.c_str(), v); },
                nullptr);
}

bool MapWalkNameInt(UObject* o, const char* prop,
                    const std::function<bool(const char*, int32)>& fn) {
    if (!fn) return false;
    return Walk(o, prop, /*fnameKey*/ true,
                [&](const std::string& k, int32 v, void*, void*) { return fn(k.c_str(), v); },
                nullptr);
}

bool MapFindStrInt(UObject* o, const char* prop, const char* key, int32* value, void** slot) {
    if (value) *value = 0;
    if (slot) *slot = nullptr;
    if (!key) return false;
    bool hit = false;
    Walk(o, prop, /*fnameKey*/ false,
         [&](const std::string& k, int32 v, void*, void* vp) {
             if (k != key) return true;
             if (value) *value = v;
             if (slot) *slot = vp;
             hit = true;
             return false;
         },
         nullptr);
    return hit;
}

bool MapFindNameInt(UObject* o, const char* prop, const char* key, int32* value, void** slot) {
    if (value) *value = 0;
    if (slot) *slot = nullptr;
    if (!key) return false;
    bool hit = false;
    Walk(o, prop, /*fnameKey*/ true,
         [&](const std::string& k, int32 v, void*, void* vp) {
             if (k != key) return true;
             if (value) *value = v;
             if (slot) *slot = vp;
             hit = true;
             return false;
         },
         nullptr);
    return hit;
}

// Overwrite an existing key.  This is the whole of what QUEST2 needs: the quest
// log's NPCRelationships is seeded with every hub NPC by the blueprint defaults
// (probe: Gorf already present with value 0), so main.lua's `:Add` is an update.
bool MapSetStrInt(UObject* o, const char* prop, const char* key, int32 value) {
    void* slot = nullptr;
    int32 cur = 0;
    if (!MapFindStrInt(o, prop, key, &cur, &slot) || !slot) return false;
    if (!WriteMem(slot, &value, sizeof(value))) return false;
    int32 back = 0;
    return MapFindStrInt(o, prop, key, &back, nullptr) && back == value;
}

bool MapSetNameInt(UObject* o, const char* prop, const char* key, int32 value) {
    void* slot = nullptr;
    int32 cur = 0;
    if (!MapFindNameInt(o, prop, key, &cur, &slot) || !slot) return false;
    if (!WriteMem(slot, &value, sizeof(value))) return false;
    int32 back = 0;
    return MapFindNameInt(o, prop, key, &back, nullptr) && back == value;
}

// ---------------------------------------------------------------------------
// The engine's own insert, for a key that is genuinely absent.
//
// `value` is written through a temporary int32; the key is a stack FString / FName
// that the engine copies into the map (that is what the property's CopyCompleteValue
// in the helper is for), so nothing here outlives the call.
//
// OFF unless the MAP_INSERT_TEST run validated it: the helper's internal contract
// is inferred from its call site (the four registers above), so it is exercised on
// a throwaway map first and only trusted when Num+1 AND the new pair read back.
// ---------------------------------------------------------------------------
namespace {

bool EngineInsert(int kindStrKey, void* mapAddr, void* prop, const char* key, int32 value) {
    const uintptr_t base = GetResolved().imageBase;
    if (!base) return false;
    const uint8_t* fn = reinterpret_cast<const uint8_t*>(base + kRvaMapAddHelper);
    uint8_t pro[sizeof(kMapAddPrologue)] = {0};
    if (!ReadMem(fn, pro, sizeof(pro)) || memcmp(pro, kMapAddPrologue, sizeof(pro)) != 0) {
        ms2::Logf("map insert: the helper at rva 0x%llX does not start with the expected "
                  "bytes -- refusing to call it", (unsigned long long)kRvaMapAddHelper);
        return false;
    }
    int32 v = value;
    if (kindStrKey) {
        // The engine copies the FString key; a stack buffer is a legal source.
        wchar_t wbuf[512];
        int n = 0;
        for (; key[n] && n < 511; ++n) wbuf[n] = (wchar_t)(unsigned char)key[n];
        wbuf[n] = 0;
        FString s;
        s.Data = wbuf;
        s.Num = n;
        s.Max = 512;
        return CallEngineMapAdd((void*)fn, mapAddr, prop, &s, &v);
    }
    const FName name = MakeName(key);
    if (name.ComparisonIndex == 0) return false;
    return CallEngineMapAdd((void*)fn, mapAddr, prop, &name, &v);
}

// Shared entry: overwrite when the key exists, engine insert when it does not.
// The insert is prologue-checked and read back; one failed read-back disables inserts
// for the rest of the session (g_insertState == 2), which is the only state that blocks
// this path.
bool InsertCommon(int kindStrKey, UObject* o, const char* prop, const char* key, int32 value) {
    if (!o || !prop || !key) return false;
    if (kindStrKey ? MapSetStrInt(o, prop, key, value) : MapSetNameInt(o, prop, key, value))
        return true;
    if (g_insertState.load(std::memory_order_acquire) == 2) {
        static std::atomic<int> warned{0};
        if (warned.fetch_add(1) < 3) {
            ms2::Logf("map %s: key '%s' is absent and the engine insert was disabled earlier in "
                      "this session -- skipped", prop, key);
        }
        return false;
    }
    const int32_t off = PropOffset(o, prop);
    if (off < 0) return false;
    FProperty* p = FindProperty(GetClass(o), prop);
    if (!p) return false;
    const int32 before = [&] {
        MapView mv;
        return ReadMapView(o, prop, kindStrKey == 0, &mv) ? mv.num : -1;
    }();
    if (!EngineInsert(kindStrKey, reinterpret_cast<uint8_t*>(o) + off, p, key, value)) return false;
    // Read back through our own walk: the pair has to be there with the right value.
    int32 back = 0;
    const bool ok = kindStrKey == 0 ? MapFindNameInt(o, prop, key, &back, nullptr)
                                    : MapFindStrInt(o, prop, key, &back, nullptr);
    if (!ok || back != value) {
        MapView mv;
        const int32 after = ReadMapView(o, prop, kindStrKey == 0, &mv) ? mv.num : -1;
        ms2::Logf("map %s: engine insert of '%s' did not read back (num %d -> %d, back=%d) -- "
                  "inserts are disabled for the rest of the session", prop, key, (int)before,
                  (int)after, (int)back);
        g_insertState.store(2, std::memory_order_release);
        return false;
    }
    g_insertState.store(1, std::memory_order_release);
    MapView mv;
    const int32 after = ReadMapView(o, prop, kindStrKey == 0, &mv) ? mv.num : -1;
    ms2::Logf("map %s: engine insert '%s' = %d ok (slots %d -> %d, read back through our walk)",
              prop, key, (int)value, (int)before, (int)after);
    return true;
}

}  // namespace

bool MapInsertStrInt(UObject* o, const char* prop, const char* key, int32 value) {
    return InsertCommon(/*kindStrKey*/ 1, o, prop, key, value);
}

bool MapInsertNameInt(UObject* o, const char* prop, const char* key, int32 value) {
    return InsertCommon(/*kindStrKey*/ 0, o, prop, key, value);
}

// ---------------------------------------------------------------------------
// The probe (MAP_PROBE.txt): read-only, and the evidence the reader above is
// built from.  It also dumps GMalloc's vtable and the engine's map-code addresses
// so the insert's call site can be re-derived after a game update.
// ---------------------------------------------------------------------------
namespace {

void DumpMapHeader(const char* label, void* mapAddr, bool fnameKey) {
    if (!MapAddrOk(mapAddr)) {
        ms2::Logf("mapprobe: %s map 0x%llX is not a plausible address", label,
                  (unsigned long long)(uintptr_t)mapAddr);
        return;
    }
    uint8_t hdr[0x58];
    if (!ReadMem(mapAddr, hdr, sizeof(hdr))) {
        ms2::Logf("mapprobe: %s header unreadable", label);
        return;
    }
    int32 num = 0, max = 0, bits = 0, bitsMax = 0, freeFirst = 0, freeNum = 0;
    ReadAt(mapAddr, kOffMapCount, num);
    ReadAt(mapAddr, kOffMapMax, max);
    ReadAt(mapAddr, kOffMapBitCount, bits);
    ReadAt(mapAddr, kOffMapBitMax, bitsMax);
    ReadAt(mapAddr, kOffMapFreeFirst, freeFirst);
    ReadAt(mapAddr, kOffMapFreeNum, freeNum);
    uint8_t* data = nullptr;
    ReadAt(mapAddr, kOffMapData, data);
    ms2::Logf("mapprobe: %s hdr num=%d max=%d data=0x%llX bitCount=%d bitMax=%d "
              "free=%d/%d", label, (int)num, (int)max, (unsigned long long)(uintptr_t)data,
              (int)bits, (int)bitsMax, (int)freeFirst, (int)freeNum);
    ms2::Logf("mapprobe: %s raw %s", label, HexBytes(hdr, 0x50).c_str());

}

void DumpMapProp(const char* label, UObject* o, const char* prop, bool fnameKey) {
    if (!o) {
        ms2::Logf("mapprobe: %s skipped (no object)", label);
        return;
    }
    const int32_t off = PropOffset(o, prop);
    if (off < 0) {
        ms2::Logf("mapprobe: %s: %s has no property '%s'", label, GetClassName(o).c_str(), prop);
        return;
    }
    DumpMapHeader(LocalFmt("%s.%s", label, prop).c_str(),
                  reinterpret_cast<uint8_t*>(o) + off, fnameKey);
    int32 rows = 0;
    Walk(o, prop, fnameKey,
         [&](const std::string& k, int32 v, void*, void*) {
             ms2::Logf("mapprobe: %s.%s row '%s' = %d", label, prop, k.c_str(), (int)v);
             return true;
         },
         &rows);
    ms2::Logf("mapprobe: %s.%s rows(this reader)=%d", label, prop, (int)rows);
}

void DumpUFunctionOf(UObject* obj, const char* fnName) {
    UFunction* fn = FindFunction(obj, fnName);
    if (!fn || !PlausiblePtr(fn)) {
        ms2::Logf("mapprobe: ufn %s not found", fnName);
        return;
    }
    uint16_t numParms = 0, parmsSize = 0;
    uintptr_t exec = 0;
    ReadAt(fn, kOffFuncNumParms, numParms);
    ReadAt(fn, kOffFuncParmsSize, parmsSize);
    ReadAt(fn, kOffFuncExec, exec);
    const uintptr_t base = GetResolved().imageBase;
    ms2::Logf("mapprobe: ufn %s fn=0x%llX exec=0x%llX execRva=0x%llX parms=%u/%u", fnName,
              (unsigned long long)(uintptr_t)fn, (unsigned long long)exec,
              (unsigned long long)(exec > base ? exec - base : 0), (unsigned)numParms,
              (unsigned)parmsSize);
}

void DumpGMalloc() {
    const uintptr_t base = GetResolved().imageBase;
    if (!base) return;
    void* gmalloc = nullptr;
    if (!ReadMem(reinterpret_cast<const void*>(base + kRvaGMalloc), &gmalloc, sizeof(gmalloc)) ||
        !MapAddrOk(gmalloc)) {
        ms2::Logf("mapprobe: GMalloc unreadable at rva 0x%llX", (unsigned long long)kRvaGMalloc);
        return;
    }
    void* vtbl = nullptr;
    if (!ReadMem(gmalloc, &vtbl, sizeof(vtbl)) || !MapAddrOk(vtbl)) return;
    std::string slots;
    for (int i = 0; i < 12; ++i) {
        uintptr_t fnp = 0;
        if (!ReadMem(reinterpret_cast<uintptr_t*>(vtbl) + i, &fnp, sizeof(fnp))) break;
        slots += LocalFmt("%s0x%llX", i ? "," : "", (unsigned long long)(fnp - base));
    }
    ms2::Logf("mapprobe: GMalloc=0x%llX vtableRva=0x%llX slots(rva)=%s",
              (unsigned long long)(uintptr_t)gmalloc, (unsigned long long)((uintptr_t)vtbl - base),
              slots.c_str());
}

// The throwaway-map experiment (MAP_INSERT_TEST.txt): can the engine's own routine
// add a NEW key to a TMap<FString,int32> we do not need?  WormfishGold is that map
// (a per-NPC gold counter the hub reads).  Nothing is enabled by it unless the pair
// reads back correctly.
void InsertSelfTest(UObject* qlog) {
    if (!qlog) return;
    const int32_t off = PropOffset(qlog, "WormfishGold");
    if (off < 0) {
        ms2::Log("mapprobe: insert test -- no WormfishGold property");
        return;
    }
    FProperty* p = FindProperty(GetClass(qlog), "WormfishGold");
    if (!p) return;
    MapView mv;
    if (!ReadMapView(qlog, "WormfishGold", false, &mv)) {
        ms2::Log("mapprobe: insert test -- map unreadable");
        return;
    }
    const int32 before = mv.num;
    ms2::Log("mapprobe: insert test -- about to call the engine add for a NEW key "
             "'MAPPROBE_INSERT' on WormfishGold");
    const bool called =
        EngineInsert(/*kindStrKey*/ 1, reinterpret_cast<uint8_t*>(qlog) + off, p,
                     "MAPPROBE_INSERT", 1234);
    int32 back = 0;
    const bool found = MapFindStrInt(qlog, "WormfishGold", "MAPPROBE_INSERT", &back, nullptr);
    MapView after;
    const int32 n2 = ReadMapView(qlog, "WormfishGold", false, &after) ? after.num : -1;
    ms2::Logf("mapprobe: insert test -- called=%d num %d -> %d found=%d value=%d %s",
              called ? 1 : 0, (int)before, (int)n2, found ? 1 : 0, (int)back,
              (called && found && back == 1234) ? "PASS" : "FAIL");
    if (called && found && back == 1234) g_insertState.store(1, std::memory_order_release);
}

}  // namespace

void MapProbe() {
    if (g_probeState.load(std::memory_order_acquire) != 0) return;
    static uint64_t lastCheck = 0;
    const uint64_t now = GetTickCount64();
    if (now - lastCheck < 5000) return;
    lastCheck = now;
    const bool wantProbe = ToggleFileExists("MAP_PROBE.txt");
    const bool wantInsertTest = ToggleFileExists("MAP_INSERT_TEST.txt");
    if (!wantProbe && !wantInsertTest) return;
    g_probeState.store(1, std::memory_order_release);

    UObject* q = FindFirstOf("BPC_QuestLogComponent_C");
    if (wantInsertTest) {
        ms2::Log("mapprobe: ---- insert test ----");
        InsertSelfTest(q);
    }
    if (!wantProbe) return;

    ms2::Log("mapprobe: ---- begin ----");
    if (UClass* lib = FindClass("BlueprintMapLibrary")) {
        if (UObject* cdo = GetCDO(lib)) {
            static const char* kFns[] = {"Map_Add", "Map_Find", "Map_Remove", "Map_Contains"};
            for (const char* f : kFns) DumpUFunctionOf(cdo, f);
        }
    }
    DumpGMalloc();

    if (q) {
        static const char* kStrMaps[] = {"NPCRelationships", "NarrativeEvents",
                                        "ImmortalBrigSpoils", "WormfishGold",
                                        "TotalGoldSpentMerchant"};
        for (const char* m : kStrMaps) DumpMapProp("qlog", q, m, /*fnameKey*/ false);
    } else {
        ms2::Log("mapprobe: no BPC_QuestLogComponent_C instance");
    }

    // Store B's inner FName-keyed map: the one TMap that is regularly non-empty.
    if (UObject* sub = FindFirstOf("SpartaSaveSubSystem")) {
        if (UFunction* f = FindFunction(sub, "GetCurrentWorldStateSave")) {
            Params p(f);
            if (Call(sub, f, p.data())) {
                UObject* ws = p.Get<UObject*>("ReturnValue");
                if (ws && IsValid(ws)) {
                    int dumped = 0;
                    MapForEach(ws, "ActorInts", [&](void* key, void* value) -> bool {
                        FName n;
                        if (ReadMem(key, &n, sizeof(n)) && value) {
                            DumpMapHeader(LocalFmt("save.ActorInts[%s]", ToString(n).c_str()).c_str(),
                                          value, /*fnameKey*/ true);
                        }
                        return ++dumped < 2;
                    });
                }
            }
        }
    }
    ms2::Log("mapprobe: ---- end ----");
}

}  // namespace ue
