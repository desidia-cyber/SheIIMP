// coop_equip.cpp -- [R8-APPEAR] the lab levers for "the peer's look", driven from
// cmd.txt: the game's OWN equipment writers, so a test can put a different shell,
// skin, mask, clothing or the dark form on the local player and watch what the
// peer's puppet does with it.
//
// WHY THE GAME'S OWN API.  Round 7 could only *fake* a different look (lookadd
// spawns an attachable on the pawn).  The real switch is one call each:
// /Game/Sparta/Core/Player/BPFL_Player_Equipment.Default__BPFL_Player_Equipment_C
// is a BlueprintFunctionLibrary with a CDO, and its Change* functions do the whole
// switch (mesh, materials, ability grant, save write) -- proven callable from the
// CDO by `equipprobe` (coop_probe.cpp, docs/HANDOFF-R5.md 5).  The parameter
// names/offsets are from research/30-player-look.md 3.2 and were re-checked
// against CXXHeaderDump/BPFL_Player_Equipment.hpp.
//
// Every write passes Save=false: the lab must not touch the owner's save.
//
// Verbs (`equip ...`):
//   dump                     current shell/skin/mask/clothing + the unlocked tags
//   state                    the above, one line each, into result_equip.txt
//   shell <tag|next>         ChangeShell(tag, Save=false)
//   shellnone                ChangeShell with an empty tag (back to no shell)
//   skin <tag|next>          ChangeShellSkin(<current shell>, tag)
//   mask <tag|next|none>     ChangeShellMask(<character id>, tag)
//   clothing <tag|next|none> ChangeShellClothing(<character id>, tag)
//   darkform                 ActivateDarkForm (into the dark form)
//   dfmask <tag|next>        ChangeDarkFormMask
//   dfclothing <tag|next>    ChangeDarkFormClothing
//   mesh <shellTag>          pawn InitMeshByID(tag): mesh only, no ability/save
//   load <path>              load::Asset(path) -- proves the peer-asset loader
//
// `next` picks the first tag in the save's own EquipmentUnlockState map that
// differs from the current one and shares its two-segment root, so a lab run needs
// no hard-coded tag names (dump prints the exact strings for the harness).

#include "coop_state.h"

#include "ms2_ue.h"

#include <cctype>
#include <string>
#include <vector>

