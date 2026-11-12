# 20 — Quests, story progression, dialogue, NPC state, save data, map/level structure (MS2 co-op research)

Scope: quests / objectives / story progress / dialogue / NPC state / save data / map + level structure for
Mortal Shell II (UE 5.6 shipping build, UE4SS Lua). Read `20-quests-progression-part2.md` for the save-object
field tables (`BPSO_*`), the DataTable/DataAsset inventory, the full map-package list, all travel/teleport/level-load
functions and the console-command list.

## 0. Method, tooling, and what this dump cannot prove

| Item | Value |
|---|---|
| Primary source | `raw\index.sqlite3` (index of `raw\ObjectDump_allassets.txt`, main menu, `LoadAllAssetsBeforeDumpingObjects=1`) |
| Evidence command | `python scripts\parse_dump.py raw raw\index.sqlite3 <regex-on-path>` (output pasted verbatim below) |
| Layout source | `raw\CXXHeaderDump\*.hpp` (member offsets + declaration order) |
| Enumeration queries | `scripts\q\qp-*.sql` (this report's queries) |
| Second dump | `raw\ObjectDump_ingame.txt` (gameplay, 126 MB) — used only to confirm (a) that the gameplay classes really are instantiated in a live world and (b) delegate-signature targets, because it was **not** indexed into SQL |

Rules applied (documented, not guesses):

* **Blueprint compiler temporaries filtered.** Parameters whose names begin `Temp_`, `CallFunc_`, `K2Node_`,
  `__Internal_`, `ExecuteUbergraph_`, `Goto_`, `Bpf`, `eb_` are dropped from every function parameter list below.
  The dump carries them; they are not part of the authored signature. `ReturnValue` is kept (it is the return value).
* **Function parameter order = dump order** = declaration order.
* **Offset values** are `[o: ..]` from the dump (hex), i.e. *offset inside the declared type at that level*:
  for a class member it is the class member offset; for a function parameter it is the offset inside the
  parameter frame (do **not** read those as class offsets).
* **Struct-typed properties** show the struct type the dump resolved (`ss:`), e.g. `StructProperty of /Script/Sparta.QuestStage`.
* **MapProperty key/value types** come from the sibling `..._Key` / `..._Value` rows (or `[kp:]/[vp:]` in the ingame dump),
  e.g. `SaveObjects_Key` is `StructProperty of /Script/CoreUObject.Guid`, value is `ObjectProperty of ...SpartaSaveObject`.

Hard limitations of this data set (stated once, used everywhere as `UNKNOWN`):

| Limitation | What I tried |
|---|---|
| **Blueprint user-defined structs are not object-dumped.** Only their `Package` row exists (verified: `qp-game-structs.sql` returns 281 `/Game/...` `ScriptStruct` rows and *all* of them are `AnimBlueprintGeneratedConstantData/MutableData`). Layouts for those thus come from `raw\CXXHeaderDump\<name>.hpp` instead of the object dump — e.g. `F_NarrativeEvents.hpp`, `F_NarrativeEventFloatTracking.hpp`, `F_AchievementProgress.hpp` are readable there. | `python scripts\q.py raw\index.sqlite3 scripts\q\qp-game-structs.sql`; `parse_dump.py raw ... "FF_NarrativeEvent"` (no rows) |
| **DataTable row struct is not recorded on the DataTable asset row.** A `DataTable` object row carries no `ss:`/`ref`. Row structs are only provable when a *struct* declares `SuperStruct = /Script/Engine.TableRowBase` (see §1.3) or when a Blueprint function's `GetDataTableRowFromName` out-parameter type names it. | `qp-rowstructs2.sql` (join on `obj.super`), `parse_dump.py raw ... "DT_QuestData1"` |
| **`UFUNCTION(exec)` is not marked in the dump.** Console commands can only be inferred from the owning class (a `CheatManager` subclass) — those rows are prefixed `GUESS:` in §14 of part 2. | `parse_dump.py show .../Script/Sparta.SpartaCheatManager` |
| The all-assets dump was taken **at the main menu**, so live gameplay instances (e.g. the quest-log component on the player) do not appear there; `ObjectDump_ingame.txt` was not indexed, so only targeted `Select-String` searches were run against it. | `Select-String -Path raw\ObjectDump_ingame.txt -Pattern "BPC_QuestLogComponent_C"` |

---

## 1. Quest / objective system

### 1.1 Native classes and structs

| Class / Struct | Parent | Key members (name : type @ offset) | Sync relevance |
|---|---|---|---|
| `/Script/Sparta.QuestObject` | `/Script/CoreUObject.Object` | `QuestID: NameProperty @0x28`, `CurrentQuestStage: IntProperty @0x30`, `CurrentQuestObjectiveProgress: MapProperty @0x38`, `CurrentStageDetails: StructProperty of QuestStage @0x88`, `QuestDetails: StructProperty of QuestDetails @0x180` | one live quest instance; stage + per-objective counters are the state to replicate |
| `/Script/Sparta.QuestObjective` | `/Script/CoreUObject.Object` | `ObjectiveData: StructProperty of QuestObjectives @0x28`, `QuestID: NameProperty @0x60`, `OnObjectiveCompleted: MulticastInlineDelegateProperty @0x68` | objective completion event |
| `/Script/Sparta.QuestDetails` | `/Script/Engine.TableRowBase` | `QuestName @0x8`, `LogDescription @0x18`, `TrackingDescription @0x28`, `bIsMainQuest @0x38`, `Stages: ArrayProperty of QuestStage @0x40`, `bAutoAccept @0x50` | **a DataTable row struct** → the quest definition table |
| `/Script/Sparta.QuestStage` | (none) | `StageName @0x0`, `Description @0x10`, `Objectives: ArrayProperty of QuestObjectives @0x20`, `ObjectiveObjects: ArrayProperty of QuestObjective @0x30`, `XPReward @0x40`, `ItemRewards: MapProperty @0x48`, `CurrencyReward @0x98`, `EnemyRewards: MapProperty @0xa0`, `RelationshipReward @0xf0` | stage definition + reward payload |
| `/Script/Sparta.QuestObjectives` | (none) | `ObjectiveName @0x0`, `Description @0x10`, `ObjectiveClass: ClassProperty @0x20`, `ObjectiveID: NameProperty @0x28`, `Quantity: IntProperty @0x30`, `bOptional @0x34`, `bBonusXP @0x35` | objective definition (class is data-driven!) |

Map/array inner types (from the sibling rows):
`QuestObject:CurrentQuestObjectiveProgress` = `TMap<FName, int32>` (`…_Key: NameProperty @0x0`, value `IntProperty @0x8`);
`QuestStage:Objectives` inner = `StructProperty of /Script/Sparta.QuestObjectives`;
`QuestStage:ObjectiveObjects` inner = `ObjectProperty of /Script/Sparta.QuestObjective`;
`QuestStage:ItemRewards` = `TMap<FName,int32>`, `QuestStage:EnemyRewards` = `TMap<FName,int32>`.

Functions:

| Class | Function | Parameters (name: type) | Notes |
|---|---|---|---|
| `QuestObject` | `InitializeQuest` | (none) | |
| `QuestObjective` | `InitializeObjective` | `InObjectiveData: StructProperty of QuestObjectives` (o 0x0), `InQuestID: NameProperty` (o 0x38) | |
| `QuestObjective` | delegate `OnObjectiveCompleted` | see §1.4 | `MulticastInlineDelegateProperty @0x68` |

Evidence — native quest classes/structs:

```
[00007FF46F039D38] Class /Script/Sparta.QuestObject [n: 47F6F] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F039F38] Class /Script/Sparta.QuestObjective [n: 47F76] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F4873B8] ScriptStruct /Script/Sparta.QuestDetails [n: 13803] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 00007FF46F3D1B28]
```

```
[00007FF46F4872F8] ScriptStruct /Script/Sparta.QuestStage [n: 137FD] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 0000000000000000]
[00007FF46F487238] ScriptStruct /Script/Sparta.QuestObjectives [n: 137F4] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 0000000000000000]
```

`QuestDetails`' parent address `00007FF46F3D1B28` resolves to the DataTable row base (proof that
`DT_QuestData1` rows are `QuestDetails`):

```
00007FF46F3D1B28	ScriptStruct	/Script/Engine.TableRowBase
```

### 1.2 Blueprint quest classes

| Class | Parent | Authored members (name : type @ offset) | Sync relevance |
|---|---|---|---|
| `/Game/Sparta/Dialogue/Blueprints/BP_QuestObject.BP_QuestObject_C` | `/Script/Sparta.QuestObject` | `IsComplete: BoolProperty @0x1e0`, `IsTurnedIn: BoolProperty @0x1e1`, `QuestObjectives: ArrayProperty (inner ObjectProperty of QuestObjective) @0x1e8` | the *runtime* quest instance; `IsComplete`/`IsTurnedIn` are the flags to mirror |
| `/Game/Sparta/Dialogue/Blueprints/BP_QuestObjective_Interact.BP_QuestObjective_Interact_C` | `/Script/Sparta.QuestObjective` | `IsInitialized: BoolProperty @0x80` | interact-objective |
| `/Game/Sparta/Dialogue/Blueprints/BP_QuestObjective_KillBrigands.BP_QuestObjective_KillBrigands_C` | `/Script/Sparta.QuestObjective` | (none beyond `UberGraphFrame`) | kill-objective |

`BP_QuestObject` functions (compiler temporaries filtered):

| Function | Parameters (name: type) |
|---|---|
| `GetQuestDetails` | (none) — internally `GetDataTableRowFromName` with out-row type `StructProperty of /Script/Sparta.QuestDetails` |
| `GetStageDataByID` | `StageName: TextProperty`, `Array: StructProperty of /Script/Sparta.QuestStage`, `StageQuantity: StructProperty of /Script/Sparta.QuestDetails` |
| `AreAllStagesComplete` | `QuestStage: IntProperty`, `QuestDetails: StructProperty of /Script/Sparta.QuestDetails`, `Return: BoolProperty` |
| `OnObjectiveIDHeard` | `ObjectiveID: NameProperty` |
| `InitializeQuest` (override) | (none) |
| `BP_QuestObjective_Interact` : `InitializeObjective` | `InObjectiveData: StructProperty of QuestObjectives`, `InQuestID: NameProperty` |
| `BP_QuestObjective_Interact` : `OnQuestObjectiveInteract` | `Actor: ObjectProperty of /Script/Engine.Actor` |
| `BP_QuestObjective_KillBrigands` : `CharacterDeath` | `Character: ObjectProperty of /Script/Sparta.SpartaCharacter` |

Evidence:

```
[000001F61C358A70] BlueprintGeneratedClass /Game/Sparta/Dialogue/Blueprints/BP_QuestObject.BP_QuestObject_C [n: 4A8EF8] [c: 00007FF46EEC3D38] [or: 000001F4C586B410] [sps: 00007FF46F039D38]
[000001F620EEC370] BlueprintGeneratedClass /Game/Sparta/Dialogue/Blueprints/BP_QuestObjective_Interact.BP_QuestObjective_Interact_C [n: 451391] [c: 00007FF46EEC3D38] [or: 000001F4C586B500] [sps: 00007FF46F039F38]
[000001F620EEBC50] BlueprintGeneratedClass /Game/Sparta/Dialogue/Blueprints/BP_QuestObjective_KillBrigands.BP_QuestObjective_KillBrigands_C [n: 2C6299] [c: 00007FF46EEC3D38] [or: 000001F4C586B460] [sps: 00007FF46F039F38]
```

### 1.3 The quest list itself — `BPC_QuestLogComponent` (the authoritative quest state)

`/Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C`, parent
`/Script/Sparta.SpartaSaveComponent` (i.e. **it is a save component**, so its data is persisted through the
save subsystem — see §9 of part 2). This is the single object a sync layer most likely has to mirror.

Authored members (all of them; offsets are class offsets):

