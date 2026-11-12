# 30 — Player look: shells, meshes, weapons, sockets, armour, cosmetics, visual state

Mortal Shell II, UE 5.6 shipping build, UE4SS Lua modding.
Evidence base: `research\raw\index.sqlite3` (index of `ObjectDump_allassets.txt`, 2 153 654 rows)
and `research\raw\CXXHeaderDump\*.hpp` (10 415 UE4SS CXX headers).

Scope of this file: **how a player looks** — shells (playable bodies), skeletal meshes, weapons,
sockets, armour/skin/cosmetic visuals, glow/VFX, harden (stone-form) and health/resolve visual state.
World state, quests, abilities/actions, enemies/bosses, items, menus and the animation catalogue are
covered by other files.

Reader: an engineer writing the co-op sync code. Everything below is either a path/property copied
from the dump, or explicitly labelled `GUESS:` / `UNKNOWN`.

---

## 0. Method, conventions, and the one hard limitation

### 0.1 Tools used

All queries run from `G:\Freespace\shell_coop\research`:

```
python scripts\parse_dump.py raw    raw\index.sqlite3 <path-regex>    # verbatim dump lines (evidence)
python scripts\parse_dump.py classes raw\index.sqlite3 --regex <re>   # class + parent
python scripts\parse_dump.py show   raw\index.sqlite3 <path-substr>   # members with offsets
python scripts\q.py raw\index.sqlite3 scripts\q\pl-*.sql             # raw SQL
```

Helper SQL files written for this report (all read-only over `index.sqlite3`):

| File | Query |
|---|---|
| `scripts\q\pl-shells.sql` | object-type histogram under `/Game/Sparta/Characters/Shells/` |
| `scripts\q\pl-skmeshes.sql` | SkeletalMesh / Skeleton / PhysicsAsset / PoseAsset under `Shells/` |
| `scripts\q\pl-shellclasses.sql` | Blueprint + AnimBlueprint classes under `Shells/` |
| `scripts\q\pl-shellitems.sql`, `pl-shellitemtypes.sql`, `pl-shellfrag.sql`, `pl-graguitem.sql` | `/Game/Sparta/Items/Shells/` inventory + `ItemFragment_SetShellClass` subobjects |
| `scripts\q\pl-shellcd.sql`, `pl-chardata.sql` | every `SpartaCharacterData` asset |
| `scripts\q\pl-playerclasses.sql`, `pl-shellmeshcomp.sql`, **`pl-shellparents.sql`** | `Core/Characters/Player` classes, per-shell `CharacterMesh0`, exact parent of every shell/dark-form pawn BP |
| `scripts\q\pl-native.sql` | native `/Script/Sparta.*` shell/mesh/weapon/armour/VFX classes |
| `scripts\q\pl-setshell.sql`, `pl-changeshell.sql`, `pl-bpfl.sql` | every `SetShell`/`ChangeShell`/`SetShellItem`/skin function + parameters |
| `scripts\q\pl-weaponclasses.sql`, `pl-wp-hier.sql`, `pl-weaponcdo.sql`, `pl-weaponcdo2.sql`, `pl-wpcomps.sql`, `pl-weaponmeshes.sql`, `pl-playerweaponsm.sql`, `pl-weaponitems.sql` | player weapons, hierarchy, CDO components, meshes, items |
| `scripts\q\pl-sockets.sql`, `pl-smsockets.sql`, `pl-socketprops.sql`, `pl-weaponsockets.sql`, `pl-counts.sql` | every socket object and socket-named property |
| `scripts\q\pl-armor.sql`, `pl-custom.sql`, `pl-vfx.sql`, `pl-dataassets.sql`, `pl-enums.sql`, `pl-variant.sql` | armour/cosmetic/glow/harden/VFX assets + user enums |

### 0.2 Conventions

* Paths are copied byte-for-byte from the dump. A Blueprint class appears as `.../Name.Name_C`
  (class suffix `_C`); a native class as `/Script/Sparta.Name`; an asset as `Package/Object.Object`.
* Offsets are the dump's `[o: HEX]` values (member offsets inside the owning class/struct).
* Blueprint functions carry compiler temporaries. **All `Temp_`, `CallFunc_`, `K2Node_`,
  `__Internal_`, `ExecuteUbergraph_`, `Goto_`, `eb_`, `bpf_` parameters are filtered out of the
  function tables below** (documented rule, not a guess — see `QUERYING.md`). Where a filtered
  `CallFunc_*` parameter is itself informative (e.g. it names the exact native call the Blueprint
  makes) it is quoted in the Notes column.
* Where the CXX header gives a cleaner declaration order than the dump, the header is cited as
  `Sparta.hpp:NNNN` / `<Class>.hpp:NN`.

### 0.3 HARD LIMITATION — asset property **values** are not in the dump

The UE4SS object dump enumerates **objects** (classes, functions, properties, assets), never
UPROPERTY *values*. Therefore:

* You cannot read `USpartaCharacterData::DefaultMaterials[0]` or `ASpartaWeapon::WeaponVariants[0].Mesh`
  from the dump.
* You cannot read a mesh's Skeleton, its MaterialInstance list, or a `SkeletalMeshSocket`'s **name**.
  FName payloads are only referenced as a name-pool index (`[n: 4781AC]`), not decoded.

Everything below is stated at the level the dump can prove. Where a link is structural rather than
declared (e.g. "this SK_ mesh is Gragu's body"), it is marked `GUESS:`.

**The one runtime-probe log used.** `research\runs\20261022-061848-ingame\meshinfo.txt` and
`shellinfo.txt` were produced by a Lua probe during an earlier gameplay session (not by dump
analysis). They are quoted verbatim in §2.7 and §3.9 because they are the *only* source in the
workspace that resolves a `CharacterMesh0` component to an actual `SK_` asset and an `ABP_` class.
They are labelled as probe output wherever used, and nothing else in this report depends on them.

---

## 1. Native classes (`/Script/Sparta`)

### 1.1 The class chain

| Class | Parent | Role |
|---|---|---|
| `/Script/Sparta.SpartaCharacter` | `/Script/CSCharacter.CSCharacter` | base character: health, items, weapons, hit-trace, armour, audio, VFX hooks |
| `/Script/Sparta.SpartaPlayerCharacter` | `/Script/Sparta.SpartaCharacter` | adds `LockOnComponent`, `PlayerExpComponent`, `TarstoneComponent`, `ArtifactComponent`, `SidearmProjectileCacheComponent`, `PathFollowingComponent`, `NavigationInvokerComponent`, `DefaultInputMappingContexts`, **`DarkformClass`**, **`ShellClass`** |
| `/Script/Sparta.SpartaAICharacter` | `/Script/Sparta.SpartaCharacter` | enemy body |
| `/Script/Sparta.SpartaCharacterData` | `/Script/Engine.DataAsset` | the per-character data asset (materials, skins, mesh variants) — `CD_*` assets |
| `/Script/Sparta.SpartaMeshCustomisation` | `/Script/Engine.DataAsset` | body/armour-piece variation templates — `VAR_*` assets |
| `/Script/Sparta.SpartaSkeletalMeshComponent` | `/Script/AnimationBudgetAllocator.SkeletalMeshComponentBudgeted` | **the mesh component every shell uses** (instanced as `CharacterMesh0`) |
| `/Script/Sparta.SpartaWeapon` | `/Script/Engine.Actor` | base weapon actor |
| `/Script/Sparta.WeaponsComponent` | `/Script/Engine.ActorComponent` | weapon-slot inventory on the character |
| `/Script/Sparta.SpartaWeaponComponent_Base` / `_Melee` / `_Ranged` / `_Effect` | `UActorComponent` / `_Base` | per-weapon melee/ranged/effects sub-components |
| `/Script/Sparta.ArmorComponent` | `/Script/Engine.ActorComponent` | current armour gameplay tag (visual + hit-reaction selector) |
| `/Script/Sparta.ArtifactComponent` | `/Script/Engine.ActorComponent` | artifact upgrade tables |
| `/Script/Sparta.SpartaVFXComponent` | `/Script/Engine.ActorComponent` | blood/substance masks, mesh-access helper |
| `/Script/Sparta.SpartaCharacterVFXData` | `/Script/Engine.DataAsset` | per-character VFX data (blood colour) |
| `/Script/Sparta.SpartaWeaponVFXData` | `/Script/Engine.DataAsset` | per-weapon VFX data keyed by `FGameplayTag` |
| `/Script/Sparta.CharacterAnimationData` | `/Script/Sparta.TypeSpecificData` | per-character anim set selection |
| `/Script/Sparta.ItemFragment_SetShellClass` | `/Script/Sparta.SpartaItemFragment` | item fragment that names the shell class |
| `/Script/Sparta.ItemFragment_SetDarkformClass` | `/Script/Sparta.SpartaItemFragment` | item fragment that names the dark-form class |

Evidence:

```
[00007FF46F055738] Class /Script/Sparta.SpartaPlayerCharacter [n: 49F82] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F03C538]
[00007FF46F043538] Class /Script/Sparta.SpartaCharacterData [n: 48C34] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[00007FF46F04FF38] Class /Script/Sparta.SpartaMeshCustomisation [n: 49AD0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[00007FF46F05CD38] Class /Script/Sparta.SpartaWeapon [n: 4AAF0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
```

Header equivalents: `Sparta.hpp:1755` (`ASpartaCharacter : public ACSCharacter`),
`Sparta.hpp:2014` (`ASpartaPlayerCharacter : public ASpartaCharacter`),
`Sparta.hpp:4448` (`USpartaCharacterData : public UDataAsset`),
`Sparta.hpp:6163` (`USpartaMeshCustomisation : public UDataAsset`),
`Sparta.hpp:7144` (`USpartaSkeletalMeshComponent : public USkeletalMeshComponentBudgeted`),
`Sparta.hpp:2176` (`ASpartaWeapon : public AActor`),
`Sparta.hpp:8250` (`UWeaponsComponent : public UActorComponent`).

### 1.2 `ASpartaPlayerCharacter` — properties (full list, `Sparta.hpp:2014-2034`)

| Property | Type | Offset | Notes |
|---|---|---|---|
| `LockOnComponent` | `USpartaTargetLockComponent*` | `0x0AE8` | |
| `PlayerExpComponent` | `UPlayerExpComponent*` | `0x0AF0` | |
| `TarstoneComponent` | `UTarstoneComponent*` | `0x0AF8` | |
| `ArtifactComponent` | `UArtifactComponent*` | `0x0B00` | |
| `SidearmProjectileCacheComponent` | `USidearmProjectileCacheComponent*` | `0x0B08` | |
| `PathFollowingComponent` | `USpartaPathFollowingComponent*` | `0x0B10` | |
| `NavigationInvokerComponent` | `UNavigationInvokerComponent*` | `0x0B18` | |
| `DefaultInputMappingContexts` | `TArray<UInputMappingContext*>` | `0x0B20` | |
| **`DarkformClass`** | `TSubclassOf<ASpartaPlayerCharacter>` | `0x0B30` | dark-form pawn class |
| **`ShellClass`** | `TSubclassOf<ASpartaPlayerCharacter>` | `0x0B38` | shell pawn class |

Functions: `GrantDefaultItemsToShell(Shell: ASpartaPlayerCharacter*)`, `GetSpartaInputComponent()`,
`GetRawMovementInputInWorldSpace()`, `GetRawMovementInput()`, `GetRawLookInputInWorldSpace()`,
`GetRawLookInput()`, `GetPlayerLevel()`.

Evidence (dump, with `[mc:]` = meta class):

```
[000001F4ACDBC780] ClassProperty /Script/Sparta.SpartaPlayerCharacter:DarkformClass [o: B30] [n: 586201] [c: 00007FF721DE8E20] [owr: 00007FF46F055738] [mc: 00007FF46F055738]
[000001F4ACDBC800] ClassProperty /Script/Sparta.SpartaPlayerCharacter:ShellClass [o: B38] [n: 586209] [c: 00007FF721DE8E20] [owr: 00007FF46F055738] [mc: 00007FF46F055738]
```

### 1.3 `USpartaCharacterData` — properties (full list, `Sparta.hpp:4448-4489`, size `0x480`)

The **shell definition data asset**. Visual fields are bolded.

| Property | Type | Offset | Notes |
|---|---|---|---|
| `CharacterName` | `FText` | `0x0030` | |
| `CharacterId` | `FGameplayTag` | `0x0040` | the shell/dark-form id tag |
| `bIsBoss` / `bIsMainBoss` / `bShouldTrackDeath` | `bool` | `0x0048/49/4A` | |
| `DefaultTags` | `FGameplayTagContainer` | `0x0050` | |
| `MainRegion` | `FGameplayTagContainer` | `0x0070` | |
| **`ArmorTag`** | `FGameplayTag` | `0x0090` | default armour set for `UArmorComponent` |
| `AITicketCost` | `int32` | `0x0098` | |
| `PreAggroBehaviors` / `EngagementBehaviors` / `DisengagementBehaviors` / `DeathBehaviors` | `TArray<USpartaCharacterExecutableBehavior*>` | `0x00A0/B0/C0/D0` | |
| `CharacterScaleOverride` | `FVector` | `0x00E0` | |
| **`WeaponOverrides`** | `TArray<TSubclassOf<ASpartaWeapon>>` | `0x00F8` | per-character default weapon classes |
| `UpgradeStatValues` | `TMap<FGameplayTag,float>` | `0x0108` | |
| `CharacterCurves` | `TMap<FGameplayTag,UCurveFloat*>` | `0x0158` | |
| `BehaviorTreeOverride` | `UBehaviorTree*` | `0x01A8` | |
| `GrantedAbilitySet` | `FSpartaAbilitySet` | `0x01B0` | |
| `AttributeData` | `TArray<FAttributeDefaults>` | `0x01E0` | |
| `Movement` | `FSpartaCharacterMovementData` | `0x01F0` | |
| `BaseDamage` | `FSpartaCharacterDamageData` | `0x0340` | |
| `WarpingData` | `FSpartaCharacterWarpingData` | `0x0350` | |
| `DeathResources` | `FSpartaCharacterDeathData` | `0x0358` | |
| `Dismemberment` | `FSpartaCharacterDismembermentData` | `0x03B8` | |
| `PhysicalHitReactionData` | `USpartaPhysicalHitReactionData*` | `0x03C0` | |
| `AudioData` | `USpartaCharacterAudioData*` | `0x03C8` | |
| **`VFXData`** | `USpartaCharacterVFXData*` | `0x03D0` | blood colour etc. |
| `StatusEffectOverlayPolicy` | `EStatusEffectOverlayPolicy` | `0x03D8` | `Sparta_enums.hpp:709` |
| **`DefaultMaterials`** | `TArray<UMaterialInstance*>` | `0x03E0` | base body materials |
| **`EffectMaterials`** | `TArray<UMaterialInstance*>` | `0x03F0` | effect-state materials |
| **`GhostMaterials`** | `TArray<UMaterialInstance*>` | `0x0400` | ghost/afterimage materials |
| **`SkinMaterialsMap`** | `TMap<FGameplayTag, FSkinMaterials>` | `0x0410` | skin tag → {Default, Effect, Ghost} |
| **`MeshVariationData`** | `USpartaMeshCustomisation*` | `0x0460` | `VAR_*` data asset |
| `TypeSpecificData` | `TArray<UTypeSpecificData*>` | `0x0468` | holds `CharacterAnimationData` etc. |
| **`CharacterAnimSet`** | `UCSAnimSet*` | `0x0478` | anim set |

Functions: `InitializeCharacterData(ASpartaCharacter* SpartaCharacter)`,
`GetData(TSubclassOf<UTypeSpecificData> Class)`.

Evidence:

```
[000001F4ACD9E180] ArrayProperty /Script/Sparta.SpartaCharacterData:DefaultMaterials [o: 3E0] [n: 587601] [c: 00007FF721DE8910] [owr: 00007FF46F043538] [ai: 000001F4ACD9E100]
[000001F49CD2E0C0] MapProperty /Script/Sparta.SpartaCharacterData:SkinMaterialsMap [o: 410] [n: 5875DC] [c: 00007FF721DE8F70] [owr: 00007FF46F043538] [kp: 000001F4ACD9E480] [vp: 000001F4ACD9E400]
[000001F4ACD9E500] ObjectProperty /Script/Sparta.SpartaCharacterData:MeshVariationData [o: 460] [n: 5875D2] [c: 00007FF721DE9420] [owr: 00007FF46F043538] [pc: 00007FF46F04FF38]
```

### 1.4 Related structs (visual)

| Struct | Fields (name: type @ offset) | Header |
|---|---|---|
| `FSkinMaterials` | `Default: TArray<UMaterialInstance*> @0x00`, `Effect: TArray<UMaterialInstance*> @0x10`, `Ghost: TArray<UMaterialInstance*> @0x20` | `Sparta.hpp:337` |
| `FSpartaCustomisationTemplate` | `SkeletalMeshCustomisations @0x00`, `StaticMeshCustomisations @0x10`, `MaterialCustomisations @0x20` | `Sparta.hpp:658` |
| `FSpartaSkeletalMeshCustomisation` | `bReplaceMainMesh: bool @0x00`, `ComponentName: FName @0x04`, `WeightedSkeletalMeshes: TArray<FSpartaWeightedSkeletalMesh> @0x10` | `Sparta.hpp:1313` |
| `FSpartaStaticMeshCustomisation` | `ComponentName: FName @0x00`, `ParentBone: FName @0x08`, `WeightedStaticMeshes: TArray<FSpartaWeightedStaticMesh> @0x10` | `Sparta.hpp:1331` |
| `FSpartaMaterialCustomisation` | `Index: int32 @0x00`, `WeightedMaterials: TArray<FSpartaWeightedMaterial> @0x08` | `Sparta.hpp:1040` |
| `FWeaponVariantData` | `Mesh: UStaticMesh* @0x00`, `bOverrideStowedSocket: bool @0x08`, `StowedSocketName: FName @0x0C`, `bOverrideInHandSocket: bool @0x14`, `InHandSocketName: FName @0x18`, `bOverrideCapsuleCollisionSize: bool @0x20`, `CapsuleCollisionHalfHeight: float @0x24`, `CapsuleCollisionRadius: float @0x28` | `Sparta.hpp:1620` |
| `FShellSkillSet` | `Skills: TArray<TSoftClassPtr<USpartaItemDefinition>> @0x00` | `Sparta.hpp:331` |
| `FSpartaCharacterShellDefaults` | `DefaultItems: TArray<TSubclassOf<USpartaItemDefinition>> @0x00`, `DefaultAbilities: TMap<TSoftClassPtr<UGameplayAbility>, FGameplayTag> @0x10` | `Sparta.hpp:564` |
| `FPlayerUpgrade` | `… GlowColor: FLinearColor @0x0030 …` | `Sparta.hpp:230` |

Evidence:

```
[00007FF46F488C78] ScriptStruct /Script/Sparta.SkinMaterials [n: 13AFF] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 0000000000000000]
[000001F4A8873E00] ArrayProperty /Script/Sparta.SkinMaterials:Default [o: 0] [n: 2F7] [c: 00007FF721DE8910] [owr: 00007FF46F488C78] [ai: 000001F4A8873D80]
[000001F4A8874000] ArrayProperty /Script/Sparta.SkinMaterials:Ghost [o: 20] [n: 315CA] [c: 00007FF721DE8910] [owr: 00007FF46F488C78] [ai: 000001F4A8873F80]
[00007FF46F48CC38] ScriptStruct /Script/Sparta.WeaponVariantData [n: 14466] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 0000000000000000]
[000001F4A887B380] ObjectProperty /Script/Sparta.WeaponVariantData:Mesh [o: 0] [n: 16848] [c: 00007FF721DE9420] [owr: 00007FF46F48CC38] [pc: 00007FF46EF64338]
[000001F4A8981820] NameProperty /Script/Sparta.WeaponVariantData:StowedSocketName [o: C] [n: 55D000] [c: 00007FF721DE90B0] [owr: 00007FF46F48CC38]
```

---

## 2. Shells (playable bodies)

### 2.1 What "a shell" is, in data terms

A shell is **four** linked objects:

1. an **item definition** `ID_Shell_*_C` under `/Game/Sparta/Items/Shells/`, carrying an
   `ItemFragment_SetShellClass` subobject (§2.2) — this is what the player picks in the inventory;
2. a **pawn Blueprint** `BP_Shell_*_C` under `/Game/Sparta/Core/Characters/Player/<Shell>/`
   (parent chain `BP_Shell_Base_C` → `BP_PlayerCharacter_C` → `ASpartaPlayerCharacter`);
3. a **`SpartaCharacterData` asset** `CD_*` (§2.4) that supplies materials / skins / anim set;
4. a **`SK_*` skeletal mesh** + an **`ABP_*` AnimBlueprint** (§2.5).

The dump proves (1)(2)(3)(4) exist and proves class parents, but does **not** expose the
cross-links as values — except for item → fragment, which is a named subobject.

### 2.2 Shell item definitions — every `ItemFragment_SetShellClass` in the build (35)

`python scripts\q.py raw\index.sqlite3 scripts\q\pl-shellfrag.sql` — complete list, no sampling.

| Item definition (`/Game/Sparta/Items/Shells/…`) | Fragment subobject |
|---|---|
| `ID_Shell_Eredrim.ID_Shell_Eredrim_C` | `Default__ID_Shell_Eredrim_C:ItemFragment_SetShellClass_0` |
| `ID_Shell_Genessa.ID_Shell_Genessa_C` | `Default__ID_Shell_Genessa_C:ItemFragment_SetShellClass_0` |
| `ID_Shell_Genessa_Memory.ID_Shell_Genessa_Memory_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Gragu.ID_Shell_Gragu_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Gragu_Helmetless.ID_Shell_Gragu_Helmetless_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Harros.ID_Shell_Harros_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_KnightLady.ID_Shell_KnightLady_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Lazlo_NoMask.ID_Shell_Lazlo_NoMask_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Necrophage.ID_Shell_Necrophage_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Necrophage_Child.ID_Shell_Necrophage_Child_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Necrophage_YoungAdult.ID_Shell_Necrophage_YoungAdult_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Smert.ID_Shell_Smert_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Solomon.ID_Shell_Solomon_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Thorn.ID_Shell_Thorn_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Thorn_Memory.ID_Shell_Thorn_Memory_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Tiel.ID_Shell_Tiel_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Tiel_Memory.ID_Shell_Tiel_Memory_C` | `…:ItemFragment_SetShellClass_0` |
| `ID_Shell_Tiel_WithFace_Memory.ID_Shell_Tiel_WithFace_Memory_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Eredrim_AltSkin.ID_Shell_Eredrim_AltSkin_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Eredrim_Obsidian.ID_Shell_Eredrim_Obsidian_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Genessa_AltSkin.ID_Shell_Genessa_AltSkin_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Genessa_Obsidian.ID_Shell_Genessa_Obsidian_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Gragu_AltSkin.ID_Shell_Gragu_AltSkin_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Gragu_Obsidian.ID_Shell_Gragu_Obsidian_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Harros_Obsidian.ID_Shell_Harros_Obsidian_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_KnightLady_AltSkin.ID_Shell_KnightLady_AltSkin_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_KnightLady_Obsidian.ID_Shell_KnightLady_Obsidian_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Necrophage_AltSkin.ID_Shell_Necrophage_AltSkin_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Necrophage_Obsidian.ID_Shell_Necrophage_Obsidian_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Smert_AltSkin.ID_Shell_Smert_AltSkin_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Smert_Obsidian.ID_Shell_Smert_Obsidian_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Thorn_AltSkin.ID_Shell_Thorn_AltSkin_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Thorn_Obsidian.ID_Shell_Thorn_Obsidian_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Tiel_AltSkin.ID_Shell_Tiel_AltSkin_C` | `…:ItemFragment_SetShellClass_0` |
| `Skins/ID_Shell_Tiel_Obsidian.ID_Shell_Tiel_Obsidian_C` | `…:ItemFragment_SetShellClass_0` |