namespace coop {
namespace equip {

namespace {

const char* const kEquipCdo =
    "/Game/Sparta/Core/Player/BPFL_Player_Equipment.Default__BPFL_Player_Equipment_C";

// FGameplayTag is a single FName in this build (a 4-byte name index + 4-byte
// number), which is why every tag parameter below is written as a ue::FName.
std::string Tag(const ue::FName& n) { return ue::ToString(n); }

ue::UObject* Cdo() {
    static ue::UObject* cdo = nullptr;
    if (cdo && ue::IsValid(cdo)) return cdo;
    cdo = ue::FindObject(kEquipCdo);
    if (!cdo || !ue::PlausiblePtr(cdo)) {
        cdo = nullptr;
        net::Log("equip: no BPFL_Player_Equipment CDO (%s)", kEquipCdo);
        return nullptr;
    }
    return cdo;
}

ue::UObject* SaveObj() {
    ue::UObject* s = ue::FindFirstOf("BP_SpartaPlayerSaveObject_C");
    return net::Valid(s) ? s : nullptr;
}

// The pawn's identity tag (ASpartaCharacter.CharacterId): the character whose
// mask/clothing is being changed, and the shell id the equipment library wants.
ue::FName CharacterId(ue::UObject* pawn) {
    ue::FName id{};
    if (pawn) ue::Get(pawn, "CharacterId", id);
    return id;
}

// FGameplayTagContainer starts with TArray<FGameplayTag> GameplayTags, so the
// container can be read as the array (the same trick coop_probe.cpp uses).
std::vector<ue::FName> TagList(ue::Params& p, const char* prop) {
    std::vector<ue::FName> out;
    struct Arr {
        ue::FName* data;
        int32_t num;
        int32_t max;
    };
    const Arr a = p.Get<Arr>(prop);
    if (!a.data || a.num <= 0 || a.num > 256) return out;
    for (int32_t i = 0; i < a.num; ++i) {
        ue::FName n{};
        if (ue::ReadMem(a.data + i, &n, sizeof(n))) out.push_back(n);
    }
    return out;
}

std::string Join(const std::vector<ue::FName>& v, int maxN = 64) {
    std::string s;
    for (int i = 0; i < (int)v.size() && i < maxN; ++i) {
        if (!s.empty()) s += ",";
        s += Tag(v[i]);
    }
    if ((int)v.size() > maxN) s += ",...";
    return s;
}

bool Contains(const std::vector<std::string>& v, const std::string& k) {
    for (const std::string& s : v) {
        if (s == k) return true;
    }
    return false;
}

// ---- the reads -------------------------------------------------------------

// GetShellSkin / GetDarkFormSkin: current skin tag + the id the save resolved.
struct Skin {
    ue::FName id{};
    ue::FName skin{};
    bool ok = false;
};

Skin CurrentSkin(ue::UObject* cdo, ue::UObject* pawn, ue::UObject* save, const char* fn,
                 const char* inName) {
    Skin out;
    ue::UFunction* f = ue::FindFunction(cdo, fn);
    if (!f) {
        net::Log("equip: no %s", fn);
        return out;
    }
    ue::Params p(f);
    p.Set<ue::FName>(inName, CharacterId(pawn));
    p.Set<ue::UObject*>("SaveObject", save);
    p.Set<ue::UObject*>("__WorldContext", pawn);
    const bool called = ue::Call(cdo, f, p.data());
    out.skin = p.Get<ue::FName>("ReturnValue");
    out.id = p.Get<ue::FName>("ShellId");
    if (!out.id.ComparisonIndex) out.id = p.Get<ue::FName>("DarkFormID");
    if (!out.id.ComparisonIndex) out.id = CharacterId(pawn);
    out.ok = called;
    return out;
}

// ---- the writes ------------------------------------------------------------

// ChangeShell / ChangeDarkForm: one id tag + Save + context + item class + save.
bool Write1(const char* fn, const char* idName, const ue::FName& id) {
    ue::UObject* cdo = Cdo();
    ue::UObject* pawn = net::Pawn();
    if (!cdo || !net::Valid(pawn)) return false;
    ue::UFunction* f = ue::FindFunction(cdo, fn);
    if (!f) {
        net::Log("equip: no %s on the equipment CDO", fn);
        return false;
    }
    ue::Params p(f);
    p.Set<ue::FName>(idName, id);
    p.Set<bool>("Save", false);
    p.Set<ue::UObject*>("__WorldContext", pawn);
    p.Set<ue::UObject*>("SaveObject", SaveObj());
    p.Set<ue::UObject*>("Item", nullptr);
    const bool called = ue::Call(cdo, f, p.data());
    net::Log("equip: %s(%s) Save=false ok=%s", fn, Tag(id).c_str(), called ? "true" : "false");
    return called;
}

// ChangeShellSkin / ChangeShellMask / ChangeShellClothing and the dark-form twins:
// two tags (id + value) + Save + context + save object.
bool Write2(const char* fn, const char* idName, const ue::FName& id, const char* valName,
            const ue::FName& val) {
    ue::UObject* cdo = Cdo();
    ue::UObject* pawn = net::Pawn();
    if (!cdo || !net::Valid(pawn)) return false;
    ue::UFunction* f = ue::FindFunction(cdo, fn);
    if (!f) {
        net::Log("equip: no %s on the equipment CDO", fn);
        return false;
    }
    ue::Params p(f);
    p.Set<ue::FName>(idName, id);
    p.Set<ue::FName>(valName, val);
    p.Set<bool>("Save", false);
    p.Set<ue::UObject*>("__WorldContext", pawn);
    p.Set<ue::UObject*>("SaveObject", SaveObj());
    p.Set<ue::UObject*>("Item", nullptr);
    const bool called = ue::Call(cdo, f, p.data());
    net::Log("equip: %s(%s, %s) Save=false ok=%s", fn, Tag(id).c_str(), Tag(val).c_str(),
             called ? "true" : "false");
    return called;
}

// ---- the attachable component: where the mask / clothing ACTUALLY becomes visible
//
// Lab run 3 proved the equipment writers are not enough: ChangeDarkFormMask and
// ChangeDarkFormClothing both answered ok=true and the pawn's attachable list stayed
// empty (`peer look applied ... (0 parts)`), i.e. the write reaches the save object but
// the actor that is *rendered* is spawned by the pawn's own component
// (`UBPC_AttachableItems_C`, HANDOFF.md 20: "masks/clothing are BP_Attachable_Item_*
// actors: AddItem, ChangeMask, ChangeClothing").  These helpers drive that component
// directly, which is the same path `lookadd` uses and that the LOOK verdict proves.
bool CallVoid(ue::UObject* o, const char* fn) {
    if (!net::Valid(o)) return false;
    ue::UFunction* f = ue::FindFunction(o, fn);
    if (!f) return false;
    ue::Params p(f);
    return ue::Call(o, f, p.data());
}

// A no-argument bool-returning UFunction (UUserWidget::IsInViewport).
bool CallBool(ue::UObject* o, const char* fn, bool* out) {
    if (out) *out = false;
    if (!net::Valid(o)) return false;
    ue::UFunction* f = ue::FindFunction(o, fn);
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(o, f, p.data())) return false;
    const int32_t off = ue::PropOffsetStruct((ue::UStruct*)f, "ReturnValue");
    if (off < 0 || off + 1 > p.size()) return false;
    uint8_t v = 0;
    if (!ue::ReadMem((uint8_t*)p.data() + off, &v, 1)) return false;
    if (out) *out = (v & 0x80) != 0;   // bool bitfields sit on bit 7
    return true;
}

ue::UObject* Attachables() {
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn)) return nullptr;
    ue::UObject* bpc = nullptr;
    if (!ue::Get(pawn, "BPC_AttachableItems", bpc)) {
        net::Log("equip: the pawn has no BPC_AttachableItems property");
        return nullptr;
    }
    if (!net::Valid(bpc)) {
        net::Log("equip: BPC_AttachableItems is null on %s", ue::GetClassName(pawn).c_str());
        return nullptr;
    }
    return bpc;
}

// How many actors the component currently holds (ActiveItems is TMap<tag,actor>).
int CountActive(ue::UObject* bpc, const char* prop) {
    int n = 0;
    if (!bpc) return 0;
    ue::MapForEach(bpc, prop, [&](void* k, void* v) {
        (void)k;
        (void)v;
        ++n;
        return n < 64;
    });
    return n;
}

