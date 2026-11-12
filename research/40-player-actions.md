# 40 -- Player abilities (GA_*) and the player attribute sets

Scope: every player-side `GA_*` ability class, the seven Sparta attribute sets, the input layer that
fires abilities, and the gameplay-ability plumbing a co-op sync layer has to hook. Enemies, bosses,
items/inventory, world state, menus and the animation catalogue are other agents' reports.

Method and evidence rules are those of `scripts/QUERYING.md` / `scripts/REPORT-TEMPLATE.md`:
every class/function row below is followed by 1-3 lines copied verbatim from
`python scripts\parse_dump.py raw raw\index.sqlite3 <regex>` (which reproduces the original
`UE4SS_ObjectDump*.txt` lines byte-for-byte). Offsets are printed exactly as the dump prints them
(`[o: 3A8]` = 0x3A8). Where a value comes only from `raw\CXXHeaderDump\Sparta.hpp` the header line is
quoted instead and labelled **HDR**.

Two conventions used throughout:

* `Base` columns show the **immediate** SuperStruct (`[sps:]`), not the whole chain.
* A cell of `` -- `` means the class declares **no own** property of that kind; the value is then
  inherited from the base named in the previous column.
* `UNKNOWN` + a "tried:" line means I could not establish it from the dump.

---

## 0. Quick map of the family (counts)

| Bucket | Count | Where |
|---|---|---|
| All classes whose path contains `/GA_` | 9944 | whole dump (includes enemies, GE_*, BP_*) |
| Classes whose path contains `/Game/Sparta/` + `/GA_` | 1560 | |
| Player ability blueprints under `/Game/Sparta/Core/Player/Ability/` | 555 | part 1 sec.5-8, part 2 sec.9-14 |
| of which `.../Ability/Interactions/` (memory/pose montage abilities) | 193 | part 2 sec.13 |
| of which `.../Ability/Weapons/` (melee weapon attack abilities) | 181 | sec.6 |
| of which `.../Ability/Input/` (`GA_Action_*` input handlers) | 52 | sec.8.1 |
| Native `Sparta` ability/attribute classes | 26 + 103 (input/notify/task names) | sec.1-4 |

Counts above come from
`SELECT COUNT(*) FROM cls WHERE path LIKE '/Game/Sparta/%/GA\_%' ESCAPE '\'` and the per-folder
`GROUP BY`; the per-folder numbers are in the enumeration tables of sec.5-8 (part 1) and
sec.9-14 (part 2).

---

## 1. Native ability foundation

### 1.1 `USpartaGameplayAbility` -- the root of every player ability

Parent: `/Script/GameplayAbilities.GameplayAbility`. Everything a sync layer needs about an ability
(cost, cooldown, activation policy, tags, child abilities) lives here.

| Property | Type (dump) | Offset | Notes |
|---|---|---|---|
| `Cooldown` | `EnumProperty` `ESpartaAbilityGlobalCooldown` | `[o: 3A8]` | Disabled / AbilityBased / TagBased |
| `CooldownDuration` | `FloatProperty` | `[o: 3AC]` | |
| `EffectMagnitude` | `FloatProperty` | `[o: 3B0]` | magnitude passed into the ability's effect |
| `Cost` | `FloatProperty` | `[o: 3B4]` | flat cost (energy); `AdditionalCosts` adds more |
| `CooldownTag` | `StructProperty` `GameplayTag` | `[o: 3B8]` | |
| `CooldownApplication` | `EnumProperty` `ESpartaAbilityCooldownApplication` | `[o: 3C0]` | OnCommit / OnEnd / Custom |
| `GlobalCooldown` | `EnumProperty` `ESpartaAbilityGlobalCooldown` | `[o: 3C1]` | |
| `GlobalCooldownTag` | `StructProperty` `GameplayTag` | `[o: 3C4]` | |
| `GlobalCooldownDuration` | `FloatProperty` | `[o: 3CC]` | |
| `bUseDeactivationTags` | `BoolProperty` | `[o: 3D0]` | |
| `DeactivationTags` | `StructProperty` `GameplayTagContainer` | `[o: 3D8]` | |
| `ActivationPolicy` | `EnumProperty` `ESpartaAbilityActivationPolicy` | `[o: 3F8]` | OnInputTriggered / WhileInputActive / OnSpawn |
| `ActivationGroup` | `EnumProperty` `ESpartaAbilityActivationGroup` | `[o: 3F9]` | Independent / Exclusive_Replaceable / Exclusive_Blocking |
| `AdditionalCosts` | `ArrayProperty` of `ObjectProperty` -> `/Script/Sparta.SpartaAbilityCost` | `[o: 400]` | inner element type proven by the child row `[ai: 000001F4ACD97780]` |
| `FailureTagToUserFacingMessages` | `MapProperty` `GameplayTag` -> `TextProperty` | `[o: 410]` | key/value types proven by the `_Key` row + `[vp: ...]` |
| `FailureTagToAnimMontage` | `MapProperty` `GameplayTag` -> `ObjectProperty` -> `/Script/Engine.AnimMontage` | `[o: 460]` | ability-failed feedback montage |
| `bLogCancelation` | `BoolProperty` | `[o: 4B0]` | |
| `ChildAbilities` | `MapProperty` `NameProperty` -> `ClassProperty`(subclass of `SpartaGameplayAbility`) | `[o: 4B8]` | key = `ChildAbilityKey` used by `TryActivateChildAbility` |

Functions (dump order, full parameter lists):

| Function | Parameters (name: type) | Notes |
|---|---|---|
| `TryActivateChildAbilityWithEventData` | `ChildAbilityKey: NameProperty`, `Payload: StructProperty of GameplayAbilities.GameplayEventData`, `bCancelThisAbility: BoolProperty`, `ReturnValue: BoolProperty` | activates `ChildAbilities[ChildAbilityKey]` |
| `TryActivateChildAbility` | `ChildAbilityKey: NameProperty`, `bCancelThisAbility: BoolProperty`, `ReturnValue: BoolProperty` | same without a payload |
| `SetLocalCooldownPaused` | `Owner: ObjectProperty of Engine.Actor`, `bPaused: BoolProperty` | |
| `SetGlobalCooldownPaused` | `Owner: ObjectProperty of Engine.Actor`, `bPaused: BoolProperty` | |
| `OnInputReleased` | (no params) | called by the input layer -- override point for release-triggered abilities |
| `OnInputPressed` | (no params) | override point for press-triggered abilities |
| `K2_OnPawnAvatarSet` | (no params) | |
| `K2_OnAbilityRemoved` | (no params) | |
| `K2_OnAbilityAdded` | (no params) | |
| `K2_OnAbilityAboutToBeRemoved` | (no params) | |
| `IsOnLocalCooldown` | `Owner: ObjectProperty of Engine.Actor`, `ReturnValue: BoolProperty` | |
| `IsOnGlobalCooldown` | `Owner: ObjectProperty of Engine.Actor`, `ReturnValue: BoolProperty` | |
| `IsOnAnyCooldown` | `Owner: ObjectProperty of Engine.Actor`, `ReturnValue: BoolProperty` | |
| `GetSpartaPlayerControllerFromActorInfo` | `ReturnValue: ObjectProperty of Sparta.SpartaPlayerController` | |
| `GetSpartaCharacterFromActorInfo` | `ReturnValue: ObjectProperty of Sparta.SpartaCharacter` | |
| `GetSpartaAIControllerFromActorInfo` | `ReturnValue: ObjectProperty of Sparta.SpartaAIController` | |
| `GetSpartaAbilitySystemComponentFromActorInfo` | `ReturnValue: ObjectProperty of Sparta.SpartaAbilitySystemComponent` | |
| `GetLocalCooldownDuration` | `ReturnValue: FloatProperty` | |
| `GetLocalCooldown` | `Owner: ObjectProperty of Engine.Actor`, `ReturnValue: FloatProperty` | |
| `GetHitPayload` | `ReturnValue: ObjectProperty of Sparta.SpartaHitPayload` | |
| `GetGlobalCooldownDuration` | `ReturnValue: FloatProperty` | |
| `GetGlobalCooldown` | `Owner: ObjectProperty of Engine.Actor`, `ReturnValue: FloatProperty` | |
| `GetControllerFromActorInfo` | `ReturnValue: ObjectProperty of Engine.Controller` | |
| `ClearLocalCooldown` | `Owner: ObjectProperty of Engine.Actor` | |
| `ClearGlobalCooldown` | `Owner: ObjectProperty of Engine.Actor` | |
| `ChangeActivationGroup` | `NewGroup: EnumProperty of Sparta.ESpartaAbilityActivationGroup`, `ReturnValue: BoolProperty` | |
| `CanChangeActivationGroup` | `NewGroup: EnumProperty of Sparta.ESpartaAbilityActivationGroup`, `ReturnValue: BoolProperty` | |
| `ApplyLocalCooldown` | `Owner: ObjectProperty of Engine.Actor`, `CooldownDurationOverride: FloatProperty` | |
| `ApplyGlobalCooldown` | `Owner: ObjectProperty of Engine.Actor`, `CooldownDurationOverride: FloatProperty` | |

Evidence -- class + representative members:
```
[00007FF46F03F938] Class /Script/Sparta.SpartaGameplayAbility [n: 487DE] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF91D38]
[000001F4ACD99200] EnumProperty /Script/Sparta.SpartaGameplayAbility:Cooldown [o: 3A8] [n: 58683D] [c: 00007FF721DE7380] [owr: 00007FF46F03F938] [em: 00007FF46F3DA4B8]
[000001F4ACD65640] FloatProperty /Script/Sparta.SpartaGameplayAbility:Cost [o: 3B4] [n: 56AAA3] [c: 00007FF721DE9380] [owr: 00007FF46F03F938]
[000001F4ACD97800] ArrayProperty /Script/Sparta.SpartaGameplayAbility:AdditionalCosts [o: 400] [n: 587053] [c: 00007FF721DE8910] [owr: 00007FF46F03F938] [ai: 000001F4ACD97780]
[000001F4ACD97780] ObjectProperty AdditionalCosts./Script/Sparta.SpartaGameplayAbility:AdditionalCosts [o: 0] [n: 587053] [c: 00007FF721DE9420] [owr: 000001F4ACD97800] [pc: 00007FF46F03B538]
[000001F49CD2E5C0] MapProperty /Script/Sparta.SpartaGameplayAbility:FailureTagToAnimMontage [o: 460] [n: 587015] [c: 00007FF721DE8F70] [owr: 00007FF46F03F938] [kp: 000001F4ACD97980] [vp: 000001F4ACD97900]
[000001F4ACD97900] ObjectProperty FailureTagToAnimMontage./Script/Sparta.SpartaGameplayAbility:FailureTagToAnimMontage [o: 8] [n: 587015] [c: 00007FF721DE9420] [owr: 000001F49CD2E5C0] [pc: 00007FF46EF2E338]
[000001F49CD2E660] MapProperty /Script/Sparta.SpartaGameplayAbility:ChildAbilities [o: 4B8] [n: 586FFA] [c: 00007FF721DE8F70] [owr: 00007FF46F03F938] [kp: 000001F4ACD63AB0] [vp: 000001F4ACD97A80]
[000001F4ACD97A80] ClassProperty ChildAbilities./Script/Sparta.SpartaGameplayAbility:ChildAbilities [o: 8] [n: 586FFA] [c: 00007FF721DE8E20] [owr: 000001F49CD2E660] [mc: 00007FF46F03F938]
```

### 1.2 The other native ability classes

