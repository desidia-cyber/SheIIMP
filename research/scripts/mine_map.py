#!/usr/bin/env python
"""mine_map.py - the curated sync-target map with machine-resolved offsets, signatures and proof lines.

Every entry below names an *exact* member or function of an *exact* class.  The script looks the
name up in the UE4SS object index and refuses to emit a row it cannot resolve, so a typo in this
file becomes a hard error instead of a silent hole in the documentation.

Output:
  research/mine/sync-map.tsv                    machine-readable target map
  research/mine/sync-map.md                     the same as markdown tables, per area

Run: python scripts\\mine_map.py
"""
import os
import sys
import collections

HERE = os.path.dirname(os.path.abspath(__file__))
RESEARCH = os.path.dirname(HERE)
MINE = os.path.join(RESEARCH, 'mine')
sys.path.insert(0, HERE)

import mine  # noqa: E402

E = '/Game/Sparta/'
PC = E + 'Core/Player/BP_PlayerController.BP_PlayerController_C'
SAVEOBJ = E + 'Core/Player/Save/BP_SpartaPlayerSaveObject.BP_SpartaPlayerSaveObject_C'

# (area, class, kind, [members])
SPEC = [
    # ---------------------------------------------------------------- peer body state
    ('BODY', '/Script/Sparta.SpartaHealthSet', 'prop',
     ['Health', 'MaxHealth', 'ShellHealth', 'MaxShellHealth', 'MinHealth', 'MinShellHealth',
      'Armor', 'Resolve', 'MaxResolve', 'Poise', 'MaxPoise', 'BreakResistance',
      'MaxBreakResistance', 'StoneStunTime', 'ResolveModify']),
    ('BODY', '/Script/Sparta.SpartaHealthComponent', 'prop',
     ['DeathState', 'AbilitySystemComponent', 'HealthSet', 'OnDamageReceived', 'OnHealingReceived',
      'OnMaxHealthChanged', 'OnShellHealthDepleted', 'OnDeathStarted', 'OnDeathFinished']),
    ('BODY', '/Script/Sparta.SpartaHealthComponent', 'func',
     ['GetHealth', 'GetHealthNormalized', 'GetShellHealth', 'GetShellHealthNormalized',
      'GetMaxHealth', 'GetMaxShellHealth', 'GetCurrentHealthNormalized', 'GetDeathState',
      'IsDeadOrDying', 'StartDeath', 'FindHealthComponent']),
    ('BODY', '/Script/Sparta.PlayerAttributeSet', 'prop',
     ['CurrentEXP', 'HealCurrentCharges', 'HealMaxCharges', 'HealAmountPerCharge',
      'ShellReviveCurrentProgress', 'ShellReviveMaxProgress', 'ShellReviveProgressGain',
      'PPItemResource', 'PPItemMaxResource', 'PPItemCost', 'BaseResolveGain', 'BaseResolveLost',
      'GloomMultiplier', 'GoldMultiplier']),
    ('BODY', '/Script/Sparta.SpartaCombatSet', 'prop',
     ['BaseDamage', 'BaseHeal', 'BasePoiseDamage', 'BaseBreakDamage', 'AttackSpeedBonus',
      'BaseDamageReduction', 'RiposteDamageMultiplier']),
    ('BODY', '/Script/Sparta.SidearmAttributeSet', 'prop',
     ['CurrentAmmo', 'MaxAmmo', 'ReloadDuration', 'ActivationCost']),
    ('BODY', '/Script/Sparta.DarkPowerAttributeSet', 'prop',
     ['CurrentAmmo', 'MaxAmmo', 'ReloadDuration', 'ActivationCost']),
    ('BODY', '/Script/Sparta.ShellAbilityAttributeSet', 'prop',
     ['Stacks', 'CurrentAmount', 'Cooldown', 'HealingMeter', 'MaxHealingMeter']),
    ('BODY', '/Script/Sparta.SpartaCharacter', 'prop',
     ['HealthComponent', 'ItemManagerComponent', 'WeaponsComponent', 'ArmorComponent',
      'CharacterId', 'AbilitySystemComponent', 'Team', 'CharacterData', 'CurrentTarget',
      'CharacterDeath', 'OnHitApplied', 'OnHitReceived', 'CharacterTeleport', 'CharacterAggro',
      'CharacterDodge', 'CharacterRoll', 'CharacterAttack', 'CharacterHit']),
    ('BODY', '/Script/Sparta.SpartaHitPayload', 'prop',
     ['HealthDamage', 'PoiseDamageOption', 'BreakDamageOption', 'DamageDataTag', 'ReactionTag',
      'StrikeDirection', 'PoiseDamage', 'BreakDamage', 'PayloadTags', 'CustomElementalStacks',
      'ChosenReactionTag', 'CalculatedDamage', 'DamageDealt']),
    ('BODY', '/Script/Sparta.SpartaHitPayload', 'func',
     ['CalculateDamage', 'CalculatePoiseDamage', 'CalculateBreakDamage', 'ApplyPayload',
      'ApplyPayloadInstigator', 'MergePayloads', 'GetDuplicate', 'SetStrikeDirection',
      'SetReactionTag', 'SetDamageDataTag']),
    ('BODY', '/Script/Sparta.SpartaHittable', 'func',
     ['SpartaApplyHit', 'GetHitResponse']),
    ('BODY', '/Script/Sparta.ArmorComponent', 'prop', []),

    # ---------------------------------------------------------------- session mirror + revive
    ('SESSION', E + 'SaveObjects/BPSO_PlayerSession.BPSO_PlayerSession_C', 'prop',
     ['Resolve', 'Health', 'ShellHealth', 'HealCurrentCharges', 'ShellReviveProgress',
      'ShellReviveMaxProgress', 'StartTransform', 'IsInDarkform', 'AbilitySystemComponent',
      'PlayerPawnCache']),
    ('SESSION', E + 'SaveObjects/BPSO_PlayerSession.BPSO_PlayerSession_C', 'func',
     ['SaveHealth', 'SaveResolve', 'SaveShellHealth', 'SaveHealingCharges', 'SaveShellRevive',
      'SaveIsDarkForm', 'GetSessionStartTransform', 'CheckForValidTransform']),
    ('SESSION', '/Script/Sparta.SpartaSessionSaveObjectBase', 'prop', []),
    ('SESSION', E + 'Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C', 'prop', []),

    # ---------------------------------------------------------------- progression (player save)
    ('PROG', SAVEOBJ, 'prop',
     ['CumulativeGloom', 'CumulativeGold', 'CumulativeGlimpses', 'TotalDeaths',
      'TotalBossesDefeated', 'FinishedPrologue', 'SkippedPrologue', 'PastTarGolemFight',
      'NewGamePlus', 'CurrentSpawnLocation', 'CurrentPrologueCheckpoint',
      'ActiveDarkFormItemDefinition', 'ActiveShellItemDefinition', 'ActiveSealItemDefinition',
      'ActiveWeaponItemDefinition', 'ActiveSidearmItemDefinition', 'ActiveChargesItemDefinition',
      'ActiveDarkFormSkinID', 'ActiveShellSkins', 'UnlockedSkins', 'ActiveDarkFormMask',
      'ActiveShellMask', 'UnlockedMasks', 'ActiveDarkFormClothing', 'ActiveShellClothing',
      'UnlockedClothing', 'EquipmentUnlockState', 'UnlockedLandingAreas', 'CleansedLandingAreas',
      'CinematicsWatched', 'EarnedAchievements', 'ShownTutorials', 'SurpriseEggsFound',
      'SoftItemStatus', 'AttentionGrabberStatus', 'PlayedLuteTracks', 'ProximaBiosampler',
      'GraguManiac', 'DropItems', 'ItemPoolState', 'ActorBools', 'SlayerSealLockout',
      'bHasLostGloom', 'LostGloomAmount', 'LostGloomLocation', 'LostGloomBiome',
      'CurrentHubEventPoints', 'TotalHubEventPoints', 'GiftEventPointBank',
      'CurrentGiftEventLevel', 'StoryCompletionActivityStarted', 'StoryCompletionActivityCompleted']),
    ('PROG', SAVEOBJ, 'func',
     ['SetActiveShell', 'SetActiveWeapon', 'SetActiveSidearm', 'SetActiveSeal', 'SetActiveDarkForm',
      'SetActiveCharges', 'AddUnlockedEquipment', 'GetEquipmentUnlockState', 'SetFinishedPrologue',
      'GetFinishedPrologue', 'IncrementCumulativeCurrency', 'IncrementTotalDeaths',
      'AddDropItems', 'RemoveDropItems', 'UpdateItemPoolState', 'UpdateUnlockedLandingAreas',
      'UpdateCleansedLandingAreas', 'IsLandingAreaUnlocked', 'IsLandingAreaCleansed',
      'SetCinematicWatched', 'CheckIfCinematicWatched', 'SetAchievementEarned',
      'IsAchievementEarned', 'SetLuteTrackPlayed', 'ResetForNewGamePlus']),
    ('PROG', '/Script/Sparta.PlayerSaveObject', 'prop',
     ['TotalPlayTimeSeconds', 'CompletedSpawnTriggerRecipes', 'CompletetedSpawnTriggers',
      'ShellStats', 'WeaponStats', 'PlayerStats', 'GameLibraryData', 'OverrideWorldGenTags',
      'Difficulty', 'LastSaveDateTime']),
    ('PROG', '/Script/Sparta.PlayerSaveObject', 'func',
     ['GetPlayerStat', 'IncreasePlayerStat', 'GetShellStat', 'IncreaseShellStat',
      'GetWeaponStat', 'IncreaseWeaponStat', 'ClearPlayerStats', 'ClearWeaponStats']),
    ('PROG', E + 'SaveObjects/BPSO_Progression.BPSO_Progression_C', 'prop',
     ['CurrentLevel', 'AvailablePoints', 'AssignedPoints', 'BondingPoints', 'GoldSpentAtShop']),
    ('PROG', '/Script/Sparta.PlayerProgressionSaveData', 'prop', []),
    ('PROG', '/Script/Sparta.PlayerExpComponent', 'prop',
     ['PlayerLevel', 'UpgradeLevels', 'OwnedUpgradeStats', 'PlayerUpgrades', 'PlayerMetaUpgrades',
      'OnCurrentEXPChanged', 'OnUpgradeSelected', 'PlayerLevelUpdated']),
    ('PROG', '/Script/Sparta.PlayerExpComponent', 'func',
     ['GetUpgradeLevel', 'SetUpgradeLevel', 'AddUpgradeLevel', 'HasUpgrade', 'HasAnyUpgrade',
      'HasAllUpgrades', 'IsUpgradeAtMaxLevel', 'GetAvailableUpgradePoints', 'RemoveUpgradeData']),
    ('PROG', '/Script/Sparta.UpgradesComponent', 'func',
     ['GetUpgradeLevel', 'AddUpgradeLevel', 'IsUpgradeAtMaxLevel']),
    ('PROG', '/Script/Sparta.PlayerProgressionComponent', 'prop',
     ['ShellBuilds', 'TierBuilds', 'SkillLevels', 'EquippedSkillInstances', 'OnSkillEquipped',
      'OnSkillUnequipped', 'OnSkillLevelChanged']),
    ('PROG', '/Script/Sparta.PlayerProgressionComponent', 'func',
     ['GetSkillLevel', 'SetSkillLevel', 'AddToSkillLevel', 'HasSkill', 'EquipSkill', 'UnequipSkill',
      'GetTierSkills', 'AddTierSkill', 'HasTierSkill', 'RefundAllSkills']),
    ('PROG', '/Script/Sparta.TarstoneComponent', 'prop',
     ['TarcoreItemDefinition', 'TarstoneBuilds', 'TarstoneLevels', 'TarstoneSupportBuild',
      'EquippedTarstoneItemInstances', 'EquippedSupportTarstoneItemInstances',
      'OnTarstoneLevelUp', 'OnTarstoneEquip', 'OnTarstoneUnequip']),
    ('PROG', '/Script/Sparta.TarstoneComponent', 'func',
     ['GetCurrentTarstoneBuildForSlot', 'EquipTarstone', 'UnequipTarstone',
      'EquipSupportTarstone', 'AddTarstoneExp', 'AddTarstoneLevel', 'ForceAddTarstoneLevel',
      'AddTarstoneToInventory', 'SetTarstoneLevel']),
    ('PROG', '/Script/Sparta.PPItemsComponent', 'prop',
     ['ActivePPItem', 'PassivePPItem', 'PPItemLevels', 'PPItemFoundAmounts',
      'PPItemFragmentDataBySlot', 'OnPPItemEquipped', 'OnPPItemLevelChanged']),
    ('PROG', '/Script/Sparta.PPItemsComponent', 'func',
     ['GetPPItemLevel', 'SetPPItemLevel', 'AddToPPItemLevel', 'EquipPPItem', 'UnequipPPItem',
      'GetEquippedPPItem', 'AddToPPItemFoundAmount', 'GetPPItemFoundAmount']),
    ('PROG', '/Script/Sparta.WeaponsComponent', 'prop',
     ['EquippedWeapons', 'DefaultWeapons', 'OnWeaponEquipped', 'OnWeaponUnequipped']),
    ('PROG', '/Script/Sparta.WeaponsComponent', 'func',
     ['GetWeaponInSlot', 'GetWeaponInHand', 'GetAllWeapons', 'SpawnAndEquipWeaponOfClass',
      'EquipWeaponToSlot', 'RemoveAllWeapons']),
    ('PROG', E + 'SaveObjects/BPSO_ShellMemories.BPSO_ShellMemories_C', 'prop',
     ['MemoryProgress', 'MemoriesWatched']),
    ('PROG', E + 'Core/Player/Components/BPC_Player_Progression.BPC_Player_Progression_C', 'prop', []),

    # ---------------------------------------------------------------- economy write API
    ('ECON', PC, 'func',
     ['S_AddGold', 'S_AddGloom', 'S_AddGlimpses', 'S_AddOvums', 'S_AddTarcores', 'S_AddVentrium',
      'S_AddLaterite', 'S_AddDorsalite', 'S_AddThoracium', 'S_AddThoraciumPrime',
      'S_AddShellPoints', 'S_UpdateBondingPoints', 'S_AddUpgrade', 'S_RemoveUpgrade',
      'S_AddWeaponLevel', 'S_AddSidearmLevel', 'S_AddItem', 'S_AddItemQuantity', 'S_AddAllItems',
      'S_UnlockShell', 'S_UnlockAllShells', 'S_SwitchToShell', 'S_UnlockWeapon',
      'S_UnlockAllWeapons', 'S_SwitchToWeapon', 'S_UnlockSidearm', 'S_UnlockAllSidearms',
      'S_SwitchToSidearm', 'S_UnlockSeal', 'S_UnlockAllSeals', 'S_SwitchToSeal',
      'S_UnlockAllClothing', 'S_UnlockAllMasks', 'S_UnlockShellShades', 'S_UnlockDarkFormShades',
      'S_Heal', 'S_GainResolve', 'S_ModifyHealingCharge', 'S_ReviveShell', 'S_KillMe',
      'S_SetDarkBroLevel', 'S_DealDamage', 'S_CompletePrologue', 'S_IncrementMemoryProgress',
      'S_StartMemory', 'S_EndActiveMemory', 'S_AddNarrativeTag', 'S_RemoveNarrativeTag',
      'S_UnlockFastTravel', 'S_UnlockMap', 'S_UnlockAllLandingAreas', 'S_UnlockAllGates',
      'S_ResetSaveObjectByGuid', 'S_ResetAllSpawnerSaveObjects', 'S_GiveTarforgeUnlockItems',
      'S_UnlockTarforgeModes', 'S_RechargePPItem', 'S_TarstoneLevelIncrementall',
      'S_AddAllTarstonesMelee', 'S_AddAllTarstonesSidearm', 'S_AddAllTarstonesSupport',
      'S_ResetForNewGamePlus', 'S_PostGolemFightSettings']),
    ('ECON', PC, 'prop',
     ['PlayerSaveGameObject', 'Progression', 'Quest', 'Narrative', 'Achievements', 'PP', 'Shell',
      'Hub', 'World', 'Teleport', 'Combat', 'Gameplay',
      'ActiveShellItemDefinition', 'ActiveWeaponItemDefinition', 'ActiveSidearmItemDefinition',
      'ActiveDarkFormItemDefinition', 'ActiveSealItemDefinition',
      'PrimaryChargesItemDefinition', 'SecondaryChargesItemDefinition',
      'ActiveShellItemHandle', 'ActiveWeaponItemHandle', 'ActiveSidearmItemHandle',
      'ActiveDarkformItemHandle', 'ActiveSealItemHandle', 'IsPlayerSaveLoaded',
      'SelectedBiome', 'SelectedSeason']),

    # ---------------------------------------------------------------- inventory / items
    ('INV', '/Script/Sparta.SpartaItemManagerComponent', 'prop',
     ['ItemList', 'OnItemStacksUpdated', 'OnLootPickedUp']),
    ('INV', '/Script/Sparta.SpartaItemManagerComponent', 'func',
     ['GetAllItems', 'GetItemStacksByClass', 'GetTotalItemCountByDefinition',
      'FindFirstItemStackByClass', 'CanAddItemByClass', 'AddItemByClass', 'AddItemInstance',
      'RemoveItemStacksByClass', 'RemoveItemStacksByInstance', 'RemoveItemInstance']),
    ('INV', '/Script/Sparta.SpartaItemInstance', 'prop',
     ['ItemDef', 'FragmentData', 'StatTags']),
    ('INV', '/Script/Sparta.SpartaItemInstance', 'func',
     ['GetItemDef', 'FindFragmentByClass', 'AddStatTagStack', 'RemoveStatTagStack',
      'GetStatTagStackCount', 'HasStatTag']),
    ('INV', '/Script/Sparta.SpartaItemDefinition', 'prop', ['DisplayName', 'Fragments']),
    ('INV', '/Script/Sparta.SpartaItemFunctionLibrary', 'func', []),
    ('INV', '/Script/Sparta.PlayerInventoryComponent', 'prop', []),
    ('INV', E + 'Core/Player/BPFL_Player.BPFL_Player_C', 'func', []),

    # ---------------------------------------------------------------- world state A (save objects)
    ('STORE-A', '/Script/Sparta.SpartaSaveObject', 'prop',
     ['PrimarySaveObjectTick', 'IgnoreSaveGameMetaSpecifier', 'bIsSessionSaveData']),
    ('STORE-A', '/Script/Sparta.SpartaSaveObject', 'func',
     ['UpdateFrom', 'ReceiveTick', 'ReceiveSpartaPreSave', 'ReceiveOnLoaded', 'ReceiveApplySkipProloguePreset',
      'GetOwningSaveComponent', 'GetOwningActor', 'SetTickEnabled', 'SetTickInterval',
      'IsTickEnabled', 'IsSessionSaveData']),
    ('STORE-A', '/Script/Sparta.SpartaSaveComponent', 'prop',
     ['SaveObjectClass', 'SaveProtocol', 'Guid', 'RuntimeData', 'bSaveLoaded', 'OnSaveLoaded',
      'OnPreSave', 'OnSessionReset', 'bApplyPrologueSkipPreset']),
    ('STORE-A', '/Script/Sparta.SpartaSaveComponent', 'func',
     ['Save', 'ResetSaveData', 'IsSaveLoaded', 'GetRuntimeData', 'GetGUID', 'GenerateNewGuid']),
    ('STORE-A', '/Script/Sparta.SpartaSaveContext', 'prop', []),
    ('STORE-A', '/Script/Sparta.SpartaSaveInterface', 'func', []),

    # ---------------------------------------------------------------- world state B
    ('STORE-B', '/Script/Sparta.SpartaSaveGame', 'prop',
     ['ChangedActors', 'ActorFloats', 'ActorInts', 'ActorNames', 'ActorBools', 'ActorObjectMap',
      'ActorObjectMapAsString']),
    ('STORE-B', '/Script/Sparta.SpartaWorldStateHelper', 'func',
     ['ReadAnInt', 'ReadAnIntByTag', 'ReadAFloat', 'ReadAFloatByTag', 'ReadAName',
      'ReadABool', 'ReadABoolByTag', 'ReadASaveObject', 'ReadMyInt', 'ReadMyIntByTag',
      'ReadMyFloat', 'ReadMyFloatByTag', 'ReadMyName', 'ReadMyBool', 'ReadMyBoolByTag',
      'ReadMySaveObject', 'ReadTheirInt', 'ReadTheirIntByTag', 'ReadTheirFloat',
      'ReadTheirFloatByTag', 'ReadTheirName', 'ReadTheirBool', 'ReadTheirBoolByTag',
      'ReadTheirSaveObject', 'ReadPlayerInt', 'ReadPlayerFloat', 'ReadPlayerName',
      'ReadPlayerBool', 'ReadIntByTags', 'ReadFloatByTags', 'ReadBoolByTags', 'ReadNameByTags',
      'WriteAnInt', 'WriteAnIntByTag', 'WriteAFloat', 'WriteAFloatByTag', 'WriteAName',
      'WriteABool', 'WriteABoolByTag', 'WriteASaveObject', 'WriteMyInt', 'WriteMyIntByTag',
      'WriteMyFloat', 'WriteMyFloatByTag', 'WriteMyName', 'WriteMyBool', 'WriteMyBoolByTag',
      'WriteMySaveObject', 'WriteTheirInt', 'WriteTheirIntByTag', 'WriteTheirFloat',
      'WriteTheirFloatByTag', 'WriteTheirName', 'WriteTheirBool', 'WriteTheirBoolByTag',
      'WriteTheirSaveObject', 'WritePlayerInt', 'WritePlayerFloat', 'WritePlayerName',
      'WritePlayerBool', 'WriteIntByTags', 'WriteFloatByTags', 'WriteBoolByTags',
      'WriteNameByTags', 'AddToChangedSet', 'RemoveFromChangedSet', 'IsInChangedSet',
      'AmIChanged', 'AreTheyChanged', 'AddMeToChangedSet', 'AddThemToChangedSet',
      'RemoveMeFromChangedSet', 'RemoveThemFromChangedSet', 'ResetAllActorInts',
      'GetSpartaActorSaveObject', 'GetPlayerUniqueNameID', 'GetPlayerDeathCounterName',
      'GetPlayerSaveObjectFromSave']),

    # ---------------------------------------------------------------- persistence / registry
    ('PERSIST', '/Script/Sparta.SpartaSaveSubSystem', 'prop',
     ['SaveObjects', 'CurrentOnDiskWorldStateSave', 'SessionSaveObject', 'RuntimeSessionSaveObject',
      'MetaData', 'bSaveLoaded', 'OnSaveGameLoaded', 'OnSaveStarted', 'OnSaveFinished',
      'PostSessionReset', 'OnSaveSystemInitialized']),
    ('PERSIST', '/Script/Sparta.SpartaSaveSubSystem', 'func',
     ['GetCurrentWorldStateSave', 'GetIsSaveLoaded', 'GetSessionSave', 'HasValidSessionSave',
      'IsAsyncSaving', 'SaveToDisk', 'SaveSession', 'AutoSave', 'SaveMetaData',
      'WriteCurrentSaveToTextFile', 'ResetSaveObjectByGuid', 'ResetSaveObjectByClass',
      'ResetSession', 'ResetAllSaveData', 'ResetAutoSaveTimer', 'SetPreventFurtherSaves',
      'EnableAutoSave', 'DisableAutoSave', 'GetPlayerSaveObjectFromSlotIndex',
      'GetSaveObjectByClassFromSlotIndex', 'GetInstance', 'SubscribeSaveInterface',
      'UnsubscribeSaveInterface', 'ChangeActiveSaveProfileSlot',
      'GetActiveSaveProfileSlotIndex', 'GetActiveSaveProfileSlotName']),

    # ---------------------------------------------------------------- data layers
    ('LAYERS', '/Script/Sparta.SpartaDataLayerSubsystem', 'prop', ['RuntimeData']),
    ('LAYERS', '/Script/Sparta.SpartaDataLayerSubsystem', 'func',
     ['SaveDataLayerState', 'RemoveDataLayerState']),
    ('LAYERS', '/Script/Sparta.DataLayersSave', 'prop', []),
    ('LAYERS', '/Script/Sparta.DataLayerFunctionLibrary', 'func', []),

    # ---------------------------------------------------------------- quests / narrative
    ('QUEST', '/Script/Sparta.QuestObject', 'prop',
     ['QuestID', 'CurrentQuestStage', 'CurrentQuestObjectiveProgress', 'CurrentStageDetails',
      'QuestDetails']),
    ('QUEST', '/Script/Sparta.QuestObject', 'func', ['InitializeQuest']),
    ('QUEST', '/Script/Sparta.QuestObjective', 'prop', []),
    ('QUEST', E + 'Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C', 'prop',
     ['CurrentActiveQuests', 'CompletedQuests', 'CurrentTrackedQuest', 'ActiveQuests',
      'NPCRelationships', 'PlayerChoice', 'NarrativeEvents', 'NarrativeTags', 'BlisterBoiDonations',
      'ImmortalBrigSpoils', 'OnQuestStarted', 'OnQuestCompleted', 'OnNarrativeTagsLoaded']),
    ('QUEST', E + 'Dialogue/QuestSystem/BPC_NarrativeEventsTracker.BPC_NarrativeEventsTracker_C',
     'prop', ['MySaveContext', 'CachedEventsTimeTracker', 'OnTrackerRefresh']),
    ('QUEST', E + 'Dialogue/QuestSystem/BPC_NarrativeEventsTracker.BPC_NarrativeEventsTracker_C',
     'func', ['FindEventTrackedTime', 'AddEventTimeTracker', 'RemoveEventTimeTracker',
              'GetMySaveData', 'InitEventsTimeTracker']),
    ('QUEST', E + 'Dialogue/Blueprints/DiscipleSystem/BPC_HubEventComponent.BPC_HubEventComponent_C',
     'prop', ['EventPointsPerEnemyKill', 'GiftEventPointBank', 'GiftEventDefaultThreshold',
              'CurrentGiftEventLevel', 'GiftEventLeaderTable', 'GiftsPickedUp', 'WasInitialized',
              'OvumsToOpenFinalLevel', 'OvumStairsEventBroadcasted', 'StartShellMemory']),
    ('QUEST', E + 'Dialogue/Blueprints/DiscipleSystem/BPC_HubEventComponent.BPC_HubEventComponent_C',
     'func', ['UpdateOvumsCollected', 'LoadOvumsCollectedFromSave', 'SaveHubData', 'OnSkyDive']),
    ('QUEST', E + 'Core/Achievements/BPC_Achievements.BPC_Achievements_C', 'prop',
     ['SlayerSealLockout', 'AchievementUpdatesQueue', 'IsUnlockingAchievements',
      'CurrentAchievementName', 'CurrentAchievementProgress']),
    ('QUEST', E + 'Core/SaveObjects/BPSO_QuestLogComponent.BPSO_QuestLogComponent_C', 'prop',
     ['NarrativeTags']),
    ('QUEST', E + 'Core/SaveObjects/BPSO_QuestLogComponent.BPSO_QuestLogComponent_C', 'func',
     ['ReceiveSpartaPreSave', 'ReceiveApplySkipProloguePreset']),
    ('QUEST', E + 'Dialogue/QuestSystem/BPSO_NarrativeEventsTracker.BPSO_NarrativeEventsTracker_C',
     'prop', ['NarrativeEventTimeTracking']),
    ('QUEST', '/Script/Sparta.SpartaCompletionSubsystem', 'prop',
     ['Snapshot', 'RunTimeSeconds', 'bRunTimerPaused', 'CompletionSettings']),
    ('QUEST', '/Script/Sparta.SpartaCompletionSubsystem', 'func',
     ['CalculateCompletion', 'SnapshotCompletion', 'GetSnapshot', 'SetRunTimeSeconds',
      'SetRunTimerPaused', 'HasSnapshot', 'ClearSnapshot']),
    ('QUEST', E + 'UI/World/Map/Blueprints/BPSO_WorldMap.BPSO_WorldMap_C', 'prop', []),
    ('QUEST', E + 'UI/World/Map/Blueprints/BPSO_WordMapObjective.BPSO_WordMapObjective_C', 'prop', []),

    # ---------------------------------------------------------------- enemies / spawners
    ('ENEMY', '/Script/Sparta.UnitSpawner', 'prop',
     ['SaveComponent', 'EnemySpawnType', 'SpawnerTags', 'SpawnedEnemy', 'CharacterId',
      'ClassReference', 'OnCharacterSpawned']),
    ('ENEMY', '/Script/Sparta.UnitSpawner', 'func',
     ['SpawnMe', 'CanSpawn', 'BeginSpawningEnemy', 'FinishSpawningEnemy', 'SpawnerLoaded',
      'LoadClasses', 'CharacterSpawned']),
    ('ENEMY', E + 'Core/SaveObjects/BPSO_SpawnerSaveObj.BPSO_SpawnerSaveObj_C', 'prop', []),
    ('ENEMY', E + 'Characters/SpawnSystem/BP_SpawnerSessionSaveObject.BP_SpawnerSessionSaveObject_C',
     'prop', ['WasKilled', 'DidSpawn', 'AttemptedSpawn', 'SpawnerOwner', 'Owner']),
    ('ENEMY', '/Script/Sparta.SpartaEnemySubsystem', 'prop',
     ['SpawnedEnemies', 'EngagedEnemies', 'OnCharacterDeathEvent', 'TopAggressor',
      'AggressorRanking', 'OnCharacterEngageStateChanged_BP']),
    ('ENEMY', '/Script/Sparta.SpartaEnemySubsystem', 'func',
     ['GetSpawnedEnemiesInDist', 'OnCharacterDeath', 'OnCharacterAddedToWorld']),
    ('ENEMY', '/Script/Sparta.SpartaEncounterManager', 'prop',
     ['AssociatedEnemies', 'HordeEncounters', 'SpawnedEncounters', 'OnEncounterStart',
      'OnEncounterCompleted', 'EncounterStartDistance']),
    ('ENEMY', '/Script/Sparta.SpartaEncounterManager', 'func',
     ['OnEnemyDeath', 'OnHordeEncounterCompleted']),
    ('ENEMY', '/Script/Sparta.SpartaCharacterData', 'prop',
     ['CharacterId', 'bIsBoss', 'bIsMainBoss', 'bShouldTrackDeath', 'DeathResources',
      'UpgradeStatValues', 'CharacterCurves', 'DefaultTags']),

    # ---------------------------------------------------------------- bosses
    ('BOSS', E + 'Core/AI/Components/BPC_BossAI.BPC_BossAI_C', 'prop',
     ['bTriggeredSecondPhase', 'bTriggeredThirdPhase', 'bIsAggroed', 'OnBossDeath', 'OnBossAggro',
      'OnBossDeaggro', 'OnSecondPhaseStarted', 'OnThirdPhaseStarted', 'SecondPhaseHealthPercent',
      'ThirdPhaseHealthPercent', 'VestigeItem', 'ItemStacks', 'bIsDead', 'TempBossHealthCalc',
      'BossAICharacter', 'SpartaAIController']),
    ('BOSS', E + 'Core/AI/Components/BPC_BossAI.BPC_BossAI_C', 'func',
     ['StartFight', 'StopFight', 'HandleAggro', 'HandleDeaggro', 'IsAlreadyAggroed',
      'ShouldStartSecondPhase', 'ShouldStartThirdPhase', 'BroadcastFightStartEvent',
      'BroadcastFightEndEvent', 'BindOnDamageEvent', 'BindOnDeathEvent',
      'SetBossAncientVestigesEffects', 'CacheCharacter', 'CacheController']),
    ('BOSS', E + 'Core/SaveObjects/BPSO_BossDLManager.BPSO_BossDLManager_C', 'prop', []),
    ('BOSS', E + 'Core/SaveObjects/BPSO_BossGate.BPSO_BossGate_C', 'prop', []),
    ('BOSS', E + 'Core/SaveObjects/BPSO_FC_FinalBossManager.BPSO_FC_FinalBossManager_C', 'prop', []),
    ('BOSS', E + 'Core/SaveObjects/BPSO_HutchbackBossTrigger.BPSO_HutchbackBossTrigger_C', 'prop', []),

    # ---------------------------------------------------------------- dialogue
    ('DIALOG', '/Script/Sparta.DialogueSubsystem', 'prop',
     ['ActiveDialogues', 'SpawnedParticipants', 'OnDlgStarted', 'OnDlgEnded',
      'OnDlgNodeEnterEvent', 'OnDlgNodeExitEvent', 'OnDlgAudioStarted']),
    ('DIALOG', '/Script/Sparta.DialogueSubsystem', 'func',
     ['SpartaStartDialogue', 'StopDialogue', 'EndAllDialoguesWithTags', 'InterruptDialogues',
      'IsDialogueCurrentlyActive', 'IsDialogueWithTagActive', 'GetAllActiveDialoguesWithTag',
      'FindActiveDialogueForInstigator', 'CheckTagConditions']),
    ('DIALOG', '/Script/Sparta.DlgBlueprintLibrary', 'func', []),
    ('DIALOG', '/Script/Sparta.SpartaEventDataAsset', 'prop', []),

    # ---------------------------------------------------------------- flow / zones / travel
    ('FLOW', '/Script/Sparta.SpartaWorldGenSubsystem', 'prop',
     ['MapSettings', 'PresetSeed', 'WeatherId', 'CurrentSeed', 'CurrentWorldState',
      'OnDifficultyChanged']),
    ('FLOW', '/Script/Sparta.SpartaWorldGenSubsystem', 'func',
     ['GetDifficulty', 'SetDifficulty', 'IncreaseDifficulty', 'BeginWorldReset',
      'AddTagToCurrentWorldState']),
    ('FLOW', '/Script/Sparta.SpartaRunManagerSubsystem', 'prop',
     ['RunStateIntegers', 'AlreadySelectedEventInstances']),
    ('FLOW', '/Script/Sparta.SpartaRunManagerSubsystem', 'func',
     ['GetRunStateInteger', 'AddRunStateInteger', 'RemoveRunStateInteger', 'LevelInstanceSelected',
      'HasLevelInstanceAlreadyBeenStreamed', 'GetEventDataForSpace']),
    ('FLOW', '/Script/Sparta.SpartaAreaSubsystem', 'prop',
     ['ActiveArea', 'CompletedAreas', 'OnAreaActivated', 'OnAreaComplete']),
    ('FLOW', '/Script/Sparta.SpartaAreaSubsystem', 'func',
     ['GetActiveArea', 'IsAreaActive', 'ActivateArea', 'CompleteArea', 'IsCompleteActive',
      'GetConnectedAreas']),
    ('FLOW', '/Script/Sparta.SpartaZoneVolumeManager', 'prop',
     ['ZoneVolumeStack', 'HighestPriorityZoneVolumeUpdated']),
    ('FLOW', '/Script/Sparta.SpartaZoneData', 'prop', ['ZoneId', 'AmbienceData', 'MusicData']),
    ('FLOW', '/Script/Sparta.SpartaZoneVolume', 'prop',
     ['ZoneData', 'Priority', 'CollisionProfile']),
    ('FLOW', E + 'Core/Utility/BPFL_WorldStreaming.BPFL_WorldStreaming_C', 'func',
     ['TeleportPlayerWithStreaming']),
    ('FLOW', E + 'Core/Player/Components/BPC_TeleportManager.BPC_TeleportManager_C', 'prop', []),
    ('FLOW', E + 'Core/World/Teleporting/BP_STH_Entrance_Gate.BP_STH_Entrance_Gate_C', 'prop', []),
    ('FLOW', E + 'Core/World/Levels/BP_LevelInstanceSelector.BP_LevelInstanceSelector_C', 'prop', []),
    ('FLOW', '/Script/Sparta.SpartaLevelStreamingDynamic', 'func',
     ['LoadLevelInstanceBySoftObjectPtr', 'LoadLevelInstance']),

    # ---------------------------------------------------------------- cinematics
    ('CINE', E + 'Core/SaveObjects/BPSO_CinematicDLUpdate.BPSO_CinematicDLUpdate_C', 'prop', []),
    ('CINE', E + 'Core/Interaction/BP_LS_CinematicDLManager.BP_LS_CinematicDLManager_C', 'prop', []),
    ('CINE', E + 'Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C', 'prop', []),
    ('CINE', E + 'Core/Player/BP_CinematicPlayerProxy.BP_CinematicPlayerProxy_C', 'prop', []),
    ('CINE', '/Script/Sparta.SpartaCinematicAudioHandler', 'prop', []),
    ('CINE', '/Script/Sparta.SpartaLoadingScreenSubsystem', 'prop', []),

    # ---------------------------------------------------------------- interactables
    ('INTER', E + 'Core/SaveObjects/BPSO_Gate.BPSO_Gate_C', 'prop',
     ['GateOpened', 'GotVarsOnce', 'StartingLoc', 'EndingLoc', 'StartingRot', 'EndingRot']),
    ('INTER', E + 'Core/SaveObjects/BPSO_FlowerChest.BPSO_FlowerChest_C', 'prop', []),
    ('INTER', E + 'Core/SaveObjects/BPSO_MovingObject.BPSO_MovingObject_C', 'prop', []),
    ('INTER', E + 'Core/SaveObjects/BPSO_WoodenElevator.BPSO_WoodenElevator_C', 'prop', []),
    ('INTER', E + 'Core/SaveObjects/BPSO_CloseAndOpenGate.BPSO_CloseAndOpenGate_C', 'prop', []),
    ('INTER', E + 'Core/SaveObjects/BPSO_Ovum.BPSO_Ovum_C', 'prop', []),
    ('INTER', E + 'Core/SaveObjects/BPSO_ShellRevive.BPSO_ShellRevive_C', 'prop', []),
    ('INTER', E + 'Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C', 'prop', []),
    ('INTER', E + 'Core/SaveObjects/BPSO_Tarforge.BPSO_Tarforge_C', 'prop', []),
    ('INTER', E + 'Core/SaveObjects/BPSO_EvilStatue.BPSO_EvilStatue_C', 'prop', []),
    ('INTER', E + 'Core/Interaction/SaveObjects/BPSO_SavedLockedChests.BPSO_SavedLockedChests_C',
     'prop', []),
    ('INTER', E + 'Core/Interaction/SaveObjects/BP_SO_SavedKeys.BP_SO_SavedKeys_C', 'prop', []),
    ('INTER', E + 'Core/Interaction/SaveObjects/BP_SO_SavedLockedDoors.BP_SO_SavedLockedDoors_C',
     'prop', []),
    ('INTER', E + 'Core/Interaction/SaveObjects/BP_SO_SimpleBool.BP_SO_SimpleBool_C', 'prop', []),
    ('INTER', E + 'Core/Interaction/BP_PressurePlateOpenDoor.BP_PressurePlateOpenDoor_C', 'prop', []),
    ('INTER', '/Script/Sparta.SpartaInputInteraction', 'prop', []),
    ('INTER', '/Script/Sparta.PickupableActor', 'prop', []),

    # ---------------------------------------------------------------- equipment (tag-keyed writers)
    # ---------------------------------------------------------------- movement / animation state
    ('MOVE', '/Script/Sparta.SpartaCharacterMovementComponent', 'prop',
     ['BlockedMovementTags', 'VelocityTracker', 'RotationMethod', 'AccelerationInputScaling',
      'AccelerationInputSmoothing', 'AccelerationInputSmoothingFactor', 'RunCollisionChecksWhenIdle',
      'NavWalkingSizeOnScreen']),
    ('MOVE', '/Script/Sparta.SpartaCharacterMovementComponent', 'func',
     ['SetMovementSpeedType', 'SetMovementSpeedTypeOverride', 'RemoveMovementSpeedTypeOverride',
      'SetMaxSpeedAdjustment', 'RemoveMaxSpeedAdjustment', 'HasMaxSpeedAdjustment',
      'SetRotationRateAdjustment', 'RemoveRotationRateAdjustment', 'SetOverrideManualUserRotation',
      'HasNavFloor']),
    ('MOVE', '/Script/Sparta.SpartaAnimInstance', 'prop', ['GameplayTagPropertyMap', 'GroundDistance']),
    ('MOVE', '/Script/Sparta.SpartaAnimInstance', 'func',
     ['InitializeAnimationData', 'RevertAnimationData']),

    ('EQUIP', E + 'Core/Player/BPFL_Player_Equipment.BPFL_Player_Equipment_C', 'func',
     ['ChangeShell', 'ChangeWeapon', 'ChangeSidearm', 'ChangeSeal', 'ChangeDarkForm',
      'ChangePrimaryCharges', 'ChangeSecondaryCharges', 'ChangeShellSkin', 'ChangeDarkFormSkin',
      'ChangeShellClothing', 'ChangeDarkFormClothing', 'ChangeShellMask', 'ChangeDarkFormMask',
      'UnlockShell', 'UnlockWeapon', 'UnlockSidearm', 'UnlockSeal', 'UnlockSkin', 'UnlockMask',
      'UnlockClothing', 'GetShellItemDefinition', 'GetWeaponItemDefinition',
      'GetSidearmItemDefinition', 'GetSealItemDefinition', 'GetDarkFormItemDefinition',
      'GetChargesItemDefinition', 'IsEquipmentUnlocked', 'IsInShell', 'IsSevered',
      'ActivateDarkForm', 'StowAndPutInHand', 'TryRestoreWeapon', 'TryRestoreSidearm',
      'TryRemoveWeapon', 'TryRemoveSidearm', 'SwapShellMaterials', 'SwapDarkFormMaterials',
      'DebugEquipmentCheck']),
]


