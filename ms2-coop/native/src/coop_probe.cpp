// coop_probe.cpp -- one-off lab probes (round 5).  Not part of the sync; only runs
// when a `cmd` verb asks for it.
//
// equipprobe [apply|revert]
//   Answers SYNC-DATA-MINE.md probe P5: can BPFL_Player_Equipment_C (a blueprint
//   function library with zero instances) be called through its CDO, the way the
//   store-B code calls Default__SpartaWorldStateHelper?  Reads the current shell,
//   skin and mask, lists the shell's skins and unlocked masks, and with `apply`
//   switches to another skin through ChangeShellSkin (Save=false); `revert` puts
//   the original skin back.
//
// FGameplayTag is a single FName (TagName), so a tag parameter is 8 bytes and a
// FGameplayTagContainer starts with TArray<FGameplayTag> GameplayTags.

#include "coop_state.h"

#include "ms2_ue.h"

#include <string>
#include <vector>

namespace coop {
namespace probe {

namespace {

const char* kEquipCdo =
    "/Game/Sparta/Core/Player/BPFL_Player_Equipment.Default__BPFL_Player_Equipment_C";

ue::FName g_origSkin{};
ue::FName g_shell{};
bool g_haveOrig = false;

std::string Tag(const ue::FName& n) { return ue::ToString(n); }

std::vector<ue::FName> TagList(ue::Params& p, const char* prop) {
    std::vector<ue::FName> out;
    struct Arr { ue::FName* data; int32_t num; int32_t max; };
    const Arr a = p.Get<Arr>(prop);
    if (!a.data || a.num <= 0 || a.num > 256) return out;
    for (int32_t i = 0; i < a.num; ++i) {
        ue::FName n{};
        if (ue::ReadMem(a.data + i, &n, sizeof(n))) out.push_back(n);
    }
    return out;
}

std::string Join(const std::vector<ue::FName>& v) {
    std::string s;
    for (const auto& n : v) {
        if (!s.empty()) s += ",";
        s += Tag(n);
    }
    return s;
}

}  // namespace

void Equip(const std::string& arg) {
    ue::UObject* cdo = ue::FindObject(kEquipCdo);
    net::Log("equipprobe: cdo=%p class=%s", (void*)cdo,
             cdo && ue::PlausiblePtr(cdo) ? ue::GetClassName(cdo).c_str() : "-");
    if (!cdo || !ue::PlausiblePtr(cdo)) return;
    ue::UObject* pawn = net::Pawn();
    if (!net::Valid(pawn)) {
        net::Log("equipprobe: no pawn");
        return;
    }

    // 1. current shell + skin
    ue::UFunction* fSkin = ue::FindFunction(cdo, "GetShellSkin");
    net::Log("equipprobe: GetShellSkin fn=%p", (void*)fSkin);
    if (!fSkin) return;
    ue::FName shell{}, skin{};
    // The pawn's own identity tag (ASpartaCharacter.CharacterId) and the player
    // save object: GetShellSkin with neither returned None/None on the first run.
    ue::FName charId{};
    ue::Get(pawn, "CharacterId", charId);
    ue::UObject* save = ue::FindFirstOf("BP_SpartaPlayerSaveObject_C");
    ue::UObject* activeDef = nullptr;
    if (save) ue::Get(save, "ActiveShellItemDefinition", activeDef);
    net::Log("equipprobe: pawn=%s CharacterId=%s save=%p activeShellDef=%s",
             ue::GetClassName(pawn).c_str(), Tag(charId).c_str(), (void*)save,
             activeDef && ue::PlausiblePtr(activeDef) ? ue::GetName(activeDef).c_str() : "-");
    if (save) {
        ue::FName t{};
        for (const char* f : {"ActiveShell", "ActiveShellId", "CurrentShell", "ActiveShellSkins"}) {
            if (ue::PropOffset(save, f) >= 0 && ue::Get(save, f, t))
                net::Log("equipprobe: save.%s (first 8 bytes as FName) = %s", f, Tag(t).c_str());
        }
    }
    {
        ue::Params p(fSkin);
        p.Set<ue::FName>("OptionalShellID", charId);
        p.Set<ue::UObject*>("SaveObject", save);
        p.Set<ue::UObject*>("__WorldContext", pawn);
        const bool ok = ue::Call(cdo, fSkin, p.data());
        skin = p.Get<ue::FName>("ReturnValue");
        shell = p.Get<ue::FName>("ShellId");
        net::Log("equipprobe: GetShellSkin ok=%d shell=%s skin=%s", ok ? 1 : 0, Tag(shell).c_str(),
                 Tag(skin).c_str());
    }
    if (!shell.ComparisonIndex) shell = charId;

    // 2. all skins for that shell
    std::vector<ue::FName> skins;
    if (ue::UFunction* f = ue::FindFunction(cdo, "GetAllSkinsForShell")) {
        ue::Params p(f);
        p.Set<ue::FName>("ShellId", shell);
        p.Set<ue::UObject*>("__WorldContext", pawn);
        const bool ok = ue::Call(cdo, f, p.data());
        skins = TagList(p, "Value");
        net::Log("equipprobe: GetAllSkinsForShell ok=%d success=%d n=%d [%s]", ok ? 1 : 0,
                 p.Get<bool>("Success") ? 1 : 0, (int)skins.size(), Join(skins).c_str());
    }

    // 3. mask: current + unlocked
    std::vector<ue::FName> masks;
    if (ue::UFunction* f = ue::FindFunction(cdo, "GetShellMask")) {
        ue::Params p(f);
        p.Set<ue::FName>("ShellId", shell);
        p.Set<ue::UObject*>("__WorldContext", pawn);
        const bool ok = ue::Call(cdo, f, p.data());
        net::Log("equipprobe: GetShellMask ok=%d success=%d mask=%s", ok ? 1 : 0,
                 p.Get<bool>("Success") ? 1 : 0, Tag(p.Get<ue::FName>("ReturnValue")).c_str());
    }
    if (ue::UFunction* f = ue::FindFunction(cdo, "GetUnlockedMasks")) {
        ue::Params p(f);
        p.Set<ue::FName>("CharacterId", shell);
        p.Set<ue::UObject*>("__WorldContext", pawn);
        const bool ok = ue::Call(cdo, f, p.data());
        masks = TagList(p, "ReturnValue");
        net::Log("equipprobe: GetUnlockedMasks ok=%d success=%d n=%d [%s]", ok ? 1 : 0,
                 p.Get<bool>("Success") ? 1 : 0, (int)masks.size(), Join(masks).c_str());
    }
    if (ue::UFunction* f = ue::FindFunction(cdo, "GetNumberOfWeaponsUnlocked")) {
        ue::Params p(f);
        p.Set<ue::UObject*>("__WorldContext", pawn);
        const bool ok = ue::Call(cdo, f, p.data());
        net::Log("equipprobe: GetNumberOfWeaponsUnlocked ok=%d unlocked=%d total=%d", ok ? 1 : 0,
                 p.Get<int32_t>("UnlockedWeapons"), p.Get<int32_t>("TotalWeapons"));
    }

    // 3b. dark form: current mask, and the write path on it (dfapply / dfrevert).
    // The test save has no shell equipped (the pawn is CharacterId.Player.Darkform.*),
    // so this is the write we can see.
    static ue::FName s_dfOrig{};
    static bool s_dfHave = false;
    if (ue::UFunction* f = ue::FindFunction(cdo, "GetDarkFormMask")) {
        ue::Params p(f);
        p.Set<ue::FName>("DarkFormID", charId);
        p.Set<ue::UObject*>("__WorldContext", pawn);
        const bool ok = ue::Call(cdo, f, p.data());
        const ue::FName cur = p.Get<ue::FName>("ReturnValue");
        net::Log("equipprobe: GetDarkFormMask ok=%d success=%d mask=%s", ok ? 1 : 0,
                 p.Get<bool>("Success") ? 1 : 0, Tag(cur).c_str());
        if (arg == "dfapply" || arg == "dfrevert") {
            ue::FName target{};
            if (arg == "dfapply") {
                for (const auto& m : masks)
                    if (m.ComparisonIndex != cur.ComparisonIndex) { target = m; break; }
                s_dfOrig = cur;
                s_dfHave = true;
            } else if (s_dfHave) {
                target = s_dfOrig;
            }
            ue::UFunction* fc = ue::FindFunction(cdo, "ChangeDarkFormMask");
            if (fc && (arg == "dfrevert" ? s_dfHave : target.ComparisonIndex != 0)) {
                ue::Params c(fc);
                c.Set<ue::FName>("CharacterId", charId);
                c.Set<ue::FName>("MaskID", target);
                c.Set<bool>("Save", false);
                c.Set<ue::UObject*>("__WorldContext", pawn);
                net::Log("equipprobe: ChangeDarkFormMask(%s, %s) ...", Tag(charId).c_str(),
                         Tag(target).c_str());
                const bool cok = ue::Call(cdo, fc, c.data());
                ue::Params q(f);
                q.Set<ue::FName>("DarkFormID", charId);
                q.Set<ue::UObject*>("__WorldContext", pawn);
                ue::Call(cdo, f, q.data());
                net::Log("equipprobe: ChangeDarkFormMask ok=%d -> mask now %s", cok ? 1 : 0,
                         Tag(q.Get<ue::FName>("ReturnValue")).c_str());
            } else {
                net::Log("equipprobe: %s -- no target mask", arg.c_str());
            }
            return;
        }
    }

    // 4. the write path
    ue::FName target{};
    if (arg == "apply") {
        for (const auto& s : skins)
            if (s.ComparisonIndex != skin.ComparisonIndex || s.Number != skin.Number) {
                target = s;
                break;
            }
        if (!target.ComparisonIndex) {
            net::Log("equipprobe: apply -- no other skin for this shell");
            return;
        }
        g_origSkin = skin;
        g_shell = shell;
        g_haveOrig = true;
    } else if (arg == "revert") {
        if (!g_haveOrig) {
            net::Log("equipprobe: revert -- nothing to revert");
            return;
        }
        target = g_origSkin;
        shell = g_shell;
    } else {
        return;
    }
    ue::UFunction* fChange = ue::FindFunction(cdo, "ChangeShellSkin");
    if (!fChange) {
        net::Log("equipprobe: no ChangeShellSkin");
        return;
    }
    ue::Params p(fChange);
    p.Set<ue::FName>("ShellId", shell);
    p.Set<ue::FName>("SkinID", target);
    p.Set<bool>("Save", false);
    p.Set<ue::UObject*>("__WorldContext", pawn);
    net::Log("equipprobe: ChangeShellSkin(%s, %s) ...", Tag(shell).c_str(), Tag(target).c_str());
    const bool ok = ue::Call(cdo, fChange, p.data());
    net::Log("equipprobe: ChangeShellSkin ok=%d", ok ? 1 : 0);

    // read back
    ue::Params q(fSkin);
    q.Set<ue::UObject*>("__WorldContext", pawn);
    ue::Call(cdo, fSkin, q.data());
    net::Log("equipprobe: after change skin=%s", Tag(q.Get<ue::FName>("ReturnValue")).c_str());
}

// pupdump -- every SceneComponent on the puppet actor: class, name, mesh asset,
// visibility, anim mode / anim instance, attach parent, and which one is the
// component the drive animates (puppet::Mesh()).  Round 5: the puppet rendered
// in its reference pose with no weapons although every drive call succeeded.
namespace {
std::string Obj(ue::UObject* o) { return o && ue::PlausiblePtr(o) ? ue::GetName(o) : "-"; }

bool CallRet(ue::UObject* o, const char* fn, void* out, size_t n) {
    ue::UFunction* f = ue::FindFunction(o, fn);
    if (!f) return false;
    ue::Params p(f);
    if (!ue::Call(o, f, p.data())) return false;
    const int32_t off = ue::PropOffsetStruct((ue::UStruct*)f, "ReturnValue");
    if (off < 0 || off + (int32_t)n > p.size()) return false;
    return ue::ReadMem((uint8_t*)p.data() + off, out, n);
}
}  // namespace

void PupDump(bool self) {
    ue::UObject* pup = self ? net::Pawn() : puppet::Actor();
    ue::UObject* drive = puppet::Mesh();
    // [R8-APPEAR] `pupdump self` must mark the SELF pawn's own body mesh: the drive
    // mesh belongs to the puppet, so nothing on our own pawn ever matched it and the
    // material block (which hangs off `c == drive`) never printed for self.
    if (self && net::Valid(pup)) {
        ue::UFunction* fc = ue::FindFunction(pup, "K2_GetComponentsByClass");
        ue::UObject* mcls = ue::FindObject("/Script/Engine.SkeletalMeshComponent");
        if (fc && mcls) {
            ue::Params pc(fc);
            pc.Set<ue::UObject*>("ComponentClass", mcls);
            if (ue::Call(pup, fc, pc.data())) {
                const auto arr = pc.Get<ue::TArray<ue::UObject*>>("ReturnValue");
                int32_t bestBones = 0;
                for (int32_t i = 0; i < arr.Num && i < 64; ++i) {
                    ue::UObject* c = nullptr;
                    if (!ue::ReadMem(arr.Data + i, &c, sizeof(c)) || !net::Valid(c)) continue;
                    int32_t nb = 0;
                    if (!CallRet(c, "GetNumBones", &nb, sizeof(nb))) continue;
                    if (nb > bestBones) {
                        bestBones = nb;
                        drive = c;
                    }
                }
            }
        }
    }
    if (!net::Valid(pup)) {
        net::Log("pupdump: no puppet");
        return;
    }
    ue::UFunction* f = ue::FindFunction(pup, "K2_GetComponentsByClass");
    ue::UObject* cls = ue::FindObject("/Script/Engine.SceneComponent");
    if (!f || !cls) {
        net::Log("pupdump: no K2_GetComponentsByClass/SceneComponent");
        return;
    }
    ue::Params p(f);
    p.Set<ue::UObject*>("ComponentClass", cls);
    ue::Call(pup, f, p.data());
    const auto arr = p.Get<ue::TArray<ue::UObject*>>("ReturnValue");
    net::Log("pupdump: puppet=%s hidden=%d drive=%s comps=%d", ue::GetFullName(pup).c_str(),
             ue::GetBool(pup, "bHidden") ? 1 : 0, Obj(drive).c_str(), arr.Num);
    for (int32_t i = 0; i < arr.Num && i < 64; ++i) {
        ue::UObject* c = nullptr;
        if (!ue::ReadMem(arr.Data + i, &c, sizeof(c)) || !net::Valid(c)) continue;
        const std::string cn = ue::GetClassName(c);
        ue::UObject* asset = nullptr;
        if (!ue::Get(c, "SkeletalMeshAsset", asset)) ue::Get(c, "StaticMesh", asset);
        ue::UObject* parent = nullptr;
        ue::Get(c, "AttachParent", parent);
        ue::UObject* leader = nullptr;
        ue::Get(c, "LeaderPoseComponent", leader);
        bool vis = false;
        CallRet(c, "IsVisible", &vis, 1);
        uint8_t mode = 255;
        ue::UObject* ai = nullptr;
        if (ue::FindFunction(c, "GetAnimationMode")) {
            CallRet(c, "GetAnimationMode", &mode, 1);
            CallRet(c, "GetAnimInstance", &ai, sizeof(ai));
        }
        ue::UObject* animCls = nullptr;
        ue::Get(c, "AnimClass", animCls);
        net::Log("pupdump: [%d]%s %s %s asset=%s vis=%d parent=%s leader=%s mode=%d ai=%s animClass=%s",
                 i, c == drive ? " DRIVE" : "", cn.c_str(), ue::GetName(c).c_str(), Obj(asset).c_str(),
                 vis ? 1 : 0, Obj(parent).c_str(), Obj(leader).c_str(), (int)mode,
                 ai ? ue::GetClassName(ai).c_str() : "-", Obj(animCls).c_str());
        if (asset || c == drive) {
            ue::FVector loc{}, scale{};
            CallRet(c, "K2_GetComponentLocation", &loc, sizeof(loc));
            CallRet(c, "K2_GetComponentScale", &scale, sizeof(scale));
            ue::FName sock{};
            ue::Get(c, "AttachSocketName", sock);
            net::Log("pupdump:      world=(%.0f %.0f %.0f) scale=(%.2f %.2f %.2f) socket=%s hiddenInGame=%d "
                     "registered=%d",
                     loc.X, loc.Y, loc.Z, scale.X, scale.Y, scale.Z, ue::ToString(sock).c_str(),
                     ue::GetBool(c, "bHiddenInGame") ? 1 : 0, ue::GetBool(c, "bRegistered") ? 1 : 0);
            // [R8-APPEAR] the SLOT MATERIALS.  A skin is a material swap on the body
            // (research/30-player-look.md 3.5), so "did the peer's skin arrive" is
            // answered here and nowhere else in this dump: compare the drive mesh's
            // mats= on both clients.  GetNumMaterials/GetMaterial(i) are the engine's
            // own accessors (UMeshComponent).
            std::string mats;
            int32_t nmats = 0;
            if (ue::FindFunction(c, "GetNumMaterials")) {
                CallRet(c, "GetNumMaterials", &nmats, sizeof(nmats));
                for (int32_t mi = 0; mi < nmats && mi < 16; ++mi) {
                    ue::UFunction* fm = ue::FindFunction(c, "GetMaterial");
                    if (!fm) break;
                    ue::Params pm(fm);
                    pm.Set<int32_t>("ElementIndex", mi);
                    if (!ue::Call(c, fm, pm.data())) break;
                    ue::UObject* m = pm.Get<ue::UObject*>("ReturnValue");
                    if (!mats.empty()) mats += ",";
                    mats += m && ue::PlausiblePtr(m) ? ue::GetName(m) : "-";
                }
            }
            net::Log("pupdump:      mats=%d [%s]", nmats, mats.c_str());
        }
    }
    // Gear may live on separate actors attached to the pawn (not components of it).
    if (ue::UFunction* fa = ue::FindFunction(pup, "GetAttachedActors")) {
        ue::Params q(fa);
        q.Set<bool>("bResetArray", true);
        q.Set<bool>("bRecursivelyIncludeAttachedActors", true);
        ue::Call(pup, fa, q.data());
        const auto acts = q.Get<ue::TArray<ue::UObject*>>("OutActors");
        net::Log("pupdump: attached actors=%d", acts.Num);
        for (int32_t i = 0; i < acts.Num && i < 32; ++i) {
            ue::UObject* a = nullptr;
            if (!ue::ReadMem(acts.Data + i, &a, sizeof(a)) || !net::Valid(a)) continue;
            net::Log("pupdump:   actor[%d] %s %s hidden=%d", i, ue::GetClassName(a).c_str(),
                     ue::GetName(a).c_str(), ue::GetBool(a, "bHidden") ? 1 : 0);
        }
    }
}

// pupcheck -- the verdict that looks at what is RENDERED, not at what was sent:
// the puppet is visible, its drive mesh is in single-node mode with a live anim
// instance, and every weapon static mesh on it has a real asset and a non-flat
// scale.  Writes result_pupvis.txt (PASS/FAIL ...).  Round 5: the old verdicts
// passed while the puppet stood in its reference pose with flattened weapons.
void PupCheck() {
    ue::UObject* pup = puppet::Actor();
    ue::UObject* mesh = puppet::Mesh();
    std::string why;
    int weapons = 0, flat = 0, unsocketed = 0;
    uint8_t mode = 255;
    ue::UObject* ai = nullptr;
    const bool pupOk = net::Valid(pup) && !ue::GetBool(pup, "bHidden");
    if (!pupOk) why += " puppet-missing-or-hidden";
    if (net::Valid(mesh)) {
        CallRet(mesh, "GetAnimationMode", &mode, 1);
        CallRet(mesh, "GetAnimInstance", &ai, sizeof(ai));
    }
    if (mode != 1) why += " animmode=" + std::to_string((int)mode);
    if (!ai || !ue::PlausiblePtr(ai)) why += " no-anim-instance";
    if (pupOk) {
        ue::UFunction* f = ue::FindFunction(pup, "K2_GetComponentsByClass");
        ue::UObject* cls = ue::FindObject("/Script/Engine.StaticMeshComponent");
        if (f && cls) {
            ue::Params p(f);
            p.Set<ue::UObject*>("ComponentClass", cls);
            ue::Call(pup, f, p.data());
            const auto arr = p.Get<ue::TArray<ue::UObject*>>("ReturnValue");
            for (int32_t i = 0; i < arr.Num && i < 64; ++i) {
                ue::UObject* c = nullptr;
                if (!ue::ReadMem(arr.Data + i, &c, sizeof(c)) || !net::Valid(c)) continue;
                ue::UObject* asset = nullptr;
                ue::Get(c, "StaticMesh", asset);
                if (!asset) continue;
                ++weapons;
                ue::FVector s{};
                CallRet(c, "K2_GetComponentScale", &s, sizeof(s));
                if (s.X < 0.01 || s.Y < 0.01 || s.Z < 0.01) ++flat;
                ue::FName sk{};
                ue::Get(c, "AttachSocketName", sk);
                if (!sk.ComparisonIndex) ++unsocketed;   // fell off its socket (see appear Pins)
            }
        }
    }
    const int expected = (int)state().appear.weaponComps.size();
    if (expected > 0 && weapons < expected) why += " weapons=" + std::to_string(weapons) + "/" + std::to_string(expected);
    if (flat > 0) why += " flat=" + std::to_string(flat);
    if (unsocketed > 0) why += " unsocketed=" + std::to_string(unsocketed);
    const std::string line = net::Fmt("%s puppet render: visible=%d animmode=%d animinstance=%s weapons=%d/%d flat=%d%s\n",
                                      why.empty() ? "PASS" : "FAIL", pupOk ? 1 : 0, (int)mode,
                                      ai ? ue::GetClassName(ai).c_str() : "-", weapons, expected, flat,
                                      why.empty() ? "" : (" --" + why).c_str());
    net::WriteFile("result_pupvis.txt", line);
    net::Log("pupcheck: %s", line.c_str());
}

// [R9-AREA] begin -- `asclist [filter]` answers "which attack ability does this
// client actually have granted": every live gameplay-ability INSTANCE in the world
// with its class and whether its avatar is our pawn (a FindAllOf walk -- no
// FGameplayAbilitySpec struct arithmetic, which would need an unproven struct size).
// The swing itself (`attack`) and its watch live in coop_realarea.cpp, next to the
// area probes, because the watch needs a 15 Hz tick and that file owns it.

void AscList(const std::string& arg) {
    ue::UObject* pawn = net::Pawn();
    const std::vector<ue::UObject*> list = ue::FindAllOf("GameplayAbility");
    int mine = 0;
    for (ue::UObject* a : list) {
        if (!net::Valid(a)) continue;
        const std::string cls = ue::GetClassName(a);
        if (!arg.empty() && cls.find(arg) == std::string::npos) continue;
        ue::UObject* avatar = nullptr;
        if (ue::UFunction* f = ue::FindFunction(a, "GetAvatarActorFromActorInfo")) {
            ue::Params p(f);
            if (ue::Call(a, f, p.data())) avatar = p.Get<ue::UObject*>("ReturnValue");
        }
        const bool own = avatar == pawn;
        if (own) ++mine;
        net::Log("asclist: %s avatar=%s%s", cls.c_str(),
                 net::Valid(avatar) ? ue::GetClassName(avatar).c_str() : "-",
                 own ? " [MINE]" : "");
    }
    net::Log("asclist: %d live ability instance(s)%s%s, %d on our pawn", (int)list.size(),
             arg.empty() ? "" : " matching '", arg.c_str(), mine);
}

// [R9-AREA] end

}  // namespace probe
}  // namespace coop