| Member | Type | Offset | Notes |
|---|---|---|---|
| `CurrentActiveQuests` | `ArrayProperty` of `NameProperty` | `0x128` | `TArray<FName>` — active quest IDs |
| `CompletedQuests` | `ArrayProperty` of `NameProperty` | `0x138` | completed quest IDs |
| `CurrentTrackedQuest` | `NameProperty` | `0x148` | the tracked (map/HUD) quest |
| `ActiveQuests` | `ArrayProperty` of `ObjectProperty of BP_QuestObject_C` | `0x150` | `TArray<UBP_QuestObject_C*>` live instances |
| `OnQuestStarted` | `MulticastInlineDelegateProperty` | `0x160` | see §1.4 |
| `NPCRelationships` | `MapProperty` of `TMap<FString,int32>` | `0x170` | per-NPC disposition counter |
| `OnQuestCompleted` | `MulticastInlineDelegateProperty` | `0x1c0` | |
| `PlayerChoice` | `StructProperty of /Script/GameplayTags.GameplayTag` | `0x1d0` | single global story choice tag |
| `OnItemQueried` | `MulticastInlineDelegateProperty` | `0x1d8` | |
| `OnMemoryQueried` | `MulticastInlineDelegateProperty` | `0x1e8` | |
| `NarrativeEvents` | `MapProperty` of `TMap<FString,int32>` | `0x1f8` | narrative-event counters |
| `OnEventQueried` | `MulticastInlineDelegateProperty` | `0x248` | |
| `NarrativeTags` | `StructProperty of GameplayTagContainer` | `0x258` | **the story-progress tag container** |
| `OnNarrativeTagsLoaded` | `MulticastInlineDelegateProperty` | `0x278` | |
| `BlisterBoiDonations` | `IntProperty` | `0x288` | |
| `ImmortalBrigSpoils` | `MapProperty` of `TMap<FString,int32>` | `0x290` | |
| `WormfishGold` | `MapProperty` of `TMap<FString,int32>` | `0x2e0` | |
| `MemoryOverride` | `BoolProperty` | `0x330` | |
| `MemoryOverrideRef` | `ObjectProperty` | `0x338` | |
| `TotalGoldSpentMerchant` | `MapProperty` of `TMap<FString,int32>` | `0x340` | |
| `Array` (compiler temp of a loop) | `StructProperty of GameplayTagContainer` | `0x390` | ignore |

Functions (compiler temporaries filtered):

| Function | Parameters (name: type) |
|---|---|
| `AddNewQuest` | `QuestID: NameProperty` |
| `CompleteQuest` | `QuestID: NameProperty` |
| `TurnInQuest` | `QuestID: NameProperty` |
| `QueryActiveQuest` | `QuestID: NameProperty`, `ReturnValue: BoolProperty` |
| `QueryCompletedQuests` | `QuestID: NameProperty`, `ReturnValue: BoolProperty`, `TurnedIn: BoolProperty` |
| `GetQuestObject` | `QuestID: NameProperty`, `Array: ObjectProperty of BP_QuestObject_C` |
| `TrackQuests` | (none) |
| `AddNarrativeTag` | `Tag: StructProperty of GameplayTag`, `SaveToDisk: BoolProperty` |
| `RemoveNarrativeTag` | `Tag: StructProperty of GameplayTag`, `SaveToDisk: BoolProperty` |
| `LoadNarrativeTags` | (none) |
| `ResetForNewGamePlus` | `NarrativeTagsToCheckRetain: StructProperty of GameplayTagContainer`, `RetainedNarrativeTags: StructProperty of GameplayTagContainer` |
| `Query` (×2 overloads) | `Query: StructProperty of /Script/Engine.DataTableRowHandle` |
| `Query Quest Item` | `QuestItemData: StructProperty of /Script/Engine.DataTableRowHandle` |
| `SaveLoaded` | `SaveContext: StructProperty of /Script/Sparta.SpartaSaveContext` |
| `SetMemoryOverride` | `EventData: ObjectProperty` |
| `OnDeathOrBossDefeat` | `EventData: ObjectProperty` |
| `ReceiveBeginPlay` / `ReceiveEndPlay` | `EndPlayReason: ByteProperty` (EndPlay only) |

Delegates (signatures proven by their `__DelegateSignature` functions):

| Delegate | Offset | Signature (name: type) |
|---|---|---|
| `OnQuestStarted` | `0x160` | `QuestID: NameProperty` |
| `OnQuestCompleted` | `0x1c0` | `QuestObject: ObjectProperty of /Script/Sparta.QuestObject` |
| `OnItemQueried` | `0x1d8` | `QuestItemData: StructProperty of /Script/Engine.DataTableRowHandle` |
| `OnMemoryQueried` | `0x1e8` | `ShellMemoryData: StructProperty of /Script/Engine.DataTableRowHandle` |
| `OnEventQueried` | `0x248` | `EventData: StructProperty of /Script/Engine.DataTableRowHandle` |
| `OnNarrativeTagsLoaded` | `0x278` | `NarrativeTags: StructProperty of GameplayTagContainer`, `QuestLog: ObjectProperty of BPC_QuestLogComponent_C` |

Evidence:

```
[000001F614CBDC60] BlueprintGeneratedClass /Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C [n: 4335D8] [c: 00007FF46EEC3D38] [or: 000001F4C586B5A0] [sps: 00007FF46F038738]
```

```
[000001F614CE8300] MapProperty /Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C:NPCRelationships [o: 170] [n: 628071] [c: 00007FF721DE8F70] [owr: 000001F614CBDC60] [kp: 000001F61C38FBA0] [vp: 000001F61C38FB30]
[000001F61C38FBA0] StrProperty NPCRelationships./Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C:NPCRelationships [o: 0] [n: 628071] [c: 00007FF721DFAB30] [owr: 000001F614CE8300]
[000001F61C38FB30] IntProperty NPCRelationships./Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C:NPCRelationships_Value [o: 10] [n: 62807A] [c: 00007FF721DE91F0] [owr: 000001F614CE8300]
```

(same three objects in the ingame dump, with instance addresses instead:
`[000002728A82A1A0] MapProperty …:NPCRelationships [o: 170] … [kp: 00000272A6608350] [vp: 00000272A66082E0]`.)

```
[000001F61C516F80] MulticastInlineDelegateProperty /Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C:OnQuestStarted [o: 160] [n: 628134] [c: 00007FF721DE9010] [owr: 000001F614CBDC60] [df: 000001F61C3BE700]
[000001F61C3BE700] Function /Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C:OnQuestStarted__DelegateSignature [n: 62813C] [c: 00007FF46EE55538] [or: 000001F614CBDC60] [f: 00007FF7183D9710]
[000001F6217886D0] NameProperty /Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C:OnQuestStarted__DelegateSignature:QuestID [o: 0] [n: 58675A] [c: 00007FF721DE90B0] [owr: 000001F61C3BE700]
```

### 1.4 Quest DataTable

| Asset | Type | Path | Row struct |
|---|---|---|---|
| `DT_QuestData1` | `DataTable` | `/Game/Sparta/Dialogue/QuestSystem/DT_QuestData1.DT_QuestData1` | GUESS: `/Script/Sparta.QuestDetails` — the dump does not record a DataTable's row struct; the only `TableRowBase` struct in the quest namespace is `QuestDetails` and `BP_QuestObject_C:GetQuestDetails` reads a row into a `QuestDetails` out-parameter. Rows themselves are not in the dump. |

Evidence:

```
[000001F4C48024B0] DataTable /Game/Sparta/Dialogue/QuestSystem/DT_QuestData1.DT_QuestData1 [n: 29EC82] [c: 00007FF46EF31938] [or: 000001F4D304F740]
```

Full quest-system folder listing (output of `parse_dump.py find raw\index.sqlite3 "/Game/Sparta/Dialogue/QuestSystem/" --types Class,BlueprintGeneratedClass,Function`, one line per object; the two evidence rows that follow are verbatim dump lines):

```
BlueprintGeneratedClass	/Game/Sparta/Dialogue/QuestSystem/BPC_NarrativeEventsTracker.BPC_NarrativeEventsTracker_C
BlueprintGeneratedClass	/Game/Sparta/Dialogue/QuestSystem/BPC_QuestInstigatorComponent.BPC_QuestInstigatorComponent_C
BlueprintGeneratedClass	/Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C
BlueprintGeneratedClass	/Game/Sparta/Dialogue/QuestSystem/BPSO_NarrativeEventsTracker.BPSO_NarrativeEventsTracker_C
BlueprintGeneratedClass	/Game/Sparta/Dialogue/QuestSystem/WBP_NarrativeEventsTrackerDebug.WBP_NarrativeEventsTrackerDebug_C
BlueprintGeneratedClass	/Game/Sparta/Dialogue/QuestSystem/WBP_QuestLogEntry_Objective.WBP_QuestLogEntry_Objective_C
BlueprintGeneratedClass	/Game/Sparta/Dialogue/QuestSystem/WBP_QuestObjectiveNotification.WBP_QuestObjectiveNotification_C
Package	/Game/Sparta/Dialogue/QuestSystem/DT_QuestData1
Package	/Game/Sparta/Dialogue/QuestSystem/F_NarrativeEventFloatTracking
```

Evidence (verbatim, two of the classes above):

```
[000001F620599520] BlueprintGeneratedClass /Game/Sparta/Dialogue/QuestSystem/BPC_QuestInstigatorComponent.BPC_QuestInstigatorComponent_C [n: 26B37E] [c: 00007FF46EEC3D38] [or: 000001F4C586A740] [sps: 00007FF46EE71738]
[000001F61C359FD0] BlueprintGeneratedClass /Game/Sparta/Dialogue/QuestSystem/BPC_NarrativeEventsTracker.BPC_NarrativeEventsTracker_C [n: 173982] [c: 00007FF46EEC3D38] [or: 000001F4C5865FB0] [sps: 00007FF46F038738]
```

### 1.5 Quest giver components

| Class | Parent | Members | Sync relevance |
|---|---|---|---|
| `/Game/Sparta/Dialogue/QuestSystem/BPC_QuestInstigatorComponent.BPC_QuestInstigatorComponent_C` | `/Script/Engine.ActorComponent` | functions only: `Display`, `DisplayRewards`, `Interact` (no authored non-temp properties) | the object a player interacts with to start/turn in a quest; `QuestLog` is reached via `GetOwningSaveComponent` cast to `BPC_QuestLogComponent` |

---

## 2. Objective-driven map markers (`WorldMapObjectiveComponent`) and world map

| Class | Parent | Members (name : type @ offset) |
|---|---|---|
| `/Script/Sparta.WorldMapObjectiveComponent` | `/Script/Sparta.SpartaSaveComponent` | `RevealType: EnumProperty of /Script/Sparta.ERevealType @0x120`, `RevealDistance: FloatProperty @0x124` |
| `/Game/Sparta/UI/World/Map/Blueprints/BPC_WorldMapObjective.BPC_WorldMapObjective_C` | `/Script/Sparta.WorldMapObjectiveComponent` | `Enabled @0x130`, `CanBeCompleted @0x131`, `bShouldSaveAsActive @0x132`, `IconClass @0x138`, `ObjectiveTag @0x140`, `BiomeTag @0x148`, `EquipmentTag @0x150`, `OptionalCustomTag @0x158`, `Name: TextProperty @0x160`, `OptionalEvent @0x170`, `OnObjectiveRevealed: MulticastInlineDelegateProperty @0x178`, `CustomDefaultIcon @0x188`, `CustomCompletedIcon @0x1b0` (last two `TSoftObjectPtr<UTexture2D>`) |
| `/Game/Sparta/UI/World/Map/Blueprints/BPC_WorldMapHandler.BPC_WorldMapHandler_C` | `/Script/Sparta.SpartaSaveComponent` | `CurrentAreaID: GameplayTag @0x13c`, `CurrentActiveObjective @0x148`, `LastPlayerLocation: FVector @0x150`, `MapObjectives: TArray<BPC_WorldMapObjective_C*> @0x168`, `MapActorWidgets @0x178`, `MapTrackers: TMap<int32,ABP_MapTracker_C*> @0x188`, `MaxTrackers @0x1e0`, `MapFilterState: TMap<GameplayTag,bool> @0x1f8`, `MapSettings: TSubclassOf<UBP_WorldMapSettings_C> @0x248`, `MapTrackingWidget @0x250` |
| `/Game/Sparta/UI/World/Map/Blueprints/BPSO_WorldMap.BPSO_WorldMap_C` | `/Script/Sparta.SpartaSaveObject` | `Trackers: TMap<int32,FVector> @0xb8`, `ObjectiveGUID: FString @0x108`, `IsInsideObjective: bool @0x118`, `LastPlayerLocation: FVector @0x120` |
| `/Game/Sparta/UI/World/Map/Blueprints/BPSO_WordMapObjective.BPSO_WordMapObjective_C` | `/Script/Sparta.SpartaSaveObject` | `Visible @0xc0`, `Revealed @0xc1`, `Completed @0xc2`, `ShowArea @0xc3`, `PlayedRevealScript @0xc4` |

Key functions:

