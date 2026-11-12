#!/usr/bin/env python
"""mine_gen.py - emit the full sync data-mine artifact set into research/mine/.

Run:  python scripts\\mine.py gen          (or  python scripts\\mine_gen.py )

Every artifact is derived from files already on disk; nothing is re-dumped and no game
process is started.  Each row carries the proof triple (source file, dump line) so that
any number in SYNC-DATA-MINE.md can be traced back to a byte in a dump.
"""
import os
import re
import sys
import json
import sqlite3
import hashlib

HERE = os.path.dirname(os.path.abspath(__file__))
RESEARCH = os.path.dirname(HERE)
RAW = os.path.join(RESEARCH, 'raw')
MINE = os.path.join(RESEARCH, 'mine')
sys.path.insert(0, HERE)

import mine  # noqa: E402

CLASS_TYPES = ('Class', 'BlueprintGeneratedClass', 'WidgetBlueprintGeneratedClass',
               'AnimBlueprintGeneratedClass', 'UserDefinedClass', 'DynamicClass',
               'ScriptStruct', 'UserDefinedStruct')

# --------------------------------------------------------------------------- topics
# Each topic: (number, name, title, [class-path prefixes / exact paths / regexes])
# An entry starting with 're:' is a regex matched against class paths.

G = '/Game/Sparta/'
TOPICS = [
    (1, 'storeA-core', 'Store A - SpartaSaveObject / SpartaSaveComponent contract', [
        '/Script/Sparta.SpartaSaveObject',
        '/Script/Sparta.SpartaSaveComponent',
        '/Script/Sparta.SpartaSaveInterface',
        '/Script/Sparta.SpartaSessionSaveObjectBase',
        '/Script/Sparta.SaveObjectTickFunction',
        '/Script/Sparta.SpartaSaveContext',
        '/Script/Sparta.SpartaMetaSave',
    ]),
    (2, 'storeA-objects', 'Store A - every save-object class in the build', [
        're:^/Game/Sparta/.+?/BP(S)?_?O?_?[A-Za-z]*SaveObject',
        're:^/Game/Sparta/Core/SaveObjects/BPSO_',
        're:^/Game/Sparta/Core/Interaction/SaveObjects/',
        '/Script/Sparta.PlayerSaveObject',
        '/Script/Sparta.PlayerProgressionSaveData',
        '/Script/Sparta.TarstoneSaveData',
        '/Script/Sparta.PPItemsSaveData',
        '/Script/Sparta.DataLayersSave',
        G + 'Core/Player/Save/BP_SpartaPlayerSaveObject.BP_SpartaPlayerSaveObject_C',
        G + 'Characters/SpawnSystem/BP_SpawnerSessionSaveObject.BP_SpawnerSessionSaveObject_C',
        G + 'SaveObjects/BPSO_PlayerSession.BPSO_PlayerSession_C',
        G + 'SaveObjects/BPSO_Progression.BPSO_Progression_C',
        G + 'SaveObjects/BPSO_ShellMemories.BPSO_ShellMemories_C',
        G + 'Dialogue/QuestSystem/BPSO_NarrativeEventsTracker.BPSO_NarrativeEventsTracker_C',
        G + 'UI/World/Map/Blueprints/BPSO_WorldMap.BPSO_WorldMap_C',
        G + 'UI/World/Map/Blueprints/BPSO_WordMapObjective.BPSO_WordMapObjective_C',
    ]),
    (3, 'storeB', 'Store B - SpartaSaveGame / SpartaWorldStateHelper / SpartaSaveSubSystem', [
        '/Script/Sparta.SpartaSaveGame',
        '/Script/Sparta.SpartaWorldStateHelper',
        '/Script/Sparta.SpartaSaveSubSystem',
        '/Script/Sparta.SpartaSaveGlobalSettings',
    ]),
    (4, 'player-save', 'Progression keystone - BP_SpartaPlayerSaveObject_C + PlayerSaveObject', [
        G + 'Core/Player/Save/BP_SpartaPlayerSaveObject.BP_SpartaPlayerSaveObject_C',
        '/Script/Sparta.PlayerSaveObject',
        '/Script/Sparta.PermanentStats',
        '/Script/Sparta.PlayerProgressionSaveData',
        '/Script/Sparta.TarstoneSaveData',
        '/Script/Sparta.PPItemsSaveData',
    ]),
    (5, 'body', 'Live body state - health / resolve / harden / heal / death', [
        '/Script/Sparta.SpartaHealthComponent',
        '/Script/Sparta.SpartaHealthSet',
        '/Script/Sparta.SpartaAttributeSet',
        '/Script/Sparta.SpartaCombatSet',
        '/Script/Sparta.PlayerAttributeSet',
        '/Script/Sparta.ShellAbilityAttributeSet',
        '/Script/Sparta.SidearmAttributeSet',
        '/Script/Sparta.DarkPowerAttributeSet',
        '/Script/Sparta.EnemyAttributeSet',
        '/Script/Sparta.SpartaAttributeStackLimitComponent',
        '/Script/Sparta.ArmorComponent',
        '/Script/Sparta.SlayerSealComponent',
        '/Script/Sparta.SpartaHoldExpirationComponent',
        '/Script/Sparta.SpartaHittable',
        '/Script/Sparta.SpartaHitPayload',
        '/Script/Sparta.SpartaHitApplier',
        '/Script/Sparta.SpartaDamageCauser',
        '/Script/Sparta.SpartaAbilitySystemComponent',
        '/Script/Sparta.SpartaPlayerCharacter',
        '/Script/Sparta.SpartaDeathCondition',
        '/Script/Sparta.SpartaMontageInterrupterComponent',
        '/Script/Sparta.SpartaTargetLockComponent',
        '/Script/Sparta.SpartaWeaponComponent_Base',
        '/Script/Sparta.SpartaWeaponComponent_Melee',
        '/Script/Sparta.SpartaWeaponComponent_Ranged',
        '/Script/Sparta.SpartaWeaponComponent_Effect',
        '/Script/Sparta.SidearmProjectileCacheComponent',
        '/Script/Sparta.SpartaHealExecution',
        '/Script/Sparta.SpartaDamageExecution',
        '/Script/Sparta.SpartaCharacterMovementComponent',
        '/Script/Sparta.SpartaAnimInstance',
        '/Script/Sparta.CharacterAnimationData',
    ]),
    (6, 'inventory', 'Inventory, item definitions and item instances', [
        '/Script/Sparta.PlayerInventoryComponent',
        '/Script/Sparta.SpartaItemManagerComponent',
        '/Script/Sparta.SpartaItemInstance',
        '/Script/Sparta.SpartaItemDefinition',
        '/Script/Sparta.SpartaItemFunctionLibrary',
        '/Script/Sparta.SpartaItemChangeEvent',
        '/Script/Sparta.SpartaItemFragment',
        '/Script/Sparta.SpartaItemFragment_StatLevels',
        '/Script/Sparta.AdditionalContainerComponent',
        '/Script/Sparta.ItemFragment_Display',
        '/Script/Sparta.ItemFragment_StackabilityRules',
        '/Script/Sparta.ItemFragment_StackableDivideSumByCountSquared',
        '/Script/Sparta.ItemFragment_Tarstone',
        '/Script/Sparta.ItemFragment_SetStats',
        '/Script/Sparta.ItemFragment_ItemTag',
        '/Script/Sparta.ItemFragment_GiveItemToPlayer',
        '/Script/Sparta.ItemFragment_GiveItemToShell',
        '/Script/Sparta.ItemFragment_WorldStateModifier',
        '/Script/Sparta.ItemFragment_Executable',
        '/Script/Sparta.ItemFragment_SetShellClass',
        '/Script/Sparta.ItemFragment_SetDarkformClass',
        '/Script/Sparta.ItemFragment_AbilitySets',
        '/Script/Sparta.ItemFragment_PickupIcon',
        '/Script/Sparta.ItemFragment_Spawnable',
    ]),
    (7, 'progression', 'Progression - shells, weapons, upgrades, tarstones, PP items, XP', [
        '/Script/Sparta.PlayerProgressionComponent',
        '/Script/Sparta.PlayerExpComponent',
        '/Script/Sparta.UpgradesComponent',
        '/Script/Sparta.UpgradesComponent',
        '/Script/Sparta.UpgradeGraph',
        '/Script/Sparta.UpgradeGraphEdge',
        '/Script/Sparta.UpgradeGraphNode',
        '/Script/Sparta.WeaponsComponent',
        '/Script/Sparta.SpartaWeapon',
        '/Script/Sparta.SpartaWeaponProgressionData',
        '/Script/Sparta.SpartaProgressionDataAsset',
        '/Script/Sparta.TarstoneComponent',
        '/Script/Sparta.TarstoneDebugComponent',
        '/Script/Sparta.PPItemsComponent',
        '/Script/Sparta.PPItemFragment',
        '/Script/Sparta.PPItemFragmentData',
        '/Script/Sparta.ArtifactComponent',
        '/Script/Sparta.SpartaAbilitySetHandleContainer',
        '/Script/Sparta.CharacterPersistantAbilitySetData',
        G + 'Core/Player/Upgrades/Core/BPFL_Progression.BPFL_Progression_C',
        G + 'Core/Player/Upgrades/Core/BPFL_Upgrades.BPFL_Upgrades_C',
        G + 'Core/Player/Upgrades/Core/BPFL_Tarstones.BPFL_Tarstones_C',
        G + 'Core/Player/Upgrades/Core/BPFL_PPItems.BPFL_PPItems_C',
        G + 'Core/Player/Upgrades/Tarforge/BPFL_Tarforge.BPFL_Tarforge_C',
    ]),
    (8, 'economy', 'Economy entry points - player controller S_* API and helper libs', [
        '/Script/Sparta.SpartaPlayerController',
        '/Script/Sparta.SpartaPlayerCharacter',
        '/Script/Sparta.SpartaLocalPlayer',
        G + 'Core/Player/BPFL_Player.BPFL_Player_C',
        G + 'Core/Player/BPFL_Player_Equipment.BPFL_Player_Equipment_C',
        G + 'Core/Player/BPFL_SaveLoad.BPFL_SaveLoad_C',
        G + 'Core/Player/BPFL_ShellMemory.BPFL_ShellMemory_C',
    ]),
    (9, 'quests', 'Quests, narrative events, completion/activities, world map', [
        '/Script/Sparta.QuestObject',
        '/Script/Sparta.QuestObjective',
        '/Script/Sparta.SpartaCompletionSubsystem',
        '/Script/Sparta.SpartaCompletionLibrary',
        '/Script/Sparta.SpartaCompletionSettings',
        '/Script/Sparta.PossibleWorldStateSet',
        '/Script/Sparta.PossibleActivity',
        '/Script/Sparta.SpartaActivityDescriptor',
        '/Script/Sparta.SpartaActivityType',
        '/Script/Sparta.WorldMapObjectiveComponent',
        '/Script/Sparta.QuestDetails',
        '/Script/Sparta.QuestStage',
        G + 'Core/SaveObjects/BPSO_QuestLogComponent.BPSO_QuestLogComponent_C',
        G + 'Dialogue/QuestSystem/BPSO_NarrativeEventsTracker.BPSO_NarrativeEventsTracker_C',
        G + 'UI/World/Map/Blueprints/BPSO_WorldMap.BPSO_WorldMap_C',
        G + 'UI/World/Map/Blueprints/BPSO_WordMapObjective.BPSO_WordMapObjective_C',
        G + 'Core/Utility/BPFL_Narrative.BPFL_Narrative_C',
        G + 'Core/Map?',
    ]),
    (10, 'enemies', 'Enemies, spawners, encounters, hordes', [
        '/Script/Sparta.SpartaAICharacter',
        '/Script/Sparta.SpartaAIController',
        '/Script/Sparta.SpartaEnemySubsystem',
        '/Script/Sparta.SpartaEncounterManager',
        '/Script/Sparta.SpartaEncounterRecipe',
        '/Script/Sparta.UnitSpawner',
        '/Script/Sparta.SpartaHordeSpawner',
        '/Script/Sparta.SpartaHordeEncounter',
        '/Script/Sparta.HordeSpawnerComponent',
        '/Script/Sparta.HordeUnitComponent',
        '/Script/Sparta.HordeCharacterData',
        '/Script/Sparta.SpawnTrigger',
        '/Script/Sparta.SpartaMassActorSpawnerSubsystem',
        '/Script/Sparta.AIDirectorSubsystem',
        '/Script/Sparta.SpartaCharacterData',
        '/Script/Sparta.SpartaEnemySpawnerDisplayData',
        '/Script/Sparta.FSpartaCharacterDeathData',
        '/Script/Sparta.SpartaCharacterDeathData',
        G + 'Core/SaveObjects/BPSO_SpawnerSaveObj.BPSO_SpawnerSaveObj_C',
        G + 'Characters/SpawnSystem/BP_SpawnerSessionSaveObject.BP_SpawnerSessionSaveObject_C',
        G + 'Characters/SpawnSystem/BP_Spawner.BP_Spawner_C',
        G + 'Characters/SpawnSystem/BP_Spawner_Proximity.BP_Spawner_Proximity_C',
        G + 'Core/SaveObjects/BPSO_PlayerTrapDungeon.BPSO_PlayerTrapDungeon_C',
    ]),
    (11, 'bosses', 'Bosses, boss gates and the boss data-layer manager', [
        G + 'Core/AI/Components/BPC_BossAI.BPC_BossAI_C',
        G + 'Core/SaveObjects/BPSO_BossDLManager.BPSO_BossDLManager_C',
        G + 'Core/SaveObjects/BPSO_BossGate.BPSO_BossGate_C',
        G + 'Core/SaveObjects/BPSO_FC_FinalBossManager.BPSO_FC_FinalBossManager_C',
        G + 'Core/SaveObjects/BPSO_FC_CorruptionCore.BPSO_FC_CorruptionCore_C',
        G + 'Core/SaveObjects/BPSO_FC_TarRoots.BPSO_FC_TarRoots_C',
        G + 'Core/SaveObjects/BPSO_HutchbackBossTrigger.BPSO_HutchbackBossTrigger_C',
        G + 'Core/SaveObjects/BPSO_LandingArea_Boss.BPSO_LandingArea_Boss_C',
        G + 'Core/SaveObjects/BPSO_TarGolemLevelScript.BPSO_TarGolemLevelScript_C',
        G + 'Core/SaveObjects/BPSO_TavernArena.BPSO_TavernArena_C',
        G + 'Core/World/Levels/BP_LevelInstanceSelector.BP_LevelInstanceSelector_C',
    ]),
    (12, 'dialogue', 'Dialogue subsystem and speech sequences', [
        '/Script/Sparta.DialogueSubsystem',
        '/Script/Sparta.DialogueNodeData',
        '/Script/Sparta.Dialogue_SpartaSpeechNode',
        '/Script/Sparta.DlgBlueprintLibrary',
        '/Script/Sparta.DialogueInstigatorComponent',
        '/Script/Sparta.SpartaDialogueActor',
        '/Script/Sparta.SpartaDlgParticipant',
        '/Script/Sparta.SpartaEventDataAsset',
        '/Script/Sparta.HubEventComponent',
        '/Script/Sparta.SpartaHubGiftEventData',
        G + 'Dialogue/QuestSystem/BPSO_NarrativeEventsTracker.BPSO_NarrativeEventsTracker_C',
    ]),
    (13, 'flow', 'Flow - world gen/reset, run manager, zones, streaming, fast travel', [
        '/Script/Sparta.SpartaWorldGenSubsystem',
        '/Script/Sparta.WorldGenMapSettings',
        '/Script/Sparta.SpartaRunManagerSubsystem',
        '/Script/Sparta.RunManagerOptionContext',
        '/Script/Sparta.SpartaGameMode',
        '/Script/Sparta.SpartaGameInstance',
        '/Script/Sparta.SpartaLevelStreamingDynamic',
        '/Script/Sparta.SpartaLevelStreamingDynamicHighPrio',
        '/Script/Sparta.SpartaStreamingSourceComponent',
        '/Script/Sparta.StreamingSource',
        '/Script/Sparta.SpartaZoneVolume',
        '/Script/Sparta.SpartaZoneVolumeManager',
        '/Script/Sparta.SpartaZoneData',
        '/Script/Sparta.SpartaAreaSubsystem',
        '/Script/Sparta.SpartaAreaId',
        '/Script/Sparta.SpartaDataLayerSubsystem',
        '/Script/Sparta.DataLayerFunctionLibrary',
        '/Script/Sparta.DataLayersSave',
        '/Script/Sparta.SpartaLevelScript',
        '/Script/Sparta.SpartaTeleportLocationInterface',
        '/Script/Sparta.SpartaTraversalActor',
        '/Script/Sparta.SpartaTraversalNode',
        '/Script/Sparta.SpartaWorldSettings',
        '/Script/Sparta.SpartaSingleton',
        '/Script/Sparta.SpartaLoadingScreenSubsystem',
        G + 'Core/Utility/BPFL_WorldStreaming.BPFL_WorldStreaming_C',
        G + 'Core/World/Teleporting/BP_SpartaTeleportHandler.BP_SpartaTeleportHandler_C',
        G + 'Core/World/Teleporting/BPI_SpartaTeleportHandler.BPI_SpartaTeleportHandler_C',
        G + 'Core/Player/Components/BPC_TeleportManager.BPC_TeleportManager_C',
        G + 'Core/GameModes/BP_SpartaGameMode.BP_SpartaGameMode_C',
        G + 'Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C',
        G + 'Core/World/BP_TeleportLocation.BP_TeleportLocation_C',
        G + 'Core/Player/BPML_WorldStreaming.BPML_WorldStreaming_C',
    ]),
    (14, 'cinematics', 'Cinematics, pause and on-screen flow', [
        '/Script/Sparta.SpartaCinematicAudioHandler',
        '/Script/Sparta.SpartaCinematicAudioData',
        '/Script/Sparta.SpartaUIManagerSubsystem',
        '/Script/Sparta.SpartaGameplayCueManager',
        G + 'Core/SaveObjects/BPSO_CinematicDLUpdate.BPSO_CinematicDLUpdate_C',
        G + 'Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C',
        G + 'Core/Player/BP_CinematicPlayerProxy.BP_CinematicPlayerProxy_C',
        G + 'Core/Audio/BP_CinematicAudioHandler.BP_CinematicAudioHandler_C',
        G + 'Core/Interaction/BP_LS_CinematicDLManager.BP_LS_CinematicDLManager_C',
        G + 'Core/World/Levels/Behavior/BP_CinematicBehavior_StartCredits.BP_CinematicBehavior_StartCredits_C',
    ]),
    (15, 'interaction', 'Interactables - gates, doors, chests, lifts, plates, breakables', [
        '/Script/Sparta.SpartaInputInteraction',
        '/Script/Sparta.SpartaInstancedObject',
        '/Script/Sparta.SpartaWorldObject',
        '/Script/Sparta.PickupableActor',
        G + 'Core/SaveObjects/BPSO_Gate.BPSO_Gate_C',
        G + 'Core/SaveObjects/BPSO_CloseAndOpenGate.BPSO_CloseAndOpenGate_C',
        G + 'Core/SaveObjects/BPSO_MovingObject.BPSO_MovingObject_C',
        G + 'Core/SaveObjects/BPSO_WoodenElevator.BPSO_WoodenElevator_C',
        G + 'Core/SaveObjects/BPSO_FlowerChest.BPSO_FlowerChest_C',
        G + 'Core/SaveObjects/BPSO_Ovum.BPSO_Ovum_C',
        G + 'Core/SaveObjects/BPSO_ShellRevive.BPSO_ShellRevive_C',
        G + 'Core/SaveObjects/BPSO_ShellGhost.BPSO_ShellGhost_C',
        G + 'Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C',
        G + 'Core/SaveObjects/BPSO_Tarforge.BPSO_Tarforge_C',
        G + 'Core/SaveObjects/BPSO_EvilStatue.BPSO_EvilStatue_C',
        G + 'Core/SaveObjects/BPSO_EridrimUnlockManager.BPSO_EridrimUnlockManager_C',
        G + 'Core/SaveObjects/BPSO_BearTrap.BPSO_BearTrap_C',
        G + 'Core/SaveObjects/BPSO_TarGate.BPSO_TarGate_C',
        G + 'Core/SaveObjects/BPSO_FallingStatue.BPSO_FallingStatue_C',
        G + 'Core/SaveObjects/BPSO_FallingCorpse.BPSO_FallingCorpse_C',
        G + 'Core/SaveObjects/BPSO_CraftingItemPickup.BPSO_CraftingItemPickup_C',
        G + 'Core/SaveObjects/BPSO_SlayerDungeonChalice.BPSO_SlayerDungeonChalice_C',
        G + 'Core/SaveObjects/BPSO_PetrifiedCultistManager.BPSO_PetrifiedCultistManager_C',
        G + 'Core/SaveObjects/BPSO_GenessaSensor.BPSO_GenessaSensor_C',
        G + 'Core/SaveObjects/BPSO_GrishaCage.BPSO_GrishaCage_C',
        G + 'Core/SaveObjects/BPSO_GrishaGhost.BPSO_GrishaGhost_C',
        G + 'Core/SaveObjects/BPSO_ShepherdSheepGate.BPSO_ShepherdSheepGate_C',
        G + 'Core/SaveObjects/BPSO_NPC.BPSO_NPC_C',
        G + 'Core/SaveObjects/BPSO_DoorLockedDungeon.BPSO_DoorLockedDungeon_C',
        G + 'Core/Interaction/SaveObjects/BPSO_SavedLockedChests.BPSO_SavedLockedChests_C',
        G + 'Core/Interaction/SaveObjects/BP_SO_SavedKeys.BP_SO_SavedKeys_C',
        G + 'Core/Interaction/SaveObjects/BP_SO_SavedLockedDoors.BP_SO_SavedLockedDoors_C',
        G + 'Core/Interaction/SaveObjects/BP_SO_SimpleBool.BP_SO_SimpleBool_C',
        G + 'Core/Interaction/BP_PressurePlateDungeon.BP_PressurePlateDungeon_C',
        G + 'Core/Interaction/BP_PressurePlateOpenDoor.BP_PressurePlateOpenDoor_C',
    ]),
    (16, 'session', 'Session / save plumbing and Steam surface', [
        G + 'SaveObjects/BPSO_PlayerSession.BPSO_PlayerSession_C',
        '/Script/Sparta.SpartaSaveGlobalSettings',
        '/Script/Sparta.SpartaSteamInventorySubsystem',
        '/Script/Sparta.SpartaSteamPresenceLibrary',
        '/Script/Sparta.SteamStatusBlueprintLibrary',
        '/Script/Sparta.SpartaAchievementsSubsystem',
        '/Script/Sparta.SpartaTelemetrySubsystem',
    ]),
    (17, 'bplibs', 'All BP function libraries (the CDO-callable Blueprint API surface)', [
        '/Game/Sparta/Core/AI/BPFL_AI.BPFL_AI_C',
        '/Game/Sparta/Core/AI/BPFL_AstralAI.BPFL_AstralAI_C',
        '/Game/Sparta/Core/Achievements/BPFL_Achievements.BPFL_Achievements_C',
        '/Game/Sparta/Core/Audio/BPFL_Audio.BPFL_Audio_C',
        '/Game/Sparta/Core/Player/BPFL_Player.BPFL_Player_C',
        '/Game/Sparta/Core/Player/BPFL_Player_Equipment.BPFL_Player_Equipment_C',
        '/Game/Sparta/Core/Player/BPFL_SaveLoad.BPFL_SaveLoad_C',
        '/Game/Sparta/Core/Player/BPFL_ShellMemory.BPFL_ShellMemory_C',
        '/Game/Sparta/Core/Player/Upgrades/Core/BPFL_PPItems.BPFL_PPItems_C',
        '/Game/Sparta/Core/Player/Upgrades/Core/BPFL_Progression.BPFL_Progression_C',
        '/Game/Sparta/Core/Player/Upgrades/Core/BPFL_Tarstones.BPFL_Tarstones_C',
        '/Game/Sparta/Core/Player/Upgrades/Core/BPFL_Upgrades.BPFL_Upgrades_C',
        '/Game/Sparta/Core/Player/Upgrades/Tarforge/BPFL_Tarforge.BPFL_Tarforge_C',
        '/Game/Sparta/Core/Utility/BPFL_AttachableItems.BPFL_AttachableItems_C',
        '/Game/Sparta/Core/Utility/BPFL_GameInstance.BPFL_GameInstance_C',
        '/Game/Sparta/Core/Utility/BPFL_MMC.BPFL_MMC_C',
        '/Game/Sparta/Core/Utility/BPFL_Narrative.BPFL_Narrative_C',
        '/Game/Sparta/Core/Utility/BPFL_Playtest.BPFL_Playtest_C',
        '/Game/Sparta/Core/Utility/BPFL_Prototyping.BPFL_Prototyping_C',
        '/Game/Sparta/Core/Utility/BPFL_TimeOfDay.BPFL_TimeOfDay_C',
        '/Game/Sparta/Core/Utility/BPFL_Transformation.BPFL_Transformation_C',
        '/Game/Sparta/Core/Utility/BPFL_Utility.BPFL_Utility_C',
        '/Game/Sparta/Core/Utility/BPFL_WorldStreaming.BPFL_WorldStreaming_C',
        '/Game/Sparta/FX/Blueprints/BPFL_VFX.BPFL_VFX_C',
        '/Game/Sparta/FX/Blueprints/Ghosts/BPFL_Ghosts.BPFL_Ghosts_C',
        '/Game/Sparta/UI/Audio/Events/BPFL_UI_Audio.BPFL_UI_Audio_C',
        '/Game/Sparta/UI/Core/BPFL_InterfaceEffects.BPFL_InterfaceEffects_C',
        '/Game/Sparta/UI/Core/BPFL_Map.BPFL_Map_C',
        '/Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C',
        '/Game/Sparta/UI/Menu/Merchant/BPFL_Shops.BPFL_Shops_C',
        '/Game/Sparta/UI/Player/Debug/StateManagement/BPFL_Debug_StateManagement.BPFL_Debug_StateManagement_C',
    ]),
    (18, 'player-controller', 'The player controller hub and every per-player BPC component', [
        '/Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C',
        '/Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C',
        '/Game/Sparta/Core/AI/BP_AICharacter.BP_AICharacter_C',
        '/Game/Sparta/Core/Player/Components/BPC_Player_Progression.BPC_Player_Progression_C',
        '/Game/Sparta/Core/Player/Components/BPC_Player_Experience.BPC_Player_Experience_C',
        '/Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C',
        '/Game/Sparta/Dialogue/QuestSystem/BPC_NarrativeEventsTracker.BPC_NarrativeEventsTracker_C',
        '/Game/Sparta/Core/Achievements/BPC_Achievements.BPC_Achievements_C',
        '/Game/Sparta/Core/Player/Components/BPC_PPItemComponent.BPC_PPItemComponent_C',
        '/Game/Sparta/Core/Player/Components/BPC_TarstoneComponent.BPC_TarstoneComponent_C',
        '/Game/Sparta/Dialogue/Blueprints/DiscipleSystem/BPC_HubEventComponent.BPC_HubEventComponent_C',
        '/Game/Sparta/UI/World/Map/Blueprints/BPC_WorldMapHandler.BPC_WorldMapHandler_C',
        '/Game/Sparta/Dialogue/Blueprints/ShellMemories/Blueprints/BPC_ShellMemoryHandler.BPC_ShellMemoryHandler_C',
        '/Game/Sparta/Core/Player/Components/BPC_TeleportManager.BPC_TeleportManager_C',
        '/Game/Sparta/Core/Components/BPC_GameplayAbilityTracker.BPC_GameplayAbilityTracker_C',
        '/Game/Sparta/Core/Player/Components/BPC_PlayerController_CombatTracker.BPC_PlayerController_CombatTracker_C',
    ]),
    (19, 'travel-actors', 'Fast-travel and gate actors used by the flow system', [
        '/Game/Sparta/Core/World/Teleporting/BP_STH_Entrance_Gate.BP_STH_Entrance_Gate_C',
        '/Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector.BP_LevelInstanceSelector_C',
        '/Game/Sparta/Core/World/BP_TeleportLocation.BP_TeleportLocation_C',
        '/Game/Sparta/Core/World/BP_TeleportLocation_OneShot.BP_TeleportLocation_OneShot_C',
        '/Game/Sparta/Core/World/Launch/BP_LandingArea_Teleport.BP_LandingArea_Teleport_C',
        '/Game/Sparta/Core/World/Levels/TombZone/BP_TombEntranceTeleporter.BP_TombEntranceTeleporter_C',
        '/Game/Sparta/Core/Interaction/BP_EndOfDungeonTeleporter.BP_EndOfDungeonTeleporter_C',
    ]),
]


