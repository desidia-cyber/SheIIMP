# 60 — Items, inventory, currency, drops, merchants, upgrades, crafting

Target: Mortal Shell II, UE 5.6 shipping build, UE4SS Lua modding.
Sources: `raw\index.sqlite3` (index of `raw\ObjectDump_allassets.txt`, main-menu all-assets dump)
and `raw\CXXHeaderDump\*.hpp`.

Working directory for every command below: `G:\Freespace\shell_coop\research`.

## 0. Method, conventions, and what is *not* in this file

* Every class/asset row has an evidence block with 1–3 lines copied verbatim from
  `python scripts\parse_dump.py raw raw\index.sqlite3 <regex>`. Blocks are fenced and are
  byte-identical dump lines (`[addr] Type /path[attrs]`).
* Property/function tables were produced with `scripts\q\ii-*.sql` (see §10) over the
  `obj`/`cls` tables, joined to resolve `ref` (StructProperty's struct, ObjectProperty's
  class, ArrayProperty's inner type). The `cls`/`obj` rows are the same objects as the dump
  lines, so a table row and its evidence block describe the same object.
* **Offsets** are the dump's `[o: ..]` values, written here in hex. `q.py` prints `off` as a
  decimal integer; those were converted.
* **Compiler temporaries filtered**: for Blueprint functions, parameters whose name starts
  with `Temp_`, `CallFunc_`, `K2Node_`, `__Internal_`, `ExecuteUbergraph_`, `Goto_`, `eb_`,
  `bpf_` are UE Blueprint-graph noise, not API parameters. Where a table lists a Blueprint
  function's "real" parameters only, the filter was applied and the noise is shown in the
  raw evidence instead. Native (`/Script/Sparta.*`) functions have no such noise and are
  listed completely.
* `GUESS:` prefixes any interpretation I could not prove. `UNKNOWN` marks a gap; §9 lists
  every one with the query I tried.
* **Every evidence line in this file was machine-verified**: `scripts\q\ii-20-verify-evidence.sql`
  extracts all 622 `[addr] Type /path` lines and looks each one up in `obj` by address **and**
  full path. Result: 617 distinct (addr, path) pairs, 614 ok / 0 bad — the extra rows are the
  header/footer of the checker's own output. Any line that failed this check was replaced with
  the real dump line via `ii-21-fix-evidence.sql`. Appendix A additionally diffed 494/494 lines
  against a fresh run of its query.
* **Out of scope** (other agents): world state, quests, player look/appearance, player
  actions/abilities, enemies & bosses, menus, animation catalogue. Shop *widgets* appear here
  only as the data source of the shop data model, not as menu layout.

### 0.1 Two facts that shape everything below

1. **There is no dedicated "gold" integer in any player component.** Currency (Gold, Gloom,
   Gloom Gemstone, Ovum, Glimpse, Specimen, Tarcore, …) is implemented as **item definitions**
   (`/Game/Sparta/Items/Pickups/ID_Gold.ID_Gold_C` etc.) and the amount is the item's
   **stack count** inside `USpartaItemManagerComponent`. See §4.
2. **Item definitions are not a flat Blueprint base.** `USpartaItemDefinition` is a native
   UObject; *all* 494 `/Game/...` item definitions are Blueprint subclasses of it through
   intermediate bases such as `ID_PickupBase_C`, `ID_EquipmentPickupBase_C`,
   `ID_WeaponPickupBase_C`, `ID_Tarstone_Base_C`. See §2.

---

## 1. Item definition system (native `/Script/Sparta`)

### 1.1 Class table

| Class | Parent | Purpose (from names + members) | Sync relevance |
|---|---|---|---|
| `/Script/Sparta.SpartaItemDefinition` | `/Script/CoreUObject.Object` | the item *type* (a Blueprint subclass per item); holds display name + fragment list | static data, never sync |
| `/Script/Sparta.SpartaItemInstance` | `/Script/CoreUObject.Object` | one live item object: `ItemDef` class + per-fragment payload + stat-tag stack | **the syncable unit** (stack contents) |
| `/Script/Sparta.SpartaItemFragment` | `/Script/CoreUObject.Object` | base of every item behaviour/display block | static data |
| `/Script/Sparta.SpartaItemFragment_StatLevels` | `SpartaItemFragment` | per-level stat table for upgrades | static data |
| `/Script/Sparta.SpartaItemManagerComponent` | `/Script/Engine.ActorComponent` | **the inventory**: replicated `FSpartaItemList` + add/remove/query API | **primary inventory sync surface** |
| `/Script/Sparta.SpartaItemFunctionLibrary` | `/Script/Engine.BlueprintFunctionLibrary` | static helpers to fetch fragments off an item def | none (read-only helper) |
| `/Script/Sparta.Async_LoadSpartaItems` | `/Script/Engine.BlueprintAsyncActionBase` | async batch-load of item class assets | none |
| `/Script/Sparta.SpartaItemChangeEvent` | `/Script/GlobalEventSubsystem.GlobalEvent` | global event carrying an item delta | **a change-notification hook** |
| `/Script/Sparta.ItemDisplayTranslation` | `/Script/CoreUObject.Object` | turns stat levels into display strings | none |
| `/Script/Sparta.ItemFragmentData_AbilitySets` | `/Script/CoreUObject.Object` | runtime payload object for `ItemFragment_AbilitySets` | none |
| `/Script/Sparta.ItemFragmentData_ClearAbilities` | `/Script/CoreUObject.Object` | runtime payload object | none |
| `/Script/Sparta.ItemFragmentData_TarstoneExperience` | `/Script/CoreUObject.Object` | **tarstone XP runtime state** (owner char, item instance, watcher tag) | tarstone XP sync |
| `/Script/Sparta.PPItemFragment` | `SpartaItemFragment` | permanent-progression item (active/passive slot, level thresholds) | PP-item level sync |
| `/Script/Sparta.PPItemFragmentData` | `/Script/CoreUObject.Object` | per-slot runtime PP item record | PP-item level sync |
| `/Script/Sparta.PickupableActor` | `/Script/Engine.Actor` | world pickup actor that adds itself to an item manager | pickup replication |

```
[00007FF46F067338] Class /Script/Sparta.SpartaItemDefinition [n: 4BCFF] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F067738] Class /Script/Sparta.SpartaItemInstance [n: 4BD47] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F035138] Class /Script/Sparta.SpartaItemFragment [n: 47A20] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F067B38] Class /Script/Sparta.SpartaItemManagerComponent [n: 4BD92] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F067138] Class /Script/Sparta.ItemFragment_SetStats [n: 4BCF3] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F035138]
[00007FF46F067938] Class /Script/Sparta.SpartaItemChangeEvent [n: 4BD86] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFCC338]
```

### 1.2 `USpartaItemDefinition` — properties and functions

| Property | Type | Offset | Notes |
|---|---|---|---|
| `DisplayName` | `TextProperty` | `0x28` | |
| `Fragments` | `ArrayProperty of ObjectProperty (SpartaItemFragment)` | `0x38` | inner ref is `/Script/Sparta.SpartaItemFragment` |

No `Function` members exist on this class. Size `0x48`.

```
[000001F4ACDEF660] TextProperty /Script/Sparta.SpartaItemDefinition:DisplayName [o: 28] [n: 104CF] [c: 00007FF721DFAB80] [owr: 00007FF46F067338]
[000001F4ACE47680] ArrayProperty /Script/Sparta.SpartaItemDefinition:Fragments [o: 38] [n: 54DA5E] [c: 00007FF721DE8910] [owr: 00007FF46F067338] [ai: 000001F4ACE47600]
[000001F4ACE47600] ObjectProperty Fragments./Script/Sparta.SpartaItemDefinition:Fragments [o: 0] [n: 54DA5E] [c: 00007FF721DE9420] [owr: 000001F4ACE47680] [pc: 00007FF46F035138]
```

### 1.3 `USpartaItemInstance` — properties and functions

| Property | Type | Offset | Notes |
|---|---|---|---|
| `FragmentData` | `MapProperty (USpartaItemFragment* → UObject*)` | `0x28` | key = fragment object, value = fragment runtime payload; dump confirms key `pc:` = `SpartaItemFragment` |
| `StatTags` | `StructProperty of /Script/Sparta.GameplayTagStackContainer` | `0x78` | per-instance stat counters (this is where stackable stat values live) |
| `ItemDef` | `ClassProperty` (`mc:` = `/Script/Sparta.SpartaItemDefinition`) | `0x1E0` | |

| Function | Parameters (name: type) | Notes |
|---|---|---|
| `RemoveStatTagStack` | `Tag: StructProperty of GameplayTag`, `StackCount: IntProperty` | no return |
| `HasStatTag` | `Tag: StructProperty of GameplayTag`, `ReturnValue: BoolProperty` | |
| `GetStatTagStackCount` | `Tag: StructProperty of GameplayTag`, `ReturnValue: IntProperty` | |
| `GetItemDef` | `ReturnValue: ClassProperty` | |
| `FindFragmentByClass` | `FragmentClass: ClassProperty`, `ReturnValue: ObjectProperty of SpartaItemFragment` | |
| `AddStatTagStack` | `Tag: StructProperty of GameplayTag`, `StackCount: IntProperty` | |

Size `0x1E8`.

```
[000001F4ACE20BA0] MapProperty /Script/Sparta.SpartaItemInstance:FragmentData [o: 28] [n: 5865E1] [c: 00007FF721DE8F70] [owr: 00007FF46F067738] [kp: 000001F4ACE48C80] [vp: 000001F4ACE48C00]
[000001F4ACE48D00] StructProperty /Script/Sparta.SpartaItemInstance:StatTags [o: 78] [n: 58A7C2] [c: 00007FF721DF9890] [owr: 00007FF46F067738] [ss: 00007FF46F48DEF8]
[000001F4ACE48D80] ClassProperty /Script/Sparta.SpartaItemInstance:ItemDef [o: 1E0] [n: 525597] [c: 00007FF721DE8E20] [owr: 00007FF46F067738] [mc: 00007FF46F067338]
[00007FF46F730658] Function /Script/Sparta.SpartaItemInstance:AddStatTagStack [n: 4BD51] [c: 00007FF46EE55538] [or: 00007FF46F067738] [f: 00007FF71C95ADD0]
[000001F4ACE50E10] IntProperty /Script/Sparta.SpartaItemInstance:RemoveStatTagStack:StackCount [o: 8] [n: 511C61] [c: 00007FF721DE91F0] [owr: 00007FF46F730AB8]
```

### 1.4 `USpartaItemFragment` and every subclass — fields

No subclass adds functions except those noted. `USpartaItemFragment` itself has **no** properties,
only two hooks.

| Class | Property | Type | Offset |
|---|---|---|---|
| `SpartaItemFragment` | *(no properties)* | — | — |
| `SpartaItemFragment_StatLevels` | `RequirementTag` | `StructProperty of GameplayTag` | `0x28` |
| `SpartaItemFragment_StatLevels` | `UpgradeLevelsMap` | `MapProperty (GameplayTag → FFloatArray)` | `0x30` |
| `ItemFragment_AbilitySets` | `AbilitySetToGrant` | `StructProperty of FSpartaAbilitySet` | `0x28` |
| `ItemFragment_BlockAddAbility` | `AbilityTag` | `StructProperty of GameplayTag` | `0x28` |
| `ItemFragment_ClearAbilities` | `AbilityTagToClear` | `StructProperty of GameplayTagContainer` | `0x28` |
| `ItemFragment_Display` | `Icon` | `SoftObjectProperty of Texture2D` | `0x28` |
| `ItemFragment_Display` | `DefaultVideo` | `SoftObjectProperty of MediaSource` | `0x50` |
| `ItemFragment_Display` | `DisplayName` | `TextProperty` | `0x78` |
| `ItemFragment_Display` | `DisplayNameSuffix` | `TextProperty` | `0x88` |
| `ItemFragment_Display` | `PrefixName` | `TextProperty` | `0x98` |
| `ItemFragment_Display` | `SubNameInfix` | `TextProperty` | `0xA8` |
| `ItemFragment_Display` | `SubName` | `TextProperty` | `0xB8` |
| `ItemFragment_Display` | `Description` | `TextProperty` | `0xC8` |
| `ItemFragment_Display` | `Effect` | `TextProperty` | `0xD8` |
| `ItemFragment_Display` | `HeaderOnAcquired` | `TextProperty` | `0xE8` |
| `ItemFragment_Display` | `HeaderOnEquipped` | `TextProperty` | `0xF8` |
| `ItemFragment_Display` | `GlowColor` | `StructProperty of LinearColor` | `0x108` |
| `ItemFragment_OverrideDisplay` | `RequirementTag` | `StructProperty of GameplayTag` | `0x118` |
| `ItemFragment_OverrideDisplay` | `bIconOverride` | `BoolProperty` | `0x120` |
| `ItemFragment_OverrideDisplay` | `bDefaultVideoOverride` | `BoolProperty` | `0x121` |
| `ItemFragment_OverrideDisplay` | `bDisplayNameOverride` | `BoolProperty` | `0x122` |
| `ItemFragment_OverrideDisplay` | `bDescriptionOverride` | `BoolProperty` | `0x123` |
| `ItemFragment_OverrideDisplay` | `bGlowColorOverride` | `BoolProperty` | `0x124` |
| `ItemFragment_Executable` | `ExecutableBehaviors` | `ArrayProperty of ObjectProperty (UExecutableBehavior)` | `0x28` |
| `ItemFragment_GiveItemToPlayer` | `ItemToGive` | `SoftClassProperty` (hpp: `TSoftClassPtr<USpartaItemDefinition>`) | `0x28` |
| `ItemFragment_GiveItemToShell` | `ItemToGive` | `SoftClassProperty` (hpp: `TSoftClassPtr<USpartaItemDefinition>`) | `0x28` |
| `ItemFragment_ItemTag` | *(no properties)* | — | — |
| `ItemFragment_PickupIcon` | `ItemRarity` | `EnumProperty of /Script/Sparta.EItemRarity` | `0x28` |
| `ItemFragment_PickupIcon` | `AudioSwitchGroup` | `ObjectProperty of AudioSwitchGroup` | `0x30` |
| `ItemFragment_PickupIcon` | `AudioSwitchValue` | `StructProperty of FAudioSwitchState` | `0x38` |
| `ItemFragment_PickupIcon` | `bInstancesRespawnAfterPickup` | `BoolProperty` | `0x60` |
| `ItemFragment_PickupIcon` | `OnPickupBehaviors` | `ArrayProperty of ObjectProperty (UExecutableBehavior)` | `0x68` |
| `ItemFragment_SetDarkformClass` | `DarkformClass` | `SoftClassProperty` (hpp: `TSoftClassPtr<ASpartaPlayerCharacter>`) | `0x28` |
| `ItemFragment_SetShellClass` | `ShellClass` | `SoftClassProperty` (hpp: `TSoftClassPtr<ASpartaPlayerCharacter>`) | `0x28` |
| `ItemFragment_SetStats` | `InitialItemStats` | `MapProperty (GameplayTag → int32)` | `0x28` |
| `ItemFragment_Spawnable` | `ActorToSpawn` | `SoftClassProperty` (hpp: `TSoftClassPtr<AActor>`) | `0x28` |
| `ItemFragment_StackabilityRules` | `bGrantDuplicateAbilitySets` | `BoolProperty` | `0x28` |
| `ItemFragment_StackableDivideSumByCountSquared` | *(no properties)* | — | — |
| `ItemFragment_Tarstone` | `TarstoneExpClass` | `ClassProperty` (hpp: `TSubclassOf<UItemFragmentData_TarstoneExperience>`) | `0x28` |
| `ItemFragment_Tarstone` | `TarstoneSlot` | `ByteProperty` (hpp: `TEnumAsByte<ETarstoneSlot>`) | `0x30` |
| `ItemFragment_Tarstone` | `TarstoneColor` | `EnumProperty of /Script/Sparta.ETarstoneColor` | `0x31` |
| `ItemFragment_Tarstone` | `CompatibilityTags` | `StructProperty of GameplayTagContainer` | `0x38` |
| `ItemFragment_Tarstone` | `ExpNeededPerLevel` | `ArrayProperty of IntProperty` | `0x58` |
| `ItemFragment_Tarstone` | `GoldNeededPerLevel` | `ArrayProperty of IntProperty` | `0x68` |
| `ItemFragment_Tarstone` | `TarcoresNeededPerLevel` | `ArrayProperty of IntProperty` | `0x78` |
| `ItemFragment_Tarstone` | `TarcoresGrantedPerLevel` | `ArrayProperty of IntProperty` | `0x88` |
| `ItemFragment_WorldStateModifier` | `OnSpawnEffectsToAdd` | `ArrayProperty (TSubclassOf<UGameplayEffect>)` | `0x28` |
| `ItemFragment_WorldStateModifier` | `OnSpawnAbilitiesToAdd` | `ArrayProperty (TSubclassOf<UGameplayAbility>)` | `0x38` |
| `PPItemFragment` | `LevelThresholds` | `ArrayProperty of IntProperty` | `0x28` |
| `PPItemFragment` | `UpgradeLevelsMap` | `MapProperty (GameplayTag → FFloatArray)` | `0x38` |
| `PPItemFragment` | `ActiveSlotUpgradeLevelsMap` | `MapProperty (GameplayTag → FFloatArray)` | `0x88` |
| `PPItemFragment` | `PassiveSlotUpgradeLevelsMap` | `MapProperty (GameplayTag → FFloatArray)` | `0xD8` |
| `PPItemFragment` | `ActiveSlotAbilitySetToGrant` | `StructProperty of FSpartaAbilitySet` | `0x128` |
| `PPItemFragment` | `PassiveSlotAbilitySetToGrant` | `StructProperty of FSpartaAbilitySet` | `0x158` |

Evidence (subclass declarations + their first field, verbatim):

```
[00007FF46F035938] Class /Script/Sparta.ItemFragment_Display [n: 47A59] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F035138]
[000001F4ACD5DF00] SoftObjectProperty /Script/Sparta.ItemFragment_Display:Icon [o: 28] [n: 519722] [c: 00007FF721DF97F0] [owr: 00007FF46F035938] [pc: 00007FF46EE75F38]
[000001F4ACD60B00] TextProperty /Script/Sparta.ItemFragment_Display:DisplayName [o: 78] [n: 104CF] [c: 00007FF721DFAB80] [owr: 00007FF46F035938]
[00007FF46F036F38] Class /Script/Sparta.ItemFragment_Tarstone [n: 47B02] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F035138]
[00007FF46F039538] Class /Script/Sparta.PPItemFragment [n: 47DF1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F035138]
[00007FF46F04E138] Class /Script/Sparta.SpartaItemFragment_StatLevels [n: 49944] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F035138]
[00007FF46F035B38] Class /Script/Sparta.ItemFragment_GiveItemToPlayer [n: 47A64] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F035138]
[00007FF46F036938] Class /Script/Sparta.ItemFragment_StackabilityRules [n: 47AC7] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F035138]
```

Result of the byte-identical regex sweep for **every** `ItemFragment*` / `PPItemFragment*` class in
the build (no Blueprint fragment subclasses exist outside `/Game/Sparta/Items/`):

| Fragment class | Path |
|---|---|
| AbilitySets | `/Script/Sparta.ItemFragment_AbilitySets` |
| BlockAddAbility | `/Script/Sparta.ItemFragment_BlockAddAbility` |
| ClearAbilities | `/Script/Sparta.ItemFragment_ClearAbilities` |
| Display | `/Script/Sparta.ItemFragment_Display` |
| OverrideDisplay | `/Script/Sparta.ItemFragment_OverrideDisplay` |
| Executable | `/Script/Sparta.ItemFragment_Executable` |
| GiveItemToPlayer | `/Script/Sparta.ItemFragment_GiveItemToPlayer` |
| GiveItemToShell | `/Script/Sparta.ItemFragment_GiveItemToShell` |
| ItemTag | `/Script/Sparta.ItemFragment_ItemTag` |
| PickupIcon | `/Script/Sparta.ItemFragment_PickupIcon` |
| SetDarkformClass | `/Script/Sparta.ItemFragment_SetDarkformClass` |
| SetShellClass | `/Script/Sparta.ItemFragment_SetShellClass` |
| SetStats | `/Script/Sparta.ItemFragment_SetStats` |
| Spawnable | `/Script/Sparta.ItemFragment_Spawnable` |
| StackabilityRules | `/Script/Sparta.ItemFragment_StackabilityRules` |
| StackableDivideSumByCountSquared | `/Script/Sparta.ItemFragment_StackableDivideSumByCountSquared` |
| Tarstone | `/Script/Sparta.ItemFragment_Tarstone` |
| WorldStateModifier | `/Script/Sparta.ItemFragment_WorldStateModifier` |
| PPItemFragment | `/Script/Sparta.PPItemFragment` |
| SpartaItemFragment_StatLevels | `/Script/Sparta.SpartaItemFragment_StatLevels` |

There is **no** `ItemFragment_Stackable*` other than the two above, and there is **no**
`PPItemFragment_*` subclass in this build.

### 1.5 Fragment functions (complete parameter lists)

| Class | Function | Parameters (name: type) |
|---|---|---|
| `SpartaItemFragment` | `OnInstanceRemoved` | `Instance: ObjectProperty of SpartaItemInstance`, `OwnerComponent: ObjectProperty of SpartaItemManagerComponent` |
| `SpartaItemFragment` | `OnInstanceCreated` | `Instance: ObjectProperty of SpartaItemInstance`, `OwnerComponent: ObjectProperty of SpartaItemManagerComponent` |
| `ItemFragment_Executable` | `Execute` | `ContextObject: ObjectProperty of Object` |
| `ItemFragment_ItemTag` | `GetItemTag` | `ReturnValue: StructProperty of GameplayTag` |
| `ItemFragment_Spawnable` | `ShouldSpawn` | `Instance: ObjectProperty of SpartaItemInstance`, `OwnerComponent: ObjectProperty of SpartaItemManagerComponent`, `ReturnValue: BoolProperty` |
| `ItemFragment_Spawnable` | `ChooseSpawnableActor` | `Instance: ObjectProperty of SpartaItemInstance`, `OwnerComponent: ObjectProperty of SpartaItemManagerComponent`, `ReturnValue: ClassProperty` |
| `ItemFragment_StackabilityRules` | `GetStackStatValue` | `StatTag: StructProperty of GameplayTag`, `Values: ArrayProperty of FloatProperty`, `ReturnValue: FloatProperty` |
| `PPItemFragment` | `OnPPItemRemoved` | `OwnerComponent: ObjectProperty of PPItemsComponent`, `FragmentData: ObjectProperty of PPItemFragmentData` |
| `PPItemFragment` | `OnPPItemGranted` | `OwnerComponent: ObjectProperty of PPItemsComponent`, `FragmentData: ObjectProperty of PPItemFragmentData` |

```
[00007FF46F6E6138] Function /Script/Sparta.SpartaItemFragment:OnInstanceRemoved [n: 14954] [c: 00007FF46EE55538] [or: 00007FF46F035138] [f: 00007FF71C95A690]
[000001F4ACD5E200] ObjectProperty /Script/Sparta.SpartaItemFragment:OnInstanceRemoved:Instance [o: 0] [n: 5556B3] [c: 00007FF721DE9420] [owr: 00007FF46F6E6138] [pc: 00007FF46F067738]
[000001F4ACD5E280] ObjectProperty /Script/Sparta.SpartaItemFragment:OnInstanceRemoved:OwnerComponent [o: 8] [n: 19DD1] [c: 00007FF721DE9420] [owr: 00007FF46F6E6138] [pc: 00007FF46F067B38]
```

### 1.6 `USpartaItemManagerComponent` — the inventory

| Property | Type | Offset | Notes |
|---|---|---|---|
| `OnLootPickedUp` | `MulticastInlineDelegateProperty` | `0xB8` | signature = `OnLootPickedUp(class USpartaItemInstance* LootItem)` |
| `OnItemStacksUpdated` | `MulticastInlineDelegateProperty` | `0xC8` | signature = `OnItemStacksUpdated(TSubclassOf<USpartaItemDefinition>, int32 Count)` |
| `ItemList` | `StructProperty of /Script/Sparta.SpartaItemList` | `0xD8` | **the actual inventory container**; `FSpartaItemList : FFastArraySerializer` ⇒ replicated array |

| Function | Parameters (name: type) | Notes |
|---|---|---|
| `AddItemByClass` | `ItemDef: ClassProperty`, `StackCount: IntProperty`, `ReturnValue: ObjectProperty of SpartaItemInstance` | **primary add** |
| `AddItemInstance` | `ItemInstance: ObjectProperty of SpartaItemInstance` | add a pre-built instance |
| `CanAddItemByClass` | `ItemDef: ClassProperty`, `StackCount: IntProperty`, `ReturnValue: BoolProperty` | stackability check |
| `FindFirstItemStackByClass` | `ItemDef: ClassProperty`, `ReturnValue: ObjectProperty of SpartaItemInstance` | |
| `GetAllItems` | `ReturnValue: ArrayProperty of ObjectProperty (SpartaItemInstance)` | `TArray<USpartaItemInstance*>` |
| `GetItemStacksByClass` | `ItemDef: ClassProperty`, `ReturnValue: IntProperty` | **this is how a currency amount is read** |
| `GetTotalItemCountByDefinition` | `ItemDef: ClassProperty`, `ReturnValue: IntProperty` | |
| `RemoveItemInstance` | `ItemInstance: ObjectProperty of SpartaItemInstance` | **primary remove** |
| `RemoveItemStacksByClass` | `ItemDef: ClassProperty`, `NumToRemove: IntProperty` | |
| `RemoveItemStacksByInstance` | `ItemInstance: ObjectProperty of SpartaItemInstance`, `NumToRemove: IntProperty` | |

Size `0x248`.

```
[000001F4ACE4A380] MulticastInlineDelegateProperty /Script/Sparta.SpartaItemManagerComponent:OnLootPickedUp [o: B8] [n: 58A7ED] [c: 00007FF721DE9010] [owr: 00007FF46F067B38] [df: 00007FF46F3CF8E0]
[000001F4ACE4A480] StructProperty /Script/Sparta.SpartaItemManagerComponent:ItemList [o: D8] [n: 58A7DD] [c: 00007FF721DF9890] [owr: 00007FF46F067B38] [ss: 00007FF46F48E438]
[000001F4ACE48A80] ObjectProperty /Script/Sparta.SpartaItemManagerComponent:AddItemByClass:ReturnValue [o: 10] [n: 4CF54F] [c: 00007FF721DE9420] [owr: 00007FF46F730B98] [pc: 00007FF46F067738]
```

### 1.7 `USpartaItemFunctionLibrary`, `UAsync_LoadSpartaItems`, `USpartaItemChangeEvent`

| Class | Function | Parameters (name: type) |
|---|---|---|
| `SpartaItemFunctionLibrary` | `FindItemDefinitionFragment` | `ItemDef: ClassProperty`, `FragmentClass: ClassProperty`, `ReturnValue: ObjectProperty of SpartaItemFragment` |
| `SpartaItemFunctionLibrary` | `FindItemDefinitionFragments` | `ItemDef: ClassProperty`, `FragmentClass: ClassProperty`, `ReturnValue: ArrayProperty of ObjectProperty` |
| `SpartaItemFunctionLibrary` | `FindMutableItemDefinitionFragment` | `ItemDef: ClassProperty`, `FragmentClass: ClassProperty`, `ReturnValue: ObjectProperty of SpartaItemFragment` |
| `Async_LoadSpartaItems` | `AsyncLoadSpartaItems` | `WorldContextObject: ObjectProperty of Object`, `ItemsToLoad: ArrayProperty of StructProperty (FSpartaItem)`, `ReturnValue: ObjectProperty of Async_LoadSpartaItems` |
| `Async_LoadSpartaItems` | `OnLoadComplete` (delegate) | `MulticastInlineDelegateProperty` at `0x30`, no params |

| `SpartaItemChangeEvent` property | Type | Offset |
|---|---|---|
| `ItemOwner` | `ObjectProperty of /Script/Engine.ActorComponent` | `0x28` |
| `Instance` | `ObjectProperty of /Script/Sparta.SpartaItemInstance` | `0x30` |
| `NewCount` | `IntProperty` | `0x38` |
| `Delta` | `IntProperty` | `0x3C` |

```
[00007FF46F067538] Class /Script/Sparta.SpartaItemFunctionLibrary [n: 4BD0A] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE70138]
[000001F4ACD57E80] ObjectProperty /Script/Sparta.Async_LoadSpartaItems:AsyncLoadSpartaItems:WorldContextObject [o: 0] [n: 4E6654] [c: 00007FF721DE9420] [owr: 00007FF46F6E3658] [pc: 00007FF46EE50070]
[000001F4ACD57F80] ArrayProperty /Script/Sparta.Async_LoadSpartaItems:AsyncLoadSpartaItems:ItemsToLoad [o: 8] [n: 585E46] [c: 00007FF721DE8910] [owr: 00007FF46F6E3658] [ai: 000001F4ACD57F00]
[000001F4ACD58000] ObjectProperty /Script/Sparta.Async_LoadSpartaItems:AsyncLoadSpartaItems:ReturnValue [o: 18] [n: 4CF54F] [c: 00007FF721DE9420] [owr: 00007FF46F6E3658] [pc: 00007FF46F02DF38]
```

### 1.8 Fragment runtime-payload objects

| Class | Property | Type | Offset |
|---|---|---|---|
| `ItemFragmentData_TarstoneExperience` | `SourceFragment` | `ObjectProperty of ItemFragment_Tarstone` | `0x28` |
| `ItemFragmentData_TarstoneExperience` | `OwnerCharacter` | `ObjectProperty of SpartaCharacter` | `0x30` |
| `ItemFragmentData_TarstoneExperience` | `ItemDef` | `ClassProperty` | `0x38` |
| `ItemFragmentData_TarstoneExperience` | `ItemInstance` | `ObjectProperty of SpartaItemInstance` | `0x40` |
| `ItemFragmentData_TarstoneExperience` | `WaitForEventTag` | `StructProperty of GameplayTag` | `0x48` |
| `PPItemFragmentData` | `bCreatedByPPItemsComponent` | `BoolProperty` | `0x28` |
| `PPItemFragmentData` | `GrantedSlot` | `EnumProperty of /Script/Sparta.EPPItemSlot` | `0x29` |
| `PPItemFragmentData` | `ItemClass` | `SoftClassProperty` | `0x30` |
| `PPItemFragmentData` | `Level` | `IntProperty` | `0x58` |
| `ItemDisplayTranslation` | `StatTag` | `StructProperty of GameplayTag` | `0x28` |
| `ItemFragmentData_AbilitySets` | *(no data properties in dump; hpp size 0x60)* | — | — |
| `ItemFragmentData_ClearAbilities` | *(no data properties in dump; hpp size 0x38)* | — | — |

| Class | Function | Parameters |
|---|---|---|
| `ItemFragmentData_TarstoneExperience` | `Initialize` | *(none)* |
| `ItemFragmentData_TarstoneExperience` | `TarstoneEvent` | `EventData: StructProperty of GameplayEventData` |
| `PPItemFragmentData` | `GetGrantedPPItemSlot` | `ReturnValue: EnumProperty of EPPItemSlot` |
| `PPItemFragmentData` | `WasCreatedByPPItemsComponent` | `ReturnValue: BoolProperty` |
| `ItemDisplayTranslation` | `ConstructWithStatLevels` | `StatLevels: ObjectProperty of SpartaItemFragment_StatLevels`, `Character: ObjectProperty of Character`, `Values: ArrayProperty of TextProperty` |
| `ItemDisplayTranslation` | `ConstructPropertyValues` | `MaxLevel: IntProperty`, `DefaultValue: FloatProperty`, `Values: ArrayProperty of TextProperty` |

```
[00007FF46F039338] Class /Script/Sparta.PPItemFragmentData [n: 47DCD] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[000001F4ACD5E400] StructProperty /Script/Sparta.ItemDisplayTranslation:StatTag [o: 28] [n: 55C6D8] [c: 00007FF721DF9890] [owr: 00007FF46F034B38] [ss: 00007FF46F3890A8]
```

### 1.9 Item-related structs (`raw\CXXHeaderDump\Sparta.hpp`)

| Struct | Field | Type | Offset | Size |
|---|---|---|---|---|
| `FPickupTemplate` | `StackCount` | `int32` | `0x00` | `0x4` |
| `FPickupTemplate` | `ItemDef` | `TSubclassOf<USpartaItemDefinition>` | `0x08` | `0x8` |
| `FSpartaItem` (`: FTableRowBase`) | `ID` | `FName` | `0x08` | `0x8` |
| `FSpartaItem` | `ItemClass` | `TSoftClassPtr<USpartaItemDefinition>` | `0x10` | `0x28` |
| `FSpartaItem` | `DisplayName` | `FString` | `0x38` | `0x10` |
| `FSpartaItem` | `Description` | `FString` | `0x48` | `0x10` |
| `FSpartaItemEntry` (`: FFastArraySerializerItem`) | `Instance` | `USpartaItemInstance*` | `0x10` | `0x8` |
| `FSpartaItemEntry` | `StackCount` | `int32` | `0x18` | `0x4` |
| `FSpartaItemEntry` | `LastObservedCount` | `int32` | `0x1C` | `0x4` |
| `FSpartaItemList` (`: FFastArraySerializer`) | `Entries` | `TArray<FSpartaItemEntry>` | `0x108` | `0x10` |
| `FSpartaItemList` | `OwnerComponent` | `USpartaItemManagerComponent*` | `0x118` | `0x8` |

`Sparta.hpp` lines 223–228, 923–945. The struct objects themselves are in the dump:

```
[000001F4ACE4A480] StructProperty /Script/Sparta.SpartaItemManagerComponent:ItemList [o: D8] [n: 58A7DD] [c: 00007FF721DF9890] [owr: 00007FF46F067B38] [ss: 00007FF46F48E438]
[000001F4ACD5FF00] StructProperty /Script/Sparta.PickupableActor:ItemDef [o: 2A8] [n: 525597] [c: 00007FF721DF9890] [owr: 00007FF46F037938] [ss: 00007FF46F486AB8]
```

---

## 2. Item DATA — the item definition assets

### 2.1 How to enumerate them correctly

`SELECT ... FROM cls WHERE super = <SpartaItemDefinition>` returns only **25** direct
Blueprint children. The real set must be computed as a **transitive closure** because every
item definition inherits through an intermediate `ID_*Base` Blueprint:

```sql
WITH RECURSIVE d(addr, path, depth) AS (
  SELECT addr, path, 0 FROM cls WHERE path='/Script/Sparta.SpartaItemDefinition'
  UNION
  SELECT c.addr, c.path, d.depth+1 FROM cls c JOIN d ON c.super=d.addr
)
SELECT d.path FROM d WHERE d.path LIKE '/Game/%' ORDER BY d.path
```

Result: **494 item-definition classes** (`scripts\q\ii-11-itemdef-closure.sql`).
Query used for the asset-type histogram: `scripts\q\ii-02-item-asset-families.sql`.
Query used for the folder breakdown: `scripts\q\ii-09-itemdef-families.sql` (fixed for the
ambiguous-column error by computing the family in PowerShell instead).

### 2.2 Families — where item definitions actually live

| Family (path prefix) | Count | Example path |
|---|---|---|
| `/Game/Sparta/Core/Player` | 198 | `/Game/Sparta/Core/Player/Upgrades/Core/ID_Upgrade_XXX…` (see §2.3) |
| `/Game/Sparta/Items/Pickups` | 124 | `/Game/Sparta/Items/Pickups/ID_Gold.ID_Gold_C` |
| `/Game/Sparta/Core/Tarstones` | 67 | `/Game/Sparta/Core/Tarstones/Melee/…`, `…/Sidearm/…`, `…/Support/…` |
| `/Game/Sparta/Items/Shells` | 47 | `/Game/Sparta/Items/Shells/ID_Shell_Thorn.ID_Shell_Thorn_C` |
| `/Game/Sparta/Items/Weapons` | 20 | `/Game/Sparta/Items/Weapons/ID_BlackNeedle.ID_BlackNeedle_C` |
| `/Game/Sparta/Items/Map` | 13 | `/Game/Sparta/Items/Map/ID_MapPiece_Forest_1.ID_MapPiece_Forest_1_C` |
| `/Game/Sparta/Items/Sidearms` | 12 | `/Game/Sparta/Items/Sidearms/ID_Sidearm_Crossbow.ID_Sidearm_Crossbow_C` |
| `/Game/Sparta/Items/Charges` | 7 | `/Game/Sparta/Items/Charges/ID_Charges_Cleansing.ID_Charges_Cleansing_C` |
| `/Game/Sparta/Items/Seals` | 6 | `/Game/Sparta/Items/Seals/ID_Seal_Slayer.ID_Seal_Slayer_C` |

### 2.3 Second-level breakdown of the big folders

| Folder | Count |
|---|---|
| `/Game/Sparta/Core/Player/Upgrades` | 150 |
| `/Game/Sparta/Core/Player/Artifacts` | 35 |
| `/Game/Sparta/Core/Tarstones/Melee` | 30 |
| `/Game/Sparta/Core/Tarstones/Sidearm` | 30 |
| `/Game/Sparta/Items/Shells` | 12 (direct) |
| `/Game/Sparta/Core/Player/Debuffs` | 10 |
| `/Game/Sparta/Items/Weapons` | 9 (direct) |
| `/Game/Sparta/Core/Tarstones/Support` | 7 |
| `/Game/Sparta/Core/Player/Ability` | 2 |
| `/Game/Sparta/Items/{Charges,Sidearms,Pickups,Seals}` | 1 each (direct) |

(`counts computed from ii-11 closure output; the remaining entries are nested deeper, e.g.
`…/Shells/Skins/`, `…/Shells/Pickups/`, `…/Pickups/PermProgression/ID/`.)

### 2.4 Asset-type histogram under `/Game/Sparta/Items/` only

`SELECT type, COUNT(*) FROM obj WHERE path LIKE '/Game/Sparta/Items/%' GROUP BY type`:
**510 Packages** (= 510 assets in that folder tree). Top object kinds:

| Type | n | Type | n |
|---|---|---|---|
| `ObjectProperty` | 915 | `SceneComponent` | 99 |
| `BoolProperty` | 696 | `StaticMeshComponent` | 94 |
| `Function` | 598 | `IF_ItemTag_C` | 91 |
| `StructProperty` | 598 | `ItemFragment_AbilitySets` | 90 |
| `Package` | 510 | `ArrayProperty` | 77 |
| `BlueprintGeneratedClass` | 393 | `NameProperty` | 76 |
| `IntProperty` | 289 | `SimpleConstructionScript` | 74 |
| `SCS_Node` | 232 | `IF_AbilityDisplayData_C` | 72 |
| `ItemFragment_Display` | 204 | `IF_UsableInventoryItem_C` | 70 |
| `ItemFragment_PickupIcon` | 138 | `InheritableComponentHandler` | 70 |
| `DoubleProperty` | 162 | `ClassProperty` | 69 |
| `FloatProperty` | 116 | `DelegateProperty` | 60 |

The `ItemFragment_Display` / `ItemFragment_PickupIcon` / `ItemFragment_AbilitySets` counts are
**fragment *instance* objects** attached to item-definition CDOs — i.e. `IF_*_C` Blueprint
fragment subclasses (e.g. `IF_ItemTag_C`, `IF_UsableInventoryItem_C`) are instantiated per item.
`GUESS:` the 204 `ItemFragment_Display` instances ≈ the number of item defs that set an icon/
description override; every item in the shipped set appears to carry a Display fragment.

Representative fragment-instance structure (from `ii-06`):

```
Package  /Game/Sparta/Items/Pickups/Tardust/ID_Tarcore
ID_Tarcore_C  /Game/Sparta/Items/Pickups/Tardust/ID_Tarcore.Default__ID_Tarcore_C
ItemFragment_Display   /Game/Sparta/Items/Pickups/Tardust/ID_Tarcore.Default__ID_Tarcore_C:ItemFragment_Display_0
ItemFragment_PickupIcon /Game/Sparta/Items/Pickups/Tardust/ID_Tarcore.Default__ID_Tarcore_C:ItemFragment_PickupIcon_0
```

⇒ fragments are **named sub-objects of the item definition's CDO**
(`<Package>.Default__<Class>_C:ItemFragment_<X>_<n>`). GUESS: they are *templates*; a live
`SpartaItemInstance` gets its own payload object of the `ItemFragmentData_*` class.

### 2.5 Full list of item definition assets

494 paths — too many for a per-row evidence block, so the **full list is Appendix A** as
verbatim dump lines (`scripts\q\ii-18-itemdef-rawlines.sql`, which reconstructs the dump line
exactly like `parse_dump.py raw`). Representative evidence:

```
[000001F4C131B1A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Gold.ID_Gold_C [n: 192ED2] [c: 00007FF46EEC3D38] [or: 000001F4A2D5D6C0] [sps: 000001F4B021F550]
[000001F7E929B8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/ID_Axatana_Axe.ID_Axatana_Axe_C [n: E9495] [c: 00007FF46EEC3D38] [or: 000001F7F77B1870] [sps: 00007FF46F067338]
[000001F4C2F4EAA0] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_Weapon_LoadFromSave.ID_Weapon_LoadFromSave_C [n: 389DB8] [c: 00007FF46EEC3D38] [or: 000001F4BFB62220] [sps: 00007FF46F067338]
```

Note the `[sps:]` field: `ID_Axatana_Axe_C` has SuperStruct `00007FF46F067338`
= `/Script/Sparta.SpartaItemDefinition` (direct child), whereas `ID_Gold_C` points at its
Blueprint parent `ID_PickupBase_C`.

---

## 3. Inventory components — properties and functions

### 3.1 Class table

| Class | Parent | Role |
|---|---|---|
| `/Script/Sparta.PlayerInventoryComponent` | `/Script/Engine.ActorComponent` | **empty shell** — no properties, no functions (size `0xB8`) |
| `/Script/Sparta.PPItemsComponent` | `/Script/Sparta.SpartaSaveComponent` | permanent-progression items: active/passive slot, levels, found counts |
| `/Script/Sparta.PPItemsSaveData` | `/Script/Sparta.SpartaSaveObject` | the persisted half of the above |
| `/Script/Sparta.WeaponsComponent` | `/Script/Engine.ActorComponent` | equipped melee weapon actors per slot |
| `/Script/Sparta.UpgradesComponent` | `/Script/Engine.ActorComponent` | legacy DataTable-driven upgrade levels |
| `/Script/Sparta.TarstoneComponent` | `/Script/Sparta.SpartaSaveComponent` | tarstone builds, levels/XP, support builds |
| `/Script/Sparta.TarstoneSaveData` | `/Script/Sparta.SpartaSaveObject` | persisted tarstone state |
| `/Script/Sparta.ArtifactComponent` | `/Script/Engine.ActorComponent` | artifact/narrative/debuff upgrade tables |
| `/Script/Sparta.ArmorComponent` | `/Script/Engine.ActorComponent` | current armor gameplay tag + overrides |
| `/Script/Sparta.SlayerSealComponent` | `/Script/Engine.ActorComponent` | seal throw/recall state machine |
| `/Script/Sparta.SidearmProjectileCacheComponent` | `/Script/Engine.ActorComponent` | per-weapon pooled projectile cache |

```
[00007FF46F037F38] Class /Script/Sparta.PlayerInventoryComponent [n: 47C28] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F039938] Class /Script/Sparta.PPItemsComponent [n: 47E02] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038738]
[00007FF46F039738] Class /Script/Sparta.PPItemsSaveData [n: 47DF9] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038338]
[00007FF46F062B38] Class /Script/Sparta.TarstoneComponent [n: 4B13E] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038738]
[00007FF46F063738] Class /Script/Sparta.UpgradesComponent [n: 4B210] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F063938] Class /Script/Sparta.WeaponsComponent [n: 4B21A] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
```

### 3.2 `UPlayerInventoryComponent` — empty

`obj` has **zero** rows owned by this class. Query:
`SELECT * FROM obj WHERE owner=(SELECT addr FROM obj WHERE path='/Script/Sparta.PlayerInventoryComponent' AND type='Class')`
→ 0 rows. `Sparta.hpp:3350` shows the body is empty, `// Size: 0xB8`.
⇒ **The real inventory is `USpartaItemManagerComponent`, not this component.**