| Class | Function | Parameters (name: type) |
|---|---|---|
| `WorldMapObjectiveComponent` | `RevealObjective` | (none) |
| `WorldMapObjectiveComponent` | `EnableRevealDistanceCheck` / `DisableRevealDistanceCheck` | (none) |
| `BPC_WorldMapObjective_C` | `CompleteObjective` / `UncompleteObjective` / `ShowArea` / `RevealObjective` | (none) |
| `BPC_WorldMapObjective_C` | `ShouldRevealObjective` | `Object: ObjectProperty` |
| `BPC_WorldMapObjective_C` | `IsCompleted` / `IsVisible` / `IsRevealed` / `HasPlayedRevealScript` / `SetPlayedRevealScript` | (none) |
| `BPC_WorldMapObjective_C` | `GetObjectiveSaveData` | `bSuccess: BoolProperty` |
| `BPC_WorldMapObjective_C` | `InitializeName` | `NewValue: TextProperty` |
| `BPC_WorldMapObjective_C` | `InitializeBiome` / `InitializeEquipmentTag` | `NewValue: StructProperty of GameplayTag` / `Value: StructProperty of GameplayTag` |
| `BPC_WorldMapObjective_C` | `InitializeEvent` | `Value: ObjectProperty of /Script/Sparta.SpartaEventDataAsset` |
| `BPC_WorldMapObjective_C` | `SaveLoaded` / `SpartaPreSave` | `SaveContext: StructProperty of /Script/Sparta.SpartaSaveContext` |
| `BPC_WorldMapObjective_C` | `SessionReset` | (none) |
| delegate `OnObjectiveRevealed` | @`0x178` | (no parameters) |
| `BPC_WorldMapHandler_C` | `SetHUBAsCurrentObjective`, `SetGateAsCurrentObjective(AreaID: GameplayTag)`, `UpdateAreaID(Object)`, `InitAreaID`, `InvalidateActiveObjective`, `LoadCurrentOjective(Objective: BPC_WorldMapObjective_C*)`, `GetMySaveData(bSuccess)`, `CreateMapRevealScriptContainer`, `UpdatePlayerLocation`, `ReprisePlayerLocationUpdate(AreaID: GameplayTag)`, `HandleStopLocationUpdate(Object)`, `CachePlayerCharacter`, `RemoveMapActor(Component: BPC_MapActorWidget_C*)`, `ToggleMapTrackingWidget`, `ToggleDebugWidget`, `LoadTrackers`, `DestroyMapTracker(Index: int32)`, `RemoveMapTracker(Index: int32)`, `CalculateTrackerIndex(Success)`, `AddMapTracker(Actor: ABP_MapTracker_C*&, Index: int32)`, `AddMapActor(Component)`, `AddMapObjective(Component: BPC_WorldMapObjective_C*)`, `StopPlayerUpdateOnMap`, `OnBeaconRest`, `OnLandingAreaSelected`, `SaveLoaded(SaveContext)` | |

Evidence:

```
[000001F4C28526B0] BlueprintGeneratedClass /Game/Sparta/UI/World/Map/Blueprints/BPC_WorldMapObjective.BPC_WorldMapObjective_C [n: 104E5C] [c: 00007FF46EEC3D38] [or: 000001F4C7051000] [sps: 00007FF46F064538]
[000001F4C2852A40] BlueprintGeneratedClass /Game/Sparta/UI/World/Map/Blueprints/BPC_WorldMapHandler.BPC_WorldMapHandler_C [n: 4C002B] [c: 00007FF46EEC3D38] [or: 000001F4C635EE30] [sps: 00007FF46F038738]
[000001F4C2F46DF0] BlueprintGeneratedClass /Game/Sparta/UI/World/Map/Blueprints/BPSO_WorldMap.BPSO_WorldMap_C [n: 1390D6] [c: 00007FF46EEC3D38] [or: 000001F4C705A830] [sps: 00007FF46F038338]
```

```
[000001F4CE284A50] BlueprintGeneratedClass /Game/Sparta/UI/World/Map/Blueprints/BPSO_WordMapObjective.BPSO_WordMapObjective_C [n: 116799] [c: 00007FF46EEC3D38] [or: 000001F4C705B820] [sps: 00007FF46F038338]
```

---

## 3. Activities, areas, and run completion

The game has a *run/content-selection* layer on top of quests: `SpartaAreaId` (a DataAsset) lists
`PossibleActivity`s, each of which names an `SpartaActivityType` (DataAsset) and a **DataLayer**; the
`SpartaAreaSubsystem` (a WorldSubsystem) tracks the active area and completed areas; `SpartaRunManagerSubsystem`
tracks which level instances (event spaces) were already streamed in; `SpartaCompletionSubsystem` scores a run.

| Class / Struct | Parent | Members (name : type @ offset) |
|---|---|---|
| `/Script/Sparta.SpartaAreaId` | `/Script/Engine.DataAsset` | `DisplayName: TextProperty @0x30`, `DataLayer: StrProperty @0x40`, `DataLayerAsset: ObjectProperty of /Script/Engine.DataLayerAsset @0x50`, `PossibleActivities: ArrayProperty (inner ObjectProperty of PossibleActivity) @0x58` |
| `/Script/Sparta.SpartaActivityType` | `/Script/Engine.DataAsset` | `Icon: ObjectProperty of Texture2D @0x30` (from ref query: 48), `DisplayName: TextProperty @0x38`, `AreaID: ObjectProperty of SpartaAreaId @0x48`, `bRequiredCompletionEvent: BoolProperty @0x50` |
| `/Script/Sparta.PossibleActivity` | `/Script/CoreUObject.Object` | `ActivityType @0x28`, `DataLayer: StrProperty @0x30`, `DataLayerAsset @0x40`, `ActivityParameters: StructProperty of ActivityParameters @0x48`, `AreaID @0x100` |
| `/Script/Sparta.ActivityParameters` | (struct) | `RandomWeight: FloatProperty @0x0`, `Costs: MapProperty (key EnumProperty of EMapGenCostType, value FloatProperty) @0x8`, `RequiredTags: GameplayTagContainer @0x58`, `ExcludedTags: GameplayTagContainer @0x78`, `AddedTags: GameplayTagContainer @0x98` |
| `/Script/Sparta.PossibleWorldStateSet` | `/Script/CoreUObject.Object` | `WorldStates: ArrayProperty (inner StructProperty of PossibleWorldState) @0x28` |
| `/Script/Sparta.PossibleWorldState` | (struct) | `WorldState: StructProperty of GameplayTags.GameplayTag @0x0`, `NestedWorldStateSets: ArrayProperty (inner ObjectProperty of PossibleWorldStateSet) @0x8` |
| `/Script/Sparta.SpartaAreaSubsystem` | `/Script/Engine.WorldSubsystem` | `ActiveArea: ObjectProperty of SpartaAreaId @0x30`, `CompletedAreas: SetProperty @0x38`, `OnAreaActivated: MulicastInlineDelegate @0x88`, `OnAreaDeactivated @0x98`, `OnAreaComplete @0xa8` |
| `/Script/Sparta.SpartaRunManagerSubsystem` | `/Script/Engine.GameInstanceSubsystem` | `AlreadySelectedEventInstances: ArrayProperty (inner SoftObjectProperty of /Script/Engine.World) @0x30`, `RunStateIntegers: MapProperty (key StructProperty of GameplayTag, value IntProperty) @0x40` |
| `/Script/Sparta.SpartaCompletionSubsystem` | `/Script/Engine.GameInstanceSubsystem` | `CompletionSettings: SoftObjectProperty of SpartaCompletionSettings @0x30`, `ResolvedSettings @0x58`, `Snapshot: StructProperty of SpartaCompletionResult @0x60`, `RunTimeSeconds: FloatProperty @0x88`, `bRunTimerPaused: BoolProperty @0x8c` |
| `/Script/Sparta.SpartaCompletionSettings` | `/Script/Engine.PrimaryDataAsset` | `Categories: ArrayProperty of SpartaCompletionCategoryDef @0x30`, `OvaShareOfSurvivalOdds @0x40`, `OvaItemDefinition @0x48`, `TarstoneTables: ArrayProperty of SoftObjectProperty of DataTable @0x70`, `ShellTags @0x80`, `WeaponTags @0x90`, `SidearmTags @0xa0`, `EquipmentUnlockStatePropertyName: NameProperty @0xb0`, `ItemsTable: SoftObjectProperty of DataTable @0xb8`, `ExcludedItemClasses @0xe0`, `bExcludePermProgressionItems @0xf0`, `PermProgressionPathFragment: StrProperty @0xf8` |
| `/Script/Sparta.SpartaCompletionResult` | (struct) | `CompletionFraction @0x0`, `SurvivalOdds @0x4`, `RunTimeSeconds @0x8`, `Categories: ArrayProperty of SpartaCompletionCategoryProgress @0x10`, `bValid @0x20` |
| `/Script/Sparta.SpartaCompletionCategoryDef` | (struct) | `Category: EnumProperty of ESpartaCompletionCategory @0x0`, `DisplayName @0x8`, `TotalOverride @0x18`, `Weight @0x1c` |
| `/Script/Sparta.SpartaCompletionCategoryProgress` | (struct) | `Category @0x0`, `DisplayName @0x8`, `Collected @0x18`, `Total @0x1c`, `Fraction @0x20`, `Weight @0x24`, `bScored @0x28` |

Functions:

| Class | Function | Parameters (name: type) |
|---|---|---|
| `SpartaAreaSubsystem` | `ActivateArea` / `CompleteArea` | `Area: ObjectProperty of SpartaAreaId` |
| `SpartaAreaSubsystem` | `GetActiveArea` | `ReturnValue: ObjectProperty of SpartaAreaId` |
| `SpartaAreaSubsystem` | `GetActivityType` | `Area: ObjectProperty of SpartaAreaId`, `ReturnValue: ObjectProperty of SpartaActivityType` |
| `SpartaAreaSubsystem` | `GetConnectedAreas` | `Area: ObjectProperty of SpartaAreaId`, `ReturnValue: ArrayProperty of SpartaAreaId` |
| `SpartaAreaSubsystem` | `IsAreaActive` / `IsCompleteActive` | `Area: ObjectProperty of SpartaAreaId`, `ReturnValue: BoolProperty` |
| `SpartaRunManagerSubsystem` | `AddRunStateInteger` | `Key: StructProperty of GameplayTag`, `Value: IntProperty` |
| `SpartaRunManagerSubsystem` | `GetRunStateInteger` | `Key: StructProperty of GameplayTag`, `ReturnValue: IntProperty` |
| `SpartaRunManagerSubsystem` | `RemoveRunStateInteger` | `Key: StructProperty of GameplayTag` |
| `SpartaRunManagerSubsystem` | `LevelInstanceSelected` | `SelectedLevel: SoftObjectProperty of /Script/Engine.World`, `ReturnValue: BoolProperty` |
| `SpartaRunManagerSubsystem` | `HasLevelInstanceAlreadyBeenStreamed` | `Level: SoftObjectProperty of /Script/Engine.World`, `ReturnValue: BoolProperty` |
| `SpartaRunManagerSubsystem` | `GetEventDataForSpace` | `OptionSet: ObjectProperty of /Script/CSOptionSet.CSOptionSetDataAsset`, `EventSpaceActor: ObjectProperty of /Script/Sparta.SpartaEventSpaceActor`, `OutError: StrProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaEventDataAsset` |
| `SpartaCompletionSubsystem` | `SnapshotCompletion` / `CalculateCompletion` | `WorldContextObject: ObjectProperty`, `ReturnValue: StructProperty of SpartaCompletionResult` |
| `SpartaCompletionSubsystem` | `ApplySurvivalCurve` | `OvaFraction: FloatProperty`, `CapabilityFraction: FloatProperty`, `ReturnValue: FloatProperty` |
| `SpartaCompletionSubsystem` | `SetRunTimeSeconds` | `InSeconds: FloatProperty` |
| `SpartaCompletionSubsystem` | `SetRunTimerPaused` | `bPaused: BoolProperty` |
| `SpartaCompletionSubsystem` | `SetCompletionSettings` | `InSettings: ObjectProperty of SpartaCompletionSettings` |
| `SpartaCompletionSubsystem` | `GetSnapshot` | `ReturnValue: StructProperty of SpartaCompletionResult` |
| `SpartaCompletionSubsystem` | `GetCompletionSettings` | `ReturnValue: ObjectProperty of SpartaCompletionSettings` |
| `SpartaCompletionSubsystem` | `GetRunTimeSeconds` / `IsRunTimerPaused` / `HasSnapshot` | `ReturnValue` (`FloatProperty` / `BoolProperty` / `BoolProperty`) |
| `SpartaCompletionSubsystem` | `ClearSnapshot` | (none) |
| `SpartaCompletionLibrary` (BlueprintFunctionLibrary) | `SnapshotRunCompletion`, `GetRunCompletionResult`, `GetRunCompletionPercent`, `GetRunTimeSeconds`, `GetRunTimeText`, `GetSurvivalOddsPercent` | each `WorldContextObject: ObjectProperty` + `ReturnValue` |

Delegate on the area subsystem:

| Delegate | Offset | Signature |
|---|---|---|
| `OnAreaActivated` / `OnAreaDeactivated` / `OnAreaComplete` | `0x88` / `0x98` / `0xa8` | `/Script/Sparta.SpartaAreaEvent__DelegateSignature` → `Area: ObjectProperty of /Script/Sparta.SpartaAreaId` (single param) |