| Class | Parent | Own properties | Functions |
|---|---|---|---|
| `/Script/Sparta.SpartaAttackAbility` | `/Script/Sparta.SpartaGameplayAbility` | `AbilityHitPayload: ObjectProperty of Sparta.SpartaHitPayload` `[o: 508]`, `AbilityHitPayloadDataModified: ObjectProperty of Sparta.SpartaHitPayload` `[o: 510]` | none |
| `/Script/Sparta.SpartaAbilityCost` | `/Script/CoreUObject.Object` | `bOnlyApplyCostOnHit: BoolProperty` `[o: 28]` | `CheckCost`, `ApplyCost` (below) |
| `/Script/Sparta.SpartaAbilityDataAsset` | `/Script/Engine.DataAsset` | `AbilitySet: StructProperty of Sparta.SpartaAbilitySet` `[o: 30]` | none |
| `/Script/Sparta.SpartaAbilitySetHandleContainer` | `/Script/CoreUObject.Object` | `Handles: StructProperty of Sparta.SpartaAbilitySet_GrantedHandles` `[o: 28]` | none |
| `/Script/Sparta.SpartaAbilityTagRelationshipMapping` | `/Script/Engine.DataAsset` | `AbilityTagRelationships: ArrayProperty`->Sparta.SpartaAbilityTagRelationship` `[o: 30]` | none |
| `/Script/Sparta.CharacterPersistantAbilitySetData` | `/Script/Sparta.TypeSpecificData` | `AbilitySet: StructProperty of Sparta.SpartaAbilitySet` `[o: 28]` | `RemoveAbilitySet(Character: ObjectProperty of Sparta.SpartaCharacter)`, `GiveAbilitySet(Character: ObjectProperty of Sparta.SpartaCharacter)` |
| `/Script/Sparta.SpartaAbilityActor` | `/Script/Engine.Actor` | `AbilitySystemComponent: ObjectProperty of Sparta.SpartaAbilitySystemComponent` `[o: 2B0]` | none |
| `/Script/Sparta.SpartaAbilitySubsystem` | `/Script/Engine.GameInstanceSubsystem` | 5 cooldown maps (below) | 20 cooldown functions (below) |

`USpartaAbilityCost` functions (dump order):

| Function | Parameters (name: type) |
|---|---|
| `CheckCost` | `Ability: ObjectProperty of Sparta.SpartaGameplayAbility`, `Handle: StructProperty of GameplayAbilities.GameplayAbilitySpecHandle`, `ActorInfo: StructProperty of GameplayAbilities.GameplayAbilityActorInfo`, `OptionalRelevantTags: StructProperty of GameplayTags.GameplayTagContainer`, `ReturnValue: BoolProperty` |
| `ApplyCost` | `Ability: ObjectProperty of Sparta.SpartaGameplayAbility`, `Handle: StructProperty of GameplayAbilities.GameplayAbilitySpecHandle`, `ActorInfo: StructProperty of GameplayAbilities.GameplayAbilityActorInfo`, `ActivationInfo: StructProperty of GameplayAbilities.GameplayAbilityActivationInfo` |

`USpartaAbilitySubsystem` properties (the **server-authoritative-ish cooldown store**; it is a
`GameInstanceSubsystem`, i.e. per client, not per world -- a sync layer must not assume it is shared):

| Property | Type (header) | Offset |
|---|---|---|
| `GlobalCooldownExpirationTagBased` | `TMap<FGameplayTag, FSpartaCooldownValue>` | `[o: 30]` |
| `GlobalCooldownExpirationClassBased` | `TMap<UClass*, FSpartaCooldownValue>` | `[o: 80]` |
| `UniqueCooldownExpiration` | `TMap<int32, FSpartaCooldownValue>` | `[o: D0]` |
| `UniqueCooldownExpirationTagBased` | `TMap<AActor*, FSpartaGameplayTagMappedFloat>` | `[o: 120]` |
| `UniqueCooldownExpirationClassBased` | `TMap<AActor*, FSpartaClassMappedFloat>` | `[o: 170]` |

`USpartaAbilitySubsystem` functions (dump order, full parameter lists):

| Function | Parameters (name: type) |
|---|---|
| `SetGlobalCooldownPausedBasedOnTag` | `Tag: StructProperty of GameplayTags.GameplayTag`, `bPaused: BoolProperty` |
| `SetGlobalCooldownPausedBasedOnClass` | `Class: ClassProperty`, `bPaused: BoolProperty` |
| `SetCooldownPausedBasedOnActorAndTag` | `OwnerActor: ObjectProperty of Engine.Actor`, `Tag: StructProperty of GameplayTags.GameplayTag`, `bPaused: BoolProperty` |
| `SetCooldownPausedBasedOnActorAndClass` | `Owner: ObjectProperty of Engine.Actor`, `Class: ClassProperty`, `bPaused: BoolProperty` |
| `SetAllCooldownsPaused` | `bPaused: BoolProperty` |
| `Reset` | (no params) |
| `IsTagOnGlobalCooldown` | `Tag: GameplayTag`, `ReturnValue: BoolProperty` |
| `IsClassOnGlobalCooldown` | `Class: ClassProperty`, `ReturnValue: BoolProperty` |
| `IsActorAndTagOnCooldown` | `OwnerActor: Actor`, `Tag: GameplayTag`, `ReturnValue: BoolProperty` |
| `IsActorAndClassOnCooldown` | `Owner: Actor`, `Class: ClassProperty`, `ReturnValue: BoolProperty` |
| `GetGlobalCooldownFromTag` | `Tag: GameplayTag`, `ReturnValue: FloatProperty` |
| `GetGlobalCooldownFromClass` | `Class: ClassProperty`, `ReturnValue: FloatProperty` |
| `GetCooldownFromActorAndTag` | `OwnerActor: Actor`, `Tag: GameplayTag`, `ReturnValue: FloatProperty` |
| `GetCooldownFromActorAndClass` | `Owner: Actor`, `Class: ClassProperty`, `ReturnValue: FloatProperty` |
| `ClearGlobalCooldownBasedOnTag` | `Tag: GameplayTag` |
| `ClearGlobalCooldownBasedOnClass` | `Class: ClassProperty` |
| `ClearCooldownBasedOnActorAndTag` | `OwnerActor: Actor`, `Tag: GameplayTag` |
| `ClearCooldownBasedOnActorAndClass` | `Owner: Actor`, `Class: ClassProperty` |
| `ApplyGlobalCooldownBasedOnTag` | `Tag: GameplayTag`, `DurationSeconds: FloatProperty` |
| `ApplyGlobalCooldownBasedOnClass` | `Class: ClassProperty`, `DurationSeconds: FloatProperty` |
| `ApplyCooldownBasedOnActorAndTag` | `OwnerActor: Actor`, `Tag: GameplayTag`, `DurationSeconds: FloatProperty` |
| `ApplyCooldownBasedOnActorAndClass` | `Owner: Actor`, `Class: ClassProperty`, `DurationSeconds: FloatProperty` |

Structs (from `Sparta.hpp`, needed to read/write ability grants):

| Struct | Members |
|---|---|
| `FSpartaAbilitySet` (`0x30`) | `GrantedGameplayAbilities: TArray<FSpartaAbilitySet_GameplayAbility>`, `GrantedGameplayEffects: TArray<FSpartaAbilitySet_GameplayEffect>`, `GrantedAttributes: TArray<FSpartaAbilitySet_AttributeSet>` |
| `FSpartaAbilitySet_GameplayAbility` (`0x18`) | `Ability: TSubclassOf<USpartaGameplayAbility>`, `AbilityLevel: int32`, `InputTag: FGameplayTag` |
| `FSpartaAbilitySet_GameplayEffect` (`0x60`) | `GameplayEffect: TSubclassOf<UGameplayEffect>`, `EffectLevel: float`, `TagMagnitudes: TMap<FGameplayTag,float>` |
| `FSpartaAbilitySet_AttributeSet` (`0x8`) | `AttributeSet: TSubclassOf<UAttributeSet>` |
| `FSpartaAbilitySet_GrantedHandles` (`0x38`) | `SpartaASC: USpartaAbilitySystemComponent*`, `AbilitySpecHandles: TArray<FGameplayAbilitySpecHandle>`, `GameplayEffectHandles: TArray<FActiveGameplayEffectHandle>`, `GrantedAttributeSets: TArray<UAttributeSet*>` |
| `FSpartaAbilityTagRelationship` (`0x88`) | `AbilityTag: FGameplayTag`, `AbilityTagsToBlock`, `AbilityTagsToCancel`, `ActivationRequiredTags`, `ActivationBlockedTags` (all `FGameplayTagContainer`) |

`FSpartaAbilitySet_GameplayAbility` is the exact place an ability gets bound to an input tag:
`Ability + AbilityLevel + InputTag`. That triple is what a sync layer has to reproduce per player.

Evidence -- the native ability classes 1.2:
```
[00007FF46F03B338] Class /Script/Sparta.SpartaAbilityActor [n: 4809B] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
[000001F4ACD7B880] ObjectProperty /Script/Sparta.SpartaAbilityActor:AbilitySystemComponent [o: 2B0] [n: 3C522] [c: 00007FF721DE9420] [owr: 00007FF46F03B338] [pc: 00007FF46F03B938]
[00007FF46F03B538] Class /Script/Sparta.SpartaAbilityCost [n: 480A5] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[000001F4ACD7D480] BoolProperty /Script/Sparta.SpartaAbilityCost:bOnlyApplyCostOnHit [o: 28] [n: 586BE4] [c: 00007FF721DE8D80] [owr: 00007FF46F03B538]
[00007FF46F6ED218] Function /Script/Sparta.SpartaAbilityCost:CheckCost [n: 1385A] [c: 00007FF46EE55538] [or: 00007FF46F03B538] [f: 00007FF7183D9710]
[00007FF46F6ED138] Function /Script/Sparta.SpartaAbilityCost:ApplyCost [n: 13854] [c: 00007FF46EE55538] [or: 00007FF46F03B538] [f: 00007FF7183D9710]
[00007FF46F03B738] Class /Script/Sparta.SpartaAbilitySubsystem [n: 480AF] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF5DF38]
[000001F49CD2EA20] MapProperty /Script/Sparta.SpartaAbilitySubsystem:GlobalCooldownExpirationTagBased [o: 30] [n: 586C7B] [c: 00007FF721DE8F70] [owr: 00007FF46F03B738] [kp: 000001F4ACD7DD00] [vp: 000001F4ACD7DC80]
[000001F4ACD7DD00] StructProperty GlobalCooldownExpirationTagBased./Script/Sparta.SpartaAbilitySubsystem:GlobalCooldownExpirationTagBased_Key [o: 0] [n: 586C8C] [c: 00007FF721DF9890] [owr: 000001F49CD2EA20] [ss: 00007FF46F3890A8]
[000001F4ACD7DC80] StructProperty GlobalCooldownExpirationTagBased./Script/Sparta.SpartaAbilitySubsystem:GlobalCooldownExpirationTagBased [o: 8] [n: 586C7B] [c: 00007FF721DF9890] [owr: 000001F49CD2EA20] [ss: 00007FF46F4876B8]
[000001F49CD2EAC0] MapProperty /Script/Sparta.SpartaAbilitySubsystem:GlobalCooldownExpirationClassBased [o: 80] [n: 586C55] [c: 00007FF721DE8F70] [owr: 00007FF46F03B738] [kp: 000001F4ACD7DE00] [vp: 000001F4ACD7DD80]
[000001F4ACD7DE00] ClassProperty GlobalCooldownExpirationClassBased./Script/Sparta.SpartaAbilitySubsystem:GlobalCooldownExpirationClassBased_Key [o: 0] [n: 586C67] [c: 00007FF721DE8E20] [owr: 000001F49CD2EAC0] [mc: 00007FF46EE50070]
[000001F4ACD7DD80] StructProperty GlobalCooldownExpirationClassBased./Script/Sparta.SpartaAbilitySubsystem:GlobalCooldownExpirationClassBased [o: 8] [n: 586C55] [c: 00007FF721DF9890] [owr: 000001F49CD2EAC0] [ss: 00007FF46F4876B8]
[000001F49CD2EB60] MapProperty /Script/Sparta.SpartaAbilitySubsystem:UniqueCooldownExpiration [o: D0] [n: 586C39] [c: 00007FF721DE8F70] [owr: 00007FF46F03B738] [kp: 000001F4ACD63420] [vp: 000001F4ACD7DE80]
[000001F4ACD63420] IntProperty UniqueCooldownExpiration./Script/Sparta.SpartaAbilitySubsystem:UniqueCooldownExpiration_Key [o: 0] [n: 586C46] [c: 00007FF721DE91F0] [owr: 000001F49CD2EB60]
[000001F4ACD7DE80] StructProperty UniqueCooldownExpiration./Script/Sparta.SpartaAbilitySubsystem:UniqueCooldownExpiration [o: 4] [n: 586C39] [c: 00007FF721DF9890] [owr: 000001F49CD2EB60] [ss: 00007FF46F4876B8]
[000001F49CD2EC00] MapProperty /Script/Sparta.SpartaAbilitySubsystem:UniqueCooldownExpirationTagBased [o: 120] [n: 586C15] [c: 00007FF721DE8F70] [owr: 00007FF46F03B738] [kp: 000001F4ACD7DF80] [vp: 000001F4ACD7DF00]
[000001F4ACD7DF80] ObjectProperty UniqueCooldownExpirationTagBased./Script/Sparta.SpartaAbilitySubsystem:UniqueCooldownExpirationTagBased_Key [o: 0] [n: 586C26] [c: 00007FF721DE9420] [owr: 000001F49CD2EC00] [pc: 00007FF46EE52028]
[000001F4ACD7DF00] StructProperty UniqueCooldownExpirationTagBased./Script/Sparta.SpartaAbilitySubsystem:UniqueCooldownExpirationTagBased [o: 8] [n: 586C15] [c: 00007FF721DF9890] [owr: 000001F49CD2EC00] [ss: 00007FF46F487778]
[000001F4F6F84A20] MapProperty /Script/Sparta.SpartaAbilitySubsystem:UniqueCooldownExpirationClassBased [o: 170] [n: 586BEF] [c: 00007FF721DE8F70] [owr: 00007FF46F03B738] [kp: 000001F4ACD7E080] [vp: 000001F4ACD7E000]
[000001F4ACD7E080] ObjectProperty UniqueCooldownExpirationClassBased./Script/Sparta.SpartaAbilitySubsystem:UniqueCooldownExpirationClassBased_Key [o: 0] [n: 586C01] [c: 00007FF721DE9420] [owr: 000001F4F6F84A20] [pc: 00007FF46EE52028]
[000001F4ACD7E000] StructProperty UniqueCooldownExpirationClassBased./Script/Sparta.SpartaAbilitySubsystem:UniqueCooldownExpirationClassBased [o: 8] [n: 586BEF] [c: 00007FF721DF9890] [owr: 000001F4F6F84A20] [ss: 00007FF46F487838]
[00007FF46F6EE558] Function /Script/Sparta.SpartaAbilitySubsystem:SetGlobalCooldownPausedBasedOnTag [n: 481F5] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E26C0]
[00007FF46F6EE478] Function /Script/Sparta.SpartaAbilitySubsystem:SetGlobalCooldownPausedBasedOnClass [n: 481E2] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E25F0]
[00007FF46F6EE398] Function /Script/Sparta.SpartaAbilitySubsystem:SetCooldownPausedBasedOnActorAndTag [n: 481CF] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E24E0]
[00007FF46F6EE2B8] Function /Script/Sparta.SpartaAbilitySubsystem:SetCooldownPausedBasedOnActorAndClass [n: 481BB] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E23D0]
[00007FF46F6EE1D8] Function /Script/Sparta.SpartaAbilitySubsystem:SetAllCooldownsPaused [n: 481AF] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E2340]
[00007FF46F6EE0F8] Function /Script/Sparta.SpartaAbilitySubsystem:Reset [n: A18F] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E2020]
[00007FF46F6EE018] Function /Script/Sparta.SpartaAbilitySubsystem:IsTagOnGlobalCooldown [n: 481A3] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1D70]
[00007FF46F6EDF38] Function /Script/Sparta.SpartaAbilitySubsystem:IsClassOnGlobalCooldown [n: 48196] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1CA0]
[00007FF46F6EDE58] Function /Script/Sparta.SpartaAbilitySubsystem:IsActorAndTagOnCooldown [n: 48189] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1BD0]
[00007FF46F6EDD78] Function /Script/Sparta.SpartaAbilitySubsystem:IsActorAndClassOnCooldown [n: 4817B] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1B00]
[00007FF46F6EDC98] Function /Script/Sparta.SpartaAbilitySubsystem:GetGlobalCooldownFromTag [n: 4816E] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1A40]
[00007FF46F6EDBB8] Function /Script/Sparta.SpartaAbilitySubsystem:GetGlobalCooldownFromClass [n: 48160] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E19A0]
[00007FF46F6EDAD8] Function /Script/Sparta.SpartaAbilitySubsystem:GetCooldownFromActorAndTag [n: 48152] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E18D0]
[00007FF46F6ED9F8] Function /Script/Sparta.SpartaAbilitySubsystem:GetCooldownFromActorAndClass [n: 48143] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1800]
[00007FF46F6ED918] Function /Script/Sparta.SpartaAbilitySubsystem:ClearGlobalCooldownBasedOnTag [n: 48133] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1730]
[00007FF46F6ED838] Function /Script/Sparta.SpartaAbilitySubsystem:ClearGlobalCooldownBasedOnClass [n: 48122] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E16A0]
[00007FF46F6ED758] Function /Script/Sparta.SpartaAbilitySubsystem:ClearCooldownBasedOnActorAndTag [n: 48111] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E15D0]
[00007FF46F6ED678] Function /Script/Sparta.SpartaAbilitySubsystem:ClearCooldownBasedOnActorAndClass [n: 480FF] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1500]
[00007FF46F6ED598] Function /Script/Sparta.SpartaAbilitySubsystem:ApplyGlobalCooldownBasedOnTag [n: 480EF] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1340]
[00007FF46F6ED4B8] Function /Script/Sparta.SpartaAbilitySubsystem:ApplyGlobalCooldownBasedOnClass [n: 480DE] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1270]
[00007FF46F6ED3D8] Function /Script/Sparta.SpartaAbilitySubsystem:ApplyCooldownBasedOnActorAndTag [n: 480CD] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1160]
[00007FF46F6ED2F8] Function /Script/Sparta.SpartaAbilitySubsystem:ApplyCooldownBasedOnActorAndClass [n: 480BB] [c: 00007FF46EE55538] [or: 00007FF46F03B738] [f: 00007FF71C7E1050]
[00007FF46F03F738] Class /Script/Sparta.SpartaAttackAbility [n: 487D3] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F03F938]
[000001F4ACD99080] ObjectProperty /Script/Sparta.SpartaAttackAbility:AbilityHitPayload [o: 508] [n: 5870C4] [c: 00007FF721DE9420] [owr: 00007FF46F03F738] [pc: 00007FF46F049338]
[000001F4ACD99100] ObjectProperty /Script/Sparta.SpartaAttackAbility:AbilityHitPayloadDataModified [o: 510] [n: 5870B4] [c: 00007FF721DE9420] [owr: 00007FF46F03F738] [pc: 00007FF46F049338]
[00007FF46F043138] Class /Script/Sparta.CharacterPersistantAbilitySetData [n: 48BFF] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F042938]
[000001F4ACD9C780] StructProperty /Script/Sparta.CharacterPersistantAbilitySetData:AbilitySet [o: 28] [n: 5875C3] [c: 00007FF721DF9890] [owr: 00007FF46F043138] [ss: 00007FF46F3CD058]
[00007FF46F6F6978] Function /Script/Sparta.CharacterPersistantAbilitySetData:RemoveAbilitySet [n: 48C19] [c: 00007FF46EE55538] [or: 00007FF46F043138] [f: 00007FF71C7FC150]
[00007FF46F6F6898] Function /Script/Sparta.CharacterPersistantAbilitySetData:GiveAbilitySet [n: 48C11] [c: 00007FF46EE55538] [or: 00007FF46F043138] [f: 00007FF71C7FBE30]
[00007FF46F065138] Class /Script/Sparta.SpartaAbilitySetHandleContainer [n: 4BB10] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[000001F4ACE41500] StructProperty /Script/Sparta.SpartaAbilitySetHandleContainer:Handles [o: 28] [n: 586C9F] [c: 00007FF721DF9890] [owr: 00007FF46F065138] [ss: 00007FF46F3CCA58]
[00007FF46F065338] Class /Script/Sparta.SpartaAbilityDataAsset [n: 4BB21] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[000001F4ACE41480] StructProperty /Script/Sparta.SpartaAbilityDataAsset:AbilitySet [o: 30] [n: 5875C3] [c: 00007FF721DF9890] [owr: 00007FF46F065338] [ss: 00007FF46F3CD058]
[00007FF46F065538] Class /Script/Sparta.SpartaAbilityTagRelationshipMapping [n: 4BB2D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[000001F4ACE41400] ArrayProperty /Script/Sparta.SpartaAbilityTagRelationshipMapping:AbilityTagRelationships [o: 30] [n: 58A1E5] [c: 00007FF721DE8910] [owr: 00007FF46F065538] [ai: 000001F4ACE41380]
[000001F4ACE41380] StructProperty AbilityTagRelationships./Script/Sparta.SpartaAbilityTagRelationshipMapping:AbilityTagRelationships [o: 0] [n: 58A1E5] [c: 00007FF721DF9890] [owr: 000001F4ACE41400] [ss: 00007FF46F48DFB8]
```

### 1.3 `USpartaAbilitySystemComponent` -- what a sync layer must hook

Parent `/Script/GameplayAbilities.AbilitySystemComponent`. This is the component that actually runs
abilities on both the player character and on `ASpartaAbilityActor`.

| Property | Type | Offset | Notes |
|---|---|---|---|
| `BlockInputGameplayEffect` | `ClassProperty` (a `UGameplayEffect` subclass; the dump prints no `mc:` target) | `[o: 1260]` | GE applied to block input |
| `UnblockableInputBlockTags` | `MapProperty` `GameplayTag` -> `int32` | `[o: 1268]` | |
| `AdditionalTagContainers` | `MapProperty` `GameplayTag` -> `GameplayTagContainer` | `[o: 12B8]` | |
| `OnStatusEffectRemoved` | `MulticastInlineDelegateProperty` | `[o: 1308]` | sig `OnStatusEffectEvent__DelegateSignature(SpartaStatusEffect: ObjectProperty)` |
| `OnStatusEffectAdded` | `MulticastInlineDelegateProperty` | `[o: 1318]` | same signature object |
| `OnUpgradeStatChanged` | `MulticastInlineDelegateProperty` | `[o: 1328]` | sig `(Tag: GameplayTag, NewValue: FloatProperty)` |
| `OnStackChanged` | `MulticastInlineDelegateProperty` | `[o: 1338]` | sig `(InHandle: FActiveGameplayEffectHandle, NewCount: int32, OldCount: int32)` |
| `OnGameplayEffectFailedToApply` | `MulticastInlineDelegateProperty` | `[o: 1348]` | sig `(GameplayEffectSpec: FGameplayEffectSpec)` |
| `TagRelationshipMapping` | `ObjectProperty of Sparta.SpartaAbilityTagRelationshipMapping` | `[o: 13C0]` | blocking/cancelling rules |
| `ActivePayloadModifiersArray` | `ArrayProperty` of `UHitPayloadModifier*` | `[o: 1448]` | |
| `ActiveIncomingPayloadModifiersArray` | `ArrayProperty` of `UHitPayloadModifier*` | `[o: 14A8]` | |
| `ActiveExecutionModifiersArray` | `ArrayProperty` of `UExecutionModifierCalculation*` | `[o: 1508]` | |
| `ActiveExecutionValueModifiersArray` | `ArrayProperty` of `UExecutionValueModifier*` | `[o: 1568]` | |
| `UpgradeStats` | `MapProperty` `GameplayTag` -> `float` | `[o: 15F8]` | |

Functions (dump order, full parameter lists -- `ReturnValue` = return value, everything else is an
argument):

| Function | Parameters (name: type) | Notes |
|---|---|---|
| `SetFloatAttributeValue` | `Attribute: StructProperty of GameplayAbilities.GameplayAttribute`, `NewValue: FloatProperty`, `ReturnValue: BoolProperty` | direct attribute write; the simplest sync primitive |
| `ResumeGameplayEffectDurationCountdowns` | `Handles: ArrayProperty of GameplayAbilities.ActiveGameplayEffectHandle` | pairs with the Pause below |
| `RemoveUpgradeStatValue` | `StatTag: GameplayTag` | |
| `RemoveUnblockableInputTags` | `Tags: GameplayTagContainer` | |
| `RemoveUnblockableInputTag` | `Tag: GameplayTag` | |
| `ProcessGameplayCueQueue` | (no params) | |
| `PauseGameplayEffectDurationCountdowns` | `Query: StructProperty of GameplayAbilities.GameplayEffectQuery`, `ReturnValue: ArrayProperty of GameplayAbilities.ActiveGameplayEffectHandle` | returns the handles to resume later |
| `OnEffectStackChanged` | `InHandle: FActiveGameplayEffectHandle`, `NewCount: IntProperty`, `OldCount: IntProperty` | broadcast target of `OnStackChanged` |
| `K2_GiveAbilityWithInputTag` | `AbilityClass: ClassProperty`, `Level: IntProperty`, `InputTag: GameplayTag`, `ReturnValue: StructProperty of GameplayAbilities.GameplayAbilitySpecHandle` | **the grant-a-bound-ability entry point** |
| `IsGameplayEffectExpirationHeld` | `Handle: FActiveGameplayEffectHandle`, `ReturnValue: BoolProperty` | |
| `GetUpgradeStatValue` | `StatTag: GameplayTag`, `ReturnValue: FloatProperty` | |
| `GetUnblockableInputTags` | `ReturnValue: StructProperty of GameplayTags.GameplayTagContainer` | |
| `GetActiveStatusEffects` | `ReturnValue: ArrayProperty` (of `USpartaStatusEffect*` -- element type not resolved in the dump, UNKNOWN) | |
| `GetActiveAbilities` | `ReturnValue: ArrayProperty` (no element type resolved) | |
| `ExecuteGameplayCueBP` | `GameplayCueTag: GameplayTag`, `GameplayCueParameters: FGameplayCueParameters` | |
| `ExecuteDeferredGameplayCue` | `GameplayCueTag: GameplayTag`, `GameplayCueParameters: FGameplayCueParameters` | |
| `AddUpgradeStatValue` | `StatTag: GameplayTag`, `Value: FloatProperty` | |
| `AddUnblockableInputTags` | `Tags: GameplayTagContainer` | |
| `AddUnblockableInputTag` | `Tag: GameplayTag` | |

Delegate signature objects (`/Script/Sparta.*__DelegateSignature`, params in dump order):

| Delegate signature | Parameters |
|---|---|
| `OnStackChanged__DelegateSignature` | `InHandle: FActiveGameplayEffectHandle [0x0]`, `NewCount: IntProperty [0x8]`, `OldCount: IntProperty [0xC]` |
| `OnStatusEffectEvent__DelegateSignature` | `SpartaStatusEffect: ObjectProperty [0x0]` |
| `OnUpgradeStatChanged__DelegateSignature` | `Tag: GameplayTag [0x0]`, `NewValue: FloatProperty [0x8]` |
| `OnGameplayEffectFailedToApply__DelegateSignature` | `GameplayEffectSpec: StructProperty [0x0]` |
| `OnInputQueued__DelegateSignature` (used by `USpartaInputComponent`) | `InputTag: StructProperty [0x0]`, `bPressed: BoolProperty [0x8]` |
| `SpartaOnMontageInterrupted__DelegateSignature` | `Montage: ObjectProperty [0x0]`, `InterruptTag: GameplayTag [0x8]`, `Cause: EnumProperty [0x10]` |

Evidence -- ASC properties, functions, delegate signatures:
```
[00007FF46F03B938] Class /Script/Sparta.SpartaAbilitySystemComponent [n: 48207] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF98538]
[000001F4ACD7FF00] MulticastInlineDelegateProperty /Script/Sparta.SpartaAbilitySystemComponent:OnStatusEffectRemoved [o: 1308] [n: 586D32] [c: 00007FF721DE9010] [owr: 00007FF46F03B938] [df: 00007FF46F3D0F08]
[000001F4ACD7FF80] MulticastInlineDelegateProperty /Script/Sparta.SpartaAbilitySystemComponent:OnStatusEffectAdded [o: 1318] [n: 586D27] [c: 00007FF721DE9010] [owr: 00007FF46F03B938] [df: 00007FF46F3D0F08]
[000001F4ACD7E780] MulticastInlineDelegateProperty /Script/Sparta.SpartaAbilitySystemComponent:OnUpgradeStatChanged [o: 1328] [n: 586D1C] [c: 00007FF721DE9010] [owr: 00007FF46F03B938] [df: 00007FF46F3D1CA8]
[000001F4ACD7E800] MulticastInlineDelegateProperty /Script/Sparta.SpartaAbilitySystemComponent:OnStackChanged [o: 1338] [n: 586D14] [c: 00007FF721DE9010] [owr: 00007FF46F03B938] [df: 00007FF46F3D0E28]
[000001F4ACD7E880] MulticastInlineDelegateProperty /Script/Sparta.SpartaAbilitySystemComponent:OnGameplayEffectFailedToApply [o: 1348] [n: 586D04] [c: 00007FF721DE9010] [owr: 00007FF46F03B938] [df: 00007FF46F3CF3A0]
[000001F4ACD7E900] ObjectProperty /Script/Sparta.SpartaAbilitySystemComponent:TagRelationshipMapping [o: 13C0] [n: 586CF8] [c: 00007FF721DE9420] [owr: 00007FF46F03B938] [pc: 00007FF46F065538]
[000001F49CD2E980] MapProperty /Script/Sparta.SpartaAbilitySystemComponent:UpgradeStats [o: 15F8] [n: 586CA4] [c: 00007FF721DE8F70] [owr: 00007FF46F03B938] [kp: 000001F4ACD7ED80] [vp: 000001F4ACD63110]
[000001F4ACD63110] FloatProperty UpgradeStats./Script/Sparta.SpartaAbilitySystemComponent:UpgradeStats [o: 8] [n: 586CA4] [c: 00007FF721DE9380] [owr: 000001F49CD2E980]
[00007FF46F6EF5F8] Function /Script/Sparta.SpartaAbilitySystemComponent:SetFloatAttributeValue [n: 48307] [c: 00007FF46EE55538] [or: 00007FF46F03B938] [f: 00007FF71C7EA050]
[00007FF46F6EEEF8] Function /Script/Sparta.SpartaAbilitySystemComponent:K2_GiveAbilityWithInputTag [n: 48291] [c: 00007FF46EE55538] [or: 00007FF46F03B938] [f: 00007FF71C7E91E0]
[000001F4A69E38F0] IntProperty /Script/GameplayAbilities.AbilityAsync_WaitGameplayTagCountChanged:AsyncWaitGameplayTagCountDelegate__DelegateSignature:TagCount [o: 0] [n: 5120C0] [c: 00007FF721DE91F0] [owr: 00007FF46F38C678]
[000001F49CDBBC80] StructProperty /Script/Engine.OnActorReady__DelegateSignature:InHandle [o: 0] [n: 4E4CCB] [c: 00007FF721DF9890] [owr: 00007FF46F2EC150] [ss: 00007FF46F2EBC08]
[000001F49CDBD280] ObjectProperty /Script/Engine.OnMontageBlendedInEndedMCDelegate__DelegateSignature:Montage [o: 0] [n: 4E50E3] [c: 00007FF721DE9420] [owr: 00007FF46F2ED8C8] [pc: 00007FF46EF2E338]
[000001F49CDBD180] ObjectProperty /Script/Engine.OnMontageBlendingOutStartedMCDelegate__DelegateSignature:Montage [o: 0] [n: 4E50E3] [c: 00007FF721DE9420] [owr: 00007FF46F2EDA08] [pc: 00007FF46EF2E338]
[000001F49CDBD080] ObjectProperty /Script/Engine.OnMontageEndedMCDelegate__DelegateSignature:Montage [o: 0] [n: 4E50E3] [c: 00007FF721DE9420] [owr: 00007FF46F2EDB38] [pc: 00007FF46EF2E338]
[000001F49CDBCF80] ObjectProperty /Script/Engine.OnMontageSectionChangedMCDelegate__DelegateSignature:Montage [o: 0] [n: 4E50E3] [c: 00007FF721DE9420] [owr: 00007FF46F2EDC68] [pc: 00007FF46EF2E338]
[000001F49CDBCF00] ObjectProperty /Script/Engine.OnMontageStartedMCDelegate__DelegateSignature:Montage [o: 0] [n: 4E50E3] [c: 00007FF721DE9420] [owr: 00007FF46F2EDDA8] [pc: 00007FF46EF2E338]
[000001F4A69CD680] StructProperty /Script/GameplayAbilities.OnGameplayTagChangedEventWrapperSignature__DelegateSignature:Tag [o: 0] [n: 4E5A4A] [c: 00007FF721DF9890] [owr: 00007FF46F38A7B8] [ss: 00007FF46F3890A8]
[000001F4A69E22A0] IntProperty /Script/GameplayAbilities.OnGameplayTagChangedEventWrapperSignature__DelegateSignature:TagCount [o: 8] [n: 5120C0] [c: 00007FF721DE91F0] [owr: 00007FF46F38A7B8]
[000001F4A69E1EB0] IntProperty /Script/GameplayAbilities.WaitGameplayTagCountDelegate__DelegateSignature:TagCount [o: 0] [n: 5120C0] [c: 00007FF721DE91F0] [owr: 00007FF46F38B7F8]
[000001F4A6A3D580] ObjectProperty /Script/CommonUI.HardwareVisibilityTagsChangedDynamicEvent__DelegateSignature:TagSubsystem [o: 0] [n: 51956B] [c: 00007FF721DE9420] [owr: 00007FF46F39CAF8] [pc: 00007FF46EFD8938]
[000001F4A6CB1B80] StructProperty /Script/Sparta.OnDialogueStartedEvent__DelegateSignature:Tags [o: 10] [n: 4CBA34] [c: 00007FF721DF9890] [owr: 00007FF46F3CEE80] [ss: 00007FF46F388FE8]
[000001F4A6CB1400] StructProperty /Script/Sparta.OnGameplayEffectFailedToApply__DelegateSignature:GameplayEffectSpec [o: 0] [n: F5FD] [c: 00007FF721DF9890] [owr: 00007FF46F3CF3A0] [ss: 00007FF46F388B00]
[000001F4A6CB1280] StructProperty /Script/Sparta.OnInputQueued__DelegateSignature:InputTag [o: 0] [n: 524FA6] [c: 00007FF721DF9890] [owr: 00007FF46F3CF560] [ss: 00007FF46F3890A8]
[000001F4A6CB2380] StructProperty /Script/Sparta.OnStackChanged__DelegateSignature:InHandle [o: 0] [n: 4E4CCB] [c: 00007FF721DF9890] [owr: 00007FF46F3D0E28] [ss: 00007FF46F389C70]
[000001F4A6CB2300] ObjectProperty /Script/Sparta.OnStatusEffectEvent__DelegateSignature:SpartaStatusEffect [o: 0] [n: 4A6DF] [c: 00007FF721DE9420] [owr: 00007FF46F3D0F08] [pc: 00007FF46F059138]
[000001F4A6CB3700] StructProperty /Script/Sparta.OnUpgradeStatChanged__DelegateSignature:Tag [o: 0] [n: 4E5A4A] [c: 00007FF721DF9890] [owr: 00007FF46F3D1CA8] [ss: 00007FF46F3890A8]
[000001F4A6CB5B80] ObjectProperty /Script/Sparta.SpartaOnMontageInterrupted__DelegateSignature:Montage [o: 0] [n: 4E50E3] [c: 00007FF721DE9420] [owr: 00007FF46F3D3AE0] [pc: 00007FF46EF2E338]
```

### 1.4 Enums the ability system uses

Values are quoted from `raw\CXXHeaderDump\Sparta_enums.hpp` (HDR). The dump itself only proves the
enum *type* attached to each property (`[em: ...]`), not the enumerator names.

| Enum | Values | Used by |
|---|---|---|
| `ESpartaAbilityGlobalCooldown` | `Disabled = 0`, `AbilityBased = 1`, `TagBased = 2` | `SpartaGameplayAbility:Cooldown`, `:GlobalCooldown` |
| `ESpartaAbilityCooldownApplication` | `OnCommit = 0`, `OnEnd = 1`, `Custom = 2` | `SpartaGameplayAbility:CooldownApplication` |
| `ESpartaAbilityActivationPolicy` | `OnInputTriggered = 0`, `WhileInputActive = 1`, `OnSpawn = 2` | `SpartaGameplayAbility:ActivationPolicy` |
| `ESpartaAbilityActivationGroup` | `Independent = 0`, `Exclusive_Replaceable = 1`, `Exclusive_Blocking = 2` | `SpartaGameplayAbility:ActivationGroup` |
| `EAbilityInputBlockBehavior` | `BlockAll = 0`, `BlockSpecified = 1`, `AllowSpecified = 2` | `FAbilityInputTagBlock:BlockBehavior` |
| `ESpartaInputValueMethod` | `Absolute = 0`, `Accumulative = 1`, `Normalized = 2` | `USpartaInputInteraction:SetCurrentValue` |
| `ESpartaMontageInterruptCause` | `GameplayEvent = 0`, `TagAdded = 1`, `TagRemoved = 2`, `AlreadyMet = 3`, `Manual = 4` | `OnMontageInterrupted` delegate |
| `ESpartaMontageInterruptBeginCheck` | `None = 0`, `AddedAndRemoved = 1`, `AddedOnly = 2`, `RemovedOnly = 3` | `SpartaMontageInterrupterNotifyState` |
| `ESpartaMontageInterruptBlendMode` | `MontageDefault = 0`, `Standard = 1`, `Inertialization = 2` | `SpartaMontageInterrupterNotifyState` |
| `ESpartaHitTraceSetup` | `UseWeaponDefaults = 0`, `Manual = 1` | `SpartaAnimNotify*_HitCheck` |
| `ESpartaHitTraceActorSource` | `Weapon = 0`, `Character = 1` | `SpartaAnimNotify*_HitCheck` |
| `ESpartaHitTraceType` | `CollisionShapes = 0`, `ComponentReferences = 1`, `PhysicsBodies = 2` | `SpartaAnimNotify*_HitCheck` |
| `ESpartaHitTraceSettings` | `Default = 0`, `Custom = 1` | `SpartaAnimNotify*_HitCheck` |
| `ESpartaHitTraceMotionSubstepLevel` | `None = 0`, `Low = 1`, `Medium = 2`, `High = 3` | `SpartaAnimNotifyState_HitCheck` |
| `ESpartaHitTraceTeam` | `None = 0`, `Enemies = 1`, `Neutrals = 2`, `Allies = 4` | hit-trace team filter |

Evidence -- header lines (HDR) plus the dump's enum-typed properties:

```
    OnInputTriggered = 0,
    WhileInputActive = 1,
    OnSpawn = 2,
    ESpartaAbilityActivationPolicy_MAX = 3,
    OnCommit = 0,
    OnEnd = 1,
    Custom = 2,
    ESpartaAbilityCooldownApplication_MAX = 3,
    Disabled = 0,
    AbilityBased = 1,
    TagBased = 2,
    ESpartaAbilityGlobalCooldown_MAX = 3,
    Independent = 0,
    Exclusive_Replaceable = 1,
    Exclusive_Blocking = 2,
    MAX = 3,
    BlockAll = 0,
    BlockSpecified = 1,
    AllowSpecified = 2,
    EAbilityInputBlockBehavior_MAX = 3,
```

---

## 2. Attribute sets -- every attribute, every function

Chain: `/Script/GameplayAbilities.AttributeSet` -> `/Script/Sparta.SpartaAttributeSet` (declares **no**
members of its own, `Sparta.hpp`: `class USpartaAttributeSet : public UAttributeSet { }; // Size: 0x30`)
-> the seven concrete sets below. Every attribute is an `FGameplayAttributeData` (size 0x10), which is
what `FGameplayAttribute` resolves against; offsets are the dump offsets.

**Which actor owns which set** is not in the object dump (set ownership is decided at runtime by the
`FSpartaAbilitySet_AttributeSet` grants). `UNKNOWN`: the exact grant list per shell/pawn.

### 2.1 `USpartaHealthSet` -- 36 attributes, 2 functions

| Attribute | Offset | Attribute | Offset |
|---|---|---|---|
| `Health` | `[o: 60]` | `MaxHealth` | `[o: 70]` |
| `ShellHealth` | `[o: 80]` | `MaxShellHealth` | `[o: 90]` |
| `MinHealth` | `[o: A0]` | `MinShellHealth` | `[o: B0]` |
| `Armor` | `[o: C0]` | `Resolve` | `[o: D0]` |
| `MaxResolve` | `[o: E0]` | `Poise` | `[o: F0]` |
| `MaxPoise` | `[o: 100]` | `BleedResistance` | `[o: 110]` |
| `BleedResistanceMultiplier` | `[o: 120]` | `FrostResistance` | `[o: 130]` |
| `FrostResistanceIncrement` | `[o: 140]` | `ShockResistance` | `[o: 150]` |
| `ShockResistanceIncrement` | `[o: 160]` | `WaspsResistance` | `[o: 170]` |
| `WaspsResistanceIncrement` | `[o: 180]` | `CurseResistance` | `[o: 190]` |
| `CurseResistanceIncrement` | `[o: 1A0]` | `ConfusionResistance` | `[o: 1B0]` |
| `ConfusionResistanceIncrement` | `[o: 1C0]` | `ConfusionBreakDamage` | `[o: 1D0]` |
| `CosmicDiseaseResistance` | `[o: 1E0]` | `CosmicDiseaseResistanceIncrement` | `[o: 1F0]` |
| `StasisWeakness` | `[o: 200]` | `BreakResistance` | `[o: 210]` |
| `MaxBreakResistance` | `[o: 220]` | `RiposteWeakness` | `[o: 230]` |
| `StoneStunTime` | `[o: 240]` | `Healing` | `[o: 258]` |
| `Damage` | `[o: 268]` | `PoiseDamage` | `[o: 278]` |
| `BreakDamage` | `[o: 288]` | `ResolveModify` | `[o: 298]` |

Note the **0x250 gap**: 0x240 `StoneStunTime` (+0x10 = 0x250) is followed by `Healing` at 0x258, so
0x250..0x257 holds something that is not a replicated attribute (header HDR confirms the jump
`StoneStunTime // 0x0240` -> `Healing // 0x0258`). Total size `0x2A8`.

Functions: `OnRep_MaxHealth(const FGameplayAttributeData& OldValue)` and
`OnRep_Health(const FGameplayAttributeData& OldValue)` -- both with exactly one parameter `OldValue:
StructProperty of GameplayAbilities.GameplayAttributeData`. `Health`/`MaxHealth` are the only two
attributes with `OnRep_` functions, i.e. **the two that replicate as an event** -- GUESS: these two are
the intended client-side notification points for health changes.


Evidence -- health set class, first/last attributes, OnRep functions:
```
[00007FF46F031538] Class /Script/Sparta.SpartaAttributeSet [n: 4778D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF99738]
[00007FF46F065D38] Class /Script/Sparta.SpartaHealthSet [n: 4BB8D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F031538]
[000001F4ACE44500] StructProperty /Script/Sparta.SpartaHealthSet:Health [o: 60] [n: 57EE40] [c: 00007FF721DF9890] [owr: 00007FF46F065D38] [ss: 00007FF46F465798]
[000001F4ACE43300] StructProperty /Script/Sparta.SpartaHealthSet:StoneStunTime [o: 240] [n: 58A3F6] [c: 00007FF721DF9890] [owr: 00007FF46F065D38] [ss: 00007FF46F465798]
[000001F4ACE43380] StructProperty /Script/Sparta.SpartaHealthSet:Healing [o: 258] [n: 58611A] [c: 00007FF721DF9890] [owr: 00007FF46F065D38] [ss: 00007FF46F465798]
[00007FF46F72EF98] Function /Script/Sparta.SpartaHealthSet:OnRep_MaxHealth [n: 4BB9D] [c: 00007FF46EE55538] [or: 00007FF46F065D38] [f: 00007FF71C890170]
[00007FF46F72EEB8] Function /Script/Sparta.SpartaHealthSet:OnRep_Health [n: 4BB96] [c: 00007FF46EE55538] [or: 00007FF46F065D38] [f: 00007FF71C88FFF0]
```

### 2.2 `USpartaCombatSet` -- 50 attributes, 4 functions (size `0x350`)

Global combat scalars (damage, speed, status-effect bases, mitigations). Applies to player *and*
enemies.

| Attribute | Offset | Attribute | Offset |
|---|---|---|---|
| `BaseDamage` | `[o: 30]` | `BaseHeal` | `[o: 40]` |
| `BasePoiseDamage` | `[o: 50]` | `BaseBreakDamage` | `[o: 60]` |
| `AttackSpeedBonus` | `[o: 70]` | `BaseWindedCounters` | `[o: 80]` |
| `BasePoisonDamage` | `[o: 90]` | `BasePoisonDuration` | `[o: A0]` |
| `BaseBleedDuration` | `[o: B0]` | `BaseBurnDamage` | `[o: C0]` |
| `BaseBurnDuration` | `[o: D0]` | `BaseFrostDuration` | `[o: E0]` |
| `BaseFreezeDuration` | `[o: F0]` | `BaseLightningDuration` | `[o: 100]` |
| `BaseLightningDamage` | `[o: 110]` | `BaseShockDamage` | `[o: 120]` |
| `BaseShockRadius` | `[o: 130]` | `BaseWaspsDuration` | `[o: 140]` |
| `BaseWaspsDamage` | `[o: 150]` | `BaseFragileDuration` | `[o: 160]` |
| `BasePerforationDuration` | `[o: 170]` | `BasePhantomDuration` | `[o: 180]` |
| `BasePhantomHealthDamage` | `[o: 190]` | `BasePhantomPoiseDamage` | `[o: 1A0]` |
| `BaseChaosDuration` | `[o: 1B0]` | `BaseChaosDamage` | `[o: 1C0]` |
| `BaseCurseStackDuration` | `[o: 1D0]` | `BaseCursedStateDuration` | `[o: 1E0]` |
| `BaseTraumaDuration` | `[o: 1F0]` | `BaseTraumaDamage` | `[o: 200]` |
| `BaseConfusionDuration` | `[o: 210]` | `BaseKnockbackMultiplier` | `[o: 220]` |
| `BaseKnockbackStrength` | `[o: 230]` | `PoisonMitigation` | `[o: 240]` |
| `BleedMitigation` | `[o: 250]` | `LightningMitigation` | `[o: 260]` |
| `BurnMitigation` | `[o: 270]` | `FrostMitigation` | `[o: 280]` |
| `CurseMitigation` | `[o: 290]` | `StasisMitigation` | `[o: 2A0]` |
| `PhantomMitigation` | `[o: 2B0]` | `ChaosMitigation` | `[o: 2C0]` |
| `WaspsMitigation` | `[o: 2D0]` | `ConfusionMitigation` | `[o: 2E0]` |
| `BaseCosmicDiseaseDuration` | `[o: 2F0]` | `CosmicDiseaseMitigation` | `[o: 300]` |
| `RiposteDamageMultiplier` | `[o: 310]` | `BaseDamageReduction` | `[o: 320]` |
| `BaseBreakDamageReduction` | `[o: 330]` | `IncomingDamageMultiplayer` | `[o: 340]` |

Functions: `OnRep_BaseHeal`, `OnRep_BaseDamageReduction`, `OnRep_BaseDamage`,
`OnRep_BaseBreakDamageReduction` -- each with the single parameter
`OldValue: StructProperty of GameplayAbilities.GameplayAttributeData`.

### 2.3 `UPlayerAttributeSet` -- 32 attributes, 0 functions (size `0x230`)

The **player-only** set: economy, synergy, revive meter, heal charges, item bonuses, and the melee
ability costs.

| Attribute | Offset | Attribute | Offset |
|---|---|---|---|
| `CurrentEXP` | `[o: 30]` | `ModifyEXP` | `[o: 40]` |
| `CriticalChance` | `[o: 50]` | `CriticalBonus` | `[o: 60]` |
| `BaseSynergyGain` | `[o: 70]` | `BaseSynergyDrain` | `[o: 80]` |
| `BaseSynergyDetachCost` | `[o: 90]` | `BaseResolveGain` | `[o: A0]` |
| `BaseResolveLost` | `[o: B0]` | `PrimaryMeleeAbilityCost` | `[o: C0]` |
| `SecondaryMeleeAbilityCost` | `[o: D0]` | `HealCurrentCharges` | `[o: E0]` |
| `HealMaxCharges` | `[o: F0]` | `HealAmountPerCharge` | `[o: 100]` |
| `GloomMultiplier` | `[o: 110]` | `GloomBonus` | `[o: 120]` |
| `GoldMultiplier` | `[o: 130]` | `GoldBonus` | `[o: 140]` |
| `ShellReviveCurrentProgress` | `[o: 150]` | `ShellReviveMaxProgress` | `[o: 160]` |
| `ShellReviveProgressGain` | `[o: 170]` | `ShellReviveMaxProgressMultiplier` | `[o: 180]` |
| `PPItemResource` | `[o: 190]` | `PPItemMaxResource` | `[o: 1A0]` |
| `PPItemCost` | `[o: 1B0]` | `ItemMaxHealthBonus` | `[o: 1C0]` |
| `ItemMaxResolveBonus` | `[o: 1D0]` | `ItemPoisonMitigationBonus` | `[o: 1E0]` |
| `ItemBleedMitigationBonus` | `[o: 1F0]` | `ItemBurnMitigationBonus` | `[o: 200]` |
| `ItemLightningMitigationBonus` | `[o: 210]` | `ItemFrostMitigationBonus` | `[o: 220]` |

