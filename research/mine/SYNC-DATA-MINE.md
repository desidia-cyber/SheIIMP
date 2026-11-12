# SYNC DATA MINE — every offset, signature, enum, asset and proof needed to implement full MS2 co-op sync

**Target** Mortal Shell II, UE 5.6.1 shipping, build `5.6.1-0+++Sparta-Depot+Day1+CL92086`
**Game exe SHA-256** `BDEDDB33D61E876E94B6C30C8EA198AECFDEFFB557D6F5678C92D1C4C17AAE69` (recorded in `research\00-sources.md`)
**What this document is** raw data, not a plan. Every class, member offset, byte offset, function
signature, enum value, asset path and count below was read out of the artefacts listed in §26 and
carries the dump line where it came from. Use it as the lookup table while implementing; the
*decisions* (what to send, when, with which merge rule) belong to `ms2-coop\docs\HANDOFF-REMAINING-SYNC.md`.
**Nothing here was guessed.** Anything static mining cannot answer is listed as `UNKNOWN` in §25
together with the exact runtime probe that would answer it.

---

## 0. How to use this document

1. **Look up the state you want to sync in §3** (the store map) — that tells you *which* store owns it.
2. **Jump to the section for that store** (§4 store A, §5 store B, §6 data layers, §7 body,
   §8 session, §9 progression, §10 economy, §11 inventory, §12 progression sub-systems,
   §13 quests, §14 enemies, §15 bosses, §16 dialogue, §17 flow, §18 cinematics, §19 interactables,
   §20 persistence).
3. **Read the offset** and, for GAS attributes, the `CurrentValue` byte offset (see §7.1 — an
   attribute member at `0x60` has its live float at `0x6C`; the reflected member offset alone is
   *not* the value you want).
4. **Call the named function** from the function table of the same section (UE4SS calls any
   reflected `UFunction` by name through `ProcessEvent`; non-reflected C++ helpers are not in the
   dump and therefore not listed).
5. **Cite the proof**: every table row ends in either `ObjectDump_allassets.txt:<line>`, a
   `Dumper-7` header/field, or a byte count of a real payload.

Everything is regenerable — see §26. Nothing in this document was produced by running the game.

### Reading conventions

| Marker | Meaning |
|---|---|
| `[FACT]` | Read directly from a dump/SDK/payload artefact; the proof is cited. |
| `[DERIVED]` | Arithmetic or join over two facts (e.g. `CurrentValue = member + 0xC`); the two inputs are cited. |
| `[UNKNOWN]` | Static mining cannot establish it; the probe that settles it is named. |
| `[PROPOSAL]` | My design suggestion for the wire/merge side; not a fact about the game. |
| `0xNN` | A reflected UPROPERTY offset inside the owning object (absolute, from `FObjectInitializer`). |
| `@+0xNN` | A byte offset *inside* a struct property, i.e. the final pointer arithmetic for a raw read. |

---

## 1. Sources and how trustworthy each is

| # | Source | What it proves | Size / shape | Trust |
|---|---|---|---|---|
| S1 | `research\raw\ObjectDump_allassets.txt` (+ `index.sqlite3`) | class existence, class hierarchy, **property offsets**, function parameter lists and order | 565 MB, **2 170 742 lines**, 2 153 654 indexed rows | primary for class/property/function facts |
| S2 | `research\raw\ObjectDump_ingame.txt` (+ `index_ingame.sqlite3`) | which of those classes actually exist in a live world | 126 MB, 536 682 indexed rows | primary for "does it exist at runtime" |
| S3 | `research\raw\Dumper7_allassets\...\CppSDK\SDK\Sparta_structs.hpp` | **enum values**, struct field layout with offsets and sizes | 243 KB, 196 structs, 105 enums | primary for enums + struct layout |
| S4 | `research\raw\Dumper7_allassets\...\CppSDK\SDK\Sparta_functions.cpp` | UFunction **flags** (`BlueprintCallable`, `Static`, `BlueprintPure`, …) | 1.79 MB, 1 766 Sparta functions | primary for flags |
| S5 | `research\raw\CXXHeaderDump\*.hpp` | a second, independent layout opinion; the only source with offsets for engine structs Dumper-7 does not emit (e.g. `FGameplayAttributeData`) | 10 469 files, 20.8 MB, **43 920 struct-field rows** | primary for engine struct layout |
| S6 | `research\mine\wire\out_A.txt`, `out_B.txt` | the **actual** payloads the current mod published (byte-exact) | 2 852 B / 4 157 B on disk | primary for the wire format |
| S7 | `ms2-coop\mod\CoopMod\Scripts\main.lua` | what the mod already reads/writes | 4 133 lines, 186 015 B | primary for "already implemented" |

### 1.1 Independent verification of the offsets (this is the important part)

Offsets are the one thing you cannot get wrong silently, so they were cross-checked between two
dumps taken with two different tools that both walk `UStruct::PropertyLink`:

| Check | Population | Result |
|---|---|---|
| Dumper-7 struct fields vs UE4SS struct fields, compared on `(module, struct, field)` | **21 356** shared fields | **0 offset/size mismatches** (`mine\structs-crosscheck-conflicts.tsv` is empty by construction) |
| Dumper-7 `Sparta_classes.hpp` class properties vs the UE4SS object dump, compared on `(class, property)` | **2 197** shared properties out of 2 199 D7 / 2 374 dump | **0 offset mismatches** (`mine\classprops-crosscheck-conflicts.tsv`) |

Consequence: an offset in this document can be used as a raw read/write address without a
second opinion. The two fields that were *not* comparable are excluded, not fudged.

---

## 2. The object-dump line format (so you can re-derive any row yourself)

```
[00007FF46F065D38] Class /Script/Sparta.SpartaHealthSet [n: 5AE3E1] [c: 00007FF46EE54D38] [or: 00007FF46F17CD68]
Property /Script/Sparta.SpartaHealthSet:Health  StructProperty of /Script/GameplayAbilities.GameplayAttributeData  [o: 0x60]
Function /Script/Sparta.SpartaHealthSet:OnRep_Health
  Parm  /Script/Sparta.SpartaHealthSet:OnRep_Health:OldValue   StructProperty of /Script/GameplayAbilities.GameplayAttributeData [o: 0x0]
```

* `[o: 0xNN]` on a member → the UPROPERTY offset. On a function's `Parm` rows → the offset of that
  parameter **inside the generated `Parms` struct**, which is also the declaration order.
* Parameters are properties whose `Outer` is the `UFunction`. Compiler temporaries
  (`Temp_`, `CallFunc_`, `K2Node_`, `__Internal_`, `ExecuteUbergraph_`, `Goto_`, `eb_`, `bpf_`) were
  filtered out of every signature in this document; `ReturnValue` was kept (it *is* the return value).
* Blueprint classes own their BP-declared variables. Some Blueprints have **several variables with
  the same name** (e.g. `BP_VFX_Breakable_C` has four `Thrshld` and four `Damage`) — for those,
  address the field by offset, never by name.
* Re-query any of this with the committed tool: `python scripts\mine.py show <exact class path>`.

---

## 3. Where state lives — the store map

`[FACT]` Five stores hold everything the game keeps across a session. Sync work is dominated by
knowing which store a thing lives in, because each store has a completely different read/write cost.

| Store | Concrete object | How it is reached | What it owns | Section |
|---|---|---|---|---|
| **A** | `USpartaSaveObject` subclasses, one per actor, held in `USpartaSaveComponent.RuntimeData` | `actor → SpartaSaveComponent (class-dependent) → GetRuntimeData()` | 57 save-object classes: gates, doors, chests, lifts, spawners, bosses, quest log, progression, player save | §4 |
| **B** | `USpartaSaveGame` (the "world state" save) | `USpartaSaveSubSystem.GetCurrentWorldStateSave()`; helper API on `USpartaWorldStateHelper` | free-form `float/int/bool/name/object` values keyed by `(actor name, key)` or `(owner tag, key tag)` | §5 |
| **C** | `UDataLayersSave` | `USpartaDataLayerSubsystem.RuntimeData` (`0x38`) | data-layer runtime states (`Unloaded`/`Loaded`/`Activated`) — this is what swaps dungeon layouts | §6 |
| **D** | the live player pawn: `USpartaAbilitySystemComponent` + attribute sets + `USpartaHealthComponent` | `SpartaCharacter.AbilitySystemComponent (0xAA8)`, `SpartaCharacter.HealthComponent (0x6A0)` | health, shell health, resolve, poise, break, heal charges, revive progress, ammo, PP resource, death state | §7 |
| **S** | `BPSO_PlayerSession_C` — a *session* save object (`bIsSessionSaveData`, `ESaveObjectProtocol`) | `SpartaSaveSubSystem.SessionSaveObject (0x120)` / `RuntimeSessionSaveObject (0x128)` | persisted mirror of D (health/resolve/heal/revive) + respawn `Transform` + `IsInDarkform` | §8 |
| **P** | `BP_SpartaPlayerSaveObject_C` — one object, 73 fields | `SpartaSaveSubSystem.SaveObjects` map, or `BP_PlayerController_C.PlayerSaveGameObject (0x8E0)` | **the entire progression**: currencies, equipment, skins, masks, clothing, unlocks, landing areas, tutorials, achievements, cinematics watched, stats, NG+ | §9 |

`[FACT]` `USpartaSaveSubSystem` is the authority for A/B/S/P: `SaveObjects` map at `0xB8`,
`CurrentOnDiskWorldStateSave` (`USpartaSaveGame`) at `0x108`, `SessionSaveObject` at `0x120`,
`RuntimeSessionSaveObject` at `0x128`, `MetaData` at `0x140`, `bSaveLoaded` at `0x14C`
(proof `ObjectDump_allassets.txt:57843`–`57858`, class `/Script/Sparta.SpartaSaveSubSystem`).

---

## 4. Store A — per-actor save objects

### 4.1 The contract: `USpartaSaveObject` and `USpartaSaveComponent`

`[FACT]` Base class `USpartaSaveObject` (`/Script/Sparta.SpartaSaveObject`, super `Object`) is
**0xB8 bytes**; every concrete save object puts its own fields from `0xB8` upward, so field offsets
are directly comparable across classes.

| Member | Type | Offset | Note |
|---|---|---|---|
| `PrimarySaveObjectTick` | `FSaveObjectTickFunction` | `0x28` | the object ticks; `Target` inside it is at `@+0x28` |
| `IgnoreSaveGameMetaSpecifier` | `bool` | `0x58` | |
| `bIsSessionSaveData` | `bool` | `0xB1` | `[FACT]` true for session-scoped objects (see §8) |

`USpartaSaveObject` functions (`[FACT]`, all reflected → callable):

| Function | Signature |
|---|---|
| `UpdateFrom` | `(SaveObject: USpartaSaveObject, Events: FGameplayTagContainer) -> bool` |
| `ReceiveTick` | `(DeltaTime: float)` |
| `ReceiveSpartaPreSave` | `(SaveContext: FSpartaSaveContext)` |
| `ReceiveOnLoaded` | `()` |
| `ReceiveApplySkipProloguePreset` | `()` |
| `GetOwningSaveComponent` | `() -> USpartaSaveComponent` |
| `GetOwningActor` | `() -> AActor` |
| `SetTickEnabled` | `(bEnabled: bool)` |
| `SetTickInterval` | `(TickInterval: float)` |
| `IsTickEnabled` | `() -> bool` |
| `IsSessionSaveData` | `() -> bool` |

`[FACT]` `UpdateFrom` is the game's **own merge primitive**: it copies state out of another save
object of the same class. That is the single most interesting function in store A for co-op — see
§26 "what this enables" and the caveat about `Events` in §25.

`[FACT]` Component side, `USpartaSaveComponent` (super `ActorComponent`), size `0x120`:

| Member | Type | Offset |
|---|---|---|
| `SaveObjectClass` | class | `0xC0` |
| `OnSaveLoaded` | delegate | `0xC8` |
| `OnPreSave` | delegate | `0xD8` |
| `OnSessionReset` | delegate | `0xE8` |
| `SaveProtocol` | `ESaveObjectProtocol` | `0xF8` |
| `bApplyPrologueSkipPreset` | bool | `0xF9` |
| `Guid` | `FGuid` | `0xFC` |
| `RuntimeData` | `USpartaSaveObject*` | `0x110` |
| `bSaveLoaded` | bool | `0x118` |

Functions: `Save(Events: FGameplayTagContainer)`, `ResetSaveData()`, `IsSaveLoaded() -> bool`,
`GetRuntimeData() -> USpartaSaveObject`, `GetGUID() -> FGuid`, `GenerateNewGuid()`.

`[FACT]` `FSpartaSaveContext` (struct, used by every `ReceiveSpartaPreSave`):

| Field | Type | Offset | Size |
|---|---|---|---|
| `SaveSubsystem` | `USpartaSaveSubSystem*` | `0x00` | `0x08` |
| `EventTags` | `FGameplayTagContainer` | `0x08` | `0x20` |
| `SlotIndex` | `int32` | `0x28` | `0x04` |
| `bSaveWholeObject` | bool | `0x2C` | `0x01` |
| `IsSaving` | bool | `0x2D` | `0x01` |
| `IsSavingSession` | bool | `0x2E` | `0x01` |

`[FACT]` `FSaveObjectTickFunction`: `Target: USpartaSaveObject*` at `@+0x28` (size `0x08`).

`[FACT]` `ESaveObjectProtocol` (Sparta): `PerInstance = 0`, `Global = 1`.

### 4.2 Identity model (why key matching breaks across maps)

`[FACT]` Actor names in the dump look like
`BP_Cultist_HUB_C_UAID_D8BBC1D053266E4902_83a1dc45e2a01438_1464626973` (proof: any `npc` entry in
`mine\wire\out_A.txt`). The `UAID_…` tail is generated at cook/load time and is **stable for the
same map in the same save**, which is why store-B keys and store-A lookups work inside one zone —
and why they silently match nothing once the two peers are in different levels or a level instance
is streamed differently. `[PROPOSAL]` therefore every wire key must carry a zone/level id (§21).

`[FACT]` Save-object GUIDs come from `USpartaSaveComponent.Guid` (`0xFC`) and
`USpartaSaveSubSystem.ResetSaveObjectByGuid(GuidString)` takes the **string** form — that is the
only per-instance handle you can send over the wire and resolve on the other side.

### 4.3 The complete save-object inventory — 57 classes

`[FACT]` 51 Blueprint save objects live under `/Game/`, plus 6 native save-data classes. The table
is `mine\saveobjects-matrix.md` (full field lists with offsets in `mine\saveobjects-matrix.tsv`).
Field counts and the first fields, all from `ObjectDump_allassets.txt`:

| Save object (last path segment) | Fields | Own fields (`name@offset`) |
|---|---|---|
| `BP_SpartaPlayerSaveObject_C` | **73** | `ActorBools@0x340`, `DropItems@0x390`, `ItemPoolState@0x3E0`, `CumulativeGloom@0x430`, `CumulativeGold@0x434`, `ActiveShellItemDefinition@0x440`, `EquipmentUnlockState@0x468`, `FinishedPrologue@0x4B9`, `CinematicsWatched@0x5B0`, `EarnedAchievements@0x650`, `ActiveShellSkins@0x7F0`, `UnlockedMasks@0x950`, `ActiveShellClothing@0xA10` … → §9 |
| `BP_SpawnerSessionSaveObject_C` | 12 | `WasKilled@0xC0`, `SpawnerOwner@0xC8`, `DidSpawn@0xD0`, `AttemptedSpawn@0xD2`, `Owner@0xD8`, `Spawner@0xE0`, `SpawnedClass@0xE8` |
| `BPSO_PlayerSession_C` | 16 | `Resolve@0xC0`, `Health@0xC8`, `ShellHealth@0xD0`, `HealCurrentCharges@0xD8`, `ShellReviveProgress@0xE0`, `ShellReviveMaxProgress@0xE4`, `StartTransform@0xF0`, `IsInDarkform@0x168` → §8 |
| `BPSO_Progression_C` | 11 | `CurrentLevel@0x1A8`, `AvailablePoints@0x1AC`, `AssignedPoints@0x1B0`, `BondingPoints@0x200`, `GoldSpentAtShop@0x250` (+ `SkillLevels@0xB8`, `ShellBuilds@0x108`, `TierBuilds@0x158` inherited from `PlayerProgressionSaveData`) |
| `BPSO_WorldMap_C` | 7 | `Trackers`, `ObjectiveGUID`, `IsInsideObjective`, `LastPlayerLocation` |
| `BPSO_WordMapObjective_C` | 9 | `Visible`, `Revealed`, `Completed`, `ShowArea`, `PlayedRevealScript` |
| `BPSO_QuestLogComponent_C` | 5 | `NarrativeTags@0xC0` (`FGameplayTagContainer`) |
| `BPSO_NarrativeEventsTracker_C` | 4 | `NarrativeEventTimeTracking@0xB8` |
| `BPSO_ShellMemories_C` | 5 | `MemoryProgress@0xB8`, `MemoriesWatched@0x108` |
| `BPSO_Gate_C` | 9 | `GateOpened@0xB8`, `GotVarsOnce@0xB9`, `StartingLoc@0xC0`, `EndingLoc@0xD8`, `StartingRot@0xF0`, `EndingRot@0x108` |
| `BPSO_MovingObject_C` | 9 | `MovedToEnd@0xB8`, `GotVarsOnce@0xB9`, `StartingLoc@0xC0`, `EndingLoc@0xD8`, `StartingRot@0xF0`, `EndingRot@0x108` |
| `BPSO_WoodenElevator_C` | 6 | `StartingFloor`, `HasBrokenOnce`, `StartingLoc` |
| `BPSO_CloseAndOpenGate_C` | 4 | `CombatEncCompleted` |
| `BPSO_DoorLockedDungeon_C` | 4 | `DoorOpened` |
| `BPSO_ShepherdSheepGate_C` | 4 | `GateOpened` |
| `BPSO_BossGate_C` | 4 | `Unlocked` |
| `BPSO_TarGate_C` | 5 | `GateClensed`, `PlayerClensed` |
| `BPSO_FlowerChest_C` | 4 | `ItemCollected` |
| `BPSO_CraftingItemPickup_C` | 5 | `ItemCollected`, `ItemInitialized` |
| `BPSO_SavedLockedChests_C` | 5 | `SavedChestIDs`, `HasBeenOpened` |
| `BP_SO_SavedKeys_C` | 4 | `SavedKeyIDs` |
| `BP_SO_SavedLockedDoors_C` | 4 | `SavedDoorIDs` |
| `BP_SO_SimpleBool_C` | 4 | `MyBool` |
| `BPSO_Ovum_C` | 5 | `Cleansed`, `OvumCollected` |
| `BPSO_ShellRevive_C` | 4 | `StationUsed` |
| `BPSO_ShellGhost_C` | 4 | `StateIndex` |
| `BPSO_ShopData_C` | 6 | `CachedItemsMap`, `TotalGoldSpent`, `StolenItems` |
| `BPSO_Tarforge_C` | 4 | `StoredTarcores` |
| `BPSO_EvilStatue_C` | 5 | `Cleansed`, `RewardGiven` |
| `BPSO_EridrimUnlockManager_C` | 6 | `WardenJumped`, `WardenDefeated`, `ShellClaimed` |
| `BPSO_GenessaSensor_C` | 4 | `SensorClensed` |
| `BPSO_GrishaCage_C` | 5 | `CageOpened`, `OpenType` |
| `BPSO_GrishaGhost_C` | 4 | `BossDefeated` |
| `BPSO_HutchbackBossTrigger_C` | 4 | `HutchbackDefeated` |
| `BPSO_LandingArea_Boss_C` | 4 | `BeaconRisen` |
| `BPSO_BossDLManager_C` | 5 | `CurrentBoss`, `RelevantDL` |
| `BPSO_FC_CorruptionCore_C` | 4 | `Cleansed` |
| `BPSO_FC_FinalBossManager_C` | 7 | `EndStrandingCompleted`, `AllCorruptionCoresClensed`, `ClensedCores`, `TotalCoresNeeded` |
| `BPSO_FC_TarRoots_C` | 7 | `T1Cleansed`, `T2Cleansed`, `T3Clensed`, `CenterClensed` |
| `BPSO_TarGolemLevelScript_C` | 4 | `StoneSealClaimed` |
| `BPSO_TavernArena_C` | 6 | `WagerPlaced`, `ArenaCombatLVL`, `RewardGiven` |
| `BPSO_PetrifiedCultistManager_C` | 4 | `Harros` |
| `BPSO_PlayerTrapDungeon_C` | 4 | `TrappedOnce` |
| `BPSO_SlayerDungeonChalice_C` | 4 | `SealBroken` |
| `BPSO_SpawnerSaveObj_C` | 4 | `WasEverKilled` |
| `BPSO_NPC_C` | 5 | `HasTriggeredInitialGreeting`, `WasEverKilled` |
| `BPSO_BearTrap_C` | 4 | `IsDisarmed` |
| `BPSO_CinematicDLUpdate_C` | 4 | `CinematicFired` |
| `BPSO_FallingCorpse_C` | 4 | `ShotDown` |
| `BPSO_FallingStatue_C` | 4 | `StatueFallen` |
| `BPSO_Egon_C` | 4 | `GloomDonated` |
| `Sparta.PlayerSaveObject` (native base) | 14 | `TotalPlayTimeSeconds@0xB8`, `CompletedSpawnTriggerRecipes@0xD0`, `CompletetedSpawnTriggers@0x120`, `ShellStats@0x170`, `WeaponStats@0x1C0`, `PlayerStats@0x210`, `GameLibraryData@0x260`, `OverrideWorldGenTags@0x2B0`, `Difficulty@0x2D0` |
| `Sparta.PlayerProgressionSaveData` | 6 | `SkillLevels`, `ShellBuilds`, `TierBuilds` |
| `Sparta.TarstoneSaveData` | 6 | `TarstoneLevels`, `TarstoneBuilds`, `TarstoneSupportBuild` |
| `Sparta.PPItemsSaveData` | 7 | `ActivePPItem`, `PassivePPItem`, `PPItemLevels`, `PPItemFoundAmounts` |
| `Sparta.DataLayersSave` | 4 | `SavedDataLayerStates@0xB8` |
| `Sparta.SpartaSessionSaveObjectBase` | 3 | *(base only)* |

`[FACT]` **130 classes own a `USpartaSaveComponent`** (`mine\savecomponent-owners.md`, derived from
the `:<Component>:SpartaSave_GEN_VARIABLE` SCS-template rows of every Blueprint). That is the upper
bound of "actors that could have per-actor state" — the mod currently watches 27 of the 57
save-object classes and 1 raw actor field, so ~24 save-object classes and most of those 130 actor
types contribute nothing to the wire.

### 4.4 Read/write recipe for store A

`[FACT]` Reading: on the live actor, fetch the save component (BP components are named
`SpartaSave`/`Save` depending on the class — two shapes exist in the dump:
`:Save` (C++ subobject, e.g. `USpartaHordeSpawner.Save` at `0xB8` on `UnitSpawner`) and
`:SpartaSave_GEN_VARIABLE` (BP SCS template)), then `GetRuntimeData()`.
`[FACT]` Writing: fields are plain UPROPERTYs → set them directly, then call
`USpartaSaveComponent:Save(Events)` to let the object publish itself.
`[FACT]` Resetting a fixture (used by tests): `USpartaSaveSubSystem:ResetSaveObjectByGuid(GuidString) -> bool`
and `ResetSaveObjectByClass(SaveObjectClass) -> int32` (returns how many were reset).

---

## 5. Store B — the world-state save and its helper API

### 5.1 `USpartaSaveGame` layout

`[FACT]` (`ObjectDump_allassets.txt`, class `/Script/Sparta.SpartaSaveGame`, super `SaveGame`):

| Map | Offset | Key type (from usage) | Value type |
|---|---|---|---|
| `ChangedActors` | `0x28` | set of names | "dirty" bookkeeping for the world-state save |
| `ActorFloats` | `0x78` | actor name | float |
| `ActorInts` | `0xC8` | actor name (`Alt`-encoded, see §5.2) | int32 |
| `ActorNames` | `0x118` | composite name | FName |
| `ActorBools` | `0x168` | composite name | bool |
| `ActorObjectMap` | `0x1B8` | name | UObject* |
| `ActorObjectMapAsString` | `0x208` | single string | serialized form |

`[FACT]` `ActorObjectMap`/`ActorObjectMapAsString` exist but the mod's `wbScan` iterates **only**
`ActorInts` and `ActorBools` (`main.lua:2710`), so float/name/object world-state is invisible to the
current wire — a real gap, and now a *quantified* one: two maps unread, plus the float map.

### 5.2 `USpartaWorldStateHelper` — the full callable API

`[FACT]` All of the following are `Static, Public, BlueprintCallable` functions on
`/Script/Sparta.SpartaWorldStateHelper` (a `BlueprintFunctionLibrary`, so it has **no instance** —
call it through the CDO `/Script/Sparta.Default__SpartaWorldStateHelper`). 90 functions; the seven
families and their exact parameter grammar:

| Family | Signature grammar | Notes |
|---|---|---|
| `WriteAn*` | `(WorldContextObject, OwnerName: FName, ValueName: FName, Value, bModify: bool)` | per-actor write |
| `WriteAn*ByTag` | `(WorldContextObject, OwnerName: FName, ValueName: FGameplayTag, Value, bModify)` | tag-keyed variant |
| `WriteMy*` / `ReadMy*` | `(WorldContextObject, ValueName: FName, …)` | "my" = the local player's actor |
| `WriteTheir*` / `ReadTheir*` | `(They: UObject, ValueName: FName, …)` | "their" = an explicit actor |
| `WritePlayer*` / `ReadPlayer*` | `(WorldContextObject, ValueName: FName, …)` | routed to the player save object |
| `Write*ByTags` / `Read*ByTags` | `(WorldContextObject, Owner: FGameplayTag, Key: FGameplayTag, Value, bModify)` | owner and key are *both* tags |
| `Write*SaveObject` / `Read*SaveObject` | `WriteASaveObject(WorldContextObject, OwnerName, Value)` / `ReadASaveObject(WorldContextObject, OwnerName) -> USpartaSaveObject` | store-A object stored in store B |

Value types per family: `Int` (`int32`), `Float` (`float`), `Name` (`FName`), `Bool` (`bool`).
Every `Read*` returns `(OutValue, ReturnValue: bool)` — the bool says "the key existed".

Bookkeeping/utility functions (`[FACT]`): `AddToChangedSet(WorldContextObject, ActorName)`,
`AddMeToChangedSet`, `AddThemToChangedSet(They)`, `IsInChangedSet(WorldContextObject, ActorName) -> bool`,
`AmIChanged`, `AreTheyChanged(They)`, `RemoveFromChangedSet(WorldContextObject, ActorName)`,
`RemoveMeFromChangedSet`, `RemoveThemFromChangedSet`, `ResetAllActorInts(WorldContextObject)`,
`GetSpartaActorSaveObject(WorldContextObject, SaveClass) -> USpartaSaveObject`,
`GetPlayerUniqueNameID() -> FName`, `GetPlayerDeathCounterName() -> FName`,
`GetPlayerSaveObjectFromSave(WorldContextObject) -> UObject`, `TempTest(WorldContext) -> bool`.

`[FACT]` Full 90-row table: `mine\topic-03-storeB.funcs.tsv` (columns: declaring class, class,
function, params, nparams, flags, dump_line).

---

## 6. Store C — data layers (dungeon layout / streaming state)

`[FACT]`: `USpartaDataLayerSubsystem` (super `WorldSubsystem`)

| Member | Type | Offset |
|---|---|---|
| `RuntimeData` | `UDataLayersSave*` | `0x38` |

| Function | Signature |
|---|---|
| `SaveDataLayerState` | `(DataLayer: UDataLayerAsset, TargetState: EDataLayerRuntimeState)` |
| `RemoveDataLayerState` | `(DataLayer: UDataLayerAsset)` |

`[FACT]` `UDataLayersSave.SavedDataLayerStates` is at `0xB8`.
`[FACT]` `EDataLayerRuntimeState` (Engine): `Unloaded = 0`, `Loaded = 1`, `Activated = 2`.
`[FACT]` Related universe: `USpartaDataLayerSubsystem`, `UDataLayerFunctionLibrary`
(`/Script/Sparta.DataLayerFunctionLibrary`), and the world-gen callback
`USpartaWorldGenSubsystem.OnDataLayerRuntimeStateChanged(DataLayer, State)` (§17).
`[FACT]` The engine's own `EDataLayerState` has the same three values — do not confuse the two enums;
the subsystem writes the *runtime* one.
`[FACT]` The mod already treats data layers as host-authoritative (`wd` wire field, 2 applies/tick,
10 s per layer, 15 000 uu gate — `main.lua:3083-3088`).

---

## 7. Store D — the live body (this is the biggest pure gap)

### 7.1 The attribute byte map — read these bytes

`[FACT]` Every gameplay attribute is a `FGameplayAttributeData` struct, and that struct is
**16 bytes**: `float BaseValue @+0x8`, `float CurrentValue @+0xC` (proof: `CXXHeaderDump\GameplayAbilities.hpp:295`,
cross-checked against Dumper-7 which emits the member as `0x10` bytes wide). So for a member at
offset `O`, the **live value** is a float at `O + 0xC` and the base value is at `O + 0x8`.
`[DERIVED]` from those two cited facts.

`USpartaHealthSet` (super `SpartaAttributeSet`):