Evidence:

```
[00007FF46F03ED38] Class /Script/Sparta.SpartaActivityType [n: 48764] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[00007FF46F03EF38] Class /Script/Sparta.PossibleActivity [n: 4876E] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F03F138] Class /Script/Sparta.SpartaAreaId [n: 48777] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
```

```
[00007FF46F03F338] Class /Script/Sparta.SpartaAreaSubsystem [n: 4877E] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7C138]
[00007FF46F064138] Class /Script/Sparta.PossibleWorldStateSet [n: 4B304] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F3D24F0] DelegateFunction /Script/Sparta.SpartaAreaEvent__DelegateSignature [n: 525B4B] [c: 00007FF46EE56338] [or: 00007FF46F17CD68]
```

```
[000001F4ACD96E00] ObjectProperty /Script/Sparta.SpartaAreaSubsystem:ActiveArea [o: 30] [n: 586FC8] [c: 00007FF721DE9420] [owr: 00007FF46F03F338] [pc: 00007FF46F03F138]
[000001F4A93E8950] SetProperty /Script/Sparta.SpartaAreaSubsystem:CompletedAreas [o: 38] [n: 586FC0] [c: 00007FF721DE94F0] [owr: 00007FF46F03F338]
[000001F4ACD96F00] MulticastInlineDelegateProperty /Script/Sparta.SpartaAreaSubsystem:OnAreaActivated [o: 88] [n: 586FB7] [c: 00007FF721DE9010] [owr: 00007FF46F03F338] [df: 00007FF46F3D24F0]
```

```
[000001F49CD2F6A0] MapProperty /Script/Sparta.SpartaRunManagerSubsystem:RunStateIntegers [o: 40] [n: 5890AF] [c: 00007FF721DE8F70] [owr: 00007FF46F056F38] [kp: 000001F4ACDF2980] [vp: 000001F4ACDE1660]
[000001F4ACDF2980] StructProperty RunStateIntegers./Script/Sparta.SpartaRunManagerSubsystem:RunStateIntegers_Key [o: 0] [n: 5890B8] [c: 00007FF721DF9890] [owr: 000001F49CD2F6A0] [ss: 00007FF46F3890A8]
```

```
[00007FF46F044738] Class /Script/Sparta.SpartaCompletionLibrary [n: 48E92] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE70138]
[00007FF46F044938] Class /Script/Sparta.SpartaCompletionSettings [n: 48EE2] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF40738]
[00007FF46F044B38] Class /Script/Sparta.SpartaCompletionSubsystem [n: 48EEF] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF5DF38]
[00007FF46F056F38] Class /Script/Sparta.SpartaRunManagerSubsystem [n: 4A243] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF5DF38]
```

Note — `SpartaActivityDescriptor` (`/Script/Sparta.SpartaActivityDescriptor`, parent `/Script/Engine.Actor`) also
exists (from the same enumeration) but has no members other than its actor base; I did **not** establish what
reads it (UNKNOWN).

---

## 4. Player progression (levels, skills, points)

| Class | Parent | Members (name : type @ offset) |
|---|---|---|
| `/Script/Sparta.PlayerProgressionComponent` | `/Script/Sparta.SpartaSaveComponent` | `ShellBuilds: MapProperty (key GameplayTag, value StructProperty of ShellSkillSet) @0x120`, `TierBuilds: MapProperty @0x170`, `SkillLevels: MapProperty (key SoftClassProperty, value IntProperty) @0x1c0`, `EquippedSkillInstances: MapProperty (key SoftClassProperty, value ObjectProperty of SpartaItemInstance) @0x210`, `OnSkillEquipped @0x260`, `OnSkillUnequipped @0x270`, `OnSkillLevelChanged @0x280` (all `MulticastInlineDelegateProperty`) |
| `/Script/Sparta.PlayerProgressionSaveData` | `/Script/Sparta.SpartaSaveObject` | `SkillLevels: MapProperty @0xb8`, `ShellBuilds: MapProperty @0x108`, `TierBuilds: MapProperty @0x158` |
| `/Game/Sparta/SaveObjects/BPSO_Progression.BPSO_Progression_C` | `/Script/Sparta.PlayerProgressionSaveData` | `CurrentLevel: int32 @0x1a8`, `AvailablePoints: int32 @0x1ac`, `AssignedPoints: TMap<GameplayTag,int32> @0x1b0`, `BondingPoints: TMap<GameplayTag,int32> @0x200`, `GoldSpentAtShop: int32 @0x250` (from `BPSO_Progression.hpp`) |
| `/Game/Sparta/Core/Player/Components/BPC_Player_Progression.BPC_Player_Progression_C` | `/Script/Sparta.PlayerProgressionComponent` | `LevelingCurve: ObjectProperty of CurveFloat @0x298`, `StartingMaxShellPoints: MapProperty @0x2a0`, `MaxLevel: IntProperty @0x2f0`, `OnPointsUpdated @0x2f8`, `OnAssignedPointsUpdated @0x308`, `OnTierPointsUpdated @0x318`, `OnTierUnlocked @0x328`, `MaxBondingPoints: IntProperty @0x338`, `ShellProgression: MapProperty @0x340`, `TierThresholds: ArrayProperty @0x390`, `TierRanges: ArrayProperty @0x3a0`, `DarkbroProgresionDataAsset: ObjectProperty of SpartaProgressionDataAsset @0x3b0`, `MySpartaASC @0x3b8`, `ProgressionGameplayEffect: ActiveGameplayEffectHandle @0x3c0`, `HealingCapacityUpgradeCost @0x3c8`, `HealingEfficiencyUpgradeCost @0x3d8`, `OnHealingPowerUpgraded @0x3e8`, `OnHealingEfficiencyUpgraded @0x3f8` |
| `/Script/Sparta.SpartaProgressionDataAsset` | `/Script/Engine.DataAsset` | `ProgressionGameplayEffectClass: ClassProperty @0x30`, `RepeatableAttributeProgression: MapProperty @0x38`, `FAttributeProgressionCyclicalDiminishing: MapProperty @0x88`, `TagToAttributeBinding: MapProperty @0xd8` |
| `/Script/Sparta.HubEventComponent` | `/Script/Engine.ActorComponent` | `CurrentHubEventPoints: FloatProperty @0xb8`, `TotalHubEventPoints: FloatProperty @0xbc` |

Functions (`PlayerProgressionComponent`, full parameter lists, temps filtered):

| Function | Parameters (name: type) |
|---|---|
| `AddTierSkill` | `ShellTag: StructProperty of GameplayTag`, `SkillClass: SoftClassProperty`, `ReturnValue: ObjectProperty of SpartaItemInstance` |
| `RemoveTierSkill` | same three |
| `EquipSkill` | same three |
| `UnequipSkill` | same three |
| `UnequipAllSkills` | `ShellTag: StructProperty of GameplayTag` |
| `AddToSkillLevel` | `SkillClass: SoftClassProperty`, `Delta: IntProperty` |
| `SetSkillLevel` | `SkillClass: SoftClassProperty`, `NewLevel: IntProperty` |
| `GetSkillLevel` | `SkillClass: SoftClassProperty`, `ReturnValue: IntProperty` |
| `FindSkillMaximumLevel` | `SkillClass: SoftClassProperty`, `ReturnValue: IntProperty` |
| `CanLevelUp` | `SkillClass: SoftClassProperty`, `ReturnValue: BoolProperty` |
| `HasSkill` | `SkillClass: SoftClassProperty`, `ReturnValue: BoolProperty` |
| `HasTierSkill` | `ShellTag: StructProperty of GameplayTag`, `SkillClass: SoftClassProperty`, `ReturnValue: BoolProperty` |
| `IsSkillAtMaxLevel` | `SkillClass: SoftClassProperty`, `ReturnValue: BoolProperty` |
| `GetTierSkills` | `ShellTag: StructProperty of GameplayTag`, `ReturnValue: ArrayProperty of SoftClassProperty` |
| `RefundAllSkills` | (none) |
| `OnChangeShellItem` | `OldShell: ClassProperty`, `NewShell: ClassProperty` |

Functions (`SpartaProgressionDataAsset`):

| Function | Parameters (name: type) |
|---|---|
| `ApplyLevel` | `ASC: ObjectProperty of SpartaAbilitySystemComponent`, `Level: IntProperty` |
| `GetDataAssetValueAtLevel` | `StatTag: StructProperty of GameplayTag`, `Level: IntProperty`, `ReturnValue: FloatProperty` |
| `GetDataAssetDeltaBetweenLevels` | `StatTag: StructProperty of GameplayTag`, `FromLevel: IntProperty`, `ToLevel: IntProperty`, `ReturnValue: FloatProperty` |
| `BuildLevelPreviewRowsMap` | `ASC: ObjectProperty of SpartaAbilitySystemComponent`, `CurrentLevel: IntProperty`, `PreviewLevel: IntProperty`, `OutRows: MapProperty` |
| `GetPreviewRowForTag` | `ASC: ObjectProperty of SpartaAbilitySystemComponent`, `StatTag: StructProperty of GameplayTag`, `CurrentLevel: IntProperty`, `PreviewLevel: IntProperty`, `OutRow: StructProperty of SpartaStatPreviewRow`, `ReturnValue: BoolProperty` |

`/Script/Sparta.SpartaWeaponProgressionData` (`/Script/Engine.DataAsset`) exists as a separate asset-based
weapon upgrade definition (see `20-...-part2.md` §11 and `F_WeaponProgression.hpp`); it is item/gun-adjacent,
so item-level detail is out of scope here.

Evidence:

```
[00007FF46F038138] Class /Script/Sparta.PlayerProgressionSaveData [n: 47C35] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038338]
[00007FF46F056138] Class /Script/Sparta.SpartaProgressionDataAsset [n: 4A11F] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[000001F4BE037180] BlueprintGeneratedClass /Game/Sparta/SaveObjects/BPSO_Progression.BPSO_Progression_C [n: 344437] [c: 00007FF46EEC3D38] [or: 000001F4BFB61F50] [sps: 00007FF46F038138]
```

```
[00007FF46F034738] Class /Script/Sparta.HubEventComponent [n: 479E4] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[000001F4ACD0F7B0] FloatProperty /Script/Sparta.HubEventComponent:CurrentHubEventPoints [o: B8] [n: 58616B] [c: 00007FF721DE9380] [owr: 00007FF46F034738]
[000001F4ACD0F820] FloatProperty /Script/Sparta.HubEventComponent:TotalHubEventPoints [o: BC] [n: 586160] [c: 00007FF721DE9380] [owr: 00007FF46F034738]
```

```
[000001F617F19520] BlueprintGeneratedClass /Game/Sparta/Core/Player/Components/BPC_Player_Progression.BPC_Player_Progression_C [n: 12E5A4] [c: 00007FF46EEC3D38] [or: 000001F4D1C17B80] [sps: 00007FF46F038538]
[000001F61C513E00] StructProperty /Game/Sparta/Core/Player/Components/BPC_Player_Progression.BPC_Player_Progression_C:UberGraphFrame [o: 290] [n: 59AB38] [c: 00007FF721DF9890] [owr: 000001F617F19520] [ss: 00007FF46F446958]
```

---

## 5. Achievements

| Class / Asset | Parent / Type | Members |
|---|---|---|
| `/Script/Sparta.SpartaAchievementsSubsystem` | `/Script/Engine.GameInstanceSubsystem` | functions only: `SubscribeToAchievementEvents()`, `SetAchievementProgress(EventData: ObjectProperty)`, `QueryAchievements()` |
| `/Script/Sparta.GEAchievementEvent` | `/Script/GlobalEventSubsystem.GlobalEvent` | blueprint-visible global event (see §6.4 note on the global-event subsystem) |
| `/Game/Sparta/Core/Achievements/BPC_Achievements.BPC_Achievements_C` | `/Script/Engine.ActorComponent` | component |
| `/Game/Sparta/Core/Achievements/BPFL_Achievements.BPFL_Achievements_C` | `/Script/Engine.BlueprintFunctionLibrary` | function library |
| `/Game/Sparta/Core/Achievements/DT_Achievements.DT_Achievements` | `DataTable` | definition table (row struct `FF_AchievementProgress` in `F_AchievementProgress.hpp`: `ProgressiveUnlock @0x0`, `Progress @0x8`, `Earned @0x10`) — GUESS that this table uses that struct: the dump does not record DataTable row structs, and `FF_AchievementProgress` is not a `TableRowBase` struct |
| `/Game/Sparta/UI/Notifies/FakeAchievement/DT_FakeAchievements.DT_FakeAchievements` | `DataTable` | in-game notification table |
| `/Game/Sparta/Core/Characters/Behaviors/BP_CharacterBehavior_Achievement_OnDeath.BP_CharacterBehavior_Achievement_OnDeath_C` | `/Script/Sparta.SpartaCharacterExecutableBehavior` | achievement trigger on character death |