### 3.3 `UPPItemsComponent`

| Property | Type | Offset | Notes |
|---|---|---|---|
| `ActivePPItem` | `SoftClassProperty` | `0x120` | hpp: `TSoftClassPtr<USpartaItemDefinition>` |
| `PassivePPItem` | `SoftClassProperty` | `0x148` | ditto |
| `PPItemLevels` | `MapProperty (TSoftClassPtr<ItemDef> → int32)` | `0x170` | |
| `PPItemFoundAmounts` | `MapProperty (TSoftClassPtr<ItemDef> → int32)` | `0x1C0` | lifetime found count |
| `PPItemFragmentDataBySlot` | `MapProperty (EPPItemSlot → PPItemFragmentData*)` | `0x210` | |
| `TrackedPPItems` | `ArrayProperty (TSoftClassPtr<ItemDef>)` | `0x260` | |
| `bIsTrackingPPItemStacks` | `BoolProperty` | `0x270` | |
| `OnPPItemEquipped` | `MulticastInlineDelegateProperty` | `0x278` | `(EPPItemSlot, TSoftClassPtr<ItemDef>, int32 CurrentLevel)` |
| `OnPPItemUnequipped` | `MulticastInlineDelegateProperty` | `0x288` | `(EPPItemSlot, TSoftClassPtr<ItemDef>)` |
| `OnPPItemLevelChanged` | `MulticastInlineDelegateProperty` | `0x298` | `(TSoftClassPtr<ItemDef>, int32 OldLevel, int32 NewLevel)` |
| `OnPPItemStackCountChanged` | `MulticastInlineDelegateProperty` | `0x2A8` | `(TSoftClassPtr<ItemDef>, int32 NewStackCount)` |
| `OnPPItemTrackingEvent` | `MulticastInlineDelegateProperty` | `0x2B8` | `(TSoftClassPtr<ItemDef>, int32 CountDelta, int32 TotalFoundAmount)` |

| Function | Parameters (name: type) |
|---|---|
| `EquipPPItem` | `Slot: EnumProperty of EPPItemSlot`, `ItemClass: SoftClassProperty`, `ReturnValue: ObjectProperty of PPItemFragmentData` |
| `UnequipPPItem` | `Slot: EnumProperty of EPPItemSlot`, `ReturnValue: ObjectProperty of PPItemFragmentData` |
| `UnequipPPItemByClass` | `ItemClass: SoftClassProperty`, `ReturnValue: ObjectProperty of PPItemFragmentData` |
| `UnequipAllPPItems` | *(none)* |
| `SetPPItemLevel` | `ItemClass: SoftClassProperty`, `NewLevel: IntProperty` |
| `AddToPPItemLevel` | `ItemClass: SoftClassProperty`, `Delta: IntProperty` |
| `AddToPPItemFoundAmount` | `ItemClass: SoftClassProperty`, `Delta: IntProperty` |
| `GetPPItemLevel` | `ItemClass: SoftClassProperty`, `ReturnValue: IntProperty` |
| `GetPPItemFoundAmount` | `ItemClass: SoftClassProperty`, `ReturnValue: IntProperty` |
| `GetEquippedPPItem` | `Slot: EnumProperty of EPPItemSlot`, `ReturnValue: SoftClassProperty` |
| `GetPPItemFragmentData` | `Slot: EnumProperty of EPPItemSlot`, `ReturnValue: ObjectProperty of PPItemFragmentData` |
| `GetRuntimeFragmentData` | `Slot: EnumProperty of EPPItemSlot`, `ReturnValue: ObjectProperty of PPItemFragmentData` |
| `GetTrackedPPItemStackCount` | `ItemClass: SoftClassProperty`, `ReturnValue: IntProperty` |
| `GetPPItemsNeededForNextLevel` | `ItemClass: SoftClassProperty`, `ReturnValue: IntProperty` |
| `GetPPItemNormalizedProgressToMaxLevel` | `ItemClass: SoftClassProperty`, `ReturnValue: FloatProperty` |
| `GetPPItemLastLevelThreshold` | `ItemClass: SoftClassProperty`, `ReturnValue: IntProperty` |
| `GetPPItemOverflowScaledValue` | `ItemClass: SoftClassProperty`, `BaseValue: FloatProperty`, `HitValue: FloatProperty`, `DiminishingReturnStrength: FloatProperty`, `ReturnValue: FloatProperty` |
| `FindPPItemMaximumLevel` | `ItemClass: SoftClassProperty`, `ReturnValue: IntProperty` |
| `IsPPItemEquipped` | `ItemClass: SoftClassProperty`, `OutSlot: EnumProperty of EPPItemSlot`, `ReturnValue: BoolProperty` |
| `IsPPItemAtMaxLevel` | `ItemClass: SoftClassProperty`, `ReturnValue: BoolProperty` |
| `HasPPItemLevel` | `ItemClass: SoftClassProperty`, `ReturnValue: BoolProperty` |
| `StartTrackingPPItemStacks` | *(none)* |
| `StopTrackingPPItemStacks` | *(none)* |
| `RefreshTrackedPPItemLevels` | *(none)* |
| `RefreshTrackedPPItemLevel` | `ItemClass: SoftClassProperty` |
| `RefreshEquippedPPItems` | *(none)* |
| `OnTrackedItemStacksUpdated` | `ItemDef: ClassProperty`, `CountDelta: IntProperty` |

Size `0x2C8`.

```
[000001F4ACD76900] MulticastInlineDelegateProperty /Script/Sparta.PPItemsComponent:OnPPItemEquipped [o: 278] [n: 5866DD] [c: 00007FF721DE9010] [owr: 00007FF46F039938] [df: 00007FF46F3CFD40]
[000001F49CD2C5E0] MapProperty /Script/Sparta.PPItemsComponent:PPItemLevels [o: 170] [n: 586677] [c: 00007FF721DE8F70] [owr: 00007FF46F039938] [kp: 000001F4ACD76500] [vp: 000001F4ACD60F60]
[000001F4ACD76880] BoolProperty /Script/Sparta.PPItemsComponent:bIsTrackingPPItemStacks [o: 270] [n: 5866E6] [c: 00007FF721DE8D80] [owr: 00007FF46F039938]
```

### 3.4 `UPPItemsSaveData`

| Property | Type | Offset |
|---|---|---|
| `ActivePPItem` | `SoftClassProperty` | `0xB8` |
| `PassivePPItem` | `SoftClassProperty` | `0xE0` |
| `PPItemLevels` | `MapProperty (TSoftClassPtr<ItemDef> → int32)` | `0x108` |
| `PPItemFoundAmounts` | `MapProperty (TSoftClassPtr<ItemDef> → int32)` | `0x158` |

No functions. Size `0x1A8`.

```
[000001F4ACD75F00] SoftClassProperty /Script/Sparta.PPItemsSaveData:ActivePPItem [o: B8] [n: 58668F] [c: 00007FF721DF97A0] [owr: 00007FF46F039738] [mc: 00007FF46F067338]
```

### 3.5 `UWeaponsComponent`

| Property | Type | Offset |
|---|---|---|
| `DefaultWeapons` | `ArrayProperty (TSoftClassPtr<ASpartaWeapon>)` | `0xB8` |
| `OnWeaponEquipped` | `MulticastInlineDelegateProperty` | `0xC8` |
| `OnWeaponUnequipped` | `MulticastInlineDelegateProperty` | `0xD8` |
| `OnDefaultWeaponsSpawned` | `MulticastInlineDelegateProperty` | `0xE8` |
| `EquippedWeapons` | `MapProperty (GameplayTag → ASpartaWeapon*)` | `0xF8` |

| Function | Parameters (name: type) |
|---|---|
| `EquipWeaponToSlot` | `Weapon: ObjectProperty of SpartaWeapon`, `bGrantAbilities: BoolProperty` |
| `SpawnAndEquipWeaponOfClass` | `WeaponClass: ClassProperty`, `ReturnValue: ObjectProperty of SpartaWeapon` |
| `RemoveAllWeapons` | *(none)* |
| `RemoveWeaponFromSlot` | `WeaponSlot: StructProperty of GameplayTag` |
| `RemoveWeaponsFromSlot` | `WeaponSlots: StructProperty of GameplayTagContainer` |
| `HideAllWeapons` | `bHide: BoolProperty` |
| `HideWeaponsInSlots` | `WeaponSlots: StructProperty of GameplayTagContainer` |
| `ShowWeaponsInSlots` | `WeaponSlots: StructProperty of GameplayTagContainer` |
| `StowWeaponsInSlots` | `WeaponSlots: StructProperty of GameplayTagContainer` |
| `PutInHandsWeaponsInSlots` | `WeaponSlots: StructProperty of GameplayTagContainer` |
| `GetAllWeapons` | `ReturnValue: ArrayProperty of ObjectProperty (SpartaWeapon)` |
| `GetWeaponInHand` | `ReturnValue: ObjectProperty of SpartaWeapon` |
| `GetWeaponInSlot` | `WeaponSlot: StructProperty of GameplayTag`, `ReturnValue: ObjectProperty of SpartaWeapon` |
| `GetWeaponsInSlots` | `WeaponSlots: StructProperty of GameplayTagContainer`, `ReturnValue: ArrayProperty of ObjectProperty (SpartaWeapon)` |
| `GetWeaponTypeFromSlot` | `WeaponSlot: StructProperty of GameplayTag`, `ReturnValue: ClassProperty` |
| `OnWeaponDestroyed` | `DestroyedActor: ObjectProperty of Actor` |
| `OnOwnerPossess` | `Pawn: ObjectProperty of Pawn`, `OldController: ObjectProperty of Controller`, `NewController: ObjectProperty of Controller` |

Size `0x150`. Note: this holds **actors**, not item instances — weapon ownership/level lives elsewhere.

```
[000001F4ACE20E20] MapProperty /Script/Sparta.WeaponsComponent:EquippedWeapons [o: F8] [n: 58A05A] [c: 00007FF721DE8F70] [owr: 00007FF46F063938] [kp: 000001F4ACE1D480] [vp: 000001F4ACE1D400]
[000001F4ACE1D280] MulticastInlineDelegateProperty /Script/Sparta.WeaponsComponent:OnWeaponEquipped [o: C8] [n: 47BF7] [c: 00007FF721DE9010] [owr: 00007FF46F063938] [df: 00007FF46F3D1D88]
```

### 3.6 `UUpgradesComponent`

| Property | Type | Offset |
|---|---|---|
| `UpgradeTable` | `ObjectProperty of /Script/Engine.DataTable` | `0xB8` |

| Function | Parameters |
|---|---|
| `AddUpgradeLevel` | `Upgrade: NameProperty` |
| `GetUpgradeLevel` | `Upgrade: NameProperty`, `ReturnValue: IntProperty` |
| `IsUpgradeAtMaxLevel` | `Upgrade: NameProperty`, `ReturnValue: BoolProperty` |

Size `0x110`. Note: this class is **DataTable-row-name keyed** (`FName`), unlike
`PlayerExpComponent`/`BPFL_Upgrades` which are keyed by `TSoftClassPtr<USpartaItemDefinition>`.

```
[000001F4ACE1C080] ObjectProperty /Script/Sparta.UpgradesComponent:UpgradeTable [o: B8] [n: 1375C] [c: 00007FF721DE9420] [owr: 00007FF46F063738] [pc: 00007FF46EF31938]
```

### 3.7 `UTarstoneComponent`

| Property | Type | Offset |
|---|---|---|
| `TarcoreItemDefinition` | `ClassProperty` (`mc:` = SpartaItemDefinition) | `0x120` |
| `TarstoneBuilds` | `MapProperty (GameplayTag → FTarstoneArray)` | `0x128` |
| `TarstoneLevels` | `MapProperty (TSubclassOf<ItemDef> → FTarstoneLevelData)` | `0x178` |
| `TarstoneSupportBuild` | `ArrayProperty (TSubclassOf<ItemDef>)` | `0x1C8` |
| `EquippedTarstoneItemInstances` | `MapProperty (TSubclassOf<ItemDef> → SpartaItemInstance*)` | `0x1D8` |
| `EquippedSupportTarstoneItemInstances` | `MapProperty (TSubclassOf<ItemDef> → SpartaItemInstance*)` | `0x228` |
| `OnTarstoneLevelUp` | `MulticastInlineDelegateProperty` | `0x278` |
| `OnTarstoneExpUp` | `MulticastInlineDelegateProperty` | `0x288` |
| `OnTarstoneEquip` | `MulticastInlineDelegateProperty` | `0x298` |
| `OnTarstoneUnequip` | `MulticastInlineDelegateProperty` | `0x2A8` |
| `OnSupportTarstoneEquip` | `MulticastInlineDelegateProperty` | `0x2B8` |
| `OnSupportTarstoneUnequip` | `MulticastInlineDelegateProperty` | `0x2C8` |
| `OnTarstoneReady` | `MulticastInlineDelegateProperty` | `0x2D8` |
| `OnWeaponTarstonesEquipped` | `MulticastInlineDelegateProperty` | `0x2E8` |
| `OnWeaponTarstonesUnequipped` | `MulticastInlineDelegateProperty` | `0x2F8` |
| `OnWeaponTarstoneHandlingComplete` | `MulticastInlineDelegateProperty` | `0x308` |

| Function | Parameters (name: type) |
|---|---|
| `EquipTarstone` | `EquipmentSlot: StructProperty of GameplayTag`, `SlotIndex: IntProperty`, `TarstoneID: ClassProperty`, `ReturnValue: ObjectProperty of SpartaItemInstance` |
| `UnequipTarstone` | `EquipmentSlot: StructProperty of GameplayTag`, `SlotIndex: IntProperty`, `ReturnValue: ObjectProperty of SpartaItemInstance` |
| `EquipSupportTarstone` | `SlotIndex: IntProperty`, `TarstoneID: ClassProperty`, `ReturnValue: ObjectProperty of SpartaItemInstance` |
| `UnequipSupportTarstone` | `SlotIndex: IntProperty`, `ReturnValue: ObjectProperty of SpartaItemInstance` |
| `SetTarstoneLevel` | `Tarstone: ObjectProperty of SpartaItemInstance`, `LevelData: StructProperty of TarstoneLevelData` |
| `AddTarstoneLevel` | `Tarstone: ClassProperty` |
| `ForceAddTarstoneLevel` | `Tarstone: ClassProperty` |
| `AddTarstoneExp` | `Tarstone: ObjectProperty of SpartaItemInstance`, `exp: IntProperty` |
| `AddTarstoneToInventory` | `Tarstone: ClassProperty`, `Level: IntProperty`, `exp: IntProperty` |
| `CanLevelUpTarstone` | `Tarstone: ClassProperty`, `ReturnValue: BoolProperty` |
| `DetermineExpRequirement` | `Level: IntProperty`, `TarstoneExperience: ObjectProperty of ItemFragment_Tarstone`, `ReturnValue: IntProperty` |
| `GetCurrentTarstoneBuildForSlot` | `Slot: StructProperty of GameplayTag`, `ReturnValue: ArrayProperty (TSubclassOf<ItemDef>)` |
| `OnWeaponEquipped` | `Weapon: ObjectProperty of SpartaWeapon` |
| `OnWeaponUnequipped` | `Weapon: ObjectProperty of SpartaWeapon` |
| `OnChangeShellItem` | `OldShell: ClassProperty`, `NewShell: ClassProperty` |

Size `0x320`.

```
[000001F4ACE21000] MapProperty /Script/Sparta.TarstoneComponent:TarstoneLevels [o: 178] [n: 589F1C] [c: 00007FF721DE8F70] [owr: 00007FF46F062B38] [kp: 000001F4ACE1AC80] [vp: 000001F4ACE1AC00]
[000001F4ACE1B000] MulticastInlineDelegateProperty /Script/Sparta.TarstoneComponent:OnTarstoneLevelUp [o: 278] [n: 589FB7] [c: 00007FF721DE9010] [owr: 00007FF46F062B38] [df: 00007FF46F3D1608]
[000001F4ACE1AA80] ClassProperty /Script/Sparta.TarstoneComponent:TarcoreItemDefinition [o: 120] [n: 58A00B] [c: 00007FF721DE8E20] [owr: 00007FF46F062B38] [mc: 00007FF46F067338]
```

### 3.8 `UTarstoneSaveData`

| Property | Type | Offset |
|---|---|---|
| `TarstoneLevels` | `MapProperty (TSubclassOf<ItemDef> → FTarstoneLevelData)` | `0xB8` |
| `TarstoneBuilds` | `MapProperty (GameplayTag → FTarstoneArray)` | `0x108` |
| `TarstoneSupportBuild` | `ArrayProperty (TSubclassOf<ItemDef>)` | `0x158` |

No functions. Size `0x168`.

```
[000001F4ACE211E0] MapProperty /Script/Sparta.TarstoneSaveData:TarstoneLevels [o: B8] [n: 589F1C] [c: 00007FF721DE8F70] [owr: 00007FF46F062938] [kp: 000001F4ACE1A080] [vp: 000001F4ACE1A000]
```

### 3.9 `UArtifactComponent`

| Property | Type | Offset | Notes |
|---|---|---|---|
| `ArtifactTables` | `ArrayProperty of StructProperty (FUpgradeTable)` | `0xB8` | |
| `NarrativeTables` | `ArrayProperty of StructProperty (FUpgradeTable)` | `0xC8` | |
| `DebuffTables` | `ArrayProperty of StructProperty (FUpgradeTable)` | `0xD8` | |

No functions in the dump (the Blueprint subclass
`/Game/Sparta/Core/Player/Artifacts/BP_ArtifactsComponent.BP_ArtifactsComponent_C` is where the
logic is). Size `0xE8`.

`FUpgradeTable` = `{ FGameplayTagQuery Requirement (0x00, 0x48); EUpgradeType UpgradeType (0x48);
UDataTable* UpgradeTable (0x50) }`, size `0x58` (`Sparta.hpp:1612`).

```
[000001F4ACD56600] ArrayProperty /Script/Sparta.ArtifactComponent:ArtifactTables [o: B8] [n: 585E38] [c: 00007FF721DE8910] [owr: 00007FF46F02D938] [ai: 000001F4ACD56580]
[000001F4ACD56580] StructProperty ArtifactTables./Script/Sparta.ArtifactComponent:ArtifactTables [o: 0] [n: 585E38] [c: 00007FF721DF9890] [owr: 000001F4ACD56600] [ss: 00007FF46F486B78]
```

### 3.10 `UArmorComponent`

| Property | Type | Offset |
|---|---|---|
| `CurrentArmorTag` | `StructProperty of GameplayTag` | `0xB8` |

| Function | Parameters |
|---|---|
| `AddArmorOverride` | `WorldContextObject: ObjectProperty of Object`, `InArmorTag: StructProperty of GameplayTag`, `OverrideTime: FloatProperty`, `ReturnValue: StructProperty of ArmorOverrideHandle` |
| `RemoveArmorOverride` | `InHandle: StructProperty of ArmorOverrideHandle` |
| `SetCurrentArmorTag` | `ArmorTag: StructProperty of GameplayTag` |
| `SetDefaultArmor` | `ArmorTag: StructProperty of GameplayTag` |
| `ResetArmorTagToDefault` | *(none)* |

Size `0xD8`.

```
[000001F4ACD56880] StructProperty /Script/Sparta.ArmorComponent:CurrentArmorTag [o: B8] [n: 585E1F] [c: 00007FF721DF9890] [owr: 00007FF46F02D738] [ss: 00007FF46F3890A8]
[000001F4ACD56A80] ObjectProperty /Script/Sparta.ArmorComponent:AddArmorOverride:WorldContextObject [o: 0] [n: 4E6654] [c: 00007FF721DE9420] [owr: 00007FF46F6E2E78] [pc: 00007FF46EE50070]
```

### 3.11 `USlayerSealComponent`

| Property | Type | Offset |
|---|---|---|
| `WeaponActor` | `ObjectProperty of Actor` | `0xB8` |
| `StowedAttachParent` | `ObjectProperty of SceneComponent` | `0xC0` |
| `StowedSocketName` | `NameProperty` | `0xC8` |
| `StowedRelativeTransform` | `StructProperty of Transform` | `0xD0` |
| `TargetActor` | `ObjectProperty of Actor` | `0x130` |
| `TargetWorldOffset` | `StructProperty of Vector` | `0x138` |
| `ArrivalDistance` … `HomingPathRefreshInterval` | `FloatProperty` ×10 | `0x150`–`0x174` |
| `bRefreshRecallPathToMovingSocket` | `BoolProperty` | `0x178` |
| `bSpinWhileActive` | `BoolProperty` | `0x179` |
| `LocalSpinAxis` | `StructProperty of Vector` | `0x180` |
| `SpinDegreesPerSecond` | `FloatProperty` | `0x198` |
| `bOrientToMovement` | `BoolProperty` | `0x19C` |
| `bDrawDebugPath` | `BoolProperty` | `0x19D` |
| `DebugDrawDuration` | `FloatProperty` | `0x1A0` |
| `OnSlayerSealStateChanged` | `MulticastInlineDelegateProperty` | `0x1A8` |
| `OnSealLaunched` | `MulticastInlineDelegateProperty` | `0x1B8` |
| `OnSealReachedTarget` | `MulticastInlineDelegateProperty` | `0x1C8` |
| `OnSealRecalled` | `MulticastInlineDelegateProperty` | `0x1D8` |
| `RuntimeSpline` | `ObjectProperty of SplineComponent` | `0x1E8` |
| `SealState` | `EnumProperty of /Script/Sparta.ESlayerSealState` | `0x1F0` |

| Function | Parameters |
|---|---|
| `ActivateSeal` | `NewTarget: ObjectProperty of Actor`, `ReturnValue: BoolProperty` |
| `RecallSeal` | *(none)* |
| `AttachSealToStowedSocket` | *(none)* |
| `SetWeaponActor` | `NewWeaponActor: ObjectProperty of Actor` |
| `SetStowedAttachTarget` | `NewParent: ObjectProperty of SceneComponent`, `NewSocketName: NameProperty` |
| `GetSealState` | `ReturnValue: EnumProperty of ESlayerSealState` |
| `IsSealActive` | `ReturnValue: BoolProperty` |

Size `0x250`. `ESlayerSealState` = `Stowed=0, Launching=1, Tracking=2, Recalling=3`.

```
[000001F4ACD7A100] EnumProperty /Script/Sparta.SlayerSealComponent:SealState [o: 1F0] [n: 5868BC] [c: 00007FF721DE7380] [owr: 00007FF46F03AD38] [em: 00007FF46F3D2488]
[000001F4ACD79E00] MulticastInlineDelegateProperty /Script/Sparta.SlayerSealComponent:OnSlayerSealStateChanged [o: 1A8] [n: 5868E5] [c: 00007FF721DE9010] [owr: 00007FF46F03AD38] [df: 00007FF46F3D23A8]
```

### 3.12 `USidearmProjectileCacheComponent`

| Property | Type | Offset |
|---|---|---|
| `CachedProjectiles` | `MapProperty (ASpartaWeapon* → FProjectileArray)` | `0xB8` |

| Function | Parameters (name: type) |
|---|---|
| `SpawnSidearmProjectile` | `ProjectileClass: ClassProperty`, `SpawnTransform: StructProperty of Transform`, `CollisionHandlingMethod: EnumProperty of ESpawnActorCollisionHandlingMethod`, `ScaleMethod: EnumProperty of ESpawnActorScaleMethod`, `Owner: ObjectProperty of Actor`, `OwningWeapon: ObjectProperty of SpartaWeapon`, `Instigator: ObjectProperty of Pawn`, `bNeedsFinishSpawn: BoolProperty`, `ReturnValue: ObjectProperty of SpartaProjectile` |
| `CacheProjectile` | `Projectile: ObjectProperty of SpartaProjectile`, `OwningWeapon: ObjectProperty of SpartaWeapon` |
| `OnProjectileDestroyed` | `DestroyedActor: ObjectProperty of Actor` |
| `OnProjectileEndPlay` | `Actor: ObjectProperty of Actor`, `EndPlayReason: ByteProperty` |
| `OnWeaponUnequipped` | `Weapon: ObjectProperty of SpartaWeapon` |

Size `0x108`. Pure ammo-object pooling; no inventory meaning — **not a sync target**.

```
[000001F49CD2C4A0] MapProperty /Script/Sparta.SidearmProjectileCacheComponent:CachedProjectiles [o: B8] [n: 586896] [c: 00007FF721DE8F70] [owr: 00007FF46F03AB38] [kp: 000001F4ACD78480] [vp: 000001F4ACD7A500]
```

---

## 4. Currency

### 4.1 Answer: what the currency counter is

**Currency is not a scalar field.** Each currency is an **item definition**, and the amount a
player holds is the **stack count of that item in `USpartaItemManagerComponent`**.

Proof chain (all four pieces are dump-verified):

1. `/Game/Sparta/Items/Pickups/ID_Gold.ID_Gold_C` is an item definition.
2. `WBP_Player_Currency:GetCurrentStackAmount` returns `CallFunc_GetItemStacksByClass_ReturnValue`
   — i.e. it calls `SpartaItemManagerComponent:GetItemStacksByClass` to read a currency amount.
3. `WBP_Player_Currency:GetCurrencyDataFromID` calls `FindItemDefinitionFragment`
   and takes an `ItemFragment_Display` — the currency's name/icon come from an item def fragment.