| Attribute | Member offset | CurrentValue read at | Meaning for co-op |
|---|---|---|---|
| `Health` | `0x60` | `0x6C` | dark-form / harbinger health |
| `MaxHealth` | `0x70` | `0x7C` | |
| `ShellHealth` | `0x80` | `0x8C` | **shell** health (the bar that matters when shelled) |
| `MaxShellHealth` | `0x90` | `0x9C` | |
| `MinHealth` | `0xA0` | `0xAC` | |
| `MinShellHealth` | `0xB0` | `0xBC` | |
| `Armor` | `0xC0` | `0xCC` | |
| `Resolve` | `0xD0` | `0xDC` | resolve / healing-charge resource |
| `MaxResolve` | `0xE0` | `0xEC` | |
| `Poise` | `0xF0` | `0xFC` | |
| `MaxPoise` | `0x100` | `0x10C` | |
| `BreakResistance` | `0x210` | `0x21C` | |
| `MaxBreakResistance` | `0x220` | `0x22C` | |
| `StoneStunTime` | `0x240` | `0x24C` | stone-form / harden stun |
| `ResolveModify` | `0x298` | `0x2A4` | |

(Full 36-attribute list with offsets: `mine\topic-05-body.props.tsv`.)

`USpartaPlayerAttributeSet` — the player-only attributes:

| Attribute | Offset | Read at |
|---|---|---|
| `CurrentEXP` | `0x30` | `0x3C` |
| `CriticalChance` / `CriticalBonus` | `0x50` / `0x60` | `0x5C` / `0x6C` |
| `BaseResolveGain` / `BaseResolveLost` | `0xA0` / `0xB0` | `0xAC` / `0xBC` |
| `HealCurrentCharges` | `0xE0` | `0xEC` |
| `HealMaxCharges` | `0xF0` | `0xFC` |
| `HealAmountPerCharge` | `0x100` | `0x10C` |
| `GloomMultiplier` / `GoldMultiplier` | `0x110` / `0x130` | `0x11C` / `0x13C` |
| `ShellReviveCurrentProgress` | `0x150` | `0x15C` |
| `ShellReviveMaxProgress` | `0x160` | `0x16C` |
| `ShellReviveProgressGain` | `0x170` | `0x17C` |
| `PPItemResource` | `0x190` | `0x19C` |
| `PPItemMaxResource` | `0x1A0` | `0x1AC` |

`USpartaSidearmAttributeSet`: `CurrentAmmo @0x30` (read `0x3C`), `MaxAmmo @0x40` (`0x4C`),
`ReloadDuration @0x50`, `ActivationCost @0x60`.
`USpartaDarkPowerAttributeSet`: same four members at the same offsets (`0x30`/`0x40`/`0x50`/`0x60`).
`USpartaShellAbilityAttributeSet`: `Stacks @0xD0`, `CurrentAmount @0xC0`, `Cooldown @0x60`,
`HealingMeter @0x180`, `MaxHealingMeter @0x190`, `BreakDamage @0x1E0`.
`USpartaCombatSet`: `BaseDamage @0x30`, `BaseHeal @0x40`, `BasePoiseDamage @0x50`,
`BaseBreakDamage @0x60`, `AttackSpeedBonus @0x70`, `BaseDamageReduction @0x320`,
`RiposteDamageMultiplier @0x310`.

### 7.2 `USpartaHealthComponent` — the getters you should call instead of reading bytes

`[FACT]` Members: `AbilitySystemComponent @0x118`, `HealthSet @0x120`, `DeathState @0x128`
(`ESpartaDeathState`), `LastDamageTimeMap @0x130`, delegates `OnDamageReceived @0xB8`,
`OnHealingReceived @0xC8`, `OnMaxHealthChanged @0xD8`, `OnShellHealthDepleted @0xE8`,
`OnDeathStarted @0xF8`, `OnDeathFinished @0x108`.
`[FACT]` Getters (BlueprintPure, no side effects): `GetHealth()`, `GetHealthNormalized()`,
`GetCurrentHealthNormalized()`, `GetShellHealth()`, `GetShellHealthNormalized()`, `GetMaxHealth()`,
`GetMaxShellHealth()`, `GetDeathState()`, `IsDeadOrDying()`, plus
`FindHealthComponent(Actor) -> USpartaHealthComponent` (static-ish) and `StartDeath()`.
`[FACT]` `ESpartaDeathState`: `NotDead = 0`, `DeathStarted = 1`, `DeathFinished = 2`.

**Why the enum matters for sync**: `[FACT]` `DeathState` has a paired
`OnRep_DeathState(OldDeathState: ESpartaDeathState)` (`ObjectDump_allassets.txt:60683`). The build has
no netcode at all (established in earlier rounds: zero replicated properties, zero `Server*`/`Multicast*`
UFunctions), so that Rep is dead code in the shipped game — but it documents the *intended* way to
transition death state, and for co-op "set the enum and let the transition code run" is more faithful
than animating a death montage by hand (which is what the mod does today via `mseq`).

### 7.3 Where the body state hangs off the pawn

`[FACT]` `USpartaCharacter` gives every access point you need without a `FindAllOf`:

| Member | Type | Offset |
|---|---|---|
| `HealthComponent` | `USpartaHealthComponent*` | `0x6A0` |
| `ItemManagerComponent` | `USpartaItemManagerComponent*` | `0x6B0` |
| `WeaponsComponent` | `UWeaponsComponent*` | `0x6B8` |
| `ArmorComponent` | `UArmorComponent*` | `0x6E0` |
| `CharacterId` | `FGameplayTag` | `0x6E8` |
| `AbilitySystemComponent` | `USpartaAbilitySystemComponent*` | `0xAA8` |
| `Team` | `FGameplayTag` | `0xAB0` |
| `CharacterData` | `USpartaCharacterData*` | `0xAC0` |
| `CurrentTarget` | `AActor*` | `0xAC8` |
| `CharacterDeath` / `CharacterHit` / `CharacterAttack` / `CharacterDodge` / `CharacterRoll` / `CharacterTeleport` | delegates | `0x9A0` / `0x990` / `0x980` / `0x960` / `0x970` / `0x9B0` |
| `OnHitApplied` / `OnHitReceived` | delegates | `0x8D8` / `0x8E8` |

`USpartaPlayerCharacter` adds `PlayerExpComponent @0xAF0`, `TarstoneComponent @0xAF8`,
`ArtifactComponent @0xB00`, `LockOnComponent @0xAE8`, `SidearmProjectileCacheComponent @0xB08`,
`DarkformClass @0xB30`, `ShellClass @0xB38`, and `GetPlayerLevel()`.

### 7.4 Harden / stone form is *not* a field

`[FACT]` It is a Gameplay Ability: `GA_Harden_Original_C` with `CanActivateStoneForm`, plus the
post-process actor `BP_Harden_PP_C` (`HandleStoneFormParameters(Alpha, DistortionDistanceAlpha, WhiteFlash)`)
— proof lines: objects `/Game/Sparta/Core/Characters/Player/Common/Abilities/StoneForm/GA_Harden_Original.GA_Harden_Original_C`
and `…/StoneForm/BP_Harden_PP.BP_Harden_PP_C`. The only *numeric* trace in the attribute sets is
`SpartaHealthSet.StoneStunTime @0x240`.
`[UNKNOWN]` whether harden state is observable as an owned gameplay tag on the ASC; the probe is in
§25 (probe P4). Until that is answered, harden must be inferred client-side from the ability being
active, which is fragile — this is why the mod currently does not sync it.

### 7.5 Movement and animation state (what the existing wire approximates by hand)

`[FACT]` `USpartaCharacterMovementComponent` (super `CSCharacterMovementComponent`):
`BlockedMovementTags: FGameplayTagContainer @0x1160`, `VelocityTracker: CSTargetVelocityTracker @0x10C8`,
`RotationMethod: ESpartaCharacterRotationMethod @0x1094`,
`AccelerationInputScaling @0x1039`, `AccelerationInputSmoothing @0x103A`,
`AccelerationInputSmoothingFactor @0x1090`, `AccelerationInputSpring @0x1040`,
`RotationSpring @0x1098`, `RunCollisionChecksWhenIdle @0x1038`, `NavWalkingSizeOnScreen @0x1180`.
Functions: `SetMovementSpeedType(Type: EMovementSpeedType)`,
`SetMovementSpeedTypeOverride(Type)`, `RemoveMovementSpeedTypeOverride(Type)`,
`SetMaxSpeedAdjustment(ID: FGameplayTag, Speed: float, DirectionalSpeedModifierCurve)`,
`RemoveMaxSpeedAdjustment(ID)`, `HasMaxSpeedAdjustment() -> bool`,
`SetRotationRateAdjustment(ID: FGameplayTag, RotationSettings)`,
`RemoveRotationRateAdjustment(ID)`, `SetOverrideManualUserRotation(NewRotation: FRotator)`,
`HasNavFloor() -> bool`, `RemoveTickBlock(BlockName)`.
`[FACT]` `EMovementSpeedType`: `Walk = 0`, `Jog = 1`, `Sprint = 2`.
`[FACT]` `ESpartaCharacterRotationMethod`: `RotationRate 0, Spring 1, VelocityTracker 2`.

`[FACT]` `USpartaAnimInstance` (super `AnimInstance`) carries
`GameplayTagPropertyMap: FGameplayTagBlueprintPropertyMap @0x3F0` — the GAS **tag → animated property**
bridge. Its payload is `PropertyMappings: TArray<FGameplayTagBlueprintPropertyMapping> @0x10`, and each
mapping is `TagToMap: FGameplayTag @0x00`, `PropertyToEdit: TFieldPath @0x08`,
`PropertyName: FName @0x28`, `PropertyGuid: FGuid @0x30`.
Functions: `InitializeAnimationData(Data: UCharacterAnimationData)`, `RevertAnimationData(Data)`.

`[DERIVED]` That struct is why the current wire's `anim + mseq + mpos + mrate + spd` are a *manual*
re-implementation: the game already maps gameplay tags straight onto anim-BP properties, so replicating
the peer's owned tag set (plus `EMovementSpeedType` and `BlockedMovementTags`) would drive locomotion,
stun, harden and attack states natively and remove the need to replicate montage positions by hand.
`[UNKNOWN]` which concrete tags the shipped anim-BPs map — needs probe P2 (tag names) plus a one-off
dump of each `USpartaAnimInstance` subclass' `GameplayTagPropertyMap`.

---

## 8. Store S — the session save object (`BPSO_PlayerSession_C`)

`[FACT]` It is a `USpartaSessionSaveObjectBase` subclass, therefore session-scoped
(`bIsSessionSaveData`), and it mirrors the body state to disk so a reload restores you mid-run.
Members (all confirmed at `ObjectDump_allassets.txt`, class `/Game/Sparta/SaveObjects/BPSO_PlayerSession.BPSO_PlayerSession_C`):

| Member | Type | Offset |
|---|---|---|
| `Resolve` | double | `0xC0` |
| `Health` | double | `0xC8` |
| `ShellHealth` | double | `0xD0` |
| `HealCurrentCharges` | double | `0xD8` |
| `ShellReviveProgress` | float | `0xE0` |
| `ShellReviveMaxProgress` | float | `0xE4` |
| `StartTransform` | `FTransform` | `0xF0` |
| `PositionCheckFrequency` | float | `0x150` |
| `PlayerPawnCache` | `APawn*` | `0x160` |
| `IsInDarkform` | bool | `0x168` |
| `AbilitySystemComponent` | `ASC*` | `0x170` |

Functions: `SaveHealth()`, `SaveResolve()`, `SaveShellHealth()`, `SaveHealingCharges()`,
`SaveShellRevive()`, `SaveIsDarkForm()`, `GetSessionStartTransform() -> FTransform`,
`ReceiveSpartaPreSave(SaveContext)`, `ReceiveOnLoaded()`, `CheckForValidTransform()`.

**Sync relevance `[FACT]`+`[DERIVED]`**: the six `Save*()` functions are the game's own
"copy from the live pawn into the session object" path. A co-op implementation can therefore read the
peer's *persisted* body state from the session object (cheap, plain fields) rather than poking GAS
attributes, and can write the local session object the same way. `StartTransform` is also the
respawn point — needed for the death/respawn flow (§17.2).

`[FACT]` `BPSO_PlayerSession_C` is a **session** object; `USpartaSaveSubSystem.SessionSaveObject (0x120)`
is the loaded one and `RuntimeSessionSaveObject (0x128)` the in-memory one. `GetSessionSave()` returns
the former; `HasValidSessionSave()` tells you whether a session exists at all.

---

## 9. Store P — the progression keystone `BP_SpartaPlayerSaveObject_C`

`[FACT]` One object, **73 fields**, subclass of `USpartaPlayerSaveObject`. If you sync this object
correctly and nothing else, progression is synced. Reached via
`BP_PlayerController_C.PlayerSaveGameObject @0x8E0` or
`USpartaSaveSubSystem.GetPlayerSaveObjectFromSlotIndex(SlotIndex)`.

### 9.1 Currencies, counters, flags

| Field | Type | Offset | Note |
|---|---|---|---|
| `CumulativeGloom` | int32 | `0x430` | only *cumulative* counters live here — the spendable balance is an attribute (§7.1 `GloomMultiplier`) or a store-B int |
| `CumulativeGold` | int32 | `0x434` | |
| `CumulativeGlimpses` | int32 | `0x520` | |
| `TotalDeaths` | int32 | `0x524` | |
| `TotalBossesDefeated` | int32 | `0x528` | |
| `FinishedPrologue` | bool | `0x4B9` | gates the whole post-prologue flow |
| `SkippedPrologue` | bool | `0x9B8` | |
| `PastTarGolemFight` | bool | `0x4B8` | |
| `NewGamePlus` | int32 | `0x798` | |
| `SlayerSealLockout` | bool | `0x6A0` | |
| `StoryCompletionActivityStarted` / `…Completed` | bool | `0x6A1` / `0x6A2` | |
| `CurrentGoodSheepAchievement` | int32 | `0x9BC` | |
| `CurrentHubEventPoints` / `TotalHubEventPoints` | double | `0x530` / `0x538` | |
| `GiftEventPointBank` | double | `0x540` | |
| `CurrentGiftEventLevel` | int32 | `0x548` | |
| `CurrentSpawnLocation` | `FText` | `0x8A0` | **the spawn-location id** — needed for respawn parity |
| `CurrentPrologueCheckpoint` | `FTransform` | `0x4C0` | |

### 9.2 Equipment — six active item definitions + one unlock map

| Field | Type | Offset |
|---|---|---|
| `ActiveDarkFormItemDefinition` | class | `0x438` |
| `ActiveShellItemDefinition` | class | `0x440` |
| `ActiveSealItemDefinition` | class | `0x448` |
| `ActiveWeaponItemDefinition` | class | `0x450` |
| `ActiveSidearmItemDefinition` | class | `0x458` |
| `ActiveChargesItemDefinition` | class | `0x460` |
| `EquipmentUnlockState` | `TMap<FGameplayTag, bool>` | `0x468` |
| `EquipmentToUnlockOnSkipPrologue` | array | `0x9A8` |
| `SaveObjectsToResetForNGP` | array | `0x890` |