Persisted achievement state lives **on the player save object**, not in a dedicated save object:
`BP_SpartaPlayerSaveObject_C:EarnedAchievements: TMap<FString,bool> @0x650` and
`CurrentGoodSheepAchievement: int32 @0x9bc`, `SlayerSealLockout: bool @0x6a0` (see part 2 §9.5).

Evidence:

```
[00007FF46F03BD38] Class /Script/Sparta.SpartaAchievementsSubsystem [n: 4831D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF5DF38]
[000001F4DCBFF2F0] DataTable /Game/Sparta/Core/Achievements/DT_Achievements.DT_Achievements [n: 2069CF] [c: 00007FF46EF31938] [or: 000001F4CBCF7360]
[000001F4DCBFF3A0] DataTable /Game/Sparta/UI/Notifies/FakeAchievement/DT_FakeAchievements.DT_FakeAchievements [n: 3C04FA] [c: 00007FF46EF31938] [or: 000001F4CBCF7400]
```

---

## 6. Dialogue

### 6.1 `DialogueSubsystem` — the single dialogue authority (WorldSubsystem)

`/Script/Sparta.DialogueSubsystem`, parent `/Script/Engine.WorldSubsystem`. Because it is a **WorldSubsystem**
there is exactly one per world — this is the object a co-op layer must drive/observe for dialogue.

| Member | Type | Offset |
|---|---|---|
| `OnDlgStarted` | `MulticastInlineDelegateProperty` | `0x30` |
| `OnDlgEnded` | `MulticastInlineDelegateProperty` | `0x40` |
| `SpawnedParticipants` | `ArrayProperty` (inner `ObjectProperty of /Script/Engine.Actor`) | `0x50` |
| `OnDlgNodeEnterEvent` | `MulticastInlineDelegateProperty` | `0x60` |
| `OnDlgNodeExitEvent` | `MulticastInlineDelegateProperty` | `0x70` |
| `OnAudioEventFinished` | `MulticastInlineDelegateProperty` | `0x80` |
| `OnDlgAudioStarted` | `MulticastInlineDelegateProperty` | `0x90` |
| `ActiveDialogues` | `ArrayProperty` (inner `StructProperty of /Script/Sparta.ActiveDialogue`) | `0xa0` |

Functions (full parameter lists):

| Function | Parameters (name: type) |
|---|---|
| `SpartaStartDialogue` | `Dialogue: ObjectProperty of /Script/DlgSystem.DlgDialogue` (0x0), `Participants: ArrayProperty (inner ObjectProperty)` (0x8), `Instigator: ObjectProperty of DialogueInstigatorComponent` (0x18), `AdditionalInterruptTags: StructProperty of GameplayTagContainer` (0x20), `AdditionalTags: StructProperty of GameplayTagContainer` (0x40), `AdditionalBlockingTags: StructProperty of GameplayTagContainer` (0x60), `ReturnValue: ObjectProperty of /Script/DlgSystem.DlgContext` (0x80) |
| `StopDialogue` | `Instigator: ObjectProperty of DialogueInstigatorComponent`, `bStopAudioImmediately: BoolProperty` |
| `StopActiveAudioOfContext` | `Context: ObjectProperty of /Script/DlgSystem.DlgContext` |
| `InterruptDialogues` | `Instigator: ObjectProperty of DialogueInstigatorComponent`, `Asset: ObjectProperty of /Script/DlgSystem.DlgDialogue`, `AdditionalInterruptTags: StructProperty of GameplayTagContainer` |
| `EndAllDialoguesWithTags` | `Tags: StructProperty of GameplayTagContainer` |
| `CheckTagConditions` | `Component: ObjectProperty of DialogueInstigatorComponent`, `Asset: ObjectProperty of /Script/DlgSystem.DlgDialogue`, `AdditionalTags: StructProperty of GameplayTagContainer`, `ReturnValue: BoolProperty` |
| `FindActiveDialogueForInstigator` | `Instigator: ObjectProperty of DialogueInstigatorComponent`, `ReturnValue: IntProperty` |
| `GetAllActiveDialoguesWithTag` | `Dialogues: ArrayProperty (inner StructProperty of ActiveDialogue)`, `InTag: StructProperty of GameplayTag`, `ReturnValue: BoolProperty` |
| `IsDialogueCurrentlyActive` | `InDialogue: ObjectProperty of /Script/DlgSystem.DlgDialogue`, `ReturnValue: BoolProperty` |
| `IsDialogueWithTagActive` | `DlgTag: StructProperty of GameplayTag`, `ReturnValue: BoolProperty` |
| `DialogueStarted` | `ActiveDlgIndex: IntProperty` |
| `DialogueEnded` | `Context: ObjectProperty of /Script/DlgSystem.DlgContext` |
| `OnSoundCompFinished` | `AudioComp: ObjectProperty of /Script/Engine.AudioComponent` |
| `OnWaveWeaverAudioEventFinished` | `EventInstance: ObjectProperty of /Script/WaveWeaver.AudioEventInstance` |

Delegates (signature objects resolved from `[df:]`):

| Delegate | Offset | Signature |
|---|---|---|
| `OnDlgStarted` | `0x30` | `/Script/Sparta.OnDialogueStartedEvent__DelegateSignature` → `Context: ObjectProperty of DlgContext` (0x0), `Instigator: ObjectProperty of DialogueInstigatorComponent` (0x8), `Tags: StructProperty of GameplayTagContainer` (0x10) |
| `OnDlgEnded` | `0x40` | same `/Script/Sparta.OnDialogueStartedEvent__DelegateSignature` (df address identical) |
| `OnDlgNodeEnterEvent` | `0x60` | `/Script/Sparta.OnDlgNodeEvent__DelegateSignature` → `Node: ObjectProperty of Dialogue_SpartaSpeechNode` (0x0), `ActiveDialogue: StructProperty of ActiveDialogue` (0x8) |
| `OnDlgNodeExitEvent` | `0x70` | same `OnDlgNodeEvent__DelegateSignature` |
| `OnAudioEventFinished` | `0x80` | same `OnDlgNodeEvent__DelegateSignature` (df address identical) |
| `OnDlgAudioStarted` | `0x90` | same `OnDlgNodeEvent__DelegateSignature` (df address identical) |

Evidence:

```
[00007FF46F032338] Class /Script/Sparta.DialogueSubsystem [n: 477F8] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7C138]
```

```
[000001F4ACD5C400] MulticastInlineDelegateProperty /Script/Sparta.DialogueSubsystem:OnDlgStarted [o: 30] [n: 586066] [c: 00007FF721DE9010] [owr: 00007FF46F032338] [df: 00007FF46F3CEE80]
[000001F4ACD5C480] MulticastInlineDelegateProperty /Script/Sparta.DialogueSubsystem:OnDlgEnded [o: 40] [n: 586060] [c: 00007FF721DE9010] [owr: 00007FF46F032338] [df: 00007FF46F3CEE80]
[000001F4ACD5C600] MulticastInlineDelegateProperty /Script/Sparta.DialogueSubsystem:OnDlgNodeEnterEvent [o: 60] [n: 58604A] [c: 00007FF721DE9010] [owr: 00007FF46F032338] [df: 00007FF46F3CF040]
```

```
[000001F4ACD5A580] MulticastInlineDelegateProperty /Script/Sparta.DialogueSubsystem:OnDlgNodeExitEvent [o: 70] [n: 586040] [c: 00007FF721DE9010] [owr: 00007FF46F032338] [df: 00007FF46F3CF040]
[000001F4ACD5A600] MulticastInlineDelegateProperty /Script/Sparta.DialogueSubsystem:OnAudioEventFinished [o: 80] [n: 586035] [c: 00007FF721DE9010] [owr: 00007FF46F032338] [df: 00007FF46F3CF040]
[000001F4ACD5A680] MulticastInlineDelegateProperty /Script/Sparta.DialogueSubsystem:OnDlgAudioStarted [o: 90] [n: 58602B] [c: 00007FF721DE9010] [owr: 00007FF46F032338] [df: 00007FF46F3CF040]
```

```
00007FF46F3CEE80	DelegateFunction	/Script/Sparta.OnDialogueStartedEvent__DelegateSignature
00007FF46F3D24F0	DelegateFunction	/Script/Sparta.SpartaAreaEvent__DelegateSignature
```

### 6.2 `ActiveDialogue` struct — the active-dialogue record

`/Script/Sparta.ActiveDialogue` (ScriptStruct, members from the ref query `qp-refs.sql`):

| Member | Type | Offset |
|---|---|---|
| `Instigator` | `ObjectProperty of /Script/Sparta.DialogueInstigatorComponent` | `0x0` |
| `DialogueContext` | `ObjectProperty of /Script/DlgSystem.DlgContext` | `0x8` |
| `DialogueAsset` | `ObjectProperty of /Script/DlgSystem.DlgDialogue` | `0x10` |
| `Tags` | `StructProperty of GameplayTagContainer` | `0x18` (dec 24) |
| `BlockingTags` | `StructProperty of GameplayTagContainer` | `0x38` (dec 56) |
| `AudioEventId` | `StructProperty of /Script/WaveWeaver.AudioEventId` | `0x58` (dec 88) |
| `SoundComp` | `ObjectProperty of /Script/Engine.AudioComponent` | `0x68` (dec 104) |
| `FacialAnimationInstance` | `ObjectProperty of /Script/Engine.AnimInstance` | `0x70` (dec 112) |
| `FacialAnimationMontage` | `ObjectProperty of /Script/Engine.AnimMontage` | `0x78` (dec 120) |

Evidence — the struct row, then its members verbatim:

```
[00007FF46F3CF120] ScriptStruct /Script/Sparta.ActiveDialogue [n: 13661] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 0000000000000000]
```

```
[000001F4A6CB1500] ObjectProperty /Script/Sparta.ActiveDialogue:Instigator [o: 0] [n: 512050] [c: 00007FF721DE9420] [owr: 00007FF46F3CF120] [pc: 00007FF46F031B38]
[000001F4A6CB1580] ObjectProperty /Script/Sparta.ActiveDialogue:DialogueContext [o: 8] [n: 9D10] [c: 00007FF721DE9420] [owr: 00007FF46F3CF120] [pc: 00007FF46F013938]
[000001F4A6CB1600] ObjectProperty /Script/Sparta.ActiveDialogue:DialogueAsset [o: 10] [n: 52550B] [c: 00007FF721DE9420] [owr: 00007FF46F3CF120] [pc: 00007FF46F013B38]
```

```
[000001F4A6CB1680] StructProperty /Script/Sparta.ActiveDialogue:Tags [o: 18] [n: 4CBA34] [c: 00007FF721DF9890] [owr: 00007FF46F3CF120] [ss: 00007FF46F388FE8]
[000001F4A6CB1700] StructProperty /Script/Sparta.ActiveDialogue:BlockingTags [o: 38] [n: 525504] [c: 00007FF721DF9890] [owr: 00007FF46F3CF120] [ss: 00007FF46F388FE8]
[000001F4A6CB1780] StructProperty /Script/Sparta.ActiveDialogue:AudioEventId [o: 58] [n: 11F83] [c: 00007FF721DF9890] [owr: 00007FF46F3CF120] [ss: 00007FF46F3B2558]
```

### 6.3 `DialogueInstigatorComponent`, `SpartaDialogueActor`, `SpartaDlgParticipant`, `DialogueNodeData`

| Class | Parent | Members (name : type @ offset) |
|---|---|---|
| `/Script/Sparta.DialogueInstigatorComponent` | `/Script/Engine.ActorComponent` | `DlgParticipantName: StructProperty of /Script/DlgSystem.DlgParticipantName @0xb8`, `ParticipantDisplayName: TextProperty @0xc0`, `AdditionalDlgTags: GameplayTagContainer @0xd0`, `AdditionalDlgCancelTags @0xf0`, `AdditionalBlockingTags @0x110` |
| `/Script/Sparta.SpartaDialogueActor` | `/Script/Engine.Actor` | `Mesh: ObjectProperty of SkeletalMeshComponent @0x2b0`, `CapsuleComponent @0x2b8`, `AudioComponentMapInitialized: MulticastInlineDelegateProperty @0x2c0`, `GameplayTagToAudioComponentMap: MapProperty (key GameplayTag, value ObjectProperty of SpartaCharacterAudioComponent) @0x2d0`, `AudioData: ObjectProperty of SpartaCharacterAudioData @0x320` |
| `/Script/Sparta.DialogueSkeletalMeshActor` | `/Script/Sparta.SpartaSkeletalMeshActor` | (no own members) |
| `/Script/Sparta.SpartaDlgParticipant` | `/Script/CoreUObject.Object` | `ParticipantName: NameProperty @0x30`, `DisplayName: TextProperty @0x38` |
| `/Script/Sparta.DialogueNodeData` | `/Script/DlgSystem.DlgNodeData` | `FacialAnimation: ObjectProperty of /Script/Engine.AnimSequence @0x28` |
| `/Script/Sparta.Dialogue_SpartaSpeechNode` | `/Script/DlgSystem.DlgNode_Custom` | see table below |
| `/Script/Sparta.DlgBlueprintLibrary` | `/Script/Engine.BlueprintFunctionLibrary` | `MigrateSubtitleSpeakerNamesToStringTables(Dialogue: DlgDialogue, StringTableIds: ArrayProperty of NameProperty, bOverwriteExistingSubtitleSpeakerText: BoolProperty, ReturnValue: IntProperty)` |

