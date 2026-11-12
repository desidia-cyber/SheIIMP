# Mortal Shell II co-op research — raw data + sync-relevant facts

Round-2 data collection for a co-op mod for **Mortal Shell II** (Unreal Engine 5.6, shipping build,
UE4SS Lua modding). Nothing here is a design and nothing here is mod code: this is the fact base the
sync engineer works from. Every file in this folder follows one rule — **no fact without evidence**
(a dump line, a `.hpp` path, or a log line from a game run).

How the data was produced (three UE4SS dumps + three Dumper-7 SDKs of the same binary + in-game
probes) is in **`00-sources.md`**; the independent cross-check of every layout fact against
that second dumper is in **`05-dumper7-vs-ue4ss.md`**; what was actually verified by playing
is in **`90-verified.md`**.

---

## 0. Ten facts worth knowing before reading anything else

| # | Fact | Evidence |
|---|---|---|
| 1 | **The game has no netcode to piggyback on.** Under `/Script/Sparta.*` and `/Game/Sparta/*` there are **zero** `Server*` / `Client*` / `Multicast*` UFunctions and **zero** `bReplicates` properties. | `scripts\q\43-net.sql`, `44-net2.sql` (0 rows / 0 rows) |
| 2 | **World state is a save-object graph, not flags on actors.** Every persistent object owns a `USpartaSaveComponent` (`Guid`, `SaveObjectClass`, `RuntimeData`, `Save(Events)`) whose `RuntimeData` is a `USpartaSaveObject` subclass. A lab `FindAllOf(SpartaSaveComponent)` in one zone returned **471** live components (`90-verified.md` §12); the same zone's in-game dump lists 234 (`10-world-state.md` §13) — the lab sees every loaded sublevel, the dump was taken at one moment. | `10-world-state.md` §1, §13; `90-verified.md` §12 |
| 3 | **There is a read/write API for named world values** — `USpartaWorldStateHelper.WriteMyBool/WriteMyInt/…/ReadMyBool/…` plus `WriteXxxByTags(Owner, Key, Value)` and `AddMeToChangedSet()`. This is the cheapest possible sync surface. | `10-world-state.md` §10 | 
| 4 | **The whole persisted world is one object**: `USpartaSaveSubSystem` → `SaveObjects: TMap`, `CurrentOnDiskWorldStateSave: USpartaSaveGame*`; `USpartaSaveGame` carries `ChangedActors: TSet<FName>` and four `TMap<FName, FSpartaMapX>` maps (`ActorBools/Ints/Floats/Names`) + `ActorObjectMap`. | `10-world-state.md` §3–§10 |
| 5 | **Actor identity is stable across launches** — the same zone actor keeps the same `UAID_…_<n>` tail in two independent runs. That is what makes name-tail matching work. | `90-verified.md` §2 vs §11: `BP_HubDoubleDoor_C_UAID_D4E98A7EF567D94802_83a1dc45e2a01438_1808979936` in both runs |
| 6 | **Damage is a single receive hook**: `ISpartaHittable::SpartaApplyHit(FHitResult, ASpartaCharacter* Instigator, USpartaHitPayload*, AActor* DamageCauser)`; the payload is a **UObject** (`USpartaHitPayload`, 0x158 bytes, 27 fields), not a struct. | `50-enemies-bosses.md` §6.0–6.3 |
| 7 | **Health lives in GAS**: `USpartaHealthComponent` + `USpartaHealthSet` (`Health`, `MaxHealth`, `ShellHealth`, `Poise`, `Resolve`, `Armor`, `BreakResistance`, …). Reads are `GetHealth()/GetCurrentHealthNormalized()/IsDeadOrDying()`, writes go through `SpartaAbilitySystemComponent:SetFloatAttributeValue`. | `40-player-actions.md` §2, `90-verified.md` §7 |
| 8 | **The player's inventory is `USpartaItemManagerComponent`** (`ItemList: FSpartaItemList`, `AddItemByClass`, `RemoveItemStacksByClass`, `OnItemStacksUpdated`). `UPlayerInventoryComponent` (`/Script/Sparta.PlayerInventoryComponent`) exists but has **zero** members. **Gold is an item** (`ID_Gold_C`), not a counter. | `60-items-inventory.md` §1–§8; class name re-verified against `raw\Dumper7` in `05-dumper7-vs-ue4ss.md` §7.1 |
| 9 | **Shells/weapons are assets, not state**: the body is `SpartaSkeletalMeshComponent.SkeletalMesh` + `AnimClass`; the shell/skin/weapon *choice* is a `FGameplayTag` changed through `BPFL_Player_Equipment` (`ChangeShell(ShellId, Save, WorldContext)` …). | `30-player-look.md` §3–§7; `90-verified.md` §10 |
| 10 | **No `FGameplayTag` can be constructed from UE4SS Lua** in this build, and `BPFL_Player_Equipment_C` has **0 instances** (`FindAllOf` → 0), so tag-driven flows cannot be driven from Lua the way class-driven ones can. | `90-verified.md` §10 (log lines) |