Setters `[FACT]`: `SetActiveShell(ID: class)`, `SetActiveWeapon(ID: class)`, `SetActiveSidearm(ID: class)`,
`SetActiveSeal(ID: class)`, `SetActiveDarkForm(ID: class)`, `SetActiveCharges(ID: class)`,
`AddUnlockedEquipment(ID: FGameplayTag, Unlocked: bool) -> bool`,
`GetEquipmentUnlockState(ID: FGameplayTag) -> bool`.

### 9.3 Skins, masks, clothing — and the **real equipment writer API**

The save fields are the *state*; the writes go through `BPFL_Player_Equipment_C`
(`/Game/Sparta/Core/Player/BPFL_Player_Equipment.BPFL_Player_Equipment_C`, super
`/Script/Engine.BlueprintFunctionLibrary` → it has a `Default__…` CDO, see §25 P5). **71 functions**,
and the interesting ones take a **GameplayTag id**, a `Save` bool and a `__WorldContext` object:

| Function | Verbatim parameters |
|---|---|
| `ChangeShell` | `(ShellId: FGameplayTag, Save: bool, __WorldContext: object, Item: class, SaveObject: BP_SpartaPlayerSaveObject_C*)` |
| `ChangeWeapon` | `(WeaponID: FGameplayTag, Save: bool, __WorldContext: object, Item: class)` |
| `ChangeSidearm` | `(SidearmID: FGameplayTag, Save: bool, __WorldContext: object, Item: class)` |
| `ChangeSeal` | `(SealID: FGameplayTag, Save: bool, __WorldContext: object, Item: class)` |
| `ChangeDarkForm` | `(DarkFormID: FGameplayTag, Save: bool, __WorldContext: object, SaveObject: BP_SpartaPlayerSaveObject_C*, Item: class)` |
| `ChangePrimaryCharges` | `(ChargesID: FGameplayTag, ActivateID: bool, Save: bool, __WorldContext: object, SoftItem: SoftClass, LoadedItem: class)` |
| `ChangeSecondaryCharges` | same shape as `ChangePrimaryCharges` |
| `UnlockShell` / `UnlockWeapon` / `UnlockSidearm` / `UnlockSeal` | `(Id: FGameplayTag, Save: bool, __WorldContext: object)` |
| `UnlockSkin` / `UnlockMask` / `UnlockClothing` | `(…Id: FGameplayTag, Save: bool, __WorldContext)` |
| `ChangeShellSkin` / `ChangeDarkFormSkin` | `(SkinID: FGameplayTag, Save: bool, __WorldContext)` |
| `ChangeShellClothing` / `ChangeDarkFormClothing` / `ChangeShellMask` / `ChangeDarkFormMask` | `(CharacterId: FGameplayTag, …ID: FGameplayTag, Save: bool, __WorldContext, SaveObject)` |
| `GetShellItemDefinition` | `(ShellId: FGameplayTag, SkinID: FGameplayTag, __WorldContext) -> SoftClass` |
| `GetWeaponItemDefinition` / `GetSidearmItemDefinition` / `GetSealItemDefinition` / `GetDarkFormItemDefinition` / `GetChargesItemDefinition` | `(…Id: FGameplayTag, __WorldContext) -> SoftClass` |
| `IsEquipmentUnlocked` | `(ID: FGameplayTag, __WorldContext) -> bool` |
| `IsInShell` / `IsSevered` | `(__WorldContext) -> bool` |
| `ActivateDarkForm` | `(__WorldContext) -> (Success: bool)` |
| `StowAndPutInHand` / `TryRestoreWeapon` / `TryRestoreSidearm` / `TryRemoveWeapon` / `TryRemoveSidearm` / `SwapShellMaterials` / `SwapDarkFormMaterials` / `DebugEquipmentCheck` | see `mine\topic-17-bplibs.funcs.tsv` |

`[DERIVED]` This changes the equipment story completely: `[FACT]` equipment identity in this game is a
**GameplayTag**, not a class path, and `Change*` performs the whole switch (mesh, materials, ability
grant, save write) in one call. So the current "visual-only look sync" can become real equipment
sync with five tag writes — *provided* P5 succeeds and the tag names are known (P2).

The save fields these functions read/write:

| Field | Type | Offset |
|---|---|---|
| `ActiveDarkFormSkinID` | map | `0x7A0` |
| `ActiveShellSkins` | map | `0x7F0` |
| `UnlockedSkins` | map | `0x840` |
| `ActiveDarkFormMask` | map | `0x8B0` |
| `ActiveShellMask` | map | `0x900` |
| `UnlockedMasks` | map | `0x950` |
| `ActiveDarkFormClothing` | map | `0x9C0` |
| `ActiveShellClothing` | map | `0xA10` |
| `UnlockedClothing` | map | `0xA60` |

### 9.4 World progression

| Field | Type | Offset | Note |
|---|---|---|---|
| `UnlockedLandingAreas` | array of `FName` | `0x550` | fast-travel nodes |
| `CleansedLandingAreas` | array of `FName` | `0x5A0` | |
| `CinematicsWatched` | `FGameplayTagContainer` (32 bytes) | `0x5B0` | the cinematic gate — see §18 |
| `ShownTutorials` | array | `0x640` | |
| `EarnedAchievements` | `TMap<FString, bool>` | `0x650` | |
| `SurpriseEggsFound` | map | `0x5D0` | |
| `SoftItemStatus` | array | `0x620` | |
| `AttentionGrabberStatus` | array | `0x630` | |
| `PlayedLuteTracks` | map | `0x6A8` | |
| `ProximaBiosampler` | map | `0x6F8` | |
| `GraguManiac` | set | `0x748` | |
| `bHasLostGloom` / `bLostGloomInsideObjective` | bool | `0x560` / `0x561` | the "lost gloom" recovery mechanic |
| `LostGloomObjectiveGUID` | `FString` | `0x568` | |
| `LostGloomAmount` | int32 | `0x578` | |
| `LostGloomLocation` | `FVector` | `0x580` | |
| `LostGloomBiome` | `FGameplayTag` | `0x598` | |

Functions: `IsLandingAreaUnlocked(ID)`, `UpdateUnlockedLandingAreas(ID)`,
`IsLandingAreaCleansed(ID)`, `UpdateCleansedLandingAreas(ID)`, `SetCinematicWatched(Tag)`,
`CheckIfCinematicWatched(Tag) -> bool`, `SetAchievementEarned(AchievementID)`,
`IsAchievementEarned(AchievementID) -> bool`, `SetLuteTrackPlayed(IndexPlayed)`,
`IncrementTotalDeaths()`, `IncrementCumulativeCurrency(CurrencyClass, Amount)`,
`ResetForNewGamePlus()`.

### 9.5 The native base `USpartaPlayerSaveObject` (stats, difficulty, world-gen tags)

| Field | Type | Offset |
|---|---|---|
| `TotalPlayTimeSeconds` | float | `0xB8` |
| `LastCachedCurrentTime` | double | `0xC0` |
| `LastSaveDateTime` | `FDateTime` | `0xC8` |
| `CompletedSpawnTriggerRecipes` | map | `0xD0` |
| `CompletetedSpawnTriggers` | set | `0x120` |
| `ShellStats` | map | `0x170` |
| `WeaponStats` | map | `0x1C0` |
| `PlayerStats` | `FPermanentStats` | `0x210` |
| `GameLibraryData` | map | `0x260` |
| `OverrideWorldGenTags` | `FGameplayTagContainer` | `0x2B0` |
| `Difficulty` | map | `0x2D0` |

`FPermanentStats` is a single `TMap<FGameplayTag, int32>` at `@+0x00` (size `0x50`) — proof
`Sparta_structs.hpp`, struct `FPermanentStats`.

Functions: `GetPlayerStat(StatTag) -> int`, `IncreasePlayerStat(WorldContextObject, StatTag, IncreaseBy)`,
`GetShellStat(ShellTag, StatTag) -> int`, `IncreaseShellStat(WorldContextObject, ShellTag, StatTag, IncreaseBy)`,
`GetWeaponStat(WeaponTag, StatTag) -> int`, `IncreaseWeaponStat(…, WeaponTag, StatTag, IncreaseBy)`,
`ClearPlayerStats(WorldContextObject)`, `ClearWeaponStats(WorldContextObject, WeaponTag)`.

**This is the level-up surface**: per-shell and per-weapon stat levels are `TMap<tag, int>` — no
per-item bookkeeping needed.

---

## 10. The economy / progression write API — `S_*` on `BP_PlayerController_C`

`[FACT]` The `S_*` cheat/debug console functions are **not** on the native `ASpartaPlayerController`;
they live on the Blueprint subclass `/Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C`
(129 `S_*` functions). They are the only complete, tag-free, side-effect-free *writer* API in the
build, which makes them the natural write path for progression sync. Selected signatures, verbatim:

| Function | Parameters |
|---|---|
| `S_AddGold` / `S_AddGloom` / `S_AddGlimpses` / `S_AddOvums` / `S_AddTarcores` | `(Stacks: int)` |
| `S_AddVentrium` / `S_AddLaterite` / `S_AddDorsalite` / `S_AddThoracium` | `(Stacks: int)` |
| `S_AddThoraciumPrime` | `(Stacks: int)` |
| `S_AddShellPoints` | `(Delta: int)` |
| `S_UpdateBondingPoints` | `(Delta: int)` |
| `S_AddUpgrade` / `S_RemoveUpgrade` | `(ID: FName, UpgradeLevel: int)` |
| `S_AddWeaponLevel` / `S_AddSidearmLevel` | `(Delta: int)` |
| `S_AddItem` | `(RowName: FName, Item: class)` |
| `S_AddItemQuantity` | `(RowName: FName, Amount: int, Item: class)` |
| `S_AddAllItems` | `(Item: class)` |
| `S_UnlockShell` / `S_UnlockWeapon` / `S_UnlockSidearm` / `S_UnlockSeal` | `(NewValue: int, CurrentTag: FGameplayTag, Tags: TArray)` |
| `S_SwitchToShell` / `S_SwitchToWeapon` / `S_SwitchToSidearm` / `S_SwitchToSeal` | `(Name: FString)` (+ `L_…ID` array) |
| `S_UnlockAllShells` / `S_UnlockAllWeapons` / `S_UnlockAllSidearms` / `S_UnlockAllSeals` | `()` or `(Tag)` |
| `S_Heal` | `(Amount: double)` |
| `S_GainResolve` | `(Amount: double)` |
| `S_ModifyHealingCharge` | `(Amount: int)` |
| `S_ReviveShell` | `()` |
| `S_KillMe` | `()` |
| `S_DealDamage` | `(Damage: float)` |
| `S_SetDarkBroLevel` | `(NewValue: int)` |
| `S_CompletePrologue` | `()` |
| `S_IncrementMemoryProgress` | `()` |
| `S_StartMemory` / `S_EndActiveMemory` | `(RowName: FName)` / `()` |
| `S_AddNarrativeTag` / `S_RemoveNarrativeTag` | `(TagToAdd: FGameplayTag)` |
| `S_UnlockFastTravel` / `S_UnlockMap` | `()` |
| `S_UnlockAllLandingAreas` | `(FoundAreas: array)` |
| `S_ResetSaveObjectByGuid` | `(GuidString: FString)` |
| `S_ResetAllSpawnerSaveObjects` | `()` |
| `S_GiveTarforgeUnlockItems` | `(Save: bool, Items: map)` |
| `S_UnlockTarforgeModes` | `(Save: bool, Tags: FGameplayTagContainer)` |
| `S_RechargePPItem` | `()` |
| `S_TarstoneLevelIncrementall` / `S_AddAllTarstonesMelee` / `…Sidearm` / `…Support` | see `mine\topic-08-economy.funcs.tsv` |
| `S_ResetForNewGamePlus` / `S_PostGolemFightSettings` | `()` |

`[FACT]` `S_AddNarrativeTag` / `S_RemoveNarrativeTag` take a `FGameplayTag` **struct literal**, and
the mod already proves that shape works (`main.lua:3029-3030`) — so tag-keyed writes are viable
today; only `FGameplayTagContainer` (used by `S_UnlockTarforgeModes`) is unproven (§25 P6).
`[FACT]` Controller state pointers: `PlayerSaveGameObject @0x8E0`, `Progression @0x7F0`,
`Quest @0x830`, `Narrative @0x7D8`, `Achievements @0x7E0`, `PP @0x7E8`, `Teleport @0x7F8`,
`World @0x800`, `Shell @0x808`, `Hub @0x820`, `Combat @0x840`, `Gameplay @0x850`,
`Sparta (save component) @0x818`, `IsPlayerSaveLoaded @0x908`, `SelectedBiome @0x8F8`,
`SelectedSeason @0x900`.
`[FACT]` Active-item handles on the controller: `ActiveShellItemHandle @0x880`,
`ActiveWeaponItemHandle @0x898`, `ActiveSidearmItemHandle @0x8A8`,
`ActiveDarkformItemHandle @0x8C0`, `ActiveSealItemHandle @0x960`,
`PrimaryChargesItemInstance @0x978`, `SecondaryChargesItemInstance @0x990`.

---

## 11. Inventory and items

### 11.1 The inventory container

`[FACT]` `USpartaItemManagerComponent` (super `ActorComponent`) — **this is the inventory**:
`ItemList @0xD8` (inline `FSpartaItemList`), `OnItemStacksUpdated @0xC8`, `OnLootPickedUp @0xB8`.

`FSpartaItemList : FFastArraySerializer` → `Entries: TArray<FSpartaItemEntry> @0x108`,
`OwnerComponent @0x118`. Since the struct is inline at `0xD8`, **the entries array header lives at
`component + 0xD8 + 0x108 = +0x1E0`** `[DERIVED]`.
`FSpartaItemEntry : FFastArraySerializerItem` → `Instance: USpartaItemInstance* @0x10`,
`StackCount: int32 @0x18`, `LastObservedCount: int32 @0x1C` (size `0x20`).

Functions: `GetAllItems() -> TArray<USpartaItemInstance>`, `GetItemStacksByClass(ItemDef) -> int`,
`GetTotalItemCountByDefinition(ItemDef) -> int`, `FindFirstItemStackByClass(ItemDef) -> instance`,
`CanAddItemByClass(ItemDef, StackCount) -> bool`, `AddItemByClass(ItemDef, StackCount) -> instance`,
`AddItemInstance(instance)`, `RemoveItemStacksByClass(ItemDef, NumToRemove)`,
`RemoveItemStacksByInstance(instance, NumToRemove)`, `RemoveItemInstance(instance)`.

### 11.2 An item instance is fully self-describing

`[FACT]` `USpartaItemInstance` (super `Object`): `FragmentData: TMap @0x28`,
`StatTags: FGameplayTagStackContainer @0x78`, `ItemDef: class @0x1E0`.
Functions: `GetItemDef() -> class`, `FindFragmentByClass(FragmentClass) -> USpartaItemFragment`,
`AddStatTagStack(Tag, StackCount)`, `RemoveStatTagStack(Tag, StackCount)`,
`GetStatTagStackCount(Tag) -> int`, `HasStatTag(Tag) -> bool`.