// ChangeMask(MaskTag, AttachableCategory) / ChangeClothing(ClothingTag, ...): the
// component's own switch.  Category 0 is the slot both of them default to; 1 is the
// second slot (EAttachableCategory has exactly two).
bool BpcChange(const char* fn, const char* tagName, const ue::FName& tag, int category) {
    ue::UObject* bpc = Attachables();
    if (!bpc) return false;
    ue::UFunction* f = ue::FindFunction(bpc, fn);
    if (!f) {
        net::Log("equip: no %s on the attachable component", fn);
        return false;
    }
    ue::Params p(f);
    p.Set<ue::FName>(tagName, tag);
    p.Set<uint8_t>("AttachableCategory", (uint8_t)category);
    const bool called = ue::Call(bpc, f, p.data());
    net::Log("equip: %s(%s, cat=%d) ok=%s items=%d masks=%d clothing=%d", fn, Tag(tag).c_str(),
             category, called ? "true" : "false", CountActive(bpc, "ActiveItems"),
             CountActive(bpc, "ActiveMasks"), CountActive(bpc, "ActiveClothing"));
    return called;
}

// FindMaskClass(Id, out class) / FindClothingClass(Id, out class): the game's own
// tag -> attachable class lookup (the same table GetMaskObjects returns).
ue::UObject* FindAttachableClass(const char* fn, const ue::FName& tag) {
    ue::UObject* bpc = Attachables();
    if (!bpc) return nullptr;
    ue::UFunction* f = ue::FindFunction(bpc, fn);
    if (!f) {
        net::Log("equip: no %s", fn);
        return nullptr;
    }
    ue::Params p(f);
    p.Set<ue::FName>(fn[4] == 'M' ? "MaskID" : "ClothingID", tag);
    if (!ue::Call(bpc, f, p.data())) return nullptr;
    ue::UObject* cls = p.Get<ue::UObject*>("Value");
    return net::Valid(cls) ? cls : nullptr;
}

// AddMask(class, cat) / AddClothing(class, cat) -- spawns the attachable outright
// (what lookadd does through AddItem, which the LOOK verdict proves works).
bool BpcAdd(const char* fn, const ue::UObject* cls, int category) {
    ue::UObject* bpc = Attachables();
    if (!bpc || !cls) return false;
    ue::UFunction* f = ue::FindFunction(bpc, fn);
    if (!f) {
        net::Log("equip: no %s", fn);
        return false;
    }
    ue::Params p(f);
    p.Set<ue::UObject*>("ActorClass", const_cast<ue::UObject*>(cls));
    p.Set<uint8_t>("AttachableCategory", (uint8_t)category);
    const bool called = ue::Call(bpc, f, p.data());
    net::Log("equip: %s(%s, cat=%d) ok=%s items=%d masks=%d clothing=%d", fn,
             ue::GetName(const_cast<ue::UObject*>(cls)).c_str(), category,
             called ? "true" : "false", CountActive(bpc, "ActiveItems"),
             CountActive(bpc, "ActiveMasks"), CountActive(bpc, "ActiveClothing"));
    return called;
}

// The body skinned mesh of the local pawn: the one with the most bones (the same
// rule appear::CaptureLook uses for "the body").
ue::UObject* BodyMesh() {
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn)) return nullptr;
    ue::UFunction* f = ue::FindFunction(pawn, "K2_GetComponentsByClass");
    ue::UObject* cls = net::EngineClass("SkeletalMeshComponent");
    if (!f || !cls) return nullptr;
    ue::Params p(f);
    p.Set<ue::UObject*>("ComponentClass", cls);
    if (!ue::Call(pawn, f, p.data())) return nullptr;
    const auto arr = p.Get<ue::TArray<ue::UObject*>>("ReturnValue");
    ue::UObject* best = nullptr;
    int32_t bestBones = 0;
    for (int32_t i = 0; i < arr.Num && i < 64; ++i) {
        ue::UObject* c = nullptr;
        if (!ue::ReadMem(arr.Data + i, &c, sizeof(c)) || !net::Valid(c)) continue;
        ue::UFunction* fb = ue::FindFunction(c, "GetNumBones");
        if (!fb) continue;
        ue::Params pb(fb);
        if (!ue::Call(c, fb, pb.data())) continue;
        const int32_t n = pb.Get<int32_t>("ReturnValue");
        if (n > bestBones) {
            bestBones = n;
            best = c;
        }
    }
    return best;
}

// SwapShellMaterials / SwapDarkFormMaterials(SkeletalMesh, TargetMaterialsType,
// __WorldContext): the game's own body-material application.  ChangeShellSkin writes
// the skin tag; this is what re-dresses the mesh, and a skin IS a material swap
// (research/30-player-look.md 3.5).
bool SwapMaterials(int type) {
    ue::UObject* cdo = Cdo();
    ue::UObject* pawn = net::Pawn();
    ue::UObject* mesh = BodyMesh();
    if (!cdo || !net::Valid(pawn) || !net::Valid(mesh)) {
        net::Log("equip: SwapMaterials -- no cdo/pawn/body mesh");
        return false;
    }
    const bool dark = Tag(CharacterId(pawn)).find("Darkform") != std::string::npos;
    ue::UFunction* f = ue::FindFunction(cdo, dark ? "SwapDarkFormMaterials" : "SwapShellMaterials");
    if (!f) {
        net::Log("equip: no Swap*Materials on the equipment CDO");
        return false;
    }
    ue::Params p(f);
    p.Set<ue::UObject*>("SkeletalMesh", mesh);
    p.Set<uint8_t>("TargetMaterialsType", (uint8_t)type);
    p.Set<ue::UObject*>("__WorldContext", pawn);
    const bool called = ue::Call(cdo, f, p.data());
    net::Log("equip: Swap%sMaterials(mesh=%s, type=%d) ok=%s", dark ? "DarkForm" : "Shell",
             ue::GetName(mesh).c_str(), type, called ? "true" : "false");
    return called;
}