4. `BP_PlayerController:S_AddGold(Stacks: IntProperty)` and the sibling `S_Add…` family grant
   currencies by stack deltas.

| Currency item definition | Path |
|---|---|
| Gold | `/Game/Sparta/Items/Pickups/ID_Gold.ID_Gold_C` |
| Gloom | `/Game/Sparta/Items/Pickups/ID_Gloom.ID_Gloom_C` |
| Gloom Gemstone | `/Game/Sparta/Items/Pickups/ID_GloomGemstone.ID_GloomGemstone_C` |
| Gloom Resin | `/Game/Sparta/Items/Pickups/ID_Gloom_Resin.ID_Gloom_Resin_C` |
| Glimpses | `/Game/Sparta/Items/Pickups/ID_Glimpses.ID_Glimpses_C` |
| Ovum | `/Game/Sparta/Items/Pickups/Core/ID_Ovum.ID_Ovum_C` |
| Specimen | `/Game/Sparta/Items/Pickups/ID_Specimen.ID_Specimen_C` |
| Undersap | `/Game/Sparta/Items/Pickups/ID_Undersap.ID_Undersap_C` |
| Tarcore (tarforge core) | `/Game/Sparta/Items/Pickups/Tardust/ID_Tarcore.ID_Tarcore_C` |
| Futilium | `/Game/Sparta/Items/Pickups/Core/ID_AM_01_Futilium.ID_AM_01_Futilium_C` |
| Ventrium | `/Game/Sparta/Items/Pickups/Tarforge/ID_TFM_01_Ventrium.ID_TFM_01_Ventrium_C` |
| Laterite | `/Game/Sparta/Items/Pickups/Tarforge/ID_TFM_02_Laterite.ID_TFM_02_Laterite_C` |
| Dorsalite | `/Game/Sparta/Items/Pickups/Tarforge/ID_TFM_03_Dorsalite.ID_TFM_03_Dorsalite_C` |
| Thoracium | `/Game/Sparta/Items/Pickups/Tarforge/ID_TFM_04_Thoracium.ID_TFM_04_Thoracium_C` |
| Thoracium Prime | `/Game/Sparta/Items/Pickups/Tarforge/ID_TFM_05_Thoracium_Prime.ID_TFM_05_Thoracium_Prime_C` |

Currency HUD widgets (one per currency, all subclasses of `WBP_Player_Currency_C`):
`/Game/Sparta/UI/Player/BP_Player_Currency_Gold.BP_Player_Currency_Gold_C`,
`…_Gloom_C`, `…_Glimpses_C`, `…_Ovums_C`, `…_Tarcores_C`, `…_ShellPoints_C`.

```
[000001F4C131B1A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Gold.ID_Gold_C [n: 192ED2] [c: 00007FF46EEC3D38] [or: 000001F4A2D5D6C0] [sps: 000001F4B021F550]
[000001F4C5834B00] Function /Game/Sparta/UI/Player/WBP_Player_Currency.WBP_Player_Currency_C:GetCurrentStackAmount [n: 64D4D1] [c: 00007FF46EE55538] [or: 000001F61511E080] [f: 00007FF7183D9710]
[000001F61985C720] IntProperty /Game/Sparta/UI/Player/WBP_Player_Currency.WBP_Player_Currency_C:GetCurrentStackAmount:CallFunc_GetItemStacksByClass_ReturnValue [o: 4] [n: 6374DE] [c: 00007FF721DE91F0] [owr: 000001F4C5834B00]
```

### 4.2 The "S_" grant/remove API on the player controller

`/Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C`. Every `S_*` function takes
only the delta — no item class parameter — so the currency class is hard-wired per function.

| Function | Real parameters (compiler temporaries filtered) |
|---|---|
| `S_AddGold` | `Stacks: IntProperty` |
| `S_AddGloom` | `Stacks: IntProperty` |
| `S_AddGlimpses` | `Stacks: IntProperty` |
| `S_AddOvums` | `Stacks: IntProperty` |
| `S_AddTarcores` | `Stacks: IntProperty` |
| `S_AddVentrium` | `Stacks: IntProperty` |
| `S_AddLaterite` | `Stacks: IntProperty` |
| `S_AddDorsalite` | `Stacks: IntProperty` |
| `S_AddThoracium` | `Stacks: IntProperty` |
| `S_AddThoraciumPrime` | `Stacks: IntProperty` |
| `S_AddShellPoints` | `Delta: IntProperty` |
| `S_AddItem` | `RowName: NameProperty`, `Item: ClassProperty` (+ `ItemTable`/`CurrentSoftItem` locals) |
| `S_AddItemQuantity` | `RowName: NameProperty`, `Amount: IntProperty`, `Item: ClassProperty` |
| `S_AddAllItems` | `Item: ClassProperty` |
| `S_AddUpgrade` | `ID: NameProperty`, `UpgradeLevel: IntProperty` |
| `S_RemoveUpgrade` | `ID: NameProperty`, `UpgradeLevel: IntProperty` |
| `S_AddWeaponLevel` | `Delta: IntProperty` |
| `S_AddSidearmLevel` | `Delta: IntProperty` |
| `S_AddNarrativeTag` / `S_RemoveNarrativeTag` | `TagToAdd: StructProperty of GameplayTag` |
| `S_AddAllTarstonesMelee` / `…Sidearm` / `…Support` | *(no real parameters; iterates a DataTable)* |

**This `S_` prefix is the single most sync-relevant fact in this section.**
GUESS: it is a developer/cheat naming convention ("S_" = server/scripted), not a replicated RPC —
nothing in the dump marks these `NetMulticast`/`Server` (the object dump does not carry function
flags), so the sync code must not assume they are RPCs.

```
[000001F61CE0E200] Function /Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C:S_AddGold [n: 662D1F] [c: 00007FF46EE55538] [or: 000001F614CB7510] [f: 00007FF7183D9710]
[000001F61C3E59C0] IntProperty /Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C:S_AddGold:Stacks [o: 0] [n: 55D168] [c: 00007FF721DE91F0] [owr: 000001F61CE0E200]
[000001F61CE0DA00] Function /Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C:S_AddTarcores [n: 66262F] [c: 00007FF46EE55538] [or: 000001F614CB7510] [f: 00007FF7183D9710]
```

### 4.3 Gold multipliers / bonuses

`/Game/Sparta/Core/Player/BPFL_Player.BPFL_Player_C` (a Blueprint function library):

| Function | Real parameters |
|---|---|
| `GetGoldBonus` | `__WorldContext: ObjectProperty of Object`, `ReturnValue: FloatProperty` |
| `GetGoldMultiplier` | `__WorldContext: ObjectProperty of Object`, `ReturnValue: FloatProperty` |
| `GetGoldNGPMultiplier` | `__WorldContext`, `ReturnValue: FloatProperty` (+ `GoldMultiplier: RuntimeFloatCurve`) |
| `GetGoldNightModeMultiplier` | `__WorldContext`, `ReturnValue: FloatProperty` (+ `GloomMultiplier: RuntimeFloatCurve`) |
| `HandleCurrencyDrop` | `DropTable: StructProperty of SpartaCurrencyDropTable`, `__WorldContext: ObjectProperty of Object`, `Success: BoolProperty`, `Item: ClassProperty`, `Stack: IntProperty` |

Backing attributes (on `USpartaPlayerAttributeSet`, `Sparta.hpp:3299–3300`):
`GoldMultiplier` at `0x130`, `GoldBonus` at `0x140` (both `FGameplayAttributeData`, size `0x10`).

```
[000001F4C0DD69F0] IntProperty /Game/Sparta/SaveObjects/BPSO_Progression.BPSO_Progression_C:GoldSpentAtShop [o: 250] [n: 5F7FB6] [c: 00007FF721DE91F0] [owr: 000001F4BE037180]
[000001F622CDC500] StructProperty /Game/Sparta/Core/Player/BPFL_Player.BPFL_Player_C:GetGoldNGPMultiplier:GoldMultiplier [o: 10] [n: 586380] [c: 00007FF721DF9890] [owr: 000001F606FBA900] [ss: 00007FF46F3AE898]
```

### 4.4 Currency drop table struct + persistent currency counters

| Struct `FSpartaCurrencyDropTable` | Type | Offset | Size |
|---|---|---|---|
| `CurrencyType` | `TSubclassOf<USpartaItemDefinition>` | `0x00` | `0x8` |
| `base` | `float` | `0x08` | `0x4` |
| `Min` | `float` | `0x0C` | `0x4` |
| `Max` | `float` | `0x10` | `0x4` |
| `DropChance` | `float` | `0x14` | `0x4` |

Total `0x18` (`Sparta.hpp:648`).

Persistent currency-ish counters found by scanning **every** property whose name contains
Gold/Currency/Money/Soul/Coin/Tarcore (query `ii-04`):

| Owner class | Property | Type | Offset |
|---|---|---|---|
| `/Game/Sparta/SaveObjects/BPSO_Progression.BPSO_Progression_C` | `GoldSpentAtShop` | `IntProperty` | `0x250` |
| `/Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C` | `TotalGoldSpent` | `IntProperty` | `0x108` |
| `/Game/Sparta/Core/SaveObjects/BPSO_Tarforge.BPSO_Tarforge_C` | `StoredTarcores` | `IntProperty` | `0xB8` |
| `/Script/Sparta.QuestStage` | `CurrencyReward` | `IntProperty` | `0x98` |
| `/Script/Sparta.SpartaCharacterDeathData` | `GoldOnDeath` | `IntProperty` | `0x04` |
| `/Script/Sparta.SpartaCharacterDeathData` | `GoldDropChance` | `FloatProperty` | `0x08` |
| `/Script/Sparta.SpartaEnemySpawnerDisplayData` | `GoldOnDeath` | `IntProperty` | `0x1C` |
| `/Script/Sparta.SpartaEnemySpawnerDisplayData` | `GoldDropChance` | `FloatProperty` | `0x20` |
| `/Game/Sparta/Core/Player/Save/BP_SpartaPlayerSaveObject.BP_SpartaPlayerSaveObject_C` | `IncrementCumulativeCurrency(Class, Amount)` | function | — |
| `/Game/Sparta/Core/Common/Currency/BP_CurrencyBase.BP_CurrencyBase_C` | `Amount` | `IntProperty` | `0x2E0` |

**`GoldSpentAtShop` / `TotalGoldSpent` / `StoredTarcores` are statistics / per-object state, NOT
the player's gold balance** — the balance is the `ID_Gold` stack count (§4.1).

```
[000001F4A8872780] ClassProperty /Script/Sparta.SpartaCurrencyDropTable:CurrencyType [o: 0] [n: 55C944] [c: 00007FF721DE8E20] [owr: 00007FF46F4885B8] [mc: 00007FF46F067338]
[000001F4A861BD80] FloatProperty /Script/Sparta.SpartaCurrencyDropTable:base [o: 8] [n: 207E0] [c: 00007FF721DE9380] [owr: 00007FF46F4885B8]
[000001F4A861BED0] FloatProperty /Script/Sparta.SpartaCurrencyDropTable:DropChance [o: 14] [n: 55C93E] [c: 00007FF721DE9380] [owr: 00007FF46F4885B8]
[000001F4C031CFE0] IntProperty /Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C:TotalGoldSpent [o: 108] [n: 67B438] [c: 00007FF721DE91F0] [owr: 000001F4CB2478A0]
```

### 4.5 World currency actors (dropped coins)

`/Game/Sparta/Core/Common/Currency/` — one Blueprint per currency kind plus a spawner:

| Class | Parent | Notes |
|---|---|---|
| `BP_CurrencyBase_C` | `/Script/Engine.Actor` | base coin actor: `Amount(0x2E0)`, `CurrencyClass(0x3C0)`, `bGrantImmediately(0x408)`, `EnabledAutoCollect(0x2E4)`, `Attract()`/`GrantCurrency()`/`OnPlayerOverlap()`/`Initialize()` |
| `BP_Currency_Gold_C` | `BP_CurrencyBase_C` | adds `PickUp()`, `ShouldAutoPickUp()`, `EnableInteractable()`, `DisableInteractable()` |
| `BP_Currency_Gloom_C` | `BP_CurrencyBase_C` | |
| `BP_Currency_Gloom_MultiSpawner_C` | `BP_CurrencyBase_C` | `InitWithLocations()` |
| `BP_Currency_Resolve_C` | `BP_CurrencyBase_C` | overrides `GrantCurrency()`, `OverlapAfterDelay()` |
| `BP_Currency_Specimen_C` | `BP_CurrencyBase_C` | |
| `BP_CurrencySpawner_C` | `/Script/Engine.Actor` | `CurrencyClass` `SoftClassProperty` `0x2E0`, `CurrencyPerActor` `IntProperty` `0x2D4`, `CurrencySpawnRequest` `StructProperty` `0x308`; functions `Spawn()`, `SpawnActor()`, `CalculateCurrencyPerActor()`, `CurrencyPreSpawn()`, `HandleImmediateGrant()` |
| `BP_CurrencySpawner_Gold_C` / `_Gloom_C` / `_Resolve_C` | `BP_CurrencySpawner_C` | per-currency spawners |

`BP_CurrencyBase:GrantCurrency()` has only Blueprint compiler temporaries as parameters (no real
inputs) — it reads the actor's own `Amount` + `CurrencyClass` fields.

```
[000001F4C064CA90] BlueprintGeneratedClass /Game/Sparta/Core/Common/Currency/BP_CurrencyBase.BP_CurrencyBase_C [n: 28CEB3] [c: 00007FF46EEC3D38] [or: 000001F4D1C16C80] [sps: 00007FF46EE52028]
[000001F614C55330] IntProperty /Game/Sparta/Core/Common/Currency/BP_CurrencyBase.BP_CurrencyBase_C:Amount [o: 2E0] [n: 557DAB] [c: 00007FF721DE91F0] [owr: 000001F4C064CA90]
[000001F4C064AE10] BlueprintGeneratedClass /Game/Sparta/Core/Common/Currency/BP_Currency_Gold.BP_Currency_Gold_C [n: 2DEA80] [c: 00007FF46EEC3D38] [or: 000001F4D1C177C0] [sps: 000001F4C064CA90]
```

---

## 5. Drops — how items reach the player

### 5.1 The drop pipeline (order established from function names + struct fields)

`enemy dies` → `GA_DeathBase_C` (a `GA_SpartaAbility_C`) → `GetCharDeathData()` returns
`FSpartaCharacterDeathData` → `DropGold` / `DropGloom` / `DropItems` / `DropOwningWeapons` →
spawns `BP_CurrencySpawner_C` (currency) or reward actors → player overlaps the coin/pickup →
`SpartaItemManagerComponent:AddItemByClass` → `OnItemStacksUpdated` fires.

### 5.2 `FSpartaCharacterDeathData` (per-enemy drop config)

| Field | Type | Offset | Size |
|---|---|---|---|
| `GloomOnDeath` | `int32` | `0x00` | `0x4` |
| `GoldOnDeath` | `int32` | `0x04` | `0x4` |
| `GoldDropChance` | `float` | `0x08` | `0x4` |
| `ItemDropTable` | `TMap<TSubclassOf<USpartaItemDefinition>, float>` | `0x10` | `0x50` |

Total `0x60` (`Sparta.hpp:515`). Dump-verified:

```
[000001F4A861BBC0] IntProperty /Script/Sparta.SpartaCharacterDeathData:GloomOnDeath [o: 0] [n: 55C96C] [c: 00007FF721DE91F0] [owr: 00007FF46F488678]
[000001F4A861BC30] IntProperty /Script/Sparta.SpartaCharacterDeathData:GoldOnDeath [o: 4] [n: 55C965] [c: 00007FF721DE91F0] [owr: 00007FF46F488678]
[000001F495E1E340] MapProperty /Script/Sparta.SpartaCharacterDeathData:ItemDropTable [o: 10] [n: 55C94B] [c: 00007FF721DE8F70] [owr: 00007FF46F488678] [kp: 000001F4A8872700] [vp: 000001F4A861BD10]
[000001F4A8872700] ClassProperty ItemDropTable./Script/Sparta.SpartaCharacterDeathData:ItemDropTable_Key [o: 0] [n: 55C953] [c: 00007FF721DE8E20] [owr: 000001F495E1E340] [mc: 00007FF46F067338]
```

### 5.3 `/Game/Sparta/Core/Ability/GA_DeathBase.GA_DeathBase_C` — drop functions

Parent: `GA_SpartaAbility_C`. Real parameters (compiler temporaries removed):

| Function | Real parameters |
|---|---|
| `DropGold` | `DeathData: StructProperty of SpartaCharacterDeathData`, `GoldBonus: IntProperty`, `GoldWithMultiplier: IntProperty`, `BaseGold: IntProperty` |
| `DropGloom` | `DeathData: StructProperty of SpartaCharacterDeathData`, `GloomBonus: IntProperty`, `GloomWithMultiplier: IntProperty`, `BaseGloom: IntProperty` |
| `DropCurrency` | `ValidInstigatorClasses: ArrayProperty` (class filter) |
| `DropItems` | `CurrentDropItemDef: ClassProperty` |
| `DropOwningWeapons` | `WeaponSlots: StructProperty of GameplayTagContainer`, `CustomLifeSpan: DoubleProperty`, `CurrentWeapon: ObjectProperty of SpartaWeapon`, `CurrentTag: StructProperty of GameplayTag` |
| `GetCharDeathData` | `Death: StructProperty of SpartaCharacterDeathData` (out) |
| `GetDropItems` | `ReturnValue: MapProperty` |
| `GetAdditionalDropItems` | `Success: BoolProperty` (out), `ReturnValue: MapProperty` |
| `ShouldDropGold` / `ShouldDropGloom` / `ShouldDropItems` / `ShouldDropWeapons` / `ShouldDropGlobal` | `ReturnValue: BoolProperty` |
| `PostGoldSpawn` / `PostGloomSpawn` | `Actor: ObjectProperty of Actor` |
| `SetOwnerLifeSpan`, `EndDeath`, `DeathCleanup`, `GrantDeathState`, `RemoveDeathState`, `PlayDeathMontage`, `GetDeathMontage` | (see raw evidence) |

```
/Game/Sparta/Core/Ability/GA_DeathBase.GA_DeathBase_C	DropGold	DeathData: StructProperty of /Script/Sparta.SpartaCharacterDeathData , GoldBonus: IntProperty , GoldWithMultiplier: IntProperty , BaseGold: IntProperty , K2Node_CreateDelegate_OutputDelegate: DelegateProperty , CallFunc_GetGoldNightModeMultiplier_ReturnValue
/Game/Sparta/Core/Ability/GA_DeathBase.GA_DeathBase_C	DropCurrency	ValidInstigatorClasses: ArrayProperty of ValidInstigatorClasses./Game/Sparta/Core/Ability/GA_DeathBase.GA_DeathBase_C:DropCurrency:ValidInstigatorClasses
/Game/Sparta/Core/Ability/GA_DeathBase.GA_DeathBase_C	PostGoldSpawn	Actor: ObjectProperty of /Script/Engine.Actor , K2Node_DynamicCast_AsBP_Currency_Spawner: ObjectProperty of /Game/Sparta/Core/Common/Currency/BP_CurrencySpawner.BP_CurrencySpawner_C
```

(those three lines are the `ii-16`/`ii-20` TSV output, reproduced verbatim; the same rows exist
as dump lines — the dump line for `DropGold` itself:)

```
[000001F4BDA88600] Function /Game/Sparta/Core/Ability/GA_DeathBase.GA_DeathBase_C:DropGold [n: 66E7FD] [c: 00007FF46EE55538] [or: 000001F615241870] [f: 00007FF7183D9710]
```

### 5.4 Reward-data objects (`/Game/Sparta/Items/Pickups/Core/`)

`BP_RewardData_C : /Script/Sparta.SpartaInstancedObject` is the base of a small polymorphism for
"what to drop":

| Class | Parent | Extra properties (offset) | Function with signature |
|---|---|---|---|
| `BP_RewardData_C` | `/Script/Sparta.SpartaInstancedObject` | `DataWorldContext` `ObjectProperty` `0x28` | `GrantReward()` (no real params), `UpdateWorldContext(DataWorldContext: ObjectProperty)` |
| `BP_RD_Currency_C` | `BP_RewardData_C` | `CurrencyData` `StructProperty of SpartaCurrencyDropTable` `0x30` | `GrantReward()` → `HandleCurrencyDrop(Item, Stack, Success)` |
| `BP_RD_Currency_Gold_C` | `BP_RD_Currency_C` | — | — |
| `BP_RD_Currency_Gloom_C` | `BP_RD_Currency_C` | — | — |
| `BP_RD_Currency_Random_C` | `BP_RewardData_C` | `CurrencyData` `0x30` | — |
| `BP_RD_PickUpItem_C` | `BP_RewardData_C` | `ItemType` `GameplayTagContainer` `0x30`, `ItemRarity` `GameplayTagContainer` `0x50`, `OverrideAmount` `IntProperty` `0x70`, `SpecificItem` `SoftClassProperty` `0x78` | `RequestPickUpItem(OverrideBiome: GameplayTag, OverrideSeason: GameplayTag, Success: Bool, Item: ClassProperty, Amount: IntProperty)`, `GetCurrentBiomeAndSeason(Biome: GameplayTag, Season: GameplayTag)` |
| `BP_RD_Artifact_C` | `BP_RewardData_C` | `UseRarity` `BoolProperty` `0x30`, `ArtifactRarity` `ByteProperty` `0x31`, `RarityData` `MapProperty` `0x38`, `CustomArtifact` `DataTableRowHandle` `0x88` | — |
| `BP_RD_Tarstone_C` | `BP_RewardData_C` | `Class` `ClassProperty` `0x30` | — |

```
[000001F4C43EF1C0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Core/BP_RewardData.BP_RewardData_C [n: 34C81A] [c: 00007FF46EEC3D38] [or: 000001F4D301B960] [sps: 00007FF46F04DB38]
[000001F8B61CAA80] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Core/BP_RD_Currency.BP_RD_Currency_C [n: 3335DF] [c: 00007FF46EEC3D38] [or: 000001F8BBEB8170] [sps: 000001F4C43EF1C0]
[000001F8BD319100] StructProperty /Game/Sparta/Items/Pickups/Core/BP_RD_Currency.BP_RD_Currency_C:CurrencyData [o: 30] [n: 8064DF] [c: 00007FF721DF9890] [owr: 000001F8B61CAA80] [ss: 00007FF46F4885B8]
```

`HandleCurrencyDrop` full row (from `ii-19`):

```
/Game/Sparta/Core/Player/BPFL_Player.BPFL_Player_C	HandleCurrencyDrop	DropTable: StructProperty of /Script/Sparta.SpartaCurrencyDropTable , __WorldContext: ObjectProperty of /Script/CoreUObject.Object , Success: BoolProperty , Item: ClassProperty , Stack: IntProperty , L_Base: FloatProperty , L_RandomValue: DoubleProperty , L_StackCount: IntProperty , L_ItemDef: ClassProperty , CallFunc_RandomBoolWithWeight_ReturnValu…
```

### 5.5 World pickup actors

| Class | Parent | Properties (offset) | Functions |
|---|---|---|---|
| `/Script/Sparta.PickupableActor` | `/Script/Engine.Actor` | `ItemDef` `StructProperty of FPickupTemplate` `0x2A8` | `AddPickupToItemManager(ItemComponent: ObjectProperty of SpartaItemManagerComponent)` |
| `/Game/Sparta/Items/Pickups/BP_PickupBase.BP_PickupBase_C` | `/Script/Engine.Actor` | `Pickup` `BPC_InteractionHandler_C` `0x2B0`, `Sphere` `SphereComponent` `0x2B8`, `Amount` `IntProperty` `0x2C8`, `SpawnChance` `DoubleProperty` `0x2D0`, `Destroy` `BoolProperty` `0x2D8`, `InteractText` `TextProperty` `0x2E0`, `OnPickedUp` `MulticastInlineDelegateProperty` `0x2F0` | `PickUp()`, `DefaultInteractionInit()`, `SetDefaultInteractText()` |
| `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | `BP_PickupParticleFX_C` | `PickUpItem` `ClassProperty` `0x330`, `IdleLoop` `AudioEvent*` `0x338`, `LoopID` `AudioEventId` `0x340`, `StartIdleLoopOnBeginPlay` `0x348`, `bPickupStopsIdleLoop` `0x349`, `SpartaSave` `SpartaSaveComponent` `0x320`, `PickUpMesh` `StaticMeshComponent` `0x328` | `PickUp()`, `IsCollected()`→bool, `SetAsCollected()`, `GetMySaveData()`→`BPSO_CraftingItemPickup_C`, `RespawnsAfterPickup()`→bool, `ExecuteOnPickupBehaviors()`, `EnableRewardActor()`, `DisableRewardActor()`, `OnPostLoad()`, `CleanseCorruptionActor()`, `SetCustomMeshMaterial(MyMaterialInstance: MaterialInstanceConstant)`, `SpawnPickUpVFX()`, `StartIdleLoop()`, `StopIdleLoop()` |
| `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup_Common_C` (+ `_Rare_Man`, `_Rare_Nat`, `_Uncommon_Man`, `_Uncommon_Nat`, `_Futilium_Group_x6`) | `BP_CraftingItemPickup_C` | — | — |
| `/Game/Sparta/Items/Pickups/BP_TarStonePickup_Man.BP_TarStonePickup_Man_C` | `BP_CraftingItemPickup_C` | — | — |
| `/Game/Sparta/Items/Pickups/BP_CurrencyBag_Pickup.BP_CurrencyBag_Pickup_C` | `BP_PickupParticleFX_C` | `CurrencyID` `ClassProperty` `0x328` | `GetMySaveData()`, `InitStaticNiagara()` |
| `/Game/Sparta/Environments/ItemPool/BP_ItemGiver.BP_ItemGiver_C` | `/Script/Engine.Actor` | `DefaultSceneRoot` `0x2B0` | `OnGrantItem()`, `RemoveParticle()` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_InteractableBase_LootCrate.BP_InteractableBase_LootCrate_C` | `BP_InteractableBase_C` | `StaticMesh` `StaticMeshComponent` `0x308` | `ReceiveBeginPlay()` — reads an `F_ShopItem` DataTable row, then `BeginDeferredActorSpawnFromClass`→`FinishSpawningActor` of `BP_ISI_TarWine_C` |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_ChainSarcophagusDrop.BP_ChainSarcophagusDrop_C` | `/Script/Engine.Actor` | — | — |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_SingleArrowDrop.BP_SingleArrowDrop_C` | `/Script/Engine.Actor` | — | — |

`BP_CraftingItemPickup` persistence is a **per-pickup save object**:
`/Game/Sparta/Core/SaveObjects/BPSO_CraftingItemPickup.BPSO_CraftingItemPickup_C`
(`ItemCollected` `BoolProperty` `0xB8`, `ItemInitialized` `BoolProperty` `0xB9`).
That pair is what makes a picked-up crafting node stay gone.

```
[00007FF46F037938] Class /Script/Sparta.PickupableActor [n: 47B7B] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
[000001F4ACD5FF00] StructProperty /Script/Sparta.PickupableActor:ItemDef [o: 2A8] [n: 525597] [c: 00007FF721DF9890] [owr: 00007FF46F037938] [ss: 00007FF46F486AB8]
[000001F7557BB100] ClassProperty /Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C:PickUpItem [o: 330] [n: 63593F] [c: 00007FF721DE8E20] [owr: 000001F7557646C0] [mc: 000001F4B021F550]
[000001F4C9009780] BoolProperty /Game/Sparta/Core/SaveObjects/BPSO_CraftingItemPickup.BPSO_CraftingItemPickup_C:ItemCollected [o: B8] [n: 658BB2] [c: 00007FF721DE8D80] [owr: 000001F4CB243160]
```

### 5.6 Give-item fragments and dialogue events

| Class | Property/Function | Detail |
|---|---|---|
| `/Script/Sparta.ItemFragment_GiveItemToPlayer` | `ItemToGive` | `SoftClassProperty` `0x28` — fragment that grants an item when its owner fragment runs |
| `/Script/Sparta.ItemFragment_GiveItemToShell` | `ItemToGive` | `SoftClassProperty` `0x28` |
| `/Game/Sparta/Dialogue/Blueprints/DialogueEvents/BP_DE_GainItem.BP_DE_GainItem_C` | `DataRow: DataTableRowHandle` `0x30`; `ExecuteUbergraph…` | dialogue event: grant the item named by a DataTable row |
| `…/BP_DE_GiveSpecificItem.BP_DE_GiveSpecificItem_C` | `ItemClass: ClassProperty` `0x30`, `OnlyGiveOnce: GameplayTag` `0x38` | dialogue event: grant one specific item, once-gated by tag |
| `…/BP_DE_LoseItem.BP_DE_LoseItem_C` | `DataRow: DataTableRowHandle` `0x30`, `Item: ClassProperty` `0x40` | dialogue event: remove an item |
| `…/BP_DE_GiveSeal.BP_DE_GiveSeal_C`, `…/BP_DE_GiveWeaponMap.BP_DE_GiveWeaponMap_C` | (out of area) | seal / weapon-map grants |

```
[00007FF46F035B38] Class /Script/Sparta.ItemFragment_GiveItemToPlayer [n: 47A64] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F035138]
[000001F8A0B07B80] ClassProperty /Game/Sparta/Dialogue/Blueprints/DialogueEvents/BP_DE_GiveSpecificItem.BP_DE_GiveSpecificItem_C:ItemClass [o: 30] [n: 525653] [c: 00007FF721DE8E20] [owr: 000001F89743F8E0] [mc: 00007FF46F067338]
[000001F8F3C7C780] StructProperty /Game/Sparta/Dialogue/Blueprints/DialogueEvents/BP_DE_GainItem.BP_DE_GainItem_C:DataRow [o: 30] [n: 86525B] [c: 00007FF721DF9890] [owr: 000001F8D3845FB0] [ss: 00007FF46F39DEA0]
```

### 5.7 Chests / breakables

* Loot crates are `BP_InteractableBase_LootCrate_C` → they *spawn* a shop-item actor
  (`BP_ISI_TarWine_C` observed in the ubergraph) rather than writing to the inventory directly.
* "Breakable" props that give items were only found as normal pickups in this area of the dump —
  the breakable-prop machinery itself is world-state territory (other agent).
* `BP_Interactable_ItemPool_C:GrantCurrency()` exists (`/Game/Sparta/Core/Common/Interactions/
  Blueprints/BP_Interactable_ItemPool.BP_Interactable_ItemPool_C`) — an interactable that grants
  currency directly.

```
[000001F8AE84FC70] BlueprintGeneratedClass /Game/Sparta/Core/Common/Interactions/Blueprints/BP_InteractableBase_LootCrate.BP_InteractableBase_LootCrate_C [n: 161F6B] [c: 00007FF46EEC3D38] [or: 000001F893C1CA90] [sps: 000001F620594DE0]
[000001F8AEEA3600] Function /Game/Sparta/Core/Common/Interactions/Blueprints/BP_InteractableBase_LootCrate.BP_InteractableBase_LootCrate_C:ExecuteUbergraph_BP_InteractableBase_LootCrate [n: 8094A5] [c: 00007FF46EE55538] [or: 000001F8AE84FC70] [f: 00007FF7183D9710]
```

---

## 6. Merchants, shops, vendors

### 6.1 Class table