`[DERIVED]` Consequence: `(ItemDef class path, StackCount, StatTags[], FragmentData)` is a complete
serialization of an inventory line — the item id space is **class paths**, not DataTable rows, which
is good news: class paths are strings both peers already have.

### 11.3 Item definitions and the fragment system

`[FACT]` `USpartaItemDefinition` has only `DisplayName: FText @0x28` and
`Fragments: TArray<USpartaItemFragment*> @0x38` — all behaviour lives in fragments. The fragments
that matter for sync are `ItemFragment_SetStats`, `ItemFragment_Tarstone`,
`ItemFragment_StackabilityRules`, `ItemFragment_StackableDivideSumByCountSquared`,
`ItemFragment_GiveItemToPlayer`, `ItemFragment_GiveItemToShell`,
`ItemFragment_WorldStateModifier`, `ItemFragment_ItemTag`, `ItemFragment_Executable`,
`ItemFragment_SetShellClass`, `ItemFragment_SetDarkformClass`, `ItemFragment_AbilitySets`.
`[FACT]` `USpartaItemFragment_StatLevels` exists separately (stat level tables).
`[FACT]` 393 Blueprint classes exist under `/Game/Sparta/Items/` (`mine\item-assets.tsv`) — these
are the concrete item definitions; the id space for items is those class paths.
`[FACT]` `UPlayerInventoryComponent` (`/Script/Sparta.PlayerInventoryComponent`) is **empty in
reflection** — no UPROPERTYs and no UFUNCTIONs (proof: `python scripts\mine.py show /Script/Sparta.PlayerInventoryComponent`
prints only the class line). `[UNKNOWN]` what it does internally; the usable inventory API is
`USpartaItemManagerComponent` (§11.1). Probe recorded as P7 in §25.

---

## 12. Progression sub-systems (the objects behind the save fields)

### 12.1 Experience / upgrade points — `UPlayerExpComponent`

`PlayerLevel @0x118`, `UpgradeLevels: map @0x120`, `OwnedUpgradeStats: FGameplayTagContainer @0xF8`,
`PlayerUpgrades: array @0xD8`, `PlayerMetaUpgrades: array @0xE8`, delegates
`OnCurrentEXPChanged @0xB8`, `OnUpgradeSelected @0xC8`, `PlayerLevelUpdated @0x1A0`.
Functions: `GetUpgradeLevel(Upgrade) -> int`, `SetUpgradeLevel(Upgrade, Level)`,
`AddUpgradeLevel(Upgrade)`, `HasUpgrade`, `HasAnyUpgrade`, `HasAllUpgrades`,
`IsUpgradeAtMaxLevel`, `GetAvailableUpgradePoints() -> int`, `RemoveUpgradeData(Upgrade)`.

### 12.2 Shell / tier skill builds — `UPlayerProgressionComponent` (a save component)

`ShellBuilds @0x120`, `TierBuilds @0x170`, `SkillLevels @0x1C0`,
`EquippedSkillInstances @0x210`; delegates `OnSkillEquipped @0x260`, `OnSkillUnequipped @0x270`,
`OnSkillLevelChanged @0x280`.
Functions: `GetSkillLevel(SkillClass) -> int`, `SetSkillLevel(SkillClass, NewLevel)`,
`AddToSkillLevel(SkillClass, Delta)`, `HasSkill`, `EquipSkill(ShellTag, SkillClass) -> instance`,
`UnequipSkill(ShellTag, SkillClass) -> instance`, `GetTierSkills(ShellTag)`, `AddTierSkill`,
`HasTierSkill`, `RefundAllSkills()`, `OnChangeShellItem(OldShell, NewShell)`.
`[FACT]` Its save object is `BPSO_Progression_C`, whose mirrored fields are
`CurrentLevel @0x1A8`, `AvailablePoints @0x1AC`, `AssignedPoints @0x1B0`, `BondingPoints @0x200`,
`GoldSpentAtShop @0x250`.

### 12.3 Tarstones — `UTarstoneComponent` (a save component)

`TarcoreItemDefinition @0x120`, `TarstoneBuilds @0x128`, `TarstoneLevels @0x178`,
`TarstoneSupportBuild @0x1C8`, `EquippedTarstoneItemInstances @0x1D8`,
`EquippedSupportTarstoneItemInstances @0x228`, plus 10 delegates from `0x278`.
Functions: `EquipTarstone(EquipmentSlot: tag, SlotIndex: int, TarstoneID: class) -> instance`,
`UnequipTarstone(EquipmentSlot, SlotIndex)`, `EquipSupportTarstone(SlotIndex, TarstoneID)`,
`UnequipSupportTarstone(SlotIndex)`, `AddTarstoneExp(Tarstone, exp)`,
`AddTarstoneLevel(Tarstone)`, `ForceAddTarstoneLevel(Tarstone)`,
`AddTarstoneToInventory(Tarstone, Level, exp)`, `SetTarstoneLevel(Tarstone, LevelData)`,
`GetCurrentTarstoneBuildForSlot(Slot)`, `CanLevelUpTarstone(Tarstone) -> bool`,
`DetermineExpRequirement(Level, TarstoneExperience) -> int`.
`[FACT]` Its save object is `TarstoneSaveData`/`BPSO_*` mirror: `TarstoneLevels`,
`TarstoneBuilds`, `TarstoneSupportBuild`.

### 12.4 Passive / active PP items — `UPPItemsComponent` (a save component)

`ActivePPItem: SoftClass @0x120`, `PassivePPItem: SoftClass @0x148`, `PPItemLevels @0x170`,
`PPItemFoundAmounts @0x1C0`, `PPItemFragmentDataBySlot @0x210`, `TrackedPPItems @0x260`,
`bIsTrackingPPItemStacks @0x270`, 5 delegates from `0x278`.
Functions: `EquipPPItem(Slot: EPPItemSlot, ItemClass: SoftClass) -> PPItemFragmentData`,
`UnequipPPItem(Slot)`, `UnequipPPItemByClass(ItemClass)`, `UnequipAllPPItems()`,
`GetPPItemLevel(ItemClass) -> int`, `SetPPItemLevel(ItemClass, NewLevel)`,
`AddToPPItemLevel(ItemClass, Delta)`, `GetPPItemFoundAmount(ItemClass) -> int`,
`AddToPPItemFoundAmount(ItemClass, Delta)`, `GetEquippedPPItem(Slot) -> SoftClass`,
`GetPPItemFragmentData(Slot)`, `GetRuntimeFragmentData(Slot)`,
`GetPPItemsNeededForNextLevel(ItemClass) -> int`, `FindPPItemMaximumLevel(ItemClass) -> int`.
`[FACT]` `EPPItemSlot`: `Active = 0`, `Passive = 1`.

### 12.5 Weapons — `UWeaponsComponent`

`EquippedWeapons: map @0xF8`, `DefaultWeapons @0xB8`, delegates `OnWeaponEquipped @0xC8`,
`OnWeaponUnequipped @0xD8`, `OnDefaultWeaponsSpawned @0xE8`.
Functions: `GetWeaponInSlot(WeaponSlot: tag) -> USpartaWeapon`, `GetWeaponInHand()`,
`GetAllWeapons()`, `SpawnAndEquipWeaponOfClass(WeaponClass) -> weapon`, `EquipWeaponToSlot(weapon, bGrantAbilities)`,
`RemoveAllWeapons()`, `RemoveWeaponFromSlot(slot)`, `HideAllWeapons(bHide)`.
`[FACT]` Weapon levels are **not** here — they are in `UPlayerSaveObject.WeaponStats`
(`TMap<tag,int>`) and written by `IncreaseWeaponStat`/`S_AddWeaponLevel`.

### 12.6 Shell memories — `BPSO_ShellMemories_C`

`MemoryProgress: map @0xB8`, `MemoriesWatched: set @0x108`.
`[FACT]` Writer functions: `S_StartMemory(RowName)`, `S_IncrementMemoryProgress()`, `S_EndActiveMemory()`
(§10), and the handler component `BPC_ShellMemoryHandler_C` on the controller (`Shell @0x808`).

---

## 13. Quests, narrative events, completion

| Object | Field | Type | Offset |
|---|---|---|---|
| `UQuestObject` | `QuestID` | FName | `0x28` |
| | `CurrentQuestStage` | int32 | `0x30` |
| | `CurrentQuestObjectiveProgress` | map | `0x38` |
| | `CurrentStageDetails` | `FQuestStage` | `0x88` |
| | `QuestDetails` | `FQuestDetails` | `0x180` |
| `BPSO_QuestLogComponent_C` | `NarrativeTags` | `FGameplayTagContainer` | `0xC0` |
| `BPSO_NarrativeEventsTracker_C` | `NarrativeEventTimeTracking` | map | `0xB8` |
| `BPSO_WorldMap_C` | `Trackers`, `ObjectiveGUID`, `IsInsideObjective`, `LastPlayerLocation` | | |
| `BPSO_WordMapObjective_C` | `Visible`, `Revealed`, `Completed`, `ShowArea`, `PlayedRevealScript` | | |

`[FACT]` `UQuestObject.InitializeQuest()` is the only reflected function.
`[FACT]` `USpartaCompletionSubsystem` (GameInstanceSubsystem) keeps
`Snapshot: FSpartaCompletionResult @0x60`, `RunTimeSeconds @0x88`, `bRunTimerPaused @0x8C`,
`CompletionSettings @0x30`; API `CalculateCompletion(WorldContextObject) -> result`,
`SnapshotCompletion`, `GetSnapshot`, `SetRunTimeSeconds`, `SetRunTimerPaused`, `HasSnapshot`,
`ClearSnapshot`, `ApplySurvivalCurve(OvaFraction, CapabilityFraction) -> float`.
`[FACT]` Narrative write path is the tag API: `S_AddNarrativeTag(Tag)` / `S_RemoveNarrativeTag(Tag)`
(§10) and the save-object container at `BPSO_QuestLogComponent_C.NarrativeTags +0xC0`, whose layout is
`TArray<FGameplayTag> GameplayTags @+0x0 (0x10)`, `TArray<FGameplayTag> ParentTags @+0x10 (0x10)`.
Both are exactly what a `[PROPOSAL]` diff-based tag sync needs: read the array of 8-byte FNames,
diff, add/remove by literal.

### 13.5 The live quest state is far richer than its save object

`[FACT]` `BPC_QuestLogComponent_C` (`/Game/Sparta/Dialogue/QuestSystem/…`, super **`SpartaSaveComponent`**
— so it both holds live state and owns a save object) carries everything a quest sync needs:

| Member | Type | Offset |
|---|---|---|
| `CurrentActiveQuests` | array | `0x128` |
| `CompletedQuests` | array | `0x138` |
| `CurrentTrackedQuest` | FName | `0x148` |
| `ActiveQuests` | array | `0x150` |
| `NPCRelationships` | map | `0x170` |
| `PlayerChoice` | `FGameplayTag` | `0x1D0` |
| `NarrativeEvents` | map | `0x1F8` |
| `NarrativeTags` | `FGameplayTagContainer` | `0x258` |
| `BlisterBoiDonations` | int32 | `0x288` |
| `ImmortalBrigSpoils` | map | `0x290` |
| `OnQuestStarted` / `OnQuestCompleted` / `OnNarrativeTagsLoaded` | delegates | `0x160` / `0x1C0` / `0x278` |

`[DERIVED]` Compare with its save object (only `NarrativeTags @0xC0`): the *persisted* form is a
subset, so **reading the save object is not enough** — `CurrentActiveQuests`, `CompletedQuests`,
`CurrentTrackedQuest`, `NPCRelationships` and `PlayerChoice` exist only on the component.

`[FACT]` `BPC_NarrativeEventsTracker_C` (super `SpartaSaveComponent`): `MySaveContext @0x128`,
`CachedEventsTimeTracker: map @0x158`, `OnTrackerRefresh @0x1B0`;
functions `FindEventTrackedTime(EventTag: FGameplayTag) -> double`,
`AddEventTimeTracker(EventTag)`, `RemoveEventTimeTracker(EventTag)`,
`GetMySaveData() -> BPSO_NarrativeEventsTracker_C`, `InitEventsTimeTracker(EventData, EventKey)`.

`[FACT]` `BPC_HubEventComponent_C` (super `HubEventComponent`): `EventPointsPerEnemyKill @0xC8`,
`GiftEventPointBank @0xD0`, `GiftEventDefaultThreshold @0xD8`, `CurrentGiftEventLevel @0xE0`,
`GiftEventLeaderTable: DataTable @0xE8`, `GiftsPickedUp: map @0xF0`, `WasInitialized @0x150`,
`OvumsToOpenFinalLevel @0x154`, `OvumStairsEventBroadcasted @0x158`, `StartShellMemory @0x15C`;
functions `UpdateOvumsCollected(OvumsToAdd)`, `LoadOvumsCollectedFromSave()`,
`SaveHubData(L_Players)`, `OnSkyDive(EventData, L_PlayerSaveObject)`.

`[FACT]` `BPC_Achievements_C`: `SlayerSealLockout @0xC0`, `AchievementUpdatesQueue: map @0xC8`,
`IsUnlockingAchievements @0x118`, `CurrentAchievementName: FString @0x120`,
`CurrentAchievementProgress: F_AchievementProgress @0x130`; plus
`UpdateSlayerSealLockoutFromSave()` and the platform `WriteAchievement` callbacks
(`OnSuccess_…` / `OnFailure_…` with `WrittenAchievementName, WrittenProgress, WrittenUserTag`).
`[DERIVED]` Achievements are written per-platform by each peer independently; the co-op-relevant state
is `BP_SpartaPlayerSaveObject_C.EarnedAchievements @0x650` plus `SlayerSealLockout`, not the queue.

---

## 14. Enemies, spawners, encounters, drops

### 14.1 Spawners — the "did this enemy spawn already" state

`[FACT]` `UUnitSpawner` (super `Actor`): `SaveComponent @0x2A8` (`USpartaSaveComponent`),
`bShouldLoadEnemyClassesOnBeginPlay @0x2B0`, `OnCharacterSpawned @0x2B8`,
`EnemySpawnType: FDataTableRowHandle @0x2C8`, `SpawnerTags: FGameplayTagContainer @0x2D8`,
`LooseGameplayTagsToAdd @0x2F8`, `SpawnedEnemy @0x318`, `CharacterId: FGameplayTag @0x320`,
`ClassReference @0x328`.
Functions: `SpawnMe(SpawnTrigger)`, `CanSpawn() -> bool`,
`BeginSpawningEnemy(EnemyClass, Transform) -> USpartaCharacter`,
`FinishSpawningEnemy(EnemyClass, Transform) -> USpartaCharacter`, `SpawnerLoaded()`,
`LoadClasses()`, `ClearLoadedClasses()`, `CharacterSpawned()`.
`[FACT]` `USpartaHordeSpawner` adds `SpawnClass: SoftClass @0x340` and `Spawn() -> Character`.