---

## 1. Files in this folder

| File | Lines | What it answers |
|---|---:|---|
| `00-sources.md` | 257 | what each raw artefact is, how/when it was made, save protection, dump quirks |
| `05-dumper7-vs-ue4ss.md` | 624 | three **Dumper-7** SDKs of the same binary (menu / all-assets / gameplay) and the mechanical diff against the UE4SS dump: **78 740** property slots compared with 0 conflicts; the three upstream bugs that broke the heavy modes and the local patch set that fixes them; the built-in build test (ClangCL) and the MSVC compile checks; what Dumper-7 adds (padding, flags, parameter structs) and what it does not (no RVAs) |
| `10-world-state.md` | 1755 | doors/gates/levers/chests/pickups/checkpoints, **the save-object + world-state system**, tags |
| `20-quests-progression.md` | 953 | quests, objectives, activities, progression, achievements, dialogue, narrative tags, NPC state |
| `20-quests-progression-part2.md` | 1076 | save architecture (per-player vs per-world), all 52 `BPSO_*`/`BPO_*` classes, DataTables/DataAssets, 371 map packages, all 90 travel/level-load functions |
| `30-player-look.md` | 2060 | shells, meshes, skeletons, materials, weapons (`WP_*`, `SM_Weapon`), sockets, armour, cosmetics, visual state |
| `40-player-actions.md` | 1968 | native ability foundation, the 7 attribute sets, the input layer, melee/parry/dodge/harden/block |
| `40-player-actions-part2.md` | 1967 | sidearms, shells/severed/revive/traversal, tarstones, artifacts, upgrades, PP items, 193 interactions, sync-hook list |
| `50-enemies-bosses.md` | 1792 | enemies, bosses, AI/perception/targeting, spawn/respawn, boss lifecycle and **the whole damage path** |
| `50-enemies-bosses-part2.md` | 916 | the complete per-family `GA_*` ability inventory (470 enemy / 224 boss) |
| `60-items-inventory.md` | 2260 | item definitions/fragments, inventory components, currency, drops, merchants, upgrades, crafting |
| `70-flow-menus.md` | 1834 | main menu/continue/new game, pause, death screen, respawn, loading screens, cutscenes, credits |
| `80-anim-catalog.md`, `-part1`, `-part2` | 188 / 2985 / 2029 | every `AnimSequence` (2979) and `AnimMontage` (2023) under `/Game/Sparta/Characters/`, categorised from the path |
| `90-verified.md` | 322 | the ten objects read/acted on **in the running game**, with before/after values and log lines |

Supporting material:

