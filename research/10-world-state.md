# 10 — World state: things in the world that change and stay changed

Mortal Shell II, UE 5.6 shipping build, UE4SS object dumps. Scope: doors, gates, levers, switches,
elevators/lifts, shortcuts, breakable walls, chests, item pickups, ladders, bridges, bonfires /
checkpoints / rest points, fog walls, boss-arena gates — **and the save / world-state system that
records them**.

This report is facts + evidence. Nothing here is a design proposal. `GUESS:` marks interpretation.
`UNKNOWN` marks something I could not establish, always followed by what I tried.

---

## 0. Provenance and method (read this before trusting a number)

| Input | What it is | Size / rows |
|---|---|---|
| `raw\ObjectDump_allassets.txt` | UE4SS object dump taken **at the main menu** with `LoadAllAssetsBeforeDumpingObjects=1` | 565 MB, 2 170 742 lines |
| `raw\index.sqlite3` | SQLite index of the above (`scripts\parse_dump.py index`) | 2 153 654 rows |
| `raw\ObjectDump_ingame.txt` | UE4SS object dump taken **in gameplay** | 126 MB (126 087 083 B) |
| `raw\index_ingame.sqlite3` | SQLite index of the in-game dump — **built by me during this task** (`python scripts\parse_dump.py index raw\ObjectDump_ingame.txt raw\index_ingame.sqlite3`) | 536 682 objects |
| `raw\CXXHeaderDump\Sparta.hpp` | native `Sparta` module: member offsets + declaration order | 513 017 B |
| `raw\CXXHeaderDump\Sparta_enums.hpp` | native enums | 16 165 B |
| `raw\CXXHeaderDump\<Class>.hpp` | one small header per Blueprint class (props with offsets, functions in declaration order) | 10 415 files |

Method, in order, for every class below:

1. enumerate candidates with SQL over the `cls` view (`scripts\q\ws-*.sql`) and
   `parse_dump.py classes --regex`;
2. read the class' small `.hpp` for layout;
3. cross-check offsets against the dump (`[o: xx]` is hex, printed without `0x`);
4. copy evidence lines from `parse_dump.py raw`.

Three mechanical caveats that affect how you read the evidence blocks:

* `parse_dump.py raw` re-emits `path` + `rest`, and `rest` was `.strip()`ed at index time, so the
  single space between the object path and `[n: …]` is **missing** in the quoted lines but present
  in the original file. Example, original:
  `[00007FF46F038338] Class /Script/Sparta.SpartaSaveObject [n: 47C43] [c: …] [or: …] [sps: …]`
* `parse_dump.py fun` filters on `type IN ("Class","Struct","Function","DelegateFunction")` and
  therefore **silently returns nothing for Blueprint classes**. Blueprint function signatures in
  this report come from `CXXHeaderDump\*.hpp` and from SQL over `obj` (`owner` = the Function),
  both of which are dump-derived.
* Blueprint compiler temporaries are filtered out of every parameter list below. Filtered prefixes:
  `Temp_`, `CallFunc_`, `K2Node_`, `ExecuteUbergraph_`, `Goto_`, `eb_`, `bpf_`, `__Internal_`,
  `BndEvt__*`. Where a parameter is named e.g. `K2Node_Event_SaveContext` the dump is telling us the
  Blueprint *overrides* a native event with that payload — I keep those only when they prove the
  override exists.

---

## 1. THE CENTRAL ANSWER — how MS2 records "this was opened / picked up / broken"

There are **four** persisted stores and one **non**-persisted one. Everything in this report falls
into one of them.

| # | Store | Owner object | Key | Persisted to | Where used |
|---|---|---|---|---|---|
| **A** | Per-object save data | `USpartaSaveObject` subclass (Blueprint `BPSO_*` / `BP_SO_*`) instantiated per actor and referenced by that actor's `USpartaSaveComponent.RuntimeData` | `SpartaSaveComponent.Guid` (`FGuid`) for `PerInstance`, or the class for `Global` | `WorldState_<slot>.sav` (see §1.4) | Doors, gates, chests, pickups, levers, elevators, boss gates, NPCs, spawners, arenas, shops, quest log, tarstone/PP-item progression |
| **B** | Actor-name → key → value maps | `USpartaSaveGame` (`SpartaSaveSubSystem.CurrentOnDiskWorldStateSave`) | outer `FName` = "actor name", inner `FName` = value name | same file | Anything written through `SpartaWorldStateHelper.WriteMy*/WriteTheir*/Write*ByTags` — e.g. `BP_HubDoor.DoorIsOpen`, `BP_BreakableWall.WallIsBroken`, `BP_Interactable_Chest.TimesUsed` |
| **C** | DataLayer runtime states | `UDataLayersSave` (`SpartaDataLayerSubsystem.RuntimeData`) | `FName` data-layer name | same file | Boss/encounter streaming gates, cinematics (`BPSO_BossDLManager.RelevantDL`, `BPSO_CinematicDLUpdate`) |
| **D** | Session-only save object | `BPSO_PlayerSession` (`SpartaSaveSubSystem.SessionSaveObject` / `RuntimeSessionSaveObject`) | — (`bIsSessionSaveData = true`) | session file, not the world-state file | Player HP / shell HP / resolve / healing charges / darkform flag / start transform |
| — | **Nothing** | — | — | — | `BP_VFX_Breakable` (the general Chaos breakable, incl. barrels/jars/carts) and every traversal actor (`SpartaTraversalActor`) have **no** `USpartaSaveComponent` — see §8.6 and §8.9 |

Store **A** is the dominant one for "things that change and stay changed".

### 1.1 Store A in detail

`USpartaSaveComponent` is an `UActorComponent` subclass at `/Script/Sparta.SpartaSaveComponent`
(parent `/Script/Engine.ActorComponent`) with these persisted-relevant members:

| Property | Type | Offset | Notes |
|---|---|---|---|
| `SaveObjectClass` | `ClassProperty` (meta-class `/Script/Sparta.SpartaSaveObject`) | `0xc0` | which save-object class this actor uses |
| `OnSaveLoaded` | `MulticastInlineDelegateProperty` | `0xc8` | sig `OnSaveGameEvent(SaveGame: FSpartaSaveContext)` |
| `OnPreSave` | `MulticastInlineDelegateProperty` | `0xd8` | sig `OnSaveGameEvent(SaveGame: FSpartaSaveContext)` |
| `OnSessionReset` | `MulticastInlineDelegateProperty` | `0xe8` | sig `OnSaveComponentEvent()` (no params) |
| `SaveProtocol` | `EnumProperty /Script/Sparta.ESaveObjectProtocol` | `0xf8` | `PerInstance = 0`, `Global = 1` |
| `bApplyPrologueSkipPreset` | `BoolProperty` | `0xf9` | |
| `Guid` | `StructProperty /Script/CoreUObject.Guid` | `0xfc` | **the per-instance persistence key** |
| `RuntimeData` | `ObjectProperty /Script/Sparta.SpartaSaveObject` | `0x110` | the live save object |
| `bSaveLoaded` | `BoolProperty` | `0x118` | |

Component API (dump order, full parameter lists):

| Function | Parameters | Notes |
|---|---|---|
| `Save` | `Events: StructProperty of /Script/GameplayTags.GameplayTagContainer` | request a save of this object |
| `ResetSaveData` | — | |
| `IsSaveLoaded` | `ReturnValue: BoolProperty` | |
| `GetRuntimeData` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveObject` | returns `RuntimeData` |
| `GetGUID` | `ReturnValue: StructProperty of /Script/CoreUObject.Guid` | |
| `GenerateNewGuid` | — | reassigns `Guid` |

`USpartaSaveObject` (parent `/Script/CoreUObject.Object`) — the base every `BPSO_*` derives from:

| Property | Type | Offset |
|---|---|---|
| `PrimarySaveObjectTick` | `StructProperty /Script/Sparta.SaveObjectTickFunction` | `0x28` |
| `IgnoreSaveGameMetaSpecifier` | `BoolProperty` | `0x58` |
| `bIsSessionSaveData` | `BoolProperty` | `0xb1` |

| Function | Parameters |
|---|---|
| `UpdateFrom` | `SaveObject: ObjectProperty of /Script/Sparta.SpartaSaveObject`, `Events: StructProperty of /Script/GameplayTags.GameplayTagContainer`, `ReturnValue: BoolProperty` |
| `SetTickInterval` | `TickInterval: FloatProperty` |
| `SetTickEnabled` | `bEnabled: BoolProperty` |
| `ReceiveTick` | `DeltaTime: FloatProperty` |
| `ReceiveSpartaPreSave` | `SaveContext: StructProperty of /Script/Sparta.SpartaSaveContext` |
| `ReceiveOnLoaded` | — |
| `ReceiveApplySkipProloguePreset` | — |
| `IsTickEnabled` | `ReturnValue: BoolProperty` |
| `IsSessionSaveData` | `ReturnValue: BoolProperty` |
| `GetTickInterval` | `ReturnValue: FloatProperty` |
| `GetOwningSaveComponent` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveComponent` |
| `GetOwningActor` | `ReturnValue: ObjectProperty of /Script/Engine.Actor` |

`ReceiveSpartaPreSave` is the **gather** hook (called before writing), `ReceiveOnLoaded` is the
**apply** hook (called after reading). Both are Blueprint-overridable and are what a sync layer has
to intercept or mirror.

Evidence (verbatim):

```
[00007FF46F038338] Class /Script/Sparta.SpartaSaveObject [n: 47C43] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F038738] Class /Script/Sparta.SpartaSaveComponent [n: 47C98] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[000001F4ACD72C80] ClassProperty /Script/Sparta.SpartaSaveComponent:SaveObjectClass [o: C0] [n: 5864E0] [c: 00007FF721DE8E20] [owr: 00007FF46F038738] [mc: 00007FF46F038338]
[000001F4ACD72D00] MulticastInlineDelegateProperty /Script/Sparta.SpartaSaveComponent:OnSaveLoaded [o: C8] [n: 4CF7E5] [c: 00007FF721DE9010] [owr: 00007FF46F038738] [df: 00007FF46F3D02E8]
[000001F4ACD72F00] EnumProperty /Script/Sparta.SpartaSaveComponent:SaveProtocol [o: F8] [n: 5864D1] [c: 00007FF721DE7380] [owr: 00007FF46F038738] [em: 00007FF46F3D9C98]
[000001F4ACD73000] StructProperty /Script/Sparta.SpartaSaveComponent:Guid [o: FC] [n: 86D] [c: 00007FF721DF9890] [owr: 00007FF46F038738] [ss: 00007FF46F2E3578]
[000001F4ACD73080] ObjectProperty /Script/Sparta.SpartaSaveComponent:RuntimeData [o: 110] [n: 5864BD] [c: 00007FF721DE9420] [owr: 00007FF46F038738] [pc: 00007FF46F038338]
[000001F4ACD73100] BoolProperty /Script/Sparta.SpartaSaveComponent:bSaveLoaded [o: 118] [n: 5864B6] [c: 00007FF721DE8D80] [owr: 00007FF46F038738]
```

**Caveat that matters for a sync layer:** `SaveObjectClass` is set on each Blueprint's CDO, and CDO
property *values* are not in the object dump. Only the *type* (`ClassProperty`, meta-class
`SpartaSaveObject`) is provable from the dump. Which concrete class each component uses is provable
only where the Blueprint holds a typed variable pointing at that class — that is exactly what §6
tabulates (56 such bindings).

### 1.2 Store B in detail

`USpartaSaveGame` (parent `/Script/Engine.SaveGame`) — the on-disk world-state container:

| Property | Type (dump) | Element types (from map sub-objects) | Offset |
|---|---|---|---|
| `ChangedActors` | `SetProperty` | `TSet<FName>` (from `Sparta.hpp`) | `0x28` |
| `ActorFloats` | `MapProperty` | `TMap<FName, FSpartaMapFloat>`; `FSpartaMapFloat.Map = TMap<FName,float>` | `0x78` |
| `ActorInts` | `MapProperty` | `TMap<FName, FSpartaMapInt>`; `FSpartaMapInt.Map = TMap<FName,int32>` | `0xc8` |
| `ActorNames` | `MapProperty` | `TMap<FName, FSpartaMapName>`; `FSpartaMapName.Map = TMap<FName,FName>` | `0x118` |
| `ActorBools` | `MapProperty` | `TMap<FName, FSpartaMapBool>`; `FSpartaMapBool.Map = TMap<FName,bool>` | `0x168` |
| `ActorObjectMap` | `MapProperty` | `TMap<FName, UObject*>` | `0x1b8` |
| `ActorObjectMapAsString` | `StrProperty` | — | `0x208` |

`USpartaSaveGame` declares **no functions** of its own (verified: `parse_dump.py fun` on
`^/Script/Sparta\.SpartaSaveGame$` prints nothing).

The only API that writes/reads these maps is `USpartaWorldStateHelper` (a
`UBlueprintFunctionLibrary`, parent `/Script/Engine.BlueprintFunctionLibrary`). It is a large
`WriteXxx` / `ReadXxx` family in four addressing modes:

| Mode | Address = | Example signature (full parameter list, dump order) |
|---|---|---|
| `My*` | the calling actor's own name | `WriteMyInt(WorldContextObject: ObjectProperty of /Script/CoreUObject.Object, ValueName: NameProperty, Value: IntProperty, bModify: BoolProperty)` |
| `Their*` | an explicit actor | `WriteTheirBoolByTag(They: ObjectProperty of /Script/CoreUObject.Object, ValueName: StructProperty of /Script/GameplayTags.GameplayTag, bValue: BoolProperty)` |
| `Player*` | the player save object | `WritePlayerFloat(WorldContextObject: ObjectProperty of /Script/CoreUObject.Object, ValueName: NameProperty, Value: FloatProperty, bModify: BoolProperty)` |
| `An*` / `ByTags` | explicit owner name, or owner+key `FGameplayTag`s | `WriteIntByTags(WorldContextObject: ObjectProperty of /Script/CoreUObject.Object, Owner: StructProperty of /Script/GameplayTags.GameplayTag, Key: StructProperty of /Script/GameplayTags.GameplayTag, Value: IntProperty, bModify: BoolProperty)` |

Every `Write*` has a matching `Read*` that returns `BoolProperty` (found / not-found) plus an
`OutValue` out-parameter, e.g.
`ReadMyBool(WorldContextObject: Object, ValueName: Name, OutValue: BoolProperty, ReturnValue: BoolProperty)`.

Changed-set bookkeeping (this is the "did anything change" mechanism):

| Function | Parameters |
|---|---|
| `AddToChangedSet` | `WorldContextObject: ObjectProperty of /Script/CoreUObject.Object`, `ActorName: NameProperty` |
| `AddMeToChangedSet` | `WorldContextObject: ObjectProperty of /Script/CoreUObject.Object` |
| `AddThemToChangedSet` | `They: ObjectProperty of /Script/CoreUObject.Object` |
| `RemoveFromChangedSet` | `WorldContextObject: Object, ActorName: NameProperty` |
| `RemoveMeFromChangedSet` | `WorldContextObject: Object` |
| `RemoveThemFromChangedSet` | `They: Object` |
| `IsInChangedSet` | `WorldContextObject: Object, ActorName: NameProperty, ReturnValue: BoolProperty` |
| `AmIChanged` | `WorldContextObject: Object, ReturnValue: BoolProperty` |
| `AreTheyChanged` | `They: Object, ReturnValue: BoolProperty` |
| `ResetAllActorInts` | `WorldContextObject: Object` |

Other `SpartaWorldStateHelper` functions relevant to world state:

| Function | Parameters |
|---|---|
| `GetSpartaActorSaveObject` | `WorldContextObject: Object, SaveClass: ClassProperty, ReturnValue: ObjectProperty of /Script/CoreUObject.Object` |
| `ReadMySaveObject` | `WorldContextObject: Object, ReturnValue: ObjectProperty of /Script/CoreUObject.Object` |
| `WriteMySaveObject` | `WorldContextObject: Object, Value: ObjectProperty of /Script/CoreUObject.Object` |
| `ReadASaveObject` | `WorldContextObject: Object, OwnerName: NameProperty, ReturnValue: ObjectProperty of /Script/CoreUObject.Object` |
| `WriteASaveObject` | `WorldContextObject: Object, OwnerName: NameProperty, Value: ObjectProperty of /Script/CoreUObject.Object` |
| `GetPlayerSaveObjectFromSave` | `WorldContextObject: Object, ReturnValue: ObjectProperty of /Script/CoreUObject.Object` |
| `GetPlayerUniqueNameID` | `ReturnValue: NameProperty` |
| `GetPlayerDeathCounterName` | `ReturnValue: NameProperty` |
| `TempTest` | `WorldContext: Object, ReturnValue: BoolProperty` |

Evidence:

```
[00007FF46F057138] Class /Script/Sparta.SpartaSaveGame [n: 4A29B] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF4A738]
[000001F4A93E85F0] SetProperty /Script/Sparta.SpartaSaveGame:ChangedActors [o: 28] [n: 589119] [c: 00007FF721DE94F0] [owr: 00007FF46F057138]
[000001F49CD2F380] MapProperty /Script/Sparta.SpartaSaveGame:ActorFloats [o: 78] [n: 589109] [c: 00007FF721DE8F70] [owr: 00007FF46F057138] [kp: 000001F4ACDE13C0] [vp: 000001F4ACDF2600]
[000001F49CD2F600] MapProperty /Script/Sparta.SpartaSaveGame:ActorObjectMap [o: 1B8] [n: 55CF91] [c: 00007FF721DE8F70] [owr: 00007FF46F057138] [kp: 000001F4ACDE1580] [vp: 000001F4ACDF2800]
[00007FF46F064D38] Class /Script/Sparta.SpartaWorldStateHelper [n: 4B7C0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE70138]
```

Map key/value element types, proven by the map's own key/value sub-objects:

```
NameProperty ActorInts./Script/Sparta.SpartaSaveGame:ActorInts_Key
StructProperty ActorInts./Script/Sparta.SpartaSaveGame:ActorInts of /Script/Sparta.SpartaMapInt
NameProperty ActorObjectMap./Script/Sparta.SpartaSaveGame:ActorObjectMap_Key
ObjectProperty ActorObjectMap./Script/Sparta.SpartaSaveGame:ActorObjectMap of /Script/CoreUObject.Object
StructProperty SaveObjects./Script/Sparta.SpartaSaveSubSystem:SaveObjects_Key of /Script/CoreUObject.Guid
ObjectProperty SaveObjects./Script/Sparta.SpartaSaveSubSystem:SaveObjects of /Script/Sparta.SpartaSaveObject
```

(The first two lines are the `path` column of `scripts\q\ws-map-kv.sql`; the tool prints
`<type>\t<path of the subobject>\t<resolved ref>`. The corresponding raw dump lines have shape
`NameProperty ActorInts./Script/Sparta.SpartaSaveGame:ActorInts_Key[o: 0] [n: …]`.)

### 1.3 The subsystem that owns everything — `USpartaSaveSubSystem`

Parent `/Script/Engine.GameInstanceSubsystem`. This is where a mod must plug in.

| Property | Type | Offset |
|---|---|---|
| `OnSaveGameLoaded` | `MulticastInlineDelegateProperty` | `0x30` |
| `PostSessionReset` | `MulticastInlineDelegateProperty` | `0x40` |
| `OnSaveSystemInitialized` | `MulticastInlineDelegateProperty` | `0x50` |
| `OnSaveStarted` | `MulticastInlineDelegateProperty` | `0x60` |
| `OnSaveFinished` | `MulticastInlineDelegateProperty` | `0x70` |
| `SaveObjects` | `MapProperty` — `TMap<FGuid, USpartaSaveObject*>` | `0xb8` |
| `CurrentOnDiskWorldStateSave` | `ObjectProperty of /Script/Sparta.SpartaSaveGame` | `0x108` |
| `SaveInterfaces` | `ArrayProperty of /Script/CoreUObject.Object` | `0x110` |
| `SessionSaveObject` | `ObjectProperty of /Script/Sparta.SpartaSaveObject` | `0x120` |
| `RuntimeSessionSaveObject` | `ObjectProperty of /Script/Sparta.SpartaSaveObject` | `0x128` |
| `MetaData` | `ObjectProperty of /Script/Sparta.SpartaMetaSave` | `0x140` |
| `bSaveLoaded` | `BoolProperty` | `0x14c` |
| `bApplySkipProloguePresetOnNextLoad` | `BoolProperty` | `0x14e` |

Functions (full parameter lists, dump order) — the ones a sync layer cares about first:

| Function | Parameters | Why it matters |
|---|---|---|
| `SaveToDisk` | `SaveEvents: GameplayTagContainer`, `bSaveImmediately: BoolProperty`, `bSaveAsync: BoolProperty` | **the write trigger**; `SaveEvents` is the tag container that tells each save object which sub-systems to include |
| `SaveSession` | — | session-only write |
| `SaveMetaData` | — | meta write |
| `SubscribeSaveInterface` | `Object: ObjectProperty of /Script/CoreUObject.Object` | register a `SpartaSaveInterface` implementer |
| `UnsubscribeSaveInterface` | `Object: ObjectProperty of /Script/CoreUObject.Object` | |
| `GetInstance` | `WorldContextObject: ObjectProperty of /Script/CoreUObject.Object`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveSubSystem` | static accessor |
| `GetCurrentWorldStateSave` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveGame` | **read the whole world-state container** |
| `GetSessionSave` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveObject` | |
| `GetSaveObjectByClassFromSlotName` | `SlotName: StrProperty`, `SaveObjectClass: ClassProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveObject` | read a save object from an on-disk slot |
| `GetSaveObjectByClassFromSlotIndex` | `SlotIndex: IntProperty`, `SaveObjectClass: ClassProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveObject` | same, by index |
| `GetPlayerSaveObjectFromSlotName` | `SlotName: StrProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.PlayerSaveObject` | |
| `GetPlayerSaveObjectFromSlotIndex` | `SlotIndex: IntProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.PlayerSaveObject` | |
| `ResetSaveObjectByGuid` | `GuidString: StrProperty`, `ReturnValue: BoolProperty` | **delete one actor's persisted state by its Guid** |
| `ResetSaveObjectByClass` | `SaveObjectClass: ClassProperty`, `ReturnValue: IntProperty` | delete all state of one save class |
| `ResetAllSaveData` | — | |
| `ResetSession` | — | |
| `SaveLoaded`-lifecycle getters | `GetIsSaveLoaded() -> Bool`, `HasValidWorldState…` see below | |
| `GetIsSaveLoaded` | `ReturnValue: BoolProperty` | gate for "do not touch anything before the world state is in" |
| `GetMetaData` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaMetaSave` | |
| `GetActiveSaveProfileSlotIndex` | `ReturnValue: IntProperty` | |
| `GetActiveSaveProfileSlotName` | `ReturnValue: StrProperty` | |
| `ChangeActiveSaveProfileSlot` | `SlotIndex: IntProperty` | |
| `DeleteSaveProfile` | `ProfileSlot: IntProperty` | |
| `RecoverSave` | `RecoverySlotName: StrProperty` | |
| `BackupActiveSaveProfile` | — | |
| `EnableAutoSave` / `DisableAutoSave` | — | |
| `EnableSaveProfileBackup` / `DisableSaveProfileBackup` | — | |
| `AutoSave` | — | |
| `SetPreventFurtherSaves` | `bPreventSaving: BoolProperty` | **useful for a sync layer that wants to own the save moment** |
| `IsAsyncSaving` | `ReturnValue: BoolProperty` | |
| `AsyncSaveFinished` | `SlotName: StrProperty`, `PlatformId: IntProperty`, `bSuccess: BoolProperty` | |
| `WriteCurrentSaveToTextFile` | — | debug dump of the current save |
| `ClearCache`, `OnEndPlayMap`, `SetValidActiveSaveProfileSlot`, `ResetAutoSaveTimer`, `RequestSkipPrologueStart`, `StopSkipPrologueRequest`, `IsProcessingPrologueSkip` | — / `ReturnValue: Bool` | |

The full function list (56 functions) is in the evidence block below; I have only annotated the ones
that matter for world state. Complete list in dump order — see `scripts\q\ws-props-save.sql` +
`parse_dump.py fun` for the two remaining groups (`GetExisting*`, `Has*`).

Evidence:

```
[00007FF46F057B38] Class /Script/Sparta.SpartaSaveSubSystem [n: 4A2D5] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF5DF38]
[000001F49CD2F2E0] MapProperty /Script/Sparta.SpartaSaveSubSystem:SaveObjects [o: B8] [n: 58919E] [c: 00007FF721DE8F70] [owr: 00007FF46F057B38] [kp: 000001F4ACDF5200] [vp: 000001F4ACDF5180]
[000001F4ACDF5280] ObjectProperty /Script/Sparta.SpartaSaveSubSystem:CurrentOnDiskWorldStateSave [o: 108] [n: 58918F] [c: 00007FF721DE9420] [owr: 00007FF46F057B38] [pc: 00007FF46F057138]
[00007FF46F7125F8] Function /Script/Sparta.SpartaSaveSubSystem:WriteCurrentSaveToTextFile [n: 48E37] [c: 00007FF46EE55538] [or: 00007FF46F057B38] [f: 00007FF71C836540]
[00007FF46F712518] Function /Script/Sparta.SpartaSaveSubSystem:UnsubscribeSaveInterface [n: 4A587] [c: 00007FF46EE55538] [or: 00007FF46F057B38] [f: 00007FF71C836390]
```