`[FACT]` The persisted spawner state is two different objects:
`BPSO_SpawnerSaveObj_C.WasEverKilled @0xB8` (global) and
`BPSO_SpawnerSessionSaveObject_C` with `WasKilled @0xC0`, `DidSpawn @0xD0`, `AttemptedSpawn @0xD2`,
`SpawnerOwner @0xC8`, `Spawner @0xE0`, `SpawnedClass @0xE8` (session-scoped).
`[DERIVED]` So "the peer killed this spawn" needs *both* classes, and the session one must be
written per-session, not per-save.

### 14.2 Live enemy bookkeeping

`[FACT]` `USpartaEnemySubsystem` (`TickableWorldSubsystem`): `SpawnedEnemies @0xE8`,
`EngagedEnemies @0x48` (set), `AggressorRanking @0x370`, `TopAggressor @0x3C0`,
`OnCharacterDeathEvent @0xD8`, `OnCharacterEngageStateChanged_BP @0x158`,
`OnScreenCheckMethod @0x40`, `PointCheckRadius @0x44`.
Functions: `GetSpawnedEnemiesInDist(Center, Dist) -> array`, `OnCharacterDeath(OwningActor)`,
`OnCharacterAddedToWorld(Character)`.

`[FACT]` `USpartaEncounterManager` (Actor): `AssociatedEnemies @0x2B8`, `HordeEncounters @0x2C8`,
`SpawnedEncounters @0x2D8`, `bStartSpawnedOnAllDead @0x2E8`, `OnEncounterStart @0x2F0`,
`EncounterStartDistance @0x300`, `OnEncounterCompleted @0x308`;
functions `OnEnemyDeath(DeadActor)`, `OnHordeEncounterCompleted(CompletedHorde)`.

### 14.3 Drop / death payload

`[FACT]` `FSpartaCharacterDeathData` (in `USpartaCharacterData.DeathResources @0x358`):

| Field | Type | Offset | Size |
|---|---|---|---|
| `GloomOnDeath` | int32 | `0x00` | `0x04` |
| `GoldOnDeath` | int32 | `0x04` | `0x04` |
| `GoldDropChance` | float | `0x08` | `0x04` |
| `ItemDropTable` | `TMap<TSubclassOf<USpartaItemDefinition>, float>` | `0x10` | `0x50` |

`[FACT]` `USpartaCharacterData`: `CharacterId @0x40`, `bIsBoss @0x48`, `bIsMainBoss @0x49`,
`bShouldTrackDeath @0x4A`, `DeathResources @0x358`, `UpgradeStatValues @0x108`,
`CharacterCurves @0x158`, `DefaultTags @0x50`, `GrantedAbilitySet @0x1B0`,
`AttributeData @0x1E0`, `Movement @0x1F0` (struct `FSpartaCharacterMovementData`),
`BaseDamage @0x340` (`FSpartaCharacterDamageData`).
`[DERIVED]` Drops are therefore deterministic from `(death data, RNG)`; mirroring the *roll result*
(materialized ground pickups) is required, mirroring the input is not sufficient unless the RNG is
shared.

### 14.4 Hit reaction plumbing (for proxy damage)

`[FACT]` `ISpartaHittable::SpartaApplyHit(HitResult: FHitResult, Instigator: USpartaCharacter,
Payload: USpartaHitPayload, DamageCauser: AActor)` and `GetHitResponse(Source, Hit) -> byte`.
`[FACT]` `USpartaHitPayload` carries: `HealthDamage @0x28` (`FHealthDamageOption`),
`PoiseDamageOption @0x34`, `BreakDamageOption @0x40`, `DamageEffect @0x50`,
`AdditionalEffects @0x58`, `DamageDataTag @0x68`, `ReactionTag @0x70`,
`StrikeDirection @0x78` (`ESpartaStrikeDirection`), `PoiseDamage @0x7C`, `PoiseDamageEffect @0x80`,
`BreakDamage @0x88`, `BreakDamageEffect @0x90`, `PoiseBrokenEffect @0x98`,
`PoiseDamageDataTag @0xA0`, `BreakDamageDataTag @0xA8`, `ChosenReactionTag @0xB0`,
`PayloadTags @0xB8` (container), `CustomElementalStacks @0xD8`, `DamageDealt @0xDC`,
`CalculatedDamage @0xE0`, `CalculatedPoiseDamage @0xE4`, `CalculatedBreakDamage @0xE8`,
`MergedPayloads @0x150`, `bIsCDO @0x154`.
Functions: `CalculateDamage(SourceASC, DamageCauser) -> float`, `CalculatePoiseDamage(…)`,
`CalculateBreakDamage(…)`, `ApplyPayload(Target, ApplyingAbility, HitResult, DamageCauser)`,
`ApplyPayloadInstigator(Target, InstigatingASC, HitResult, DamageCauser, EffectContext)`,
`MergePayloads(OuterForNewPayload, Payloads) -> payload`, `GetDuplicate(Outer) -> payload`,
`CreateGameplayEffectContextHandle(SourceASC, DamageCauser, HitResult)`.
`[FACT]` `ESpartaStrikeDirection`: `None 0, Left 1, Right 2, Up 3, Down 4, Forward 5, Backward 6`.

---

## 15. Bosses

`[FACT]` `BPC_BossAI_C` (`/Game/Sparta/Core/AI/Components/BPC_BossAI.BPC_BossAI_C`, super
`ActorComponent`) — the whole boss fight state machine:

| Member | Type | Offset |
|---|---|---|
| `bTriggeredSecondPhase` | bool | `0xC0` |
| `bTriggeredThirdPhase` | bool | `0xC1` |
| `bIsAggroed` | bool | `0xC2` |
| `BossAICharacter` | `BP_AICharacter_C*` | `0xC8` |
| `SpartaAIController` | `ASpartaAIController*` | `0xD0` |
| `OnBossDeath` | delegate | `0xD8` |
| `OnBossAggro` | delegate | `0xE8` |
| `SecondPhaseHealthPercent` | double | `0xF8` |
| `ThirdPhaseHealthPercent` | double | `0x100` |
| `OnBossDeaggro` | delegate | `0x108` |
| `OnSecondPhaseStarted` | delegate | `0x118` |
| `OnThirdPhaseStarted` | delegate | `0x260` |
| `VestigeItem` | class | `0x238` |
| `VestigeEffects` | array | `0x240` |
| `ItemStacks` | int32 | `0x250` |
| `bIsDead` | bool | `0x254` |
| `TempBossHealthCalc` | double | `0x258` |

Functions: `StartFight()`, `StopFight()`, `HandleAggro()`, `HandleDeaggro()`,
`IsAlreadyAggroed() -> bool`, `CheckAggroState()`, `ShouldStartSecondPhase()`,
`ShouldStartThirdPhase()`, `SetTempBossHealth(InDamage)`, `BroadcastFightStartEvent()`,
`BroadcastFightEndEvent()`, `BindOnDamageEvent()`, `BindOnDeathEvent()`, `BindOnAggro()`,
`BindOnDeAggro()`, `BindOnDestroyedEvent()`, `SetBossAncientVestigesEffects()`,
`AddBossFightCameraModifier()`, `RemoveBossFightCameraModifier()`, `CacheCharacter()`,
`CacheController()`, `PostPossess()`, plus handlers `OnBossDamageEvent`, `OnBossDeathEvent`,
`OnBossDestroyedEvent`, `OnAggroEvent(SpartaEngagementData)`, `OnDeaggroEvent(SpartaEngagementData)`,
`HideEnemyUI()`.

`[DERIVED]` Sync design: phases are *derived* from health (two double fields + `bTriggered*Phase`), so
sending boss health + the three flags reproduces the fight; the delegates are local-only and must not
be "replayed" on the peer.

`[FACT]` Boss persistence lives in `BPSO_BossDLManager_C` (`CurrentBoss @0xB8`, `RelevantDL @0xC0`),
`BPSO_BossGate_C.Unlocked`, `BPSO_HutchbackBossTrigger_C.HutchbackDefeated`,
`BPSO_LandingArea_Boss_C.BeaconRisen`, `BPSO_FC_FinalBossManager_C` (4 counters) and
`BPSO_TarGolemLevelScript_C.StoneSealClaimed`.

---

## 16. Dialogue

`[FACT]` `UDialogueSubsystem` (WorldSubsystem): `ActiveDialogues: TArray @0xA0`,
`SpawnedParticipants @0x50`, delegates `OnDlgStarted @0x30`, `OnDlgEnded @0x40`,
`OnDlgNodeEnterEvent @0x60`, `OnDlgNodeExitEvent @0x70`, `OnAudioEventFinished @0x80`,
`OnDlgAudioStarted @0x90`.

| Function | Signature |
|---|---|
| `SpartaStartDialogue` | `(Dialogue: UDlgDialogue*, Participants: array, Instigator: UDialogueInstigatorComponent*, AdditionalInterruptTags: container, AdditionalTags: container, AdditionalBlockingTags: container) -> UDlgContext*` |
| `StopDialogue` | `(Instigator: UDialogueInstigatorComponent*, bStopAudioImmediately: bool)` |
| `EndAllDialoguesWithTags` | `(Tags: FGameplayTagContainer)` |
| `InterruptDialogues` | `(Instigator, Asset: UDlgDialogue*, AdditionalInterruptTags)` |
| `IsDialogueCurrentlyActive` | `(InDialogue) -> bool` |
| `IsDialogueWithTagActive` | `(DlgTag: FGameplayTag) -> bool` |
| `GetAllActiveDialoguesWithTag` | `(Dialogues: out array, InTag) -> bool` |
| `FindActiveDialogueForInstigator` | `(Instigator) -> int32` |
| `CheckTagConditions` | `(Component, Asset, AdditionalTags) -> bool` |

`[FACT]` The dialogue itself is a third-party plugin type (`/Script/DlgSystem.DlgContext`,
`/Script/DlgSystem.DlgDialogue`); the choice hook the game uses is
`UDlgContext::ChooseSpeechSequenceOptionFromReplicated` (name present in the `DlgSystem` reflection,
see `mine\topic-12-dialogue.funcs.tsv`), which is the natural "peer picked option N" entry point.
`[FACT]` Related components: `UDialogueInstigatorComponent`, `ASpartaDialogueActor`,
`USpartaDlgParticipant`, `UDlgBlueprintLibrary`, `USpartaEventDataAsset`, `UHubEventComponent`.
`[DERIVED]` Sync surface: `(DlgDialogue asset path, ActiveDialogue index, chosen option index)` —
no state, no floating point. Cheapest area of the whole project.

---

## 17. Flow — world reset, run manager, zones, level instances, fast travel

### 17.1 World generation and difficulty

`[FACT]` `USpartaWorldGenSubsystem` (WorldSubsystem): `MapSettings: map @0x30`,
`PresetSeed: int32 @0x80`, `WeatherId: int64 @0x88`, `CurrentSeed: int32 @0x90`,
`CurrentWorldState: FGameplayTagContainer @0x98`, `OnDifficultyChanged @0xC0`.
Functions: `SetDifficulty(BiomeTag: FGameplayTag, NewDifficulty: int)`,
`GetDifficulty(BiomeTag) -> int`, `IncreaseDifficulty(BiomeTag, IncreaseBy) -> int`,
`BeginWorldReset()`, `AddTagToCurrentWorldState(Tag)`,
`OnDataLayerRuntimeStateChanged(DataLayer, State)`.
`[DERIVED]` `PresetSeed` + `CurrentSeed` + `WeatherId` are three integers: syncing them makes both
peers generate the same world (this is the "full world sync" primitive, and it is cheap).
`BeginWorldReset()` is the death-driven world reset — call it on both sides when one player dies.

### 17.2 Run manager (roguelite run state)

`[FACT]` `USpartaRunManagerSubsystem` (GameInstanceSubsystem): `RunStateIntegers: map @0x40`,
`AlreadySelectedEventInstances: array @0x30`.
Functions: `GetRunStateInteger(Key: FGameplayTag) -> int`,
`AddRunStateInteger(Key: FGameplayTag, Value: int)`, `RemoveRunStateInteger(Key)`,
`LevelInstanceSelected(SelectedLevel: SoftObject(world)) -> bool`,
`HasLevelInstanceAlreadyBeenStreamed(Level) -> bool`,
`GetEventDataForSpace(OptionSet, EventSpaceActor, OutError) -> USpartaEventDataAsset`.

### 17.3 Zones and areas

`[FACT]` `USpartaZoneVolumeManager` (GameInstanceSubsystem): `ZoneVolumeStack @0x40`, delegate
`HighestPriorityZoneVolumeUpdated @0x30`.
`[FACT]` `USpartaAreaSubsystem` (WorldSubsystem): `ActiveArea: USpartaAreaId* @0x30`,
`CompletedAreas: set @0x38`, delegates `OnAreaActivated @0x88`, `OnAreaDeactivated @0x98`,
`OnAreaComplete @0xA8`.
`[FACT]` **Zone identity is printable**: `USpartaZoneData` (a `DataAsset`) has
`ZoneId: FName @0x30`, `AmbienceData @0x38`, `MusicData @0x40`; and `USpartaZoneVolume`
(a `TriggerVolume`) has `ZoneData: USpartaZoneData* @0x2E0`, `Priority: int32 @0x2E8`,
`CollisionProfile @0x2EC`. `[DERIVED]` so the wire can carry a real, human-readable zone id instead of
inventing one, and `USpartaZoneVolumeManager.ZoneVolumeStack @0x40` gives the local stack to compare.
Functions: `GetActiveArea() -> USpartaAreaId`, `IsAreaActive(Area) -> bool`,
`ActivateArea(Area)`, `CompleteArea(Area)`, `IsCompleteActive(Area) -> bool`,
`GetConnectedAreas(Area)`, `GetActivityType(Area) -> USpartaActivityType`.

### 17.4 Fast travel / teleport with streaming

`[FACT]` The one function that moves a player between zones **with** streaming and a screen fade:

```
BPFL_WorldStreaming.TeleportPlayerWithStreaming(
    Location: FVector, Rotation: FRotator, ControlRotation: FRotator,
    ScreenTransitionClass: class, TransitionZOrder: int,
    FadeInDuration: double, FadeOutDuration: double,
    OptionalZoneData: USpartaZoneData*, WorldContext: object)     // flags: Static, Public, BlueprintCallable
```
(proof `mine\topic-13-flow.funcs.tsv`, `dump_line` 269197; library `/Game/Sparta/Core/Utility/BPFL_WorldStreaming.BPFL_WorldStreaming_C`.)