Plus 3 further shell items with **no** `SetShellClass` fragment: `ID_Shell_LoadFromSave.ID_Shell_LoadFromSave_C`,
`ID_ShellPickupBase.ID_ShellPickupBase_C` (`Pickups/`), `ID_Shell_Genessa_Memory` counted above.

Evidence:

```
[000001F8CEEE9FD0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Gragu.ID_Shell_Gragu_C [n: 385CEF] [c: 00007FF46EEC3D38] [or: 000001F8CF1B8580] [sps: 000001F76D3E4330]
[000001F8CF1B88F0] ItemFragment_SetShellClass /Game/Sparta/Items/Shells/ID_Shell_Gragu.Default__ID_Shell_Gragu_C:ItemFragment_SetShellClass_0 [n: 47AAD] [c: 00007FF46F036538] [or: 000001F7ED6260D0]
[00007FF46F036538] Class /Script/Sparta.ItemFragment_SetShellClass [n: 47AAD] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F035138]
```

The `[c:]` of the fragment subobject is the fragment **class** `00007FF46F036538` =
`/Script/Sparta.ItemFragment_SetShellClass`, which is why `pl-shellfrag.sql` can enumerate the 35
shell items without reading any property value.

`UItemFragment_SetShellClass` itself:

| Class | Property | Type | Offset |
|---|---|---|---|
| `/Script/Sparta.ItemFragment_SetShellClass` | `ShellClass` | `TSoftClassPtr<ASpartaPlayerCharacter>` | `0x0028` |
| `/Script/Sparta.ItemFragment_SetDarkformClass` | `DarkformClass` | `TSoftClassPtr<ASpartaPlayerCharacter>` | `0x0028` |

```
[000001F4ACD5D980] SoftClassProperty /Script/Sparta.ItemFragment_SetShellClass:ShellClass [o: 28] [n: 586209] [c: 00007FF721DF97A0] [owr: 00007FF46F036538] [mc: 00007FF46F055738]
```

Neither fragment has any function — they are pure data holders.

### 2.3 Dark-form item definitions — every `ItemFragment_SetDarkformClass` (9)

| Item definition | Fragment subobject |
|---|---|
| `/Game/Sparta/Items/Shells/ID_Darkform_CorruptedGenessa.ID_Darkform_CorruptedGenessa_C` | `Default__ID_Darkform_CorruptedGenessa_C:ItemFragment_SetDarkformClass_0` |
| `/Game/Sparta/Items/Shells/ID_Darkform_Eggman.ID_Darkform_Eggman_C` | `…:ItemFragment_SetDarkformClass_0` |
| `/Game/Sparta/Items/Shells/ID_Darkform_StrongOne.ID_Darkform_StrongOne_C` | `…:ItemFragment_SetDarkformClass_0` |
| `/Game/Sparta/Items/Shells/ID_Darkform_StrongOne_Cosmic.ID_Darkform_StrongOne_Cosmic_C` | `…` |
| `/Game/Sparta/Items/Shells/ID_Darkform_StrongOne_Obsidian.ID_Darkform_StrongOne_Obsidian_C` | `…` |
| `/Game/Sparta/Items/Shells/ID_Darkform_StrongOne_Red.ID_Darkform_StrongOne_Red_C` | `…` |
| `/Game/Sparta/Items/Shells/ID_Darkform_StrongOne_Robe.ID_Darkform_StrongOne_Robe_C` | `…` |
| `/Game/Sparta/Items/Shells/ID_Darkform_StrongOne_Skeleton.ID_Darkform_StrongOne_Skeleton_C` | `…` |
| `/Game/Sparta/Items/Shells/ID_Darkform_TarredCorpse.ID_Darkform_TarredCorpse_C` | `…:ItemFragment_SetDarkformClass_0` |

Also present: `ID_DarkForm_LoadFromSave.ID_DarkForm_LoadFromSave_C` (no fragment).

### 2.4 Shell pawn Blueprints — every `BP_Shell_*` / `BP_Darkform_*` class (36)

`scripts\q\pl-shellparents.sql` (parent join over `cls`/`obj.super`) — complete list.
All under `/Game/Sparta/Core/Characters/Player/`.

| Blueprint class | Parent | Kind |
|---|---|---|
| `Common/BP_Shell_Base.BP_Shell_Base_C` | `/Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C` | **the shell base — every playable shell and every dark form derives from it** |
| `Common/BP_Shell_Decor.BP_Shell_Decor_C` | `/Script/Engine.Actor` | decorative corpse/shell prop |
| `Common/BP_Shell_Decoy.BP_Shell_Decoy_C` | `/Script/Sparta.SpartaCharacter` | decoy |
| `Common/BP_Decoy_Bait.BP_Decoy_Bait_C` | `…Common/BP_Shell_Decoy.BP_Shell_Decoy_C` | decoy bait |
| `Common/BP_Shell_Dummy_Grabbed.BP_Shell_Dummy_Grabbed_C` | `/Script/Sparta.SpartaCharacter` | grabbed dummy |
| `Common/BP_Shell_Spawnable.BP_Shell_Spawnable_C` | `/Script/Sparta.SpartaCharacter` | spawnable shell |
| `Common/BP_Shell_Spawnable_Ghost.BP_Shell_Spawnable_Ghost_C` | `…BP_Shell_Spawnable_C` | ghost variant |
| `Common/BP_Shell_Spawnable_Traversal.BP_Shell_Spawnable_Traversal_C` | `…BP_Shell_Spawnable_C` | traversal variant |
| `Common/BP_Shell_Summon.BP_Shell_Summon_C` | `/Script/Sparta.SpartaAICharacter` | summoned AI shell |
| `Common/BP_DarkForm_Spawnable.BP_DarkForm_Spawnable_C` | `/Script/Sparta.SpartaCharacter` | dark-form spawnable |
| `CorruptedGenessa/BP_Shell_CorruptedGenessa.BP_Shell_CorruptedGenessa_C` | `…BP_Shell_Base_C` | shell |
| `Eggman/BP_Darkform_Eggman.BP_Darkform_Eggman_C` | `…BP_Shell_Base_C` | dark form |
| `Eredrim/BP_Shell_Eredrim.BP_Shell_Eredrim_C` | `…BP_Shell_Base_C` | **shell** |
| `Genessa/BP_Shell_Genessa.BP_Shell_Genessa_C` | `…BP_Shell_Base_C` | **shell** |
| `Gragu/BP_Shell_Gragu.BP_Shell_Gragu_C` | `…BP_Shell_Base_C` | **shell** |
| `Harros/BP_Shell_Harros.BP_Shell_Harros_C` | `…BP_Shell_Base_C` | **shell** |
| `KnightLady/BP_Shell_KnightLady.BP_Shell_KnightLady_C` | `…BP_Shell_Base_C` | **shell** |
| `Necrophage/BP_Shell_Necrophage.BP_Shell_Necrophage_C` | `…BP_Shell_Base_C` | **shell** |
| `Necrophage/BP_Shell_Necrophage_NoMask.BP_Shell_Necrophage_NoMask_C` | `…BP_Shell_Base_C` | shell variant |
| `Necrophage/BP_Shell_Necrophage_Young.BP_Shell_Necrophage_Young_C` | `…BP_Shell_Base_C` | shell variant |
| `Necrophage/BP_Shell_Necrophage_YoungAdult.BP_Shell_Necrophage_YoungAdult_C` | `…BP_Shell_Base_C` | shell variant |
| `Smert/BP_Shell_Smert.BP_Shell_Smert_C` | `…BP_Shell_Base_C` | **shell** |
| `Solomon/BP_Shell_Solomon.BP_Shell_Solomon_C` | `…BP_Shell_Base_C` | **shell** |
| `StrongOne/BP_Darkform_StrongOne.BP_Darkform_StrongOne_C` | `…BP_Shell_Base_C` | dark form |
| `StrongOne/BP_Darkform_StrongOne_Robe.BP_Darkform_StrongOne_Robe_C` | `…StrongOne/BP_Darkform_StrongOne.BP_Darkform_StrongOne_C` | dark form |
| `StrongOne/BP_Darkform_StrongOne_Skeleton.BP_Darkform_StrongOne_Skeleton_C` | `…StrongOne/BP_Darkform_StrongOne.BP_Darkform_StrongOne_C` | dark form |
| `TarredCorpse/BP_Darkform_TarredCorpse.BP_Darkform_TarredCorpse_C` | `…BP_Shell_Base_C` | dark form |
| `ThornBoi/BP_Shell_Thorn.BP_Shell_Thorn_C` | `…BP_Shell_Base_C` | **shell** |
| `ThornBoi/BP_Shell_Thorn_Memories.BP_Shell_Thorn_Memories_C` | `…BP_Shell_Base_C` | shell (memory scene) |
| `Tiel/BP_Shell_Tiel.BP_Shell_Tiel_C` | `…BP_Shell_Base_C` | **shell** |
| `Tiel/BP_Shell_Tiel_ShadowClone.BP_Shell_Tiel_ShadowClone_C` | `/Script/Sparta.SpartaAICharacter` | clone (AI) |
| `Tiel/BP_Shell_Tiel_WithFace.BP_Shell_Tiel_WithFace_C` | `…Tiel/BP_Shell_Tiel.BP_Shell_Tiel_C` | shell variant |
| `AstralGenessa/BP_Shell_Genessa_AstralBase.BP_Shell_Genessa_AstralBase_C` | `…Genessa/BP_Shell_Genessa.BP_Shell_Genessa_C` | astral clone base |
| `AstralGenessa/BP_Shell_Genessa_AstralClone.BP_Shell_Genessa_AstralClone_C` | `…BP_Shell_Genessa_AstralBase_C` | clone |
| `AstralGenessa/BP_Shell_Genessa_AstralClone_Single.BP_Shell_Genessa_AstralClone_Single_C` | `…BP_Shell_Genessa_AstralClone_C` | clone |
| `AstralGenessa/BP_Shell_Genessa_AstralCopy.BP_Shell_Genessa_AstralCopy_C` | `…BP_Shell_Genessa_AstralBase_C` | copy |

Key structural fact: **all 8 dark-form pawns and all 10 playable shells share the single parent
`BP_Shell_Base_C`** — there is no separate "dark form base". Consequently `InitMeshByID` /
`ShellIDTag` are available on dark forms too.

The common player pawn is at a different path: `/Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C`
(parent `/Script/Sparta.SpartaPlayerCharacter`).

Evidence:

```
[000001F617E634F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C [n: 22444C] [c: 00007FF46EEC3D38] [or: 000001F4D1C170E0] [sps: 00007FF46F055738]
[000001F614CC66D0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C [n: 32515E] [c: 00007FF46EEC3D38] [or: 000001F4D1C16A00] [sps: 000001F617E634F0]
[000001F8ACE83C10] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Gragu/BP_Shell_Gragu.BP_Shell_Gragu_C [n: 21B6FC] [c: 00007FF46EEC3D38] [or: 000001F8B1028E40] [sps: 000001F614CC66D0]
```

### 2.5 `ABP_Shell_Base_C` — the class that actually swaps the body mesh

From `CXXHeaderDump\BP_Shell_Base.hpp` (full):

| Member | Type | Offset |
|---|---|---|
| `UberGraphFrame` | `FPointerToUberGraphFrame` | `0x0D20` |
| `BPC_LockOnTarget` | `UBPC_LockOnTarget_C*` | `0x0D28` |
| `CollisionActivationTimerID` | `FString` | `0x0D30` |
| **`GrantedFromItem`** | `USpartaItemInstance*` | `0x0D40` |
| `WBP_EnemyHealth` | `UWBP_Enemy_Health_C*` | `0x0D48` |
| **`ShellIDTag`** | `FGameplayTag` | `0x0D50` |

Functions of interest (from the same header):

| Function | Parameters |
|---|---|
| **`InitMeshByID`** | `ShellId: FGameplayTag` |
| `LoadWeaponFromID` | – |
| `ClearPrimaryWeapon` | – |
| `ClearSidearm` | – |
| `SetItemInstance` | `ItemInstance: USpartaItemInstance*` |
| `GetItemInstance` | `ItemInstance: USpartaItemInstance&` |
| `InitializePlayerCharacterObject` | – |
| `ClearComponents` | – |
| `ActivateShellCollision` | – |
| `DisableShellCollisionChannel` | `Channel: TEnumAsByte<ECollisionChannel>`, `NewResponse: TEnumAsByte<ECollisionResponse>` |
| `OnItemAdded` / `OnItemRemoved` | `Pawn: APawn*` |
| `ReceiveBeginPlay` / `ReceiveDestroyed` | – |
| `UpdateCharacterRotationSettings`, `StartContinuousCameraShake`, `InitializeAttackWarningWidget` | – |
| `SetFightTarget` / `UpdateFightTarget` | `Actor: AActor*` |
| `GetCharacterCapsuleCollider` | `Return: TArray<UPrimitiveComponent*>&` |
| `CanEnterPreDeathState` | `Return: TEnumAsByte<E_PreDeathTypes::Type>&` |
| `GetAICharacterWidget` | `Widget: UUserWidget*&` |

`ABP_PlayerCharacter_C` (the parent, `CXXHeaderDump\BP_PlayerCharacter.hpp`) adds:

| Member / function | Detail |
|---|---|
| **`DefaultMesh`** | `TSoftObjectPtr<USkeletalMesh>`, offset `0x0CE8` |
| **`MatInstances`** | `TArray<UMaterialInstanceDynamic*>`, offset `0x0CB8` |
| `BPC_GhostFX` | `UBPC_GhostFX_C*` @ `0x0BC8` |
| `BPC_VFXComponent` | `UBPC_VFXComponent_C*` @ `0x0BD0` (BP of `USpartaVFXComponent`) |
| `CreateMatInstances()` | – |
| **`GetDefaultMeshSoftReference()`** | returns the `TSoftObjectPtr` above |
| **`InitDefaultMesh()`** | applies `DefaultMesh` |
| **`GetCharacterSkeletalMeshComponent()`** | returns the body mesh component |
| `GetVFXComponent` | `VFXComponent: UBPC_VFXComponent_C*&` |
| `GetDeathAnimationSet` | `Result: USpartaDeathAnimationSet*&` |
| `GetCharacterID`, `GetStatusEffectContainer` | – |

Evidence:

```
[000001F61A741380] SoftObjectProperty /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C:DefaultMesh [o: CE8] [n: 4C98F4] [c: 00007FF721DF97F0] [owr: 000001F617E634F0] [pc: 00007FF46EF5E938]
```

`InitMeshByID` / `InitDefaultMesh` also exist on `BP_Shell_Summon_C`, `BP_Shell_Genessa_AstralBase_C`
and `BP_Shell_Tiel_ShadowClone_C` (grep of `CXXHeaderDump` for `InitMeshByID|InitDefaultMesh`).

### 2.6 Skeletal meshes under `/Game/Sparta/Characters/Shells/` — **every** `SkeletalMesh` (26)

`scripts\q\pl-skmeshes.sql` — complete, no sampling. Skeleton/PhysicsAsset rows from the same query.

| # | SkeletalMesh | PhysicsAsset |
|---|---|---|
| 1 | `/Game/Sparta/Characters/Shells/DarkForm/Art/Mesh/SK_DarkForm.SK_DarkForm` | `PA_DarkForm` |
| 2 | `/Game/Sparta/Characters/Shells/DarkForm/Art/Mesh/SK_DarkForm_02.SK_DarkForm_02` | |
| 3 | `/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk` | |
| 4 | `/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk_FrogMask.SK_DarkForm_Bulk_FrogMask` | `PA_DarkForm_frogmask` |
| 5 | `/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk_OnlyRobe.SK_DarkForm_Bulk_OnlyRobe` | |
| 6 | `/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk_Robe.SK_DarkForm_Bulk_Robe` | `PA_DarkFormRobe` |
| 7 | `/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk_backpack.SK_DarkForm_Bulk_backpack` | |
| 8 | `/Game/Sparta/Characters/Shells/DarkForm_Skel/Art/Mesh/SK_DarkBro_SKeleton.SK_DarkBro_SKeleton` | |
| 9 | `/Game/Sparta/Characters/Shells/DarkForm_Skel/Art/Mesh/SkMesh/SK_DarkformSkeleton_SkydiveOnly.SK_DarkformSkeleton_SkydiveOnly` | |
| 10 | `/Game/Sparta/Characters/Shells/Eredrim/Art/Mesh/SK_Eredrim.SK_Eredrim` | `PA_Eredrim`, `PA_Eredrim_Cloth` |
| 11 | `/Game/Sparta/Characters/Shells/Gragu/Art/Mesh/SK_Shell_Gragu.SK_Shell_Gragu` | `PA_Shell_Gragu` |
| 12 | `/Game/Sparta/Characters/Shells/Hadern/Art/Mesh/SK_Hadern.SK_Hadern` | `SK_Hadern_PhysicsAsset` |
| 13 | `/Game/Sparta/Characters/Shells/Harros/Art/Mesh/SK_Harros.SK_Harros` | `PA_Harros`, `PA_HarrosCloth` |
| 14 | `/Game/Sparta/Characters/Shells/Harros/Art/Mesh/SK_Harros_Ghost.SK_Harros_Ghost` | |
| 15 | `/Game/Sparta/Characters/Shells/KnightLady/Art/Mesh/SK_Shell_KnightLady_V04.SK_Shell_KnightLady_V04` | `PA_Shell_KnightLady` |
| 16 | `/Game/Sparta/Characters/Shells/Necrophage/Art/Mesh/SK_Necrophage_Armored.SK_Necrophage_Armored` | |
| 17 | `/Game/Sparta/Characters/Shells/Necrophage/Art/Mesh/SK_Necrophage_ArmoredNoMask.SK_Necrophage_ArmoredNoMask` | |
| 18 | `/Game/Sparta/Characters/Shells/Necrophage/Art/Mesh/SK_Necrophage_ArmoredOn.SK_Necrophage_ArmoredOn` | |
| 19 | `/Game/Sparta/Characters/Shells/Necrophage/Art/Mesh/SK_Necrophage_Young.SK_Necrophage_Young` | `SK_Necrophage_Young_PhysicsAsset` |
| 20 | `/Game/Sparta/Characters/Shells/Smert/Mesh/SK_Shell_Smert.SK_Shell_Smert` | `PA_Shell_Smert`, `…_Cloth`, `…_Cloth_Hood`, `…_Danglies` |
| 21 | `/Game/Sparta/Characters/Shells/Solomon/Art/Mesh/SK_Solomon.SK_Solomon` | `SK_Solomon_PhysicsAsset` |
| 22 | `/Game/Sparta/Characters/Shells/ThornBoi/Art/Mesh/SK_ThornBoi.SK_ThornBoi` | `PA_ThornBoi` |
| 23 | `/Game/Sparta/Characters/Shells/ThornBoi/Art/Mesh/SK_ThornBoi_NoHelmet.SK_ThornBoi_NoHelmet` | |
| 24 | `/Game/Sparta/Characters/Shells/ThornBoi/Art/Mesh/SK_ThornBoi_Thorns.SK_ThornBoi_Thorns` | |
| 25 | `/Game/Sparta/Characters/Shells/Tiel/Art/Mesh/SK_Tiel.SK_Tiel` | `PA_Tiel`, `PA_Tiel_Cloth`, `PA_Tiel_ClothBottoms` |
| 26 | `/Game/Sparta/Characters/Shells/Tiel/Art/Mesh/SK_Tiel_Face.SK_Tiel_Face` | |

Whole-build counts from `scripts\q\pl-shells.sql`: 26 `SkeletalMesh`, 26 `PhysicsAsset`,
**1** `Skeleton`, 42 `PoseAsset` (RBF solver poses), 15 `AnimBlueprintGeneratedClass`,
136 `MorphTarget`, 282 `MaterialInstanceConstant`, 19 `ClothingAssetCommon` under
`/Game/Sparta/Characters/Shells/`.

Evidence:

```
[000001F8848ABEC0] SkeletalMesh /Game/Sparta/Characters/Shells/Gragu/Art/Mesh/SK_Shell_Gragu.SK_Shell_Gragu [n: 4781AC] [c: 00007FF46EF5E938] [or: 000001F88B3281C0]
[000001F8846BF900] ClothingAssetCommon /Game/Sparta/Characters/Shells/Gragu/Art/Mesh/SK_Shell_Gragu.SK_Shell_Gragu:SK_Shell_Gragu_Clothing_1 [n: 7EBB0E] [c: 00007FF46EF70F38] [or: 000001F8848ABEC0]
```

`GUESS:` pairing shell → `SK_` asset. The dump cannot show which `SK_` mesh a `CD_`/`BP_Shell_`
points at, so the pairing below is inferred purely from the **directory name**. It is a strong
signal (each shell has exactly one directory) but it is not proof.

| Shell | `SK_` mesh (directory-inferred) | Notes |
|---|---|---|
| Eredrim | `SK_Eredrim` | |
| Gragu | `SK_Shell_Gragu` | |
| Harros | `SK_Harros`, `SK_Harros_Ghost` | ghost variant for memory scenes |
| KnightLady | `SK_Shell_KnightLady_V04` | |
| Necrophage | `SK_Necrophage_Armored`, `…_ArmoredNoMask`, `…_ArmoredOn`, `SK_Necrophage_Young` | mask on/off + young/young-adult |
| Smert | `SK_Shell_Smert` | |
| Solomon | `SK_Solomon` | |
| Thorns (ThornBoi) | `SK_ThornBoi`, `SK_ThornBoi_NoHelmet`, `SK_ThornBoi_Thorns` | |
| Tiel | `SK_Tiel`, `SK_Tiel_Face` | `WithFace` item exists |
| Genessa | UNKNOWN | **no** `SK_` under `Shells/Genessa/` — see §2.9 |
| Hadern | `SK_Hadern` | no `ID_Shell_Hadern` item (§2.9) |
| Dark form (Bulk / StrongOne) | `SK_DarkForm_Bulk`, `…_FrogMask`, `…_OnlyRobe`, `…_Robe`, `…_backpack` | |
| Dark form (Skel) | `SK_DarkBro_SKeleton`, `SK_DarkformSkeleton_SkydiveOnly` | |
| Dark form (base) | `SK_DarkForm`, `SK_DarkForm_02` | |

### 2.7 Shell AnimBlueprints (15) and Skeletons (1)

| AnimBlueprintGeneratedClass | Parent |
|---|---|
| `/Game/Sparta/Characters/Shells/DarkForm/Art/Mesh/ABP_ShellSnatched_Post.ABP_ShellSnatched_Post_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/ABP_DarkForm_Bulk_AnimationPostprocess.ABP_DarkForm_Bulk_AnimationPostprocess_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/ABP_DarkForm_Robe_Postprocess.ABP_DarkForm_Robe_Postprocess_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/ABP_DarkForm_frogMask_Postprocess.ABP_DarkForm_frogMask_Postprocess_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/ABP_DarkForm_onlyRobe_Postprocess.ABP_DarkForm_onlyRobe_Postprocess_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/DarkForm_Skel/Art/Mesh/SkMesh/ABP_DarkFormSkeleton.ABP_DarkFormSkeleton_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/Eredrim/Art/Mesh/ABP_Eredrim_Post.ABP_Eredrim_Post_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/Gragu/Art/Mesh/AB_Gragu_Postprocess.AB_Gragu_Postprocess_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/Harros/Art/Mesh/ABP_Harros_PostProccess_AnimBlueprint.ABP_Harros_PostProccess_AnimBlueprint_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/KnightLady/Art/Mesh/ABP_Shell_KnightLady_Post_V03.ABP_Shell_KnightLady_Post_V03_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/Necrophage/Art/Mesh/ABP_Necrophage_PostProcess.ABP_Necrophage_PostProcess_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/Smert/Mesh/ABP_Smert_Post.ABP_Smert_Post_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/Solomon/Art/Mesh/ABP_Solomon_Post.ABP_Solomon_Post_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/ThornBoi/Art/Mesh/ABP_ThornBoi_Post.ABP_ThornBoi_Post_C` | `/Script/Engine.AnimInstance` |
| `/Game/Sparta/Characters/Shells/Tiel/Art/Mesh/ABP_Tiel_Post.ABP_Tiel_Post_C` | `/Script/Engine.AnimInstance` |