| Class | Parent | Role |
|---|---|---|
| `/Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C` | `/Script/Sparta.SpartaSaveObject` | **persistent shop stock & spend counters** |
| `/Game/Sparta/UI/Menu/Merchant/BP_ShopHandlerBase.BP_ShopHandlerBase_C` | `/Script/Engine.Actor` | per-shop actor: id, name, stock map, trade events |
| `…/BP_ShopHandler_BrigBlackMarket_C` | `BP_ShopHandlerBase_C` | |
| `…/BP_ShopHandler_Hunters_C` | `BP_ShopHandlerBase_C` | |
| `…/BP_ShopHandler_Maps_C` | `BP_ShopHandlerBase_C` | |
| `…/BP_ShopHandler_Materials_C` | `BP_ShopHandlerBase_C` | |
| `…/BP_ShopHandler_NightMerchant_C` | `BP_ShopHandlerBase_C` | |
| `…/BP_ShopHandler_RareFindings_C` | `BP_ShopHandlerBase_C` | |
| `…/BP_ShopHandler_Sell_C` | `BP_ShopHandlerBase_C` | sell-side handler |
| `…/BP_ShopHandler_Tarstones_C` | `BP_ShopHandlerBase_C` | |
| `…/BP_ShopHandler_Tavern_C` | `BP_ShopHandlerBase_C` | |
| `…/BPFL_Shops.BPFL_Shops_C` | `/Script/Engine.BlueprintFunctionLibrary` | `CreateShopWidget(...)` |
| `/Game/Sparta/Dialogue/NPCs/BP_Merchant.BP_Merchant_C` | `/Game/Sparta/Core/AI/BP_NPC.BP_NPC_C` | merchant NPC: `Shops` array, buy/cant-buy dialogue |
| `/Game/Sparta/Core/World/Progression/BP_KeepSakeVendor.BP_KeepSakeVendor_C` | `BP_NPC_C` | keepsake vendor |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_ShopItem.BP_Interactable_ShopItem_C` | `BP_InteractableBase_C` | one on-shelf item you interact with |
| `/Game/Sparta/Items/Shop/BP_ShopItemRandomizer.BP_ShopItemRandomizer_C` | `/Script/Engine.Actor` | restocks a shop |
| `/Game/Sparta/UI/Menu/Merchant/WBP_MGT_Shop.WBP_MGT_Shop_C` | `/Script/Sparta.SpartaWidget` | shop UI (menu territory — listed for the data it consumes) |

```
[000001F4CB2478A0] BlueprintGeneratedClass /Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C [n: 4BE016] [c: 00007FF46EEC3D38] [or: 000001F4A708E6B0] [sps: 00007FF46F038338]
[000001F61C3534F0] BlueprintGeneratedClass /Game/Sparta/UI/Menu/Merchant/BP_ShopHandlerBase.BP_ShopHandlerBase_C [n: 211700] [c: 00007FF46EEC3D38] [or: 000001F4C7050B00] [sps: 00007FF46EE52028]
[000001F8A77B8350] BlueprintGeneratedClass /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_ShopItem.BP_Interactable_ShopItem_C [n: 40E1D9] [c: 00007FF46EEC3D38] [or: 000001F8A0B4EB60] [sps: 000001F620594DE0]
```

### 6.2 `BPSO_ShopData_C` — the shop save object

| Property | Type | Offset | Notes |
|---|---|---|---|
| `CachedItemsMap` | `MapProperty (TSoftClassPtr<USpartaItemDefinition> → int32)` | `0xB8` | **current stock: item def → remaining count** |
| `TotalGoldSpent` | `IntProperty` | `0x108` | |
| `StolenItems` | `BoolProperty` | `0x10C` | set by `StealAllItems()` |

Size `0x10D`. No functions in the dump (the whole class is 3 properties).

```
[000001F4CC21BD20] MapProperty /Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C:CachedItemsMap [o: B8] [n: 63CE09] [c: 00007FF721DE8F70] [owr: 000001F4CB2478A0] [kp: 000001F4CBA12C80] [vp: 000001F4C031CF70]
[000001F4CBA12C80] SoftClassProperty CachedItemsMap./Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C:CachedItemsMap [o: 0] [n: 63CE09] [c: 00007FF721DF97A0] [owr: 000001F4CC21BD20] [mc: 00007FF46F067338]
[000001F4CBA12C00] BoolProperty /Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C:StolenItems [o: 10C] [n: 63D110] [c: 00007FF721DE8D80] [owr: 000001F4CB2478A0]
```

### 6.3 `BP_ShopHandlerBase_C` — properties

| Property | Type | Offset |
|---|---|---|
| `SellerID` | `StructProperty of GameplayTag` | `0x2E0` |
| `ShopID` | `NameProperty` | `0x2E8` |
| `ShopName` | `TextProperty` | `0x2F0` |
| `ShopSubname` | `TextProperty` | `0x300` |
| `GlobalShopCondition` | `ObjectProperty of BP_ConditionObject_C` | `0x310` |
| `ShopType` | `ByteProperty` (hpp/enum: `Enum_ShopType`) | `0x318` |
| `DefaultItemList` | `MapProperty (TSoftClassPtr<ItemDef> → int32)` | `0x320` |
| `bShouldBindToOvumChange` | `BoolProperty` | `0x370` |
| `CameraTransform` | `StructProperty of Transform` | `0x380` |
| `ShopVisuals` | `ArrayProperty of ObjectProperty (Actor)` | `0x3E0` |
| `CachedPlayerCharacter` | `ObjectProperty of Character` | `0x3F0` |
| `CachedOriginalItemList` | `MapProperty` | `0x3F8` |
| `CachedPlayerItemManager` | `ObjectProperty of SpartaItemManagerComponent` | `0x448` |
| `MyCameraState` | `ObjectProperty of CSCameraState` | `0x450` |
| `ShopCloseStarted` | `MulticastInlineDelegateProperty` | `0x458` |
| `ShopCloseCompleted` | `MulticastInlineDelegateProperty` | `0x468` |
| `MyShopWidget` | `ObjectProperty of WBP_MGT_Shop_C` | `0x478` |
| `ShopOpen` | `MulticastInlineDelegateProperty` | `0x480` |
| `TradeComplete` | `MulticastInlineDelegateProperty` | `0x490` |
| `TradeFailed` | `MulticastInlineDelegateProperty` | `0x4A0` |
| `ItemSelected` | `MulticastInlineDelegateProperty` | `0x4B0` |

(plus SCS-generated `Billboard`/`TextRender`/`Arrow`/`MyCamera`/`Scene`/`ShopSaveComponent`
component properties at `0x2B0`–`0x2D8`.)

Delegate signatures (from the `*__DelegateSignature` functions):

| Delegate | Signature |
|---|---|
| `ShopOpen` / `ShopCloseStarted` / `ShopCloseCompleted` | *(no params)* |
| `TradeComplete` | `TradeType: ByteProperty`, `TradeItem: SoftClassProperty`, `TradeQuantity: IntProperty` |
| `TradeFailed` | `Reason: NameProperty` |
| `ItemSelected` | `Widget: ObjectProperty of WBP_ButtonShopEntry_C`, `SoftItem: SoftClassProperty` |

### 6.4 `BP_ShopHandlerBase_C` — functions (real parameters)

| Function | Real parameters |
|---|---|
| `OnTradeEvent` | `Widget: ObjectProperty of WBP_ButtonShopEntry_C`, `TradeType: ByteProperty`, `TradeItem: SoftClassProperty`, `TradeQuantity: IntProperty`, `TradeValue: MapProperty` |
| `PostTradeEvent` | `Widget`, `TradeType: ByteProperty`, `TradeItem: SoftClassProperty`, `TradeQuantity: IntProperty` |
| `OnTradeFailEvent` | `Reason: NameProperty` |
| `OnItemSelectedEvent` | `Widget: ObjectProperty of WBP_ButtonShopEntry_C`, `SoftItem: SoftClassProperty` |
| `OnAnyStackChanged` | `ItemDef: ClassProperty`, `Count: IntProperty` |
| `UpdateSaveData` | `SaveObject: ObjectProperty of BPSO_ShopData_C`, `TradeItem: SoftClassProperty`, `TradeQuantity: IntProperty`, `ExistingValue: IntProperty` |
| `AddItemMapToSaveData` | `MapToAdd: MapProperty` |
| `AddItemsToMap` | `InMap: MapProperty`, `UpdatedValue: IntProperty` |
| `RemoveItemsFromMap` | `InMap: MapProperty`, `UpdatedValue: IntProperty` |
| `GetShopSaveData` | `ReturnValue: ObjectProperty of BPSO_ShopData_C` |
| `IsShopAvailable` | `ReturnValue: BoolProperty` |
| `HasItemsInShop` | `ReturnValue: BoolProperty`, `TotalAmount: IntProperty` |
| `BuildPurchaseItemList` | *(none)* |
| `BuildSellItemList` | *(none)* |
| `ModifyItemList` | `Completed: BoolProperty` |
| `ModifyItemListPostLoad` | *(none)* |
| `CacheOriginalItemList` / `ResetItemList` / `LoadItemsMap` | *(none)* |
| `CachePlayerCharacter` / `CachePlayerItemManager` | *(none)* |
| `CreateMyShop` | *(none)* |
| `StealAllItems` | *(none)* |
| `DrawItemList` | `CurrentItem: ClassProperty`, `CurrentAmount: IntProperty`, `DebugDuration: FloatProperty` |
| `ShopCustomEvent` | `EventName: NameProperty` |
| `AddCameraState` / `RemoveCameraState` | *(none)* |
| `BindToOvumChanged` / `OnOvumStackChanged` | *(none)* |
| `PostPlayerSaveLoaded` / `OnCloseStarted` / `OnCloseComplete` / `InitMyID` / `ReceiveBeginPlay` / `UserConstructionScript` | *(none)* |

**There is no function named `Purchase` / `Sell` / `Buy`.** Trade flows through the single
`OnTradeEvent` → `UpdateSaveData` → `PostTradeEvent` chain, with `ShopType`/`TradeType` selecting
the direction. `E_ShopItemType` (4 anonymous enumerators) and `Enum_ShopType` (2 anonymous
enumerators) have **no leaked enumerator names** — see §9.

```
/Game/Sparta/UI/Menu/Merchant/BP_ShopHandlerBase.BP_ShopHandlerBase_C	OnTradeEvent	Widget: ObjectProperty of /Game/Sparta/UI/Menu/Merchant/WBP_ButtonShopEntry.WBP_ButtonShopEntry_C , TradeType: ByteProperty , TradeItem: SoftClassProperty , TradeQuantity: IntProperty , TradeValue: MapProperty , K2Node_MakeMap_Map: MapProperty , K2Node_SwitchEnum_CmpSuccess: BoolProperty , CallFunc_GetShopSaveData_ReturnValue: ObjectProperty of /Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C
/Game/Sparta/UI/Menu/Merchant/BP_ShopHandlerBase.BP_ShopHandlerBase_C	TradeComplete__DelegateSignature	TradeType: ByteProperty , TradeItem: SoftClassProperty , TradeQuantity: IntProperty
/Game/Sparta/UI/Menu/Merchant/BP_ShopHandlerBase.BP_ShopHandlerBase_C	UpdateSaveData	SaveObject: ObjectProperty of /Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C , TradeItem: SoftClassProperty , TradeQuantity: IntProperty , ExistingValue: IntProperty
```

### 6.5 `F_ShopItem` — the shop-entry struct

`/Game/Sparta/Items/Shop/F_ShopItem.F_ShopItem` (`UserDefinedStruct`). Field names carry UE's
GUID suffixes (struct is a Blueprint UserDefinedStruct):

| Field (as in dump) | Type | Offset |
|---|---|---|
| `ID_2_D33D91064C11BF0BBDE3FABF33B9C982` | `NameProperty` | `0x00` |
| `Type_27_93A589EF4244803FA43DC8881B3FA2C8` | `ByteProperty` (`TEnumAsByte<E_ShopItemType::Type>` per hpp) | `0x08` |
| `Name_15_C9165B084B67E355A8DA379528E17D6F` | `TextProperty` | `0x10` |
| `Effect_33_88866CE74818C4B3301194914E2CB915` | `TextProperty` | `0x20` |
| `Description_17_5C43C63046C27FDBD1E2549499C8FC84` | `TextProperty` | `0x30` |
| `Icon_24_1F4DA98A4AE6D4D291488190C102771C` | `ObjectProperty of Texture2D` | `0x40` |
| `InteractableActor_28_83FB47584C97CE4E5F13C28AB3353887` | `SoftClassProperty` (`ABP_Interactable_ShopItem_C`) | `0x48` |
| `Requirements_9_5FA63DC746697B3603C10787E03F536C` | `MapProperty (TSoftClassPtr<ItemDef> → int32)` | `0x70` |
| `ItemTag_36_A50D35014CE7364675780EA1D6EC04AC` | `StructProperty of GameplayTag` | `0xC0` |
| `AltRequirements_43_829BD7944ABF139A1B53E49A234EE29B` | `MapProperty (TSoftClassPtr<ItemDef> → int32)` | `0xC8` |

Size `0x118`. → **price = `Requirements` map (item-def → count)**, so a shop price is expressed in
*items* (e.g. 100 × `ID_Gold`), not a bare integer.

```
[000001F8974587A0] UserDefinedStruct /Game/Sparta/Items/Shop/F_ShopItem.F_ShopItem [n: 3BFDCD] [c: 00007FF46EE54138] [or: 000001F8A0B4D940]
[000001F893D866E0] MapProperty /Game/Sparta/Items/Shop/F_ShopItem.F_ShopItem:Requirements_9_5FA63DC746697B3603C10787E03F536C [o: 70] [n: 713189] [c: 00007FF721DE8F70] [owr: 000001F8974587A0] [kp: 000001F8A786D180] [vp: 000001F8A7B07240]
[000001F8A786D180] SoftClassProperty Requirements_9_5FA63DC746697B3603C10787E03F536C./Game/Sparta/Items/Shop/F_ShopItem.F_ShopItem:Requirements_9_5FA63DC746697B3603C10787E03F536C [o: 0] [n: 713189] [c: 00007FF721DF97A0] [owr: 000001F893D866E0] [mc: 00007FF46F067338]
```

### 6.6 `BP_Interactable_ShopItem_C` — one shelf slot

| Property | Type | Offset |
|---|---|---|
| `SpartaSave` | `ObjectProperty of SpartaSaveComponent` | `0x308` |
| `Widget_WorldTooltip` | `WidgetComponent` | `0x310` |
| `ShopItemData` | `StructProperty of F_ShopItem` | `0x318` |
| `IsItemFree` | `BoolProperty` | `0x430` |
| `ItemTypeCheck2` | `ByteProperty` | `0x431` |
| `ItemTypeCheck1` | `ByteProperty` | `0x432` |
| `MerchantMood` | `StructProperty of GameplayTag` | `0x434` |
| `IsItemMemory` | `BoolProperty` | `0x43C` |
| `Element` | `IntProperty` | `0x440` |
| `IsInteractable` | `BoolProperty` | `0x444` |
| `DlgContext` | `StructProperty of GameplayTag` | `0x448` |
| `ItemType` | `StructProperty of GameplayTag` | `0x450` |
| `ActiveDlgInstigatorRef` | `ObjectProperty of BPC_DialogueInstigator_C` | `0x458` |
| `IsItemAltCost` | `BoolProperty` | `0x460` |
| `DestroyAfterBuy` | `BoolProperty` | `0x461` |
| `TriggeredOnce` | `BoolProperty` | `0x462` |

| Function | Real parameters |
|---|---|
| `OnInteract` | `InteractionComponent: ObjectProperty of BPC_InteractionHandler_C`, `InputAction: ObjectProperty of InputAction` |
| `RunItemRequirementCheck` | `ReturnValue: BoolProperty` |
| `UpdateInteractPrompt` | *(none)* |
| `GrantItemEffect` / `OnItemAcquired` / `SpawnShopItemSmallNotify` / `ReceiveBeginPlay` | *(none)* |
| `EnableInteract` | `EventData: ObjectProperty of Object` |
| `MakeItemFree` | `EventData: ObjectProperty of Object` |
| `CustomEvent` | `EventData: ObjectProperty of Object` |
| `BroadcastDialogueEvent` | `Event: StructProperty of GameplayTag` |
| `OnFocusChanged` | `HasFocus: BoolProperty` |
| `OnRelevancyChanged` | `HasRelevancy: BoolProperty` |
| `SetItemTypeTag` / `CreateWorldTooltip` | *(none)* |

```
[000001F8A786CE80] StructProperty /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_ShopItem.BP_Interactable_ShopItem_C:ShopItemData [o: 318] [n: 7137B0] [c: 00007FF721DF9890] [owr: 000001F8A77B8350] [ss: 000001F8974587A0]
[000001F8A786CE00] BoolProperty /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_ShopItem.BP_Interactable_ShopItem_C:IsItemFree [o: 430] [n: 7132AB] [c: 00007FF721DE8D80] [owr: 000001F8A77B8350]
[000001F8A786C900] BoolProperty /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_ShopItem.BP_Interactable_ShopItem_C:DestroyAfterBuy [o: 461] [n: 7F6A62] [c: 00007FF721DE8D80] [owr: 000001F8A77B8350]
```

### 6.7 `BP_Merchant_C` — the merchant NPC

Parent `/Game/Sparta/Core/AI/BP_NPC.BP_NPC_C`. Gameplay-relevant properties:

| Property | Type | Offset |
|---|---|---|
| `BPC_Interaction_Shop` | `ObjectProperty of BPC_InteractionHandler_C` | `0xFD0` |
| `SpartaSave` | `ObjectProperty of SpartaSaveComponent` | `0xFD8` |
| `MerchantDeath` | `MulticastInlineDelegateProperty` | `0x1030` |
| `LoadedShops` | `ArrayProperty of ObjectProperty (BP_ShopHandlerBase_C)` | `0x1040` |
| `ShopDialogueCooldown` | `StructProperty of TimerHandle` | `0x1050` |
| `ShopDialogueInCooldown` | `BoolProperty` | `0x1060` |
| `InteractableAfterDlg` | `BoolProperty` | `0x1061` |
| `MerchantMenu` | `ObjectProperty of WBP_MGT_Merrick_C` | `0x1068` |
| `Shops` | `ArrayProperty of ObjectProperty (BP_ShopHandlerBase_C)` | `0x1070` |
| `bMapGiven` / `FirstTimeOnly` / `bHasCrossbow` / `IsCurrentlyDead` | `BoolProperty` ×4 | `0x1080`–`0x1083` |
| `Tag` | `StructProperty of CSTagContainerQuery` | `0x1088` |

| Function | Real parameters |
|---|---|
| `PlayBuyDialogue` | `DialogueContext: StructProperty of GameplayTag` |
| `PlayCantBuyDialogue` | *(none)* |
| `PlayGreetingDialogue` / `PlayGoodbyeDialogue` / `PlayInteractDialogue` | *(none)* |
| `ResetShopDialogue` / `SaveInitialGreeting` / `UnbindShopEvents` | *(none)* |
| `UpdateCustomTriggerState` | `NewValue: BoolProperty` |
| `ReactToDogHit` / `ReactToDogKilled` | *(none)* |

```
[000001F8C3ED8B00] ArrayProperty /Game/Sparta/Dialogue/NPCs/BP_Merchant.BP_Merchant_C:Shops [o: 1070] [n: 662692] [c: 00007FF721DE8910] [owr: 000001F8D0A998B0] [ai: 000001F8C3ED8A80]
[000001F8C3ED8A80] SoftObjectProperty Shops./Game/Sparta/Dialogue/NPCs/BP_Merchant.BP_Merchant_C:Shops [o: 0] [n: 662692] [c: 00007FF721DF97F0] [owr: 000001F8C3ED8B00] [pc: 000001F61C3534F0]
[000001F8C3ED8F00] MulticastInlineDelegateProperty /Game/Sparta/Dialogue/NPCs/BP_Merchant.BP_Merchant_C:MerchantDeath [o: 1030] [n: 85F401] [c: 00007FF721DE9010] [owr: 000001F8D0A998B0] [df: 000001F8E3327600]
```

### 6.8 `BPFL_Shops` and shop-widget creation

| Function | Real parameters (compiler temporaries filtered) |
|---|---|
| `CreateShopWidget` | `ConstructType: ByteProperty`, `ParentWidget: ObjectProperty of PanelWidget`, `ZOrder: IntProperty`, `SellerID: StructProperty of GameplayTag`, `ShopType: ByteProperty`, `Name: TextProperty`, `SubName: TextProperty`, `ItemsMap: MapProperty`, `OnItemSelected: DelegateProperty`, `OnTrade: DelegateProperty`, `OnTradeFail: DelegateProperty`, `OnCloseStarted: DelegateProperty`, `OnClosed: DelegateProperty`, `__WorldContext: ObjectProperty of Object`, `ShopWidget: ObjectProperty of WBP_MGT_Shop_C` (out) |

`BPFL_Shops` has exactly **one** function.

```
/Game/Sparta/UI/Menu/Merchant/BPFL_Shops.BPFL_Shops_C	CreateShopWidget	ConstructType: ByteProperty , ParentWidget: ObjectProperty of /Script/UMG.PanelWidget , ZOrder: IntProperty , SellerID: StructProperty of /Script/GameplayTags.GameplayTag , ShopType: ByteProperty , Name: TextProperty , SubName: TextProperty , ItemsMap: MapProperty , OnItemSelected: DelegateProperty , OnTrade: DelegateProperty , OnTradeFail: DelegateProperty , OnCloseStarted: DelegateProperty , OnClosed: DelegateProperty , __WorldContext: ObjectProperty of /Script/CoreUObject.Object , ShopWidget: ObjectProperty of /Game/Sparta/UI/Menu/Merchant/WBP_MGT_Shop.WBP_MGT_Shop_C
```

### 6.9 Shop UI data consumers (fields the menu reads — not menu layout)

| Widget | Relevant properties / functions |
|---|---|
| `WBP_MGT_Shop_C` | `GoldCurrency` `ObjectProperty` `0x418` |
| `WBP_MGT_ForgeMode_C` | `GoldCurrency` `ObjectProperty` `0x410` |
| `WBP_MGT_TarstoneMode_C` | `BP_Player_Currency_Tarcores`; `CanForgeCheck(Gold, Tarcores)`, `OnConfirmCompleted(Gold, Tarcores)`, `PayCost(Gold, Tarcores)`, `AddRewards` |
| `WBP_ButtonShopEntry_C` | the per-row button passed into every shop delegate |
| `WBP_BurstNotify_Merchant_Purchase_C` | `Trigger:Currency` `SoftClassProperty` `0x38` |
| `WBP_Player_Currency_C` | `GetCurrentStackAmount()`, `GetCurrencyDataFromID()`, `GetItemManager()`, `UpdateDisplayedAmount()`, `UpdatePreviewAmount()` |

```
[000001F61C33F280] ObjectProperty /Game/Sparta/UI/Menu/Merchant/WBP_MGT_Shop.WBP_MGT_Shop_C:GoldCurrency [o: 418] [n: 63CBCA] [c: 00007FF721DE9420] [owr: 000001F61B1546F0] [pc: 000001F61511D8A0]
```

---

## 7. Upgrades and crafting

### 7.1 `UUpgradeGraph` / `UUpgradeGraphNode` / `UUpgradeGraphEdge`

| Class | Parent | Property | Type | Offset |
|---|---|---|---|---|
| `/Script/Sparta.UpgradeGraph` | `/Script/GenericGraphRuntime.GenericGraph` | `UpgradeDataTables` | `ArrayProperty of ObjectProperty (DataTable)` | `0x90` |
| `UpgradeGraph` | | `MetaUpgradeDataTables` | `ArrayProperty of ObjectProperty (DataTable)` | `0xA0` |
| `UpgradeGraph` | | `UnusedUpgradeDataTables` | `ArrayProperty of ObjectProperty (DataTable)` | `0xB0` |
| `UpgradeGraph` | | `NodeDisplaySettings` | `StructProperty of /Script/Sparta.NodeDisplaySettings` | `0xC0` |
| `/Script/Sparta.UpgradeGraphNode` | `/Script/GenericGraphRuntime.GenericGraphNode` | `PlayerUpgrade` | `StructProperty of /Script/Sparta.PlayerUpgrade` | `0xA0` |
| `/Script/Sparta.UpgradeGraphEdge` | `/Script/GenericGraphRuntime.GenericGraphEdge` | `Selection` | `TextProperty` | `0x40` |

No functions on any of the three. Sizes: Graph `0xC8`, Node `0x200`, Edge `0x50`.

`FPlayerUpgrade : FTableRowBase` (the row type behind `UpgradeDataTables`, `Sparta.hpp:230`):

| Field | Type | Offset |
|---|---|---|
| `ID` | `FName` | `0x08` |
| `Name` | `FText` | `0x10` |
| `Description` | `FText` | `0x20` |
| `GlowColor` | `FLinearColor` | `0x30` |
| `Upgrade` | `TSoftClassPtr<USpartaItemDefinition>` | `0x40` |
| `UpgradeLevels` | `TArray<FUpgradeLevel>` | `0x68` |
| `Tooltips` | `TArray<FName>` | `0x78` |
| `UpgradeTags` | `FGameplayTagContainer` | `0x88` |
| `Requirements` | `FGameplayTagQuery` | `0xA8` |
| `UpgradeRequirements` | `FGameplayTagQuery` | `0xF0` |
| `UpgradeImage` | `UTexture2D*` | `0x138` |
| `DefaultVideo` | `UMediaSource*` | `0x140` |
| `OptionalVideos` | `TArray<UMediaSource*>` | `0x148` |

```
[00007FF46F063138] Class /Script/Sparta.UpgradeGraph [n: 4B1F7] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F016938]
[000001F4ACE1C280] ArrayProperty /Script/Sparta.UpgradeGraph:UpgradeDataTables [o: 90] [n: 58A042] [c: 00007FF721DE8910] [owr: 00007FF46F063138] [ai: 000001F4ACE1C200]
[000001F4ACE1C180] StructProperty /Script/Sparta.UpgradeGraphNode:PlayerUpgrade [o: A0] [n: 137B0] [c: 00007FF721DF9890] [owr: 00007FF46F063538] [ss: 00007FF46F487178]
```

### 7.2 The upgrade-level authorities

There are **two parallel upgrade systems**:

| System | Key type | Where |
|---|---|---|
| legacy DataTable upgrades | `FName` row id | `USpartaUpgradesComponent:UpgradeTable` + `AddUpgradeLevel(FName)` |
| Blueprint upgrade graph | `TSoftClassPtr<USpartaItemDefinition>` | `UPlayerExpComponent:UpgradeLevels` + `BPFL_Upgrades` + `BP_UpgradePoolHandler` |

`UPlayerExpComponent` (`/Script/Sparta.PlayerExpComponent`, `Sparta.hpp:3318`) —
**this is the player-level / upgrade-point authority**:

| Property | Type | Offset |
|---|---|---|
| `OnCurrentEXPChanged` | delegate | `0xB8` |
| `OnUpgradeSelected` | delegate (`FSpartaItem PlayerUpgrade`) | `0xC8` |
| `PlayerUpgrades` | `TArray<FUpgradeTable>` | `0xD8` |
| `PlayerMetaUpgrades` | `TArray<FUpgradeTable>` | `0xE8` |
| `OwnedUpgradeStats` | `FGameplayTagContainer` | `0xF8` |
| `PlayerLevel` | `int32` | `0x118` |
| `UpgradeLevels` | `TMap<TSoftClassPtr<USpartaItemDefinition>, int32>` | `0x120` |
| `PlayerLevelUpdated` | delegate | `0x1A0` |

Functions: `AddUpgradeLevel(Upgrade: SoftClassPtr)`, `SetUpgradeLevel(Upgrade: SoftClassPtr, Level: int32)`,
`GetUpgradeLevel(Upgrade)`, `GetAvailableUpgradePoints()`, `HasUpgrade/HasAnyUpgrade/HasAllUpgrades`,
`IsUpgradeAtMaxLevel`, `RemoveUpgradeData(Upgrade)`, `UpgradeStatChanged(Tag: GameplayTag, NewLevel: float)`.
(The exact param types come from `Sparta.hpp:3332–3347`; the dump rows for `PlayerExpComponent` were
not enumerated in this pass — see §9.)

`BPFL_Upgrades` (`/Game/Sparta/Core/Player/Upgrades/Core/BPFL_Upgrades.BPFL_Upgrades_C`):

| Function | Real parameters |
|---|---|
| `GetUpgradeCurrentLevel` | `Upgrade: SoftClassProperty`, `__WorldContext`, `ReturnValue: IntProperty` |
| `GetUpgradeLevelFromCharacter` | `Character: ObjectProperty of Character`, `Upgrade: SoftClassProperty`, `__WorldContext`, `ReturnValue: IntProperty` |
| `GetHasUpgradeFromCharacter` | `Character`, `Upgrade: SoftClassProperty`, `__WorldContext`, `ReturnValue: BoolProperty` |
| `GetIsUpgradeAtMaxLevelFromCharacter` | `Character`, `Upgrade: SoftClassProperty`, `__WorldContext`, `ReturnValue: BoolProperty` |
| `GetUpgradeStatValueFromCharacter` | `Character: ObjectProperty of SpartaCharacter`, `StatTag: GameplayTag`, `__WorldContext`, `ReturnValue` |
| `GetExperienceComponent` | `__WorldContext`, `Success: BoolProperty`, `ReturnValue: ObjectProperty of BPC_Player_Experience_C` |
| `GetWeaponCurrentElemental` | `Character`, `WeaponSlot: GameplayTag`, `__WorldContext`, `ReturnValue` |
| `GetWeaponElementalEfficiency` | `Character`, `WeaponSlot: GameplayTag`, `ElementalTag: GameplayTag`, … |
| `HasDualWieldProficency` / `HasHeavyFinisher` / `HasLightFinisher` / `HasSidearmBuff` / `HasSidearmPermanentBuff` / `HasWeaponBuff` / `HasWeaponPermanentBuff` | `Character` (+ `__WorldContext`), `ReturnValue: BoolProperty` |

### 7.3 `BP_UpgradePoolHandler_C` — the level-up choice pool

Parent `/Script/Sparta.SpartaInstancedObject`. This is what decides the 3 upgrade choices.

| Property | Type | Offset |
|---|---|---|
| `Category` | `EnumProperty of /Script/Sparta.EUpgradeType` | `0x30` |
| `ExperienceComponent` | `ObjectProperty of BPC_Player_Experience_C` | `0x38` |
| `PlayerCharacter` | `ObjectProperty of SpartaPlayerCharacter` | `0x40` |
| `CurrentPool` | `ArrayProperty of StructProperty (FSpartaItem)` | `0x48` |
| `Requirement` | `StructProperty of GameplayTagQuery` | `0x58` |
| `PoolSettings` | `ArrayProperty of StructProperty (F_UpgradePoolSettings)` | `0xA0` |
| `NonCoolPool` | `ArrayProperty of StructProperty (FSpartaItem)` | `0xB0` |
| `AvailablePool` | `ArrayProperty of StructProperty (FSpartaItem)` | `0xC0` |
| `SelectedUpgrades` | `ArrayProperty of StructProperty (FSpartaItem)` | `0xD0` |
| `Index` | `IntProperty` | `0xE0` |
| `L_Upgrade` | `StructProperty of FSpartaItem` | `0xF0` |
| `UpgradesFiltered` | `MulticastInlineDelegateProperty` | `0x148` |

| Function | Real parameters |
|---|---|
| `Initialize` | `PlayerCharacter: ObjectProperty of SpartaPlayerCharacter`, `ExperienceComponent: ObjectProperty of BPC_Player_Experience_C` |
| `InitializePool` | `L_Setting: StructProperty of F_UpgradePoolSettings` |
| `SelectUpgrade` | `Output_Get: StructProperty of FSpartaItem` (out), `L_SelectedUpgrade: StructProperty of FSpartaItem` |
| `IsUpgradeAvailable` | `Upgrade: StructProperty of FSpartaItem`, `ReturnValue: BoolProperty` |
| `IsUpgradeAlreadySelected` | `Upgrade: StructProperty of FSpartaItem`, `ReturnValue: BoolProperty` |
| `IsPoolActive` | `ReturnValue: BoolProperty` |
| `RemoveFromPool` | `Upgrade: StructProperty of FSpartaItem`, `Removed: BoolProperty` (out) |
| `FilterUpgrades` / `IncrementIndex` / `Reset` | *(none)* |

```
[000001F4C96C7600] EnumProperty /Game/Sparta/Core/Player/Upgrades/Core/BP_UpgradePoolHandler.BP_UpgradePoolHandler_C:Category [o: 30] [n: 16F0F] [c: 00007FF721DE7380] [owr: 000001F606B66A60] [em: 00007FF46F3D8438]
[000001F4C96C9080] ArrayProperty /Game/Sparta/Core/Player/Upgrades/Core/BP_UpgradePoolHandler.BP_UpgradePoolHandler_C:SelectedUpgrades [o: D0] [n: 642FDB] [c: 00007FF721DE8910] [owr: 000001F606B66A60] [ai: 000001F4C96C9000]
```

### 7.4 `USpartaWeaponProgressionData` — weapon level curves

Parent `/Script/Engine.DataAsset`. **Eight `FToggleableRuntimeCurve` fields** at `0x30`, one per
`EWeaponStatType` enumerator, then tuning ints.

| Property | Type | Offset |
|---|---|---|
| `DamageMultiplier` | `StructProperty of ToggleableRuntimeCurve` | `0x30` |
| `PoiseDamageMultiplier` | " | `0x110` |
| `ElementalEfficiency` | " | `0x1F0` |
| `CritChance` | " | `0x2D0` |
| `CritDamage` | " | `0x3B0` |
| `EnergyGainMultiplier` | " | `0x490` |
| `EnergyConsumption` | " | `0x570` |
| `GuardMeterMultiplier` | " | `0x650` |
| `PrepopulateMaxLevel` | `IntProperty` | `0x730` |
| `PrepopulateValue` | `FloatProperty` | `0x734` |
| `bPrepopulateOnCreate` | `BoolProperty` | `0x738` |
| `DefaultMinLevel` | `IntProperty` | `0x73C` |
| `DefaultMaxLevel` | `IntProperty` | `0x740` |
| `DefaultStartValue` | `FloatProperty` | `0x744` |
| `DefaultEndValue` | `FloatProperty` | `0x748` |
| `bDefaultConstantInterp` | `BoolProperty` | `0x74C` |
| `MirrorMaxLevel` | `IntProperty` | `0x750` |
| `bAutoSyncCachedValuesAndCurves` | `BoolProperty` | `0x754` |
| `bSyncConstantInterp` | `BoolProperty` | `0x755` |

| Function | Parameters |
|---|---|
| `GetValuesAtLevel` | `Level: IntProperty`, `OutValues: MapProperty (EWeaponStatType → float)`, `bIncludeDisabled: BoolProperty` |
| `GetStatValueAtLevel` | `Stat: EnumProperty of EWeaponStatType`, `Level: IntProperty`, `ReturnValue: FloatProperty` |
| `GetNormalizedProgression` | `Stat: EnumProperty of EWeaponStatType`, `Level: IntProperty`, `OverrideMaxLevel: IntProperty`, `ReturnValue: FloatProperty` |
| `GetAllStatCurveViews` | `OutViews: ArrayProperty of FStatCurveView`, `bIncludeDisabled: BoolProperty` |

Size `0x758`. `EWeaponStatType` = DamageMultiplier 0 … GuardMeterMultiplier 7.

```
[00007FF46F05EF38] Class /Script/Sparta.SpartaWeaponProgressionData [n: 4AD73] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[000001F4ACE12300] StructProperty /Script/Sparta.SpartaWeaponProgressionData:DamageMultiplier [o: 30] [n: 589A15] [c: 00007FF721DF9890] [owr: 00007FF46F05EF38] [ss: 00007FF46F48CCF8]
[000001F4ACE12880] BoolProperty /Script/Sparta.SpartaWeaponProgressionData:bSyncConstantInterp [o: 755] [n: 589970] [c: 00007FF721DE8D80] [owr: 00007FF46F05EF38]
```

### 7.5 `ItemFragment_Tarstone` — per-tarstone progression cost curve

See §1.4 for the fields. Reading them as a progression definition:

* `ExpNeededPerLevel: TArray<int32>` — XP required at each level
* `GoldNeededPerLevel: TArray<int32>` — **gold cost at each level**
* `TarcoresNeededPerLevel: TArray<int32>` — tarcore cost at each level
* `TarcoresGrantedPerLevel: TArray<int32>` — tarcore refund/grant at each level
* `CompatibilityTags: FGameplayTagContainer` — which equipment slots this tarstone fits
* `TarstoneSlot: ETarstoneSlot` = `Primary 0 / Sidearm 1 / Support 2`
* `TarstoneColor: ETarstoneColor` = `Blue 0 / Red 1 / Purple 2`
* `TarstoneExpClass: TSubclassOf<UItemFragmentData_TarstoneExperience>`

```
[000001F4ACD5F080] ArrayProperty /Script/Sparta.ItemFragment_Tarstone:ExpNeededPerLevel [o: 58] [n: 586258] [c: 00007FF721DE8910] [owr: 00007FF46F036F38] [ai: 000001F4ACD60860]
[000001F4ACD5F100] ArrayProperty /Script/Sparta.ItemFragment_Tarstone:GoldNeededPerLevel [o: 68] [n: 58624E] [c: 00007FF721DE8910] [owr: 00007FF46F036F38] [ai: 000001F4ACD608D0]
[000001F4ACD5F180] ArrayProperty /Script/Sparta.ItemFragment_Tarstone:TarcoresNeededPerLevel [o: 78] [n: 586242] [c: 00007FF721DE8910] [owr: 00007FF46F036F38] [ai: 000001F4ACD60940]
[000001F4ACD5F200] ArrayProperty /Script/Sparta.ItemFragment_Tarstone:TarcoresGrantedPerLevel [o: 88] [n: 586235] [c: 00007FF721DE8910] [owr: 00007FF46F036F38] [ai: 000001F4ACD609B0]
```

Structs used by tarstones (`Sparta.hpp:1533/1546`):

| Struct | Field | Type | Offset |
|---|---|---|---|
| `FTarstoneArray` | `Tarstones` | `TArray<TSubclassOf<USpartaItemDefinition>>` | `0x00` |
| `FTarstoneID` | `ItemDefinition` | `TSoftClassPtr<USpartaItemDefinition>` | `0x00` |
| `FTarstoneID` | `Index` | `int32` | `0x28` |
| `FTarstoneLevelData` | `Level` | `int32` | `0x00` |
| `FTarstoneLevelData` | `exp` | `int32` | `0x04` |
| `FTarstoneLevelData` | `Durability` | `int32` | `0x08` |

### 7.6 Tarforge (the crafting station)

| Class | Parent | Role |
|---|---|---|
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Tarforge.BP_Tarforge_C` | `BP_InteractableBase_C` | the forge interactable (forge / smelt / tarstone modes) |
| `/Game/Sparta/Core/SaveObjects/BPSO_Tarforge.BPSO_Tarforge_C` | `/Script/Sparta.SpartaSaveObject` | **persistent forge state: `StoredTarcores: IntProperty` at `0xB8`** |
| `/Game/Sparta/Core/Player/Upgrades/Tarforge/BPFL_Tarforge.BPFL_Tarforge_C` | BlueprintFunctionLibrary | all forge math/queries |