`[FACT]` Level-instance spawning primitives used underneath:
`USpartaLevelStreamingDynamic.LoadLevelInstance(WorldContextObject, LevelName, Location, Rotation, bOutSuccess, OptionalLevelNameOverride, OptionalLevelStreamingClass, bLoadAsTempPackage)`
and `LoadLevelInstanceBySoftObjectPtr(…, Level: SoftObject(world), …)` — both also on
`USpartaLevelStreamingDynamicHighPrio`.
`[FACT]` Companion flow actors: `BP_TeleportLocation_C`, `BP_TeleportLocation_OneShot_C`,
`BP_STH_Entrance_Gate_C` (gate network), `BP_LevelInstanceSelector_C`,
`BPC_TeleportManager_C` (component on the player controller at `+0x7F8`),
`BPSO_CinematicDLUpdate_C`, `BPC_ShellMemoryHandler_C`.
`[FACT]` `BPSO_PlayerSession_C.StartTransform @0xF0` + `GetSessionStartTransform()` is the respawn
transform; `BPSO_PlayerSaveObject_C.CurrentSpawnLocation @0x8A0` is the spawn *id*.

### 17.5 The zone-mismatch problem, quantified

`[DERIVED]` from §4.2 + §5.2: every store-B key is `(actor name, key)` and every store-A lookup is by
actor name, while actor names embed a per-map `UAID_…` tail. There is no code path in the mod that
compares zone identity, and `USpartaRunManagerSubsystem.LevelInstanceSelected` /
`HasLevelInstanceAlreadyBeenStreamed` are the only game-side records of "which variant of this room
is loaded". `[PROPOSAL]` publish `(map path, level-instance names, area id)` and refuse to apply
world state when it does not match.

---

## 18. Cinematics and pause

`[FACT]` The cinematic gate is a **save-object + tag container pair**:
`BPSO_CinematicDLUpdate_C.CinematicFired @0xB8` (per-cinematic DL object) and
`BPSO_PlayerSaveObject_C.CinematicsWatched @0x5B0` (`FGameplayTagContainer`), written with
`SetCinematicWatched(Tag)` and read with `CheckIfCinematicWatched(Tag) -> bool`.
`[FACT]` Cinematic start/stop is driven by GlobalEvent assets, e.g.
`GlobalEvent_Cinematic_Start`, `GlobalEvent_Cinematic_End`, `GlobalEvent_Cinematic_EnableTrigger`,
`GlobalEvent_Prologue_WakeUpCinematicEnds`, `GlobalEvent_Midpoint_CinematicStart`,
`GlobalEvent_FC_Monolith_CinematicStart` (all `/Game/Sparta/Core/GES/…`).
`[FACT]` During a cinematic the player pawn is replaced by a proxy:
`BP_CinematicPlayerProxy_C`, `BP_CinematicPlayer_C` (fog area), plus
`BP_LS_CinematicDLManager_C` (level script) and `BP_CinematicAudioHandler_C`.
`[DERIVED]` Consequence: the mod's `body`/`apart` mesh fields must be suppressed while
`CinematicsWatched` is mid-write and a proxy pawn is active, otherwise the peer sees two pawns.
`[FACT]` `USpartaLoadingScreenSubsystem` exists for the fade side; `USpartaUIManagerSubsystem` owns
widgets.

---

## 19. Interactables (store A actors, live-state caveat)

`[FACT]` Persistent interactables are straightforward store-A objects (§4.3). The problem is the
**live** state that is *not* persisted:

| Object | Live field | Offset | Persisted? |
|---|---|---|---|
| `BP_VFX_Breakable_C` | `IsBroken` | `0x518` | no save object |
| `BP_VFX_Breakable_C` | `Thresholds`, `SpawnVFX`, `System` | `0x519`, `0x538`, `0x540` | no |
| `BP_PressurePlateOpenDoor_C` | `Opened` | `0x361` | no |
| `BP_PressurePlateOpenDoor_C` | `RemainOpenAfterFirstInteract` | `0x360` | no |
| `BP_PressurePlateOpenDoor_C` | `InitialLocationPlate`, `InitialLocationWall` | `0x318`, `0x348` | no |
| `BP_PressurePlateDungeon_C` | plate timers/state | see `mine\topic-15-interaction.props.tsv` | no |
| `BPSO_FlowerChest_C` | `ItemCollected` | `0xB8` | **yes** (store A) |
| `BPSO_Gate_C` | `GateOpened` | `0xB8` | **yes** |
| `BPSO_MovingObject_C` | `MovedToEnd` | `0xB8` | **yes** |
| `BPSO_WoodenElevator_C` | `StartingFloor`, `HasBrokenOnce` | `0xB8`, `0xB9` | **yes** |