**These are all `_Post` / `PostProcess` (post-process AnimBlueprints), not the main body ABP.**
The main-body ABPs live beside the shell BP under `Core/Characters/Player/<Shell>/`:

| Main shell AnimBlueprint | Parent |
|---|---|
| `/Game/Sparta/Core/Characters/Player/Common/ABP_Shell_Default.ABP_Shell_Default_C` | `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` |
| `/Game/Sparta/Core/Characters/Player/Eredrim/ABP_Shell_Eredrim.ABP_Shell_Eredrim_C` | `…ABP_AI_Humanoid_C` |
| `/Game/Sparta/Core/Characters/Player/Genessa/ABP_Shell_Genessa.ABP_Shell_Genessa_C` | `…ABP_AI_Humanoid_C` |
| `/Game/Sparta/Core/Characters/Player/Gragu/ABP_Shell_Gragu.ABP_Shell_Gragu_C` | `…ABP_AI_Humanoid_C` |
| `/Game/Sparta/Core/Characters/Player/KnightLady/ABP_Shell_KnightLady.ABP_Shell_KnightLady_C` | `…ABP_AI_Humanoid_C` |
| `/Game/Sparta/Core/Characters/Player/Necrophage/ABP_Shell_Necrophage.ABP_Shell_Necrophage_C` | `…ABP_AI_Humanoid_C` |
| `/Game/Sparta/Core/Characters/Player/Smert/ABP_Shell_Smert.ABP_Shell_Smert_C` | `…ABP_AI_Humanoid_C` |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/ABP_Shell_Thorn.ABP_Shell_Thorn_C` | `…ABP_AI_Humanoid_C` |
| `/Game/Sparta/Core/Characters/Player/Tiel/ABP_Shell_Tiel.ABP_Shell_Tiel_C` | `…ABP_AI_Humanoid_C` |

No `ABP_Shell_Harros_*` or `ABP_Shell_Solomon_*` exists (UNKNOWN which ABP those two shells use —
`pl-playerclasses.sql` lists no such class).

**The player's own AnimBlueprint is shared by all shells** and lives outside the shell folders:

```
/Game/Sparta/Characters/Humans/Player/Animations/ABP_Player.ABP_Player_C
```

This is confirmed independently by the runtime probe log (§0.3), which reports `animclass =
AnimBlueprintGeneratedClass /Game/Sparta/Characters/Humans/Player/Animations/ABP_Player.ABP_Player_C`
on the live player `CharacterMesh0` while the mesh asset was `SK_DarkForm_Bulk`. Verbatim:

```
       animclass=AnimBlueprintGeneratedClass /Game/Sparta/Characters/Humans/Player/Animations/ABP_Player.ABP_Player_C animinst=ABP_Player_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel.BP_PlayerCharacter_C_2147479936.CharacterMesh0.ABP_Player_C_2147473036