| Path | What it is |
|---|---|
| `raw\ObjectDump_allassets.txt` | 2 170 742-line object dump, main menu, `LoadAllAssetsBeforeDumpingObjects=1` (565 MB) |
| `raw\CXXHeaderDump\` | 10 469 UE4SS CXX headers (class layouts with offsets, structs, enums, one file per Blueprint class) |
| `raw\ObjectDump_ingame.txt` | 553 057-line dump taken in gameplay (126 MB) |
| `raw\index.sqlite3`, `raw\index_ingame.sqlite3` | SQLite indexes of the two object dumps (2 153 654 / 536 682 rows) |
| `raw\Dumper7\` | Dumper-7 v7.1.0 SDK, **main menu** (5 365 files, 128 MB, 8 525 classes): per-package `*_classes.hpp` / `*_structs.hpp` / `*_functions.cpp` / `*_parameters.hpp`, `SDK.hpp`, `Assertions.inl` |
| `raw\Dumper7_allassets\` | Dumper-7 SDK in the **same all-assets state as `raw\CXXHeaderDump`** (18 893 files, 239 MB, **16 040 classes**) — the like-for-like partner for the diff |
| `raw\Dumper7_gameplay\` | Dumper-7 SDK taken **in gameplay** (7 406 files, 152 MB, 9 441 classes) |
| `tools\dumper7-patches.diff` | the 6-file patch set that makes the two heavy Dumper-7 modes work (see `05-*` §3) |
| `slices\d7-vs-ue4ss.json`, `…-allassets.json`, `…-gameplay.json`, `slices\d7-enum-compare{,-allassets}.json` | the machine-readable results of the cross-check scripts, one per SDK |
| `slices\anim_catalog.csv` | the machine-readable animation catalogue behind `80-*` |
| `slices\*.txt` | per-area extracted slices some reports quote from |
| `scripts\` | the tooling — see §6 |
| `runs\<stamp>-<mode>\` | per-run logs (`run.log`, `lab.log`, `UE4SS.log`, `saves-before/after.txt`) of every dump run |

---

## 2. The five most useful things per area, for syncing across two players

Everything below is a **fact about an existing API**; the "why" is what a sync layer can key on.
Full tables, parameter lists and evidence lines are in the linked report.

### 2.1 World state (`10-world-state.md`)

| # | Class / function | Why it matters |
|---|---|---|
| 1 | `USpartaSaveComponent` — `Guid` (FGuid), `SaveObjectClass` (class), `GetRuntimeData()`, `Save(Events: GameplayTagContainer)`, delegates `OnSaveLoaded` / `OnPreSave` | one component per persistent actor; `RuntimeData` **is** the actor's state object, and the component is where a write has to land to be persisted |
| 2 | `USpartaSaveObject` — `ReceiveSpartaPreSave(SaveContext)`, `ReceiveOnLoaded()`, `GetOwningActor()`, `GetOwningSaveComponent()`, `UpdateFrom(other, Events)` | base class of every state object; `ReceiveOnLoaded` is the moment the game applies persisted state |
| 3 | `USpartaWorldStateHelper` — `WriteMyBool/Int/Float/Name(WorldContext, ValueName, Value[, bModify])`, `ReadMyBool/…`, `WriteXxxByTags(Owner, Key, Value)`, `AddMeToChangedSet()` | the highest-level write path: named values, no class knowledge required |
| 4 | `USpartaSaveSubSystem` — `SaveObjects` (TMap), `GetSessionSave()`, `SaveToDisk(SaveEvents, bSaveImmediately, bSaveAsync)`, `SaveSession()`, `ResetSaveObjectByGuid(GuidString)`, delegates `OnSaveGameLoaded` / `OnSaveStarted` / `OnSaveFinished` / `PostSessionReset` | the single authority for "what is persisted"; `SaveToDisk` is the disk flush a host must gate |
| 5 | the concrete state classes: `BP_SO_SimpleBool_C.MyBool`, `BPSO_Gate_C.GateOpened`, `BPSO_WoodenElevator`, `BPSO_SavedLockedChests`, `BPSO_BossGate_C.Unlocked`, `BPSO_LandingArea_Boss_C.BeaconRisen`, `BPSO_FC_FinalBossManager_C.{EndStrandingCompleted, ClensedCores, TotalCoresNeeded}` | 51 Blueprint save-object classes with flat fields — the actual "did the door open" bits |

### 2.2 Quests / progression (`20-quests-progression.md`)

| # | Class / function | Why it matters |
|---|---|---|
| 1 | `BPC_QuestLogComponent_C` (a `USpartaSaveComponent`) — `CurrentActiveQuests`, `CompletedQuests`, `CurrentTrackedQuest`, `ActiveQuests` (live `BP_QuestObject_C`s), `NarrativeTags`, `NPCRelationships`, `NarrativeEvents`, `PlayerChoice`; delegates `OnQuestStarted` / `OnQuestCompleted` + 4 query delegates | **the single per-player story-state object**; its persisted twin is `BPSO_QuestLogComponent_C` (only field: `NarrativeTags`) |
| 2 | `/Script/Sparta.QuestObject` — `QuestID: FName`, `CurrentQuestStage: int`, `CurrentQuestObjectiveProgress: TMap`, `CurrentStageDetails: FQuestStage`, `QuestDetails: FQuestDetails` | the live quest instance; the report's conclusion is *replicate the quest log, not these spawned objects* (they are rebuilt from it on load) |
| 3 | `/Script/Sparta.QuestObjective` — `ObjectiveData`, `QuestID`, `OnObjectiveCompleted` (MulticastInlineDelegate), `InitializeObjective(...)` | per-objective record + its completion event |
| 4 | `BPSO_Progression_C` (`UPlayerProgressionSaveData`) — `CurrentLevel`, `AvailablePoints`, `AssignedPoints: TMap<FGameplayTag,int>`, `BondingPoints`, `GoldSpentAtShop`; `PlayerProgressionComponent` | where player progression is persisted |
| 5 | `DialogueSubsystem` (WorldSubsystem) — `ActiveDialogues: TArray<ActiveDialogue>`, `SpartaStartDialogue(Dialogue, Participants, Instigator, AdditionalInterruptTags, AdditionalTags, AdditionalBlockingTags) → DlgContext` (6 delegates, all signatures resolved) + `BPSO_WordMapObjective_C.{Visible,Revealed,Completed,ShowArea}` | dialogue-in-flight (transient, must be host-driven) and the map/objective reveal+completion state |

### 2.3 Player look (`30-player-look.md`)

| # | Class / function | Why it matters |
|---|---|---|
| 1 | `USpartaSkeletalMeshComponent` — `SkeletalMesh`, `AnimClass` (on `CharacterMesh0`) | the body a peer must render; two readable paths, no tag needed |
| 2 | `BPFL_Player_Equipment_C` — `ChangeShell(ShellId: FGameplayTag, Save: bool, WorldContext)`, `ChangeShellSkin`, `ChangeDarkForm`, `ChangeWeapon/ChangeSidearm/ChangeSeal` | every "which body / which skin / which weapon" switch funnels through this library |
| 3 | `USpartaCharacterData` (DataAsset per shell) — `DefaultMaterials`, `SkinMaterialsMap`, mesh refs; `InitMeshByID`, `SwapShellMaterials` | the mapping from a shell tag to meshes/materials |
| 4 | `UWeaponsComponent` — `EquippedWeapons: TMap<FGameplayTag, ASpartaWeapon*>`, `EquipWeaponToSlot`, `OnWeaponEquipped/OnWeaponUnequipped`; `ASpartaWeapon.WeaponVariants` | which weapon is in which slot, and its visual variant |
| 5 | `WP_WeaponBase_C` — `EquipState` / `CurrentEquipState` (`EWeaponEquipState`), `StowedSocket`, `InHandSocket` | drawn vs stowed decides the socket, which decides what the peer sees |

### 2.4 Player actions (`40-player-actions.md`)

| # | Class / function | Why it matters |
|---|---|---|
| 1 | `USpartaAbilitySystemComponent` — `SetFloatAttributeValue(Attribute: FGameplayAttribute, NewValue: float) -> bool`, `GetActiveAbilities()`, `GetActiveStatusEffects()`, `K2_GiveAbilityWithInputTag(AbilityClass, Level, InputTag)`, `PauseGameplayEffectDurationCountdowns(Query)` / `Resume…`, delegates `OnStatusEffectAdded/Removed`, `OnStackChanged`, `OnUpgradeStatChanged` | the write primitive for any attribute (`SetFloatAttributeValue`) plus the observability of effects |
| 2 | the 7 attribute sets, field-by-field with offsets: `SpartaHealthSet` (36 attrs), `SpartaCombatSet` (50), `PlayerAttributeSet` (32), `ShellAbilityAttributeSet` (32), `SidearmAttributeSet` (8), `DarkPowerAttributeSet` (4), `EnemyAttributeSet` (3); base `SpartaAttributeSet` is empty. **Only 6 attributes are `ReplicatedUsing`** (`SpartaHealthSet:OnRep_Health/OnRep_MaxHealth` + 4 on `SpartaCombatSet`) | the complete list of numbers that can differ between two copies of the same fight — and the proof that the game itself does *not* replicate them |
| 3 | `GA_PlayMontageBase_C:Montage` (`0x5E0`) + `MontageStartSection` (`0x600`), reached through the base ladder `SpartaGameplayAbility` → `SpartaAttackAbility` → `GA_SpartaBase_C` → `GA_PlayMontageBase_C` → `GA_AttackBase_C` → `GA_AttackBase_Melee_C` | **the** montage hook: one field to read (what is playing) and one identifier to ship (asset path + section, see `80-anim-catalog.md`) |
| 4 | `AbilityTask_SpartaWaitAttChange` + `Character:GetCurrentMontage()` | the "wait until an attribute changes" task and the montage cursor the shipped mod already serialises |
| 5 | two-actor abilities: `GA_Parry_Riposte_C` (`Victim`, `ParticipantSelectedMontage`) and `GA_Traversal_ShellThrow_C` (`ShellCharacter` + `PlayerAnimation` + `ShellAnimation`); `USpartaAnimNotify_HitCheck` / `SpartaMontageInterrupterComponent` | the only abilities that already bind **two** actors — the closest thing in the game to a co-op action, and the hit/interrupt moments a peer's visual disagrees with |

### 2.5 Enemies and bosses (`50-enemies-bosses.md`)

| # | Class / function | Why it matters |
|---|---|---|
| 1 | `ISpartaHittable::SpartaApplyHit(FHitResult, ASpartaCharacter* Instigator, USpartaHitPayload*, AActor* DamageCauser)` | the one entry point for "this actor got hit" — the natural place to replay a peer's hit |
| 2 | `USpartaHitPayload` (UObject, 0x158) — `HealthDamage`, `PoiseDamageOption`, `BreakDamageOption`, `DamageEffect`, `AdditionalEffects`, `DamageDataTag`, `ReactionTag`, `StrikeDirection`, `PoiseDamageEffect`, `BreakDamageEffect`, … | everything needed to reconstruct a hit faithfully |
| 3 | `ASpartaCharacter` delegates — `OnHitApplied` (0x8D8), `OnHitReceived` (0x8E8), `OnHealthDamageCaused` (0x8F8), `CharacterHit` (0x990), `CharacterDeath` (0x9A0), `OnTargetedBy` (0x9D0), `OnTeamChanged` (0x9F0) | passive observation of combat on either client, no polling |
| 4 | `USpartaHealthComponent` — `GetHealth`, `GetCurrentHealthNormalized`, `IsDeadOrDying`, `StartDeath`, `DeathState`, delegates `OnDamageReceived` / `OnDeathStarted` / `OnDeathFinished` / `OnShellHealthDepleted` | proven in-game: writing 50 % and calling `StartDeath()` both work (see `90-verified.md` §7) |
| 5 | `BPC_BossAI_C` — `OnBossAggro` (0xE8), `OnBossDeath` (0xD8), `OnSecondPhaseStarted` (0x118), `OnThirdPhaseStarted` (0x260), `SecondPhaseHealthPercent` (0xF8), `ThirdPhaseHealthPercent` (0x100), reward `VestigeItem` (0x238) / `ItemStacks` (0x250) | the boss-fight state machine: aggro, phases, death, rewards |

### 2.6 Items / inventory (`60-items-inventory.md`)

| # | Class / function | Why it matters |
|---|---|---|
| 1 | `USpartaItemManagerComponent` — `AddItemByClass(ItemDef, StackCount)`, `RemoveItemStacksByClass(ItemDef, NumToRemove)`, `GetItemStacksByClass(ItemDef)`, `GetAllItems()`, `ItemList: FSpartaItemList`, delegates `OnItemStacksUpdated` / `OnLootPickedUp` | the actual inventory; one delegate pair covers every pickup |
| 2 | `USpartaItemInstance` / `USpartaItemDefinition` + `FSpartaItemList` (FFastArraySerializer) | the per-stack record (definition class, stack count, `StatTags`) |
| 3 | `UPPItemsComponent` (passive/active items) — `PPItemLevels`, `PPItemFoundAmounts`, `ActivePPItem`, `EquipPPItem`, delegates `OnPPItem*`; saved in `PPItemsSaveData` | second inventory with its own save object |
| 4 | `UTarstoneComponent` / `TarstoneSaveData`, `USlayerSealComponent`, `UArmorComponent` | the equipment side of inventory, each with its own save object and delegates |
| 5 | `BP_ShopHandlerBase::OnTradeEvent(Widget, TradeType, TradeItem, TradeQuantity, TradeValue)` → `UpdateSaveData(...)` → `PostTradeEvent(...)`; `BPSO_ShopData.CachedItemsMap`; `BPFL_Tarforge:ChangeWeaponLevel(WeaponTag, Delta)` | there is **no** `Buy`/`Sell` function — every trade and every upgrade goes through these |

### 2.7 Flow / menus (`70-flow-menus.md`)

| # | Class / function | Why it matters |
|---|---|---|
| 1 | `WBP_MainMenu_C` — `ContinueGame()`, `StartNewGame(ActiveSlot, SkipPrologue)`, `LoadMap(LoadedIndex)`, `IsMenuOpen()`, `GetMenuCloseEvent()` | the exact functions a host/guest handshake has to bracket (already used by the lab: `ContinueGame ok=true`) |
| 2 | `GlobalEventSubsystem.GESFunctions:SubscribeToEvent(Object, EventClass, Filter, Event → bool)` with `GlobalEvent_OnMenuOpen_C` / `_OnMenuClose_C`, `GlobalEvent_Cinematic_Start_C` / `_End_C`, `BP_GlobalEvent_LoadingScreenCreated_C` / `_Completed_C`, `BP_GlobalEvent_StreamingScreenCompleted_C`, `GlobalEvent_CreditsFinished_C` | **one event bus announces every blocking moment** — this is the cheapest hand-over hook in the game |
| 3 | `USpartaSaveSubSystem` — `HasValidSessionSave()`, `HasValidSaveForProfileSlot(Slot)`, `SaveSession()`, `SaveToDisk(...)`, `GetSessionSave()` | "is there a session to continue", and the moment a save is committed |
| 4 | death chain: `SpartaHealthComponent:StartDeath()` → `GA_Death_Player_C:HandlePlayerDeath(GrantDeathState)` → `WBP_DeathScreen_C` (`OnDeathSelection`, `ResetWorld`, `CreateLoadingScreen`, `OnLoadingScreenCompleted`) → `GA_Death_Player_C:EndDeath()`; `ESpartaDeathState` = `NotDead=0`, `DeathStarted=1`, `DeathFinished=2` | death → screen → respawn; the hand-over point of control |
| 5 | `BP_CinematicPlayer_C` (`LevelSequenceToPlay` @0x2c8, `CutsceneActor` @0x2c0, multicast `CinematicEndReached` @0x2e8; `PlayCutscene`/`EndCutscene`/`ForceStopCinematic` take **no** parameters) + `USpartaCinematicAudioHandler.IsPlayingCinematic()` | the one actor that plays the 52 `LevelSequence`s — cutscene start/end brackets |
| — | **pause is not `SetPause`:** it is `BPFL_UI_C:EnableUIInput(..., PauseGame, ...)` / `DisableUIInput(..., UnpauseGame, ...)` + `SpartaPlayerController:SetShouldPerformFullTickWhenPaused(bEnabled)` | there is no `SetPause` wrapper anywhere in `/Script/Sparta` |

### 2.8 Animation catalogue (`80-anim-catalog.md`)

| # | Item | Why |
|---|---|---|
| 1 | `2979` `AnimSequence` + `2023` `AnimMontage` assets under `/Game/Sparta/Characters/` (complete list in `-part1`/`-part2`, CSV in `slices\anim_catalog.csv`) | the identifier space for "what is the peer playing" |
| 2 | `/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/` (405 sequences) — `Idles/A_Shared_Idle_H`, `Walk/H2/A_Shared_Locomotion_Walk_Fwd_H2`, `A_Shared_Locomotion_Sprint_800` | the sequences the shipped mod already plays on the peer body |
| 3 | per-weapon locomotion subfolders under the same folder (`Ballistazooka/`, `ClockworkScythe/`, …) plus the ABP layers at `/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion_*` | shell- and weapon-specific sets to choose from |
| 4 | `riposte` (290 assets) and `hit` (815) buckets | paired player/enemy anims; a hit replay must pick the same side |
| 5 | `vo` (131) and `cinematic` (127) buckets | must **not** be replayed by a peer-visual layer |

---

## 3. Cross-area facts a sync layer needs (and where they are proven)

1. **Actor identity.** Actors are named `<Class>_UAID_<hash>_<hash>_<int>` and the tail is stable
   across launches and machines (same save) — `90-verified.md` §2 vs §11 shows the identical
   `BP_HubDoubleDoor_C_UAID_D4E98A7EF567D94802_83a1dc45e2a01438_1808979936` in two separate runs;
   `ms2-coop\docs\DESIGN.md` §"Matching the peer's actors" documents the same for NPCs.
   A save object hangs off its actor as `<ActorName>.SpartaSave.RuntimeData_<n>`.
2. **Save-scope.** `SpartaSaveObject.bIsSessionSaveData` (0xb1) and the subsystem's
   `SessionSaveObject` / `RuntimeSessionSaveObject` vs `CurrentOnDiskWorldStateSave` separate
   *per-session (per-player)* state from *per-world* state — `10-world-state.md` §1.
3. **The four save files** on disk are `MetaSaveGame.sav`, `SpartaGameSettings.sav`,
   `WorldState_0.sav` (+ `WorldState_Backup_0_<n>.sav`); entering gameplay rewrites
   `MetaSaveGame.sav` and creates a backup copy — `00-sources.md` §3.
4. **No replication metadata exists anywhere in the dump** — you cannot ask the engine who owns a
   value; ownership must be assigned by the mod (`60-items-inventory.md` UNKNOWN #1).
5. **Tag-driven APIs cannot be exercised from Lua** in this UE4SS build (`FGameplayTag` not
   constructible), while class/actor-driven APIs can be called by name — `90-verified.md` §10, §13.
6. **`FindAllOf(<ClassShortName>)` is the usable discovery primitive**; objects that live under
   `/Engine/Transient` (save objects, subsystem state) are found too but are *not* in
   `…:PersistentLevel.…` — the lab filters on that to separate world actors from state objects
   (`90-verified.md` preamble).
7. **Every layout fact here has two independent sources.** The Dumper-7 SDK agrees with
   `CXXHeaderDump` on all **78 740** compared property slots (all-assets pair), on all parents and
   on all class sizes (0 conflicts; 1 953 sizes differ only by alignment rounding), and its 97
   Sparta enums are value-identical — `05-dumper7-vs-ue4ss.md` §6. The same dump also proves the build string
   `5.6.1-0+++Sparta-Depot+Day1+CL92086` that `DESIGN.md` carried over, and gives the
   `ProcessEvent` index (`0x4C`) and `GWorld` offset (`0xAD4E7F0`).

---

## 4. Open questions (aggregated; every report has its own numbered list)

| # | Question | Where |
|---|---|---|
| 1 | Nothing in the dump marks which values are replicated/authoritative; ownership of every counter must be decided by the mod. | `60-*` #1, `40-*`, `10-*` §11 |
| 2 | `FGameplayTag` cannot be built from Lua → tag-driven functions (`ChangeShell`, `WriteXxxByTags`, `K2_GiveAbilityWithInputTag`) are unusable until someone finds a construction route (or a native wrapper). | `90-verified.md` §10, §13 |
| 3 | Behaviour-tree ↔ controller binding and ability → montage mapping are **not** in the dump (UPROPERTY values are absent); needs a live CDO read. | `50-*` #1, #2 |
| 4 | Chest state (`BP_SO_SimpleBool_C.MyBool` behind `GetMySaveData()`) could not be read back in-game; the lab needs a `getprop <object path> <prop>` command. | `90-verified.md` §5, §13 |
| 5 | `AreaName` (checkpoint), `LocationName`/`Category` (teleports), `DoorIsOpenName`, `Guid`, `BeaconRisen` come back as opaque userdata from this UE4SS build — formatting FText/FName/FGuid is unsolved. | `90-verified.md` §2, §6, §8, §11 |
| 6 | Rest → respawn wiring (who respawns enemies and when) was not established; no `Respawn`/`Rest` function exists in `/Script/Sparta`. | `50-*` #5 |
| 7 | The damage execution calculations (`SpartaDamageExecution`, `SpartaBreakExecution`, `SpartaPoiseExecution`, `SpartaHealExecution`) are empty in reflection — the formulas are C++-only, so a replayed hit cannot be re-derived, only replayed. | `50-*` #4 |
| 8 | Enumerator *names* for several gameplay enums (`Enum_ShopType`, `E_ShopItemType`, `E_LootType`, `E_UpgradePoolPolicy`, `EUpgradeType`, `EStatusEffectOverlayPolicy`, `EBloodMaskApplyMode`) are not dumped — only `NewEnumerator0..N`. | `60-*` #2, `30-*` §8 |
| 9 | `UPlayerInventoryComponent` (`/Script/Sparta.PlayerInventoryComponent`) is an empty class; what (if anything) attaches it is unknown. | `60-*` #3, `05-*` §7.1 |
| 10 | Quest/dialogue state was not exercised in game (needs player-driven dialogue); the classes and save objects are documented, the runtime behaviour is not. | `20-*`, `90-verified.md` §13 |
| 11 | Dumper-7 dumps of the heavy states need a **locally patched Dumper-7** (upstream v7.1.0 fails: GObjects chunk cap → fastfail in the all-assets state, `InitGWorld` full-memory scans → stall in gameplay). With the patch set (`tools\dumper7-patches.diff`) both succeed: all-assets 16 040 classes, gameplay 9 441. | `05-*` §3, §7.1 |

---

## 5. How to reproduce

```powershell
# the three dumps (test copy only; the real install is never touched)
cd G:\Freespace\shell_coop\research\scripts
& .\run-dump.ps1 -Mode obj      # main menu, LoadAllAssetsBeforeDumpingObjects=1  -> raw\ObjectDump_allassets.txt
& .\run-dump.ps1 -Mode cxx      # main menu, LoadAllAssetsBeforeGeneratingCXXHeaders=1 -> raw\CXXHeaderDump
& .\run-dump.ps1 -Mode ingame   # gameplay, no asset preload               -> raw\ObjectDump_ingame.txt