`[DERIVED]` So the mod's "chest/lift replay" strategy works for the persisted set and can never work
for breakables/plates; those need a live event sync (a hit that breaks a barrel must be applied to
the peer's copy) — which is exactly the wound in `HANDOFF-REMAINING-SYNC.md` G3.
`[FACT]` 27 `BP_SO_SimpleBool_C` instances are live in one hub zone (they are the generic
"has this one-shot happened" save object; `MyBool @0xB8`).

---

## 20. Persistence — the save subsystem API

`[FACT]` `USpartaSaveSubSystem` complete surface (`mine\topic-03-storeB.funcs.tsv`), the parts that
matter for a co-op mod:

| Function | Signature | Why it matters |
|---|---|---|
| `GetCurrentWorldStateSave` | `() -> USpartaSaveGame` | store B handle |
| `GetPlayerSaveObjectFromSlotIndex` | `(SlotIndex) -> UPlayerSaveObject` | read progression |
| `GetSaveObjectByClassFromSlotIndex` | `(SlotIndex, SaveObjectClass) -> USpartaSaveObject` | read any save object |
| `SaveToDisk` | `(SaveEvents: FGameplayTagContainer, bSaveImmediately: bool, bSaveAsync: bool)` | force a save after a peer-driven change |
| `SaveSession` | `()` | persist the session object |
| `AutoSave` / `EnableAutoSave` / `DisableAutoSave` | | stop autosave stomping synced state |
| `WriteCurrentSaveToTextFile` | `()` | **dump the whole save to text** — the cheapest possible verification tool |
| `ResetSaveObjectByGuid` | `(GuidString) -> bool` | reset one fixture |
| `ResetSaveObjectByClass` | `(SaveObjectClass) -> int32` | reset a whole class |
| `ResetSession` / `ResetAllSaveData` | | test fixtures |
| `SetPreventFurtherSaves` | `(bPreventSaving: bool)` | freeze saves during a sync burst |
| `IsAsyncSaving` / `HasValidSessionSave` / `GetIsSaveLoaded` | `() -> bool` | guards |
| `GetInstance` | `(WorldContextObject) -> USpartaSaveSubSystem` (Static) | acquire the subsystem |
| `SubscribeSaveInterface` / `UnsubscribeSaveInterface` | `(Object)` | the save-event hook (`ISpartaSaveInterface` on a component receives the context) |

`[FACT]` Delegates on the subsystem: `OnSaveGameLoaded @0x30`, `PostSessionReset @0x40`,
`OnSaveSystemInitialized @0x50`, `OnSaveStarted @0x60`, `OnSaveFinished @0x70`.
`[DERIVED]` `OnSaveGameLoaded` + `OnSaveFinished` are the correct barriers for "publish after the
game has finished mutating state", which is what the mod's `WS_SETTLE` timer approximates today.

---

## 21. The wire — what is actually on it today, byte for byte

`[FACT]` Both payloads are one JSON object per publish (latest-wins), written by the mod and captured
verbatim in `mine\wire\out_A.txt` / `out_B.txt`:

| Property | Value |
|---|---|
| A payload | `2 850` bytes JSON (+ CRLF = `2 852` on disk) |
| B payload | `4 155` bytes JSON (+ CRLF = `4 157` on disk) |
| Top-level keys | **31** (A) / **30** (B — no `wd`) |
| Encoding | UTF-8, no BOM (first bytes `7B 22 74` = `{"t`) |
| Publish rate | 15 Hz (`sendOwn`, `main.lua:1566`) |
| Measured throughput | ~60 KB/s per direction at 15 Hz with these payloads |

Key inventory (`mine\wire-fields.md`): `t, id, sq, x, y, z, vx, vy, vz, yaw, pitch, anim, spd, mseq,
mpos, mrate, mid, body, apart, wpn, wsock, wpns, npc, pd, nhp, dead, ws, wb, wq, wd, wl`.

Byte cost of each key in the A sample (`mine\wire-field-sizes.md`):

| key | JSON bytes | key | JSON bytes |
|---|---|---|---|
| `wd` (data layers) | 509 | `ws` (save objects) | 210 |
| `wpns` (weapon list) | 477 | `nhp` (npc health) | 152 |
| `wb` (world-state) | 379 | `body` (mesh path) | 97 |
| `wq` (quests) | 339 | `npc` | 268 |
| `dead` | ~90 | rest | < 70 each |

`[DERIVED]` The payload already exceeds one MTU (1 500 B) by 2–3×, so the relay's single UDP
datagram has no retransmit and partially lost publishes are simply dropped — that is a structural
limit, not a bug (§25 P8).

### 21.1 `[PROPOSAL]` the fields that must be added (all backed by §4–§19)

| Proposed key | Contents | Source of truth |
|---|---|---|
| `php` | `Health`, `MaxHealth`, `ShellHealth`, `MaxShellHealth` | `SpartaHealthSet` `0x60/0x70/0x80/0x90` (+`0xC`) |
| `prs` | `Resolve`, `MaxResolve` | `SpartaHealthSet` `0xD0/0xE0` |
| `ppp` | `PPItemResource`, `PPItemMaxResource` | `PlayerAttributeSet` `0x190/0x1A0` |
| `phc` | `HealCurrentCharges`, `HealMaxCharges` | `PlayerAttributeSet` `0xE0/0xF0` |
| `pdf` | `ShellReviveCurrentProgress`, `ShellReviveMaxProgress` | `PlayerAttributeSet` `0x150/0x160` |
| `pds` | `DeathState` (`ESpartaDeathState`) + `IsInDarkform` | `SpartaHealthComponent.DeathState 0x128`, `BPSO_PlayerSession_C.IsInDarkform 0x168` |
| `pam` | `CurrentAmmo`, `MaxAmmo` (sidearm) + dark power ammo | `SidearmAttributeSet` `0x30/0x40` |
| `psx` | `StartTransform` (respawn anchor) | `BPSO_PlayerSession_C.StartTransform 0xF0` |
| `pinv` | item lines `(ItemDef class path, StackCount, StatTags)` | §11.1–§11.2 |
| `pprog` | the diff of §9.1–§9.5 (send a delta, not the object) | `BP_SpartaPlayerSaveObject_C` |
| `pzone` | `(map path, level-instance names, ActiveArea)` | §17.3/§17.5 |
| `pboss` | per-boss `(health, phase flags)` | §15 |
| `pdia` | `(dialogue asset, active index, chosen option)` | §16 |

`[PROPOSAL]` With `ws/wb/wq/wd` split into separate messages and rate-tiered (world state at 1–2 Hz,
body at 10–15 Hz), each message fits a datagram and the MTU problem disappears without a protocol
change.

---

## 22. Enums you will need (with values)

`[FACT]` From `Sparta_structs.hpp`, all 498 Sparta enum values in `mine\enums-sparta.md`:

| Enum | Members |
|---|---|
| `ESpartaDeathState` | `NotDead 0, DeathStarted 1, DeathFinished 2` |
| `ESaveObjectProtocol` | `PerInstance 0, Global 1` |
| `EPPItemSlot` | `Active 0, Passive 1` |
| `ESpartaStrikeDirection` | `None 0, Left 1, Right 2, Up 3, Down 4, Forward 5, Backward 6` |
| `ESpartaOnScreenCheckType` | `ViewFrustum 0, InFOVAngle 1, InFOVAngle_OnlyUpdateIfCharMoves 2` |
| `EDataLayerRuntimeState` (Engine) | `Unloaded 0, Loaded 1, Activated 2` |
| `EDataLayerState` (Engine) | `Unloaded 0, Loaded 1, Activated 2` |

`[FACT]` Total enum values mined: **17 705** across all modules (`mine\enums.tsv`), of which 498 are
Sparta's. Use that file rather than re-deriving a value by name.

---

## 23. Structs you will need (with offsets)

| Struct | Field | Offset | Size |
|---|---|---|---|
| `FGameplayAttributeData` | `BaseValue` | `0x08` | `0x04` |
| | `CurrentValue` | `0x0C` | `0x04` |
| `FGameplayTag` | `TagName: FName` | `0x00` | `0x08` |
| `FGameplayTagContainer` | `GameplayTags: TArray<FGameplayTag>` | `0x00` | `0x10` |
| | `ParentTags: TArray<FGameplayTag>` | `0x10` | `0x10` |
| `FGameplayTagStackContainer` (Sparta's own, not the GameplayAbilities one) | `Stacks: TArray<FGameplayTagStack>` | `0x108` | `0x10` |
| `FSpartaItemList` | `Entries: TArray<FSpartaItemEntry>` | `0x108` | `0x10` |
| | `OwnerComponent` | `0x118` | `0x08` |
| `FSpartaItemEntry` | `Instance: USpartaItemInstance*` | `0x10` | `0x08` |
| | `StackCount` | `0x18` | `0x04` |
| | `LastObservedCount` | `0x1C` | `0x04` |
| `FSpartaCharacterDeathData` | `GloomOnDeath` | `0x00` | `0x04` |
| | `GoldOnDeath` | `0x04` | `0x04` |
| | `GoldDropChance` | `0x08` | `0x04` |
| | `ItemDropTable` | `0x10` | `0x50` |
| `FPermanentStats` | `PermanentStats: TMap<FGameplayTag,int32>` | `0x00` | `0x50` |
| `FSpartaSaveContext` | see §4.1 | | |
| `FSaveObjectTickFunction` | `Target` | `0x28` | `0x08` |

`[FACT]` 43 842 struct-field rows total (`mine\structs.tsv`), 669 Sparta-only (`mine\structs-sparta.md`),
43 920 UE4SS rows (`mine\structs-ue4ss.tsv`). Offsets agree (see §1.1).

---

## 24. Assets, IDs and the two things that are *not* in any dump

`[FACT]` **260 DataTable assets** exist (`mine\datatables.tsv`), e.g.
`/Game/Sparta/Items/Pickups/Core/DT_PickUpItems`, `/Game/Sparta/Items/Shop/DT_ShopItems`,
`/Game/Sparta/Items/FrogMomma/DT_PotionIngredients`, `DT_Attributes_*` per boss/enemy.
`[FACT]` **24 row structs** derive from `TableRowBase`, including `SpartaItem`,
`SpartaSimpleInventoryItem`, `PlayerUpgrade`, `QuestDetails`, `EntitlementID`,
`SpartaEnemySpawnerDisplayData`, `SpartaHubGiftEventData`, `SpartaSurpriseEggData`.

`[UNKNOWN]` **DataTable row names.** Proven absent: a `DataTable` object in the UE4SS dump has **0
children** (probe: children of `/Game/Sparta/Items/Pickups/Core/DT_PickUpItems` = 0), there is no
`SpartaItem`-typed object anywhere in the index, and `UDataTable` itself exposes **no** reflected
functions. The row names exist only inside the cooked table. **But the tooling to read them is
reflected and known**, so this is a mechanical probe, not an open problem — `UDataTableFunctionLibrary`
(`/Script/Engine.DataTableFunctionLibrary`):

| Function | Signature |
|---|---|
| `GetDataTableRowNames` | `(Table: UDataTable, OutRowNames: TArray<FName>)` |
| `GetDataTableRowStruct` | `(Table: UDataTable) -> UScriptStruct` |
| `GetDataTableRowFromName` | `(Table: UDataTable, RowName: FName) -> (OutRow: FTableRowBase, bool)` |
| `DoesDataTableRowExist` | `(Table: UDataTable, RowName: FName) -> bool` |
| `GetDataTableColumnNames` | `(Table: UDataTable, OutColumnNames: TArray<FName>)` |
| `GetDataTableColumnAsString` | `(Table, RowName, ColumnName) -> FString` |

Call them on the library's CDO (`/Script/Engine.Default__DataTableFunctionLibrary`) with the 260
paths from `mine\datatables.tsv` and the row-name gap closes permanently.

`[UNKNOWN]` **Gameplay tag names.** `FGameplayTag` is just an `FName`; the string table is not an
object in the dump (`GameplayTag`-typed objects: **0**). Probe P2 in §25.

`[FACT]` What *is* available as an id space without any probe:
* **item ids** — 393 Blueprint item definitions under `/Game/Sparta/Items/` (`mine\item-assets.tsv`),
  reachable as `class` values, which is what `S_AddItem(Item: class)` wants;
* **equipment ids** — the six `Active*ItemDefinition` class fields (§9.2);
* **shell/weapon/seal/sidearm ids** — `FGameplayTag` values in `EquipmentUnlockState`, plus
  `S_UnlockShell(…, CurrentTag)` / `S_SwitchToShell(Name)` which take a **string**;
* **landing area ids** — `FName` in `UnlockedLandingAreas` and `CurrentSpawnLocation` is `FText`;
* **quest ids** — `UQuestObject.QuestID: FName`;
* **spawner ids** — `UUnitSpawner.CharacterId: FGameplayTag`.

`[DERIVED]` So a working inventory/progression sync does **not** need DataTable row names: class
paths and FNames cover it. Row names are only needed if you choose to drive
`S_AddItem(RowName, …)` instead of `AddItemByClass(ItemDef, StackCount)` — §11.1 shows the latter.

---

## 25. UNKNOWN register — every hole and the probe that closes it

Static mining is finished; the remaining questions are all *runtime* questions. Each row is a
concrete probe, not a vague "investigate".

| id | Question | Why it matters | Probe |
|---|---|---|---|
| P1 | DataTable row names per table | only needed for the `S_AddItem(RowName)` path | `UDataTableFunctionLibrary::GetDataTableRowNames(table, out)` on the CDO `/Script/Engine.Default__DataTableFunctionLibrary`, for each of the 260 paths in `mine\datatables.tsv` |
| P2 | Gameplay tag string table | needed to *print*/diff tags, and to build tag literals for `S_AddNarrativeTag`, `EquipmentUnlockState`, `Difficulty`, `ShellStats` | iterate the project tag table asset(s) (`GameplayTagTableRow` struct exists, `/Script/GameplayTags.GameplayTagTableRow`) and dump every `TagName` to a TSV |
| P3 | Does `USpartaSaveObject.UpdateFrom(Other, Events)` merge or overwrite? And does `Events` gate it? | `UpdateFrom` may be a free full-object merge = the cheapest possible store-A sync primitive | two peers, call `UpdateFrom(peerObject, emptyContainer)` on a class with 3+ booleans and observe which change (`UpdateFrom` at `ObjectDump_allassets.txt:53849`) |
| P4 | Is harden / stone form observable as an owned gameplay tag? | §7.4 — otherwise harden cannot be synced faithfully | `UBlueprintGameplayTagLibrary::GetOwnedGameplayTags(Interface)` → `FGameplayTagContainer`, called on the ASC before/during/after `GA_Harden_Original`; or `ASC:GetGameplayTagCount(Tag)` per candidate tag once P2 gives names. `ASC` also exposes `GetActiveEffectsWithAllTags`, `FindAllAbilitiesWithTags`, `TryActivateAbilitiesByTag` |
| P5 | Can `BPFL_Player_Equipment_C` be invoked from its CDO? | decides whether `ChangeShell/ChangeShellSkin/ChangeClothing/ChangeMask` are usable (real equipment sync instead of mesh preview) | `local cdo = StaticFindObject('/Game/Sparta/Core/Player/BPFL_Player_Equipment.Default__BPFL_Player_Equipment_C')` then call one getter; success → real API |
| P6 | Is `FGameplayTagContainer` constructible from Lua as a struct literal? | `S_UnlockTarforgeModes`, `EndAllDialoguesWithTags`, `SaveToDisk(SaveEvents)` all take one | build `{GameplayTags={<one 8-byte {TagName=FName('X')}>}, ParentTags={}}` and pass it to a reader that only inspects it (`CheckTagConditions`) |
| P7 | What is `UPlayerInventoryComponent` for? It has zero reflected members | decides whether inventory reads must go through `SpartaItemManagerComponent` only | list the player pawn's components, compare `USpartaItemManagerComponent:GetAllItems()` count against what the UI shows |
| P8 | Actual UDP MTU path: is `sendto` with 4 KB fragmented or dropped? | decides whether §21's split is mandatory | relay-side capture with the existing FileLink (byte counts already known) and a 1 400-byte cap experiment |
| P9 | Does `WriteCurrentSaveToTextFile()` write a stable, diffable text form? | would give a zero-effort save-diff verifier for both peers | call it on both peers after the same actions and diff the outputs |
| P10 | Are `S_*` functions gated by authority/console-only in a packaged build? | they are on the BP controller, so they should be callable, but a client-side `HasAuthority` check cannot be excluded statically | call `S_AddGold(1)` from the guest and read `BP_SpartaPlayerSaveObject_C.CumulativeGold` back |

---

## 26. Artefact manifest, regeneration and integrity

Everything below is generated from files that were already on disk; **no game process is started**
by any of it.

```powershell
cd G:\Freespace\shell_coop\research
python scripts\mine.py enums          # enums.tsv            (17 705 rows, all modules)
python scripts\mine.py structs        # structs.tsv          (43 842 rows, Dumper-7)
python scripts\mine.py flags          # funcflags.tsv        (51 566 rows, UFunction flags)
python scripts\mine_cxx_structs.py    # structs-ue4ss.tsv    (43 920 rows, UE4SS headers)
python scripts\mine.py gen            # 19 topic .props/.funcs/.structs TSVs + sync-targets.tsv
python scripts\mine_extra.py          # enums/structs Sparta views, native classes, save-component
                                      # owners, live census, datatables, item assets, save-object
                                      # matrix, hashes
python scripts\mine_map.py            # sync-map.tsv + sync-map.md  (837 curated targets)
python scripts\mine_wire.py           # wire/*.pretty.json + wire-fields.tsv + wire-field-sizes.tsv
python scripts\tsv2md.py mine\X.tsv   # markdown rendering of any artefact
```

| Artefact | Rows / size | Contents |
|---|---|---|
| `sync-map.tsv` / `.md` | **837** | the curated map: area, class, member, offset **or** full signature, GAS read hint, proof line |
| `saveobjects-matrix.tsv` / `.md` | 57 | every save object with its complete field list and offsets |
| `topic-01..19-*.props.tsv` | 8 071 | every property of every class in a topic, with offsets and dump lines |
| `topic-01..19-*.funcs.tsv` | 14 665 | every function with ordered parameters and UFunction flags |
| `topic-*.structs.tsv` | per topic | the struct layouts those properties reference |
| `enums.tsv` (all) / `enums-sparta.tsv` | 17 705 / 498 | enum members with explicit values |
| `structs.tsv` / `structs-sparta.tsv` / `structs-ue4ss.tsv` | 43 842 / 669 / 43 920 | struct field offsets from two independent dumps |
| `structs-crosscheck-conflicts.tsv`, `classprops-crosscheck-conflicts.tsv` | 0 / 0 | the verification of §1.1 — empty because there are no conflicts |
| `funcflags.tsv` | 51 566 | `BlueprintCallable`/`Static`/`BlueprintPure`/… per function (1 766 Sparta) |
| `native-classes.tsv` / `.md` | 504 | every `/Script/Sparta.*` UClass with its super chain |
| `savecomponent-owners.tsv` / `.md` | 130 | every class that owns a `USpartaSaveComponent` |
| `live-objects.tsv` | 9 566 | per-class object count in the live gameplay dump |
| `datatables.tsv` / `.md` | 260 | every DataTable asset (row names are runtime-only, §24) |
| `item-assets.tsv` / `.md` | 393 | the item-definition id space |
| `wire/out_A.txt`, `wire/out_B.txt`, `wire/*.pretty.json`, `wire-fields.tsv`, `wire-field-sizes.tsv` | 2 850 / 4 155 B | the real published payloads, byte-exact |
| `hashes.tsv` | 79 | SHA-256 + byte size of every artefact and of both indices |
| `manifest.json` | — | file list with row counts |

Totals: **94 files, 24.3 MB, 22 736 mined property/function rows, 837 curated targets, 0 offset
conflicts.**

---

## 27. What this data makes possible (one paragraph, so the tables are not read in a vacuum)

`[DERIVED]` The expensive problems in the remaining-sync handoff all reduce to things mined above:
(a) *peer body state* is six `FGameplayAttributeData` floats plus one enum, reachable through
component pointers at fixed offsets (§7); (b) *progression* is one object with 73 fields, a complete
setter API and 129 `S_*` writers (§9, §10); (c) *world state* is two maps plus a 90-function helper
library whose function names spell out the key grammar (§5); (d) *flow* is three integers on the
world-gen subsystem plus one teleport function with a documented signature (§17); and (e)
*equipment* — the thing the mod currently fakes by swapping meshes — has five tag-keyed writers on
`BPFL_Player_Equipment_C` that perform the entire switch, save write included (§9.3).

`[DERIVED]` Two structural cautions follow from the data rather than from guesswork: equipment,
difficulty, narrative and skins are all identified by **GameplayTag** (§9.3, §9.5, §13), while items
are identified by **class path** (§11.2) — so a wire protocol needs both id spaces and a way to
print tags (P2); and `USpartaSaveObject:UpdateFrom` may already be a whole-object merge primitive
(§4.1, probe P3), which would remove per-field store-A replication for the classes it covers.

What remains genuinely unknown is not "where is the state" but "what does the game do when you poke
it" — exactly what the ten probes in §25 are for.

---


---

## 28. Appendix — ready-to-paste probe snippets for the ten §25 questions

`[PROPOSAL]` These are the probes of §25 written out. They are **not** part of the mod and were not
run to produce this document; they belong in `ms2-coop\mod\CoopLab\Scripts\main.lua` (the lab mod)
and are driven by `ms2-coop\lab.ps1`, per the standing research rules. Everything they need —
`StaticFindObject`, class paths, offsets — is in §3–§24.

```lua
-- P1: every row name of every DataTable (the 260 paths are in mine\datatables.tsv)
-- UDataTable has NO reflected functions; the library does, so call its CDO.
local lib = StaticFindObject('/Script/Engine.Default__DataTableFunctionLibrary')
for _, path in ipairs(TABLE_PATHS) do              -- paste from mine\datatables.tsv
  local dt = StaticFindObject(path)
  if dt and dt:IsValid() then
    local out = {}                                  -- TArray<FName> out-param
    lib:GetDataTableRowNames(dt, out)
    local struct = lib:GetDataTableRowStruct(dt)     -- confirms which row struct to expect
    for i = 1, #out do
      log(string.format('%s\t%s\t%s', path, tostring(struct), tostring(out[i])))
    end
  end
end
-- emit coop\probe_p1.tsv : table path <TAB> row struct <TAB> row name

-- P2: the gameplay-tag string table (FGameplayTag is only an FName, so dump the registry)
-- the project tag tables use the reflected struct /Script/GameplayTags.GameplayTagTableRow
-- emit coop\probe_p2.tsv as  tag <TAB> parent

-- P3: does USpartaSaveObject:UpdateFrom merge or overwrite?   (UpdateFrom @ ObjectDump:53849)
local so    = <save object on this peer>
local other = <save object with 3+ booleans flipped>
so:UpdateFrom(other, { GameplayTags = {}, ParentTags = {} })   -- empty container
-- log every boolean before/after to coop\probe_p3.txt

-- P4: is harden observable as a gameplay tag?
local lib = StaticFindObject('/Script/GameplayTags.Default__BlueprintGameplayTagLibrary')
local asc = <player pawn>.AbilitySystemComponent
local before = lib:GetOwnedGameplayTags(asc)        -- returns FGameplayTagContainer
-- activate GA_Harden_Original, wait, deactivate
local during = lib:GetOwnedGameplayTags(asc)
-- diff before/during -> coop\probe_p4.txt
-- once P2 gives names, ASC:GetGameplayTagCount(Tag) -> int is the cheap per-tag test

-- P5: can BPFL_Player_Equipment be called from its CDO?
local cdo = StaticFindObject('/Game/Sparta/Core/Player/BPFL_Player_Equipment.Default__BPFL_Player_Equipment_C')
local ok, res = pcall(function() return cdo:GetShellItemDefinition(world) end)  -- any getter
-- log ok, res to coop\probe_p5.txt

-- P6: is FGameplayTagContainer constructible as a struct literal?
local c = { GameplayTags = { { TagName = FName('Test.Tag') } }, ParentTags = {} }
-- pass it to a reader that only inspects it (e.g. CheckTagConditions on a dummy component);
-- if the call returns without crashing, the shape is valid

-- P7: what does PlayerInventoryComponent hold?
-- list the player pawn's components, then compare USpartaItemManagerComponent:GetAllItems()
-- with the count the inventory UI shows

-- P9: is WriteCurrentSaveToTextFile diffable?
-- call it on both peers after the same actions, then diff the two text files

-- P10: are the S_* writers gated on the guest?
-- call S_AddGold(1) from the guest, then read BP_SpartaPlayerSaveObject_C.CumulativeGold (0x434)
```

`[FACT]` Everything these snippets index into was verified statically here:
`USpartaSaveObject:UpdateFrom` (`ObjectDump_allassets.txt:53849`), the five
`UDataTableFunctionLibrary` row/column functions, `UBlueprintGameplayTagLibrary:GetOwnedGameplayTags(Interface)`,
`UAbilitySystemComponent:GetGameplayTagCount`, `BPFL_Player_Equipment_C` as a
`BlueprintFunctionLibrary` (so it has a `Default__…` CDO), `USpartaSaveSubSystem:WriteCurrentSaveToTextFile`,
and `S_AddGold` on `BP_PlayerController_C`. What is *not* proven is whether these calls are safe at
the moment you call them — that is what §4.4/§25 and the proven-crash list in
`HANDOFF-REMAINING-SYNC.md` are for.

---
*Generated from `research\raw\*` on 2026-10-22; the doc itself is not part of the mod and no file
under `ms2-coop\` was modified to produce it.*
