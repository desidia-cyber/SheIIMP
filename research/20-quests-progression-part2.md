# 20 — Quests, story progression, dialogue, NPC state, save data, map/level structure — PART 2

Continuation of `20-quests-progression.md`. This part covers: the save-data architecture and every save object,
which fields are per-player vs per-world, the DataTable/DataAsset inventory, the map/level/streaming structure with
the full proven map-package list, all travel/teleport/level-load functions, console commands, and the UNKNOWN list.

Read the method/limitation table in part 1 §0 first — the same rules apply (compiler temporaries filtered,
DataTable row structs not recorded on the asset row, Blueprint user-defined structs only exist as `Package` rows).

---

## 9. Save data — classes, ownership, and persistence path

### 9.1 Class table

| Class | Parent | Members (name : type @ offset) | Role |
|---|---|---|---|
| `/Script/Sparta.SpartaSaveObject` | `/Script/CoreUObject.Object` | `PrimarySaveObjectTick: StructProperty of SaveObjectTickFunction @0x28`, `IgnoreSaveGameMetaSpecifier: BoolProperty @0x58`, `bIsSessionSaveData: BoolProperty @0xb1` | base of every save object |
| `/Script/Sparta.SpartaSessionSaveObjectBase` | `/Script/Sparta.SpartaSaveObject` | function `GetSessionStartTransform() → StructProperty of CoreUObject.Transform` | base of **session** save objects |
| `/Script/Sparta.SpartaSaveComponent` | `/Script/Engine.ActorComponent` | `SaveObjectClass: ClassProperty @0xc0`, `OnSaveLoaded @0xc8`, `OnPreSave @0xd8`, `OnSessionReset @0xe8` (delegates), `SaveProtocol: EnumProperty of ESaveObjectProtocol @0xf8`, `bApplyPrologueSkipPreset: BoolProperty @0xf9`, `Guid: StructProperty of CoreUObject.Guid @0xfc`, `RuntimeData: ObjectProperty of SpartaSaveObject @0x110`, `bSaveLoaded: BoolProperty @0x118` | the actor-side handle; **every "stateful" actor component the game has is a `SpartaSaveComponent`** |
| `/Script/Sparta.SpartaSaveInterface` | `/Script/CoreUObject.Interface` | — | `SubscribeSaveInterface(Object)` / `UnsubscribeSaveInterface(Object)` |
| `/Script/Sparta.SpartaSaveGame` | `/Script/Engine.SaveGame` | `ChangedActors: SetProperty @0x28`, `ActorFloats: MapProperty @0x78`, `ActorInts: MapProperty @0xc8`, `ActorNames: MapProperty @0x118`, `ActorBools: MapProperty @0x168`, `ActorObjectMap: MapProperty @0x1b8`, `ActorObjectMapAsString: StrProperty @0x208` | the **world state** save file (level-actor deltas) |
| `/Script/Sparta.SpartaMetaSave` | `/Script/Engine.SaveGame` | `ActiveSaveProfileSlot: IntProperty @0x28`, `ActiveSaveRecoverySlot: MapProperty (IntProperty→IntProperty) @0x30` | tiny meta file: which profile slot is active |
| `/Script/Sparta.SpartaSaveSubSystem` | `/Script/Engine.GameInstanceSubsystem` | 73 members, see §9.2 | **the save authority** |
| `/Script/Sparta.SpartaSaveGlobalSettings` | `/Script/DeveloperSettings.DeveloperSettings` | (settings container) | config, not state |
| `/Script/Sparta.PlayerSaveObject` | `/Script/Sparta.SpartaSaveObject` | `TotalPlayTimeSeconds: FloatProperty @0xb8`, `LastCachedCurrentTime: DoubleProperty @0xc0`, `LastSaveDateTime: DateTime @0xc8`, `CompletedSpawnTriggerRecipes: MapProperty @0xd0`, `CompletetedSpawnTriggers: SetProperty @0x120`, `ShellStats: MapProperty @0x170`, `WeaponStats: MapProperty @0x1c0`, `PlayerStats: StructProperty of PermanentStats @0x210`, `GameLibraryData: MapProperty @0x260`, `OverrideWorldGenTags: GameplayTagContainer @0x2b0`, `Difficulty: MapProperty @0x2d0` | **per-player** persistent object |
| `/Script/Sparta.DataLayersSave` | `/Script/Sparta.SpartaSaveObject` | `SavedDataLayerStates: MapProperty (NameProperty→EnumProperty of /Script/Engine.EDataLayerRuntimeState) @0xb8` | persistent **data-layer** state |
| `/Script/Sparta.PlayerProgressionSaveData` | `/Script/Sparta.SpartaSaveObject` | `SkillLevels @0xb8`, `ShellBuilds @0x108`, `TierBuilds @0x158` | per-player progression |
| `/Script/Sparta.PPItemsSaveData`, `/Script/Sparta.TarstoneSaveData` | `/Script/Sparta.SpartaSaveObject` | (item-side; other agent's area) | per-player |

`SpartaSaveObject` functions (full lists):

| Function | Parameters (name: type) |
|---|---|
| `UpdateFrom` | `SaveObject: ObjectProperty of SpartaSaveObject`, `Events: StructProperty of GameplayTagContainer`, `ReturnValue: BoolProperty` |
| `ReceiveSpartaPreSave` | `SaveContext: StructProperty of /Script/Sparta.SpartaSaveContext` |
| `ReceiveOnLoaded` | (none) |
| `ReceiveApplySkipProloguePreset` | (none) |
| `ReceiveTick` | `DeltaTime: FloatProperty` |
| `SetTickInterval` | `TickInterval: FloatProperty` |
| `SetTickEnabled` | `bEnabled: BoolProperty` |
| `GetTickInterval` | `ReturnValue: FloatProperty` |
| `IsTickEnabled` | `ReturnValue: BoolProperty` |
| `IsSessionSaveData` | `ReturnValue: BoolProperty` |
| `GetOwningSaveComponent` | `ReturnValue: ObjectProperty of SpartaSaveComponent` |
| `GetOwningActor` | `ReturnValue: ObjectProperty of /Script/Engine.Actor` |

`SpartaSaveComponent` functions:

| Function | Parameters (name: type) |
|---|---|
| `Save` | `Events: StructProperty of GameplayTagContainer` |
| `ResetSaveData` | (none) |
| `IsSaveLoaded` | `ReturnValue: BoolProperty` |
| `GetRuntimeData` | `ReturnValue: ObjectProperty of SpartaSaveObject` |
| `GetGUID` | `ReturnValue: StructProperty of CoreUObject.Guid` |
| `GenerateNewGuid` | (none) |

`SpartaSaveContext` struct (members resolved by `qp-refs.sql`): `SaveSubsystem: ObjectProperty of SpartaSaveSubSystem @0x0`,
`EventTags: GameplayTagContainer @0x8`, `SlotIndex: IntProperty @0x28` (dec 40), `bSaveWholeObject: BoolProperty @0x2c`
(dec 44), `IsSaving: BoolProperty @0x2d` (dec 45), `IsSavingSession: BoolProperty @0x2e` (dec 46).

Evidence:

```
[00007FF46F038338] Class /Script/Sparta.SpartaSaveObject [n: 47C43] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F038738] Class /Script/Sparta.SpartaSaveComponent [n: 47C98] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F038F38] Class /Script/Sparta.PlayerSaveObject [n: 47D71] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038338]
```

```
[00007FF46F045338] Class /Script/Sparta.DataLayersSave [n: 48F97] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038338]
[00007FF46F050138] Class /Script/Sparta.SpartaMetaSave [n: 49AFE] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF4A738]
[00007FF46F057138] Class /Script/Sparta.SpartaSaveGame [n: 4A29B] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF4A738]
```

```
[00007FF46F057338] Class /Script/Sparta.SpartaSaveGlobalSettings [n: 4A2A3] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE5C938]
[00007FF46F057538] Class /Script/Sparta.SpartaSaveInterface [n: 4A2B0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52A30]
[00007FF46F057B38] Class /Script/Sparta.SpartaSaveSubSystem [n: 4A2D5] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF5DF38]
```

```
[00007FF46F057D38] Class /Script/Sparta.SpartaSessionSaveObjectBase [n: 4A594] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038338]
```

```
[000001F4ACD71600] BoolProperty /Script/Sparta.SpartaSaveObject:bIsSessionSaveData [o: B1] [n: 586463] [c: 00007FF721DE8D80] [owr: 00007FF46F038338]
```

```
[000001F49CD2DDA0] MapProperty /Script/Sparta.DataLayersSave:SavedDataLayerStates [o: B8] [n: 587BA2] [c: 00007FF721DE8F70] [owr: 00007FF46F045338] [kp: 000001F4ACD689E0] [vp: 000001F4ACDA3300]
[000001F4ACD689E0] NameProperty SavedDataLayerStates./Script/Sparta.DataLayersSave:SavedDataLayerStates_Key [o: 0] [n: 587BAD] [c: 00007FF721DE90B0] [owr: 000001F49CD2DDA0]
[000001F4ACDA3300] EnumProperty SavedDataLayerStates./Script/Sparta.DataLayersSave:SavedDataLayerStates [o: 8] [n: 587BA2] [c: 00007FF721DE7380] [owr: 000001F49CD2DDA0] [em: 00007FF46F2ED1C8]
```

`SpartaSaveGame`'s per-actor value containers are the wrapper structs `SpartaMapBool / SpartaMapFloat / SpartaMapInt / SpartaMapName`
(each a single member `Map`, `ss:` targets `00007FF46F48C578` etc.), keyed by `NameProperty` (the changed actor's
name) — i.e. the world save is **actor-name-keyed**:

```
[000001F4A93E85F0] SetProperty /Script/Sparta.SpartaSaveGame:ChangedActors [o: 28] [n: 589119] [c: 00007FF721DE94F0] [owr: 00007FF46F057138]
[000001F49CD2F380] MapProperty /Script/Sparta.SpartaSaveGame:ActorFloats [o: 78] [n: 589109] [c: 00007FF721DE8F70] [owr: 00007FF46F057138] [kp: 000001F4ACDE13C0] [vp: 000001F4ACDF2600]
[000001F4ACDE13C0] NameProperty ActorFloats./Script/Sparta.SpartaSaveGame:ActorFloats_Key [o: 0] [n: 589110] [c: 00007FF721DE90B0] [owr: 000001F49CD2F380]
```

```
[000001F4ACDF2600] StructProperty ActorFloats./Script/Sparta.SpartaSaveGame:ActorFloats [o: 8] [n: 589109] [c: 00007FF721DF9890] [owr: 000001F49CD2F380] [ss: 00007FF46F48C578]
[000001F49CD2F420] MapProperty /Script/Sparta.SpartaSaveGame:ActorInts [o: C8] [n: 5890FB] [c: 00007FF721DE8F70] [owr: 00007FF46F057138] [kp: 000001F4ACDE1430] [vp: 000001F4ACDF2680]
[000001F4ACDE1430] NameProperty ActorInts./Script/Sparta.SpartaSaveGame:ActorInts_Key [o: 0] [n: 589101] [c: 00007FF721DE90B0] [owr: 000001F49CD2F420]
```

### 9.2 `SpartaSaveSubSystem` — the save authority (GameInstanceSubsystem, 73 members)

Properties (the ones that matter):

| Member | Type | Offset |
|---|---|---|
| `OnSaveGameLoaded` | `MulticastInlineDelegateProperty` | `0x30` |
| `PostSessionReset` | `MulticastInlineDelegateProperty` | `0x40` |
| `OnSaveSystemInitialized` | `MulticastInlineDelegateProperty` | `0x50` |
| `OnSaveStarted` | `MulticastInlineDelegateProperty` | `0x60` |
| `OnSaveFinished` | `MulticastInlineDelegateProperty` | `0x70` |
| `SaveObjects` | `MapProperty` **`TMap<Guid, SpartaSaveObject>`** | `0xb8` |
| `CurrentOnDiskWorldStateSave` | `ObjectProperty of /Script/Sparta.SpartaSaveGame` | `0x108` |
| `SaveInterfaces` | `ArrayProperty` (inner `ObjectProperty`) | `0x110` |
| `SessionSaveObject` | `ObjectProperty of /Script/Sparta.SpartaSaveObject` | `0x120` |
| `RuntimeSessionSaveObject` | `ObjectProperty of /Script/Sparta.SpartaSaveObject` | `0x128` |
| `MetaData` | `ObjectProperty of /Script/Sparta.SpartaMetaSave` | `0x140` |
| `bSaveLoaded` | `BoolProperty` | `0x14c` |
| `bApplySkipProloguePresetOnNextLoad` | `BoolProperty` | `0x14e` |

Functions (all 62, full parameter lists, temps filtered):

| Function | Parameters (name: type) |
|---|---|
| `WriteCurrentSaveToTextFile` | (none) |
| `SaveToDisk` | `SaveEvents: StructProperty of GameplayTagContainer`, `bSaveImmediately: BoolProperty`, `bSaveAsync: BoolProperty` |
| `SaveSession` / `SaveMetaData` / `AutoSave` / `ResetSession` / `ResetAutoSaveTimer` / `ResetAllSaveData` / `RequestSkipPrologueStart` / `StopSkipPrologueRequest` / `SetValidActiveSaveProfileSlot` / `EnableAutoSave` / `DisableAutoSave` / `EnableSaveProfileBackup` / `DisableSaveProfileBackup` / `DeleteActiveSaveProfile` / `BackupActiveSaveProfile` / `ClearCache` / `OnEndPlayMap` | (none) |
| `SetPreventFurtherSaves` | `bPreventSaving: BoolProperty` |
| `RecoverSave` | `RecoverySlotName: StrProperty` |
| `ResetSaveObjectByGuid` | `GuidString: StrProperty`, `ReturnValue: BoolProperty` |
| `ResetSaveObjectByClass` | `SaveObjectClass: ClassProperty`, `ReturnValue: IntProperty` |
| `DeleteSaveProfile` | `ProfileSlot: IntProperty` |
| `ChangeActiveSaveProfileSlot` | `SlotIndex: IntProperty` |
| `SubscribeSaveInterface` / `UnsubscribeSaveInterface` | `Object: ObjectProperty` |
| `HasValidSaveForProfileSlot` | `Slot: IntProperty`, `ReturnValue: BoolProperty` |
| `GetSessionSaveSlotName` | `SlotIndex: IntProperty`, `ReturnValue: StrProperty` |
| `GetSaveRecoverySlotName` | `ProfileSlot: IntProperty`, `RecoverySlot: IntProperty`, `ReturnValue: StrProperty` |
| `GetSaveProfileSlotName` | `ProfileSlot: IntProperty`, `ReturnValue: StrProperty` |
| `GetSaveProfileSlotIndexFromName` | `SlotName: StrProperty`, `ReturnValue: IntProperty` |
| `GetSaveProfileDataAsStringFromSlot` | `Slot: IntProperty`, `ReturnValue: StrProperty` |
| `GetSaveProfileDataAsStringFromName` | `SlotName: StrProperty`, `ReturnValue: StrProperty` |
| `GetSaveObjectByClassFromSlotName` | `SlotName: StrProperty`, `SaveObjectClass: ClassProperty`, `ReturnValue: ObjectProperty of SpartaSaveObject` |
| `GetSaveObjectByClassFromSlotIndex` | `SlotIndex: IntProperty`, `SaveObjectClass: ClassProperty`, `ReturnValue: ObjectProperty of SpartaSaveObject` |
| `GetPlayerSaveObjectFromSlotName` | `SlotName: StrProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.PlayerSaveObject` |
| `GetPlayerSaveObjectFromSlotIndex` | `SlotIndex: IntProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.PlayerSaveObject` |
| `GetExistingSaveRecoverySlotNamesForProfileSlot` | `ProfileSlot: IntProperty`, `ReturnValue: ArrayProperty of StrProperty` |
| `GetSessionSave` | `ReturnValue: ObjectProperty of SpartaSaveObject` |
| `GetCurrentWorldStateSave` | `ReturnValue: ObjectProperty of SpartaSaveGame` |
| `GetMetaData` | `ReturnValue: ObjectProperty of SpartaMetaSave` |
| `GetInstance` | `WorldContextObject: ObjectProperty`, `ReturnValue: ObjectProperty of SpartaSaveSubSystem` |
| `GetActiveSaveProfileSlotName` / `GetActiveSaveProfileDataAsString` / `GetMetaSaveDataAsString` | `ReturnValue: StrProperty` |
| `GetActiveSaveProfileSlotIndex` / `GetMaxSaveProfileSlots` / `GetMaxSaveRecoverySlots` | `ReturnValue: IntProperty` |
| `HasValidSessionSave` / `HasValidSaveForActiveProfileSlot` / `HasValidMetaData` / `HasAnyValidSaveProfile` / `GetIsSaveLoaded` / `IsAsyncSaving` / `IsProcessingPrologueSkip` | `ReturnValue: BoolProperty` |
| `GetExistingSaveRecoverySlotNames` / `GetExistingSaveProfileSlotNames` / `GetExistingSaveFileNames` | `ReturnValue: ArrayProperty of StrProperty` |
| `AsyncSaveFinished` | `SlotName: StrProperty`, `PlatformId: IntProperty`, `bSuccess: BoolProperty` |

Evidence:

```
[000001F49CD2F2E0] MapProperty /Script/Sparta.SpartaSaveSubSystem:SaveObjects [o: B8] [n: 58919E] [c: 00007FF721DE8F70] [owr: 00007FF46F057B38] [kp: 000001F4ACDF5200] [vp: 000001F4ACDF5180]
[000001F4ACDF5200] StructProperty SaveObjects./Script/Sparta.SpartaSaveSubSystem:SaveObjects_Key [o: 0] [n: 5891A5] [c: 00007FF721DF9890] [owr: 000001F49CD2F2E0] [ss: 00007FF46F2E3578]
[000001F4ACDF5180] ObjectProperty SaveObjects./Script/Sparta.SpartaSaveSubSystem:SaveObjects [o: 10] [n: 58919E] [c: 00007FF721DE9420] [owr: 000001F49CD2F2E0] [pc: 00007FF46F038338]
```

(`SaveObjects_Key`'s `ss:` target `00007FF46F2E3578` resolves to `/Script/CoreUObject.Guid`.)

```
[000001F4ACDF5280] ObjectProperty /Script/Sparta.SpartaSaveSubSystem:CurrentOnDiskWorldStateSave [o: 108] [n: 58918F] [c: 00007FF721DE9420] [owr: 00007FF46F057B38] [pc: 00007FF46F057138]
[000001F4ACDF3300] ObjectProperty /Script/Sparta.SpartaSaveSubSystem:SessionSaveObject [o: 120] [n: 58917D] [c: 00007FF721DE9420] [owr: 00007FF46F057B38] [pc: 00007FF46F038338]
[000001F4ACDF3380] ObjectProperty /Script/Sparta.SpartaSaveSubSystem:RuntimeSessionSaveObject [o: 128] [n: 589170] [c: 00007FF721DE9420] [owr: 00007FF46F057B38] [pc: 00007FF46F038338]
```

```
[000001F4ACDF3400] ObjectProperty /Script/Sparta.SpartaSaveSubSystem:MetaData [o: 140] [n: 442] [c: 00007FF721DE9420] [owr: 00007FF46F057B38] [pc: 00007FF46F050138]
```

### 9.3 The player save object — `BP_SpartaPlayerSaveObject_C` (parent `PlayerSaveObject`)

This is the richest per-player state container (offsets/fields from `raw\CXXHeaderDump\BP_SpartaPlayerSaveObject.hpp`,
class row from the object dump). Fields relevant to quests/story/map:

| Field | Type | Offset |
|---|---|---|
| `ActorBools` | `TMap<FName,bool>` | `0x340` |
| `ItemPoolState` | `TMap<FName,int32>` | `0x3e0` |
| `CumulativeGloom` / `CumulativeGold` | `int32` | `0x430` / `0x434` |
| `ActiveDarkFormItemDefinition` … `ActiveChargesItemDefinition` | `TSubclassOf<USpartaItemDefinition>` ×6 | `0x438`–`0x460` |
| `EquipmentUnlockState` | `TMap<FGameplayTag,bool>` | `0x468` |
| `PastTarGolemFight` | `bool` | `0x4b8` |
| `FinishedPrologue` | `bool` | `0x4b9` |
| `CurrentPrologueCheckpoint` | `FTransform` | `0x4c0` |
| `CumulativeGlimpses` / `TotalDeaths` / `TotalBossesDefeated` | `int32` | `0x520` / `0x524` / `0x528` |
| `CurrentHubEventPoints` / `TotalHubEventPoints` / `GiftEventPointBank` | `double` | `0x530` / `0x538` / `0x540` |
| `CurrentGiftEventLevel` | `int32` | `0x548` |
| `UnlockedLandingAreas` | `TArray<FName>` | `0x550` |
| `bHasLostGloom` / `bLostGloomInsideObjective` | `bool` | `0x560` / `0x561` |
| `LostGloomObjectiveGUID` / `LostGloomAmount` / `LostGloomLocation` / `LostGloomBiome` | `FString`/`int32`/`FVector`/`FGameplayTag` | `0x568`/`0x578`/`0x580`/`0x598` |
| `CleansedLandingAreas` | `TArray<FName>` | `0x5a0` |
| `CinematicsWatched` | `FGameplayTagContainer` | `0x5b0` |
| `SurpriseEggsFound` | `TMap<int32,bool>` | `0x5d0` |
| `AttentionGrabberStatus` | `TArray<FName>` | `0x630` |
| `ShownTutorials` | `TArray<FName>` | `0x640` |
| `EarnedAchievements` | `TMap<FString,bool>` | `0x650` |
| `SlayerSealLockout` / `StoryCompletionActivityStarted` / `StoryCompletionActivityCompleted` | `bool` | `0x6a0` / `0x6a1` / `0x6a2` |
| `PlayedLuteTracks` | `TMap<FString,bool>` | `0x6a8` |
| `ProximaBiosampler` | `TMap<FGameplayTag,int32>` | `0x6f8` |
| `NewGamePlus` | `int32` | `0x798` |
| `SaveObjectsToResetForNGP` | `TArray<TSubclassOf<USpartaSaveObject>>` | `0x890` |
| `CurrentSpawnLocation` | `FText` | `0x8a0` |
| `PlayNewGamePlusTutorialNextSpawn` / `SkippedPrologue` | `bool` | `0x9a0` / `0x9b8` |
| `EquipmentToUnlockOnSkipPrologue` | `TArray<FGameplayTag>` | `0x9a8` |
| `CurrentGoodSheepAchievement` | `int32` | `0x9bc` |
| `ActiveShellSkins` / `UnlockedSkins` / `ActiveShellMask` / `UnlockedMasks` / `ActiveShellClothing` / `UnlockedClothing` / `ActiveDarkFormSkinID` / `ActiveDarkFormMask` / `ActiveDarkFormClothing` | `TMap<FGameplayTag, …>` | `0x7a0`–`0xa60` |

Functions (selection): `SetAchievementEarned(AchievementID: FString)`, `IsAchievementEarned(AcievementID: FString, Earned: bool&)`,
`SetCinematicWatched(Tag: FGameplayTag)`, `CheckIfCinematicWatched(Tag: FGameplayTag)`,
`GetFinishedPrologue(FinishedPrologue: bool&)`, `SetFinishedPrologue(FinishedPrologue: bool)`,
`IsLandingAreaUnlocked(const FName& ID)`, `UpdateUnlockedLandingAreas(const FName& ID)`,
`IsLandingAreaCleansed(const FName& ID)`, `UpdateCleansedLandingAreas(const FName& ID)`,
`SetStoryActivityCompleted()`, `GetStoryActivityCompleted(ActivityCompleted: bool&)`,
`SetStoryCompletionActivityStarted()`, `GetStoryCompletionActivityStarted(ActivityStarted: bool&)`,
`SetSlayerSealLockout(AchievementLockout: bool)`, `GetSlayerSealLockout(AchievementsLocked: bool&)`,
`ResetForNewGamePlus()`, `ReceiveApplySkipProloguePreset()`, `ReceiveOnLoaded()`,
`SetLuteTrackPlayed(IndexPlayed: int32)`, `GetNumberPlayedLuteTracks(TracksPlayed: int32&)`,
`IncrementCumulativeCurrency(Class: UClass*, Amount: int32)`, `GetEquipmentUnlockState(const FGameplayTag ID)`,
`AddUnlockedEquipment(const FGameplayTag& ID, const bool Unlocked)`.

Evidence (the save-object class row, proving `/Game/Sparta/Core/Player/Save/BP_SpartaPlayerSaveObject.BP_SpartaPlayerSaveObject_C`
is driven through the save stack — `BPC_QuestLogComponent_C:LoadNarrativeTags` and `BPC_Player_Progression_C` both
cast to this class):

```
[000001F606B61150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Save/BP_SpartaPlayerSaveObject.BP_SpartaPlayerSaveObject_C [n: 3A373D] [c: 00007FF46EEC3D38] [or: 000001F4D1C15D80] [sps: 00007FF46F038F38]
[000001F622D1FF80] StructProperty /Game/Sparta/Core/Player/Save/BP_SpartaPlayerSaveObject.BP_SpartaPlayerSaveObject_C:UberGraphFrame [o: 338] [n: 59AB38] [c: 00007FF721DF9890] [owr: 000001F606B61150] [ss: 00007FF46F446958]
```

(the authoritative field list is `raw\CXXHeaderDump\BP_SpartaPlayerSaveObject.hpp`, whose first line is
`class UBP_SpartaPlayerSaveObject_C : public UPlayerSaveObject`; note its first member `UberGraphFrame` sits at
`0x338`, matching the `[o: 338]` above.)

`BP_SpartaMetaSave_C` (parent `USpartaMetaSave`) adds `ShownMetaTutorials: TArray<FName> @0x80` and
`bIntroCinematicWatched: bool @0x90`.
`BPSO_PlayerSession_C` (parent `USpartaSessionSaveObjectBase`) — the **per-session** player state:

| Field | Type | Offset |
|---|---|---|
| `Resolve` / `Health` / `ShellHealth` / `HealCurrentCharges` | `double` | `0xc0` / `0xc8` / `0xd0` / `0xd8` |
| `ShellReviveProgress` / `ShellReviveMaxProgress` | `float` | `0xe0` / `0xe4` |
| `StartTransform` | `FTransform` | `0xf0` |
| `PositionCheckFrequency` | `float` | `0x150` |
| `PlayerPawnCache` | `APawn*` | `0x160` |
| `IsInDarkform` | `bool` | `0x168` |
| `AbilitySystemComponent` | `UAbilitySystemComponent*` | `0x170` |

Evidence — the class row from the **all-assets** dump, and the same class plus its CDO from the **ingame** dump:

```
[00007FF46F9605A0] BlueprintGeneratedClass /Game/Sparta/SaveObjects/BPSO_PlayerSession.BPSO_PlayerSession_C [n: 9C8B7] [c: 00007FF46EEC3D38] [or: 00007FF46F95FF90] [sps: 00007FF46F057D38]
```

```
[00007FF4358105B0] BlueprintGeneratedClass /Game/Sparta/SaveObjects/BPSO_PlayerSession.BPSO_PlayerSession_C [n: 96B7C] [c: 00007FF434D73D38] [or: 00007FF43580FFA0] [sps: 00007FF434F07D38]
[00007FF4358112B0] BPSO_PlayerSession_C /Game/Sparta/SaveObjects/BPSO_PlayerSession.Default__BPSO_PlayerSession_C [n: 5BDD14] [c: 00007FF4358105B0] [or: 00007FF43580FFA0]
```

(the second block is from `raw\ObjectDump_ingame.txt` — same content, different run addresses.)

---

## 10. Which fields are per-player and which are per-world

The dump proves the *containers*, and it proves the class hierarchy. It does **not** label scope per field, so the
table below separates "proven" from "GUESS".

Proven facts used for the classification:

1. `SpartaSaveSubSystem` holds three *distinct* things: `SaveObjects` (`TMap<FGuid, SpartaSaveObject>` — the registry of
   all save objects), `SessionSaveObject` / `RuntimeSessionSaveObject` (`SpartaSaveObject`), and
   `CurrentOnDiskWorldStateSave` (`SpartaSaveGame`) — evidence in §9.2.
2. `SpartaSaveObject::bIsSessionSaveData` exists and is readable at runtime via `IsSessionSaveData()` — so an object
   knows which kind it is (evidence in §9.1).
3. `SpartaSaveSubSystem::GetPlayerSaveObjectFromSlotName/-SlotIndex` returns a `PlayerSaveObject` **from a save-profile
   slot**, while `GetCurrentWorldStateSave` returns a `SpartaSaveGame` — two different files.
4. `SpartaSessionSaveObjectBase` exists as the session-branch base class, and `BPSO_PlayerSession_C` derives from it.
5. Many stateful actors' components derive from `SpartaSaveComponent` with `SaveProtocol: ESaveObjectProtocol` and a
   per-actor `Guid` — i.e. each actor's save object is addressable by GUID inside the save object map.
6. `BPSO_PlayerSaveObject`-family objects (`BPSO_Progression`, `BPSO_ShellMemories`) live under
   `/Game/Sparta/SaveObjects/` next to `BPSO_PlayerSession`, and are `PlayerProgressionSaveData` /
   `SpartaSaveObject`.

| State | Object | Scope | Basis |
|---|---|---|---|
| Level/actor state (moved gates, opened chests, killed bosses, data-layer switches) | per-actor `BPSO_*` instances registered in `SaveObjects` by GUID | **per-world-ish** — GUESS: these are the "changed actors" that `SpartaSaveGame.ChangedActors` / `ActorBools`… track | `SpartaSaveGame` has `ChangedActors` + name-keyed `ActorBools/Ints/Floats/Names/ObjectMap` |
| Data-layer runtime state | `DataLayersSave.SavedDataLayerStates: TMap<FName, EDataLayerRuntimeState>` | world (single instance, no player dimension) | the map key is a data-layer **Name**, not a player id |
| Session HP / position / darkform / shell-revive | `BPSO_PlayerSession_C` (session save object) | **per-player, per-session** — GUESS that it is discarded on `SpartaSaveSubSystem::ResetSession()` / the `PostSessionReset` delegate | derives `SpartaSessionSaveObjectBase`; holds `StartTransform` + `GetSessionStartTransform()`; `SpartaSaveSubSystem` exposes `ResetSession()` and a `PostSessionReset` multicast delegate |
| Skill levels, shell/tier builds | `BPSO_Progression_C` (`PlayerProgressionSaveData`) | **per-player, persistent** | reached from `GetPlayerSaveObject*`-family / player save |
| Shell memories watched / progress | `BPSO_ShellMemories_C` (`MemoryProgress: TMap<FGameplayTag,int32>`, `MemoriesWatched: TSet<FName>`) | **per-player** | sits in `/Game/Sparta/SaveObjects/` with `BPSO_Progression`; `BPC_ShellMemoryHandler` is a `SpartaSaveComponent` |
| Quest log (active/completed quests, narrative tags, NPC relationships, narrative events) | `BPSO_QuestLogComponent_C` | **per-player** — GUESS from naming/symmetry with progression | it is the `RuntimeData` of the player's `BPC_QuestLogComponent` (`LoadNarrativeTags` casts `GetSpartaPlayerCharacter → GetRuntimeData`) |
| Achievements earned, tutorials shown, cinematics watched, landing areas unlocked/cleansed, NG+ level | `BP_SpartaPlayerSaveObject_C` fields | **per-player** | they live on a `PlayerSaveObject`, which the subsystem hands out *per profile slot* |
| NPC greeting/kill flags, shop gold, Eridrim unlock | `BPSO_NPC_C`, `BPSO_ShopData_C`, `BPSO_EridrimUnlockManager_C` | UNKNOWN (both are plausible) | no dump evidence binds them to either file |
| Active area / completed areas | `SpartaAreaSubsystem` (WorldSubsystem, in-memory only) | world-runtime, transient | no save object of that name exists anywhere in the dump |
| Run state integers / streamed level-instance list | `SpartaRunManagerSubsystem` (GameInstanceSubsystem) | run-runtime, transient | no save object found |

UNKNOWN — what I tried: `SELECT path FROM obj WHERE path LIKE '%SaveObject%'` shows no save object for the area
or run-manager subsystems; `SpartaAreaId` / `SpartaActivityType` have **only their CDOs** in the dump
(`/Script/Sparta.Default__SpartaAreaId`, `/Script/Sparta.Default__SpartaActivityType`, 1 each), so the whole
area/activity content set appears to be empty in this build.

---

## 11. Every `BPSO_*` / `BPO_*` class (52) with its proven fields

Enumeration: `scripts\q\qp-bpso.sql` → 52 rows. Fields are from `raw\CXXHeaderDump\<name>.hpp`; all of them are
`SpartaSaveObject` subclasses unless noted. Everything is `0x00B8 +` because the base ends at `0xB8`.

| Class | Path | Parent | Fields |
|---|---|---|---|
| `BPSO_PlayerSession_C` | `/Game/Sparta/SaveObjects/BPSO_PlayerSession` | `SpartaSessionSaveObjectBase` | see §9.3 (HP/pos/session) |
| `BPSO_Progression_C` | `/Game/Sparta/SaveObjects/BPSO_Progression` | `PlayerProgressionSaveData` | `CurrentLevel`, `AvailablePoints`, `AssignedPoints<TMap<GameplayTag,int32>>`, `BondingPoints`, `GoldSpentAtShop` |
| `BPSO_ShellMemories_C` | `/Game/Sparta/SaveObjects/BPSO_ShellMemories` | `SpartaSaveObject` | `MemoryProgress: TMap<FGameplayTag,int32> @0xb8`, `MemoriesWatched: TSet<FName> @0x108` |
| `BPSO_QuestLogComponent_C` | `/Game/Sparta/Core/SaveObjects/BPSO_QuestLogComponent` | `SpartaSaveObject` | `NarrativeTags: FGameplayTagContainer @0xc0` (only field) |
| `BPSO_NarrativeEventsTracker_C` | `/Game/Sparta/Dialogue/QuestSystem/BPSO_NarrativeEventsTracker` | `SpartaSaveObject` | `NarrativeEventTimeTracking: TMap<FGameplayTag,FF_NarrativeEventFloatTracking> @0xb8` |
| `BPSO_WorldMap_C` | `/Game/Sparta/UI/World/Map/Blueprints/BPSO_WorldMap` | `SpartaSaveObject` | `Trackers: TMap<int32,FVector>`, `ObjectiveGUID: FString`, `IsInsideObjective: bool`, `LastPlayerLocation: FVector` |
| `BPSO_WordMapObjective_C` | `/Game/Sparta/UI/World/Map/Blueprints/BPSO_WordMapObjective` | `SpartaSaveObject` | `Visible`, `Revealed`, `Completed`, `ShowArea`, `PlayedRevealScript` (5 bools @0xc0–0xc4) |
| `BPSO_NPC_C` | `/Game/Sparta/Core/SaveObjects/BPSO_NPC` | `SpartaSaveObject` | `HasTriggeredInitialGreeting: bool`, `WasEverKilled: bool` |
| `BPSO_ShopData_C` | `/Game/Sparta/Core/SaveObjects/BPSO_ShopData` | `SpartaSaveObject` | `CachedItemsMap: TMap<TSoftClassPtr<USpartaItemDefinition>,int32>`, `TotalGoldSpent: int32`, `StolenItems: bool` |
| `BPSO_EridrimUnlockManager_C` | `/Game/Sparta/Core/SaveObjects/BPSO_EridrimUnlockManager` | `SpartaSaveObject` | `WardenJumped`, `WardenDefeated`, `ShellClaimed` |
| `BPSO_Egon_C` | `/Game/Sparta/Maps/Playgrounds/NarrativeEvents/MajorNarrativeEvents/EgonInteractions/Lazlo/BPSO_Egon` | `SpartaSaveObject` | `GloomDonated: int32 @0xb8` |
| `BPSO_Gate_C` | `/Game/Sparta/Core/SaveObjects/BPSO_Gate` | `SpartaSaveObject` | `GateOpened`, `GotVarsOnce`, `StartingLoc`, `EndingLoc`, `StartingRot`, `EndingRot` |
| `BPSO_MovingObject_C` | `/Game/Sparta/Core/SaveObjects/BPSO_MovingObject` | `SpartaSaveObject` | `MovedToEnd`, `GotVarsOnce`, + same 4 transforms |
| `BPSO_CloseAndOpenGate_C` | `/Game/Sparta/Core/SaveObjects/BPSO_CloseAndOpenGate` | `SpartaSaveObject` | `CombatEncCompleted: bool` |
| `BPSO_BossGate_C` | `/Game/Sparta/Core/SaveObjects/BPSO_BossGate` | `SpartaSaveObject` | `Unlocked: bool` |
| `BPSO_TarGate_C` | `/Game/Sparta/Core/SaveObjects/BPSO_TarGate` | `SpartaSaveObject` | `GateClensed`, `PlayerClensed` |
| `BPSO_DoorLockedDungeon_C` | `/Game/Sparta/Core/SaveObjects/BPSO_DoorLockedDungeon` | `SpartaSaveObject` | `DoorOpened: bool` |
| `BPSO_WoodenElevator_C` | `/Game/Sparta/Core/SaveObjects/BPSO_WoodenElevator` | `SpartaSaveObject` | `StartingFloor: int32`, `HasBrokenOnce: bool`, `StartingLoc: FVector` |
| `BPSO_ShepherdSheepGate_C` | `/Game/Sparta/Core/SaveObjects/BPSO_ShepherdSheepGate` | `SpartaSaveObject` | `GateOpened: bool` |
| `BPSO_Ovum_C` | `/Game/Sparta/Core/SaveObjects/BPSO_Ovum` | `SpartaSaveObject` | `Cleansed`, `OvumCollected` |
| `BPSO_EvilStatue_C` | `/Game/Sparta/Core/SaveObjects/BPSO_EvilStatue` | `SpartaSaveObject` | `Cleansed`, `RewardGiven` |
| `BPSO_GrishaCage_C` | `/Game/Sparta/Core/SaveObjects/BPSO_GrishaCage` | `SpartaSaveObject` | `CageOpened: bool`, `OpenType: FGameplayTag` |
| `BPSO_GrishaGhost_C` | `/Game/Sparta/Core/SaveObjects/BPSO_GrishaGhost` | `SpartaSaveObject` | `BossDefeated: bool` |
| `BPSO_GenessaSensor_C` | `/Game/Sparta/Core/SaveObjects/BPSO_GenessaSensor` | `SpartaSaveObject` | `SensorClensed: bool` |
| `BPSO_HutchbackBossTrigger_C` | `/Game/Sparta/Core/SaveObjects/BPSO_HutchbackBossTrigger` | `SpartaSaveObject` | `HutchbackDefeated: bool` |
| `BPSO_LandingArea_Boss_C` | `/Game/Sparta/Core/SaveObjects/BPSO_LandingArea_Boss` | `SpartaSaveObject` | `BeaconRisen: bool` |
| `BPSO_BossDLManager_C` | `/Game/Sparta/Core/SaveObjects/BPSO_BossDLManager` | `SpartaSaveObject` | `CurrentBoss: TEnumAsByte<E_CoreBosses::Type>`, `RelevantDL: UDataLayerAsset*` |
| `BPSO_CinematicDLUpdate_C` | `/Game/Sparta/Core/SaveObjects/BPSO_CinematicDLUpdate` | `SpartaSaveObject` | `CinematicFired: bool` |
| `BPSO_FC_CorruptionCore_C` | `/Game/Sparta/Core/SaveObjects/BPSO_FC_CorruptionCore` | `SpartaSaveObject` | `Cleansed: bool @0xb8` |
| `BPSO_FC_FinalBossManager_C` | `/Game/Sparta/Core/SaveObjects/BPSO_FC_FinalBossManager` | `SpartaSaveObject` | `EndStrandingCompleted`, `AllCorruptionCoresClensed`, `ClensedCores: int32`, `TotalCoresNeeded: int32` |
| `BPSO_FC_TarRoots_C` | `/Game/Sparta/Core/SaveObjects/BPSO_FC_TarRoots` | `SpartaSaveObject` | `T1Cleansed`, `T2Cleansed`, `T3Clensed`, `CenterClensed` |
| `BPSO_TarGolemLevelScript_C` | `/Game/Sparta/Core/SaveObjects/BPSO_TarGolemLevelScript` | `SpartaSaveObject` | `StoneSealClaimed: bool` |
| `BPSO_Tarforge_C` | `/Game/Sparta/Core/SaveObjects/BPSO_Tarforge` | `SpartaSaveObject` | `StoredTarcores: int32` |
| `BPSO_CraftingItemPickup_C` | `/Game/Sparta/Core/SaveObjects/BPSO_CraftingItemPickup` | `SpartaSaveObject` | `ItemCollected`, `ItemInitialized` |
| `BPSO_FlowerChest_C` | `/Game/Sparta/Core/SaveObjects/BPSO_FlowerChest` | `SpartaSaveObject` | `ItemCollected: bool` |
| `BPSO_SavedLockedChests_C` | `/Game/Sparta/Core/Interaction/SaveObjects/BPSO_SavedLockedChests` | `SpartaSaveObject` | `SavedChestIDs: TArray<TSoftClassPtr<USpartaItemDefinition>>`, `HasBeenOpened: bool` |
| `BPSO_SpawnerSaveObj_C` | `/Game/Sparta/Core/SaveObjects/BPSO_SpawnerSaveObj` | `SpartaSaveObject` | `WasEverKilled: bool` |
| `BPSO_BearTrap_C` | `/Game/Sparta/Core/SaveObjects/BPSO_BearTrap` | `SpartaSaveObject` | `IsDisarmed: bool` |
| `BPSO_FallingCorpse_C` | `/Game/Sparta/Core/SaveObjects/BPSO_FallingCorpse` | `SpartaSaveObject` | `ShotDown: bool` |
| `BPSO_FallingStatue_C` | `/Game/Sparta/Core/SaveObjects/BPSO_FallingStatue` | `SpartaSaveObject` | `StatueFallen: bool` |
| `BPSO_PlayerTrapDungeon_C` | `/Game/Sparta/Core/SaveObjects/BPSO_PlayerTrapDungeon` | `SpartaSaveObject` | `TrappedOnce: bool` |
| `BPSO_PetrifiedCultistManager_C` | `/Game/Sparta/Core/SaveObjects/BPSO_PetrifiedCultistManager` | `SpartaSaveObject` | `Harros: bool` |
| `BPSO_ShellGhost_C` | `/Game/Sparta/Core/SaveObjects/BPSO_ShellGhost` | `SpartaSaveObject` | `StateIndex: int32` |
| `BPSO_ShellRevive_C` | `/Game/Sparta/Core/SaveObjects/BPSO_ShellRevive` | `SpartaSaveObject` | `StationUsed: bool` |
| `BPSO_SlayerDungeonChalice_C` | `/Game/Sparta/Core/SaveObjects/BPSO_SlayerDungeonChalice` | `SpartaSaveObject` | `SealBroken: bool` |
| `BPSO_TavernArena_C` | `/Game/Sparta/Core/SaveObjects/BPSO_TavernArena` | `SpartaSaveObject` | `WagerPlaced`, `ArenaCombatLVL: int32`, `RewardGiven` |
| `BPO_STH_C` | `/Game/Sparta/Core/World/Teleporting/BPO_STH` | `SpartaInstancedObject` | (base of the teleport-handler instanced objects) |
| `BPO_STH_Beacon_C` | `/Game/Sparta/Core/World/Teleporting/BPO_STH_Beacon` | `BPO_STH_C` | fast-travel beacon |
| `BPO_STH_Beacon_MainBoss_C` | `/Game/Sparta/Core/World/Teleporting/BPO_STH_Beacon_MainBoss` | `BPO_STH_Beacon_C` | |
| `BPO_STH_Generic_C` | `/Game/Sparta/Core/World/Teleporting/BPO_STH_Generic` | `BPO_STH_C` | |
| `BPO_TutorialHandler_C` | `/Game/Sparta/UI/Tutorials/Blueprints/BPO_TutorialHandler` | `SpartaWorldObject` | see part 1 §9 |
| `BPO_Tut_DLCs_C` | `/Game/Sparta/UI/Tutorials/Others/BPO_Tut_DLCs` | `BPO_TutorialHandler_C` | |

Evidence — one row per folder family (the enumeration is 52 rows in total; see `slices\qp-bpso-all.txt`):

```
[000001F4BE032A40] BlueprintGeneratedClass /Game/Sparta/SaveObjects/BPSO_ShellMemories.BPSO_ShellMemories_C [n: 25213F] [c: 00007FF46EEC3D38] [or: 000001F4C41E2FE0] [sps: 00007FF46F038338]
[000001F4D4597C30] BlueprintGeneratedClass /Game/Sparta/Core/SaveObjects/BPSO_Gate.BPSO_Gate_C [n: 38D0D7] [c: 00007FF46EEC3D38] [or: 000001F4D0774D40] [sps: 00007FF46F038338]
[000001F4DC012320] BlueprintGeneratedClass /Game/Sparta/Core/Interaction/SaveObjects/BPSO_SavedLockedChests.BPSO_SavedLockedChests_C [n: 36A547] [c: 00007FF46EEC3D38] [or: 000001F4D33C9E30] [sps: 00007FF46F038338]
```

```
[000001F4DB719FD0] BlueprintGeneratedClass /Game/Sparta/Core/World/Teleporting/BPO_STH.BPO_STH_C [n: 78155] [c: 00007FF46EEC3D38] [or: 000001F4D33C1E60] [sps: 00007FF46F04DB38]
[000001F841D7FC70] BlueprintGeneratedClass /Game/Sparta/Core/World/Teleporting/BPO_STH_Generic.BPO_STH_Generic_C [n: 3C7E56] [c: 00007FF46EEC3D38] [or: 000001F84C2F80D0] [sps: 000001F4DB719FD0]
[00007FF46F04DB38] Class /Script/Sparta.SpartaInstancedObject [n: 498E8] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
```

```
[000001F4C43EF8E0] BlueprintGeneratedClass /Game/Sparta/Maps/Playgrounds/NarrativeEvents/MajorNarrativeEvents/EgonInteractions/Lazlo/BPSO_Egon.BPSO_Egon_C [n: 3092A2] [c: 00007FF46EEC3D38] [or: 000001F4D301B7D0] [sps: 00007FF46F038338]
```

---

## 12. DataTables and DataAssets for these systems

### 12.1 All `DataTable` objects under `/Game/Sparta` (249 total); the ones tied to this area

| Asset | Path | Row struct (proof level) |
|---|---|---|
| `DT_QuestData1` | `/Game/Sparta/Dialogue/QuestSystem/DT_QuestData1.DT_QuestData1` | `QuestDetails` (proven `TableRowBase`; table linkage GUESS — see part 1 §1.4) |
| `DT_ShellMemories` | `/Game/Sparta/Dialogue/Blueprints/ShellMemories/MemoryData/DT_ShellMemories.DT_ShellMemories` | UNKNOWN — no `TableRowBase` struct in the shell-memory namespace; row type is a Blueprint struct not present in the dump |
| `DT_Achievements` | `/Game/Sparta/Core/Achievements/DT_Achievements.DT_Achievements` | `FF_AchievementProgress` (GUESS: `F_AchievementProgress.hpp` gives `ProgressiveUnlock/Progress/Earned`; the struct is not `TableRowBase`-derived and not dumped) |
| `DT_FakeAchievements` | `/Game/Sparta/UI/Notifies/FakeAchievement/DT_FakeAchievements.DT_FakeAchievements` | UNKNOWN |
| `DT_LoreText` | `/Game/Sparta/UI/Core/LoreText/DT_LoreText.DT_LoreText` | UNKNOWN |
| `DT_NarrativeArtifactsNew` | `/Game/Sparta/Core/Player/Artifacts/DT_NarrativeArtifactsNew.DT_NarrativeArtifactsNew` | UNKNOWN |
| `DT_TextStyle_Dialogues` | `/Game/Sparta/UI/Core/Text/DT_TextStyle_Dialogues.DT_TextStyle_Dialogues` | GUESS: `/Script/UMG.RichTextStyleRow` (the engine's standard style row; row struct not recorded on the asset) |
| `DT_TextStyle_Tutorials` | `/Game/Sparta/UI/Core/Text/DT_TextStyle_Tutorials.DT_TextStyle_Tutorials` | GUESS: `/Script/UMG.RichTextStyleRow` (same) |
| `DT_WeaponProgression` | `/Game/Sparta/Core/Player/Upgrades/Tarforge/DT_WeaponProgression.DT_WeaponProgression` | `/Script/Sparta.PlayerUpgrade` is the only `TableRowBase` upgrade struct (GUESS for this table) |
| `DT_SpartaGiftEventLeader` | `/Game/Sparta/Dialogue/Blueprints/DiscipleSystem/DT_SpartaGiftEventLeader.DT_SpartaGiftEventLeader` | `/Script/Sparta.SpartaHubGiftEventData` (`TableRowBase`, proven struct; table linkage GUESS) |
| `DT_SurpriseEggData` | `/Game/Sparta/Dialogue/Blueprints/DiscipleSystem/DT_SurpriseEggData.DT_SurpriseEggData` | `/Script/Sparta.SpartaSurpriseEggData` (`TableRowBase`, proven struct; linkage GUESS) |

Evidence:

```
[000001F4C48024B0] DataTable /Game/Sparta/Dialogue/QuestSystem/DT_QuestData1.DT_QuestData1 [n: 29EC82] [c: 00007FF46EF31938] [or: 000001F4D304F740]
[000001F4DCBFF710] DataTable /Game/Sparta/Dialogue/Blueprints/ShellMemories/MemoryData/DT_ShellMemories.DT_ShellMemories [n: 28DFE8] [c: 00007FF46EF31938] [or: 000001F4CBCF6BE0]
[000001F4DCBFF2F0] DataTable /Game/Sparta/Core/Achievements/DT_Achievements.DT_Achievements [n: 2069CF] [c: 00007FF46EF31938] [or: 000001F4CBCF7360]
```

```
[000001F754CC54D0] DataTable /Game/Sparta/UI/Core/LoreText/DT_LoreText.DT_LoreText [n: 1BEC8A] [c: 00007FF46EF31938] [or: 000001F769B0E930]
[000001F4DCBFB720] DataTable /Game/Sparta/Core/Player/Artifacts/DT_NarrativeArtifactsNew.DT_NarrativeArtifactsNew [n: 72EED] [c: 00007FF46EF31938] [or: 000001F4CBCF64B0]
[000001F4DCBFF3A0] DataTable /Game/Sparta/UI/Notifies/FakeAchievement/DT_FakeAchievements.DT_FakeAchievements [n: 3C04FA] [c: 00007FF46EF31938] [or: 000001F4CBCF7400]
```

Every `TableRowBase`-derived struct in the whole build (30 rows; the Sparta ones are the provable table row types):

```
/Script/Sparta.EntitlementID	/Script/Engine.TableRowBase
/Script/Sparta.PlayerUpgrade	/Script/Engine.TableRowBase
/Script/Sparta.QuestDetails	/Script/Engine.TableRowBase
/Script/Sparta.SpartaEnemySpawnerDisplayData	/Script/Engine.TableRowBase
/Script/Sparta.SpartaHubGiftEventData	/Script/Engine.TableRowBase
/Script/Sparta.SpartaItem	/Script/Engine.TableRowBase
/Script/Sparta.SpartaSimpleInventoryItem	/Script/Engine.TableRowBase
/Script/Sparta.SpartaSurpriseEggData	/Script/Engine.TableRowBase
```

(that block is the `type / path / parent` output of `scripts\q\qp-rowstructs2.sql`, which joins
`obj.super` → `/Script/Engine.TableRowBase`; the equivalent object-dump row is the `ScriptStruct` row with
`[sps: 00007FF46F3D1B28]`:

```
[00007FF46F4873B8] ScriptStruct /Script/Sparta.QuestDetails [n: 13803] [c: 00007FF46EE54338] [or: 00007FF46F17CD68] [sps: 00007FF46F3D1B28]
```)

### 12.2 DataAsset census under `/Game/Sparta` (whole content tree)

| Class | Count | Meaning |
|---|---|---|
| `SpartaEventDataAsset` | 388 real (+1 `Default__`) | **event-space / level-instance descriptors** — one per dungeon/event content piece |
| `CSOptionSetDataAsset` | 257 real (+1 `Default__`) | **conditional option sets** — select which dialogue/event wins given world state |
| `DataTable` | 249 | (see above) |
| `SpartaAbilityDataAsset` | 90 | abilities |
| `DataLayerAsset` | 122 | data layers (streaming/state slices) |
| `SpartaZoneData` | 99 real (+1 `Default__`) | audio/music zone definitions |
| `SpartaWeaponProgressionData` | 14 real (+1 `Default__`) | weapon upgrade curves |
| `SpartaCompletionSettings` | 1 real: `/Game/Sparta/Core/Completion/DA_SpartaCompletion` | run-completion scoring config |
| `SpartaProgressionDataAsset` | 1 real: `/Game/Sparta/Core/Player/Upgrades/DarkBro/DA_DarkbroProgression` | dark-form progression curves |
| `SpartaMapTileSet` | 1 real: `/Game/Sparta/UI/World/Map/Blueprints/DT_MapTiles` | world-map tile set (note: the asset is named `DT_*` but its class is `SpartaMapTileSet`, **not** a DataTable) |
| `WorldGenMapSettings` | 1 real: `/Game/Sparta/Maps/MAPS/TheForest/DA_GenSettings_Forest_Hollows` | world-gen settings per map |
| `SpartaAreaId` / `SpartaActivityType` | **only the CDOs** (`/Script/Sparta.Default__SpartaAreaId`, `/Script/Sparta.Default__SpartaActivityType`) | content set is empty in this build |
| `CompositeDataTable` | 1 | (composite) |

Evidence:

```
[000001F8C0844AE0] SpartaCompletionSettings /Game/Sparta/Core/Completion/DA_SpartaCompletion.DA_SpartaCompletion [n: 49EF1F] [c: 00007FF46F044938] [or: 000001F889F92900]
[000001F4DD7624A0] SpartaMapTileSet /Game/Sparta/UI/World/Map/Blueprints/DT_MapTiles.DT_MapTiles [n: 104E69] [c: 00007FF46F04F538] [or: 000001F4CBCF3CB0]
[000001F4DD3B7E00] SpartaProgressionDataAsset /Game/Sparta/Core/Player/Upgrades/DarkBro/DA_DarkbroProgression.DA_DarkbroProgression [n: 2BD0E4] [c: 00007FF46F056138] [or: 000001F4CBCF2680]
```

```
[000001F4DDB502B0] SpartaZoneData /Game/Sparta/Maps/LevelDesignTools/BlueprintTools/TestZones/CoreWorldZones/Zone_HUB.Zone_HUB [n: 407816] [c: 00007FF46F060938] [or: 000001F4CBCF6E60]
```

`SpartaEventDataAsset` members (this is how a level instance is addressed):

| Member | Type | Offset |
|---|---|---|
| `LocationOffset` | `StructProperty of CoreUObject.Vector` | `0x30` |
| `RotationOffset` | `StructProperty of CoreUObject.Rotator` | `0x48` |
| `bIsNarrativeEvent` | `BoolProperty` | `0x60` |
| `LevelInstance` | `SoftObjectProperty of /Script/Engine.World` | `0x68` |
| `EventName` | `TextProperty` | `0x90` |
| `EventIcon` / `CompletedEventIcon` / `EventImage` | `SoftObjectProperty of Texture2D` | `0xa0` / `0xc8` / `0xf0` |

Evidence:

```
[000001F4ACDA7000] SoftObjectProperty /Script/Sparta.SpartaEventDataAsset:LevelInstance [o: 68] [n: 37BBA] [c: 00007FF721DF97F0] [owr: 00007FF46F046D38] [pc: 00007FF46EEE1338]
```

`CSOptionSetDataAsset` members: `GlobalConditions: ArrayProperty (inner StructProperty of GlobalCondition) @0x30`,
`Options: ArrayProperty (inner StructProperty of CSOption) @0x40`; evaluated by

| Function | Parameters (name: type) |
|---|---|
| `/Script/CSOptionSet.CSOptionSetLibrary:EvaluateOptionSet` | `WorldContext: ObjectProperty`, `OptionSet: ObjectProperty of CSOptionSetDataAsset`, `OutOptions: ArrayProperty of CSOption`, `OutError: StrProperty`, `Context: ObjectProperty`, `bEvaluatePassThroughOptionSets: BoolProperty`, `ReturnValue: BoolProperty` |
| `/Script/CSOptionSet.CSOptionSetLibrary:EvaluateConditions` | `WorldContextObject: ObjectProperty`, `Conditions: ArrayProperty of ObjectProperty of CSOptionCondition`, `Context: ObjectProperty`, `ReturnValue: BoolProperty` |
| `/Script/CSOptionSet.CSOptionCondition:EvaluateCondition` / `:ModifyPriority` | (extension points — the game implements `Con_Check*` conditions) |

Example option-set assets (dialogue selection is therefore data-driven, not hard-coded):

```
/Game/Sparta/Dialogue/Dialogues/OptionSets/Gorf/OS_Gorf_Dlg.OS_Gorf_Dlg
/Game/Sparta/Dialogue/Dialogues/OptionSets/Ruk/OS_Ruk_Dlg.OS_Ruk_Dlg
/Game/Sparta/Dialogue/Dialogues/OptionSets/EggCultist/OS_Hub_EggCultist_Base.OS_Hub_EggCultist_Base
/Game/Sparta/Core/World/Levels/Options/OS_ForestEvents_Village.OS_ForestEvents_Village
```

The `Con_Check*` conditions the game ships (`/Game/Sparta/Core/World/Levels/Options/Conditions/`), all parented to
`/Script/CSOptionSet.CSOptionCondition`:

`Con_CheckCurrentShell`, `Con_CheckDungeonLevelNames`, `Con_CheckForNewGamePlus`, `Con_CheckIfNotCurrentShell`,
`Con_CheckIfSpecificShellUnlocked`, `Con_CheckLevelName`, `Con_CheckNarrativeTags`,
`Con_CheckNumberOfShellsUnlocked`, `Con_CheckParticipantTags`, `Con_CheckPlayerInventory`.

Evidence:

```
[00007FF46F017F38] Class /Script/CSOptionSet.CSOptionSetDataAsset [n: 46151] [c: 00007FF46EE55138] [or: 00007FF46F17C728] [sps: 00007FF46EEDA938]
[000001F4ACD2A180] ArrayProperty /Script/CSOptionSet.CSOptionSetDataAsset:GlobalConditions [o: 30] [n: 5853F8] [c: 00007FF721DE8910] [owr: 00007FF46F017F38] [ai: 000001F4ACD2A100]
[000001F4ACD2A280] ArrayProperty /Script/CSOptionSet.CSOptionSetDataAsset:Options [o: 40] [n: 4FB488] [c: 00007FF721DE8910] [owr: 00007FF46F017F38] [ai: 000001F4ACD2A200]
```

---

## 13. Maps, levels, and streaming

### 13.1 What streaming technology is actually used

The whole game content under `/Game/Sparta/Maps/` is **World Partition**, with **Level Instances** for
dungeon/event content and **Data Layers** for state slices. Object-type census under `/Game/Sparta/Maps/`
(`scripts\q\qp-mapcensus.sql`):

| Type | Count |
|---|---|
| `World` | 6947 (371 persistent/named maps + 6576 `_Generated_` cell packages) |
| `Level` | 6947 |
| `SpartaWorldSettings` | 6947 |
| `WorldPartitionRuntimeLevelStreamingCell` | 6901 |
| `WorldPartitionLevelStreamingDynamic` | 6579 |
| `WorldPartitionRuntimeCellDataHashSet` | 6436 |
| `WorldPartitionHLOD` | 2961 |
| `WorldPartitionAutoInstancedActor` | 826 |
| `WorldPartitionStreamingSourceComponent` | 470 |
| `WorldPartitionRuntimeCellDataSpatialHash` | 465 |
| `WorldPartition` | 331 |
| `WorldPartitionLevelStreamingPolicy` | 331 |
| `WorldPartitionRuntimeHashSet` | 210 |
| `WorldPartitionRuntimeSpatialHash` | 121 |
| `LevelStreamingLevelInstance` | 5 |
| `LevelInstanceComponent` | 75951 |
| `SpartaSaveComponent` | 9617 (one per stateful actor in the maps) |

Notably **no `LevelStreamingAlwaysLoaded` / `LevelStreamingPersistent` objects exist** — classical sublevel
streaming is not used; `SpartaLevelStreamingDynamic` / `…HighPrio` exist as classes only:

```
[00007FF46F04E738] Class /Script/Sparta.SpartaLevelStreamingDynamic [n: 4996C] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEE1738]
[00007FF46F04E938] Class /Script/Sparta.SpartaLevelStreamingDynamicHighPrio [n: 4997B] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEE1738]
```

### 13.2 Streaming / level / zone classes

| Class | Parent | Members |
|---|---|---|
| `/Script/Sparta.SpartaWorldSettings` | `/Script/Engine.WorldSettings` | none of its own — the world-partition + zone config lives on the engine base |
| `/Script/Sparta.SpartaLevelScript` | `/Script/Engine.Actor` | `WorldStateTags: GameplayTagQuery @0x2a8`; functions `OnEncounterStarted(Context: StructProperty of LevelScriptContext)`, `OnEncounterFinished()` |
| `/Script/Sparta.StreamingSource` | `/Script/Engine.Actor` | streaming source actor (target of `OnActorStreamingCompleted`) |
| `/Script/Sparta.SpartaStreamingSourceComponent` | `/Script/Engine.WorldPartitionStreamingSourceComponent` | streaming-source component |
| `/Script/Sparta.SpartaDataLayerSubsystem` | `/Script/Engine.WorldSubsystem` | `RuntimeData: ObjectProperty of /Script/Sparta.DataLayersSave @0x38`; `SaveDataLayerState(DataLayer: ObjectProperty of DataLayerAsset, TargetState: EnumProperty of EDataLayerRuntimeState)`, `RemoveDataLayerState(DataLayer)` |
| `/Script/Sparta.DataLayerFunctionLibrary` | `/Script/Engine.BlueprintFunctionLibrary` | (helpers; no members) |
| `/Script/Sparta.DataLayersSave` | `/Script/Sparta.SpartaSaveObject` | `SavedDataLayerStates: TMap<FName, EDataLayerRuntimeState> @0xb8` |
| `/Script/Sparta.SpartaZoneData` | `/Script/Engine.DataAsset` | `ZoneId: NameProperty @0x30`, `AmbienceData: StructProperty of SpartaZoneAmbienceData @0x38`, `MusicData: StructProperty of SpartaZoneMusicData @0x40` |
| `/Script/Sparta.SpartaZoneVolume` | `/Script/Engine.TriggerVolume` | `ZoneData: ObjectProperty of SpartaZoneData @0x2e0`, `Priority: IntProperty @0x2e8`, `CollisionProfile: StructProperty of CollisionProfileName @0x2ec` |
| `/Script/Sparta.SpartaZoneVolumeManager` | `/Script/Engine.GameInstanceSubsystem` | `HighestPriorityZoneVolumeUpdated: MulticastInlineDelegateProperty @0x30`, `ZoneVolumeStack: ArrayProperty (inner WeakObjectProperty of SpartaZoneVolume) @0x40` |
| `/Script/Sparta.SpartaWorldGenSubsystem` | `/Script/Engine.WorldSubsystem` | (see `BP_WorldGenSubsystem_C`: `BeginWorldReset()`, `HandleResetReady()`) |
| `/Script/Sparta.WorldGenMapSettings` | `/Script/Engine.DataAsset` | world-gen settings (1 asset: `DA_GenSettings_Forest_Hollows`) |
| `/Script/Sparta.SpartaMapTileSet` | `/Script/Engine.DataAsset` | `TileSize: IntProperty @0x30`, `NumLevels: IntProperty @0x34`, `Levels: ArrayProperty (inner StructProperty of SpartaMapTileLevel) @0x38` |
| `/Script/Sparta.SpartaAreaId`, `SpartaAreaSubsystem` | see part 1 §3 | |

`SpartaZoneVolume` functions: `BeginOverlap(in_OverlappedComponent, in_OtherActor, in_OtherComp, in_OtherBodyIndex,
in_bFromSweep, in_SweepResult)` and `EndOverlap(in_OverlappedComponent, in_OtherActor, in_OtherComp, in_OtherBodyIndex)`.

`SpartaMapTileSet` / `SpartaZoneData` evidence:

```
[00007FF46F060938] Class /Script/Sparta.SpartaZoneData [n: 4AF48] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[00007FF46F060B38] Class /Script/Sparta.SpartaZoneVolume [n: 4AF50] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF66B38]
[00007FF46F060D38] Class /Script/Sparta.SpartaZoneVolumeManager [n: 4AF59] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF5DF38]
```

```
[00007FF46F045538] Class /Script/Sparta.SpartaDataLayerSubsystem [n: 48F9F] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7C138]
[00007FF46F031738] Class /Script/Sparta.DataLayerFunctionLibrary [n: 47797] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE70138]
[00007FF46F04E538] Class /Script/Sparta.SpartaLevelScript [n: 49962] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
```

```
[00007FF46F04F538] Class /Script/Sparta.SpartaMapTileSet [n: 49A0B] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[00007FF46F05A938] Class /Script/Sparta.SpartaStreamingSourceComponent [n: 4A887] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF3EB38]
[00007FF46F060338] Class /Script/Sparta.SpartaWorldGenSubsystem [n: 4AEFE] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7C138]
```

```
[00007FF46F060738] Class /Script/Sparta.SpartaWorldSettings [n: 4AF3D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF29338]
[00007FF46F062338] Class /Script/Sparta.StreamingSource [n: 4B061] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
[00007FF46F064338] Class /Script/Sparta.WorldGenMapSettings [n: 4B310] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
```

### 13.3 Persistent level and its generated cells (proof of World Partition)

```
[000001F976B4C2E0] World /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01 [n: 3DE727] [c: 00007FF46EEE1338] [or: 000001F977A009C0]
```

```
[000001F976B10DC0] BlueprintGeneratedClass /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01_C [n: 1F286D] [c: 00007FF46EEC3D38] [or: 000001F977A009C0] [sps: 00007FF46EF52738]
```

```
[000001F9796AF380] Package /Game/Sparta/Maps/Core_Levels/L_Core_World_01/_Generated_/001E6Y03RQTJCWT45XI4MJ0P0 [n: 2FB45E] [c: 00007FF46EE55338] [or: 0000000000000000]
[000001F97B5BEBA0] World /Game/Sparta/Maps/Core_Levels/L_Core_World_01/_Generated_/001E6Y03RQTJCWT45XI4MJ0P0.L_Core_World_01 [n: 3DE727] [c: 00007FF46EEE1338] [or: 000001F9796AF380]
```

```
[000001F97AD46940] WorldPartitionRuntimeLevelStreamingCell /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel.SpartaWorldSettings.WorldPartition_0.WorldPartitionRuntimeHashSet_0.5RB5MQ6F4XQP8SGGQHG6JDVZQ [n: 9823B7] [c: 00007FF46EF6F538] [or: 000001F975DAF040]
[000001F975DAF040] WorldPartitionRuntimeHashSet /Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel.SpartaWorldSettings.WorldPartition_0.WorldPartitionRuntimeHashSet_0 [n: 30063] [c: 00007FF46EF28D38] [or: 000001F9796DC280]
```

The persistent level's own level script (also the proof that `SpartaLevelScript`-family actors drive day/night
Data Layers):

```
class AL_Core_World_01_C : public ALevelScriptActor
{
    TArray<UObject*> LandscapeObjects;                                                // 0x02B8 (size: 0x10)
    double Player Character Abs Z;                                                    // 0x02C8 (size: 0x8)
    class UChildActorComponent* Active Light Scenario State;                          // 0x02D0 (size: 0x8)
    bool PlayerInSlice1;                                                              // 0x02D8 (size: 0x1)
    bool PlayerInSlice2;                                                              // 0x02D9 (size: 0x1)
    int64 WeatherPrologueID;                                                          // 0x02E8 (size: 0x8)
    int64 WeatherDayID;                                                               // 0x02F0 (size: 0x8)
    int64 WeatherNightID;                                                             // 0x02F8 (size: 0x8)
    void DataLayerHandler(EDataLayerRuntimeState DayTimeState, EDataLayerRuntimeState NightTimeState);
    void SetWeatherToDay();  void SetWeatherToNight();  void StartIntroCutscene(); …
};
```

(from `raw\CXXHeaderDump\L_Core_World_01.hpp`.)

### 13.4 Data Layers

122 `DataLayerAsset` objects, all under `/Game/Sparta/Environments/Data_Layers/…` plus a few weather/audio ones.
Examples (proven paths):

```
/Game/Sparta/Environments/Data_Layers/L_Core_World_01/DL_HubTower/DL_HUB.DL_HUB
/Game/Sparta/Environments/Data_Layers/L_Core_World_01/DL_HubTower/DL_HUB_Corruption.DL_HUB_Corruption
/Game/Sparta/Environments/Data_Layers/L_Core_World_01/DL_FINALCAVERN/DL_FINALCAVERN.DL_FINALCAVERN
/Game/Sparta/Environments/Data_Layers/L_Core_World_01/DL_ENEMIES/DL_F_ENEMIES.DL_F_ENEMIES
/Game/Sparta/Environments/Data_Layers/L_Core_World_01/DL_CITYRUIN/DL_SLICE1_LIGHTING_DAY.DL_SLICE1_LIGHTING_DAY
/Game/Sparta/Environments/Data_Layers/L_Core_World_01/DL_CITYRUIN/DL_SLICE1_LIGHTING_NIGHT.DL_SLICE1_LIGHTING_NIGHT
/Game/Sparta/Maps/Playgrounds/L_BeatyCorner_Playground_DL/DL_Dry.DL_Dry
/Game/Sparta/Core/Weather/Audio/DL_Summer_Day_Hollows_Audio.DL_Summer_Day_Hollows_Audio
```

Data-layer *state* is persisted per layer **Name** in `DataLayersSave.SavedDataLayerStates` (evidence in §9.1).

### 13.5 All proven map packages — 371 named `World` objects (full list)

Persistent / named map packages (excluding the 6576 `_Generated_` cell packages). Source:
`scripts\q\qp-worlds.sql` (`SELECT path FROM obj WHERE type='World' AND path LIKE '/Game/Sparta/Maps/%'
AND path NOT LIKE '%/_Generated_/%'` → 371 rows).

```
/Game/Sparta/Maps/AnimZoo/L_AnimZoo_FinalBoss_Intro.L_AnimZoo_FinalBoss_Intro
/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01
/Game/Sparta/Maps/Event_Instances/NonCombat/LI_TarForge_Mobile.LI_TarForge_Mobile
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_Dwarf_Tomb_Arena.LI_Dwarf_Tomb_Arena
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_Hex_Tomb_Arena.LI_Hex_Tomb_Arena
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_Hub_Chappel_Room_Head.LI_Hub_Chappel_Room_Head
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_Hub_Chappel_Room_Hex.LI_Hub_Chappel_Room_Hex
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_Hub_Chappel_Room_LOW.LI_Hub_Chappel_Room_LOW
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_Hub_Chappel_Room_Moth.LI_Hub_Chappel_Room_Moth
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_Hub_Chappel_Room_Sword.LI_Hub_Chappel_Room_Sword
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_LOW_Tomb_Arena.LI_LOW_Tomb_Arena
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_Moth_Tomb_Arena.LI_Moth_Tomb_Arena
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_Mouth_Tomb_Arena.LI_Mouth_Tomb_Arena
/Game/Sparta/Maps/MAPS/LI/BossRuns/LI_SM_Tomb_Arena.LI_SM_Tomb_Arena
/Game/Sparta/Maps/MAPS/LI/ChasmEncounters/…            (56 packages under 23 named encounter folders × LVL1/2/3)
/Game/Sparta/Maps/MAPS/LI/ForestEncounters/…           (63 packages, e.g. …/Minor/Brigand/GraveHoles/LI_FW_B_LVL1_GraveDigger)
/Game/Sparta/Maps/MAPS/LI/LI_DoomOGenemyPlacement.LI_DoomOGenemyPlacement
/Game/Sparta/Maps/MAPS/LI/LI_FogBoss_Blk.LI_FogBoss_Blk
/Game/Sparta/Maps/MAPS/LI/LI_Hexapod_Arena_CoreWorld.LI_Hexapod_Arena_CoreWorld
/Game/Sparta/Maps/MAPS/LI/LI_Hub_Chappel_Room_01.LI_Hub_Chappel_Room_01
/Game/Sparta/Maps/MAPS/LI/LI_IcePostBoss_Dungeon.LI_IcePostBoss_Dungeon
/Game/Sparta/Maps/MAPS/LI/PrologueEncounters/…          (6 packages)
/Game/Sparta/Maps/MAPS/Menu/L_MainMenu.L_MainMenu
/Game/Sparta/Maps/MAPS/Menu/L_Main_Menu_Background_01.L_Main_Menu_Background_01
/Game/Sparta/Maps/MAPS/ShellMemory/L_Core_ShellMemory.L_Core_ShellMemory
/Game/Sparta/Maps/MAPS/TheForest/L_Forest_Hollows.L_Forest_Hollows
/Game/Sparta/Maps/Playgrounds/DungeonLayouts/LI_Tomb_Layout_01.LI_Tomb_Layout_01   (…125 packages total:
    LI_Tomb_Layout_01..19,21,22,24..35 each with up to 3 *_Event_0N companions, LI_Tomb_Medium_Layout_01..17,
    19..24,26,27, FC1..FC4, LI_Tomb_Sml_Arena_FC5..FC7, L_Tomb_Default_S1/S2, LI_BossAreaDungeon_01,
    LI_GenessaSenser, LI_Tomb_TowerOfInfinity)
/Game/Sparta/Maps/Playgrounds/LI_End_FinalArea.LI_End_FinalArea
/Game/Sparta/Maps/Playgrounds/LI_Test_MapStation.LI_Test_MapStation
/Game/Sparta/Maps/Playgrounds/NarrativeEvents/Hub/LI_Cultists_Hub_01.LI_Cultists_Hub_01
/Game/Sparta/Maps/Playgrounds/NarrativeEvents/MajorNarrativeEvents/EgonInteractions/Lazlo/ThePlea/LI_Egon_Lazlo_02.LI_Egon_Lazlo_02
/Game/Sparta/Maps/Playgrounds/NarrativeEvents/MinorNarrativeEvents/LI_Gorf_10.LI_Gorf_10
/Game/Sparta/Maps/Temp/HexapodTestArena.HexapodTestArena
```

(the abbreviated `…` entries are folder groups; the full verbatim 371-line list is
`slices\qp-worlds-clean.txt`.)

Exact counts per folder (computed from `slices\qp-worlds-clean.txt`):

| Folder prefix | Count |
|---|---|
| `/Game/Sparta/Maps/MAPS/LI` | 151 — of which `ForestEncounters` 63, `ChasmEncounters` 56, `BossRuns` 11, `PrologueEncounters` 6, `Templates` 3, loose `LI_*` files 12 |
| `/Game/Sparta/Maps/Playgrounds/DungeonLayouts` | 125 |
| `/Game/Sparta/Maps/Playgrounds/NarrativeEvents` | 55 — of which `MinorNarrativeEvents` 38, `MajorNarrativeEvents` 13, `Hub` 3, `Memories` 1 |
| `/Game/Sparta/Maps/Playgrounds/*.L_*` (loose files) | 31 |
| `/Game/Sparta/Maps/Playgrounds/MiniBossEncArenas` | 1 |
| `/Game/Sparta/Maps/MAPS/Menu` | 2 |
| `/Game/Sparta/Maps/MAPS/TheForest` | 1 |
| `/Game/Sparta/Maps/MAPS/ShellMemory` | 1 |
| `/Game/Sparta/Maps/Core_Levels` | 1 |
| `/Game/Sparta/Maps/AnimZoo` | 1 |
| `/Game/Sparta/Maps/Event_Instances/NonCombat` | 1 |
| `/Game/Sparta/Maps/Temp` | 1 |
| **Total** | **371** |

Evidence (`World` rows for the four maps that matter most):

```
[000001F4BFC22A10] World /Game/Sparta/Maps/MAPS/Menu/L_MainMenu.L_MainMenu [n: 1DCC5F] [c: 00007FF46EEE1338] [or: 000001F4A6CF2180]
[000001FA1B0B48A0] World /Game/Sparta/Maps/MAPS/ShellMemory/L_Core_ShellMemory.L_Core_ShellMemory [n: 45F724] [c: 00007FF46EEE1338] [or: 000001FA1B056230]
[000001FA1B0B1FE0] World /Game/Sparta/Maps/MAPS/TheForest/L_Forest_Hollows.L_Forest_Hollows [n: 221AF0] [c: 00007FF46EEE1338] [or: 000001FA1B053DF0]
```

```
[000001F976A9EBA0] World /Game/Sparta/Maps/AnimZoo/L_AnimZoo_FinalBoss_Intro.L_AnimZoo_FinalBoss_Intro [n: 3C47E6] [c: 00007FF46EEE1338] [or: 000001F9758DA6F0]
[000001FA098448A0] World /Game/Sparta/Maps/Playgrounds/DungeonLayouts/LI_Tomb_Layout_01.LI_Tomb_Layout_01 [n: 8202D] [c: 00007FF46EEE1338] [or: 000001FA29026460]
```

**Large persistent map vs. many level instances**: `L_Core_World_01` is the only map with a World Partition
cell explosion (6576 `_Generated_` packages under it). GUESS: it is the single overworld; `L_Core_ShellMemory` is
the shell-memory space, `L_Forest_Hollows` a separate forest map, and everything else (prefixed `LI_` / `L_Tomb_*`)
is a level instance streamed in at runtime. That mapping is inferred from names + the level-instance machinery in
§13.6, not stated by any dump row.

### 13.6 Level instances — how a dungeon/event is loaded

| Class | Parent | Key members (from `.hpp`) |
|---|---|---|
| `/Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector.BP_LevelInstanceSelector_C` | `/Script/Sparta.SpartaEventSpaceActor` | `PreviewEvent`, `Enabled`, `DrawDebug`, `EventType: ELevelSelectorType`, `LoadingPolicy: ELevelSelectorPolicy`, `SingleEvent`, `EventsOverride: TArray<USpartaEventDataAsset*>`, `OptionSet: UCSOptionSetDataAsset*`, `PreviewLevelInstance`, `LevelInstance: TSoftObjectPtr<UWorld>`, `LoadedLevel: ULevelStreamingDynamic*`, `OnFailedLoad` delegate, `bIsLevelLoaded`, `LastEventLoaded`, `Optional Level Streaming Class: TSubclassOf<ULevelStreamingDynamic>` |
| selector functions | | `LoadLevel(SelectedEvent: USpartaEventDataAsset*)`, `UnloadLevel()`, `SelectLevel(Success: bool&)`, `GetEventTransform(Event: USpartaEventDataAsset*&, NewParam: FTransform&)`, `OnLevelShown()`, `OnUnloadingComplete()`, `OnLoadingFailed()`, `LoadMe()`, `UnloadMe()`, `Event_Trigger()` |
| `/Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector_TombZone.BP_LevelInstanceSelector_TombZone_C` | `BP_LevelInstanceSelector_C` | `BPC_WorldMapObjective`, `WorldPartitionStreamingSource`, `TombZoneTrigger`, `DungeonSpawnRef: TSoftObjectPtr<ABP_DungeonSpawnLocations_C>`, `IsEntrance`, `DungeonEntranceRef`, `TombExitTeleportTarget: FTransform`, `TeleportToExit`, `OnTombZoneActivated/Deactivated` delegates, `StartupBehaviors`/`ExitDungeonByTeleportBehaviors: TArray<UBP_TeleportBehavior_Base_C*>`, `Weather Preset`, `ShouldOriginStayLoaded` |
| tomb-zone functions | | `InitiateTeleport()`, `TeleportPlayerToExit()`, `CommitActivationTeleport()`, `CommitDeactivationTeleport()`, `CommitTeleport()`, `GetDungeonTeleportData(Location: FVector&, Rotation: FRotator&, ZoneData: USpartaZoneData*&)`, `ActivateTombZone()`, `DeactivateTombZone()`, `LockDungeon()`, `PlayerExitByTeleport(EventData: UObject*)`, `TeleportAndDisableStreamingSource(StreamingSource: AStreamingSource*)`, `Teleport PlayerNoStreamingSource()`, `ExecuteStartupBehaviors()`, `DisableOriginStreamingSource()`, `EnableOriginStreamingSource()`, `SetDungeonLighting()`, `SetOverworldLighting()`, `ConstructMapActor()` |
| `/Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector_RestSite.BP_LevelInstanceSelector_RestSite_C` | `BP_LevelInstanceSelector_C` | rest-site variant (`LoadLevel`) |
| `/Game/Sparta/Core/World/Levels/BP_LevelInstanceLoader.BP_LevelInstanceLoader_C` | `/Script/Engine.Actor` | DEPRECATED class `ADEPRECATED_BP_LevelInstanceLoader_C`; `Level/Preview: TSoftClassPtr<ABP_LevelInstanceActor_Base_C>`, `LVA_WorldAssetSoftObject: TSoftObjectPtr<UWorld>`, `OnLevelInstanceLoaded` / `OnLevelInstanceShown` delegates; `LoadLevelInstance()` |
| `/Game/Sparta/Core/World/Levels/BP_LevelInstanceActor_Base.BP_LevelInstanceActor_Base_C` | `/Script/Engine.LevelInstance` | `WorldAssetSoftObject: TSoftObjectPtr<UWorld>`, `BiomeTag`, `TypeTag`, `SubTypeTag`, `SeasonTag`, `LevelInstanceTags: FGameplayTagContainer`, `LocationOffset`, `RotationOffset` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_DungeonTransition.BP_DungeonTransition_C` | — | `Teleport()` (dungeon transition helper) |

Evidence:

```
[000001F4EB56EAA0] BlueprintGeneratedClass /Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector.BP_LevelInstanceSelector_C [n: 133495] [c: 00007FF46EEC3D38] [or: 000001F4CEED38F0] [sps: 00007FF46F046F38]
[000001F8CB9BE380] BlueprintGeneratedClass /Game/Sparta/Core/World/Levels/BP_LevelInstanceLoader.BP_LevelInstanceLoader_C [n: 8575E] [c: 00007FF46EEC3D38] [or: 000001F8C384A740] [sps: 00007FF46EE52028]
```

Enums used by the selector (all `NewEnumerator*`, i.e. **their value names were stripped by the CXX generator**):

| Enum | Values |
|---|---|
| `/Script/CSOptionSet` / `ELevelSelectorType` | `NewEnumerator0=0, NewEnumerator1=1, NewEnumerator2=2` |
| `ELevelSelectorPolicy` | `NewEnumerator0=0, NewEnumerator1=1, NewEnumerator2=2` |
| `ELevelSubGroup` | `NewEnumerator0=0, NewEnumerator1=1, NewEnumerator2=2` |
| `ELevelBiome` | `NewEnumerator0=0, NewEnumerator1=1, NewEnumerator2=2` |
| `ETravelType` | `NewEnumerator0=0, NewEnumerator1=1` |
| `ETeleportMethod` | `NewEnumerator0=0, NewEnumerator1=1, NewEnumerator2=2` |

(from `raw\CXXHeaderDump\ELevelSelectorType_enums.hpp`, `…Policy…`, `…SubGroup…`, `ELevelBiome_enums.hpp`,
`ETravelType_enums.hpp`, `ETeleportMethod_enums.hpp`.) UNKNOWN: the semantic meaning of each enumerator.

---

## 14. Travel / teleport / fast travel / level load — every function

### 14.1 The canonical teleport helper (`BPFL_WorldStreaming`, BlueprintFunctionLibrary)

`/Game/Sparta/Core/Utility/BPFL_WorldStreaming.BPFL_WorldStreaming_C` — this is *the* game-wide "move the player
across the world with streaming" entry point:

| Function | Parameters (name: type) |
|---|---|
| `TeleportPlayerWithStreaming` | `Location: FVector`, `Rotation: FRotator`, `ControlRotation: FRotator`, `ScreenTransitionClass: TSubclassOf<UWBP_LS_TransitionBase_C>`, `TransitionZOrder: int32`, `FadeInDuration: double`, `FadeOutDuration: double`, `OptionalZoneData: USpartaZoneData*`, `__WorldContext: UObject*` |
| `ResolveDungeonSpawnLocation` | `DungeonSpawn: TSoftObjectPtr<ABP_DungeonSpawnLocations_C>`, `__WorldContext: UObject*`, `Success: bool&`, `ZoneData: USpartaZoneData*&` |
| `BroadcastLeaveDungeonByTeleport` | `LeaveTag: FGameplayTag`, `__WorldContext: UObject*` |
| `UpdatePlayerCharacterSource (NonBlocking)` | `UpdateEnabledState: bool`, `UpdateShape: bool`, `UpdatePriority: bool`, `SourceEnabled: bool`, `ShapeSettings: FStreamingSourceShape&`, `ShapeIndex: int32`, `SourcePriority: EStreamingSourcePriority`, `__WorldContext: UObject*` |
| `UpdatePlayerCharacterSource (Blocking)` | as above + `UpdateBlocking: bool`, `Blocking: bool` |
| `UpdatePlayerControllerSource` | `UpdateEnabledState`, `UpdateShape`, `UpdatePriority`, `SourceEnabled`, `ShapeSettings`, `ShapeIndex`, `SourcePriority`, `__WorldContext` |
| `DefaultPlayerCharacterSource (Blocking)` | `__WorldContext: UObject*`, `SourceEnabled: bool&`, `Shape: FStreamingSourceShape&`, `Priority: EStreamingSourcePriority&` |
| `DefaultPlayerCharacterSource (NonBlocking)` | same |
| `DefaultPlayerControllerSourceSettings` | same |

Corresponding global event (`/Game/Sparta/Core/GES/GlobalEvent_TeleportPlayerWithStreaming`), payload struct
`UGlobalEvent_TeleportPlayerWithStreaming_C`:

| Field | Type | Offset |
|---|---|---|
| `Location` | `FVector` | `0x28` |
| `Rotation` | `FRotator` | `0x40` |
| `ControlRotation` | `FRotator` | `0x58` |
| `ScreenTransitionClass` | `TSubclassOf<UWBP_LS_TransitionBase_C>` | `0x70` |
| `FadeInDuration` / `FadeOutDuration` | `double` | `0x78` / `0x80` |
| `OptionalZoneData` | `USpartaZoneData*` | `0x88` |
| `ZOrder` | `int32` | `0x90` |

Evidence:

```
[000001F4EB56E380] BlueprintGeneratedClass /Game/Sparta/Core/Utility/BPFL_WorldStreaming.BPFL_WorldStreaming_C [n: 2DF4D7] [c: 00007FF46EEC3D38] [or: 000001F4D1C146B0] [sps: 00007FF46EE70138]
```

### 14.2 The ability that performs it (`GA_TeleportPlayerWithStreamingSupport`)

`/Game/Sparta/Core/Player/Ability/GA_TeleportPlayerWithStreamingSupport.GA_TeleportPlayerWithStreamingSupport_C`
(parent `GA_SpartaAbility_C`): `SpawnedStreamingSource: ABP_BlockingStreamingSource_C* @0x518`,
`EventData: UGlobalEvent_TeleportPlayerWithStreaming_C* @0x520`, `CachedFadeOutDuration @0x528`,
`MySpartaCharacter @0x530`, `Invulnerability: FActiveGameplayEffectHandle @0x538`,
`FallDamageImmunity @0x540`.

Functions: `Teleport()`, `CommitTeleport()`, `CompleteTeleportAfterDelay()`, `FadeOutCompleted()`, `FadeInCompleted()`,
`HandleFadeIn(OnCompleted: const FHandleFadeInOnCompleted&)`, `SpawnStreamingSource()`, `RemoveStreamingSource()`,
`BindOnActorStreamingCompleted()`, `OnActorStreamingCompleted(StreamingSource: AStreamingSource*)`,
`OnTeleportComplete()`, `BroadcastZoneChange()`, `UnblockPlayer()`, `ResetHUD()`, `ResetState()`,
`CacheOwnerCharacter()`, `Initialize()`, `SpartaAbilityCommitted(CommittedFromEvent: bool, EventData: FGameplayEventData)`.

Evidence:

```
[000001F617FAB8C0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_TeleportPlayerWithStreamingSupport.GA_TeleportPlayerWithStreamingSupport_C [n: 1C116E] [c: 00007FF46EEC3D38] [or: 000001F4CEED0060] [sps: 000001F4DC018E00]
```

### 14.3 `BP_SpartaTeleportHandler` — walk-in portal handler

`/Game/Sparta/Core/World/Teleporting/BP_SpartaTeleportHandler.BP_SpartaTeleportHandler_C` (`AActor`).
Critical members: `Spline`, `ProximityDetection: USphereComponent*`, `My Streaming Source: USpartaStreamingSourceComponent*`,
`BiomeTag`, `TeleportMethod: ETeleportMethod @0x2ec`, `ActivationDistance @0x2f0`, `RelevancyAngle @0x2f8`,
`RelevancyHeightRange @0x3a0`, `LoadedLinkHandler: ABP_SpartaTeleportHandler_C* @0x3b0`,
`ActivationBehaviors: TArray<UExecutableBehavior*> @0x3d8`, `PostTeleportDelay @0x3e8`,
`OnTeleportEvent: delegate @0x3f0` (`OnTeleportEvent__DelegateSignature(Location: FVector, Rotation: FRotator)`),
`SreamingRadius @0x400`, `BlockOnSlowStreaming @0x408`, `IsInRange/IsRelevant/IsTeleporting @0x409-0x40b`,
`TeleportLocationAlignmentMarker: TSoftObjectPtr<AActor> @0x418`, `MyZoneData: USpartaZoneData* @0x440`,
`TransitionClass: TSubclassOf<UWBP_LS_TransitionBase_C> @0x448`, `TransitionDuration (In)/(Out) @0x450/0x458`,
`bHasAutomaticMovement @0x460`.

Teleport-flow functions (in call order as named): `RelevancyCheck()`, `AngleCheck()`, `HeightCheck()`,
`DistanceCheck()`, `TeleportCheck()`, `IsCrossingTeleportDetection(Point, PortalLocation, PortalNormal)`,
`GetLinkedHandler()`, `LoadLinkedHandler()`, `GetSeamlessTeleportLocation()`, `GetSeamlessTeleportRotation()`,
`GetSeamlessTeleportControlRotation()`, `CommitSeamlessTeleport()`, `InitializeScreenTransitionTeleport()`,
`CommitScreenTransitionTeleport()`, `ResetScreenTransitionTeleport()`, `PreTeleportEvent()`, `TeleportCompleteEvent()`,
`PostDelayTeleportEvent()`, `PostTeleportEvent()`, `CommitBehaviours()`, `OnTeleportCommitted()`,
`OnTeleportCompleted()`, `OnTeleportPreCompleted()`, `InitPostTeleportDelay()`, `AddAutomaticMovement(Direction)`,
`StopAutomaticMovement()`, `EnableStreaming()`/`DisableStreaming()`, `Enable()`/`Disable()`,
`UpdatePriority()`/`CalculatePriority()`/`ResetPriority()`, `EnableKeepLoaded()`/`DisableKeepLoaded()`,
`GetOptionalTeleportDestination(Success: bool&, ZoneData: USpartaZoneData*&)`, `CacheMyLocation()`,
`CacheMyTransform()`, `VisualizeLink()`, `PlaySFX()`, `PlayScreenEffect()`, `UpdateVelocity()`,
`TeleportDetectionOpen()`/`TeleportDetectionClose()`/`TeleportDetectionTick()`, `CacheHandlerInManager()`.

A manager component collects them all:
`/Game/Sparta/Core/Player/Components/BPC_TeleportManager.BPC_TeleportManager_C` (`UActorComponent`) with
`AllHandlers: TArray<ABP_SpartaTeleportHandler_C*> @0xc0`, `CachedCharacter @0xd0`, `CameraState @0xd8`,
`CurrentDungeonEvent: USpartaEventDataAsset* @0xe0`; functions `AddHandler(NewItem: ABP_SpartaTeleportHandler_C*&)`,
`HandleCameraState()`.

Teleport-handler subclasses (`BP_STH_*`, `/Game/Sparta/Core/World/Teleporting/`):
`BP_STH_Entrance_Dungeon`, `BP_STH_Entrance_Gate`, `BP_STH_Entrance_Well`, `BP_STH_Exit`,
`BP_STH_Exit_Dungeon`, `BP_STH_Exit_Gate`, `BP_STH_Exit_Well` — plus the instanced-object family
`BPO_STH`, `BPO_STH_Beacon`, `BPO_STH_Beacon_MainBoss`, `BPO_STH_Generic` (§11).
The interface `/Script/Sparta.SpartaTeleportLocationInterface:Teleport` is the shared entry point.

Evidence:

```
[000001F617BE0A30] BlueprintGeneratedClass /Game/Sparta/Core/World/Teleporting/BP_SpartaTeleportHandler.BP_SpartaTeleportHandler_C [n: A3C8C] [c: 00007FF46EEC3D38] [or: 000001F4D1C17590] [sps: 00007FF46EE52028]
```

```
[000001F61A1D5300] MulticastInlineDelegateProperty /Game/Sparta/Core/World/Teleporting/BP_SpartaTeleportHandler.BP_SpartaTeleportHandler_C:OnTeleportEvent [o: 3F0] [n: 666E76] [c: 00007FF721DE9010] [owr: 000001F617BE0A30] [df: 000001F61A1C8F00]
[000001F61A1C8F00] Function /Game/Sparta/Core/World/Teleporting/BP_SpartaTeleportHandler.BP_SpartaTeleportHandler_C:OnTeleportEvent__DelegateSignature [n: 666B8E] [c: 00007FF46EE55538] [or: 000001F617BE0A30] [f: 00007FF7183D9710]
[000001F4C315EE80] StructProperty /Game/Sparta/Core/World/Teleporting/BP_SpartaTeleportHandler.BP_SpartaTeleportHandler_C:OnTeleportEvent__DelegateSignature:Location [o: 0] [n: 3F9] [c: 00007FF721DF9890] [owr: 000001F61A1C8F00] [ss: 00007FF46F2E3948]
```

### 14.4 Fast travel

| Class / function | Parent | Details |
|---|---|---|
| `/Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_FastTravel.GA_Traversal_FastTravel_C` | `GA_PlayMontageBase_C` | `CameraStateClass @0x628`, `MyCameraState @0x630`; functions `OnFastTravelNotify()`, `OnPreFastTravelNotify()`, `OnFadeToBlack()`, `AddCameraState()`, `RemoveCameraState()`, `SpartaAbilityCommitted(CommittedFromEvent: bool, EventData: FGameplayEventData)`, `K2_OnEndAbility(bWasCancelled: bool)` |
| `GA_Traversal_FastTravel_HUB_C`, `GA_Traversal_FastTravel_HUB_LoseGloom_C` | `GA_Traversal_FastTravel_C` | HUB variants |
| `/Game/Sparta/Core/World/Launch/BP_FastTravelPoint.BP_FastTravelPoint_C` | `ASpartaPlayerStart` | `DisplayName: FText @0x2f8`; functions `CanFastTravel()`, `GetAreaName()`, `EligibleForFastTravel()` — **this is the fast-travel anchor actor** |
| `/Game/Sparta/UI/Menu/…/WBP_MGT_FastTravel.WBP_MGT_FastTravel_C` | widget | the fast-travel menu (UI = another agent's area, listed for completeness) |
| unlock gameplay effects | | `GE_Unlock_FastTravel`, `GE_Unlock_WorldMap`, `GE_Unlock_MapPing` (`raw\CXXHeaderDump\GE_Unlock_FastTravel.hpp` etc.) |
| item fragments | | `ID_FastTravel.hpp`, `ID_MapUnlock.hpp`, `ID_Unlock_MapPing.hpp` (fast-travel/map unlocks are **items**) |

Evidence:

```
[000001F8D3B1B1A0] BlueprintGeneratedClass /Game/Sparta/Core/World/Launch/BP_FastTravelPoint.BP_FastTravelPoint_C [n: 31D236] [c: 00007FF46EEC3D38] [or: 000001F8C3841820] [sps: 00007FF46F055D38]
[000001F8D2BE3E30] TextProperty /Game/Sparta/Core/World/Launch/BP_FastTravelPoint.BP_FastTravelPoint_C:DisplayName [o: 2F8] [n: 104CF] [c: 00007FF721DFAB80] [owr: 000001F8D3B1B1A0]
```

(the `sps:` target `00007FF46F055D38` is `/Script/Sparta.SpartaPlayerStart`, per
`parse_dump.py show … "/Script/Sparta.SpartaPlayerStart"`; the `DisplayName` offset `0x2f8` matches
`BP_FastTravelPoint.hpp`.)

`GA_Traversal_FastTravel` / `BPC_TeleportManager` class paths (from
`parse_dump.py find … "(GA_Traversal_FastTravel|BPC_TeleportManager)\.\w+_C$" --types BlueprintGeneratedClass`):

```
BlueprintGeneratedClass	/Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_FastTravel.GA_Traversal_FastTravel_C
BlueprintGeneratedClass	/Game/Sparta/Core/Player/Components/BPC_TeleportManager.BPC_TeleportManager_C
```

### 14.5 Other teleport/level-load entry points found (complete list from the function-name search)

Search: `scripts\q\qp-travel.sql` — every `Function` whose own name matches
`Travel|Teleport|LoadMap|OpenLevel|SetLevel|StreamingLevel|Respawn|FastTravel|Warp|ZoneTransition|Transition|LoadLevel|UnloadLevel|Shelleport|Seamless|LoadStreamLevel|ChangeLevel|GoToMap`
(90 rows). The **complete verbatim 90-row path list** is `slices\qp-travelfuncs.txt`; the table below abbreviates
only the repeated `/Game/Sparta/...` prefix inside a cell (the `…` never stands for a different class).

| Path | Note |
|---|---|
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_BagTeleport.BP_BagTeleport_C:TeleportToPrison` | scripted teleport |
| `…BP_Interactable_Mushroom_FirstPersonEntrance…:TeleportPlayerToMushroomDungeon` | memory/mushroom entry |
| `…BP_Interactable_TarsporeMushroom…:Teleport` | |
| `/Game/Sparta/Core/Interaction/BP_FP_Jumpscare…:TeleportPlayerBack` | |
| `/Game/Sparta/Core/Interaction/BP_Interaction_Ovum…:TeleportToDungeonEntrance`, `…_Boss…`, `…BP_Interaction_P_LeaveDungeon…` | ovum / leave-dungeon |
| `/Game/Sparta/Core/Player/Ability/GA_TeleportPlayerWithStreamingSupport…:Teleport` | §14.2 |
| `/Game/Sparta/Core/Player/BP_PlayerController…:TeleportPerfTest` | dev |
| `/Game/Sparta/Core/Utility/BPFL_WorldStreaming…:TeleportPlayerWithStreaming` | §14.1 |
| `/Game/Sparta/Core/World/BP_TeleportLocation…:Teleport` | §14.6 |
| `/Game/Sparta/Core/World/FogArea/BP_CinematicPlayer…:TeleportPlayer` | |
| `/Game/Sparta/Core/World/LauncherDevice/BP_SkydiveHandler…:TeleportPlayer` / `:TeleportSelf` | skydive/launcher |
| `/Game/Sparta/Core/World/Levels/BP_DungeonEventSelector…:LoadLevel` | level instance load |
| `/Game/Sparta/Core/World/Levels/BP_LevelInstanceLoader…:LoadLevelInstance` | |
| `/Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector…:LoadLevel` / `:UnloadLevel` | |
| `…BP_LevelInstanceSelector_RestSite…:LoadLevel` | |
| `…BP_LevelInstanceSelector_TombZone…:Teleport` / `:TeleportAndDisableStreamingSource` / `:TeleportPlayerToExit` | |
| `/Game/Sparta/Dialogue/Blueprints/ShellMemories/Blueprints/BP_ShellMemory…:UnloadLevelInstance` | memory space |
| `…BP_ShellMemoryHandler…:TeleportToDestination` / `:TeleportToOrigin` | **memory-space in/out teleport** |
| `/Game/Sparta/Dialogue/NPCs/BP_NPC_Genessa_Ghost…:TeleportAfterPlayer` / `:TeleportBegins` / `:TeleportInFrontOfPlayer` | NPC moves with player |
| `/Game/Sparta/Dialogue/Blueprints/ShellMemories/Blueprints/BP_NPCTeleportLocator` (+~20 subclasses) | NPC teleport targets |
| `/Game/Sparta/Environments/Interactive/Traversal/BP_Teleport.BP_Teleport_C:Teleport` | traversal-node teleport (§14.7) |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_DungeonTransition…:Teleport` | |
| `…BP_LevelScript_FinalBossManager…:TeleportToCentral` / `:TeleportToEndStranding` | final-boss scripted teleports |
| `/Game/Sparta/UI/MainMenu/WBP_MainMenu…:LoadMap` | `LoadMap` on the main menu = the actual map load from UI |
| `/Game/Sparta/UI/Menu/Demo/WBP_DemoEnd_Notify…:LoadMap` | demo end |
| `/Script/Engine.GameplayStatics:LoadStreamLevel` / `:LoadStreamLevelBySoftObjectPtr` / `:OpenLevel` / `:OpenLevelBySoftObjectPtr` | engine-provided |
| `/Script/Engine.LevelStreamingDynamic:LoadLevelInstance` / `:LoadLevelInstanceBySoftObjectPtr`, `/Script/Engine.LevelInstanceInterface:LoadLevelInstance` / `:UnloadLevelInstance` | engine-provided |
| `/Script/Sparta.SpartaTeleportLocationInterface:Teleport` | game interface |
| `/Script/Engine.CheatManager:Teleport` | engine cheat |

### 14.6 `BP_TeleportLocation` / traversal nodes

| Class | Parent | Members |
|---|---|---|
| `/Game/Sparta/Core/World/BP_TeleportLocation.BP_TeleportLocation_C` | `AActor` | `WorldPartitionStreamingSource: UWorldPartitionStreamingSourceComponent* @0x2b0`, `TextRender`, `Location`, `Root`, `Billboard`, `Category: FString @0x2d8`, `LocationName: FString @0x2e8`, `TeleportFadeTime: double @0x2f8`, `CheckStreamingStateTimerHandle @0x300`; functions `Teleport()`, `OnTeleport()`, `CheckStreamingState()`, `OnStreamingComplete()`, `GetCategory()`, `GetLocationName()` |
| `…BP_TeleportLocation_OneShot_C` | `BP_TeleportLocation_C` | overrides `OnTeleport`, `OnStreamingComplete`, `GetLocationName` |
| `…BP_TeleportLocation_PerfTest_C` | `BP_TeleportLocation_C` | dev |
| `/Game/Sparta/Environments/Interactive/Traversal/BP_Teleport.BP_Teleport_C` | `ASpartaTraversalNode` | `Destination: TSoftObjectPtr<ABP_Teleport_Destination_C> @0x330`, `UnlockFromSpawnTrigger: ABP_SpawnTrigger_C* @0x358`, `StartLocked @0x360`, `In Data Layer: FActorDataLayer @0x364`, `Unload @0x36c`, `StreamingSource: AStreamingSource* @0x370`; functions `Teleport()`, `StreamingCompleted(StreamingSource: AStreamingSource*)`, `UnlockMe()`, `GetConnectedTraversalNodes()` |
| `…BP_Teleport_Destination.BP_Teleport_Destination_C` | `ASpartaTraversalNode` | `ConnectedTraversalNodes: TArray<TSoftObjectPtr<ASpartaTraversalNode>> @0x2b8` |
| `…BP_TeleportBehavior_Base_C` | `UExecutableBehavior` | empty base; subclasses `BP_TeleportBehavior_DestroyActor`, `…_SetActorVisibility`, `…_SetMusic` (`SwitchGroup: UAudioSwitchGroup* @0x30`, `SwitchValue: FAudioSwitchState @0x38`) |
| `…BP_MemoryTeleporter.BP_MemoryTeleporter_C` | `AActor` | 3 components only; two `OnEventFired_*` handlers (`GlobalEvent_ShellMemory_TeleportPlayer` / `…_BeginInteract`) |
| `…BP_TombEntranceTeleporter.BP_TombEntranceTeleporter_C` | `AActor` | `Box` overlap → teleport |
| `…BP_EndOfDungeonTeleporter.BP_EndOfDungeonTeleporter_C` | `ABP_Interaction_C` | `LocationToTeleport: ATargetPoint*`, `WeaponUnlock`, `ShellToUnlock`, `EnemyRequired: ABP_Spawner_C*`, `ShowTeleport()`, `SafeGuardForEnemyDead()` |
| `…BP_Teleporter_PM7_S1.BP_Teleporter_PM7_S1_C` | `AActor` | box trigger → `MerrickActor` |

Evidence:

```
[000001F76C156DF0] BlueprintGeneratedClass /Game/Sparta/Core/World/BP_TeleportLocation.BP_TeleportLocation_C [n: 17FCC6] [c: 00007FF46EEC3D38] [or: 000001F763863710] [sps: 00007FF46EE52028]
```

---

## 15. Console commands

The dump does **not** mark `UFUNCTION(exec)`. What exists is a `CheatManager` subclass — every function on it is a
console command in practice, so the list is marked as inferred:

`/Script/Sparta.SpartaCheatManager` (parent `/Script/Engine.CheatManager`), 15 functions, GUESS-exec:

| Function | Parameters (name: type) |
|---|---|
| `WriteCurrentSaveToTextFile` | (none) |
| `SaveSessionNow` | (none) |
| `ResetSession` | (none) |
| `ResetAllSaveData` | (none) |
| `EnableProfilingMode` | (none) |
| `DLG_PrintActiveDialogues` | (none) |
| `UnlockEntitlement` | `EntitlementRow: StrProperty` |
| `UnlockAllEntitlements` | (none) |
| `RemoveEntitlement` | `EntitlementRow: StrProperty` |
| `RemoveAllEntitlements` | (none) |
| `DestroyAllEnemies` | (none) |
| `DestroyAllAI` | (none) |
| `Debug_CheckPS5DemoFiles` | (none) |
| `Debug_CheckNonStreamedTextures` | `Filename: StrProperty` |
| `Debug_CheckFallbackMeshes` | `Filename: StrProperty` |

Additionally, DlgSystem registers its own console commands at runtime:
`/Script/DlgSystem.DlgManager:RegisterDialogueConsoleCommands` / `:UnregisterDialogueConsoleCommands`
(`ReturnValue: BoolProperty`) — the actual command names are created at runtime and are **UNKNOWN**.

Evidence:

```
[00007FF46F043F38] Class /Script/Sparta.SpartaCheatManager [n: 48D95] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF37B38]
```

---

## 16. UNKNOWNs and open questions (consolidated)

| # | UNKNOWN | What I tried |
|---|---|---|
| 1 | **Scope (per-player vs per-world) of the `BPSO_*` world objects** (gates, chests, bosses, NPC flags). The engine has both a per-profile `PlayerSaveObject` and a `SpartaSaveGame` world state, but no dump line says which one owns e.g. `BPSO_Gate_C`. | `qp-bpso.sql`, `parse_dump.py show … SpartaSaveSubSystem`, `… SpartaSaveGame`; no field references a save-object class per scope. Resolve in game by comparing two save files. |
| 2 | **DataTable row structs** for `DT_ShellMemories`, `DT_Achievements`, `DT_LoreText`, `DT_FakeAchievements`, `DT_NarrativeArtifactsNew`, `DT_TextStyle_*`, `DT_WeaponProgression`, `DT_SpartaGiftEventLeader`, `DT_SurpriseEggData`. Only `QuestDetails` is provable (`TableRowBase`). | `qp-rowstructs2.sql` (all `TableRowBase` structs), `qp-game-structs.sql` (all `/Game` structs = only AnimBP data), `parse_dump.py raw … "DT_QuestData1"`. DataTable assets carry no `ss:` attribute in this dump. |
| 3 | **`SpartaAreaId` / `SpartaActivityType` content is empty** — only CDOs exist. Whether the area/activity system runs at all in this build. | `qp-da3.sql` → counts 1 / 1, both `Default__`. |
| 4 | **`SpartaCompletionSubsystem` / `SpartaRunManagerSubsystem` persistence.** No save object found for either. | `qp-dataassets2.sql`, `qp-bpso.sql`; no `BPSO_*` for them. |
| 5 | **Which `AWorld` is the "current map" at runtime** (the dump was taken at the main menu, and `World` rows exist for all 371 maps + 6576 cells regardless of load state). The persist-travel path is `WBP_MainMenu_C:LoadMap` → `OpenLevel*`, but the parameter values are not in the dump. | `parse_dump.py raw … "L_MainMenu$"`, `qp-worlds.sql`; no live-world marker. |
| 6 | **`SpartaSaveGame`'s per-actor maps are keyed by `FName`** (actor name) — whether that name is stable across runs (and therefore usable as a sync key) is UNKNOWN. | `qp-questlog-refs.sql` on `SpartaMapBool/Int/Float/Name`; the wrapper struct holds one `Map` member at offset 0, key type `NameProperty`. |
| 7 | **`ESaveObjectProtocol` enumerator meanings** (`/Script/Sparta.ESaveObjectProtocol`, used by `SpartaSaveComponent:SaveProtocol @0xf8`) — the CXX enum dump gives only `NewEnumeratorN`. | `raw\CXXHeaderDump\ESaveObjectProtocol_enums.hpp` not read in detail; see `Sparta_enums.hpp`. |
| 8 | **Semantics of `ETravelType`, `ETeleportMethod`, `ELevelSelectorType`, `ELevelSelectorPolicy`, `ELevelSubGroup`, `ELevelBiome`** — all enumerators are `NewEnumerator0..N`. | `raw\CXXHeaderDump\E*_enums.hpp`. |
| 9 | **How `BPC_QuestLogComponent_C:ImmortalBrigSpoils` / `WormfishGold` / `TotalGoldSpentMerchant` / `NPCRelationships` keys are formed** (`FString` keys) — the key strings are runtime values, not in the dump. | raw rows for each map (`parse_dump.py raw … "NPCRelationships\."` etc.), key type `StrProperty` at offset 0. |
| 10 | **`NPCRelationships`/`NarrativeEvents` value semantics** (a relationship level? a talk count?). | no accessor function on the class; only the map itself. |
| 11 | **Whether dialogue replication already exists** — DlgSystem has `ChooseSpeechSequenceOptionFromReplicated(OptionIndex)` and `DlgContext:OnRep_SerializedParticipants` + `SerializedParticipants`, i.e. the plugin has its own replication hooks. Whether the MS2 build enables them (single-player shipping build) is UNKNOWN. | `parse_dump.py show … "/Script/DlgSystem.DlgContext"`; presence proven, enablement not. |
| 12 | **`SpartaDialogueActor` audio mapping** (`GameplayTagToAudioComponentMap`) — pre-initialised at begin-play; ordering vs. co-op joining mid-dialogue is UNKNOWN. | part 1 §6.3. |
| 13 | Console command **names** (exec strings) — the dump has the `UFunction` names but not the registered console tokens (they are usually the same lower-cased name, but that is a GUESS). | §15. |
| 14 | **Which save object holds "fast travel unlocked per beacon"** — `BPO_STH_Beacon` family is `SpartaInstancedObject`, not a save object; `BPSO_LandingArea_Boss_C:BeaconRisen` is the closest proven flag, but the unlock list itself is UNKNOWN (candidates: `BP_SpartaPlayerSaveObject_C:UnlockedLandingAreas`, or `BPSO_WorldMap_C`). | `qp-bpso.sql`, `parse_dump.py show … "BPO_STH"`. |

---

### Appendix A — queries created for this report

| File | Purpose |
|---|---|
| `scripts\q\qp-classes.sql` | quest/dialogue/NPC/keyword class enumeration (1246 rows) |
| `scripts\q\qp-focus.txt` (slice) | the subset of the above that matches quest/objective/dialogue/achievement/activity |
| `scripts\q\qp-questlog-refs.sql` | resolve the quest-log component's container/map types + `SpartaSave*` struct members |
| `scripts\q\qp-refs.sql` | resolve `ActiveDialogue`, `SaveObjectTickFunction`, `ShellSkillSet`, `SpartaActivityType`, `SpartaZoneData`, `SpartaSaveContext` members |
| `scripts\q\qp-datatables.sql` | every `DataTable` under `/Game` (251 rows) |
| `scripts\q\qp-rowstructs2.sql` | every struct deriving from `/Script/Engine.TableRowBase` (30 rows) |
| `scripts\q\qp-game-structs.sql` | every `ScriptStruct` under `/Game` (281 rows, all AnimBP data — the Blueprint-struct limitation) |
| `scripts\q\qp-bpso.sql` | every `BPSO_*` / `BPO_*` class (52 rows) |
| `scripts\q\qp-dataassets.sql`, `qp-dataassets2.sql`, `qp-da3.sql` | DataAsset/DataTable census + per-class counts |
| `scripts\q\qp-maps.sql`, `qp-mapcensus.sql`, `qp-worlds.sql` | map object census, non-generated `World` list (371), WorldPartition type counts |
| `scripts\q\qp-travel.sql` | every travel/teleport/level-load function (90 rows) |
| `scripts\q\qp-dlgcount.sql`, `qp-dlgassets.sql` | `DlgDialogue` count (977) + folder distribution + console/debug classes |
| `scripts\q\qp-addr.sql` | resolve raw addresses (delegate-signature targets, struct parents) |