def class_rows(c, script=None):
    q = "SELECT o.path,o.type,o.super,s.path AS sup FROM obj o LEFT JOIN obj s ON s.addr=UPPER(o.super) WHERE o.type IN (%s)" % ','.join('?' * len(CLASS_TYPES))
    return list(c.execute(q, CLASS_TYPES))


def select_classes(all_paths, specs):
    """Resolve topic specs (exact path, or 're:' regex) against known class paths."""
    known = set(all_paths)
    picked, seen = [], set()
    for spec in specs:
        if spec.startswith('re:'):
            rx = re.compile(spec[3:])
            for p in all_paths:
                if rx.search(p) and p not in seen:
                    seen.add(p)
                    picked.append(p)
        else:
            if spec in known:
                if spec not in seen:
                    seen.add(spec)
                    picked.append(spec)
    return picked


def main():
    os.makedirs(MINE, exist_ok=True)
    c = mine.db('index.sqlite3')
    ci = mine.db('index_ingame.sqlite3')

    rows = class_rows(c)
    all_paths = sorted({r['path'] for r in rows})
    live = {r[0] for r in ci.execute("SELECT DISTINCT path FROM obj WHERE path LIKE '/Game/%' OR path LIKE '/Script/Sparta.%'")}

    d7structs = {}
    sp = os.path.join(MINE, 'structs.tsv')
    if os.path.exists(sp):
        with open(sp, 'r', encoding='utf-8') as fh:
            next(fh)
            for line in fh:
                f = line.rstrip('\n').split('\t')
                if len(f) >= 7:
                    d7structs.setdefault(f[1], []).append(f)

    flags = {}
    fp = os.path.join(MINE, 'funcflags.tsv')
    if os.path.exists(fp):
        with open(fp, 'r', encoding='utf-8') as fh:
            next(fh)
            for line in fh:
                f = line.rstrip('\n').split('\t')
                if len(f) >= 4:
                    flags[(f[1], f[2])] = f[3]

    manifest = []
    missing = []
    targets = []

    for num, name, title, specs in TOPICS:
        picked = select_classes(all_paths, specs)
        for spec in specs:
            if not spec.startswith('re:') and spec not in set(all_paths) and '?' not in spec:
                missing.append(spec)
        props, funcs, structs = [], [], []
        for cls in picked:
            short_full = cls.split('.')[-1]
            short = short_full.replace('_C', '')
            for decl, nm, t, off, line in mine.flatten(c, cls):
                props.append((decl, cls, nm, t, off, line))
            for decl, fname, line, ps in mine.funcs(c, cls):
                fl = flags.get((short_full, fname)) or flags.get((short, fname), '')
                funcs.append((decl, cls, fname, '; '.join('%s %s' % (p[0], p[1]) for p in ps),
                              len(ps), fl, line))
            # struct dependencies of this class' own properties
            wanted = set()
            obj = mine.find_obj(c, cls)
            if obj:
                for r in mine.own_members(c, obj['addr']):
                    if r['refd'] and r['type'] == 'StructProperty':
                        wanted.add(r['refd'].rsplit('.', 1)[-1])
            seen_struct = set()
            for w in sorted(wanted):
                for key in (w, 'F' + w):
                    if key in seen_struct:
                        continue
                    seen_struct.add(key)
                    for srow in d7structs.get(key, []):
                        structs.append(srow)
        structs = sorted(set(map(tuple, structs)))

        f1 = 'topic-%02d-%s.props.tsv' % (num, name)
        f2 = 'topic-%02d-%s.funcs.tsv' % (num, name)
        f3 = 'topic-%02d-%s.structs.tsv' % (num, name)
        mine.write_tsv(f1, ('declared_in', 'class', 'member', 'type', 'offset', 'dump_line'), props)
        mine.write_tsv(f2, ('declared_in', 'class', 'function', 'params', 'nparams', 'flags', 'dump_line'), funcs)
        mine.write_tsv(f3, ('module', 'struct', 'field_offset', 'field_size', 'field_type', 'field', 'flags'), structs)
        manifest.append((f1, len(props)))
        manifest.append((f2, len(funcs)))
        manifest.append((f3, len(set(structs))))
        for cls in picked:
            targets.append((num, name, cls, 'live' if cls in live else 'static-only'))
        print('%-28s classes=%-4d props=%-5d funcs=%-4d structs=%d'
              % (name, len(picked), len(props), len(funcs), len(set(map(tuple, structs)))))

    mine.write_tsv('sync-targets.tsv', ('topic_no', 'topic', 'class', 'seen_in_ingame_dump'), targets)

    if missing:
        print('\n!! unresolved specs:')
        for m in sorted(set(missing)):
            print('   ', m)

    with open(os.path.join(MINE, 'manifest.json'), 'w', encoding='utf-8') as fh:
        json.dump({'files': manifest}, fh, indent=1)


if __name__ == '__main__':
    main()