# indexes
python parse_dump.py index ..\raw\ObjectDump_allassets.txt ..\raw\index.sqlite3
python parse_dump.py index ..\raw\ObjectDump_ingame.txt    ..\raw\index_ingame.sqlite3

# the in-game probes from 90-verified.md (one session each, saves snapshotted+restored)
& G:\Freespace\shell_coop\ms2-coop\lab.ps1 -CommandFile ..\scripts\verify1.txt -BootWait 60 -Tag verify1
& G:\Freespace\shell_coop\ms2-coop\lab.ps1 -CommandFile ..\scripts\verify2.txt -BootWait 60 -Tag verify2
& G:\Freespace\shell_coop\ms2-coop\lab.ps1 -CommandFile ..\scripts\verify3.txt -BootWait 75 -Tag verify3

# the second dumper: patched build from tools\dumper7-src (git apply ..\dumper7-patches.diff,
# cmake -G "Visual Studio 17 2022" -A x64, --config Release) -> tools\dumper7\Dumper-7-patched.dll
& .\run-dumper7.ps1 -Mode menu                                            # -> raw\Dumper7
& .\run-dumper7.ps1 -Mode allassets -DllPath ..\..\tools\dumper7\Dumper-7-patched.dll
& .\run-dumper7.ps1 -Mode gameplay  -DllPath ..\..\tools\dumper7\Dumper-7-patched.dll