### 2.4 `UShellAbilityAttributeSet` -- 32 attributes, 0 functions (size `0x230`)

Per-shell-ability numbers. One instance per shell ability, likely granted by the ability's
`FSpartaAbilitySet_AttributeSet` grant -- GUESS: it is what the shell abilities' cost/damage
calculations read (`PrimaryEnergyCost`/`SecondaryEnergyCost`/`Cooldown`/`Cost`).

| Attribute | Offset | Attribute | Offset |
|---|---|---|---|
| `PrimaryEnergyCost` | `[o: 30]` | `SecondaryEnergyCost` | `[o: 40]` |
| `duration` | `[o: 50]` | `Cooldown` | `[o: 60]` |
| `Damage` | `[o: 70]` | `DamageBonus` | `[o: 80]` |
| `DamageReduction` | `[o: 90]` | `MinAmount` | `[o: A0]` |
| `MaxAmount` | `[o: B0]` | `CurrentAmount` | `[o: C0]` |
| `Stacks` | `[o: D0]` | `CriticalHitChanceBonus` | `[o: E0]` |
| `CriticalHitDamageBonus` | `[o: F0]` | `AttacksNumber` | `[o: 100]` |
| `PoiseDamage` | `[o: 110]` | `CustomDamage` | `[o: 120]` |
| `MaxDamage` | `[o: 130]` | `MaxPoiseDamage` | `[o: 140]` |
| `Cost` | `[o: 150]` | `Radius` | `[o: 160]` |
| `CustomCost` | `[o: 170]` | `HealingMeter` | `[o: 180]` |
| `MaxHealingMeter` | `[o: 190]` | `CustomHealingFactor` | `[o: 1A0]` |
| `Chance` | `[o: 1B0]` | `Threshold` | `[o: 1C0]` |
| `ResolveGainBoost` | `[o: 1D0]` | `BreakDamage` | `[o: 1E0]` |
| `MaxBreakDamage` | `[o: 1F0]` | `SecondaryDamage` | `[o: 200]` |
| `SecondaryPoiseDamage` | `[o: 210]` | `SecondaryBreakDamage` | `[o: 220]` |

### 2.5 `USidearmAttributeSet` -- 8 attributes, 0 functions (size `0xB0`)

| Attribute | Offset | Attribute | Offset |
|---|---|---|---|
| `CurrentAmmo` | `[o: 30]` | `MaxAmmo` | `[o: 40]` |
| `ReloadDuration` | `[o: 50]` | `ActivationCost` | `[o: 60]` |
| `PrimaryEnergyCost` | `[o: 70]` | `MinRequiredEnergy` | `[o: 80]` |
| `SecondaryEnergyCost` | `[o: 90]` | `BuffEnergyCost` | `[o: A0]` |

`CurrentAmmo` / `MaxAmmo` / `ReloadDuration` / `ActivationCost` are the four fields a co-op layer must
mirror for a sidearm; `MinRequiredEnergy`, `PrimaryEnergyCost`, `SecondaryEnergyCost`,
`BuffEnergyCost` are the dark-power energy economy.

### 2.6 `UDarkPowerAttributeSet` -- 4 attributes, 0 functions (size `0x70`)

| Attribute | Offset |
|---|---|
| `CurrentAmmo` | `[o: 30]` |
| `MaxAmmo` | `[o: 40]` |
| `ReloadDuration` | `[o: 50]` |
| `ActivationCost` | `[o: 60]` |

Same four field names as `USidearmAttributeSet` but as a *separate set* -- i.e. a dark-power weapon
carries its own ammo attributes, independent of the sidearm.

### 2.7 `UEnemyAttributeSet` -- 3 attributes, 0 functions (size `0x60`)

| Attribute | Offset | Notes |
|---|---|---|
| `WindedCounters` | `[o: 30]` | stagger counter |
| `ModifyWindedCounters` | `[o: 40]` | |
| `TarstoneEXP` | `[o: 50]` | tarstone reward |


Evidence -- combat / player / shell-ability / sidearm / dark-power / enemy sets:
```
[000001F4ACD59B00] StructProperty /Script/Sparta.DarkPowerAttributeSet:CurrentAmmo [o: 30] [n: 585FA6] [c: 00007FF721DF9890] [owr: 00007FF46F031338] [ss: 00007FF46F465798]
[000001F4ACD59C80] StructProperty /Script/Sparta.DarkPowerAttributeSet:ActivationCost [o: 60] [n: 585F91] [c: 00007FF721DF9890] [owr: 00007FF46F031338] [ss: 00007FF46F465798]
[000001F4ACD5B980] StructProperty /Script/Sparta.EnemyAttributeSet:WindedCounters [o: 30] [n: 5860ED] [c: 00007FF721DF9890] [owr: 00007FF46F032D38] [ss: 00007FF46F465798]
[000001F4ACD5BA80] StructProperty /Script/Sparta.EnemyAttributeSet:TarstoneEXP [o: 50] [n: 5860DB] [c: 00007FF721DF9890] [owr: 00007FF46F032D38] [ss: 00007FF46F465798]
[000001F4ACD70800] StructProperty /Script/Sparta.PlayerAttributeSet:CurrentEXP [o: 30] [n: 52543E] [c: 00007FF721DF9890] [owr: 00007FF46F037B38] [ss: 00007FF46F465798]
[000001F4ACD70D80] StructProperty /Script/Sparta.PlayerAttributeSet:HealCurrentCharges [o: E0] [n: 5863AA] [c: 00007FF721DF9890] [owr: 00007FF46F037B38] [ss: 00007FF46F465798]
[000001F4ACD5F800] StructProperty /Script/Sparta.PlayerAttributeSet:ShellReviveCurrentProgress [o: 150] [n: 58636C] [c: 00007FF721DF9890] [owr: 00007FF46F037B38] [ss: 00007FF46F465798]
[000001F4ACD79180] StructProperty /Script/Sparta.ShellAbilityAttributeSet:PrimaryEnergyCost [o: 30] [n: 58684D] [c: 00007FF721DF9890] [owr: 00007FF46F03A538] [ss: 00007FF46F465798]
[000001F4ACD77980] StructProperty /Script/Sparta.ShellAbilityAttributeSet:Cost [o: 150] [n: 56AAA3] [c: 00007FF721DF9890] [owr: 00007FF46F03A538] [ss: 00007FF46F465798]
[000001F4ACD78D80] StructProperty /Script/Sparta.SidearmAttributeSet:CurrentAmmo [o: 30] [n: 585FA6] [c: 00007FF721DF9890] [owr: 00007FF46F03A938] [ss: 00007FF46F465798]
[000001F4ACD79000] StructProperty /Script/Sparta.SidearmAttributeSet:MinRequiredEnergy [o: 80] [n: 58685F] [c: 00007FF721DF9890] [owr: 00007FF46F03A938] [ss: 00007FF46F465798]
[000001F4ACE43980] StructProperty /Script/Sparta.SpartaCombatSet:BaseDamage [o: 30] [n: 555D06] [c: 00007FF721DF9890] [owr: 00007FF46F065938] [ss: 00007FF46F465798]
[000001F4ACE43100] StructProperty /Script/Sparta.SpartaCombatSet:IncomingDamageMultiplayer [o: 340] [n: 58A203] [c: 00007FF721DF9890] [owr: 00007FF46F065938] [ss: 00007FF46F465798]
[00007FF46F72EDD8] Function /Script/Sparta.SpartaCombatSet:OnRep_BaseHeal [n: 4BB7D] [c: 00007FF46EE55538] [or: 00007FF46F065938] [f: 00007FF71C88D7D0]
[00007FF46F031338] Class /Script/Sparta.DarkPowerAttributeSet [n: 47781] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F031538]
[00007FF46F032D38] Class /Script/Sparta.EnemyAttributeSet [n: 47918] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F031538]
[00007FF46F037B38] Class /Script/Sparta.PlayerAttributeSet [n: 47B90] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F031538]
[00007FF46F03A538] Class /Script/Sparta.ShellAbilityAttributeSet [n: 47FA0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F031538]
[00007FF46F03A938] Class /Script/Sparta.SidearmAttributeSet [n: 47FBE] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F031538]
[00007FF46F065938] Class /Script/Sparta.SpartaCombatSet [n: 4BB4D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F031538]
```

### 2.8 Which set belongs to whom -- what I could and could not establish

* `USpartaHealthSet` and `USpartaCombatSet` carry `OnRep_*` functions; the others do not. In UE that
  means only those six attributes are declared `ReplicatedUsing` -- GUESS: those are the attributes the
  engine itself replicates, everything else is local and a co-op layer must replicate it explicitly.
* `UNKNOWN` which set is granted to the player pawn. Tried:
  `SELECT ... FROM obj WHERE type='Class'` for an owning component that lists attribute-set types, and
  `parse_dump.py show` on `CharacterPersistantAbilitySetData` (it only carries one `FSpartaAbilitySet`).
  The grant lists live in Blueprint default values, which the object dump does not print.

---

## 3. The input layer

### 3.1 `USpartaInputConfig` (DataAsset) -- the tag<->action table

| Property | Type (dump) | Offset |
|---|---|---|
| `NativeInputActions` | `ArrayProperty`->`StructProperty of Sparta.SpartaInputAction` (`[ai: ...]` child row) | `[o: 30]` |
| `AbilityInputActions` | `ArrayProperty`->`StructProperty of Sparta.SpartaInputAction` | `[o: 40]` |
| `InputTag_Root` | `StructProperty of GameplayTags.GameplayTag` | `[o: 50]` |
| `InputTag_Move_Forward` | `GameplayTag` | `[o: 58]` |
| `InputTag_Move_Backward` | `GameplayTag` | `[o: 60]` |
| `InputTag_Move_Left` | `GameplayTag` | `[o: 68]` |
| `InputTag_Move_Right` | `GameplayTag` | `[o: 70]` |
| `InputTag_Move_Stick` | `GameplayTag` | `[o: 78]` |
| `InputTag_Look_Mouse` | `GameplayTag` | `[o: 80]` |
| `InputTag_Look_Stick` | `GameplayTag` | `[o: 88]` |
| `UnblockableInputTags` | `GameplayTagContainer` | `[o: 90]` |

| Function | Parameters (name: type) |
|---|---|
| `FindNativeInputActionForTag` | `InputTag: StructProperty of GameplayTags.GameplayTag`, `bLogNotFound: BoolProperty`, `ReturnValue: ObjectProperty of EnhancedInput.InputAction` |
| `FindAbilityInputActionForTag` | `InputTag: StructProperty of GameplayTags.GameplayTag`, `bLogNotFound: BoolProperty`, `ReturnValue: ObjectProperty of EnhancedInput.InputAction` |

`FSpartaInputAction` (struct, size `0x10`): `InputAction: class UInputAction*` (offset `0x0000`),
`InputTag: FGameplayTag` (`0x0008`). This is *the* mapping table: one row = one input action bound to
one gameplay tag, and the tag is what an ability is granted against
(`K2_GiveAbilityWithInputTag(AbilityClass, Level, InputTag)`).

### 3.2 `USpartaInputComponent` (parent `UEnhancedInputComponent`) -- press/release + queue + blocking

| Property | Type | Offset |
|---|---|---|
| `OnInputQueued` | `MulticastInlineDelegateProperty` (sig `OnInputQueued__DelegateSignature`) | `[o: 178]` |
| `PlayerCharacter` | `ObjectProperty of Sparta.SpartaPlayerCharacter` | `[o: 188]` |
| `PlayerController` | `ObjectProperty of Sparta.SpartaPlayerController` | `[o: 190]` |
| `InputQueue` | `StructProperty of Sparta.InputQueue` | `[o: 198]` |
| `ActiveBlocks` | `ArrayProperty`->`StructProperty of Sparta.AbilityInputTagBlock` | `[o: 1B0]` |

| Function | Parameters (name: type) |
|---|---|
| `StopBlockingInput` | `InputBlock: StructProperty of Sparta.AbilityInputTagBlock` |
| `StartBlockingInput` | `InputBlock: StructProperty of Sparta.AbilityInputTagBlock` |
| `RemoveSingleFrameInputBlocks` | (no params) |
| `InvertMoveInput` | `bInvert: BoolProperty` |

Related structs (header HDR, exact layout):

| Struct | Members |
|---|---|
| `FAbilityInputTagBlock` (`0x30`) | `BlockBehavior: EAbilityInputBlockBehavior` (`0x0000`), `InputTag: FGameplayTagContainer` (`0x0008`) |
| `FInputQueue` (`0x10`) | `InputQueue: TArray<FCachedAbilityInput>` (`0x0000`) |
| `FCachedAbilityInput` (`0x18`) | `CacheTime: double` (`0x0000`), `InputTag: FGameplayTag` (`0x0008`), `bPressed: bool` (`0x0010`) |

`OnInputQueued(InputTag: FGameplayTag, bPressed: bool)` is the single event that says "the local player
pressed/released the button bound to this tag". That is the event a co-op input-mirroring layer wants.

### 3.3 `USpartaInputInteraction` (UObject) -- the "hold button to interact" object

| Property | Type | Offset |
|---|---|---|
| `InputID` | `NameProperty` | `[o: 30]` |
| `Action` | `NameProperty` | `[o: 38]` |
| `OnInputStarted` | `DelegateProperty` | `[o: 40]` |
| `OnInputOngoing` | `DelegateProperty` | `[o: 50]` |
| `OnInputCompleted` | `DelegateProperty` | `[o: 60]` |
| `OnInputCancelled` | `DelegateProperty` | `[o: 70]` |
| `OnProgressChanged` | `DelegateProperty` | `[o: 80]` |

`UNKNOWN`: the five delegate *signatures*. Tried: `parse_dump.py find raw\index.sqlite3
"FBindInputWithCallbacks|SpartaInputInteractionOn"` -> no such object exists in the dump (the
`DelegateProperty` rows carry no resolvable `ss:`/`pc:`). The header repeats the same handler
declaration five times, `void SpartaInputInteractionEvent(class USpartaInputInteraction* Input);`
(HDR), which strongly suggests each delegate passes the `USpartaInputInteraction*` itself --
GUESS.

| Function | Parameters (name: type) |
|---|---|
| `UnbindInput` | `PlayerController: ObjectProperty of Sparta.SpartaPlayerController`, `bClearCallbacks: BoolProperty` |
| `Tick` | `DeltaTime: FloatProperty` |
| `ShouldTick` | `ReturnValue: BoolProperty` |
| `SetCurrentValue` | `Value: FloatProperty`, `Method: ByteProperty` (TEnumAsByte of `ESpartaInputValueMethod`) |
| `IsProgressive` | `ReturnValue: BoolProperty` |
| `IsInputBound` | `ReturnValue: BoolProperty` |
| `GetTimeDilation` | `ReturnValue: FloatProperty` |
| `GetProgress` | `ReturnValue: FloatProperty` |
| `GetMinValue` | `ReturnValue: FloatProperty` |
| `GetMaxValue` | `ReturnValue: FloatProperty` |
| `GetInputAction` | `ReturnValue: ObjectProperty of EnhancedInput.InputAction` |
| `GetCurrentValue` | `ReturnValue: FloatProperty` |
| `BroadcastProgressChanged` | (no params) |
| `BroadcastInputStarted` | (no params) |
| `BroadcastInputOngoing` | (no params) |
| `BroadcastInputCompleted` | (no params) |
| `BroadcastInputCancelled` | (no params) |
| `BindInputWithCallbacks` | `PlayerController: ObjectProperty of Sparta.SpartaPlayerController`, `Started: DelegateProperty`, `Ongoing: DelegateProperty`, `Complete: DelegateProperty`, `Cancelled: DelegateProperty`, `ProgressChanged: DelegateProperty` |
| `BindInput` | `PlayerController: ObjectProperty of Sparta.SpartaPlayerController` |

### 3.4 Enhanced Input subclasses: modifiers and triggers

| Class | Parent | Own properties |
|---|---|---|
| `USpartaInputMappingContext` | `UInputMappingContext` | none (size `0xB0`) |
| `USpartaEnhancedPlayerInput` | `UEnhancedPlayerInput` | none (size `0x8B0`) |
| `USpartaInputModifierAllowInputWithTags` | `UInputModifier` | `InputAllowingTag: FGameplayTagContainer` `[o: 28]` |
| `USpartaInputModifierBlockInputWithTags` | `UInputModifier` | `InputBlockingTag: FGameplayTagContainer` `[o: 28]` |
| `USpartaInputModifierBlockInputTagQuery` | `UInputModifier` | `BlockInputTagQuery: FGameplayTagQuery` `[o: 28]`, `BlendOutTime: float` `[o: 70]`, `BlendOutCurve: FRuntimeFloatCurve` `[o: 78]`, `DisableBlendOutTagQuery: FGameplayTagQuery` `[o: 100]` |
| `USpartaInputModifierControllerStickOnlyXAxis` | `UInputModifier` | `bOnlyPositive: bool` `[o: 28]` |
| `USpartaInputModifierQuantizeMagnitude` | `UInputModifier` | `Range: FVector2D` `[o: 28]`, `Intervals: int32` `[o: 38]` |
| `USpartaInputModifierResponseCurveMagnitude` | `UInputModifier` | `Curve: UCurveFloat*` `[o: 28]` |
| `USpartaInputModifierScaleAction` | `UInputModifier` | `Action: UInputAction*` `[o: 28]`, `Scale: FVector` `[o: 30]` |
| `USpartaInputTriggerConditional` | `UInputTrigger` | `ActiveTrigger: UInputTrigger*` `[o: 50]` |
| `USpartaInputTriggerHoldThenPulse` | `UInputTriggerTimedBase` | `HoldTimeThreshold: float` `[o: 58]`, `Interval: float` `[o: 5C]`, `TriggerLimit: int32` `[o: 60]` |
| `USpartaInputTriggerMouseAccumulatedThreshold` | `UInputTrigger` | `TriggerThreshold: float` `[o: 50]`, `RetriggerDelay: float` `[o: 54]`, `InputDecay: float` `[o: 58]` |
| `USpartaInputTriggerTimeout` | `UInputTriggerTimedBase` | `Timeout: float` `[o: 58]` |

Only one of the triggers/modifiers has functions:
`USpartaInputTriggerConditional.GetDesiredTrigger(PlayerInput: ObjectProperty of
EnhancedInput.EnhancedPlayerInput, ModifiedValue: StructProperty of EnhancedInput.InputActionValue,
DeltaTime: FloatProperty, ReturnValue: ObjectProperty of EnhancedInput.InputTrigger)`, and
`GetActiveTrigger(ReturnValue: ObjectProperty of EnhancedInput.InputTrigger)`.

**Input blocking during animation** is done with notifies, not with the modifiers:

| Class | Parent | Own properties |
|---|---|---|
| `UAnimNotifyState_InputBlock` | `UCSAnimNotifyState` | `Block: StructProperty of Sparta.AbilityInputTagBlock` `[o: 78]` |
| `UAnimNotifyState_InputQueue` | `UCSAnimNotifyState` | none (size `0x78`) |

### 3.5 FGameplayTag names used for ability input -- `UNKNOWN`

I could not prove a single **tag string** from this dump. Tried, in order:

1. `SELECT type, COUNT(*) FROM obj WHERE type LIKE '%GameplayTag%' GROUP BY type` -- the dump has no
   `GameplayTag` object type (only `GameplayTagsList` / `RestrictedGameplayTagsList` *classes* and the
   `BP_DE_AddGameplayTag_C` style notifies).
2. `SELECT path FROM obj WHERE path LIKE '%InputTag%'` (72 rows) -- every hit is a **property name**
   (`SpartaInputConfig:InputTag_Root`, `SpartaAbilitySet_GameplayAbility:InputTag`,
   `CachedAbilityInput:InputTag`, ...), never a tag value.
3. `SELECT ... WHERE type LIKE '%GameplayTag%' AND path LIKE '/Game/%'` -- only Blueprint notify
   *objects* (`ANS_AddGameplayTags_C`, `CSAnimNotify_SendGameplayTagEvent`), whose tag payload is a
   default property value and therefore not in the dump.

So all that can be pointed at is the *shape*: 8 named `FGameplayTag` properties on
`USpartaInputConfig` (`InputTag_Root`, `InputTag_Move_*`, `InputTag_Look_*`) and the anonymous
`InputTag` fields above. A co-op engineer must read the tag strings at runtime
(`UE4SS` `GetProperty`/`FGameplayTag::RequestGameplayTag`) or from the `.ini` tag list -- the object
dump does not carry them.


Evidence -- input layer class rows:
```
[00007FF46F02D138] Class /Script/Sparta.AnimNotifyState_InputBlock [n: 47552] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF9FB38]
[00007FF46F02D338] Class /Script/Sparta.AnimNotifyState_InputQueue [n: 47560] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF9FB38]
[00007FF46F046B38] Class /Script/Sparta.SpartaEnhancedPlayerInput [n: 49167] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCE738]
[00007FF46F04BD38] Class /Script/Sparta.SpartaInputInteraction [n: 4976A] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F04BF38] Class /Script/Sparta.SpartaInputMappingContext [n: 497E1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCED38]
[00007FF46F04C138] Class /Script/Sparta.SpartaInputModifierQuantizeMagnitude [n: 497EF] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCEF38]
[00007FF46F04C338] Class /Script/Sparta.SpartaInputModifierResponseCurveMagnitude [n: 49802] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCEF38]
[00007FF46F04C538] Class /Script/Sparta.SpartaInputModifierScaleAction [n: 49818] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCEF38]
[00007FF46F04C738] Class /Script/Sparta.SpartaInputModifierBlockInputWithTags [n: 49828] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCEF38]
[00007FF46F04C938] Class /Script/Sparta.SpartaInputModifierBlockInputTagQuery [n: 4983C] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCEF38]
[00007FF46F04CB38] Class /Script/Sparta.SpartaInputModifierControllerStickOnlyXAxis [n: 49850] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCEF38]
[00007FF46F04CD38] Class /Script/Sparta.SpartaInputModifierAllowInputWithTags [n: 49867] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCEF38]
[00007FF46F04CF38] Class /Script/Sparta.SpartaInputTriggerTimeout [n: 4987B] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFD0938]
[00007FF46F04D138] Class /Script/Sparta.SpartaInputTriggerHoldThenPulse [n: 49889] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFD0938]
[00007FF46F04D338] Class /Script/Sparta.SpartaInputTriggerMouseAccumulatedThreshold [n: 4989A] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFD0738]
[00007FF46F04D538] Class /Script/Sparta.SpartaInputTriggerConditional [n: 498B1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFD0738]
[00007FF46F066938] Class /Script/Sparta.SpartaInputComponent [n: 4BC7A] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCD738]
[00007FF46F066B38] Class /Script/Sparta.SpartaInputConfig [n: 4BCB1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[00007FF46F486338] ScriptStruct /Script/Sparta.CachedAbilityInput [n: 135C9] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 0000000000000000]
[00007FF46F4863F8] ScriptStruct /Script/Sparta.InputQueue [n: 135DB] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 0000000000000000]
[00007FF46F486578] ScriptStruct /Script/Sparta.AbilityInputTagBlock [n: 135EA] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 0000000000000000]
[00007FF46F48E2B8] ScriptStruct /Script/Sparta.SpartaInputAction [n: 14940] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 0000000000000000]
```

Evidence -- input layer key properties:
```
[000001F4ACD57080] StructProperty /Script/Sparta.AnimNotifyState_InputBlock:Block [o: 78] [n: 527657] [c: 00007FF721DF9890] [owr: 00007FF46F02D138] [ss: 00007FF46F486578]
[000001F4ACD6C950] NameProperty /Script/Sparta.SpartaInputInteraction:InputID [o: 30] [n: 511C1D] [c: 00007FF721DE90B0] [owr: 00007FF46F04BD38]
[000001F4ACDB3B80] DelegateProperty /Script/Sparta.SpartaInputInteraction:OnInputStarted [o: 40] [n: 588388] [c: 00007FF721DE8E70] [owr: 00007FF46F04BD38] [df: 00007FF46F3D3138]
[000001F4ACDB3D80] DelegateProperty /Script/Sparta.SpartaInputInteraction:OnProgressChanged [o: 80] [n: 588364] [c: 00007FF721DE8E70] [owr: 00007FF46F04BD38] [df: 00007FF46F3D3138]
[000001F4ACE46500] MulticastInlineDelegateProperty /Script/Sparta.SpartaInputComponent:OnInputQueued [o: 178] [n: 58A6EE] [c: 00007FF721DE9010] [owr: 00007FF46F066938] [df: 00007FF46F3CF560]
[000001F4ACE46680] StructProperty /Script/Sparta.SpartaInputComponent:InputQueue [o: 198] [n: 135DB] [c: 00007FF721DF9890] [owr: 00007FF46F066938] [ss: 00007FF46F4863F8]
[000001F4ACE46780] ArrayProperty /Script/Sparta.SpartaInputComponent:ActiveBlocks [o: 1B0] [n: 58A6DE] [c: 00007FF721DE8910] [owr: 00007FF46F066938] [ai: 000001F4ACE46700]
[000001F4ACE46700] StructProperty ActiveBlocks./Script/Sparta.SpartaInputComponent:ActiveBlocks [o: 0] [n: 58A6DE] [c: 00007FF721DF9890] [owr: 000001F4ACE46780] [ss: 00007FF46F486578]
[000001F4ACE47D00] ArrayProperty /Script/Sparta.SpartaInputConfig:NativeInputActions [o: 30] [n: 58A769] [c: 00007FF721DE8910] [owr: 00007FF46F066B38] [ai: 000001F4ACE47C80]
[000001F4ACE47C80] StructProperty NativeInputActions./Script/Sparta.SpartaInputConfig:NativeInputActions [o: 0] [n: 58A769] [c: 00007FF721DF9890] [owr: 000001F4ACE47D00] [ss: 00007FF46F48E2B8]
[000001F4ACE47E00] ArrayProperty /Script/Sparta.SpartaInputConfig:AbilityInputActions [o: 40] [n: 58A75E] [c: 00007FF721DE8910] [owr: 00007FF46F066B38] [ai: 000001F4ACE47D80]
[000001F4ACE47D80] StructProperty AbilityInputActions./Script/Sparta.SpartaInputConfig:AbilityInputActions [o: 0] [n: 58A75E] [c: 00007FF721DF9890] [owr: 000001F4ACE47E00] [ss: 00007FF46F48E2B8]
[000001F4ACE47E80] StructProperty /Script/Sparta.SpartaInputConfig:InputTag_Root [o: 50] [n: 58A756] [c: 00007FF721DF9890] [owr: 00007FF46F066B38] [ss: 00007FF46F3890A8]
[000001F4ACE47F00] StructProperty /Script/Sparta.SpartaInputConfig:InputTag_Move_Forward [o: 58] [n: 58A74A] [c: 00007FF721DF9890] [owr: 00007FF46F066B38] [ss: 00007FF46F3890A8]
[000001F4ACE48280] StructProperty /Script/Sparta.SpartaInputConfig:UnblockableInputTags [o: 90] [n: 58A6FD] [c: 00007FF721DF9890] [owr: 00007FF46F066B38] [ss: 00007FF46F388FE8]
```

Evidence -- input structs and the delegates `OnInputQueued`/`SpartaInputInteractionEvent`:
```
[000001F4A6CB1280] StructProperty /Script/Sparta.OnInputQueued__DelegateSignature:InputTag [o: 0] [n: 524FA6] [c: 00007FF721DF9890] [owr: 00007FF46F3CF560] [ss: 00007FF46F3890A8]
[000001F4A6CB1300] BoolProperty /Script/Sparta.OnInputQueued__DelegateSignature:bPressed [o: 8] [n: 52557A] [c: 00007FF721DE8D80] [owr: 00007FF46F3CF560]
[000001F4A8619FC0] DoubleProperty /Script/Sparta.CachedAbilityInput:CacheTime [o: 0] [n: 55C638] [c: 00007FF721DE93D0] [owr: 00007FF46F486338]
[000001F4A864D900] StructProperty /Script/Sparta.CachedAbilityInput:InputTag [o: 8] [n: 524FA6] [c: 00007FF721DF9890] [owr: 00007FF46F486338] [ss: 00007FF46F3890A8]
[000001F4A864D980] BoolProperty /Script/Sparta.CachedAbilityInput:bPressed [o: 10] [n: 52557A] [c: 00007FF721DE8D80] [owr: 00007FF46F486338]
[000001F4A864D880] ArrayProperty /Script/Sparta.InputQueue:InputQueue [o: 0] [n: 135DB] [c: 00007FF721DE8910] [owr: 00007FF46F4863F8] [ai: 000001F4A864D800]
[000001F4A864D800] StructProperty InputQueue./Script/Sparta.InputQueue:InputQueue [o: 0] [n: 135DB] [c: 00007FF721DF9890] [owr: 000001F4A864D880] [ss: 00007FF46F486338]
[000001F4A864F780] EnumProperty /Script/Sparta.AbilityInputTagBlock:BlockBehavior [o: 0] [n: 55C646] [c: 00007FF721DE7380] [owr: 00007FF46F486578] [em: 00007FF46F3D81C8]
[000001F4A864D700] StructProperty /Script/Sparta.AbilityInputTagBlock:InputTag [o: 8] [n: 524FA6] [c: 00007FF721DF9890] [owr: 00007FF46F486578] [ss: 00007FF46F388FE8]
[000001F4A887D280] ObjectProperty /Script/Sparta.SpartaInputAction:InputAction [o: 0] [n: 3FF9B] [c: 00007FF721DE9420] [owr: 00007FF46F48E2B8] [pc: 00007FF46EFCE938]
[000001F4A887D300] StructProperty /Script/Sparta.SpartaInputAction:InputTag [o: 8] [n: 524FA6] [c: 00007FF721DF9890] [owr: 00007FF46F48E2B8] [ss: 00007FF46F3890A8]
```

---

## 4. Ability-task / montage-interrupt / hit-window plumbing

### 4.1 Sparta ability tasks

| Class | Parent | Properties | Functions |
|---|---|---|---|
| `/Script/Sparta.AbilityTask_FindSurfacePoints` | `/Script/GameplayAbilities.AbilityTask` | `OnSurfacePointsFound: MulticastInlineDelegateProperty` (sig `(SurfacePoints: ArrayProperty)`) | `FindSurfacePoints` (below) |
| `/Script/Sparta.AbilityTask_SpartaWaitAttChange` | `/Script/GameplayAbilities.AbilityTask` | `OnChange` delegate (sig `SpartaWaitAttributeChangeDelegate(float NewValue, float OldValue)` -- HDR), `ExternalOwner: ObjectProperty of GameplayAbilities.AbilitySystemComponent` `[o: E8]` | `SpartaWaitAttributeChangeDelegate`, `SpartaWaitForAttributeChange` |