def read_hint(kind, typ, off):
    """Turn a reflected member into the byte-level read the implementer actually needs."""
    if off is None:
        return ''
    if 'GameplayAttributeData' in typ:
        return 'BaseValue @+0x%X, CurrentValue @+0x%X' % (off + 0x8, off + 0xC)
    if 'GameplayTagContainer' in typ:
        return 'TArray<FGameplayTag> @+0x%X (0x10), ParentTags @+0x%X (0x10)' % (off, off + 0x10)
    if typ == 'StructProperty of /Script/GameplayTags.GameplayTag':
        return 'FName TagName @+0x%X' % off
    return ''


def main():
    c = mine.db('index.sqlite3')
    rows = []
    unresolved = []
    for area, cls, kind, members in SPEC:
        obj = mine.find_obj(c, cls)
        if not obj:
            unresolved.append((area, cls, '<class>'))
            continue
        if kind == 'prop':
            flat = {nm: (decl, t, off, line) for decl, nm, t, off, line in mine.flatten(c, cls)}
            for m in members:
                if m not in flat:
                    unresolved.append((area, cls, m))
                    continue
                decl, t, off, line = flat[m]
                rows.append((area, cls, 'prop', m, t, off, read_hint(kind, t, int(off, 16)),
                             '', decl, line))  # area,class,kind,member,type,offset,hint,nparams,declared_in,line
        else:
            funs = {nm: (line, ps, decl) for decl, nm, line, ps in mine.funcs(c, cls)}
            for m in members:
                if m not in funs:
                    unresolved.append((area, cls, m + '()'))
                    continue
                line, ps, decl = funs[m]
                sig = ', '.join('%s %s' % (p[0], p[1].replace('Property of ', '')) for p in ps)
                rows.append((area, cls, 'func', m + '()', sig, '', '', str(len(ps)), decl, line))
    mine.write_tsv('sync-map.tsv',
                   ('area', 'class', 'kind', 'member', 'type_or_signature', 'offset', 'read_hint',
                    'nparams', 'declared_in', 'dump_line'), rows)
    print('sync-map.tsv : %d rows' % len(rows))
    if unresolved:
        print('\n!! UNRESOLVED (%d) - fix the spec, do not ship:' % len(unresolved))
        for u in unresolved:
            print('   ', u)
        return 1

    # markdown rendering, one table per area
    by_area = collections.OrderedDict()
    for r in rows:
        by_area.setdefault(r[0], []).append(r)
    out = []
    for area, rs in by_area.items():
        out.append('### %s (%d entries)\n' % (area, len(rs)))
        out.append('| member | class | type / signature | offset / read hint | proof line |')
        out.append('|---|---|---|---|---|')
        for r in rs:
            symbol = (r[4] or '').replace('|', '\\|')
            hint = r[6] or ''
            off = ('`%s`' % r[5]) if r[5] else ''
            if hint:
                hint = '%s `%s`' % (off, hint)
            else:
                hint = off
            proof = '`ObjectDump_allassets.txt:%s`' % r[9]
            out.append('| `%s` | `%s` | %s | %s | %s |' % (r[3], r[1].rsplit('/', 1)[-1],
                                                           symbol, hint, proof))
        out.append('')
    with open(os.path.join(MINE, 'sync-map.md'), 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(out) + '\n')
    print('sync-map.md  : %d tables' % len(by_area))
    return 0


if __name__ == '__main__':
    sys.exit(main())