# checks
python d7_sdk_build_check.py --sdk ..\raw\Dumper7 --out ..\..\local\sdkbuild\menu
python d7_compare.py --d7 ..\raw\Dumper7_allassets\5.6.1-0+++Sparta-Depot+Day1+CL92086-MortalShell2\CppSDK\SDK --ue4ss ..\raw\CXXHeaderDump --json ..\slices\d7-vs-ue4ss-allassets.json
python d7_enum_compare.py ..\raw\Dumper7_allassets\5.6.1-0+++Sparta-Depot+Day1+CL92086-MortalShell2\CppSDK\SDK\Sparta_structs.hpp ..\raw\CXXHeaderDump\Sparta_enums.hpp
```

`run-dump.ps1` sets the ini keys, starts `lab.ps1`, waits until the output file stops growing, kills
the game, copies the artefact into `raw\`, restores the ini and re-checks the owner's saves.

## 6. Tooling in `scripts\`

| File | Purpose |
|---|---|
| `QUERYING.md` | **start here** — dump line format, the SQLite schema, how to read a function, evidence rules |
| `REPORT-TEMPLATE.md` | the report shape every file follows |
| `parse_dump.py` | `index` / `classes` / `show` / `fun` / `find` / `raw` / `members` over the dump or the index |
| `q.py` + `q\*.sql` | run a SQL file against the index (150+ query files, one set per report area) |
| `clsinfo.py` | compact property + function listing for planning probes |
| `anim_catalog.py`, `anim_folders.py`, `make_anim_report.py` | the step-8 catalogue and its markdown |
| `check_dump.py` | line/type/package statistics + "did any line fail to parse" |
| `repair_evidence.py` | audit: every `[addr] Type /path` line in a report is looked up in the index; `--apply` re-emits it byte-exactly from the dump |
| `run-dump.ps1` | the dump runner described above |
| `run-dumper7.ps1` | the Dumper-7 runner (same save/ini protocol; `-Mode menu\|gameplay\|allassets`) |
| `inject-dll.ps1` | x64 LoadLibraryW injector used to load `Dumper-7.dll` into the running game |
| `d7_compare.py` | UE4SS-vs-Dumper-7 class/property/offset diff (`--d7`, `--ue4ss`, `--json`, `--key`) |
| `d7_enum_compare.py` | Sparta enum-value comparison between the two dumps (`<structs.hpp> <enums.hpp> [out.json]`) |
| `d7_sdk_build_check.py` | MSVC-only compile check of a generated SDK (reuses Dumper-7's own CMakeLists/TestMain) |
| `verify1.txt` / `verify2.txt` / `verify3.txt` | the CoopLab command files behind `90-verified.md` |

Quality gates actually run on this folder (both scripts are in `scripts\`):

* `python scripts\repair_evidence.py` — every line that parses as a dump line is looked up by address
  and compared with the index: **3597 evidence lines, all byte-identical, 0 wrong, 0 unknown
  addresses**. (The tool rewrote 3594 of them once, because lines rebuilt from SQL had lost the
  single space before `[n:`; the reports now quote the dump exactly.)
* `python scripts\audit_paths.py` — every `/Game/...` and `/Script/...` token in a report is checked
  against the index: 3214 paths, ~1 % flagged, and the flagged ones inspected by hand turned out to
  be legitimate shorthand (`…/GA_SpartaAbility_C` for `…/GA_SpartaAbility.GA_SpartaAbility_C`) or a
  dumper quirk (`NameProperty ActorInts./Script/Sparta.SpartaSaveGame:ActorInts_Key`), not invented
  objects.

## 7. What was changed in the repo this round

* `ms2-coop\mod\CoopLab\Scripts\main.lua` — four additive changes only (backup of the previous file:
  `local\main.lua.CoopLab.bak-before-dumps`): the `dumpobj` / `dumpcxx` / `dumpsm` commands, the
  `HOLD_MENU.txt` flag that keeps the lab on the main menu, and the `scan` / `getp` / `callf` probe
  commands used by `90-verified.md`. Default behaviour without those files/commands is unchanged.
* `ms2-coop\mod\CoopMod\Scripts\main.lua` — **not touched**.
* the second dumper: `tools\dumper7\Dumper-7.dll` (upstream release), `tools\dumper7\Dumper-7-patched.dll`
  (our build), `tools\dumper7-src\` (patched source tree) + `tools\dumper7-patches.diff`,
  `research\scripts\{run-dumper7,inject-dll}.ps1` and `d7_{compare,enum_compare,sdk_build_check}.py`.
  Nothing in `ms2-coop\` changed for it (`Dumper-7.dll`/`.ini` are staged into the test install by
  the runner and removed again, see `05-dumper7-vs-ue4ss.md` §12).
* The real install `E:\Games\Mortal Shell II` — **not touched**; both test installs are back to
  `LoadAllAssetsBefore* = 0`.
* Owner's saves: snapshotted before every run and restored. The three files are now back on the
  06:01 baseline and hash-checked against it (`MetaSaveGame.sav 0C94B2DC…`,
  `SpartaGameSettings.sav 47095B2B…`, `WorldState_0.sav 96EDA857…`). They had drifted: a game
  session wrote two of them at `2026-10-22 14:57:49`, before this round's Dumper-7 runs; the
  restore was made on explicit instruction and the replaced state
  (`47CC875E…` / `46C3EFD0…`) is kept in
  `local\save-backups\20261022-194547-pre-restore-of-14-57-state\`. Earlier script-made
  `WorldState_Backup_0_*.sav` copies were moved to
  `local\save-backups\20261022-062129-rundump-ingame\extras-created-by-game\`.