### 1.4 What is actually on disk

`USpartaSaveGlobalSettings` (parent `/Script/DeveloperSettings.DeveloperSettings`) holds the file
naming; its CDO values are **not** in the dump, so the exact strings are UNKNOWN from the dump.

| Property | Type | Offset |
|---|---|---|
| `TotalDeathsName` | `NameProperty` | `0x38` |
| `MetaDataFileName` | `StrProperty` | `0x40` |
| `SaveProfileFileNamePrefix` | `StrProperty` | `0x50` |
| `SaveRecoveryFileNamePrefix` | `StrProperty` | `0x60` |
| `SessionFileNamePrefix` | `StrProperty` | `0x70` |
| `MaxSaveProfileSlots` | `IntProperty` | `0x80` |
| `MaxSaveRecoverySlots` | `IntProperty` | `0x84` |
| `SaveRecoveryBackupFrequency` | `FloatProperty` | `0x88` |

Observed on disk in the existing research run (not the dump — a `sha256sum` listing captured at
`runs\20261022-061848-ingame\saves-before.txt`), which matches those four name fields:

```
CAEF263B8EB4225144A0CABAE5F4B1C903F4D90813659DA2DABF6F85154EE7FC  MetaSaveGame.sav
47095B2B5E72D5B302A6FD22354703722850308560E85183A6791602F603B7FD  SpartaGameSettings.sav
96EDA8577BBDBFFC89C21309F2D98993BEAFD47C02342E3D1E2F721E28BAC49A  WorldState_0.sav
96EDA8577BBDBFFC89C21309F2D98993BEAFD47C02342E3D1E2F721E28BAC49A  WorldState_Backup_0_0.sav
```

GUESS: `SaveProfileFileNamePrefix` = `WorldState_`, `SaveRecoveryFileNamePrefix` =
`WorldState_Backup_`, `MetaDataFileName` = `MetaSaveGame.sav`. `SpartaGameSettings.sav` is a
`USaveGame` written by `SpartaSettingsSaveObject_C` (§5.4), not by this subsystem.

### 1.5 Cross-check: the live world really does use all of this

From `raw\index_ingame.sqlite3` (536 682 objects, in-game):

* **234** live `SpartaSaveComponent` instances in `:PersistentLevel` (see §13).
* Live `SpartaSaveGame` object named `WorldState_2147480430`, i.e. the runtime object name is
  literally `WorldState`, inside
  `/Engine/Transient.SpartaGameEngine_2147482593:BP_SpartaGameInstance_C_2147482225.SpartaSaveSubSystem_2147482069`.
* Live save-object instances: `BPSO_WordMapObjective_C` × 133, `BPSO_TarGate_C` × 28,
  `BP_SO_SimpleBool_C` × 27, `BPSO_SpawnerSaveObj_C` × 14, `BPSO_NPC_C` × 13,
  `BPSO_CinematicDLUpdate_C` × 11, `BPSO_BossGate_C` × 6, `BPSO_LandingArea_Boss_C` × 6,
  `BPSO_CraftingItemPickup_C` × 5, `BPSO_ShopData_C` × 5, `BPSO_FC_CorruptionCore_C` × 3, and
  exactly **1** each of `BPSO_FC_FinalBossManager_C`, `BPSO_FC_TarRoots_C`,
  `BPSO_GenessaSensor_C`, `BPSO_MovingObject_C`, `BPSO_NarrativeEventsTracker_C`,
  `BPSO_Progression_C`, `BPSO_QuestLogComponent_C`, `BPSO_ShellMemories_C`,
  `BPSO_SlayerDungeonChalice_C`, `BPSO_Tarforge_C`, `BPSO_WoodenElevator_C`, `BPSO_WorldMap_C`,
  `BP_SpartaPlayerSaveObject_C`.

Evidence:

```
[000002728A233B20] SpartaSaveGame /Engine/Transient.SpartaGameEngine_2147482593:BP_SpartaGameInstance_C_2147482225.SpartaSaveSubSystem_2147482069.WorldState_2147480430 [n: 55D283] [c: 00007FF434F07138] [or: 000002712176DA80]
[000002712176DA80] SpartaSaveSubSystem /Engine/Transient.SpartaGameEngine_2147482593:BP_SpartaGameInstance_C_2147482225.SpartaSaveSubSystem_2147482069 [n: 4A2D5] [c: 00007FF434F07B38] [or: 00000271388D10C0]
```

---

## 2. Native save / world-state classes — inventory

All `/Script/Sparta` classes whose name matched `Save|WorldState|Singleton|Instanced|DataLayer|WorldObject|Persistent|Saveable`, plus the two reached from the search terms but with no state of their own:

| Class | Parent | Kind | Persisted? | Sync relevance |
|---|---|---|---|---|
| `/Script/Sparta.SpartaSaveObject` | `/Script/CoreUObject.Object` | base save object | via subclass | base of every save object |
| `/Script/Sparta.SpartaSaveComponent` | `/Script/Engine.ActorComponent` | actor component | yes (owns a save object) | **per-actor persistence handle** |
| `/Script/Sparta.SpartaSaveSubSystem` | `/Script/Engine.GameInstanceSubsystem` | subsystem | yes (writes `WorldState_*.sav`) | **central hub** |
| `/Script/Sparta.SpartaSaveGame` | `/Script/Engine.SaveGame` | save container | yes | **store B** |
| `/Script/Sparta.SpartaMetaSave` | `/Script/Engine.SaveGame` | save container | yes (`MetaSaveGame.sav`) | slot bookkeeping only |
| `/Script/Sparta.SpartaSaveInterface` | `/Script/CoreUObject.Interface` | interface | — | `SpartaPreSave` / `SaveLoaded` / `SessionReset` hooks |
| `/Script/Sparta.SpartaSaveGlobalSettings` | `/Script/DeveloperSettings.DeveloperSettings` | settings | config | file naming |
| `/Script/Sparta.SpartaWorldStateHelper` | `/Script/Engine.BlueprintFunctionLibrary` | function library | — | **store B API** |
| `/Script/Sparta.SpartaWorldObject` | `/Script/CoreUObject.Object` | empty base (`Size: 0x28`, no members, no functions) | no | scriptable-object base for 11 Blueprints (§2.1) — **not** world state |
| `/Script/Sparta.SpartaInstancedObject` | `/Script/CoreUObject.Object` | empty base (`Size: 0x28`) | no | scriptable-object base for 12 Blueprints, incl. teleport handlers `BPO_STH_*` |
| `/Script/Sparta.SpartaSingleton` | `/Script/CoreUObject.Object` | singleton base | no | 2 props only (`QuestDataTable`, `EnemySpawnTypes`) |
| `/Script/Sparta.PlayerSaveObject` | `/Script/Sparta.SpartaSaveObject` | save object | yes | player-level state (store A, `Player*` helpers) |
| `/Script/Sparta.PlayerProgressionSaveData` | `/Script/Sparta.SpartaSaveObject` | save object | yes | `SkillLevels`, `ShellBuilds`, `TierBuilds` |
| `/Script/Sparta.PPItemsSaveData` | `/Script/Sparta.SpartaSaveObject` | save object | yes | `ActivePPItem`, `PassivePPItem`, `PPItemLevels`, `PPItemFoundAmounts` |
| `/Script/Sparta.TarstoneSaveData` | `/Script/Sparta.SpartaSaveObject` | save object | yes | `TarstoneLevels`, `TarstoneBuilds`, `TarstoneSupportBuild` |
| `/Script/Sparta.DataLayersSave` | `/Script/Sparta.SpartaSaveObject` | save object | yes | **store C** |
| `/Script/Sparta.SpartaDataLayerSubsystem` | `/Script/Engine.WorldSubsystem` | subsystem | yes (via `RuntimeData`) | `SaveDataLayerState` / `RemoveDataLayerState` |
| `/Script/Sparta.DataLayerFunctionLibrary` | `/Script/Engine.BlueprintFunctionLibrary` | function library | — | no members (verified: 0 members in dump) |
| `/Script/Sparta.PossibleWorldStateSet` | `/Script/CoreUObject.Object` | data object | n/a (asset-side) | `WorldStates: TArray<FPossibleWorldState>` |
| `/Script/Sparta.SpartaSessionSaveObjectBase` | `/Script/Sparta.SpartaSaveObject` | session base | session file | `GetSessionStartTransform()` |
| `/Script/Sparta.ItemFragment_WorldStateModifier` | `/Script/Sparta.SpartaItemFragment` | item fragment | no | item→world-state effects, not a record |

Evidence:

```
[00007FF46F057338] Class /Script/Sparta.SpartaSaveGlobalSettings [n: 4A2A3] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE5C938]
[00007FF46F057538] Class /Script/Sparta.SpartaSaveInterface [n: 4A2B0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52A30]
[00007FF46F060538] Class /Script/Sparta.SpartaWorldObject [n: 4AF33] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F04DB38] Class /Script/Sparta.SpartaInstancedObject [n: 498E8] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F064138] Class /Script/Sparta.PossibleWorldStateSet [n: 4B304] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F045338] Class /Script/Sparta.DataLayersSave [n: 48F97] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038338]
[00007FF46F045538] Class /Script/Sparta.SpartaDataLayerSubsystem [n: 48F9F] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7C138]
[00007FF46F050138] Class /Script/Sparta.SpartaMetaSave [n: 49AFE] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF4A738]
[00007FF46F057D38] Class /Script/Sparta.SpartaSessionSaveObjectBase [n: 4A594] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038338]
[00007FF46F058138] Class /Script/Sparta.SpartaSingleton [n: 4A5B1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F038F38] Class /Script/Sparta.PlayerSaveObject [n: 47D71] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038338]
```

### 2.1 `SpartaWorldObject` / `SpartaInstancedObject` are NOT world state

Both are empty bases (`// Size: 0x28`, zero members, zero functions) in `Sparta.hpp`. Their
Blueprint descendants are scriptable helper objects. Complete list (SQL over `cls` joined on
`super`):

| Base | Descendant |
|---|---|
| `SpartaInstancedObject` | `/Game/Sparta/Core/Common/Interactions/InstancedConditions/BP_InteractionComponent_Condition.BP_InteractionComponent_Condition_C` |
| | `/Game/Sparta/Core/Conditions/BP_ConditionObject.BP_ConditionObject_C` |
| | `/Game/Sparta/Core/GameplayCues/VFX/Objects/GC_Params.GC_Params_C` |
| | `/Game/Sparta/Core/GameplayCues/VFX/Objects/IO_WindParams.IO_WindParams_C` |
| | `/Game/Sparta/Core/Player/Upgrades/Core/BP_UpgradePoolHandler.BP_UpgradePoolHandler_C` |
| | `/Game/Sparta/Core/Tarstones/Melee/Abilities/BP_ElementalMechanicOverrideData.BP_ElementalMechanicOverrideData_C` |
| | `/Game/Sparta/Core/World/SplineMovement/BP_SplineMovementInput.BP_SplineMovementInput_C` |
| | `/Game/Sparta/Core/World/Teleporting/BPO_STH.BPO_STH_C` |
| | `/Game/Sparta/FX/Blueprints/BP_WeaponEffectDataObject.BP_WeaponEffectDataObject_C` |
| | `/Game/Sparta/Items/Pickups/Core/BP_ModifyItemCost.BP_ModifyItemCost_C` |
| | `/Game/Sparta/Items/Pickups/Core/BP_RewardData.BP_RewardData_C` |
| `SpartaWorldObject` | `/Game/Sparta/Core/Ability/BP_PayloadStatusEffectHandler.BP_PayloadStatusEffectHandler_C` |
| | `/Game/Sparta/Core/Effects/StatusEffects/Knockback/BP_KnockbackObject.BP_KnockbackObject_C` |
| | `/Game/Sparta/Core/GameplayCues/VFX/Objects/BP_CharacterSwitchVFXData.BP_CharacterSwitchVFXData_C` |
| | `/Game/Sparta/Core/Player/Upgrades/Shells/Core/BP_ShellProgressionData.BP_ShellProgressionData_C` |
| | `/Game/Sparta/Core/World/SplineMovement/BP_SplineMovementScript.BP_SplineMovementScript_C` |
| | `/Game/Sparta/UI/Core/Navigation/BP_UILogic.BP_UILogic_C` |
| | `/Game/Sparta/UI/Menu/Misc/BP_UIAttentionGrabberLogic.BP_UIAttentionGrabberLogic_C` |
| | `/Game/Sparta/UI/NPC/BP_VerticalMenuOptionLogic.BP_VerticalMenuOptionLogic_C` |
| | `/Game/Sparta/UI/Notifies/Blueprints/BP_Notify_InspectLogic.BP_Notify_InspectLogic_C` |
| | `/Game/Sparta/UI/Notifies/Blueprints/BP_Notify_PromptLogic.BP_Notify_PromptLogic_C` |
| | `/Game/Sparta/UI/Tutorials/Blueprints/BPO_TutorialHandler.BPO_TutorialHandler_C` |
| `SpartaSingleton` | `/Game/Sparta/Core/GameInstance/BP_SpartaSingleton.BP_SpartaSingleton_C` |

Evidence — `BPO_STH_C` really does derive from `USpartaInstancedObject`
(`[sps: 00007FF46F04DB38]` = the address of `Class /Script/Sparta.SpartaInstancedObject` above), and
`BP_SpartaSingleton_C` from `USpartaSingleton`:

```
[000001F4DB719FD0] BlueprintGeneratedClass /Game/Sparta/Core/World/Teleporting/BPO_STH.BPO_STH_C [n: 78155] [c: 00007FF46EEC3D38] [or: 000001F4D33C1E60] [sps: 00007FF46F04DB38]
[000001F4DB77D780] MulticastInlineDelegateProperty /Game/Sparta/Core/World/Teleporting/BPO_STH.BPO_STH_C:OnEnterComplete [o: 30] [n: 67D3D4] [c: 00007FF721DE9010] [owr: 000001F4DB719FD0] [df: 000001F4DB74F600]
[000001F623616A60] BlueprintGeneratedClass /Game/Sparta/Core/GameInstance/BP_SpartaSingleton.BP_SpartaSingleton_C [n: 335C6C] [c: 00007FF46EEC3D38] [or: 000001F4CEED47A0] [sps: 00007FF46F058138]
```

### 2.2 Remaining native members I did not need to tabulate

| Class | Members I did not tabulate above |
|---|---|
| `SpartaMetaSave` | `ActiveSaveProfileSlot: IntProperty @0x28`, `ActiveSaveRecoverySlot: MapProperty @0x30` (`TMap<int32,int32>` per `Sparta.hpp`). No functions. |
| `SpartaSingleton` | `QuestDataTable: ObjectProperty of /Script/Engine.DataTable @0x28`, `EnemySpawnTypes: ObjectProperty of /Script/Engine.DataTable @0x30`; functions `GetInstanceUnchecked() -> SpartaSingleton`, `GetInstance() -> SpartaSingleton`. |
| `PlayerSaveObject` | 11 members: `TotalPlayTimeSeconds: FloatProperty @0xb8`, `LastCachedCurrentTime: DoubleProperty @0xc0`, `LastSaveDateTime: StructProperty (DateTime) @0xc8`, `CompletedSpawnTriggerRecipes: MapProperty @0xd0`, `CompletetedSpawnTriggers: SetProperty @0x120` *(name misspelled in the build)*, `ShellStats: MapProperty @0x170`, `WeaponStats: MapProperty @0x1c0`, `PlayerStats: StructProperty (FPermanentStats) @0x210`, `GameLibraryData: MapProperty @0x260`, `OverrideWorldGenTags: StructProperty (GameplayTagContainer) @0x2b0`, `Difficulty: MapProperty @0x2d0`. 8 functions: `IncreaseWeaponStat(WorldContextObject, WeaponTag: GameplayTag, StatTag: GameplayTag, IncreaseBy: Int)`, `IncreaseShellStat(WorldContextObject, ShellTag, StatTag, IncreaseBy)`, `IncreasePlayerStat(WorldContextObject, StatTag, IncreaseBy)`, `GetWeaponStat(WeaponTag, StatTag) -> Int`, `GetShellStat(ShellTag, StatTag) -> Int`, `GetPlayerStat(StatTag) -> Int`, `ClearWeaponStats(WorldContextObject, WeaponTag)`, `ClearPlayerStats(WorldContextObject)` |
| `DataLayersSave` | `SavedDataLayerStates: MapProperty @0xb8`, `TMap<FName, EDataLayerRuntimeState>`. No functions. |
| `SpartaDataLayerSubsystem` | `RuntimeData: ObjectProperty of /Script/Sparta.DataLayersSave @0x38`; functions `SaveDataLayerState(DataLayer: ObjectProperty of /Script/Engine.DataLayerAsset, TargetState: EnumProperty of /Script/Engine.EDataLayerRuntimeState)`, `RemoveDataLayerState(DataLayer: ObjectProperty of /Script/Engine.DataLayerAsset)`. |
| `PossibleWorldStateSet` | `WorldStates: ArrayProperty @0x28` of `StructProperty of /Script/Sparta.PossibleWorldState`. No functions. |
| `SpartaSessionSaveObjectBase` | `GetSessionStartTransform() -> Transform`. No properties of its own. |
| `SpartaSaveInterface` | `SpartaPreSave(SaveContext: StructProperty of /Script/Sparta.SpartaSaveContext)`, `SessionReset()`, `SaveLoaded(SaveContext: StructProperty of /Script/Sparta.SpartaSaveContext)`. |
| `DataLayerFunctionLibrary` | no members at all. |
| `ItemFragment_WorldStateModifier` | `OnSpawnEffectsToAdd: ArrayProperty @0x28`, `OnSpawnAbilitiesToAdd: ArrayProperty @0x38`. |

Evidence for the map/member claims:

```
[000001F4ACD6D980] IntProperty /Script/Sparta.SpartaMetaSave:ActiveSaveProfileSlot [o: 28] [n: 588753] [c: 00007FF721DE91F0] [owr: 00007FF46F050138]
[000001F49CD2FC40] MapProperty /Script/Sparta.SpartaMetaSave:ActiveSaveRecoverySlot [o: 30] [n: 588739] [c: 00007FF721DE8F70] [owr: 00007FF46F050138] [kp: 000001F4ACD6DA60] [vp: 000001F4ACD6D9F0]
[000001F49CD2DDA0] MapProperty /Script/Sparta.DataLayersSave:SavedDataLayerStates [o: B8] [n: 587BA2] [c: 00007FF721DE8F70] [owr: 00007FF46F045338] [kp: 000001F4ACD689E0] [vp: 000001F4ACDA3300]
[000001F4ACDF3280] ArrayProperty /Script/Sparta.SpartaSaveSubSystem:SaveInterfaces [o: 110] [n: 589187] [c: 00007FF721DE8910] [owr: 00007FF46F057B38] [ai: 000001F4ACDF3200]
```

---

## 3. Structs and enums used by the save system

From `raw\CXXHeaderDump\Sparta.hpp` (verbatim, with declared offsets):

| Struct | Layout | Size |
|---|---|---|
| `FSpartaSaveContext` | `SaveSubsystem: USpartaSaveSubSystem* @0x00`; `EventTags: FGameplayTagContainer @0x08`; `SlotIndex: int32 @0x28`; `bSaveWholeObject: bool @0x2c`; `IsSaving: bool @0x2d`; `IsSavingSession: bool @0x2e` | `0x30` |
| `FSaveObjectTickFunction` | `: FTickFunction`, `Target: USpartaSaveObject* @0x28` | `0x30` |
| `FSpartaMapBool` | `Map: TMap<FName,bool> @0x00` | `0x50` |
| `FSpartaMapFloat` | `Map: TMap<FName,float> @0x00` | `0x50` |
| `FSpartaMapInt` | `Map: TMap<FName,int32> @0x00` | `0x50` |
| `FSpartaMapName` | `Map: TMap<FName,FName> @0x00` | `0x50` |
| `FSpartaMapObject` | `ActorObjectMap: TMap<FName,UObject*> @0x00` | `0x50` |
| `FSpartaSaveDataMapItem` | `Data: TMap<FName,FString> @0x00` | `0x50` |
| `FPossibleWorldState` | `WorldState: FGameplayTag @0x00`; `NestedWorldStateSets: TArray<UPossibleWorldStateSet*> @0x08` | `0x18` |
| `FPermanentStats` | `PermanentStats: TMap<FGameplayTag,int32> @0x00` (type of `PlayerSaveObject.PlayerStats`) | `0x50` |
| `FPickupTemplate` | `StackCount: int32 @0x00`; `ItemDef: TSubclassOf<USpartaItemDefinition> @0x08` | `0x10` |

Enums:

| Enum | Values | Where |
|---|---|---|
| `/Script/Sparta.ESaveObjectProtocol` | `PerInstance = 0`, `Global = 1`, `ESaveObjectProtocol_MAX = 2` | `Sparta_enums.hpp`; used by `SpartaSaveComponent.SaveProtocol` |
| `/Script/Engine.EDataLayerRuntimeState` | UNKNOWN value names — it is an engine enum and the dump only records the type reference (`[em: …]`) | type of `DataLayersSave.SavedDataLayerStates` values |
| `/Game/Sparta/Core/Interaction/E_CoreBosses` | `NewEnumerator0..10` — **no semantic labels** | type of `BPSO_BossDLManager.CurrentBoss` |
| `/Game/Sparta/Core/Interaction/E_DungeonButton_VisualTypes` | `NewEnumerator0..4` | `BP_InteractionOpenGate.ButtonVisualtype` |
| `/Game/Sparta/Core/Interaction/E_ChestRewardtype` | `NewEnumerator0..3` | `BP_Interactable_Chest_General.RewardType` |
| `/Game/Sparta/Core/Interaction/E_FlowerTypeMaterials` | `NewEnumerator0..2` (`E_FlowerTypeMaterials_enums.hpp`, 163 B) | `BP_Interactable_Chest_General.Visual Type` |

`ESaveObjectProtocol` is the important one: it explains why one save class can be shared by many
actors (`Global`, e.g. `BPSO_SavedLockedChests`, `BP_SO_SavedKeys`) while another is duplicated per
actor (`PerInstance`, e.g. `BPSO_TarGate` — 28 live instances in-game, §13).

Evidence:

```
[000001F4ACDF3580] ObjectProperty /Script/Sparta.SpartaSaveSubSystem:UnsubscribeSaveInterface:Object [o: 0] [n: 20B] [c: 00007FF721DE9420] [owr: 00007FF46F712518] [pc: 00007FF46EE50070]
[000001F4ACD72F00] EnumProperty /Script/Sparta.SpartaSaveComponent:SaveProtocol [o: F8] [n: 5864D1] [c: 00007FF721DE7380] [owr: 00007FF46F038738] [em: 00007FF46F3D9C98]
```

Enum header (`raw\CXXHeaderDump\Sparta_enums.hpp`):

```cpp
enum class ESaveObjectProtocol {
    PerInstance = 0,
    Global = 1,
    ESaveObjectProtocol_MAX = 2,
};
```

---

## 4. Delegates (every one on/around the world-state classes)

`[df: …]` on the property line is a reference to the `DelegateFunction` that defines the signature.
Resolving the four distinct `df:` targets used here gives the parameter lists:

| Owner | Delegate | Type | Offset | Signature (resolved `df:`) |
|---|---|---|---|---|
| `/Script/Sparta.SpartaSaveComponent` | `OnSaveLoaded` | `MulticastInlineDelegateProperty` | `0xc8` | `OnSaveGameEvent__DelegateSignature(SaveGame: StructProperty of /Script/Sparta.SpartaSaveContext)` |
| `/Script/Sparta.SpartaSaveComponent` | `OnPreSave` | `MulticastInlineDelegateProperty` | `0xd8` | same `OnSaveGameEvent__DelegateSignature` |
| `/Script/Sparta.SpartaSaveComponent` | `OnSessionReset` | `MulticastInlineDelegateProperty` | `0xe8` | `OnSaveComponentEvent__DelegateSignature()` — no parameters |
| `/Script/Sparta.SpartaSaveSubSystem` | `OnSaveGameLoaded` | `MulticastInlineDelegateProperty` | `0x30` | `SpartaSaveLoadedSignature__DelegateSignature(Context: StructProperty of /Script/Sparta.SpartaSaveContext)` |
| `/Script/Sparta.SpartaSaveSubSystem` | `PostSessionReset` | `MulticastInlineDelegateProperty` | `0x40` | `SpartaSaveInitializedSignature__DelegateSignature()` |
| `/Script/Sparta.SpartaSaveSubSystem` | `OnSaveSystemInitialized` | `MulticastInlineDelegateProperty` | `0x50` | `SpartaSaveInitializedSignature__DelegateSignature()` |
| `/Script/Sparta.SpartaSaveSubSystem` | `OnSaveStarted` | `MulticastInlineDelegateProperty` | `0x60` | `SpartaSaveInitializedSignature__DelegateSignature()` |
| `/Script/Sparta.SpartaSaveSubSystem` | `OnSaveFinished` | `MulticastInlineDelegateProperty` | `0x70` | `SpartaSaveInitializedSignature__DelegateSignature()` |
| `/Game/…/BP_HubLift.BP_HubLift_C` | `LiftStart` | `MulticastInlineDelegateProperty` | `0x468` | `LiftStart__DelegateSignature(TargetFloor: IntProperty)` |
| `/Game/…/BP_HubLift.BP_HubLift_C` | `LiftEnd` | `MulticastInlineDelegateProperty` | `0x478` | `LiftEnd__DelegateSignature()` |
| `/Game/…/BP_HiddenWall.BP_HiddenWall_C` | `SolutionHiddenWallTriggered` | `MulticastInlineDelegateProperty` | `0x358` | `SolutionHiddenWallTriggered__DelegateSignature()` |
| `/Game/…/BP_LandingAreaBase.BP_LandingAreaBase_C` | `OnEnableStateChanged` | `MulticastInlineDelegateProperty` | `0x538` | UNKNOWN signature (no `__DelegateSignature` Function object found on this class) |
| `/Game/…/BPO_STH.BPO_STH_C` | `OnEnterComplete` | `MulticastInlineDelegateProperty` | `0x30` | `OnEnterComplete__DelegateSignature()` |
| `/Game/…/BPO_STH.BPO_STH_C` | `OnLeaveComplete` | `MulticastInlineDelegateProperty` | `0x40` | `OnLeaveComplete__DelegateSignature()` |
| `/Game/…/BPO_STH_Beacon.BPO_STH_Beacon_C` | `OnEnterEvent` | `MulticastInlineDelegateProperty` | `0xe0` | `OnEnterEvent__DelegateSignature(Location: Vector, Rotation: Rotator)` |
| `/Game/…/BPO_STH_Beacon.BPO_STH_Beacon_C` | `OnLeaveEvent` | `MulticastInlineDelegateProperty` | `0xf0` | `OnLeaveEvent__DelegateSignature(Location: Vector, Rotation: Rotator)` |
| `/Game/…/BPO_STH_Beacon.BPO_STH_Beacon_C` | `OnCompletedEvent` | `MulticastInlineDelegateProperty` | `0x128` | `OnCompletedEvent__DelegateSignature()` |
| `/Game/…/BPO_STH_Generic.BPO_STH_Generic_C` | `OnEnterEvent` | `MulticastInlineDelegateProperty` | `0xc8` | `OnEnterEvent__DelegateSignature(Location: Vector, Rotation: Rotator)` |
| `/Game/…/BPO_STH_Generic.BPO_STH_Generic_C` | `OnLeaveEvent` | `MulticastInlineDelegateProperty` | `0xd8` | `OnLeaveEvent__DelegateSignature(Location: Vector, Rotation: Rotator)` |
| `/Game/…/BP_InteractableBase.BP_InteractableBase_C` | `OnQuestSelected` | `MulticastInlineDelegateProperty` | `0x2c0` | `OnQuestSelected__DelegateSignature(QuestID: FName, Quest: ObjectProperty of /Game/Sparta/…/BP_QuestObject.BP_QuestObject_C)` |
| `/Game/…/BP_InteractableBase.BP_InteractableBase_C` | `OnInteractDispatch` | `MulticastInlineDelegateProperty` | `0x2f0` | `OnInteractDispatch__DelegateSignature(BPInteractableBase: ObjectProperty of /Game/…/BP_InteractableBase.BP_InteractableBase_C)` |
| `/Game/…/BPC_InteractionHandler.BPC_InteractionHandler_C` | `OnInteraction` | `MulticastInlineDelegateProperty` | `0x298` | 3 params, proven by the binder signature in `BP_InteractableBase.hpp`: `(InputAction: ObjectProperty of UInputAction, Component: ObjectProperty of BPC_InteractionHandler_C, InteractID: NameProperty)` — **fires on every interact** |
| `/Game/…/BPC_InteractionHandler.BPC_InteractionHandler_C` | `OnFocusStateChanged` | `MulticastInlineDelegateProperty` | `0x320` | `(HasFocus: BoolProperty)` |
| `/Game/…/BPC_InteractionHandler.BPC_InteractionHandler_C` | `OnRelevancyStateChanged` | `MulticastInlineDelegateProperty` | `0x330` | `(HasRelevancy: BoolProperty)` |
| `/Game/…/BPC_InteractionHandler.BPC_InteractionHandler_C` | `OnInteractionEnd` | `MulticastInlineDelegateProperty` | `0x340` | UNKNOWN signature |
| `/Game/…/BP_Spawner.BP_Spawner_C` | `OnSpawnedEnemyDeathStarted` | `MulticastInlineDelegateProperty` | `0x378` | `(OwningActor: Actor)` |
| `/Game/…/BP_Spawner.BP_Spawner_C` | `OnSpawnedEnemyDeathFinished` | `MulticastInlineDelegateProperty` | `0x388` | `(OwningActor: Actor)` |
| `/Game/…/BP_Spawner.BP_Spawner_C` | `OnSpawnedEnemyDestroyed` | `MulticastInlineDelegateProperty` | `0x398` | `(DestroyedActor: Actor)` |
| `/Game/…/BP_Spawner.BP_Spawner_C` | `OnEnemySpawned` | `MulticastInlineDelegateProperty` | `0x3a8` | `(SpawnedCharacter: ObjectProperty of /Game/…/BP_AICharacter.BP_AICharacter_C)` |
| `/Game/…/BP_Spawner.BP_Spawner_C` | `OnEnemySpawnedFinished` | `MulticastInlineDelegateProperty` | `0x4f8` | `(SpawnedCharacter: ASpartaAICharacter*, SpawnerRef: BP_Spawner_C*)` |
| `/Game/…/BP_Spawner.BP_Spawner_C` | `SpawnerSaveLoaded` | `MulticastInlineDelegateProperty` | `0x668` | — |
| `/Game/…/BP_DungeonEventSelector.BP_DungeonEventSelector_C` | `OnLevelInstanceLoaded` | `MulticastInlineDelegateProperty` | `0x340` | — |
| `/Game/…/SpartaSettingsSaveObject.SpartaSettingsSaveObject_C` | `OnScalabilityPropagated` | `MulticastInlineDelegateProperty` | `0xe0` | — |
| `/Game/…/SpartaSettingsSaveObject.SpartaSettingsSaveObject_C` | `OnGraphicsSettingsUpdated` | `MulticastInlineDelegateProperty` | `0x110` | — |

Evidence for the signature resolution (`df:` targets):

```
[00007FF46F3D02E8] DelegateFunction /Script/Sparta.OnSaveGameEvent__DelegateSignature [n: 525714] [c: 00007FF46EE56338] [or: 00007FF46F17CD68]
00007FF46F3D02E8  StructProperty  /Script/Sparta.OnSaveGameEvent__DelegateSignature:SaveGame
[00007FF46F3D0208] DelegateFunction /Script/Sparta.OnSaveComponentEvent__DelegateSignature [n: 5256FF] [c: 00007FF46EE56338] [or: 00007FF46F17CD68]
[00007FF46F3D3EC8] DelegateFunction /Script/Sparta.SpartaSaveLoadedSignature__DelegateSignature [n: 525FB1] [c: 00007FF46EE56338] [or: 00007FF46F17CD68]
00007FF46F3D3EC8  StructProperty  /Script/Sparta.SpartaSaveLoadedSignature__DelegateSignature:Context
[00007FF46F3D3DE8] DelegateFunction /Script/Sparta.SpartaSaveInitializedSignature__DelegateSignature [n: 525F97] [c: 00007FF46EE56338] [or: 00007FF46F17CD68]
```

Evidence for the properties:

```
[000001F4ACDF4F00] MulticastInlineDelegateProperty /Script/Sparta.SpartaSaveSubSystem:OnSaveGameLoaded [o: 30] [n: 5891D4] [c: 00007FF721DE9010] [owr: 00007FF46F057B38] [df: 00007FF46F3D3EC8]
[000001F4ACDF5100] MulticastInlineDelegateProperty /Script/Sparta.SpartaSaveSubSystem:OnSaveFinished [o: 70] [n: 5891AE] [c: 00007FF721DE9010] [owr: 00007FF46F057B38] [df: 00007FF46F3D3DE8]
```

---

## 5. Save-object Blueprint classes (store A contents)

**51 BlueprintGeneratedClass save objects** are direct descendants of
`/Script/Sparta.SpartaSaveObject`, `/Script/Sparta.PlayerSaveObject`,
`/Script/Sparta.PlayerProgressionSaveData` or `/Script/Sparta.SpartaSessionSaveObjectBase`
(query `scripts\q\ws-metrics.sql`, `save_objects_blueprint = 51`). Naming split:
**46** match `BPSO_*`, **3** match `BP_SO_*`, **2** other (`BP_SpartaPlayerSaveObject`,
`BP_SpawnerSessionSaveObject`). A 52nd save game class, `SpartaSettingsSaveObject_C`, is a plain
`USaveGame` and is covered in §5.4.

All evidence lines for the class objects are in the block after §5.2.

### 5.1 `BPSO_*` / `BP_SO_*` property table

Offsets are from `raw\CXXHeaderDump\<Class>.hpp` and agree with the dump's `[o: xx]`.
`UberGraphFrame` at `0xb8` is a Blueprint implementation detail, listed once here and omitted from
later tables where it is the only extra member.