// Close the tutorial / notify popups the game puts up (the "LINGERING SHADOW" card a
// shell change triggers).  Same mechanism coop_death.cpp uses for the death screen:
// UUserWidget::RemoveFromParent on every live instance, found by class every time.
int ClosePopups() {
    static const char* const kCls[] = {
        "WBP_Notify_Tutorial_C",     "WBP_Notify_Tutorial_Container_C",
        "WBP_Notify_Large_Shell_C",  "WBP_Notify_Large_Item_Default_C",
        "WBP_Notify_Large_Base_C",   "WBP_Notify_Base_C",
        "WBP_BurstNotify_C",         "WBP_Notify_Small_C",
    };
    int closed = 0;
    for (const char* c : kCls) {
        for (ue::UObject* w : ue::FindAllOf(c)) {
            if (!net::Valid(w)) continue;
            // Only what is actually ON SCREEN: the class list also matches the
            // hidden template instances the container keeps for reuse, and removing
            // one of those while the game is animating it is a double free waiting
            // to happen (the 0xC0000374 in ntdll is this build's known Steam crash,
            // but there is no reason to add a second way to reach it).
            bool inVp = false;
            if (!CallBool(w, "IsInViewport", &inVp) || !inVp) continue;
            const bool ok = CallVoid(w, "RemoveFromParent");
            net::Log("equip popup: %s %s -> RemoveFromParent=%s", c, ue::GetName(w).c_str(),
                     ok ? "true" : "false");
            if (ok) ++closed;
        }
    }
    net::Log("equip popup: %d widget(s) closed", closed);
    return closed;
}

// ---- the save's own unlocked-equipment tags --------------------------------

// EquipmentUnlockState is the one map in this save that names every piece of
// equipment the player owns (FGameplayTag -> bool).  It is where `next` comes
// from, and where `dump` gets the exact tag strings from.
void UnlockedTags(std::vector<std::string>* out) {
    out->clear();
    ue::UObject* save = SaveObj();
    if (!save) return;
    ue::MapForEach(save, "EquipmentUnlockState", [&](void* key, void* value) {
        (void)value;
        ue::FName n{};
        if (!key || !ue::ReadMem(key, &n, sizeof(n))) return true;
        if (!n.ComparisonIndex) return true;
        out->push_back(Tag(n));
        return out->size() < 512;
    });
}

// "Shell.Harros.Obsidian" -> "Shell.Harros" (kept for the log lines).
std::string Root2(const std::string& tag) {
    size_t a = tag.find('.');
    if (a == std::string::npos) return tag;
    size_t b = tag.find('.', a + 1);
    return b == std::string::npos ? tag : tag.substr(0, b);
}

// ---- the catalogue of every valid tag --------------------------------------
//
// The test save has nothing but its starting weapons and seals unlocked
// (`equip dump`: unlocks=4), so a lab that wants to see a mask/clothing/shell/skin
// has to unlock one first.  These are the two places the game itself keeps the
// full set: `GetMaskObjects`/`GetClothingObjects` (a TMap<FGameplayTag, ...> of
// every attachable in the game) and `BPFL_Player::GetShellsIDsTagContainer`
// (every shell id).  Both are pure reads, so calling them is harmless.
enum Kind { kMask, kClothing, kShell, kSkin };

const char* KindName(Kind k) {
    switch (k) {
        case kMask: return "mask";
        case kClothing: return "clothing";
        case kShell: return "shell";
        default: return "skin";
    }
}

// A FGameplayTagContainer: TArray<FGameplayTag> GameplayTags (ptr/num/max) then
// ParentTags (one more FGameplayTag).  24 bytes in this build.
struct TagContainer {
    ue::FName* data;
    int32_t num;
    int32_t max;
    ue::FName parent;
};

// One FGameplayTag of a container, for UnlockMask/UnlockClothing's `CharacterIDs`.
TagContainer OneTag(ue::FName* slot) {
    TagContainer c{};
    c.data = slot;
    c.num = slot && slot->ComparisonIndex ? 1 : 0;
    c.max = c.num;
    return c;
}