`BP_Tarforge_C` gameplay-relevant properties:

| Property | Type | Offset |
|---|---|---|
| `SpartaSave` | `ObjectProperty of SpartaSaveComponent` | `0x330` |
| `BPC_InteractionHandler_ForgeMouth` | `ObjectProperty of BPC_InteractionHandler_C` | `0x338` |
| `BPC_PointOfInterest` | `ObjectProperty of BPC_PointOfInterest_C` | `0x340` |
| `TagToCheck` | `StructProperty of GameplayTag` | `0x458` |
| `CheckForDeath` | `StructProperty of GameplayTag` | `0x460` |
| `WBP_Widget` | `ObjectProperty of WBP_Menu_Game_Tab_C` | `0x468` |
| `InsertedWeapons` | `ArrayProperty of ObjectProperty (Actor)` | `0x470` |
| `InsertedWeaponsTags` | `StructProperty of GameplayTagContainer` | `0x480` |
| `bStartActive` | `BoolProperty` | `0x4F0` |
| `TarforgeUnlockIDs` | `ArrayProperty` | `0x4F8` |
| `Returned` | `ClassProperty` | `0x508` |
| `HasTarforgeFood` | `BoolProperty` | `0x510` |
| `ShellMemory_WeatherID` | `Int64Property` | `0x528` |

`BP_Tarforge_C` functions (real parameters):

| Function | Real parameters |
|---|---|
| `GetStoredTarcores` | `StoredTarcores: IntProperty` (out) |
| `UpdateStoredTarcores` | `CoresIn: IntProperty` |
| `SwitchMouthPromptText` / `UpdateMouthInteracability` / `UpdateTarcorePickupVFX` | *(none)* |

`BPFL_Tarforge` functions (real parameters only):

| Function | Real parameters |
|---|---|
| `GetForgeModeRequirements` | `Level: IntProperty`, `__WorldContext`, `Success: BoolProperty`, `ReturnValue: MapProperty` (item-def → amount) |
| `GetSmeltModeRewardAndCost` | `WeaponTag: GameplayTag`, `Debug: BoolProperty`, `__WorldContext`, `Success: BoolProperty`, `ReturnReward: MapProperty`, `ReturnCost: IntProperty` |
| `GetUpgradableWeapons` | `CheckLevel: BoolProperty`, `__WorldContext`, `Success`, `ReturnValue: ArrayProperty of GameplayTag` |
| `GetUpgradableSidearms` | same shape |
| `GetUpgradedWeapons` / `GetUpgradedSidearms` / `GetUpgradedEquipment` | `__WorldContext`, `Success`, `ReturnValue: ArrayProperty of GameplayTag` |
| `ChangeWeaponLevel` | `WeaponTag: GameplayTag`, `Delta: IntProperty`, `__WorldContext`, `Success: BoolProperty`, `ReturnValue: IntProperty` |
| `ClearWeaponLevel` | `WeaponTag: GameplayTag`, `__WorldContext` |
| `GetWeaponLevel` | `WeaponTag: GameplayTag`, `__WorldContext`, `ReturnValue: IntProperty` |
| `GetWeaponMaxLevel` | `__WorldContext`, `ReturnValue: IntProperty` |
| `GetWeaponProgressionData` | `WeaponTag: GameplayTag`, `__WorldContext`, `Success: BoolProperty`, `Data: ObjectProperty of SpartaWeaponProgressionData` |
| `GetWeaponProgressionStatValue` | `SpartaCharacter`, `WeaponSlot: GameplayTag`, `Stat: EnumProperty of EWeaponStatType`, `FallbackValue: DoubleProperty`, `__WorldContext`, `ReturnValue: DoubleProperty` |
| `BroadcastEquipmentLevelChange` | `EquipmentTag: GameplayTag`, `NewLevel: IntProperty`, `__WorldContext`, `Type: GameplayTag` |
| `GetBlacksmithDeadMaterialCostPenalty` | `CurrentForgeCurveValue: DoubleProperty`, `__WorldContext`, `Penalty: IntProperty` |
| `IsSidearmForgeModeUnlocked` / `IsWeaponForgeModeUnlocked` etc. | `__WorldContext`, … |

**Weapon level is not a component field — it is a per-weapon stat stored in the player save
object** (`BP_SpartaPlayerSaveObject_C:GetWeaponStat(WeaponTag, StatTag)`). See §9 UNKNOWN for the
`WeaponStats` map offset (owned by the save-object agent).

```
[000001F8ACDBFBA0] IntProperty /Game/Sparta/Core/SaveObjects/BPSO_Tarforge.BPSO_Tarforge_C:StoredTarcores [o: B8] [n: 7F8175] [c: 00007FF721DE91F0] [owr: 000001F8920B3FA0]
[000001F8B0DDD000] ArrayProperty /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Tarforge.BP_Tarforge_C:InsertedWeapons [o: 470] [n: 7FB8A0] [c: 00007FF721DE8910] [owr: 000001F8B0CA2DD0] [ai: 000001F8B0DDCF80]
[000001F8B0DDD180] StructProperty /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Tarforge.BP_Tarforge_C:TagToCheck [o: 458] [n: 572960] [c: 00007FF721DF9890] [owr: 000001F8B0CA2DD0] [ss: 00007FF46F3890A8]
```

### 7.7 Tarstone helper library `BPFL_Tarstones`

`/Game/Sparta/Core/Player/Upgrades/Core/BPFL_Tarstones.BPFL_Tarstones_C` — 30 functions, all
taking `SoftTarstone: SoftClassProperty` + `__WorldContext` and returning out-params:

| Function | Extra in/out params |
|---|---|
| `GetLevelUpRequirements` | `Gold: IntProperty` (out), `Tarcores: IntProperty` (out), `CurrentLevel: IntProperty` |
| `GetLevelUpRewards` | `Tarcores: IntProperty` (out), `CurrentLevel: IntProperty` |
| `IncrementTarstoneLevel` / `ForceIncrementTarstoneLevel` | `Success: BoolProperty` (out) |
| `GetTarstoneCurrentLevel` | `Level: IntProperty` (out) |
| `GetTarstoneCurrentExp` | `ReturnValue: IntProperty` |
| `GetTarstoneRequiredExp` | `ReturnValue: IntProperty`, `CurrentLevel: IntProperty` |
| `GetTarstoneExpProgress` | `ReturnValue: DoubleProperty`, `RequiredExp`, `CurrentEXP` |
| `GetTarstoneExpValues` | `Array: ArrayProperty` (out) |
| `FindTarstoneMaxLevel` | `ReturnValue: IntProperty`, `DefaultMaxLevel` |
| `IsTarstoneAtMaxLevel` | `ReturnValue: BoolProperty`, `CurrentLevel`, `MaxLevel` |
| `IsTarstoneCollected` / `IsTarstoneEquipped` / `IsSupportTarstoneEquipped` / `IsTarstoneReady` / `IsTarstoneCompatible` / `IsTarstoneItem` | `ReturnValue: BoolProperty` (+ `EquipmentTag(s)`) |
| `GetTarstoneInSlot` / `GetSupportTarstoneInSlot` | `Success: BoolProperty`, `ReturnValue: ClassProperty` |
| `GrabCollectedTarstones` / `GrabUpgradableTarstones` | `Success: BoolProperty`, `ReturnValue: ArrayProperty` |
| `GetCollectedTarstonesNumber` / `GetTotalTarstonesNumber` | `ReturnValue: IntProperty` |
| `GetTarstoneComponent` | `Success: BoolProperty`, `ReturnValue: ObjectProperty of BPC_TarstoneComponent_C` |
| `BuildTarstoneName` | `IncludeLevel: BoolProperty`, `ReturnValue: TextProperty` |
| `BuildTarstoneDescription` / `BuildTarstoneEffects` / `BuildNewTarstoneEffects` | `ReturnValue: TextProperty` / `ArrayProperty` |
| `GetTarstoneColor` | `ReturnValue: EnumProperty of ETarstoneColor` |
| `GetTarstoneCompatibility` | `Compatibility: GameplayTagContainer` (out) |
| `GetTarstoneSlot` | `Tarstone: ByteProperty` (out) |
| `GetUnlockedMeleeElementalTag` / `GetUnlockedSidearmElementalTag` | `Success: BoolProperty`, `ReturnValue: GameplayTag` |

```
/Game/Sparta/Core/Player/Upgrades/Core/BPFL_Tarstones.BPFL_Tarstones_C	GetLevelUpRequirements	SoftTarstone: SoftClassProperty , __WorldContext: ObjectProperty of /Script/CoreUObject.Object , Gold: IntProperty , Tarcores: IntProperty , CurrentLevel: IntProperty
/Game/Sparta/Core/Player/Upgrades/Core/BPFL_Tarstones.BPFL_Tarstones_C	GetLevelUpRewards	SoftTarstone: SoftClassProperty , __WorldContext: ObjectProperty of /Script/CoreUObject.Object , Tarcores: IntProperty , CurrentLevel: IntProperty
/Game/Sparta/Core/Player/Upgrades/Core/BPFL_Tarstones.BPFL_Tarstones_C	IsTarstoneEquipped	EquipmentTags: StructProperty of /Script/GameplayTags.GameplayTagContainer , SoftTarstone: SoftClassProperty , __WorldContext: ObjectProperty of /Script/CoreUObject.Object
```

### 7.8 "Smelt" / "Forge" / "Craft" keyword sweep

Full sweep of `Forge|Craft|Smelt` over `cls` produced (selection relevant to items):