| Class | Property | Type | Offset | Meaning (from name) |
|---|---|---|---|---|
| `/Game/Sparta/Core/Interaction/SaveObjects/BPSO_SavedLockedChests.BPSO_SavedLockedChests_C` | `SavedChestIDs` | `TArray<TSoftClassPtr<USpartaItemDefinition>>` | `0xb8` | which locked chests were opened |
| | `HasBeenOpened` | `bool` | `0xc8` | |
| `/Game/Sparta/Core/Interaction/SaveObjects/BP_SO_SavedKeys.BP_SO_SavedKeys_C` | `SavedKeyIDs` | `TArray<FString>` | `0xb8` | **keys picked up, as strings** |
| `/Game/Sparta/Core/Interaction/SaveObjects/BP_SO_SavedLockedDoors.BP_SO_SavedLockedDoors_C` | `SavedDoorIDs` | `TArray<TSoftClassPtr<USpartaItemDefinition>>` | `0xb8` | **locked doors already unlocked** |
| `/Game/Sparta/Core/Interaction/SaveObjects/BP_SO_SimpleBool.BP_SO_SimpleBool_C` | `MyBool` | `bool` | `0xb8` | **the generic one-bit world flag** (27 live instances in-game) |
| `/Game/Sparta/Core/SaveObjects/BPSO_BearTrap.BPSO_BearTrap_C` | `IsDisarmed` | `bool` | `0xb8` | bear trap disarmed |
| `…/BPSO_BossDLManager.BPSO_BossDLManager_C` | `CurrentBoss` | `TEnumAsByte<E_CoreBosses::Type>` (`ByteProperty`) | `0xb8` | current boss phase |
| | `RelevantDL` | `UDataLayerAsset*` | `0xc0` | **which DataLayer is active for the boss** |
| `…/BPSO_BossGate.BPSO_BossGate_C` | `Unlocked` | `bool` | `0xb8` | **boss-arena gate unlocked** |
| `…/BPSO_CinematicDLUpdate.BPSO_CinematicDLUpdate_C` | `CinematicFired` | `bool` | `0xb8` | |
| `…/BPSO_CloseAndOpenGate.BPSO_CloseAndOpenGate_C` | `CombatEncCompleted` | `bool` | `0xb8` | arena gate opened by combat completion |
| `…/BPSO_CraftingItemPickup.BPSO_CraftingItemPickup_C` | `ItemCollected` | `bool` | `0xb8` | **item picked up** |
| | `ItemInitialized` | `bool` | `0xb9` | |
| `…/BPSO_DoorLockedDungeon.BPSO_DoorLockedDungeon_C` | `DoorOpened` | `bool` | `0xb8` | **dungeon door opened** |
| `…/BPSO_EridrimUnlockManager.BPSO_EridrimUnlockManager_C` | `WardenJumped` | `bool` | `0xb8` | |
| | `WardenDefeated` | `bool` | `0xb9` | |
| | `ShellClaimed` | `bool` | `0xba` | |
| `…/BPSO_EvilStatue.BPSO_EvilStatue_C` | `Cleansed` | `bool` | `0xb8` | |
| | `RewardGiven` | `bool` | `0xb9` | |
| `…/BPSO_FC_CorruptionCore.BPSO_FC_CorruptionCore_C` | `Cleansed` | `bool` | `0xb8` | |
| `…/BPSO_FC_FinalBossManager.BPSO_FC_FinalBossManager_C` | `EndStrandingCompleted` | `bool` | `0xb8` | |
| | `AllCorruptionCoresClensed` | `bool` | `0xb9` | |
| | `ClensedCores` | `int32` | `0xbc` | |
| | `TotalCoresNeeded` | `int32` | `0xc0` | |
| `…/BPSO_FC_TarRoots.BPSO_FC_TarRoots_C` | `T1Cleansed` / `T2Cleansed` / `T3Clensed` / `CenterClensed` | `bool` | `0xb8`/`0xb9`/`0xba`/`0xbb` | |
| `…/BPSO_FallingCorpse.BPSO_FallingCorpse_C` | `ShotDown` | `bool` | `0xb8` | |
| `…/BPSO_FallingStatue.BPSO_FallingStatue_C` | `StatueFallen` | `bool` | `0xb8` | |
| `…/BPSO_FlowerChest.BPSO_FlowerChest_C` | `ItemCollected` | `bool` | `0xb8` | |
| `…/BPSO_Gate.BPSO_Gate_C` | `GateOpened` | `bool` | `0xb8` | **gate opened** |
| | `GotVarsOnce` | `bool` | `0xb9` | |
| | `StartingLoc` / `EndingLoc` | `FVector` | `0xc0` / `0xd8` | |
| | `StartingRot` / `EndingRot` | `FRotator` | `0xf0` / `0x108` | |
| `…/BPSO_GenessaSensor.BPSO_GenessaSensor_C` | `SensorClensed` | `bool` | `0xb8` | |
| `…/BPSO_GrishaCage.BPSO_GrishaCage_C` | `CageOpened` | `bool` | `0xb8` | |
| | `OpenType` | `FGameplayTag` | `0xbc` | **how it was opened (tag)** |
| `…/BPSO_GrishaGhost.BPSO_GrishaGhost_C` | `BossDefeated` | `bool` | `0xb8` | |
| `…/BPSO_HutchbackBossTrigger.BPSO_HutchbackBossTrigger_C` | `HutchbackDefeated` | `bool` | `0xb8` | |
| `…/BPSO_LandingArea_Boss.BPSO_LandingArea_Boss_C` | `BeaconRisen` | `bool` | `0xb8` | |
| `…/BPSO_MovingObject.BPSO_MovingObject_C` | `MovedToEnd` | `bool` | `0xb8` | **moving platform / drawbridge / lift at end position** |
| | `GotVarsOnce` | `bool` | `0xb9` | |
| | `StartingLoc` / `EndingLoc` | `FVector` | `0xc0` / `0xd8` | |
| | `StartingRot` / `EndingRot` | `FRotator` | `0xf0` / `0x108` | |
| `…/BPSO_NPC.BPSO_NPC_C` | `HasTriggeredInitialGreeting` | `bool` | `0xb8` | |
| | `WasEverKilled` | `bool` | `0xb9` | |
| `…/BPSO_Ovum.BPSO_Ovum_C` | `Cleansed` | `bool` | `0xb8` | |
| | `OvumCollected` | `bool` | `0xb9` | |
| `…/BPSO_PetrifiedCultistManager.BPSO_PetrifiedCultistManager_C` | `Harros` | `bool` | `0xb8` | |
| `…/BPSO_PlayerTrapDungeon.BPSO_PlayerTrapDungeon_C` | `TrappedOnce` | `bool` | `0xb8` | |
| `…/BPSO_QuestLogComponent.BPSO_QuestLogComponent_C` | `UberGraphFrame` | `PointerToUberGraphFrame` | `0xb8` | |
| | `NarrativeTags` | `FGameplayTagContainer` | `0xc0` | **(quests agent's area — listed for completeness)** |
| `…/BPSO_ShellGhost.BPSO_ShellGhost_C` | `StateIndex` | `int32` | `0xb8` | |
| `…/BPSO_ShellRevive.BPSO_ShellRevive_C` | `StationUsed` | `bool` | `0xb8` | |
| `…/BPSO_ShepherdSheepGate.BPSO_ShepherdSheepGate_C` | `GateOpened` | `bool` | `0xb8` | used by `BP_WoodenSelfClosingDoor` |
| `…/BPSO_ShopData.BPSO_ShopData_C` | `CachedItemsMap` | `TMap<TSoftClassPtr<USpartaItemDefinition>, int32>` | `0xb8` | |
| | `TotalGoldSpent` | `int32` | `0x108` | |
| | `StolenItems` | `bool` | `0x10c` | |
| `…/BPSO_SlayerDungeonChalice.BPSO_SlayerDungeonChalice_C` | `SealBroken` | `bool` | `0xb8` | |
| `…/BPSO_SpawnerSaveObj.BPSO_SpawnerSaveObj_C` | `WasEverKilled` | `bool` | `0xb8` | **enemy kept dead (permadeath)** |
| `…/BPSO_TarGate.BPSO_TarGate_C` | `GateClensed` | `bool` | `0xb8` | |
| | `PlayerClensed` | `bool` | `0xb9` | |
| `…/BPSO_TarGolemLevelScript.BPSO_TarGolemLevelScript_C` | `StoneSealClaimed` | `bool` | `0xb8` | |
| `…/BPSO_Tarforge.BPSO_Tarforge_C` | `StoredTarcores` | `int32` | `0xb8` | |
| `…/BPSO_TavernArena.BPSO_TavernArena_C` | `WagerPlaced` | `bool` | `0xb8` | |
| | `ArenaCombatLVL` | `int32` | `0xbc` | |
| | `RewardGiven` | `bool` | `0xc0` | |
| `…/BPSO_WoodenElevator.BPSO_WoodenElevator_C` | `StartingFloor` | `int32` | `0xb8` | **lift floor** |
| | `HasBrokenOnce` | `bool` | `0xbc` | |
| | `StartingLoc` | `FVector` | `0xc0` | |
| `/Game/Sparta/Dialogue/QuestSystem/BPSO_NarrativeEventsTracker.BPSO_NarrativeEventsTracker_C` | `NarrativeEventTimeTracking` | `TMap<FGameplayTag, FF_NarrativeEventFloatTracking>` | `0xb8` | |
| `/Game/Sparta/Maps/…/Lazlo/BPSO_Egon.BPSO_Egon_C` | `GloomDonated` | `int32` | `0xb8` | |
| `/Game/Sparta/SaveObjects/BPSO_ShellMemories.BPSO_ShellMemories_C` | `MemoryProgress` | `TMap<FGameplayTag, int32>` | `0xb8` | |
| | `MemoriesWatched` | `TSet<FName>` | `0x108` | |
| `/Game/Sparta/UI/World/Map/Blueprints/BPSO_WordMapObjective.BPSO_WordMapObjective_C` | `Visible` / `Revealed` / `Completed` / `ShowArea` / `PlayedRevealScript` | `bool` | `0xc0`/`0xc1`/`0xc2`/`0xc3`/`0xc4` | 133 live instances |
| `/Game/Sparta/UI/World/Map/Blueprints/BPSO_WorldMap.BPSO_WorldMap_C` | `Trackers` | `TMap<int32, FVector>` | `0xb8` | |
| | `ObjectiveGUID` | `FString` | `0x108` | |
| | `IsInsideObjective` | `bool` | `0x118` | |
| | `LastPlayerLocation` | `FVector` | `0x120` | |
| `/Game/Sparta/SaveObjects/BPSO_Progression.BPSO_Progression_C` | `CurrentLevel` | `int32` | `0x1a8` | (inherits `PlayerProgressionSaveData`) |
| | `AvailablePoints` | `int32` | `0x1ac` | |
| | `AssignedPoints` | `TMap<FGameplayTag,int32>` | `0x1b0` | |
| | `BondingPoints` | `TMap<FGameplayTag,int32>` | `0x200` | |
| | `GoldSpentAtShop` | `int32` | `0x250` | |
| `/Game/Sparta/Characters/SpawnSystem/BP_SpawnerSessionSaveObject.BP_SpawnerSessionSaveObject_C` | `WasKilled` | `bool` | `0xc0` | session-scoped spawner state |
| | `SpawnerOwner` | `ObjectProperty of BP_Spawner_C` | `0xc8` | |
| | `DidSpawn` / `Debug` / `AttemptedSpawn` | `bool` | `0xd0`/`0xd1`/`0xd2` | |
| | `Owner` | `ObjectProperty of /Script/Sparta.SpartaSaveComponent` | `0xd8` | |
| | `Spawner` | `bool` | `0xe0` | |
| | `SpawnedClass` | `ClassProperty` | `0xe8` | |

`BPSO_PlayerSession` and `BP_SpartaPlayerSaveObject` are large; they belong to the player-look /
player-actions agents but their **save-object shape** is in scope, so they are in §5.3 and §5.4.

### 5.2 Functions declared on save-object classes (complete)

Only a handful of save objects override anything (9 of the 51 declare at least one function; the
other 42 declare none). `ReceiveSpartaPreSave` is the gather hook and `ReceiveOnLoaded` the apply
hook; everything else is a helper.

| Class | Function | Parameters |
|---|---|---|
| `BP_SO_SimpleBool_C` | `ReceiveApplySkipProloguePreset` | — |
| `BPSO_Ovum_C` | `ReceiveApplySkipProloguePreset` | — |
| `BPSO_SpawnerSaveObj_C` | `ReceiveApplySkipProloguePreset` | — |
| `BPSO_TarGolemLevelScript_C` | `ReceiveApplySkipProloguePreset` | — |
| `BPSO_QuestLogComponent_C` | `ReceiveApplySkipProloguePreset` | — |
| | `ReceiveSpartaPreSave` | `SaveContext: StructProperty of /Script/Sparta.SpartaSaveContext` |
| `BPSO_WordMapObjective_C` | `ReceiveTick` | `DeltaTime: FloatProperty` |
| `BPSO_PlayerSession_C` | `SaveShellRevive`, `SaveIsDarkForm`, `SaveHealingCharges`, `SaveShellHealth`, `SaveHealth`, `SaveResolve` | — |
| | `GetSessionStartTransform` | `ReturnValue: StructProperty of /Script/CoreUObject.Transform` |
| | `ReceiveSpartaPreSave` | `SaveContext: StructProperty of /Script/Sparta.SpartaSaveContext` |
| | `ReceiveOnLoaded` | — |
| | `CheckForValidTransform` | — |
| `BP_SpartaPlayerSaveObject_C` | `ReceiveOnLoaded` | — |
| | `ReceiveApplySkipProloguePreset` | — |
| | `ResetForNewGamePlus`, `UnlockDefaultPrologueEquip`, `ApplyDefaultSeal`, `IncrementCumulativeCurrency`, `SetStoryActivityCompleted`, `GetStoryActivityCompleted`, `SetStoryCompletionActivityStarted`, `GetStoryCompletionActivityStarted`, `SetSlayerSealLockout`, `GetSlayerSealLockout`, `SetAchievementEarned`, `IsAchievementEarned`, `SetCinematicWatched`, `CheckIfCinematicWatched`, `SetActiveSeal`, `SetActiveWeapon`, `SetActiveSidearm`, `SetActiveShell`, `SetActiveDarkForm`, `SetActiveCharges`, `SetLuteTrackPlayed`, `GetNumberPlayedLuteTracks`, `IsLandingAreaCleansed`, `UpdateCleansedLandingAreas`, `IsLandingAreaUnlocked`, `UpdateUnlockedLandingAreas`, `UpdateItemPoolState`, `GetEquipmentUnlockState`, `AddUnlockedEquipment`, `AddDropItems`, `RemoveDropItems`, `IncrementTotalDeaths`, `GetFinishedPrologue`, `SetFinishedPrologue` | — (all zero-parameter Blueprint functions) |
| `BP_SpawnerSessionSaveObject_C` | `UpdateAttemptedSpawn`, `SetupOwnerForResuming`, `CanSpawn`, `DebugPrint`, `ReceiveOnLoaded`, `SpawnedEnemyKilled`, `EnemySpawned`, `ReceiveSpartaPreSave`, `ExecuteUbergraph_BP_SpawnerSessionSaveObject` | — / `SaveContext` |

**Important negative:** none of the door/gate/chest/lever `BPSO_*` classes has its own
`ReceiveSpartaPreSave` override. Their state is plain serialized properties; there is no Blueprint
gather logic to hook on them. The gather/apply work happens in the *actor*, via the component's
`OnPreSave` / `OnSaveLoaded` delegates (§4).

Evidence — the 46+3 class objects of the save-object families:

```
[00007FF46F9605A0] BlueprintGeneratedClass /Game/Sparta/SaveObjects/BPSO_PlayerSession.BPSO_PlayerSession_C [n: 9C8B7] [c: 00007FF46EEC3D38] [or: 00007FF46F95FF90] [sps: 00007FF46F057D38]
[000001F4BE037180] BlueprintGeneratedClass /Game/Sparta/SaveObjects/BPSO_Progression.BPSO_Progression_C [n: 344437] [c: 00007FF46EEC3D38] [or: 000001F4BFB61F50] [sps: 00007FF46F038138]
[000001F4BE032320] BlueprintGeneratedClass /Game/Sparta/Dialogue/QuestSystem/BPSO_NarrativeEventsTracker.BPSO_NarrativeEventsTracker_C [n: 29AE34] [c: 00007FF46EEC3D38] [or: 000001F4C41E3260] [sps: 00007FF46F038338]
```

and, for the interaction-folder ones:

```
[000001F4C58A1C00] BlueprintGeneratedClass /Game/Sparta/Core/Interaction/SaveObjects/BP_SO_SavedLockedDoors.BP_SO_SavedLockedDoors_C [n: 13F9A5] [c: 00007FF46EEC3D38] [or: 000001F4A708BE60] [sps: 00007FF46F038338]
[000001F4D4DACA90] BlueprintGeneratedClass /Game/Sparta/Core/Interaction/SaveObjects/BP_SO_SimpleBool.BP_SO_SimpleBool_C [n: 32D4A2] [c: 00007FF46EEC3D38] [or: 000001F4D31F8DA0] [sps: 00007FF46F038338]
[000001F4CDF714E0] BlueprintGeneratedClass /Game/Sparta/Core/Interaction/SaveObjects/BP_SO_SavedKeys.BP_SO_SavedKeys_C [n: 335EFF] [c: 00007FF46EEC3D38] [or: 000001F4D33C4430] [sps: 00007FF46F038338]
```

### 5.3 `BPSO_PlayerSession` (store D — session only)

Parent `/Script/Sparta.SpartaSessionSaveObjectBase`. `bIsSessionSaveData` comes from the base (the
CDO value is not dumped). Header `raw\CXXHeaderDump\BPSO_PlayerSession.hpp`:

| Property | Type | Offset |
|---|---|---|
| `UberGraphFrame` | `PointerToUberGraphFrame` | `0xb8` |
| `Resolve` | `double` | `0xc0` |
| `Health` | `double` | `0xc8` |
| `ShellHealth` | `double` | `0xd0` |
| `HealCurrentCharges` | `double` | `0xd8` |
| `ShellReviveProgress` | `float` | `0xe0` |
| `ShellReviveMaxProgress` | `float` | `0xe4` |
| `StartTransform` | `FTransform` | `0xf0` |
| `PositionCheckFrequency` | `float` | `0x150` |
| `PositionCheckTimerHandle` | `FTimerHandle` | `0x158` |
| `PlayerPawnCache` | `APawn*` | `0x160` |
| `IsInDarkform` | `bool` | `0x168` |
| `AbilitySystemComponent` | `UAbilitySystemComponent*` | `0x170` |

### 5.4 Player-level save objects (referenced from `BP_PlayerController`)

`BP_SpartaPlayerSaveObject` (parent `/Script/Sparta.PlayerSaveObject`) — 60+ own properties. The
world-state-relevant ones:

| Property | Type | Offset |
|---|---|---|
| `ActorBools` | `MapProperty` | `0x340` |
| `DropItems` / `ItemPoolState` | `MapProperty` | `0x390` / `0x3e0` |
| `FinishedPrologue` | `bool` | `0x4b9` |
| `CurrentPrologueCheckpoint` | `FTransform` | `0x4c0` |
| `TotalDeaths` / `TotalBossesDefeated` | `int32` | `0x524` / `0x528` |
| `UnlockedLandingAreas` | `ArrayProperty` | `0x550` |
| `CleansedLandingAreas` | `ArrayProperty` | `0x5a0` |
| `CinematicsWatched` | `FGameplayTagContainer` | `0x5b0` |
| `EarnedAchievements` | `MapProperty` | `0x650` |
| `NewGamePlus` | `int32` | `0x798` |
| `SaveObjectsToResetForNGP` | `ArrayProperty` | `0x890` |
| `SkippedPrologue` | `bool` | `0x9b8` |

`/Game/Sparta/Core/Player/Save/SpartaSettingsSaveObject.SpartaSettingsSaveObject_C` — parent
`/Script/Engine.SaveGame` (i.e. a plain `USaveGame`, **not** a `SpartaSaveObject`, so it is not
covered by `SpartaSaveSubSystem.SaveObjects`). 60+ settings properties from `0x28` to `0x224`
(`bInvertMouseY @0x28`, `MouseSensitivity @0x30`, … `SpeakerConfig @0x224`), plus
`OnScalabilityPropagated @0xe0` and `OnGraphicsSettingsUpdated @0x110`.
Returned by `BP_PlayerController.GetSettingsSaveObject()`.

Evidence:

```
[000001F606B61150] BlueprintGeneratedClass /Game/Sparta/Core/Player/Save/BP_SpartaPlayerSaveObject.BP_SpartaPlayerSaveObject_C [n: 3A373D] [c: 00007FF46EEC3D38] [or: 000001F4D1C15D80] [sps: 00007FF46F038F38]
[00007FF46F87AEE0] BlueprintGeneratedClass /Game/Sparta/Core/Player/Save/SpartaSettingsSaveObject.SpartaSettingsSaveObject_C [n: 1ECD19] [c: 00007FF46EEC3D38] [or: 00007FF46F8751D0] [sps: 00007FF46EF4A738]
```

(`[sps: 00007FF46F038F38]` is `Class /Script/Sparta.PlayerSaveObject`; `[sps: 00007FF46EF4A738]`
is `/Script/Engine.SaveGame`, i.e. `SpartaSettingsSaveObject_C` is a plain `USaveGame`.)

---

## 6. Actor → save-object binding map (the "who reads which save class" table)

This is the most load-bearing table in the report: it is the only dump-provable mapping from a
world actor to the concrete save-object class it uses. Source: class-level `ObjectProperty` whose
`ref` resolves to a `BPSO_*` / `BP_SO_*` class (query `scripts\q\ws-saveobj-bind.sql`, 56 rows —
**53 real bindings** below, plus 3 rows that are the save-object classes' own array properties
(`BPSO_SavedLockedChests.SavedChestIDs`, `BP_SO_SavedLockedDoors.SavedDoorIDs`,
`BP_SO_SavedKeys.SavedKeyIDs`) resolving to their own inner element type, which I excluded as
non-bindings).

| Actor class | Property | Offset | Save-object class |
|---|---|---|---|
| `/Game/Sparta/Core/Interaction/BP_InteractionPickUpKey.BP_InteractionPickUpKey_C` | `SaveObject` | `0x480` | `BP_SO_SavedKeys_C` |
| `/Game/Sparta/Core/Interaction/BP_Interaction_UnlockVisual.BP_Interaction_UnlockVisual_C` | `SaveObject` | `0x3b8` | `BP_SO_SavedKeys_C` |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_CatapultFiring_MissingAmmo.BP_CatapultFiring_MissingAmmo_C` | `As BP SO Simple Bool` | `0x3b8` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Characters/SpawnSystem/BP_TarArena.BP_TarArena_C` | `As` | `0x388` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_FC_CentralIslandTP.BP_Interactable_FC_CentralIslandTP_C` | `As` | `0x350` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Key_EndDungeon.BP_Interactable_Key_EndDungeon_C` | `As` | `0x470` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_LockedLightChest.BP_LockedLightChest_C` | `As` | `0x5b8` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_MakeBridgeByAttack.BP_MakeBridgeByAttack_C` | `As` | `0x3b0` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Interaction/BP_DungeonDoorKey.BP_DungeonDoorKey_C` | `As` | `0x318` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Interaction/BP_GenessaSester_Ghost.BP_GenessaSester_Ghost_C` | `As` | `0x2f0` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Interaction/BP_GenessaUnlockReturn_Manager.BP_GenessaUnlockReturn_Manager_C` | `As` | `0x310` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Interaction/BP_GrishaDeathPit_ExitHandler.BP_GrishaDeathPit_ExitHandler_C` | `As` | `0x2c8` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Interaction/BP_GroupEncRewardGiver.BP_GroupEncRewardGiver_C` | `As` | `0x310` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Interaction/BP_InteractionOpenHiddenWall.BP_InteractionOpenHiddenWall_C` | `As` | `0x490` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Interaction/BP_InteractionStoneGate.BP_InteractionStoneGate_C` | `As` | `0x470` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Interaction/BP_OpenCoffin.BP_OpenCoffin_C` | `As` | `0x4a0` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Interaction/BP_PrisonCeilingBreak.BP_PrisonCeilingBreak_C` | `As` | `0x7e0` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Dialogue/Blueprints/Interactables/BP_IronMaiden.BP_IronMaiden_C` | `As` | `0x380` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_Grand_Elevator_02.BP_Grand_Elevator_02_C` | `As` | `0x550` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_DungeonTransition.BP_DungeonTransition_C` | `As` | `0x350` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/Interaction/BP_LS_CinematicDLManager.BP_LS_CinematicDLManager_C` | `As` | `0x2d0` | `BPSO_CinematicDLUpdate_C` |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_CloseAndOpenLeverGateByAttack.BP_CloseAndOpenLeverGateByAttack_C` | `As` | `0x300` | `BPSO_CloseAndOpenGate_C` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_BagTeleport.BP_BagTeleport_C` | `As` | `0x3b0` | `BPSO_CraftingItemPickup_C` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_EredrimUnlockHandler.BP_Interactable_EredrimUnlockHandler_C` | `As` | `0x378` | `BPSO_EridrimUnlockManager_C` |
| `/Game/Sparta/Core/Interaction/BP_EvilStatue.BP_EvilStatue_C` | `As` | `0x3f0` | `BPSO_EvilStatue_C` |
| `/Game/Sparta/Core/Interaction/BP_FC_CorruptionCore.BP_FC_CorruptionCore_C` | `As` | `0x2e0` | `BPSO_FC_CorruptionCore_C` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_LevelScript_FinalBossManager.BP_LevelScript_FinalBossManager_C` | `As` | `0x368` | `BPSO_FC_FinalBossManager_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRoots_FC.BP_TarRoots_FC_C` | `As` | `0x3d0` | `BPSO_FC_TarRoots_C` |
| `/Game/Sparta/Items/EnvShooting/BP_EnvShootingCagedHead.BP_EnvShootingCagedHead_C` | `As` | `0x4f8` | `BPSO_FallingCorpse_C` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_StatueFallBossRunup.BP_StatueFallBossRunup_C` | `As` | `0x3c0` | `BPSO_FallingStatue_C` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BlockoutGate.BP_BlockoutGate_C` | `As` | `0x3a0` | `BPSO_Gate_C` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_HutchbackBossTrigger.BP_HutchbackBossTrigger_C` | `As` | `0x380` | `BPSO_HutchbackBossTrigger_C` |
| `/Game/Sparta/Core/World/Launch/BP_LandingArea_Boss.BP_LandingArea_Boss_C` | `BeaconSaveObject` | `0x6f8` | `BPSO_LandingArea_Boss_C` |
| `/Game/Sparta/Core/Interaction/BP_Electric_StoneGate.BP_Electric_StoneGate_C` | `As` | `0x480` | `BPSO_MovingObject_C` |
| `/Game/Sparta/Core/Interaction/BP_ProximaUnlockGate.BP_ProximaUnlockGate_C` | `As` | `0x490` | `BPSO_MovingObject_C` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_Drawbridge.BP_Drawbridge_C` | `As` | `0x388` | `BPSO_MovingObject_C` |
| `/Game/Sparta/Core/Interaction/BP_Interaction_Ovum.BP_Interaction_Ovum_C` | `OvumSaveObject` | `0x3f8` | `BPSO_Ovum_C` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_WoodenSelfClosingDoor.BP_WoodenSelfClosingDoor_C` | `As` | `0x370` | `BPSO_ShepherdSheepGate_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_Interactable_TarRoot_BossLock.BP_Interactable_TarRoot_BossLock_C` | `As` | `0x398` | `BPSO_TarGate_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_Interactable_TarRoot_Gate.BP_Interactable_TarRoot_Gate_C` | `As` | `0x380` | `BPSO_TarGate_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRootGate.BP_TarRootGate_C` | `As` | `0x470` | `BPSO_TarGate_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRootGate_03.BP_TarRootGate_03_C` | `As` | `0x6d8` | `BPSO_TarGate_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRoots_MainBossHall.BP_TarRoots_MainBossHall_C` | `As` | `0x378` | `BPSO_TarGate_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRoots_Sml.BP_TarRoots_Sml_C` | `As` | `0x378` | `BPSO_TarGate_C` |
| `/Game/Sparta/Core/Interaction/BP_LS_TavernArenaManager.BP_LS_TavernArenaManager_C` | `As` | `0x2f8` | `BPSO_TavernArena_C` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_EndDungeonRewardDoor.BP_EndDungeonRewardDoor_C` | `As` | `0x378` | `BPSO_WoodenElevator_C` |
| `/Game/Sparta/Core/Interaction/BP_DarkFather_Ritual.BP_DarkFather_Ritual_C` | `As` | `0x3a0` | `BPSO_WoodenElevator_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/BP_Circle_Wooden_Elevator.BP_Circle_Wooden_Elevator_C` | `As` | `0x468` | `BPSO_WoodenElevator_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_Grand_Elevator_Snow.BP_Grand_Elevator_Snow_C` | `As` | `0x4b0` | `BPSO_WoodenElevator_C` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/Wooden_Elevator/BP_Wooden_Elevator_Platform_01.BP_Wooden_Elevator_Platform_01_C` | `As` | `0x4e0` | `BPSO_WoodenElevator_C` |
| `/Game/Sparta/Core/Player/Ability/ShellRevive/GA_ShellReviveAction.GA_ShellReviveAction_C` | `SessionSaveObject` | `0x540` | `BPSO_PlayerSession_C` |
| `/Game/Sparta/UI/World/Map/Widgets/WBP_WMI_AreaBase.WBP_WMI_AreaBase_C` | `MySaveData` | `0x570` | `BPSO_WordMapObjective_C` |
| `/Game/Sparta/UI/Player/Debug/WBP_Debug_MapTracking.WBP_Debug_MapTracking_C` | `HandlerSaveData` | `0x378` | `BPSO_WorldMap_C` |

Evidence (three representative rows, verbatim):

```
[000001F8B9057C30] BlueprintGeneratedClass /Game/Sparta/Core/Common/Interactions/Blueprints/BP_DoorLockedDungeon.BP_DoorLockedDungeon_C [n: 2CDBA5] [c: 00007FF46EEC3D38] [or: 000001F8B894EAC0] [sps: 00007FF46EE52028]
[000001F9DA33D1B0] BlueprintGeneratedClass /Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_Drawbridge.BP_Drawbridge_C [n: 108E81] [c: 00007FF46EEC3D38] [or: 000001F9D9E07630] [sps: 00007FF46EE52028]
[000001F8C1DC3880] BlueprintGeneratedClass /Game/Sparta/Core/Common/Interactions/Blueprints/BP_WoodenSelfClosingDoor.BP_WoodenSelfClosingDoor_C [n: 1D1EDE] [c: 00007FF46EEC3D38] [or: 000001F8C1E230D0] [sps: 00007FF46EE52028]
```

and the binding property itself, e.g.

```
[000001F8C1E3A600] ObjectProperty /Game/Sparta/Core/Common/Interactions/Blueprints/BP_WoodenSelfClosingDoor.BP_WoodenSelfClosingDoor_C:As BPSO Shepherd Sheep Gate [o: 370] [n: 80D1A7] [c: 00007FF721DE9420] [owr: 000001F8C1DC3880] [pc: 000001F4C58A1F90]
```

(`scripts\q\ws-saveobj-refs.sql` prints exactly this relation as
`owner_path → prop → ptype → class`; the 56-row result of that query restricted to class-level
owners is the §6 table (53 bindings + 3 self-references).

---

## 7. Every class that owns a `USpartaSaveComponent` (134)

This is the definitive "what persists in the world" list, obtained by resolving every
`ObjectProperty` whose `ref` is `/Script/Sparta.SpartaSaveComponent` and whose owner is a class
(`scripts\q\ws-savecomp-owners.sql`). 133 Blueprints + 1 native.

Grouping by folder (`scripts\q\ws-savecomp-groups.sql`; the groups sum to 134):

| Folder group | Classes |
|---|---|
| `/Game/Sparta/Core/Interaction*` + `/Game/Sparta/Core/Common/Interactions/*` | 66 |
| `/Game/Sparta/Dialogue/NPCs/*` + `/Game/Sparta/Core/AI/*` + `/Game/Sparta/Maps/Playgrounds/*` | 19 |
| `/Game/Sparta/Environments/*` (elevators, tar-root gates, flower chests) | 14 |
| `/Game/Sparta/Items/*` (pickups) | 8 |
| `/Game/Sparta/Characters/SpawnSystem/*` | 6 |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/*` | 6 |
| `/Game/Sparta/Blueprints/*` (DungeonScripts, prologue trigger) | 5 |
| `/Game/Sparta/Core/World/*` (landing areas, level selectors, spline movement, STH gates) | 5 |
| `/Game/Sparta/Dialogue/Blueprints/*` (Interactables) | 2 |
| `/Game/Sparta/Core/Player/*` + `/Game/Sparta/UI/*` | 2 |
| `/Script/*` (native `AUnitSpawner`) | 1 |

Property is almost always called `SpartaSave` (exceptions noted). Full list:

| Actor class | Prop | Offset |
|---|---|---|
| `/Game/Sparta/Blueprints/BP_FinishedPrologueTrigger.BP_FinishedPrologueTrigger_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_BellBridgeActivator.BP_BellBridgeActivator_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_CatapultFiring_MissingAmmo.BP_CatapultFiring_MissingAmmo_C` | `SpartaSave` | `0x3b0` |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_CloseAndOpenLeverGateByAttack.BP_CloseAndOpenLeverGateByAttack_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_SageInfluence.BP_SageInfluence_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Characters/SpawnSystem/BP_CrownDoor.BP_CrownDoor_C` | `SpartaSave` | `0x2c8` |
| `/Game/Sparta/Characters/SpawnSystem/BP_SpartaLevelScript_HubTower.BP_SpartaLevelScript_HubTower_C` | `SpartaSave` | `0x370` |
| `/Game/Sparta/Characters/SpawnSystem/BP_Spawner.BP_Spawner_C` | `SpartaSave` | `0x348` |
| `/Game/Sparta/Characters/SpawnSystem/BP_SpawnerDependency.BP_SpawnerDependency_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Characters/SpawnSystem/BP_SpawnerSessionSaveObject.BP_SpawnerSessionSaveObject_C` | `Owner` | `0xd8` |
| `/Game/Sparta/Characters/SpawnSystem/BP_TarArena.BP_TarArena_C` | `SpartaSave` | `0x2d8` |
| `/Game/Sparta/Core/AI/BP_NPC_TavernBrig.BP_NPC_TavernBrig_C` | `SpartaSave` | `0xfc8` |
| `/Game/Sparta/Core/AI/BP_NPC_TavernBrig_Belligerent.BP_NPC_TavernBrig_Belligerent_C` | `SpartaSave` | `0xfc8` |
| `/Game/Sparta/Core/AI/BP_NPC_TavernBrig_Unfriendly.BP_NPC_TavernBrig_Unfriendly_C` | `SpartaSave` | `0xfc8` |
| `/Game/Sparta/Core/AI/BP_NPC_TavernDrunk.BP_NPC_TavernDrunk_C` | `SpartaSave` | `0xfc8` |
| `/Game/Sparta/Core/AI/BP_NPC_TavernMusician.BP_NPC_TavernMusician_C` | `SpartaSave` | `0xfc8` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_BagTeleport.BP_BagTeleport_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_DoorLockedDungeon.BP_DoorLockedDungeon_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_EndDungeonRewardDoor.BP_EndDungeonRewardDoor_C` | `SpartaSave` | `0x2c0` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_FP_Lock.BP_FP_Lock_C` | `SpartaSave` | `0x310` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_HiddenWall.BP_HiddenWall_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_HutchbackBossTrigger.BP_HutchbackBossTrigger_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_InteractableBase_Corpse.BP_InteractableBase_Corpse_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Chest.BP_Interactable_Chest_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_EredrimUnlockHandler.BP_Interactable_EredrimUnlockHandler_C` | `SpartaSave` | `0x310` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_FC_CentralIslandTP.BP_Interactable_FC_CentralIslandTP_C` | `SpartaSave` | `0x320` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_GrimoriumSocket.BP_Interactable_GrimoriumSocket_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Key_EndDungeon.BP_Interactable_Key_EndDungeon_C` | `SpartaSave` | `0x338` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Lock_Door.BP_Interactable_Lock_Door_C` | `SpartaSave` | `0x320` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Lock_Door_Tavern.BP_Interactable_Lock_Door_Tavern_C` | `SpartaSave` | `0x320` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Mushroom_FirstPersonEntrance.BP_Interactable_Mushroom_FirstPersonEntrance_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_PlaceBook.BP_Interactable_PlaceBook_C` | `SpartaSave` | `0x310` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Placemushrooms.BP_Interactable_Placemushrooms_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_PutLever.BP_Interactable_PutLever_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_PutMissingAmmo.BP_Interactable_PutMissingAmmo_C` | `SpartaSave` | `0x310` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_SM_SecretPipe.BP_Interactable_SM_SecretPipe_C` | `SpartaSave` | `0x310` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_ShopItem.BP_Interactable_ShopItem_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_MakeBridgeByAttack.BP_MakeBridgeByAttack_C` | `SpartaSave` | `0x2c0` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_ParasiteGolemHandler.BP_ParasiteGolemHandler_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_PlayerTrappedOnOverlap.BP_PlayerTrappedOnOverlap_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_StatueFallBossRunup.BP_StatueFallBossRunup_C` | `SpartaSave` | `0x2d8` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Tarforge.BP_Tarforge_C` | `SpartaSave` | `0x330` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_ThornBoiHandler.BP_ThornBoiHandler_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_VampireKnightHandler.BP_VampireKnightHandler_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_WoodenSelfClosingDoor.BP_WoodenSelfClosingDoor_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Core/Interaction/BP_BearTrap.BP_BearTrap_C` | `SpartaSave` | `0x438` |
| `/Game/Sparta/Core/Interaction/BP_DamageStatue.BP_DamageStatue_C` | `SpartaSave` | `0x3a8` |
| `/Game/Sparta/Core/Interaction/BP_DarkFather_Ritual.BP_DarkFather_Ritual_C` | `SpartaSave` | `0x2e0` |
| `/Game/Sparta/Core/Interaction/BP_DestroyActorsWithTag.BP_DestroyActorsWithTag_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Interaction/BP_DungeonDoorKey.BP_DungeonDoorKey_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Interaction/BP_Electric_StoneGate.BP_Electric_StoneGate_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_EvilStatue.BP_EvilStatue_C` | `SpartaSave` | `0x330` |
| `/Game/Sparta/Core/Interaction/BP_FC_CorruptionCore.BP_FC_CorruptionCore_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Core/Interaction/BP_GenessaCorruptSenser.BP_GenessaCorruptSenser_C` | `SpartaSave` | `0x3e0` |
| `/Game/Sparta/Core/Interaction/BP_GenessaSester_Ghost.BP_GenessaSester_Ghost_C` | `SpartaSave` | `0x2c0` |
| `/Game/Sparta/Core/Interaction/BP_GenessaUnlockReturn_Manager.BP_GenessaUnlockReturn_Manager_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Interaction/BP_GhostMovingFar.BP_GhostMovingFar_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Interaction/BP_GrishaDeathPit_ExitHandler.BP_GrishaDeathPit_ExitHandler_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Core/Interaction/BP_GroupEncRewardGiver.BP_GroupEncRewardGiver_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Interaction/BP_InteractionActivateSiegeMemory.BP_InteractionActivateSiegeMemory_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_InteractionInfinitySealPrologue.BP_InteractionInfinitySealPrologue_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_InteractionKnockOnVillageDoor.BP_InteractionKnockOnVillageDoor_C` | `SpartaSave` | `0x3d0` |
| `/Game/Sparta/Core/Interaction/BP_InteractionLockedOpenGate.BP_InteractionLockedOpenGate_C` | `SpartaSave` | `0x3c0` |
| `/Game/Sparta/Core/Interaction/BP_InteractionMapFragment.BP_InteractionMapFragment_C` | `SpartaSave` | `0x370` |
| `/Game/Sparta/Core/Interaction/BP_InteractionOpenGate.BP_InteractionOpenGate_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_InteractionOpenHiddenWall.BP_InteractionOpenHiddenWall_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_InteractionPickUpGrimorium.BP_InteractionPickUpGrimorium_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_InteractionPickUpKey.BP_InteractionPickUpKey_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_InteractionStoneGate.BP_InteractionStoneGate_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_Interaction_DrinkAlchemyMixture.BP_Interaction_DrinkAlchemyMixture_C` | `SpartaSave` | `0x3a8` |
| `/Game/Sparta/Core/Interaction/BP_Interaction_LightKey.BP_Interaction_LightKey_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_Interaction_Ovum.BP_Interaction_Ovum_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Core/Interaction/BP_Interaction_StartCinematic_Targolem_Prologue.BP_Interaction_StartCinematic_Targolem_Prologue_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_Interaction_UnlockVisual.BP_Interaction_UnlockVisual_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/BP_LS_CinematicDLManager.BP_LS_CinematicDLManager_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Interaction/BP_LS_PetrifiedCultistManager.BP_LS_PetrifiedCultistManager_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Interaction/BP_LS_TavernArenaManager.BP_LS_TavernArenaManager_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Core/Interaction/BP_OpenCoffin.BP_OpenCoffin_C` | `SpartaSave` | `0x3d8` |
| `/Game/Sparta/Core/Interaction/BP_PrisonCeilingBreak.BP_PrisonCeilingBreak_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/Interaction/BP_ProximaUnlockGate.BP_ProximaUnlockGate_C` | `SpartaSave` | `0x3b0` |
| `/Game/Sparta/Core/Interaction/BP_UnlockWeapon.BP_UnlockWeapon_C` | `SpartaSave` | `0x3a0` |
| `/Game/Sparta/Core/Interaction/Equipment/BP_Interactable_Shell_Locked.BP_Interactable_Shell_Locked_C` | `Locked` | `0x310` |
| `/Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C` | `Sparta` | `0x818` |
| `/Game/Sparta/Core/World/Launch/BP_LandingArea_Boss.BP_LandingArea_Boss_C` | `SpartaSave` | `0x688` |
| `/Game/Sparta/Core/World/Levels/BP_DungeonEventSelector.BP_DungeonEventSelector_C` | `SpartaSave` | `0x2f8` |
| `/Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector.BP_LevelInstanceSelector_C` | `SpartaSave` | `0x2e0` |
| `/Game/Sparta/Core/World/SplineMovement/BP_SplineBasedMovement.BP_SplineBasedMovement_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Core/World/Teleporting/BP_STH_Entrance_Gate.BP_STH_Entrance_Gate_C` | `SpartaSave` | `0x508` |
| `/Game/Sparta/Dialogue/Blueprints/Interactables/BP_GloomSiphon.BP_GloomSiphon_C` | `SpartaSave` | `0x340` |
| `/Game/Sparta/Dialogue/Blueprints/Interactables/BP_IronMaiden.BP_IronMaiden_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Dialogue/NPCs/BP_BabyGrisha.BP_BabyGrisha_C` | `SpartaSave` | `0xfc8` |
| `/Game/Sparta/Dialogue/NPCs/BP_BabyGrisha_Rescued.BP_BabyGrisha_Rescued_C` | `SpartaSave` | `0xfe0` |
| `/Game/Sparta/Dialogue/NPCs/BP_Blacksmith.BP_Blacksmith_C` | `SpartaSave` | `0xfd0` |
| `/Game/Sparta/Dialogue/NPCs/BP_Cultist_HUB.BP_Cultist_HUB_C` | `SpartaSave` | `0xfd8` |
| `/Game/Sparta/Dialogue/NPCs/BP_Cultist_HUB_Beacon.BP_Cultist_HUB_Beacon_C` | `SpartaSave` | `0xfd8` |
| `/Game/Sparta/Dialogue/NPCs/BP_Cultist_HUB_OvumCaretaker.BP_Cultist_HUB_OvumCaretaker_C` | `SpartaSave` | `0xfc8` |
| `/Game/Sparta/Dialogue/NPCs/BP_FriendlyGrisha.BP_FriendlyGrisha_C` | `SpartaSave` | `0xfd0` |
| `/Game/Sparta/Dialogue/NPCs/BP_Gorf.BP_Gorf_C` | `SpartaSave` | `0xfe0` |
| `/Game/Sparta/Dialogue/NPCs/BP_Merchant.BP_Merchant_C` | `SpartaSave` | `0xfd8` |
| `/Game/Sparta/Dialogue/NPCs/BP_NPC_GrishaHunter.BP_NPC_GrishaHunter_C` | `SpartaSave` | `0xfc8` |
| `/Game/Sparta/Dialogue/NPCs/BP_NPC_HilgaTransformed.BP_NPC_HilgaTransformed_C` | `SpartaSave` | `0xfd0` |
| `/Game/Sparta/Dialogue/NPCs/BP_NPC_Shopkeeper_Dog.BP_NPC_Shopkeeper_Dog_C` | `SpartaSave` | `0xfe0` |
| `/Game/Sparta/Environments/ItemPool/Meta/BP_AttackFlower.BP_AttackFlower_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Environments/ItemPool/Meta/BP_FlowerChest.BP_FlowerChest_C` | `SpartaSave` | `0x2c8` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/BP_Circle_Wooden_Elevator.BP_Circle_Wooden_Elevator_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_Grand_Elevator_02.BP_Grand_Elevator_02_C` | `SpartaSave` | `0x2c0` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_Grand_Elevator_Snow.BP_Grand_Elevator_Snow_C` | `SpartaSave` | `0x2f8` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_Interactable_TarRoot_BossLock.BP_Interactable_TarRoot_BossLock_C` | `SpartaSave` | `0x2e0` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_Interactable_TarRoot_Gate.BP_Interactable_TarRoot_Gate_C` | `SpartaSave` | `0x2c0` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRootGate.BP_TarRootGate_C` | `SpartaSave` | `0x398` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRootGate_03.BP_TarRootGate_03_C` | `SpartaSave` | `0x678` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRoots_FC.BP_TarRoots_FC_C` | `SpartaSave` | `0x2e0` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRoots_MainBossHall.BP_TarRoots_MainBossHall_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRoots_Sml.BP_TarRoots_Sml_C` | `SpartaSave` | `0x2c0` |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/Wooden_Elevator/BP_Wooden_Elevator_Platform_01.BP_Wooden_Elevator_Platform_01_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Environments/Props/Set_Dressing/P_Grisha_Cage_01/Temp_BP/BP_Grisha_Cage_01_WithLogic.BP_Grisha_Cage_01_WithLogic_C` | `CageSaveComponent` | `0x308` |
| `/Game/Sparta/Items/EnvShooting/BP_EnvShootingCagedHead.BP_EnvShootingCagedHead_C` | `SpartaSave` | `0x438` |
| `/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C` | `SpartaSave` | `0x320` |
| `/Game/Sparta/Items/Pickups/BP_CurrencyBag_Pickup.BP_CurrencyBag_Pickup_C` | `SpartaSave` | `0x310` |
| `/Game/Sparta/Items/Pickups/BP_Old_Artifact_Common.BP_Old_Artifact_Common_C` | `SpartaSave` | `0x320` |
| `/Game/Sparta/Items/Pickups/BP_Old_Artifact_Rare.BP_Old_Artifact_Rare_C` | `SpartaSave` | `0x330` |
| `/Game/Sparta/Items/Pickups/BP_Old_Artifact_Uncommon.BP_Old_Artifact_Uncommon_C` | `SpartaSave` | `0x328` |
| `/Game/Sparta/Items/Pickups/BP_Pickup_VratkoFeetPic.BP_Pickup_VratkoFeetPic_C` | `SpartaSave` | `0x308` |
| `/Game/Sparta/Items/Pickups/QuestItems/BP_CustomPickup_BagheadMoonshine.BP_CustomPickup_BagheadMoonshine_C` | `SpartaSave` | `0x310` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BlockoutGate.BP_BlockoutGate_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BreakableWall.BP_BreakableWall_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_Drawbridge.BP_Drawbridge_C` | `SpartaSave` | `0x2b8` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_DungeonTransition.BP_DungeonTransition_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_HubDoor.BP_HubDoor_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_LevelScript_FinalBossManager.BP_LevelScript_FinalBossManager_C` | `SpartaSave` | `0x2b0` |
| `/Game/Sparta/Maps/Playgrounds/…/Lazlo/BP_Egon_Base.BP_Egon_Base_C` | `SpartaSave` | `0xff8` |
| `/Game/Sparta/Maps/Playgrounds/…/Lazlo/BP_Egon_Dead.BP_Egon_Dead_C` | `SpartaSave` | `0xff0` |
| `/Game/Sparta/UI/Menu/Merchant/BP_ShopHandlerBase.BP_ShopHandlerBase_C` | `ShopSaveComponent` | `0x2d8` |
| `/Script/Sparta.UnitSpawner` (native) | `SaveComponent` | `0x2a8` |

Evidence:

```
[000001F61A189FD0] BlueprintGeneratedClass /Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_HubDoor.BP_HubDoor_C [n: 3D1732] [c: 00007FF46EEC3D38] [or: 000001F4C586EC00] [sps: 00007FF46EE52028]
[000001F61A2E5B00] ObjectProperty /Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_HubDoor.BP_HubDoor_C:SpartaSave [o: 2B0] [n: 5F05A7] [c: 00007FF721DE9420] [owr: 000001F61A189FD0] [pc: 00007FF46F038738]
[000001F8DE10DB80] ObjectProperty /Game/Sparta/Environments/Props/Set_Dressing/P_Grisha_Cage_01/Temp_BP/BP_Grisha_Cage_01_WithLogic.BP_Grisha_Cage_01_WithLogic_C:CageSaveComponent [o: 308] [n: 86633C] [c: 00007FF721DE9420] [owr: 000001F8DE173C10] [pc: 00007FF46F038738]
```

---

## 8. World objects by kind

Common ancestor note: most interactables are `AActor`-derived Blueprints, and there are **two**
parallel interaction base families:

| Base | Path | Used by |
|---|---|---|
| `ABP_InteractableBase_C` | `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_InteractableBase.BP_InteractableBase_C` (parent `/Script/Engine.Actor`) | the **newer** family: chests, lock doors, put-lever, pray-to-open-door, self-closing door, grisha cage, gloom door |
| `ABP_Interaction_C` | `/Game/Sparta/Core/Interaction/BP_Interaction.BP_Interaction_C` (parent `/Script/Engine.Actor`) | the **older** family: open gate, locked open gate, stone gate, lift call, open door, hidden wall |

`BP_InteractableBase_C` members (own props only; parent `AActor`):

| Property | Type | Offset |
|---|---|---|
| `UberGraphFrame` | `PointerToUberGraphFrame` | `0x2a8` |
| `Scene_Root` | `SceneComponent*` | `0x2b0` |
| `BPC_InteractionHandler` | `BPC_InteractionHandler_C*` | `0x2b8` |
| `OnQuestSelected` | `MulticastInlineDelegateProperty` | `0x2c0` |
| `InteractText` | `FText` | `0x2d0` |
| `2DSound` | `SoundWave*` | `0x2e0` |
| `WBP_WorldTooltip` | `WBP_WorldActorTooltip_C*` | `0x2e8` |
| `OnInteractDispatch` | `MulticastInlineDelegateProperty` | `0x2f0` |

`BP_Interaction_C` members (own props only; the class derives directly from `AActor`, so its own
layout starts at `0x2a8` and its **subclasses** therefore start at `0x398`):

| Property | Type | Offset |
|---|---|---|
| `UberGraphFrame` | `PointerToUberGraphFrame` | `0x2a8` |
| `IndicatorWidget` | `SpartaWidgetComponent*` | `0x2b0` |
| `InteractionLocation` | `SceneComponent*` | `0x2b8` |
| `Trigger` | `BoxComponent*` | `0x2c0` |
| `InteractionRootComponent` | `SceneComponent*` | `0x2c8` |
| `InputContext` | `InputMappingContext*` | `0x2d0` |
| `TriggerInputHandle` / `CompletedInputHandle` / `StartedInputHandle` | `FInputActionEventHandle` | `0x2d8` / `0x2dc` / `0x2e0` |
| `IsActive` | `bool` | `0x2e4` |
| `TagQuery` | `FGameplayTagQuery` | `0x2e8` |
| `PromptWidget` | `TArray<WBP_InteractionPrompt_Generic_C*>` | `0x330` |
| `ElapsedTime` | `float` | `0x340` |
| `Inputs` | `TArray<USpartaInputInteraction*>` | `0x348` |
| `Prompts` | `TArray<FFInteractionPrompt>` | `0x358` |
| `RelevanceDistanceOffset` | `double` | `0x368` |
| `DotIndicator` | `WBP_InteractionIndicator_C*` | `0x370` |
| `InteractionDurationGameplayEffects` | `TArray<TSubclassOf<UGameplayEffect>>` | `0x378` |
| `InteractionDurationEffectHandles` | `TArray<FActiveGameplayEffectHandle>` | `0x388` |

`BP_Interaction_C` functions with parameters (the rest are zero-argument):

| Function | Parameters |
|---|---|
| `GetPayloadObject` | `PayloadObject: ObjectProperty of /Script/CoreUObject.Object` (out) |
| `GetWarpTargets` / `GetWarpReferences` | `WarpReference: ArrayProperty of FMotionWarpingTarget` (out) |
| `RemoveWarpReferences` / `AddWarpReferences` | `Character: ObjectProperty of /Script/Sparta.SpartaCharacter` |
| `CanFocus` / `IsRelevant` | `FailureReasons: StrProperty` (out) |
| `GetInputFromID` | `Input ID: NameProperty` |
| `ShouldBindInput` | `Input: ObjectProperty of /Script/Sparta.SpartaInputInteraction` |
| `OverrideInteractionPrompt` | `Prompt: StructProperty of FFInteractionPrompt`, `Show: BoolProperty` (out) |
| `ShouldStartActive` | `bActive: BoolProperty` (out) |
| `ReceiveEndPlay` | `EndPlayReason: EnumProperty of EEndPlayReason` |
| `ReceiveTick` | `DeltaSeconds: FloatProperty` |
| `OnInputEvent_Completed` / `_Started` / `_Cancelled` / `_Ongoing` / `_ProgressChanged` | `Input: ObjectProperty of /Script/Sparta.SpartaInputInteraction` |

### 8.1 Doors

| Class | Parent | Persisted state | Where |
|---|---|---|---|
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_HubDoor.BP_HubDoor_C` | `AActor` | `DoorIsOpenName: FName @0x2d8` (store-B key!) + `DoorIsOpen: bool @0x2e0`; `SpartaSave @0x2b0` | store B |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_HubDoubleDoor.BP_HubDoubleDoor_C` | `BP_HubDoor_C` | inherits; adds `ShiftingUp? @0x358`, `AudioLoop`, `TimeLeft` | store B |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_HubDoorManager.BP_HubDoorManager_C` | `AActor` | `DoorReferences: TArray<TSoftObjectPtr<BP_HubDoor_C>> @0x2b8`; `WeaponDoor @0x2c8`; `SideArmDoor @0x2f0`; `ExcavationDoor @0x318` | no own save |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Lock_Door.BP_Interactable_Lock_Door_C` | `BP_InteractableBase_C` | `SpartaSave @0x320`, `DontSaveDoorOpened: bool @0x370`, `KeyRequired: TMap<TSoftClassPtr<USpartaItemDefinition>,int32> @0x378` | `BP_SO_SimpleBool_C` via `UpdateMyBool`/`GetMySaveData` |
| `…/BP_Interactable_Lock_Door_Tavern.BP_Interactable_Lock_Door_Tavern_C` | `BP_InteractableBase_C` | same + `TavernArenaManagerRef @0x4d8`, `Wager: int32 @0x500` | same |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_DoorLockedDungeon.BP_DoorLockedDungeon_C` | `AActor` | `TombstonesBroken: int32 @0x330`, `Keys: TArray<TSoftObjectPtr<BP_DungeonDoorKey_C>> @0x348`, `SpartaSave @0x2b0` | `BPSO_DoorLockedDungeon_C.DoorOpened` |
| `…/BP_EndDungeonRewardDoor.BP_EndDungeonRewardDoor_C` | `AActor` | `Locks: TArray<BP_Interactable_Lock_EndDungeon_C*> @0x300`, `SpartaSave @0x2c0`, `As BPSO Wooden Elevator @0x378` | `BPSO_WoodenElevator_C` |
| `…/BP_GloomDoor.BP_GloomDoor_C` | `BP_InteractableBase_C` | `KeyRequired @0x3b0`, `Heart: BP_Interaction_CorruptionCore_C* @0x5d0`, `LoadedRequirements @0x570`; **no `SpartaSave` property and it is not in the 134-row owner list** — persistence UNKNOWN (see §12 #12) | UNKNOWN |
| `…/BP_WoodenSelfClosingDoor.BP_WoodenSelfClosingDoor_C` | `AActor` | `StartingRot @0x340`, `EndingRot @0x358`, `As BPSO Shepherd Sheep Gate @0x370`, `SpartaSave @0x2b8` | `BPSO_ShepherdSheepGate_C` (`ReadGateOpenedSave`) |
| `/Game/Sparta/Core/Interaction/BP_InteractionOpenDoor.BP_InteractionOpenDoor_C` | `BP_Interaction_C` | `DoorRef: StaticMeshActor* @0x3b0`, `Initial Rotation @0x3b8`, `DoorOpenSFX`, `DoorCloseSFX` | **no SpartaSave prop** — parent has none; see §8.7 note |
| `…/BP_InteractionKnockOnVillageDoor.BP_InteractionKnockOnVillageDoor_C` | `BP_Interaction_C` | `SpartaSave @0x3d0` | store A (class not in §6) |
| `/Game/Sparta/Characters/SpawnSystem/BP_CrownDoor.BP_CrownDoor_C` | `AActor` | `SpartaSave @0x2c8` | store A |
| `/Game/Sparta/Blueprints/BP_GolemDoorFall.BP_GolemDoorFall_C` | `AActor` | no save component, no `BPSO` ref | none |

`BP_HubDoor_C` functions (from `.hpp`, dump-confirmed via `ExecuteUbergraph_BP_HubDoor`):

| Function | Parameters |
|---|---|
| `OpenDoorAnim__FinishedFunc` / `OpenDoorAnim__UpdateFunc` | — |
| `OpenDoor` / `CloseDoor` | — |
| `BndEvt__BP_HubDoor_SpartaSave_K2Node_ComponentBoundEvent_0_OnSaveGameEvent__DelegateSignature` | `SaveGame: StructProperty of /Script/Sparta.SpartaSaveContext` |
| `ReceiveBeginPlay` | — |
| `ExecuteUbergraph_BP_HubDoor` | `EntryPoint: IntProperty` |

Evidence:

```
[000001F61A2E5B00] ObjectProperty /Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_HubDoor.BP_HubDoor_C:SpartaSave [o: 2B0] [n: 5F05A7] [c: 00007FF721DE9420] [owr: 000001F61A189FD0] [pc: 00007FF46F038738]
[000001F6199BBC30] NameProperty /Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_HubDoor.BP_HubDoor_C:DoorIsOpenName [o: 2D8] [n: 62B204] [c: 00007FF721DE90B0] [owr: 000001F61A189FD0]
[000001F61A2E5880] BoolProperty /Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_HubDoor.BP_HubDoor_C:DoorIsOpen [o: 2E0] [n: 62B1FE] [c: 00007FF721DE8D80] [owr: 000001F61A189FD0]
```

### 8.2 Gates, boss-arena gates, fog walls

| Class | Parent | Persisted state | Store |
|---|---|---|---|
| `/Game/Sparta/Core/Interaction/BP_InteractionOpenGate.BP_InteractionOpenGate_C` | `BP_Interaction_C` | `SpartaSave @0x3a0`, `SaveGateOpen: bool @0x438`, `UseLever: bool @0x460`, `ButtonVisualtype @0x4b8`; functions `SaveGateOpened()`, `GetSaveData()`, `ForceOpen()`, `OpenGateForcefully()` | `BP_SO_SimpleBool_C` |
| `…/BP_InteractionLockedOpenGate.BP_InteractionLockedOpenGate_C` | `BP_Interaction_C` | `SpartaSave @0x3c0`, `KeyRequired @0x440`, `RemoveActorsWhenGateOpen @0x4f8`, `TagOfActorsToRemove: FName @0x4fc`; functions `OpenGate()`, `SaveGateOpen()` | store A |
| `…/BP_InteractionStoneGate.BP_InteractionStoneGate_C` | `BP_Interaction_C` | `SpartaSave @0x3a0`, `As BP SO Simple Bool @0x470` | `BP_SO_SimpleBool_C` |
| `…/BP_Electric_StoneGate.BP_Electric_StoneGate_C` | `BP_Interaction_C` | `SpartaSave @0x3a0`, `As BPSO_MovingObject @0x480` | `BPSO_MovingObject_C` |
| `/Game/Sparta/Core/Interaction/BP_PressurePlateOpenDoor.BP_PressurePlateOpenDoor_C` | `AActor` | `Opened: bool @0x361`, `RemainOpenAfterFirstInteract: bool @0x360`, `ArrowOrigins: TArray<ATargetPoint*> @0x330`; **no SpartaSave** | none |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_HiddenWall.BP_HiddenWall_C` | `AActor` | `RevealedByHit @0x340`, `AlreadyRevealed @0x368`, `SaveWallRevealed: bool @0x399`, `SolutionHiddenWallTriggered` delegate @0x358, `SpartaSave @0x2b0` | `BP_SO_SimpleBool_C` |
| `…/BP_InteractionOpenHiddenWall.BP_InteractionOpenHiddenWall_C` | `BP_Interaction_C` | `SpartaSave @0x3a0`, `As BP SO Simple Bool @0x490` | `BP_SO_SimpleBool_C` |
| `/Game/Sparta/Core/World/Levels/BP_BossGate.BP_BossGate_C` | `AActor` | **no save component prop**; meshes `BossGate`, `Fog`, `Plane`, `Trigger`; only `ApproachingGate()` + timeline (pure presentation/trigger) | none |
| `/Game/Sparta/Core/World/Levels/BP_BlockedBossGate.BP_BlockedBossGate_C` | `AActor` | **no save component, no functions** — 3 components only | none |
| `/Game/Sparta/Core/World/Teleporting/BP_STH_Entrance_Gate.BP_STH_Entrance_Gate_C` | `BP_STH_Entrance_C` | `SpartaSave @0x508`, `BossCharacterID: FGameplayTag @0x5f8`, `CorruptionActors @0x600`, `CorruptionGates: TArray<TSoftObjectPtr<BP_TarRootGate_C>> @0x610`; functions `IsUnlocked()`, `UnlockGate(Save: bool)`, `GetMySaveData()`, `UpdateCollisions(Unlocked)`, `CleanseTarGates()`, `HandleLoading()` | `BPSO_BossGate_C.Unlocked` |
| `…/BP_STH_Exit_Gate.BP_STH_Exit_Gate_C` | `BP_STH_Exit_C` | UNKNOWN — no `.hpp` inspected; listed for completeness | — |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/GrandElevator/BP_TarRootGate.BP_TarRootGate_C` | `AActor` | `SpartaSave @0x398`, `As BPSO_TarGate @0x470`, fn `SaveStateHandler` | `BPSO_TarGate_C` |
| `…/BP_TarRootGate_03.BP_TarRootGate_03_C` | `AActor` | `SpartaSave @0x678`, `As BPSO_TarGate @0x6d8` | `BPSO_TarGate_C` |
| `…/BP_Interactable_TarRoot_Gate.BP_Interactable_TarRoot_Gate_C` / `…_BossLock_C` | `AActor` | `SpartaSave @0x2c0` / `@0x2e0`, `As BPSO_TarGate @0x380` / `@0x398` | `BPSO_TarGate_C` |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_CloseAndOpenLeverGateByAttack.BP_CloseAndOpenLeverGateByAttack_C` | `AActor` | `SpartaSave @0x2b8`, `As BPSO_CloseAndOpenGate @0x300` | `BPSO_CloseAndOpenGate_C.CombatEncCompleted` |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_OpenByAttackGate.BP_OpenByAttackGate_C` | `AActor` | no `SpartaSave` in dump props | none |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BlockoutGate.BP_BlockoutGate_C` | `AActor` | `SpartaSave @0x2b0`, `As BPSO_Gate @0x3a0` | `BPSO_Gate_C` |

Fog/barrier volumes — **none** of these persist:

| Class | Parent | State | Persisted |
|---|---|---|---|
| `/Game/Sparta/Lighting/Blueprints/BP_FogVolume.BP_FogVolume_C` | `AActor` | `Enabled: bool @0x2c8`, `Extinction @0x2f0`, `Falloff Distance @0x2f8`; no save component | no |
| `/Game/Sparta/Core/World/FogArea/FogPauseArea.FogPauseArea_C` | `AActor` | `PauseAreaSize: double @0x2c0`; 2 overlap handlers | no |
| `/Game/Sparta/Core/Interaction/BP_BookBarrierCatalyst.BP_BookBarrierCatalyst_C` | `AActor` | — | no save component |
| `/Game/Sparta/Core/Interaction/BP_EndLevel_SwitchFogCleanUpTrigger.BP_EndLevel_SwitchFogCleanUpTrigger_C` | `AActor` | — | no save component |
| `/Game/Sparta/Dialogue/Blueprints/ShellMemories/Blueprints/BP_LocalFogVolume_Interactive.BP_LocalFogVolume_Interactive_C` | `AActor` | — | no save component |
| `/Game/Sparta/Core/World/Levels/LVA_FogBossArena.LVA_FogBossArena_C` | `BP_LevelInstanceActor_Base_C` | — | no save component |

Evidence:

```
[000001F8CB9B26B0] BlueprintGeneratedClass /Game/Sparta/Core/World/Levels/BP_BossGate.BP_BossGate_C [n: 21C428] [c: 00007FF46EEC3D38] [or: 000001F8C3849340] [sps: 00007FF46EE52028]
[000001F8C6A97FC0] BlueprintGeneratedClass /Game/Sparta/Core/World/Levels/BP_BlockedBossGate.BP_BlockedBossGate_C [n: 4065E8] [c: 00007FF46EEC3D38] [or: 000001F8C38497F0] [sps: 00007FF46EE52028]
[000001F61A2E3380] ObjectProperty /Game/Sparta/Core/World/Teleporting/BP_STH_Entrance_Gate.BP_STH_Entrance_Gate_C:SpartaSave [o: 508] [n: 5F05A7] [c: 00007FF721DE9420] [owr: 000001F617BE0310] [pc: 00007FF46F038738]
[000001F4AC3DB1A0] BlueprintGeneratedClass /Game/Sparta/Lighting/Blueprints/BP_FogVolume.BP_FogVolume_C [n: 26FEB3] [c: 00007FF46EEC3D38] [or: 000001F4ABF842F0] [sps: 00007FF46EE52028]
[000001F8D3B1BC50] BlueprintGeneratedClass /Game/Sparta/Core/World/FogArea/FogPauseArea.FogPauseArea_C [n: 162A88] [c: 00007FF46EEC3D38] [or: 000001F8C3840150] [sps: 00007FF46EE52028]
```

### 8.3 Chests, crates and pickups

| Class | Parent | Persisted state | Store |
|---|---|---|---|
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Chest.BP_Interactable_Chest_C` | `BP_InteractableBase_C` | `SpartaSave @0x308`, `SingleUse: bool @0x340`, **`TimesUsed: int32 @0x344`**, **`TimesUsedName: FName @0x348`**, `RewardDataObjects: TArray<BP_RewardData_C*> @0x350`, `RewardDataAsset @0x360`, `Interactable: bool @0x368`, `IsMemory: bool @0x369`; functions `LoadChestState()`, `SaveChestState()`, `OnChestOpen()`, `OnOpenComplete(Interrupted: bool)`, `InitializeRewardData()` | **store B** (`TimesUsedName` is the key; `InitializeRewardData` calls `ReadPlayerInt`) |
| `…/BP_Interactable_Chest_Crypt.BP_Interactable_Chest_Crypt_C` | `BP_Interactable_Chest_C` | empty body | as parent |
| `…/BP_Interactable_Chest_General.BP_Interactable_Chest_General_C` | `BP_Interactable_Chest_C` | `GiveSpecificTarstone @0x390`, `SpecificTarstone: FDataTableRowHandle @0x398`, `RewardGiven: bool @0x3b0`, **`UniqueID: FName @0x3b4`**, `PickUpItem @0x3c0`, `Amount: int32 @0x3c8`, `RewardType @0x3cc` | as parent |
| `…/BP_Interactable_Chest_Seal.BP_Interactable_Chest_Seal_C` | `_General_C` | `Seal ID: FGameplayTag @0x3d8` | as parent |
| `…/BP_Interactable_Chest_Skin.BP_Interactable_Chest_Skin_C` | `_General_C` | `SkinCategory @0x3d8`, `EquipmentID: FGameplayTag @0x3dc`, `SkinID: FGameplayTag @0x3e4`, `RequiredItem @0x3f0`, `RequiredAmount @0x3f8`; functions `LoadChestState()`, `SaveChestState()`, `SaveMyState()`, `GetMySaveData()` | as parent |
| `…/BP_LockedLightChest.BP_LockedLightChest_C` | `_General_C` | `CurrentLights: int32 @0x460`, `LightsRequired: int32 @0x464`, `Unlocked: bool @0x468`, `LightKeyRefs: TArray<TSoftObjectPtr<BP_Interaction_LightKey_C>> @0x5c0`, `As BP SO Simple Bool @0x5b8` | **`BP_SO_SimpleBool_C`** |
| `…/BP_InteractableBase_LootCrate.BP_InteractableBase_LootCrate_C` | `BP_InteractableBase_C` | only `StaticMesh` — **no save** | none |
| `…/BP_Interactable_LockedChest_PlayerEquipments.BP_Interactable_LockedChest_PlayerEquipments_C` | `BP_InteractableBase_C` | `KeyRequired @0x3a0`, `CurrentID @0x3f0`, `LoadedID @0x418`; `OpenChest()`; **no SpartaSave prop** | none visible |
| `/Game/Sparta/Environments/ItemPool/Meta/BP_FlowerChest.BP_FlowerChest_C` | `AActor` | `HitsReceived @0x338`, `RequiredHits @0x348`, `RewardExtracted: bool @0x368`, `SpartaSave @0x2c8`; fn `SaveItemCollected()` | `BPSO_FlowerChest_C.ItemCollected` |
| `…/BP_FlowerChest_Thoracium.BP_FlowerChest_Thoracium_C` | `BP_FlowerChest_C` | inherits | as parent |
| `/Game/Sparta/Environments/ItemPool/Meta/BP_UnlimitedChest.BP_UnlimitedChest_C` | `AActor` | `SpawnedAmount @0x3b8`, `MaxSpawn @0x3bc`; **no SpartaSave prop** → intentionally unlimited | none |
| `/Game/Sparta/Core/Interaction/SaveObjects/BPSO_SavedLockedChests…` | — | list of opened locked chests (`SavedChestIDs`, `HasBeenOpened`) | store A, global |
| Item pickups (`BP_CraftingItemPickup_C`, `BP_CurrencyBag_Pickup_C`, `BP_Old_Artifact_*`, `BP_Pickup_VratkoFeetPic_C`, `BP_CustomPickup_BagheadMoonshine_C`) | — | `SpartaSave` + `As BPSO_CraftingItemPickup` / `As BP SO Simple Bool` | `BPSO_CraftingItemPickup_C.ItemCollected` / `BP_SO_SimpleBool_C` |
| `BP_InteractableBase_Corpse_C` | `BP_InteractableBase_C` | `SpartaSave @0x308`; `SaveItemCollected()` | `BPSO_CraftingItemPickup_C` |

**Item-pickup detail that matters** (`BP_CraftingItemPickup_C`, from `ws-saveobj-refs.sql`):
`GetMySaveData()` returns `BPSO_CraftingItemPickup_C`; `IsCollected()` and `SetAsCollected()` both
route through `CallFunc_GetMySaveData_ReturnValue`. So "picked up" = `ItemCollected` on a per-item
save object.

Evidence:

```
[000001F8B61C98B0] BlueprintGeneratedClass /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Chest.BP_Interactable_Chest_C [n: 183836] [c: 00007FF46EEC3D38] [or: 000001F8BBEB8530] [sps: 000001F620594DE0]
[000001F8BBECB1B0] IntProperty /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Chest.BP_Interactable_Chest_C:TimesUsed [o: 344] [n: 806190] [c: 00007FF721DE91F0] [owr: 000001F8B61C98B0]
[000001F8BBECB140] NameProperty /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Chest.BP_Interactable_Chest_C:TimesUsedName [o: 348] [n: 806196] [c: 00007FF721DE90B0] [owr: 000001F8B61C98B0]
[000001F9128214E0] BlueprintGeneratedClass /Game/Sparta/Environments/ItemPool/Meta/BP_FlowerChest.BP_FlowerChest_C [n: 336FCC] [c: 00007FF46EEC3D38] [or: 000001F90DD69480] [sps: 00007FF46EE52028]
[000001F920637FC0] BlueprintGeneratedClass /Game/Sparta/Environments/ItemPool/Meta/BP_UnlimitedChest.BP_UnlimitedChest_C [n: 18DCCB] [c: 00007FF46EEC3D38] [or: 000001F9277021D0] [sps: 00007FF46EE52028]
```

The `BP_Interactable_Chest.InitializeRewardData` implementation proves the store-B read path (it
reads a player int through the helper):

```
[000001F8BBECB990] IntProperty /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Chest.BP_Interactable_Chest_C:InitializeRewardData:CallFunc_ReadPlayerInt_OutValue [o: 10] [n: 80602D] [c: 00007FF721DE91F0] [owr: 000001F8BD2E7B00]
[000001F8BD31CE00] BoolProperty /Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_Chest.BP_Interactable_Chest_C:InitializeRewardData:CallFunc_ReadPlayerInt_ReturnValue [o: 14] [n: 80603E] [c: 00007FF721DE8D80] [owr: 000001F8BD2E7B00]
```

### 8.4 Levers, switches, pressure plates, gates-by-attack

| Class | Parent | Persisted state |
|---|---|---|
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_PutLever.BP_Interactable_PutLever_C` | `BP_InteractableBase_C` | `SpartaSave @0x308`, `KeyRequired @0x318`, `LoadedRequirements @0x410`, `LockedGate: BP_InteractionLockedOpenGate_C* @0x478`; functions `PutLever()`, `TryToPutLever()`, `GetMySaveData()`, `UpdateMyBool(NewValue: bool)` |
| `…/BP_Interactable_PrayToOpenDoor.BP_Interactable_PrayToOpenDoor_C` | `BP_InteractableBase_C` | `KeyRequired @0x320`, `Door: StaticMeshActor* @0x428`, `BlockedAbilities: FGameplayTagContainer @0x478`; **no SpartaSave prop** |
| `…/BP_Interactable_PlaceBook.BP_Interactable_PlaceBook_C` | `BP_InteractableBase_C` | `SpartaSave @0x310` |
| `…/BP_Interactable_Placemushrooms.BP_Interactable_Placemushrooms_C` | `BP_InteractableBase_C` | `SpartaSave @0x308` |
| `…/BP_Interactable_PutMissingAmmo.BP_Interactable_PutMissingAmmo_C` | `BP_InteractableBase_C` | `SpartaSave @0x310` |
| `/Game/Sparta/Core/Interaction/BP_Interaction_LiftCall.BP_Interaction_LiftCall_C` | `BP_Interaction_C` | `Reusable: bool @0x3d0`, `TriggeredActor: TArray<TSoftObjectPtr<AActor>> @0x3d8`, `RequiredTargetActorState: FGameplayTagQuery @0x3e8`, `OriginLevelLever: bool @0x430`; functions `TriggerMovingActor(TopLevelLever: bool)`, `GetLiftStateBool(Top: bool&)` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_HubLiftLever.BP_HubLiftLever_C` | `BP_InteractableBase_C` | `BP_Lift: TSoftObjectPtr<BP_HubLift_C> @0x338`, `TargetFloor: int32 @0x360`, `LoadedLift: BP_HubLift_C* @0x368`; functions `CheckTargetFloor()`, `SetActivatedRot()`, `SetDeactivatedRot()`, `OnLiftStart(TargetFloor: int32)`, `OnLiftEnd()`, `BindOnLiftStart()`, `BindOnLiftEnd()` |
| `/Game/Sparta/Core/Interaction/BP_PressurePlateOpenDoor.BP_PressurePlateOpenDoor_C` (+`_Morgue`, `_Oriental`) | `AActor` | `Opened @0x361`, `RemainOpenAfterFirstInteract @0x360` — **not persisted** |
| `/Game/Sparta/Blueprints/DungeonScripts/BP_CloseAndOpenLeverGateByAttack.BP_CloseAndOpenLeverGateByAttack_C` | `AActor` | `BPSO_CloseAndOpenGate_C` |
| `/Game/Sparta/Core/Interaction/BP_Interaction_LightKey.BP_Interaction_LightKey_C` | `BP_Interaction_C` | `SpartaSave @0x3a0`; `GetSaveData()` / `SaveLight()` against `BP_SO_SimpleBool_C` |

Universal lever/shortcut note: **there is no class whose name contains `Shortcut`**
(`cls` count for `%Shortcut%` = 0), and no class whose name contains `Shrine` (0) — traversal
shortcuts in MS2 are not named as such in this build.

### 8.5 Elevators / lifts

| Class | Parent | Persisted state |
|---|---|---|
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_HubLift.BP_HubLift_C` | `AActor` | `bLiftActive @0x3a3`, `bPlayerInLift @0x3a2`, `StartingLocation @0x3a8`, `TargetLocation @0x3c0`, `TargetFloor: int32 @0x448`, spline `MovementSpline @0x308`; delegates `LiftStart @0x468`, `LiftEnd @0x478`; **no SpartaSave prop** — the lift itself does not persist its floor |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_HubLiftLever.BP_HubLiftLever_C` | `BP_InteractableBase_C` | no save prop; drives the lift |
| `/Game/Sparta/Core/Interaction/BP_Interaction_LiftCall.BP_Interaction_LiftCall_C` | `BP_Interaction_C` | no save prop; `OriginLevelLever: bool @0x430` picks top/bottom lever |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/BPI_LiftCall.BPI_LiftCall_C` | `/Script/CoreUObject.Interface` | interface: `EnableMovingActor()`, `DisableMovingActor()`, `GetLiftStateBool(bool& Top)`, `TriggerMovingActor(bool TopLevelLever)` |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BlockoutLift.BP_BlockoutLift_C` | `AActor` | no save component |
| `/Game/Sparta/Environments/Props/ManMade/Elevators/BP_Circle_Wooden_Elevator.BP_Circle_Wooden_Elevator_C` | `AActor` | `SpartaSave @0x2b8`, `As BPSO_WoodenElevator @0x468` |
| `…/BP_Circle_Cathedral_Elevator.BP_Circle_Cathedral_Elevator_C` | `BP_Circle_Wooden_Elevator_C` | inherits |
| `…/GrandElevator/BP_Grand_Elevator_02.BP_Grand_Elevator_02_C` | `AActor` | `SpartaSave @0x2c0`, `As BP SO Simple Bool @0x550` |
| `…/GrandElevator/BP_Grand_Elevator_Snow.BP_Grand_Elevator_Snow_C` | `AActor` | `SpartaSave @0x2f8`, `As BPSO_WoodenElevator @0x4b0` |
| `…/Wooden_Elevator/BP_Wooden_Elevator_Platform_01.BP_Wooden_Elevator_Platform_01_C` | `AActor` | `SpartaSave @0x2b8`, `As BPSO_WoodenElevator @0x4e0` |
| `…/Wooden_Elevator/BP_FC_Elevator.BP_FC_Elevator_C` | `AActor` | no `SpartaSave` prop in dump |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_Drawbridge.BP_Drawbridge_C` | `AActor` | `SpartaSave @0x2b8`, `IsMoving @0x370`, `IsMovingUp @0x380`, `As BPSO_MovingObject @0x388`, `StartLocation @0x308`, `EndLocation @0x2f0`, `StartRotation @0x340`, `EndRotation @0x358`; fn `TriggerMovingActor(TopLevelLever: bool)`, `GetLiftStateBool(Top: bool&)` — **the "lift" pattern is reused for drawbridges** |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_MakeBridgeByAttack.BP_MakeBridgeByAttack_C` | `AActor` | `SpartaSave @0x2c0`, `SavedBridge: bool @0x3b8`, `As BP SO Simple Bool @0x3b0`, `InitialLocation @0x380`, `InitialRotation @0x398` |
| `BPO_Wooden_Elevator_Platform_Ghost` | `BP_EnvironmentActorBase_Ghost_C` | shell-memory ghost; not world state |

Evidence:

```
[000001F8BB318A70] BlueprintGeneratedClass /Game/Sparta/Core/Common/Interactions/Blueprints/BP_HubLift.BP_HubLift_C [n: 37F9F5] [c: 00007FF46EEC3D38] [or: 000001F8BBEB3C10] [sps: 00007FF46EE52028]
[000001F8C7C94330] BlueprintGeneratedClass /Game/Sparta/Core/Interaction/BP_Interaction_LiftCall.BP_Interaction_LiftCall_C [n: 15935E] [c: 00007FF46EEC3D38] [or: 000001F8B6A26050] [sps: 000001F61A5B0A30]
[000001F9622A1870] BlueprintGeneratedClass /Game/Sparta/Environments/Props/ManMade/Elevators/BP_Circle_Wooden_Elevator.BP_Circle_Wooden_Elevator_C [n: 3673D9] [c: 00007FF46EEC3D38] [or: 000001F95E7A8A80] [sps: 00007FF46EE52028]
[000001F915D1C700] BlueprintGeneratedClass /Game/Sparta/Environments/Props/ManMade/Elevators/Wooden_Elevator/BP_Wooden_Elevator_Platform_01.BP_Wooden_Elevator_Platform_01_C [n: 2B5846] [c: 00007FF46EEC3D38] [or: 000001F91255A9C0] [sps: 00007FF46EE52028]
```

### 8.6 Breakable walls and destructibles

| Class | Parent | State | Persisted |
|---|---|---|---|
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BreakableWall.BP_BreakableWall_C` | `AActor` | `SpartaSave @0x2b0`, **`WallIsBrokenName: FName @0x2e0`**, **`WallIsBroken: bool @0x2e8`**, `SqueezeThrough: TArray<TSoftObjectPtr<BP_Interaction_Traversal_C>> @0x2c8`; functions `LockTraversals()`, `UnlockTraversals()`, `SpartaApplyHit(...)`, `BndEvt__BP_BreakableWall_SpartaSave_…OnSaveGameEvent…` | **yes — store B** |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_HiddenWall.BP_HiddenWall_C` | `AActor` | `SaveWallRevealed @0x399` | yes — `BP_SO_SimpleBool_C` |
| `/Game/Sparta/FX/Systems/Breakables/BP_VFX_Physical.BP_VFX_Physical_C` | `AActor` | `Disabled @0x3e1`, `WasTriggered @0x3ba`, `IsIntersecting @0x388`, `SpawnDisablePlane`… | **no save component** |
| `/Game/Sparta/FX/Systems/Breakables/BP_VFX_Breakable.BP_VFX_Breakable_C` | `BP_VFX_Physical_C` | `IsBroken: bool @0x518`, `Thresholds @0x519`, `KillAfterDestruction @0x570`, `OnBroken` delegate @0x578, `bIsSecondaryBreak @0x6ba`; functions `TriggerDamage(...)`, `CustomBreak(HitResult, Instigator)`, `OnBrokenEvent()`, `ConvertToBreakable()`, `SpawnDisable()` | **NOT persisted** (no `USpartaSaveComponent` anywhere in its 40+ members) |
| `BP_DestructiblePlaceholder_C` | `BP_VFX_Breakable_C` | `ManualSpawnCraftingItem @0x6d8`, `ManualPickupItem @0x6e0` | not persisted |
| `BP_DestructiblePlaceholderWall_C` | `BP_VFX_Breakable_C` | empty body (`Size: 0x6C2`) | not persisted |
| `BP_DestructiblePlaceholderCart_C`, `…Cart_OLD_C`, `…Chandelier_C`, `…FruitBasket_C`, `…Holder_C`, `…Wall_OLD_C`, `…_OLD_C`, `…ThornBoiRitual_C`, `…ThornBoiRitual_OLD1_C`, `BP_DestructibleGroundPlaceholder_C` | `BP_VFX_Breakable_C` / `AActor` | cosmetic props | not persisted |
| `/Game/Sparta/FX/Systems/Breakables/Items/BP_VFX_Breakable_Battleground_Barrier_01_C`, `…_02_C` | `BP_VFX_Breakable_C` | enemy-placed barriers | not persisted |
| `/Game/Sparta/Core/Interaction/BP_PrisonCeilingBreak.BP_PrisonCeilingBreak_C` | `AActor` | `SpartaSave @0x2b0`, `As BP SO Simple Bool @0x7e0` | yes — `BP_SO_SimpleBool_C` |

**This is the single most important negative in the report:** destroying a barrel, cart, jar,
chandelier or battleground barrier is **not** written to any save store. Only the
`Maps/LevelDesignTools/BlueprintTools/BP_BreakableWall` (a specific, hand-placed wall actor) and the
`BP_HiddenWall` / `BP_PrisonCeilingBreak` actors persist their broken-ness. A sync layer that
broadcasts every Chaos break will desync from the host's *save* (nothing to persist) even though it
matches what the host *sees*.

Evidence:

```
[000001FA153146C0] BlueprintGeneratedClass /Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BreakableWall.BP_BreakableWall_C [n: 1A4A14] [c: 00007FF46EEC3D38] [or: 000001FA152B31C0] [sps: 00007FF46EE52028]
[000001FA14F17A20] NameProperty /Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BreakableWall.BP_BreakableWall_C:WallIsBrokenName [o: 2E0] [n: 9EE01E] [c: 00007FF721DE90B0] [owr: 000001FA153146C0]
[000001F9FB5D4500] BoolProperty /Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BreakableWall.BP_BreakableWall_C:WallIsBroken [o: 2E8] [n: 9EE017] [c: 00007FF721DE8D80] [owr: 000001FA153146C0]
[000001F4CC6D6DF0] BlueprintGeneratedClass /Game/Sparta/FX/Systems/Breakables/BP_VFX_Breakable.BP_VFX_Breakable_C [n: 8652A] [c: 00007FF46EEC3D38] [or: 000001F6EF73FC90] [sps: 000001F7483F9520]
```

### 8.7 Traps

| Class | Parent | State | Persisted |
|---|---|---|---|
| `/Game/Sparta/Core/Interaction/BP_BearTrap.BP_BearTrap_C` | `BP_EnvShootingObjectBase_C` | `SpartaSave @0x438`; fn `DisableTrap()` → `K2Node_DynamicCast_AsBPSO_Bear_Trap` | **`BPSO_BearTrap_C.IsDisarmed`** |
| `/Game/Sparta/Core/Interaction/BP_Trap.BP_Trap_C` | `AActor` | no save component in dump | none |
| `/Game/Sparta/Core/Interaction/BP_MakeRockTrapPlayer.BP_MakeRockTrapPlayer_C` | `AActor` | no save component | none |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_PlayerTrappedOnOverlap.BP_PlayerTrappedOnOverlap_C` | `AActor` | `SpartaSave @0x2b0` | store A |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_SpikeTrap.BP_SpikeTrap_C` | `AActor` | no save component | none |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_MushroomTrap.BP_MushroomTrap_C` | `BP_InteractableBase_C` | no `SpartaSave` prop | none |
| `BPSO_PlayerTrapDungeon_C` | — | `TrappedOnce: bool @0xb8` | store A (global) |

### 8.8 Checkpoints, bonfires, rest points, beacons (the respawn/fast-travel nodes)

MS2's "bonfire" is a **Bone Beacon**; the classes are `SpartaPlayerStart` derivatives plus the
`BPO_STH_*` teleport handlers. `BP_Bonfire_01` is only a light fixture — see §11.

| Class | Parent | State / API | Persisted |
|---|---|---|---|
| `/Script/Sparta.SpartaPlayerStart` | `/Script/Engine.PlayerStart` | `StreamingSourceComponent: UWorldPartitionStreamingSourceComponent* @0x2d8`, `RootCapsuleMobility @0x2e0`, **`AreaID: FGameplayTag @0x2e4`**, `ZoneData: USpartaZoneData* @0x2f0`; functions `GetIsUnlocked()`, `GetDesiredPlayerStartRotation()`, `GetDesiredPlayerStartLocation()`, `GetAreaName()`, `EligibleForFastTravel()`, `CanFastTravel()` | unlock state lives in `BP_SpartaPlayerSaveObject.UnlockedLandingAreas` / `CleansedLandingAreas` |
| `/Game/Sparta/Core/World/BP_SpartaPlayerStart.BP_SpartaPlayerStart_C` | `ASpartaPlayerStart` | adds `AreaName: FText @0x2f8`, overrides `GetAreaName()` | as above |
| `/Game/Sparta/Core/World/Launch/BP_LandingAreaBase.BP_LandingAreaBase_C` | `ASpartaPlayerStart` | huge component set (beacon mesh/VFX, dive-down spline, 2 `BPC_InteractionHandler_C` for the travel menu, `BPC_WorldMapObjective_C` @0x390), delegate `OnEnableStateChanged @0x538` | as above |
| `…/BP_LandingArea_Checkpoint.BP_LandingArea_Checkpoint_C` | `BP_LandingAreaBase_C` | `bVisibleOnMap: bool @0x688`; functions `EligibleForFastTravel()`, `CanFastTravel()`, `HandleMapObjective()`, `UpdateState()`, `HandleBeaconRevitalize()`, `UpdateVisualsBeginPlay(LightBeacon, Cleanse)`, `HandleInteract()`, `ClenseAdditionalCorruption()` | `BPSO_WordMapObjective_C` + `BP_SpartaPlayerSaveObject` |
| `…/BP_LandingArea_Boss.BP_LandingArea_Boss_C` | `BP_LandingAreaBase_C` | `SpartaSave @0x688`, **`BeaconSaveObject: BPSO_LandingArea_Boss_C* @0x6f8`** | `BPSO_LandingArea_Boss_C.BeaconRisen` |
| `…/BP_LandingArea_C` , `…Teleport`, `…Selector`, `…HUB`, `…FinalLevel` | — | 2/13/6/1/7 live instances; not individually inspected | UNKNOWN |
| `/Game/Sparta/Blueprints/BP_PrologueCheckpoint.BP_PrologueCheckpoint_C` | `ASpartaPlayerStart` | `OverlapEnabled @0x318`, `OverlapQuery: FGameplayTagQuery @0x320`, `ApplicationGlobalEvent: TSubclassOf<UGlobalEvent> @0x368`, `ApplicationEventTag: FGameplayTag @0x380`, `AreaName: FText @0x388`; no save component — **it writes `BP_SpartaPlayerSaveObject.CurrentPrologueCheckpoint`** (that field exists at `0x4c0`; the writer is UNKNOWN in the dump) | indirectly |
| `/Game/Sparta/Core/World/Teleporting/BPO_STH.BPO_STH_C` | `USpartaInstantiatedObject`→`SpartaInstancedObject` | `MyContext: UObject* @0x28`, delegates `OnEnterComplete @0x30`, `OnLeaveComplete @0x40`; functions `GetTeleportControlRotation()`, `GetTeleportRotation()`, `GetTeleportLocation()` | — (handler, not state) |
| `…/BPO_STH_Beacon.BPO_STH_Beacon_C` | `BPO_STH_C` | `bGateActivated: bool @0x110`, `LinkedBossGate: TSoftObjectPtr<BP_STH_Entrance_Gate_C> @0xa8`, `ReceivedLeaveTag: FGameplayTag @0x140`, `MyDungeonLeaveTag: FGameplayTag @0x158`, `DestinationZoneData @0x148`; functions `TryUnlockGate(Save: bool)`, `CleanseBeacon()`, `InitBossGate()`, `IsCompletedLeave()`, `CommitTeleport (Enter)(Success: bool&)` | gate unlock → `BPSO_BossGate_C.Unlocked` |
| `…/BPO_STH_Generic.BPO_STH_Generic_C` | `BPO_STH_C` | `EnterSpawnLocation @0x58`, `LeaveSpawnLocation @0x80`, `DungeonLeaveTag: FGameplayTag @0xb0`, `LinkedDungeon: TSoftObjectPtr<BP_LevelInstanceSelector_C> @0xf8` | — |
| `/Game/Sparta/Core/Interaction/BP_ShatteredBeacon.BP_ShatteredBeacon_C` | `BP_Interaction_C` | `TeleportHandler: BPO_STH_Generic_C* @0x430`, `BPC_WorldMapObjective @0x3a0`; functions `SetDeactivated()`, `InitializeMapObjective()`, `ProximaTeleport()` | `BPSO_WordMapObjective_C` |
| `/Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector_RestSite.BP_LevelInstanceSelector_RestSite_C` | `BP_LevelInstanceSelector_C` | the only class in the build with "RestSite" in the name; inherits `SpartaSave` @0x2e0 from its parent (§ below) | store A |
| `/Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector.BP_LevelInstanceSelector_C` | `AActor` | `SpartaSave @0x2e0`, `PreviewLevelInstance @0x350`, `LoadedLevel: LevelStreamingDynamic* @0x358`, `Level: TSoftObjectPtr<World> @0x360`, `ShouldReloadLevel @0x388`, `LayoutName: FText @0x398`; functions `SelectLevel()`, `LoadLevel()`, `UnloadLevel()`, `GetEventTransform()`, `ShowPreviewTile()` | store A |
| `/Game/Sparta/Core/World/Levels/BP_DungeonEventSelector.BP_DungeonEventSelector_C` | `AActor` | `SpartaSave @0x2f8`, `PreviewEvent: USpartaEventDataAsset* @0x318`, `Event @0x320`, `EventsOverride @0x328`, `EventType @0x338`, delegate `OnLevelInstanceLoaded @0x340` | store A |

**There is no `Checkpoint` native class and no `Bonfire` gameplay class.** The two class names that
contain `Checkpoint` are `BP_PrologueCheckpoint_C` and `BP_LandingArea_Checkpoint_C`; `cls` count
for `%Checkpoint%` = 2, and for `%Bonfire%` = 4 — all four are props/VFX
(`BP_Bonfire_01_C`, `BP_Bonfire_Soup_01b_C`, `BP_Bonfire_Soup_01b_OLD_C`,
`BP_Bonfire_PM3_S1_EnvActor_C`).

### 8.9 Ladders, traversal, shortcuts

`cls` count for `%Ladder%` = 4, all `PLA_Wooden_Ladder_*` `PackedLevelActor`s (pure art, no logic).
Climbing/vaulting/squeezing is `SpartaTraversalActor`:

| Class | Parent | State | Persisted |
|---|---|---|---|
| `/Script/Sparta.SpartaTraversalActor` | `/Script/Sparta.SpartaTraversalNode` → `AActor` | functions `UnlockTraversal(bUsed: bool)`, `LockTraversal()`; **zero properties, no save component** | **no** |
| `/Script/Sparta.SpartaTraversalNode` | `AActor` | `GetConnectedTraversalNodes()`, `BaseDeactivateDestination()`, `BaseActivateDestination()` | no |
| `/Game/Sparta/Core/Common/Interactions/BP_Interaction_Traversal.BP_Interaction_Traversal_C` | `BP_Interaction_C` | `Reusable: bool @0x3ab`, `TraversalAbility: TSubclassOf<UGameplayAbility> @0x3b0`, `LinkedTraversals @0x3c0`, `IsDisabledDueToDamage @0x3d8`, `ShouldWaitForCombat`… | **no save component** |
| `…/BP_Interaction_Traversal_SqueezeThrough_C`, `…_Vault_C`, `…_JumpToLedgeTarpad_C`, `…_SqueezeThrough_NoWeaponStow_C` | `BP_Interaction_Traversal_C` | — | no |
| `/Game/Sparta/Core/Common/Interactions/BP_SpartaTraversalActor_C` | `ASpartaTraversalActor` | — | no |

`BP_BreakableWall` is the **only** thing that gates traversals across a save: it holds
`SqueezeThrough: TArray<TSoftObjectPtr<BP_Interaction_Traversal_C>>` and calls
`LockTraversals()` / `UnlockTraversals()` (§8.6), so reopening a save with a broken wall unlocks the
squeeze-through traversal.

Evidence:

```
[000001F8CB635500] BlueprintGeneratedClass /Game/Sparta/Core/Interaction/BP_PressurePlateOpenDoor.BP_PressurePlateOpenDoor_C [n: 3F358D] [c: 00007FF46EEC3D38] [or: 000001F8CAED3850] [sps: 00007FF46EE52028]
[000001F8C6A92DD0] BlueprintGeneratedClass /Game/Sparta/Core/World/Launch/BP_LandingArea_Checkpoint.BP_LandingArea_Checkpoint_C [n: 2E89E9] [c: 00007FF46EEC3D38] [or: 000001F8C3845EC0] [sps: 000001F617BEE710]
[000001F8C6A97180] BlueprintGeneratedClass /Game/Sparta/Core/World/Launch/BP_LandingArea_Boss.BP_LandingArea_Boss_C [n: 115199] [c: 00007FF46EEC3D38] [or: 000001F8C3842720] [sps: 000001F617BEE710]
[000001F74146BFE0] BlueprintGeneratedClass /Game/Sparta/Blueprints/BP_PrologueCheckpoint.BP_PrologueCheckpoint_C [n: 316698] [c: 00007FF46EEC3D38] [or: 000001F7411E63C0] [sps: 00007FF46F055D38]
[000001F8D3B1DC60] BlueprintGeneratedClass /Game/Sparta/Core/World/BP_SpartaPlayerStart.BP_SpartaPlayerStart_C [n: 1ED3E9] [c: 00007FF46EEC3D38] [or: 000001F8C3840560] [sps: 00007FF46F055D38]
```

### 8.10 Store C — DataLayers as world state

| Class | Parent | State |
|---|---|---|
| `/Script/Sparta.DataLayersSave` | `SpartaSaveObject` | `SavedDataLayerStates: TMap<FName, EDataLayerRuntimeState> @0xb8` |
| `/Script/Sparta.SpartaDataLayerSubsystem` | `/Script/Engine.WorldSubsystem` | `RuntimeData: DataLayersSave* @0x38`; `SaveDataLayerState(DataLayer: DataLayerAsset*, TargetState: EDataLayerRuntimeState)`, `RemoveDataLayerState(DataLayer: DataLayerAsset*)` |
| `BPSO_BossDLManager_C` | `SpartaSaveObject` | `CurrentBoss @0xb8`, `RelevantDL: DataLayerAsset* @0xc0` |
| `BPSO_CinematicDLUpdate_C` | `SpartaSaveObject` | `CinematicFired @0xb8` |
| `/Game/Sparta/Core/Common/Interactions/Blueprints/BP_ChangeSpatiallyLoadedStateOnActors.BP_ChangeSpatiallyLoadedStateOnActors_C` | `AActor` | exists (name only); **no `SpartaSaveComponent`** in the 134-row list |

In-game there are many live `/Engine/Transient.DataLayersSave_*` objects (15+ in the dump), so the
subsystem keeps per-world instances, not a single global one.

Evidence:

```
[000001F49CD2DDA0] MapProperty /Script/Sparta.DataLayersSave:SavedDataLayerStates [o: B8] [n: 587BA2] [c: 00007FF721DE8F70] [owr: 00007FF46F045338] [kp: 000001F4ACD689E0] [vp: 000001F4ACDA3300]
[00007FF46F045338] Class /Script/Sparta.DataLayersSave [n: 48F97] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038338]
NameProperty SavedDataLayerStates./Script/Sparta.DataLayersSave:SavedDataLayerStates_Key
EnumProperty SavedDataLayerStates./Script/Sparta.DataLayersSave:SavedDataLayerStates of /Script/Engine.EDataLayerRuntimeState
```

---

## 9. GameplayTags used for world state

### 9.1 What the dump can and cannot prove

**Can prove:** that a property/parameter is a tag (`StructProperty` whose `ref` resolves to
`/Script/GameplayTags.GameplayTag`), a tag container (`GameplayTagContainer`), or a tag query
(`GameplayTagQuery`); and that a struct field is a tag (`FPossibleWorldState.WorldState`).

**Cannot prove: the tag string values.** I verified there are no tag literals in any dump:

| Attempt | Result |
|---|---|
| `SELECT COUNT(*) FROM obj WHERE type='GameplayTag'` on `raw\index.sqlite3` | `0` — the build contains **no** `GameplayTag` UObjects |
| `SELECT type,path FROM obj WHERE type IN ('GameplayTagsList',…)` | only `/Engine/Transient.GameplayTagsManager_2147482638:Native`, `…:PluginTags.ini`, `/Script/GameplayTags.Default__GameplayTagsList`, `/Script/GameplayTags.Default__GameplayTagsSettings`, `/Script/GameplayTags.Default__RestrictedGameplayTagsList` — the *managers*, whose tag arrays are not dumped |
| ripgrep over `raw\ObjectDump_allassets.txt` for `GameplayCue\.[A-Za-z]+\.[A-Za-z]+` | no matches |
| ripgrep over all 10 415 `CXXHeaderDump\*.hpp` for `\.[A-Z][A-Za-z0-9]+\.[A-Z][A-Za-z0-9]+` | no matches |
| `Select-String` over `raw\ObjectDump_allassets.txt` for `(Character|World|Door|Gate|Save|State)\.[A-Za-z]+(\.[A-Za-z]+)+` | no matches |

So: **every tag name in this build is UNKNOWN from the dump.** Property names, types and the
owner/key structure are provable; the strings are not.

### 9.2 The tag-typed slots that carry world state (provable)

| Owner | Property / parameter | Tag type | Offset |
|---|---|---|---|
| `USpartaSaveObject.UpdateFrom` | `Events` | `GameplayTagContainer` | `0x8` (param) |
| `USpartaSaveSubSystem.SaveToDisk` | `SaveEvents` | `GameplayTagContainer` | `0x0` (param) |
| `USpartaSaveSubSystem.Save` path via `SpartaSaveComponent.Save` | `Events` | `GameplayTagContainer` | `0x0` (param) |
| `FSpartaSaveContext` | `EventTags` | `GameplayTagContainer` | `0x08` (struct field, from `Sparta.hpp`) |
| `USpartaWorldStateHelper` | `Owner`, `Key` params of `WriteIntByTags` / `WriteFloatByTags` / `WriteBoolByTags` / `WriteNameByTags` and the `Read*ByTags` twins | `GameplayTag` | params — see §1.2 |
| `USpartaWorldStateHelper` | `ValueName` params of every `*ByTag` variant | `GameplayTag` | params |
| `FPossibleWorldState` | `WorldState` | `GameplayTag` | `0x00` |
| `UBPSO_GrishaCage_C` | `OpenType` | `GameplayTag` | `0xbc` |
| `ABP_Interactable_Chest_Skin_C` | `EquipmentID`, `SkinID` | `GameplayTag` | `0x3dc`, `0x3e4` |
| `ABP_Interactable_Chest_Seal_C` | `Seal ID` | `GameplayTag` | `0x3d8` |
| `ABP_STH_Entrance_Gate_C` | `BossCharacterID` | `GameplayTag` | `0x5f8` |
| `ABP_PrologueCheckpoint_C` | `ApplicationEventTag` | `GameplayTag` | `0x380` |
| `ABP_PrologueCheckpoint_C` | `OverlapQuery` | `GameplayTagQuery` | `0x320` |
| `ABP_Interaction_LiftCall_C` | `RequiredTargetActorState` | `GameplayTagQuery` | `0x3e8` |
| `ABP_Interaction_C` (base) | `TagQuery` | `GameplayTagQuery` | `0x2e8` |
| `ABP_Spawner_C` | `TerritoryTag`, `TeamToSetOnSpawn` | `GameplayTag` | `0x4a0`, `0x4e4` |
| `ABP_SPartaPlayerStart_C` (native `ASpartaPlayerStart.AreaID`) | `AreaID` | `GameplayTag` | `0x2e4` |
| `BPO_STH_Beacon_C` | `ReceivedLeaveTag`, `MyDungeonLeaveTag` | `GameplayTag` | `0x140`, `0x158` |
| `BPO_STH_Generic_C` | `DungeonLeaveTag` | `GameplayTag` | `0xb0` |
| `UBPSO_QuestLogComponent_C` | `NarrativeTags` | `GameplayTagContainer` | `0xc0` |
| `UBPSO_ShellMemories_C` | `MemoryProgress` keys | `GameplayTag` | map key (`0x0` sub-object) |
| `UBPSO_Progression_C` | `AssignedPoints`, `BondingPoints` keys | `GameplayTag` | map keys |
| `UBPSO_NarrativeEventsTracker_C` | `NarrativeEventTimeTracking` keys | `GameplayTag` | map key |
| `UBP_SpartaPlayerSaveObject_C` | `LostGloomBiome`, `CinematicsWatched` | `GameplayTag`, `GameplayTagContainer` | `0x598`, `0x5b0` |
| `UPlayerSaveObject` (native) | `OverrideWorldGenTags` | `GameplayTagContainer` | `0x2b0` |
| `UPlayerSaveObject` (native) | `ShellStats`, `WeaponStats`, `PlayerStats` keys | `GameplayTag` | map keys / `FPermanentStats` |
### 9.3 Conclusion for the sync layer

A sync layer **cannot** copy world state by tag name from static analysis; it must copy the
`FGameplayTag` values *at runtime* out of `USpartaSaveGame.Actor*` maps and the
`SpartaWorldStateHelper.*ByTags` `Owner`/`Key` pairs (they are `FGameplayTag` structs at runtime, so
`GameplayTag.ToString()` via a UE4SS Lua `FGameplayTag` helper would work). That is a runtime-read
fact, not a design recommendation.

---

## 10. What a sync layer must read and write (facts only)

### 10.1 Read

| # | Read | Exact API / property |
|---|---|---|
| 1 | is the world state loaded | `SpartaSaveSubSystem.GetIsSaveLoaded() -> BoolProperty`; `HasValidSaveForActiveProfileSlot() -> Bool` |
| 2 | the whole persisted container | `SpartaSaveSubSystem.GetCurrentWorldStateSave() -> SpartaSaveGame*` |
| 3 | the changed set | `SpartaSaveGame.ChangedActors` (`TSet<FName>` @`0x28`) |
| 4 | per-actor named values | `SpartaSaveGame.ActorBools/ActorInts/ActorFloats/ActorNames` @`0x168`/`0xc8`/`0x78`/`0x118`, each `TMap<FName, FSpartaMapX>` where the inner `TMap<FName,X>` is the key/value pair |
| 5 | per-actor object references | `SpartaSaveGame.ActorObjectMap` @`0x1b8` (`TMap<FName,UObject*>`) |
| 6 | a live actor's save object | `SpartaSaveComponent.GetRuntimeData() -> SpartaSaveObject*`; `GetGUID() -> Guid`; `IsSaveLoaded() -> Bool` |
| 7 | the concrete save-object class of an actor | `SpartaSaveComponent.SaveObjectClass` (`ClassProperty` @`0xc0`) — read at runtime from the component, not from the BP CDO |
| 8 | a save object out of an on-disk slot | `SpartaSaveSubSystem.GetSaveObjectByClassFromSlotIndex(SlotIndex: Int, SaveObjectClass: Class) -> SpartaSaveObject*` (and `…FromSlotName(SlotName: Str, …)`) |
| 9 | the session state | `SpartaSaveSubSystem.GetSessionSave() -> SpartaSaveObject*`; `SessionSaveObject` @`0x120`; `RuntimeSessionSaveObject` @`0x128` |
| 10 | DataLayer runtime state | `SpartaDataLayerSubsystem.RuntimeData -> DataLayersSave*`, then `DataLayersSave.SavedDataLayerStates` @`0xb8` |
| 11 | named world values without touching the maps | `SpartaWorldStateHelper.ReadMyBool/ReadMyInt/ReadMyFloat/ReadMyName(WorldContextObject, ValueName, OutValue, ReturnValue)`; `…ByTag` variants; `ReadASaveObject(WorldContextObject, OwnerName)` |
| 12 | an actor's save object by class | `SpartaWorldStateHelper.GetSpartaActorSaveObject(WorldContextObject: Object, SaveClass: Class) -> Object` |
| 13 | "did this actor's state change" | `SpartaWorldStateHelper.AmIChanged(WorldContextObject) -> Bool`, `AreTheyChanged(They) -> Bool`, `IsInChangedSet(WorldContextObject, ActorName) -> Bool` |
| 14 | change notifications | component `OnPreSave` (`FSpartaSaveContext`) and `OnSaveLoaded` (`FSpartaSaveContext`); subsystem `OnSaveGameLoaded` (`FSpartaSaveContext`), `OnSaveStarted`, `OnSaveFinished`, `PostSessionReset`, `OnSaveSystemInitialized` |

### 10.2 Write

| # | Write | Exact API / property |
|---|---|---|
| 1 | change one named value | `WriteMyBool/…Int/…Float/…Name(WorldContextObject, ValueName, Value[, bModify])`; `WriteTheirXxx(They, …)`; `WritePlayerXxx(WorldContextObject, …)`; `WriteXxxByTag(…, ValueName: GameplayTag, …)`. `bModify: BoolProperty` selects "set" vs "add" for the numeric variants |
| 2 | two-level (owner+key) write | `WriteIntByTags/WriteFloatByTags/WriteBoolByTags/WriteNameByTags(WorldContextObject, Owner: GameplayTag, Key: GameplayTag, Value, bModify)` |
| 3 | assign an arbitrary actor name | `WriteAnInt/WriteAFloat/WriteABool/WriteAName/WriteAnIntByTag/…(WorldContextObject, OwnerName: FName, ValueName, Value, bModify)` |
| 4 | write a save-object reference | `WriteMySaveObject(WorldContextObject, Value)` / `WriteTheirSaveObject(They, Value)` / `WriteASaveObject(WorldContextObject, OwnerName, Value)` / `WritePlayerSaveObject(WorldContextObject, Value)` |
| 5 | mark an actor dirty | `AddMeToChangedSet(WorldContextObject)`, `AddThemToChangedSet(They)`, `AddToChangedSet(WorldContextObject, ActorName)`; undo with the matching `Remove*` |
| 6 | set fields on a save object | write the `BPSO_*` properties directly (see §5.1); the save objects carry no setters |
| 7 | push a save to disk | `SpartaSaveSubSystem.SaveToDisk(SaveEvents: GameplayTagContainer, bSaveImmediately: Bool, bSaveAsync: Bool)`; `SaveSession()`; `SaveMetaData()`; or `SpartaSaveComponent.Save(Events: GameplayTagContainer)` for one actor |
| 8 | gate saving | `SpartaSaveSubSystem.SetPreventFurtherSaves(bPreventSaving: Bool)`; `DisableAutoSave()` / `EnableAutoSave()`; `ResetAutoSaveTimer()` |
| 9 | reset one actor's persisted state | `SpartaSaveSubSystem.ResetSaveObjectByGuid(GuidString: Str) -> Bool`; `ResetSaveObjectByClass(SaveObjectClass: Class) -> Int`; `ResetAllSaveData()` |
| 10 | reset a component | `SpartaSaveComponent.ResetSaveData()`; `GenerateNewGuid()` |
| 11 | write a DataLayer state | `SpartaDataLayerSubsystem.SaveDataLayerState(DataLayer: DataLayerAsset*, TargetState: EDataLayerRuntimeState)` / `RemoveDataLayerState(DataLayer)` |
| 12 | Blueprint-side hook points | override `USpartaSaveObject.ReceiveSpartaPreSave(SaveContext)` (gather) and `ReceiveOnLoaded()` (apply) on a save-object subclass; bind `SpartaSaveComponent.OnPreSave` / `OnSaveLoaded` |
| 13 | a ready-made Blueprint bridge for one actor | `BP_PlayerController.S_ResetSaveObjectByGuid(GuidString: StrProperty)` — resolves the subsystem via `GetGameInstanceSubsystem` and calls `ResetSaveObjectByGuid` (dump-proven), followed by `BP_PlayerController`'s own `OnSaveLoaded/PostPlayerSaveLoaded` reload path |

### 10.3 Where the sync layer cannot go (proven gaps)

| Gap | Evidence |
|---|---|
| no per-actor "unlock" API on `SpartaSaveComponent` other than `Save` / `ResetSaveData` / `GenerateNewGuid` | the component's complete function list is 6 entries (§1.1) |
| the changed set is not exposed as a delegate — only `OnSaveStarted` / `OnSaveFinished` on the subsystem | §4 |
| no `Register`/`Store`/`Set` function exists on any of the save classes (searched every function name on the 4 native save classes + all 51 BP save objects; `scripts\q\ws-saveobj-funcnames.sql` = 20 rows total) | §5.2 |
| `SpartaSaveGame` has no functions at all | `parse_dump.py fun` on it prints nothing |

---

## 11. What I deliberately did NOT document, and why

Candidate counts are `cls` rows matching the name keyword, excluding `/Script/%`
(`scripts\q\ws-counts.sql`).

| Keyword | Classes | What I did with them / why skipped |
|---|---|---|
| `Shortcut` | **0** | nothing exists — no shortcuts in this build under that name |
| `Shrine` | **0** | nothing exists |
| `WorldState` (BP classes) | **0** | the only `WorldState`-named things are native (`SpartaWorldStateHelper`, `WorldState` is a runtime *object name*) |
| `Persistent` | **0** | — |
| `Saveable` | **0** | — |
| `Bonfire` | 4 | skipped as cosmetic: `BP_Bonfire_01_C` (parent `BP_LightFixture_C`), `BP_Bonfire_Soup_01b_C` + `_OLD_C` (parent `BP_VFX_Breakable_Light_C` / `BP_LightFixture_C`), `BP_Bonfire_PM3_S1_EnvActor_C` (shell-memory ghost prop) — **no bonfire/checkpoint gameplay class** |
| `CameraShake_*` / `CameraState_*` | 8 in the door/gate/lift set alone | per instructions; pure presentation (`CameraShake_HubLift_C`, `CameraShake_OpenDoor_C`, `CameraShake_Player_HubDoorClose_C`, `CameraShake_TombZone_BossGates_C`, `CameraState_BoneGateFar_C`, `CameraState_GateCleansed_C`, `CameraState_HubElevator_C`, `CameraState_TombZone_BossGates_C`) |
| `WBP_*` / `WidgetBlueprintGeneratedClass` | 658 in the build, 3 matched my door/gate keywords | UI — the menus agent owns it: `WBP_SaveGameEntry_C`, `WBP_Debug_BeaconsAndGates_C`, `WBP_LTWI_Gates_C`. (`WBP_Debug_MapTracking_C` is *cited* in §6 as a save-object reader, not documented as a class.) |
| `PLA_*` (`PackedLevelActor`) | 1281 in the build, ~40 matched | pure art assembly, no logic and no save component anywhere: `PLA_Castle_*_Gate_*`, `PLA_Mine_Gate_*`, `PLA_Quarry_elevator_*`, `PLA_Wooden_Ladder_*`, `PLA_Battleground_Barrier_*`, `PLA_Stone_Village_Door_01`, `PLA_Door_old_01`, `PLA_HUB_Main_Gate`, `PLA_Gate_Roots_01`, … |
| `GE_*` / `DEM_*` (gameplay effects / execution modifiers) | 673 in the build, 3 matched | combat effects that matched on `NegateDamage` / `TimedTrapAvoidance`, not world state: `DEM_NegateDamage_C`, `GE_NegateDamage_C`, `GE_TimedTrapAvoidance_C` |
| `GA_*` (gameplay abilities) | 1560 in the build, 4 matched | player/enemy abilities — the player-actions agent owns them: `GA_Player_OpenDoor_C`, `GA_Player_KnockOnDoor_2Knocks_C`, `GA_Traversal_BoneGate_Far_C`, `GA_Traversal_EnterBeacon_C` |
| `GlobalEvent_*` (`/Game/Sparta/Core/GES/*`) | 348 in the build, 14 matched | *runtime* event objects (`/Script/GlobalEventSubsystem.GlobalEvent` subclasses) fired by the world, never persisted: `GlobalEvent_CageDoor_KeyPurchased_C`, `GlobalEvent_CageDoor_Locked_C`, `GlobalEvent_CageDoor_Open_C`, `GlobalEvent_CageDoor_OpenMemory_C`, `GlobalEvent_GateTutorial_C`, `GlobalEvent_HubDoorGrandOpening_C`, `GlobalEvent_TombZone_OpenedGeneralChest_C`, `GlobalEvent_FogEvent_Pause_C`, `GlobalEvent_FogEvent_Unpause_C`, `GlobalEvent_FogManagerDebug_C`, `GlobalEvent_Investigate_Activate_C`, `GlobalEvent_Investigate_End_C`, `GlobalEvent_BP_Interactable_Investigate_C`, `BP_GlobalEvent_EM5_S1_DoorOpen_C` |
| `LVA_*` (`BP_LevelInstanceActor_Base_C`) | 40 in the build, ~30 matched | level-instance containers; the four that carry state are documented as *selectors* in §8.8, the rest are `LVA_Forest_*` dungeon tiles |
| `Pickup` | 285 | the items agent's area; I documented only the *persistence* shape (`BP_CraftingItemPickup` / `As BPSO_CraftingItemPickup` / `BP_SO_SimpleBool`, §8.3) |
| `Interact` | 722 | far too many to enumerate; I documented the two base families (§8) and every interactable that appears in the save-binding tables (§6/§7). Classes reachable only through `Interact` and with no save component were skipped |
| `Rest` | 66 | all `Restore*`/`Rest*` matches on health-restore effects, `CrestOfBrotherhood`, `Restrain`, `PointOfInterest`, `ChestTomb`, `Forest` — only `BP_LevelInstanceSelector_RestSite_C` is relevant and it is in §8.8 |
| `Switch` | 30 | all ability/target-switch/AI-aggro classes (`GA_SwitchLockOnAttachedSocket_Handler_C`, `GA_AI_AngrySwitchListener_*`, `GE_BlockTargetSwitching_C`, `BP_BLocker_Switch_C`, …) — **no world switch actor** |
| `Lift` | 7 | all 7 accounted for: `BP_HubLift_C` + `BP_HubLiftLever_C` + `BP_Interaction_LiftCall_C` + `BPI_LiftCall_C` + `BP_BlockoutLift_C` (§8.5), `CameraShake_HubLift_C` (presentation), `PLA_HUB_Stairs_Lift_Ring_WHITE_C` (art) |
| `Elevator` | 33 | every elevator that carries save state is in §8.5; the remainder are `PLA_*` art assemblies (`PLA_Quarry_elevator_*`, `PLA_Const_Elevator_*`, `PLA_Castle_Elevator_01`, `PLA_Wooden_Elevator_*`, `PLA_Circle_Elevator_Shaft_01`, `PLA_LI_Castle_Tower_Elevator_01`, `PLA_Elevator_Coffin_Island_a_01`, `PLA_Const_Broken_Elevator_DRY`) and camera states (`CameraState_HubElevator_C`) |
| `Loot` | 1 | `BP_InteractableBase_LootCrate_C` (§8.3) |
| `Gate` | 57 | every gate with save state is in §8.2; the remainder are camera states/shakes (`CameraShake_TombZone_BossGates_C`, `CameraState_BoneGateFar_C`, `CameraState_GateCleansed_C`, `CameraState_TombZone_BossGates_C`), `GlobalEvent_*` (`GlobalEvent_CageDoor_*`, `GlobalEvent_GateTutorial_C`, …), `WP_Brigand_Shield_Door_C` (a shield mesh whose name contains "Door"), `BP_GateHiddenObjects_C`, and `PLA_*` art |
| `Door` | 54 | every door with save state is in §8.1; the remainder are `PLA_*` art (`PLA_Castle_*_Door_*`, `PLA_Mine_Gate_*`, `PLA_Stone_Village_Door_01`, `PLA_Door_old_01`, `PLA_Castle_Wooden_Wall_Gate_Door_01`), `CameraShake_*`, the `GA_Player_OpenDoor` / `GA_Player_KnockOnDoor_2Knocks` abilities (player-actions agent), and shell-memory ghost doors (`BP_MotherRoom_Doors_TheVisit_EnvActor_C`, `BP_FakeDoor_TheVisit_Interaction_C`, `BP_HutDoor_TheRash_TM1_S1_C`, `BP_CastleGate_Doors_TheVisit_EnvActor_C`, `BP_EredrimTheBattle_Door_Interaction_Ghost_C`, `BP_Grisha_Cage_01_Door_C`, `BP_Grisha_Cage_01_Door_Memory_C`) |
| `Chest` | 24 | 12 covered: the 7 chest/crate classes + `BP_FlowerChest_C` + `BP_FlowerChest_Thoracium_C` + `BP_UnlimitedChest_C` (§8.3) + `BPSO_SavedLockedChests_C` + `BPSO_FlowerChest_C` (§5.1). The other 12 are `GlobalEvent_TombZone_OpenedGeneralChest_C`, `GC_Dismemberment_Chest_C` (a VFX cue), the 4 `LVA_Forest_ChestTomb*` level instances, `PLA_Brigand_Camp_Chest_01_C`, `BP_BrigandCamp_Set_Chest_01_C` / `_SNOW_C` / `_02_C` (StaticMeshActors), `BP_VFX_Chest_03_C` (a bumpable VFX), `ID_Dungeon_LostEquipmentChest_C` (an item — items agent) |
| `Destructible` | 12 | all 12 listed in §8.6 |
| `Breakable` | 92 | the `BP_VFX_Breakable_*` family is the bulk (only 2 of them are named as separate `Barrier` items in §8.6); §8.6 documents the base classes, the two wall/reveal actors that DO persist, and states the negative for the rest — I did not enumerate all 92 because they are all cosmetic variants of the same base with no save state |
| `Checkpoint` | 2 | both covered (§8.8) |
| `Barrier` | 5 | 3 covered: `BP_BookBarrierCatalyst_C` (§8.2), `BP_VFX_Breakable_Battleground_Barrier_01_C` / `_02_C` (§8.6); the other 2 are `PLA_Battleground_Barrier_01_C` / `_02_C` art |
| `Fog` | 15 | 5 covered: `BP_FogVolume_C`, `FogPauseArea_C`, `BP_EndLevel_SwitchFogCleanUpTrigger_C`, `BP_LocalFogVolume_Interactive_C`, `LVA_FogBossArena_C` (§8.2). The other 10: `GC_PoisonFog_C` (gameplay cue), `BP_VFX_EnvFOG_C` (VFX), `BP_Spawner_Fog_VampireFromTheSky_C` / `_Single_C` (spawners — enemies agent), `GlobalEvent_FogEvent_Pause_C` / `_Unpause_C` / `GlobalEvent_FogManagerDebug_C` (event objects), `BP_CinematicPlayer_C`, `BP_DeathManager_C` (fog-area managers), `BP_EasyFog_CPD_C` (a third-party fog blueprint) |
| `Trap` | 9 | all 9 covered: 7 in §8.7 + `BPSO_BearTrap_C` / `BPSO_PlayerTrapDungeon_C` in §5.1 |
| `Lever` | 6 | the 3 world levers are in §8.4 (`BP_CloseAndOpenLeverGateByAttack_C`, `BP_HubLiftLever_C`, `BP_Interactable_PutLever_C`); the other 3 are item definitions `ID_MissingLever_C`, `ID_MissingLever_DeepRockKingdom_C`, `ID_MissingLever_DissaraySage_C` (items agent) |
| `SaveGame` | 1 | `WBP_SaveGameEntry_C` — UI |
| `SaveObject` (BP) | 49 | all 49 are in §5/§6/§7 |

---

## 12. UNKNOWNs and open questions

| # | UNKNOWN | What I tried |
|---|---|---|
| 1 | Every `FGameplayTag` **string value** used for world state | `obj WHERE type='GameplayTag'` → 0 rows; `GameplayTagsList`/`GameplayTagsManager` objects hold no dumpable tag names; ripgrep for tag-like literals over the 565 MB dump and over all 10 415 `.hpp` → 0 hits. The tag *slots* are tabulated in §9.2 |
| 2 | The concrete `SaveObjectClass` each `USpartaSaveComponent` uses | CDO values are not dumped. Only the *type* (`ClassProperty` meta `/Script/Sparta.SpartaSaveObject`) is provable. The 56 typed-variable bindings in §6 are the partial answer; for the remaining ~78 actors the class is UNKNOWN. Method that would settle it: read `SpartaSaveComponent.SaveObjectClass` at runtime from a live instance |
| 3 | The `FName` **values** of store-B keys (`BP_HubDoor.DoorIsOpenName`, `BP_Interactable_Chest.TimesUsedName`, `BP_BreakableWall.WallIsBrokenName`, `BP_Interactable_Chest_General.UniqueID`) | these are Blueprint default values on the CDO (`Default__<Class>_C`), and the dump format carries no property *defaults* at all — only type, offset and name. So this is structurally unavailable from the dump, not a failed search. Method that would settle it: read the `FName` at runtime off a live instance (`UE4SS Lua: Instance.DoorIsOpenName`) |
| 4 | `SpartaSaveGlobalSettings` string values (file prefixes) | CDO not dumped; §1.4 instead quotes the on-disk file names observed in an existing research artifact |
| 5 | Value names of `E_CoreBosses` (`BPSO_BossDLManager.CurrentBoss`), `EDataLayerRuntimeState`, `E_DungeonButton_VisualTypes`, `E_ChestRewardtype`, `E_Flowers…` | `E_CoreBosses_enums.hpp` contains only `NewEnumerator0..10`; the dump's `UserDefinedEnum /Game/Sparta/Core/Interaction/E_CoreBosses` object has no children. No display-name metadata in either source |
| 6 | Which BP classes *implement* `SpartaSaveInterface` | the dump has no "implemented interfaces" field; `SubscribeSaveInterface(Object)` is only ever referenced from native code and from `BP_PlayerController`'s `S_ResetSaveObjectByGuid` helper path. `cls` has no interface-implementation column. Query that would settle it: runtime `Object:Implements<USpartaSaveInterface>()` |
| 7 | How `SpartaSaveSubSystem.SaveObjects` (`TMap<FGuid,…>`) is populated, and how a save object reaches `SpartaSaveGame.ActorObjectMap` | both are native `Sparta` functions; the native bodies are not in the object dump. `Sparta.hpp` gives the declarations only. GUESS: on `SaveToDisk`, each registered component's `RuntimeData` is written into `ActorObjectMap` keyed by `Guid.ToString()`, and `SaveObjects` is the live cache. Evidence that this is the right shape: `ResetSaveObjectByGuid(GuidString: Str)` (string, not `FGuid`) and `ActorObjectMap` is `TMap<FName,UObject*>` while `SaveObjects` is `TMap<FGuid,…>` |
| 8 | Whether `SpartaSaveComponent.Guid` is stable across sessions | `GenerateNewGuid()` exists; when it is called is native. UNKNOWN. This is the key question for cross-client identity of a per-instance save object |
| 9 | `BP_LandingAreaBase_C.OnEnableStateChanged` signature | no `__DelegateSignature` Function object found on that class; `df:` target not resolved |
| 10 | `BPC_InteractionHandler_C.OnInteractionEnd` signature | same |
| 11 | `BP_STH_Exit_Gate_C` / `BP_STH_Exit_C` / `BPO_STH_Exit_*` members | I did not read their `.hpp` files; they exist (live instances in §13) but are out of my depth for this pass |
| 12 | `BP_GloomDoor_C` save mechanism | it has `KeyRequired` (gloom cost) and no `SpartaSave` property; the dump shows no save component and no `BPSO` cast. Whether its opened state is recorded (via `BP_SO_SavedLockedDoors_C`) is UNKNOWN |
| 13 | `BP_Interactable_LockedChest_PlayerEquipments_C` / `BP_Interactable_PrayToOpenDoor_C` persistence | no `SpartaSave` property in the dump; UNKNOWN whether they persist at all |
| 14 | `BP_VFX_Breakable` per-instance identity | it has no `SpartaSaveComponent`, so there is no Guid and no store-A record. There is no way to persist "this barrel is broken" in this build — but a *sync* layer can still replicate the visual break. Whether the game re-spawns a broken barrel after reload is UNKNOWN (would need a runtime test) |
| 15 | `SpartaInstancedObject` vs `SpartaWorldObject` semantics | both are empty `UObject` bases with identical layout (`Size: 0x28`); the naming suggests "instanced per world" vs "world-owned", but the dump gives no behaviour. GUESS only |
| 16 | `BP_ChangeSpatiallyLoadedStateOnActors_C` behaviour | class exists, in the interaction folder, but has no save component and I did not read its `.hpp` |

---

## 13. Live instances (from `raw\ObjectDump_ingame.txt`)

`raw\ObjectDump_ingame.txt` **did exist** when I finished (126 087 083 bytes, mtime 22.10.2026
06:22:39). I indexed it with
`python scripts\parse_dump.py index raw\ObjectDump_ingame.txt raw\index_ingame.sqlite3`
→ **536 682 objects**. The coordinator later confirmed the same index is complete and current and
asked that it not be rebuilt; I re-ran every query below against it afterwards and every number in
§13.1/§13.2 reproduced identically.

Index scope, dump-verified (`scripts\q\ws-ingame-sanity.sql`, `ws-ingame-maps.sql`,
`ws-ingame-maps3.sql`, `ws-ingame-maps4.sql`) — this matters, because "live instance" here means
"live in one particular world at one particular moment":

| Fact | Value |
|---|---|
| `obj` rows | 536 682 |
| `cls` rows (every class-like object actually instantiated in the live process) | 9 570 — vs **16 095** in the main-menu all-assets index `raw\index.sqlite3`, so the gameplay process has ~59 % of the build's class objects resident |
| objects whose path contains `:PersistentLevel` | 58 962 |
| distinct level paths among those | 1 294 |
| of which the main level `/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01` | 24 454 objects |
| of which `_Generated_` World-Partition sublevels of `L_Core_World_01` | 1 292 level paths, 34 506 objects |
| further `L_Core_World_01` objects owned by the level itself but **not** under `:PersistentLevel` (e.g. `…/L_Core_World_01.L_Core_World_01:AISystem_2147480647`) | 6 104 — excluded from the tables below |
| anything outside `L_Core_World_01` | **2** objects, both `/Script/Engine.*` placeholders (`/Script/Engine.World`, `/Script/Engine.LevelCollection`) |

So the whole gameplay dump is one world — `L_Core_World_01` (the core/hub world) plus the streamed
in World-Partition sublevels it pulled in. Per the coordinator the dump was taken **at the player's
checkpoint spawn**; that placement is not itself provable from the dump, but the map scope is.

Everything below is a `:PersistentLevel` query against that index
(`scripts\q\ws-ingame-live.sql`, `ws-ingame-saveobj.sql`, `ws-ingame-paths.sql`).

### 13.1 Counts per class actually present in a real world

| Object class | Live instances |
|---|---|
| `SpartaSaveComponent` | **234** |
| `BP_STH_Exit_Dungeon_C` | 34 |
| `BP_TarRootGate_C` | 18 |
| `BP_LandingAreaBase_C` | 14 |
| `BP_PrologueCheckpoint_C` | 14 |
| `BP_LandingArea_Teleport_C` | 13 |
| `BP_TarRootGate_03_C` | 10 |
| `BP_Interactable_Chest_Skin_C` | 8 |
| `BP_FogVolume_C` | 7 |
| `BP_LandingArea_FinalLevel_C` | 7 |
| `BP_LandingAreaSelector_C` | 6 |
| `BP_LandingArea_Boss_C` | 6 |
| `BP_LandingArea_Checkpoint_C` | 6 |
| `BP_STH_Entrance_Gate_C` | 6 |
| `BP_STH_Exit_Gate_C` | 6 |
| `BP_SpartaPlayerStart_C` | 3 |
| `BP_VFX_Breakable_Wooden_Barrel_a_Closed_01_C` | 3 |
| `BP_DungeonTransition_C` | 2 |
| `BP_LandingArea_C` | 2 |
| `BP_STH_Exit_Well_C` | 2 |
| `BP_BagTeleport_C` | 1 |
| `BP_EndDungeonRewardDoor_C` | 1 |
| `BP_HiddenWall_Base_Natural_Rock_C` | 1 |
| `BP_HiddenWall_Base_Natural_Rock_CannonBall_C` | 1 |
| `BP_HubDoubleDoor_C` | 1 |
| `BP_HubLiftLever_C` | 1 |
| `BP_HubLift_C` | 1 |
| `BP_LandingArea_HUB_C` | 1 |
| `BP_VFX_Breakable_CartJars_C` | 1 |
| `BP_VFX_Breakable_Cart_02_A_C` | 1 |
| `FogPauseArea_C` | 1 |

Classes with **zero** live instances of the exact names I queried: `BP_HubDoor_C`,
`BP_DoorLockedDungeon_C`, `BP_Interactable_Lock_Door_C`, `BP_BreakableWall_C`, `BP_BossGate_C`,
`BP_BlockedBossGate_C`, `BP_Interactable_Chest_C` (the base — only its `_Skin` subclass was live),
`BP_BearTrap_C`, `BP_Trap_C`, `BP_SpikeTrap_C`, `BP_FlowerChest_C`, `BP_UnlimitedChest_C`,
`BP_LockedLightChest_C`, `BP_GloomDoor_C`, `BP_Grisha_Cage_01_WithLogic_C`. Their Blueprints are
loaded (they appear as `BlueprintGeneratedClass` objects) but the world the dump was taken in does
not have those instances streamed in — so absence here does **not** mean the class is unused.

This is single-world evidence: the whole dump is `L_Core_World_01` + its streamed sublevels (§13
scope table), so a class that only appears in, say, a forest or dungeon map will correctly show zero
here. Treat the counts as "proven to exist live *somewhere*", never as "proven absent elsewhere".

### 13.2 Live save-object instances

| Save-object class | Live instances |
|---|---|
| `BPSO_WordMapObjective_C` | 133 |
| `BPSO_TarGate_C` | 28 |
| `BP_SO_SimpleBool_C` | 27 |
| `BPSO_SpawnerSaveObj_C` | 14 |
| `BPSO_NPC_C` | 13 |
| `BPSO_CinematicDLUpdate_C` | 11 |
| `BPSO_BossGate_C` | 6 |
| `BPSO_LandingArea_Boss_C` | 6 |
| `BPSO_CraftingItemPickup_C` | 5 |
| `BPSO_ShopData_C` | 5 |
| `BPSO_FC_CorruptionCore_C` | 3 |
| `BPSO_FC_FinalBossManager_C` | 1 |
| `BPSO_FC_TarRoots_C` | 1 |
| `BPSO_GenessaSensor_C` | 1 |
| `BPSO_MovingObject_C` | 1 |
| `BPSO_NarrativeEventsTracker_C` | 1 |
| `BPSO_Progression_C` | 1 |
| `BPSO_QuestLogComponent_C` | 1 |
| `BPSO_ShellMemories_C` | 1 |
| `BPSO_SlayerDungeonChalice_C` | 1 |
| `BPSO_Tarforge_C` | 1 |
| `BPSO_WoodenElevator_C` | 1 |
| `BPSO_WorldMap_C` | 1 |
| `BP_SpartaPlayerSaveObject_C` | 1 |

Two structural readings of this table:

* `PerInstance` save objects are duplicated per actor (`BPSO_TarGate_C` × 28 matches the 18+10 live
  `BP_TarRootGate_C` / `BP_TarRootGate_03_C` instances; `BP_SO_SimpleBool_C` × 27 matches the many
  actors bound to it in §6).
* `Global` save objects are singletons (`BPSO_Progression_C`, `BPSO_QuestLogComponent_C`,
  `BPSO_WorldMap_C`, … exactly 1 each).

### 13.3 Live path shapes (useful for a sync layer that keys on actor identity)

```
[000002728A233B20] SpartaSaveGame /Engine/Transient.SpartaGameEngine_2147482593:BP_SpartaGameInstance_C_2147482225.SpartaSaveSubSystem_2147482069.WorldState_2147480430 [n: 55D283] [c: 00007FF434F07138] [or: 000002712176DA80]
[000002712176DA80] SpartaSaveSubSystem /Engine/Transient.SpartaGameEngine_2147482593:BP_SpartaGameInstance_C_2147482225.SpartaSaveSubSystem_2147482069 [n: 4A2D5] [c: 00007FF434F07B38] [or: 00000271388D10C0]
```

Actor instances are named `…:PersistentLevel.<Class>_C_UAID_<hex>_<hex>[_<hex>]` and their save
component is a child object suffixed `.SpartaSave`, e.g.

```
/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel.BP_EndDungeonRewardDoor_C_UAID_D4E98A7EF567E19302_1693709662
/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel.BP_EndDungeonRewardDoor_C_UAID_D4E98A7EF567E19302_1693709662.SpartaSave
/Game/Sparta/Maps/Core_Levels/L_Core_World_01/_Generated_/CUM674YA7WEQ1OG3M7EUETQNK.L_Core_World_01:PersistentLevel.BP_HubDoubleDoor_C_UAID_D4E98A7EF567D94802_83a1dc45e2a01438_1808979936
/Game/Sparta/Maps/Core_Levels/L_Core_World_01/_Generated_/2X4U4ZE5Y7L94HLLG9EVNQVBA.L_Core_World_01:PersistentLevel.BP_HubLift_C_UAID_D4E98A7EF5672F1B02_83a1dc45e2a01438_1653632767
/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel.BP_LandingArea_Checkpoint_C_UAID_D4E98A7EF56706DF02_2125821657
/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel.PlayerStart_UAID_001A7DDA71153E1702_1482099407
```

Note the `UAID` suffix — that is the UE World Partition actor-instance ID, and it is a *different*
identity from `SpartaSaveComponent.Guid`. Do not conflate them.

---

## 14. One-page index of the evidence scripts

All SQL I wrote for this report is in `research\scripts\q\` and can be re-run with
`python scripts\q.py raw\index.sqlite3 scripts\q\<file>`:

| File | What it answers |
|---|---|
| `ws-counts.sql` | candidate counts per name keyword |
| `ws-props-save.sql` | properties of every native save class (the §2 tables) |
| `ws-saveobj-tree.sql` | complete save-object subclass tree (51 BP + 6 native) |
| `ws-bpso-props.sql` | every property of every `BPSO_*`/`BP_SO_*` class incl. map key/value sub-objects |
| `ws-saveobj-funcnames.sql` | all function names declared on save-object classes (20 rows) |
| `ws-saveobj-funcs.sql` | those functions' parameters in dump order |
| `ws-saveobj-refs.sql` | every property anywhere whose type is a save-object class (401 rows) |
| `ws-saveobj-bind.sql` | the class-level subset of the above (56 rows: 53 bindings + 3 self-refs) — the §6 table |
| `ws-savecomp-owners.sql` | every class owning a `USpartaSaveComponent` (134 rows) — the §7 table |
| `ws-map-kv.sql` | map key/value element types for store A/B/C |
| `ws-subclasses.sql` | subclasses of `SpartaWorldObject` / `SpartaInstancedObject` / `SpartaSingleton` |
| `ws-delegates.sql` | resolves `df:` addresses to `DelegateFunction` signatures |
| `ws-delegate-props.sql`, `ws-delegate-props2.sql` | all delegate properties on the world-state classes |
| `ws-cand-a.sql`, `ws-cand-b.sql`, `ws-cand-c.sql` | candidate enumeration (door/gate/lift/chest/fog/trap; save-object folders; beacon/checkpoint/respawn) |
| `ws-tags-intro.sql`, `ws-tags-lists.sql`, `ws-tags-assets.sql` | the tag investigation behind §9.1 |
| `ws-enumobjs.sql` | what the dump has for `E_CoreBosses` / `ESaveObjectProtocol` |
| `ws-skipped.sql` | the `Rest` / `Switch` samples behind §11 |
| `ws-levelselector.sql` | `BP_LevelInstanceSelector_C` / `BP_DungeonEventSelector_C` members |
| `ws-metrics.sql` | the headline counts (51 / 134 / 46 / 3) |
| `ws-ingame-live.sql`, `ws-ingame-saveobj.sql`, `ws-ingame-paths.sql` | the §13 live-instance tables (run against `raw\index_ingame.sqlite3`) |
| `ws-ingame-sanity.sql`, `ws-ingame-maps.sql`, `ws-ingame-maps3.sql`, `ws-ingame-maps4.sql` | the §13 index-scope verification: row counts, `:PersistentLevel` counts, which world(s) the gameplay dump actually covers. (`ws-ingame-maps2.sql` is a looser first draft of the same question and is superseded by maps3/maps4 — it buckets the 6 104 world-owned non-`PersistentLevel` objects of the main level as a spurious extra "map") |
| `ws-savecomp-groups.sql` | the §7 folder grouping (sums to 134) |
| `ws-cand-small.sql` | exact member lists for the small keyword sets (Lever 6, Trap 9, Fog 15, Barrier 5, Chest 24, Ladder 4) |
| `ws-family-counts.sql` | build-wide family counts used in §11 (`PLA_` 1281, `GA_` 1560, `WBP_` 658, `LVA_` 40, `GlobalEvent_` 348, `GE_`/`DEM_` 673) |
| `ws-cls-count.sql` | `cls` row count of the all-assets index (16 095) for the §13 comparison |