void CollectTags(Kind kind, std::vector<std::string>* out) {
    out->clear();
    ue::UObject* cdo = Cdo();
    ue::UObject* pawn = net::Pawn();
    if (!cdo || !net::Valid(pawn)) return;
    if (kind == kMask || kind == kClothing) {
        // First the ones the character already has: `GetUnlockedMasks` /
        // `GetUnlockedClothing` are a different store from EquipmentUnlockState
        // (the flower crown is unlocked there and NOT in the unlock map), and a tag
        // the player already owns needs no unlock call at all.
        if (ue::UFunction* fu = ue::FindFunction(cdo, kind == kMask ? "GetUnlockedMasks"
                                                                   : "GetUnlockedClothing")) {
            ue::Params pu(fu);
            pu.Set<ue::FName>("CharacterId", CharacterId(pawn));
            pu.Set<ue::UObject*>("__WorldContext", pawn);
            if (ue::Call(cdo, fu, pu.data())) {
                for (const ue::FName& n : TagList(pu, "ReturnValue")) {
                    if (n.ComparisonIndex) out->push_back(Tag(n));
                }
            }
        }
        ue::UFunction* f =
            ue::FindFunction(cdo, kind == kMask ? "GetMaskObjects" : "GetClothingObjects");
        if (!f) return;
        ue::Params p(f);
        p.Set<ue::UObject*>("__WorldContext", pawn);
        if (!ue::Call(cdo, f, p.data())) return;
        const int32_t off = ue::PropOffsetStruct((ue::UStruct*)f, "ReturnValue");
        if (off < 0 || off >= p.size()) return;
        // The returned map is a TMap<FGameplayTag, (class)>: 8-byte tag keys.
        ue::MapForEachRaw((uint8_t*)p.data() + off, 8, 8, [&](void* k, void* v) {
            (void)v;
            ue::FName n{};
            if (!k || !ue::ReadMem(k, &n, sizeof(n)) || !n.ComparisonIndex) return true;
            if (!Contains(*out, Tag(n))) out->push_back(Tag(n));
            return out->size() < 256;
        });
        return;
    }
    if (kind == kShell) {
        // BPFL_Player_C is the library that owns the shell id container.
        ue::UObject* lib = ue::FindObject("/Game/Sparta/Core/Player/BPFL_Player.Default__BPFL_Player_C");
        if (!lib || !ue::PlausiblePtr(lib)) return;
        ue::UFunction* f = ue::FindFunction(lib, "GetShellsIDsTagContainer");
        if (!f) return;
        ue::Params p(f);
        p.Set<ue::UObject*>("__WorldContext", pawn);
        if (!ue::Call(lib, f, p.data())) return;
        for (const ue::FName& n : TagList(p, "ReturnValue")) {
            if (n.ComparisonIndex) out->push_back(Tag(n));
        }
        return;
    }
    // skins: the dark form's set and the current shell's set
    const ue::FName charId = CharacterId(pawn);
    struct Row {
        const char* fn;
        const char* inName;
        ue::FName in;
    };
    const Row rows[] = {
        {"GetAllSkinsForDarkForm", "DarkFormID", charId},
        {"GetAllSkinsForShell", "ShellId", charId},
    };
    for (const Row& r : rows) {
        ue::UFunction* f = ue::FindFunction(cdo, r.fn);
        if (!f) continue;
        ue::Params p(f);
        p.Set<ue::FName>(r.inName, r.in);
        p.Set<ue::UObject*>("__WorldContext", pawn);
        if (!ue::Call(cdo, f, p.data())) continue;
        for (const ue::FName& n : TagList(p, "Value")) {
            if (n.ComparisonIndex) out->push_back(Tag(n));
        }
    }
}

// The tag a verb argument means: a literal name, `next` (the first catalogue tag
// that is not the current one), or none.  `next` prefers a tag the player already
// owns, then falls back to the catalogue + an in-memory unlock.
bool PickTag(Kind kind, const std::string& arg, const std::string& cur, ue::FName* out,
             bool* unlocked) {
    *unlocked = false;
    if (arg == "none" || arg == "None" || arg.empty()) {
        *out = ue::FName{};
        return true;
    }
    if (arg != "next") {
        *out = ue::MakeFName(arg.c_str());
        if (!out->ComparisonIndex) {
            net::Log("equip: the name pool does not know '%s'", arg.c_str());
            return false;
        }
        // A literal tag is still unlocked when the save does not own it: the test
        // save owns almost nothing, and an unlock with Save=false only touches the
        // in-memory save object.
        std::vector<std::string> owned;
        UnlockedTags(&owned);
        *unlocked = !Contains(owned, Tag(*out));
        return true;
    }
    std::vector<std::string> owned;
    UnlockedTags(&owned);
    std::vector<std::string> all;
    CollectTags(kind, &all);
    auto pick = [&](const std::vector<std::string>& src, bool ownedSrc) {
        for (const std::string& t : src) {
            if (t == cur) continue;
            const bool has = Contains(owned, t);
            if (ownedSrc && !has) continue;
            // A tag from EquipmentUnlockState is only a candidate when it is in this
            // kind's catalogue: the unlock map holds weapons and seals too, and a
            // mask writer handed "Weapon.HadernsSword" answers ok=true and does
            // nothing at all (seen in lab run 2, 15:52:12).
            if (ownedSrc && !Contains(all, t)) continue;
            if (!ownedSrc && has) continue;   // already-owned ones were tried above
            *out = ue::MakeFName(t.c_str());
            if (!out->ComparisonIndex) continue;
            *unlocked = !has;
            return true;
        }
        return false;
    };
    if (pick(owned, true)) return true;
    if (pick(all, false)) return true;
    net::Log("equip %s next: no candidate besides '%s' (catalogue=%d owned=%d)",
             KindName(kind), cur.c_str(), (int)all.size(), (int)owned.size());
    return false;
}

// The game's own unlock writer, with Save=false so the owner's save is untouched.
// A tag that is already unlocked is left alone (the writers are not idempotent
// cheap: they broadcast global events).
void TryUnlock(Kind kind, const ue::FName& id, const ue::FName& owner) {
    if (!id.ComparisonIndex) return;
    std::vector<std::string> owned;
    UnlockedTags(&owned);
    if (Contains(owned, Tag(id))) return;
    ue::UObject* cdo = Cdo();
    ue::UObject* pawn = net::Pawn();
    if (!cdo || !net::Valid(pawn)) return;
    const char* fn = kind == kMask     ? "UnlockMask"
                     : kind == kClothing ? "UnlockClothing"
                     : kind == kShell    ? "UnlockShell"
                                         : "UnlockSkin";
    ue::UFunction* f = ue::FindFunction(cdo, fn);
    if (!f) {
        net::Log("equip: no %s", fn);
        return;
    }
    ue::FName slot = owner;   // the CharacterIDs container points at this name
    ue::Params p(f);
    if (kind == kMask || kind == kClothing) {
        p.Set<ue::FName>(kind == kMask ? "MaskID" : "ClothingID", id);
        p.Set<TagContainer>("CharacterIDs", OneTag(&slot));
    } else if (kind == kShell) {
        p.Set<ue::FName>("ShellId", id);
    } else {
        p.Set<ue::FName>("EquipmentID", owner);
        p.Set<ue::FName>("SkinID", id);
    }
    p.Set<bool>("Save", false);
    p.Set<ue::UObject*>("__WorldContext", pawn);
    const bool called = ue::Call(cdo, f, p.data());
    net::Log("equip: %s(%s, owner=%s) Save=false ok=%s", fn, Tag(id).c_str(), Tag(owner).c_str(),
             called ? "true" : "false");
}