| Function | Parameters (name: type) |
|---|---|
| `AbilityTask_FindSurfacePoints.FindSurfacePoints` | `OwningAbility: ObjectProperty of GameplayAbilities.GameplayAbility`, `NumYawTraces: IntProperty`, `YawAngleRange: StructProperty of CoreUObject.Vector2D`, `NumPitchTraces: IntProperty`, `PitchAngleRange: StructProperty of CoreUObject.Vector2D`, `MinDist: FloatProperty`, `MaxDist: FloatProperty`, `ObjectTypes: ArrayProperty`, `TargetLocations: ArrayProperty`, `CameraRotation: StructProperty of CoreUObject.Rotator`, `AngleScoreCurve: StructProperty of Engine.RuntimeFloatCurve`, `ReturnValue: ObjectProperty of Sparta.AbilityTask_FindSurfacePoints` |
| `AbilityTask_SpartaWaitAttChange.SpartaWaitForAttributeChange` | `OwningAbility: ObjectProperty of GameplayAbilities.GameplayAbility`, `Attribute: StructProperty of GameplayAbilities.GameplayAttribute`, `WithSrcTag: StructProperty of GameplayTags.GameplayTag`, `WithoutSrcTag: StructProperty of GameplayTags.GameplayTag`, `TriggerOnce: BoolProperty`, `OptionalExternalOwner: ObjectProperty of Engine.Actor`, `ReturnValue: ObjectProperty of Sparta.AbilityTask_SpartaWaitAttChange` |

`AbilityTask_SpartaWaitAttChange` is the ready-made way to observe **any** attribute (e.g.
`PlayerAttributeSet:HealCurrentCharges` or `SpartaHealthSet:Health`) from an ability -- a co-op layer can
use the same mechanism instead of polling.

The player Blueprint abilities also use the **CSAbilityTasks** family; the delegate shapes below are the
ones they bind (relevant because the notify callback is how an ability learns about hit windows):

| Delegate signature | Parameters |
|---|---|
| `CSAbilityTasks.PlayMontageAndWaitWithNotifiesDelegate__DelegateSignature` | `NotifyName: NameProperty [0x0]`, `NotifyObject: ObjectProperty [0x8]`, `bIsNotifyBegin: BoolProperty [0x10]` |
| `CSAbilityTasks.PlayMontageAndWaitForEventDelegate__DelegateSignature` | `EventTag: StructProperty [0x0]`, `EventData: StructProperty [0x8]` |

### 4.2 `USpartaMontageInterrupterComponent` -- the "you may be interrupted now" window

Parent `UActorComponent`.

| Property | Type | Offset |
|---|---|---|
| `OnMontageInterrupted` | `MulticastInlineDelegateProperty` (sig `SpartaOnMontageInterrupted__DelegateSignature`) | `[o: B8]` |
| `bDebugForceEnabled` | `BoolProperty` | `[o: C8]` |

| Function | Parameters (name: type) |
|---|---|
| `OnInterruptWindowOpened` | `Montage: ObjectProperty of Engine.AnimMontage` |
| `OnInterruptWindowClosed` | `Montage: ObjectProperty of Engine.AnimMontage`, `bInterrupted: BoolProperty` |
| `InterruptAll` | `Reason: StructProperty of GameplayTags.GameplayTag` |

Header also declares the delegate handler `void SpartaOnMontageInterrupted(class UAnimMontage* Montage,
FGameplayTag InterruptTag, ESpartaMontageInterruptCause Cause);` (HDR) -- same three fields the
delegate signature carries.

`USpartaMontageInterrupterNotifyState` (parent `UCSAnimNotifyState`) -- the notify that opens that window:

| Property | Type | Offset |
|---|---|---|
| `InterruptEventTags` | `GameplayTagContainer` | `[o: 78]` |
| `InterruptOnTagsAdded` | `GameplayTagContainer` | `[o: 98]` |
| `InterruptOnTagsRemoved` | `GameplayTagContainer` | `[o: B8]` |
| `BeginConditionCheck` | `EnumProperty of Sparta.ESpartaMontageInterruptBeginCheck` | `[o: D8]` |
| `BlendOutMode` | `EnumProperty of Sparta.ESpartaMontageInterruptBlendMode` | `[o: D9]` |
| `BlendOutTimeOverride` | `FloatProperty` | `[o: DC]` |
| `BlendOption` | `EnumProperty of Engine.EAlphaBlendOption` | `[o: E0]` |
| `CustomBlendCurve` | `ObjectProperty of Engine.CurveFloat` | `[o: E8]` |
| `bAutoAddComponent` | `BoolProperty` | `[o: F0]` |

### 4.3 The notify classes that mark hit windows and character/weapon events

| Class | Parent | Own properties (dump offsets) |
|---|---|---|
| `USpartaAnimNotifyState_HitCheck` | `UCSAnimNotifyState` | `DamagePayload: ObjectProperty of Sparta.SpartaHitPayload` `[o: 78]`, `WeaponSlot: GameplayTag` `[o: 80]`, `TraceSetup: ESpartaHitTraceSetup` `[o: 88]`, `TraceSource: ESpartaHitTraceActorSource` `[o: 89]`, `TraceType: ESpartaHitTraceType` `[o: 8A]`, `ComponentReferences: ArrayProperty` `[o: 90]`, `CollisionShapes: ArrayProperty` `[o: A0]`, `PhysicsBodyBones: ArrayProperty` `[o: B0]`, `TraceSettings: ESpartaHitTraceSettings` `[o: C0]`, `TeamRelationship: IntProperty` `[o: C4]`, `TraceProfile: NameProperty` `[o: C8]`, `UseDeferredHitDispatch: BoolProperty` `[o: D0]`, `bAllowMultipleHitsOnSameActor: BoolProperty` `[o: D1]`, `MultipleHitOnSameActorDelay: FloatProperty` `[o: D4]`, `EnvironmentalCollisionChannels: ArrayProperty` `[o: D8]`, `EnvironmentalHitFrequencyLimit: FloatProperty` `[o: E8]`, `bUseSharedHitList: BoolProperty` `[o: EC]`, `SharedHitListId: NameProperty` `[o: F0]`, `MotionSubstep: ESpartaHitTraceMotionSubstepLevel` `[o: F8]` -- functions `Replace`, `GetTraceProfileOptions` |
| `USpartaAnimNotify_HitCheck` | `UCSAnimNotify` | `DamagePayload` `[o: 80]`, `WeaponSlot` `[o: 88]`, `TraceSetup` `[o: 90]`, `TraceSource` `[o: 91]`, `TraceType` `[o: 92]`, `ComponentReferences` `[o: 98]`, `CollisionShapes` `[o: A8]`, `PhysicsBodyBones` `[o: B8]`, `TraceSettings` `[o: C8]`, `TeamRelationship` `[o: CC]`, `TraceProfile` `[o: D0]`, `UseDeferredHitDispatch` `[o: D8]` -- function `GetTraceProfileOptions` |
| `USpartaAnimNotify_CharacterEvent` | `UCSAnimNotify` | `Event_WW: ObjectProperty of WaveWeaver.AudioEvent` `[o: 80]`, `BodyLocation: GameplayTag` `[o: 88]`, `bFollow: bool` `[o: 90]`, `bIsInterruptable: bool` `[o: 91]`, `SoundModifiers: StructProperty` `[o: 94]` |
| `USpartaAnimNotify_WeaponEvent` | `UCSAnimNotify` | `Event_WW: ObjectProperty of WaveWeaver.AudioEvent` `[o: 80]`, `Sound: ObjectProperty of Engine.SoundBase` `[o: 88]`, `bFollow: bool` `[o: 90]`, `WeaponSlot: GameplayTag` `[o: 94]`, `SoundModifiers` `[o: 9C]` |

`USpartaAnimNotify_CharacterEvent` / `USpartaAnimNotify_WeaponEvent` carry **no** gameplay payload --
only audio (`Event_WW`, `Sound`) and a `WeaponSlot` tag. GUESS: they are cosmetically named "Event"
notifies for the weapon/character audio systems, not gameplay hit windows; the *hit* windows are the
two `*_HitCheck` classes and gameplay events go through `CSAnimNotify_SendGameplayTagEvent`
(1391 instances, `/Script/CSAnimationLibrary`) which is outside this report's area.

This matters for a sync layer: an attack's damage moment is a **notify window inside a montage**, so
mirroring "which montage, which section, when started" reproduces the hit windows deterministically --
there is no separate "attack hit" RPC to replicate.

### 4.4 `USpartaHitTrace` -- the object that actually performs the trace

`BP_SpartaHitCheckWeapon` on `GA_AttackBase_Melee` is an `ObjectProperty of /Script/Sparta.SpartaHitTrace`;
its members (dump offsets) are the runtime hit-trace state:

| Property | Type | Offset |
|---|---|---|
| `TraceHandle` | `StructProperty of Sparta.SpartaHitTraceHandle` | `[o: 28]` |
| `TeamRelationship` | `IntProperty` | `[o: 30]` |
| `TraceCollisionProfile` | `NameProperty` | `[o: 34]` |
| `UseDeferredHitDispatch` | `BoolProperty` | `[o: 3C]` |
| `bAllowMultipleHitsOnSameActor` | `BoolProperty` | `[o: 3D]` |
| `MultipleHitOnSameActorDelay` | `FloatProperty` | `[o: 40]` |
| `EnvironmentalCollisionChannels` | `ArrayProperty` | `[o: 48]` |
| `EnvironmentalHitFrequencyLimit` | `FloatProperty` | `[o: 58]` |
| `bUseSharedHitList` | `BoolProperty` | `[o: 5C]` |
| `SharedHitListId` | `NameProperty` | `[o: 60]` |
| `HitList` | `MapProperty` | `[o: 78]` |
| `IgnoreList` | `ArrayProperty` | `[o: C8]` |
| `Source` | `ObjectProperty of Engine.Actor` | `[o: D8]` |
| `TraceComponent` | `ObjectProperty of Sparta.SpartaHitTraceComponent` | `[o: E0]` |
| `SourceCharacter` | `ObjectProperty of Sparta.SpartaCharacter` | `[o: E8]` |
| `TraceData` | `MapProperty` | `[o: F8]` |
| `PrimitiveComponents` | `ArrayProperty` | `[o: 148]` |
| `TraceShapes` | `ArrayProperty` | `[o: 158]` |


Evidence -- ability tasks, interrupter, hit-window notifies, hit trace:
```
[00007FF46F02C738] Class /Script/Sparta.AbilityTask_FindSurfacePoints [n: 474D1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF8ED38]
[00007FF46F02C938] Class /Script/Sparta.AbilityTask_SpartaWaitAttChange [n: 474EB] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF8ED38]
[00007FF46F03D938] Class /Script/Sparta.SpartaAnimNotifyState_HitCheck [n: 486C1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF9FB38]
[00007FF46F03DF38] Class /Script/Sparta.SpartaAnimNotify_CharacterEvent [n: 48700] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF9F938]
[00007FF46F03E338] Class /Script/Sparta.SpartaAnimNotify_HitCheck [n: 4871F] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF9F938]
[00007FF46F03E738] Class /Script/Sparta.SpartaAnimNotify_WeaponEvent [n: 4873E] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF9F938]
[00007FF46F049938] Class /Script/Sparta.SpartaHitTrace [n: 49506] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F050738] Class /Script/Sparta.SpartaMontageInterrupterComponent [n: 49B56] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F050938] Class /Script/Sparta.SpartaMontageInterrupterNotifyState [n: 49B6F] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF9FB38]
[00007FF46F3D1288] DelegateFunction /Script/Sparta.OnSurfacePointsFound__DelegateSignature [n: 5258E4] [c: 00007FF46EE56338] [or: 00007FF46F17CD68]
[000001F4A6CB4180] ArrayProperty /Script/Sparta.OnSurfacePointsFound__DelegateSignature:SurfacePoints [o: 0] [n: 5258F9] [c: 00007FF721DE8910] [owr: 00007FF46F3D1288] [ai: 000001F4A6CB4100]
[000001F4A6CB4100] StructProperty SurfacePoints./Script/Sparta.OnSurfacePointsFound__DelegateSignature:SurfacePoints [o: 0] [n: 5258F9] [c: 00007FF721DF9890] [owr: 000001F4A6CB4180] [ss: 00007FF46F2EBB48]
```

Evidence -- notifies / interrupter / task properties and delegate signatures:
```
[000001F4ACD94E00] ObjectProperty /Script/Sparta.SpartaAnimNotifyState_HitCheck:DamagePayload [o: 78] [n: 55CB97] [c: 00007FF721DE9420] [owr: 00007FF46F03D938] [pc: 00007FF46F049338]
[000001F4ACD64450] IntProperty /Script/Sparta.SpartaAnimNotifyState_HitCheck:TeamRelationship [o: C4] [n: 55CB62] [c: 00007FF721DE91F0] [owr: 00007FF46F03D938]
[000001F4ACD93780] EnumProperty /Script/Sparta.SpartaAnimNotifyState_HitCheck:MotionSubstep [o: F8] [n: 55CAF5] [c: 00007FF721DE7380] [owr: 00007FF46F03D938] [em: 00007FF46F3D9208]
[000001F4ACD94980] StructProperty /Script/Sparta.SpartaAnimNotify_CharacterEvent:BodyLocation [o: 88] [n: 586F6B] [c: 00007FF721DF9890] [owr: 00007FF46F03DF38] [ss: 00007FF46F3890A8]
[000001F4ACD96000] ObjectProperty /Script/Sparta.SpartaAnimNotify_HitCheck:DamagePayload [o: 80] [n: 55CB97] [c: 00007FF721DE9420] [owr: 00007FF46F03E338] [pc: 00007FF46F049338]
[000001F4ACD96080] StructProperty /Script/Sparta.SpartaAnimNotify_HitCheck:WeaponSlot [o: 88] [n: 4CF787] [c: 00007FF721DF9890] [owr: 00007FF46F03E338] [ss: 00007FF46F3890A8]
[000001F4ACD641B0] NameProperty /Script/Sparta.SpartaAnimNotify_HitCheck:TraceProfile [o: D0] [n: 55CB5B] [c: 00007FF721DE90B0] [owr: 00007FF46F03E338]
[000001F4ACD95E00] StructProperty /Script/Sparta.SpartaAnimNotify_WeaponEvent:WeaponSlot [o: 94] [n: 4CF787] [c: 00007FF721DF9890] [owr: 00007FF46F03E738] [ss: 00007FF46F3890A8]
[000001F4ACDB7900] MulticastInlineDelegateProperty /Script/Sparta.SpartaMontageInterrupterComponent:OnMontageInterrupted [o: B8] [n: 3B3BE] [c: 00007FF721DE9010] [owr: 00007FF46F050738] [df: 00007FF46F3D3AE0]
[000001F4ACDB7980] BoolProperty /Script/Sparta.SpartaMontageInterrupterComponent:bDebugForceEnabled [o: C8] [n: 58877D] [c: 00007FF721DE8D80] [owr: 00007FF46F050738]
[000001F4ACDB9480] StructProperty /Script/Sparta.SpartaMontageInterrupterNotifyState:InterruptEventTags [o: 78] [n: 5887C5] [c: 00007FF721DF9890] [owr: 00007FF46F050938] [ss: 00007FF46F388FE8]
[000001F4ACDB7400] StructProperty /Script/Sparta.SpartaMontageInterrupterNotifyState:InterruptOnTagsAdded [o: 98] [n: 5887BA] [c: 00007FF721DF9890] [owr: 00007FF46F050938] [ss: 00007FF46F388FE8]
[000001F4A6CB5B80] ObjectProperty /Script/Sparta.SpartaOnMontageInterrupted__DelegateSignature:Montage [o: 0] [n: 4E50E3] [c: 00007FF721DE9420] [owr: 00007FF46F3D3AE0] [pc: 00007FF46EF2E338]
[000001F4A6CB5B00] StructProperty /Script/Sparta.SpartaOnMontageInterrupted__DelegateSignature:InterruptTag [o: 8] [n: 525F58] [c: 00007FF721DF9890] [owr: 00007FF46F3D3AE0] [ss: 00007FF46F3890A8]
[000001F4A6CB5C80] EnumProperty /Script/Sparta.SpartaOnMontageInterrupted__DelegateSignature:Cause [o: 10] [n: 525EAB] [c: 00007FF721DE7380] [owr: 00007FF46F3D3AE0] [em: 00007FF46F3D3BC0]
```

---

## 5. The player Blueprint ability ladder

Every player ability is a Blueprint class. The chain from native C++ to a concrete attack is short and
identical for all of them:

```
/Script/GameplayAbilities.GameplayAbility
  | /Script/Sparta.SpartaGameplayAbility            (sec.1.1 -- cost, cooldown, tags, child abilities)
      | /Game/Sparta/Core/Ability/GA_SpartaAbility_C             (adds nothing but UberGraphFrame)
      | /Script/Sparta.SpartaAttackAbility           (sec.1.2 -- AbilityHitPayload)
          | /Game/Sparta/Core/Ability/GA_SpartaBase_C            (soft targeting + motion warping)
              | /Game/Sparta/Core/Ability/GA_PlayMontageBase_C   (**Montage** + play-rate + async task)
              |   | /Game/Sparta/Core/Ability/GA_AttackBase_C        (payload / victim / damage)
              |   |   | /Game/Sparta/Core/Ability/GA_AttackBase_Melee_C  (**hit-check notifies**, hit-stop)
              |   |       | .../Core/Player/Ability/GA_Player_Attack_Light_C
              |   |       | .../Core/Player/Ability/GA_Player_Attack_Heavy_C
              |   |       | .../Core/Player/Ability/GA_WeaponAbilityBase_C
              |   |       | .../Core/Player/Ability/Plunging/GA_Player_Attack_Plunging_C
              |   | /Game/Sparta/Core/Ability/GA_StrafeBase_C        (8-direction strafe montages)
              |   | .../Core/Player/Ability/GA_WeaponBuffBase_C, GA_SidearmBuffBase_C, GA_Sidearm_Draw/Stow_C
              | .../Core/Player/Ability/GA_Player_AttackSelectorBase_C  (combo list -> child abilities)
              | .../Core/Player/Ability/GA_CharacterAbilityBase_C   (activation query, timers)
                  | .../Core/Player/Ability/GA_ShellAbilityBase_C    (shell duration + severed reaction)
```

The most important single field for a sync layer is `GA_PlayMontageBase_C:Montage`
(`ObjectProperty of Engine.AnimMontage`, `[o: 5E0]`) -- every montage-playing ability (attacks,
interactions, traversals, hearths, deaths) reads its animation from there, and `MontageStartSection`,
`MontagePlayRate`, `SelectedAbilityMontage` (`[o: 608]`) and `MontageAsyncTask`
(`AbilityTask_PlayMontageAndWaitWithNotifies`, `[o: 610]`) control how it is played.

### 5.1 Base-ladder properties (the fields a sync layer reproduces)

| Class | Property | Type | Offset | Notes |
|---|---|---|---|---|
| `GA_SpartaBase_C` | `bUseSoftTargeting` | `BoolProperty` | `[o: 520]` | |
| | `SoftTargetingSettings` | `ClassProperty` | `[o: 528]` | |
| | `bHasSoftTarget`, `CurrentSoftTarget` | `BoolProperty`, `ObjectProperty of Sparta.SpartaAICharacter` | `[o: 530]`, `[o: 538]` | |
| | `MotionWarpingComponent` | `ObjectProperty of CSMotionWarpingExtensions.CSMotionWarpingComponent` | `[o: 540]` | |
| | `WT_DesiredRotation_NoTarget`, `WT_DesiredRotation_Target`, `WT_DesiredEndLocation`, `WT_FightTarget_Root` | `NameProperty` | `[o: 548]`..`[o: 5C8]` | motion-warp target names |
| | `bWarpTranslation`, `bWarpRotation`, `bContinuousWarpTargetsUpdate`, `WarpTranslationDistance` | bools + `Vector2D` | `[o: 560]`.. | |
| `GA_PlayMontageBase_C` | `Montage` | `ObjectProperty of Engine.AnimMontage` | `[o: 5E0]` | **the montage** |
| | `MontagePlayRate` | `DoubleProperty` | `[o: 5E8]` | |
| | `MontagePlayRateVariance`, `MontageStartTime` | `CSFloatMinMax` | `[o: 5F0]`, `[o: 5F8]` | |
| | `MontageStartSection` | `NameProperty` | `[o: 600]` | |
| | `SelectedAbilityMontage` | `ObjectProperty of Engine.AnimMontage` | `[o: 608]` | |
| | `MontageAsyncTask` | `ObjectProperty of CSAbilityTasks.AbilityTask_PlayMontageAndWaitWithNotifies` | `[o: 610]` | |
| | `StopMontageWhenAbilityEnds`, `DebugMontage`, `EndAbilityOnMontageEnd`, `ConstrainRootMotionToNavmesh` | `BoolProperty` | `[o: 618]`..`[o: 61B]` | |
| `GA_AttackBase_C` | `CurrentPayload` | `ObjectProperty of Sparta.SpartaHitPayload` | `[o: 628]` | |
| | `AttackVictim` | `ObjectProperty of Engine.Actor` | `[o: 630]` | |
| | `AttackHitResult` | `StructProperty of Engine.HitResult` | `[o: 638]` | |
| | `DamageCauser` | `ObjectProperty of Engine.Actor` | `[o: 740]` | |
| | `DamageDealt` | `FloatProperty` | `[o: 748]` | |
| | `CurrentDamageTags` | `GameplayTagContainer` | `[o: 750]` | |
| | `BloodVFX` | `ClassProperty` | `[o: 738]` | |
| `GA_AttackBase_Melee_C` | `ANS_HitCheckNotifies` | `ArrayProperty` | `[o: 778]` | collected hit-check notifies |
| | `BP_SpartaHitCheckWeapon` | `ObjectProperty of Sparta.SpartaHitTrace` | `[o: 788]` | the live trace object (sec.4.4) |
| | `bUseHitStop`, `HitStopDuration`, `HitStopTimeDilation`, `HitStopBlendIn/Out` | bool + `DoubleProperty` + `AlphaBlend` | `[o: 791]`, `[o: 858]`, `[o: 860]`, `[o: 7F0]`, `[o: 820]` | |
| | `IsHoldAttack`, `HoldAttackDamage` | `BoolProperty`, `Vector2D` | `[o: 885]`, `[o: 888]` | the "Hold" variants |
| | `AutoRipostePolicy`, `UseMeleeAssist`, `RootMotionFollowNavPath`, `ResetComboCountOnAbilityEnd` | `ByteProperty` / bools | `[o: 884]`, `[o: 869]`, `[o: 8BC]`, `[o: 790]` | |
| `GA_Player_AttackSelectorBase_C` | `ComboAttackList` | `ArrayProperty` | `[o: 5E0]` | ordered combo abilities |
| | `RunningAttackList` | `ArrayProperty` | `[o: 5F0]` | |
| | `BPC_ComboCounter` | `ObjectProperty of .../BPC_Player_ComboCounter.BPC_Player_ComboCounter_C` | `[o: 600]` | the combo index holder |
| | `GrantedAbilityHandles`, `AdditionalAttacks` | `ArrayProperty` | `[o: 608]`, `[o: 618]` | |
| | `PlungingAttackTags`, `PlungingTraceLength` | `GameplayTagContainer`, `FloatProperty` | `[o: 628]`, `[o: 648]` | |

**How an attack is triggered** (evidence: the class names plus sec.3): the input layer
(`USpartaInputComponent`) turns a button into a gameplay tag; the tag is bound to an ability through
`FSpartaAbilitySet_GameplayAbility{Ability, AbilityLevel, InputTag}` or
`SpartaAbilitySystemComponent::K2_GiveAbilityWithInputTag`; the bound *selector* ability
(`GA_Player_AttackSelector_Light/Heavy` + per-weapon child) grants the combo abilities and activates
the right one via `TryActivateChildAbility(ChildAbilityKey, bCancelThisAbility)`. The combo index lives
in `BPC_Player_ComboCounter`, not in the ability.


Evidence -- the Blueprint ability ladder (class rows):
```
[000001F4DC018E00] BlueprintGeneratedClass /Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C [n: 47CB2A] [c: 00007FF46EEC3D38] [or: 000001F4D31FA380] [sps: 00007FF46F03F938]
[000001F615243160] BlueprintGeneratedClass /Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C [n: 114284] [c: 00007FF46EEC3D38] [or: 000001F4D1C17C20] [sps: 00007FF46F03F738]
[000001F615242DD0] BlueprintGeneratedClass /Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C [n: 43200B] [c: 00007FF46EEC3D38] [or: 000001F4D1C18800] [sps: 000001F615243160]
[000001F4BBD2F8E0] BlueprintGeneratedClass /Game/Sparta/Core/Ability/GA_AttackBase.GA_AttackBase_C [n: 1326A1] [c: 00007FF46EEC3D38] [or: 000001F4D1C192A0] [sps: 000001F615242DD0]
[000001F4BBD2F550] BlueprintGeneratedClass /Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C [n: 33E1B7] [c: 00007FF46EEC3D38] [or: 000001F4D1C17130] [sps: 000001F4BBD2F8E0]
[000001F76F83B1A0] BlueprintGeneratedClass /Game/Sparta/Core/Ability/GA_StrafeBase.GA_StrafeBase_C [n: 3B46F7] [c: 00007FF46EEC3D38] [or: 000001F76F6A8D00] [sps: 000001F615242DD0]
```

Evidence -- ladder properties that matter for sync (`Montage`, hit-check list, combo list):
```
[000001F615670E80] BoolProperty /Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C:bUseSoftTargeting [o: 520] [n: 668F93] [c: 00007FF721DE8D80] [owr: 000001F615243160]
[000001F615670C80] ObjectProperty /Game/Sparta/Core/Ability/GA_SpartaBase.GA_SpartaBase_C:MotionWarpingComponent [o: 540] [n: 3D06F] [c: 00007FF721DE9420] [owr: 000001F615243160] [pc: 00007FF46EFC2D38]
[000001F615671980] ObjectProperty /Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C:Montage [o: 5E0] [n: 4E50E3] [c: 00007FF721DE9420] [owr: 000001F615242DD0] [pc: 00007FF46EF2E338]
[000001F61521A500] NameProperty /Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C:MontageStartSection [o: 600] [n: 622137] [c: 00007FF721DE90B0] [owr: 000001F615242DD0]
[000001F615671800] ObjectProperty /Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C:SelectedAbilityMontage [o: 608] [n: 61E351] [c: 00007FF721DE9420] [owr: 000001F615242DD0] [pc: 00007FF46EF2E338]
[000001F615671780] ObjectProperty /Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C:MontageAsyncTask [o: 610] [n: 66CB63] [c: 00007FF721DE9420] [owr: 000001F615242DD0] [pc: 00007FF46F003338]
[000001F615671600] BoolProperty /Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C:EndAbilityOnMontageEnd [o: 61A] [n: 66CACA] [c: 00007FF721DE8D80] [owr: 000001F615242DD0]
[000001F4C06BEF00] ArrayProperty /Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C:ANS_HitCheckNotifies [o: 778] [n: 65EACA] [c: 00007FF721DE8910] [owr: 000001F4BBD2F550] [ai: 000001F4C06BEE80]
[000001F4C06BEE80] ObjectProperty ANS_HitCheckNotifies./Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C:ANS_HitCheckNotifies [o: 0] [n: 65EACA] [c: 00007FF721DE9420] [owr: 000001F4C06BEF00] [pc: 00007FF46F03D938]
[000001F4C06BEE00] ObjectProperty /Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C:BP_SpartaHitCheckWeapon [o: 788] [n: 65EF80] [c: 00007FF721DE9420] [owr: 000001F4BBD2F550] [pc: 00007FF46F049938]
[000001F4C06BE780] BoolProperty /Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C:IsHoldAttack [o: 885] [n: 65EAD5] [c: 00007FF721DE8D80] [owr: 000001F4BBD2F550]
[000001F4BA76F580] ArrayProperty /Game/Sparta/Core/Player/Ability/GA_Player_AttackSelectorBase.GA_Player_AttackSelectorBase_C:ComboAttackList [o: 5E0] [n: 74A2D4] [c: 00007FF721DE8910] [owr: 000001F4C1684DE0] [ai: 000001F4BA76F500]
[000001F4BA76F500] ClassProperty ComboAttackList./Game/Sparta/Core/Player/Ability/GA_Player_AttackSelectorBase.GA_Player_AttackSelectorBase_C:ComboAttackList [o: 0] [n: 74A2D4] [c: 00007FF721DE8E20] [owr: 000001F4BA76F580] [mc: 000001F4BBD2F8E0]
[000001F4BA76F380] ObjectProperty /Game/Sparta/Core/Player/Ability/GA_Player_AttackSelectorBase.GA_Player_AttackSelectorBase_C:BPC_ComboCounter [o: 600] [n: 74A1D8] [c: 00007FF721DE9420] [owr: 000001F4C1684DE0] [pc: 000001F614CC4DE0]
```

---

## 6. Melee attack abilities -- light/heavy/charged attacks and combos

181 ability classes (190 classes in the folder, the rest are item definitions,
fragments, notifies and gameplay effects) under `/Game/Sparta/Core/Player/Ability/Weapons/`, one set per weapon family
(Axatana=Axe+Katanas, AxeDagger, BattleAxe, BlackNeedle, ClockworkScythe, Fists, HadernsSword,
HeavyHammer, MartyrsBlade). The naming is completely regular:

| Suffix pattern | Meaning (from the class name + parent) | Parent |
|---|---|---|
| `GA_Player_AttackSelector_Light_<Weapon>` / `_Heavy_<Weapon>` | per-weapon combo selector for the light / heavy input | `GA_Player_AttackSelector_Light_C` / `_Heavy_C` |
| `..._Selector_..._Hold` | selector variant used while the button is held (`IsHoldAttack`) | same |
| `GA_Player_Attack_<Weapon>_A1..A3` | light combo step 1..3 | `GA_Player_Attack_Light_C` (or a per-weapon `*_Light_Base`) |
| `GA_Player_Attack_<Weapon>_B1..B3` | heavy combo step 1..3 | `GA_Player_Attack_Heavy_C` (or `*_Heavy_Base`) |
| `..._A<n>_Hold` / `..._B<n>_Hold` | charged variant of that step | same as the step |
| `..._A_Finisher` / `..._B_Finisher` | finisher step | same as the step |
| `..._A<n>_Double`, `..._double` | double-swing variant (Axatana / AxeDagger) | same |
| `GA_Running_Attack_<Weapon>[_B]` | running attack, light `_A` / heavy `_B` | light / heavy |
| `GA_WeaponBuff_<Weapon>` | the weapon's infusion/buff ability | `GA_WeaponBuffBase_C` |
| `GA_<Weapon>_Handler` | per-weapon handler (Axatana only) | `GA_SpartaAbility_C` |
| `GA_GenessaTrainer_BlackNeedle_B1..B3` | tutorial/trainer copies of the heavy combo | `GA_AttackBase_Melee_C` |