`Dialogue_SpartaSpeechNode` members (the node type that carries the actual spoken line):

| Member | Type | Offset |
|---|---|---|
| `bIsVirtualParent` | `BoolProperty` | `0x98` |
| `bVirtualParentFireDirectChildEnterEvents` | `BoolProperty` | `0x99` |
| `Text` | `TextProperty` | `0xa0` |
| `TextArguments` | `ArrayProperty of TextArguments` | `0xb0` |
| `SpeakerState` | `NameProperty` | `0xc0` |
| `NodeData` | `ObjectProperty of /Script/DlgSystem.DlgNodeData` | `0xc8` |
| `SoundWave` | `ObjectProperty of /Script/Engine.SoundWave` | `0xd0` |
| `GenericData` | `ObjectProperty` | `0xd8` |
| `AudioEvent` | `ObjectProperty of /Script/WaveWeaver.AudioEvent` | `0xf8` |
| `FacialAnimation` | `ObjectProperty of /Script/Engine.AnimSequence` | `0x100` |
| `bAutoProceedToNextNode` | `BoolProperty` | `0x108` |
| `AutoProceedDelayTimer` | `FloatProperty` | `0x10c` |
| `bAutoGenerateOptions` | `BoolProperty` | `0x110` |
| `bUseCustomSubtitleName` | `BoolProperty` | `0x111` |
| `SubtitleSpeakerName` | `NameProperty` | `0x114` |
| `SubtitleSpeakerText` | `TextProperty` | `0x120` |

Its functions: `IsVirtualParent()`, `GetSubtitleSpeakerText()`, `GetSubtitleSpeakerName()`,
`GetNodeSoundWave()`, `GetNodeAudioEvent()` (all no parameters except `ReturnValue`).

`DialogueInstigatorComponent` functions: `StopDialogue(bStopAudioImmediately: BoolProperty)`,
`InterruptDialogue(Interrupter: ObjectProperty of DialogueInstigatorComponent)`,
`ReceiveInterruptDialogue(Interrupter: ObjectProperty of DialogueInstigatorComponent)`.

Evidence:

```
[00007FF46F031B38] Class /Script/Sparta.DialogueInstigatorComponent [n: 477B4] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F031D38] Class /Script/Sparta.DialogueNodeData [n: 477D4] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F015138]
[00007FF46F031F38] Class /Script/Sparta.DialogueSkeletalMeshActor [n: 477DD] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F032138]
```

```
[00007FF46F032538] Class /Script/Sparta.Dialogue_SpartaSpeechNode [n: 478A2] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F015538]
[00007FF46F032938] Class /Script/Sparta.DlgBlueprintLibrary [n: 478E8] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE70138]
[00007FF46F045F38] Class /Script/Sparta.SpartaDialogueActor [n: 49007] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
```

```
[00007FF46F046138] Class /Script/Sparta.SpartaDlgParticipant [n: 49012] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
```

### 6.4 DlgSystem plugin (third-party dialogue graph) — the runtime half

| Class | Parent | Members (name : type @ offset) |
|---|---|---|
| `/Script/DlgSystem.DlgDialogue` | `/Script/CoreUObject.Object` | `DialogueTags: GameplayTagContainer @0x28`, `CancelDialogueTags @0x48`, `BlockingDialogueTags @0x68`, `Version: IntProperty @0x88`, `Name: NameProperty @0x8c`, `Guid: StructProperty of CoreUObject.Guid @0x94`, `ParticipantsClasses: ArrayProperty (inner ClassProperty) @0xa8`, `ParticipantsData: MapProperty @0xb8`, `AllSpeakerStates: SetProperty @0x108`, `StartNode: ObjectProperty of DlgNode @0x158`, `StartNodes: ArrayProperty (inner ObjectProperty of DlgNode) @0x160`, `Nodes: ArrayProperty (inner ObjectProperty of DlgNode) @0x170`, `NodesGUIDToIndexMap: MapProperty @0x180` |
| `/Script/DlgSystem.DlgContext` | `/Script/DlgSystem.DlgObject` | `OnDialogueEnded: MulticastInlineDelegateProperty @0x28`, `Dialogue: ObjectProperty of DlgDialogue @0x38`, `SerializedParticipants: ArrayProperty @0x40`, `Participants: MapProperty @0x50` |
| `/Script/DlgSystem.DlgManager` | `/Script/Engine.BlueprintFunctionLibrary` | 40 members, functions only (plus `SetDialogueHistory`, `SetDialoguePersistentWorldContextObject`) |
| `/Script/DlgSystem.DlgNode` | `/Script/CoreUObject.Object` | base node; `DlgNode_Speech`, `DlgNode_SpeechSequence`, `DlgNode_Selector`, `DlgNode_Proxy`, `DlgNode_End`, `DlgNode_Custom` are subclasses |
| `/Script/DlgSystem.DlgNodeData` | `/Script/DlgSystem.DlgObject` | per-node payload (`Sparta.DialogueNodeData` extends it) |
| `/Script/DlgSystem.DlgConditionCustom` / `DlgEventCustom` / `DlgTextArgumentCustom` | `DlgObject` | extension points — the game uses both heavily (see §6.6) |
| `/Script/Sparta.Dialogue_SpartaSpeechNode` | `/Script/DlgSystem.DlgNode_Custom` | the game's speech node (§6.3) |

`DlgContext` is the per-conversation state machine — the object to replicate. Its key query/mutate functions
(full lists, temps filtered):

| Function | Parameters (name: type) |
|---|---|
| `ChooseOption` | `OptionIndex: IntProperty`, `ReturnValue: BoolProperty` |
| `ChooseOptionFromAll` | `Index: IntProperty`, `ReturnValue: BoolProperty` |
| `ChooseSpeechSequenceOptionFromReplicated` | `OptionIndex: IntProperty`, `ReturnValue: BoolProperty` |
| `ChooseChild` | `OptionIndex: IntProperty`, `ReturnValue: BoolProperty` |
| `ReevaluateOptions` / `ReevaluateChildren` | `ReturnValue: BoolProperty` |
| `IsValidOptionIndex` / `IsValidNodeIndex` / `IsValidAllOptionIndex` / `IsOptionSatisfied` | `Index`/`OptionIndex`/`NodeIndex`: IntProperty, `ReturnValue: BoolProperty` |
| `IsValidNodeGUID` | `NodeGuid: StructProperty of Guid`, `ReturnValue: BoolProperty` |
| `IsOptionConnectedToVisitedNode` | `Index: IntProperty`, `bLocalHistory: BoolProperty`, `bIndexSkipsUnsatisfiedEdges: BoolProperty`, `ReturnValue: BoolProperty` |
| `IsOptionConnectedToEndNode` | `Index: IntProperty`, `bIndexSkipsUnsatisfiedEdges: BoolProperty`, `ReturnValue: BoolProperty` |
| `IsNodeVisited` | `NodeIndex: IntProperty`, `NodeGuid: StructProperty of Guid`, `bLocalHistory: BoolProperty`, `ReturnValue: BoolProperty` |
| `WasNodeIndexVisitedInThisContext` / `WasNodeGUIDVisitedInThisContext` | `NodeIndex`/`NodeGuid`, `ReturnValue` |
| `HasDialogueEnded` | `ReturnValue: BoolProperty` |
| `GetVisitedNodeIndices` / `GetVisitedNodeGUIDs` | `ReturnValue: SetProperty` |
| `GetActiveNodeIndex` | `ReturnValue: IntProperty` |
| `GetActiveNodeGUID` | `ReturnValue: StructProperty of Guid` |
| `GetActiveNodeText` | `ReturnValue: TextProperty` |
| `GetActiveNodeSpeakerState` | `ReturnValue: NameProperty` |
| `GetActiveNodeParticipantName` | `ReturnValue: NameProperty` |
| `GetActiveNodeParticipant` | `ReturnValue: ObjectProperty` |
| `GetActiveNodeData` | `ReturnValue: ObjectProperty of DlgNodeData` |
| `GetActiveNodeGenericData` | `ReturnValue: ObjectProperty` |
| `GetActiveNodeSound` | `ReturnValue: ObjectProperty of SoundBase` |
| `GetActiveNodeVoiceDialogueWave` | `ReturnValue: ObjectProperty of DialogueWave` |
| `GetActiveNodeParticipantIcon` | `ReturnValue: ObjectProperty of Texture2D` |
| `GetActiveNodeParticipantDisplayName` | `ReturnValue: TextProperty` |
| `GetMutableActiveNode` / `GetMutableActiveNodeAsSpeechSequence` | `ReturnValue: ObjectProperty of DlgNode` / `DlgNode_SpeechSequence` |
| `GetOptionsNum` / `GetAllOptionsNum` | `ReturnValue: IntProperty` |
| `GetOption` | `OptionIndex: IntProperty`, `ReturnValue: StructProperty of /Script/DlgSystem.DlgEdge` |
| `GetOptionFromAll` | `Index: IntProperty`, `ReturnValue: StructProperty of /Script/DlgSystem.DlgEdgeData` |
| `GetOptionText` | `OptionIndex: IntProperty`, `ReturnValue: TextProperty` |
| `GetOptionSpeakerState` | `OptionIndex: IntProperty`, `ReturnValue: NameProperty` |
| `GetOptionsArray` / `GetAllOptionsArray` | `ReturnValue: ArrayProperty` |
| `GetParticipantsMap` / `GetParticipants` | `ReturnValue: MapProperty` |
| `GetParticipantFromName` | `Participant: StructProperty of DlgParticipantName`, `ReturnValue: ObjectProperty` |
| `GetMutableParticipant` | `ParticipantName: NameProperty`, `ReturnValue: ObjectProperty` |
| `GetNodeIndexForGUID` | `NodeGuid: StructProperty of Guid`, `ReturnValue: IntProperty` |
| `GetNodeGUIDForIndex` | `NodeIndex: IntProperty`, `ReturnValue: StructProperty of Guid` |
| `GetDialoguePathName` | `ReturnValue: StrProperty` |
| `GetDialogueName` | `ReturnValue: NameProperty` |
| `GetDialogueGUID` | `ReturnValue: StructProperty of Guid` |
| `GetDialogue` | `ReturnValue: ObjectProperty of DlgDialogue` |
| `GetContextString` | `ReturnValue: StrProperty` |
| `OnRep_SerializedParticipants` | (none) |

`DlgManager` (BlueprintFunctionLibrary) entry points — the game-start path is `DialogueSubsystem::SpartaStartDialogue`,
but DlgSystem also exposes:

| Function | Parameters (name: type) |
|---|---|
| `StartDialogue` | `Dialogue: ObjectProperty of DlgDialogue`, `Participants: ArrayProperty (inner ObjectProperty)`, `ReturnValue: ObjectProperty of DlgContext` |
| `StartDialogue2/3/4` | `Dialogue` + `Participant0..1/2/3`, `ReturnValue: ObjectProperty of DlgContext` |
| `StartDialogueWithDefaultParticipants` | `WorldContextObject: ObjectProperty`, `Dialogue: ObjectProperty of DlgDialogue`, `ReturnValue: ObjectProperty of DlgContext` |
| `StartMonologue` | `Dialogue: ObjectProperty of DlgDialogue`, `Participant: ObjectProperty`, `ReturnValue: ObjectProperty of DlgContext` |
| `ResumeDialogueFromNodeIndex` | `Dialogue: DlgDialogue`, `Participants: ArrayProperty`, `StartIndex: IntProperty`, `AlreadyVisitedNodes: SetProperty`, `bFireEnterEvents: BoolProperty`, `ReturnValue: ObjectProperty of DlgContext` |
| `ResumeDialogueFromNodeGUID` | `Dialogue: DlgDialogue`, `Participants: ArrayProperty`, `StartNodeGUID: StructProperty of Guid`, `AlreadyVisitedNodes: SetProperty`, `bFireEnterEvents: BoolProperty`, `ReturnValue: ObjectProperty of DlgContext` |
| `SetDialogueHistory` | `DlgHistory: MapProperty` |
| `SetDialoguePersistentWorldContextObject` | `WorldContextObject: ObjectProperty` |
| `GetDialogueWorld` | `ReturnValue: ObjectProperty of /Script/Engine.World` |
| `RegisterDialogueConsoleCommands` / `UnregisterDialogueConsoleCommands` | `ReturnValue: BoolProperty` |
| `GetObjectsWithDialogueParticipantInterface` | `WorldContextObject: ObjectProperty`, `ReturnValue: ArrayProperty` |