// The test character is a dark form (`CharacterId.Player.Darkform.StrongOne`): its
// mask/clothing live in the dark-form maps, not the shell ones.
bool IsDarkForm(const std::string& id) { return id.find("Darkform") != std::string::npos; }

// ---- the state line the harness reads --------------------------------------

struct State {
    std::string character, shell, skin, mask, clothing, dfmask, dfclothing, dfskin;
    int unlocks = 0;
    int amasks = 0, aclothing = 0;   // the attachable component's census
    std::string unlockTags;
};

State ReadState() {
    State s;
    ue::UObject* cdo = Cdo();
    ue::UObject* pawn = net::Pawn();
    if (!cdo || !net::Valid(pawn)) return s;
    ue::UObject* save = SaveObj();
    s.character = Tag(CharacterId(pawn));

    const Skin sk = CurrentSkin(cdo, pawn, save, "GetShellSkin", "OptionalShellID");
    s.shell = Tag(sk.id);
    s.skin = Tag(sk.skin);
    // The dark form's own skin is a separate store (GetDarkFormSkin): ChangeDarkFormSkin
    // does NOT move GetShellSkin's ReturnValue, which is why the first version of this
    // state line could not show a dark-form skin change at all.
    {
        ue::UFunction* f = ue::FindFunction(cdo, "GetDarkFormSkin");
        if (f) {
            ue::Params p(f);
            p.Set<ue::FName>("OptionalDarkFormID", CharacterId(pawn));
            p.Set<ue::UObject*>("SaveObject", save);
            p.Set<ue::UObject*>("__WorldContext", pawn);
            if (ue::Call(cdo, f, p.data())) s.dfskin = Tag(p.Get<ue::FName>("ReturnValue"));
        }
    }
    // The attachable census: what is actually SPAWNED on the pawn (a mask/clothing
    // write that the game ignores leaves this at 0).
    if (ue::UObject* bpc = Attachables()) {
        s.amasks = CountActive(bpc, "ActiveMasks");
        s.aclothing = CountActive(bpc, "ActiveClothing");
    }

    // The four tag getters below share one shape: (id, __WorldContext) -> tag.
    struct Getter {
        const char* fn;
        const char* inName;
        ue::FName in;
        std::string* out;
    };
    const ue::FName charId = CharacterId(pawn);
    const Getter getters[] = {
        {"GetShellMask", "ShellId", sk.id, &s.mask},
        {"GetShellClothing", "ShellId", sk.id, &s.clothing},
        {"GetDarkFormMask", "DarkFormID", charId, &s.dfmask},
        {"GetDarkFormClothing", "DarkFormID", charId, &s.dfclothing},
    };
    for (const Getter& g : getters) {
        ue::UFunction* f = ue::FindFunction(cdo, g.fn);
        if (!f) continue;
        ue::Params p(f);
        p.Set<ue::FName>(g.inName, g.in);
        p.Set<ue::UObject*>("__WorldContext", pawn);
        if (ue::Call(cdo, f, p.data())) *g.out = Tag(p.Get<ue::FName>("ReturnValue"));
    }
    std::vector<std::string> all;
    UnlockedTags(&all);
    s.unlocks = (int)all.size();
    for (size_t i = 0; i < all.size() && i < 160; ++i) {
        if (i) s.unlockTags += ",";
        s.unlockTags += all[i];
    }
    return s;
}

void WriteStateFile(const State& s) {
    const std::string line = net::Fmt(
        "character=%s shell=%s skin=%s dfskin=%s mask=%s clothing=%s dfmask=%s dfclothing=%s"
        " amasks=%d aclothing=%d unlocks=%d\n",
        s.character.c_str(), s.shell.c_str(), s.skin.c_str(), s.dfskin.c_str(), s.mask.c_str(),
        s.clothing.c_str(), s.dfmask.c_str(), s.dfclothing.c_str(), s.amasks, s.aclothing,
        s.unlocks);
    net::WriteFile("result_equip.txt", line);
}

}  // namespace