```

Skeletons:

| Skeleton | Where | Socket objects |
|---|---|---|
| `/Game/Sparta/Characters/Humans/_Shared/SKEL_Human_Skeleton.SKEL_Human_Skeleton` | humanoid shared skeleton used by the human shells | **82** `SkeletalMeshSocket` subobjects |
| `/Game/Sparta/Characters/Shells/DarkForm_Skel/Art/Mesh/SkMesh/SKEL_DarkformSkeleton.SKEL_DarkformSkeleton` | the only `Skeleton` asset under `Shells/` | UNKNOWN count (none under that path; `pl-sockets.sql`) |

Evidence:

```
[000001F4D5E08960] Skeleton /Game/Sparta/Characters/Humans/_Shared/SKEL_Human_Skeleton.SKEL_Human_Skeleton [n: 260C0A] [c: 00007FF46EF32738] [or: 000001F4D3012770]
[000001F887573740] Skeleton /Game/Sparta/Characters/Shells/DarkForm_Skel/Art/Mesh/SkMesh/SKEL_DarkformSkeleton.SKEL_DarkformSkeleton [n: E8D29] [c: 00007FF46EF32738] [or: 000001F87A8D7950]
[000001F4D3012770] Package /Game/Sparta/Characters/Humans/_Shared/SKEL_Human_Skeleton [n: 1AF079] [c: 00007FF46EE55338] [or: 0000000000000000]
```

**UNKNOWN:** the mesh → skeleton link. Nothing in the dump carries a `SkeletalMesh::Skeleton`
value, so "all humanoid shells use `SKEL_Human_Skeleton`" is `GUESS:` from the folder name and the
82 sockets it owns. To settle it, read `SkeletalMesh.Skeleton` at runtime.

### 2.8 Shell `SpartaCharacterData` assets — the 25 player/decoy/dark-form `CD_*` assets

`scripts\q\pl-shellcd.sql` (filter: `type='SpartaCharacterData'` and a player/shell keyword in the path):

| Asset | Shell |
|---|---|
| `/Game/Sparta/Core/Characters/Player/Common/CD_Player.CD_Player` | default player |
| `/Game/Sparta/Core/Characters/Player/Common/CD_Decoy.CD_Decoy` | decoy |
| `/Game/Sparta/Core/Characters/Player/Common/CD_Decoy_Bait.CD_Decoy_Bait` | decoy bait |
| `/Game/Sparta/Core/Characters/Player/Eredrim/CD_Shell_Eredrim.CD_Shell_Eredrim` | Eredrim |
| `/Game/Sparta/Core/Characters/Player/Genessa/CD_Genessa.CD_Genessa` | Genessa |
| `/Game/Sparta/Core/Characters/Player/Genessa/CD_Genessa_Training.CD_Genessa_Training` | Genessa (tutorial) |
| `/Game/Sparta/Core/Characters/Player/Genessa/CD_CorruptedGenessa_Training.CD_CorruptedGenessa_Training` | corrupted Genessa (tutorial) |
| `/Game/Sparta/Core/Characters/Player/CorruptedGenessa/CD_CorruptedGenessa.CD_CorruptedGenessa` | corrupted Genessa |
| `/Game/Sparta/Core/Characters/Player/Gragu/CD_Gragu.CD_Gragu` | Gragu |
| `/Game/Sparta/Core/Characters/Player/Harros/CD_Harros.CD_Harros` | Harros |
| `/Game/Sparta/Core/Characters/Player/KnightLady/CD_KnightLady.CD_KnightLady` | KnightLady |
| `/Game/Sparta/Core/Characters/Player/Necrophage/CD_Necrophage.CD_Necrophage` | Necrophage |
| `/Game/Sparta/Core/Characters/Player/Necrophage/CD_Necrophage_Young.CD_Necrophage_Young` | Necrophage (young) |
| `/Game/Sparta/Core/Characters/Player/Smert/CD_Smert.CD_Smert` | Smert |
| `/Game/Sparta/Core/Characters/Player/Solomon/CD_Shell_Solomon.CD_Shell_Solomon` | Solomon |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/CD_Shell_Thorn.CD_Shell_Thorn` | Thorns |
| `/Game/Sparta/Core/Characters/Player/ThornBoi/CD_Shell_Thorn_Memories.CD_Shell_Thorn_Memories` | Thorns (memory) |
| `/Game/Sparta/Core/Characters/Player/Tiel/CD_Tiel.CD_Tiel` | Tiel |
| `/Game/Sparta/Core/Characters/Player/Tiel/CD_Tiel_WithFace.CD_Tiel_WithFace` | Tiel (with face) |
| `/Game/Sparta/Core/Characters/Player/Tiel/CD_Decoy_Tiel.CD_Decoy_Tiel` | Tiel decoy |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/CD_Genessa_Astral_Clone.CD_Genessa_Astral_Clone` | astral clone |
| `/Game/Sparta/Core/Characters/Player/AstralGenessa/CD_Genessa_Astral_Copy.CD_Genessa_Astral_Copy` | astral copy |
| `/Game/Sparta/Core/Characters/Player/StrongOne/CD_StrongOne.CD_StrongOne` | dark form |
| `/Game/Sparta/Core/Characters/Player/StrongOne/CD_StrongOne_Robe.CD_StrongOne_Robe` | dark form (robe) |
| `/Game/Sparta/Core/Characters/Player/Eggman/CD_Eggman.CD_Eggman` | dark form |
| `/Game/Sparta/Core/Characters/Player/Common/CD_Darkform…` | (none; see `pl-chardata.sql`) |

Every one of these has exactly one `CharacterAnimationData` subobject, e.g.
`…/CD_Gragu.CD_Gragu:CharacterAnimationData_0` (`type = CharacterAnimationData`), except
`CD_Harros`, `CD_Smert`, `CD_Shell_Solomon`, `CD_Tiel`(?), `CD_Shell_Thorn_Memories` — see
`scripts\q\pl-custom.sql` output: the list of 28 `CharacterAnimationData` subobjects does not
include Harros/Smert/Solomon/Eredrim. **UNKNOWN** why; probably an inherited default asset value.

Evidence:

```
[000001F8B5712990] SpartaCharacterData /Game/Sparta/Core/Characters/Player/Gragu/CD_Gragu.CD_Gragu [n: 166071] [c: 00007FF46F043538] [or: 000001F8B1028E90]
```

### 2.9 Shells with a definition but no mesh folder, and vice versa

* **Genessa** has `BP_Shell_Genessa_C`, `CD_Genessa`, `ID_Shell_Genessa` + 2 skins and
  `ABP_Shell_Genessa_C`, but **no** `SkeletalMesh` anywhere under
  `/Game/Sparta/Characters/Shells/Genessa/`. Tried:
  `SELECT type,path FROM obj WHERE type='SkeletalMesh' AND path LIKE '/Game/Sparta/Characters/Shells/Genessa/%'`
  → 0 rows. UNKNOWN which mesh she uses.
* **Hadern** has `SK_Hadern` + `SK_Hadern_PhysicsAsset` + `HPM_…`-style assets but **no**
  `ID_Shell_Hadern` item (see `pl-shellitems.sql`), so his mesh is not a playable-shell body in
  this build.
* `ID_Shell_Lazlo_NoMask` exists with a `SetShellClass` fragment, but there is **no**
  `BP_Shell_Lazlo*` class and **no** `SkeletalMesh` under `…/Shells/Lazlo/`. UNKNOWN; note
  `GA_Lazlo_*` / `GE_Lazlo_WorthyArmor` upgrades exist under `Core/Player/Upgrades/Shells/Lazlo/`.
* `BP_Shell_Necrophage_NoMask_C` and `SK_Necrophage_ArmoredNoMask` exist but no
  `ID_Shell_Necrophage_NoMask` item.

### 2.10 Per-shell CDO component: `CharacterMesh0`

Every shell/pawn CDO owns a `SpartaSkeletalMeshComponent` subobject **named `CharacterMesh0`**
(35 rows: `pl-shellmeshcomp.sql`). That name is stable and is the component a mod should look up.

| CDO | Component |
|---|---|
| `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.Default__BP_Shell_Base_C:CharacterMesh0` | `SpartaSkeletalMeshComponent` |
| `/Game/Sparta/Core/Characters/Player/Gragu/BP_Shell_Gragu.Default__BP_Shell_Gragu_C:CharacterMesh0` | `SpartaSkeletalMeshComponent` |
| …one per shell, full list via `pl-shellmeshcomp.sql` | `SpartaSkeletalMeshComponent` |

Full component list on a shell CDO (`find /Game/Sparta/Core/Characters/Player/Gragu/BP_Shell_Gragu.Default__BP_Shell_Gragu_C`):

| Subobject | Class |
|---|---|
| `AbilitySystemComponent` | `SpartaAbilitySystemComponent` |
| `ArmorComponent` | `ArmorComponent` |
| `ArtifactComponent` | `BP_ArtifactsComponent_C` |
| `CharMoveComp` | `SpartaCharacterMovementComponent` |
| **`CharacterMesh0`** | **`SpartaSkeletalMeshComponent`** |
| `CollisionCylinder` | `CapsuleComponent` |
| `FoleyComponent` | `SpartaCharacterFoleyComponent` |
| `HealthComponent` | `BPC_Player_Health_C` |
| `HitTraceComponent` | `SpartaHitTraceComponent` |
| `ItemManagerComponent` | `SpartaItemManagerComponent` |
| `LockOnComponent` | `BP_TargetLockComponent_C` |
| `MotionWarping` | `CSMotionWarpingComponent` |
| `NavigationInvokerComponent` | `NavigationInvokerComponent` |
| `PathFollowingComponent` | `SpartaPathFollowingComponent` |
| `PlayerExpComponent` | `BPC_Player_Experience_C` |
| `SidearmProjectileCacheComponent` | `SidearmProjectileCacheComponent` |
| `TarstoneComponent` | `BPC_TarstoneComponent_C` |
| `VoxComponent` | `SpartaCharacterVoxComponent` |
| `WeaponsComponent` | `WeaponsComponent` |

Evidence:

```
[000001F8B544CCD0] SpartaSkeletalMeshComponent /Game/Sparta/Core/Characters/Player/Gragu/BP_Shell_Gragu.Default__BP_Shell_Gragu_C:CharacterMesh0 [n: 9760] [c: 00007FF46F058338] [or: 000001F8A7EED570]
[000001F4ACDF4B80] MulticastInlineDelegateProperty /Script/Sparta.SpartaSkeletalMeshComponent:OnMeshCustomisationComplete [o: 10B8] [n: 5891FB] [c: 00007FF721DE9010] [owr: 00007FF46F058338] [df: 00007FF46F3D0B88]
```

(`[c:]` = `00007FF46F058338` = `SpartaSkeletalMeshComponent`, `[or:]` = the CDO package. The same
query, filtered by shell folder, returns all 35 `CharacterMesh0` rows.)

---

## 3. How the game switches shell / skin / body mesh

### 3.1 The full set of shell-setting functions in the build

`scripts\q\pl-setshell.sql` (functions whose name matches `SetShell|SwapShell|ChangeShell|ShellSwap|
SetDarkform|SetSkeletalMesh|ChangeCharacterMesh|SetCharacterMesh|SetMesh|EquipShell|ApplySkin|SetSkin|
ChangeSkin`). The **game-specific** ones:

| Function | Class |
|---|---|
| **`ChangeShell`** | `/Game/Sparta/Core/Player/BPFL_Player_Equipment.BPFL_Player_Equipment_C` |
| **`ChangeShellSkin`** | same |
| **`ChangeShellClothing`** | same |
| **`ChangeShellMask`** | same |
| `ChangeDarkFormSkin` / `ChangeDarkFormClothing` / `ChangeDarkFormMask` | same |
| **`SwapShellMaterials`** | same |
| `SwapDarkFormMaterials` | same |
| `GetShellSkin`, `GetShellClothing`, `GetShellMask` | same |
| `GetDarkFormSkin`, `GetDarkFormClothing`, `GetDarkFormMask` | same |
| `GetAllSkinsForShell`, `GetAllSkinsForDarkForm` | same |
| `UnlockSkin`, `IsSkinUnlocked`, `HasAnyUnlockedSkin` | same |
| `UnlockClothing`, `UnlockMask`, `HasAnyUnlockedClothing`, `HasAnyUnlockedMask` | same |
| `ValidateShellSkinFromTag`, `ValidateShellSkinFromID`, `ValidateDarkFormSkinFromTag`, `ValidateDarkFormSkinFromID` | same |
| `GetSkinTagFromID`, `GetClothingName`, `GetMaskName`, `GetClothingObjects`, `GetMaskObjects` | same |
| `StowAndPutInHand` | same |
| `DoesSkinRequireEntitlement`, `CheckRedHarbingerRequirement` | same |
| `SendRefreshClothingEvent`, `SendRefreshMasksEvent` | same |
| **`SetShellItem`** / `SetDarkformItem` | `/Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C` |
| **`SetShell`** / `GetShell` / `GetDarkform` / `SetActiveShellCharacterData` / `SetActiveDarkformCharacterData` / `OnChangeShellItem` | `/Script/Sparta.SpartaPlayerController` |
| **`InitMeshByID`** | `/Game/Sparta/Core/Characters/Player/Common/BP_Shell_Base.BP_Shell_Base_C` (also `BP_Shell_Summon_C`) |
| `InitDefaultMesh`, `GetDefaultMeshSoftReference` | `/Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C` |
| `Equip` / `Unequip` / `PutInHand` / `Stow` / `HideWeapon` | `/Script/Sparta.SpartaWeapon` |
| `ApplySkin` | `/Game/Sparta/UI/Menu/LandingArea/WBP_MGT_ChangeSkin.WBP_MGT_ChangeSkin_C` (menu — other agent's area) |
| `SetSkeletalMeshAsset` | `/Script/Engine.SkeletalMeshComponent` (engine) |

There is **no** `SwapShell`, `ChangeShell` (native), `ShellSwap`, `SetSkeletalMesh` (native) or
`SetDarkformClass` function in `/Script/Sparta`. `pl-setshell.sql` searched for all of them.

### 3.2 `BPFL_Player_Equipment_C` — the shell/skin/clothing/mask API

Blueprint function library, parent `/Script/Engine.BlueprintFunctionLibrary`, 84 members.
All of these take an implicit `__WorldContext` and are pure utility (no state on the object).

| Function | Parameters (name: type @ offset) |
|---|---|
| **`ChangeShell`** | `ShellId: GameplayTag @0`, `Save: bool @8`, `__WorldContext: Object* @16`, `Item: ClassProperty @24`, `SaveObject: BP_SpartaPlayerSaveObject_C* @32` |
| **`ChangeShellSkin`** | `ShellId: GameplayTag @0`, `SkinID: GameplayTag @8`, `Save: bool @16`, `__WorldContext: Object* @24`, `Item: ClassProperty @32`, `SaveObject: BP_SpartaPlayerSaveObject_C* @40` |
| **`ChangeShellClothing`** | `CharacterId: GameplayTag @0`, `ClothingID: GameplayTag @8`, `Save: bool @16`, `__WorldContext: Object* @24`, `SaveObject: BP_SpartaPlayerSaveObject_C* @32` |
| **`ChangeShellMask`** | `CharacterId: GameplayTag @0`, `MaskID: GameplayTag @8`, `Save: bool @16`, `__WorldContext: Object* @24`, `SaveObject: BP_SpartaPlayerSaveObject_C* @32` |
| `ChangeDarkFormClothing` | `CharacterId: GameplayTag @0`, `ClothingID: GameplayTag @8`, `Save: bool @16`, `__WorldContext: Object* @24`, `SaveObject: …* @32` |
| `ChangeDarkFormMask` | `CharacterId: GameplayTag @0`, `MaskID: GameplayTag @8`, `Save: bool @16`, `__WorldContext: Object* @24`, `SaveObject: …* @32` |
| `ChangeDarkFormSkin` | `DarkFormID: GameplayTag @0`, `SkinID: GameplayTag @8`, `Save: bool @16`, `__WorldContext: Object* @24`, `SaveObject: …* @32`, `Item: ClassProperty @40` |
| **`SwapShellMaterials`** | `SkeletalMesh: SkeletalMeshComponent* @0`, `TargetMaterialsType: ByteProperty @8`, `__WorldContext: Object* @16`, `SkinMaterials: SkinMaterials (StructProperty of /Script/Sparta.SkinMaterials) @24`, `SkinTag: GameplayTag @72`, `TargetMaterials: ArrayProperty<MaterialInstance*> @80`, `DefaultIndex: IntProperty @96`, `EffectIndex: IntProperty @100`, `FoundData: SpartaCharacterData* @104` |
| `SwapDarkFormMaterials` | identical signature to `SwapShellMaterials` |
| `GetShellSkin` | `OptionalShellID: GameplayTag @0`, `__WorldContext @8`, `ReturnValue: GameplayTag @16`, `ShellId: GameplayTag @24`, `SaveObject: …* @32` |
| `GetDarkFormSkin` | `OptionalDarkFormID: GameplayTag @0`, `__WorldContext @8`, `ReturnValue: GameplayTag @16`, `DarkFormID: GameplayTag @24`, `SaveObject: …* @32` |
| `GetShellClothing` | `ShellId @0`, `__WorldContext @8`, `Success: bool @16`, `ReturnValue: GameplayTag @20`, `ValidMasks: GameplayTagContainer @32`, `ExistingCharacters: GameplayTagContainer @64`, `CurrentMask: GameplayTag @96`, `SaveObject @104` |
| `GetDarkFormClothing` | same shape with `DarkFormID @0` |
| `GetShellMask` | same shape as `GetShellClothing` |
| `GetShellSkin`, `GetAllSkinsForShell` | `ShellId @0`, `__WorldContext @8`, `Success: bool @16`, `Value: GameplayTagContainer @24`, then per-shell arrays |
| `GetAllSkinsForShell` per-shell arrays | `ProximaSkins @56`, `GraguSkins @72`, `SolomonSkins @88`, `HarrosSkins @104`, `SarielSkins @120`, `EredrimSkins @136`, `SmertSkins @152`, `LazloSkins @168`, `GenessaSkins @184`, `TielSkins @200` |
| `GetAllSkinsForDarkForm` | `DarkFormID @0`, `__WorldContext @8`, `Success @16`, `Value: GameplayTagContainer @24`, `HarbingerSkins: ArrayProperty<GameplayTag> @56` |
| `ChangeShellSkin` and `UnlockSkin`, `IsSkinUnlocked`, `DoesSkinRequireEntitlement` | see `pl-bpfl.sql` |
| `ValidateShellSkinFromID` | `InID: ClassProperty @0`, `UpdateSaveData: bool @8`, `__WorldContext @16`, `IsValid: bool @24`, `OutID: ClassProperty @32`, `NewID: ClassProperty @40`, `Fragment: IF_ItemTag_C* @48` |
| `StowAndPutInHand` | `WeaponToStow: GameplayTag @0`, `WeaponToEquip: GameplayTag @8`, `CustomPutInHandSocket: NameProperty @16`, `WeaponsComponent: WeaponsComponent* @24`, `__WorldContext @32`, `WeaponRef: SpartaWeapon* @40` |

**`ChangeShell` internals, as revealed by the compiler temporaries it left in the dump** — this is
the most valuable single trace in this report, because it names the native calls in order:

| Temporary (filtered from the table) | Meaning |
|---|---|
| `CallFunc_GetShellItemDefinition_ReturnValue` → `SoftClassProperty` | shell item definition is a **soft class** |
| `CallFunc_LoadClassAsset_Blocking_ReturnValue` → `ClassProperty` | it is loaded synchronously |
| `K2Node_ClassDynamicCast_AsSparta_Item_Definition` | cast to `SpartaItemDefinition` |
| `CallFunc_GetShellSkin_ReturnValue` → `GameplayTag` | current skin tag read *before* the swap |
| `CallFunc_GetPlayerSaveObjectFromCurrentSave_ReturnValue` + `_Success` | save-object write path |
| `CallFunc_SpawnObject_ReturnValue` → `GlobalEvent_ShellChanged_C*` | **spawns `/Game/Sparta/Core/GES/GlobalEvent_ShellChanged.GlobalEvent_ShellChanged_C`** |
| `CallFunc_BroadcastEvent_ReturnValue` → `IntProperty` | broadcasts it |
| `K2Node_DynamicCast_AsBPI_Player_Controller` | goes through the `BPI_Player_Controller` interface |
| `CallFunc_GetShellFullName_ReturnValue` → `TextProperty`; `CallFunc_GetTextSourceString_ReturnValue` → `StrProperty` | telemetry (shell name) |
| `CallFunc_GetGameInstanceSubsystem_ReturnValue` → `SpartaBetaTelemetrySubsystem*` | telemetry |
| `CallFunc_GetPlayerController_ReturnValue` → `PlayerController*` | |

Evidence (`pl-changeshell.sql`, 20 rows):

```
/Game/Sparta/Core/Player/BPFL_Player_Equipment.BPFL_Player_Equipment_C:ChangeShell:ShellId	StructProperty	/Script/GameplayTags.GameplayTag	0
/Game/Sparta/Core/Player/BPFL_Player_Equipment.BPFL_Player_Equipment_C:ChangeShell:CallFunc_SpawnObject_ReturnValue	ObjectProperty	/Game/Sparta/Core/GES/GlobalEvent_ShellChanged.GlobalEvent_ShellChanged_C	88
/Game/Sparta/Core/Player/BPFL_Player_Equipment.BPFL_Player_Equipment_C:ChangeShell:CallFunc_GetShellItemDefinition_ReturnValue	SoftClassProperty		152
```

### 3.3 `BP_PlayerController_C::SetShellItem` / `SetDarkformItem`

| Function | Parameters |
|---|---|
| **`SetShellItem`** | `Item: ClassProperty @0` |
| **`SetDarkformItem`** | `Item: ClassProperty @0`, plus filtered `CallFunc_NotEqual_ClassClass_ReturnValue` (compares the new item class to the current one before applying) |

These are the controller-side entry points; they take the **item definition class**, not the pawn class.

### 3.4 Native `ASpartaPlayerController` — the authoritative pointer swap

| Member | Type | Offset |
|---|---|---|
| `OnChangeShellItem` | `MulticastInlineDelegateProperty` | `0x0758` |
| `SpartaPlayerCharacter` | `ASpartaPlayerCharacter*` | `0x0770` |
| **`Shell`** | `ASpartaPlayerCharacter*` | `0x0778` |

| Function | Parameters | Notes |
|---|---|---|
| **`SetShell`** | `Shell: ObjectProperty of /Script/Sparta.SpartaPlayerCharacter @0` | sets `Shell` |
| `GetShell` | `ReturnValue: /Script/Sparta.SpartaPlayerCharacter @0` | |
| `GetDarkform` | `ReturnValue: /Script/Sparta.SpartaPlayerCharacter @0` | |
| `GetSpartaPlayerCharacter` | `ReturnValue: …` | |
| **`SetActiveShellCharacterData`** | `CharacterData: ObjectProperty of /Script/Sparta.SpartaCharacterData @0` | applies the `CD_*` asset to the live pawn |
| **`SetActiveDarkformCharacterData`** | `CharacterData: /Script/Sparta.SpartaCharacterData @0` | |
| `OnChangeShellItem` (delegate) | `OldShell: TSubclassOf<USpartaItemDefinition>`, `NewShell: TSubclassOf<USpartaItemDefinition>` | |

Evidence:

```
[000001F4ACDBEF00] MulticastInlineDelegateProperty /Script/Sparta.SpartaPlayerController:OnChangeShellItem [o: 758] [n: 47D14] [c: 00007FF721DE9010] [owr: 00007FF46F055938] [df: 00007FF46F3CE780]
[000001F4ACDBF080] ObjectProperty /Script/Sparta.SpartaPlayerController:Shell [o: 778] [n: 525763] [c: 00007FF721DE9420] [owr: 00007FF46F055938] [pc: 00007FF46F055738]
[00007FF46F70C938] Function /Script/Sparta.SpartaPlayerController:SetShell [n: 4A0B1] [c: 00007FF46EE55538] [or: 00007FF46F055938] [f: 00007FF71C82D4A0]
[000001F4ACDBF600] ObjectProperty /Script/Sparta.SpartaPlayerController:SetShell:Shell [o: 0] [n: 525763] [c: 00007FF721DE9420] [owr: 00007FF46F70C938] [pc: 00007FF46F055738]
[000001F4ACDBD700] ObjectProperty /Script/Sparta.SpartaPlayerController:SetActiveShellCharacterData:CharacterData [o: 0] [n: 524DF3] [c: 00007FF721DE9420] [owr: 00007FF46F70C778] [pc: 00007FF46F043538]
```

`ASpartaCharacter` (parent) has the *lower-level* data application, which is what actually repaints
the body:

| Function | Parameters |
|---|---|
| **`SetCharacterData`** | `Data: const USpartaCharacterData*` |
| `RevertCharacterData` | – |
| `InitialiseCharacterData` | – |
| `GetCharacterData` | – |
| `OnSetCharacterData` (delegate) | `OnSetCharacterData(const USpartaCharacterData* CharacterData)` — `FSpartaCharacterOnSetCharacterData` @ `0x09C0` |
| `SetAudioData` | `in_Data: USpartaCharacterAudioData*` |
| `GetAnimInstance` | – |
| `SetTeam` / `OnTeamChanged` | `NewTeam: FGameplayTag` / `(ASpartaCharacter* Owner, FGameplayTag OldTeam, FGameplayTag NewTeam)` |

Header: `Sparta.hpp:1838`, `:1840`, `:1860`, `:1805-1806`, `:1877`.

### 3.5 Item-level shell equip

`/Game/Sparta/Items/Shells/Pickups/BP_Behavior_EquipShell.BP_Behavior_EquipShell_C`
(parent `/Game/Sparta/Items/Pickups/Core/BP_Executable_UseItem.BP_Executable_UseItem_C`):

| Member | Type | Offset |
|---|---|---|
| `ShellTag` | `GameplayTag` | `0x60` |

| Function | Parameters |
|---|---|
| `CanUse` | `Item: SoftClassProperty @0`, `Fragment: IF_UsableInventoryItem_C* @0x28`, `UseType: ByteProperty @0x30`, `ReturnValue: bool @0x31` |
| `EquipVFX` | (compiler temporaries only: `MakeGameplayCueParameters`, `GetPlayerCharacter`) |
| `Execute` | `InWorldContextObject: Object* @0` |

Note `CanUse` calls `GetCurrentShellID` and compares tags — i.e. re-equipping the shell you are
already wearing is short-circuited. `EquipVFX` fires a **gameplay cue**, so the equip visual is a
cue, not a bespoke mesh swap.

Same shape for weapons: `/Game/Sparta/Items/Weapons/Pickups/BP_Behavior_EquipWeapon.BP_Behavior_EquipWeapon_C`
with `WeaponTag` @ `0x60`, `CanUse(Item, Fragment, UseType)`, `Execute(InWorldContextObject)`.

### 3.6 Order of operations implied by the data (for the sync author)

`GUESS:` (derived only from the temporary names in §3.2, not from code):

1. `BP_PlayerController::SetShellItem(Item)` receives the item class →
2. `BPFL_Player_Equipment::ChangeShell(ShellId, Save, …)` resolves the item, gets the current skin,
   spawns+broadcasts `GlobalEvent_ShellChanged_C` →
3. something spawns `ItemFragment_SetShellClass::ShellClass` as the new pawn and calls
   `ASpartaPlayerController::SetShell(Shell)` →
4. the new pawn's `default` `CharacterData` is applied via `SetActiveShellCharacterData` →
   `ASpartaCharacter::SetCharacterData` →
5. `ABP_Shell_Base_C::InitMeshByID(ShellId)` / `ABP_PlayerCharacter_C::InitDefaultMesh()` sets
   `CharacterMesh0`'s mesh and `MatInstances` →
6. `BPFL_Player_Equipment::SwapShellMaterials(SkeletalMesh, TargetMaterialsType, …, SkinMaterials, SkinTag, TargetMaterials, DefaultIndex, EffectIndex, FoundData)` applies the skin.

**For co-op sync this means: replicating the *item class* + *skin tag* + *clothing/mask tags* is
sufficient; the mesh and materials are derived.** The three tags are all `FGameplayTag`s.

### 3.7 Visual state that is NOT the shell: clothing, masks, skins, glow

| Concept | Where | Data |
|---|---|---|
| Shell | `ID_Shell_*` + `BP_Shell_*_C` + `CD_*` | §2 |
| **Skin** (alt/obsidian) | `ID_Shell_<Shell>_AltSkin`, `…_Obsidian` under `/Game/Sparta/Items/Shells/Skins/` | `ItemFragment_SetShellClass` + `IF_ItemTag` (= skin tag via `GetSkinTagFromID`) |
| **Clothing** | `ChangeShellClothing(CharacterId, ClothingID, Save)` | gameplay tag, stored in `BP_SpartaPlayerSaveObject_C` |
| **Mask** | `ChangeShellMask(CharacterId, MaskID, Save)` | gameplay tag |
| **Dark form skin** | `ID_Darkform_*`, `ChangeDarkFormSkin` | gameplay tag; `HarbingerSkins` array in `GetAllSkinsForDarkForm` |
| **Glow colour** | `/Game/Sparta/Core/Items/IF_GlowColor.IF_GlowColor_C` (parent `/Script/Sparta.SpartaItemFragment`) | `GlowColor: StructProperty of /Script/CoreUObject.LinearColor @0x28`; base-class version `UItemFragment_Display::GlowColor @0x0108` and `UItemFragment_OverrideDisplay::bGlowColorOverride @0x0124` |

Refresh plumbing: `GlobalEvent_RefreshClothing` and `GlobalEvent_RefreshMasks` live under
`/Game/Sparta/Items/Shells/Attachables/`; `BPFL_Player_Equipment::SendRefreshClothingEvent()` and
`SendRefreshMasksEvent()` broadcast them. Weapon equivalents exist too
(`SendRefreshMasksEvent` is shell-side; weapon refresh uses `GlobalEvent_SkinUnlocked` /
`GlobalEvent_UI_CosmeticUnlocked` under `/Game/Sparta/Core/GES/`).

Evidence:

```
[000001F4D2F9DB00] StructProperty /Game/Sparta/Core/Items/IF_GlowColor.IF_GlowColor_C:GlowColor [o: 28] [n: 554E7E] [c: 00007FF721DF9890] [owr: 000001F4B73C34F0] [ss: 00007FF46F2E5B18]
```

Skin tags the game already hard-codes per shell (from `GetAllSkinsForShell` parameter names):
`ProximaSkins`, `GraguSkins`, `SolomonSkins`, `HarrosSkins`, `SarielSkins`, `EredrimSkins`,
`SmertSkins`, `LazloSkins`, `GenessaSkins`, `TielSkins`, and `HarbingerSkins` for dark forms.
So there are at least 11 shell families and a Harbinger family — including **Proxima**, **Sariel**
and **Lazlo**, which have no `BP_Shell_*` pawn class in this build (UNKNOWN / cut content).

### 3.8 `GA_CharacterSkinHandler` — the skin-change ability

`/Game/Sparta/Core/Player/Ability/GA_CharacterSkinHandler.GA_CharacterSkinHandler_C`

| Function | Real parameters |
|---|---|
| `CheckDarkForm` | `ID: ClassProperty`, `Tag: GameplayTag` |
| `IsInDarkform` | `ReturnValue: bool` |

Filtered temporaries show it calls `GetCurrentDarkFormID` (→ `ID`, `Tag`, `SkinTag`, `Success`),
`ValidateDarkFormSkinFromID` (→ `IsValid`, `OutID`), `IsInDarkForm`, `ActivateDarkForm`, and casts
to `BPI_Player_Controller`. So **entering the dark form itself validates + applies the dark-form
skin** — i.e. dark-form appearance and shell appearance are two separate tag sets.

`GA_TransformationHandlerBase_C::ChangeWeaponEquipState` (`NewState: ByteProperty`, calls
`SetWeaponsStateFromSlot`) is the weapon-state analogue; see §5.

### 3.9 Runtime confirmation of the live component/mesh relationship

From `runs\20261022-061848-ingame\shellinfo.txt` (probe log, §0.3):

```
pawn: BP_PlayerCharacter_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel.BP_PlayerCharacter_C_2147479936
GetCharacterSkeletalMeshComponent = SpartaSkeletalMeshComponent /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel.BP_PlayerCharacter_C_2147479936.CharacterMesh0
mesh asset     = SkeletalMesh /Game/Sparta/Characters/Shells/DarkForm_Bulk/Art/Mesh/SK_DarkForm_Bulk.SK_DarkForm_Bulk
anim class     = AnimBlueprintGeneratedClass /Game/Sparta/Characters/Humans/Player/Animations/ABP_Player.ABP_Player_C
pc:GetShell    = UObject: 00000145EB6A4758
pawn.DefaultMesh (soft) = TSoftObjectPtrUserdata: 00000143CA327648
pc.ActiveShellCharacterData = UObject: 00000145EB6A4B58
```

Two facts to take from this: (a) in the dark form the *same* `BP_PlayerCharacter_C` actor keeps the
same `CharacterMesh0` and only the asset changes — so a shell swap in-place is possible;
(b) `pc.ActiveShellCharacterData` is a live property on the controller even though it does not
appear in the CXX header — it is the runtime value backing `SetActiveShellCharacterData`.

Also from `meshinfo.txt` (probe log): the `SK_DarkForm_Bulk` mesh has **164 bones**, first bone
chain `Root, Pelvis, spine_01…spine_05, neck_01, neck_02, Head, clavicle_r, upperarm_r, lowerarm_r,
Hand_R, index_*, middle_*, thumb_*, pinky_*, ring_*, prop_r, wrist_inner_r, wrist_outer_r,
lowerarm_twist_01_r, lowerarm_twist_02_r, lowerarm_correctiveRoot_r, lowerarm_out_r`. That bone
list is also in `runs\20261022-061848-ingame\meshinfo.txt` verbatim.

---

## 4. `USpartaSkeletalMeshComponent` — the mesh owner

`/Script/Sparta.SpartaSkeletalMeshComponent`, parent
`/Script/AnimationBudgetAllocator.SkeletalMeshComponentBudgeted`. Instanced as `CharacterMesh0` on
every shell/pawn and as `WeaponMesh` on every player weapon (see §5.2).

| Property | Type | Offset |
|---|---|---|
| `BaseVisibleDistanceFactorThesholds` | `TArray<float>` | `0x1040` |
| `BaseVisibleDistanceFactorThesholdsMap` | `TMap<int32,float>` | `0x1050` |
| `bSkipKinematicBonesUpdateOnMovement` | `bool` | `0x10A0` |
| **`OnMeshCustomisationComplete`** | `MulticastInlineDelegateProperty` | `0x10B8` |
| `bIsCustomizationComplete` | `bool` | `0x10C8` |

| Function | Parameters |
|---|---|
| `SpawnClothElement` | (none) |
| `HandleMeshCustomisationComplete` | (none) |

Delegate signature (from `Sparta.hpp:7150` — the header names the signature method):
`FSpartaSkeletalMeshComponentOnMeshCustomisationComplete::OnSpartaMeshCustomisationComplete(class USpartaSkeletalMeshComponent* SkeletalMeshComponent)`.
The `DelegateFunction` object itself is **UNKNOWN** — tried
`parse_dump.py raw ... "SpartaSkeletalMeshComponentOnMeshCustomisationComplete"` → only the property
row matches; the `[df: 00007FF46F3D0B88]` target object is not in the dump.

Note the dump's field name is spelled `bIsCustomizationComplete` (American), while the CXX header
writes `bIsCustomizationComplete` too — but the class name is `SpartaMeshCustomisation` (British).
Both spellings are real; do not "fix" them.

Evidence:

```
[00007FF46F058338] Class /Script/Sparta.SpartaSkeletalMeshComponent [n: 4A5C5] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCCD38]
[000001F4ACDF4B80] MulticastInlineDelegateProperty /Script/Sparta.SpartaSkeletalMeshComponent:OnMeshCustomisationComplete [o: 10B8] [n: 5891FB] [c: 00007FF721DE9010] [owr: 00007FF46F058338] [df: 00007FF46F3D0B88]
[000001F4ACDF4C00] BoolProperty /Script/Sparta.SpartaSkeletalMeshComponent:bIsCustomizationComplete [o: 10C8] [n: 5891EE] [c: 00007FF721DE8D80] [owr: 00007FF46F058338]
```

### 4.1 `USpartaMeshCustomisation` — the `VAR_*` data assets

| Property | Type | Offset |
|---|---|---|
| `Defaults` | `FSpartaCustomisationTemplate` | `0x0030` |
| `Templates` | `TMap<FGameplayTag, FSpartaCustomisationTemplate>` | `0x0060` |

| Function | Parameters |
|---|---|
| `HasTemplate` | `Template: GameplayTag @0`, `ReturnValue: bool @8` |
| `ApplyCustomisationFromTemplate` | `Template: GameplayTag @0`, `SkeletalMeshComponent: SkeletalMeshComponent* @8`, `bAsync: bool @0x10`, `MetaTags: GameplayTagContainer @0x18` |
| `ApplyCustomisation` | `SkeletalMeshComponent: SkeletalMeshComponent* @0`, `bAsync: bool @8`, `MetaTags: GameplayTagContainer @0x10` |

Every asset of this type in the build (9 + the CDO):

| Asset |
|---|
| `/Game/Sparta/Characters/Enemies/Brigands/BrigBase/VAR_Brigand.VAR_Brigand` |
| `/Game/Sparta/Characters/Enemies/Brigands/BrigCrossbow/VAR_Brigand_Crossbow.VAR_Brigand_Crossbow` |
| `/Game/Sparta/Characters/Enemies/Brigands/HordeBrigand/VAR_HeadKing_Villager.VAR_HeadKing_Villager` |
| `/Game/Sparta/Characters/Enemies/Brigands/HordeBrigand/VAR_Horde_Brigand.VAR_Horde_Brigand` |
| `/Game/Sparta/Characters/Enemies/Brigands/HordeBrigand/VAR_Horde_Brigand_Ghost.VAR_Horde_Brigand_Ghost` |
| `/Game/Sparta/Characters/Enemies/Brigands/HordeGrenadier/VAR_Horde_Brigand_Grenadier.VAR_Horde_Brigand_Grenadier` |
| `/Game/Sparta/Characters/Enemies/Brigands/HordeShieldBrigand/VAR_Horde_Brigand_Shield.VAR_Horde_Brigand_Shield` |
| `/Game/Sparta/Characters/Enemies/TarredCorpse/VAR_TarredCorpse.VAR_TarredCorpse` |
| `/Game/Sparta/Characters/Enemies/Wraith/VAR_Wraith.VAR_Wraith` |

**No `VAR_*` asset exists for any player shell** — so the player's armour-piece variation goes
through `SpartaCharacterData::MeshVariationData` (§1.3) pointing at one of the above, or is unused.
UNKNOWN which; the pointer value is not in the dump.

Evidence:

```
[00007FF46F04FF38] Class /Script/Sparta.SpartaMeshCustomisation [n: 49AD0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[000001F49CD2FCE0] MapProperty /Script/Sparta.SpartaMeshCustomisation:Templates [o: 60] [n: 58872B] [c: 00007FF721DE8F70] [owr: 00007FF46F04FF38] [kp: 000001F4ACDB7E80] [vp: 000001F4ACDB7E00]
[000001F4ACDB8080] ObjectProperty /Script/Sparta.SpartaMeshCustomisation:ApplyCustomisationFromTemplate:SkeletalMeshComponent [o: 8] [n: 27494] [c: 00007FF721DE9420] [owr: 00007FF46F707538] [pc: 00007FF46EE96F38]
```

### 4.2 Other mesh-related helpers in `Sparta`

| Class | Function | Parameters |
|---|---|---|
| `/Script/Sparta.SpartaSocketMovementPrediction` | `GetSocketMovementPrediction` | `WorldContextObject: const UObject*`, `SocketName: FName`, `MeshComp: USkeletalMeshComponent*`, `PredictionTime: float` |
| `/Script/Sparta.SpartaItemManagerComponent` | `SkeletalMeshInitialized` | `SkeletalMeshComponent: USpartaSkeletalMeshComponent*` |
| ” | `AddSkeletalMeshProxy` / `RemoveSkeletalMeshProxy` | `SkeletalMeshComponent: USkeletalMeshComponent*` |
| `/Script/Sparta.SocketAdjustmentData` (`UTypeSpecificData`) | `GetAdjustmentForSocket` | `SocketName: FName`; data: `SocketAdjustments: TMap<FName, FTransform> @0x0028` |
| `/Script/Sparta.SpartaWeaponThrowSampler` | `StartManualCapture` | `InMesh: USkeletalMeshComponent*`, `InSocketName: FName` |
| ” | `StartCapture` | `InMesh: USkeletalMeshComponent*`, `InSocketName: FName`, `DurationSeconds: float` |
| `/Script/Sparta.SpartaObj` (`PlayMontageOnSkeletalMeshWithNotifies`) | see `Sparta.hpp:2506` | `Mesh: USkeletalMeshComponent*`, `TaskInstanceName: FName`, `MontageToPlay: UAnimMontage*`, `Rate: float`, `StartSection: FName`, `StartTime: float` |

`USocketAdjustmentData` (`Sparta.hpp:3605`) is important: it is a per-character `TMap<FName, FTransform>`
of socket corrections. `AWP_WeaponBase_C::GetCharacterSocketAdjustment(FName Socket)` and
`GetModifiedSocketTransform(FName SocketName, FTransform& Result)` (§5.5) are the consumers.

---

## 5. Player weapons

### 5.1 Class hierarchy

`/Game/Sparta/Core/Weapons/WP_WeaponBase.WP_WeaponBase_C` (parent `/Script/Sparta.SpartaWeapon`) is
the root of every player weapon.

| Base class | Parent | Notes |
|---|---|---|
| `/Game/Sparta/Core/Weapons/WP_WeaponBase.WP_WeaponBase_C` | `/Script/Sparta.SpartaWeapon` | sockets, equip state, anim layers, VFX getters |
| `/Game/Sparta/Core/Weapons/WP_WeaponBase_Static.WP_WeaponBase_Static_C` | `…WP_WeaponBase_C` | adds `SM_Weapon: UStaticMeshComponent*` @ `0x05F8`, `Niagara: UNiagaraComponent*` @ `0x05E0`, `TrailLocation: USphereComponent*` @ `0x05E8`, `DefaultCollisionShape: UCapsuleComponent*` @ `0x05F0`, `PermanentVFX_0: UNiagaraComponent*` @ `0x0600`, `isAttacking: bool` @ `0x0608`, `CustomWeaponCollisions: TArray<UPrimitiveComponent*>` @ `0x0610` |
| `/Game/Sparta/Core/Weapons/WP_WeaponBase_Static_Ghost.WP_WeaponBase_Static_Ghost_C` | `…WP_WeaponBase_Static_C` | ghost/memory variant (6 classes use it) |
| `/Game/Sparta/Core/Weapons/WP_SidearmBase.WP_SidearmBase_C` | `…WP_WeaponBase_C` | adds `SpartaWeaponComponent_Ranged*` @ `0x05E0`, `Ammunition Component: UBPC_Ammunition_C*` @ `0x05E8`, `Arrow: UArrowComponent*` @ `0x05F0`, **`ProjectileSpawnSocket: FName` @ `0x05F8`**, `DefaultProjectile: TSubclassOf<ABP_ProjectileBase_C>` @ `0x0600`, `BlendSpace: UBlendSpace*` @ `0x0608`, `LocomotionAnimLayer: TArray<TSubclassOf<UAnimInstance>>` @ `0x0610`, `CameraState: TSubclassOf<UCSCameraState>` @ `0x0620`, `OptionalMovementSpeed: double` @ `0x0628`, `CharacterRelativeAimOffset: FVector` @ `0x0630` |

### 5.2 Every player weapon class (58) with its CDO mesh component

Every one of the 58 has exactly one `SkeletalMeshComponent` subobject named **`WeaponMesh`**
(`pl-weaponcdo.sql`, `pl-wpcomps.sql`). 46 of them additionally have
`DefaultCollisionShape_GEN_VARIABLE: CapsuleComponent`; all 58 have
`DefaultSceneRoot_GEN_VARIABLE: SceneComponent`; **none** has an `SM_Weapon` StaticMeshComponent
subobject in the dump (it is declared on `WP_WeaponBase_Static_C` but is not dumped as a subobject —
probably created by the `InheritableComponentHandler`, which does appear: 15 `InheritableComponentHandler`
rows under `Core/Weapons/Player/`).

| Weapon class (`/Game/Sparta/Core/Weapons/Player/…`) | Parent | Mesh comp |
|---|---|---|
| `Axatana/WP_Axatana_Axe.WP_Axatana_Axe_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `Axatana/WP_Axatana_Katana_Left.WP_Axatana_Katana_Left_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `Axatana/WP_Axatana_Katana_Right.WP_Axatana_Katana_Right_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `Axatana/WP_Axatana_Katana_Right_Spawnable.WP_Axatana_Katana_Right_Spawnable_C` | `…WP_Axatana_Katana_Right_C` | `WeaponMesh` |
| `Ballistazooka/WP_Ballistazooka.WP_Ballistazooka_C` | `WP_SidearmBase_C` | `WeaponMesh` |
| `BattleAxe/WP_BattleAxe.WP_BattleAxe_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `BattleAxe/WP_BattleAxe_Ghost.WP_BattleAxe_Ghost_C` | `WP_WeaponBase_Static_Ghost_C` | `WeaponMesh` |
| `BlackNeedle/WP_BlackNeedle.WP_BlackNeedle_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `BlackNeedle/WP_BlackNeedle_Ghost.WP_BlackNeedle_Ghost_C` | `WP_WeaponBase_Static_Ghost_C` | `WeaponMesh` |
| `BlackNeedle/WP_SimpleSpear.WP_SimpleSpear_C` | `…WP_BlackNeedle_C` | `WeaponMesh` |
| `ClockworkScythe/WP_ClockworkScythe.WP_ClockworkScythe_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `Crossbow/WP_Crossbow.WP_Crossbow_C` | `WP_SidearmBase_C` | `WeaponMesh` |
| `CursedChild/WP_CursedChild.WP_CursedChild_C` | `WP_SidearmBase_C` | `WeaponMesh` |
| `DaggerAxe/WP_DaggerAxe_Axe.WP_DaggerAxe_Axe_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `DaggerAxe/WP_DaggerAxe_Axe_Ghost.WP_DaggerAxe_Axe_Ghost_C` | `WP_WeaponBase_Static_Ghost_C` | `WeaponMesh` |
| `DaggerAxe/WP_DaggerAxe_Dagger.WP_DaggerAxe_Dagger_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `DaggerAxe/WP_DaggerAxe_Dagger_Ghost.WP_DaggerAxe_Dagger_Ghost_C` | `WP_WeaponBase_Static_Ghost_C` | `WeaponMesh` |
| `DarkBro/WP_AlienHeart.WP_AlienHeart_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `DarkBro/WP_Body_ShoulderBash.WP_Body_ShoulderBash_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `DarkBro/WP_Fist_L.WP_Fist_L_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `DarkBro/WP_Fist_L_Memory.WP_Fist_L_Memory_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `DarkBro/WP_Fist_R.WP_Fist_R_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `DarkBro/WP_Fist_R_Memory.WP_Fist_R_Memory_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `DarkBro/WP_Kick_L.WP_Kick_L_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `DarkBro/WP_Kick_R.WP_Kick_R_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `HadernsSword/WP_HadernsSword.WP_HadernsSword_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `HadernsSword/WP_HadernsSword_SarcophagusSword.WP_HadernsSword_SarcophagusSword_C` | `WP_WeaponBase_Static_Ghost_C` | `WeaponMesh` |
| `HadernsSword/WP_SarcophagusSword.WP_SarcophagusSword_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `HallowedSword/WP_HallowedSword_Right.WP_HallowedSword_Right_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `HammerChisel/WP_HammerChisel_Chisel.WP_HammerChisel_Chisel_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `HammerChisel/WP_HammerChisel_Hammer.WP_HammerChisel_Hammer_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `HeavyHammer/WP_HeavyHammer.WP_HeavyHammer_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `InfiniteSeal/WP_InfiniteSeal.WP_InfiniteSeal_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `InfiniteSeal/WP_SlayerSeal.WP_SlayerSeal_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `InfiniteSeal/WP_StoneSeal.WP_StoneSeal_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `InfiniteSeal/WP_TarnishedSeal.WP_TarnishedSeal_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `MachineGun/WP_MachineGun.WP_MachineGun_C` | `WP_SidearmBase_C` | `WeaponMesh` |
| `MartyrsBlade/WP_MartyrsBlade.WP_MartyrsBlade_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `MemoryWeapons/WP_CannibalSword_Memory.WP_CannibalSword_Memory_C` | `WP_WeaponBase_Static_Ghost_C` | `WeaponMesh` |
| `MemoryWeapons/WP_IncompleteScythe_Ghost.WP_IncompleteScythe_Ghost_C` | `WP_WeaponBase_Static_Ghost_C` | `WeaponMesh` |
| `MemoryWeapons/WP_MachineGun_Memory.WP_MachineGun_Memory_C` | `…WP_MachineGun_C` | `WeaponMesh` |
| `MemoryWeapons/WP_MachineGun_Memory_NoFade.WP_MachineGun_Memory_NoFade_C` | `…WP_MachineGun_C` | `WeaponMesh` |
| `MemoryWeapons/WP_MartyrsSword_Memory.WP_MartyrsSword_Memory_C` | `WP_WeaponBase_Static_Ghost_C` | `WeaponMesh` |
| `MemoryWeapons/WP_SarcophagusSword_Memory_Stowed_NoFade.WP_SarcophagusSword_Memory_Stowed_NoFade_C` | `…WP_SarcophagusSword_C` | `WeaponMesh` |
| `MemoryWeapons/WP_TrainingSpear.WP_TrainingSpear_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `MemoryWeapons/WP_Wraith_Sword_Memory.WP_Wraith_Sword_Memory_C` | `WP_WeaponBase_Static_Ghost_C` | `WeaponMesh` |
| `NailShotgun/WP_NailShotgun.WP_NailShotgun_C` | `WP_SidearmBase_C` | `WeaponMesh` |
| `ParasiteGun/WP_ParasiteGun.WP_ParasiteGun_C` | `WP_SidearmBase_C` | `WeaponMesh` |
| `SheepTransformation/WP_Sheep_Arm_Player_L.WP_Sheep_Arm_Player_L_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `SheepTransformation/WP_Sheep_Arm_Player_R.WP_Sheep_Arm_Player_R_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `Shells/Eredrim/WP_Eredrim_Diapason.WP_Eredrim_Diapason_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `Shells/Eredrim/WP_Eredrim_Diapason_Obsidian.WP_Eredrim_Diapason_Obsidian_C` | `…WP_Eredrim_Diapason_C` | `WeaponMesh` |
| `Shells/Genessa/WP_Genessa_Catalyst.WP_Genessa_Catalyst_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `Shells/Gragu/WP_Gragu_Punch_R.WP_Gragu_Punch_R_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `Shells/Tiel/Dagger/WP_Tiel_Dagger_Right.WP_Tiel_Dagger_Right_C` | `WP_WeaponBase_Static_C` | `WeaponMesh` |
| `Shells/Tiel/Dagger/WP_Tiel_Dagger_Right_Ghost.WP_Tiel_Dagger_Right_Ghost_C` | `WP_WeaponBase_Static_Ghost_C` | `WeaponMesh` |
| `SimpleLute/WP_Lute_Simple.WP_Lute_Simple_C` | `WP_SidearmBase_C` | `WeaponMesh` |
| `Trebuchaxe/WP_Trebuchaxe.WP_Trebuchaxe_C` | `WP_SidearmBase_C` | `WeaponMesh` |

Additional classes in that tree that are **not** weapons:
`SimpleLute/BPI_Lute.BPI_Lute_C` (interface, parent `/Script/CoreUObject.Interface`),
`Ballistazooka/ABP_Ballistazooka.ABP_Ballistazooka_C` (parent `/Script/Engine.AnimInstance`),
`Ballistazooka/ABPL_Locomotion_ADS_Ballistazooka.ABPL_Locomotion_ADS_Ballistazooka_C`
(parent `/Game/Sparta/Core/Animations/Layers/ABPL_Locomotion_Blendspace.ABPL_Locomotion_Blendspace_C`),
`_Shared/DA_ProjectileAudioData_Player.DA_ProjectileAudioData_Player_C`,
`_Shared/DA_WeaponAudioData_Melee_Player.DA_WeaponAudioData_Melee_Player_C`,
`_Shared/DA_WeaponAudio_Data_Ranged_Player.DA_WeaponAudio_Data_Ranged_Player_C`.
One more player weapon lives outside that folder:
`/Game/Sparta/Core/Characters/Player/KnightLady/Abilities/WP_KnightLady_Hook_Left.WP_KnightLady_Hook_Left_C`
(has `SM_Weapon_GEN_VARIABLE: StaticMeshComponent`, confirming the static-mesh component name).

Evidence:

```
[000001F4C3DA3FA0] BlueprintGeneratedClass /Game/Sparta/Core/Weapons/WP_WeaponBase.WP_WeaponBase_C [n: 11D75D] [c: 00007FF46EEC3D38] [or: 000001F4D1C19F70] [sps: 00007FF46F05CD38]
[000001F8DD7FBC50] BlueprintGeneratedClass /Game/Sparta/Core/Weapons/Player/HadernsSword/WP_HadernsSword.WP_HadernsSword_C [n: 3B5672] [c: 00007FF46EEC3D38] [or: 000001F8DDAC3620] [sps: 000001F4C3DA3C10]
[000001F8D89BEEF0] SkeletalMeshComponent /Game/Sparta/Core/Weapons/Player/HadernsSword/WP_HadernsSword.Default__WP_HadernsSword_C:WeaponMesh [n: 589676] [c: 00007FF46EE96F38] [or: 000001F8D29C9700]
```

### 5.3 `ASpartaWeapon` — properties, delegates, functions (`Sparta.hpp:2176-2225`, size `0x4B8`)

| Property | Type | Offset |
|---|---|---|
| `AudioDataChanged` (delegate) | `FSpartaWeaponAudioDataChanged` | `0x02B8` |
| `AdditionalWeaponToSpawn` | `TSubclassOf<ASpartaWeapon>` | `0x02C8` |
| **`WeaponMesh`** | `USkeletalMeshComponent*` | `0x02D0` |
| `AudioData` | `USpartaWeaponAudioData*` | `0x02D8` |
| `WeaponComponents` | `TArray<USpartaWeaponComponent_Base*>` | `0x02E0` |
| **`WeaponVariants`** | `TArray<FWeaponVariantData>` | `0x02F0` |
| `GrantedAbilitySet` | `FSpartaAbilitySet` | `0x0300` |
| `UpgradeStatValues` | `TMap<FGameplayTag,float>` | `0x0330` |
| `ElementalEfficiency` | `TMap<FGameplayTag,int32>` | `0x0380` |
| `WeaponSlot` | `FGameplayTag` | `0x03D0` |
| `WeaponTag` | `FGameplayTag` | `0x03D8` |
| `EquipmentTag` | `FGameplayTag` | `0x03E0` |
| `bIsMainHand` | `bool` | `0x03E8` |
| `WeaponName` | `FName` | `0x03EC` |
| `EquipDelegate` (delegate) | `FSpartaWeaponEquipDelegate` | `0x03F8` |
| `UnequipDelegate` (delegate) | `FSpartaWeaponUnequipDelegate` | `0x0408` |
| **`PutInHandDelegate`** (delegate) | `FSpartaWeaponPutInHandDelegate` | `0x0418` |
| **`StowDelegate`** (delegate) | `FSpartaWeaponStowDelegate` | `0x0428` |
| `HitDelegate` (delegate) | `FSpartaWeaponHitDelegate` | `0x0438` |
| `BaseDamage` / `ShellSummonDamage` / `BasePoiseDamage` / `BaseBreakDamage` | `float` | `0x0448/44C/450/454` |
| `FaceToTargetInterpSpeed` | `float` | `0x0458` |
| `ShellReviveEfficiency` | `float` | `0x045C` |
| `ResolveGainMultiplier` / `UnityGainMultiplier` | `float` | `0x0460/464` |
| `HoldingCharacter` | `ASpartaCharacter*` | `0x0468` |
| `GrantedAbilitySetHandles` | `FSpartaAbilitySet_GrantedHandles` | `0x0478` |
| `AdditionalWeapon` | `ASpartaWeapon*` | `0x04B0` |

| Function | Full parameters (declaration order, `Sparta.hpp`) |
|---|---|
| **`Equip`** | `Character: ASpartaCharacter*`, `bGrantAbilities: bool` |
| **`Unequip`** | `Character: ASpartaCharacter*`, `bDestroy: bool` |
| **`PutInHand`** | `Socket: FName` |
| **`Stow`** | `Socket: FName` |
| `HideWeapon` | `bHide: bool` |
| `IsWeaponHidden` | – |
| `SetAudioData` | `in_AudioData: USpartaWeaponAudioData*` |
| `WeaponHit` | `HitPayload: USpartaHitPayload*`, `HitResult: FHitResult`, `DamagingWeapon: const ASpartaWeapon*`, `InInstigator: ASpartaCharacter*` |
| `InitialiseCinematicProxyMesh` | `CinematicCharacterMeshComponent: USkeletalMeshComponent*`, `CinematicWeaponMeshComponent: UMeshComponent*` |
| `GetDefaultCollisionShapes` | – |

Delegate signatures (from the header's signature-method names):

| Delegate | Signature |
|---|---|
| `FSpartaWeaponEquipDelegate` | `WeaponEquipDelegate(ASpartaCharacter* Character)` |
| `FSpartaWeaponUnequipDelegate` | `WeaponEquipDelegate(ASpartaCharacter* Character)` (header reuses the name — a codegen artefact) |
| `FSpartaWeaponPutInHandDelegate` | `WeaponInHandDelegate()` |
| `FSpartaWeaponStowDelegate` | `WeaponInHandDelegate()` (same artefact) |
| `FSpartaWeaponHitDelegate` | `WeaponHitDelegate(const USpartaHitPayload* HitPayload, const FHitResult HitResult, const ASpartaWeapon* DamagingWeapon)` |
| `FSpartaWeaponAudioDataChanged` | `WeaponAudioDataChanged(USpartaWeaponAudioData* in_AudioData)` |

Evidence:

```
[000001F4ACDFCF80] ObjectProperty /Script/Sparta.SpartaWeapon:WeaponMesh [o: 2D0] [n: 589676] [c: 00007FF721DE9420] [owr: 00007FF46F05CD38] [pc: 00007FF46EE96F38]
[000001F4ACDFD200] ArrayProperty /Script/Sparta.SpartaWeapon:WeaponVariants [o: 2F0] [n: 589665] [c: 00007FF721DE8910] [owr: 00007FF46F05CD38] [ai: 000001F4ACDFD180]
[000001F4ACDFB600] MulticastInlineDelegateProperty /Script/Sparta.SpartaWeapon:PutInHandDelegate [o: 418] [n: 589625] [c: 00007FF721DE9010] [owr: 00007FF46F05CD38] [df: 00007FF46F3D4D88]
[000001F4ACDFB680] MulticastInlineDelegateProperty /Script/Sparta.SpartaWeapon:StowDelegate [o: 428] [n: 58961E] [c: 00007FF721DE9010] [owr: 00007FF46F05CD38] [df: 00007FF46F3D4D88]
```

### 5.4 `UWeaponsComponent` — the slot inventory (`Sparta.hpp:8250-8278`)

| Property | Type | Offset |
|---|---|---|
| **`DefaultWeapons`** | `TArray<TSoftClassPtr<ASpartaWeapon>>` | `0x00B8` |
| `OnWeaponEquipped` (delegate) | `FWeaponsComponentOnWeaponEquipped` | `0x00C8` |
| `OnWeaponUnequipped` (delegate) | `FWeaponsComponentOnWeaponUnequipped` | `0x00D8` |
| `OnDefaultWeaponsSpawned` (delegate) | `FWeaponsComponentOnDefaultWeaponsSpawned` | `0x00E8` |
| **`EquippedWeapons`** | `TMap<FGameplayTag, ASpartaWeapon*>` | `0x00F8` |

| Function | Parameters |
|---|---|
| `EquipWeaponToSlot` | `Weapon: ASpartaWeapon*`, `bGrantAbilities: bool` |
| `SpawnAndEquipWeaponOfClass` | `WeaponClass: TSubclassOf<ASpartaWeapon>` |
| `GetWeaponInSlot` | `WeaponSlot: FGameplayTag` |
| `GetWeaponsInSlots` | `WeaponSlots: FGameplayTagContainer` |
| `GetWeaponTypeFromSlot` | `WeaponSlot: FGameplayTag` |
| `GetWeaponInHand` | – |
| `GetAllWeapons` | – |
| `PutInHandsWeaponsInSlots` | `WeaponSlots: FGameplayTagContainer` |
| `StowWeaponsInSlots` | `WeaponSlots: FGameplayTagContainer` |
| `ShowWeaponsInSlots` / `HideWeaponsInSlots` | `WeaponSlots: FGameplayTagContainer` |
| `HideAllWeapons` | `bHide: bool` |
| `RemoveWeaponFromSlot` | `WeaponSlot: FGameplayTag` |
| `RemoveWeaponsFromSlot` | `WeaponSlots: FGameplayTagContainer` |
| `RemoveAllWeapons` | – |
| `OnWeaponDestroyed` | `DestroyedActor: AActor*` |
| `OnOwnerPossess` | `Pawn: APawn*`, `OldController: AController*`, `NewController: AController*` |

Delegate signatures: `OnWeaponEquipped(ASpartaWeapon* Weapon)`,
`OnWeaponUnequipped(ASpartaWeapon* Weapon)`, `OnDefaultWeaponsSpawned()`.

Evidence:

```
[000001F4ACE1D200] ArrayProperty /Script/Sparta.WeaponsComponent:DefaultWeapons [o: B8] [n: 58A07B] [c: 00007FF721DE8910] [owr: 00007FF46F063938] [ai: 000001F4ACE1D180]
[000001F4ACE1D180] SoftClassProperty DefaultWeapons./Script/Sparta.WeaponsComponent:DefaultWeapons [o: 0] [n: 58A07B] [c: 00007FF721DF97A0] [owr: 000001F4ACE1D200] [mc: 00007FF46F05CD38]
[000001F4ACE20E20] MapProperty /Script/Sparta.WeaponsComponent:EquippedWeapons [o: F8] [n: 58A05A] [c: 00007FF721DE8F70] [owr: 00007FF46F063938] [kp: 000001F4ACE1D480] [vp: 000001F4ACE1D400]
```

Because `EquippedWeapons` is keyed by `FGameplayTag` **slot**, the natural sync unit for weapons is
`(slot tag → weapon tag/class + equip state)`.

### 5.5 Weapon sub-components

| Class | Parent | Properties |
|---|---|---|
| `/Script/Sparta.SpartaWeaponComponent_Base` | `UActorComponent` | `AudioComponentName: FName @0xB8`, `WeaponAudioComponent: UAdvancedAudioComponent* @0xC0`, `Weapon: ASpartaWeapon* @0xC8` |
| `/Script/Sparta.SpartaWeaponComponent_Melee` | `_Base` | `DamageTags: FGameplayTagContainer @0xE0`, `bShouldUseDynamicWhooshAudioTriggers: bool @0x0100`, `Hit`(delegate)`@0x0108`, `WhooshStarted`@`0x0118`, `WhooshStopped`@`0x0128`, `ChargeStarted`@`0x0138`, `ChargeCompleted`@`0x0148`, `ChargeCanceled`@`0x0158`, `MeleeAudioData: USpartaMeleeWeaponAudioBehavior* @0x0168` |
| `/Script/Sparta.SpartaWeaponComponent_Ranged` | `_Base` | `DamageTags @0xE0`, `Fired`@`0x0100`, `FireStarted`@`0x0110`, `FireEnded`@`0x0120`, `DryFired`@`0x0130`, `ChargeBegin`@`0x0140`, `ChargeEnd`@`0x0150`, `ChargeCanceled`@`0x0160`, `RangedAudioBehavior: USpartaRangedWeaponAudioBehavior* @0x0170`, `FiringEventId: FAudioEventId @0x0178`, `ChargeAudioPlayingId: FAudioEventId @0x0180` |
| `/Script/Sparta.SpartaWeaponComponent_Effect` | `_Base` | `EffectAudioData: USpartaWeaponEffectsAudioData* @0xD8`, `EquippedPlayingId`/`WhooshPlayingId`/`ChargePlayingId`: `FAudioEventId` @ `0xE0/E8/F0` |

`USpartaWeaponComponent_Base` functions: `Uninitialize()`, `SetAudioSwitchOnWeapon(SwitchGroup: UAudioSwitchGroup*, SwitchValue: const FAudioSwitchState)`,
`PlaySoundOnWeapon(Event: const UAudioEvent*, SoundModifiers: FAudioEventSoundModifierOverrides)`,
`OnWeaponUnequip(Character: ASpartaCharacter*)`, `OnWeaponStow()`, `OnWeaponPutInHand()`,
`OnWeaponHit(HitPayload: const USpartaHitPayload*, HitResult: FHitResult, DamagingWeapon: const ASpartaWeapon*)`,
`OnWeaponEquip(Character: ASpartaCharacter*)`, `OnWeaponAudioDataChanged(in_AudioData: USpartaWeaponAudioData*)`.

`USpartaWeaponComponent_Melee` visual functions (**these are the ones with sockets/VFX**):

| Function | Parameters |
|---|---|
| `GetCurrentFXSocketWorldVelocity` | – |
| `GetCurrentFXSocketRelativeVelocity` | – |
| `StartWhooshFX` | `in_AttackType: const ESpartaAttackType`, `SoundModifiers: FAudioEventSoundModifierOverrides` |
| `StopWhooshFX` | `in_AttackType: const ESpartaAttackType` |
| `StartWhooshAudio` / `StopWhooshAudio` | `in_AttackType: const ESpartaAttackType` (+ modifiers for `StartWhooshAudio`) |
| `UpdateWeaponMovementTracking` | `DeltaSeconds: const float` |
| `HandleDynamicWhoosh` | `BoneSpeed: double` |
| `ChargeStartVFX` / `ChargeStartSFX` / `ChargeStartFX` | – |
| `ChargeCompleteVFX` / `ChargeCompleteSFX` / `ChargeCompleteFX` | – |
| `ChargeCancelVFX` / `…SFX` / `…FX` | – |
| `OnStateAttackMeleeChanged` | `in_GameplayTag: FGameplayTag`, `in_Count: int32` |

`USpartaWeaponComponent_Effect` exposes the full VFX/SFX matrix:
`PlayWeaponEquippedVFX/SFX`, `PlayWeaponUnequippedVFX/SFX`, `PlayWeaponDrawnVFX/SFX`,
`PlayWeaponStowedVFX/SFX`, `PlayMeleeWeaponWhooshVFX(ESpartaAttackType)/SFX`,
`PlayMeleeWeaponHitVFX(float Damage)/SFX`, `PlayRangedWeaponFiredVFX/SFX`,
`PlayRangedWeaponDryFiredVFX/SFX`, `PlayRangedWeaponChargeVFX/SFX`, `ResetWeapon()`,
`SetEffectAudioData(USpartaWeaponEffectsAudioData*)`.

Evidence:

```
[00007FF46F05DB38] Class /Script/Sparta.SpartaWeaponComponent_Base [n: 4AB94] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F05DF38] Class /Script/Sparta.SpartaWeaponComponent_Melee [n: 4AC56] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F05DB38]
[00007FF46F063938] Class /Script/Sparta.WeaponsComponent [n: 4B21A] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
```

Note the class addresses: `SpartaWeaponComponent_Base` = `0x00007FF46F05DB38`,
`SpartaWeaponComponent_Melee` = `0x00007FF46F05DF38` (its `[sps:]` parent is the `_Base` address
`…05DB38`, which is easy to mistake for the child's own address when reading a two-column
`classes` listing — always read the leading `[addr]` field).

### 5.6 Player weapon static meshes — every `StaticMesh` under `/Game/Sparta/Weapons/Player/` (48)

`scripts\q\pl-playerweaponsm.sql`

| StaticMesh |
|---|
| `/Game/Sparta/Weapons/Player/Axatana/Katanas/Katanas/SM_Axatana_Axe_Offset.SM_Axatana_Axe_Offset` |
| `/Game/Sparta/Weapons/Player/Axatana/Katanas/Katanas/SM_Axatana_Katana_L_Model.SM_Axatana_Katana_L_Model` |
| `/Game/Sparta/Weapons/Player/Axatana/Katanas/Katanas/SM_Axatana_Katana_R_Model.SM_Axatana_Katana_R_Model` |
| `/Game/Sparta/Weapons/Player/Axatana/SM_Axatana_Idle.SM_Axatana_Idle` |
| `/Game/Sparta/Weapons/Player/Axe_Dagger/SM_Weap_Player_Axe.SM_Weap_Player_Axe` |
| `/Game/Sparta/Weapons/Player/Axe_Dagger/SM_Weap_Player_Dagger.SM_Weap_Player_Dagger` |
| `/Game/Sparta/Weapons/Player/Axe_Dagger/SM_Weap_Player_Dagger_New.SM_Weap_Player_Dagger_New` |
| `/Game/Sparta/Weapons/Player/Axe_Dagger/SM_Weap_Player_Dagger_New_LoRes.SM_Weap_Player_Dagger_New_LoRes` |
| `/Game/Sparta/Weapons/Player/BattleAxe/SM_Gragu_BattleAxe_01.SM_Gragu_BattleAxe_01` |
| `/Game/Sparta/Weapons/Player/Clockwork_Scythe/Art/Mesh/SM_Clockwork_Scythe.SM_Clockwork_Scythe` |
| `/Game/Sparta/Weapons/Player/Clockwork_Scythe/Art/Mesh/SM_Scythe-Saw_ShellMemory.SM_Scythe-Saw_ShellMemory` |
| `/Game/Sparta/Weapons/Player/CursedChild/SM_CursedChildStatic.SM_CursedChildStatic` |
| `/Game/Sparta/Weapons/Player/GenessasIncenseBurner/Art/Mesh/SM_GenessasIncenseBurner.SM_GenessasIncenseBurner` |
| `/Game/Sparta/Weapons/Player/GenessasIncenseBurner/Art/Mesh/SM_GenessasIncenseBurner_Body.SM_GenessasIncenseBurner_Body` |
| `/Game/Sparta/Weapons/Player/GenessasIncenseBurner/Art/Mesh/SM_GenessasIncenseBurner_Handle.SM_GenessasIncenseBurner_Handle` |
| `/Game/Sparta/Weapons/Player/GiantSword_AltSkin/SM_GiantSword_AltSkin.SM_GiantSword_AltSkin` |
| `/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01` |
| `/Game/Sparta/Weapons/Player/HadernsSword/SM_HadernsSword_Display.SM_HadernsSword_Display` |
| `/Game/Sparta/Weapons/Player/HadernsSword/SM_SarcophagusSword_Model.SM_SarcophagusSword_Model` |
| `/Game/Sparta/Weapons/Player/HallowedSword/SM_HallowedSword_01.SM_HallowedSword_01` |
| `/Game/Sparta/Weapons/Player/HammerChisel/SM_HammerChisel_Chisel_01.SM_HammerChisel_Chisel_01` |
| `/Game/Sparta/Weapons/Player/HammerChisel/SM_HammerChisel_Hammer_01.SM_HammerChisel_Hammer_01` |
| `/Game/Sparta/Weapons/Player/HeavyHammer/Art/Mesh/SM_ObsidianHammer_01.SM_ObsidianHammer_01` |
| `/Game/Sparta/Weapons/Player/Infinite_Seal/SM_Weap_Infinite_Seal.SM_Weap_Infinite_Seal` |
| `/Game/Sparta/Weapons/Player/MS1_TarnishedSeal/TarnishedSeal.TarnishedSeal` |
| `/Game/Sparta/Weapons/Player/MartyrsBlade/SM_MartyrsBlade_01_Broken.SM_MartyrsBlade_01_Broken` |
| `/Game/Sparta/Weapons/Player/MartyrsBlade/SM_MartyrsBlade_01_Broken_End.SM_MartyrsBlade_01_Broken_End` |
| `/Game/Sparta/Weapons/Player/Nail_Shotgun/Mesh/Nail_Shotgun_StaticMesh.Nail_Shotgun_StaticMesh` |
| `/Game/Sparta/Weapons/Player/ParasiteGun/Projectile/SM_Spike_Bullet_01.SM_Spike_Bullet_01` |
| `/Game/Sparta/Weapons/Player/ParasiteGun/Projectile/SM_Spike_Bullet_02.SM_Spike_Bullet_02` |
| `/Game/Sparta/Weapons/Player/ParasiteGun/Projectile/SM_Spike_Bullet_03.SM_Spike_Bullet_03` |
| `/Game/Sparta/Weapons/Player/Proxima/Art/MeshV2/SM_Proxima_MG_Barrel.SM_Proxima_MG_Barrel` |
| `/Game/Sparta/Weapons/Player/Proxima/Art/MeshV2/SM_Proxima_MG_Base.SM_Proxima_MG_Base` |
| `/Game/Sparta/Weapons/Player/Proxima/Art/MeshV2/SM_Proxima_MG_Proxy_V2.SM_Proxima_MG_Proxy_V2` |
| `/Game/Sparta/Weapons/Player/Proxima/Art/SK/SM_Proxima_barrel.SM_Proxima_barrel` |
| `/Game/Sparta/Weapons/Player/Proxima/Art/SK/SM_Proxima_base.SM_Proxima_base` |
| `/Game/Sparta/Weapons/Player/SlayerSeal/StaticMesh/SM_SlayerSeal_Model.SM_SlayerSeal_Model` |
| `/Game/Sparta/Weapons/Player/Spear_BlackNeedle/Mesh/SM_BlackNeedle.SM_BlackNeedle` |
| `/Game/Sparta/Weapons/Player/Spear_BlackNeedle/Proxy/BlackNeedle.BlackNeedle` |
| `/Game/Sparta/Weapons/Player/Tarsus_GiantSword/Mesh/Tarsus_Sword_StaticMesh.Tarsus_Sword_StaticMesh` |
| `/Game/Sparta/Weapons/Player/TielDagger/Art/Mesh/SM_TielDagger.SM_TielDagger` |
| `/Game/Sparta/Weapons/Player/Tiel_Crossbow/SM_Weap_Tiel_Crossbow_1_3.SM_Weap_Tiel_Crossbow_1_3` |
| `/Game/Sparta/Weapons/Player/Tiel_Crossbow/SM_Weap_Tiel_Crossbow_Display.SM_Weap_Tiel_Crossbow_Display` |
| `/Game/Sparta/Weapons/Player/Tiel_Crossbow/TielCrossbow_Bolt.TielCrossbow_Bolt` |
| `/Game/Sparta/Weapons/Player/Trebuchaxe/Art/Mesh/SM_Trebuchaxe_Display.SM_Trebuchaxe_Display` |
| `/Game/Sparta/Weapons/Player/Trebuchaxe/Art/Mesh/SM_Trebuchaxe_Projectile.SM_Trebuchaxe_Projectile` |
| `/Game/Sparta/Weapons/Player/UntarnishedSeal/StaticMesh/SM_UntarnishedSeal_Model.SM_UntarnishedSeal_Model` |
| `/Game/Sparta/Weapons/Player/VatraSeal/StaticMesh/SM_VatraSeal_Model.SM_VatraSeal_Model` |

Which `WP_` class uses which `SM_` cannot be read from the dump (`SM_Weapon.Mesh` is a value).
`GUESS:` the folder name (`HadernsSword/` → `SM_HadernsSword_01`) is the mapping in all but the
Proxima/GiantSword/Seal cases.

Player weapon **skeletal** meshes + skeletons under `/Game/Sparta/Weapons/Player/`:

| SkeletalMesh | Skeleton |
|---|---|
| `/Game/Sparta/Weapons/Player/Axatana/Katanas/Katana_L.Katana_L` | `Katana_L_Skeleton` |
| `/Game/Sparta/Weapons/Player/Axatana/Katanas/Katana_R.Katana_R` | `Katana_R_Skeleton` |
| `/Game/Sparta/Weapons/Player/Axe_Dagger/SK_Weap_Player_Axe.SK_Weap_Player_Axe` | `SK_Weap_Player_Axe_Skeleton` |
| `/Game/Sparta/Weapons/Player/Axe_Dagger/SK_Weap_Player_Dagger.SK_Weap_Player_Dagger` | `SK_Weap_Player_Dagger_Skeleton` |
| `/Game/Sparta/Weapons/Player/CursedChild/Art/Mesh/SK_CursedChild.SK_CursedChild` | `SK_CursedChild_Skeleton` |
| `/Game/Sparta/Weapons/Player/GenessasIncenseBurner/Art/Mesh/SK_GenessasIncenseBurner.SK_GenessasIncenseBurner` | `SK_GenessasIncenseBurner_Skeleton` |
| `/Game/Sparta/Weapons/Player/HadernsSword/SK_HadernsSword_01.SK_HadernsSword_01` | `Hadern_SW_Skeleton` |
| `/Game/Sparta/Weapons/Player/HallowedSword/SK_Weap_Player_HSword…` (skeleton: `SK_Weap_Player_HSword_Skeleton`) | `SK_Weap_Player_HSword_Skeleton` |
| `/Game/Sparta/Weapons/Player/MartyrsBlade/…` (skeleton `SK_MartyrsBlade_Skeleton`) | `SK_MartyrsBlade_Skeleton` |
| `/Game/Sparta/Weapons/Player/ParasiteGun/Art/Mesh/SK_ParasiteGun…` | `SK_ParasiteGun_Skeleton` |
| `/Game/Sparta/Weapons/Player/Spear_BlackNeedle/Mesh/SK_BlackNeedle…` | `SK_BlackNeedle_Skeleton` |
| `/Game/Sparta/Weapons/Player/TielDagger/Art/Mesh/…` | `Skeleton_TielDagger` |
| `/Game/Sparta/Weapons/Player/Tiel_Crossbow/…` | `SK_Weap_Tiel_Crossbow_Skeleton` |
| `/Game/Sparta/Weapons/Player/Trebuchaxe/Art/Mesh/…` | `SK_Trebuchaxe_Skeleton` |

(`pl-weaponsockets.sql` enumerates the skeletons — see §5.7.)

### 5.7 Sockets — what the dump can and cannot tell you

**Cannot:** the *name* of any socket. `SkeletalMeshSocket` and `StaticMeshSocket` objects appear as
`…:SkeletalMeshSocket_0`, `…:StaticMeshSocket_3` — those are the UObject names, and the actual
`SocketName: FName` payload is not dumped (see §0.3).

**Can:** every socket-bearing object, the socket count per mesh, and every UPROPERTY whose *name*
mentions a socket. That is the material below.

#### 5.7.1 Socket-object census

| Metric | Count | Query |
|---|---|---|
| `SkeletalMeshSocket` objects, whole build | **249** | `pl-counts.sql` |
| `StaticMeshSocket` objects, whole build | **80** (79 game + 1 `/Script/Engine.Default__StaticMeshSocket`) | `pl-counts.sql`, `pl-smsockets.sql` |
| `SkeletalMeshSocket` on `/Game/Sparta/Characters/Humans/_Shared/SKEL_Human_Skeleton` | **82** | `pl-human-skel.sql` |
| `StaticMeshSocket` under `/Game/Sparta/Weapons/Player/` | **58** | `pl-counts.sql` |
| `SkeletalMeshSocket` under `/Game/Sparta/Weapons/Player/` | **17** | `pl-counts.sql` |

Player-weapon **skeleton** socket counts (`pl-weaponsockets.sql`):

| Skeleton | sockets |
|---|---|
| `/Game/Sparta/Weapons/Player/Axatana/Katanas/Katana_L_Skeleton.Katana_L_Skeleton` | 1 |
| `/Game/Sparta/Weapons/Player/Axatana/Katanas/Katana_R_Skeleton.Katana_R_Skeleton` | 1 |
| `/Game/Sparta/Weapons/Player/Axe_Dagger/SK_Weap_Player_Axe_Skeleton.SK_Weap_Player_Axe_Skeleton` | 1 |
| `/Game/Sparta/Weapons/Player/Axe_Dagger/SK_Weap_Player_Dagger_Skeleton.SK_Weap_Player_Dagger_Skeleton` | 1 |
| `/Game/Sparta/Weapons/Player/CursedChild/Art/Mesh/SK_CursedChild_Skeleton.SK_CursedChild_Skeleton` | 1 |
| `/Game/Sparta/Weapons/Player/HallowedSword/SK_Weap_Player_HSword_Skeleton.SK_Weap_Player_HSword_Skeleton` | 1 |
| `/Game/Sparta/Weapons/Player/MartyrsBlade/SK_MartyrsBlade_Skeleton.SK_MartyrsBlade_Skeleton` | 1 |
| `/Game/Sparta/Weapons/Player/ParasiteGun/Art/Mesh/SK_ParasiteGun_Skeleton.SK_ParasiteGun_Skeleton` | **2** |
| `/Game/Sparta/Weapons/Player/Spear_BlackNeedle/Mesh/SK_BlackNeedle_Skeleton.SK_BlackNeedle_Skeleton` | 1 |
| `/Game/Sparta/Weapons/Player/TielDagger/Art/Mesh/Skeleton_TielDagger.Skeleton_TielDagger` | 1 |
| `/Game/Sparta/Weapons/Player/Tiel_Crossbow/SK_Weap_Tiel_Crossbow_Skeleton.SK_Weap_Tiel_Crossbow_Skeleton` | **4** |
| `/Game/Sparta/Weapons/Player/Trebuchaxe/Art/Mesh/SK_Trebuchaxe_Skeleton.SK_Trebuchaxe_Skeleton` | **2** |

Static-mesh socket counts on player weapon meshes (`pl-smsockets.sql`) — the ones a mod would use to
attach trails/muzzle VFX:

| StaticMesh | `StaticMeshSocket` count |
|---|---|
| `…/Axe_Dagger/SM_Weap_Player_Axe.SM_Weap_Player_Axe` | 7 |
| `…/Axe_Dagger/SM_Weap_Player_Dagger_New.SM_Weap_Player_Dagger_New` | 5 |
| `…/BattleAxe/SM_Gragu_BattleAxe_01.SM_Gragu_BattleAxe_01` | 5 |
| `…/GiantSword_AltSkin/SM_GiantSword_AltSkin.SM_GiantSword_AltSkin` | 5 |
| `…/HadernsSword/SM_HadernsSword_01.SM_HadernsSword_01` | 5 |
| `…/Clockwork_Scythe/Art/Mesh/SM_Clockwork_Scythe.SM_Clockwork_Scythe` | 4 |
| `…/HeavyHammer/Art/Mesh/SM_ObsidianHammer_01.SM_ObsidianHammer_01` | 4 |
| `…/Axatana/Katanas/Katanas/SM_Axatana_Axe_Offset.SM_Axatana_Axe_Offset` | 3 |
| `…/Nail_Shotgun/Mesh/Nail_Shotgun_StaticMesh.Nail_Shotgun_StaticMesh` | 3 |
| `…/Spear_BlackNeedle/Mesh/SM_BlackNeedle.SM_BlackNeedle` | 3 |
| `…/Axe_Dagger/SM_Weap_Player_Dagger_New_LoRes.SM_Weap_Player_Dagger_New_LoRes` | 2 |
| `…/HadernsSword/SM_HadernsSword_Display.SM_HadernsSword_Display` | 2 |
| `…/Trebuchaxe/Art/Mesh/SM_Trebuchaxe_Projectile.SM_Trebuchaxe_Projectile` | 2 |
| `…/Axe_Dagger/SM_Weap_Player_Dagger.SM_Weap_Player_Dagger` | 1 |
| `…/HallowedSword/SM_HallowedSword_01.SM_HallowedSword_01` | 1 |
| `…/Infinite_Seal/SM_Weap_Infinite_Seal.SM_Weap_Infinite_Seal` | 1 |
| `…/MartyrsBlade/SM_MartyrsBlade_01_Broken.SM_MartyrsBlade_01_Broken` | 1 |
| `…/Proxima/Art/MeshV2/SM_Proxima_MG_Barrel.SM_Proxima_MG_Barrel` | 1 |
| `…/Proxima/Art/MeshV2/SM_Proxima_MG_Base.SM_Proxima_MG_Base` | 1 |
| `…/Proxima/Art/SK/SM_Proxima_barrel.SM_Proxima_barrel` | 1 |
| `…/Tiel_Crossbow/SM_Weap_Tiel_Crossbow_1_3.SM_Weap_Tiel_Crossbow_1_3` | 1 |
| `/Game/Sparta/Characters/Shells/Eredrim/Art/Mesh/SM_EredrimDiapazon.SM_EredrimDiapazon` | 1 (shell weapon prop) |

Total: 58. Evidence:

```
[000001F8C9CAD660] StaticMeshSocket /Game/Sparta/Weapons/Player/Axe_Dagger/SM_Weap_Player_Axe.SM_Weap_Player_Axe:StaticMeshSocket_0 [n: 2FC0E] [c: 00007FF46EF23738] [or: 000001F8CB7CB140]
[000001F4C633F700] SkeletalMeshSocket /Game/Sparta/Characters/Humans/_Shared/SKEL_Human_Skeleton.SKEL_Human_Skeleton:SkeletalMeshSocket_0 [n: 2F9DA] [c: 00007FF46EF20538] [or: 000001F4D5E08960]
```

Both kinds of socket object are named `<digits>` only; the `[n:]` values differ (`2FC0E` static vs
`2F9DA` skeletal) because the two classes live in different modules.

#### 5.7.2 Every socket-named UPROPERTY in the player pipeline

`pl-socketprops.sql` searched every row whose path contains `socket`. Below is the filtered set that
matters for the player (all Blueprint compiler temporaries and RigVM metadata removed).

| Class | Property | Type | Offset | Meaning |
|---|---|---|---|---|
| `/Script/Sparta.SpartaWeapon` | `Stow(FName Socket)` | param | — | native stow-to-socket |
| `/Script/Sparta.SpartaWeapon` | `PutInHand(FName Socket)` | param | — | native draw-to-socket |
| `/Script/Sparta.SpartaWeapon` | `AdditionalWeaponToSpawn` | `TSubclassOf<ASpartaWeapon>` | `0x02C8` | twin/dual-wield spawn |
| `/Script/Sparta.FWeaponVariantData` | `bOverrideStowedSocket` / `StowedSocketName` | `bool` / `FName` | `0x08` / `0x0C` | per-variant stow socket |
| `/Script/Sparta.FWeaponVariantData` | `bOverrideInHandSocket` / `InHandSocketName` | `bool` / `FName` | `0x14` / `0x18` | per-variant hand socket |
| `/Script/Sparta.SocketAdjustmentData` | `SocketAdjustments` | `TMap<FName, FTransform>` | `0x0028` | per-character socket correction table |
| `/Script/Sparta.SocketAdjustmentData` | `GetAdjustmentForSocket(FName SocketName)` | fn | — | lookup |
| `/Script/Sparta.SpartaSocketMovementPrediction` | `GetSocketMovementPrediction(UObject*, FName SocketName, USkeletalMeshComponent*, float)` | fn | — | movement prediction |
| `/Script/Sparta.SpartaWeaponThrowSampler` | `StartManualCapture(USkeletalMeshComponent*, FName)` / `StartCapture(USkeletalMeshComponent*, FName, float)` | fn | — | throw sampling |
| `/Script/Sparta.SpartaWeaponComponent_Melee` | `GetCurrentFXSocketWorldVelocity()` / `GetCurrentFXSocketRelativeVelocity()` | fn | — | whoosh FX socket |
| `/Script/Sparta.SpartaHomingSplineComponent` | `SplineAttachSocketName` / `SetSplineAttachTarget(USceneComponent* NewParent, FName NewSocketName)` | `FName` / fn | `0x00C8` | `Sparta.hpp:5568-5589` |
| `/Script/Sparta.SlayerSealComponent` | `StowedSocketName`, `bRefreshRecallPathToMovingSocket`, `SetStowedAttachTarget(USceneComponent* NewParent, FName NewSocketName)`, `AttachSealToStowedSocket()` | `FName` / `bool` / fn / fn | `0x00C8` / `0x0178` | `Sparta.hpp:3560-3604` |
| `/Script/Sparta.SpartaMeleeWeaponAudioBehavior` | `WhooshVelocitySocket` | `FName` | `0x0080` | `Sparta.hpp:6137-6141` |
| **`/Game/Sparta/Core/Weapons/WP_WeaponBase.WP_WeaponBase_C`** | **`StowedSocket`** | `NameProperty` | **`0x04FC`** | **the live stow socket** |
| **`…WP_WeaponBase_C`** | **`InHandSocket`** | `NameProperty` | **`0x0504`** | **the live hand socket** |
| `…WP_WeaponBase_C` | `InterpolateToStowedSocket` | `BoolProperty` | `0x0560` | |
| `…WP_WeaponBase_C` | `DisableInterpolationOnAttach` | `bool` | `0x0561` | |
| `…WP_WeaponBase_C` | `OnWeaponEquipStateChanged` | `MulticastInlineDelegateProperty` | `0x04E8` | |
| `…WP_WeaponBase_C` | `GetInHandSocketName()` / `GetStowedSocketName()` | fn | — | accessors |
| `…WP_WeaponBase_C` | `UpdateInHandSocketName(FName Socket)` / `UpdateStowSocketName(FName Socket)` | fn | — | **setters** |
| `…WP_WeaponBase_C` | `AttachWeaponToInHandSocket(FName SocketOverride)` / `AttachWeaponToStowSocket(FName SocketOverride)` | fn | — | |
| `…WP_WeaponBase_C` | `PutInHand(FName Socket)` / `Stow(FName Socket)` | fn | — | overrides |
| `…WP_WeaponBase_C` | `InterpolateWeaponToSocket(float Alpha, FName InSocketName)` | fn | — | |
| `…WP_WeaponBase_C` | `GetModifiedSocketTransform(FName SocketName, FTransform& Result)` | fn | — | |
| `…WP_WeaponBase_C` | `GetCharacterSocketAdjustment(FName Socket)` | fn | — | |
| `…WP_SidearmBase_C` | **`ProjectileSpawnSocket`** | `FName` | `0x05F8` | muzzle socket |
| `…WP_SidearmBase_C` | `GetSidearmMuzzleSocketName(FName& MuzzleSocket)` | fn | — | |
| `/Game/Sparta/Core/Animations/ANS/ANS_SpawnMeshInSocket.ANS_SpawnMeshInSocket_C` | `SocketName` | `NameProperty` | `0x30` | anim-notify socket |
| `/Game/Sparta/Core/Animations/AN/AN_SetWeaponEquipState.AN_SetWeaponEquipState_C` | `OverrideSocket` | `NameProperty` | `0x8C` | **anim-driven socket override** |
| `/Game/Sparta/Core/Animations/ANS/ANS_HandleWeaponsEquipState.ANS_HandleWeaponsEquipState_C` | `CustomPutInHandSocket` | `NameProperty` | `0x40` | **anim-driven socket override** |
| `/Game/Sparta/Core/Player/BPFL_Player_Equipment.BPFL_Player_Equipment_C` | `StowAndPutInHand:CustomPutInHandSocket` | `NameProperty` | `0x10` | |

Evidence:

```
[000001F4C06A5250] NameProperty /Game/Sparta/Core/Weapons/WP_WeaponBase.WP_WeaponBase_C:StowedSocket [o: 4FC] [n: 670528] [c: 00007FF721DE90B0] [owr: 000001F4C3DA3FA0]
[000001F4C06A51E0] NameProperty /Game/Sparta/Core/Weapons/WP_WeaponBase.WP_WeaponBase_C:InHandSocket [o: 504] [n: 670575] [c: 00007FF721DE90B0] [owr: 000001F4C3DA3FA0]
[000001F615FE2980] BoolProperty /Game/Sparta/Core/Weapons/WP_WeaponBase.WP_WeaponBase_C:InterpolateToStowedSocket [o: 560] [n: 6705C4] [c: 00007FF721DE8D80] [owr: 000001F4C3DA3FA0]
```

#### 5.7.3 `ANS_HandleWeaponsEquipState_C` and `AN_SetWeaponEquipState_C` — the anim-driven equip points

These two anim notifies are what actually move a weapon from hand to back during a draw/stow montage.
They are the montage-side trigger for everything in §5.7.2.

| Class | Member | Type | Offset |
|---|---|---|---|
| `ANS_HandleWeaponsEquipState_C` (parent `/Script/Engine.AnimNotifyState`) | `WeaponToStow` | `GameplayTag` | `0x30` |
| ” | `WeaponToPutInHand` | `GameplayTag` | `0x38` |
| ” | `CustomPutInHandSocket` | `FName` | `0x40` |
| `AN_SetWeaponEquipState_C` (parent `/Script/CSAnimationLibrary.CSAnimNotify`) | `WeaponSlotToAffect` | `GameplayTag` | `0x80` |
| ” | **`WeaponEquipState`** | `ByteProperty` (`EWeaponEquipState`) | `0x88` |
| ” | **`OverrideSocket`** | `FName` | `0x8C` |

Functions: `ANS_HandleWeaponsEquipState_C::Received_NotifyEnd(MeshComp: USkeletalMeshComponent*, Animation: USkeletalMeshSequenceBase*, EventReference: FAnimNotifyEventReference, ReturnValue: bool)` —
its non-temp locals show it calls `GetComponentByClass(WeaponsComponent)`, `GetWeaponInSlot(WeaponToStow/WeaponToPutInHand)`,
casts to `BPI_Weapon_Base`, and reads `GetWeaponEffectTags()` + `IsWeaponHidden()`.
`AN_SetWeaponEquipState_C::Received_Notify(...)` does the same, plus a `SwitchEnum` on
`WeaponEquipState`.

So: **the equip state machine is a Blueprint enum (`EWeaponEquipState`) driven from anim notifies
that write `OverrideSocket` into `AWP_WeaponBase_C::StowedSocket` / `InHandSocket`.**

### 5.8 Upgrade / infusion visual variants

| Mechanism | Where | Detail |
|---|---|---|
| `WeaponVariants` | `ASpartaWeapon::WeaponVariants: TArray<FWeaponVariantData>` @ `0x02F0` | each entry = replacement `UStaticMesh*` + optional stow/hand socket override + optional capsule-collision override |
| `WeaponOverrides` | `USpartaCharacterData::WeaponOverrides: TArray<TSubclassOf<ASpartaWeapon>>` @ `0x00F8` | per-character default weapon class list |
| `UpgradeStatValues` | `ASpartaWeapon` @ `0x0330`; `USpartaCharacterData` @ `0x0108` | numeric upgrade deltas |
| `ElementalEfficiency` | `ASpartaWeapon` @ `0x0380` | `TMap<FGameplayTag, int32>` |
| `USpartaWeaponProgressionData` | `/Script/Sparta.SpartaWeaponProgressionData` (parent `UDataAsset`) | `DamageMultiplier`, `PoiseDamageMultiplier`, `ElementalEfficiency`, `CritChance`, `CritDamage`, `EnergyGainMultiplier`, `EnergyConsumption`, `GuardMeterMultiplier` — all `FToggleableRuntimeCurve` `@0x30/0x110/0x1F0/0x2D0/0x3B0/0x490/0x570/0x650`; `PrepopulateMaxLevel @0x730`, `PrepopulateValue @0x734`, `bPrepopulateOnCreate @0x738` |
| Obsidian / AltSkin weapon classes | `WP_Eredrim_Diapason_Obsidian_C` (parent `WP_Eredrim_Diapason_C`), `WP_TarnishedSeal_C`, `WP_SlayerSeal_C`, `WP_StoneSeal_C`, `WP_UntarnishedSeal…`, `SM_GiantSword_AltSkin`, `SM_Weap_Player_Dagger_New` | **separate Blueprint subclasses**, not variants |
| Ghost/memory variants | `WP_WeaponBase_Static_Ghost_C` subclasses: `WP_BattleAxe_Ghost`, `WP_BlackNeedle_Ghost`, `WP_DaggerAxe_Axe_Ghost`, `WP_DaggerAxe_Dagger_Ghost`, `WP_IncompleteScythe_Ghost`, `WP_Tiel_Dagger_Right_Ghost`, `WP_HadernsSword_SarcophagusSword`, `WP_CannibalSword_Memory`, `WP_MartyrsSword_Memory`, `WP_Wraith_Sword_Memory` | **separate classes** |
| Per-weapon VFX | `USpartaWeaponVFXData` assets (`VFXData_*`) | see §5.9 |

There is **no** class/asset named `*Infusion*` or `*WeaponVariant*` in the dump:
`SELECT type, COUNT(*) FROM obj WHERE path LIKE '/Game/Sparta/%Variant%'` returns only
lighting-volume types (`DirectionalLightComponent`, `SkyAtmosphereComponent`, …). Infusion visuals
must therefore go through `ASpartaWeapon::WeaponVariants` (mesh swap) + `WeaponEffectTags`.

Weapon items (`/Game/Sparta/Items/Weapons/`, 21 classes = 19 weapon items + 2 pickup classes):
`ID_Axatana_Single`, `ID_AxeDagger`, `ID_AxeDagger_Memory`, `ID_BattleAxe`, `ID_BattleAxe_Memory`,
`ID_BlackNeedle`, `ID_BlackNeedle_Memory`, `ID_ClockworkScythe`, `ID_Fists`, `ID_GreatSword_Memory`,
`ID_HadernsSword`, `ID_HallowedSword`, `ID_HeavyHammer`, `ID_IncompleteScythe_Memory`,
`ID_MartyrsBlade`, `ID_MartyrsBlade_Memory`, `ID_SacrophagusSword_Memory`,
`ID_SarcophagusSword_Memory_Stowed_NoFade`, `ID_Weapon_LoadFromSave`, plus
`Pickups/ID_WeaponPickupBase` and `Pickups/BP_Behavior_EquipWeapon`.
Note there is **no** `ID_Smert_Fists`, `ID_ParasiteGun`, `ID_NailShotgun`, `ID_Trebuchaxe`,
`ID_MachineGun`, `ID_Crossbow`, `ID_Lute` item — the sidearms are granted by other means
(UNKNOWN; `ID_Fists` is the only unarmed item).

### 5.9 Weapon VFX data — every `SpartaWeaponVFXData` asset (38)

| Asset |
|---|
| `/Game/Sparta/FX/Core/VFXData_Axatana_Axe.VFXData_Axatana_Axe` |
| `/Game/Sparta/FX/Core/VFXData_Axatana_Katana_Left.VFXData_Axatana_Katana_Left` |
| `/Game/Sparta/FX/Core/VFXData_Axatana_Katana_Right.VFXData_Axatana_Katana_Right` |
| `/Game/Sparta/FX/Core/VFXData_Ballistazooka.VFXData_Ballistazooka` |
| `/Game/Sparta/FX/Core/VFXData_BattleAxe.VFXData_BattleAxe` |
| `/Game/Sparta/FX/Core/VFXData_BlackNeedle.VFXData_BlackNeedle` |
| `/Game/Sparta/FX/Core/VFXData_BrigRegal_Sword.VFXData_BrigRegal_Sword` |
| `/Game/Sparta/FX/Core/VFXData_ClockworkScythe.VFXData_ClockworkScythe` |
| `/Game/Sparta/FX/Core/VFXData_Crossbow.VFXData_Crossbow` |
| `/Game/Sparta/FX/Core/VFXData_CursedChild.VFXData_CursedChild` |
| `/Game/Sparta/FX/Core/VFXData_DaggerAxe_Axe.VFXData_DaggerAxe_Axe` |
| `/Game/Sparta/FX/Core/VFXData_DaggerAxe_Dagger.VFXData_DaggerAxe_Dagger` |
| `/Game/Sparta/FX/Core/VFXData_GolemAxe.VFXData_GolemAxe` |
| `/Game/Sparta/FX/Core/VFXData_HadernSword.VFXData_HadernSword` |
| `/Game/Sparta/FX/Core/VFXData_HeavyHammer.VFXData_HeavyHammer` |
| `/Game/Sparta/FX/Core/VFXData_MachineGun.VFXData_MachineGun` |
| `/Game/Sparta/FX/Core/VFXData_MartyrsBlade.VFXData_MartyrsBlade` |
| `/Game/Sparta/FX/Core/VFXData_NailShotgun.VFXData_NailShotgun` |
| `/Game/Sparta/FX/Core/VFXData_Offsrping_Sword.VFXData_Offsrping_Sword` |
| `/Game/Sparta/FX/Core/VFXData_ParasiteGun.VFXData_ParasiteGun` |
| `/Game/Sparta/FX/Core/VFXData_SimpleLute.VFXData_SimpleLute` |
| `/Game/Sparta/FX/Core/VFXData_Trebuchaxe.VFXData_Trebuchaxe` |
| `/Game/Sparta/Core/Characters/Player/Smert/Abilities/VFX/VFXData_SmertFist.VFXData_SmertFist` |
| `/Game/Sparta/Core/Weapons/Player/Shells/Gragu/VFXData_Gragu_Punch.VFXData_Gragu_Punch` |
| `/Game/Sparta/Core/Weapons/Enemy/BallistaHead/VFXData_BallistaHead_Head…` |
| `/Game/Sparta/Core/Weapons/Enemy/Brigand/VFXData_Brigand_Crossbow…`, `VFXData_Wraith_Sword…` |
| `/Game/Sparta/Core/Weapons/Enemy/Brigand_Elite/VFXData_BrigElite_Axe…`, `Winter/VFXData_BrigElite_Hammer…` |
| `/Game/Sparta/Core/Weapons/Enemy/CultistSpearLady/VFXData_Enemy_Lightning…` |
| `/Game/Sparta/Core/Weapons/Enemy/Draugr/VFXData_Draugr_Mace…` |
| `/Game/Sparta/Core/Weapons/Enemy/HeavyCultist/VFXData_HeavyCultist_Hammer…` |
| `/Game/Sparta/Core/Weapons/Enemy/Shepherd/VFXData_ShepherdStaff…` |
| `/Game/Sparta/Core/Weapons/Enemy/Sicario/VFXData_Sicario_HadernSword…` |
| `/Game/Sparta/Core/Weapons/Enemy/TwinSisters/VFXData_TwinSister_{Crossbow,Dagger,Rapier,Spear}…` |

`USpartaWeaponVFXData` (`Sparta.hpp:7772-7780`, size `0x98`):

| Property | Type | Offset |
|---|---|---|
| `VFXData` | `TMap<FGameplayTag, USpartaWeaponVFXObject*>` | `0x0030` |
| `DefaultCameraShake` | `TSubclassOf<UCameraShakeBase>` | `0x0080` |
| `CameraShakeInnerRadius` / `CameraShakeOuterRadius` / `CameraShakeFalloff` | `float` | `0x0088/8C/90` |

`USpartaWeaponVFXObject` = `UObject`, no extra members (`Sparta.hpp:7782`).

Evidence:

```
[00007FF46F05F338] Class /Script/Sparta.SpartaWeaponVFXData [n: 4ADE7] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[000001F4ACE21460] MapProperty /Script/Sparta.SpartaWeaponVFXData:VFXData [o: 30] [n: 587618] [c: 00007FF721DE8F70] [owr: 00007FF46F05F338] [kp: 000001F4ACE14080] [vp: 000001F4ACE14000]
[000001F4ACE14100] ClassProperty /Script/Sparta.SpartaWeaponVFXData:DefaultCameraShake [o: 80] [n: 589A4A] [c: 00007FF721DE8E20] [owr: 00007FF46F05F338] [mc: 00007FF46EED2138]
```

### 5.10 Weapon-specific locomotion

The task asked for `/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/`. **That folder does
not exist.** `SELECT path FROM obj WHERE type='Package' AND path LIKE '/Game/Sparta/Characters/Shells/_Shared/%'`
returns only `Abilities/`, `Actions/`, `Actions/Audio/`, `Actions/AxeDagger/`, `Actions/Axeatana/`,
`Actions/BearTrap/`, `Actions/Block/`, … — no `Locomotion/`.

The player weapon locomotion layers actually live at `/Game/Sparta/Characters/Humans/Player/Animations/`:

| AnimBlueprint (locomotion layer) | Note |
|---|---|
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_H1` | heavy-weapon class 1 |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_H2` | heavy-weapon class 2 |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_L` | light |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_ADS_Crossbow` | **per-sidearm ADS** |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_ADS_CursedChild` | **per-sidearm ADS** |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_ADS_Lute` | **per-sidearm ADS** |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_ADS_Nailgun` | **per-sidearm ADS** |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_ADS_Trebuchaxe` | **per-sidearm ADS** |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_MotionMatching` | main MM layer |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_ScytheAbility` | Clockwork Scythe ability |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_Sheep` | sheep transformation |
| `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_StateMachine` | state machine |
| `/Game/Sparta/Core/Weapons/Player/Ballistazooka/ABPL_Locomotion_ADS_Ballistazooka.ABPL_Locomotion_ADS_Ballistazooka_C` | **stored with the weapon** (parent `ABPL_Locomotion_Blendspace_C`) |

Selection mechanism (per weapon, not per shell): `AWP_WeaponBase_C::LocomotionStyle: FGameplayTag`
@ `0x05B0` + `GetLocomotionStyle()`; `AWP_SidearmBase_C::LocomotionAnimLayer: TArray<TSubclassOf<UAnimInstance>>`
@ `0x0610` + `GetSidearmLocomotionAnimLayers()`; `AWP_WeaponBase_C::OnEquip_AnimationLayers` /
`OnPutInHand_AnimationLayers` @ `0x0568/0x0578` with `ApplyAnimationLayers(TArray<TSoftClassPtr<UAnimInstance>>&)`
and `RemoveAnimationLayers(...)`.

Motion-matching databases (`PoseSearchDatabase`) are organised by **weapon weight class**:
`…/Animations/MotionMatching/Database/{H1,H2,L,M}/…` — `PSN_Player_Locomotion_{H1,H2,L,M}`,
`PSD_Player_{Idles,Jogs,Sprints,Strafes,Walks,Falls,Lands}_*`. There is no "M" variant of
`ABPL_Locomotion` (UNKNOWN why).

---

## 6. Armour, cosmetics, glow, and the components behind them

### 6.1 `UArmorComponent` (`Sparta.hpp:2476-2485`, size `0xD8`)

| Property | Type | Offset |
|---|---|---|
| `CurrentArmorTag` | `FGameplayTag` | `0x00B8` |

| Function | Parameters |
|---|---|
| `SetDefaultArmor` | `ArmorTag: const FGameplayTag&` |
| `SetCurrentArmorTag` | `ArmorTag: const FGameplayTag&` |
| `ResetArmorTagToDefault` | – |
| `AddArmorOverride` | `WorldContextObject: const UObject*`, `InArmorTag: FGameplayTag`, `OverrideTime: float` → returns `FArmorOverrideHandle` |
| `RemoveArmorOverride` | `InHandle: FArmorOverrideHandle` |

`CurrentArmorTag` drives hit-reaction/impact-cue selection:
`U…GameplayCueNotify` with `m_ArmorTagsToTrigger: FGameplayTagContainer @0x0050`,
`m_bArmorTagExactMatch: bool @0x0070`, `m_ExecutedArmorTag: FGameplayTag @0x00B0`,
`ShouldPlay(const FGameplayTagContainer& in_DamageTags, const FGameplayTag& in_ArmorTag)`,
`Execute(const AActor* in_ImpactTarget, const FGameplayCueParameters& in_Parameters, const FGameplayTagContainer& in_DamageTags, const FGameplayTag& in_ArmorTag)`
(`Sparta.hpp:5632-5645`). So armour type = a tag, and it is **the only volumetric visual difference
between armour tiers** that the dump exposes.

Armour tag source: `USpartaCharacterData::ArmorTag @0x0090`.

Evidence:

```
[000001F4ACD56880] StructProperty /Script/Sparta.ArmorComponent:CurrentArmorTag [o: B8] [n: 585E1F] [c: 00007FF721DF9890] [owr: 00007FF46F02D738] [ss: 00007FF46F3890A8]
[00007FF46F6E3118] Function /Script/Sparta.ArmorComponent:SetCurrentArmorTag [n: 475AE] [c: 00007FF46EE55538] [or: 00007FF46F02D738] [f: 00007FF71C7C5690]
[000001F4ACD56B80] StructProperty /Script/Sparta.ArmorComponent:AddArmorOverride:ReturnValue [o: 14] [n: 4CF54F] [c: 00007FF721DF9890] [owr: 00007FF46F6E2E78] [ss: 00007FF46F486638]
```

### 6.2 `UArtifactComponent` (`Sparta.hpp:2487-2493`)

| Property | Type | Offset |
|---|---|---|
| `ArtifactTables` | `TArray<FUpgradeTable>` | `0x00B8` |
| `NarrativeTables` | `TArray<FUpgradeTable>` | `0x00C8` |
| `DebuffTables` | `TArray<FUpgradeTable>` | `0x00D8` |

No functions. `FUpgradeTable` = `Requirement: FGameplayTagQuery @0x00`, `UpgradeType: EUpgradeType @0x48`,
`UpgradeTable: UDataTable* @0x50` (`Sparta.hpp:1612`).

### 6.3 `USpartaVFXComponent` (`Sparta.hpp:7480-7489`, size `0xD8`)

| Property | Type | Offset |
|---|---|---|
| `TargetMaskValue` | `float` | `0x00C4` |

| Function | Parameters |
|---|---|
| `UpdateMaskValue` | `DeltaTime: float` |
| `AddBloodMaskEffect` | `Strength: float`, `duration: float`, `ID: FName`, `ApplyMode: EBloodMaskApplyMode`, `MaxTotalStrength: float` |
| `RemoveBloodMaskEffect` | `ID: FName` |
| `GetSubstanceMaskEnabled` | → `bool` |
| `GetAllMeshes` | → `TArray<UMeshComponent*>` (inner `pc: 00007FF46EE72938` = `UMeshComponent`) |

The Blueprint wrapper is `/Game/Sparta/UI…`-adjacent: `UBPC_VFXComponent_C`, held by
`ABP_PlayerCharacter_C::BPC_VFXComponent @ 0x0BD0` and retrievable via
`ABP_PlayerCharacter_C::GetVFXComponent(UBPC_VFXComponent_C*&)`.
`EBloodMaskApplyMode` is at `Sparta_enums.hpp:25`.

Evidence:

```
[00007FF46F05C938] Class /Script/Sparta.SpartaVFXComponent [n: 4AAC7] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[000001F4ACDE4BC0] FloatProperty /Script/Sparta.SpartaVFXComponent:TargetMaskValue [o: C4] [n: 589588] [c: 00007FF721DE9380] [owr: 00007FF46F05C938]
[000001F4ACDFC100] ObjectProperty ReturnValue./Script/Sparta.SpartaVFXComponent:GetAllMeshes:ReturnValue [o: 0] [n: 4CF54F] [c: 00007FF721DE9420] [owr: 000001F4ACDFC180] [pc: 00007FF46EE72938]
```

### 6.4 `USpartaCharacterVFXData` (`Sparta.hpp:4542-4546`)

| Property | Type | Offset |
|---|---|---|
| `BloodColor` | `FLinearColor` | `0x0030` |

That is the **entire** class. Per-character VFX variation is a single colour.

Evidence:

```
[00007FF46F043B38] Class /Script/Sparta.SpartaCharacterVFXData [n: 48D50] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[000001F4ACDA0C80] StructProperty /Script/Sparta.SpartaCharacterVFXData:BloodColor [o: 30] [n: 5877AA] [c: 00007FF721DF9890] [owr: 00007FF46F043B38] [ss: 00007FF46F2E5B18]
```

### 6.5 Glow

| Where | Field |
|---|---|
| `/Game/Sparta/Core/Items/IF_GlowColor.IF_GlowColor_C` (parent `/Script/Sparta.SpartaItemFragment`) | `GlowColor: FLinearColor` @ `0x28` |
| `/Script/Sparta.ItemFragment_Display` | `GlowColor: FLinearColor` @ `0x0108` |
| `/Script/Sparta.ItemFragment_OverrideDisplay` | `bGlowColorOverride: bool` @ `0x0124` (+ `RequirementTag`, `bIconOverride`, `bDefaultVideoOverride`, `bDisplayNameOverride`, `bDescriptionOverride`) |
| `/Script/Sparta.FPlayerUpgrade` (DataTable row) | `GlowColor: FLinearColor` @ `0x0030` |
| `/Script/Sparta.ItemFragment_Display` neighbours | `Icon`, `DefaultVideo`, `DisplayName`, `DisplayNameSuffix`, `PrefixName`, `SubNameInfix`, `SubName`, `Description`, `Effect`, `HeaderOnAcquired`, `HeaderOnEquipped` |

Glow is an **item/inventory** concept (rarity tint on pickups & tooltips), not a body shader the dump
exposes. Body glow/emissive goes through `FSkinMaterials.Effect` / `SpartaCharacterData::EffectMaterials`.

Evidence: see §6.5 line quoted in §3.7.

### 6.6 Harden / stone form (the player's `Harden` ability)

The whole feature lives under `/Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/`:

| Asset | Class / parent |
|---|---|
| `BP_Harden_PP.BP_Harden_PP_C` | `BlueprintGeneratedClass`, parent `/Script/Engine.Actor` — **the post-process actor** |
| `GA_Harden_Original.GA_Harden_Original_C` | gameplay ability |
| `GE_Harden_Active.GE_Harden_Active_C` | gameplay effect |
| `GE_Harden_Perfect.GE_Harden_Perfect_C` | gameplay effect (perfect-harden) |

Input: `/Game/Sparta/Core/Player/Ability/Input/GA_Action_StoneForm_Press.GA_Action_StoneForm_Press_C`
and `…_Release.GA_Action_StoneForm_Release_C`.
Cues: `/Game/Sparta/Core/GameplayCues/VFX/StoneForm/GC_StoneForm_Harden.GC_StoneForm_Harden_C`,
`GC_StoneForm_Player.GC_StoneForm_Player_C`, `GC_StoneForm_AI.GC_StoneForm_AI_C`.
Camera: `/Game/Sparta/Core/Camera/CameraShakes/CSL_StoneForm_Enter.CSL_StoneForm_Enter_C` /
`CSL_StoneForm_Leave.CSL_StoneForm_Leave_C`.
Block-related: `/Game/Sparta/Core/Effects/GE_Block_Harden.GE_Block_Harden_C`,
`/Game/Sparta/Core/Animations/ANS/ANS_BlockHarden.ANS_BlockHarden_C`.
UI: `/Game/Sparta/UI/Player/WBP_Player_Bar_StoneForm.WBP_Player_Bar_StoneForm_C`
(menu/widget — other agent's file), `/Game/Sparta/UI/Player/Debug/StoneForm/WBP_Debug_StoneForm.WBP_Debug_StoneForm_C`.

`BP_Harden_PP_C` members (from `parse_dump.py show`):

| Member | Type | Offset |
|---|---|---|
| `PP_StoneForm` | `UPostProcessComponent*` | `0x2b0` |
| `Scene_Root` | `USceneComponent*` | `0x2b8` |
| `TimelineHeartbeat` | `UTimelineComponent*` | `0x2c8` (+ `TimelineHeartbeat_Heartbeat_3770…: float @0x2c0`, `…__Direction_…: ByteProperty @0x2c4`) |
| `TimelineStart` | `UTimelineComponent*` | `0x2e0` (`TimelineStart_WhiteFlash`, `_DistortionDistanceAlpha`, `_Alpha`, `__Direction`) |
| `TimelineEnd` | `UTimelineComponent*` | `0x2f8` (same 4 tracks) |
| `OwnerCharacter` | `ASpartaCharacter*` | `0x300` |
| **`StornFormPPMat`** | `UMaterialInstanceDynamic*` | `0x308` (note the typo — **`StornFormPPMat`**, not `Storm`) |
| `OnEndCompleted` | `MulticastInlineDelegateProperty` | `0x310` |
| `AudioId` | `FAudioEventId` | `0x320` |

Function: **`HandleStoneFormParameters(Alpha: float, DistortionDistanceAlpha: float, WhiteFlash: float)`**
— that is exactly the three animated scalars (alpha, distortion, white flash). Plus
`AddPPMaterial()`, `ActivationAudio()`, `DeactivationAudio(bBreakSound: bool)`.

**Sync note:** harden is therefore (a) a gameplay effect tag on the ASC and (b) a locally spawned
post-process actor. Only (a) needs replicating; (b) is derived.

`ANS_BlockHarden_C` + `GE_Block_Harden_C` cover the block-harden variant;
`ANS_HyperArmor_C` + `GE_HyperArmor_C` / `GE_HyperArmorIgnoreImmunity_C` are the
armour-tag-based super-armour. Full paths (from `pl-armor.sql`):

| Class |
|---|
| `/Game/Sparta/Core/Animations/ANS/ANS_BlockHarden.ANS_BlockHarden_C` |
| `/Game/Sparta/Core/Animations/ANS/ANS_HyperArmor.ANS_HyperArmor_C` |
| `/Game/Sparta/Core/Effects/GE_Block_Harden.GE_Block_Harden_C` |
| `/Game/Sparta/Core/Effects/GE_HyperArmor.GE_HyperArmor_C` |
| `/Game/Sparta/Core/Effects/GE_HyperArmorIgnoreImmunity.GE_HyperArmorIgnoreImmunity_C` |

Splendid Armour (Necrophage ability) has its own visual set:
`/Game/Sparta/Core/Characters/Player/Necrophage/Abilities/BP_SplendidArmorHandler.BP_SplendidArmorHandler_C`,
`GA_SplendidArmor_Default/Cooldown`, `GE_SplendidArmor_Charged/Cooldown`,
`/Game/Sparta/Core/GameplayCues/VFX/Shell/GC_SplendidArmorBase.GC_SplendidArmorBase_C`,
`GC_SplendidArmor_Shockwave_{Default,Impact,Overheated}_C`,
`/Game/Sparta/Core/Camera/CameraShakes/CameraShake_SplendidArmor.CameraShake_SplendidArmor_C`.
Related pose data: `…/Animations/PoseOffsets/Necrophage_H2_Splendid_Armor`.
Mesh side: `SK_Necrophage_Armored` / `_ArmoredNoMask` / `_ArmoredOn` (§2.6).

Also: `/Game/Sparta/Core/GameplayCues/VFX/Shell/GC_ProximaArmor.GC_ProximaArmor_C` — a
`/GameplayCues/VFX/Shell/` cue named after Proxima. `GUESS:` that means Proxima was a shell at some
point (§10 item 11); the only hard fact is that the cue exists and lives in the Shell cue folder.

`GC_StoneForm_*` / `BP_Harden_PP_C` evidence:

```
[000001F4DC013FA0] BlueprintGeneratedClass /Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/BP_Harden_PP.BP_Harden_PP_C [n: 30F69E] [c: 00007FF46EEC3D38] [or: 000001F4CFD28E40] [sps: 00007FF46EE52028]
```

### 6.7 Death / health / resolve visual state

| Enum | Values | Where |
|---|---|---|
| `ESpartaDeathState` | `NotDead=0`, `DeathStarted=1`, `DeathFinished=2`, `ESpartaDeathState_MAX=3` | `Sparta_enums.hpp:315` |
| `EStatusEffectOverlayPolicy` | enum in `Sparta_enums.hpp:709`, used by `SpartaCharacterData::StatusEffectOverlayPolicy @0x03D8` | |
| `EBloodMaskApplyMode` | `Sparta_enums.hpp:25`, used by `SpartaVFXComponent::AddBloodMaskEffect` | |
| `EHealthDamageType`, `ESpartaAttackType`, `ESpartaHitTraceType` | `Sparta_enums.hpp:62/261/449` | |

Health/resolve attributes that a HUD/visual reads:

| Attribute | Owner |
|---|---|
| `ShellHealth @0x0080`, `MaxShellHealth @0x0090`, `MinShellHealth @0x00B0` | `/Script/Sparta.SpartaHealthSet` (`Sparta.hpp:5346`; fields at `:5350/5351/5353`) |
| `ShellReviveCurrentProgress @0x0150`, `ShellReviveMaxProgress @0x0160`, `ShellReviveProgressGain @0x0170`, `ShellReviveMaxProgressMultiplier @0x0180` | `Sparta.hpp:3301-3304` |
| `Armor @0x00C0` | `Sparta.hpp:5354` |

Health-component events/delegates (`Sparta.hpp:5310` = `USpartaHealthComponent`; delegate `:5318`;
getters `:5334-5336`):
`FSpartaHealthComponentOnShellHealthDepleted OnShellHealthDepleted @0x00E8`,
`GetShellHealthNormalized()`, `GetShellHealth()`, `GetMaxShellHealth()`.

`GUESS:` the exclusive `Shell*` prefix on `SpartaHealthSet` / `SpartaHealthComponent` suggests the
dark form has a health pool of its own. The dump shows exactly one health attribute-set class and
only `Shell*` getters, so a two-pool model is an inference from naming, not a proven fact.

`FSpartaCharacterDeathData DeathResources @0x0358` on `SpartaCharacterData`, and
`ABP_PlayerCharacter_C::GetDeathAnimationSet(USpartaDeathAnimationSet*& Result)` +
`OnCharacterDeath(class UObject* EventData)` + `RecordPlayerDeathEvent(Instigator, Causer)`.

`USpartaCharacterData::StatusEffectOverlayPolicy @0x03D8` is an `EnumProperty` whose value type is
`[em: 00007FF46F3D8A50]`; the dump does not resolve the enum *entries*, only the type
(§8). Do not assume the enum class — it is not dumpable.

Evidence:

```
[000001F4ACD9E080] EnumProperty /Script/Sparta.SpartaCharacterData:StatusEffectOverlayPolicy [o: 3D8] [n: 58760A] [c: 00007FF721DE7380] [owr: 00007FF46F043538] [em: 00007FF46F3D8A50]
```

---

## 7. Weapons and shells: mount points (what to actually sync vs. derive)

| Piece of state | Is it synced state or derived? | Evidence |
|---|---|---|
| Shell identity | **synced** — item class + shell tag | `ID_Shell_*` + `ItemFragment_SetShellClass:ShellClass`; `BP_Shell_Base_C:ShellIDTag` |
| Skin | **synced** — tag | `ChangeShellSkin(ShellId, SkinID, …)`; `GetShellSkin` |
| Clothing | **synced** — tag | `ChangeShellClothing(CharacterId, ClothingID, …)` |
| Mask | **synced** — tag | `ChangeShellMask(CharacterId, MaskID, …)` |
| Dark-form active | **synced** — bool/tag | `GA_CharacterSkinHandler_C::IsInDarkform`, `CheckDarkForm`, `State_InShell` tag (`Sparta.hpp:4781`) |
| Armour tag override | synced if driven by gameplay | `ArmorComponent::AddArmorOverride(WorldContextObject, InArmorTag, OverrideTime)` |
| Skeletal mesh, materials, MatInstances | **derived** | `InitMeshByID`, `SwapShellMaterials`, `SpartaCharacterData::{DefaultMaterials,SkinMaterialsMap}` |
| Weapon in slot | **synced** — (slot tag → weapon) | `WeaponsComponent::EquippedWeapons: TMap<FGameplayTag, ASpartaWeapon*>` |
| Weapon equip state | **synced** — enum | `WP_WeaponBase_C::EquipState` / `CurrentEquipState`, `EWeaponEquipState` |
| Socket choice | derived from equip state + variants | `WP_WeaponBase_C::StowedSocket` / `InHandSocket`, `AN_SetWeaponEquipState_C::OverrideSocket` |
| Weapon visual variant | synced if it comes from an upgrade | `ASpartaWeapon::WeaponVariants`, `UpgradeStatValues`, `ElementalEfficiency` |
| Harden active | **synced** — tag/effect | `GE_Harden_Active`, `GE_Harden_Perfect`; post-process is local |
| Blood/substance mask, glow | local only | `SpartaVFXComponent::AddBloodMaskEffect`, `IF_GlowColor` |
| Death state | **synced** — enum | `ESpartaDeathState` |

---

## 8. Enums

### 8.1 Native (`Sparta_enums.hpp`) — the only ones relevant to looks

| Enum | Header | Values |
|---|---|---|
| `ESpartaDeathState` | `:315` | `NotDead=0`, `DeathStarted=1`, `DeathFinished=2`, `ESpartaDeathState_MAX=3` |
| `EStatusEffectOverlayPolicy` | `:709` | (values not read — see UNKNOWN) |
| `EBloodMaskApplyMode` | `:25` | (ditto) |
| `ESpartaTeam` | `:689` | (used by hit-trace / VFX team filtering) |
| `EItemRarity` | `:75` | drives pickup glow |
| `EUpgradeType` | `:779` | used by `FUpgradeTable` |
| `EWeaponStatType` | `:795` | |
| `ESlayerSealState` | `:159` | seal weapons |

There is **no** native mesh-state enum, no native `EWeaponEquipState`, no native shell enum. Grep for
`^enum class \w*(Shell|Weapon|Skin|Armor|Mesh|Harden|Stone|Custom|Variant|Equip|Death|Glow)\w*`
in `Sparta_enums.hpp` returns only `ESpartaDeathState` and `EWeaponStatType`.

### 8.2 Blueprint `UserDefinedEnum`s (the important ones)

`scripts\q\pl-enums.sql` — 80 `UserDefinedEnum` objects in the build. The look-relevant ones:

| Enum | Path |
|---|---|
| **`EWeaponEquipState`** | `/Game/Sparta/Core/Weapons/Enums/EWeaponEquipState.EWeaponEquipState` |
| **`ESkinCategory`** | `/Game/Sparta/Core/Utility/Enums/ESkinCategory.ESkinCategory` |
| **`EWeaponTransformation`** | `/Game/Sparta/Core/Utility/Enums/EWeaponTransformation.EWeaponTransformation` |
| **`E_CharacterMaterialType`** | `/Game/Sparta/Core/Characters/Player/Common/E_CharacterMaterialType.E_CharacterMaterialType` |
| `EStatusEffect` | `/Game/Sparta/Core/Utility/Enums/EStatusEffect.EStatusEffect` |
| `EAttachableCategory` | `/Game/Sparta/Core/Utility/Enums/EAttachableCategory.EAttachableCategory` |
| `EAttachMethod` | `/Game/Sparta/Core/Utility/Enums/EAttachMethod.EAttachMethod` |
| `ESkinCategory` neighbours | `E_ArtifactsRarity`, `E_PreDeathTypes`, `E_UpgradePoolPolicy` |

**The enum *entries* are not in the dump.** A `UserDefinedEnum` object appears, but its
`FName` entry objects do not
(`parse_dump.py find ... "EWeaponEquipState"` returns only the `Package` + `UserDefinedEnum` rows,
plus unrelated `ChangeWeaponEquipState` functions). To get the values you must read
`UEnum::DisplayNameMap` / `UUserDefinedEnum` children at runtime, or open the uasset.

Evidence:

```
[000001F4963834C0] UserDefinedEnum /Game/Sparta/Core/Weapons/Enums/EWeaponEquipState.EWeaponEquipState [n: 3C3284] [c: 00007FF46EF66D38] [or: 000001F4A708DCB0]
[000001F4DC567FC0] UserDefinedEnum /Game/Sparta/Core/Utility/Enums/ESkinCategory.ESkinCategory [n: 38D0EB] [c: 00007FF46EF66D38] [or: 000001F4D33CB9B0]
[000001F8CA148EC0] UserDefinedEnum /Game/Sparta/Core/Utility/Enums/EWeaponTransformation.EWeaponTransformation [n: 329739] [c: 00007FF46EF66D38] [or: 000001F8D760F510]
```

### 8.3 Gameplay tags found in the pipeline

| Tag-typed field | Where |
|---|---|
| `ShellIDTag` | `BP_Shell_Base_C` @ `0x0D50` |
| `ShellId`, `SkinID`, `ClothingID`, `MaskID`, `CharacterId`, `DarkFormID` | parameters throughout `BPFL_Player_Equipment_C` |
| `CharacterId` | `USpartaCharacterData` @ `0x0040`; `ASpartaCharacter` @ `0x06E8` |
| `ArmorTag` | `USpartaCharacterData` @ `0x0090`; `ArmorComponent::CurrentArmorTag` @ `0x00B8` |
| `WeaponSlot`, `WeaponTag`, `EquipmentTag` | `ASpartaWeapon` @ `0x03D0/3D8/3E0` |
| `LocomotionStyle` | `AWP_WeaponBase_C` @ `0x05B0` |
| `WeaponEffectTags` | `AWP_WeaponBase_C` @ `0x0540` |
| `InHandTags` | `AWP_WeaponBase_C` @ `0x0590` |
| `CustomPutInHandSocket` (FName, not a tag) | `ANS_HandleWeaponsEquipState_C` @ `0x40` |
| `State_InShell` | `Sparta.hpp:4781` |
| `Event_ShellBroken` | `Sparta.hpp:4788` |
| `DamageOption_ForceShell` | `Sparta.hpp:4807` |
| `ShellTags: TArray<FGameplayTag>` | `Sparta.hpp:4741` |

---

## 9. Complete inventory counts (for the summary)

| Thing | Count | Source |
|---|---|---|
| `SkeletalMesh` under `/Game/Sparta/Characters/Shells/` | **26** | `pl-skmeshes.sql` |
| `Skeleton` under `/Game/Sparta/Characters/Shells/` | **1** | `pl-skmeshes.sql` |
| `Skeleton` in whole build | 119 | `pl-human-skel.sql` |
| `AnimBlueprintGeneratedClass` under `Shells/` | **15** (all `_Post`/`PostProcess`) | `pl-shellclasses.sql` |
| Shell/dark-form pawn Blueprints (`BP_Shell_*`, `BP_Darkform_*`, `BP_Decoy_Bait`) | **36** | `pl-shellparents.sql` |
| of which direct children of `BP_Shell_Base_C` | **18** | `pl-shellparents.sql` |
| `ItemFragment_SetShellClass` instances | **35** | `pl-shellfrag.sql` |
| `ItemFragment_SetDarkformClass` instances | **9** | `pl-shellfrag.sql` |
| `SpartaCharacterData` assets for player/dark-form/decoy | **25** | `pl-shellcd.sql` (31 rows matched; 5 are `ShellMemories` NPC CDs + `CD_Shellsnatcher`) |
| `SpartaMeshCustomisation` assets | **9** (+CDO) | `pl-custom.sql` |
| Player weapon classes (`WP_*` in `Core/Weapons/Player`) | **58** | `pl-counts.sql` |
| Player weapon `SkeletalMeshComponent` named `WeaponMesh` | **58** | `pl-weaponcdo.sql` |
| `StaticMesh` under `/Game/Sparta/Weapons/Player/` | **48** | `pl-playerweaponsm.sql` |
| `StaticMeshSocket` under `/Game/Sparta/Weapons/Player/` | **58** | `pl-counts.sql` |
| `SkeletalMeshSocket` under `/Game/Sparta/Weapons/Player/` | **17** | `pl-counts.sql` |
| `SkeletalMeshSocket` on `SKEL_Human_Skeleton` | **82** | `pl-human-skel.sql` |
| `SkeletalMeshSocket` whole build | **249** | `pl-counts.sql` |
| `StaticMeshSocket` whole build | **80** | `pl-counts.sql` |
| `SpartaWeaponVFXData` assets | **38** | `pl-custom.sql` |
| Player locomotion ABPs | **12** (`ABPL_Locomotion_*`) + 1 with the Ballistazooka | `pl-loco.sql` |
| Player weapon items (`ID_*` in `Items/Weapons`) | **19** items (+ `ID_WeaponPickupBase`, `BP_Behavior_EquipWeapon` = 21 classes) | `pl-weaponitems.sql` |
| Shell skin items (`ID_Shell_*_{AltSkin,Obsidian}`) | **17** | `pl-shellitems.sql` |
| Weapon sockets on the mesh the mod would attach to | UNKNOWN (names not dumped) | §5.7 |

---

## 10. UNKNOWNs and open questions

1. **Socket names.** The dump exposes 249 `SkeletalMeshSocket` + 80 `StaticMeshSocket` *objects* but
   none of their `SocketName` values. Tried: full `obj` scan for `%socket%` (only compiler-temp
   names + the UPROPERTY names in §5.7.2), `Select-String "Socket_"` on the 565 MB
   `ObjectDump_allassets.txt` (773 hits, all `…Socket_<N>` object names or UI texture names),
   `parse_dump.py raw` for `SkeletalMeshSocket_0` (row shape only). **Resolution requires a runtime
   read** (`Mesh:GetSocketNames()` / `USkeletalMesh::GetActiveSocketList`) or a uasset inspection.
2. **Mesh → Skeleton / Mesh → MaterialInstance links.** No asset UPROPERTY values in the dump.
3. **Shell → `SK_`/`ABP_`/`CD_` mapping.** Directory-name inference only (§2.6), marked `GUESS:`.
   The authoritative relation is `USpartaCharacterData`'s inherited mesh field, which is not dumped
   and whose *name* does not even appear on `SpartaCharacterData` (it must live on
   `ACSCharacter`/the Blueprint CDO).
4. **The exact name of the shell-body mesh property.** `ABP_PlayerCharacter_C::DefaultMesh`
   (`TSoftObjectPtr<USkeletalMesh>`, `0x0CE8`) is the strongest candidate, but nothing in the dump
   proves it is what `CharacterMesh0` uses. `BP_Shell_Base_C` has `InitMeshByID(FGameplayTag)` but
   the dump does not reveal what it reads.
5. **`EWeaponEquipState`, `ESkinCategory`, `EWeaponTransformation`, `E_CharacterMaterialType`,
   `EStatusEffectOverlayPolicy`, `EBloodMaskApplyMode` entry lists.** `UserDefinedEnum` objects
   exist, entries do not (native ones are in `Sparta_enums.hpp` for the native ones only).
6. **`FSpartaSkeletalMeshComponentOnMeshCustomisationComplete` `DelegateFunction` object.** Property
   row exists (`[df: 00007FF46F3D0B88]`), the signature object is not in the dump. The signature is
   known from `Sparta.hpp:7150`.
7. **`SM_Weapon` StaticMeshComponent is declared on `WP_WeaponBase_Static_C` but is not dumped as a
   subobject** on any of the 58 player weapons. Only `WP_KnightLady_Hook_Left_C` has
   `SM_Weapon_GEN_VARIABLE`. UNKNOWN whether the rest create it at runtime or via
   `InheritableComponentHandler` (15 `InheritableComponentHandler` rows exist under
   `Core/Weapons/Player/`).
8. **Genessa's shell mesh.** No `SkeletalMesh` under `/Game/Sparta/Characters/Shells/Genessa/`
   (query returns 0 rows). Genessa has `BP_Shell_Genessa_C`, `CD_Genessa`, `ID_Shell_Genessa`,
   `ABP_Shell_Genessa_C` and 2 skins — so the mesh must be shared or live elsewhere. UNKNOWN.
9. **Harros / Solomon body AnimBlueprint.** No `ABP_Shell_Harros_*` / `ABP_Shell_Solomon_*` class
   exists in `Core/Characters/Player/`. UNKNOWN which ABP they use.
10. **`ID_Shell_Lazlo_NoMask`, `ID_Shell_Necrophage_Child`, `ID_Shell_Necrophage_YoungAdult`,
    `ID_Shell_Gragu_Helmetless`** have `SetShellClass` fragments but no matching `BP_Shell_*` class
    in the dump. They likely reuse an existing shell's class with a different mesh/skin. UNKNOWN.
11. **Proxima / Sariel / Lazlo** appear as legacy skin families in
    `BPFL_Player_Equipment_C::GetAllSkinsForShell` (parameter names `ProximaSkins`, `SarielSkins`,
    `LazloSkins`) and Proxima has armour VFX (`GC_ProximaArmor_C`) and meshes
    (`SM_Proxima_MG_*`, `SM_Proxima_barrel`, `SM_Proxima_base`) — but there is **no** `BP_Shell_Proxima*`
    pawn and **no** `ID_Shell_Proxima`. UNKNOWN whether Proxima is a cut shell or the MachineGun's
    internal name.
12. **CORRECTED (coordinator, after a re-query): `/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/`
    DOES exist** — it holds **405** `AnimSequence` assets, including the three sequences the shipped
    co-op mod plays on the peer body. The earlier claim that the folder was absent was wrong (the
    probe had looked for `ABPL_*` blueprints, which do live under `Humans/Player/Animations/`).
    Details: shared locomotion layers are `…/Humans/Player/Animations/ABPL_Locomotion_*` (§5.10),
    while the shared shell *sequences* are in `_Shared/Animation/Locomotion/` (`Idles/`, `Walk/H2/`,
    plus per-weapon subfolders such as `Ballistazooka/`, `ClockworkScythe/`). Verbatim:

    ```
    [000001F4D4E0D800] AnimSequence /Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/Idles/A_Shared_Idle_H.A_Shared_Idle_H [n: 129917] [c: 00007FF46EF2FD38] [or: 000001F4C0A3DFD0]
    [000001F4D751D400] AnimSequence /Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/Walk/H2/A_Shared_Locomotion_Walk_Fwd_H2.A_Shared_Locomotion_Walk_Fwd_H2 [n: 428A97] [c: 00007FF46EF2FD38] [or: 000001F4C4656500]
    [000001F4BF449E00] AnimSequence /Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/A_Shared_Locomotion_Sprint_800.A_Shared_Locomotion_Sprint_800 [n: 4ABF2E] [c: 00007FF46EF2FD38] [or: 000001F4C0A3EC50]
    ```

    (queries: `scripts\q\47-loco.sql`, `48-sharedidle.sql`, `51-walkh2.sql`, `52-lococount.sql`,
    `53-locolines.sql`)
13. **Which `WP_*` class uses which `SM_*`/`SK_*` mesh.** Values not dumped; folder-name `GUESS:`.
14. **Weapon "infusion"** — no asset or class named `*Infusion*` exists
    (`SELECT type,path FROM obj WHERE path LIKE '%Infus%'` → 0 rows in `/Game/Sparta`). The variant
    mechanism is `ASpartaWeapon::WeaponVariants` + `WeaponEffectTags` (§5.8). UNKNOWN whether the
    live game changes `WeaponVariants` at runtime or swaps to a different `WP_*` class entirely.
15. **`CD_Harros`, `CD_Smert`, `CD_Shell_Solomon`, `CD_Eredrim` have no `CharacterAnimationData`
    subobject** while `CD_Gragu`, `CD_Tiel`, `CD_Genessa`, `CD_StrongOne` etc. do
    (`pl-custom.sql`, 28 `CharacterAnimationData` subobjects). UNKNOWN — likely inherited from
    `CD_Player`.
16. **`USocketAdjustmentData` instances.** The class exists and takes `TMap<FName, FTransform>`; the
    dump lists no asset of that type under `/Game/Sparta` (`pl-custom.sql` searched for it and found
    only `/Script/Sparta.Default__…` for related types). UNKNOWN whether the per-character socket
    correction table is populated at all.
17. **Shell `CD_*` → `VAR_*` mapping.** No `VAR_*` asset exists for any player shell (§4.1), yet
    `SpartaCharacterData::MeshVariationData @0x0460` exists. UNKNOWN what (if anything) it points at
    for shells.