`DlgDialogue` has ~50 getters (`GetDlgTags`, `GetCancelDlgTags`, `GetBlockingTags`, `GetDialogueName`,
`GetDialogueFName`, `GetDialogueVersion`, `GetGUID`, `GetNodes`, `GetStartNodes`/`GetMutableStartNodes`,
`GetNodeIndexForGUID`, `GetNodeGUIDForIndex`, `GetMutableNodeFromIndex`, `GetMutableNodeFromGUID`,
`GetParticipantClasses`, `GetParticipantsNum`, `GetParticipantsData`, `GetAllSpeakerStates`, and
`GetParticipant{Bool,Int,Float,FName,Event,Condition,CustomTextArgument,CustomEvent,CustomCondition}Names`
plus `Get...Names(ParticipantName, Outset: SetProperty)` out-param variants).

Evidence — parent resolution proof that `DialogueNodeData` extends `DlgSystem.DlgNodeData` (`00007FF46F015138`)
and `Dialogue_SpartaSpeechNode` extends `DlgSystem.DlgNode_Custom` (`00007FF46F015538`):

```
[00007FF46F031D38] Class /Script/Sparta.DialogueNodeData [n: 477D4] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F015138]
[00007FF46F032538] Class /Script/Sparta.Dialogue_SpartaSpeechNode [n: 478A2] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F015538]
```

```
[00007FF46F013338] Class /Script/DlgSystem.DlgConditionCustom [n: 4552D] [c: 00007FF46EE55138] [or: 00007FF46F17C548] [sps: 00007FF46F013538]
[00007FF46F013938] Class /Script/DlgSystem.DlgContext [n: 4554E] [c: 00007FF46EE55138] [or: 00007FF46F17C548] [sps: 00007FF46F013538]
[00007FF46F013B38] Class /Script/DlgSystem.DlgDialogue [n: 457ED] [c: 00007FF46EE55138] [or: 00007FF46F17C548] [sps: 00007FF46EE50070]
```

```
[00007FF46F013F38] Class /Script/DlgSystem.DlgEventCustom [n: 459E1] [c: 00007FF46EE55138] [or: 00007FF46F17C548] [sps: 00007FF46F013538]
[00007FF46F014D38] Class /Script/DlgSystem.DlgManager [n: 45A55] [c: 00007FF46EE55138] [or: 00007FF46F17C548] [sps: 00007FF46EE70138]
```

### 6.5 Dialogue assets — countable, 977 `DlgDialogue` objects

`SELECT COUNT(*) … WHERE type='DlgDialogue'` → **977**, of which 1 is the CDO
`/Script/DlgSystem.Default__DlgDialogue`, leaving **976 real dialogue assets**.

Distribution by folder (exact counts; `scripts\q\qp-dlgcount.sql` + a Python folder grouping over all 977 rows):

| Folder | Count |
|---|---|
| `/Game/Sparta/Dialogue/Dialogues/ShellMemories` | 367 |
| `/Game/Sparta/Dialogue/Dialogues/Egon` | 115 |
| `/Game/Sparta/Dialogue/Dialogues/HUB` | 65 |
| `/Game/Sparta/Dialogue/Dialogues/FrogMomma` | 56 |
| `/Game/Sparta/Dialogue/Dialogues/Vlas` | 50 |
| `/Game/Sparta/Dialogue/Dialogues/Gorf` | 45 |
| `/Game/Sparta/Dialogue/Dialogues/Blacksmith` | 35 |
| `/Game/Sparta/Dialogue/Dialogues/Merchant` | 31 |
| `/Game/Sparta/Dialogue/Dialogues/Ruk` | 29 |
| `/Game/Sparta/Dialogue/Dialogues/Genessa` | 20 |
| `/Game/Sparta/Dialogue/Dialogues/Tavern` | 17 |
| `/Game/Sparta/Dialogue/Dialogues/Intro` | 16 |
| `/Game/Sparta/Dialogue/Dialogues/ImmortalBrigand` | 15 |
| `/Game/Sparta/Dialogue/Dialogues/Shellkeeper` | 15 |
| `/Game/Sparta/Dialogue/Dialogues/UnderMether` | 11 |
| `/Game/Sparta/Dialogue/Dialogues/GrishaHunter` | 8 |
| `/Game/Sparta/Dialogue/Dialogues/Thestus` | 7 |
| `/Game/Sparta/Dialogue/Dialogues/Villagers` | 7 |
| `/Game/Sparta/Dialogue/Dialogues/Grisha` | 7 |
| `/Game/Sparta/Dialogue/Dialogues/Baghead` | 6 |
| `/Game/Sparta/Dialogue/Blueprints/ShellMemories` | 5 |
| `/Game/Sparta/Dialogue/Dialogues/Gragu` | 5 |
| `/Game/Sparta/Dialogue/Dialogues/Brigands` | 4 |
| `/Game/Sparta/Dialogue/Blueprints/Tutorials` | 3 |
| `/Game/Sparta/Dialogue/Dialogues/Tarforge` | 3 |
| `/Game/Sparta/Dialogue/Dialogues/CrazedHorde` | 2 |
| `/Game/Sparta/Dialogue/Dialogues/Wormfish` | 1 |
| `/Game/Sparta/Dialogue/Dialogues/Dog` | 1 |
| `/Game/Sparta/Maps/Playgrounds/NarrativeEvents` | 1 |
| `/Game/Developers/rscha` | 29 (developer test dialogues) |
| `/Script/DlgSystem` | 1 — the CDO `Default__DlgDialogue` (not an asset) |

Example asset paths (verbatim from the dump):

```
/Game/Sparta/Dialogue/Dialogues/Baghead/Dlg_Baghead_Interact_MoonshineQuestStart.Dlg_Baghead_Interact_MoonshineQuestStart
/Game/Sparta/Dialogue/Dialogues/Blacksmith/Dlg_Blacksmith_MasterTree_Greeting.Dlg_Blacksmith_MasterTree_Greeting
/Game/Sparta/Dialogue/Blueprints/Tutorials/DLG_Undermether_Tutorial_Sidearm.DLG_Undermether_Tutorial_Sidearm
/Game/Sparta/Dialogue/Blueprints/ShellMemories/Genessa/Dlg_Genessa_Memory_TheAscension_1_Intro.Dlg_Genessa_Memory_TheAscension_1_Intro
```

Evidence (raw `DlgDialogue` asset rows — proves the asset class and that nodes are `Dialogue_SpartaSpeechNode` children):

```
[000001F91BD35400] DlgDialogue /Game/Sparta/Dialogue/Dialogues/Baghead/Dlg_Baghead_Interact_MoonshineQuestStart.Dlg_Baghead_Interact_MoonshineQuestStart [n: 318FD3] [c: 00007FF46F013B38] [or: 000001F9134B8BC0]
[000001F91C039980] Dialogue_SpartaSpeechNode /Game/Sparta/Dialogue/Dialogues/Baghead/Dlg_Baghead_Interact_MoonshineQuestStart.Dlg_Baghead_Interact_MoonshineQuestStart:Dialogue_SpartaSpeechNode_0 [n: 478A2] [c: 00007FF46F032538] [or: 000001F91BD35400]
```

```
[000001F91BD36600] DlgDialogue /Game/Sparta/Dialogue/Blueprints/Tutorials/DLG_Undermether_Tutorial_Sidearm.DLG_Undermether_Tutorial_Sidearm [n: C2E1D] [c: 00007FF46F013B38] [or: 000001F9132BC590]
```

Evidence — the quest DataTable package + asset rows (verbatim):

```
[000001F4D304F740] Package /Game/Sparta/Dialogue/QuestSystem/DT_QuestData1 [n: 48E9D0] [c: 00007FF46EE55338] [or: 0000000000000000]
[000001F4C48024B0] DataTable /Game/Sparta/Dialogue/QuestSystem/DT_QuestData1.DT_QuestData1 [n: 29EC82] [c: 00007FF46EF31938] [or: 000001F4D304F740]
```

### 6.6 Dialogue conditions and events actually used (DlgSystem extension points)

Blueprint `DlgConditionCustom` subclasses under `/Game/Sparta/Dialogue/Blueprints/DialogueConditions/`:

`BP_DC_CheckBossDefeated`, `BP_DC_CheckEgonMood`, `BP_DC_CheckForSpecificShellUnlocked`,
`BP_DC_CheckPlayerGold`, `BP_DC_DoesNotHaveItem`, `BP_DC_HasItem`, `BP_DC_NarrativeTagCheck`,
`BP_DC_NarrativeTagCheck_DoesNotHaveTag`, `BP_DC_PlayerNotShellCheck`, `BP_DC_PlayerShellCheck`,
`BP_DC_PlayerWeaponCheck`.

Blueprint `DlgEventCustom` subclasses under `/Game/Sparta/Dialogue/Blueprints/DialogueEvents/` (31 total):
`BP_DE_ActivatePlayerAbility`, `BP_DE_AddGameplayTag`, `BP_DE_AddNarrativeTag`, `BP_DE_AddTagByCharacterID`,
`BP_DE_BlockMovement`, `BP_DE_BlockPlayerInput`, `BP_DE_BroadcastGlobalEvent`, `BP_DE_GainItem`,
`BP_DE_GiveGameplayAbility`, `BP_DE_GiveSeal`, `BP_DE_GiveSpecificItem`, `BP_DE_GiveWeaponMap`,
`BP_DE_LoseItem`, `BP_DE_OpenDialogueAsset`, `BP_DE_PlayFacialAnimation`, `BP_DE_PlayMontageFromCollection`,
`BP_DE_PlayMontageNPC`, `BP_DE_PlayMontageNPCOfClass`, `BP_DE_PlayMontageOnParticipant`,
`BP_DE_PlayMontageOnParticipant_WithFollowUpMontage`, `BP_DE_PlaySound`, `BP_DE_PlayerEmote`,
`BP_DE_RemoveGameplayTag`, `BP_DE_RevealSidearmObjective`, `BP_DE_RevealWeaponObjective`,
`BP_DE_SendEventToCustomParticipant`, `BP_DE_SendEventToParticipant`, `BP_DE_SetCameraView`,
`BP_DE_SetPlayerWalk`, `BP_DE_UnblockMovement`, `BP_DE_UnblockPlayerInput`.