// ===========================================================================
// equip::Cmd -- one line from cmd.txt, already split into verb + rest
// ===========================================================================
void Cmd(const std::string& rest) {
    // verb [arg]
    size_t sp = 0;
    while (sp < rest.size() && (unsigned char)rest[sp] <= ' ') ++sp;
    size_t e = sp;
    while (e < rest.size() && (unsigned char)rest[e] > ' ') ++e;
    std::string verb = rest.substr(sp, e - sp);
    size_t as = e;
    while (as < rest.size() && (unsigned char)rest[as] <= ' ') ++as;
    std::string arg = rest.substr(as);
    if (verb.empty()) {
        net::Log("equip: verbs are dump|state|shell|skin|mask|clothing|darkform|dfmask|dfclothing|mesh|load");
        return;
    }

    ue::UObject* cdo = Cdo();
    ue::UObject* pawn = net::Pawn();
    if (!cdo || !net::Valid(pawn)) {
        net::Log("equip %s: no equipment CDO or no pawn", verb.c_str());
        return;
    }

    if (verb == "dump" || verb == "state") {
        const State s = ReadState();
        net::Log("equip state: character=%s shell=%s skin=%s mask=%s clothing=%s dfmask=%s"
                 " dfclothing=%s unlocks=%d",
                 s.character.c_str(), s.shell.c_str(), s.skin.c_str(), s.mask.c_str(),
                 s.clothing.c_str(), s.dfmask.c_str(), s.dfclothing.c_str(), s.unlocks);
        // The unlocked tags, in one log line per 8 so a lab run can read the exact
        // strings without a file round-trip.
        std::vector<std::string> all;
        UnlockedTags(&all);
        std::string line;
        for (size_t i = 0; i < all.size(); ++i) {
            if (!line.empty()) line += " ";
            line += all[i];
            if ((i + 1) % 8 == 0 || i + 1 == all.size()) {
                net::Log("equip unlock: %s", line.c_str());
                line.clear();
            }
        }
        // The pawn's own shell tag, if it has one (BP_Shell_Base_C.ShellIDTag).
        ue::FName idTag{};
        if (ue::PropOffset(pawn, "ShellIDTag") >= 0 && ue::Get(pawn, "ShellIDTag", idTag)) {
            net::Log("equip: pawn.ShellIDTag=%s", Tag(idTag).c_str());
        }
        // The per-character lists, so a lab run has tag names even when the
        // EquipmentUnlockState walk comes back empty.
        struct ListCall {
            const char* fn;
            const char* inName;
            ue::FName in;
        };
        const ListCall lists[] = {
            {"GetAllSkinsForShell", "ShellId", ue::MakeFName(s.shell.c_str())},
            {"GetAllSkinsForDarkForm", "DarkFormID", CharacterId(pawn)},
            {"GetUnlockedMasks", "CharacterId", CharacterId(pawn)},
            {"GetUnlockedClothing", "CharacterId", CharacterId(pawn)},
        };
        for (const ListCall& c : lists) {
            ue::UFunction* f = ue::FindFunction(cdo, c.fn);
            if (!f) {
                net::Log("equip: no %s", c.fn);
                continue;
            }
            ue::Params p(f);
            p.Set<ue::FName>(c.inName, c.in);
            p.Set<ue::UObject*>("__WorldContext", pawn);
            const bool called = ue::Call(cdo, f, p.data());
            std::vector<ue::FName> v = TagList(p, "Value");
            if (v.empty()) v = TagList(p, "ReturnValue");
            if (v.empty()) v = TagList(p, "ValidMasks");
            net::Log("equip: %s(%s) ok=%d success=%d n=%d [%s]", c.fn, Tag(c.in).c_str(),
                     called ? 1 : 0, p.Get<bool>("Success") ? 1 : 0, (int)v.size(),
                     Join(v, 32).c_str());
        }
        WriteStateFile(s);
        return;
    }

    if (verb == "load") {
        // Item 5's own probe: load an arbitrary /Game/ path through the loader.
        ue::UObject* o = coop::load::Asset(arg);
        net::WriteFile("result_load.txt",
                       net::Fmt("%s %s (%d loaded, %d failed)\n", o ? "PASS" : "FAIL", arg.c_str(),
                                coop::load::Done(), coop::load::Failed()));
        net::Log("equip load: %s -> %s", arg.c_str(),
                 o ? ue::GetClassName(o).c_str() : "not loaded");
        return;
    }

    const State before = ReadState();
    const ue::FName charId = CharacterId(pawn);
    const bool dark = IsDarkForm(before.character);
    bool unlock = false;

    if (verb == "catalogue") {
        // Read-only: what CAN be worn in this build (the save itself unlocks only
        // the starting weapons and seals).
        const Kind kinds[] = {kShell, kMask, kClothing, kSkin};
        for (Kind k : kinds) {
            std::vector<std::string> v;
            CollectTags(k, &v);
            std::string line;
            for (size_t i = 0; i < v.size(); ++i) {
                if (!line.empty()) line += " ";
                line += v[i];
                if ((i + 1) % 6 == 0 || i + 1 == v.size()) {
                    net::Log("equip catalogue %s: %s", KindName(k), line.c_str());
                    line.clear();
                }
            }
            if (v.empty()) net::Log("equip catalogue %s: EMPTY", KindName(k));
        }
        return;
    }

    if (verb == "shell" || verb == "shellnone") {
        ue::FName id{};
        if (verb == "shellnone") {
            id = ue::FName{};
        } else if (!PickTag(kShell, arg, before.shell, &id, &unlock)) {
            return;
        }
        if (unlock) TryUnlock(kShell, id, charId);
        Write1("ChangeShell", "ShellId", id);
    } else if (verb == "darkform") {
        // ActivateDarkForm(__WorldContext) -> Success.  The transformation itself;
        // it swaps the body class, so the look sender must publish a new body.
        if (ue::UFunction* f = ue::FindFunction(cdo, "ActivateDarkForm")) {
            ue::Params p(f);
            p.Set<ue::UObject*>("__WorldContext", pawn);
            const bool called = ue::Call(cdo, f, p.data());
            net::Log("equip: ActivateDarkForm ok=%s success=%d", called ? "true" : "false",
                     p.Get<bool>("Success") ? 1 : 0);
        } else {
            net::Log("equip: no ActivateDarkForm");
        }
    } else if (verb == "skin") {
        ue::FName id{};
        if (!PickTag(kSkin, arg, before.skin, &id, &unlock)) return;
        if (unlock) TryUnlock(kSkin, id, charId);
        // Two halves, because round 8's lab run 3 showed one is not enough: the
        // writer records the skin, the swap actually dresses the body with it.
        Write2(dark ? "ChangeDarkFormSkin" : "ChangeShellSkin", dark ? "DarkFormID" : "ShellId",
               charId, "SkinID", id);
        SwapMaterials(0);
    } else if (verb == "swapmat") {
        SwapMaterials(arg.empty() ? 0 : (int)atoi(arg.c_str()));
    } else if (verb == "mask" || verb == "dfmask") {
        const bool useDark = dark || verb == "dfmask";
        const std::string cur = useDark ? before.dfmask : before.mask;
        ue::FName id{};
        if (!PickTag(kMask, arg, cur, &id, &unlock)) return;
        if (unlock) TryUnlock(kMask, id, charId);
        Write2(useDark ? "ChangeDarkFormMask" : "ChangeShellMask", "CharacterId", charId,
               "MaskID", id);
        // ...and the component that actually spawns the actor the look sender sees.
        // The verdict is the CENSUS, not the call's return value: ChangeMask answers
        // ok=true for a category it did not use.  Masks live in ActiveMasks (not
        // ActiveItems!), so the census has to read that map -- the first version read
        // ActiveItems, saw 0, and spawned a SECOND mask in slot 1 (lab run 5).
        int n = CountActive(Attachables(), "ActiveMasks");
        BpcChange("ChangeMask", "MaskTag", id, 0);
        if (CountActive(Attachables(), "ActiveMasks") == n) BpcChange("ChangeMask", "MaskTag", id, 1);
        if (CountActive(Attachables(), "ActiveMasks") == n) {
            ue::UObject* cls = FindAttachableClass("FindMaskClass", id);
            if (cls) BpcAdd("AddMask", cls, 0);
        }
    } else if (verb == "clothing" || verb == "dfclothing") {
        const bool useDark = dark || verb == "dfclothing";
        const std::string cur = useDark ? before.dfclothing : before.clothing;
        ue::FName id{};
        if (!PickTag(kClothing, arg, cur, &id, &unlock)) return;
        if (unlock) TryUnlock(kClothing, id, charId);
        Write2(useDark ? "ChangeDarkFormClothing" : "ChangeShellClothing", "CharacterId", charId,
               "ClothingID", id);
        int n = CountActive(Attachables(), "ActiveClothing");
        BpcChange("ChangeClothing", "ClothingTag", id, 0);
        if (CountActive(Attachables(), "ActiveClothing") == n) {
            BpcChange("ChangeClothing", "ClothingTag", id, 1);
        }
        if (CountActive(Attachables(), "ActiveClothing") == n) {
            ue::UObject* cls = FindAttachableClass("FindClothingClass", id);
            if (cls) BpcAdd("AddClothing", cls, 0);
        }
    } else if (verb == "additem") {
        // The lookadd route, straight: AddItem(<attachable class path>).  This is the
        // one attachable spawn the LOOK verdict proves works, so it is the fallback
        // when the component's own Change* path does nothing.
        ue::UObject* cls = net::Find(arg, true);
        ue::UObject* bpc = Attachables();
        if (!cls) {
            net::Log("equip additem: class %s not found", arg.c_str());
        } else if (bpc) {
            ue::UFunction* f = ue::FindFunction(bpc, "AddItem");
            if (!f) {
                net::Log("equip additem: no AddItem");
            } else {
                ue::Params p(f);
                p.Set<ue::UObject*>("ActorClass", cls);
                const bool ok = ue::Call(bpc, f, p.data());
                net::Log("equip additem(%s) ok=%s items=%d", arg.c_str(), ok ? "true" : "false",
                         CountActive(bpc, "ActiveItems"));
            }
        }
    } else if (verb == "popup") {
        // Close the tutorial/notify card (e.g. "LINGERING SHADOW" after a shell
        // change) so the lab can photograph the character instead of a popup.
        ClosePopups();
    } else if (verb == "attach") {
        // Read-only census of the attachable component.
        ue::UObject* bpc = Attachables();
        net::Log("equip attach: bpc=%s items=%d masks=%d clothing=%d", bpc ? "live" : "none",
                 CountActive(bpc, "ActiveItems"), CountActive(bpc, "ActiveMasks"),
                 CountActive(bpc, "ActiveClothing"));
    } else if (verb == "mesh") {
        // BP_Shell_Base_C::InitMeshByID(ShellId): the body mesh only.  This is the
        // lever when the shell is not unlocked for ChangeShell.
        ue::FName id = ue::MakeFName(arg.c_str());
        ue::UFunction* f = ue::FindFunction(pawn, "InitMeshByID");
        if (!f) {
            net::Log("equip mesh: the pawn (%s) has no InitMeshByID",
                     ue::GetClassName(pawn).c_str());
            return;
        }
        ue::Params p(f);
        p.Set<ue::FName>("ShellId", id);
        const bool called = ue::Call(pawn, f, p.data());
        net::Log("equip mesh: InitMeshByID(%s) ok=%s", Tag(id).c_str(), called ? "true" : "false");
    } else {
        net::Log("equip: unknown verb '%s' (dump|state|catalogue|shell|shellnone|skin|swapmat|"
                 "mask|clothing|darkform|dfmask|dfclothing|mesh|load|popup|attach)",
                 verb.c_str());
        return;
    }

    // Every write is followed by the read-back, so the log shows the new state and
    // the harness can wait for it without a second command.
    const State after = ReadState();
    net::Log("equip %s %s -> character=%s shell=%s skin=%s mask=%s clothing=%s", verb.c_str(),
             arg.c_str(), after.character.c_str(), after.shell.c_str(), after.skin.c_str(),
             after.mask.c_str(), after.clothing.c_str());
    WriteStateFile(after);
}

}  // namespace equip
}  // namespace coop