Everything except the classes listed in sec.6.3 declares **no own property**: the montage, the combo
index and the hit payload are all inherited. So *one* implementation covers all of them: read
`Montage` + `MontageStartSection` + `PlayRate` on activation, replicate "ability X started at time T",
and the hit windows follow from the montage.

### 6.1 Enumeration (one row per class, full paths)

| Class | Parent | Own props |
|---|---|---|
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_AttackSelector_Heavy_Axatana_Axe.GA_Player_AttackSelector_Heavy_Axatana_Axe_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_AttackSelector_Heavy_Axatana_Axe_Hold.GA_Player_AttackSelector_Heavy_Axatana_Axe_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_Attack_Axatana_Axe_B1.GA_Player_Attack_Axatana_Axe_B1_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.GA_Player_Attack_Heavy_Ataxana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_Attack_Axatana_Axe_B1_Hold.GA_Player_Attack_Axatana_Axe_B1_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.GA_Player_Attack_Heavy_Ataxana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_Attack_Axatana_Axe_B2.GA_Player_Attack_Axatana_Axe_B2_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.GA_Player_Attack_Heavy_Ataxana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_Attack_Axatana_Axe_B2_Hold.GA_Player_Attack_Axatana_Axe_B2_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.GA_Player_Attack_Heavy_Ataxana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_Attack_Axatana_Axe_B3.GA_Player_Attack_Axatana_Axe_B3_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.GA_Player_Attack_Heavy_Ataxana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_Attack_Axatana_Axe_B3_Hold.GA_Player_Attack_Axatana_Axe_B3_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.GA_Player_Attack_Heavy_Ataxana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_Attack_Katanas_RunningAttack_Axe.GA_Player_Attack_Katanas_RunningAttack_Axe_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Axatana_Handler.GA_Axatana_Handler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | see 6.3 |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.GA_Player_Attack_Heavy_Ataxana_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | see 6.3 |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | see 6.3 |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_AttackSelector_Light_Katanas.GA_Player_AttackSelector_Light_Katanas_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_AttackSelector_Light_Katanas_Hold.GA_Player_AttackSelector_Light_Katanas_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_A1.GA_Player_Attack_Katanas_A1_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_A1_Hold.GA_Player_Attack_Katanas_A1_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_A1_double.GA_Player_Attack_Katanas_A1_double_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_A2.GA_Player_Attack_Katanas_A2_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_A2_Hold.GA_Player_Attack_Katanas_A2_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_A2_double.GA_Player_Attack_Katanas_A2_double_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_A3.GA_Player_Attack_Katanas_A3_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_A3_Hold.GA_Player_Attack_Katanas_A3_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_A3_double.GA_Player_Attack_Katanas_A3_double_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_RunningAttack.GA_Player_Attack_Katanas_RunningAttack_C` | `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_WeaponBuff_Katanas.GA_WeaponBuff_Katanas_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponBuffBase.GA_WeaponBuffBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Heavy_AxeDagger.GA_Player_AttackSelector_Heavy_AxeDagger_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Heavy_AxeDagger_Hold.GA_Player_AttackSelector_Heavy_AxeDagger_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Light_AxeDagger.GA_Player_AttackSelector_Light_AxeDagger_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Light_AxeDagger_Hold.GA_Player_AttackSelector_Light_AxeDagger_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_A1.GA_Player_Attack_AxeDagger_A1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_A1_Double.GA_Player_Attack_AxeDagger_A1_Double_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_A1_Hold.GA_Player_Attack_AxeDagger_A1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_A2.GA_Player_Attack_AxeDagger_A2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_A2_Double.GA_Player_Attack_AxeDagger_A2_Double_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_A2_Hold.GA_Player_Attack_AxeDagger_A2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_A3.GA_Player_Attack_AxeDagger_A3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_A3_Double.GA_Player_Attack_AxeDagger_A3_Double_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_B1.GA_Player_Attack_AxeDagger_B1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_B1_Hold.GA_Player_Attack_AxeDagger_B1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_B2.GA_Player_Attack_AxeDagger_B2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_B2_Hold.GA_Player_Attack_AxeDagger_B2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_Attack_AxeDagger_B3.GA_Player_Attack_AxeDagger_B3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Running_Attack_AxeDagger_A.GA_Running_Attack_AxeDagger_A_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Running_Attack_AxeDagger_B.GA_Running_Attack_AxeDagger_B_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_WeaponBuff_AxeDagger.GA_WeaponBuff_AxeDagger_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponBuffBase.GA_WeaponBuffBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Heavy_BattleAxe.GA_Player_AttackSelector_Heavy_BattleAxe_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Heavy_BattleAxe_Hold.GA_Player_AttackSelector_Heavy_BattleAxe_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Light_BattleAxe.GA_Player_AttackSelector_Light_BattleAxe_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Light_BattleAxe_Hold.GA_Player_AttackSelector_Light_BattleAxe_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_A1.GA_Player_Attack_BattleAxe_A1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_A1_Hold.GA_Player_Attack_BattleAxe_A1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_A2.GA_Player_Attack_BattleAxe_A2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_A2_Hold.GA_Player_Attack_BattleAxe_A2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_A3.GA_Player_Attack_BattleAxe_A3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_A3_Hold.GA_Player_Attack_BattleAxe_A3_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_A_Finisher.GA_Player_Attack_BattleAxe_A_Finisher_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_B1.GA_Player_Attack_BattleAxe_B1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_B1_Hold.GA_Player_Attack_BattleAxe_B1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_B2.GA_Player_Attack_BattleAxe_B2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_B2_Hold.GA_Player_Attack_BattleAxe_B2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_B3.GA_Player_Attack_BattleAxe_B3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_B3_Hold.GA_Player_Attack_BattleAxe_B3_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_B_Finisher.GA_Player_Attack_BattleAxe_B_Finisher_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Running_Attack_B_BattleAxe.GA_Running_Attack_B_BattleAxe_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Running_Attack_BattleAxe.GA_Running_Attack_BattleAxe_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_WeaponBuff_BattleAxe.GA_WeaponBuff_BattleAxe_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponBuffBase.GA_WeaponBuffBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Heavy_BlackNeedle.GA_Player_AttackSelector_Heavy_BlackNeedle_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Heavy_BlackNeedle_Hold.GA_Player_AttackSelector_Heavy_BlackNeedle_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Light_BlackNeedle.GA_Player_AttackSelector_Light_BlackNeedle_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Light_BlackNeedle_Hold.GA_Player_AttackSelector_Light_BlackNeedle_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_A1_Hold.GA_Player_Attack_BlackNeedle_A1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_A2_Hold.GA_Player_Attack_BlackNeedle_A2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_A3_Hold.GA_Player_Attack_BlackNeedle_A3_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_A_Finisher.GA_Player_Attack_BlackNeedle_A_Finisher_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_B1_Hold.GA_Player_Attack_BlackNeedle_B1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_B2_Hold.GA_Player_Attack_BlackNeedle_B2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_B3_Hold.GA_Player_Attack_BlackNeedle_B3_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_B_Finisher.GA_Player_Attack_BlackNeedle_B_Finisher_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_BlackNeedle_A1.GA_Player_BlackNeedle_A1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_BlackNeedle_A2.GA_Player_BlackNeedle_A2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_BlackNeedle_A3.GA_Player_BlackNeedle_A3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_BlackNeedle_B1.GA_Player_BlackNeedle_B1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_BlackNeedle_B2.GA_Player_BlackNeedle_B2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_BlackNeedle_B3.GA_Player_BlackNeedle_B3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Running_Attack_BlackNeedle.GA_Running_Attack_BlackNeedle_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Running_Attack_BlackNeedle_B.GA_Running_Attack_BlackNeedle_B_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_WeaponBuff_BlackNeedle_Activation.GA_WeaponBuff_BlackNeedle_Activation_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponBuffBase.GA_WeaponBuffBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GenessaTrainer/GA_GenessaTrainer_BlackNeedle_B1.GA_GenessaTrainer_BlackNeedle_B1_C` | `/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GenessaTrainer/GA_GenessaTrainer_BlackNeedle_B2.GA_GenessaTrainer_BlackNeedle_B2_C` | `/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GenessaTrainer/GA_GenessaTrainer_BlackNeedle_B3.GA_GenessaTrainer_BlackNeedle_B3_C` | `/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Heavy_ClockworkScythe.GA_Player_AttackSelector_Heavy_ClockworkScythe_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Heavy_ClockworkScythe_Hold.GA_Player_AttackSelector_Heavy_ClockworkScythe_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Light_ClockworkScythe.GA_Player_AttackSelector_Light_ClockworkScythe_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Light_ClockworkScythe_Hold.GA_Player_AttackSelector_Light_ClockworkScythe_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_A1.GA_Player_Attack_ClockworkScythe_A1_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_A1_Hold.GA_Player_Attack_ClockworkScythe_A1_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_A2.GA_Player_Attack_ClockworkScythe_A2_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_A2_Hold.GA_Player_Attack_ClockworkScythe_A2_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_A3.GA_Player_Attack_ClockworkScythe_A3_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_A3_Finisher.GA_Player_Attack_ClockworkScythe_A3_Finisher_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_A3_Hold.GA_Player_Attack_ClockworkScythe_A3_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_B1.GA_Player_Attack_ClockworkScythe_B1_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_B1_Hold.GA_Player_Attack_ClockworkScythe_B1_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_B2.GA_Player_Attack_ClockworkScythe_B2_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_B2_Hold.GA_Player_Attack_ClockworkScythe_B2_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_B3.GA_Player_Attack_ClockworkScythe_B3_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_B3_Finisher.GA_Player_Attack_ClockworkScythe_B3_Finisher_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_B3_Hold.GA_Player_Attack_ClockworkScythe_B3_Hold_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Grinder.GA_Player_Attack_ClockworkScythe_Grinder_C` | `/Game/Sparta/Core/Ability/GA_AttackBase_Melee.GA_AttackBase_Melee_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | see 6.3 |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | see 6.3 |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Running_Attack_ClockworkScythe.GA_Running_Attack_ClockworkScythe_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Running_Attack_ClockworkScythe_B.GA_Running_Attack_ClockworkScythe_B_C` | `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_WeaponBuff_ClockworkScythe.GA_WeaponBuff_ClockworkScythe_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponBuffBase.GA_WeaponBuffBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Fists/GA_Player_Attack_Fists_A1.GA_Player_Attack_Fists_A1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Fists/GA_Player_Attack_Fists_A1_SmertMemory.GA_Player_Attack_Fists_A1_SmertMemory_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Fists/GA_Player_Attack_Fists_A2.GA_Player_Attack_Fists_A2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Fists/GA_Player_Attack_Fists_A2_SmertMemory.GA_Player_Attack_Fists_A2_SmertMemory_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Fists/GA_Player_Attack_Fists_B1.GA_Player_Attack_Fists_B1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/Fists/GA_Player_Attack_Fists_B2.GA_Player_Attack_Fists_B2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Heavy_HadernsSword.GA_Player_AttackSelector_Heavy_HadernsSword_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Heavy_HadernsSword_Hold.GA_Player_AttackSelector_Heavy_HadernsSword_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Light_HadernsSword.GA_Player_AttackSelector_Light_HadernsSword_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Light_HadernsSword_Hold.GA_Player_AttackSelector_Light_HadernsSword_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_Attack_HadernSword_A1_Hold.GA_Player_Attack_HadernSword_A1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_Attack_HadernSword_A2_Hold.GA_Player_Attack_HadernSword_A2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_Attack_HadernSword_A3_Hold.GA_Player_Attack_HadernSword_A3_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_Attack_HadernsSword_B1_Hold.GA_Player_Attack_HadernsSword_B1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_Attack_HadernsSword_B2_Hold.GA_Player_Attack_HadernsSword_B2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_Attack_HadernsSword_B3_Hold.GA_Player_Attack_HadernsSword_B3_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_A1.GA_Player_HadernsSword_A1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_A2.GA_Player_HadernsSword_A2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_A3.GA_Player_HadernsSword_A3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_A_Finisher.GA_Player_HadernsSword_A_Finisher_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_B1.GA_Player_HadernsSword_B1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_B2.GA_Player_HadernsSword_B2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_B3.GA_Player_HadernsSword_B3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_B_Finisher.GA_Player_HadernsSword_B_Finisher_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_RunningAttack.GA_Player_HadernsSword_RunningAttack_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_RunningAttack_B.GA_Player_HadernsSword_RunningAttack_B_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_WeaponBuff_HadernsSword.GA_WeaponBuff_HadernsSword_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponBuffBase.GA_WeaponBuffBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Heavy_HeavyHammer.GA_Player_AttackSelector_Heavy_HeavyHammer_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Heavy_HeavyHammer_Hold.GA_Player_AttackSelector_Heavy_HeavyHammer_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Light_HeavyHammer.GA_Player_AttackSelector_Light_HeavyHammer_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Light_HeavyHammer_Hold.GA_Player_AttackSelector_Light_HeavyHammer_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_A1.GA_Player_Attack_HeavyHammer_A1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_A1_Hold.GA_Player_Attack_HeavyHammer_A1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_A2.GA_Player_Attack_HeavyHammer_A2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_A2_Hold.GA_Player_Attack_HeavyHammer_A2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_A3.GA_Player_Attack_HeavyHammer_A3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_A3_Hold.GA_Player_Attack_HeavyHammer_A3_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_A_Finisher.GA_Player_Attack_HeavyHammer_A_Finisher_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_B1.GA_Player_Attack_HeavyHammer_B1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_B1_Hold.GA_Player_Attack_HeavyHammer_B1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_B2.GA_Player_Attack_HeavyHammer_B2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_B2_Hold.GA_Player_Attack_HeavyHammer_B2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_B3.GA_Player_Attack_HeavyHammer_B3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_B3_Hold.GA_Player_Attack_HeavyHammer_B3_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_B_Finisher.GA_Player_Attack_HeavyHammer_B_Finisher_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Running_Attack_HeavyHammer.GA_Running_Attack_HeavyHammer_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Running_Attack_HeavyHammer_B.GA_Running_Attack_HeavyHammer_B_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_WeaponBuff_HeavyHammer.GA_WeaponBuff_HeavyHammer_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponBuffBase.GA_WeaponBuffBase_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Heavy_MartyrsBlade.GA_Player_AttackSelector_Heavy_MartyrsBlade_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Heavy_MartyrsBlade_Hold.GA_Player_AttackSelector_Heavy_MartyrsBlade_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Heavy.GA_Player_AttackSelector_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Light_MartyrsBlade.GA_Player_AttackSelector_Light_MartyrsBlade_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Light_MartyrsBlade_Hold.GA_Player_AttackSelector_Light_MartyrsBlade_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_AttackSelector_Light.GA_Player_AttackSelector_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_A1.GA_Player_Attack_MartyrsBlade_A1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_A1_Hold.GA_Player_Attack_MartyrsBlade_A1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_A2.GA_Player_Attack_MartyrsBlade_A2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_A2_Hold.GA_Player_Attack_MartyrsBlade_A2_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_A3.GA_Player_Attack_MartyrsBlade_A3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_A_Finisher.GA_Player_Attack_MartyrsBlade_A_Finisher_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_B1.GA_Player_Attack_MartyrsBlade_B1_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_B1_Hold.GA_Player_Attack_MartyrsBlade_B1_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_B2.GA_Player_Attack_MartyrsBlade_B2_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_B3.GA_Player_Attack_MartyrsBlade_B3_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_B3_Hold.GA_Player_Attack_MartyrsBlade_B3_Hold_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_B_Finisher.GA_Player_Attack_MartyrsBlade_B_Finisher_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Running_Attack_MartyrsBlade.GA_Running_Attack_MartyrsBlade_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Light.GA_Player_Attack_Light_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Running_Attack_MartyrsBlade_B.GA_Running_Attack_MartyrsBlade_B_C` | `/Game/Sparta/Core/Player/Ability/GA_Player_Attack_Heavy.GA_Player_Attack_Heavy_C` | none |
| `/Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_WeaponBuff_MartyrsBlade.GA_WeaponBuff_MartyrsBlade_C` | `/Game/Sparta/Core/Player/Ability/GA_WeaponBuffBase.GA_WeaponBuffBase_C` | none |

### 6.2 The only attack classes that carry extra state

| Class | Property | Type | Offset |
|---|---|---|---|
| `GA_Player_Attack_Light_Axatana_C` | `CommittedFromEvent` | `BoolProperty` | `0x8E8` |
| | `EventData` | `StructProperty of GameplayAbilities.GameplayEventData` | `0x8F0` |
| | `Type` | `ByteProperty` (axe<->katanas transform selector) | `0x9A0` |
| | `TransformationMontage` | `ObjectProperty of Engine.AnimMontage` | `0x9A8` |
| | `TransformationPlayRate` | `FloatProperty` | `0x9B0` |
| | `AttackMontageSectionName` | `NameProperty` | `0x9B4` |
| `GA_Player_Attack_Heavy_Ataxana_C` | `CommittedFromEvent` | `BoolProperty` | `0x8F0` |
| | `EventData` | `GameplayEventData` | `0x8F8` |
| | `Type` | `ByteProperty` | `0x9A8` |
| | `TransformationMontage` | `ObjectProperty of Engine.AnimMontage` | `0x9B0` |
| | `TransformationPlayRate` | `FloatProperty` | `0x9B8` |
| | `AttackMontageSectionName` | `NameProperty` | `0x9BC` |
| `GA_Player_Attack_ClockworkScythe_Light_Base_C` | `KnockbackPower` | `FloatProperty` | `0x8E0` |
| `GA_Player_Attack_ClockworkScythe_Heavy_Base_C` | `KnockbackPower` | `FloatProperty` | `0x8E4` |
| `GA_Axatana_Handler_C` | `WeaponsComponent` | `ObjectProperty of Sparta.SpartaWeaponsComponent` | `0x518` |

The Axatana is the only weapon whose *attack* ability also plays a transformation montage
(`TransformationMontage` + `AttackMontageSectionName`): the weapon switches between axe and katanas
mid-combo, so a co-op layer must replicate `Type` with the attack, or the two clients animate different
weapons.

### 6.3 Montage assets that exist for these abilities

I did **not** resolve ability->montage *defaults*: the object dump prints class members but not their
default values, so the `Montage` property's assigned asset is not visible. What I can prove is that
montage assets whose name matches the ability exist, e.g.:

| Ability | Matching montage asset (name match only -- GUESS that it is the one assigned) |
|---|---|
| `GA_Player_HadernsSword_A1/B1` ... | `/Game/Sparta/Characters/Shells/_Shared/Animation/Attacks/HadernSword/A_Shared_HadernSword_A_Hold_Montage.A_Shared_HadernSword_A_Hold_Montage`, `.../A_Shared_HadernSword_B_Montage.A_Shared_HadernSword_B_Montage` |
| `GA_Player_Attack_HadernsSword_A1..` | `.../HadernSword/A_Shared_HadernSword_AA_Montage.A_Shared_HadernSword_AA_Montage`, `.../AAA_Montage...` |
| `GA_Player_BlackNeedle_A1.. / _A_Finisher` | `.../BlackNeedle/HoldV2/A_Shared_Attacks_BlackNeedle_A_CUT_Montage.A_Shared_Attacks_BlackNeedle_A_CUT_Montage`, `..._AAA_CUT_Finisher_Montage...` |
| `GA_Player_Attack_Fists_A1/B1` | `.../Fist/AM_Shared_Attacks_Fists_A.AM_Shared_Attacks_Fists_A`, `...AM_Shared_Attacks_Fists_B.AM_Shared_Attacks_Fists_B` |
| `GA_Player_Attack_ClockworkScythe_*` | the scythe montages live under `.../Shells/_Shared/Animation/Attacks/Scythe/` (e.g. `.../Scythe/Hold_V2/A_Shared_Attacks_Scythe_A_02_V2_Montage.A_Shared_Attacks_Scythe_A_02_V2_Montage`); the three assets whose path *does* contain "Clockwork" are locomotion only: `.../Shells/_Shared/Animation/Locomotion/ClockworkScythe/A_Shared_Scythe_Ability_Draw_Montage.A_Shared_Scythe_Ability_Draw_Montage`, `..._Stowe_Montage...`, `..._Ability_Idle_Montage...` |

`UNKNOWN` (per ability): the exact `Montage` asset. Tried: the object dump has no CDO default-value
records; the only way is to read the field at runtime (`UE4SS` property read on the CDO). The full
montage catalogue is another agent's report -- do not duplicate it here.



Evidence -- the five weapon-attack classes with own state, and one representative of each regular family:
```
[000001F4C33B68A0] FloatProperty /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C:KnockbackPower [o: 8E0] [n: 653227] [c: 00007FF721DE9380] [owr: 000001F4C1682A40]
[000001F8D832EAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.GA_Player_Attack_Heavy_Ataxana_C [n: 143E4C] [c: 00007FF46EEC3D38] [or: 000001F8D760F3D0] [sps: 000001F4C16814E0]
[000001F8D832B1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C [n: 37FDFF] [c: 00007FF46EEC3D38] [or: 000001F8D760EED0] [sps: 000001F4B1779520]
[000001F8D837AF80] ByteProperty /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C:Type [o: 9A0] [n: BA08] [c: 00007FF721DE8DD0] [owr: 000001F8D832B1A0]
[000001F8D837AF00] ObjectProperty /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C:TransformationMontage [o: 9A8] [n: 849280] [c: 00007FF721DE9420] [owr: 000001F8D832B1A0] [pc: 00007FF46EF2E338]
[000001F8D8362070] NameProperty /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C:AttackMontageSectionName [o: 9B4] [n: 8491C9] [c: 00007FF721DE90B0] [owr: 000001F8D832B1A0]
[000001F8D837BE00] ObjectProperty /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Axatana_Handler.GA_Axatana_Handler_C:WeaponsComponent [o: 518] [n: 4B21A] [c: 00007FF721DE9420] [owr: 000001F8D8329190] [pc: 00007FF46F063938]
```

Evidence -- the weapon-attack enumeration (raw class lines; the table in 6.1 is generated from the same rows):
```
[000001F4C1682A40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.GA_Player_Attack_ClockworkScythe_Light_Base_C [n: 65FB3] [c: 00007FF46EEC3D38] [or: 000001F4C4ED7D10] [sps: 000001F4B1779520]
[000001F4C4C05D00] GA_Player_Attack_ClockworkScythe_Light_Base_C /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Light_Base.Default__GA_Player_Attack_ClockworkScythe_Light_Base_C [n: 74A556] [c: 000001F4C1682A40] [or: 000001F4C4ED7D10]
[000001F4C1680DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.GA_Player_Attack_ClockworkScythe_Heavy_Base_C [n: 3D8369] [c: 00007FF46EEC3D38] [or: 000001F4C4ED7F90] [sps: 000001F4C16814E0]
[000001F4C4C07B90] GA_Player_Attack_ClockworkScythe_Heavy_Base_C /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Heavy_Base.Default__GA_Player_Attack_ClockworkScythe_Heavy_Base_C [n: 74A4B7] [c: 000001F4C1680DC0] [or: 000001F4C4ED7F90]
[000001F4C168A6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Running_Attack_ClockworkScythe.GA_Running_Attack_ClockworkScythe_C [n: 269FFA] [c: 00007FF46EEC3D38] [or: 000001F4C4ED8D00] [sps: 000001F4C1682A40]
[000001F4C4C02A10] GA_Running_Attack_ClockworkScythe_C /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Running_Attack_ClockworkScythe.Default__GA_Running_Attack_ClockworkScythe_C [n: 74A717] [c: 000001F4C168A6F0] [or: 000001F4C4ED8D00]
[000001F4C168F1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Light_ClockworkScythe_Hold.GA_Player_AttackSelector_Light_ClockworkScythe_Hold_C [n: 4740DE] [c: 00007FF46EEC3D38] [or: 000001F4C4ED8C60] [sps: 000001F4C1683FA0]
[000001F4BF3C6FA0] GA_Player_AttackSelector_Light_ClockworkScythe_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Light_ClockworkScythe_Hold.Default__GA_Player_AttackSelector_Light_ClockworkScythe_Hold_C [n: 74A762] [c: 000001F4C168F1C0] [or: 000001F4C4ED8C60]
[000001F4C6993880] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Heavy_ClockworkScythe.GA_Player_AttackSelector_Heavy_ClockworkScythe_C [n: 33A20C] [c: 00007FF46EEC3D38] [or: 000001F4C4ED8F80] [sps: 000001F4C16846C0]
[000001F4BF3C7630] GA_Player_AttackSelector_Heavy_ClockworkScythe_C /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Heavy_ClockworkScythe.Default__GA_Player_AttackSelector_Heavy_ClockworkScythe_C [n: 74A653] [c: 000001F4C6993880] [or: 000001F4C4ED8F80]
[000001F4C699EAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Light_ClockworkScythe.GA_Player_AttackSelector_Light_ClockworkScythe_C [n: 1623A6] [c: 00007FF46EEC3D38] [or: 000001F4C4ED7BD0] [sps: 000001F4C1683FA0]
[000001F4BF3C7CC0] GA_Player_AttackSelector_Light_ClockworkScythe_C /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Light_ClockworkScythe.Default__GA_Player_AttackSelector_Light_ClockworkScythe_C [n: 74A609] [c: 000001F4C699EAA0] [or: 000001F4C4ED7BD0]
[000001F4C6999C40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Running_Attack_ClockworkScythe_B.GA_Running_Attack_ClockworkScythe_B_C [n: 207059] [c: 00007FF46EEC3D38] [or: 000001F4C4ED8120] [sps: 000001F4C1680DC0]
[000001F4C4C052D0] GA_Running_Attack_ClockworkScythe_B_C /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Running_Attack_ClockworkScythe_B.Default__GA_Running_Attack_ClockworkScythe_B_C [n: 74A415] [c: 000001F4C6999C40] [or: 000001F4C4ED8120]
[000001F4C6999520] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Heavy_ClockworkScythe_Hold.GA_Player_AttackSelector_Heavy_ClockworkScythe_Hold_C [n: 8D554] [c: 00007FF46EEC3D38] [or: 000001F4C4ED7EF0] [sps: 000001F4C16846C0]
[000001F4BF3C89E0] GA_Player_AttackSelector_Heavy_ClockworkScythe_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_AttackSelector_Heavy_ClockworkScythe_Hold.Default__GA_Player_AttackSelector_Heavy_ClockworkScythe_Hold_C [n: 74A4D3] [c: 000001F4C6999520] [or: 000001F4C4ED7EF0]
[000001F7E9297FC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_WeaponBuff_AxeDagger.GA_WeaponBuff_AxeDagger_C [n: 11CF53] [c: 00007FF46EEC3D38] [or: 000001F7F77B1E10] [sps: 000001F7E929D540]
[000001F7C0390EB0] GA_WeaponBuff_AxeDagger_C /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_WeaponBuff_AxeDagger.Default__GA_WeaponBuff_AxeDagger_C [n: 75CA2D] [c: 000001F7E9297FC0] [or: 000001F7F77B1E10]
[000001F7E929FC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_WeaponBuff_BlackNeedle_Activation.GA_WeaponBuff_BlackNeedle_Activation_C [n: 436E72] [c: 00007FF46EEC3D38] [or: 000001F7F77B2270] [sps: 000001F7E929D540]
[000001F7C0391600] GA_WeaponBuff_BlackNeedle_Activation_C /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_WeaponBuff_BlackNeedle_Activation.Default__GA_WeaponBuff_BlackNeedle_Activation_C [n: 75CD35] [c: 000001F7E929FC70] [or: 000001F7F77B2270]
[000001F7E929F550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_WeaponBuff_MartyrsBlade.GA_WeaponBuff_MartyrsBlade_C [n: 1ECAC5] [c: 00007FF46EEC3D38] [or: 000001F7F77B3030] [sps: 000001F7E929D540]
[000001F7C0391D50] GA_WeaponBuff_MartyrsBlade_C /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_WeaponBuff_MartyrsBlade.Default__GA_WeaponBuff_MartyrsBlade_C [n: 75CD94] [c: 000001F7E929F550] [or: 000001F7F77B3030]
[000001F7E929EE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_WeaponBuff_HeavyHammer.GA_WeaponBuff_HeavyHammer_C [n: 32565D] [c: 00007FF46EEC3D38] [or: 000001F7F77B3080] [sps: 000001F7E929D540]
[000001F76BC8E2C0] GA_WeaponBuff_HeavyHammer_C /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_WeaponBuff_HeavyHammer.Default__GA_WeaponBuff_HeavyHammer_C [n: 75CD81] [c: 000001F7E929EE30] [or: 000001F7F77B3080]
[000001F76BD07180] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_WeaponBuff_BattleAxe.GA_WeaponBuff_BattleAxe_C [n: 23186E] [c: 00007FF46EEC3D38] [or: 000001F7F77B1D20] [sps: 000001F7E929D540]
[000001F76BC8DB70] GA_WeaponBuff_BattleAxe_C /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_WeaponBuff_BattleAxe.Default__GA_WeaponBuff_BattleAxe_C [n: 75CB51] [c: 000001F76BD07180] [or: 000001F7F77B1D20]
[000001F76BD06A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_WeaponBuff_Katanas.GA_WeaponBuff_Katanas_C [n: 1AB624] [c: 00007FF46EEC3D38] [or: 000001F7F77B2450] [sps: 000001F7E929D540]
[000001F76BC8D420] GA_WeaponBuff_Katanas_C /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_WeaponBuff_Katanas.Default__GA_WeaponBuff_Katanas_C [n: 75CD24] [c: 000001F76BD06A60] [or: 000001F7F77B2450]
[000001F76BD06340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_WeaponBuff_ClockworkScythe.GA_WeaponBuff_ClockworkScythe_C [n: 44C7C1] [c: 00007FF46EEC3D38] [or: 000001F7F77B1C30] [sps: 000001F7E929D540]
[000001F76BC8CCD0] GA_WeaponBuff_ClockworkScythe_C /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_WeaponBuff_ClockworkScythe.Default__GA_WeaponBuff_ClockworkScythe_C [n: 75CB7B] [c: 000001F76BD06340] [or: 000001F7F77B1C30]
[000001F76BD05C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_WeaponBuff_HadernsSword.GA_WeaponBuff_HadernsSword_C [n: 18CE19] [c: 00007FF46EEC3D38] [or: 000001F7F77B2F90] [sps: 000001F7E929D540]
[000001F76BC8C580] GA_WeaponBuff_HadernsSword_C /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_WeaponBuff_HadernsSword.Default__GA_WeaponBuff_HadernsSword_C [n: 75CDD5] [c: 000001F76BD05C20] [or: 000001F7F77B2F90]
[000001F7ED2C7C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_A_Finisher.GA_Player_Attack_BlackNeedle_A_Finisher_C [n: 28005F] [c: 00007FF46EEC3D38] [or: 000001F7ED2789E0] [sps: 000001F4B1779520]
[000001F8CAF80150] GA_Player_Attack_BlackNeedle_A_Finisher_C /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_A_Finisher.Default__GA_Player_Attack_BlackNeedle_A_Finisher_C [n: 83618D] [c: 000001F7ED2C7C30] [or: 000001F7ED2789E0]
[000001F8CE7C5FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Light_BlackNeedle.GA_Player_AttackSelector_Light_BlackNeedle_C [n: 420890] [c: 00007FF46EEC3D38] [or: 000001F7ED278620] [sps: 000001F4C1683FA0]
[000001F8B8CE0D30] GA_Player_AttackSelector_Light_BlackNeedle_C /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Light_BlackNeedle.Default__GA_Player_AttackSelector_Light_BlackNeedle_C [n: 836215] [c: 000001F8CE7C5FB0] [or: 000001F7ED278620]
[000001F8CE7C5890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_B_Finisher.GA_Player_Attack_BlackNeedle_B_Finisher_C [n: 3B072D] [c: 00007FF46EEC3D38] [or: 000001F7ED279A70] [sps: 000001F4C16814E0]
[000001F8CE79CD10] GA_Player_Attack_BlackNeedle_B_Finisher_C /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_Attack_BlackNeedle_B_Finisher.Default__GA_Player_Attack_BlackNeedle_B_Finisher_C [n: 83624E] [c: 000001F8CE7C5890] [or: 000001F7ED279A70]
[000001F8CE7C26B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Heavy_BlackNeedle.GA_Player_AttackSelector_Heavy_BlackNeedle_C [n: 3A3535] [c: 00007FF46EEC3D38] [or: 000001F7ED2797F0] [sps: 000001F4C16846C0]
[000001F7ED493B20] GA_Player_AttackSelector_Heavy_BlackNeedle_C /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Heavy_BlackNeedle.Default__GA_Player_AttackSelector_Heavy_BlackNeedle_C [n: 83630A] [c: 000001F8CE7C26B0] [or: 000001F7ED2797F0]
[000001F8CE7CA6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Running_Attack_BlackNeedle_B.GA_Running_Attack_BlackNeedle_B_C [n: 23187C] [c: 00007FF46EEC3D38] [or: 000001F7ED278670] [sps: 000001F4C16814E0]
[000001F8C88AD740] GA_Running_Attack_BlackNeedle_B_C /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Running_Attack_BlackNeedle_B.Default__GA_Running_Attack_BlackNeedle_B_C [n: 8361FF] [c: 000001F8CE7CA6F0] [or: 000001F7ED278670]
[000001F8CE7C9FD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Heavy_BlackNeedle_Hold.GA_Player_AttackSelector_Heavy_BlackNeedle_Hold_C [n: 150A11] [c: 00007FF46EEC3D38] [or: 000001F7ED2799D0] [sps: 000001F4C16846C0]
[000001F8B8CE06A0] GA_Player_AttackSelector_Heavy_BlackNeedle_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Heavy_BlackNeedle_Hold.Default__GA_Player_AttackSelector_Heavy_BlackNeedle_Hold_C [n: 83627B] [c: 000001F8CE7C9FD0] [or: 000001F7ED2799D0]
[000001F8CE7C98B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Running_Attack_BlackNeedle.GA_Running_Attack_BlackNeedle_C [n: 395A53] [c: 00007FF46EEC3D38] [or: 000001F7ED279700] [sps: 000001F4B1779520]
[000001F8C88AE170] GA_Running_Attack_BlackNeedle_C /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Running_Attack_BlackNeedle.Default__GA_Running_Attack_BlackNeedle_C [n: 83635D] [c: 000001F8CE7C98B0] [or: 000001F7ED279700]
[000001F8CE7C9190] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Light_BlackNeedle_Hold.GA_Player_AttackSelector_Light_BlackNeedle_Hold_C [n: 2C594F] [c: 00007FF46EEC3D38] [or: 000001F7ED279750] [sps: 000001F4C1683FA0]
[000001F7ED490D30] GA_Player_AttackSelector_Light_BlackNeedle_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/BlackNeedle/GA_Player_AttackSelector_Light_BlackNeedle_Hold.Default__GA_Player_AttackSelector_Light_BlackNeedle_Hold_C [n: 83633F] [c: 000001F8CE7C9190] [or: 000001F7ED279750]
[000001F8D832EAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.GA_Player_Attack_Heavy_Ataxana_C [n: 143E4C] [c: 00007FF46EEC3D38] [or: 000001F8D760F3D0] [sps: 000001F4C16814E0]
[000001F8C66348A0] GA_Player_Attack_Heavy_Ataxana_C /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Heavy_Ataxana.Default__GA_Player_Attack_Heavy_Ataxana_C [n: 8492E1] [c: 000001F8D832EAA0] [or: 000001F8D760F3D0]
[000001F8D832B1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.GA_Player_Attack_Light_Axatana_C [n: 37FDFF] [c: 00007FF46EEC3D38] [or: 000001F8D760EED0] [sps: 000001F4B1779520]
[000001F8D6FEA450] GA_Player_Attack_Light_Axatana_C /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Player_Attack_Light_Axatana.Default__GA_Player_Attack_Light_Axatana_C [n: 84942A] [c: 000001F8D832B1A0] [or: 000001F8D760EED0]
[000001F8D832A6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_Attack_Katanas_RunningAttack_Axe.GA_Player_Attack_Katanas_RunningAttack_Axe_C [n: 1E3964] [c: 00007FF46EEC3D38] [or: 000001F8D760EFC0] [sps: 000001F8D832B1A0]
[000001F8D6FE9A20] GA_Player_Attack_Katanas_RunningAttack_Axe_C /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_Attack_Katanas_RunningAttack_Axe.Default__GA_Player_Attack_Katanas_RunningAttack_Axe_C [n: 84936E] [c: 000001F8D832A6F0] [or: 000001F8D760EFC0]
[000001F8D8329FD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_AttackSelector_Heavy_Axatana_Axe.GA_Player_AttackSelector_Heavy_Axatana_Axe_C [n: 48987A] [c: 00007FF46EEC3D38] [or: 000001F8D760F010] [sps: 000001F4C16846C0]
[000001F8D1537630] GA_Player_AttackSelector_Heavy_Axatana_Axe_C /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_AttackSelector_Heavy_Axatana_Axe.Default__GA_Player_AttackSelector_Heavy_Axatana_Axe_C [n: 849440] [c: 000001F8D8329FD0] [or: 000001F8D760F010]
[000001F8D83298B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_AttackSelector_Heavy_Axatana_Axe_Hold.GA_Player_AttackSelector_Heavy_Axatana_Axe_Hold_C [n: 37B8FC] [c: 00007FF46EEC3D38] [or: 000001F8D760FE70] [sps: 000001F4C16846C0]
[000001F8D1537CC0] GA_Player_AttackSelector_Heavy_Axatana_Axe_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Axe/GA_Player_AttackSelector_Heavy_Axatana_Axe_Hold.Default__GA_Player_AttackSelector_Heavy_Axatana_Axe_Hold_C [n: 84945C] [c: 000001F8D83298B0] [or: 000001F8D760FE70]
[000001F8D8329190] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Axatana_Handler.GA_Axatana_Handler_C [n: CEC02] [c: 00007FF46EEC3D38] [or: 000001F8D760FE20] [sps: 000001F4DC018E00]
[000001F8C839FA90] GA_Axatana_Handler_C /Game/Sparta/Core/Player/Ability/Weapons/Axatana/GA_Axatana_Handler.Default__GA_Axatana_Handler_C [n: 84949F] [c: 000001F8D8329190] [or: 000001F8D760FE20]
[000001F7ED315170] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_RunningAttack.GA_Player_Attack_Katanas_RunningAttack_C [n: F1A24] [c: 00007FF46EEC3D38] [or: 000001F8D760F970] [sps: 000001F8D832B1A0]
[000001F8D6FE3440] GA_Player_Attack_Katanas_RunningAttack_C /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_Attack_Katanas_RunningAttack.Default__GA_Player_Attack_Katanas_RunningAttack_C [n: 8495A8] [c: 000001F7ED315170] [or: 000001F8D760F970]
[000001F7ED314A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_AttackSelector_Light_Katanas.GA_Player_AttackSelector_Light_Katanas_C [n: 29DF4A] [c: 00007FF46EEC3D38] [or: 000001F8D760F920] [sps: 000001F4C1683FA0]
[000001F8D1536280] GA_Player_AttackSelector_Light_Katanas_C /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_AttackSelector_Light_Katanas.Default__GA_Player_AttackSelector_Light_Katanas_C [n: 8495E4] [c: 000001F7ED314A50] [or: 000001F8D760F920]
[000001F7ED314330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_AttackSelector_Light_Katanas_Hold.GA_Player_AttackSelector_Light_Katanas_Hold_C [n: 23EADD] [c: 00007FF46EEC3D38] [or: 000001F8D760F880] [sps: 000001F4C1683FA0]
[000001F8D1536910] GA_Player_AttackSelector_Light_Katanas_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/Axatana/Katanas/GA_Player_AttackSelector_Light_Katanas_Hold.Default__GA_Player_AttackSelector_Light_Katanas_Hold_C [n: 8495FE] [c: 000001F7ED314330] [or: 000001F8D760F880]
[000001F7ED31C370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Running_Attack_AxeDagger_B.GA_Running_Attack_AxeDagger_B_C [n: 13B523] [c: 00007FF46EEC3D38] [or: 000001F7ED3401F0] [sps: 000001F4C16814E0]
[000001F7ED35A450] GA_Running_Attack_AxeDagger_B_C /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Running_Attack_AxeDagger_B.Default__GA_Running_Attack_AxeDagger_B_C [n: 849770] [c: 000001F7ED31C370] [or: 000001F7ED3401F0]
[000001F7ED31BC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Heavy_AxeDagger.GA_Player_AttackSelector_Heavy_AxeDagger_C [n: CEC0D] [c: 00007FF46EEC3D38] [or: 000001F7ED340290] [sps: 000001F4C16846C0]
[000001F8D15341B0] GA_Player_AttackSelector_Heavy_AxeDagger_C /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Heavy_AxeDagger.Default__GA_Player_AttackSelector_Heavy_AxeDagger_C [n: 849755] [c: 000001F7ED31BC50] [or: 000001F7ED340290]
[000001F7ED31B530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Heavy_AxeDagger_Hold.GA_Player_AttackSelector_Heavy_AxeDagger_Hold_C [n: 24BF5A] [c: 00007FF46EEC3D38] [or: 000001F7ED340240] [sps: 000001F4C16846C0]
[000001F8D1534840] GA_Player_AttackSelector_Heavy_AxeDagger_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Heavy_AxeDagger_Hold.Default__GA_Player_AttackSelector_Heavy_AxeDagger_Hold_C [n: 84979A] [c: 000001F7ED31B530] [or: 000001F7ED340240]
[000001F7ED31AE10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Running_Attack_AxeDagger_A.GA_Running_Attack_AxeDagger_A_C [n: 448144] [c: 00007FF46EEC3D38] [or: 000001F7ED3401A0] [sps: 000001F4B1779520]
[000001F8D7549A20] GA_Running_Attack_AxeDagger_A_C /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Running_Attack_AxeDagger_A.Default__GA_Running_Attack_AxeDagger_A_C [n: 8497B7] [c: 000001F7ED31AE10] [or: 000001F7ED3401A0]
[000001F7ED31A6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Light_AxeDagger.GA_Player_AttackSelector_Light_AxeDagger_C [n: 1E39A7] [c: 00007FF46EEC3D38] [or: 000001F7ED3400B0] [sps: 000001F4C1683FA0]
[000001F8D1534ED0] GA_Player_AttackSelector_Light_AxeDagger_C /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Light_AxeDagger.Default__GA_Player_AttackSelector_Light_AxeDagger_C [n: 8497CC] [c: 000001F7ED31A6F0] [or: 000001F7ED3400B0]
[000001F7ED319FD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Light_AxeDagger_Hold.GA_Player_AttackSelector_Light_AxeDagger_Hold_C [n: 2A28C8] [c: 00007FF46EEC3D38] [or: 000001F7ED341410] [sps: 000001F4C1683FA0]
[000001F8D1535560] GA_Player_AttackSelector_Light_AxeDagger_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/AxeDagger/GA_Player_AttackSelector_Light_AxeDagger_Hold.Default__GA_Player_AttackSelector_Light_AxeDagger_Hold_C [n: 8497E7] [c: 000001F7ED319FD0] [or: 000001F7ED341410]
[000001F7ED31F1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_A_Finisher.GA_Player_Attack_BattleAxe_A_Finisher_C [n: 2848F2] [c: 00007FF46EEC3D38] [or: 000001F7ED340FB0] [sps: 000001F4B1779520]
[000001F7ED355D00] GA_Player_Attack_BattleAxe_A_Finisher_C /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_A_Finisher.Default__GA_Player_Attack_BattleAxe_A_Finisher_C [n: 8498B9] [c: 000001F7ED31F1C0] [or: 000001F7ED340FB0]
[000001F7ED394A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_B_Finisher.GA_Player_Attack_BattleAxe_B_Finisher_C [n: 384193] [c: 00007FF46EEC3D38] [or: 000001F7ED340B00] [sps: 000001F4C16814E0]
[000001F7ED3515B0] GA_Player_Attack_BattleAxe_B_Finisher_C /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_Attack_BattleAxe_B_Finisher.Default__GA_Player_Attack_BattleAxe_B_Finisher_C [n: 849975] [c: 000001F7ED394A50] [or: 000001F7ED340B00]
[000001F7ED394330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Heavy_BattleAxe.GA_Player_AttackSelector_Heavy_BattleAxe_C [n: 1665CC] [c: 00007FF46EEC3D38] [or: 000001F7ED341EB0] [sps: 000001F4C16846C0]
[000001F8D1532770] GA_Player_AttackSelector_Heavy_BattleAxe_C /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Heavy_BattleAxe.Default__GA_Player_AttackSelector_Heavy_BattleAxe_C [n: 84998E] [c: 000001F7ED394330] [or: 000001F7ED341EB0]
[000001F7ED393880] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Running_Attack_B_BattleAxe.GA_Running_Attack_B_BattleAxe_C [n: 129FE2] [c: 00007FF46EEC3D38] [or: 000001F7ED341D70] [sps: 000001F4C16814E0]
[000001F7ED350B80] GA_Running_Attack_B_BattleAxe_C /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Running_Attack_B_BattleAxe.Default__GA_Running_Attack_B_BattleAxe_C [n: 8499C6] [c: 000001F7ED393880] [or: 000001F7ED341D70]
[000001F7ED393160] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Heavy_BattleAxe_Hold.GA_Player_AttackSelector_Heavy_BattleAxe_Hold_C [n: 1E39BD] [c: 00007FF46EEC3D38] [or: 000001F7ED341E10] [sps: 000001F4C16846C0]
[000001F8D15320E0] GA_Player_AttackSelector_Heavy_BattleAxe_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Heavy_BattleAxe_Hold.Default__GA_Player_AttackSelector_Heavy_BattleAxe_Hold_C [n: 8499A9] [c: 000001F7ED393160] [or: 000001F7ED341E10]
[000001F7ED392A40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Light_BattleAxe.GA_Player_AttackSelector_Light_BattleAxe_C [n: 183B62] [c: 00007FF46EEC3D38] [or: 000001F7ED341DC0] [sps: 000001F4C1683FA0]
[000001F8D1532E00] GA_Player_AttackSelector_Light_BattleAxe_C /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Light_BattleAxe.Default__GA_Player_AttackSelector_Light_BattleAxe_C [n: 8499F0] [c: 000001F7ED392A40] [or: 000001F7ED341DC0]
[000001F7ED392320] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Running_Attack_BattleAxe.GA_Running_Attack_BattleAxe_C [n: 26E93C] [c: 00007FF46EEC3D38] [or: 000001F7ED341BE0] [sps: 000001F4B1779520]
[000001F7ED350150] GA_Running_Attack_BattleAxe_C /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Running_Attack_BattleAxe.Default__GA_Running_Attack_BattleAxe_C [n: 849A28] [c: 000001F7ED392320] [or: 000001F7ED341BE0]
[000001F7ED391C00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Light_BattleAxe_Hold.GA_Player_AttackSelector_Light_BattleAxe_Hold_C [n: 412F6E] [c: 00007FF46EEC3D38] [or: 000001F7ED341C30] [sps: 000001F4C1683FA0]
[000001F8D1533490] GA_Player_AttackSelector_Light_BattleAxe_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/BattleAxe/GA_Player_AttackSelector_Light_BattleAxe_Hold.Default__GA_Player_AttackSelector_Light_BattleAxe_Hold_C [n: 849A0B] [c: 000001F7ED391C00] [or: 000001F7ED341C30]
[000001F7ED3914E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Grinder.GA_Player_Attack_ClockworkScythe_Grinder_C [n: 3D8353] [c: 00007FF46EEC3D38] [or: 000001F7ED341B40] [sps: 000001F4BBD2F550]
[000001F7ED3CF5D0] GA_Player_Attack_ClockworkScythe_Grinder_C /Game/Sparta/Core/Player/Ability/Weapons/ClockworkScythe/GA_Player_Attack_ClockworkScythe_Grinder.Default__GA_Player_Attack_ClockworkScythe_Grinder_C [n: 849A87] [c: 000001F7ED3914E0] [or: 000001F7ED341B40]
[000001F7ED399190] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_RunningAttack_B.GA_Player_HadernsSword_RunningAttack_B_C [n: BE169] [c: 00007FF46EEC3D38] [or: 000001F7ED342720] [sps: 000001F4C16814E0]
[000001F7ED3C7B90] GA_Player_HadernsSword_RunningAttack_B_C /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_RunningAttack_B.Default__GA_Player_HadernsSword_RunningAttack_B_C [n: 849BF5] [c: 000001F7ED399190] [or: 000001F7ED342720]
[000001F7ED398A70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_B_Finisher.GA_Player_HadernsSword_B_Finisher_C [n: 254537] [c: 00007FF46EEC3D38] [or: 000001F7ED3424F0] [sps: 000001F4C16814E0]
[000001F8D7548FF0] GA_Player_HadernsSword_B_Finisher_C /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_B_Finisher.Default__GA_Player_HadernsSword_B_Finisher_C [n: 849C40] [c: 000001F7ED398A70] [or: 000001F7ED3424F0]
[000001F7ED398350] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Heavy_HadernsSword.GA_Player_AttackSelector_Heavy_HadernsSword_C [n: 3AC25C] [c: 00007FF46EEC3D38] [or: 000001F7ED342810] [sps: 000001F4C16846C0]
[000001F8D15306A0] GA_Player_AttackSelector_Heavy_HadernsSword_C /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Heavy_HadernsSword.Default__GA_Player_AttackSelector_Heavy_HadernsSword_C [n: 849BC6] [c: 000001F7ED398350] [or: 000001F7ED342810]
[000001F7ED397C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Heavy_HadernsSword_Hold.GA_Player_AttackSelector_Heavy_HadernsSword_Hold_C [n: 28009D] [c: 00007FF46EEC3D38] [or: 000001F7ED342770] [sps: 000001F4C16846C0]
[000001F8D1530D30] GA_Player_AttackSelector_Heavy_HadernsSword_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Heavy_HadernsSword_Hold.Default__GA_Player_AttackSelector_Heavy_HadernsSword_Hold_C [n: 849C57] [c: 000001F7ED397C30] [or: 000001F7ED342770]
[000001F7ED39F550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_A_Finisher.GA_Player_HadernsSword_A_Finisher_C [n: 46B45A] [c: 00007FF46EEC3D38] [or: 000001F7ED342360] [sps: 000001F4B1779520]
[000001F7ED3C6730] GA_Player_HadernsSword_A_Finisher_C /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_A_Finisher.Default__GA_Player_HadernsSword_A_Finisher_C [n: 849CA3] [c: 000001F7ED39F550] [or: 000001F7ED342360]
[000001F7ED406A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_RunningAttack.GA_Player_HadernsSword_RunningAttack_C [n: 21BB5A] [c: 00007FF46EEC3D38] [or: 000001F7ED342270] [sps: 000001F4B1779520]
[000001F7ED3C48A0] GA_Player_HadernsSword_RunningAttack_C /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_HadernsSword_RunningAttack.Default__GA_Player_HadernsSword_RunningAttack_C [n: 849CE0] [c: 000001F7ED406A60] [or: 000001F7ED342270]
[000001F7ED406340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Light_HadernsSword.GA_Player_AttackSelector_Light_HadernsSword_C [n: 33E6EA] [c: 00007FF46EEC3D38] [or: 000001F7ED342040] [sps: 000001F4C1683FA0]
[000001F8D12CEC50] GA_Player_AttackSelector_Light_HadernsSword_C /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Light_HadernsSword.Default__GA_Player_AttackSelector_Light_HadernsSword_C [n: 849CF9] [c: 000001F7ED406340] [or: 000001F7ED342040]
[000001F7ED405C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Light_HadernsSword_Hold.GA_Player_AttackSelector_Light_HadernsSword_Hold_C [n: 43FA6A] [c: 00007FF46EEC3D38] [or: 000001F7ED341FF0] [sps: 000001F4C1683FA0]
[000001F8C682A420] GA_Player_AttackSelector_Light_HadernsSword_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/HadernsSword/GA_Player_AttackSelector_Light_HadernsSword_Hold.Default__GA_Player_AttackSelector_Light_HadernsSword_Hold_C [n: 849D15] [c: 000001F7ED405C20] [or: 000001F7ED341FF0]
[000001F7ED4026B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_A_Finisher.GA_Player_Attack_HeavyHammer_A_Finisher_C [n: 325647] [c: 00007FF46EEC3D38] [or: 000001F7ED342F90] [sps: 000001F4B1779520]
[000001F7ED3C0150] GA_Player_Attack_HeavyHammer_A_Finisher_C /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_A_Finisher.Default__GA_Player_Attack_HeavyHammer_A_Finisher_C [n: 849DF1] [c: 000001F7ED4026B0] [or: 000001F7ED342F90]
[000001F7ED40DFF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_B_Finisher.GA_Player_Attack_HeavyHammer_B_Finisher_C [n: 335FDE] [c: 00007FF46EEC3D38] [or: 000001F7ED342AE0] [sps: 000001F4C16814E0]
[000001F7ED44B8B0] GA_Player_Attack_HeavyHammer_B_Finisher_C /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_Attack_HeavyHammer_B_Finisher.Default__GA_Player_Attack_HeavyHammer_B_Finisher_C [n: 849EB5] [c: 000001F7ED40DFF0] [or: 000001F7ED342AE0]
[000001F7ED40D8D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Heavy_HeavyHammer.GA_Player_AttackSelector_Heavy_HeavyHammer_C [n: 4B9314] [c: 00007FF46EEC3D38] [or: 000001F7ED3429A0] [sps: 000001F4C16846C0]
[000001F8C682CB80] GA_Player_AttackSelector_Heavy_HeavyHammer_C /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Heavy_HeavyHammer.Default__GA_Player_AttackSelector_Heavy_HeavyHammer_C [n: 849ECF] [c: 000001F7ED40D8D0] [or: 000001F7ED3429A0]
[000001F7ED40CE20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Running_Attack_HeavyHammer_B.GA_Running_Attack_HeavyHammer_B_C [n: D77CC] [c: 00007FF46EEC3D38] [or: 000001F7ED343D00] [sps: 000001F4C16814E0]
[000001F7ED44AE80] GA_Running_Attack_HeavyHammer_B_C /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Running_Attack_HeavyHammer_B.Default__GA_Running_Attack_HeavyHammer_B_C [n: 849F09] [c: 000001F7ED40CE20] [or: 000001F7ED343D00]
[000001F7ED40C700] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Heavy_HeavyHammer_Hold.GA_Player_AttackSelector_Heavy_HeavyHammer_Hold_C [n: 41BF4B] [c: 00007FF46EEC3D38] [or: 000001F7ED343DA0] [sps: 000001F4C16846C0]
[000001F8CF963B20] GA_Player_AttackSelector_Heavy_HeavyHammer_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Heavy_HeavyHammer_Hold.Default__GA_Player_AttackSelector_Heavy_HeavyHammer_Hold_C [n: 849EEB] [c: 000001F7ED40C700] [or: 000001F7ED343DA0]
[000001F7ED40BFE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Light_HeavyHammer.GA_Player_AttackSelector_Light_HeavyHammer_C [n: 33A225] [c: 00007FF46EEC3D38] [or: 000001F7ED343CB0] [sps: 000001F4C1683FA0]
[000001F8D1530010] GA_Player_AttackSelector_Light_HeavyHammer_C /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Light_HeavyHammer.Default__GA_Player_AttackSelector_Light_HeavyHammer_C [n: 849F35] [c: 000001F7ED40BFE0] [or: 000001F7ED343CB0]
[000001F7ED40B8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Running_Attack_HeavyHammer.GA_Running_Attack_HeavyHammer_C [n: 20B3F3] [c: 00007FF46EEC3D38] [or: 000001F7ED343B20] [sps: 000001F4B1779520]
[000001F7ED44A450] GA_Running_Attack_HeavyHammer_C /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Running_Attack_HeavyHammer.Default__GA_Running_Attack_HeavyHammer_C [n: 849F6F] [c: 000001F7ED40B8C0] [or: 000001F7ED343B20]
[000001F7ED40B1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Light_HeavyHammer_Hold.GA_Player_AttackSelector_Light_HeavyHammer_Hold_C [n: 302B4E] [c: 00007FF46EEC3D38] [or: 000001F7ED343B70] [sps: 000001F4C1683FA0]
[000001F8D15313C0] GA_Player_AttackSelector_Light_HeavyHammer_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/HeavyHammer/GA_Player_AttackSelector_Light_HeavyHammer_Hold.Default__GA_Player_AttackSelector_Light_HeavyHammer_Hold_C [n: 849F51] [c: 000001F7ED40B1A0] [or: 000001F7ED343B70]
[000001F7ED408350] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_A_Finisher.GA_Player_Attack_MartyrsBlade_A_Finisher_C [n: 1C12BB] [c: 00007FF46EEC3D38] [or: 000001F7ED343710] [sps: 000001F4B1779520]
[000001F7ED446730] GA_Player_Attack_MartyrsBlade_A_Finisher_C /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_A_Finisher.Default__GA_Player_Attack_MartyrsBlade_A_Finisher_C [n: 84A02C] [c: 000001F7ED408350] [or: 000001F7ED343710]
[000001F7ED4F6A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_B_Finisher.GA_Player_Attack_MartyrsBlade_B_Finisher_C [n: 17F366] [c: 00007FF46EEC3D38] [or: 000001F7ED3447A0] [sps: 000001F4C16814E0]
[000001F7ED442A10] GA_Player_Attack_MartyrsBlade_B_Finisher_C /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_Attack_MartyrsBlade_B_Finisher.Default__GA_Player_Attack_MartyrsBlade_B_Finisher_C [n: 84A0DC] [c: 000001F7ED4F6A60] [or: 000001F7ED3447A0]
[000001F7ED4F5FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Running_Attack_MartyrsBlade_B.GA_Running_Attack_MartyrsBlade_B_C [n: 89072] [c: 00007FF46EEC3D38] [or: 000001F7ED344610] [sps: 000001F4C16814E0]
[000001F7ED441FE0] GA_Running_Attack_MartyrsBlade_B_C /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Running_Attack_MartyrsBlade_B.Default__GA_Running_Attack_MartyrsBlade_B_C [n: 84A113] [c: 000001F7ED4F5FB0] [or: 000001F7ED344610]
[000001F7ED4F5890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Heavy_MartyrsBlade.GA_Player_AttackSelector_Heavy_MartyrsBlade_C [n: 1FE3E2] [c: 00007FF46EEC3D38] [or: 000001F7ED3446B0] [sps: 000001F4C16846C0]
[000001F8CF969070] GA_Player_AttackSelector_Heavy_MartyrsBlade_C /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Heavy_MartyrsBlade.Default__GA_Player_AttackSelector_Heavy_MartyrsBlade_C [n: 84A0F7] [c: 000001F7ED4F5890] [or: 000001F7ED3446B0]
[000001F7ED4F5170] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Heavy_MartyrsBlade_Hold.GA_Player_AttackSelector_Heavy_MartyrsBlade_Hold_C [n: 42DDA1] [c: 00007FF46EEC3D38] [or: 000001F7ED344660] [sps: 000001F4C16846C0]
[000001F8CF964840] GA_Player_AttackSelector_Heavy_MartyrsBlade_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Heavy_MartyrsBlade_Hold.Default__GA_Player_AttackSelector_Heavy_MartyrsBlade_Hold_C [n: 84A141] [c: 000001F7ED4F5170] [or: 000001F7ED344660]
[000001F7ED4F4A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Running_Attack_MartyrsBlade.GA_Running_Attack_MartyrsBlade_C [n: 3B995F] [c: 00007FF46EEC3D38] [or: 000001F7ED3445C0] [sps: 000001F4B1779520]
[000001F7ED4415B0] GA_Running_Attack_MartyrsBlade_C /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Running_Attack_MartyrsBlade.Default__GA_Running_Attack_MartyrsBlade_C [n: 84A160] [c: 000001F7ED4F4A50] [or: 000001F7ED3445C0]
[000001F7ED4F4330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Light_MartyrsBlade.GA_Player_AttackSelector_Light_MartyrsBlade_C [n: 2959F0] [c: 00007FF46EEC3D38] [or: 000001F7ED3444D0] [sps: 000001F4C1683FA0]
[000001F8D1531A50] GA_Player_AttackSelector_Light_MartyrsBlade_C /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Light_MartyrsBlade.Default__GA_Player_AttackSelector_Light_MartyrsBlade_C [n: 84A176] [c: 000001F7ED4F4330] [or: 000001F7ED3444D0]
[000001F7ED4F3C10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Light_MartyrsBlade_Hold.GA_Player_AttackSelector_Light_MartyrsBlade_Hold_C [n: 261715] [c: 00007FF46EEC3D38] [or: 000001F7ED3443E0] [sps: 000001F4C1683FA0]
[000001F8D1533B20] GA_Player_AttackSelector_Light_MartyrsBlade_Hold_C /Game/Sparta/Core/Player/Ability/Weapons/MartyrsBlade/GA_Player_AttackSelector_Light_MartyrsBlade_Hold.Default__GA_Player_AttackSelector_Light_MartyrsBlade_Hold_C [n: 84A192] [c: 000001F7ED4F3C10] [or: 000001F7ED3443E0]
```

---

## 7. Dodge / roll / dash, block, harden, parry, riposte

### 7.1 Dash -- `/Game/Sparta/Core/Player/Ability/Dash/`

| Class | Parent | Own props | Notes |
|---|---|---|---|
| `.../Dash/GA_DashBase.GA_DashBase_C` | `GA_SpartaAbility_C` | 19 (below) | base for every shell dash |
| `.../Dash/GA_Dash_CorruptedGenessa.GA_Dash_CorruptedGenessa_C` | `GA_DashBase_C` | none | |
| `.../Dash/GA_Dash_Eredrim.GA_Dash_Eredrim_C` | `GA_DashBase_C` | none | |
| `.../Dash/GA_Dash_Genessa.GA_Dash_Genessa_C` | `GA_DashBase_C` | none | |
| `.../Dash/GA_Dash_Gragu.GA_Dash_Gragu_C` | `GA_DashBase_C` | none | |
| `.../Dash/GA_Dash_Harros.GA_Dash_Harros_C` | `GA_DashBase_C` | none | |
| `.../Dash/GA_Dash_Necrophage.GA_Dash_Necrophage_C` | `GA_DashBase_C` | none | |
| `.../Dash/GA_Dash_Proxima.GA_Dash_Proxima_C` | `GA_DashBase_C` | none | |
| `.../Dash/GA_Dash_Sariel.GA_Dash_Sariel_C` | `GA_DashBase_C` | none | |
| `.../Dash/GA_Dash_Sheep.GA_Dash_Sheep_C` | `GA_DashBase_C` | none | transformation dash |
| `.../Dash/GA_Dash_Smert.GA_Dash_Smert_C` | `GA_DashBase_C` | none | |
| `.../Dash/GA_Dash_StrongOne.GA_Dash_StrongOne_C` | `GA_DashBase_C` | none | |
| `.../Dash/GA_Dash_Tiel.GA_Dash_Tiel_C` | `GA_DashBase_C` | 10 (below) | shadow-dash that damages |
| `.../Core/Characters/Player/Common/Abilities/.../GA_Dash_Shell_AI.GA_Dash_Shell_AI_C` | `GA_DashBase_C` | none | AI-driven shell dash |
| `.../Common/.../ShellSummon/GA_ShellSummon_Dash.GA_ShellSummon_Dash_C` | `GA_DashBase_C` | 1 | summons (see sec.10) |

`GA_DashBase_C` properties -- the i-frame windows a co-op layer must mirror:

| Property | Type | Offset |
|---|---|---|
| `MovementInput` | `StructProperty of CoreUObject.Vector2D` | `0x518` |
| `AbilityMontage` | `ObjectProperty of Engine.AnimMontage` | `0x528` |
| `DashPlayRate` | `FloatProperty` | `0x530` |
| `DashDistance` | `FloatProperty` | `0x534` |
| `DashDirection` | `EnumProperty of Sparta.ESpartaMovementDirection` | `0x538` |
| `InvulnerabilityDuration` | `FloatProperty` | `0x53C` |
| `StrafeDashDistance` | `DoubleProperty` | `0x540` |
| `DashTravelTime` | `FloatProperty` | `0x548` |
| `EndLocation` / `InitialLocation` | `FVector` | `0x550` / `0x568` |
| `AudioEvent` | `ObjectProperty of WaveWeaver.AudioEvent` | `0x580` |
| `GE_Invulnerability` | `StructProperty of ActiveGameplayEffectHandle` | `0x588` |
| `DashOrientation` | `FRotator` | `0x590` |
| `DefaultDashMontages`, `H1DashMontages`, `GetUpDashMontage`, `H2DashMontages` | `CSMontageContainer_EightDirections` | `0x5A8`, `0x5E8`, `0x628`, `0x678` |
| `DashVFXCue` | `GameplayTag` | `0x668` |
| `StopDashMontageOnAbilityEnd` | `BoolProperty` | `0x670` |

`GA_Dash_Tiel_C` adds a damaging dash: `HitEnemies: ArrayProperty` `0x6C0`, `TraceRate: float` `0x6D0`,
`DrawDebugTrace: ByteProperty` `0x6D4`, `GE_NoProjectileCollision: ClassProperty` `0x6D8`,
`GE_NoProjectileCollision_Handler: ActiveGameplayEffectHandle` `0x6E0`,
`HitReceivedTask: AbilityTask_WaitGameplayEvent` `0x6E8`, `HitPayload: ObjectProperty of
Sparta.SpartaHitPayload` `0x6F0`, `DefaultStacks: int32` `0x6F8`, `Victim: ObjectProperty of
Engine.Actor` `0x700`, `DefaultShadowDash: float` `0x708`.

### 7.2 Roll -- `/Game/Sparta/Core/Player/Ability/Roll/`

| Class | Parent | Own props |
|---|---|---|
| `.../Roll/GA_RollBase.GA_RollBase_C` | `GA_SpartaAbility_C` | 14 |
| `.../Roll/GA_Roll_Smert.GA_Roll_Smert_C` | `GA_RollBase_C` | none |

`GA_RollBase_C` mirrors `GA_DashBase_C` field-for-field with roll naming:
`MovementInput` `0x518`, `AbilityMontage` `0x528`, `RollPlayRate` `0x530`, `RollDistance` `0x534`,
`RollDirection` (`ESpartaMovementDirection`) `0x538`, `InvulnerabilityDuration` `0x53C`,
`StrafeRollDistance` `0x540`, `RollTravelTime` `0x548`, `EndLocation` `0x550`, `InitialLocation` `0x568`,
`AudioEvent` `0x580`, `GE_Invulnerability` `0x588`, `RollOrientation` `0x590`,
`DefaultRollMontages` (`CSMontageContainer_EightDirections`) `0x5A8`.

Note: only **Smert** has a roll variant; all other shells use the dash variants above -- GUESS: rolling
vs dashing is a per-shell design choice, not a separate input.

### 7.3 Block -- `/Game/Sparta/Core/Characters/Player/Common/Abilities/ActiveBlock/GA_ActiveBlock_C`

Parent `GA_SpartaAbility_C`. 22 own properties, all of them block state or block effect handles:

| Property | Type | Offset |
|---|---|---|
| `BlockRegenHandle` | `ActiveGameplayEffectHandle` | `0x518` |
| `ArmorOverrideHandle` | `StructProperty of Sparta.ArmorOverrideHandle` | `0x520` |
| `BlockDamageHandle` | `ActiveGameplayEffectHandle` | `0x53C` |
| `PerfectBlockHandle` | `ActiveGameplayEffectHandle` | `0x544` |
| `PerfectBlockCooldownHandle` | `ActiveGameplayEffectHandle` | `0x54C` |
| `RotationMethodHandle` | `ActiveGameplayEffectHandle` | `0x554` |
| `HitStopHandle` | `CSGlobalTimeDilationHandle` | `0x560` |
| `CameraModifierHandle` | `CSCameraStateModifierHandle` | `0x588` |
| `KnockbackStrength_Normal` / `_Perfect` | `FloatProperty` | `0x58C` / `0x590` |
| `PerfectBlockKnockbackReduction` | `DoubleProperty` | `0x598` |
| `BlockLoopMontage` | `ObjectProperty of Engine.AnimMontage` | `0x5A0` |
| `HitStopDuration`, `HitStopTimeDilation` | `DoubleProperty` | `0x5A8`, `0x5B0` |
| `HitStopBlend` | `FAlphaBlend` | `0x5B8` |
| `CameraModifier` | `CSCameraStateModifier` | `0x5E8` |
| `InputReleaseTimeHandle` | `TimerHandle` | `0x6F0` |
| `ActivationTime`, `RequiredActivationTime` | `DoubleProperty` | `0x6F8`, `0x700` |
| `BlockNewConditions` | `ActiveGameplayEffectHandle` | `0x708` |
| `BlockReceivingHitsHandler` | `TimerHandle` | `0x710` |
| `PerfectBlockInvulnerbilityDuration` | `FloatProperty` | `0x718` |

`ActivationTime` vs `RequiredActivationTime` is the perfect-block window: holding block accumulates
`ActivationTime`; a hit while it is below `RequiredActivationTime` is a *perfect* block. Sync
implication: blocking is a *held state* (`WhileInputActive`), so replicate "blocking on/off + start
time", not a per-hit event.

### 7.4 Harden / Stone Form -- `.../Common/Abilities/StoneForm/GA_Harden_Original_C`

Parent `GA_SpartaAbility_C`. 37 own properties; this is the "harden"/petrify ability.

| Property | Type | Offset |
|---|---|---|
| `bInStoneForm`, `bFullyLeftStoneForm` | `BoolProperty` | `0x518`, `0x519` |
| `StoneFormHitPayload` | `ObjectProperty of .../BP_SpartaHitPayload.BP_SpartaHitPayload_C` | `0x520` |
| `ActivationBlockingTags` | `GameplayTagContainer` | `0x528` |
| `EnterStoneFormInterpSpeed` / `LeaveStoneFormInterpSpeed` | `DoubleProperty` | `0x548` / `0x550` |
| `DefaultAnimRateScale` / `TargetAnimRateScale` | `DoubleProperty` | `0x558` / `0x560` |
| `StoneFormDurationAfterHit` | `FloatProperty` | `0x568` |
| `StoneFormSelfKnockback` | `FloatProperty` | `0x56C` |
| `StoneFormCooldown` | `FloatProperty` | `0x570` |
| `ArmorOverrideHandle` | `ArmorOverrideHandle` | `0x574` |
| `GE_PoiseImmunity` | `ActiveGameplayEffectHandle` | `0x590` |
| `GE_Harden` | `ActiveGameplayEffectHandle` | `0x598` |
| `BP_Harden_PP` | `ObjectProperty of .../StoneForm/BP_Harden_PP.BP_Harden_PP_C` | `0x5A0` |
| `PressTask` / `ReleaseTask` | `AbilityTask_WaitGameplayEvent` | `0x5A8` / `0x5B0` |
| `StoneFormPoiseDamage` | `FloatProperty` | `0x5B8` |
| `PerfectStoneFormDuration` | `FloatProperty` | `0x5BC` |
| `PerfectStoneFormCooldown` | `FloatProperty` | `0x5C0` |
| `PerfectStoneFormPoiseDamage` | `FloatProperty` | `0x5C4` |
| `PerfectStoneFormBreakDamage` | `FloatProperty` | `0x5C8` |
| `PerfectStoneFormSelfKnockback` | `FloatProperty` | `0x5CC` |
| `WidgetBar` | `ObjectProperty of .../WBP_Player_Bar_StoneForm.WBP_Player_Bar_StoneForm_C` | `0x5D0` |
| `CachedCapsuleRadius` | `FloatProperty` | `0x5D8` |
| `DelayedEndTimer` | `TimerHandle` | `0x5E0` |
| `MyDeltaTime` | `DoubleProperty` | `0x5E8` |
| `BlockedHit`, `BlockedRangedAttack` | `BoolProperty` | `0x5F0`, `0x600` |
| `AnimRateScaleTask` | `ObjectProperty of CSAbilityTasks.AbilityTask_TickUntilCancelled` | `0x5F8` |
| `BlockingMontage` | `ObjectProperty of Engine.AnimMontage` | `0x608` |
| `LastHitInstigator` | `ObjectProperty of Engine.Actor` | `0x610` |
| `LastHitPayload` | `ObjectProperty of Sparta.SpartaHitPayload` | `0x618` |
| `LastHitEffectContext` | `GameplayEffectContextHandle` | `0x620` |
| `LastHitDamageCauser` | `ObjectProperty of Engine.Actor` | `0x638` |
| `LastHitResult` | `FHitResult` | `0x640` |
| `bRateScaleTicking` | `BoolProperty` | `0x740` |

### 7.5 Parry / riposte / deflect -- `/Game/Sparta/Core/Player/Ability/Parry/`

| Class | Parent | Own props | Role |
|---|---|---|---|
| `.../Parry/GA_Parry_Handler.GA_Parry_Handler_C` | `GA_SpartaAbility_C` | 2 | receives the "was parried" gameplay event |
| `.../Parry/GA_Parry_Action_Single.GA_Parry_Action_Single_C` | `GA_PlayMontageBase_C` | 2 | the parry attempt animation |
| `.../Parry/GA_Parry_Successful.GA_Parry_Successful_C` | `GA_PlayMontageBase_C` | 7 | the parry success reaction |
| `.../Parry/GA_Parry_Successful_Projectile_Deflect.GA_Parry_Successful_Projectile_Deflect_C` | `GA_Parry_Successful_C` | 4 | projectile deflection variant |
| `.../Parry/GA_Parry_Riposte.GA_Parry_Riposte_C` | `GA_SpartaAbility_C` | 16 | **paired animation** with the parried enemy |

| Class | Property | Type | Offset |
|---|---|---|---|
| `GA_Parry_Handler_C` | `HitReceivedPayload` | `GameplayEventData` | `0x518` |
| | `BlockDamageHandle` | `ActiveGameplayEffectHandle` | `0x5C8` |
| `GA_Parry_Action_Single_C` | `SecondPreParryMontage` | `ObjectProperty of Engine.AnimMontage` | `0x628` |
| | `FirstPreParryMontage` | `ObjectProperty of Engine.AnimMontage` | `0x630` |
| `GA_Parry_Successful_C` | `AudioEvent` | `ObjectProperty of WaveWeaver.AudioEvent` | `0x628` |
| | `KnockbackStrength` | `FloatProperty` | `0x630` |
| | `KnockbackReduction` | `DoubleProperty` | `0x638` |
| | `ParryVictim` | `ObjectProperty of Engine.Actor` | `0x640` |
| | `CommittedFromEvent` | `BoolProperty` | `0x648` |
| | `EventData` | `GameplayEventData` | `0x650` |
| | `BreakDamage` | `DoubleProperty` | `0x700` |
| `GA_Parry_Riposte_C` | `InstigatorMontages` | `MapProperty` | `0x518` |
| | `MontageIndex` | `IntProperty` | `0x568` |
| | `OwnerSpartaCharacter` | `ObjectProperty of Sparta.SpartaCharacter` | `0x570` |
| | `Victim` | `ObjectProperty of Engine.Actor` | `0x578` |
| | `RipostePayload` | `StructProperty of Sparta.SpartaHitPayloadHolder` | `0x580` |
| | `InstigatorSelectedMontage` / `ParticipantSelectedMontage` | `ObjectProperty of Engine.AnimMontage` | `0x588` / `0x590` |
| | `VictimWeapon` / `StolenWeapon` | `ObjectProperty of Sparta.SpartaWeapon` | `0x598` / `0x5A0` |
| | `DuplicatePayload` | `ObjectProperty of Sparta.SpartaHitPayload` | `0x5A8` |
| | `InstigatorMontages_Quick` | `MapProperty` | `0x5B0` |
| | `UseBaseRiposte` | `BoolProperty` | `0x600` |
| | `TargetMontageAnimMap` | `MapProperty` | `0x608` |
| | `WeaknessAttribute`, `MultiplierAttribute` | `GameplayAttribute` | `0x658`, `0x690` |
| | `EnemiesWithQuickRiposte` | `GameplayTagContainer` | `0x6C8` |

`GA_Parry_Riposte_C` is a **two-actor ability**: it holds `Victim`, the victim's `VictimWeapon`,
`ParticipantSelectedMontage` (the victim's animation) and `RipostePayload`. It is the single most
co-op-critical class in this family: the riposte animation is authored as a *paired montage*, so both
players must run the same ability on the same pair of actors or the animations desync. `MontageIndex`,
`TargetMontageAnimMap` and `InstigatorMontages`/`InstigatorMontages_Quick` select which montage pair.

### 7.6 Plunging attack -- `.../Ability/Plunging/GA_Player_Attack_Plunging_C`

Parent `GA_AttackBase_Melee_C`, 15 own properties:
`CameraState` (`CSCameraState`) `0x8D8`, `BaseDamage` `0x8E0`, `PoiseDamage` `0x8E8`,
`WarpingDuration` `0x8F0`, `WarpDistanceZ` `0x8F4`, `StartSFX` (`AudioEventId`) `0x8F8`,
`PlungeWindParam` `0x900`, `PlungeFallTime` `0x908`, `MaxTimeForFullWind` `0x910`, `UpdateFreq` `0x918`,
`BodyComponent` (`ObjectProperty of Sparta.SpartaCharacterAudioComponent`) `0x920`,
`TimeForFullWind` `0x928`, `CCParamTimeHandle` (`TimerHandle`) `0x930`,
`LoopMontage` `0x938`, `LandingMontage` `0x940`.
The plunge is triggered from the selector's `PlungingAttackTags` / `PlungingTraceLength`
(sec.5.1), i.e. an attack while airborne.

### 7.7 Movement / aim / lock-on abilities (state, not montage)

| Class | Parent | Own properties (offset) |
|---|---|---|
| `GA_Sprint_C` | `GA_SpartaBase_C` | `SprintCameraShake: ObjectProperty of Engine.CameraShakeBase` `0x5E0`, `bIsSprinting: bool` `0x5E8`, `ReleaseTask`/`ToggleTask: AbilityTask_WaitGameplayEvent` `0x5F0`/`0x5F8`, `VelocityThreshold` `0x600`, `ActiveLowVelocityTime` `0x608`, `LowVelocityDeactivationDelay` `0x610` |
| `GA_ForceSprint_C` | `GA_SpartaAbility_C` | none |
| `GA_Sprint_Sheep_C` | `GA_Sprint_C` | `SprintRotationSpring` |
| `GA_Walk_C` | `GA_SpartaBase_C` | `IsWalking` `0x5E0`, `IsWalkingInputPressed` `0x5E1`, `IsWalkingInputToggled` `0x5E2`, `TagQueryPass` `0x5E3`, `CameraState: ObjectProperty of CSCameraState` `0x5E8` |
| `GA_Aiming_Handler_C` | `GA_SpartaAbility_C` | `LockOnComponent` `0x518`, `SoftTargetingComponent` `0x520`, `SidearmWeapon: ObjectProperty of Sparta.SpartaWeapon` `0x528`, `GE_DesiredRotation` `0x530`, `CameraStateInstance` `0x538`, `AimComponent` `0x540`, `CheckFallingTimer` `0x548`, `StowBlocked` `0x550`, `StowRequested` `0x551`, `Input: StructProperty of Sparta.AbilityInputTagBlock` `0x558` |
| `GA_ToggleLockOn_C` | `GA_SpartaBase_C` | `LockOnComponent: ObjectProperty of .../BP_TargetLockComponent.BP_TargetLockComponent_C` `0x5E0` |
| `GA_SwitchLockOnTarget_C` | `GA_SpartaBase_C` | none |
| `GA_CharacterSkinHandler_C` | `GA_SpartaAbility_C` | none |
| `GA_IdleWhileStreaming_C` | `GA_PlayMontageBase_C` | none |
| `GA_WaitOnHoldInput_C` | `GA_PlayMontageBase_C` | none |
| `GA_BugIt_C` / `GA_BugItGo_C` | `GA_SpartaBase_C` | `GA_BugItGo_C`: `CameraOffset`, `OutputString` |
| `GA_Player_Spawn_C` | `GA_SpartaAbility_C` | none |

### 7.8 Charges (heal flasks) and player death

| Class | Parent | Own properties (offset) |
|---|---|---|
| `GA_ChargesBase_C` | `GA_SpartaAbility_C` | `CachedCharacter: ObjectProperty of Sparta.SpartaCharacter` `0x518`, `CachedHealthComponent: ObjectProperty of Sparta.SpartaHealthComponent` `0x520`, `ChargeEffect: ClassProperty` `0x528`, `ActivationMontage: ObjectProperty of Engine.AnimMontage` `0x530`, `MontagePlayRate: float` `0x538`, `bHealingCommitted: bool` `0x53C`, `bHealingStarted: bool` `0x53D` |
| `GA_Charges_Channeling_C` | `GA_ChargesBase_C` | `CostPerSecond` `0x548`, `HealPerSecond` `0x550`, `ReleaseTask: AbilityTask_WaitGameplayEvent` `0x558`, `Period: float` `0x560`, `ActivationCost` `0x568` |
| `GA_Charges_Default_C` | `GA_ChargesBase_C` | `AdditiveMontage` |
| `GA_Charges_Gragu_C` | `GA_ChargesBase_C` | `AdditiveMontage` |
| `GA_Charges_Cleansing_C`, `GA_Charges_Mixed_C` | `GA_ChargesBase_C` | none |
| `GA_ChargesHandler_C` | `GA_SpartaAbility_C` | `CachedCharacter`, `CachedHealthComponent` |
| `GA_ChargesHandler_Cleansing_C` | `GA_ChargesHandler_C` | none |
| `GA_ChargesHandler_Gragu_C` | `GA_ChargesHandler_C` | `FillAmountDelta: double` `0x530`, `CausedDeathWaitTask` / `AddChargeWaitTask: AbilityTask_WaitGameplayEvent` `0x538` / `0x540` |
| `GA_Death_Player_C` | `GA_DeathBase_C` | `DeathMontages: ArrayProperty` `0x5E0`, `WBP_DeathScreen: ObjectProperty of UMG.UserWidget` `0x5F0`, `PlayMontage: bool` `0x5F8`, `MyEventData: GameplayEventData` `0x600`, `MyCommittedFromEvent: bool` `0x6B0`, `bDroppedWeapons: bool` `0x6B1`, `DeathScreenWidgetClass: SoftClassProperty` `0x6B8`, `CachedScreenWidgetClass: ClassProperty` `0x6E0` |
| `GA_PreventDeathHandler_C` | `GA_SpartaAbility_C` | `DeathPreventionEffects: MapProperty` `0x518` |

The heal-charge count itself is an **attribute**, not ability state:
`UPlayerAttributeSet:HealCurrentCharges` / `:HealMaxCharges` / `:HealAmountPerCharge` (sec.2.3). A co-op
layer that mirrors those three attributes gets healing right without touching the abilities.


Evidence -- dodge/block/harden/parry/plunging/movement class rows:
```
[000001F4CBA43160] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_ForceSprint.GA_ForceSprint_C [n: 1147E3] [c: 00007FF46EEC3D38] [or: 000001F4D077CF90] [sps: 000001F4DC018E00]
[000001F4DC42B3E0] GA_ForceSprint_C /Game/Sparta/Core/Player/Ability/GA_ForceSprint.Default__GA_ForceSprint_C [n: 65C107] [c: 000001F4CBA43160] [or: 000001F4D077CF90]
[000001F4CBA4DC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_PreventDeathHandler.GA_PreventDeathHandler_C [n: 1ECA67] [c: 00007FF46EEC3D38] [or: 000001F4D0157AE0] [sps: 000001F4DC018E00]
[000001F4DC6A8DD0] GA_PreventDeathHandler_C /Game/Sparta/Core/Player/Ability/GA_PreventDeathHandler.Default__GA_PreventDeathHandler_C [n: 659848] [c: 000001F4CBA4DC60] [or: 000001F4D0157AE0]
[000001F617FABFE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_CharacterSkinHandler.GA_CharacterSkinHandler_C [n: 961FD] [c: 00007FF46EEC3D38] [or: 000001F4D1C1AC40] [sps: 000001F4DC018E00]
[000001F618895CE0] GA_CharacterSkinHandler_C /Game/Sparta/Core/Player/Ability/GA_CharacterSkinHandler.Default__GA_CharacterSkinHandler_C [n: 672075] [c: 000001F617FABFE0] [or: 000001F4D1C1AC40]
[000001F619275170] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Walk.GA_Walk_C [n: 18CD55] [c: 00007FF46EEC3D38] [or: 000001F4D1C1AD30] [sps: 000001F615243160]
[000001F617EB47F0] GA_Walk_C /Game/Sparta/Core/Player/Ability/GA_Walk.Default__GA_Walk_C [n: 671FBB] [c: 000001F619275170] [or: 000001F4D1C1AD30]
[000001F619274330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_ToggleLockOn.GA_ToggleLockOn_C [n: 1DAC16] [c: 00007FF46EEC3D38] [or: 000001F4D1C19160] [sps: 000001F615243160]
[000001F617EB3C10] GA_ToggleLockOn_C /Game/Sparta/Core/Player/Ability/GA_ToggleLockOn.Default__GA_ToggleLockOn_C [n: 66EDC2] [c: 000001F619274330] [or: 000001F4D1C19160]
[000001F619272A40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Player_Spawn.GA_Player_Spawn_C [n: 1A2AE6] [c: 00007FF46EEC3D38] [or: 000001F4CEED33F0] [sps: 000001F4DC018E00]
[000001F61792EFB0] GA_Player_Spawn_C /Game/Sparta/Core/Player/Ability/GA_Player_Spawn.Default__GA_Player_Spawn_C [n: 657EEF] [c: 000001F619272A40] [or: 000001F4CEED33F0]
[000001F619270A30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_BugIt.GA_BugIt_C [n: 331F3F] [c: 00007FF46EEC3D38] [or: 000001F4D1C1AE20] [sps: 000001F615243160]
[000001F617EB3030] GA_BugIt_C /Game/Sparta/Core/Player/Ability/GA_BugIt.Default__GA_BugIt_C [n: 67189A] [c: 000001F619270A30] [or: 000001F4D1C1AE20]
[000001F619886340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_BugItGo.GA_BugItGo_C [n: 2F93A0] [c: 00007FF46EEC3D38] [or: 000001F4D1C19250] [sps: 000001F615243160]
[000001F6188CD8A0] GA_BugItGo_C /Game/Sparta/Core/Player/Ability/GA_BugItGo.Default__GA_BugItGo_C [n: 66E731] [c: 000001F619886340] [or: 000001F4D1C19250]
[000001F61A4E9C40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Charges/GA_ChargesHandler.GA_ChargesHandler_C [n: 43B0E2] [c: 00007FF46EEC3D38] [or: 000001F4CEED0790] [sps: 000001F4DC018E00]
[000001F61A2C2BF0] GA_ChargesHandler_C /Game/Sparta/Core/Player/Ability/Charges/GA_ChargesHandler.Default__GA_ChargesHandler_C [n: 65298F] [c: 000001F61A4E9C40] [or: 000001F4CEED0790]
[000001F61A5B5C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Charges/GA_ChargesBase.GA_ChargesBase_C [n: 2A2755] [c: 00007FF46EEC3D38] [or: 000001F4D1C173B0] [sps: 000001F4DC018E00]
[000001F4C37DAE70] GA_ChargesBase_C /Game/Sparta/Core/Player/Ability/Charges/GA_ChargesBase.Default__GA_ChargesBase_C [n: 6678B6] [c: 000001F61A5B5C20] [or: 000001F4D1C173B0]
[000001F61A5B5170] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Default.GA_Charges_Default_C [n: C6837] [c: 00007FF46EEC3D38] [or: 000001F4D1C172C0] [sps: 000001F61A5B5C20]
[000001F61A2C1630] GA_Charges_Default_C /Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Default.Default__GA_Charges_Default_C [n: 6679B4] [c: 000001F61A5B5170] [or: 000001F4D1C172C0]
[000001F614CC0A30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Sprint.GA_Sprint_C [n: 466970] [c: 00007FF46EEC3D38] [or: 000001F4D1C19700] [sps: 000001F615243160]
[000001F61D358E00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_SwitchLockOnTarget.GA_SwitchLockOnTarget_C [n: 3FC81A] [c: 00007FF46EEC3D38] [or: 000001F4D1C17A90] [sps: 000001F615243160]
[000001F61A31D670] GA_SwitchLockOnTarget_C /Game/Sparta/Core/Player/Ability/GA_SwitchLockOnTarget.Default__GA_SwitchLockOnTarget_C [n: 664D22] [c: 000001F61D358E00] [or: 000001F4D1C17A90]
[000001F61A2DD8A0] GA_Sprint_C /Game/Sparta/Core/Player/Ability/GA_Sprint.Default__GA_Sprint_C [n: 66DA9B] [c: 000001F614CC0A30] [or: 000001F4D1C19700]
[000001F61D753FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Aiming_Handler.GA_Aiming_Handler_C [n: 346A85] [c: 00007FF46EEC3D38] [or: 000001F4D1C18CB0] [sps: 000001F4DC018E00]
[000001F61D753C10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C [n: 84B0D] [c: 00007FF46EEC3D38] [or: 000001F4D1C19430] [sps: 000001F615241870]
[000001F4C3267C60] GA_Death_Player_C /Game/Sparta/Core/Player/Ability/GA_Death_Player.Default__GA_Death_Player_C [n: 66E52C] [c: 000001F61D753C10] [or: 000001F4D1C19430]
[000001F61FDE2A40] GA_Aiming_Handler_C /Game/Sparta/Core/Player/Ability/Input/GA_Aiming_Handler.Default__GA_Aiming_Handler_C [n: 66A888] [c: 000001F61D753FA0] [or: 000001F4D1C18CB0]
[000001F80D7AB8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Sprint_Sheep.GA_Sprint_Sheep_C [n: 88F2E] [c: 00007FF46EEC3D38] [or: 000001F80D67C590] [sps: 000001F614CC0A30]
[000001F80C40D8A0] GA_Sprint_Sheep_C /Game/Sparta/Core/Player/Ability/GA_Sprint_Sheep.Default__GA_Sprint_Sheep_C [n: 76C27E] [c: 000001F80D7AB8C0] [or: 000001F80D67C590]
[000001F8B679DC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_IdleWhileStreaming.GA_IdleWhileStreaming_C [n: 265E0C] [c: 00007FF46EEC3D38] [or: 000001F889F93C10] [sps: 000001F615242DD0]
[000001F8B8A920E0] GA_IdleWhileStreaming_C /Game/Sparta/Core/Player/Ability/GA_IdleWhileStreaming.Default__GA_IdleWhileStreaming_C [n: 80F40F] [c: 000001F8B679DC60] [or: 000001F889F93C10]
[000001F8CA5EBC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Channeling.GA_Charges_Channeling_C [n: 1B887A] [c: 00007FF46EEC3D38] [or: 000001F634EDC310] [sps: 000001F61A5B5C20]
[000001F8D2245CE0] GA_Charges_Channeling_C /Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Channeling.Default__GA_Charges_Channeling_C [n: 84646D] [c: 000001F8CA5EBC50] [or: 000001F634EDC310]
[000001F8CA5EB1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Cleansing.GA_Charges_Cleansing_C [n: 27BA9D] [c: 00007FF46EEC3D38] [or: 000001F634EDC130] [sps: 000001F61A5B5C20]
[000001F8D221C430] GA_Charges_Cleansing_C /Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Cleansing.Default__GA_Charges_Cleansing_C [n: 846492] [c: 000001F8CA5EB1A0] [or: 000001F634EDC130]
[000001F8CA5EAA80] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Gragu.GA_Charges_Gragu_C [n: 19E7AC] [c: 00007FF46EEC3D38] [or: 000001F634EDC040] [sps: 000001F61A5B5C20]
[000001F8D2212110] GA_Charges_Gragu_C /Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Gragu.Default__GA_Charges_Gragu_C [n: 846563] [c: 000001F8CA5EAA80] [or: 000001F634EDC040]
[000001F8CA5EA360] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Mixed.GA_Charges_Mixed_C [n: 346A62] [c: 00007FF46EEC3D38] [or: 000001F634EDBF00] [sps: 000001F61A5B5C20]
[000001F8D2222BF0] GA_Charges_Mixed_C /Game/Sparta/Core/Player/Ability/Charges/GA_Charges_Mixed.Default__GA_Charges_Mixed_C [n: 846584] [c: 000001F8CA5EA360] [or: 000001F634EDBF00]
[000001F8CA5E9C40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Charges/GA_ChargesHandler_Cleansing.GA_ChargesHandler_Cleansing_C [n: 28485F] [c: 00007FF46EEC3D38] [or: 000001F634EDBE10] [sps: 000001F61A4E9C40]
[000001F8D222AE70] GA_ChargesHandler_Cleansing_C /Game/Sparta/Core/Player/Ability/Charges/GA_ChargesHandler_Cleansing.Default__GA_ChargesHandler_Cleansing_C [n: 846593] [c: 000001F8CA5E9C40] [or: 000001F634EDBE10]
[000001F8CA5E9520] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Charges/GA_ChargesHandler_Gragu.GA_ChargesHandler_Gragu_C [n: 302A1F] [c: 00007FF46EEC3D38] [or: 000001F634EDBDC0] [sps: 000001F61A4E9C40]
[000001F841DD9340] GA_ChargesHandler_Gragu_C /Game/Sparta/Core/Player/Ability/Charges/GA_ChargesHandler_Gragu.Default__GA_ChargesHandler_Gragu_C [n: 84668A] [c: 000001F8CA5E9520] [or: 000001F634EDBDC0]
```

Evidence -- dodge/roll/parry class rows (a second, tighter query; `Default__` CDO rows excluded):
```
[000001F617F178A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Handler.GA_Parry_Handler_C [n: 43B134] [c: 00007FF46EEC3D38] [or: 000001F4D1C18DF0] [sps: 000001F4DC018E00]
[000001F617F1F8E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Successful.GA_Parry_Successful_C [n: 3C7383] [c: 00007FF46EEC3D38] [or: 000001F4D1C18F30] [sps: 000001F615242DD0]
[000001F617F1EE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Action_Single.GA_Parry_Action_Single_C [n: 9A896] [c: 00007FF46EEC3D38] [or: 000001F4D1C18EE0] [sps: 000001F615242DD0]
[000001F617FA66D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Successful_Projectile_Deflect.GA_Parry_Successful_Projectile_Deflect_C [n: 4AC5B0] [c: 00007FF46EEC3D38] [or: 000001F4D1C18E40] [sps: 000001F617F1F8E0]
[000001F617FA3FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Riposte.GA_Parry_Riposte_C [n: D777C] [c: 00007FF46EEC3D38] [or: 000001F4D1C1AEC0] [sps: 000001F4DC018E00]
[000001F61A5BB8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_DashBase.GA_DashBase_C [n: 2AFBD5] [c: 00007FF46EEC3D38] [or: 000001F4D1C19750] [sps: 000001F4DC018E00]
[000001F61A5BB530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_StrongOne.GA_Dash_StrongOne_C [n: 400E32] [c: 00007FF46EEC3D38] [or: 000001F4D1C19930] [sps: 000001F61A5BB8C0]
[000001F61D35F8E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Plunging/GA_Player_Attack_Plunging.GA_Player_Attack_Plunging_C [n: 20F4B0] [c: 00007FF46EEC3D38] [or: 000001F4D1C15CE0] [sps: 000001F4BBD2F550]
[000001F76C159190] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Tiel.GA_Dash_Tiel_C [n: 3D827A] [c: 00007FF46EEC3D38] [or: 000001F76386BAF0] [sps: 000001F61A5BB8C0]
[000001F80D7AEAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Sheep.GA_Dash_Sheep_C [n: 10278A] [c: 00007FF46EEC3D38] [or: 000001F80D67C860] [sps: 000001F61A5BB8C0]
[000001F891DF1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Necrophage.GA_Dash_Necrophage_C [n: 48D87A] [c: 00007FF46EEC3D38] [or: 000001F88CA17180] [sps: 000001F61A5BB8C0]
[000001F8B589F1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Roll/GA_RollBase.GA_RollBase_C [n: 6A5E4] [c: 00007FF46EEC3D38] [or: 000001F8B5933260] [sps: 000001F4DC018E00]
[000001F8B5A57510] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Roll/GA_Roll_Smert.GA_Roll_Smert_C [n: 454CF9] [c: 00007FF46EEC3D38] [or: 000001F8B59332B0] [sps: 000001F8B589F1C0]
[000001F8CB63CE20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Eredrim.GA_Dash_Eredrim_C [n: 6A55D] [c: 00007FF46EEC3D38] [or: 000001F8CAEDFF10] [sps: 000001F61A5BB8C0]
[000001F8CB63B8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Genessa.GA_Dash_Genessa_C [n: 3774D9] [c: 00007FF46EEC3D38] [or: 000001F8CAEDF060] [sps: 000001F61A5BB8C0]
[000001F8CB63A360] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Harros.GA_Dash_Harros_C [n: 176FF6] [c: 00007FF46EEC3D38] [or: 000001F7ED270970] [sps: 000001F61A5BB8C0]
[000001F7ED2C7510] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Smert.GA_Dash_Smert_C [n: 21FC77] [c: 00007FF46EEC3D38] [or: 000001F7ED270AB0] [sps: 000001F61A5BB8C0]
[000001F8BC46A360] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Sariel.GA_Dash_Sariel_C [n: 2502F4] [c: 00007FF46EEC3D38] [or: 000001F7ED271BE0] [sps: 000001F61A5BB8C0]
[000001F8CEEEA6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Gragu.GA_Dash_Gragu_C [n: 30FB0A] [c: 00007FF46EEC3D38] [or: 000001F8CF1B83F0] [sps: 000001F61A5BB8C0]
[000001F8C61FC370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_Proxima.GA_Dash_Proxima_C [n: 1147D2] [c: 00007FF46EEC3D38] [or: 000001F8C62B1780] [sps: 000001F61A5BB8C0]
[000001F8CA5E86E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Dash/GA_Dash_CorruptedGenessa.GA_Dash_CorruptedGenessa_C [n: 12E2B5] [c: 00007FF46EEC3D38] [or: 000001F634EDBC30] [sps: 000001F61A5BB8C0]
```

Evidence -- dodge/block/harden/parry state properties:
```
[000001F617E28180] BoolProperty /Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/GA_Harden_Original.GA_Harden_Original_C:bInStoneForm [o: 518] [n: 641B6E] [c: 00007FF721DE8D80] [owr: 000001F617BE9190]
[000001F617E13110] FloatProperty /Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/GA_Harden_Original.GA_Harden_Original_C:StoneFormCooldown [o: 570] [n: 672898] [c: 00007FF721DE9380] [owr: 000001F617BE9190]
[000001F617E27E80] StructProperty /Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/GA_Harden_Original.GA_Harden_Original_C:GE_Harden [o: 598] [n: 672BB6] [c: 00007FF721DF9890] [owr: 000001F617BE9190] [ss: 00007FF46F389C70]
[000001F61880D000] ObjectProperty /Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Riposte.GA_Parry_Riposte_C:Victim [o: 578] [n: 524FDF] [c: 00007FF721DE9420] [owr: 000001F617FA3FA0] [pc: 00007FF46EE52028]
[000001F61880CF80] StructProperty /Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Riposte.GA_Parry_Riposte_C:RipostePayload [o: 580] [n: 6716E6] [c: 00007FF721DF9890] [owr: 000001F617FA3FA0] [ss: 00007FF46F489AB8]
[000001F61880CE80] ObjectProperty /Game/Sparta/Core/Player/Ability/Parry/GA_Parry_Riposte.GA_Parry_Riposte_C:ParticipantSelectedMontage [o: 590] [n: 671705] [c: 00007FF721DE9420] [owr: 000001F617FA3FA0] [pc: 00007FF46EF2E338]
[000001F61A638890] FloatProperty /Game/Sparta/Core/Player/Ability/Dash/GA_DashBase.GA_DashBase_C:InvulnerabilityDuration [o: 53C] [n: 66D607] [c: 00007FF721DE9380] [owr: 000001F61A5BB8C0]
[000001F61A65FA00] StructProperty /Game/Sparta/Core/Player/Ability/Dash/GA_DashBase.GA_DashBase_C:GE_Invulnerability [o: 588] [n: 106E27] [c: 00007FF721DF9890] [owr: 000001F61A5BB8C0] [ss: 00007FF46F389C70]
[000001F61A65F900] StructProperty /Game/Sparta/Core/Player/Ability/Dash/GA_DashBase.GA_DashBase_C:DefaultDashMontages [o: 5A8] [n: 66D57E] [c: 00007FF721DF9890] [owr: 000001F61A5BB8C0] [ss: 00007FF46F46AAD8]
[000001F4C3AD23F0] FloatProperty /Game/Sparta/Core/Player/Ability/Plunging/GA_Player_Attack_Plunging.GA_Player_Attack_Plunging_C:WarpDistanceZ [o: 8F4] [n: 65EA07] [c: 00007FF721DE9380] [owr: 000001F61D35F8E0]
[000001F61D72D380] ObjectProperty /Game/Sparta/Core/Player/Ability/Plunging/GA_Player_Attack_Plunging.GA_Player_Attack_Plunging_C:LandingMontage [o: 940] [n: 65E800] [c: 00007FF721DE9420] [owr: 000001F61D35F8E0] [pc: 00007FF46EF2E338]
[000001F8B0F32100] ObjectProperty /Game/Sparta/Core/Characters/Player/Common/Abilities/ActiveBlock/GA_ActiveBlock.GA_ActiveBlock_C:BlockLoopMontage [o: 5A0] [n: 7FCA6B] [c: 00007FF721DE9420] [owr: 000001F8B0F17510] [pc: 00007FF46EF2E338]
[000001F8B0E43E30] DoubleProperty /Game/Sparta/Core/Characters/Player/Common/Abilities/ActiveBlock/GA_ActiveBlock.GA_ActiveBlock_C:ActivationTime [o: 6F8] [n: 55E2B5] [c: 00007FF721DE93D0] [owr: 000001F8B0F17510]
[000001F8B0E43DC0] DoubleProperty /Game/Sparta/Core/Characters/Player/Common/Abilities/ActiveBlock/GA_ActiveBlock.GA_ActiveBlock_C:RequiredActivationTime [o: 700] [n: 7FCD73] [c: 00007FF721DE93D0] [owr: 000001F8B0F17510]
[000001F8B576AFF0] FloatProperty /Game/Sparta/Core/Player/Ability/Roll/GA_RollBase.GA_RollBase_C:InvulnerabilityDuration [o: 53C] [n: 66D607] [c: 00007FF721DE9380] [owr: 000001F8B589F1C0]
[000001F8B58CEF80] StructProperty /Game/Sparta/Core/Player/Ability/Roll/GA_RollBase.GA_RollBase_C:DefaultRollMontages [o: 5A8] [n: 7FF66E] [c: 00007FF721DF9890] [owr: 000001F8B589F1C0] [ss: 00007FF46F46AAD8]
```

---

## 8. The input-handler abilities (`GA_Action_*`, `GA_Attack_*`, `GA_Sidearm_*`)

Every one of these is a **thin `USpartaGameplayAbility` subclass with no properties**: it exists only to
be the ability that is granted against one input tag, and its Blueprint graph turns
`OnInputPressed`/`OnInputReleased` into the real action (activating the state ability, changing camera,
setting tags). Trigger mechanism per sec.3.1/sec.1.3: `FSpartaAbilitySet_GameplayAbility.InputTag` ->
`USpartaInputComponent` -> `USpartaGameplayAbility::OnInputPressed/OnInputReleased`.

### 8.1 Enumeration -- `/Game/Sparta/Core/Player/Ability/Input/`

| Class | Parent | Own props |
|---|---|---|
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Active_Hold.GA_Action_Ability_Active_Hold_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Active_Press.GA_Action_Ability_Active_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Active_Release.GA_Action_Ability_Active_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Shell_Hold.GA_Action_Ability_Shell_Hold_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Shell_Press.GA_Action_Ability_Shell_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Shell_Press_Unblocked.GA_Action_Ability_Shell_Press_Unblocked_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Shell_Release.GA_Action_Ability_Shell_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Shell_Release_Unblocked.GA_Action_Ability_Shell_Release_Unblocked_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_SlayerPunch_Hold.GA_Action_Ability_SlayerPunch_Hold_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_SlayerPunch_Press.GA_Action_Ability_SlayerPunch_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_SlayerPunch_Release.GA_Action_Ability_SlayerPunch_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Weapon_Press.GA_Action_Ability_Weapon_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Weapon_Release.GA_Action_Ability_Weapon_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_ActiveBlock_Hold.GA_Action_ActiveBlock_Hold_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | see 8.2 |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_ActiveBlock_Press.GA_Action_ActiveBlock_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_ActiveBlock_Release.GA_Action_ActiveBlock_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Hold.GA_Action_Aiming_Hold_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | see 8.2 |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Hold_Unblocked.GA_Action_Aiming_Hold_Unblocked_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Press.GA_Action_Aiming_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Press_Unblocked.GA_Action_Aiming_Press_Unblocked_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Release.GA_Action_Aiming_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Release_Unblocked.GA_Action_Aiming_Release_Unblocked_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_ChangeTrack.GA_Action_ChangeTrack_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Charges_Press.GA_Action_Charges_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Charges_Release.GA_Action_Charges_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Dash.GA_Action_Dash_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Parry_Press.GA_Action_Parry_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Parry_Release.GA_Action_Parry_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Play.GA_Action_Play_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_SitDown.GA_Action_SitDown_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_SpecialAbility_Shell_Press.GA_Action_SpecialAbility_Shell_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_SpecialAbility_Shell_Release.GA_Action_SpecialAbility_Shell_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Sprint_Press.GA_Action_Sprint_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Sprint_Release.GA_Action_Sprint_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Sprint_Toggle.GA_Action_Sprint_Toggle_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_StoneForm_Press.GA_Action_StoneForm_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_StoneForm_Release.GA_Action_StoneForm_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_UseItem.GA_Action_UseItem_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Walk_Press.GA_Action_Walk_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Action_Walk_Release.GA_Action_Walk_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Aiming_Handler.GA_Aiming_Handler_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | see 8.2 |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Attack_Primary.GA_Attack_Primary_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Attack_Primary_Release.GA_Attack_Primary_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Attack_Primary_Unblocked.GA_Attack_Primary_Unblocked_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Attack_Secondary.GA_Attack_Secondary_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Attack_Secondary_Release.GA_Attack_Secondary_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Attack_Secondary_Unblocked.GA_Attack_Secondary_Unblocked_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Sidearm_Primary_Press.GA_Sidearm_Primary_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Sidearm_Primary_Release.GA_Sidearm_Primary_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Sidearm_Secondary_Press.GA_Sidearm_Secondary_Press_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_Sidearm_Secondary_Release.GA_Sidearm_Secondary_Release_C` | `/Game/Sparta/Core/Ability/GA_SpartaAbility.GA_SpartaAbility_C` | none |
| `/Game/Sparta/Core/Player/Ability/Input/GA_WaitOnHoldInput.GA_WaitOnHoldInput_C` | `/Game/Sparta/Core/Ability/GA_PlayMontageBase.GA_PlayMontageBase_C` | none |

### 8.2 The only input handlers with own state

| Class | Property | Type | Offset | Meaning |
|---|---|---|---|---|
| `GA_Action_ActiveBlock_Hold_C` | `bRequiredInputRetrigger` | `BoolProperty` | `0x518` | block must be re-pressed to re-enter |
| `GA_Action_Aiming_Hold_C` | `bRequiredInputRetrigger` | `BoolProperty` | `0x518` | same for aiming |

(`GA_Aiming_Handler_C` -- 10 properties -- is documented in sec.7.7; it is the *state* ability that the
`GA_Action_Aiming_*` handlers drive.)

### 8.3 What each handler family does (read off the class names; all of them are
`GA_SpartaAbility_C` children unless stated)

| Family | Classes | Meaning |
|---|---|---|
| `GA_Action_Ability_{Active,Shell}_{Press,Hold,Release}[_Unblocked]` | 8 | shell/active ability on the 4 input phases |
| `GA_Action_SpecialAbility_Shell_{Press,Release}` | 2 | the shell's "special" (ultimate) input |
| `GA_Action_Ability_SlayerPunch_{Press,Hold,Release}` | 3 | Smert's punch input |
| `GA_Action_Ability_Weapon_{Press,Release}` | 2 | weapon ability input |
| `GA_Action_ActiveBlock_{Press,Hold,Release}` | 3 | blocking (`GA_ActiveBlock_C` is the state ability) |
| `GA_Action_Aiming_{Press,Hold,Release}[_Unblocked]` | 6 | aiming / sidearm aim |
| `GA_Action_StoneForm_{Press,Release}` | 2 | harden (stone form) input |
| `GA_Action_Parry_{Press,Release}` | 2 | parry input |
| `GA_Action_Charges_{Press,Release}` | 2 | heal-charge input |
| `GA_Action_Sprint_{Press,Release,Toggle}` | 3 | sprint, including the toggle mode (`GA_Walk_C` uses `IsWalkingInputToggled`) |
| `GA_Action_Walk_{Press,Release}` | 2 | walk |
| `GA_Action_{Dash,UseItem,SitDown,Play,ChangeTrack}` | 5 | single-shot actions |
| `GA_Attack_Primary[_Release][_Unblocked]`, `GA_Attack_Secondary[_Release][_Unblocked]` | 6 | the melee attack inputs (drive the weapon attack selectors of sec.6) |
| `GA_Sidearm_{Primary,Secondary}_{Press,Release}` | 4 | sidearm fire inputs (drive sec.9) |
| `GA_WaitOnHoldInput_C` | 1 | parent `GA_PlayMontageBase_C`; waits for a hold input inside a montage |

The `_Unblocked` suffix marks the copy that is granted when the normal one is blocked -- i.e.
`USpartaAbilitySystemComponent.BlockInputGameplayEffect` / `UnblockableInputBlockTags` (sec.1.3) decide
which of the pair is live. A sync layer that feels `OnInputQueued` should replicate the *tag*, not the
handler class, and let each client pick its own handler.

Evidence -- input handlers:
```
[000001F4DC017C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Charges_Press.GA_Action_Charges_Press_C [n: 1AB590] [c: 00007FF46EEC3D38] [or: 000001F4CEED7C20] [sps: 000001F4DC018E00]
[000001F4DC01FC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Release.GA_Action_Aiming_Release_C [n: 20F482] [c: 00007FF46EEC3D38] [or: 000001F4CEEDF1A0] [sps: 000001F4DC018E00]
[000001F4DC01F550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Shell_Press.GA_Action_Ability_Shell_Press_C [n: 4897E7] [c: 00007FF46EEC3D38] [or: 000001F4CEEDF150] [sps: 000001F4DC018E00]
[000001F4CBA46DF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Sidearm_Secondary_Release.GA_Sidearm_Secondary_Release_C [n: 412E3D] [c: 00007FF46EEC3D38] [or: 000001F4D01569B0] [sps: 000001F4DC018E00]
[000001F4CBA466D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Shell_Hold.GA_Action_Ability_Shell_Hold_C [n: 3EA852] [c: 00007FF46EEC3D38] [or: 000001F4D015DEE0] [sps: 000001F4DC018E00]
[000001F4CBA45FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Hold.GA_Action_Aiming_Hold_C [n: 358857] [c: 00007FF46EEC3D38] [or: 000001F4D015DC10] [sps: 000001F4DC018E00]
[000001F4CBA45500] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Attack_Secondary.GA_Attack_Secondary_C [n: CAA0B] [c: 00007FF46EEC3D38] [or: 000001F4D0773C60] [sps: 000001F4DC018E00]
[000001F4CBA44DE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Attack_Primary_Release.GA_Attack_Primary_Release_C [n: 27FFA2] [c: 00007FF46EEC3D38] [or: 000001F4D0773C10] [sps: 000001F4DC018E00]
[000001F4CBA446C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Attack_Secondary_Unblocked.GA_Attack_Secondary_Unblocked_C [n: 41BE38] [c: 00007FF46EEC3D38] [or: 000001F4D0773A30] [sps: 000001F4DC018E00]
[000001F4CBA43FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Press.GA_Action_Aiming_Press_C [n: 2C0FD0] [c: 00007FF46EEC3D38] [or: 000001F4D07739E0] [sps: 000001F4DC018E00]
[000001F4CBA43880] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Shell_Press_Unblocked.GA_Action_Ability_Shell_Press_Unblocked_C [n: 1B8887] [c: 00007FF46EEC3D38] [or: 000001F4D0773710] [sps: 000001F4DC018E00]
[000001F4CBA41F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Parry_Press.GA_Action_Parry_Press_C [n: 28D26A] [c: 00007FF46EEC3D38] [or: 000001F4CFD226D0] [sps: 000001F4DC018E00]
[000001F4CBA41870] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Active_Press.GA_Action_Ability_Active_Press_C [n: B4FBF] [c: 00007FF46EEC3D38] [or: 000001F4CFD22C70] [sps: 000001F4DC018E00]
[000001F4CBA406A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Play.GA_Action_Play_C [n: 3E1266] [c: 00007FF46EEC3D38] [or: 000001F4A70834E0] [sps: 000001F4DC018E00]
[000001F4CBA4EAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Attack_Primary.GA_Attack_Primary_C [n: 492251] [c: 00007FF46EEC3D38] [or: 000001F4A7084700] [sps: 000001F4DC018E00]
[000001F4CBA4D1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Parry_Release.GA_Action_Parry_Release_C [n: 102793] [c: 00007FF46EEC3D38] [or: 000001F4D31FBB90] [sps: 000001F4DC018E00]
[000001F4CBA4CA90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Charges_Release.GA_Action_Charges_Release_C [n: 3EA862] [c: 00007FF46EEC3D38] [or: 000001F4D31FC9A0] [sps: 000001F4DC018E00]
[000001F4CBA4C370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Shell_Release.GA_Action_Ability_Shell_Release_C [n: 45DB84] [c: 00007FF46EEC3D38] [or: 000001F4D31FC900] [sps: 000001F4DC018E00]
[000001F4CBA4BC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Attack_Secondary_Release.GA_Attack_Secondary_Release_C [n: 9A7B0] [c: 00007FF46EEC3D38] [or: 000001F4D31FC8B0] [sps: 000001F4DC018E00]
[000001F4CBA4B530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Sidearm_Primary_Release.GA_Sidearm_Primary_Release_C [n: 405B8C] [c: 00007FF46EEC3D38] [or: 000001F4D31FC860] [sps: 000001F4DC018E00]
[000001F4CBA49FD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Active_Release.GA_Action_Ability_Active_Release_C [n: 4BD938] [c: 00007FF46EEC3D38] [or: 000001F4D31FC7C0] [sps: 000001F4DC018E00]
[000001F4CBA498B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Sprint_Release.GA_Action_Sprint_Release_C [n: CA9FD] [c: 00007FF46EEC3D38] [or: 000001F4D31FC770] [sps: 000001F4DC018E00]
[000001F4CBA49190] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Walk_Press.GA_Action_Walk_Press_C [n: 3E126F] [c: 00007FF46EEC3D38] [or: 000001F4D31FC720] [sps: 000001F4DC018E00]
[000001F4CBA48A70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Hold_Unblocked.GA_Action_Aiming_Hold_Unblocked_C [n: 3B9834] [c: 00007FF46EEC3D38] [or: 000001F4D31FC6D0] [sps: 000001F4DC018E00]
[000001F4CBA48350] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Weapon_Release.GA_Action_Ability_Weapon_Release_C [n: 2C589D] [c: 00007FF46EEC3D38] [or: 000001F4D31FC680] [sps: 000001F4DC018E00]
[000001F4CBA47C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_ChangeTrack.GA_Action_ChangeTrack_C [n: 36EBD4] [c: 00007FF46EEC3D38] [or: 000001F4D31FC630] [sps: 000001F4DC018E00]
[000001F4CBA4FC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Release_Unblocked.GA_Action_Aiming_Release_Unblocked_C [n: 46FA3E] [c: 00007FF46EEC3D38] [or: 000001F4D31FC5E0] [sps: 000001F4DC018E00]
[000001F4CBA4F550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Weapon_Press.GA_Action_Ability_Weapon_Press_C [n: 6A567] [c: 00007FF46EEC3D38] [or: 000001F4D31FC590] [sps: 000001F4DC018E00]
[000001F4CBA4EE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Walk_Release.GA_Action_Walk_Release_C [n: 72E14] [c: 00007FF46EEC3D38] [or: 000001F4D31FC540] [sps: 000001F4DC018E00]
[000001F4DCBA7180] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Attack_Primary_Unblocked.GA_Attack_Primary_Unblocked_C [n: 417221] [c: 00007FF46EEC3D38] [or: 000001F4D31FC4F0] [sps: 000001F4DC018E00]
[000001F4DCBA6A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_SitDown.GA_Action_SitDown_C [n: B97A1] [c: 00007FF46EEC3D38] [or: 000001F4D31FC4A0] [sps: 000001F4DC018E00]
[000001F4DCBA6340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Sprint_Toggle.GA_Action_Sprint_Toggle_C [n: D3077] [c: 00007FF46EEC3D38] [or: 000001F4D31FC450] [sps: 000001F4DC018E00]
[000001F4DCBA5C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Sidearm_Secondary_Press.GA_Sidearm_Secondary_Press_C [n: 2AFBED] [c: 00007FF46EEC3D38] [or: 000001F4D31FD800] [sps: 000001F4DC018E00]
[000001F4DCBA5500] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_SpecialAbility_Shell_Release.GA_Action_SpecialAbility_Shell_Release_C [n: 47400B] [c: 00007FF46EEC3D38] [or: 000001F4D31FD7B0] [sps: 000001F4DC018E00]
[000001F4DCBA4DE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Shell_Release_Unblocked.GA_Action_Ability_Shell_Release_Unblocked_C [n: 2025AA] [c: 00007FF46EEC3D38] [or: 000001F4D31FD580] [sps: 000001F4DC018E00]
[000001F4DCBA46C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Sidearm_Primary_Press.GA_Sidearm_Primary_Press_C [n: 466977] [c: 00007FF46EEC3D38] [or: 000001F4D33C2F90] [sps: 000001F4DC018E00]
[000001F4DCBA26B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_Active_Hold.GA_Action_Ability_Active_Hold_C [n: 4623D8] [c: 00007FF46EEC3D38] [or: 000001F4D33C2D60] [sps: 000001F4DC018E00]
[000001F4DCBA06A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_UseItem.GA_Action_UseItem_C [n: 27FF97] [c: 00007FF46EEC3D38] [or: 000001F4D33C8A80] [sps: 000001F4DC018E00]
[000001F4DCBAEAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Dash.GA_Action_Dash_C [n: 1D64EB] [c: 00007FF46EEC3D38] [or: 000001F4D33C8A30] [sps: 000001F4DC018E00]
[000001F4DCBAE380] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Aiming_Press_Unblocked.GA_Action_Aiming_Press_Unblocked_C [n: 9A79E] [c: 00007FF46EEC3D38] [or: 000001F4D33C89E0] [sps: 000001F4DC018E00]
[000001F4DCBADC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Sprint_Press.GA_Action_Sprint_Press_C [n: 2D6A1A] [c: 00007FF46EEC3D38] [or: 000001F4D33C8990] [sps: 000001F4DC018E00]
[000001F4DCBAC700] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_SpecialAbility_Shell_Press.GA_Action_SpecialAbility_Shell_Press_C [n: 22CFDB] [c: 00007FF46EEC3D38] [or: 000001F4D33C8670] [sps: 000001F4DC018E00]
[000001F61D753FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Aiming_Handler.GA_Aiming_Handler_C [n: 346A85] [c: 00007FF46EEC3D38] [or: 000001F4D1C18CB0] [sps: 000001F4DC018E00]
[000001F8CA0BF8E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_WaitOnHoldInput.GA_WaitOnHoldInput_C [n: 42925D] [c: 00007FF46EEC3D38] [or: 000001F634ED8BC0] [sps: 000001F615242DD0]
[000001F8D73E6A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_SlayerPunch_Hold.GA_Action_Ability_SlayerPunch_Hold_C [n: 21FC80] [c: 00007FF46EEC3D38] [or: 000001F634EDCC70] [sps: 000001F4DC018E00]
[000001F8D73E6340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_SlayerPunch_Press.GA_Action_Ability_SlayerPunch_Press_C [n: 1A71DE] [c: 00007FF46EEC3D38] [or: 000001F634EDCC20] [sps: 000001F4DC018E00]
[000001F8D73E5C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_Ability_SlayerPunch_Release.GA_Action_Ability_SlayerPunch_Release_C [n: 21FC93] [c: 00007FF46EEC3D38] [or: 000001F634EDCBD0] [sps: 000001F4DC018E00]
[000001F8D73E5500] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_ActiveBlock_Hold.GA_Action_ActiveBlock_Hold_C [n: E92EE] [c: 00007FF46EEC3D38] [or: 000001F634EDCB80] [sps: 000001F4DC018E00]
[000001F8D73E4A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_ActiveBlock_Press.GA_Action_ActiveBlock_Press_C [n: 49F2E6] [c: 00007FF46EEC3D38] [or: 000001F634EDCA90] [sps: 000001F4DC018E00]
[000001F8D73E4330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_ActiveBlock_Release.GA_Action_ActiveBlock_Release_C [n: 20F471] [c: 00007FF46EEC3D38] [or: 000001F634EDCA40] [sps: 000001F4DC018E00]
[000001F8D73E3C10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_StoneForm_Press.GA_Action_StoneForm_Press_C [n: 2502FD] [c: 00007FF46EEC3D38] [or: 000001F634EDC9F0] [sps: 000001F4DC018E00]
[000001F8D73E34F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Input/GA_Action_StoneForm_Release.GA_Action_StoneForm_Release_C [n: 16A47E] [c: 00007FF46EEC3D38] [or: 000001F634EDC9A0] [sps: 000001F4DC018E00]
```

---

**End of part 1.** Part 2 (`40-player-actions-part2.md`) continues with, in order: sidearms/ranged
weapons (sec.9), shell abilities + shell swap/summon/severed/revive/downed (sec.10), tarstones, weapon
abilities and artifacts (sec.11), consumables, charges and item use (sec.12), interactions and memories
(sec.13), traversal / transformation / fast travel / rest (sec.14), and the consolidated sync-hook list plus
the UNKNOWN list (sec.15-sec.16).