Evidence (the two DlgSystem extension-point base classes the game's DC/DE blueprints derive from):

```
[00007FF46F013338] Class /Script/DlgSystem.DlgConditionCustom [n: 4552D] [c: 00007FF46EE55138] [or: 00007FF46F17C548] [sps: 00007FF46F013538]
[00007FF46F013F38] Class /Script/DlgSystem.DlgEventCustom [n: 459E1] [c: 00007FF46EE55138] [or: 00007FF46F17C548] [sps: 00007FF46F013538]
```

### 6.7 Dialogue-enabled interactables and cinematic driver

| Class | Parent | Notes |
|---|---|---|
| `/Game/Sparta/Dialogue/Blueprints/Interactables/BP_InteractableWithDialogue.BP_InteractableWithDialogue_C` | `/Script/Engine.Actor` | has `OnDialogueEnded__DelegateSignature` binding and `OnQuestSelected__DelegateSignature` |
| `/Game/Sparta/Dialogue/Blueprints/Interactables/BP_Interactable.BP_Interactable_C` | `/Script/Engine.Actor` | `OnQuestSelected__DelegateSignature` |
| `/Game/Sparta/Dialogue/Blueprints/BP_NarrativeEvent.BP_NarrativeEvent_C` | `/Game/Sparta/Dialogue/Blueprints/BP_ActorSequence.BP_ActorSequence_C` | `OnEventEnded__DelegateSignature` |
| `/Game/Sparta/Dialogue/Blueprints/BP_ActorSequence.BP_ActorSequence_C` | `/Script/Engine.Actor` | base for narrative/memory sequences |
| `/Game/Sparta/Dialogue/Blueprints/BPC_DialogueInstigator.BPC_DialogueInstigator_C` | `/Script/Sparta.DialogueInstigatorComponent` | adds `OnDialogueStarted__DelegateSignature`, `OnDialogueEnded__DelegateSignature` |
| `/Game/Sparta/Dialogue/Blueprints/BPI_ObjectiveTarget.BPI_ObjectiveTarget_C` | `/Script/CoreUObject.Interface` | objective-target interface |
| `/Game/Sparta/Dialogue/NPCs/…` | various | one `BP_NPC_*` per named NPC (`BP_NPC_Genessa_Ghost`, `BP_NPC_GenessaTertius_TheCenser_GM3_S1`, …) |
| `/Game/Sparta/Core/AI/BP_DialogueActor.BP_DialogueActor_C` | `/Script/Sparta.SpartaDialogueActor` | 10 subclasses: `…_BagheadCultist_1/2/3`, `…_Brigand`, `…_BrigandDepraved`, `…_BrigandElite`, `…_BrigandKnightly`, `…_BrigandRanged`, `…_BrigandRegal` |

---

## 7. Narrative tags and the narrative-events tracker

The story flag system is a **GameplayTagContainer inside the quest-log save object**, not a bespoke table:

* Runtime: `BPC_QuestLogComponent_C:NarrativeTags: FGameplayTagContainer @0x258`, mutated by
  `AddNarrativeTag(Tag: FGameplayTag, SaveToDisk: bool)` / `RemoveNarrativeTag(Tag, SaveToDisk)` and
  reloaded by `LoadNarrativeTags()` which casts the save component's runtime data to
  `BPSO_QuestLogComponent_C` and reads its `NarrativeTags`.
* Persisted: `BPSO_QuestLogComponent_C:NarrativeTags: FGameplayTagContainer @0xc0` (its only authored field).
* Read by dialogue via `BP_DC_NarrativeTagCheck` / `BP_DC_NarrativeTagCheck_DoesNotHaveTag`, by level selection
  via `Con_CheckNarrativeTags` (`/Game/Sparta/Core/World/Levels/Options/Conditions/Con_CheckNarrativeTags.Con_CheckNarrativeTags_C`,
  parent `/Script/CSOptionSet.CSOptionCondition`), and by item fragments via
  `/Game/Sparta/Core/Items/IF_AddNarrativeTag.IF_AddNarrativeTag_C`.
* Written by world triggers via `/Game/Sparta/…/BP_Trigger_AddNarrativeTag` and by dialogue events via
  `BP_DE_AddNarrativeTag`.
* `ResetForNewGamePlus(NarrativeTagsToCheckRetain, RetainedNarrativeTags)` removes all but a whitelist.

Narrative *event* timing is tracked separately:

| Class / struct | Parent | Members |
|---|---|---|
| `/Game/Sparta/Dialogue/QuestSystem/BPC_NarrativeEventsTracker.BPC_NarrativeEventsTracker_C` | `/Script/Sparta.SpartaSaveComponent` | functions `AddEventTimeTracker`, `FindEventTrackedTime`, `GetMySaveData`, `InitEventsTimeTracker`, `RemoveEventTimeTracker`, `RefreshEventsTimeTracker`, `SaveLoaded`, `ToggleDebug`, `OnTrackerRefresh__DelegateSignature` |
| `/Game/Sparta/Dialogue/QuestSystem/BPSO_NarrativeEventsTracker.BPSO_NarrativeEventsTracker_C` | `/Script/Sparta.SpartaSaveObject` | `NarrativeEventTimeTracking: TMap<FGameplayTag, FF_NarrativeEventFloatTracking> @0xb8` (from `BPSO_NarrativeEventsTracker.hpp`) |
| `/Game/Sparta/Dialogue/QuestSystem/F_NarrativeEventFloatTracking` | Blueprint struct (layout only in `F_NarrativeEventFloatTracking.hpp`) | `IsTracking: bool @0x0`, `InitialTime: double @0x8`, `TrackedFloat: double @0x10` |
| — | `F_NarrativeEvents.hpp` (`FF_NarrativeEvents`) | `EventName: FText @0x0`, `Part: FText @0x10`, `Reward: double @0x20`, `EventActor: TSoftClassPtr<ABP_NarrativeEvent_C> @0x28` |
| `/Game/Sparta/Core/Characters/Behaviors/…`, `/Game/Sparta/Characters/NPCs/…` | — | narrative-event actors (`BP_NarrativeEvent_*`, `BP_GrishaEvent_01/02`) |

Evidence:

```
[000001F61C359FD0] BlueprintGeneratedClass /Game/Sparta/Dialogue/QuestSystem/BPC_NarrativeEventsTracker.BPC_NarrativeEventsTracker_C [n: 173982] [c: 00007FF46EEC3D38] [or: 000001F4C5865FB0] [sps: 00007FF46F038738]
[000001F4BE032320] BlueprintGeneratedClass /Game/Sparta/Dialogue/QuestSystem/BPSO_NarrativeEventsTracker.BPSO_NarrativeEventsTracker_C [n: 29AE34] [c: 00007FF46EEC3D38] [or: 000001F4C41E3260] [sps: 00007FF46F038338]
```

```
[000001F4C41E32B0] Package /Game/Sparta/Dialogue/QuestSystem/F_NarrativeEventFloatTracking [n: 1CEB40] [c: 00007FF46EE55338] [or: 0000000000000000]
```

---

## 8. NPC state

| Class | Parent | Members (name : type @ offset) |
|---|---|---|
| `/Game/Sparta/Core/SaveObjects/BPSO_NPC.BPSO_NPC_C` | `/Script/Sparta.SpartaSaveObject` | `HasTriggeredInitialGreeting: bool @0xb8`, `WasEverKilled: bool @0xb9` (from `BPSO_NPC.hpp`) |
| `/Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C` | `/Script/Sparta.SpartaSaveObject` | `CachedItemsMap: TMap<TSoftClassPtr<USpartaItemDefinition>, int32> @0xb8`, `TotalGoldSpent: int32 @0x108`, `StolenItems: bool @0x10c` |
| `/Game/Sparta/Core/SaveObjects/BPSO_EridrimUnlockManager.BPSO_EridrimUnlockManager_C` | `/Script/Sparta.SpartaSaveObject` | `WardenJumped: bool @0xb8`, `WardenDefeated: bool @0xb9`, `ShellClaimed: bool @0xba` |
| `/Game/Sparta/Maps/Playgrounds/NarrativeEvents/MajorNarrativeEvents/EgonInteractions/Lazlo/BPSO_Egon.BPSO_Egon_C` | `/Script/Sparta.SpartaSaveObject` | (see `BPSO_Egon.hpp`) |
| `/Game/Sparta/Dialogue/Blueprints/DiscipleSystem/BPC_HubEventComponent.BPC_HubEventComponent_C` | `/Script/Sparta.HubEventComponent` | delegates `OnHubEventComponentLoaded__DelegateSignature`, `OnOvumCollected__DelegateSignature` |
| `/Game/Sparta/Dialogue/Blueprints/DiscipleSystem/BP_DiscipleSpawner.BP_DiscipleSpawner_C` | `/Script/Sparta.UnitSpawner` | hub-NPC spawner |
| `BPC_QuestLogComponent_C:NPCRelationships` | `TMap<FString,int32>` @`0x170` | per-NPC relationship counter (see §1.3) |
| `BPC_QuestLogComponent_C:PlayerChoice` | `FGameplayTag` @`0x1d0` | a single global "which branch did the player pick" tag |

NPC *dialogue participation* is expressed by `SpartaDlgParticipant` subclasses under
`/Game/Sparta/Dialogue/Blueprints/Participants/` (`BP_EredrimParticipant`, `BP_HadernParticipant`,
`BP_HarrosParticipant`, `BP_SolomonParticipant`, `BP_TielParticipant`, `BP_ChildVillager`,
`BP_ManagedParticipantExample`), all parented to `/Script/Sparta.SpartaDlgParticipant`.

Evidence:

```
[000001F4DCD21F90] BlueprintGeneratedClass /Game/Sparta/Core/SaveObjects/BPSO_NPC.BPSO_NPC_C [n: 2AB76A] [c: 00007FF46EEC3D38] [or: 000001F4CBCF06A0] [sps: 00007FF46F038338]
[000001F4CB2478A0] BlueprintGeneratedClass /Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C [n: 4BE016] [c: 00007FF46EEC3D38] [or: 000001F4A708E6B0] [sps: 00007FF46F038338]
[000001F8BD6A4DE0] BlueprintGeneratedClass /Game/Sparta/Core/SaveObjects/BPSO_EridrimUnlockManager.BPSO_EridrimUnlockManager_C [n: 132F57] [c: 00007FF46EEC3D38] [or: 000001F8BBEBB820] [sps: 00007FF46F038338]
```

---

## 9. Tutorials

| Class | Parent | Members (name : type @ offset) |
|---|---|---|
| `/Game/Sparta/UI/Tutorials/Blueprints/BPO_TutorialHandler.BPO_TutorialHandler_C` | `/Script/Sparta.SpartaWorldObject` | `MyCharacter: ObjectProperty of Character @0x28`, `TutorialList: ArrayProperty (inner SoftClassProperty) @0x30`, `bShouldActivate @0x40`, `MyTag: StructProperty of GameplayTag @0x44`, `OnComplete: MulticastInlineDelegateProperty @0x50`, `OnTriggerFail: MulticastInlineDelegateProperty @0x60`, `CanTrigger @0x70` |
| `/Game/Sparta/UI/Tutorials/Others/BPO_Tut_DLCs.BPO_Tut_DLCs_C` | `…BPO_TutorialHandler_C` | DLC-specific |
| `/Game/Sparta/UI/Core/Text/DT_TextStyle_Tutorials.DT_TextStyle_Tutorials` | `DataTable` | tutorial text styling |
| player-save side | `BP_SpartaPlayerSaveObject_C:ShownTutorials: TArray<FName> @0x640`, plus `BP_SpartaMetaSave_C:ShownMetaTutorials: TArray<FName> @0x80` | **the actual "which tutorial was shown" state** |

`BPO_TutorialHandler` functions: `BindToEvent()`, `UnbindFromEvent()`,
`RefreshState(Complete: BoolProperty)`, `ShouldShowTutorial(Tutorial: SoftClassProperty, ReturnValue: BoolProperty)`.

Evidence:

```
[000001F4EB407180] BlueprintGeneratedClass /Game/Sparta/UI/Tutorials/Blueprints/BPO_TutorialHandler.BPO_TutorialHandler_C [n: 4943CA] [c: 00007FF46EEC3D38] [or: 000001F4C7050F60] [sps: 00007FF46F060538]
[000001F4C05C3600] ObjectProperty /Game/Sparta/UI/Tutorials/Blueprints/BPO_TutorialHandler.BPO_TutorialHandler_C:MyCharacter [o: 28] [n: 637A08] [c: 00007FF721DE9420] [owr: 000001F4EB407180] [pc: 00007FF46EF37538]
[000001F4DCBF8E90] DataTable /Game/Sparta/UI/Core/Text/DT_TextStyle_Tutorials.DT_TextStyle_Tutorials [n: 4B28D1] [c: 00007FF46EF31938] [or: 000001F4CBCF0CE0]
```

---

## 10. Quick map of "who owns the story state" (for the sync engineer)

| State | Owner object | Persisted into | Sync scope suggestion |
|---|---|---|---|
| Quest definitions | `DT_QuestData1` (`QuestDetails`/`QuestStage`/`QuestObjectives` rows) | asset, read-only | not synced (static) |
| Active/completed quests, tracked quest, narrative tags, NPC relationships, narrative-event counters, player choice | `BPC_QuestLogComponent_C` (a `SpartaSaveComponent`; `LoadNarrativeTags` reaches its data via `GetSpartaPlayerCharacter` → `GetRuntimeData` → cast to `BPSO_QuestLogComponent_C`) | `BPSO_QuestLogComponent_C` (save object) | per-player authoritative object |
| Live quest instances + objective progress | `BP_QuestObject_C` (spawned, held in `ActiveQuests`) + `QuestObject.CurrentQuestStage/CurrentQuestObjectiveProgress` | recreated from `BPC_QuestLogComponent` on load | derived — replicate the quest-log, not the spawned objects |
| Map markers / area / objective reveal | `BPC_WorldMapObjective_C` per marker + `BPC_WorldMapHandler_C` per player | `BPSO_WordMapObjective_C`, `BPSO_WorldMap_C` | per-player widget state; beacon/objective *reveal* is per-player, `Completed` may need world sync (UNKNOWN which) |
| Active area + completed areas | `SpartaAreaSubsystem` (WorldSubsystem) | UNKNOWN (no save object found for it) | probably world-scope; verify in game |
| Run state integers, streamed level instances | `SpartaRunManagerSubsystem` (GameInstanceSubsystem) | UNKNOWN | world/run scope |
| Dialogue in flight | `DialogueSubsystem.ActiveDialogues` + `DlgContext` | none (transient) | drive from host; block local starts on clients |
| NPC persistent flags | `BPSO_NPC_C`, `BPSO_ShopData_C`, `BPSO_EridrimUnlockManager_C`, `BPSO_QuestLogComponent_C:NarrativeTags` | save objects | per-player or world — UNKNOWN (see part 2 §10) |
| Tutorial shown | `BP_SpartaPlayerSaveObject_C:ShownTutorials` | player save | per-player |

Continue in `20-quests-progression-part2.md`.