| Path | Kind |
|---|---|
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Tarforge.BP_Tarforge_C` | the only `*Forge*` gameplay actor |
| `/Game/Sparta/Core/SaveObjects/BPSO_Tarforge.BPSO_Tarforge_C` | forge save object |
| `/Game/Sparta/Core/Player/Upgrades/Tarforge/BPFL_Tarforge.BPFL_Tarforge_C` | forge library |
| `/Game/Sparta/Core/SaveObjects/BPSO_CraftingItemPickup.BPSO_CraftingItemPickup_C` | per-node crafting-pickup save |
| `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup*.BP_CraftingItemPickup*_C` | 6 crafting-node pickups |
| `/Game/Sparta/Items/Pickups/BP_TarStonePickup_Man.BP_TarStonePickup_Man_C` | tarstone crafting node |
| `/Game/Sparta/Items/FrogMomma/BP_Interactable_PotionIngredient.BP_Interactable_PotionIngredient_C` | potion-ingredient interactable |
| `/Game/Sparta/UI/Menu/Tarforge/WBP_*` | forge menu widgets (menu territory) |

**There is no `Smelt` class or function outside `BPFL_Tarforge:GetSmeltModeRewardAndCost` and
`ID_Tarforge_SmeltMode.ID_Tarforge_SmeltMode_C`** (an item def). The forge has three modes, each
an item definition: `ID_Tarforge_ForgeMode_Weapon_C`, `ID_Tarforge_ForgeMode_Sidearm_C`,
`ID_Tarforge_SmeltMode_C`, `ID_Tarforge_TarstoneMode_C` — all under
`/Game/Sparta/Items/Pickups/Tarforge/`.

```
[000001F8B0CA2DD0] BlueprintGeneratedClass /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Tarforge.BP_Tarforge_C [n: 132829] [c: 00007FF46EEC3D38] [or: 000001F8B6CC1B90] [sps: 000001F620594DE0]
[000001F8A15C8A70] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tarforge/ID_Tarforge_SmeltMode.ID_Tarforge_SmeltMode_C [n: 8EFCC] [c: 00007FF46EEC3D38] [or: 000001F8ACEE3FD0] [sps: 000001F4B021F550]
```

---

## 8. Sync-oriented summary (facts only, no design)

| Data | Where it lives | Change notification | Add/remove API |
|---|---|---|---|
| any item stack (incl. Gold) | `SpartaItemManagerComponent:ItemList` (`FSpartaItemList`, `FFastArraySerializer`) | `OnItemStacksUpdated(ItemDef, Count)`, `OnLootPickedUp(Instance)`, `SpartaItemChangeEvent{ItemOwner, Instance, NewCount, Delta}` | `AddItemByClass(ItemDef, StackCount)` / `RemoveItemStacksByClass(ItemDef, NumToRemove)` / `AddItemInstance` / `RemoveItemInstance` |
| PP-item slots & levels | `PPItemsComponent` (`PPItemLevels`, `PPItemFoundAmounts`, `ActivePPItem`, `PassivePPItem`) — persisted in `PPItemsSaveData` | `OnPPItemEquipped/Unequipped/LevelChanged/StackCountChanged/TrackingEvent` | `EquipPPItem` / `UnequipPPItem` / `AddToPPItemLevel` / `AddToPPItemFoundAmount` |
| Tarstones | `TarstoneComponent` (builds + levels + equipped instances) — persisted in `TarstoneSaveData` | `OnTarstoneLevelUp/ExpUp/Equip/Unequip`, `OnSupportTarstoneEquip/Unequip`, `OnTarstoneReady`, `OnWeaponTarstones*` | `EquipTarstone` / `UnequipTarstone` / `AddTarstoneExp` / `AddTarstoneToInventory` |
| equipped weapons (actors) | `WeaponsComponent:EquippedWeapons` | `OnWeaponEquipped` / `OnWeaponUnequipped` / `OnDefaultWeaponsSpawned` | `EquipWeaponToSlot` / `SpawnAndEquipWeaponOfClass` / `RemoveWeaponFromSlot` |
| weapon level | **not** a component field; per-weapon stat in the player save object via `GetWeaponStat(WeaponTag, StatTag)` | `GlobalEvent_EquipmentLevelChange` (via `BPFL_Tarforge:BroadcastEquipmentLevelChange`) | `BPFL_Tarforge:ChangeWeaponLevel(WeaponTag, Delta)` |
| shop stock | `BPSO_ShopData:CachedItemsMap` (per-shop save object) | `BP_ShopHandlerBase:TradeComplete` / `TradeFailed` / `ItemSelected` | `BP_ShopHandlerBase:UpdateSaveData(SaveObject, TradeItem, TradeQuantity, ExistingValue)` |
| merchant dead / theft | `BP_Merchant:IsCurrentlyDead`, `BPSO_ShopData:StolenItems` | `BP_Merchant:MerchantDeath` | — |
| forge tarcore pool | `BPSO_Tarforge:StoredTarcores` | (none found) | `BP_Tarforge:UpdateStoredTarcores(CoresIn)` |
| armor tag | `ArmorComponent:CurrentArmorTag` | (none) | `SetCurrentArmorTag` / `SetDefaultArmor` / `AddArmorOverride` |
| seal throw state | `SlayerSealComponent:SealState` | `OnSlayerSealStateChanged` | `ActivateSeal(Target)` / `RecallSeal()` |

---

## 9. UNKNOWNs and open questions

| # | UNKNOWN / open question | What I tried |
|---|---|---|
| 1 | **No replication/authority metadata.** The object dump carries no `FUNC_Net*` / `Replicated` flags, so I cannot say *whether* `ItemList`, the `On*` delegates, or the `S_*` functions are replicated, nor which of the two peers owns the stack count. | Read `parse_dump.py` (`iter_objects` only parses `or/owr/sps/ss/pc/ai/em/o`); `QUERYING.md` documents no flags field. Needs a live PIE / netdriver inspection. |
| 2 | **`Enum_ShopType` and `E_ShopItemType` enumerator names.** Both are `UserDefinedEnum`s whose entries are still `NewEnumerator0..N` in `CXXHeaderDump` — the display names are stored as localized `FText` that UE4SS did not dump. `Enum_ShopType` has 2 entries, `E_ShopItemType` has 4. Same for `E_LootType` (11) and `E_UpgradePoolPolicy` (3). | Read `raw\CXXHeaderDump\{Enum_ShopType,E_ShopItemType,E_LootType,E_UpgradePoolPolicy}_enums.hpp`; queried `obj` for the enum objects (only `UserDefinedEnum` rows, no enumerator children). |
| 3 | **`UPlayerInventoryComponent` semantics** (class `/Script/Sparta.PlayerInventoryComponent`; earlier drafts of this line called it `USpartaPlayerInventoryComponent` — that FName does not exist, corrected in `05-dumper7-vs-ue4ss.md` §7.1). The class is completely empty (size `0xB8`, zero members) — I cannot say what it is attached to or why it exists. | `[00007FF46F037F38] Class /Script/Sparta.PlayerInventoryComponent [n: 47C28] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]` → `SELECT * FROM obj WHERE owner=(SELECT addr … 'PlayerInventoryComponent')` → 0 rows; `Sparta.hpp:3350-3352` empty body; `raw\Dumper7\SDK\Sparta_classes.hpp` → `class UPlayerInventoryComponent final : public UActorComponent` |
| 4 | **`UPlayerExpComponent` property/function table** was read from `Sparta.hpp:3318-3348`, not enumerated from the dump in this pass (context budget). Offsets in §7.2 come from the header and are trustworthy, but there is no `parse_dump raw` block for them. | `Sparta.hpp` read; the dump rows exist under `/Script/Sparta.PlayerExpComponent:*` and can be pulled with `parse_dump.py raw raw\index.sqlite3 "Sparta\.PlayerExpComponent"`. |
| 5 | **`FGameplayTagStackContainer` layout (`0x168` at `SpartaItemInstance:StatTags`)** was not expanded (it belongs to the GAS/attribute area). | Not queried; `Sparta.hpp:130`. |
| 6 | **Which save object owns `BP_SpartaPlayerSaveObject_C:WeaponStats` / `ShellStats` offsets.** `UPlayerSaveObject` in `Sparta.hpp:3393-3415` has `WeaponStats` at `0x01C0` and `PlayerStats` at `0x0210`, but `BP_SpartaPlayerSaveObject_C` is a Blueprint subclass whose own header was not opened here; belongs to the world-state/save agent's slice. | `Sparta.hpp` read; the Blueprint header `BP_SpartaPlayerSaveObject.hpp` was not read. |
| 7 | **`ItemFragmentData_AbilitySets` / `ItemFragmentData_ClearAbilities` field layout.** Both have *no* UPROPERTY rows in the dump; `Sparta.hpp` gives only total sizes (`0x60` / `0x38`). Their members are presumably plain C++ (`UPROPERTY()`-less). | `ii-13` query returned zero rows for both classes. |
| 8 | **Whether the 204 `ItemFragment_Display` instances map 1:1 to item definitions** (i.e. whether every item has a Display fragment). I only have the count, not the join. | `ii-02` histogram gives the count; a per-item join query was not run. |
| 9 | **`F_ShopItem` "Requirements vs AltRequirements" semantics** — which one is the gold price and which is the alternative currency is not derivable from names alone. | Struct/hpp read only. `GUESS:` `Requirements` is the primary price and `AltRequirements` an alternate payment (e.g. Ovums), because `BP_Interactable_ShopItem_C` has an `IsItemAltCost` bool at `0x460`. |
| 10 | **`BP_ShopItemRandomizer_C`, `BP_ShopHandler_Sell_C` and the 9 other `BP_ShopHandler_*` subclasses** — I documented the base class fully but did not enumerate each subclass' own members. | Base class enumerated (`ii-15`/`ii-16`/`ii-17`); subclasses only listed by path/parent (`ii-14`). |
| 11 | **`ESpartaItemRarity`/`EItemRarity` → `ItemFragment_PickupIcon:ItemRarity` enum values** — read from `Sparta_enums.hpp` as `Common/Uncommon/Rare/Epic/Legendary`, but I did not cross-check that the property's `em:` ref resolves to that same enum object. | `Sparta_enums.hpp` grep. |
| 12 | **Where a *chest* writes its loot.** `BP_InteractableBase_LootCrate_C` spawns `BP_ISI_TarWine_C` in its ubergraph; other chest variants (`BP_Interactable_Chest_Seal_C`, `BP_Interactable_LockedChest_PlayerEquipments_C`) exist but were not expanded — chest/breakable behaviour overlaps the world-state agent's area. | `parse_dump.py show` on the loot crate; class list only for the others. |
| 13 | **`EUpgradeType` enumerator values** (used by `BP_UpgradePoolHandler:Category` and `FUpgradeTable:UpgradeType`) were not read. | Not read; it is in `Sparta_enums.hpp` (`grep -n "enum class EUpgradeType"`). |

---

## 10. Queries used (all under `research\scripts\q\`)

| File | Purpose |
|---|---|
| `ii-01-native-item-classes.sql` | every `/Script/*` class whose path matches Item/Inventory/Tarstone/Artifact/Armor/Sidearm/Seal/Currency/Upgrade/Shop/Merchant/Vendor/Forge/Craft/Loot/Drop/Pickup/Weapon, with parent |
| `ii-02-item-asset-families.sql` | asset-type histogram + full path list under `/Game/Sparta/Items/` |
| `ii-03-core-item-members.sql` | members of `SpartaItemDefinition` / `SpartaItemInstance` / `SpartaItemFragment` |
| `ii-04-currency-props.sql` | every `*Property` whose member name contains Gold/Currency/Money/Tarcoin/Soul/Coin/Tarcore/Scrap/Futilium |
| `ii-05-currency-classes.sql` | classes + functions with currency-ish names |
| `ii-06-currency-fns.sql` | currency functions/structs/Tarcore paths |
| `ii-07-gold-itemdefs.sql` | currency item defs; `S_Add*` / `IncrementCumulativeCurrency` signatures |
| `ii-08-itemdef-classes.sql` | direct children of `SpartaItemDefinition` and of the fragment base |
| `ii-09-itemdef-families.sql` | family grouping for item definitions |
| `ii-10-items-dir-parents.sql` | parents of every class under `/Game/Sparta/Items/` |
| `ii-11-itemdef-closure.sql` | **transitive** closure of `SpartaItemDefinition` (the correct enumeration) |
| `ii-12-inventory-components.sql` | compact member dump (props+offsets, funcs+params) for the 12 inventory components |
| `ii-13-item-system.sql` | same for the item/fragment/upgrade-graph system |
| `ii-14-shop-drop-paths.sql` | exact paths+parents for shop/drop/forge blueprints |
| `ii-15-shop-drop-props.sql` | properties of the shop/drop/forge blueprints |
| `ii-16-shop-fns.sql` | functions+params of shop/merchant/drop/forge blueprints (CC temporaries filtered) |
| `ii-17-shophandler-props.sql` | properties + delegates of `BP_ShopHandlerBase` / `BP_Merchant` / `BP_UpgradePoolHandler` |
| `ii-18-itemdef-rawlines.sql` | verbatim dump lines for all 494 item definitions (Appendix A) |
| `ii-19-currency-plumbing.sql` | currency grant/read functions in `BPFL_Player` / `WBP_Player_Currency` / `BP_PlayerController` |
| `ii-20-verify-evidence.sql` | **self-check**: every `[addr] Type /path` line in this report is looked up in `obj` by addr **and** path (all 614 distinct lines verified) |
| `ii-21-fix-evidence.sql` | re-fetch of real dump lines for any evidence row that failed the check |
| `ii-22-check-weird-line.sql` | spot-check of one reconstructed line |

Generic pattern for one class:

```
python scripts\parse_dump.py show raw\index.sqlite3 <path-substring>
python scripts\parse_dump.py fun  raw\index.sqlite3 <regex-on-class-path>
python scripts\parse_dump.py raw  raw\index.sqlite3 <regex-on-path>   # evidence
python scripts\q.py raw\index.sqlite3 scripts\q\ii-12-inventory-components.sql
```

---

## Appendix A — all 494 item-definition classes, verbatim dump lines

Reconstructed byte-identically to `parse_dump.py raw` by `scripts\q\ii-18-itemdef-rawlines.sql`
(same `[addr] type path rest` shape). `[sps:]` is the SuperStruct; item definitions whose
`sps` is `00007FF46F067338` are direct children of `/Script/Sparta.SpartaItemDefinition`.
Sorted by path; the block was diffed line-by-line against a fresh run of that query —
494/494 lines identical. Every one of these 494 addresses was also confirmed to exist in
`obj` with exactly this path (`ii-20-verify-evidence.sql`).

```
[000001F7E929B8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/ID_Axatana_Axe.ID_Axatana_Axe_C [n: E9495] [c: 00007FF46EEC3D38] [or: 000001F7F77B1870] [sps: 00007FF46F067338]
[000001F7E929B530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/ID_Axatana_Katanas.ID_Axatana_Katanas_C [n: ED724] [c: 00007FF46EEC3D38] [or: 000001F7F77B17D0] [sps: 00007FF46F067338]
[000001F7E929B1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/Weapons/Axatana/ID_Axatana_Katanas_Spawnable.ID_Axatana_Katanas_Spawnable_C [n: 19E8A6] [c: 00007FF46EEC3D38] [or: 000001F7F77B1820] [sps: 000001F7E929B530]
[000001F7ED5A1C00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_AcolytesThorns.ID_Artifact_AcolytesThorns_C [n: 34AF94] [c: 00007FF46EEC3D38] [or: 000001F7ED346500] [sps: 00007FF46F067338]
[000001F7ED5A1870] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_AsceticsAlchemyStone.ID_Artifact_AsceticsAlchemyStone_C [n: 2478D7] [c: 00007FF46EEC3D38] [or: 000001F7ED346370] [sps: 00007FF46F067338]
[000001F7ED5A14E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_BlackBlood.ID_Artifact_BlackBlood_C [n: 3C73F4] [c: 00007FF46EEC3D38] [or: 000001F7ED346190] [sps: 00007FF46F067338]
[000001F7ED5A1150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_ClockworkSight.ID_Artifact_ClockworkSight_C [n: E94F4] [c: 00007FF46EEC3D38] [or: 000001F7ED346050] [sps: 00007FF46F067338]
[000001F7ED5A0DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_CrestOfBrotherhood.ID_Artifact_CrestOfBrotherhood_C [n: D30FF] [c: 00007FF46EEC3D38] [or: 000001F7ED345F10] [sps: 00007FF46F067338]
[000001F7ED5A0A30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_EredrimsInitiateTalisman.ID_Artifact_EredrimsInitiateTalisman_C [n: 425270] [c: 00007FF46EEC3D38] [or: 000001F7ED345DD0] [sps: 00007FF46F067338]
[000001F7ED5A06A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_FoulCenser.ID_Artifact_FoulCenser_C [n: 3C2AA6] [c: 00007FF46EEC3D38] [or: 000001F7ED347130] [sps: 00007FF46F067338]
[000001F7ED5A0310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_GenessasRosary.ID_Artifact_GenessasRosary_C [n: 16A587] [c: 00007FF46EEC3D38] [or: 000001F7ED346FA0] [sps: 00007FF46F067338]
[000001F4B73C14E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_GentlemansTeaCup.ID_Artifact_GentlemansTeaCup_C [n: 32D58E] [c: 00007FF46EEC3D38] [or: 000001F4D0154250] [sps: 00007FF46F067338]
[000001F7ED5AC700] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_GrishaGift.ID_Artifact_GrishaGift_C [n: 154C84] [c: 00007FF46EEC3D38] [or: 000001F7ED346DC0] [sps: 00007FF46F067338]
[000001F7ED5AC370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_HungryTarstone.ID_Artifact_HungryTarstone_C [n: 466A61] [c: 00007FF46EEC3D38] [or: 000001F7ED346870] [sps: 00007FF46F067338]
[000001F7ED5ABFE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_ImperialWrit.ID_Artifact_ImperialWrit_C [n: 35CCB2] [c: 00007FF46EEC3D38] [or: 000001F7ED346730] [sps: 00007FF46F067338]
[000001F7ED5ABC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_Mango.ID_Artifact_Mango_C [n: 1DADA3] [c: 00007FF46EEC3D38] [or: 000001F7ED347A90] [sps: 00007FF46F067338]
[000001F7ED5AB8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_MartyrsCursingCharm.ID_Artifact_MartyrsCursingCharm_C [n: 436EB8] [c: 00007FF46EEC3D38] [or: 000001F7ED347900] [sps: 00007FF46F067338]
[000001F7ED5AB530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_MoltenShaft.ID_Artifact_MoltenShaft_C [n: 492424] [c: 00007FF46EEC3D38] [or: 000001F7ED3477C0] [sps: 00007FF46F067338]
[000001F7ED5AB1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_PerfumedCenser.ID_Artifact_PerfumedCenser_C [n: 425284] [c: 00007FF46EEC3D38] [or: 000001F7ED347680] [sps: 00007FF46F067338]
[000001F7ED5AAE10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_SavedByTheShell.ID_Artifact_SavedByTheShell_C [n: 1A2BC7] [c: 00007FF46EEC3D38] [or: 000001F7ED347540] [sps: 00007FF46F067338]
[000001F7ED5AAA80] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_ScholarsAmulet.ID_Artifact_ScholarsAmulet_C [n: 20F4F4] [c: 00007FF46EEC3D38] [or: 000001F7ED347400] [sps: 00007FF46F067338]
[000001F7ED5AA6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_ScriptureOfPenance.ID_Artifact_ScriptureOfPenance_C [n: 365F43] [c: 00007FF46EEC3D38] [or: 000001F7ED3472C0] [sps: 00007FF46F067338]
[000001F7ED5AA360] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_SealOfTheStonebound.ID_Artifact_SealOfTheStonebound_C [n: ED72F] [c: 00007FF46EEC3D38] [or: 000001F7ED347180] [sps: 00007FF46F067338]
[000001F7ED5A9FD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_SolemnOffering.ID_Artifact_SolemnOffering_C [n: 143EFA] [c: 00007FF46EEC3D38] [or: 000001F7ED3484E0] [sps: 00007FF46F067338]
[000001F7ED5A9C40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_SolomonsEffigy.ID_Artifact_SolomonsEffigy_C [n: 4853EC] [c: 00007FF46EEC3D38] [or: 000001F7ED3483A0] [sps: 00007FF46F067338]
[000001F7ED5A98B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_TarForgedIdol.ID_Artifact_TarForgedIdol_C [n: 3841F8] [c: 00007FF46EEC3D38] [or: 000001F7ED348260] [sps: 00007FF46F067338]
[000001F7ED5A9520] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_TarsaltCoating.ID_Artifact_TarsaltCoating_C [n: 49AE67] [c: 00007FF46EEC3D38] [or: 000001F7ED348120] [sps: 00007FF46F067338]
[000001F7ED5A9190] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_TarsusGauntlet.ID_Artifact_TarsusGauntlet_C [n: 4966BC] [c: 00007FF46EEC3D38] [or: 000001F7ED347FE0] [sps: 00007FF46F067338]
[000001F7ED5A8E00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_TatteredEffigyOfTheScholar.ID_Artifact_TatteredEffigyOfTheScholar_C [n: 448184] [c: 00007FF46EEC3D38] [or: 000001F7ED347EA0] [sps: 00007FF46F067338]
[000001F7ED5A8A70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_TwicebornsRequital.ID_Artifact_TwicebornsRequital_C [n: 1C5528] [c: 00007FF46EEC3D38] [or: 000001F7ED347D60] [sps: 00007FF46F067338]
[000001F7ED5A86E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/ID_Artifact_TwinSestersStone.ID_Artifact_TwinSestersStone_C [n: 17F37C] [c: 00007FF46EEC3D38] [or: 000001F7ED347C20] [sps: 00007FF46F067338]
[000001F7ED5D5C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_GoldBoost.ID_Artifact_GoldBoost_C [n: CAAE9] [c: 00007FF46EEC3D38] [or: 000001F7ED348AD0] [sps: 00007FF46F067338]
[000001F7ED5D5890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_GorfsBlessing.ID_Artifact_GorfsBlessing_C [n: 2DF120] [c: 00007FF46EEC3D38] [or: 000001F7ED348990] [sps: 00007FF46F067338]
[000001F7ED5D5500] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_HealOnMelee.ID_Artifact_HealOnMelee_C [n: 3F3751] [c: 00007FF46EEC3D38] [or: 000001F7ED3488A0] [sps: 00007FF46F067338]
[000001F7ED5D5170] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_IncreaseCritChanceOnSprintAttack.ID_Artifact_IncreaseCritChanceOnSprintAttack_C [n: 24BF88] [c: 00007FF46EEC3D38] [or: 000001F7ED3487B0] [sps: 00007FF46F067338]
[000001F7ED5D4DE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_LightningImmunity.ID_Artifact_LightningImmunity_C [n: 409FBA] [c: 00007FF46EEC3D38] [or: 000001F7ED3486C0] [sps: 00007FF46F067338]
[000001F7ED5D4A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_PoisonBuff.ID_Artifact_PoisonBuff_C [n: 400FB7] [c: 00007FF46EEC3D38] [or: 000001F7ED349A70] [sps: 00007FF46F067338]
[000001F7ED5D46C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Artifacts/NarrativeArtifacts/ID_Artifact_XpBoost.ID_Artifact_XpBoost_C [n: 346BDF] [c: 00007FF46EEC3D38] [or: 000001F7ED349930] [sps: 00007FF46F067338]
[000001F8B8AE7510] BlueprintGeneratedClass /Game/Sparta/Core/Player/Debuffs/ID_Debuff_BrandOfTheDeserter.ID_Debuff_BrandOfTheDeserter_C [n: 272D3E] [c: 00007FF46EEC3D38] [or: 000001F7ED34BFA0] [sps: 00007FF46F067338]
[000001F8B8AE7180] BlueprintGeneratedClass /Game/Sparta/Core/Player/Debuffs/ID_Debuff_DreamDamage.ID_Debuff_DreamDamage_C [n: 4BDD92] [c: 00007FF46EEC3D38] [or: 000001F7ED34BF00] [sps: 00007FF46F067338]
[000001F8B8AE6DF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Debuffs/ID_Debuff_Plague.ID_Debuff_Plague_C [n: B08B2] [c: 00007FF46EEC3D38] [or: 000001F7ED34BE60] [sps: 00007FF46F067338]
[000001F8B8AE6A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Debuffs/ID_Debuff_Test15.ID_Debuff_Test15_C [n: 4A84E6] [c: 00007FF46EEC3D38] [or: 000001F7ED34BD70] [sps: 00007FF46F067338]
[000001F8B8AE66D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Debuffs/ID_Debuff_Test26.ID_Debuff_Test26_C [n: 466BE2] [c: 00007FF46EEC3D38] [or: 000001F7ED34BC30] [sps: 00007FF46F067338]
[000001F8B8AE6340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Debuffs/ID_Debuff_Test27.ID_Debuff_Test27_C [n: 466BEC] [c: 00007FF46EEC3D38] [or: 000001F7ED34BB90] [sps: 00007FF46F067338]
[000001F8B8AE5FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Debuffs/ID_Debuff_Test5.ID_Debuff_Test5_C [n: 3B0A34] [c: 00007FF46EEC3D38] [or: 000001F7ED34BAF0] [sps: 00007FF46F067338]
[000001F8B8AE5C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Debuffs/ID_Debuff_Test6.ID_Debuff_Test6_C [n: 49B01C] [c: 00007FF46EEC3D38] [or: 000001F7ED34BA50] [sps: 00007FF46F067338]
[000001F7ED5DE710] BlueprintGeneratedClass /Game/Sparta/Core/Player/Debuffs/ID_Debuff_Test9.ID_Debuff_Test9_C [n: 4742B8] [c: 00007FF46EEC3D38] [or: 000001F7ED34B9B0] [sps: 00007FF46F067338]
[000001F8B8AE5890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Debuffs/ID_Debuff_ThrowsOfMercy.ID_Debuff_ThrowsOfMercy_C [n: B5253] [c: 00007FF46EEC3D38] [or: 000001F7ED34CDB0] [sps: 00007FF46F067338]
[000001F8B8AEF8E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Melee/Generic/ID_Melee_Generic_Crazed.ID_Melee_Generic_Crazed_C [n: 6AA08] [c: 00007FF46EEC3D38] [or: 000001F7ED34E070] [sps: 00007FF46F067338]
[000001F8B8AEF550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Melee/Generic/ID_Melee_Generic_CritBonus.ID_Melee_Generic_CritBonus_C [n: 2FE54F] [c: 00007FF46EEC3D38] [or: 000001F7ED34DF30] [sps: 00007FF46F067338]
[000001F8B8AEF1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Melee/Generic/ID_Melee_Generic_CritChance.ID_Melee_Generic_CritChance_C [n: 31CB78] [c: 00007FF46EEC3D38] [or: 000001F7ED34DDF0] [sps: 00007FF46F067338]
[000001F8B8AEEE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Melee/Generic/ID_Melee_Generic_PoiseBonus.ID_Melee_Generic_PoiseBonus_C [n: 2E3A22] [c: 00007FF46EEC3D38] [or: 000001F7ED34DCB0] [sps: 00007FF46F067338]
[000001F8CDF07510] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Melee/Generic/ID_Melee_Generic_ResolveGain.ID_Melee_Generic_ResolveGain_C [n: B08BC] [c: 00007FF46EEC3D38] [or: 000001F7ED34DB70] [sps: 00007FF46F067338]
[000001F8CDF066D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Melee/Passives/Perforation/ID_Melee_InflictPerforation.ID_Melee_InflictPerforation_C [n: 3A79EC] [c: 00007FF46EEC3D38] [or: 000001F7ED34D8F0] [sps: 00007FF46F067338]
[000001F8CDF05C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Meta/MapPing/ID_Unlock_MapPing.ID_Unlock_MapPing_C [n: 4A8620] [c: 00007FF46EEC3D38] [or: 000001F7ED34EC00] [sps: 00007FF46F067338]
[000001F8C8136DF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Alacrity.ID_Eredrim_Alacrity_C [n: 46FD22] [c: 00007FF46EEC3D38] [or: 000001F8C80F0100] [sps: 00007FF46F067338]
[000001F8C8136A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Anguish.ID_Eredrim_Anguish_C [n: A366C] [c: 00007FF46EEC3D38] [or: 000001F8C80F1320] [sps: 00007FF46F067338]
[000001F8C81366D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Apathy.ID_Eredrim_Apathy_C [n: FE657] [c: 00007FF46EEC3D38] [or: 000001F8C80F1190] [sps: 00007FF46F067338]
[000001F8C8136340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Carnage.ID_Eredrim_Carnage_C [n: 3FCC69] [c: 00007FF46EEC3D38] [or: 000001F8C80F1000] [sps: 00007FF46F067338]
[000001F8C8135FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Consecration.ID_Eredrim_Consecration_C [n: 3F3A76] [c: 00007FF46EEC3D38] [or: 000001F8C80F0EC0] [sps: 00007FF46F067338]
[000001F8C8135C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Massacre.ID_Eredrim_Massacre_C [n: 26A35D] [c: 00007FF46EEC3D38] [or: 000001F8C80F0D80] [sps: 00007FF46F067338]
[000001F8C8135890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Oppression.ID_Eredrim_Oppression_C [n: 32965A] [c: 00007FF46EEC3D38] [or: 000001F8C80F0C40] [sps: 00007FF46F067338]
[000001F8C8135500] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Persistence.ID_Eredrim_Persistence_C [n: 44C9B9] [c: 00007FF46EEC3D38] [or: 000001F8C80F0B00] [sps: 00007FF46F067338]
[000001F8C8135170] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Radiance.ID_Eredrim_Radiance_C [n: 9F246] [c: 00007FF46EEC3D38] [or: 000001F8C80F1E60] [sps: 00007FF46F067338]
[000001F8C8134DE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Resonance.ID_Eredrim_Resonance_C [n: 6EA35] [c: 00007FF46EEC3D38] [or: 000001F8C80F1D20] [sps: 00007FF46F067338]
[000001F8C8134A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_SealAffinity.ID_Eredrim_SealAffinity_C [n: DC07D] [c: 00007FF46EEC3D38] [or: 000001F8C80F1B90] [sps: 00007FF46F067338]
[000001F8C81346C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Tank.ID_Eredrim_Tank_C [n: 466C3F] [c: 00007FF46EEC3D38] [or: 000001F8C80F1BE0] [sps: 00007FF46F067338]
[000001F8C8134330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Tier_1.ID_Eredrim_Tier_1_C [n: 2FE5D3] [c: 00007FF46EEC3D38] [or: 000001F8C80F1910] [sps: 00007FF46F067338]
[000001F8C8133FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Tier_2.ID_Eredrim_Tier_2_C [n: 2592AA] [c: 00007FF46EEC3D38] [or: 000001F8C80F17D0] [sps: 00007FF46F067338]
[000001F8C8133C10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Tier_3.ID_Eredrim_Tier_3_C [n: 132E1C] [c: 00007FF46EEC3D38] [or: 000001F8C80F1640] [sps: 00007FF46F067338]
[000001F8C8133880] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Eredrim/ID_Eredrim_Tier_4.ID_Eredrim_Tier_4_C [n: 3E5ED1] [c: 00007FF46EEC3D38] [or: 000001F8C80F14B0] [sps: 00007FF46F067338]
[000001F8C8190310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Affinity.ID_Genessa_Affinity_C [n: 417618] [c: 00007FF46EEC3D38] [or: 000001F8C80F3710] [sps: 00007FF46F067338]
[000001F8C819EAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Alteration.ID_Genessa_Alteration_C [n: 4A3DF9] [c: 00007FF46EEC3D38] [or: 000001F8C80F3530] [sps: 00007FF46F067338]
[000001F8C819E710] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Augmentation.ID_Genessa_Augmentation_C [n: 4B94EE] [c: 00007FF46EEC3D38] [or: 000001F8C80F3580] [sps: 00007FF46F067338]
[000001F8C819E380] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Catalyst.ID_Genessa_Catalyst_C [n: 3B4F78] [c: 00007FF46EEC3D38] [or: 000001F8C80F47A0] [sps: 00007FF46F067338]
[000001F8C819DFF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Causality.ID_Genessa_Causality_C [n: 3D3BC2] [c: 00007FF46EEC3D38] [or: 000001F8C80F4660] [sps: 00007FF46F067338]
[000001F8C819DC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Communion.ID_Genessa_Communion_C [n: 492745] [c: 00007FF46EEC3D38] [or: 000001F8C80F4520] [sps: 00007FF46F067338]
[000001F8C819D8D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Distraction.ID_Genessa_Distraction_C [n: D3357] [c: 00007FF46EEC3D38] [or: 000001F8C80F43E0] [sps: 00007FF46F067338]
[000001F8C819D540] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Impetus.ID_Genessa_Impetus_C [n: 321042] [c: 00007FF46EEC3D38] [or: 000001F8C80F42A0] [sps: 00007FF46F067338]
[000001F8C819D1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Instigation.ID_Genessa_Instigation_C [n: 33E97D] [c: 00007FF46EEC3D38] [or: 000001F8C80F4160] [sps: 00007FF46F067338]
[000001F8C819CE20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Intangible.ID_Genessa_Intangible_C [n: A79CF] [c: 00007FF46EEC3D38] [or: 000001F8C80F3FD0] [sps: 00007FF46F067338]
[000001F8C819CA90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Mirage.ID_Genessa_Mirage_C [n: 28D750] [c: 00007FF46EEC3D38] [or: 000001F8C80F3E90] [sps: 00007FF46F067338]
[000001F8C819C700] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Tier_1.ID_Genessa_Tier_1_C [n: 437107] [c: 00007FF46EEC3D38] [or: 000001F8C80F51F0] [sps: 00007FF46F067338]
[000001F8C819C370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Tier_2.ID_Genessa_Tier_2_C [n: 34B207] [c: 00007FF46EEC3D38] [or: 000001F8C80F50B0] [sps: 00007FF46F067338]
[000001F8C819BFE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Tier_3.ID_Genessa_Tier_3_C [n: 33E98A] [c: 00007FF46EEC3D38] [or: 000001F8C80F4F70] [sps: 00007FF46F067338]
[000001F8C819BC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Tier_4.ID_Genessa_Tier_4_C [n: 3B0ACA] [c: 00007FF46EEC3D38] [or: 000001F8C80F4E30] [sps: 00007FF46F067338]
[000001F8C819B8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Genessa/ID_Genessa_Timeless.ID_Genessa_Timeless_C [n: 4742FC] [c: 00007FF46EEC3D38] [or: 000001F8C80F4020] [sps: 00007FF46F067338]
[000001F8C81A1F90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Anger.ID_Gragu_Anger_C [n: 213F54] [c: 00007FF46EEC3D38] [or: 000001F8C80F5600] [sps: 00007FF46F067338]
[000001F8C81A1C00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Attunement.ID_Gragu_Attunement_C [n: 432968] [c: 00007FF46EEC3D38] [or: 000001F8C80F66E0] [sps: 00007FF46F067338]
[000001F8C81A1870] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Bloodlust.ID_Gragu_Bloodlust_C [n: 24338A] [c: 00007FF46EEC3D38] [or: 000001F8C80F65A0] [sps: 00007FF46F067338]
[000001F8C81A14E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Bloodthirst.ID_Gragu_Bloodthirst_C [n: 148475] [c: 00007FF46EEC3D38] [or: 000001F8C80F6460] [sps: 00007FF46F067338]
[000001F8C81A1150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Devourer.ID_Gragu_Devourer_C [n: 321057] [c: 00007FF46EEC3D38] [or: 000001F8C80F6320] [sps: 00007FF46F067338]
[000001F8C81A0DC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Escalation.ID_Gragu_Escalation_C [n: 8034F] [c: 00007FF46EEC3D38] [or: 000001F8C80F61E0] [sps: 00007FF46F067338]
[000001F8C81A0A30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Fortify.ID_Gragu_Fortify_C [n: C6AF7] [c: 00007FF46EEC3D38] [or: 000001F8C80F6050] [sps: 00007FF46F067338]
[000001F8C81A06A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Harvest.ID_Gragu_Harvest_C [n: 6AACE] [c: 00007FF46EEC3D38] [or: 000001F8C80F60A0] [sps: 00007FF46F067338]
[000001F8C81A0310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Heartless.ID_Gragu_Heartless_C [n: E981C] [c: 00007FF46EEC3D38] [or: 000001F8C80F5E20] [sps: 00007FF46F067338]
[000001F8C81AEAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Heartwork.ID_Gragu_Heartwork_C [n: 19A363] [c: 00007FF46EEC3D38] [or: 000001F8C80F5CE0] [sps: 00007FF46F067338]
[000001F8C81AE710] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Hunger.ID_Gragu_Hunger_C [n: 3800AC] [c: 00007FF46EEC3D38] [or: 000001F8C80F7040] [sps: 00007FF46F067338]
[000001F8C81AE380] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Maniac.ID_Gragu_Maniac_C [n: 3A79FC] [c: 00007FF46EEC3D38] [or: 000001F8C80F6EB0] [sps: 00007FF46F067338]
[000001F8C81ADFF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Tier_1.ID_Gragu_Tier_1_C [n: 6AAD8] [c: 00007FF46EEC3D38] [or: 000001F8C80F6D70] [sps: 00007FF46F067338]
[000001F8C81ADC60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Tier_2.ID_Gragu_Tier_2_C [n: 13B7C9] [c: 00007FF46EEC3D38] [or: 000001F8C80F6C30] [sps: 00007FF46F067338]
[000001F8C81AD8D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Gragu/ID_Gragu_Tier_3.ID_Gragu_Tier_3_C [n: 2B8A45] [c: 00007FF46EEC3D38] [or: 000001F8C80F6AF0] [sps: 00007FF46F067338]
[000001F8D88F5FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Adaptation.ID_Lazlo_Adaptation_C [n: 30755A] [c: 00007FF46EEC3D38] [or: 000001F8C80F8490] [sps: 00007FF46F067338]
[000001F8D88F5C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Breaker.ID_Lazlo_Breaker_C [n: 2C1312] [c: 00007FF46EEC3D38] [or: 000001F8C80F8120] [sps: 00007FF46F067338]
[000001F8D88F5890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Detonation.ID_Lazlo_Detonation_C [n: 3BE427] [c: 00007FF46EEC3D38] [or: 000001F8C80F7FE0] [sps: 00007FF46F067338]
[000001F8D88F5500] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Furnace.ID_Lazlo_Furnace_C [n: 3C2EDB] [c: 00007FF46EEC3D38] [or: 000001F8C80F7EA0] [sps: 00007FF46F067338]
[000001F8D88F5170] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Incandescence.ID_Lazlo_Incandescence_C [n: 1ECDD8] [c: 00007FF46EEC3D38] [or: 000001F8C80F7D10] [sps: 00007FF46F067338]
[000001F8D88F4DE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Inflamed.ID_Lazlo_Inflamed_C [n: 2E830D] [c: 00007FF46EEC3D38] [or: 000001F8C80F7D60] [sps: 00007FF46F067338]
[000001F8D88F4A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_InnerFire.ID_Lazlo_InnerFire_C [n: 1A763F] [c: 00007FF46EEC3D38] [or: 000001F8C80F8F80] [sps: 00007FF46F067338]
[000001F8D88F46C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Rekindle.ID_Lazlo_Rekindle_C [n: 6EA4C] [c: 00007FF46EEC3D38] [or: 000001F8C80F8E40] [sps: 00007FF46F067338]
[000001F8D88F4330] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Rush.ID_Lazlo_Rush_C [n: 18CFB5] [c: 00007FF46EEC3D38] [or: 000001F8C80F8D00] [sps: 00007FF46F067338]
[000001F8D88F3FA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Temperament.ID_Lazlo_Temperament_C [n: 19EAF0] [c: 00007FF46EEC3D38] [or: 000001F8C80F8BC0] [sps: 00007FF46F067338]
[000001F8D88F3C10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Tier_1.ID_Lazlo_Tier_1_C [n: 3F84DC] [c: 00007FF46EEC3D38] [or: 000001F8C80F8A30] [sps: 00007FF46F067338]
[000001F8D88F3880] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Tier_2.ID_Lazlo_Tier_2_C [n: 3A7A2B] [c: 00007FF46EEC3D38] [or: 000001F8C80F88F0] [sps: 00007FF46F067338]
[000001F8D88F34F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Tier_3.ID_Lazlo_Tier_3_C [n: 372F5F] [c: 00007FF46EEC3D38] [or: 000001F8C80F87B0] [sps: 00007FF46F067338]
[000001F8D88F3160] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_Tier_4.ID_Lazlo_Tier_4_C [n: 466C49] [c: 00007FF46EEC3D38] [or: 000001F8C80F8670] [sps: 00007FF46F067338]
[000001F8D88F2DD0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_WarmedUp.ID_Lazlo_WarmedUp_C [n: 3CBA82] [c: 00007FF46EEC3D38] [or: 000001F8C80F8A80] [sps: 00007FF46F067338]
[000001F8D88F2A40] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Lazlo/ID_Lazlo_WorthyArmor.ID_Lazlo_WorthyArmor_C [n: 4B4F78] [c: 00007FF46EEC3D38] [or: 000001F8C80F98E0] [sps: 00007FF46F067338]
[000001F8D88FF550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Ampereon.ID_Proxima_Ampereon_C [n: 36EE57] [c: 00007FF46EEC3D38] [or: 000001F8C80FA1A0] [sps: 00007FF46F067338]
[000001F8D88FF1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Attunement.ID_Proxima_Attunement_C [n: 18885C] [c: 00007FF46EEC3D38] [or: 000001F8C80F9FC0] [sps: 00007FF46F067338]
[000001F8D88FEE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Biohazard.ID_Proxima_Biohazard_C [n: 195D51] [c: 00007FF46EEC3D38] [or: 000001F8C80F9E80] [sps: 00007FF46F067338]
[000001F8D8977510] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Biohunter.ID_Proxima_Biohunter_C [n: 1189C3] [c: 00007FF46EEC3D38] [or: 000001F8C80F9D40] [sps: 00007FF46F067338]
[000001F8D8977180] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Collector.ID_Proxima_Collector_C [n: 1D68D3] [c: 00007FF46EEC3D38] [or: 000001F8C80F9C00] [sps: 00007FF46F067338]
[000001F8D8976DF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Discharge.ID_Proxima_Discharge_C [n: 213F7E] [c: 00007FF46EEC3D38] [or: 000001F8C80F9AC0] [sps: 00007FF46F067338]
[000001F8D8976A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Energize.ID_Proxima_Energize_C [n: 4AC752] [c: 00007FF46EEC3D38] [or: 000001F8C80FAE20] [sps: 00007FF46F067338]
[000001F8D89766D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Exoshell.ID_Proxima_Exoshell_C [n: 342A10] [c: 00007FF46EEC3D38] [or: 000001F8C80FACE0] [sps: 00007FF46F067338]
[000001F8D8976340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Fortress.ID_Proxima_Fortress_C [n: 1A764A] [c: 00007FF46EEC3D38] [or: 000001F8C80FABA0] [sps: 00007FF46F067338]
[000001F8D8975FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Reflection.ID_Proxima_Reflection_C [n: 35CF3B] [c: 00007FF46EEC3D38] [or: 000001F8C80FAA60] [sps: 00007FF46F067338]
[000001F8D8975C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Seizure.ID_Proxima_Seizure_C [n: 24C298] [c: 00007FF46EEC3D38] [or: 000001F8C80FA920] [sps: 00007FF46F067338]
[000001F8D8975890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Shellshock.ID_Proxima_Shellshock_C [n: 23EE95] [c: 00007FF46EEC3D38] [or: 000001F8C80FA7E0] [sps: 00007FF46F067338]
[000001F8D8975500] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Tier_1.ID_Proxima_Tier_1_C [n: 91F6A] [c: 00007FF46EEC3D38] [or: 000001F8C80FA650] [sps: 00007FF46F067338]
[000001F8D8975170] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Tier_2.ID_Proxima_Tier_2_C [n: 261A3E] [c: 00007FF46EEC3D38] [or: 000001F8C80FA510] [sps: 00007FF46F067338]
[000001F8D8974DE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Tier_3.ID_Proxima_Tier_3_C [n: 26EC17] [c: 00007FF46EEC3D38] [or: 000001F8C80FB870] [sps: 00007FF46F067338]
[000001F8D8974A50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Proxima/ID_Proxima_Tier_4.ID_Proxima_Tier_4_C [n: 43FDB4] [c: 00007FF46EEC3D38] [or: 000001F8C80FB730] [sps: 00007FF46F067338]
[000001F8D8977FC0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Absolution.ID_Smert_Absolution_C [n: 266165] [c: 00007FF46EEC3D38] [or: 000001F8C80FC180] [sps: 00007FF46F067338]
[000001F8D8977C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Beatdown.ID_Smert_Beatdown_C [n: 21BE21] [c: 00007FF46EEC3D38] [or: 000001F8C80FC040] [sps: 00007FF46F067338]
[000001F8D89778A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Blessing.ID_Smert_Blessing_C [n: 18CFBE] [c: 00007FF46EEC3D38] [or: 000001F8C80FBF00] [sps: 00007FF46F067338]
[000001F8D897FC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Devotion.ID_Smert_Devotion_C [n: 27BE25] [c: 00007FF46EEC3D38] [or: 000001F8C80FBDC0] [sps: 00007FF46F067338]
[000001F8D897F8E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Fervor.ID_Smert_Fervor_C [n: 361B5A] [c: 00007FF46EEC3D38] [or: 000001F8C80FBC80] [sps: 00007FF46F067338]
[000001F8D897F550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Kicker.ID_Smert_Kicker_C [n: 2C9EB8] [c: 00007FF46EEC3D38] [or: 000001F8C80FBB40] [sps: 00007FF46F067338]
[000001F8D897F1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_LastVow.ID_Smert_LastVow_C [n: 3661D9] [c: 00007FF46EEC3D38] [or: 000001F8C80FBA00] [sps: 00007FF46F067338]
[000001F8D897EE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Limitless.ID_Smert_Limitless_C [n: 45DF02] [c: 00007FF46EEC3D38] [or: 000001F8C80F4250] [sps: 00007FF46F067338]
[000001F8D89D7510] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Novertime.ID_Smert_Novertime_C [n: 13B7E2] [c: 00007FF46EEC3D38] [or: 000001F8C80F4750] [sps: 00007FF46F067338]
[000001F8D89D7180] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Resilience.ID_Smert_Resilience_C [n: 35CF48] [c: 00007FF46EEC3D38] [or: 000001F7ED34DEE0] [sps: 00007FF46F067338]
[000001F8D89D6DF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Striker.ID_Smert_Striker_C [n: 2DAEC5] [c: 00007FF46EEC3D38] [or: 000001F7ED34B730] [sps: 00007FF46F067338]
[000001F8D89D6A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Tenacity.ID_Smert_Tenacity_C [n: FE68C] [c: 00007FF46EEC3D38] [or: 000001F7ED34A3D0] [sps: 00007FF46F067338]
[000001F8D89D66D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Tier_1.ID_Smert_Tier_1_C [n: 272D84] [c: 00007FF46EEC3D38] [or: 000001F7ED347F90] [sps: 00007FF46F067338]
[000001F8D89D6340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Tier_2.ID_Smert_Tier_2_C [n: 4550DB] [c: 00007FF46EEC3D38] [or: 000001F7ED348490] [sps: 00007FF46F067338]
[000001F8D89D5FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Tier_3.ID_Smert_Tier_3_C [n: 47434F] [c: 00007FF46EEC3D38] [or: 000001F7ED3474F0] [sps: 00007FF46F067338]
[000001F8D89D5C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Smert/ID_Smert_Tier_4.ID_Smert_Tier_4_C [n: 7324B] [c: 00007FF46EEC3D38] [or: 000001F7ED347A40] [sps: 00007FF46F067338]
[000001F8D89DD8D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Affliction.ID_Sariel_Affliction_C [n: D7B29] [c: 00007FF46EEC3D38] [or: 000001F8CF1BED40] [sps: 00007FF46F067338]
[000001F8D89DD540] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Agonize.ID_Sariel_Agonize_C [n: 26617B] [c: 00007FF46EEC3D38] [or: 000001F8CF1BED90] [sps: 00007FF46F067338]
[000001F8D89DD1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Entitlement.ID_Sariel_Entitlement_C [n: 1668D5] [c: 00007FF46EEC3D38] [or: 000001F8C80FC9F0] [sps: 00007FF46F067338]
[000001F8D89DCE20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Frailty.ID_Sariel_Frailty_C [n: 137255] [c: 00007FF46EEC3D38] [or: 000001F8C80FC860] [sps: 00007FF46F067338]
[000001F8D89DCA90] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Growth.ID_Sariel_Growth_C [n: 1D68DF] [c: 00007FF46EEC3D38] [or: 000001F8C80FC8B0] [sps: 00007FF46F067338]
[000001F8D89DC700] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Reaper.ID_Sariel_Reaper_C [n: 3FCC96] [c: 00007FF46EEC3D38] [or: 000001F8C80FC630] [sps: 00007FF46F067338]
[000001F8D89DC370] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Restoration.ID_Sariel_Restoration_C [n: 4BDF01] [c: 00007FF46EEC3D38] [or: 000001F8C80FC4F0] [sps: 00007FF46F067338]
[000001F8D89DBFE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Reversal.ID_Sariel_Reversal_C [n: 6AAE2] [c: 00007FF46EEC3D38] [or: 000001F8C80FD850] [sps: 00007FF46F067338]
[000001F8D89DBC50] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Scourge.ID_Sariel_Scourge_C [n: CEEF1] [c: 00007FF46EEC3D38] [or: 000001F8C80FD710] [sps: 00007FF46F067338]
[000001F8D89DB8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Synthesis.ID_Sariel_Synthesis_C [n: 4550E5] [c: 00007FF46EEC3D38] [or: 000001F8C80FD5D0] [sps: 00007FF46F067338]
[000001F8D89DB530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Thornfall.ID_Sariel_Thornfall_C [n: 3E16CF] [c: 00007FF46EEC3D38] [or: 000001F8C80FD490] [sps: 00007FF46F067338]
[000001F8D89DB1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Tier_1.ID_Sariel_Tier_1_C [n: 16266A] [c: 00007FF46EEC3D38] [or: 000001F8C80FD300] [sps: 00007FF46F067338]
[000001F8D89DAE10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Tier_2.ID_Sariel_Tier_2_C [n: 4814CC] [c: 00007FF46EEC3D38] [or: 000001F8C80FD1C0] [sps: 00007FF46F067338]
[000001F8D89DAA80] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Tier_3.ID_Sariel_Tier_3_C [n: 1C1515] [c: 00007FF46EEC3D38] [or: 000001F8C80FD080] [sps: 00007FF46F067338]
[000001F8D89DA6F0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Tier_4.ID_Sariel_Tier_4_C [n: 4A865A] [c: 00007FF46EEC3D38] [or: 000001F8C80FCF40] [sps: 00007FF46F067338]
[000001F8D89DA360] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Thorn/ID_Sariel_Torment.ID_Sariel_Torment_C [n: 2C9EF3] [c: 00007FF46EEC3D38] [or: 000001F8C80FD350] [sps: 00007FF46F067338]
[000001F8D8A17C30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_Ambush.ID_Tiel_Ambush_C [n: 3C2EE5] [c: 00007FF46EEC3D38] [or: 000001F8C80FE610] [sps: 00007FF46F067338]
[000001F8D8A178A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_CriticalRole.ID_Tiel_CriticalRole_C [n: 4BDF0E] [c: 00007FF46EEC3D38] [or: 000001F8C80FF740] [sps: 00007FF46F067338]
[000001F8D8A1FC70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_Deathmark.ID_Tiel_Deathmark_C [n: 2C131C] [c: 00007FF46EEC3D38] [or: 000001F8C80FF600] [sps: 00007FF46F067338]
[000001F8D8A1F8E0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_Escalation.ID_Tiel_Escalation_C [n: 401373] [c: 00007FF46EEC3D38] [or: 000001F8C80FF4C0] [sps: 00007FF46F067338]
[000001F8D8A1F550] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_LastingLegacy.ID_Tiel_LastingLegacy_C [n: 1ECDF1] [c: 00007FF46EEC3D38] [or: 000001F8C80FF380] [sps: 00007FF46F067338]
[000001F8D8A1F1C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_Lethality.ID_Tiel_Lethality_C [n: 3F3AB5] [c: 00007FF46EEC3D38] [or: 000001F8C80FF240] [sps: 00007FF46F067338]
[000001F8D8A1EE30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_Nightstride.ID_Tiel_Nightstride_C [n: 346E79] [c: 00007FF46EEC3D38] [or: 000001F8C80FF100] [sps: 00007FF46F067338]
[000001F8D8A57510] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_PlagueBearer.ID_Tiel_PlagueBearer_C [n: 3B0B0E] [c: 00007FF46EEC3D38] [or: 000001F8C80FEFC0] [sps: 00007FF46F067338]
[000001F8D8A57180] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_PoisonBurst.ID_Tiel_PoisonBurst_C [n: 36EE74] [c: 00007FF46EEC3D38] [or: 000001F8C80FEE30] [sps: 00007FF46F067338]
[000001F8D8A56DF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_SealAffinity.ID_Tiel_SealAffinity_C [n: 437126] [c: 00007FF46EEC3D38] [or: 000001F8C80FEE80] [sps: 00007FF46F067338]
[000001F8D8A56A60] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_ShadowClone.ID_Tiel_ShadowClone_C [n: 35CF54] [c: 00007FF46EEC3D38] [or: 000001F8C80FFEC0] [sps: 00007FF46F067338]
[000001F8D8A566D0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_ShadowStalk.ID_Tiel_ShadowStalk_C [n: 2B8AAC] [c: 00007FF46EEC3D38] [or: 000001F8C80FFD80] [sps: 00007FF46F067338]
[000001F8D8A56340] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_Tier_1.ID_Tiel_Tier_1_C [n: 444109] [c: 00007FF46EEC3D38] [or: 000001F8C80FFBF0] [sps: 00007FF46F067338]
[000001F8D8A55FB0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_Tier_2.ID_Tiel_Tier_2_C [n: 261A6E] [c: 00007FF46EEC3D38] [or: 000001F8C80FFAB0] [sps: 00007FF46F067338]
[000001F8D8A55C20] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_Tier_3.ID_Tiel_Tier_3_C [n: 27BE61] [c: 00007FF46EEC3D38] [or: 000001F8C80FF970] [sps: 00007FF46F067338]
[000001F8D8A55890] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Shells/Tiel/ID_Tiel_Tier_4.ID_Tiel_Tier_4_C [n: 17F683] [c: 00007FF46EEC3D38] [or: 000001F8C80FF830] [sps: 00007FF46F067338]
[000001F8D8A50A30] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Sidearm/Generic/ID_Sidearm_Generic_Crazed.ID_Sidearm_Generic_Crazed_C [n: 6AAF8] [c: 00007FF46EEC3D38] [or: 000001F8D8A60790] [sps: 00007FF46F067338]
[000001F8D8A506A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Sidearm/Generic/ID_Sidearm_Generic_CriticalHitChance.ID_Sidearm_Generic_CriticalHitChance_C [n: 34F6F2] [c: 00007FF46EEC3D38] [or: 000001F8D8A60650] [sps: 00007FF46F067338]
[000001F8D8A50310] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Sidearm/Generic/ID_Sidearm_Generic_CriticalHitDamage.ID_Sidearm_Generic_CriticalHitDamage_C [n: 2F9891] [c: 00007FF46EEC3D38] [or: 000001F8D8A60510] [sps: 00007FF46F067338]
[000001F8D8A5EAA0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Sidearm/Generic/ID_Sidearm_Generic_MaxAmmo.ID_Sidearm_Generic_MaxAmmo_C [n: 2592B5] [c: 00007FF46EEC3D38] [or: 000001F8D8A603D0] [sps: 00007FF46F067338]
[000001F8D8A5E710] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Sidearm/Generic/ID_Sidearm_Generic_PoiseDamage.ID_Sidearm_Generic_PoiseDamage_C [n: 26EC22] [c: 00007FF46EEC3D38] [or: 000001F8D8A60240] [sps: 00007FF46F067338]
[000001F8D8A5E380] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Sidearm/Generic/ID_Sidearm_Generic_ReloadDuration.ID_Sidearm_Generic_ReloadDuration_C [n: DC0EC] [c: 00007FF46EEC3D38] [or: 000001F8D8A60100] [sps: 00007FF46F067338]
[000001F8D8A5DFF0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Sidearm/Generic/ID_Sidearm_Generic_ResolveGain.ID_Sidearm_Generic_ResolveGain_C [n: 2A2B81] [c: 00007FF46EEC3D38] [or: 000001F8D8A613C0] [sps: 00007FF46F067338]
[000001F8D8A5D1B0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Sidearm/Passives/Fragile/ID_Sidearm_InflictFragile.ID_Sidearm_InflictFragile_C [n: 420B77] [c: 00007FF46EEC3D38] [or: 000001F8D8A61190] [sps: 00007FF46F067338]
[000001F8D8A5B530] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/NailShotgun/ID_NailShotgun_DamageDecreaseRangeIncrease.ID_NailShotgun_DamageDecreaseRangeIncrease_C [n: 154F1F] [c: 00007FF46EEC3D38] [or: 000001F8D8A60EC0] [sps: 00007FF46F067338]
[000001F8D8A5B1A0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/NailShotgun/ID_NailShotgun_DamageIncreaseRangeDecrease.ID_NailShotgun_DamageIncreaseRangeDecrease_C [n: ED979] [c: 00007FF46EEC3D38] [or: 000001F8D8A60D80] [sps: 00007FF46F067338]
[000001F8D8A5AE10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/NailShotgun/ID_NailShotgun_DecreaseSpreadAngle.ID_NailShotgun_DecreaseSpreadAngle_C [n: A79DC] [c: 00007FF46EEC3D38] [or: 000001F8D8A60F10] [sps: 00007FF46F067338]
[000001F8D8A5AA80] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/NailShotgun/ID_NailShotgun_IncreaseSpreadAngle.ID_NailShotgun_IncreaseSpreadAngle_C [n: 16A835] [c: 00007FF46EEC3D38] [or: 000001F8D8A60B50] [sps: 00007FF46F067338]
[000001F8D8A59520] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/ParasiteGun/ID_ParasiteGun_Contagtion_Curse.ID_ParasiteGun_Contagtion_Curse_C [n: 2AB6FC] [c: 00007FF46EEC3D38] [or: 000001F8D8A61C80] [sps: 00007FF46F067338]
[000001F8D8A59190] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/ParasiteGun/ID_ParasiteGun_Contagtion_Poison.ID_ParasiteGun_Contagtion_Poison_C [n: 450A8E] [c: 00007FF46EEC3D38] [or: 000001F8D8A61B40] [sps: 00007FF46F067338]
[000001F8D8A58E00] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/ParasiteGun/ID_ParasiteGun_IncreasedShootingRate.ID_ParasiteGun_IncreasedShootingRate_C [n: 2A2B92] [c: 00007FF46EEC3D38] [or: 000001F8D8A61960] [sps: 00007FF46F067338]
[000001F8D8A58A70] BlueprintGeneratedClass /Game/Sparta/Core/Player/Upgrades/Weapons/Trebuchaxe/ID_Trebuchaxe_Overcharge.ID_Trebuchaxe_Overcharge_C [n: 28D7C1] [c: 00007FF46EEC3D38] [or: 000001F8D8A617D0] [sps: 00007FF46F067338]
[000001F8D90E66D0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_CriticalFlow.ID_Melee_CriticalFlow_C [n: 36AAE8] [c: 00007FF46EEC3D38] [or: 000001F8D8E67E50] [sps: 00007FF46F067338]
[000001F8D90E6340] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_DualWieldProficiency.ID_Melee_DualWieldProficiency_C [n: 34F75D] [c: 00007FF46EEC3D38] [or: 000001F8D8E67D60] [sps: 00007FF46F067338]
[000001F8D90E5FB0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_GrantLeechOnKill.ID_Melee_GrantLeechOnKill_C [n: 3296E0] [c: 00007FF46EEC3D38] [or: 000001F8D8E67C20] [sps: 00007FF46F067338]
[000001F8D90E5C20] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_GrantWarpOnHit.ID_Melee_GrantWarpOnHit_C [n: 33E9FD] [c: 00007FF46EEC3D38] [or: 000001F8D8E68F80] [sps: 00007FF46F067338]
[000001F8D90E5890] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_HeavyAttackFinisher_Critical.ID_Melee_HeavyAttackFinisher_Critical_C [n: 23EF07] [c: 00007FF46EEC3D38] [or: 000001F8D8E68E40] [sps: 00007FF46F067338]
[000001F8D90E5500] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_HeavyAttackFinisher_Weak.ID_Melee_HeavyAttackFinisher_Weak_C [n: 2BD28E] [c: 00007FF46EEC3D38] [or: 000001F8D8E68D00] [sps: 00007FF46F067338]
[000001F8D90E5170] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_HeavyHoldAttack.ID_Melee_HeavyHoldAttack_C [n: ED990] [c: 00007FF46EEC3D38] [or: 000001F8D8E68BC0] [sps: 00007FF46F067338]
[000001F8D90E4DE0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictBleed.ID_Melee_InflictBleed_C [n: 21809E] [c: 00007FF46EEC3D38] [or: 000001F8D8E68A80] [sps: 00007FF46F067338]
[000001F8D90E4A50] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictBurn.ID_Melee_InflictBurn_C [n: 35418E] [c: 00007FF46EEC3D38] [or: 000001F8D8E68990] [sps: 00007FF46F067338]
[000001F8D90E46C0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictCurse.ID_Melee_InflictCurse_C [n: 13FF11] [c: 00007FF46EEC3D38] [or: 000001F8D8E688A0] [sps: 00007FF46F067338]
[000001F8D90E4330] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictFrost.ID_Melee_InflictFrost_C [n: 2BD2A1] [c: 00007FF46EEC3D38] [or: 000001F8D8E68710] [sps: 00007FF46F067338]
[000001F76BD04A50] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictLightning.ID_Melee_InflictLightning_C [n: 4743F8] [c: 00007FF46EEC3D38] [or: 000001F7F77B3BC0] [sps: 00007FF46F067338]
[000001F8D90E3FA0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictPhantom.ID_Melee_InflictPhantom_C [n: 377862] [c: 00007FF46EEC3D38] [or: 000001F8D8E68620] [sps: 00007FF46F067338]
[000001F8B61CBFE0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictPoison.ID_Melee_InflictPoison_C [n: 9F39D] [c: 00007FF46EEC3D38] [or: 000001F8BBEB7810] [sps: 00007FF46F067338]
[000001F8D90E3C10] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictStasis.ID_Melee_InflictStasis_C [n: 46FDA0] [c: 00007FF46EEC3D38] [or: 000001F8D8E699D0] [sps: 00007FF46F067338]
[000001F8D90E3880] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_InflictTrauma.ID_Melee_InflictTrauma_C [n: 1BD0C8] [c: 00007FF46EEC3D38] [or: 000001F8D8E698E0] [sps: 00007FF46F067338]
[000001F8D90E34F0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_LightAttackFinisher_Break.ID_Melee_LightAttackFinisher_Break_C [n: 2BD2BA] [c: 00007FF46EEC3D38] [or: 000001F8D8E697F0] [sps: 00007FF46F067338]
[000001F8D90E3160] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_LightAttackFinisher_Resolve.ID_Melee_LightAttackFinisher_Resolve_C [n: 6EAFB] [c: 00007FF46EEC3D38] [or: 000001F8D8E696B0] [sps: 00007FF46F067338]
[000001F8D90E2DD0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_LightHoldAttack.ID_Melee_LightHoldAttack_C [n: 2F99FE] [c: 00007FF46EEC3D38] [or: 000001F8D8E69570] [sps: 00007FF46F067338]
[000001F8D90E2A40] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_Unyielding.ID_Melee_Unyielding_C [n: 41C1CD] [c: 00007FF46EEC3D38] [or: 000001F8D8E69430] [sps: 00007FF46F067338]
[000001F8D90E26B0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_ClockworkChainsaw.ID_Melee_WeaponAbility_ClockworkChainsaw_C [n: 4AC812] [c: 00007FF46EEC3D38] [or: 000001F8D8E692F0] [sps: 00007FF46F067338]
[000001F8D90E2320] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_ClockworkGrinder.ID_Melee_WeaponAbility_ClockworkGrinder_C [n: 11D235] [c: 00007FF46EEC3D38] [or: 000001F8D8E69200] [sps: 00007FF46F067338]
[000001F8D90E1F90] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_DeadlyFlurry.ID_Melee_WeaponAbility_DeadlyFlurry_C [n: A7B23] [c: 00007FF46EEC3D38] [or: 000001F8D8E69110] [sps: 00007FF46F067338]
[000001F8D90E1C00] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_HeavyStomps.ID_Melee_WeaponAbility_HeavyStomps_C [n: 1261C3] [c: 00007FF46EEC3D38] [or: 000001F8D8E6A4C0] [sps: 00007FF46F067338]
[000001F8D90E1870] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_HomingThrow.ID_Melee_WeaponAbility_HomingThrow_C [n: 14415C] [c: 00007FF46EEC3D38] [or: 000001F8D8E6A3D0] [sps: 00007FF46F067338]
[000001F8D90E14E0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_Katanas.ID_Melee_WeaponAbility_Katanas_C [n: 14416F] [c: 00007FF46EEC3D38] [or: 000001F8D8E6A290] [sps: 00007FF46F067338]
[000001F8D90E1150] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_PlummetStrike.ID_Melee_WeaponAbility_PlummetStrike_C [n: 295D0B] [c: 00007FF46EEC3D38] [or: 000001F8D8E6A100] [sps: 00007FF46F067338]
[000001F8D90E0DC0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_SpiralSurge.ID_Melee_WeaponAbility_SpiralSurge_C [n: 4A3E91] [c: 00007FF46EEC3D38] [or: 000001F8D8E69FC0] [sps: 00007FF46F067338]
[000001F8D90E0A30] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_StormStrike.ID_Melee_WeaponAbility_StormStrike_C [n: 73302] [c: 00007FF46EEC3D38] [or: 000001F8D8E69ED0] [sps: 00007FF46F067338]
[000001F8D90E06A0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Melee/ID_Melee_WeaponAbility_WeaponThrow.ID_Melee_WeaponAbility_WeaponThrow_C [n: 1B8EF9] [c: 00007FF46EEC3D38] [or: 000001F8D8E69DE0] [sps: 00007FF46F067338]
[000001F8DB535C20] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_AllySummon.ID_Sidearm_AllySummon_C [n: 114ABD] [c: 00007FF46EEC3D38] [or: 000001F8CF353760] [sps: 00007FF46F067338]
[000001F8DB535890] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_AttractionShot.ID_Sidearm_AttractionShot_C [n: C6C62] [c: 00007FF46EEC3D38] [or: 000001F8CF353670] [sps: 00007FF46F067338]
[000001F8DB535500] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Bombardment.ID_Sidearm_Bombardment_C [n: 172F13] [c: 00007FF46EEC3D38] [or: 000001F8CF353530] [sps: 00007FF46F067338]
[000001F8B61CB530] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Boombastic.ID_Sidearm_Boombastic_C [n: 3BE5D2] [c: 00007FF46EEC3D38] [or: 000001F8BBEB7220] [sps: 00007FF46F067338]
[000001F8DB535170] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Chainburst.ID_Sidearm_Chainburst_C [n: 1D6A14] [c: 00007FF46EEC3D38] [or: 000001F8CF3533F0] [sps: 00007FF46F067338]
[000001F8DB534DE0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_ClusterBomb.ID_Sidearm_ClusterBomb_C [n: 150DD4] [c: 00007FF46EEC3D38] [or: 000001F8CF354700] [sps: 00007FF46F067338]
[000001F8DB534A50] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_ConcurrentCrossfire.ID_Sidearm_ConcurrentCrossfire_C [n: 89438] [c: 00007FF46EEC3D38] [or: 000001F8CF354610] [sps: 00007FF46F067338]
[000001F8DB5346C0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Corrosion.ID_Sidearm_Corrosion_C [n: 336366] [c: 00007FF46EEC3D38] [or: 000001F8CF3544D0] [sps: 00007FF46F067338]
[000001F8DB534330] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Deadshot.ID_Sidearm_Deadshot_C [n: 18896A] [c: 00007FF46EEC3D38] [or: 000001F8CF354390] [sps: 00007FF46F067338]
[000001F8DB533FA0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_ExplosiveReaction.ID_Sidearm_ExplosiveReaction_C [n: 361BEE] [c: 00007FF46EEC3D38] [or: 000001F8CF3542A0] [sps: 00007FF46F067338]
[000001F7A75A26B0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictBleed.ID_Sidearm_InflictBleed_C [n: FA15F] [c: 00007FF46EEC3D38] [or: 000001F7A7124F20] [sps: 00007FF46F067338]
[000001F8DB533C10] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictBreakOnCriticalHit.ID_Sidearm_InflictBreakOnCriticalHit_C [n: 49B1E8] [c: 00007FF46EEC3D38] [or: 000001F8CF354160] [sps: 00007FF46F067338]
[000001F8DB533880] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictBurn.ID_Sidearm_InflictBurn_C [n: 3DCD2B] [c: 00007FF46EEC3D38] [or: 000001F8CF354070] [sps: 00007FF46F067338]
[000001F8DB5334F0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictCurse.ID_Sidearm_InflictCurse_C [n: 1CE07D] [c: 00007FF46EEC3D38] [or: 000001F8CF353EE0] [sps: 00007FF46F067338]
[000001F8DB533160] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictFrost.ID_Sidearm_InflictFrost_C [n: 395EEF] [c: 00007FF46EEC3D38] [or: 000001F8CF355240] [sps: 00007FF46F067338]
[000001F7459A5500] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictLightning.ID_Sidearm_InflictLightning_C [n: 26A492] [c: 00007FF46EEC3D38] [or: 000001F807DB8CB0] [sps: 00007FF46F067338]
[000001F8DB532DD0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictPhantom.ID_Sidearm_InflictPhantom_C [n: 3B9E2C] [c: 00007FF46EEC3D38] [or: 000001F8CF355100] [sps: 00007FF46F067338]
[000001F8DB532A40] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictPoison.ID_Sidearm_InflictPoison_C [n: F1EBA] [c: 00007FF46EEC3D38] [or: 000001F8CF355010] [sps: 00007FF46F067338]
[000001F8DB5326B0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_InflictTrauma.ID_Sidearm_InflictTrauma_C [n: 3D86F1] [c: 00007FF46EEC3D38] [or: 000001F8CF354ED0] [sps: 00007FF46F067338]
[000001F8DB532320] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_LuteRiff.ID_Sidearm_LuteRiff_C [n: 3F3BA5] [c: 00007FF46EEC3D38] [or: 000001F8CF354D90] [sps: 00007FF46F067338]
[000001F8DB531F90] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_PiercingShot.ID_Sidearm_PiercingShot_C [n: C2450] [c: 00007FF46EEC3D38] [or: 000001F8CF354C50] [sps: 00007FF46F067338]
[000001F7F1C77510] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_QuickShooting.ID_Sidearm_QuickShooting_C [n: 20F8AE] [c: 00007FF46EEC3D38] [or: 000001F80A6E9750] [sps: 00007FF46F067338]
[000001F8DB531C00] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Quickcharge.ID_Sidearm_Quickcharge_C [n: 41C1F2] [c: 00007FF46EEC3D38] [or: 000001F8CF354B10] [sps: 00007FF46F067338]
[000001F8DB531870] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_RepulsiveShot.ID_Sidearm_RepulsiveShot_C [n: 2CE35F] [c: 00007FF46EEC3D38] [or: 000001F8CF3549D0] [sps: 00007FF46F067338]
[000001F8DB5314E0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Scattershot.ID_Sidearm_Scattershot_C [n: 23EF62] [c: 00007FF46EEC3D38] [or: 000001F8CF354890] [sps: 00007FF46F067338]
[000001F81A29DFF0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Slingshot.ID_Sidearm_Slingshot_C [n: 9660F] [c: 00007FF46EEC3D38] [or: 000001F81A28B820] [sps: 00007FF46F067338]
[000001F8DB531150] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Spiraling.ID_Sidearm_Spiraling_C [n: 32970C] [c: 00007FF46EEC3D38] [or: 000001F8CF355BF0] [sps: 00007FF46F067338]
[000001F8DB530DC0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_StickyBomb.ID_Sidearm_StickyBomb_C [n: 4A86B8] [c: 00007FF46EEC3D38] [or: 000001F8CF355AB0] [sps: 00007FF46F067338]
[000001F8DB530A30] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_ThreadweaverShot.ID_Sidearm_ThreadweaverShot_C [n: 17B781] [c: 00007FF46EEC3D38] [or: 000001F8CF355970] [sps: 00007FF46F067338]
[000001F8393B5FB0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Sidearm/ID_Sidearm_Tiltshot.ID_Sidearm_Tiltshot_C [n: 3778C8] [c: 00007FF46EEC3D38] [or: 000001F831067DB0] [sps: 00007FF46F067338]
[000001F8DB539C40] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Support/ID_Support_DamageReduction.ID_Support_DamageReduction_C [n: 261B4C] [c: 00007FF46EEC3D38] [or: 000001F8CF3560A0] [sps: 00007FF46F067338]
[000001F8DB5398B0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Support/ID_Support_GloomBoost.ID_Support_GloomBoost_C [n: 89449] [c: 00007FF46EEC3D38] [or: 000001F8CF355FB0] [sps: 00007FF46F067338]
[000001F8DB539520] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Support/ID_Support_GoldBoost.ID_Support_GoldBoost_C [n: 80423] [c: 00007FF46EEC3D38] [or: 000001F8CF355EC0] [sps: 00007FF46F067338]
[000001F8DB539190] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Support/ID_Support_OnKill_DamageReduction.ID_Support_OnKill_DamageReduction_C [n: 1076D4] [c: 00007FF46EEC3D38] [or: 000001F8CF355DD0] [sps: 00007FF46F067338]
[000001F8DB538E00] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Support/ID_Support_OnKill_PoiseDamage.ID_Support_OnKill_PoiseDamage_C [n: 22D4A4] [c: 00007FF46EEC3D38] [or: 000001F8CF355CE0] [sps: 00007FF46F067338]
[000001F8DB538A70] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Support/ID_Support_OnSidearmKill_GainResolve.ID_Support_OnSidearmKill_GainResolve_C [n: 485693] [c: 00007FF46EEC3D38] [or: 000001F8CF357040] [sps: 00007FF46F067338]
[000001F8DB5386E0] BlueprintGeneratedClass /Game/Sparta/Core/Tarstones/Support/ID_Support_RiposteDamage.ID_Support_RiposteDamage_C [n: 358C9E] [c: 00007FF46EEC3D38] [or: 000001F8CF356F00] [sps: 00007FF46F067338]
[000001F97576CA90] BlueprintGeneratedClass /Game/Sparta/Items/Charges/ID_Charges_Channeling.ID_Charges_Channeling_C [n: 3157E3] [c: 00007FF46EEC3D38] [or: 000001F9755BB690] [sps: 000001F61A4E9FD0]
[000001F97576C700] BlueprintGeneratedClass /Game/Sparta/Items/Charges/ID_Charges_Cleansing.ID_Charges_Cleansing_C [n: 49C86B] [c: 00007FF46EEC3D38] [or: 000001F9755BB550] [sps: 000001F61A4E9FD0]
[000001F61A5B4A50] BlueprintGeneratedClass /Game/Sparta/Items/Charges/ID_Charges_Default.ID_Charges_Default_C [n: 4BF6CC] [c: 00007FF46EEC3D38] [or: 000001F4C5866FA0] [sps: 000001F61A4E9FD0]
[000001F97576C370] BlueprintGeneratedClass /Game/Sparta/Items/Charges/ID_Charges_Gragu.ID_Charges_Gragu_C [n: 3F08F1] [c: 00007FF46EEC3D38] [or: 000001F9755BB410] [sps: 000001F61A4E9FD0]
[000001F4BE035500] BlueprintGeneratedClass /Game/Sparta/Items/Charges/ID_Charges_LoadFromSave.ID_Charges_LoadFromSave_C [n: 2B5C9C] [c: 00007FF46EEC3D38] [or: 000001F4D3011A00] [sps: 00007FF46F067338]
[000001F97576BFE0] BlueprintGeneratedClass /Game/Sparta/Items/Charges/ID_Charges_Mixed.ID_Charges_Mixed_C [n: 1B1375] [c: 00007FF46EEC3D38] [or: 000001F9755BB320] [sps: 000001F61A4E9FD0]
[000001F61A4E9FD0] BlueprintGeneratedClass /Game/Sparta/Items/Charges/Pickups/ID_ChargesPickupBase.ID_ChargesPickupBase_C [n: 3748A2] [c: 00007FF46EEC3D38] [or: 000001F4C5866F50] [sps: 000001F4C0649190]
[000001F4C91A4330] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPieceBase.ID_MapPieceBase_C [n: 6BE70] [c: 00007FF46EEC3D38] [or: 000001F4C46502E0] [sps: 000001F4B021F550]
[000001F4C91A3FA0] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_City_1.ID_MapPiece_City_1_C [n: 2FFF65] [c: 00007FF46EEC3D38] [or: 000001F4C4650240] [sps: 000001F4C91A4330]
[000001F4C91A26B0] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_City_2.ID_MapPiece_City_2_C [n: 8A73B] [c: 00007FF46EEC3D38] [or: 000001F4C4651410] [sps: 000001F4C91A4330]
[000001F4C91A1C00] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_City_3.ID_MapPiece_City_3_C [n: 2B5CAA] [c: 00007FF46EEC3D38] [or: 000001F4C4651320] [sps: 000001F4C91A4330]
[000001F4C91A3880] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_City_4.ID_MapPiece_City_4_C [n: 45F3E1] [c: 00007FF46EEC3D38] [or: 000001F4C4650100] [sps: 000001F4C91A4330]
[000001F4C91A34F0] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_City_5.ID_MapPiece_City_5_C [n: 1DC8B1] [c: 00007FF46EEC3D38] [or: 000001F4C46500B0] [sps: 000001F4C91A4330]
[000001F4C91A1F90] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_City_6.ID_MapPiece_City_6_C [n: 123146] [c: 00007FF46EEC3D38] [or: 000001F4C4651370] [sps: 000001F4C91A4330]
[000001F4C91A2320] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_Forest_1.ID_MapPiece_Forest_1_C [n: 192EB9] [c: 00007FF46EEC3D38] [or: 000001F4C46513C0] [sps: 000001F4C91A4330]
[000001F4C91A2DD0] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_Forest_2.ID_MapPiece_Forest_2_C [n: 31580E] [c: 00007FF46EEC3D38] [or: 000001F4C4650010] [sps: 000001F4C91A4330]
[000001F4C91A3C10] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_Forest_3.ID_MapPiece_Forest_3_C [n: 42220A] [c: 00007FF46EEC3D38] [or: 000001F4C4650150] [sps: 000001F4C91A4330]
[000001F4C91A2A40] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_Forest_4.ID_MapPiece_Forest_4_C [n: 2C7177] [c: 00007FF46EEC3D38] [or: 000001F4C4651460] [sps: 000001F4C91A4330]
[000001F4C91A3160] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapPiece_Forest_5.ID_MapPiece_Forest_5_C [n: E6380] [c: 00007FF46EEC3D38] [or: 000001F4C4650060] [sps: 000001F4C91A4330]
[000001F4DCBA9FD0] BlueprintGeneratedClass /Game/Sparta/Items/Map/ID_MapUnlock.ID_MapUnlock_C [n: 2193D8] [c: 00007FF46EEC3D38] [or: 000001F4C46511E0] [sps: 000001F4C91A4330]
[000001F9758266D0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Core/ID_AM_01_Futilium.ID_AM_01_Futilium_C [n: 178870] [c: 00007FF46EEC3D38] [or: 000001F9755BDA30] [sps: 000001F4B021F550]
[000001F4C0649190] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Core/ID_EquipmentPickupBase.ID_EquipmentPickupBase_C [n: 407495] [c: 00007FF46EEC3D38] [or: 000001F4C5865DD0] [sps: 000001F4B021F550]
[000001F4B0217C30] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Core/ID_Ovum.ID_Ovum_C [n: 1F2797] [c: 00007FF46EEC3D38] [or: 000001F4A2D5A4C0] [sps: 000001F4B021F550]
[000001F975827FC0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_AlienIdol.ID_AlienIdol_C [n: 2561FA] [c: 00007FF46EEC3D38] [or: 000001F9758D0330] [sps: 000001F4B021F550]
[000001F8ABC586E0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Ancient_Artifact.ID_Ancient_Artifact_C [n: 493CEB] [c: 00007FF46EEC3D38] [or: 000001F8B6A22270] [sps: 000001F4B021F550]
[000001F4AB6D14E0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Artifact_FinalBoss.ID_Artifact_FinalBoss_C [n: 2A41C2] [c: 00007FF46EEC3D38] [or: 000001F4AB596AF0] [sps: 000001F4B021F550]
[000001F90595E710] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Artifact_MarshPearl.ID_Artifact_MarshPearl_C [n: 192EC5] [c: 00007FF46EEC3D38] [or: 000001F915A6F2E0] [sps: 000001F4B021F550]
[000001F4AD6406A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Artifact_Monolith.ID_Artifact_Monolith_C [n: 22157C] [c: 00007FF46EEC3D38] [or: 000001F4B5E69CF0] [sps: 000001F4B021F550]
[000001F4B8D5CA90] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Artifact_Offspring.ID_Artifact_Offspring_C [n: 1BE62E] [c: 00007FF46EEC3D38] [or: 000001F4B6CC4430] [sps: 000001F4B021F550]
[000001F4BBD0DC60] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Artifact_ParasiteGolem.ID_Artifact_ParasiteGolem_C [n: 30D431] [c: 00007FF46EEC3D38] [or: 000001F4BBD63990] [sps: 000001F4B021F550]
[000001F975827C30] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Artifact_TwinSesters.ID_Artifact_TwinSesters_C [n: 28EDB3] [c: 00007FF46EEC3D38] [or: 000001F9758D0240] [sps: 000001F4B021F550]
[000001F8AECDCA90] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Attachable_Heart.ID_Attachable_Heart_C [n: D4C78] [c: 00007FF46EEC3D38] [or: 000001F8921E7EF0] [sps: 000001F4B021F550]
[000001F91BF14A50] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_BagheadMoonshine.ID_BagheadMoonshine_C [n: 385C96] [c: 00007FF46EEC3D38] [or: 000001F9134B7090] [sps: 000001F4B021F550]
[000001F8BD6A6DF0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_BlackMarrowKey.ID_BlackMarrowKey_C [n: 3BB315] [c: 00007FF46EEC3D38] [or: 000001F8BBEBA4C0] [sps: 000001F4B021F550]
[000001F4B40298B0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_CaterpillarSibling.ID_CaterpillarSibling_C [n: 4387AF] [c: 00007FF46EEC3D38] [or: 000001F4B4111FA0] [sps: 000001F4B021F550]
[000001F8C883B8C0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Common_Old_Artifact.ID_Common_Old_Artifact_C [n: 308DA4] [c: 00007FF46EEC3D38] [or: 000001F8C88B19B0] [sps: 000001F4B021F550]
[000001F9758278A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_CageKey.ID_Dungeon_CageKey_C [n: C4004] [c: 00007FF46EEC3D38] [or: 000001F9758D0150] [sps: 000001F4B021F550]
[000001F97582FC70] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_EndSliceKey_01.ID_Dungeon_EndSliceKey_01_C [n: 145697] [c: 00007FF46EEC3D38] [or: 000001F9758D0100] [sps: 000001F4B021F550]
[000001F97582F8E0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_EndSliceKey_02.ID_Dungeon_EndSliceKey_02_C [n: 21D4A4] [c: 00007FF46EEC3D38] [or: 000001F9758D00B0] [sps: 000001F4B021F550]
[000001F97582F550] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_EndSliceKey_03.ID_Dungeon_EndSliceKey_03_C [n: 45B14A] [c: 00007FF46EEC3D38] [or: 000001F9758D0060] [sps: 000001F4B021F550]
[000001F97582F1C0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_FP_Key.ID_Dungeon_FP_Key_C [n: 2D82E5] [c: 00007FF46EEC3D38] [or: 000001F9758D0010] [sps: 000001F4B021F550]
[000001F97582EE30] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_FP_Key_Book_1.ID_Dungeon_FP_Key_Book_1_C [n: 4BF6E9] [c: 00007FF46EEC3D38] [or: 000001F9758D1460] [sps: 000001F4B021F550]
[000001F9758F7510] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_FP_Key_Book_2.ID_Dungeon_FP_Key_Book_2_C [n: 1BA644] [c: 00007FF46EEC3D38] [or: 000001F9758D1410] [sps: 000001F4B021F550]
[000001F9758F7180] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_FP_Key_Book_3.ID_Dungeon_FP_Key_Book_3_C [n: 1E5578] [c: 00007FF46EEC3D38] [or: 000001F9758D13C0] [sps: 000001F4B021F550]
[000001F9758F6DF0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_FP_Key_FinalDoor.ID_Dungeon_FP_Key_FinalDoor_C [n: 3B6A1A] [c: 00007FF46EEC3D38] [or: 000001F9758D1370] [sps: 000001F4B021F550]
[000001F9758F6A60] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_FP_MushroomSoup.ID_Dungeon_FP_MushroomSoup_C [n: 422216] [c: 00007FF46EEC3D38] [or: 000001F9758D01A0] [sps: 000001F4B021F550]
[000001F9758F66D0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_LostEquipmentChest.ID_Dungeon_LostEquipmentChest_C [n: 4387BC] [c: 00007FF46EEC3D38] [or: 000001F9758D12D0] [sps: 000001F4B021F550]
[000001F9758F6340] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_SunkenKey.ID_Dungeon_SunkenKey_C [n: 340096] [c: 00007FF46EEC3D38] [or: 000001F9758D1280] [sps: 000001F4B021F550]
[000001F8B9055500] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_VillageKey.ID_Dungeon_VillageKey_C [n: 40B7C3] [c: 00007FF46EEC3D38] [or: 000001F8B894C360] [sps: 000001F4B021F550]
[000001F9758F5FB0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Dungeon_WellKey.ID_Dungeon_WellKey_C [n: 24064E] [c: 00007FF46EEC3D38] [or: 000001F9758D1230] [sps: 000001F4B021F550]
[000001F9758F5C20] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_EbonflarGlass.ID_EbonflarGlass_C [n: 180F61] [c: 00007FF46EEC3D38] [or: 000001F9758D1320] [sps: 000001F4B021F550]
[000001F807E178A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_ElectricConduit.ID_ElectricConduit_C [n: 422225] [c: 00007FF46EEC3D38] [or: 000001F807DB8D50] [sps: 000001F4B021F550]
[000001F8ACE8B1A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_EndlessCore.ID_EndlessCore_C [n: 15AC25] [c: 00007FF46EEC3D38] [or: 000001F8ACEE9E30] [sps: 000001F4B021F550]
[000001F90595DC60] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Epic_Old_Artifact.ID_Epic_Old_Artifact_C [n: 392B76] [c: 00007FF46EEC3D38] [or: 000001F91CA92590] [sps: 000001F4B021F550]
[000001F61A18CE20] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_FastTravel.ID_FastTravel_C [n: 15AC2E] [c: 00007FF46EEC3D38] [or: 000001F4C5865CE0] [sps: 000001F4B021F550]
[000001F4C2853880] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Glimpses.ID_Glimpses_C [n: 363362] [c: 00007FF46EEC3D38] [or: 000001F4C586DC60] [sps: 000001F4B021F550]
[000001F4C131BC50] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Gloom.ID_Gloom_C [n: 397732] [c: 00007FF46EEC3D38] [or: 000001F4A2D5A420] [sps: 000001F4B021F550]
[000001F8C24C8E00] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_GloomGemstone.ID_GloomGemstone_C [n: 32F091] [c: 00007FF46EEC3D38] [or: 000001F8C1E2C130] [sps: 000001F4B021F550]
[000001F8C24C8350] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Gloom_Resin.ID_Gloom_Resin_C [n: 2970EA] [c: 00007FF46EEC3D38] [or: 000001F8C1E2BA50] [sps: 000001F4B021F550]
[000001F4C131B1A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Gold.ID_Gold_C [n: 192ED2] [c: 00007FF46EEC3D38] [or: 000001F4A2D5D6C0] [sps: 000001F4B021F550]
[000001F9758F5890] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_GraveyardCryptKey.ID_GraveyardCryptKey_C [n: ADC8F] [c: 00007FF46EEC3D38] [or: 000001F9758D1190] [sps: 000001F4B021F550]
[000001F9758F5500] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_GrimoriumObscurum.ID_GrimoriumObscurum_C [n: 244A78] [c: 00007FF46EEC3D38] [or: 000001F9758D1140] [sps: 000001F4B021F550]
[000001F8DE1746C0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_GrishaCageKey.ID_GrishaCageKey_C [n: 1976CF] [c: 00007FF46EEC3D38] [or: 000001F8D376BE10] [sps: 000001F4B021F550]
[000001F9758F5170] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_IceCrown.ID_IceCrown_C [n: 392B82] [c: 00007FF46EEC3D38] [or: 000001F9758D10F0] [sps: 000001F4B021F550]
[000001F9758F4DE0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_KS_EredrimDiapason.ID_KS_EredrimDiapason_C [n: 189F57] [c: 00007FF46EEC3D38] [or: 000001F9758D11E0] [sps: 000001F4B021F550]
[000001F8921A98B0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_KS_GenessaIncenseBurner.ID_KS_GenessaIncenseBurner_C [n: 3748BA] [c: 00007FF46EEC3D38] [or: 000001F8921E43E0] [sps: 000001F4B021F550]
[000001F9758F4A50] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_KS_Gragu.ID_KS_Gragu_C [n: 2CB138] [c: 00007FF46EEC3D38] [or: 000001F9758D10A0] [sps: 000001F4B021F550]
[000001F9758F46C0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_KS_KnightLady.ID_KS_KnightLady_C [n: 2E0AE0] [c: 00007FF46EEC3D38] [or: 000001F9758D1050] [sps: 000001F4B021F550]
[000001F8AE84CE20] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_KS_LazlosMask.ID_KS_LazlosMask_C [n: 2A873D] [c: 00007FF46EEC3D38] [or: 000001F893C14C00] [sps: 000001F4B021F550]
[000001F8AE84C700] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_KS_SmertBrassKnuckles.ID_KS_SmertBrassKnuckles_C [n: 41D6DD] [c: 00007FF46EEC3D38] [or: 000001F893C15A10] [sps: 000001F4B021F550]
[000001F9758F4330] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_KS_Thorn.ID_KS_Thorn_C [n: 2E9893] [c: 00007FF46EEC3D38] [or: 000001F9758D1000] [sps: 000001F4B021F550]
[000001F897431870] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_KS_TielsDagger.ID_KS_TielsDagger_C [n: 3D9E14] [c: 00007FF46EEC3D38] [or: 000001F893C117D0] [sps: 000001F4B021F550]
[000001F4BE034DE0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_LadyFirstOffspring.ID_LadyFirstOffspring_C [n: 9365E] [c: 00007FF46EEC3D38] [or: 000001F4BFB6F6A0] [sps: 000001F4B021F550]
[000001F9758F3FA0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Legendary_Old_Artifact.ID_Legendary_Old_Artifact_C [n: 2215A3] [c: 00007FF46EEC3D38] [or: 000001F9758D0F60] [sps: 000001F4B021F550]
[000001F9758F3C10] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_LostScripture.ID_LostScripture_C [n: 29B78D] [c: 00007FF46EEC3D38] [or: 000001F9758D0EC0] [sps: 000001F4B021F550]
[000001F9758F3880] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_SideArm_Ballistazooka.ID_Map_SideArm_Ballistazooka_C [n: 45F415] [c: 00007FF46EEC3D38] [or: 000001F9758D0E70] [sps: 000001F4B021F550]
[000001F8D0A99C40] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_SideArm_Crossbow.ID_Map_SideArm_Crossbow_C [n: 422230] [c: 00007FF46EEC3D38] [or: 000001F8DFD49750] [sps: 000001F4B021F550]
[000001F9758F34F0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_SideArm_CursedChild.ID_Map_SideArm_CursedChild_C [n: 3D120C] [c: 00007FF46EEC3D38] [or: 000001F9758D0D80] [sps: 000001F4B021F550]
[000001F9758F3160] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_SideArm_NeedleSubmachineGun.ID_Map_SideArm_NeedleSubmachineGun_C [n: 116061] [c: 00007FF46EEC3D38] [or: 000001F9758D0C90] [sps: 000001F4B021F550]
[000001F9758F2DD0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_SideArm_TrebuchAxe.ID_Map_SideArm_TrebuchAxe_C [n: 2EDAEF] [c: 00007FF46EEC3D38] [or: 000001F9758D0BA0] [sps: 000001F4B021F550]
[000001F9758F2A40] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_Weapon_Axatana.ID_Map_Weapon_Axatana_C [n: 13445C] [c: 00007FF46EEC3D38] [or: 000001F9758D0AB0] [sps: 000001F4B021F550]
[000001F9758F26B0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_Weapon_AxeDagger.ID_Map_Weapon_AxeDagger_C [n: 180F6B] [c: 00007FF46EEC3D38] [or: 000001F9758D1E60] [sps: 000001F4B021F550]
[000001F9758F2320] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_Weapon_BlackNeedle.ID_Map_Weapon_BlackNeedle_C [n: 791F2] [c: 00007FF46EEC3D38] [or: 000001F9758D1D70] [sps: 000001F4B021F550]
[000001F9758F1F90] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_Weapon_ClockworkScythe.ID_Map_Weapon_ClockworkScythe_C [n: 237538] [c: 00007FF46EEC3D38] [or: 000001F9758D1C80] [sps: 000001F4B021F550]
[000001F9758F1C00] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_Weapon_GreatAxe.ID_Map_Weapon_GreatAxe_C [n: 1A8CF3] [c: 00007FF46EEC3D38] [or: 000001F9758D1B90] [sps: 000001F4B021F550]
[000001F9758F1870] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_Weapon_GreatMartyrSword.ID_Map_Weapon_GreatMartyrSword_C [n: 8A768] [c: 00007FF46EEC3D38] [or: 000001F9758D1AA0] [sps: 000001F4B021F550]
[000001F9758F14E0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Map_Weapon_ObsidianHammer.ID_Map_Weapon_ObsidianHammer_C [n: D4C84] [c: 00007FF46EEC3D38] [or: 000001F9758D19B0] [sps: 000001F4B021F550]
[000001F9758F1150] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_MemoryTiel_TheContract_Key.ID_MemoryTiel_TheContract_Key_C [n: 35E463] [c: 00007FF46EEC3D38] [or: 000001F9758D1870] [sps: 000001F4B021F550]
[000001F8B9EDB1A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_MethersFlesh.ID_MethersFlesh_C [n: 2B5CF4] [c: 00007FF46EEC3D38] [or: 000001F8B9EC3D50] [sps: 000001F4B021F550]
[000001F897431C00] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_MissingCannonBall.ID_MissingCannonBall_C [n: 4268E7] [c: 00007FF46EEC3D38] [or: 000001F893C189E0] [sps: 000001F4B021F550]
[000001F81A130A30] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_MissingLever.ID_MissingLever_C [n: 1E5586] [c: 00007FF46EEC3D38] [or: 000001F81D079700] [sps: 000001F4B021F550]
[000001F9758F0DC0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_MissingLever_DeepRockKingdom.ID_MissingLever_DeepRockKingdom_C [n: 1B60EA] [c: 00007FF46EEC3D38] [or: 000001F9758D1820] [sps: 000001F4B021F550]
[000001F758E3D540] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_MissingLever_DissaraySage.ID_MissingLever_DissaraySage_C [n: 2C2911] [c: 00007FF46EEC3D38] [or: 000001F741179430] [sps: 000001F4B021F550]
[000001F9758F0A30] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_MushroomVillageKey.ID_MushroomVillageKey_C [n: 3C8E84] [c: 00007FF46EEC3D38] [or: 000001F9758D17D0] [sps: 000001F4B021F550]
[000001F8A77BB530] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Mushroom_Weltcap.ID_Mushroom_Weltcap_C [n: 1B60FC] [c: 00007FF46EEC3D38] [or: 000001F8A0B49D90] [sps: 000001F4B021F550]
[000001F4B021F550] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_PickupBase.ID_PickupBase_C [n: 33BCB2] [c: 00007FF46EEC3D38] [or: 000001F4A2D5A3D0] [sps: 00007FF46F067338]
[000001F9758F06A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_QuenchingAcid.ID_QuenchingAcid_C [n: 237549] [c: 00007FF46EEC3D38] [or: 000001F9758D18C0] [sps: 000001F4B021F550]
[000001F91BF12DD0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Rare_Old_Artifact.ID_Rare_Old_Artifact_C [n: 167D05] [c: 00007FF46EEC3D38] [or: 000001F91379AD30] [sps: 000001F4B021F550]
[000001F8C8837180] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_SacredGland_Cathedral.ID_SacredGland_Cathedral_C [n: 4A9C0B] [c: 00007FF46EEC3D38] [or: 000001F8C7F0BF50] [sps: 000001F4B021F550]
[000001F8C8836DF0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_SacredGland_Fire.ID_SacredGland_Fire_C [n: 208971] [c: 00007FF46EEC3D38] [or: 000001F8C7F0BEB0] [sps: 000001F4B021F550]
[000001F8C8837510] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_SacredGland_Snow.ID_SacredGland_Snow_C [n: 348362] [c: 00007FF46EEC3D38] [or: 000001F8C7F0BFF0] [sps: 000001F4B021F550]
[000001F97582B530] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Sigil_Tarnished.ID_Sigil_Tarnished_C [n: 433F3C] [c: 00007FF46EEC3D38] [or: 000001F9755BFB00] [sps: 000001F4B021F550]
[000001F8B7EE86E0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Specimen.ID_Specimen_C [n: 4B691B] [c: 00007FF46EEC3D38] [or: 000001F8B593DD50] [sps: 000001F4B021F550]
[000001F8920BC700] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Steel.ID_Steel_C [n: 292DC9] [c: 00007FF46EEC3D38] [or: 000001F8A0B4A470] [sps: 000001F4B021F550]
[000001F9758F0310] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_SunEssence.ID_SunEssence_C [n: 433F47] [c: 00007FF46EEC3D38] [or: 000001F9758D1780] [sps: 000001F4B021F550]
[000001F8A77BB8C0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Tar_Immaculate.ID_Tar_Immaculate_C [n: 497E03] [c: 00007FF46EEC3D38] [or: 000001F8A0B4A380] [sps: 000001F4B021F550]
[000001F8D384C700] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_ThestusFlame.ID_ThestusFlame_C [n: 15AC4E] [c: 00007FF46EEC3D38] [or: 000001F8D3767FE0] [sps: 000001F4B021F550]
[000001F8C24C78A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_Undersap.ID_Undersap_C [n: 123165] [c: 00007FF46EEC3D38] [or: 000001F8C1E2C9A0] [sps: 000001F4B021F550]
[000001F90595DFF0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_VlasCageKey.ID_VlasCageKey_C [n: 6795D] [c: 00007FF46EEC3D38] [or: 000001F91CA92EA0] [sps: 000001F4B021F550]
[000001F975829C40] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_VratkoFeetPic_01.ID_VratkoFeetPic_01_C [n: 1F27D7] [c: 00007FF46EEC3D38] [or: 000001F9758D0920] [sps: 000001F4B021F550]
[000001F91CC47180] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_VratkoFeetPic_02.ID_VratkoFeetPic_02_C [n: 1E0BBF] [c: 00007FF46EEC3D38] [or: 000001F91CA98350] [sps: 000001F4B021F550]
[000001F91CC46A60] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_VratkoFeetPic_03.ID_VratkoFeetPic_03_C [n: 2E989B] [c: 00007FF46EEC3D38] [or: 000001F91CA98210] [sps: 000001F4B021F550]
[000001F91BF1EE30] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_VratkoFeetPic_04.ID_VratkoFeetPic_04_C [n: 3F08FB] [c: 00007FF46EEC3D38] [or: 000001F91CA98490] [sps: 000001F4B021F550]
[000001F91CC47510] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_VratkoFeetPic_05.ID_VratkoFeetPic_05_C [n: EF0D8] [c: 00007FF46EEC3D38] [or: 000001F91CA983F0] [sps: 000001F4B021F550]
[000001F91CC46DF0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_VratkoFeetPic_06.ID_VratkoFeetPic_06_C [n: 24DB0B] [c: 00007FF46EEC3D38] [or: 000001F91CA982B0] [sps: 000001F4B021F550]
[000001F9758298B0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/ID_WeaponFragment.ID_WeaponFragment_C [n: 3C8E91] [c: 00007FF46EEC3D38] [or: 000001F9758D0970] [sps: 000001F4B021F550]
[000001F4BE034330] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Base.ID_PP_Base_C [n: 225D78] [c: 00007FF46EEC3D38] [or: 000001F4C0A357E0] [sps: 000001F4B021F550]
[000001F8437B7C30] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Bloodseed.ID_PP_Bloodseed_C [n: 244A84] [c: 00007FF46EEC3D38] [or: 000001F843767FE0] [sps: 000001F4BE034330]
[000001F83C3E14E0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Effigy_Burned.ID_PP_Effigy_Burned_C [n: 2861F4] [c: 00007FF46EEC3D38] [or: 000001F8394EF470] [sps: 000001F4BE034330]
[000001F76BAE4330] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Gear_Strange.ID_PP_Gear_Strange_C [n: 44149B] [c: 00007FF46EEC3D38] [or: 000001F4DD94C220] [sps: 000001F4BE034330]
[000001F7FD9598B0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Grisha.ID_PP_Grisha_C [n: 1A8D1A] [c: 00007FF46EEC3D38] [or: 000001F80A6E3030] [sps: 000001F4BE034330]
[000001F6198806A0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_HealingAmount.ID_PP_HealingAmount_C [n: A919D] [c: 00007FF46EEC3D38] [or: 000001F4C586DB70] [sps: 000001F4B021F550]
[000001F619880310] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_HealingCharge.ID_PP_HealingCharge_C [n: C7EFB] [c: 00007FF46EEC3D38] [or: 000001F4C586EA70] [sps: 000001F4B021F550]
[000001F97582C700] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Mango.ID_PP_Mango_C [n: 1E5590] [c: 00007FF46EEC3D38] [or: 000001F9755BEF20] [sps: 000001F4BE034330]
[000001F7F1C31F90] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_MartyrAsh.ID_PP_MartyrAsh_C [n: 3046C0] [c: 00007FF46EEC3D38] [or: 000001F7F719E480] [sps: 000001F4BE034330]
[000001F74659D540] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Moonshine_Inferior.ID_PP_Moonshine_Inferior_C [n: 3C8EBF] [c: 00007FF46EEC3D38] [or: 000001F746698DA0] [sps: 000001F4BE034330]
[000001F614CB3160] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Mushroom_Weltcap.ID_PP_Mushroom_Weltcap_C [n: 41D724] [c: 00007FF46EEC3D38] [or: 000001F4C58674A0] [sps: 000001F4BE034330]
[000001F81042FC70] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_ParadoxicalScripture.ID_PP_ParadoxicalScripture_C [n: 4BF733] [c: 00007FF46EEC3D38] [or: 000001F81ABB0330] [sps: 000001F4BE034330]
[000001F819DA3160] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Sheep.ID_PP_Sheep_C [n: 2A41E7] [c: 00007FF46EEC3D38] [or: 000001F81077AA10] [sps: 000001F4BE034330]
[000001F7DCB7E710] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Tarspore.ID_PP_Tarspore_C [n: F3349] [c: 00007FF46EEC3D38] [or: 000001F7F6DE7F90] [sps: 000001F4BE034330]
[000001F9758FAA80] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_Trollweed.ID_PP_Trollweed_C [n: 3377D5] [c: 00007FF46EEC3D38] [or: 000001F9758D2180] [sps: 000001F4BE034330]
[000001F8C883CA90] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/PermProgression/ID/ID_PP_UniqueScripture.ID_PP_UniqueScripture_C [n: 4268FF] [c: 00007FF46EEC3D38] [or: 000001F8C7F0BD70] [sps: 000001F4BE034330]
[000001F746594330] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/QuestItems/ID_PoisonedDagger.ID_PoisonedDagger_C [n: 3678D0] [c: 00007FF46EEC3D38] [or: 000001F746695CE0] [sps: 000001F4B021F550]
[000001F4C2F4DFF0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tardust/ID_Tarcore.ID_Tarcore_C [n: 43CC88] [c: 00007FF46EEC3D38] [or: 000001F4BFB61F00] [sps: 000001F4C2F4E380]
[000001F4C2F4E380] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tardust/ID_Tardust_Base.ID_Tardust_Base_C [n: 3CD355] [c: 00007FF46EEC3D38] [or: 000001F4BFB63350] [sps: 000001F4B021F550]
[000001F61A5BA6F0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tarforge/ID_TFM_01_Ventrium.ID_TFM_01_Ventrium_C [n: 286200] [c: 00007FF46EEC3D38] [or: 000001F4C586D9E0] [sps: 000001F4B021F550]
[000001F61A4EAA80] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tarforge/ID_TFM_02_Laterite.ID_TFM_02_Laterite_C [n: 14DCEC] [c: 00007FF46EEC3D38] [or: 000001F4C586D8F0] [sps: 000001F4B021F550]
[000001F61A188E00] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tarforge/ID_TFM_03_Dorsalite.ID_TFM_03_Dorsalite_C [n: 11E7E2] [c: 00007FF46EEC3D38] [or: 000001F4C586EBB0] [sps: 000001F4B021F550]
[000001F61A5BAA80] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tarforge/ID_TFM_04_Thoracium.ID_TFM_04_Thoracium_C [n: 1E5598] [c: 00007FF46EEC3D38] [or: 000001F4C586ECF0] [sps: 000001F4B021F550]
[000001F4C131F8E0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tarforge/ID_TFM_05_Thoracium_Prime.ID_TFM_05_Thoracium_Prime_C [n: 2C292F] [c: 00007FF46EEC3D38] [or: 000001F4B1D47900] [sps: 000001F4B021F550]
[000001F8A15CF1C0] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tarforge/ID_Tarforge_ForgeMode_Sidearm.ID_Tarforge_ForgeMode_Sidearm_C [n: 3817F6] [c: 00007FF46EEC3D38] [or: 000001F8ACEE4070] [sps: 000001F4B021F550]
[000001F4C9378350] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tarforge/ID_Tarforge_ForgeMode_Weapon.ID_Tarforge_ForgeMode_Weapon_C [n: 2B5D0B] [c: 00007FF46EEC3D38] [or: 000001F4C41EBDC0] [sps: 000001F4B021F550]
[000001F8A15C8A70] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tarforge/ID_Tarforge_SmeltMode.ID_Tarforge_SmeltMode_C [n: 8EFCC] [c: 00007FF46EEC3D38] [or: 000001F8ACEE3FD0] [sps: 000001F4B021F550]
[000001F8B6CD1150] BlueprintGeneratedClass /Game/Sparta/Items/Pickups/Tarforge/ID_Tarforge_TarstoneMode.ID_Tarforge_TarstoneMode_C [n: 1CB1A1] [c: 00007FF46EEC3D38] [or: 000001F8ACEEE930] [sps: 000001F4B021F550]
[000001F9758F86E0] BlueprintGeneratedClass /Game/Sparta/Items/Seals/ID_Seal_Default.ID_Seal_Default_C [n: 210EE5] [c: 00007FF46EEC3D38] [or: 000001F9758D51F0] [sps: 000001F9758F8A70]
[000001F9758F8350] BlueprintGeneratedClass /Game/Sparta/Items/Seals/ID_Seal_Infinite.ID_Seal_Infinite_C [n: 468606] [c: 00007FF46EEC3D38] [or: 000001F9758D5010] [sps: 000001F9758F8A70]
[000001F4BE035C20] BlueprintGeneratedClass /Game/Sparta/Items/Seals/ID_Seal_LoadFromSave.ID_Seal_LoadFromSave_C [n: 127688] [c: 00007FF46EEC3D38] [or: 000001F4BFB69610] [sps: 00007FF46F067338]
[000001F9758F78A0] BlueprintGeneratedClass /Game/Sparta/Items/Seals/ID_Seal_Slayer.ID_Seal_Slayer_C [n: B1EE6] [c: 00007FF46EEC3D38] [or: 000001F9758D4E80] [sps: 000001F9758F8A70]
[000001F9758FFC70] BlueprintGeneratedClass /Game/Sparta/Items/Seals/ID_Seal_Stone.ID_Seal_Stone_C [n: 97CBC] [c: 00007FF46EEC3D38] [or: 000001F9758D4CA0] [sps: 000001F9758F8A70]
[000001F9758F8A70] BlueprintGeneratedClass /Game/Sparta/Items/Seals/Pickups/ID_SealPickupBase.ID_SealPickupBase_C [n: 3F0907] [c: 00007FF46EEC3D38] [or: 000001F9758D3DF0] [sps: 000001F4C0649190]
[000001F4BE033160] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_DarkForm_LoadFromSave.ID_DarkForm_LoadFromSave_C [n: 237573] [c: 00007FF46EEC3D38] [or: 000001F4C41E6780] [sps: 00007FF46F067338]
[000001F9758FF8E0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Darkform_CorruptedGenessa.ID_Darkform_CorruptedGenessa_C [n: 445396] [c: 00007FF46EEC3D38] [or: 000001F9758D4BB0] [sps: 00007FF46F067338]
[000001F9758FF550] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Darkform_Eggman.ID_Darkform_Eggman_C [n: A0A62] [c: 00007FF46EEC3D38] [or: 000001F9758D49D0] [sps: 00007FF46F067338]
[000001F4EB565C20] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Darkform_StrongOne.ID_Darkform_StrongOne_C [n: C7F19] [c: 00007FF46EEC3D38] [or: 000001F4C5866F00] [sps: 00007FF46F067338]
[000001F9758FF1C0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Darkform_StrongOne_Cosmic.ID_Darkform_StrongOne_Cosmic_C [n: 4A0DD5] [c: 00007FF46EEC3D38] [or: 000001F9758D4840] [sps: 000001F4EB565C20]
[000001F9758FEE30] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Darkform_StrongOne_Obsidian.ID_Darkform_StrongOne_Obsidian_C [n: 4829AC] [c: 00007FF46EEC3D38] [or: 000001F9758D5B50] [sps: 000001F4EB565C20]
[000001F975DC7510] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Darkform_StrongOne_Red.ID_Darkform_StrongOne_Red_C [n: 25EDA6] [c: 00007FF46EEC3D38] [or: 000001F9758D59C0] [sps: 000001F4EB565C20]
[000001F61C35A6F0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Darkform_StrongOne_Robe.ID_Darkform_StrongOne_Robe_C [n: 189FBE] [c: 00007FF46EEC3D38] [or: 000001F4C5865E70] [sps: 00007FF46F067338]
[000001F975DC7180] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Darkform_StrongOne_Skeleton.ID_Darkform_StrongOne_Skeleton_C [n: 1388C9] [c: 00007FF46EEC3D38] [or: 000001F9758D5830] [sps: 000001F4EB565C20]
[000001F975DC6DF0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Darkform_TarredCorpse.ID_Darkform_TarredCorpse_C [n: 392B91] [c: 00007FF46EEC3D38] [or: 000001F9758D4890] [sps: 00007FF46F067338]
[000001F8CB63C700] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Eredrim.ID_Shell_Eredrim_C [n: 36C246] [c: 00007FF46EEC3D38] [or: 000001F8CAEDFF60] [sps: 000001F76D3E4330]
[000001F8CB63AA80] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Genessa.ID_Shell_Genessa_C [n: 204182] [c: 00007FF46EEC3D38] [or: 000001F8CAEDF970] [sps: 000001F76D3E4330]
[000001F8C2C8B8C0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Genessa_Memory.ID_Shell_Genessa_Memory_C [n: 4BAC7E] [c: 00007FF46EEC3D38] [or: 000001F8CEA1BC30] [sps: 000001F8CB63AA80]
[000001F8CEEE9FD0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Gragu.ID_Shell_Gragu_C [n: 385CEF] [c: 00007FF46EEC3D38] [or: 000001F8CF1B8580] [sps: 000001F76D3E4330]
[000001F8CEEE9C40] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Gragu_Helmetless.ID_Shell_Gragu_Helmetless_C [n: 24067C] [c: 00007FF46EEC3D38] [or: 000001F8CF1B7180] [sps: 000001F8CEEE9FD0]
[000001F8CB639C40] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Harros.ID_Shell_Harros_C [n: 464025] [c: 00007FF46EEC3D38] [or: 000001F7ED2709C0] [sps: 00007FF46F067338]
[000001F8C61F7510] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_KnightLady.ID_Shell_KnightLady_C [n: 25A7C3] [c: 00007FF46EEC3D38] [or: 000001F8C62B2770] [sps: 000001F76D3E4330]
[000001F8CFAF6340] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Lazlo_NoMask.ID_Shell_Lazlo_NoMask_C [n: 35E474] [c: 00007FF46EEC3D38] [or: 000001F8CF990240] [sps: 000001F76D3E4330]
[000001F4C2F4E710] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_LoadFromSave.ID_Shell_LoadFromSave_C [n: B1EFD] [c: 00007FF46EEC3D38] [or: 000001F4BFB621D0] [sps: 00007FF46F067338]
[000001F891DFF1C0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Necrophage.ID_Shell_Necrophage_C [n: 48F3FA] [c: 00007FF46EEC3D38] [or: 000001F88CA198E0] [sps: 000001F76D3E4330]
[000001F841D7B8C0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Necrophage_Child.ID_Shell_Necrophage_Child_C [n: 2A8785] [c: 00007FF46EEC3D38] [or: 000001F84C2F6DC0] [sps: 00007FF46F067338]
[000001F8A77BAA80] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Necrophage_YoungAdult.ID_Shell_Necrophage_YoungAdult_C [n: BB3D5] [c: 00007FF46EEC3D38] [or: 000001F8A0B4D620] [sps: 00007FF46F067338]
[000001F7ED2C6DF0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Smert.ID_Shell_Smert_C [n: 8A7A0] [c: 00007FF46EEC3D38] [or: 000001F7ED271EB0] [sps: 000001F76D3E4330]
[000001F975DC6A60] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Solomon.ID_Shell_Solomon_C [n: 16399F] [c: 00007FF46EEC3D38] [or: 000001F9758D5560] [sps: 000001F76D3E4330]
[000001F7ED2C5C20] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Thorn.ID_Shell_Thorn_C [n: 4569C1] [c: 00007FF46EEC3D38] [or: 000001F7ED271C80] [sps: 000001F76D3E4330]
[000001F8D0B75FB0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Thorn_Memory.ID_Shell_Thorn_Memory_C [n: 350E40] [c: 00007FF46EEC3D38] [or: 000001F8C62BDBC0] [sps: 00007FF46F067338]
[000001F76957B8C0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Tiel.ID_Shell_Tiel_C [n: 274723] [c: 00007FF46EEC3D38] [or: 000001F76D3D6AA0] [sps: 000001F76D3E4330]
[000001F8C61F14E0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Tiel_Memory.ID_Shell_Tiel_Memory_C [n: DDA16] [c: 00007FF46EEC3D38] [or: 000001F8D2133DF0] [sps: 00007FF46F067338]
[000001F8D1EFEE30] BlueprintGeneratedClass /Game/Sparta/Items/Shells/ID_Shell_Tiel_WithFace_Memory.ID_Shell_Tiel_WithFace_Memory_C [n: 13CC6F] [c: 00007FF46EEC3D38] [or: 000001F8D2135150] [sps: 000001F8C61F14E0]
[000001F76D3E4330] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Pickups/ID_ShellPickupBase.ID_ShellPickupBase_C [n: 350E4D] [c: 00007FF46EEC3D38] [or: 000001F76D3D1A50] [sps: 000001F4C0649190]
[000001F975DC66D0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Eredrim_AltSkin.ID_Shell_Eredrim_AltSkin_C [n: 1A016C] [c: 00007FF46EEC3D38] [or: 000001F9758D5420] [sps: 000001F8CB63C700]
[000001F975DC6340] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Eredrim_Obsidian.ID_Shell_Eredrim_Obsidian_C [n: 3FE31C] [c: 00007FF46EEC3D38] [or: 000001F9758D66E0] [sps: 000001F8CB63C700]
[000001F975DC5FB0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Genessa_AltSkin.ID_Shell_Genessa_AltSkin_C [n: B6BA5] [c: 00007FF46EEC3D38] [or: 000001F9758D6500] [sps: 000001F8CB63AA80]
[000001F975DC5C20] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Genessa_Obsidian.ID_Shell_Genessa_Obsidian_C [n: 4829BD] [c: 00007FF46EEC3D38] [or: 000001F9758D6320] [sps: 000001F8CB63AA80]
[000001F975DC5890] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Gragu_AltSkin.ID_Shell_Gragu_AltSkin_C [n: 19BAE5] [c: 00007FF46EEC3D38] [or: 000001F9758D6140] [sps: 000001F8CEEE9FD0]
[000001F975DC5500] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Gragu_Obsidian.ID_Shell_Gragu_Obsidian_C [n: D91C8] [c: 00007FF46EEC3D38] [or: 000001F9758D5F60] [sps: 000001F8CEEE9FD0]
[000001F975DC5170] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Harros_Obsidian.ID_Shell_Harros_Obsidian_C [n: 25624F] [c: 00007FF46EEC3D38] [or: 000001F9758D5D80] [sps: 000001F8CB639C40]
[000001F975DC4DE0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_KnightLady_AltSkin.ID_Shell_KnightLady_AltSkin_C [n: 4BF752] [c: 00007FF46EEC3D38] [or: 000001F9758D70E0] [sps: 000001F8C61F7510]
[000001F975DC4A50] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_KnightLady_Obsidian.ID_Shell_KnightLady_Obsidian_C [n: 700F3] [c: 00007FF46EEC3D38] [or: 000001F9758D6F00] [sps: 000001F8C61F7510]
[000001F975DC46C0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Necrophage_AltSkin.ID_Shell_Necrophage_AltSkin_C [n: 210F15] [c: 00007FF46EEC3D38] [or: 000001F9758D6D20] [sps: 000001F891DFF1C0]
[000001F975DC4330] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Necrophage_Obsidian.ID_Shell_Necrophage_Obsidian_C [n: 38E86F] [c: 00007FF46EEC3D38] [or: 000001F9758D6B90] [sps: 000001F891DFF1C0]
[000001F975DC3FA0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Smert_AltSkin.ID_Shell_Smert_AltSkin_C [n: 31588C] [c: 00007FF46EEC3D38] [or: 000001F9758D69B0] [sps: 000001F7ED2C6DF0]
[000001F975DC3C10] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Smert_Obsidian.ID_Shell_Smert_Obsidian_C [n: 180FDF] [c: 00007FF46EEC3D38] [or: 000001F9758D6820] [sps: 000001F7ED2C6DF0]
[000001F975DC3880] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Thorn_AltSkin.ID_Shell_Thorn_AltSkin_C [n: 397763] [c: 00007FF46EEC3D38] [or: 000001F9758D6A00] [sps: 000001F7ED2C5C20]
[000001F975DC34F0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Thorn_Obsidian.ID_Shell_Thorn_Obsidian_C [n: 3D546E] [c: 00007FF46EEC3D38] [or: 000001F9758D79F0] [sps: 000001F7ED2C5C20]
[000001F975DC3160] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Tiel_AltSkin.ID_Shell_Tiel_AltSkin_C [n: 3046CA] [c: 00007FF46EEC3D38] [or: 000001F9758D7860] [sps: 000001F76957B8C0]
[000001F975DC2DD0] BlueprintGeneratedClass /Game/Sparta/Items/Shells/Skins/ID_Shell_Tiel_Obsidian.ID_Shell_Tiel_Obsidian_C [n: 297142] [c: 00007FF46EEC3D38] [or: 000001F9758D76D0] [sps: 000001F76957B8C0]
[000001F975DC1150] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_Ballistazooka.ID_Sidearm_Ballistazooka_C [n: 81B25] [c: 00007FF46EEC3D38] [or: 000001F9758D7D10] [sps: 000001F4C06478A0]
[000001F61FE1FC70] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_Crossbow.ID_Sidearm_Crossbow_C [n: 2C71C7] [c: 00007FF46EEC3D38] [or: 000001F4A8A9CA40] [sps: 000001F4C06478A0]
[000001F975DC0DC0] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_CursedChild.ID_Sidearm_CursedChild_C [n: BF79D] [c: 00007FF46EEC3D38] [or: 000001F9758D7C20] [sps: 000001F4C06478A0]
[000001F49C813880] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_LoadFromSave.ID_Sidearm_LoadFromSave_C [n: 3CD35F] [c: 00007FF46EEC3D38] [or: 000001F4A8DEF510] [sps: 00007FF46F067338]
[000001F975DC0A30] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_Lute_Simple.ID_Sidearm_Lute_Simple_C [n: 44DF17] [c: 00007FF46EEC3D38] [or: 000001F9758D8FD0] [sps: 000001F4C06478A0]
[000001F975DC06A0] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_MachineGun.ID_Sidearm_MachineGun_C [n: 2EDB21] [c: 00007FF46EEC3D38] [or: 000001F9758D8EE0] [sps: 000001F4C06478A0]
[000001F8C65D46C0] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_MachineGun_Memory.ID_Sidearm_MachineGun_Memory_C [n: 4B6938] [c: 00007FF46EEC3D38] [or: 000001F8C62B54C0] [sps: 000001F4C06478A0]
[000001F8CEEEEE30] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_MachineGun_Memory_NoFade.ID_Sidearm_MachineGun_Memory_NoFade_C [n: 31E00E] [c: 00007FF46EEC3D38] [or: 000001F8C62B1B40] [sps: 000001F4C06478A0]
[000001F4C064FC70] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_NailShotgun.ID_Sidearm_NailShotgun_C [n: 36C250] [c: 00007FF46EEC3D38] [or: 000001F4C58674F0] [sps: 000001F4C06478A0]
[000001F975DC0310] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_ParasiteGun.ID_Sidearm_ParasiteGun_C [n: 48F423] [c: 00007FF46EEC3D38] [or: 000001F9758D8DF0] [sps: 000001F4C06478A0]
[000001F8CB63E380] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/ID_Sidearm_Trebuchaxe.ID_Sidearm_Trebuchaxe_C [n: 1A017A] [c: 00007FF46EEC3D38] [or: 000001F8CAEDF1F0] [sps: 000001F4C06478A0]
[000001F4C06478A0] BlueprintGeneratedClass /Game/Sparta/Items/Sidearms/Pickups/ID_SidearmPickupBase.ID_SidearmPickupBase_C [n: 2E98CE] [c: 00007FF46EEC3D38] [or: 000001F4C5866DC0] [sps: 000001F4C0649190]
[000001F975DCEAA0] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_Axatana_Single.ID_Axatana_Single_C [n: 497E4C] [c: 00007FF46EEC3D38] [or: 000001F9758D8D00] [sps: 000001F4C0648E00]
[000001F8C80C0A30] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_AxeDagger.ID_AxeDagger_C [n: 4569CA] [c: 00007FF46EEC3D38] [or: 000001F8CAEDC220] [sps: 000001F4C0648E00]
[000001F76D89D540] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_AxeDagger_Memory.ID_AxeDagger_Memory_C [n: 15F4B7] [c: 00007FF46EEC3D38] [or: 000001F76D3D6870] [sps: 00007FF46F067338]
[000001F4C47F4DE0] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_BattleAxe.ID_BattleAxe_C [n: 449A3D] [c: 00007FF46EEC3D38] [or: 000001F4C5867B80] [sps: 000001F4C0648E00]
[000001F8CEEEFC70] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_BattleAxe_Memory.ID_BattleAxe_Memory_C [n: 26FEA7] [c: 00007FF46EEC3D38] [or: 000001F8CF1B53D0] [sps: 00007FF46F067338]
[000001F8C89AF1C0] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_BlackNeedle.ID_BlackNeedle_C [n: B6BB3] [c: 00007FF46EEC3D38] [or: 000001F8CAEDB5F0] [sps: 000001F4C0648E00]
[000001F8CE8D8350] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_BlackNeedle_Memory.ID_BlackNeedle_Memory_C [n: 7011B] [c: 00007FF46EEC3D38] [or: 000001F8CEA16AA0] [sps: 00007FF46F067338]
[000001F975DCE710] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_ClockworkScythe.ID_ClockworkScythe_C [n: 152179] [c: 00007FF46EEC3D38] [or: 000001F9758D8C10] [sps: 000001F4C0648E00]
[000001F8CFAFF8E0] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_Fists.ID_Fists_C [n: 2F2078] [c: 00007FF46EEC3D38] [or: 000001F8CF99F330] [sps: 00007FF46F067338]
[000001F8CFAF4DE0] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_GreatSword_Memory.ID_GreatSword_Memory_C [n: C7F31] [c: 00007FF46EEC3D38] [or: 000001F8CF995F60] [sps: 00007FF46F067338]
[000001F4C0648A70] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_HadernsSword.ID_HadernsSword_C [n: 1A0187] [c: 00007FF46EEC3D38] [or: 000001F4C5867590] [sps: 000001F4C0648E00]
[000001F975DCE380] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_HallowedSword.ID_HallowedSword_C [n: 1456E1] [c: 00007FF46EEC3D38] [or: 000001F9758D8AD0] [sps: 00007FF46F067338]
[000001F975DCDFF0] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_HeavyHammer.ID_HeavyHammer_C [n: 4BF76A] [c: 00007FF46EEC3D38] [or: 000001F9758D8990] [sps: 000001F4C0648E00]
[000001F916EF0A30] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_IncompleteScythe_Memory.ID_IncompleteScythe_Memory_C [n: 43CCAA] [c: 00007FF46EEC3D38] [or: 000001F913C14930] [sps: 00007FF46F067338]
[000001F8C7C98350] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_MartyrsBlade.ID_MartyrsBlade_C [n: 2C71D3] [c: 00007FF46EEC3D38] [or: 000001F8CAEDCEF0] [sps: 000001F4C0648E00]
[000001F8F54C9FD0] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_MartyrsBlade_Memory.ID_MartyrsBlade_Memory_C [n: 1D8191] [c: 00007FF46EEC3D38] [or: 000001F8F5469070] [sps: 000001F4C0648E00]
[000001F9009DCA90] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_SacrophagusSword_Memory.ID_SacrophagusSword_Memory_C [n: 363390] [c: 00007FF46EEC3D38] [or: 000001F9061F3670] [sps: 00007FF46F067338]
[000001F8CFAF9C40] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_SarcophagusSword_Memory_Stowed_NoFade.ID_SarcophagusSword_Memory_Stowed_NoFade_C [n: 385CF8] [c: 00007FF46EEC3D38] [or: 000001F8CF99EA20] [sps: 000001F4C0648E00]
[000001F4C2F4EAA0] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/ID_Weapon_LoadFromSave.ID_Weapon_LoadFromSave_C [n: 389DB8] [c: 00007FF46EEC3D38] [or: 000001F4BFB62220] [sps: 00007FF46F067338]
[000001F4C0648E00] BlueprintGeneratedClass /Game/Sparta/Items/Weapons/Pickups/ID_WeaponPickupBase.ID_WeaponPickupBase_C [n: 4569D2] [c: 00007FF46EEC3D38] [or: 000001F4C5866780] [sps: 000001F4C0649190]
```
