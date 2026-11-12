# 50 — Enemies, bosses, their AI, and the damage path (MS2 / Sparta, UE 5.6)

Scope of this file: enemy & boss classes, their AI controller / behaviour-tree wiring, aggro &
perception, spawn/respawn machinery, boss-fight lifecycle, and the **complete damage path**.
Out of scope (other reports): world state, quests, player look, player actions, items, menus,
animation catalogue.

Data source: `raw/index.sqlite3` (built from `raw/ObjectDump_allassets.txt`, main menu,
`LoadAllAssetsBeforeDumpingObjects=1`) plus `raw/CXXHeaderDump/Sparta.hpp`,
`raw/CXXHeaderDump/Sparta_enums.hpp` and the per-Blueprint `.hpp` files.

All commands below were run from `G:\Freespace\shell_coop\research`.

## Conventions used in this file

* Class rows are taken from the `cls` view (`addr`,`type`,`path`,`super`); `super` is the
  `[sps:]` SuperStruct resolved through `obj.addr`.
* **Naming convention (important for LookupObject):** in function/delegate tables the first column
  uses the **C++ class name** as written in `Sparta.hpp` (`ASpartaAICharacter`,
  `USpartaHealthComponent`, `ISpartaHittable`, …). The **reflection path** used by UE4SS drops the
  `A`/`U`/`I` prefix: `ASpartaAICharacter` → `/Script/Sparta.SpartaAICharacter`,
  `USpartaHealthComponent` → `/Script/Sparta.SpartaHealthComponent`,
  `UTargetSelectionComponent` → `/Script/Sparta.TargetSelectionComponent`. §1.3 lists the exact
  paths for the targeting classes because a few of them also lose the `Sparta` infix.
* Function parameter lists are the `*Property` children of the `Function` object **in dump order**
  (`ORDER BY line`). Blueprint compiler temporaries were filtered with:
  `path NOT LIKE '%:CallFunc\_%' / '%:K2Node\_%' / '%:Temp\_%' / '%:Goto\_%' / '%__Internal\_%'`.
  This is a documented filter (QUERYING.md), not a guess.
* The dump does **not** mark out-parameters. A parameter literally named `ReturnValue` /
  `Return` / `Result` is treated as the return value, nothing more.
* **Offsets**: `obj.off` is stored as a decimal integer of the hex offset. Displayed here as hex
  (184 → `0xB8`). The `... // 0xNNNN (size: 0xNN)` comments are copied verbatim from `Sparta.hpp`.
* Evidence lines in Appendix A are emitted byte-identically from the dump by
  `'['||addr||'] '||type||' '||path||rest` (`scripts\q\eb-20-ev-enemies.sql`, `eb-21-ev-bosses.sql`);
  the reconstruction was verified against `parse_dump.py raw` output.
* `UNKNOWN` + one line stating what was tried is used wherever a fact could not be established.

---

# 1. Native (C++) class inventory

## 1.1 AI / enemy / spawn bases

| Class | Super | Sync relevance |
|---|---|---|
| `/Script/Sparta.SpartaAICharacter` (`ASpartaAICharacter`) | `ASpartaCharacter` | enemy pawn base; aggro/deaggro/death delegates |
| `/Script/Sparta.SpartaAIController` (`ASpartaAIController`) | `AModularAIController` | enemy controller base; engagement state machine, BT/BB pointers |
| `/Script/Sparta.SpartaEnemySubsystem` (`USpartaEnemySubsystem`) | `UTickableWorldSubsystem` | world-level enemy registry, engaged set, aggressor ranking, death event |
| `/Script/Sparta.UnitSpawner` (`AUnitSpawner`) | `AActor` | per-unit spawn actor |
| `/Script/Sparta.SpartaHordeSpawner` (`ASpartaHordeSpawner`) | `AUnitSpawner` | horde spawn actor, `SpawnClass` |
| `/Script/Sparta.HordeSpawnerComponent` (`UHordeSpawnerComponent`) | `UActorComponent` | interval/max-active horde driver |
| `/Script/Sparta.HordeUnitComponent` (`UHordeUnitComponent`) | `UPawnComponent` | per-unit horde membership |
| `/Script/Sparta.HordeCharacterData` (`UHordeCharacterData`) | `UTypeSpecificData` | EQS ring parameters for horde members |
| `/Script/Sparta.HordeClusterCenter` (`AHordeClusterCenter`) | — | horde cluster actor (receives unit deaths) |
| `/Script/Sparta.SpartaEncounterManager` (`ASpartaEncounterManager`) | `AActor` | encounter grouping + start/completed delegates |
| `/Script/Sparta.SpartaHordeEncounter` (`ASpartaHordeEncounter`) | `AActor` | horde encounter actor |
| `/Script/Sparta.SpartaEncounterRecipe` (`USpartaEncounterRecipe`) | `UDataAsset` | spawn recipe data asset |
| `/Script/Sparta.SpawnTrigger` (`ASpawnTrigger`) | `AActor` | volume/trigger that owns spawners & recipes |
| `/Script/Sparta.SpartaMassActorSpawnerSubsystem` | — | Mass actor pooling (`ReleaseActorToPool`) |
| `/Script/Sparta.SpartaDialogueActor` | — | NPC dialogue actor (see `BP_DialogueActor*`) |

Note on the naming: the classes are `ASpartaAICharacter` / `ASpartaAIController` (actor prefix `A`),
not `USparta…`. `grep "^class U.*AICharacter"` in `Sparta.hpp` returns nothing — the `U` form does
not exist.

## 1.2 Damage-path classes

| Class | Super | Notes |
|---|---|---|
| `/Script/Sparta.ISpartaHittable` | `IInterface` | **receiver** interface, 2 functions |
| `/Script/Sparta.ISpartaHitApplier` | `IInterface` | **sender** interface, 1 function |
| `/Script/Sparta.ISpartaDamageCauser` | `IInterface` | `GetDamageTags()` |
| `/Script/Sparta.SpartaHitPayload` (`USpartaHitPayload`) | `UObject` | the payload — **a UObject, not a struct** |
| `/Script/Sparta.SpartaHitTrace` (`USpartaHitTrace`) | `UObject` | one trace instance |
| `/Script/Sparta.SpartaHitTraceComponent` (`USpartaHitTraceComponent`) | `UActorComponent` | trace host, 3 delegates |
| `/Script/Sparta.SpartaHitTraceSubsystem` (`USpartaHitTraceSubsystem`) | `UTickableWorldSubsystem` | deferred-dispatch queue |
| `/Script/Sparta.SpartaHitTraceShape` (`USpartaHitTraceShape`) | `UObject` | shape base, `Size: 0x28` |
| `/Script/Sparta.SpartaHitTraceShape_Capsule` | `USpartaHitTraceShape` | |
| `/Script/Sparta.SpartaHitTraceShape_Line` | `USpartaHitTraceShape` | |
| `/Script/Sparta.SpartaHitTraceShape_Sphere` | `USpartaHitTraceShape` | |
| `/Script/Sparta.SpartaHitTraceShapes_Box` | `USpartaHitTraceShape` | |
| `/Script/Sparta.SpartaDamageCauser` | — | |
| `/Script/Sparta.SpartaDamageDebugSubsystem` | — | `HandleDamageReceived` |
| `/Script/Sparta.SpartaExecutionCalculation` | `UGameplayEffectExecutionCalculation` | `Size: 0x40`, no reflected members |
| `/Script/Sparta.SpartaDamageExecution` | `USpartaExecutionCalculation` | **no reflected members at all** |
| `/Script/Sparta.SpartaBreakExecution` | `USpartaExecutionCalculation` | **no reflected members at all** |
| `/Script/Sparta.SpartaPoiseExecution` | — | |
| `/Script/Sparta.SpartaHealExecution` | — | |
| `/Script/Sparta.SpartaWindedExecution` | — | |
| `/Script/Sparta.ExecutionValueModifier` | — | `CanApplyExecutionModifier` |
| `/Script/Sparta.ExecutionModifierCalculation` | — | no reflected functions |
| `/Script/Sparta.DamageExecutionModifier` | — | |
| `/Script/Sparta.BreakExecutionModifier` | — | |
| `/Script/Sparta.PoiseExecutionModifier` | — | |
| `/Script/Sparta.HealExecutionModifier` | — | |
| `/Script/Sparta.WindedExecutionModifier` | — | |
| `/Script/Sparta.HitPayloadModifier` | — | 2 functions |
| `/Script/Sparta.WindedDamageBonus`, `/Script/Sparta.WindedPoiseDamageBonus` | — | no reflected functions |
| `/Script/Sparta.SpartaDeathCondition` | — | `Evaluate` |
| `/Script/Sparta.SpartaDeathCondition_ClosestBone` | `USpartaDeathCondition` | **no reflected functions** |
| `/Script/Sparta.SpartaDeathCondition_StrikeDirection` | `USpartaDeathCondition` | **no reflected functions** |
| `/Script/Sparta.SpartaDeathAnimationSet` (`USpartaDeathAnimationSet`) | `UDataAsset` | `Size: 0xA8` |
| `/Script/Sparta.SpartaHitReactionAnimSet` (`USpartaHitReactionAnimSet`) | `UDataAsset` | `Size: 0xA8` |
| `/Script/Sparta.SpartaPhysicalHitReactionData` | — | bone-chain lookup |
| `/Script/Sparta.SpartaHealthComponent` (`USpartaHealthComponent`) | `UGameFrameworkComponent` | 6 delegates |
| `/Script/Sparta.SpartaHealthSet` (`USpartaHealthSet`) | `USpartaAttributeSet` | 38 attributes |
| `/Script/Sparta.SpartaAnimNotify_HitCheck` | — | |
| `/Script/Sparta.SpartaAnimNotifyState_HitCheck` | — | |

## 1.3 Targeting / perception classes

| Dump path (reflection) | C++ name in `Sparta.hpp` | Super |
|---|---|---|
| `/Script/Sparta.AIDirectorSubsystem` | `UAIDirectorSubsystem` | `UTickableWorldSubsystem` |
| `/Script/Sparta.SpartaAIPerceptionSystem` | `USpartaAIPerceptionSystem` | `UAIPerceptionSystem` |
| `/Script/Sparta.TargetSelectionComponent` | `UTargetSelectionComponent` | `UControllerComponent` |
| `/Script/Sparta.SpartaTargetLockComponent` | `USpartaTargetLockComponent` | `UActorComponent` |
| `/Script/Sparta.SpartaTargeter` | `USpartaTargeter` | `UObject` |
| `/Script/Sparta.SpartaTargeter_FrontalCone` | `USpartaTargeter_FrontalCone` | `USpartaTargeter` |
| `/Script/Sparta.SpartaTargeter_UseOwner` | `USpartaTargeter_UseOwner` | `USpartaTargeter` |
| `/Script/Sparta.SpartaTargetableComponent` | `USpartaTargetableComponent` | `UWidgetComponent` |
| `/Script/Sparta.SpartaTargetableInterface` | `ISpartaTargetableInterface` | `IInterface` |
| `/Script/Sparta.SoftTargetingComponent` | `USoftTargetingComponent` | `UActorComponent` |
| `/Script/Sparta.SoftTargetingSettings` | `USoftTargetingSettings` | `UObject` |

**Reflection paths drop the `U`/`I` prefix** for several classes — this is a real trap. Getting the
path wrong (`/Script/Sparta.UTargetSelectionComponent`) returns nothing. Verified with
`eb-24-targetclasses.sql`. Two names behave *non*-uniformly: `SpartaTargetLockComponent`,
`SpartaTargeter*`, `SpartaAIPerceptionSystem` and `SpartaTargetableComponent` keep the `Sparta`
prefix and lose the `U`, while `TargetSelectionComponent`, `AIDirectorSubsystem`,
`SoftTargetingComponent`, `SoftTargetingSettings` lose both.
`/Script/Sparta.SpartaSaveInterface` is the `ISpartaSaveInterface` reflection path.

**`SpartaTargetSelectionComponent` does not exist** — the reflection path is
`/Script/Sparta.TargetSelectionComponent` (no `Sparta` prefix). There is no
`SpartaAIPerceptionSystem`-prefixed-with-U type either; the dump path is
`/Script/Sparta.SpartaAIPerceptionSystem` (`Sparta.hpp` line 3680).

---

# 2. Enemy Blueprint families (`/Game/Sparta/Characters/Enemies/`)

**709** `BlueprintGeneratedClass` objects live under this folder; **470** of them are `GA_*`
abilities, leaving **239** non-ability Blueprint classes (`eb-25-counts.sql`: `path NOT GLOB
'*/GA_*'`). §2.1 enumerates the character/controller classes; the complete 239-row list with its
verbatim dump lines is Appendix A.

Every playable enemy character derives from
`/Game/Sparta/Core/AI/BP_AICharacter.BP_AICharacter_C` → `/Script/Sparta.SpartaAICharacter`,
and every enemy controller from
`/Game/Sparta/Core/AI/BP_AIController.BP_AIController_C` → `/Script/Sparta.SpartaAIController`.
The only exceptions among character classes are the horde units (→ `BP_HordeCharacter_C`),
`BP_NPC*`/`BP_NPC_Ghost` (non-hostile NPCs), `BP_Shepherd_Staff_Character` (→
`/Script/Sparta.SpartaAICharacter` directly) and projectile/damage-area actors
(→ `/Script/Engine.Actor` or a `BP_Projectile*Base`).

## 2.1 Family table — characters, controllers, behaviour trees

`BT` is the `BehaviorTree` asset(s) in that family folder (asset enumeration is fact; the
binding of a specific BT to a specific controller is `GUESS:` — see §2.3).

| Family folder | Character BP(s) | Controller BP(s) | BehaviourTree asset(s) in folder |
|---|---|---|---|
| `Aristocrat` | `BP_Aristocrat_C`, `BP_LordVellen_MB_C`, `BP_SicarioTheKeeper_C`, `BP_ThornboiWithWeapon_C` | `BP_Aristocrat_Controller_C`, `BP_Thorn_Aristocrat_Controller_C` | `BT_Aristocrat_Base`, `BT_Thorn_Aristocrat_Base` |
| `Babka/Accordion` | `BP_Babka_Accordion_C` | `BP_Babka_Accordion_Controller_C` | `BT_Babka_Accordion_Base`, `BT_PairedAnimReaction_Babka_Accordion` |
| `Babka/Broom` | `BP_Babka_Broom_C` | `BP_Babka_Controller_C` | `BT_Babka_Base`, `BT_PairedAnimReaction_Babka_Broom` |
| `BabyEnemy` | `BP_BabyEnemy_C` | `BP_BabyEnemy_Controller_C` | `BT_BabyEnemy_Aggressive` |
| `BabySnail` | `BP_BabySnail_C` | `BP_BabySnail_Controller_C` | `BT_BabySnail_Aggressive` |
| `BallBoy` | `BP_BallBoy_C`, `BP_BallBoy_Miniboss_Memory_C` | `BP_BallBoy_Controller_C` | `BT_BallBoy_Aggressive` |
| `BallistaHead` | `BP_BallistaHead_C`, `BP_BallistaHead_Lightning_C`, `BP_BallistaHead_Miniboss_C` | `BP_BallistaHead_Controller_C`, `BP_BallistazookaHead_Controller_C` | `BT_BallistaHead_Base_Aggressive`, `BT_BallistaHead_Base_Aggressive_Lightning`, `BT_BallistazookaHead_Miniboss` |
| `Batman` | `BP_Batman_C`, `BP_Batman_Suicide_C` | `BP_Batman_Controller_C`, `BP_Batman_Suicide_Controller_C` | `BT_Batman_Base_Aggressive`, `BT_Batman_Suicide_Base_Aggressive` |
| `Batushka` | `BP_Batushka_C`, `BP_Batushka_MiniBoss_C` | `BP_Batushka_Controller_C` | `BT_Batushka_Base` |
| `BellSnail` | `BP_BellSnail_C`, `BP_BoneSnail_C` | `BP_BellSnail_Controller_C` | `BT_BellSnail_Aggressive` |
| `BladeSlave` | `BP_BladeSlave_C` | `BP_BladeSlave_Controller_C` | `BT_BladeSlave_Base` |
| `Brigands/BrigBase` | `BP_Brigand_Base_C`, `BP_Brigand_Regal_C`, `BP_Brigand_Tiel_Memory_C` | `BP_BrigBase_Controller_C` | `BT_BrigBase_Base`, `BT_PairedAnimReaction_Taunt` |
| `Brigands/BrigCrossbow` | `BP_Brigand_Crossbow_C` | `BP_BrigBase_Crossbow_Controller_C` | `BT_BrigBase_Crossbow_Base`, `BT_BrigBase_Crossbow_Ranged`, `BT_BrigRegal_Crossbow_Ranged`, `BT_BrigRegal_Melee` |
| `Brigands/BrigElite` | `BP_Brigand_Elite_C`, `BP_Brigand_Elite_Jumper_C` | `BP_Brigand_EliteController_C` | `BT_Brigand_Elite_Base`, `BT_Brigand_Elite_JUMPER` |
| `Brigands/BrigElite/BrigElite_Fire` | `BP_Brigand_Elite_Fire_MiniBoss_C` | `BP_Brigand_Elite_FireMiniboss_Controller_C` | `BT_Brigand_Elite_Fire_MiniBoss` |
| `Brigands/BrigElite/BrigElite_Winter` | `BP_Brigand_Elite_Winter_MiniBoss_C` | `BP_Brigand_Elite_Winter_MiniBoss_Controller_C` | `BT_Brigand_Elite_Winter_MiniBoss` |
| `Brigands/BrigKnightly` | `BP_Brigand_Knightly_C` | `BP_Brigand_KnightlyController_C` | `BT_BrigBase_Knightly` |
| `Brigands/BrigRanged_Rock` | `BP_Brigand_Ranged_C`, `BP_Brigand_Ranged_Tanky_C` | `BP_AIController_Brigand_Ranged_C` (→ `BP_HordeAIController_C`) | `BT_Brigand_Ranged` |
| `Brigands/BrigTanky` | `BP_TankyBrigand_C`, `BP_TankyBrigand_Bronze_C`, `BP_TankyBrigand_Sword_C` | `BP_TankyBrigand_Controller_C`, `BP_TankyBrigand_Sword_Controller_C` | `BT_TankyBrigand_Aggressive`, `BT_TankyBrigand_Sword_Aggressive` |
| `Brigands/BrigTarded` | `BP_Horde_Brigand_Tarded_C` | — | — |
| `Brigands/Heavy` | `BP_Brigand_Heavy_C` | `BP_Brigand_HeavyController_C` | `BT_Brigand_Heavy_Base`, `BT_PairedAnimReaction_Brigand_Heavy` |
| `Brigands/HordeBrigand` | `BP_Horde_Brigand_C`, `BP_HeadKing_Villager_C` | `BP_AIController_Horde_Brigand_Light_C` | `BT_Horde_BaseAggressive` |
| `Brigands/HordeGrenadier` | `BP_Horde_Brigand_Grenadier_C` | `BP_AIController_Horde_Brigand_Grenadier_C` | `BT_Horde_Brigand_Grenadier` |
| `Brigands/HordeShieldBrigand` | `BP_Horde_Brigand_Shield_C` | `BP_AIController_Horde_Brigand_Shield_C` | `BT_Horde_Brigand_Shield` |
| `CannibalKnight` | `BP_Cannibal_Knight_C`, `BP_Cannibal_Knight_MiniBoss_C`, `BP_Cannibal_Knight_Miniboss_Memory_C`, `BP_Cannibal_Knight_Zombie_C` | `BP_CannibalKnight_Controller_C` | `BT_Cannibal_KnightBoss_Aggressive`, `BT_Cannibal_Knight_Aggressive`, `BT_Cannibal_Knight_Aggressive_Memory`, `BT_Hierarch` |
| `CentipedeGhost` | `BP_CentipedeGhost_C` | `BP_CentipedeGhost_Controller_C` | `BT_CentipedeGhost_Aggressive` |
| `CultistBase` | `BP_Cultist_Base_C`, `BP_Cultist_Mace_C`, `BP_Cultist_Sword_C`, `BP_Cultist_Sword_Purple_C` | `BP_CultistBase_Controller_C` | `BT_CultistBase_Base`, `BT_PairedAnimReaction_Cultist_Base` |
| `CultistRanged` | `BP_Cultist_Ranged_C`, `BP_Cultist_Ranged_Purple_C` | `BP_CultistRanged_Controller_C` | `BT_CultistRanged_Base` |
| `CultistSpearLady` | `BP_CultistSpearLady_C`, `BP_SesterSecundusMiniBoss_C` | `BP_CultistSpearLady_Controller_C`, `BP_SecundusMiniboss_Controller_C` | `BT_CultistSpearLady`, `BT_GenessaPrime_Memory`, `BT_PairedAnimReaction_CultistSpearLady`, `BT_SecundusMiniboss` |
| `Draugr` | `BP_Draugr_C` | `BP_Draugr_Controller_C` | `BT_Draugr` |
| `DungeonChampion` | `BP_DungeonChampion_C` | `BP_DungeonChampion_Controller_C` | `BT_DungeonChampion_Base` |
| `FrogMama` | `BP_FrogMama_C` | `BP_FrogMama_Controller_C` | `BT_FrogMama_Base_Aggressive` |
| `Grisha` | `BP_Grisha_C`, `BP_Grisha_Alien_C`, `BP_Grisha_Caged_Miniboss_C`, `BP_Grisha_Pale_C` | `BP_GrishaController_C` | `BT_BaseGrisha`, `BT_PairedAnimReaction_Grisha` |
| `GrishaHunter/Axe` | `BP_GrishaHunter_Axe_C` | `BP_GrishaHunter_Axe_Controller_C` | `BT_GrishaHunter_Axe_Base` |
| `GrishaHunter/Bow` | `BP_GrishaHunter_Bow_C`, `BP_TarredArcher_C` | `BP_GrishaHunter_Bow_Controller_C` | `BT_GrishaHunter_Bow_Base` |
| `GrishaHunter/Spear` | `BP_GrishaHunter_Spear_C` | `BP_GrishaHunter_Spear_Controller_C` | `BT_GrishaHunter_Spear_Base` |
| `HeadTree` | `BP_HeadTree_C` | `BP_HeadTree_Controller_C` | `BT_HeadTree_Base_Aggressive` |
| `HutchbackCarrier` | `BP_Hutchback_C`, `BP_Hutchback_Miniboss_C` | `BP_Hutchback_Controller_C` | `BT_Hutchback_Aggressive`, `BT_Hutchback_MiniBoss_Aggressive` |
| `HutchbackCarrier/Clone` | `BP_HutchbackClone_C` | `BP_Hutchback_Clone_Controller_C` | `BT_Hutchback_Clone_Aggressive` |
| `MS1_HeavyCultist` | `BP_HeavyCultist_C`, `BP_HeavyCultist_MiniBoss_C` | `BP_HeavyCultist_Controller_C`, `BP_HeavyCultist_Miniboss_Controller_C` | `BT_HeavyCultist_Base`, `BT_HeavyCultist_Miniboss` |
| `MS1_TwinSisters/Crossbow` | `BP_TwinSister_Crossbow_C` | `BP_TwinSister_Crossbow_Controller_C` | `BT_TwinSister_Crossbow_Base`, `BT_TwinSister_Crossbow_MiniBoss` |
| `MS1_TwinSisters/Daggers` | `BP_TwinSister_Daggers_C` | `BP_TwinSister_Daggers_Controller_C` | `BT_TwinSister_Daggers_Base`, `BT_TwinSister_Daggers_MiniBoss` |
| `MS1_TwinSisters/SpearDagger` | `BP_TwinSister_SpearDagger_C`, `BP_TwinSister_SpearDagger_Miniboss_C` | `BP_TwinSister_SpearDagger_Controller_C` | `BT_TwinSister_SpearDagger_Base`, `BT_TwinSister_SpearDagger_MiniBoss` |
| `Miner` | `BP_Miner_C`, `BP_MinerKing_C` | `BP_Miner_Controller_C` | `BT_Miner_Base`, `BT_Miner_King` |
| `MultiHeadedHarpy` | `BP_MultiHeadedHarpy_C` | `BP_MultiHeadedHarpy_Controller_C` | `BT_MultiHeadedHarpy_Base` |
| `PapaRoach` | `BP_PapaRoach_C`, `BP_PapaRoach_Elite_C` | `BP_PapaRoach_Controller_C` | `BT_PapaRoach_Base` |
| `ParasiteKnight` | `BP_ParasiteKnight_C`, `BP_ParasiteKnight_Green_C` | `BP_ParasiteKnight_Controller_C` | `BT_ParasiteKnight_Aggressive` |
| `ShellSnatcher` | `BP_Shellsnatcher_C` | `BP_Shellsnatcher_Controller_C` | `BT_Shellsnatcher_Base` |
| `Shepherd` | `BP_Shepherd_C` | `BP_Shepherd_Controller_C` | `BT_Shepherd_Base` |
| `Shepherd/Sheep` | `BP_Sheep_C`, `BP_SheepEvil_C`, `BP_SheepGood_C` | `BP_Sheep_Controller_C` | `BT_Sheep_Base`, `BT_PairedAnimReaction_Sheep` |
| `Sicario` | `BP_Sicario_C`, `BP_Sicario_MiniBoss_C` | `BP_Sicario_Controller_C` | `BT_Sicario_Base`, `BT_Sicario_Miniboss` |
| `Slug` | `BP_Slug_C`, `BP_Slug_Elite_C` | `BP_Slug_Controller_C` | `BT_Slug_Base_Aggressive` |
| `Snowworm` | `BP_Snowworm_C` | `BP_Snowworm_Controller_C` | `BT_Snowworm_Base` |
| `Spider` | `BP_Spider_C`, `BP_Spider_Elite_C` | `BP_Spider_Controller_C` | `BT_Spider_Base_Aggressive`, `BT_PairedAnimReaction_Spider` |
| `SpiderBro` | `BP_SpiderBro_C`, `BP_SpiderBro_Elite_C`, `BP_SpiderBro_Memory_C` | `BP_SpiderBro_Controller_C` | `BT_SpiderBro_Base_Aggressive`, `BT_SpiderBro_Base_Aggressive_Memory`, `BT_PairedAnimReaction_SpiderBro` |
| `StoneCrab` | `BP_StoneCrab_C`, `BP_StoneCrab_MB_C`, `BP_StoneCrab_Obsidian_C` | `BP_StoneCrab_Controller_C` | `BT_StoneCrab_Base_Aggressive`, `BT_StoneCrab_MiniBoss` |
| `TarredCorpse` | `BP_TarredCorpse_C`, `BP_TarredCorpse_EggKeeper_C`, `BP_TarredCorpse_Fire_C`, `BP_TarredCorpse_MinibossNecromancer_C`, `BP_Tarred_Foundling_C` | `BP_TarredCorpse_Controller_C` | `BT_TarredCorpse_Aggressive`, `BT_TarredCorpse_MiniBoss_Aggressive` |
| `TarredStoner` | `BP_TarredStoner_C` | `BP_AIController_TarredStoner_C` (→ `BP_HordeAIController_C`) | `BT_TarredStoner` |
| `TarredVestige` | `BP_TarredVestige_C`, `BP_TarredVestige_MiniBoss_C` | `BP_TarredVestige_Controller_C` | `BT_TarredVestige_Base_Aggressive`, `BT_TarredVestige_MiniBoss_Aggressive` |
| `TheCoffin` | `BP_TheCoffin_C` | `BP_TheCoffin_Controller_C` | `BT_TheCoffin_Base` |
| `TheHead` | `BP_TheHead_C`, `BP_TheHeadKing_C` | `BP_TheHead_Controller_C` | `BT_TheHead_Base`, `BT_TheHead_Miniboss`, `BT_PairedAnimReaction_TheHead` |
| `ToxicStalker` | `BP_ToxicStalker_C`, `BP_ToxicStalkerHomunculus_C`, `BP_ToxicStalker_Frost_C` | `BP_ToxicStalker_Controller_C` | `BT_ToxicStalker_Aggressive`, `BT_ToxicStalker_Frost`, `BT_ToxicStalker_Hommunculus`, `BT_PairedAnimReaction_ToxicStalker` |
| `Turrets/LarvaTurret` | `BP_TurretLivingBase_C`, `BP_TurretLivingBase_PairTest_C`, `BP_TurretLiving_Poison_C` | — | `BT_LivingTurret` |
| `Vampire` | `BP_Vampire_C`, `BP_VampireKnight_C`, `BP_VampireKnight_Part2_C`, `BP_Vampire_Cultist_C` | `BP_Vampire_Controller_C`, `BP_Vampire_MiniBoss_Controller_C` | `BT_Vampire_Aggressive`, `BT_Vampire_Knight_Phase_2`, `BT_PairedAnimReaction_Vampire` |
| `Wraith` | `BP_Wraith_C`, `BP_Wraith_Dark_C`, `BP_Wraith_MiniBoss_C` | `BP_Wraith_Controller_C` | `BT_Wraith_Base`, `BT_Wraith_Miniboss_Base`, `BT_PairedAnimReaction_Wraith` |
| `_Shared` | `ABPI_Enemy_C` (Blueprint interface, → `/Script/CoreUObject.Interface`) | — | — |

Shared base BTs (in `/Game/Sparta/Core/AI/`): `BT_Base`, `BT_BaseAggressive`, `BT_BaseShell`,
`BT_Base_Dummy`, `BT_BrigBaseAggressive`, `BT_CoordinatedMovement`, `BT_NPC`,
`BT_PairedAnimReactionBase`, `BT_PairedAnimReactionBase_Far`, `BT_PairedAnimReaction_Wait`.

Shared Blackboards (`BlackboardData`) — **only 4 exist in the whole build**:
`/Game/Sparta/Core/AI/BB_Base.BB_Base`, `BB_Base_Memories`, `BB_NPC`,
`/Game/Sparta/Characters/Enemies/Brigands/HordeBrigand/BB_Horde.BB_Horde`.

**No `StateTree` assets exist in this build.** Verified by
`SELECT type,COUNT(*) FROM obj WHERE type LIKE '%StateTree%' OR type LIKE '%Behavior%' OR type LIKE '%Blackboard%' GROUP BY type`
→ only `BehaviorTree` (130 rows) and `BlackboardData` (4 rows).

## 2.2 Per-family health component / montages / projectiles

| Aspect | Fact |
|---|---|
| Health component of an enemy | inherited `USpartaHealthComponent` on `ASpartaCharacter` at offset **`0x06A0`**; no enemy BP defines its own. `BP_AICharacter` only adds `BPC_EnemyHealthWidget_C Enemy` (`0xb48`) and `WBP_EnemyHealth` (`0xbc0`). |
| Hit reaction set | `USpartaHitReactionAnimSet* HitReactionAnimSet` at **`0x06A8`** on `ASpartaCharacter` |
| Hit trace component | `USpartaHitTraceComponent* HitTraceComponent` at **`0x06C0`** |
| Anim BP per family | `ABP_<Family>_C` in the family folder, most deriving from `/Game/Sparta/Characters/Enemies/_Shared/ABP_AI_Humanoid.ABP_AI_Humanoid_C` (verbatim examples in Appendix A) |
| Montage assets actually played | `UNKNOWN` at family granularity: the montage is set on the `AnimMontage* Montage` / `SoftObjectProperty` field of each `GA_*` ability BP, and the dump records property rows but **not their values**. I tried `parse_dump.py show` on `GA_*` classes and the `Default__` CDOs — only member offsets, never values. `GA_PlayMontageBase_C` is the common ability base. The animation catalogue is another agent's area. |
| Projectile classes | see §2.4 |
| Spawner / respawn | see §5 |

## 2.3 Behaviour-tree ↔ controller binding

**`UNKNOWN` (value-level).** `ASpartaAIController` holds
`class UBehaviorTree* BaseBehaviorTree; // 0x03D0` and `class UBlackboardData* BlackboardData; // 0x03D8`
(`Sparta.hpp` line 1683 block) plus `TArray<FDynamicTreeOverride> DynamicTreeOverrides; // 0x03E0`.
The dump lists the *property* on each `BP_*_Controller_C`, but UPROPERTY **values are not in the
object dump**, so which BT asset each controller CDO points at cannot be read from this data.
What I tried: `parse_dump.py show` on `BP_Aristocrat_Controller_C` and on
`BP_Aristocrat_Controller.Default__BP_Aristocrat_Controller_C` — subobjects (PerceptionComponent,
BrainComponent, Blackboard, TargetSelectionComponent, PathFollowingComponent) are listed, the BT
value is not. Runtime resolution would need a live `Blueprint` CDO read through UE4SS.

`GUESS:` `BT_<Family>_Base` (or `BT_<Family>_Aggressive`) is the base tree for
`BP_<Family>_Controller_C` in the same folder — the naming is 1:1 in every family above that has
both, but this is an inference from names, not a dump value.

## 2.4 Enemy projectile / damage-area classes per family

| Family | Projectile / area BP classes |
|---|---|
| `BallistaHead` | `BP_Projectile_Ballista_C`, `BP_Projectile_Ballista_Lightning_C`, `BP_Projectile_Ballistazooka_Miniboss__C`, `BP_Projectile_Ballistazooka_Miniboss_Bombarment_C` |
| `Batman` | `BP_ProjectileParticle_Batman_C` |
| `Brigands` | `BP_Projectile_Crossbow_Brigand_Regal_C`, `BP_BrigRanged_Projectile_C`, `BP_VFX_BrigandRockDestroyed_C` |
| `CentipedeGhost` | `BP_ProjectileParticle_CentipedeGhost_AOE_C`, `BP_Growing_BoxEffectCollider_C` |
| `CultistRanged` | `BP_RangedCultistFrogBlastProjectile_C` |
| `CultistSpearLady` | `BP_Projectile_CultistSpearLady_C` |
| `DungeonChampion` | `BP_Projectile_DungeonChampion_C`, `BP_Projectile_DungeonChampion_Return_C` |
| `FrogMama` | `BP_FrogMama_Projectile_C`, `BP_GroundPulsatingProjectileFrogMama_C` |
| `GrishaHunter` | `BP_Projectile_GrishaHunter_C` |
| `MS1_TwinSisters` | `BP_Projectile_TwinSister_Crossbow_C` |
| `MultiHeadedHarpy` | `BP_ProjectileParticle_MultiHeadedHarpy_C`, `BP_MultiHeadedharpy_Scream_DamageArea_C` |
| `MS1_HeavyCultist` | `BP_DamageArea_Shockwave_HeavyCultist_C` |
| `Miner` | `BP_FallRock_C`, `BP_RockFalling_DamageArea_C` |
| `SpiderBro` | `BP_Projectile_SpiderBro_C`, `BP_Projectile_Web_Grab_SpiderBro_C` |
| `TarredCorpse` | `BP_TarredCorpse_MiniBoss_BlastProjectile_C` |
| `TarredStoner` | `BP_TarredStoner_Projectile_C` |
| `TarredVestige` | `BP_RootProjectile_C`, `BP_RootProjectile_MiniBoss_C` |
| `Turrets` | `BP_LivingTurretHomingProjectile_C`, `BP_LivingTurretPoisonHomingProjectile_C` |
| `ToxicStalker` | `BP_ToxicStalker_Death_Burst_C` (+`_Frost`, `_Hommunculus`), `BP_ToxicStalker_Fart_AOE_C` (+`_Frost`, `_Hommunculus`) |
| `ShellSnatcher` | `BP_GrabbedShell_Dummy_C` |

---

# 3. Boss families (`/Game/Sparta/Characters/Bosses/`)

**278** `BlueprintGeneratedClass` objects live under this folder; **224** are `GA_*` abilities,
leaving **54** non-ability Blueprint classes in 12 boss **families**. All boss characters derive from
`/Game/Sparta/Core/AI/BP_AICharacter.BP_AICharacter_C`.

| Family folder | Character BP(s) | Controller BP(s) | BehaviourTree asset(s) |
|---|---|---|---|
| `BigBottle` | `BP_BigBottle_Ghost_C` (→ `BP_NPC_Ghost_C`, i.e. a ghost NPC) | `BP_BigBottleController_C` | `BT_BigBottle` |
| `Dwarf` | `BP_DwarfBoss_C` | `BP_DwarfBoss_Controller_C` | `BT_DwarfBoss_Base`, `BT_PairedAnimReaction_DwarfBoss` |
| `FinalBoss` | `BP_FinalBoss_C` | `BP_FinalBoss_Controller_C` | `BT_FinalBoss`, `BT_PairedAnimReaction_FinalBoss` |
| `HeadBoss` | `BP_HeadBoss_C` | `BP_HeadBossController_C` | `BT_HeadBoss` |
| `Hexapod` | `BP_Hexapod_C` | `BP_HexapodController_C`, `BP_HexapodController_Memory_C` | `BT_Hexapod`, `BT_Hexapod_Memories`, `BT_PairedAnimReaction_Hexapod` |
| `LadyOfTheWoods` | `BP_LadyOfTheWoods_C` | `BP_LadyOfTheWoodsController_C` | `BT_LadyOfTheWoods` |
| `Monolith` | `BP_Monolith_C` | `BP_Monolith_Controller_C` | `BT_Monolith_Base` |
| `MothKnight` | `BP_MothKnight_C` | `BP_MothKnightController_C` | `BT_MothKnight` |
| `Offspring` | `BP_Offspring_C` | `BP_Offspring_Controller_C` | `BT_Offspring_Base`, `BT_PairedAnimReaction_Offspring` |
| `ParasiteGolem` | `BP_ParasiteGolem_C`, `BP_ParasiteGolem_Cinematic_C` | `BP_ParasiteGolem_Controller_C` | `BT_ParasiteGolem_Base` |
| `Swordman` | `BP_Swordman_C` | `BP_Swordman_Controller_C` | `BT_Swordman_Base`, `BT_PairedAnimReaction_Swordman` |
| `TarGolem` | `BP_TarGolem_C`, `BP_TarGolem_Headless_C` | `BP_TarGolemController_C`, `BP_TarGolemParasiteController_C` | `BT_TarGolem`, `BT_TarGolem_Headless` |
| `Shared` | — | — | — (holds `GA_Boss_Stun_PhaseChange_C`) |

Boss cinematics (sequences with a `DirectorBP`): `CIN_DwarfBoss_01`, `LS_FinalBoss_Intro`,
`LevelSequence_Monolith_Intro`, `CIN_Swordman_Intro_01`, `BP_ParasiteGolem_Cinematic`.

## 3.1 Boss-fight lifecycle — `BPC_BossAI`

The boss state machine is a component, not a native class:
`/Game/Sparta/Core/AI/Components/BPC_BossAI.BPC_BossAI_C`, super `/Script/Engine.ActorComponent`,
**58 members**. This is the single most important boss class for sync.

| Property | Type | Offset | Notes |
|---|---|---|---|
| `UberGraphFrame` | StructProperty | `0xb8` | BP frame |
| `bTriggeredSecondPhase` | BoolProperty | `0xc0` | phase flag |
| `bTriggeredThirdPhase` | BoolProperty | `0xc1` | phase flag |
| `bIsAggroed` | BoolProperty | `0xc2` | |
| `BossAICharacter` | `BP_AICharacter_C` | `0xc8` | cached owner |
| `SpartaAIController` | `/Script/Sparta.SpartaAIController` | `0xd0` | cached controller |
| `OnBossDeath` | MulticastInlineDelegate | `0xd8` | fight end / death |
| `OnBossAggro` | MulticastInlineDelegate | `0xe8` | fight start (aggro) |
| `SecondPhaseHealthPercent` | DoubleProperty | `0xf8` | phase-2 threshold |
| `ThirdPhaseHealthPercent` | DoubleProperty | `0x100` | phase-3 threshold |
| `OnBossDeaggro` | MulticastInlineDelegate | `0x108` | |
| `OnSecondPhaseStarted` | MulticastInlineDelegate | `0x118` | phase-2 start |
| `CameraModifierHande` | `CSCameraStateModifierHandle` | `0x128` | boss cam |
| `CameraModifier` | `CSCameraStateModifier` | `0x130` | |
| `VestigeItem` | ClassProperty | `0x238` | **reward item on death** |
| `VestigeEffects` | ArrayProperty of ClassProperty | `0x240` | reward effects |
| `ItemStacks` | IntProperty | `0x250` | reward quantity |
| `bIsDead` | BoolProperty | `0x254` | |
| `TempBossHealthCalc` | DoubleProperty | `0x258` | |
| `OnThirdPhaseStarted` | MulticastInlineDelegate | `0x260` | phase-3 start |

Functions (filtered):
`HandleAggro()`, `HandleDeaggro()`, `IsAlreadyAggroed(ReturnValue:BoolProperty)`,
`BroadcastFightStartEvent()`, `BroadcastFightEndEvent()`, `CacheCharacter(Success:BoolProperty)`,
`CacheController(Success:BoolProperty)`.
`BroadcastFightStartEvent` spawns
`/Game/Sparta/Core/GES/GlobalEvent_BossFight_Started.GlobalEvent_BossFight_Started_C`;
`BroadcastFightEndEvent` spawns
`/Game/Sparta/Core/GES/GlobalEvent_BossFight_Ended.GlobalEvent_BossFight_Ended_C`
(both appear as `CallFunc_SpawnObject_ReturnValue` object types inside those functions).

## 3.2 Boss / encounter arena & trigger classes

Searched `Volume|Trigger|Arena|Gate|Fog|Wall` under `/Game/Sparta/Characters/Bosses/%` and
`/Game/Sparta/Maps/%` (`eb-07-arena.sql`).

| Class | Parent | Notes |
|---|---|---|
| `/Game/Sparta/Maps/MAPS/LI/LI_Hexapod_Arena_CoreWorld.LI_Hexapod_Arena_CoreWorld_C` | `/Script/Engine.LevelScriptActor` | Hexapod arena level script |
| `/Game/Sparta/Maps/MAPS/LI/PLA_Boss_Gate_Transition_02.PLA_Boss_Gate_Transition_02_C` | `/Script/Engine.PackedLevelActor` | boss gate transition |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BlockoutGate.BP_BlockoutGate_C` | `/Script/Engine.Actor` | |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_BreakableWall.BP_BreakableWall_C` | `/Script/Engine.Actor` | |
| `/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_CB_SpawnEnemyTrigger.BP_CB_SpawnEnemyTrigger_C` | `/Script/Engine.Actor` | |
| `/Game/Sparta/Maps/Playgrounds/MiniBossEncArenas/L_Shepherd_Sheep_EncArena.L_Shepherd_Sheep_EncArena_C` | `/Script/Engine.LevelScriptActor` | test arena |
| `/Game/Sparta/Characters/Bosses/LadyOfTheWoods/Abilities/GA_LadyOfTheWoods_FireWall_AOE.GA_LadyOfTheWoods_FireWall_AOE_C` | `GA_AttackBase_C` | ability named "FireWall", **not** an arena wall |

**No dedicated `BossArenaVolume` / `FogWall` class exists in this build.** `UNKNOWN`: I searched all
class paths matching `Volume|Trigger|Arena|Gate|Fog|Wall` across the whole `/Game` tree for Volume
and Trigger types and found no fog-wall or arena-volume class; the actual gating must therefore be
level geometry + `ASpawnTrigger` + `ASpartaEncounterManager` (see §5), or a class named without
those words. The `BP_SpawnLimitsVolume` referenced by `BP_AICharacter.SpawnLimitsVolume` (`0xcb0`)
is the closest match found (`/Game/Sparta/Characters/SpawnSystem/BP_SpawnLimitsVolume.BP_SpawnLimitsVolume_C`).

## 3.3 Fight start / end / phase / defeat functions

| Class | Function | Parameters (name: type) |
|---|---|---|
| `BPC_BossAI_C` | `HandleAggro` | — |
| `BPC_BossAI_C` | `HandleDeaggro` | — |
| `BPC_BossAI_C` | `IsAlreadyAggroed` | `ReturnValue: BoolProperty` |
| `BPC_BossAI_C` | `BroadcastFightStartEvent` | — |
| `BPC_BossAI_C` | `BroadcastFightEndEvent` | — |
| `BPC_BossAI_C` | `OnBossAggro__DelegateSignature` | — |
| `BPC_BossAI_C` | `OnBossDeaggro__DelegateSignature` | — |
| `BPC_BossAI_C` | `OnBossDeath__DelegateSignature` | — |
| `BPC_BossAI_C` | `OnSecondPhaseStarted__DelegateSignature` | — |
| `/Script/Sparta.SpartaAIController` | `HandleAggro` | `in_Payload: StructProperty of /Script/Sparta.SpartaEngagementData` |
| `/Script/Sparta.SpartaAIController` | `HandlePreAggro` | `in_Payload: StructProperty of /Script/Sparta.SpartaEngagementData` |
| `/Script/Sparta.SpartaAIController` | `HandleDeaggro` | `in_Payload: StructProperty of /Script/Sparta.SpartaEngagementData` |
| `/Script/Sparta.SpartaAICharacter` | `OnAggro` | `in_Payload: StructProperty of /Script/Sparta.SpartaEngagementData` |
| `/Script/Sparta.SpartaAICharacter` | `OnPreAggro` | `in_Payload: StructProperty of /Script/Sparta.SpartaEngagementData` |
| `/Script/Sparta.SpartaAICharacter` | `OnDeaggro` | `in_Payload: StructProperty of /Script/Sparta.SpartaEngagementData` |
| `/Script/Sparta.SpartaAICharacter` | `OnDeath` | `in_OwningActor: ObjectProperty of /Script/Engine.Actor` |
| `/Script/Sparta.SpartaAIController` | `OnPawnDeath` | `OwningActor: ObjectProperty of /Script/Engine.Actor` |
| `/Script/Sparta.SpartaAIController` | `OnPlayerDeath` | `DamagingWeapon: ObjectProperty of /Script/Sparta.SpartaWeapon ; Damage: FloatProperty ; EventData: StructProperty of /Script/Sparta.SpartaCharEventData` |
| `/Script/Sparta.SpartaEncounterManager` | `OnHordeEncounterCompleted` | `CompletedHorde: ObjectProperty of /Script/Engine.Actor` |
| `/Script/Sparta.SpartaEncounterManager` | `OnEnemyDeath` | `DeadActor: ObjectProperty of /Script/Engine.Actor` |
| `/Script/Sparta.SpartaEncounterManager` | `EncounterStartDelegate__DelegateSignature` | — |
| `/Script/Sparta.SpartaEncounterManager` | `OnEncounterCompletedDelegate__DelegateSignature` | — |
| `/Script/Sparta.SpartaHordeEncounter` | `StartEncounter` | `InSpawnTrigger: ObjectProperty of /Script/Sparta.SpawnTrigger` |
| `/Script/Sparta.SpartaHordeEncounter` | `OnEnemyDeath` | `ThatsOneDeadCharacter: ObjectProperty of /Script/Engine.Actor` |
| `/Script/Sparta.SpartaLevelScript` | `OnEncounterStarted` | `Context: StructProperty of /Script/Sparta.FLevelScriptContext` |
| `/Script/Sparta.SpartaLevelScript` | `OnEncounterFinished` | — |

Boss phase abilities found by name (`Phase`): `GA_Boss_Stun_PhaseChange_C` (shared base),
`GA_DwarfBoss_PhaseChange_C`, `GA_FinalBoss_StunPhaseChange_C`,
`GA_Offspring_ShortStunPhaseChange_C`, `GA_Offspring_StunPhaseChange_FakeDeath_C`,
`GA_ParasiteGolem_Stun_PhaseChange_C`, `GA_Swordman_Stun_PhaseChange_C`, `GA_PlayPhaseChange_C`
(TarGolem), `GA_MothKnight_ChangePhaseNoRiposte_C`, `GA_ShepherdSheep_PhaseChange_Attack_C`,
`GA_HexaPod_Stun_C`, `GA_Monolith_Stun_C`, `GA_GolemStun_C`, `GA_LadyOfTheWood_Stun_C`,
`GA_HeadBoss_Stun_C`, `GA_MothKnight_Stun_C`.
`BT_Vampire_Knight_Phase_2` and `BT_Hexapod_Memories` are the only BT assets whose *names* imply a
phase variant.

## 3.4 Boss health-bar widgets

| Widget class | Parent |
|---|---|
| `/Game/Sparta/UI/Enemy/WBP_Enemy_Boss_Health.WBP_Enemy_Boss_Health_C` | `/Script/UMG.UserWidget` |
| `/Game/Sparta/UI/Enemy/WBP_Enemy_Boss_Health_Container.WBP_Enemy_Boss_Health_Container_C` | `/Script/UMG.UserWidget` |
| `/Game/Sparta/UI/Enemy/WBP_Enemy_GroupBoss_Health.WBP_Enemy_GroupBoss_Health_C` | `WBP_Enemy_Boss_Health_C` |
| `/Game/Sparta/UI/Menu/WBP_BossDefeatedMessage.WBP_BossDefeatedMessage_C` | `/Script/UMG.UserWidget` |
| `/Game/Sparta/UI/World/Map/Widgets/WBP_WMI_Boss.WBP_WMI_Boss_C` | `WBP_WorldMapIconBase_C` |
| `/Game/Sparta/FX/Blueprints/WBP_ScreenCoverBossSiphon.WBP_ScreenCoverBossSiphon_C` | `/Script/UMG.UserWidget` |

Functions (compiler temporaries filtered):

| Class | Function | Parameters (name: type) |
|---|---|---|
| `WBP_Enemy_Boss_Health_C` | `BindToOnDamageTaken` | — |
| `WBP_Enemy_Boss_Health_C` | `CacheOwnerAICharacter` | — |
| `WBP_Enemy_Boss_Health_C` | `GetBossName` | `ReturnValue: TextProperty` |
| `WBP_Enemy_Boss_Health_C` | `InitBossName` | — |
| `WBP_Enemy_Boss_Health_C` | `ResetLerp` | `InPercent: FloatProperty` |
| `WBP_Enemy_Boss_Health_C` | `ResetOpacity` | — |
| `WBP_Enemy_Boss_Health_C` | `Tick` | `MyGeometry: StructProperty of SlateCore.Geometry ; InDeltaTime: FloatProperty` |
| `WBP_Enemy_Boss_Health_C` | `UpdateLerp` | `LerpInterpSpeed: DoubleProperty ; TargetRatio: DoubleProperty` |
| `WBP_Enemy_Boss_Health_C` | `UpdateOpacity` | `MyCondition: BoolProperty` |
| `WBP_Enemy_Boss_Health_C` | `UpdatePreview` | `Ratio: FloatProperty ; Percentage: DoubleProperty ; BarTotalLength: FloatProperty ; ProcessedPadding: DoubleProperty` |
| `WBP_Enemy_Boss_Health_Container_C` | `AddBossHealth` | (temp `K2Node_DynamicCast_bSuccess: BoolProperty`) |
| `WBP_Enemy_Boss_Health_Container_C` | `AddGroupBossHealth` | (temp `K2Node_DynamicCast_bSuccess: BoolProperty`) |
| `WBP_Enemy_Boss_Health_Container_C` | `AddTimedEncounter` | (temp `K2Node_DynamicCast_bSuccess: BoolProperty`) |
| `WBP_Enemy_Boss_Health_Container_C` | `RemoveBossHealth` | — |
| `WBP_Enemy_Boss_Health_Container_C` | `ForceRemoveBossHealth` | `Success: BoolProperty` |

`UpdateHealthPercent` also exists on `WBP_Enemy_Boss_Health_C` (evidence: its
`CallFunc_HasMatchingGameplayTag_ReturnValue` / `CallFunc_IsValid_ReturnValue` temp properties),
but its own parameter list is empty in the dump — `UNKNOWN`: no non-temporary property is owned by
that function.

## 3.5 Reward / drop on death

| Source | Field | Type | Offset |
|---|---|---|---|
| `BPC_BossAI_C` | `VestigeItem` | ClassProperty | `0x238` |
| `BPC_BossAI_C` | `VestigeEffects` | ArrayProperty of ClassProperty | `0x240` |
| `BPC_BossAI_C` | `ItemStacks` | IntProperty | `0x250` |
| `BP_AICharacter_C` | `UseCharacterDataRewards` | BoolProperty | `0xda0` |
| `BP_AICharacter_C` | `CharacterDeathData` | StructProperty of `/Script/Sparta.SpartaCharacterDeathData` | `0xda8` |
| `BP_AICharacter_C` | `AdditionalDropItems` | MapProperty | `0xe08` |
| `BP_AICharacter_C` | `SetDeathResources` | fn, `CharacterDeathData: StructProperty of SpartaCharacterDeathData` | — |
| `BP_AICharacter_C` | `HealthValueBeforeDmgRecieved` | FloatProperty | `0xc70` |

`FSpartaCharacterDeathData` (`Sparta.hpp`): `GloomOnDeath int32 @0x0000`,
`GoldOnDeath int32 @0x0004`, `GoldDropChance float @0x0008`,
`ItemDropTable TMap<TSubclassOf<USpartaItemDefinition>,float> @0x0010` — total `Size: 0x60`.

Other drop structures: `FSpartaCurrencyDropTable` (`CurrencyType @0x0000`, `base @0x0008`,
`Min @0x000C`, `Max @0x0010`, `DropChance @0x0014`, `Size: 0x18`),
`FPickupTemplate`, `FSpartaItem` (`FTableRowBase`, `ID @0x0008`, `ItemClass @0x0010`,
`DisplayName @0x0038`, `Description @0x0048`, `Size: 0x58`).

No `SpawnPickup` function exists on any class whose path contains `Pickup` + `Spawn`; searched
`SELECT path FROM cls WHERE path LIKE '/Script/Sparta.%Pickup%'`.

---

# 4. Aggro / perception / targeting

## 4.1 Engagement data and state

`FSpartaEngagementData` is a **one-field struct** — `Character: ASpartaCharacter* @0x0000`,
`Size: 0x8`. Every aggro/deaggro payload carries only the engaging character.

`ESpartaAIEngagementState` (`Sparta_enums.hpp`): `Unset=0, PreAggro=1, Aggro=2, DeAggro=3`.

`ASpartaAIController` holds `ESpartaAIEngagementState EngagementState; // 0x03F0` and three
delegates. `ASpartaCharacter` separately exposes `CharacterAggro` (`0x950`).

## 4.2 Aggro / target / perception functions — full parameter lists

| Class | Function | Parameters (dump order) |
|---|---|---|
| `ASpartaAICharacter` | `OnPreAggro` | `in_Payload: StructProperty of SpartaEngagementData` |
| `ASpartaAICharacter` | `OnAggro` | `in_Payload: StructProperty of SpartaEngagementData` |
| `ASpartaAICharacter` | `OnDeaggro` | `in_Payload: StructProperty of SpartaEngagementData` |
| `ASpartaAIController` | `HandlePreAggro` | `in_Payload: StructProperty of SpartaEngagementData` |
| `ASpartaAIController` | `HandleAggro` | `in_Payload: StructProperty of SpartaEngagementData` |
| `ASpartaAIController` | `HandleDeaggro` | `in_Payload: StructProperty of SpartaEngagementData` |
| `ASpartaAIController` | `ChangeSightRadius` | `NewRadius: FloatProperty` |
| `ASpartaAIController` | `SetDynamicSubtree` | `InjectTag: StructProperty of GameplayTag ; BehaviorAsset: ObjectProperty of /Script/AIModule.BehaviorTree` |
| `ASpartaAIController` | `GetDynamicSubtree` | `InjectTag: StructProperty of GameplayTag ; OutBehaviorTree: ObjectProperty of BehaviorTree ; ReturnValue: BoolProperty` |
| `ASpartaAIController` | `DisableAvoidance` | — |
| `ASpartaCharacter` | `SetCurrentTarget` | `NewTarget: ObjectProperty of /Script/Engine.Actor` |
| `ASpartaCharacter` | `GetCurrentTarget` | `ReturnValue: ObjectProperty of /Script/Engine.Actor` |
| `ASpartaCharacter` | `SetTeam` | `NewTeam: StructProperty of GameplayTag` |
| `ASpartaCharacter` | `GetTeamRelationship` | `Other: ObjectProperty of /Script/Engine.Actor` |
| `ASpartaCharacter` | `OnTargetedBy` | `Targeter: ObjectProperty of /Script/Sparta.SpartaCharacter` |
| `ASpartaCharacter` | `OnTeamChanged` | `Owner: ObjectProperty of SpartaCharacter ; OldTeam: StructProperty of GameplayTag ; NewTeam: StructProperty of GameplayTag` |
| `USpartaEnemySubsystem` | `OnPlayerTargetedBy` | `Targeter: ObjectProperty of /Script/Sparta.SpartaCharacter` |
| `USpartaEnemySubsystem` | `OnEngagedEnemyTargetChanged` | `Source: ObjectProperty of SpartaCharacter ; OldTarget: ObjectProperty of Actor ; NewTarget: ObjectProperty of Actor` |
| `USpartaEnemySubsystem` | `OnEngagedEnemyDeaggro` | `in_Payload: StructProperty of SpartaEngagementData` |
| `USpartaEnemySubsystem` | `GetSpawnedEnemiesInDist` | `Center: StructProperty of CoreUObject.Vector ; Dist: FloatProperty ; ReturnValue: ArrayProperty` |
| `UTargetSelectionComponent` | `SetTargetOverride` | `NewTarget: ObjectProperty of Actor ; OverrideTime: FloatProperty` |
| `UTargetSelectionComponent` | `RemoveTargetOverride` | `Handle: StructProperty of /Script/Sparta.FTargetOverrideHandle` |
| `UTargetSelectionComponent` | `RecalculateTarget` | `bForce: BoolProperty ; BlockingTagsToIgnore: StructProperty of GameplayTagContainer` |
| `UTargetSelectionComponent` | `OnActorPerceptionUpdated` | `Actor: ObjectProperty of Actor ; Stimulus: StructProperty of /Script/AIModule.AIStimulus` |
| `UTargetSelectionComponent` | `OnTargetDeath` | `OwningActor: ObjectProperty of Actor` |
| `UTargetSelectionComponent` | `OnOwnerPossess` | `OldPawn: ObjectProperty of Pawn ; NewPawn: ObjectProperty of Pawn` |
| `UTargetSelectionComponent` | `OnTeamChanged` | `Owner: ObjectProperty of SpartaCharacter ; OldTeam: StructProperty of GameplayTag ; NewTeam: StructProperty of GameplayTag` |
| `UTargetSelectionComponent` | `OnTargetTeamChanged` | `Character: ObjectProperty of SpartaCharacter ; OldTeam ; NewTeam: StructProperty of GameplayTag` |
| `UTargetSelectionComponent` | `AddTagModifier` | `Tag: StructProperty of GameplayTag ; ModifierType: EnumProperty of ETargetPrioModifierType ; Value: FloatProperty` |
| `UTargetSelectionComponent` | `RemoveTagModifier` | `Tag: StructProperty of GameplayTag` |
| `UTargetSelectionComponent` | `GetTagModifier` | `Tag: StructProperty of GameplayTag ; OutModifier: StructProperty of /Script/Sparta.FTargetPriorityModifier` |
| `UTargetSelectionComponent` | `ClearTagModifiers` | — |
| `UTargetSelectionComponent` | `ClearCurrentTarget` | — |
| `UTargetSelectionComponent` | `BlockSwitching` | `bBlock: BoolProperty` |
| `UTargetSelectionComponent` | `ForgetAll` | — |
| `UTargetSelectionComponent` | `FindTargetWithPath` | — |
| `UTargetSelectionComponent` | `OnTargetSwitched__DelegateSignature` | `Owner: ObjectProperty of SpartaCharacter ; OldTarget: ObjectProperty of Actor ; NewTarget: ObjectProperty of Actor` |
| `USpartaTargetLockComponent` | `ToggleTargetLock` | `TargetingTag: StructProperty of GameplayTag` |
| `USpartaTargetLockComponent` | `TargetSpecificCharacter` | `Target: ObjectProperty of /Script/Engine.Actor` |
| `USpartaTargetLockComponent` | `SwitchTarget` | `TargetBehavior: EnumProperty of ESpartaSwitchTargetBehavior ; bClearTargetIfFailed: BoolProperty ; TargetingTag: StructProperty of GameplayTag` |
| `USpartaTargetLockComponent` | `SwitchTargetWithInputDirection` | `Input: StructProperty of CoreUObject.Vector2D ; bClearTargetIfFailed: BoolProperty ; TargetingTag: StructProperty of GameplayTag` |
| `USpartaTargetLockComponent` | `TransformVectorToTargetLockSpace` | `WorldSpaceVector: StructProperty of CoreUObject.Vector` |
| `USpartaTargetLockComponent` | `ShouldMoveTowardsTarget` | — |
| `USpartaTargetLockComponent` | `LockedTargetChanged` | `NewTarget: ObjectProperty of /Script/Engine.Actor` |
| `USpartaTargetLockComponent` | `OnLockedTargetChangedDelegate` | `NewTarget: ObjectProperty of /Script/Engine.Actor` |
| `USpartaTargeter` | `TryGetSingleTarget` | — |
| `USpartaTargeter` | `IsValidTarget` | `PotentialTarget: ObjectProperty of /Script/Engine.Actor` |
| `USpartaTargeter` | `DoInstantTargeting` | `TargetingTag: StructProperty of GameplayTag` |
| `USpartaTargeter` | `ApplyTargetingOnce` | `TargetingTag: StructProperty of GameplayTag` |
| `USpartaTargeter` | `BeginContinuousTargeting` | — |
| `USpartaTargeter` | `EndContinuousTargeting` | — |
| `USpartaTargeter` | `ConsiderTarget` | `PotentialTarget: ObjectProperty of Actor ; HitResult: StructProperty of Engine.HitResult` |
| `USpartaTargeter` | `SpartaTargeterNewTargetDelegate__DelegateSignature` | `HitCharacter: ObjectProperty of Actor ; HitResult: StructProperty of Engine.HitResult` |
| `USpartaTargetableComponent` | `GetIsOnScreen` | `QueryType: EnumProperty of EOnScreenQuery` |
| `UAIDirectorSubsystem` | `GetAIDirector` | `WorldContextObject: ObjectProperty of /Script/Engine.Object` |
| `UAIDirectorSubsystem` | `IsAbilityOnGlobalCooldown` | `Ability: ObjectProperty of /Script/GameplayAbilities.GameplayAbility` |
| `UAIDirectorSubsystem` | `EnableGlobalCooldown` | `Ability: ObjectProperty of GameplayAbility ; CooldownTime: FloatProperty` |
| `USoftTargetingComponent` | `UpdateTargetingVector` | — |
| `USoftTargetingComponent` | `UpdateMovementDirection` | — |
| `USoftTargetingComponent` | `SphereOverlapComplete` | `OutHits: ArrayProperty ; bResult: BoolProperty` |
| `USoftTargetingComponent` | `RunScoring` | `Actors: ArrayProperty of Actor ; SelectedActor: ObjectProperty of Actor` |
| `USoftTargetingComponent` | `RunMultiSphereTrace` | `TargetingDirection: StructProperty of Vector ; Radius: FloatProperty ; ValidActors: ArrayProperty` |
| `USoftTargetingComponent` | `GetScoreBonusAndMulitplier` | `Actor: ObjectProperty of Actor ; ScoreBonus: FloatProperty ; ScoreMult: FloatProperty` |
| `USoftTargetingComponent` | `GetCameraForwardVector` | — |

`USpartaTargeter` and `USpartaTargetLockComponent` both declare a delegate
(`OnNewTarget @0x28`, `OnLockedTargetChanged @0xB8`); `UTargetSelectionComponent` declares
`OnTargetSwitched @0xB8`.

## 4.3 Perception setup (facts from controller CDOs)

Every AI controller CDO under `Enemies/` + `Bosses/` carries an `AIPerceptionComponent` with
`AISenseConfig_Sight`, `AISenseConfig_Damage` and (in most cases) `AISenseConfig_Team` subobjects
(`eb-12-senses.sql`, 228 rows). Exceptions where a sense is absent:

* `AISenseConfig_Team` **missing** on: `BP_GrishaController`, `BP_Shepherd_Controller`,
  `BP_Sheep_Controller`, `BP_HexapodController`, `BP_LadyOfTheWoodsController`,
  `BP_TarGolemController`, `BP_TarGolemParasiteController`, `BP_BigBottleController`,
  `BP_HeadBossController`, `BP_NPCController`, `BP_HordeAIController`.
* `AISenseConfig_Damage` **missing** on: `BP_Shepherd_Controller`, `BP_Sheep_Controller`,
  `BP_HexapodController`, `BP_LadyOfTheWoodsController`, `BP_TarGolemController`,
  `BP_TarGolemParasiteController`, `BP_BigBottleController`, `BP_GrishaController`,
  `BP_NPCController`, `BP_HordeAIController`.

`USpartaAIPerceptionSystem` adds only
`TArray<TSubclassOf<UAISense>> DefaultSenses; // 0x0138 (size: 0x10)`, `Size: 0x148`.

Other AIPerception-relevant data: `BTD_LineOfSight_C`, `BTD_PlayerInSpawnTrigger_C`,
`BTD_TargetAccessible_C`, `BTD_CanUseTraversal_C`, `BTD_IsAIDisabled_C`,
`BTT_FindTargetWithPath_C` in `/Game/Sparta/Core/AI/Decorator/` + `/Task/`.

---

# 5. Spawn / respawn / encounter

## 5.1 Native spawn classes — functions with full parameter lists

| Class | Function | Parameters (dump order) |
|---|---|---|
| `ASpawnTrigger` | `StartEncounter` | — |
| `ASpawnTrigger` | `StartSpawning` | — |
| `ASpawnTrigger` | `EndEncounter` | — |
| `ASpawnTrigger` | `ValidateSpawners` | `ReturnValue: BoolProperty` |
| `ASpawnTrigger` | `SpawnTriggerLoaded` | — |
| `ASpawnTrigger` | `ShouldLoadEnemyClassesOnBeginPlay` | `ReturnValue: BoolProperty` |
| `ASpawnTrigger` | `SaveEncounterState` | `EventData: ObjectProperty of /Script/Engine.Object` |
| `ASpawnTrigger` | `OnDifficultyChanged` | `BiomeTag: StructProperty of GameplayTag ; OldDifficulty: IntProperty ; NewDifficulty: IntProperty` |
| `ASpawnTrigger` | `LoadEnemyClasses` | — |
| `ASpawnTrigger` | `ClearLoadedClasses` | — |
| `ASpawnTrigger` | `AssetsLoaded` | — |
| `AUnitSpawner` | `SpawnMe` | `SpawnTrigger: ObjectProperty of /Script/Sparta.SpawnTrigger` |
| `AUnitSpawner` | `SpawnerLoaded` | — |
| `AUnitSpawner` | `LoadClasses` | — |
| `AUnitSpawner` | `ClearLoadedClasses` | — |
| `AUnitSpawner` | `CharacterSpawned` | — |
| `AUnitSpawner` | `CanSpawn` | `ReturnValue: BoolProperty` |
| `AUnitSpawner` | `BeginSpawningEnemy` | `EnemyClass: ClassProperty of SpartaCharacter ; Transform: StructProperty of CoreUObject.Transform ; ReturnValue: ObjectProperty of Actor` |
| `AUnitSpawner` | `FinishSpawningEnemy` | `EnemyClass: ClassProperty of SpartaCharacter ; Transform: StructProperty of CoreUObject.Transform ; ReturnValue: ObjectProperty of Actor` |
| `ASpartaHordeSpawner` | `Spawn` | `ReturnValue: ObjectProperty of Actor` |
| `UHordeSpawnerComponent` | `StartEncounter` | `InSpawnTrigger: ObjectProperty of /Script/Sparta.SpawnTrigger` |
| `UHordeSpawnerComponent` | `SetEnemyTarget` | `NewEnemy: ObjectProperty of /Script/Engine.Character` |
| `UHordeSpawnerComponent` | `OnEnemyDeath` | `DeadCharacter: ObjectProperty of Actor` |
| `UHordeSpawnerComponent` | `CharacterSpawned` | `SpawnedCharacter: ObjectProperty of SpartaCharacter` |
| `UHordeSpawnerComponent` | `OnHordeEncounterCompletedDelegate__DelegateSignature` | `CompletedHorde: ObjectProperty of HordeSpawnerComponent` |
| `UHordeUnitComponent` | `OnDeath` | `DeadActor: ObjectProperty of Actor` |
| `AHordeClusterCenter` | `OnUnitDeath` | `DeadActor: ObjectProperty of Actor` |
| `ASpartaHordeEncounter` | `SetEnemyTarget` | `NewEnemy: ObjectProperty of /Script/Engine.Character` |
| `ASpartaEncounterManager` | `OnHordeEncounterCompleted` | `CompletedHorde: ObjectProperty of Actor` |
| `ASpartaEncounterManager` | `OnEnemyDeath` | `DeadActor: ObjectProperty of Actor` |
| `USpartaMassActorSpawnerSubsystem` | `ReleaseActorToPool` | `Actor: ObjectProperty of Actor ; ReturnValue: BoolProperty` |
| `ASpartaAICharacter` | `SpawnFromSpawnerFinished` | — |
| `ASpartaAICharacter` | `IsSpawnFinished` | `ReturnValue: BoolProperty` |
| `ASpartaAICharacter` | `BeginPlay` | — |

There is **no** native function whose name contains `Respawn` or `Rest` in the spawn family
(`SpartaAICharacter`, `SpartaAIController`, `SpawnTrigger`, `UnitSpawner`, `Horde*`,
`SpartaEncounter*`): verified with
`SELECT path FROM obj WHERE type='Function' AND (path LIKE '%:Respawn%' OR path LIKE '%:Rest%') AND path LIKE '/Script/Sparta.%'`
→ 0 rows in these classes. Respawn-on-rest is therefore `GUESS:` implemented by the gameplay
layer re-running `SpawnTrigger.StartEncounter` / `UnitSpawner.SpawnMe` after a world/session reset;
`ISpartaSaveInterface.SessionReset()` exists on `/Script/Sparta.ISpartaSaveInterface` and is the
only native "reset" entry point, but no code path connecting it to enemy respawn is visible in the
dump.

## 5.2 Spawn-relevant properties (offsets)

`ASpawnTrigger`: `HordeSpawner UHordeSpawnerComponent* @0x02A8`,
`LevelScripts TArray<ASpartaLevelScript*> @0x02B0`, `ChosenRecipe USpartaEncounterRecipe* @0x02C0`,
`SpawnedEnemies TArray<ASpartaCharacter*> @0x02C8`, `Spawners TArray<AUnitSpawner*> @0x02D8`,
`bUseDefaultEncounterFirst @0x02E8`, `bKeepDefaultEncounterInRotation @0x02E9`,
`ForceRecipeIndex int32 @0x02EC`, `FirstEncounterTags @0x02F0`,
`EncounterRecipes TArray<FSpartaEnemyRecipe> @0x0310`, `TriggerTags @0x0320`,
`OnSpawnTriggerLoaded @0x0340`, `ClassReferences TArray<UObject*> @0x0350`,
`bEncounterStarted @0x0361`. `Size: 0x378`.

`AUnitSpawner`: `SaveComponent @0x02A8`, `bShouldLoadEnemyClassesOnBeginPlay @0x02B0`,
`OnCharacterSpawned @0x02B8`, `EnemySpawnType FDataTableRowHandle @0x02C8`,
`SpawnerTags @0x02D8`, `LooseGameplayTagsToAdd @0x02F8`, `SpawnedEnemy @0x0318`,
`CharacterId FGameplayTag @0x0320`, `ClassReference UObject* @0x0328`. `Size: 0x340`.
`ASpartaHordeSpawner` adds `SpawnClass TSoftClassPtr<ACharacter> @0x0340`, `Size: 0x378`.

`UHordeSpawnerComponent`: `Spawners @0x00B8`, `MinSpawnInterval @0x00C8`,
`MaxSpawnInterval @0x00CC`, `MaxActiveEnemies @0x00D8`, `TotalSpawnCount @0x00DC`,
`ActiveSpawnCount @0x00E0`, `OnHordeCompleted @0x00E8`, `bSetTargetToPlayer @0x00F8`,
`bSetPlayedAggroOnSpawn @0x00F9`, `SpawnTrigger @0x0100`. `Size: 0x110`.

`ASpartaHordeEncounter`: `Spawners @0x02A8`, `SpawnInterval @0x02B8`, `MaxActiveEnemies @0x02C8`,
`TotalSpawnCount @0x02CC`, `ActiveSpawnCount @0x02D0`,
`TraversalsToUnlockOnComplete @0x02D8`, `OnHordeCompleted @0x02E8`,
`bSetTargetToPlayer @0x02F8`, `bSetPlayedAggroOnSpawn @0x02F9`, `SpawnTrigger @0x0300`.
`Size: 0x308`.

`ASpartaEncounterManager`: `Billboard @0x02A8`, `TextRender @0x02B0`,
`AssociatedEnemies TArray<ASpartaCharacter*> @0x02B8`,
`HordeEncounters TArray<ASpartaHordeEncounter*> @0x02C8`,
`SpawnedEncounters TArray<FSpawnedEncounterParams> @0x02D8`, `bStartSpawnedOnAllDead @0x02E8`,
`OnEncounterStart @0x02F0`, `EncounterStartDistance float @0x0300`,
`OnEncounterCompleted @0x0308`. `Size: 0x340`.

`USpartaEncounterRecipe` (`UDataAsset`): `Name FName @0x0030`,
`EnemySpawns TArray<FSpartaEnemySpawnRecipe> @0x0038`,
`HordeSpawnSettings FSpartaHordeSpawnRecipe @0x0048`, `Requirements @0x0070`,
`RecipeTags @0x0138`. `Size: 0x158`.

`UHordeCharacterData`: `EQSInnerRingRadius @0x0028`, `EQSOuterRingRadius @0x002C`,
`EQSRings @0x0030`, `KeepFormationThreshold @0x0034`, `EQSMoveThreshold @0x0038`. `Size: 0x40`.

`USpartaEnemySubsystem`: `OnScreenCheckMethod @0x0040`, `PointCheckRadius @0x0044`,
`EngagedEnemies TSet<ASpartaCharacter*> @0x0048`, `OnSpawnEffects @0x0098`,
`OnSpawnAbilities @0x00A8`, `EnviromentalInstigatorClass @0x00B8`,
`TargetableComponents TArray<USpartaTargetableComponent*> @0x00C0`,
`OnCharacterDeathEvent @0x00D8`, `SpawnedEnemies TArray<ASpartaAICharacter*> @0x00E8`,
`AggressorCount TMap<AActor*,int32> @0x0100`, `EnviromentalInstigator @0x0150`,
`OnCharacterEngageStateChanged_BP @0x0158`, `AggressorRanking TMap<ASpartaCharacter*,float> @0x0370`,
`TopAggressor ASpartaCharacter* @0x03C0`, `EnemyHealthWidgets FUserWidgetPool @0x03D0`.
`Size: 0x460`.

## 5.3 Blueprint spawn/respawn hooks on enemies

| Class | Function | Parameters |
|---|---|---|
| `BP_AICharacter_C` | `DespawnEnemy` | (temps only) |
| `BP_AICharacter_C` | `ResetSpawnTrigger` | `Return: BoolProperty` |
| `BP_AICharacter_C` | `SpawnControllerDeferred` | — |
| `BP_AICharacter_C` | `IsSpawnFinished` | `ReturnValue: BoolProperty` |
| `BP_AICharacter_C` | `PostSpawnMovement` | (temps only) |
| `BP_AICharacter_C` | `ShowWeaponPostSpawn` | — |
| `BP_AICharacter_C` | `BroadcastNearbyEvent` | `EventEffect: ClassProperty ; Distance: DoubleProperty ; duration: DoubleProperty` |
| `BP_AICharacter_C` | `ShouldReactToAllyDeath` | `DyingCharacter: ObjectProperty of Actor ; ReturnValue: BoolProperty` |

Spawn-related `BP_AICharacter` properties: `SpawnLimitsVolume` (`BP_SpawnLimitsVolume_C`) `0xcb0`,
`SpawnTrigger` (`BP_SpawnTrigger_C`) `0xcd0`, `spawner` (`BP_Spawner_C`) `0xd30`,
`PlacedOnLevel BoolProperty 0xca8`, `OnSpawnedTransform Transform 0xc10`,
`JumpSpawnMontage SoftObjectProperty 0xc78`, `SpawnAdjust DoubleProperty 0xca0`.

Related BP classes: `/Game/Sparta/Characters/SpawnSystem/BP_Spawner.BP_Spawner_C`,
`BP_SpawnTrigger.BP_SpawnTrigger_C`, `BP_Spawner_Proximity.BP_Spawner_Proximity_C`,
`BP_SpawnLimitsVolume.BP_SpawnLimitsVolume_C` (referenced from `BP_AICharacter`).

Spawn abilities: `GA_Enemy_SpawnHidden`, `GA_Draugr_SpawnHidden`, `GA_SpawnJumpDown`,
`GA_BabyEnemy_GroundSpawn`, `GA_TarredCorpse_SpawnHidden_Ground`, `GA_Vampire_SkySpawn`,
`GA_Enemy_MoveRootToSpawn`, `GA_AllEnemies_Retreat`, `GA_AllEnemies_RetreatAndDespawn`,
`GA_AllEnemies_Despawn`.

---

# 6. THE DAMAGE PATH

## 6.0 The chain, top to bottom

1. `USpartaHitTraceComponent` (`ASpartaCharacter.HitTraceComponent @0x6C0`) runs
   `USpartaHitTrace::CheckOnce` / `BeginContinuousCheck` against `FSpartaHitTraceShapeInfo`
   shapes and fires `OnTraceHit` (`USpartaHitTrace*, const FHitResult&`).
   Deferred dispatch goes through `USpartaHitTraceSubsystem.PendingHits`.
2. The hit is applied to the target: `ISpartaHittable::SpartaApplyHit(HitResult, Instigator,
   Payload, DamageCauser)`.
3. The payload `USpartaHitPayload` carries health/poise/break damage, effects and tags; the
   gameplay-effect execution path runs `USpartaDamageExecution` / `USpartaBreakExecution` /
   `USpartaPoiseExecution` / `USpartaHealExecution` / `USpartaWindedExecution` with the
   `*ExecutionModifier` hooks.
4. `USpartaHealthComponent` applies the result to `USpartaHealthSet` and broadcasts
   `OnDamageReceived` / `OnDeathStarted` / `OnDeathFinished`.
5. `ASpartaCharacter` broadcasts `OnHitReceived`, `CharacterHit`, `CharacterDeath`;
   `USpartaEnemySubsystem` broadcasts `OnCharacterDeathEvent`;
   `ASpartaAICharacter` `OnDeath`; `BPC_BossAI` `OnBossDeath`.

## 6.1 (a) Functions a caller can call to apply a hit

### Interfaces

| Interface | Function | Parameters (dump order) | Return |
|---|---|---|---|
| `/Script/Sparta.SpartaHittable` (`ISpartaHittable`) | `SpartaApplyHit` | `HitResult: StructProperty of /Script/Engine.HitResult` ; `Instigator: ObjectProperty of /Script/Sparta.SpartaCharacter` ; `Payload: ObjectProperty of /Script/Sparta.SpartaHitPayload` ; `DamageCauser: ObjectProperty of /Script/Engine.Actor` | none |
| `/Script/Sparta.SpartaHittable` | `GetHitResponse` | `Source: ObjectProperty of /Script/Engine.Actor` ; `Hit: StructProperty of /Script/Engine.HitResult` | `ReturnValue: ByteProperty` (enum) |
| `/Script/Sparta.SpartaHitApplier` (`ISpartaHitApplier`) | `SpartaHitApplied` | `HitResult: StructProperty of /Script/Engine.HitResult` ; `Instigator: ObjectProperty of SpartaCharacter` ; `Payload: ObjectProperty of SpartaHitPayload` ; `DamageCauser: ObjectProperty of Actor` | none |
| `/Script/Sparta.SpartaDamageCauser` (`ISpartaDamageCauser`) | `GetDamageTags` | — | `ReturnValue: StructProperty of GameplayTagContainer` |

### `USpartaHitPayload` — the payload object (19 setters + 6 operations)

| Function | Parameters |
|---|---|
| `SetHealthDamage` | `InHealthDamage: StructProperty of /Script/Sparta.FHealthDamageOption` |
| `SetDamageEffect` | `InDamageEffect: ClassProperty of /Script/GameplayAbilities.GameplayEffect` |
| `SetDamageDataTag` | `InDamageDataTag: StructProperty of GameplayTag` |
| `SetDamageDealt` | `InDamageDealt: FloatProperty` |
| `SetPoiseDamage` | `InPoiseDamage: FloatProperty` |
| `SetPoiseDamageOption` | `InPoiseDamage: StructProperty of FHealthDamageOption` |
| `SetPoiseDamageEffect` | `InPoiseDamageEffect: ClassProperty of GameplayEffect` |
| `SetPoiseDamageDataTag` | `InPoiseDamageDataTag: StructProperty of GameplayTag` |
| `SetPoiseBrokenEffect` | `InPoiseBrokenEffect: ClassProperty of GameplayEffect` |
| `SetBreakDamage` | `InBreakDamage: FloatProperty` |
| `SetBreakDamageOption` | `InBreakDamage: StructProperty of FHealthDamageOption` |
| `SetBreakDamageEffect` | `InBreakDamageEffect: ClassProperty of GameplayEffect` |
| `SetBreakDamageDataTag` | `InBreakDamageDataTag: StructProperty of GameplayTag` |
| `SetReactionTag` | `InReactionTag: StructProperty of GameplayTag` |
| `SetChosenReactionTag` | `InChosenReactionTag: StructProperty of GameplayTag` |
| `SetStrikeDirection` | `InStrikeDirection: EnumProperty of /Script/Sparta.ESpartaStrikeDirection` |
| `SetPayloadTags` | `InPayloadTags: StructProperty of GameplayTagContainer` |
| `SetCustomElementalStacks` | `InCustomElementalStacks: IntProperty` |
| `SetAdditionalEffects` | `InAdditionalEffects: ArrayProperty of /Script/Sparta.FAdditionalEffect` |
| `ApplyPayload` | `Target: ObjectProperty of Actor` ; `ApplyingAbility: ObjectProperty of /Script/Sparta.SpartaGameplayAbility` ; `HitResult: StructProperty of HitResult` ; `DamageCauser: ObjectProperty of Actor` |
| `ApplyPayloadInstigator` | `Target: ObjectProperty of Actor` ; `InstigatingASC: ObjectProperty of /Script/GameplayAbilities.AbilitySystemComponent` ; `HitResult: StructProperty of HitResult` ; `DamageCauser: ObjectProperty of Actor` ; `EffectContext: StructProperty of /Script/GameplayAbilities.GameplayEffectContextHandle` |
| `CreateGameplayEffectContextHandle` | `SourceASC: ObjectProperty of AbilitySystemComponent` ; `DamageCauser: ObjectProperty of Actor` ; `HitResult: StructProperty of HitResult` | `ReturnValue: StructProperty of GameplayEffectContextHandle` |
| `CalculateDamage` | `SourceASC: ObjectProperty of AbilitySystemComponent` ; `DamageCauser: ObjectProperty of Actor` | `ReturnValue: FloatProperty` |
| `CalculatePoiseDamage` | `SourceASC: ObjectProperty of AbilitySystemComponent` ; `DamageCauser: ObjectProperty of Actor` | `ReturnValue: FloatProperty` |
| `CalculateBreakDamage` | `SourceASC: ObjectProperty of AbilitySystemComponent` ; `DamageCauser: ObjectProperty of Actor` | `ReturnValue: FloatProperty` |
| `MergePayloads` | `OuterForNewPayload: ObjectProperty of UObject` ; `Payloads: ArrayProperty of /Script/Sparta.SpartaHitPayload` | `ReturnValue: ObjectProperty of SpartaHitPayload` |
| `GetDuplicate` | `Outer: ObjectProperty of UObject` | `ReturnValue: ObjectProperty of SpartaHitPayload` |

`USpartaHitPayload` object size: `// Size: 0x158` (`Sparta.hpp`). The class is a `UObject`, so a
fresh payload must be created with `GetDuplicate(Outer)` / `MergePayloads` rather than by copying a
struct — there is no `FSpartaHitPayload` struct in this build (searched
`grep "^struct .*HitPayload" raw\CXXHeaderDump\Sparta.hpp` → only `FSpartaHitPayloadHolder`).

### `USpartaHitTrace` — creating and driving a trace

| Function | Parameters | Return |
|---|---|---|
| `CreateHitTrace` | `Params: StructProperty of /Script/Sparta.FSpartaHitTraceParams` ; `Source: ObjectProperty of Actor` ; `OptionalWeapon: ObjectProperty of /Script/Sparta.SpartaWeapon` ; `OptionalTraceData: MapProperty (GameplayTag → UObject)` | `ReturnValue: ObjectProperty of SpartaHitTrace` |
| `AddTraceShape` | `TraceShape: ObjectProperty of /Script/Sparta.SpartaHitTraceShape` ; `Component: ObjectProperty of /Script/Engine.SceneComponent` ; `BoneName: NameProperty` | — |
| `SetTraceShapes` | `TraceShapes: ArrayProperty of /Script/Sparta.FSpartaCollisionShapeTrace` | — |
| `SetTracePrimitives` | `Components: ArrayProperty of /Script/Engine.PrimitiveComponent` | — |
| `SetTraceData` | `Key: StructProperty of GameplayTag` ; `Value: ObjectProperty of UObject` | — |
| `SetSource` | `Source: ObjectProperty of Actor` | — |
| `GetTraceData` | `Key: StructProperty of GameplayTag` | `ReturnValue: ObjectProperty of UObject` |
| `GetTraceDataAsType` | `Key: StructProperty of GameplayTag` ; `Class: ClassProperty` | `ReturnValue: ObjectProperty of Object` |
| `GetSource` / `GetSourceCharacter` | — | `ObjectProperty` |
| `GetHitTraceProfileOptions` | — | `ArrayProperty` |
| `CheckOnce` | — | — |
| `BeginContinuousCheck` | — | — |
| `EndContinuousCheck` | — | — |

`USpartaHitTraceComponent`:

| Function | Parameters | Return |
|---|---|---|
| `PerformTrace` | `HitTrace: ObjectProperty of SpartaHitTrace` | `ReturnValue: StructProperty of FSpartaHitTraceHandle` |
| `StartContinuousTrace` | `HitTrace: ObjectProperty of SpartaHitTrace` | `ReturnValue: StructProperty of FSpartaHitTraceHandle` |
| `StartContinuousTraceWithContext` | `Context: ObjectProperty of UObject` ; `HitTrace: ObjectProperty of SpartaHitTrace` | — |
| `StopContinuousTrace` | `Handle: StructProperty of FSpartaHitTraceHandle` | — |
| `EndContinuousTraceWithContext` | `Context: ObjectProperty of UObject` | — |
| `UnbindOnTraceHitFromObject` | `Object: ObjectProperty of UObject` | — |
| `EnableHitProcessing` / `DisableHitProcessing` | — | — |
| `IsHitProcessingEnabled` | — | `ReturnValue: BoolProperty` |

Shape factories: `USpartaHitTraceShape_Capsule::MakeCapsule(Radius:Float, HalfHeight:Float,
Rotation:FRotator, Offset:FVector)`, `…_Sphere::MakeSphere(Radius:Float, Offset:FVector)`,
`…_Line::MakeLine(Offset:FVector)`, `USpartaHitTraceShapes_Box::MakeBox(Extent:FVector,
Rotation:FRotator, Offset:FVector)` — each returns the shape as `ObjectProperty`.

Anim notifies that start traces: `/Script/Sparta.SpartaAnimNotify_HitCheck` and
`/Script/Sparta.SpartaAnimNotifyState_HitCheck`, both exposing only
`GetTraceProfileOptions()` → `ArrayProperty`.

### Execution / modifier hooks

| Class | Function | Parameters | Return |
|---|---|---|---|
| `DamageExecutionModifier` | `ApplyDamageExecutionModifier` | `Damage: Float ; DamageReduction: Float ; IncomingDamageMultiplier: Float ; CriticalChance: Float ; CriticalBonus: Float ; SourceASC: Object ; TargetASC: Object ; Spec: Struct` | `ReturnValue: Bool` |
| `DamageExecutionModifier` | `ApplyPostDamageReductionModifier` | `Damage: Float ; SourceASC: Object ; TargetASC: Object ; Spec: Struct` | `ReturnValue: Bool` |
| `BreakExecutionModifier` | `ApplyBreakExecutionModifier` | `Damage: Float ; DamageReduction: Float ; SourceASC: Object ; TargetASC: Object ; Spec: Struct` | `ReturnValue: Bool` |
| `PoiseExecutionModifier` | `ApplyPoiseExecutionModifier` | `Damage: Float ; SourceASC: Object ; TargetASC: Object ; Spec: Struct` | `ReturnValue: Bool` |
| `HealExecutionModifier` | `ApplyHealExecutionModifier` | `Healing: Float ; SourceASC: Object ; TargetASC: Object ; Spec: Struct` | `ReturnValue: Bool` |
| `WindedExecutionModifier` | `ApplyWindedExecutionModifier` | `WindedCounters: Float ; SourceASC: Object ; TargetASC: Object ; Spec: Struct` | `ReturnValue: Bool` |
| `HitPayloadModifier` | `CanApplyPayloadModifier` | `SpartaHitPayload: Object ; SourceASC: Object ; TargetASC: Object ; DamageCauser: Object ; HitResult: Struct` | `ReturnValue: Bool` |
| `HitPayloadModifier` | `ApplyPayloadModifier` | `SpartaHitPayload: Object ; SourceASC: Object ; TargetASC: Object ; DamageCauser: Object ; HitResult: Struct` | none |
| `ExecutionValueModifier` | `CanApplyExecutionModifier` | `SourceASC: Object of AbilitySystemComponent ; TargetASC: Object of AbilitySystemComponent ; SourceTags: Struct of GameplayTagContainer ; TargetTags: Struct of GameplayTagContainer` | `ReturnValue: Bool` |

`USpartaDamageExecution`, `USpartaBreakExecution`, `USpartaPoiseExecution`, `USpartaHealExecution`,
`USpartaWindedExecution` and `USpartaExecutionCalculation` are **empty in reflection** — they
declare no UFUNCTION/UPROPERTY. Their behaviour is C++-only and cannot be hooked through Blueprint
reflection.

### Hit-reaction / death-condition data access

| Class | Function | Parameters | Return |
|---|---|---|---|
| `SpartaHitReactionAnimSet` | `GetHitReactionAnimation` | `ReactionType: Struct(GameplayTag) ; Direction: Enum(ESpartaStrikeDirection) ; ASC: Object ; SourceTags: Struct(GameplayTagContainer) ; bAdditive: Bool ; bRandomizeStartTime: Bool ; StartTimeRange: Struct(Vector2D) ; ChosenReactionTag: Struct(GameplayTag) ; ReactionCooldown: Float` | `ReturnValue: ObjectProperty of /Script/Engine.AnimMontage` |
| `SpartaHitReactionAnimSet` | `HasReaction` | `ReactionType: Struct(GameplayTag)` | `ReturnValue: Bool` |
| `SpartaHitReactionAnimSet` | `HasReactionForDirection` | `ReactionType: Struct(GameplayTag) ; Direction: Enum` | `ReturnValue: Bool` |
| `SpartaDeathAnimationSet` | `GetDeathAnimation` | `DeathType: Struct(GameplayTag) ; GameplayEventData: Struct` | `ReturnValue: Struct(FDeathAnimation)` |
| `SpartaDeathCondition` | `Evaluate` | `EventData: Struct(GameplayEventData)` | `ReturnValue: Bool` |
| `SpartaPhysicalHitReactionData` | `FindClosestBoneChainToHitLocation` | `Mesh: Object ; HitLocation: Struct(Vector)` | `ReturnValue: IntProperty` |
| `SpartaPhysicalHitReactionData` | `HasBoneInChain` | `ChainIndex: Int ; BoneName: Name` | `ReturnValue: Bool` |

## 6.2 (b) Functions / delegates that fire on receiving a hit

### `USpartaHealthComponent` — offsets and signatures (from `Sparta.hpp`)

| Delegate | Offset | Signature |
|---|---|---|
| `OnDamageReceived` | `0x00B8` | `(USpartaHealthComponent* HealthComponent, float Value, AActor* Instigator, const FGameplayEffectSpec& EffectSpec)` |
| `OnHealingReceived` | `0x00C8` | `(USpartaHealthComponent*, float Value, AActor* Instigator, const FGameplayEffectSpec&)` |
| `OnMaxHealthChanged` | `0x00D8` | `(USpartaHealthComponent*, float OldValue, float NewValue, AActor* Instigator)` |
| `OnShellHealthDepleted` | `0x00E8` | `(AActor* OwningActor)` |
| `OnDeathStarted` | `0x00F8` | `(AActor* OwningActor)` |
| `OnDeathFinished` | `0x0108` | `(AActor* OwningActor)` |

Non-delegate members: `AbilitySystemComponent` `0x0118`, `HealthSet` `0x0120`,
`DeathState ESpartaDeathState` `0x0128`, `LastDamageTimeMap TMap<AActor*,float>` `0x0130`.
`Size: 0x180`.

`USpartaHealthComponent` functions:

| Function | Parameters | Return |
|---|---|---|
| `InitializeWithAbilitySystem` | `InASC: ObjectProperty of /Script/Sparta.SpartaAbilitySystemComponent` | — |
| `UninitializeFromAbilitySystem` | — | — |
| `StartDeath` | — | — |
| `OnRep_DeathState` | `OldDeathState: EnumProperty of /Script/Sparta.ESpartaDeathState` | — |
| `IsDeadOrDying` | — | `ReturnValue: Bool` |
| `GetDeathState` | — | `ReturnValue: EnumProperty of ESpartaDeathState` |
| `GetHealth` / `GetMaxHealth` / `GetCurrentMaxHealth` | — | `ReturnValue: Float` |
| `GetHealthNormalized` / `GetCurrentHealthNormalized` | — | `ReturnValue: Float` |
| `GetShellHealth` / `GetMaxShellHealth` / `GetShellHealthNormalized` | — | `ReturnValue: Float` |
| `FindHealthComponent` | `Actor: ObjectProperty of /Script/Engine.Actor` | `ReturnValue: ObjectProperty of SpartaHealthComponent` |

### `ASpartaCharacter` — receiving-side delegates (all `MulticastInlineDelegateProperty`)

| Delegate | Offset | Signature (from `Sparta.hpp`) |
|---|---|---|
| `OnHitApplied` | `0x08D8` | `(USpartaHitPayload* Hit, const FHitResult HitResult, const ASpartaWeapon* DamagingWeapon, ASpartaCharacter* Instigator)` |
| `OnHitReceived` | `0x08E8` | `(AActor* InstigatorActor, UAbilitySystemComponent* InstigatorASC, USpartaHitPayload* Payload, FGameplayEffectContextHandle EffectContext, AActor* DamageCauser, const FHitResult& HitResult)` |
| `OnHealthDamageCaused` | `0x08F8` | `(float Value, AActor* Victim, FGameplayEffectContextHandle EffectContextHandle)` |
| `CharacterAggro` | `0x0950` | `(FSpartaCharEventData)` |
| `CharacterDodge` | `0x0960` | `(FSpartaCharEventData)` |
| `CharacterRoll` | `0x0970` | `(FSpartaCharEventData)` |
| `CharacterAttack` | `0x0980` | `(const ASpartaWeapon* AttackingWeapon, const FAudioSwitchState AttackType, FSpartaCharEventData)` |
| `CharacterHit` | `0x0990` | `(const ASpartaWeapon* DamagingWeapon, float Damage, FSpartaCharEventData)` |
| `CharacterDeath` | `0x09A0` | `(const ASpartaWeapon* DamagingWeapon, float Damage, FSpartaCharEventData)` |
| `CharacterTeleport` | `0x09B0` | `(FSpartaCharEventData)` |
| `OnSetCharacterData` | `0x09C0` | `(const USpartaCharacterData*)` |
| `OnTargetedBy` | `0x09D0` | `(ASpartaCharacter* Targeter)` |
| `OnAttackInitiatedAgainstSelf` | `0x09E0` | `(ASpartaCharacter* Targeter)` |
| `OnTeamChanged` | `0x09F0` | `(ASpartaCharacter* Owner, FGameplayTag OldTeam, FGameplayTag NewTeam)` |

Other `ASpartaCharacter` damage members: `HealthComponent @0x06A0`,
`HitReactionAnimSet @0x06A8`, `HitTraceComponent @0x06C0`, `CurrentTarget @0x0AC8`,
`Team @0x0AB0`, `CharacterId @0x06E8`. Functions: `OnDeathStarted(AActor* OwningActor)`,
`IsDead()`, `GetTeamRelationship(const AActor* Other)`, `SetTeam(FGameplayTag)`,
`SetCurrentTarget(AActor*)`, `GetCurrentTarget()`, `OnEncounterStarted()`.

### `USpartaHitTraceComponent` delegates

| Delegate | Offset | Signature |
|---|---|---|
| `OnTraceHit` | `0x00B8` | `SpartaTraceHitEvent(const USpartaHitTrace* Trace, const FHitResult& HitResult)` |
| `OnContinuousTraceStarted` | `0x00C8` | `SpartaTraceContinuousEvent(const USpartaHitTrace* Trace)` |
| `OnContinuousTraceStopped` | `0x00D8` | `SpartaTraceContinuousEvent(const USpartaHitTrace* Trace)` |

### Aggro / encounter / spawn delegates (all `MulticastInlineDelegateProperty`)

| Class | Delegate | Offset (hex) | Signature |
|---|---|---|---|
| `ASpartaAICharacter` | `OnSpawnFromSpawnerFinished` | `0xB08` | `(ASpartaAICharacter* Character)` |
| `ASpartaAIController` | `OnPreAggro` | `0x3F8` | `SpartaEngagementEvent(const FSpartaEngagementData&)` |
| `ASpartaAIController` | `OnAggro` | `0x408` | `SpartaEngagementEvent(const FSpartaEngagementData&)` |
| `ASpartaAIController` | `OnDeaggro` | `0x418` | `SpartaEngagementEvent(const FSpartaEngagementData&)` |
| `ASpartaEncounterManager` | `OnEncounterStart` | `0x2F0` | `EncounterStartDelegate()` |
| `ASpartaEncounterManager` | `OnEncounterCompleted` | `0x308` | `OnEncounterCompletedDelegate()` |
| `ASpartaEnemySubsystem` | `OnCharacterDeathEvent` | `0xD8` | `OnCharacterDeath(ASpartaCharacter*)` |
| `ASpartaEnemySubsystem` | `OnCharacterEngageStateChanged_BP` | `0x158` | `OnCharacterEngageDynamic(ASpartaCharacter*, bool bEngaged)` |
| `ASpartaHordeEncounter` | `OnHordeCompleted` | `0x2E8` | `OnHordeCompletedDelegate(AActor* CompletedHorde)` |
| `UHordeSpawnerComponent` | `OnHordeCompleted` | `0xE8` | `(UHordeSpawnerComponent* CompletedHorde)` |
| `ASpartaTargetLockComponent` | `OnLockedTargetChanged` | `0xB8` | `(AActor* NewTarget)` |
| `USpartaTargeter` | `OnNewTarget` | `0x28` | `(AActor* HitCharacter, FHitResult HitResult)` |
| `UTargetSelectionComponent` | `OnTargetSwitched` | `0xB8` | `(ASpartaCharacter* Owner, AActor* OldTarget, AActor* NewTarget)` |
| `ASpawnTrigger` | `OnSpawnTriggerLoaded` | `0x340` | `()` |
| `AUnitSpawner` | `OnCharacterSpawned` | `0x2B8` | `(ASpartaCharacter* SpawnedCharacter)` |

### Blueprint-side hit receipt

`BPC_EnemyHealthWidget_C` (on every `BP_AICharacter_C` at `0xb48`) is the BP listener:

| Function | Parameters |
|---|---|
| `OnDamageTaken` | `HealthComponent: ObjectProperty of SpartaHealthComponent ; Value: FloatProperty ; Instigator: ObjectProperty of Actor ; EffectSpec: StructProperty of GameplayEffectSpec` |
| `OnHealingReceived` | `HealthComponent: Object ; Value: Float ; Instigator: Object ; EffectSpec: Struct` |
| `OnBreakDamage` | `NewValue: DoubleProperty ; OldValue: DoubleProperty` |
| `OnGameplayEffectApplied` | `Actor: Object ; SpecHandle: Struct ; ActiveHandle: Struct` |
| `OnDeathStarted` | `OwningActor: ObjectProperty of Actor` |
| `BindOnDamageTaken` / `UnbindFromDamageTaken` | — |
| `BindOnHealingReceived` | — |
| `BindOnBreakDamageTaken` | — |
| `BindOnDeathStarted` / `BindOnDeathEvent` | — |
| `BindOnExplodeEvent` / `BindOnKillSelfEvent` | — |
| `BindOnGameplayEffectApplied` / `BindOnLockedOn` | — |
| `InitializeMe` / `InitializeComponent(Success: Bool)` / `InvalidateMe` / `EndTasks` | — |
| `IsBossCharacter` | `ReturnValue: BoolProperty` |
| `SpawnParryableAttackIndicator` | `InstantaneousWarningSocket: NameProperty` |
| `ClearParryableAttackIndicator` | — |
| `SpawnRiposteIndicator` | `SocketName: NameProperty` |
| `ClearRiposteIndicator` | — |
| `SpawnWarningIndicator` | `InSocket: NameProperty` |
| `ClearWarningIndicator` | — |

`BP_AICharacter_C` receives hits via: `OnNearByDeath` (MulticastInlineDelegate `0xbb0`),
`hasPreDeathStateType` (ByteProperty `0xbc8`), `CanEnterPreDeathState` → `Return: ByteProperty`,
`GetDeathAnimationSet` → `SpartaDeathAnimationSet`, `GetCharacterID` → `GameplayTag`,
`SetDeathResources(CharacterDeathData: SpartaCharacterDeathData)`,
`getCurrentHealthValueBeforeDmgRecieved` → `Health: DoubleProperty`,
`GetPriorityBonus(GameplayTag)` → `ReturnValue: FloatProperty`,
`CanBeTargeted()` → `ReturnValue: BoolProperty`, `GetAIFightTarget()` → `Actor`,
`SetAIRiposteTriggerDistance(Input: DoubleProperty)`,
`GetAIRiposteTriggerDistance` → `Return: DoubleProperty`,
`GetAIRiposteTriggerAngle` → `Return: Vector2D`.
`BP_AICharacter` damage/death properties: `DeathAnimset SpartaDeathAnimationSet* 0xba8`,
`RiposteTriggerDistance 0xd38`, `RiposteTriggerAngle Vector2D 0xd90`,
`DisableDynamicShadowAfterDeath 0xd70`, `RemoveCorpseAfterDeath 0xd71`,
`CorpseRemovalTimer TimerHandle 0xd78`, `CorpseRemovalThresholdInSeconds 0xd80`,
`CanPushPlayerCharacter 0xd88`, `HasEverBeenDamaged 0xd89`, `RagdollPushInfluence 0xd68`.

Death abilities reachable by name: `GA_Death_AI_C` (base), `GA_Death_AI_BellSnail`,
`GA_Death_AI_HeadTree`, `GA_Death_AI_Miner`, `GA_Death_Batman_Suicide`, `GA_Death_BrigElite_Jumper`,
`GA_Death_Explosive`, `GA_Death_Explosive_Grenadier`, `GA_Death_GiantBaby`,
`GA_Death_ToxicStalker` (+`_Frost`, `_Hommunculus`), `GA_Death_FinalBoss`, `GA_Death_Hexapod`,
`GA_Death_Swordman`, `GA_Death_AI_Parasite_Golem`, `GA_Death_AI_TarGolem`, `GA_PreDeath`.
Parry/riposte abilities: `GA_Enemy_Parry_Attack`, `GA_ParryResistanceHandler`,
`GA_Sicario_Parry_Attack`, `GA_TwinSister_Daggers_Parry_Attack`,
`GA_TwinSister_SpearDaggers_Parry_Attack`, `GA_StoneCrab_Riposte_AOE_EnemyDmg`,
`GA_AI_Victim_Execution_InstantDeath`, `GA_MothKnight_ChangePhaseNoRiposte`.

## 6.3 (c) Payload / struct field tables

### `FHealthDamageOption` — `Size: 0xC`

| Field | Type | Offset |
|---|---|---|
| `Type` | `EHealthDamageType` | `0x0000` |
| `FloatValue` | `float` | `0x0004` |
| `Multiplier` | `float` | `0x0008` |

`EHealthDamageType`: `Float = 0`, `InstigatingCharacterData = 1`, `InstigatingCharacterWeapon = 2`.

### `USpartaHitPayload` — every field, `Size: 0x158`

| Field | Type | Offset |
|---|---|---|
| `HealthDamage` | `FHealthDamageOption` | `0x0028` |
| `PoiseDamageOption` | `FHealthDamageOption` | `0x0034` |
| `BreakDamageOption` | `FHealthDamageOption` | `0x0040` |
| `DamageEffect` | `TSubclassOf<UGameplayEffect>` | `0x0050` |
| `AdditionalEffects` | `TArray<FAdditionalEffect>` | `0x0058` |
| `DamageDataTag` | `FGameplayTag` | `0x0068` |
| `ReactionTag` | `FGameplayTag` | `0x0070` |
| `StrikeDirection` | `ESpartaStrikeDirection` | `0x0078` |
| `PoiseDamage` | `float` | `0x007C` |
| `PoiseDamageEffect` | `TSubclassOf<UGameplayEffect>` | `0x0080` |
| `BreakDamage` | `float` | `0x0088` |
| `BreakDamageEffect` | `TSubclassOf<UGameplayEffect>` | `0x0090` |
| `PoiseBrokenEffect` | `TSubclassOf<UGameplayEffect>` | `0x0098` |
| `PoiseDamageDataTag` | `FGameplayTag` | `0x00A0` |
| `BreakDamageDataTag` | `FGameplayTag` | `0x00A8` |
| `ChosenReactionTag` | `FGameplayTag` | `0x00B0` |
| `PayloadTags` | `FGameplayTagContainer` | `0x00B8` |
| `CustomElementalStacks` | `int32` | `0x00D8` |
| `DamageDealt` | `float` | `0x00DC` |
| `CalculatedDamage` | `float` | `0x00E0` |
| `CalculatedPoiseDamage` | `float` | `0x00E4` |
| `CalculatedBreakDamage` | `float` | `0x00E8` |
| `CachedTargetTags` | `FGameplayTagContainer` | `0x00F0` |
| `CachedSourceTags` | `FGameplayTagContainer` | `0x0110` |
| `CachedTargetTagsPostApply` | `FGameplayTagContainer` | `0x0130` |
| `MergedPayloads` | `int32` | `0x0150` |
| `bIsCDO` | `bool` | `0x0154` |

`ESpartaStrikeDirection`: `None=0, Left=1, Right=2, Up=3, Down=4, Forward=5, Backward=6`.
`ESpartaApplyOrder`: `PreDamage=0, PostDamage=1, PostEvent=2`.

### `FSpartaHitTraceParams` — `Size: 0x80`

| Field | Type | Offset |
|---|---|---|
| `DamagePayload` | `USpartaHitPayload*` | `0x0000` |
| `TraceSetup` | `ESpartaHitTraceSetup` | `0x0008` |
| `TraceSource` | `ESpartaHitTraceActorSource` | `0x0009` |
| `TraceType` | `ESpartaHitTraceType` | `0x000A` |
| `ComponentNames` | `TArray<FName>` | `0x0010` |
| `CollisionShapes` | `TArray<FSpartaHitTraceShapeInfo>` | `0x0020` |
| `PhysicsBodyBones` | `TArray<FName>` | `0x0030` |
| `TraceSettings` | `ESpartaHitTraceSettings` | `0x0040` |
| `TeamRelationship` | `int32` | `0x0044` |
| `TraceProfile` | `FName` | `0x0048` |
| `UseDeferredHitDispatch` | `bool` | `0x0050` |
| `bAllowMultipleHitsOnSameActor` | `bool` | `0x0051` |
| `MultipleHitOnSameActorDelay` | `float` | `0x0054` |
| `EnvironmentalCollisionChannels` | `TArray<TEnumAsByte<ECollisionChannel>>` | `0x0058` |
| `EnvironmentalHitFrequencyLimit` | `float` | `0x0068` |
| `bUseSharedHitList` | `bool` | `0x006C` |
| `SharedHitListId` | `FName` | `0x0070` |
| `MotionSubstep` | `ESpartaHitTraceMotionSubstepLevel` | `0x0078` |

Enums: `ESpartaHitTraceSetup {UseWeaponDefaults=0, Manual=1}`,
`ESpartaHitTraceActorSource {Weapon=0, Character=1}`,
`ESpartaHitTraceType {CollisionShapes=0, ComponentReferences=1, PhysicsBodies=2}`,
`ESpartaHitTraceSettings {Default=0, Custom=1}`,
`ESpartaHitTraceTeam {None=0, Enemies=1, Neutrals=2, Allies=4}`,
`ESpartaHitTraceMotionSubstepLevel {None=0, Low=1, Medium=2, High=3}`.

### Other hit-path structs

| Struct | Fields (name: type @offset) | Size |
|---|---|---|
| `FSpartaHitTraceShapeInfo` | `HitTraceShape: USpartaHitTraceShape* @0x0000`, `BoneName: FName @0x0008` | `0x10` |
| `FSpartaHitTraceHandle` | *(no fields)* | `0x8` |
| `FSpartaHitTraceProfiles` | `ProfileName: FName @0x0000`, `CollisionResponse: FCollisionResponseContainer @0x0008` | `0x28` |
| `FSpartaCollisionShapeTrace` | `Rotation: FQuat @0x0010`, `Offset: FVector @0x0030`, `Component: USceneComponent* @0x0048`, `BoneName: FName @0x0050` | `0x60` |
| `FSpartaHitPayloadHolder` | `Data: USpartaHitPayload* @0x0000` | `0x8` |
| `FSpartaActiveHitTrace` | `HitTrace: USpartaHitTrace* @0x0008` | `0x10` |
| `FSpartaPendingHitTrace` | `Trace: USpartaHitTrace* @0x0000` | `0x118` |
| `FSpartaSharedHitList` | `HitList: TMap<AActor*,float> @0x0008` | `0x58` |
| `FAdditionalEffect` | `Effect: TSubclassOf<UGameplayEffect> @0x0000`, `ApplyOrder: ESpartaApplyOrder @0x0008` | `0x10` |
| `FHitPredictionResult` | `Attacker: ASpartaCharacter* @0x0000`, `HitPayload: USpartaHitPayload* @0x0008`, `HitNotifyBegin: float @0x0010` | `0x18` |
| `FSpartaHitReactionAnimData` | `Description: FString @0x0000`, `ReactionTypes: FGameplayTagContainer @0x0010`, `ReactionRedirects: TArray<FSpartaHitReactionRedirect> @0x0030`, `bAdditive: bool @0x0040`, `ReactionCooldown: float @0x0044`, `Settings: TArray<FSpartaHitReactionAnimSettings> @0x0048` | `0x58` |
| `FSpartaHitReactionAnimSettings` | `StrikeDirection: ESpartaStrikeDirection @0x0000`, `Animations: TArray<UAnimMontage*> @0x0008`, `RepetitionPolicy: ERepetitionPolicy @0x0018`, `bRandomizeStartTime: bool @0x0019`, `StartTimeRange: FVector2D @0x0020` | `0x30` |
| `FSpartaHitReactionRedirect` | `RedirectSourceRequireTags @0x0000`, `RedirectSourceIgnoreTags @0x0020`, `RedirectTargetRequireTags @0x0040`, `RedirectTargetIgnoreTags @0x0060`, `Reaction: FGameplayTag @0x0080` | `0x88` |
| `FSpartaHitRedirectRules` | `ReactionTypes: FGameplayTagContainer @0x0000`, `ReactionRedirects: TArray<FSpartaHitReactionRedirect> @0x0020` | `0x30` |
| `FSpartaCharacterDamageData` | `BaseDamage @0x0000`, `BaseRangedDamage @0x0004`, `BasePoiseDamage @0x0008`, `BaseBreakDamage @0x000C` | `0x10` |
| `FSpartaCharacterDeathData` | `GloomOnDeath @0x0000`, `GoldOnDeath @0x0004`, `GoldDropChance @0x0008`, `ItemDropTable @0x0010` | `0x60` |
| `FSpartaDeathAnimationData` | `Description: FString @0x0000`, `DeathTypes: FGameplayTagContainer @0x0010`, `Settings: TArray<FSpartaDeathAnimationSettings> @0x0030` | `0x40` |
| `FSpartaDeathAnimationSettings` | `Conditions: TArray<USpartaDeathCondition*> @0x0000`, `Priority: int32 @0x0010`, `Animations: TArray<FDeathAnimation> @0x0018` | `0x28` |
| `FDeathAnimation` | `Animation: UAnimMontage* @0x0000` | `0x8` |
| `FSpartaEngagementData` | `Character: ASpartaCharacter* @0x0000` | `0x8` |
| `FTargetPriorityModifier` | `ModifierType: ETargetPrioModifierType @0x0000`, `MultiplierValue: float @0x0004` | `0x8` |
| `FSpartaTraceRecord` | *(no named fields)* | `0xD0` |
| `FSpartaHitTraceResultCache` | *(no named fields)* | `0x18` |

`USpartaHealthSet` attributes (`FGameplayAttributeData`, 0x10 each): `Health @0x0060`,
`MaxHealth @0x0070`, `ShellHealth @0x0080`, `MaxShellHealth @0x0090`, `MinHealth @0x00A0`,
`MinShellHealth @0x00B0`, `Armor @0x00C0`, `Resolve @0x00D0`, `MaxResolve @0x00E0`,
`Poise @0x00F0`, `MaxPoise @0x0100`, `BleedResistance @0x0110`,
`BleedResistanceMultiplier @0x0120`, `FrostResistance @0x0130`,
`FrostResistanceIncrement @0x0140`, `ShockResistance @0x0150`,
`ShockResistanceIncrement @0x0160`, `WaspsResistance @0x0170`,
`WaspsResistanceIncrement @0x0180`, `CurseResistance @0x0190`,
`CurseResistanceIncrement @0x01A0`, `ConfusionResistance @0x01B0`,
`ConfusionResistanceIncrement @0x01C0`, `ConfusionBreakDamage @0x01D0`,
`CosmicDiseaseResistance @0x01E0`, `CosmicDiseaseResistanceIncrement @0x01F0`,
`StasisWeakness @0x0200`, `BreakResistance @0x0210`, `MaxBreakResistance @0x0220`,
`RiposteWeakness @0x0230`, `StoneStunTime @0x0240`, `Healing @0x0258`, `Damage @0x0268`,
`PoiseDamage @0x0278`, `BreakDamage @0x0288`, `ResolveModify @0x0298`.
`Size: 0x2A8`. Note the gap at `0x0248` (the dump/hpp shows `StoneStunTime 0x0240` followed by
`Healing 0x0258`) — 0x10 bytes at `0x0250` are not a named UPROPERTY in the header.

## 6.4 (d) Death / respawn entry points

| Class | Function / delegate | Parameters | Notes |
|---|---|---|---|
| `USpartaHealthComponent` | `StartDeath` | — | the only native "begin dying" call |
| `USpartaHealthComponent` | `OnRep_DeathState` | `OldDeathState: ESpartaDeathState` | replicates death state |
| `USpartaHealthComponent` | `IsDeadOrDying` | → `Bool` | |
| `USpartaHealthComponent` | `GetDeathState` | → `ESpartaDeathState` | `NotDead=0, DeathStarted=1, DeathFinished=2` |
| `USpartaHealthComponent` | `OnDeathStarted` / `OnDeathFinished` delegates | `(AActor* OwningActor)` | offsets `0xF8` / `0x108` |
| `ASpartaCharacter` | `OnDeathStarted` | `OwningActor: ObjectProperty of Actor` | |
| `ASpartaCharacter` | `IsDead` | — | |
| `ASpartaCharacter` | `CharacterDeath` delegate | `(const ASpartaWeapon*, float Damage, FSpartaCharEventData)` | offset `0x9A0` |
| `ASpartaAICharacter` | `OnDeath` | `in_OwningActor: ObjectProperty of Actor` | |
| `ASpartaAIController` | `OnPawnDeath` | `OwningActor: ObjectProperty of Actor` | |
| `ASpartaAIController` | `OnPlayerDeath` | `DamagingWeapon: Object ; Damage: Float ; EventData: Struct` | |
| `USpartaEnemySubsystem` | `OnCharacterDeath` | `OwningActor: ObjectProperty of Actor` | bound to `OnCharacterDeathEvent @0xD8` |
| `USpartaEnemySubsystem` | `OnCharacterDestroyed` | `Actor: ObjectProperty of Actor ; EndPlayReason: ByteProperty (EEndPlayReason)` | |
| `USpartaEncounterManager` | `OnEnemyDeath` | `DeadActor: ObjectProperty of Actor` | drives encounter completion |
| `USpartaHordeEncounter` | `OnEnemyDeath` | `ThatsOneDeadCharacter: ObjectProperty of Actor` | |
| `UHordeSpawnerComponent` | `OnEnemyDeath` | `DeadCharacter: ObjectProperty of Actor` | |
| `UHordeUnitComponent` | `OnDeath` | `DeadActor: ObjectProperty of Actor` | |
| `AHordeClusterCenter` | `OnUnitDeath` | `DeadActor: ObjectProperty of Actor` | |
| `UTargetSelectionComponent` | `OnTargetDeath` | `OwningActor: ObjectProperty of Actor` | |
| `BPC_BossAI_C` | `OnBossDeath` delegate | `()` | offset `0xd8` |
| `BPC_BossAI_C` | `OnBossDeath__DelegateSignature` | — | |
| `USpartaDeathCondition` | `Evaluate` | `EventData: FGameplayEventData` → `Bool` | picks a death animation |
| `USpartaDeathAnimationSet` | `GetDeathAnimation` | `DeathType: FGameplayTag ; GameplayEventData` → `FDeathAnimation` | |
| `ISpartaSaveInterface` | `SessionReset` | — | only native "reset" entry point found |

**Respawn:** no `Respawn` function exists anywhere in `/Script/Sparta` relevant to enemies —
`UNKNOWN`. I ran `SELECT path FROM obj WHERE type='Function' AND path LIKE '%:Respawn%'` filtered to
`/Script/Sparta.%` and to the enemy BP folders: 0 matches. The nearest thing is
`BP_AICharacter_C.ResetSpawnTrigger` (`Return: BoolProperty`) which asks the *spawn trigger* to
reset, plus `ASpawnTrigger::StartEncounter` being callable again once `bEncounterStarted`
(`0x0361`) is cleared.

---

# 7. UNKNOWNs / open questions

1. **BT ↔ controller binding** — `ASpartaAIController.BaseBehaviorTree` (`0x3D0`) values are not in
   the object dump. Need a live CDO read (`Blueprint CDO → BaseBehaviorTree`) or the `.uasset`
   parse. Everything in §2.1's BT column is *asset existence*, not binding.
2. **Ability → montage mapping** — `AnimMontage` values on `GA_*` abilities are not in the dump.
   `UNKNOWN`, tried `parse_dump.py show` on `GA_*` classes and their `Default__` CDOs.
3. **Arena volume / fog wall class** — no class matching `Volume|Arena|Gate|Fog|Wall` under
   `Bosses/` or `Maps/` looks like a boss-fight boundary. Nearest is
   `BP_SpawnLimitsVolume_C`. Boss-fight gating may be pure level geometry.
4. **`SpartaDamageExecution` internals** — empty in reflection; damage formula is C++-only.
   Same for `SpartaBreakExecution` / `SpartaPoiseExecution` / `SpartaHealExecution` /
   `SpartaWindedExecution`. Hook points are the `*ExecutionModifier` BlueprintNative events only.
5. **Respawn-on-rest** — no native `Respawn`/`Rest` function found. The mechanism connecting
   player rest to enemy re-spawn was not established (not in my area either, but it is a
   prerequisite for enemy sync).
6. **`WBP_Enemy_Boss_Health_C.UpdateHealthPercent`** — has temp properties but no own parameters
   in the dump; its real signature is unknown.
7. **`USpartaHealthSet` gap at `0x0248..0x0258`** — 16 bytes of unnamed state between
   `StoneStunTime` and `Healing`.
8. **`FSpartaTraceRecord` (`0xD0`) and `FSpartaHitTraceResultCache` (`0x18`)** — completely opaque
   (no named fields).
9. **Exact delegate names on `BPC_BossAI`'s phase-3 path** — `OnThirdPhaseStarted` exists as a
   property (`0x260`) but no `OnThirdPhaseStarted__DelegateSignature` function was found in the
   dump; only `OnSecondPhaseStarted`, `OnBossAggro`, `OnBossDeaggro`, `OnBossDeath`.

---

# Appendix A — verbatim dump lines for the enemy BP classes

Produced byte-identically by `scripts\q\eb-20-ev-enemies.sql`; reconstruction
(`'['||addr||'] '||type||' '||path||rest`) was verified against `parse_dump.py raw` output.
239 lines — every `BlueprintGeneratedClass` under `/Game/Sparta/Characters/Enemies/` except `GA_*`.

```
[000001F4C6A4CE20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Aristocrat/BP_Aristocrat.BP_Aristocrat_C [n: 398DC2] [c: 00007FF46EEC3D38] [or: 000001F4C6C49B60] [sps: 000001F61524EAA0]
[000001F4C6A40310] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Aristocrat/BP_Aristocrat_Controller.BP_Aristocrat_Controller_C [n: 2EF062] [c: 00007FF46EEC3D38] [or: 000001F4C6C49C00] [sps: 000001F61FE14330]
[000001F4C6A4F1C0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Aristocrat/BP_LordVellen_MB.BP_LordVellen_MB_C [n: E34A1] [c: 00007FF46EEC3D38] [or: 000001F4C6C4CD10] [sps: 000001F61524EAA0]
[000001F4CC772320] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Aristocrat/BP_SicarioTheKeeper.BP_SicarioTheKeeper_C [n: 483BDE] [c: 00007FF46EEC3D38] [or: 000001F4C93635D0] [sps: 000001F61524EAA0]
[000001F4C20EAA80] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Aristocrat/BP_Thorn_Aristocrat_Controller.BP_Thorn_Aristocrat_Controller_C [n: 2CC6AE] [c: 00007FF46EEC3D38] [or: 000001F4C936C680] [sps: 000001F61FE14330]
[000001F4C20E9190] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Aristocrat/BP_ThornboiWithWeapon.BP_ThornboiWithWeapon_C [n: 3F1BA2] [c: 00007FF46EEC3D38] [or: 000001F4C21017D0] [sps: 000001F61524EAA0]
[000001F4D0B22A40] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Babka/Accordion/BP_Babka_Accordion.BP_Babka_Accordion_C [n: 47B8AF] [c: 00007FF46EEC3D38] [or: 000001F4C21042F0] [sps: 000001F61524EAA0]
[000001F4D0B22320] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Babka/Accordion/BP_Babka_Accordion_Controller.BP_Babka_Accordion_Controller_C [n: 37A489] [c: 00007FF46EEC3D38] [or: 000001F4C21047A0] [sps: 000001F61FE14330]
[000001F4D0B2AA80] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Babka/Broom/BP_Babka_Broom.BP_Babka_Broom_C [n: 139DCC] [c: 00007FF46EEC3D38] [or: 000001F4D2F13530] [sps: 000001F61524EAA0]
[000001F4D0B2AE10] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Babka/Broom/BP_Babka_Controller.BP_Babka_Controller_C [n: 4A6B47] [c: 00007FF46EEC3D38] [or: 000001F4D2F137B0] [sps: 000001F61FE14330]
[000001F4D4258E00] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BabyEnemy/BP_BabyEnemy.BP_BabyEnemy_C [n: 382C9F] [c: 00007FF46EEC3D38] [or: 000001F4D2F1D530] [sps: 000001F61524EAA0]
[000001F4D0AD6DF0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BabyEnemy/BP_BabyEnemy_Controller.BP_BabyEnemy_Controller_C [n: 375DDE] [c: 00007FF46EEC3D38] [or: 000001F4D2F1D5D0] [sps: 000001F61FE14330]
[000001F4D476D540] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BabySnail/BP_BabySnail.BP_BabySnail_C [n: 27E896] [c: 00007FF46EEC3D38] [or: 000001F4D4774FC0] [sps: 000001F61524EAA0]
[000001F4D47626B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BabySnail/BP_BabySnail_Controller.BP_BabySnail_Controller_C [n: 48C165] [c: 00007FF46EEC3D38] [or: 000001F4D4774480] [sps: 000001F61FE14330]
[000001F4D4765170] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallBoy/Abilities/BTD_CheckForAbyss.BTD_CheckForAbyss_C [n: 494F96] [c: 00007FF46EEC3D38] [or: 000001F4D4774B60] [sps: 00007FF46EFB4B38]
[000001F4DBD7EAA0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallBoy/BP_BallBoy.BP_BallBoy_C [n: 439B62] [c: 00007FF46EEC3D38] [or: 000001F4D4776960] [sps: 000001F61524EAA0]
[000001F4DBD70310] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallBoy/BP_BallBoy_Controller.BP_BallBoy_Controller_C [n: 27E8C5] [c: 00007FF46EEC3D38] [or: 000001F4D4776A00] [sps: 000001F61FE14330]
[000001F4DBD7D1B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallBoy/BP_BallBoy_Miniboss_Memory.BP_BallBoy_Miniboss_Memory_C [n: 205785] [c: 00007FF46EEC3D38] [or: 000001F4C2542590] [sps: 000001F4DBD7EAA0]
[000001F4DD012320] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallistaHead/Abilities/BP_Ballistazooka_Miniboss_Bombardment_Handler.BP_Ballistazooka_Miniboss_Bombardment_Handler_C [n: 35FDE3] [c: 00007FF46EEC3D38] [or: 000001F4C2542310] [sps: 00007FF46EE52028]
[000001F4DD011C00] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallistaHead/Abilities/BP_Projectile_Ballista.BP_Projectile_Ballista_C [n: 31F67D] [c: 00007FF46EEC3D38] [or: 000001F4C2545010] [sps: 000001F756ACCE20]
[000001F4DD01DC60] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallistaHead/Abilities/BP_Projectile_Ballista_Lightning.BP_Projectile_Ballista_Lightning_C [n: 3A1E7C] [c: 00007FF46EEC3D38] [or: 000001F4C2549D90] [sps: 000001F4DD011C00]
[000001F4DD018350] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallistaHead/Abilities/BP_Projectile_Ballistazooka_Miniboss_.BP_Projectile_Ballistazooka_Miniboss__C [n: 2B2E60] [c: 00007FF46EEC3D38] [or: 000001F4C254CA40] [sps: 000001F4DD011C00]
[000001F4DD017C30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallistaHead/Abilities/BP_Projectile_Ballistazooka_Miniboss_Bombarment.BP_Projectile_Ballistazooka_Miniboss_Bombarment_C [n: 341403] [c: 00007FF46EEC3D38] [or: 000001F4C254CCC0] [sps: 000001F4DD011C00]
[000001F76BAE6A60] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallistaHead/BP_BallistaHead.BP_BallistaHead_C [n: 23468E] [c: 00007FF46EEC3D38] [or: 000001F4DD949CA0] [sps: 000001F61524EAA0]
[000001F4D0AD5500] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallistaHead/BP_BallistaHead_Controller.BP_BallistaHead_Controller_C [n: D1823] [c: 00007FF46EEC3D38] [or: 000001F4DD949DE0] [sps: 000001F61FE14330]
[000001F76BAE3FA0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallistaHead/BP_BallistaHead_Lightning.BP_BallistaHead_Lightning_C [n: 3E4400] [c: 00007FF46EEC3D38] [or: 000001F4DD94C2C0] [sps: 000001F76BAE6A60]
[000001F76BAE0DC0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallistaHead/BP_BallistaHead_Miniboss.BP_BallistaHead_Miniboss_C [n: B8281] [c: 00007FF46EEC3D38] [or: 000001F4DD94C720] [sps: 000001F61524EAA0]
[000001F76BAE5500] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BallistaHead/BP_BallistazookaHead_Controller.BP_BallistazookaHead_Controller_C [n: F03D9] [c: 00007FF46EEC3D38] [or: 000001F4DD94BA00] [sps: 000001F61FE14330]
[000001F76BAE98B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Batman/Abilities/Projectile/BP_ProjectileParticle_Batman.BP_ProjectileParticle_Batman_C [n: 382D0B] [c: 00007FF46EEC3D38] [or: 000001F4DD94FF60] [sps: 000001F76F3D0310]
[000001F79283AA80] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Batman/BP_Batman.BP_Batman_C [n: 34DE4C] [c: 00007FF46EEC3D38] [or: 000001F78B565790] [sps: 000001F61524EAA0]
[000001F79283AE10] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Batman/BP_Batman_Controller.BP_Batman_Controller_C [n: 4A2455] [c: 00007FF46EEC3D38] [or: 000001F78B565830] [sps: 000001F61FE14330]
[000001F792838A70] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Batman/BP_Batman_Suicide.BP_Batman_Suicide_C [n: 2F3782] [c: 00007FF46EEC3D38] [or: 000001F78B565CE0] [sps: 000001F79283AA80]
[000001F792838E00] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Batman/BP_Batman_Suicide_Controller.BP_Batman_Suicide_Controller_C [n: 2A9F69] [c: 00007FF46EEC3D38] [or: 000001F78B565D30] [sps: 000001F61FE14330]
[000001F74A6C78A0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Batushka/BP_Batushka.BP_Batushka_C [n: 105C7E] [c: 00007FF46EEC3D38] [or: 000001F7A7120B00] [sps: 000001F61524EAA0]
[000001F74A6C7C30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Batushka/BP_Batushka_Controller.BP_Batushka_Controller_C [n: 4238EE] [c: 00007FF46EEC3D38] [or: 000001F7A7120B50] [sps: 000001F61FE14330]
[000001F74A6CF8E0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Batushka/BP_Batushka_MiniBoss.BP_Batushka_MiniBoss_C [n: 3C5DF0] [c: 00007FF46EEC3D38] [or: 000001F7A7123CB0] [sps: 000001F74A6C78A0]
[000001F7A75A7FC0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BellSnail/BP_BellSnail.BP_BellSnail_C [n: C54FC] [c: 00007FF46EEC3D38] [or: 000001F7A712A560] [sps: 000001F61524EAA0]
[000001F7A75A8350] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BellSnail/BP_BellSnail_Controller.BP_BellSnail_Controller_C [n: 4117D1] [c: 00007FF46EEC3D38] [or: 000001F7A712AB00] [sps: 000001F61FE14330]
[000001F7A75A78A0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BellSnail/BP_BoneSnail.BP_BoneSnail_C [n: 39456E] [c: 00007FF46EEC3D38] [or: 000001F7A712B5F0] [sps: 000001F61524EAA0]
[000001F7B8BE7FC0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BladeSlave/BP_BladeSlave.BP_BladeSlave_C [n: 2DDBEC] [c: 00007FF46EEC3D38] [or: 000001F7B8D11280] [sps: 000001F61524EAA0]
[000001F7B8BE8350] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/BladeSlave/BP_BladeSlave_Controller.BP_BladeSlave_Controller_C [n: 238B7C] [c: 00007FF46EEC3D38] [or: 000001F7B8D112D0] [sps: 000001F61FE14330]
[000001F4D0AD2DD0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigBase/BP_BrigBase_Controller.BP_BrigBase_Controller_C [n: B3987] [c: 00007FF46EEC3D38] [or: 000001F7A5CB5CE0] [sps: 000001F61FE14330]
[000001F4D0AD26B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigBase/BP_Brigand_Base.BP_Brigand_Base_C [n: 27E9E6] [c: 00007FF46EEC3D38] [or: 000001F7A5CB7130] [sps: 000001F61524EAA0]
[000001F793DDBC50] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigBase/BP_Brigand_Regal.BP_Brigand_Regal_C [n: 18FB05] [c: 00007FF46EEC3D38] [or: 000001F7A5CB8170] [sps: 000001F793DDC370]
[000001F793DDAA80] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigBase/BP_Brigand_Tiel_Memory.BP_Brigand_Tiel_Memory_C [n: 13190E] [c: 00007FF46EEC3D38] [or: 000001F7A5CB92F0] [sps: 000001F61524EAA0]
[000001F793DDCE20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigCrossbow/BP_BrigBase_Crossbow_Controller.BP_BrigBase_Crossbow_Controller_C [n: CD8C0] [c: 00007FF46EEC3D38] [or: 000001F7A5CB8210] [sps: 000001F61FE14330]
[000001F793DDC370] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigCrossbow/BP_Brigand_Crossbow.BP_Brigand_Crossbow_C [n: 90682] [c: 00007FF46EEC3D38] [or: 000001F7A5CB81C0] [sps: 000001F61524EAA0]
[000001F793DDE710] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigCrossbow/Regal/BP_Projectile_Crossbow_Brigand_Regal.BP_Projectile_Crossbow_Brigand_Regal_C [n: 14F5AA] [c: 00007FF46EEC3D38] [or: 000001F7B8D19390] [sps: 000001F793DDEAA0]
[000001F7B6E77180] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigElite/BP_Brigand_Elite.BP_Brigand_Elite_C [n: 334C61] [c: 00007FF46EEC3D38] [or: 000001F7A643EB10] [sps: 000001F61524EAA0]
[000001F7B6E77510] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigElite/BP_Brigand_EliteController.BP_Brigand_EliteController_C [n: 3EDA5A] [c: 00007FF46EEC3D38] [or: 000001F7A643EBB0] [sps: 000001F61FE14330]
[000001F7B6E766D0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigElite/BP_Brigand_Elite_Jumper.BP_Brigand_Elite_Jumper_C [n: 28798C] [c: 00007FF46EEC3D38] [or: 000001F7A643ECA0] [sps: 000001F7B6E77180]
[000001F7B6E73160] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigElite/BrigElite_Fire/BP_Brigand_Elite_FireMiniboss_Controller.BP_Brigand_Elite_FireMiniboss_Controller_C [n: 40CFA8] [c: 00007FF46EEC3D38] [or: 000001F7A643FBF0] [sps: 000001F61FE14330]
[000001F7B6E72DD0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigElite/BrigElite_Fire/BP_Brigand_Elite_Fire_MiniBoss.BP_Brigand_Elite_Fire_MiniBoss_C [n: 2C898F] [c: 00007FF46EEC3D38] [or: 000001F7A643FB00] [sps: 000001F61524EAA0]
[000001F7B7576340] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigElite/BrigElite_Winter/BP_Brigand_Elite_Winter_MiniBoss.BP_Brigand_Elite_Winter_MiniBoss_C [n: 17E113] [c: 00007FF46EEC3D38] [or: 000001F7B6FB49D0] [sps: 000001F61524EAA0]
[000001F7B75766D0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigElite/BrigElite_Winter/BP_Brigand_Elite_Winter_MiniBoss_Controller.BP_Brigand_Elite_Winter_MiniBoss_Controller_C [n: 3CEB26] [c: 00007FF46EEC3D38] [or: 000001F7B6FB4A20] [sps: 000001F61FE14330]
[000001F7B7575500] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigKnightly/BP_Brigand_Knightly.BP_Brigand_Knightly_C [n: 1E6F88] [c: 00007FF46EEC3D38] [or: 000001F7B6FBAD80] [sps: 000001F61524EAA0]
[000001F7B7575890] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigKnightly/BP_Brigand_KnightlyController.BP_Brigand_KnightlyController_C [n: 431216] [c: 00007FF46EEC3D38] [or: 000001F7B6FB9AC0] [sps: 000001F61FE14330]
[000001F7B02A3160] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigRanged_Rock/Abilities/BP_BrigRanged_Projectile.BP_BrigRanged_Projectile_C [n: 21E9E7] [c: 00007FF46EEC3D38] [or: 000001F7B6FBC950] [sps: 000001F4A27D5C20]
[000001F7B02AF1C0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigRanged_Rock/BP_AIController_Brigand_Ranged.BP_AIController_Brigand_Ranged_C [n: 30E6ED] [c: 00007FF46EEC3D38] [or: 000001F7B038D1C0] [sps: 000001F7B02AF550]
[000001F7B02AEE30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigRanged_Rock/BP_Brigand_Ranged.BP_Brigand_Ranged_C [n: 3FB3FA] [c: 00007FF46EEC3D38] [or: 000001F7B038DC10] [sps: 000001F61524EAA0]
[000001F7BEA06DF0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigRanged_Rock/BP_Brigand_Ranged_Tanky.BP_Brigand_Ranged_Tanky_C [n: 4AF776] [c: 00007FF46EEC3D38] [or: 000001F7B038D8A0] [sps: 000001F7B02AEE30]
[000001F7B02AEAA0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigRanged_Rock/Projectiles/BP_VFX_BrigandRockDestroyed.BP_VFX_BrigandRockDestroyed_C [n: 275E6D] [c: 00007FF46EEC3D38] [or: 000001F7B0384390] [sps: 00007FF46EE52028]
[000001F7B0287510] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigTanky/BP_TankyBrigand.BP_TankyBrigand_C [n: 3284A0] [c: 00007FF46EEC3D38] [or: 000001F7CE9E4110] [sps: 000001F61524EAA0]
[000001F7B02866D0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigTanky/BP_TankyBrigand_Bronze.BP_TankyBrigand_Bronze_C [n: 1A18E9] [c: 00007FF46EEC3D38] [or: 000001F7CE9E4F70] [sps: 000001F7B0287510]
[000001F7BEA0EE30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigTanky/BP_TankyBrigand_Controller.BP_TankyBrigand_Controller_C [n: 11BB32] [c: 00007FF46EEC3D38] [or: 000001F7CE9E4160] [sps: 000001F61FE14330]
[000001F7B0285C20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigTanky/BP_TankyBrigand_Sword.BP_TankyBrigand_Sword_C [n: B847F] [c: 00007FF46EEC3D38] [or: 000001F7CE9E5B00] [sps: 000001F7B0287510]
[000001F7B0285FB0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigTanky/BP_TankyBrigand_Sword_Controller.BP_TankyBrigand_Sword_Controller_C [n: 222DCD] [c: 00007FF46EEC3D38] [or: 000001F7CE9E4B60] [sps: 000001F61FE14330]
[000001F7B0289FD0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/BrigTarded/BP_Horde_Brigand_Tarded.BP_Horde_Brigand_Tarded_C [n: 146E24] [c: 00007FF46EEC3D38] [or: 000001F7CE9E6DC0] [sps: 000001F7B028A6F0]
[000001F7C0466A60] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/Heavy/BP_Brigand_Heavy.BP_Brigand_Heavy_C [n: 4B8088] [c: 00007FF46EEC3D38] [or: 000001F7CE9ED440] [sps: 000001F61524EAA0]
[000001F7C0466DF0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/Heavy/BP_Brigand_HeavyController.BP_Brigand_HeavyController_C [n: 3991C9] [c: 00007FF46EEC3D38] [or: 000001F7CE9ED4E0] [sps: 000001F61FE14330]
[000001F4BE037510] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/HordeBrigand/Abilities/BP_AI_Traversal.BP_AI_Traversal_C [n: 390000] [c: 00007FF46EEC3D38] [or: 000001F4BFB62630] [sps: 00007FF46F05BF38]
[000001F7B028B8C0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/HordeBrigand/BP_AIController_Horde_Brigand_Light.BP_AIController_Horde_Brigand_Light_C [n: 2A13EB] [c: 00007FF46EEC3D38] [or: 000001F7CE9E6370] [sps: 000001F7B02AF550]
[000001F4D0AD06A0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/HordeBrigand/BP_HeadKing_Villager.BP_HeadKing_Villager_C [n: 9DB0F] [c: 00007FF46EEC3D38] [or: 000001F7DC960790] [sps: 000001F7B028B1A0]
[000001F7B028A6F0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/HordeBrigand/BP_Horde_Brigand.BP_Horde_Brigand_C [n: 2C430F] [c: 00007FF46EEC3D38] [or: 000001F7CE9E6F00] [sps: 000001F7B028B1A0]
[000001F7DC9D5C20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/HordeGrenadier/BP_AIController_Horde_Brigand_Grenadier.BP_AIController_Horde_Brigand_Grenadier_C [n: 1AE8AD] [c: 00007FF46EEC3D38] [or: 000001F7DC960B00] [sps: 000001F7B02AF550]
[000001F7DC9D5890] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/HordeGrenadier/BP_Horde_Brigand_Grenadier.BP_Horde_Brigand_Grenadier_C [n: 275FE8] [c: 00007FF46EEC3D38] [or: 000001F7DC9619B0] [sps: 000001F7B028B1A0]
[000001F7DC9D14E0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/HordeShieldBrigand/BP_AIController_Horde_Brigand_Shield.BP_AIController_Horde_Brigand_Shield_C [n: 3A6402] [c: 00007FF46EEC3D38] [or: 000001F7DC964980] [sps: 000001F7B02AF550]
[000001F7DC9D1150] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Brigands/HordeShieldBrigand/BP_Horde_Brigand_Shield.BP_Horde_Brigand_Shield_C [n: 1C85A3] [c: 00007FF46EEC3D38] [or: 000001F7DC965FB0] [sps: 000001F7B028B1A0]
[000001F7E2E36DF0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CannibalKnight/BP_CannibalKnight_Controller.BP_CannibalKnight_Controller_C [n: 2B75C0] [c: 00007FF46EEC3D38] [or: 000001F7EF451550] [sps: 000001F61FE14330]
[000001F7E2E36A60] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CannibalKnight/BP_Cannibal_Knight.BP_Cannibal_Knight_C [n: C577C] [c: 00007FF46EEC3D38] [or: 000001F7EF4524F0] [sps: 000001F61524EAA0]
[000001F7E2E36340] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CannibalKnight/BP_Cannibal_Knight_MiniBoss.BP_Cannibal_Knight_MiniBoss_C [n: 23DB65] [c: 00007FF46EEC3D38] [or: 000001F7EF4522C0] [sps: 000001F7E2E36A60]
[000001F7E2E35C20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CannibalKnight/BP_Cannibal_Knight_Miniboss_Memory.BP_Cannibal_Knight_Miniboss_Memory_C [n: 3A22F1] [c: 00007FF46EEC3D38] [or: 000001F7EF452EF0] [sps: 000001F7E2E36A60]
[000001F7E2E35500] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CannibalKnight/BP_Cannibal_Knight_Zombie.BP_Cannibal_Knight_Zombie_C [n: 2F3A78] [c: 00007FF46EEC3D38] [or: 000001F7EF452E00] [sps: 000001F61524EAA0]
[000001F7E2E34DE0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CentipedeGhost/Abilities/BP_Growing_BoxEffectCollider.BP_Growing_BoxEffectCollider_C [n: 128FFB] [c: 00007FF46EEC3D38] [or: 000001F7EF453850] [sps: 00007FF46EE52028]
[000001F7E2E30DC0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CentipedeGhost/Abilities/BP_ProjectileParticle_CentipedeGhost_AOE.BP_ProjectileParticle_CentipedeGhost_AOE_C [n: 3D29F4] [c: 00007FF46EEC3D38] [or: 000001F7EF4535D0] [sps: 000001F76F3D0310]
[000001F7C01F0310] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CentipedeGhost/BP_CentipedeGhost.BP_CentipedeGhost_C [n: 339157] [c: 00007FF46EEC3D38] [or: 000001F7E31651A0] [sps: 000001F61524EAA0]
[000001F7E30E7FC0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CentipedeGhost/BP_CentipedeGhost_Controller.BP_CentipedeGhost_Controller_C [n: 39DD8E] [c: 00007FF46EEC3D38] [or: 000001F7E3165240] [sps: 000001F61FE14330]
[000001F7E2F9B530] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistBase/BP_CultistBase_Controller.BP_CultistBase_Controller_C [n: 1C4352] [c: 00007FF46EEC3D38] [or: 000001F7DB30E020] [sps: 000001F61FE14330]
[000001F7E2F9C370] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistBase/Mace/BP_Cultist_Mace.BP_Cultist_Mace_C [n: 3901DF] [c: 00007FF46EEC3D38] [or: 000001F7DB30EC50] [sps: 000001F7E2F9BC50]
[000001F7E2F9B8C0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistBase/Sword/BP_Cultist_Sword.BP_Cultist_Sword_C [n: 1D9D3C] [c: 00007FF46EEC3D38] [or: 000001F7DB30E700] [sps: 000001F61524EAA0]
[000001F7C0357C30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistBase/Sword/BP_Cultist_Sword_Purple.BP_Cultist_Sword_Purple_C [n: 34E2F4] [c: 00007FF46EEC3D38] [or: 000001F7DB30E4D0] [sps: 000001F7E2F9B8C0]
[000001F7E2F9BC50] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistBase/Torch/BP_Cultist_Base.BP_Cultist_Base_C [n: 423C4C] [c: 00007FF46EEC3D38] [or: 000001F7DB30EC00] [sps: 000001F61524EAA0]
[000001F7DCB7DFF0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistRanged/BP_CultistRanged_Controller.BP_CultistRanged_Controller_C [n: 2DDF15] [c: 00007FF46EEC3D38] [or: 000001F7F6DE8030] [sps: 000001F61FE14330]
[000001F7DCB7DC60] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistRanged/BP_Cultist_Ranged.BP_Cultist_Ranged_C [n: 135D13] [c: 00007FF46EEC3D38] [or: 000001F7F6DE7F40] [sps: 000001F61524EAA0]
[000001F7DCB7D1B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistRanged/BP_Cultist_Ranged_Purple.BP_Cultist_Ranged_Purple_C [n: F08E1] [c: 00007FF46EEC3D38] [or: 000001F7F6DE8DA0] [sps: 000001F7DCB7DC60]
[000001F7DCB75890] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistRanged/BP_RangedCultistFrogBlastProjectile.BP_RangedCultistFrogBlastProjectile_C [n: 222FD2] [c: 00007FF46EEC3D38] [or: 000001F7F6DE1550] [sps: 000001F756ACCE20]
[000001F793DDEAA0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistSpearLady/Abilities/BP_Projectile_CultistSpearLady.BP_Projectile_CultistSpearLady_C [n: 32C19C] [c: 00007FF46EEC3D38] [or: 000001F7B8D19250] [sps: 000001F756ACCE20]
[000001F7EF5046C0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistSpearLady/BP_CultistSpearLady.BP_CultistSpearLady_C [n: 37EB5E] [c: 00007FF46EEC3D38] [or: 000001F7F71965F0] [sps: 000001F61524EAA0]
[000001F7EF504A50] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistSpearLady/BP_CultistSpearLady_Controller.BP_CultistSpearLady_Controller_C [n: 30EA2F] [c: 00007FF46EEC3D38] [or: 000001F7F7196640] [sps: 000001F61FE14330]
[000001F7EF503FA0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistSpearLady/BP_SecundusMiniboss_Controller.BP_SecundusMiniboss_Controller_C [n: 3DB7F9] [c: 00007FF46EEC3D38] [or: 000001F7F7197090] [sps: 000001F61FE14330]
[000001F7EF503880] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/CultistSpearLady/BP_SesterSecundusMiniBoss.BP_SesterSecundusMiniBoss_C [n: 13A417] [c: 00007FF46EEC3D38] [or: 000001F7F71968C0] [sps: 000001F61524EAA0]
[000001F7F1C31C00] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Draugr/BP_Draugr.BP_Draugr_C [n: 13E99E] [c: 00007FF46EEC3D38] [or: 000001F7F719E8E0] [sps: 000001F61524EAA0]
[000001F7F1C34DE0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Draugr/BP_Draugr_Controller.BP_Draugr_Controller_C [n: 20E657] [c: 00007FF46EEC3D38] [or: 000001F7F719D120] [sps: 000001F61FE14330]
[000001F7F1C3D1B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/DungeonChampion/Abilites/BP_Projectile_DungeonChampion.BP_Projectile_DungeonChampion_C [n: 4AB5BD] [c: 00007FF46EEC3D38] [or: 000001F7F5C30790] [sps: 000001F756ACCE20]
[000001F7F1C3CE20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/DungeonChampion/Abilites/BP_Projectile_DungeonChampion_Return.BP_Projectile_DungeonChampion_Return_C [n: 3060DE] [c: 00007FF46EEC3D38] [or: 000001F7F5C33170] [sps: 000001F7F1C3D1B0]
[000001F76BD046C0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/DungeonChampion/BP_DungeonChampion.BP_DungeonChampion_C [n: 27ECD7] [c: 00007FF46EEC3D38] [or: 000001F7F77B0060] [sps: 000001F61524EAA0]
[000001F7E929EAA0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/DungeonChampion/BP_DungeonChampion_Controller.BP_DungeonChampion_Controller_C [n: 1EB9F1] [c: 00007FF46EEC3D38] [or: 000001F7F77B1460] [sps: 000001F61FE14330]
[000001F76BD0EE30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/FrogMama/Abilites/BP_FrogMama_Projectile.BP_FrogMama_Projectile_C [n: 241F14] [c: 00007FF46EEC3D38] [or: 000001F7F77B5330] [sps: 000001F756ACCE20]
[000001F7EC5E3160] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/FrogMama/Abilites/BP_GroundPulsatingProjectileFrogMama.BP_GroundPulsatingProjectileFrogMama_C [n: 1E24F7] [c: 00007FF46EEC3D38] [or: 000001F7F77B3710] [sps: 000001F4A27D5C20]
[000001F8050CA360] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/FrogMama/BP_FrogMama.BP_FrogMama_C [n: 26D773] [c: 00007FF46EEC3D38] [or: 000001F805437630] [sps: 000001F61524EAA0]
[000001F8050CA6F0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/FrogMama/BP_FrogMama_Controller.BP_FrogMama_Controller_C [n: 1B33BE] [c: 00007FF46EEC3D38] [or: 000001F805437680] [sps: 000001F61FE14330]
[000001F7FD959190] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Grisha/BP_Grisha.BP_Grisha_C [n: 2762B6] [c: 00007FF46EEC3D38] [or: 000001F80A6E2DB0] [sps: 000001F61524EAA0]
[000001F7FD95C700] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Grisha/BP_GrishaController.BP_GrishaController_C [n: 2B32E0] [c: 00007FF46EEC3D38] [or: 000001F80A6E2EA0] [sps: 000001F61FE14330]
[000001F80CB77180] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Grisha/BP_Grisha_Alien.BP_Grisha_Alien_C [n: 241F7E] [c: 00007FF46EEC3D38] [or: 000001F80A6E4200] [sps: 000001F7FD959190]
[000001F7F1C77180] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Grisha/BP_Grisha_Caged_Miniboss.BP_Grisha_Caged_Miniboss_C [n: 71F74] [c: 00007FF46EEC3D38] [or: 000001F80A6E5510] [sps: 000001F7FD959190]
[000001F7F1C746C0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Grisha/BP_Grisha_Pale.BP_Grisha_Pale_C [n: 13A4F5] [c: 00007FF46EEC3D38] [or: 000001F80A6E9200] [sps: 000001F7FD959190]
[000001F8054C34F0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/GrishaHunter/Axe/BP_GrishaHunter_Axe.BP_GrishaHunter_Axe_C [n: BD215] [c: 00007FF46EEC3D38] [or: 000001F805DD26D0] [sps: 000001F61524EAA0]
[000001F8054C3880] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/GrishaHunter/Axe/BP_GrishaHunter_Axe_Controller.BP_GrishaHunter_Axe_Controller_C [n: 1E734A] [c: 00007FF46EEC3D38] [or: 000001F805DD2860] [sps: 000001F61FE14330]
[000001F8054C2DD0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/GrishaHunter/Bow/Abilities/BP_Projectile_GrishaHunter.BP_Projectile_GrishaHunter_C [n: 135F42] [c: 00007FF46EEC3D38] [or: 000001F805DD33F0] [sps: 000001F756ACCE20]
[000001F8054CD540] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/GrishaHunter/Bow/BP_GrishaHunter_Bow.BP_GrishaHunter_Bow_C [n: 2BBC88] [c: 00007FF46EEC3D38] [or: 000001F805DD5C40] [sps: 000001F61524EAA0]
[000001F8054CD8D0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/GrishaHunter/Bow/BP_GrishaHunter_Bow_Controller.BP_GrishaHunter_Bow_Controller_C [n: 1B344C] [c: 00007FF46EEC3D38] [or: 000001F805DD4890] [sps: 000001F61FE14330]
[000001F8054C7C30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/GrishaHunter/Bow/BP_TarredArcher.BP_TarredArcher_C [n: 3FFED1] [c: 00007FF46EEC3D38] [or: 000001F805DD6140] [sps: 000001F8054CD540]
[000001F813136340] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/GrishaHunter/Spear/BP_GrishaHunter_Spear.BP_GrishaHunter_Spear_C [n: 31FC19] [c: 00007FF46EEC3D38] [or: 000001F805DD79F0] [sps: 000001F61524EAA0]
[000001F8131366D0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/GrishaHunter/Spear/BP_GrishaHunter_Spear_Controller.BP_GrishaHunter_Spear_Controller_C [n: 317329] [c: 00007FF46EEC3D38] [or: 000001F805DD7A40] [sps: 000001F61FE14330]
[000001F8131326B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/HeadTree/BP_HeadTree.BP_HeadTree_C [n: 28C601] [c: 00007FF46EEC3D38] [or: 000001F805DDD760] [sps: 000001F61524EAA0]
[000001F813133160] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/HeadTree/BP_HeadTree_Controller.BP_HeadTree_Controller_C [n: 43E81F] [c: 00007FF46EEC3D38] [or: 000001F805DDC400] [sps: 000001F61FE14330]
[000001F80A90C700] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/HutchbackCarrier/BP_Hutchback.BP_Hutchback_C [n: 1D9F5C] [c: 00007FF46EEC3D38] [or: 000001F807B1B280] [sps: 000001F61524EAA0]
[000001F80A90CA90] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/HutchbackCarrier/BP_Hutchback_Controller.BP_Hutchback_Controller_C [n: 41ADCA] [c: 00007FF46EEC3D38] [or: 000001F807B1B3C0] [sps: 000001F61FE14330]
[000001F80A90BFE0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/HutchbackCarrier/BP_Hutchback_Miniboss.BP_Hutchback_Miniboss_C [n: 7AF54] [c: 00007FF46EEC3D38] [or: 000001F807B1BC30] [sps: 000001F80A90C700]
[000001F80A90B1A0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/HutchbackCarrier/Clone/BP_HutchbackClone.BP_HutchbackClone_C [n: 2A5CCC] [c: 00007FF46EEC3D38] [or: 000001F807B1CA90] [sps: 000001F80A90C700]
[000001F80A90B530] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/HutchbackCarrier/Clone/BP_Hutchback_Clone_Controller.BP_Hutchback_Clone_Controller_C [n: 1A6252] [c: 00007FF46EEC3D38] [or: 000001F807B1BEB0] [sps: 000001F61FE14330]
[000001F810421C00] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_HeavyCultist/Abilities/BP_DamageArea_Shockwave_HeavyCultist.BP_DamageArea_Shockwave_HeavyCultist_C [n: 14FB00] [c: 00007FF46EEC3D38] [or: 000001F819B1B320] [sps: 000001F810422A40]
[000001F81042F550] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_HeavyCultist/BP_HeavyCultist.BP_HeavyCultist_C [n: 442F29] [c: 00007FF46EEC3D38] [or: 000001F81ABB01F0] [sps: 000001F61524EAA0]
[000001F81042F8E0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_HeavyCultist/BP_HeavyCultist_Controller.BP_HeavyCultist_Controller_C [n: 216DAD] [c: 00007FF46EEC3D38] [or: 000001F81ABB0240] [sps: 000001F61FE14330]
[000001F81AC97510] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_HeavyCultist/BP_HeavyCultist_MiniBoss.BP_HeavyCultist_MiniBoss_C [n: 28C61D] [c: 00007FF46EEC3D38] [or: 000001F81ABB02E0] [sps: 000001F81042F550]
[000001F81042EE30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_HeavyCultist/BP_HeavyCultist_Miniboss_Controller.BP_HeavyCultist_Miniboss_Controller_C [n: 46A3B8] [c: 00007FF46EEC3D38] [or: 000001F81ABB0F60] [sps: 000001F61FE14330]
[000001F81AC96A60] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_TwinSisters/Abilities/Crossbow/BP_Projectile_TwinSister_Crossbow.BP_Projectile_TwinSister_Crossbow_C [n: 3288D6] [c: 00007FF46EEC3D38] [or: 000001F81ABB2270] [sps: 000001F756ACCE20]
[000001F81AEE86E0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_TwinSisters/Crossbow/BP_TwinSister_Crossbow.BP_TwinSister_Crossbow_C [n: 46EA1E] [c: 00007FF46EEC3D38] [or: 000001F81ABBFBF0] [sps: 000001F61524EAA0]
[000001F81313B530] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_TwinSisters/Crossbow/BP_TwinSister_Crossbow_Controller.BP_TwinSister_Crossbow_Controller_C [n: 349E49] [c: 00007FF46EEC3D38] [or: 000001F81ABBF100] [sps: 000001F61FE14330]
[000001F81313AE10] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_TwinSisters/Daggers/BP_TwinSister_Daggers.BP_TwinSister_Daggers_C [n: 24B0E7] [c: 00007FF46EEC3D38] [or: 000001F81CC515A0] [sps: 000001F61524EAA0]
[000001F81313AA80] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_TwinSisters/Daggers/BP_TwinSister_Daggers_Controller.BP_TwinSister_Daggers_Controller_C [n: 49147E] [c: 00007FF46EEC3D38] [or: 000001F81CC51AF0] [sps: 000001F61FE14330]
[000001F81CCA6340] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_TwinSisters/SpearDagger/BP_TwinSister_SpearDagger.BP_TwinSister_SpearDagger_C [n: 3FB957] [c: 00007FF46EEC3D38] [or: 000001F81CC52EF0] [sps: 000001F61524EAA0]
[000001F81CCA66D0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_TwinSisters/SpearDagger/BP_TwinSister_SpearDagger_Controller.BP_TwinSister_SpearDagger_Controller_C [n: 2BBDAF] [c: 00007FF46EEC3D38] [or: 000001F81CC52F40] [sps: 000001F61FE14330]
[000001F81CCA5C20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MS1_TwinSisters/SpearDagger/BP_TwinSister_SpearDagger_Miniboss.BP_TwinSister_SpearDagger_Miniboss_C [n: 3A67E1] [c: 00007FF46EEC3D38] [or: 000001F81CC530D0] [sps: 000001F81CCA6340]
[000001F819CD8350] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Miner/Abilities/BP_FallRock.BP_FallRock_C [n: 1AEC20] [c: 00007FF46EEC3D38] [or: 000001F807B1D2B0] [sps: 00007FF46EE52028]
[000001F819CD7C30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Miner/Abilities/BP_RockFalling_DamageArea.BP_RockFalling_DamageArea_C [n: 283AAD] [c: 00007FF46EEC3D38] [or: 000001F819B19B60] [sps: 00007FF46EE52028]
[000001F819CDA6F0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Miner/BP_Miner.BP_Miner_C [n: 3019FB] [c: 00007FF46EEC3D38] [or: 000001F807B1D350] [sps: 000001F61524EAA0]
[000001F819CD8A70] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Miner/BP_MinerKing.BP_MinerKing_C [n: 25C5F4] [c: 00007FF46EEC3D38] [or: 000001F807B1D300] [sps: 000001F819CDA6F0]
[000001F819CDC700] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Miner/BP_Miner_Controller.BP_Miner_Controller_C [n: 3996B2] [c: 00007FF46EEC3D38] [or: 000001F807B1C450] [sps: 000001F61FE14330]
[000001F81313A6F0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MultiHeadedHarpy/Abilities/BP_MultiHeadedharpy_Scream_DamageArea.BP_MultiHeadedharpy_Scream_DamageArea_C [n: 2F8411] [c: 00007FF46EEC3D38] [or: 000001F81CC547F0] [sps: 00007FF46EE52028]
[000001F81CCA1150] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MultiHeadedHarpy/Abilities/BP_ProjectileParticle_MultiHeadedHarpy.BP_ProjectileParticle_MultiHeadedHarpy_C [n: 1DE3C6] [c: 00007FF46EEC3D38] [or: 000001F81CC546B0] [sps: 000001F76F3D0310]
[000001F63431D540] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MultiHeadedHarpy/BP_MultiHeadedHarpy.BP_MultiHeadedHarpy_C [n: 1F8CEE] [c: 00007FF46EEC3D38] [or: 000001F807DB7540] [sps: 000001F61524EAA0]
[000001F807E19FD0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/MultiHeadedHarpy/BP_MultiHeadedHarpy_Controller.BP_MultiHeadedHarpy_Controller_C [n: 1CCDD4] [c: 00007FF46EEC3D38] [or: 000001F807DB7680] [sps: 000001F61FE14330]
[000001F8083D2320] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/PapaRoach/BP_PapaRoach.BP_PapaRoach_C [n: 6D9BF] [c: 00007FF46EEC3D38] [or: 000001F8083E2F90] [sps: 000001F61524EAA0]
[000001F8083D3FA0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/PapaRoach/BP_PapaRoach_Controller.BP_PapaRoach_Controller_C [n: 2B78B4] [c: 00007FF46EEC3D38] [or: 000001F8083E2FE0] [sps: 000001F61FE14330]
[000001F8083D1C00] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/PapaRoach/BP_PapaRoach_Elite.BP_PapaRoach_Elite_C [n: 169694] [c: 00007FF46EEC3D38] [or: 000001F8083E38A0] [sps: 000001F8083D2320]
[000001F807D1EE30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ParasiteKnight/BP_ParasiteKnight.BP_ParasiteKnight_C [n: 36509B] [c: 00007FF46EEC3D38] [or: 000001F815C04A20] [sps: 000001F61524EAA0]
[000001F807D1C700] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ParasiteKnight/BP_ParasiteKnight_Controller.BP_ParasiteKnight_Controller_C [n: 4B3D61] [c: 00007FF46EEC3D38] [or: 000001F815C05010] [sps: 000001F61FE14330]
[000001F822E87180] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ParasiteKnight/BP_ParasiteKnight_Green.BP_ParasiteKnight_Green_C [n: 76643] [c: 00007FF46EEC3D38] [or: 000001F815C06C30] [sps: 000001F61524EAA0]
[000001F822E81C00] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ShellSnatcher/Abilities/BP_GrabbedShell_Dummy.BP_GrabbedShell_Dummy_C [n: 1F8D28] [c: 00007FF46EEC3D38] [or: 000001F815C068C0] [sps: 00007FF46EE52028]
[000001F8231F5890] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ShellSnatcher/BP_Shellsnatcher.BP_Shellsnatcher_C [n: 4B85A5] [c: 00007FF46EEC3D38] [or: 000001F815C0F9C0] [sps: 000001F61524EAA0]
[000001F8231F5C20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ShellSnatcher/BP_Shellsnatcher_Controller.BP_Shellsnatcher_Controller_C [n: 26D9AE] [c: 00007FF46EEC3D38] [or: 000001F815C0FA60] [sps: 000001F61FE14330]
[000001F8231F4DE0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Shepherd/Abilities/BP_AIController_Dummy_Empty.BP_AIController_Dummy_Empty_C [n: 331051] [c: 00007FF46EEC3D38] [or: 000001F80D670060] [sps: 000001F8231F5170]
[000001F8231F98B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Shepherd/Abilities/BP_Shepherd_Staff_Character.BP_Shepherd_Staff_Character_C [n: 2AEDF7] [c: 00007FF46EEC3D38] [or: 000001F80D672680] [sps: 00007FF46F03C338]
[000001F81074E710] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Shepherd/BP_Shepherd.BP_Shepherd_C [n: 41F85B] [c: 00007FF46EEC3D38] [or: 000001F80D67BD70] [sps: 000001F61524EAA0]
[000001F8107406A0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Shepherd/BP_Shepherd_Controller.BP_Shepherd_Controller_C [n: 1F4827] [c: 00007FF46EEC3D38] [or: 000001F80D679700] [sps: 000001F61FE14330]
[000001F819DA5C20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Shepherd/Sheep/BP_Sheep.BP_Sheep_C [n: 72201] [c: 00007FF46EEC3D38] [or: 000001F81077AE70] [sps: 000001F61524EAA0]
[000001F819DA2DD0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Shepherd/Sheep/BP_SheepEvil.BP_SheepEvil_C [n: 17A5D8] [c: 00007FF46EEC3D38] [or: 000001F81077A880] [sps: 000001F61524EAA0]
[000001F819DA26B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Shepherd/Sheep/BP_SheepGood.BP_SheepGood_C [n: E3C78] [c: 00007FF46EEC3D38] [or: 000001F81077B230] [sps: 000001F61524EAA0]
[000001F813137C30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Shepherd/Sheep/BP_Sheep_Controller.BP_Sheep_Controller_C [n: 7B13D] [c: 00007FF46EEC3D38] [or: 000001F810779390] [sps: 000001F61FE14330]
[000001F81A292A40] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Sicario/BP_Sicario.BP_Sicario_C [n: 18C06E] [c: 00007FF46EEC3D38] [or: 000001F81A289FC0] [sps: 000001F61524EAA0]
[000001F81A292DD0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Sicario/BP_Sicario_Controller.BP_Sicario_Controller_C [n: DAD5A] [c: 00007FF46EEC3D38] [or: 000001F81A289250] [sps: 000001F61FE14330]
[000001F81A29DC60] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Sicario/BP_Sicario_MiniBoss.BP_Sicario_MiniBoss_C [n: 83D28] [c: 00007FF46EEC3D38] [or: 000001F81A28BFF0] [sps: 000001F81A292A40]
[000001F8298A1F90] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Slug/BP_Slug.BP_Slug_C [n: 3BD404] [c: 00007FF46EEC3D38] [or: 000001F8296C9BB0] [sps: 000001F61524EAA0]
[000001F8298A5500] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Slug/BP_Slug_Controller.BP_Slug_Controller_C [n: 40914A] [c: 00007FF46EEC3D38] [or: 000001F8296C9C00] [sps: 000001F61FE14330]
[000001F8298A14E0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Slug/BP_Slug_Elite.BP_Slug_Elite_C [n: 21AE45] [c: 00007FF46EEC3D38] [or: 000001F8296CA560] [sps: 000001F8298A1F90]
[000001F81BDC26B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Snowworm/BP_Snowworm.BP_Snowworm_C [n: 283C9F] [c: 00007FF46EEC3D38] [or: 000001F81BD12400] [sps: 000001F61524EAA0]
[000001F81BDC2A40] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Snowworm/BP_Snowworm_Controller.BP_Snowworm_Controller_C [n: 30AE33] [c: 00007FF46EEC3D38] [or: 000001F81BD123B0] [sps: 000001F61FE14330]
[000001F8233D7180] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Spider/BP_Spider.BP_Spider_C [n: 2B375F] [c: 00007FF46EEC3D38] [or: 000001F81BD1E020] [sps: 000001F61524EAA0]
[000001F8233D7510] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Spider/BP_Spider_Controller.BP_Spider_Controller_C [n: 3B8A21] [c: 00007FF46EEC3D38] [or: 000001F81BD1E160] [sps: 000001F61FE14330]
[000001F8233D6340] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Spider/BP_Spider_Elite.BP_Spider_Elite_C [n: 3CAC06] [c: 00007FF46EEC3D38] [or: 000001F81BD1E890] [sps: 000001F8233D7180]
[000001F8233DE710] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/SpiderBro/Abilities/BP_Projectile_SpiderBro.BP_Projectile_SpiderBro_C [n: 3F269A] [c: 00007FF46EEC3D38] [or: 000001F81BD1EE80] [sps: 000001F756ACCE20]
[000001F8233DE380] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/SpiderBro/Abilities/BP_Projectile_Web_Grab_SpiderBro.BP_Projectile_Web_Grab_SpiderBro_C [n: 47C343] [c: 00007FF46EEC3D38] [or: 000001F81BD1EF70] [sps: 000001F756ACCE20]
[000001F8237FF550] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/SpiderBro/BP_SpiderBro.BP_SpiderBro_C [n: 246C15] [c: 00007FF46EEC3D38] [or: 000001F82360CEF0] [sps: 000001F61524EAA0]
[000001F8237FF8E0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/SpiderBro/BP_SpiderBro_Controller.BP_SpiderBro_Controller_C [n: 3B42DF] [c: 00007FF46EEC3D38] [or: 000001F82360CFE0] [sps: 000001F61FE14330]
[000001F81A137510] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/SpiderBro/BP_SpiderBro_Elite.BP_SpiderBro_Elite_C [n: 1BC1BD] [c: 00007FF46EEC3D38] [or: 000001F82360D0D0] [sps: 000001F8237FF550]
[000001F8237FEE30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/SpiderBro/BP_SpiderBro_Memory.BP_SpiderBro_Memory_C [n: 324A2D] [c: 00007FF46EEC3D38] [or: 000001F82360E840] [sps: 000001F8237FF550]
[000001F8237F5C20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/StoneCrab/BP_StoneCrab.BP_StoneCrab_C [n: 1DE5CC] [c: 00007FF46EEC3D38] [or: 000001F81D078E90] [sps: 000001F61524EAA0]
[000001F8237F5890] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/StoneCrab/BP_StoneCrab_Controller.BP_StoneCrab_Controller_C [n: 17E87F] [c: 00007FF46EEC3D38] [or: 000001F81D078030] [sps: 000001F61FE14330]
[000001F81A131870] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/StoneCrab/BP_StoneCrab_MB.BP_StoneCrab_MB_C [n: 49E5ED] [c: 00007FF46EEC3D38] [or: 000001F81D078BC0] [sps: 000001F61524EAA0]
[000001F81A131150] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/StoneCrab/BP_StoneCrab_Obsidian.BP_StoneCrab_Obsidian_C [n: 2063EB] [c: 00007FF46EEC3D38] [or: 000001F81D078940] [sps: 000001F61524EAA0]
[000001F82A9326B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredCorpse/Abilities/MiniBoss/BP_Horde_Brigand_Grenadier_Summon.BP_Horde_Brigand_Grenadier_Summon_C [n: 13EECB] [c: 00007FF46EEC3D38] [or: 000001F81D07FCE0] [sps: 000001F7DC9D5890]
[000001F82AACC370] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredCorpse/Abilities/MiniBoss/BP_TarredCorpse_MiniBoss_BlastProjectile.BP_TarredCorpse_MiniBoss_BlastProjectile_C [n: 2C9150] [c: 00007FF46EEC3D38] [or: 000001F82A983A30] [sps: 000001F756ACCE20]
[000001F82AB6B8C0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredCorpse/BP_TarredCorpse.BP_TarredCorpse_C [n: 35C19F] [c: 00007FF46EEC3D38] [or: 000001F82AC95E70] [sps: 000001F61524EAA0]
[000001F82AB6C370] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredCorpse/BP_TarredCorpse_Controller.BP_TarredCorpse_Controller_C [n: 2B7B6B] [c: 00007FF46EEC3D38] [or: 000001F82AC95650] [sps: 000001F61FE14330]
[000001F82AB6B1A0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredCorpse/BP_TarredCorpse_EggKeeper.BP_TarredCorpse_EggKeeper_C [n: 26DC09] [c: 00007FF46EEC3D38] [or: 000001F82AC96FA0] [sps: 000001F82AB6B8C0]
[000001F82AB6A6F0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredCorpse/BP_TarredCorpse_Fire.BP_TarredCorpse_Fire_C [n: 48CD11] [c: 00007FF46EEC3D38] [or: 000001F82AC96D70] [sps: 000001F82AB6B8C0]
[000001F82AB69190] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredCorpse/BP_TarredCorpse_MinibossNecromancer.BP_TarredCorpse_MinibossNecromancer_C [n: 206453] [c: 00007FF46EEC3D38] [or: 000001F82AC96A00] [sps: 000001F82AB6B8C0]
[000001F82AB6BFE0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredCorpse/BP_Tarred_Foundling.BP_Tarred_Foundling_C [n: 1C8A84] [c: 00007FF46EEC3D38] [or: 000001F82AC96050] [sps: 000001F61524EAA0]
[000001F8381EA360] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredStoner/Abilities/BP_TarredStoner_Projectile.BP_TarredStoner_Projectile_C [n: 49173D] [c: 00007FF46EEC3D38] [or: 000001F82AC977C0] [sps: 000001F4A27D5C20]
[000001F838299190] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredStoner/BP_AIController_TarredStoner.BP_AIController_TarredStoner_C [n: AFC40] [c: 00007FF46EEC3D38] [or: 000001F831067720] [sps: 000001F7B02AF550]
[000001F8393B5C20] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredStoner/BP_TarredStoner.BP_TarredStoner_C [n: 44FC12] [c: 00007FF46EEC3D38] [or: 000001F831068350] [sps: 000001F61524EAA0]
[000001F8393B7FC0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredVestige/Abilities/BP_RootProjectile.BP_RootProjectile_C [n: 3AB3E3] [c: 00007FF46EEC3D38] [or: 000001F831069250] [sps: 000001F4A27D5C20]
[000001F8394AE710] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredVestige/Abilities/BP_RootProjectile_MiniBoss.BP_RootProjectile_MiniBoss_C [n: 283D3B] [c: 00007FF46EEC3D38] [or: 000001F83106CB80] [sps: 000001F8393B7FC0]
[000001F83C3E1150] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredVestige/BP_TarredVestige.BP_TarredVestige_C [n: 32009C] [c: 00007FF46EEC3D38] [or: 000001F8394EE750] [sps: 000001F61524EAA0]
[000001F83C3E3160] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredVestige/BP_TarredVestige_Controller.BP_TarredVestige_Controller_C [n: 3B43A0] [c: 00007FF46EEC3D38] [or: 000001F8394EE930] [sps: 000001F61FE14330]
[000001F8306134F0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TarredVestige/BP_TarredVestige_MiniBoss.BP_TarredVestige_MiniBoss_C [n: 246D56] [c: 00007FF46EEC3D38] [or: 000001F8394EFCE0] [sps: 000001F83C3E1150]
[000001F8309DD540] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TheCoffin/BP_TheCoffin.BP_TheCoffin_C [n: 911FB] [c: 00007FF46EEC3D38] [or: 000001F83FD33990] [sps: 000001F61524EAA0]
[000001F8309DD8D0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TheCoffin/BP_TheCoffin_Controller.BP_TheCoffin_Controller_C [n: BD630] [c: 00007FF46EEC3D38] [or: 000001F83FD331C0] [sps: 000001F61FE14330]
[000001F8309D8350] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TheHead/BP_TheHead.BP_TheHead_C [n: 3AFC24] [c: 00007FF46EEC3D38] [or: 000001F82EFD16E0] [sps: 000001F61524EAA0]
[000001F8309D7C30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TheHead/BP_TheHeadKing.BP_TheHeadKing_C [n: 3DBDC0] [c: 00007FF46EEC3D38] [or: 000001F82EFD23B0] [sps: 000001F61524EAA0]
[000001F8237F2DD0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/TheHead/BP_TheHead_Controller.BP_TheHead_Controller_C [n: 246E68] [c: 00007FF46EEC3D38] [or: 000001F82EFD1780] [sps: 000001F61FE14330]
[000001F81D1CC370] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ToxicStalker/Abilities/BP_ToxicStalker_Death_Burst.BP_ToxicStalker_Death_Burst_C [n: 3E9E02] [c: 00007FF46EEC3D38] [or: 000001F82EFD2AE0] [sps: 00007FF46EE52028]
[000001F81D1CF550] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ToxicStalker/Abilities/BP_ToxicStalker_Fart_AOE.BP_ToxicStalker_Fart_AOE_C [n: 6DCB4] [c: 00007FF46EEC3D38] [or: 000001F82EFD5C40] [sps: 00007FF46EE52028]
[000001F825BF8A70] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ToxicStalker/Abilities/Frost/BP_ToxicStalker_Death_Burst_Frost.BP_ToxicStalker_Death_Burst_Frost_C [n: 3A6D38] [c: 00007FF46EEC3D38] [or: 000001F82EFD9980] [sps: 000001F81D1CC370]
[000001F829AE9190] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ToxicStalker/Abilities/Frost/BP_ToxicStalker_Fart_AOE_Frost.BP_ToxicStalker_Fart_AOE_Frost_C [n: 1B3A78] [c: 00007FF46EEC3D38] [or: 000001F82EFD98E0] [sps: 000001F81D1CF550]
[000001F829BDF8E0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ToxicStalker/Abilities/Hommunculus/BP_ToxicStalker_Death_Burst_Hommunculus.BP_ToxicStalker_Death_Burst_Hommunculus_C [n: 95619] [c: 00007FF46EEC3D38] [or: 000001F829A1C4A0] [sps: 000001F81D1CC370]
[000001F842B83880] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ToxicStalker/Abilities/Hommunculus/BP_ToxicStalker_Fart_AOE_Hommunculus.BP_ToxicStalker_Fart_AOE_Hommunculus_C [n: 2C4E5B] [c: 00007FF46EEC3D38] [or: 000001F842B51EB0] [sps: 000001F81D1CF550]
[000001F835606340] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ToxicStalker/BP_ToxicStalker.BP_ToxicStalker_C [n: B8F10] [c: 00007FF46EEC3D38] [or: 000001F842B5AB00] [sps: 000001F61524EAA0]
[000001F83560B8C0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ToxicStalker/BP_ToxicStalkerHomunculus.BP_ToxicStalkerHomunculus_C [n: 26DDD0] [c: 00007FF46EEC3D38] [or: 000001F842B5DD50] [sps: 000001F61524EAA0]
[000001F842B89520] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ToxicStalker/BP_ToxicStalker_Controller.BP_ToxicStalker_Controller_C [n: 16E11C] [c: 00007FF46EEC3D38] [or: 000001F842B59480] [sps: 000001F61FE14330]
[000001F835600A30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/ToxicStalker/BP_ToxicStalker_Frost.BP_ToxicStalker_Frost_C [n: 19501C] [c: 00007FF46EEC3D38] [or: 000001F842B5C900] [sps: 000001F61524EAA0]
[000001F8437B46C0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Turrets/LarvaTurret/BP_TurretLivingBase.BP_TurretLivingBase_C [n: 3FBE7E] [c: 00007FF46EEC3D38] [or: 000001F843760830] [sps: 000001F61524EAA0]
[000001F8437B0DC0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Turrets/LarvaTurret/BP_TurretLivingBase_PairTest.BP_TurretLivingBase_PairTest_C [n: 48CF19] [c: 00007FF46EEC3D38] [or: 000001F843762A90] [sps: 000001F61524EAA0]
[000001F8437B4330] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Turrets/LarvaTurret/BP_TurretLiving_Poison.BP_TurretLiving_Poison_C [n: 495C97] [c: 00007FF46EEC3D38] [or: 000001F842B5F7E0] [sps: 000001F8437B46C0]
[000001F835607C30] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Turrets/LarvaTurret/Projectiles/BP_LivingTurretHomingProjectile.BP_LivingTurretHomingProjectile_C [n: 129586] [c: 00007FF46EEC3D38] [or: 000001F843760A10] [sps: 000001F4A27D5C20]
[000001F8437B5170] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Turrets/LarvaTurret/Projectiles/BP_LivingTurretPoisonHomingProjectile.BP_LivingTurretPoisonHomingProjectile_C [n: 276984] [c: 00007FF46EEC3D38] [or: 000001F8437609C0] [sps: 000001F835607C30]
[000001F8437B78A0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Vampire/BP_Vampire.BP_Vampire_C [n: 1DE791] [c: 00007FF46EEC3D38] [or: 000001F843767D10] [sps: 000001F61524EAA0]
[000001F843F65500] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Vampire/BP_VampireKnight.BP_VampireKnight_C [n: 491851] [c: 00007FF46EEC3D38] [or: 000001F84376ABF0] [sps: 000001F61524EAA0]
[000001F843F64A50] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Vampire/BP_VampireKnight_Part2.BP_VampireKnight_Part2_C [n: 41243A] [c: 00007FF46EEC3D38] [or: 000001F84376A560] [sps: 000001F61524EAA0]
[000001F8437B9520] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Vampire/BP_Vampire_Controller.BP_Vampire_Controller_C [n: F10A3] [c: 00007FF46EEC3D38] [or: 000001F843767F40] [sps: 000001F61FE14330]
[000001F843F65FB0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Vampire/BP_Vampire_Cultist.BP_Vampire_Cultist_C [n: 1DE798] [c: 00007FF46EEC3D38] [or: 000001F84376A2E0] [sps: 000001F8437B78A0]
[000001F843F65890] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Vampire/BP_Vampire_MiniBoss_Controller.BP_Vampire_MiniBoss_Controller_C [n: 2A61FB] [c: 00007FF46EEC3D38] [or: 000001F84376A100] [sps: 000001F61FE14330]
[000001F7498F7FC0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Wraith/Abilties/GE_EnemySpecialActivation.GE_EnemySpecialActivation_C [n: 19963B] [c: 00007FF46EEC3D38] [or: 000001F76F38C180] [sps: 00007FF46F048738]
[000001F845B3DFF0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Wraith/BP_Wraith.BP_Wraith_C [n: 154243] [c: 00007FF46EEC3D38] [or: 000001F84594F920] [sps: 000001F61524EAA0]
[000001F8237F1870] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Wraith/BP_Wraith_Controller.BP_Wraith_Controller_C [n: 31C1E1] [c: 00007FF46EEC3D38] [or: 000001F84594F9C0] [sps: 000001F61FE14330]
[000001F845B3D8D0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Wraith/BP_Wraith_Dark.BP_Wraith_Dark_C [n: 72500] [c: 00007FF46EEC3D38] [or: 000001F84B160D30] [sps: 000001F845B3DFF0]
[000001F845B3D1B0] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/Wraith/BP_Wraith_MiniBoss.BP_Wraith_MiniBoss_C [n: 290E77] [c: 00007FF46EEC3D38] [or: 000001F84B160B50] [sps: 000001F845B3D8D0]
[000001F4C131AA80] BlueprintGeneratedClass /Game/Sparta/Characters/Enemies/_Shared/ABPI_Enemy.ABPI_Enemy_C [n: 46E178] [c: 00007FF46EEC3D38] [or: 000001F4A2D5D210] [sps: 00007FF46EE52A30]
```

# Appendix B — verbatim dump lines for the boss BP classes

`scripts\q\eb-21-ev-bosses.sql`, 54 lines (every `BlueprintGeneratedClass` under
`/Game/Sparta/Characters/Bosses/` except `GA_*`).

```
[000001F76D327180] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/BigBottle/BP_BigBottleController.BP_BigBottleController_C [n: 47B490] [c: 00007FF46EEC3D38] [or: 000001F763867F90] [sps: 000001F61FE14330]
[000001F769579520] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/BigBottle/BP_BigBottle_Ghost.BP_BigBottle_Ghost_C [n: A1B34] [c: 00007FF46EEC3D38] [or: 000001F76386B730] [sps: 000001F620EE6340]
[000001F76F537510] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Dwarf/Abilities/BP_ProjectileParticle_DwarfBoss_HeadExplosion.BP_ProjectileParticle_DwarfBoss_HeadExplosion_C [n: 13554A] [c: 00007FF46EEC3D38] [or: 000001F776F1E430] [sps: 000001F76F3D0310]
[000001F77FF0D8D0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Dwarf/BP_DwarfBoss.BP_DwarfBoss_C [n: 23CF76] [c: 00007FF46EEC3D38] [or: 000001F4938E0920] [sps: 000001F61524EAA0]
[000001F77FF026B0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Dwarf/BP_DwarfBoss_Controller.BP_DwarfBoss_Controller_C [n: 43DCD3] [c: 00007FF46EEC3D38] [or: 000001F77FC7F8D0] [sps: 000001F61FE14330]
[000001F77FF07C30] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Dwarf/Cinematic/Intro/CIN_DwarfBoss_01.CIN_DwarfBoss_01_DirectorBP_C [n: 245C3E] [c: 00007FF46EEC3D38] [or: 000001F4938E38A0] [sps: 00007FF46EECCB38]
[000001F77FF02A40] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/FinalBoss/Abilities/BP_FinalBoss_FireRing.BP_FinalBoss_FireRing_C [n: 181FB2] [c: 00007FF46EEC3D38] [or: 000001F4938E9C00] [sps: 000001F4CB24EAA0]
[000001F4A27D5890] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/FinalBoss/Abilities/BP_Projectile_FinalBoss_FireBreathCollision.BP_Projectile_FinalBoss_FireBreathCollision_C [n: 3FAC2C] [c: 00007FF46EEC3D38] [or: 000001F4938E9B10] [sps: 000001F4A27D5C20]
[000001F4A27D4A50] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/FinalBoss/Abilities/BP_Projectile_FinalBoss_FireSpitCollider.BP_Projectile_FinalBoss_FireSpitCollider_C [n: EBB04] [c: 00007FF46EEC3D38] [or: 000001F4938EAAB0] [sps: 000001F756ACCE20]
[000001F4A9415500] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/FinalBoss/Abilities/GE_ModifyCriticalHitChanceInstantForOneHit.GE_ModifyCriticalHitChanceInstantForOneHit_C [n: 200A08] [c: 00007FF46EEC3D38] [or: 000001F4A707E430] [sps: 00007FF46F048738]
[000001F4AB6D9190] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/FinalBoss/BP_FinalBoss.BP_FinalBoss_C [n: 356D16] [c: 00007FF46EEC3D38] [or: 000001F4AB597A90] [sps: 000001F61524EAA0]
[000001F4AB6DAA80] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/FinalBoss/BP_FinalBoss_Controller.BP_FinalBoss_Controller_C [n: 1C3BEB] [c: 00007FF46EEC3D38] [or: 000001F4AB596730] [sps: 000001F61FE14330]
[000001F4ACB44330] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/FinalBoss/Cinematic/Intro/LS_FinalBoss_Intro.LS_FinalBoss_Intro_DirectorBP_C [n: 109B2E] [c: 00007FF46EEC3D38] [or: 000001F4AB59DD00] [sps: 00007FF46EECCB38]
[000001F76F53F1C0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/HeadBoss/Abilities/BP_HeadBossDamageArea.BP_HeadBossDamageArea_C [n: 10E266] [c: 00007FF46EEC3D38] [or: 000001F4ADCA88F0] [sps: 00007FF46EE52028]
[000001F4ADE25500] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/HeadBoss/Abilities/BP_HeadBossDamageAreaSucking.BP_HeadBossDamageAreaSucking_C [n: 38AD10] [c: 00007FF46EEC3D38] [or: 000001F4ADCA8620] [sps: 00007FF46EE52028]
[000001F4ADE2B8C0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/HeadBoss/Abilities/BP_ProjectileParticle_HeadBoss.BP_ProjectileParticle_HeadBoss_C [n: F4365] [c: 00007FF46EEC3D38] [or: 000001F4ADCACE00] [sps: 000001F76F3D0310]
[000001F4ACB48A70] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/HeadBoss/Abilities/BP_Projectile_HeadBoss_Poop.BP_Projectile_HeadBoss_Poop_C [n: 38287F] [c: 00007FF46EEC3D38] [or: 000001F4ADCAAE70] [sps: 000001F756ACCE20]
[000001F4AEF2A360] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/HeadBoss/BP_HeadBoss.BP_HeadBoss_C [n: 46507A] [c: 00007FF46EEC3D38] [or: 000001F4AE1B52E0] [sps: 000001F61524EAA0]
[000001F4AEF2A6F0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/HeadBoss/BP_HeadBossController.BP_HeadBossController_C [n: 356D4B] [c: 00007FF46EEC3D38] [or: 000001F4AE1B5600] [sps: 000001F61FE14330]
[000001F4ADE2EE30] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Hexapod/Abilities/BP_HexapodLurkGroundAttack.BP_HexapodLurkGroundAttack_C [n: 31F1A9] [c: 00007FF46EEC3D38] [or: 000001F4AF30A6A0] [sps: 00007FF46EE52028]
[000001F4AFB0DC60] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Hexapod/Abilities/BP_Hexapod_Projectile_BigSlow.BP_Hexapod_Projectile_BigSlow_C [n: 344ED8] [c: 00007FF46EEC3D38] [or: 000001F4AF30AC40] [sps: 000001F4A27D5C20]
[000001F4AFCD78A0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Hexapod/BP_Hexapod.BP_Hexapod_C [n: F8767] [c: 00007FF46EEC3D38] [or: 000001F4B0983490] [sps: 000001F61524EAA0]
[000001F4B375AE10] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Hexapod/BP_HexapodController.BP_HexapodController_C [n: 12CA68] [c: 00007FF46EEC3D38] [or: 000001F4B0983990] [sps: 000001F61FE14330]
[000001F4B3915500] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Hexapod/BP_HexapodController_Memory.BP_HexapodController_Memory_C [n: 3058C0] [c: 00007FF46EEC3D38] [or: 000001F4B3646050] [sps: 000001F61FE14330]
[000001F4B033B530] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Hexapod/HexapodGrabPayload.HexapodGrabPayload_C [n: B7DB5] [c: 00007FF46EEC3D38] [or: 000001F4B0984CA0] [sps: 00007FF46EFA1338]
[000001F4B3915170] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/LadyOfTheWoods/Abilities/BP_LadyOfTheWoods_FireTrailCollider.BP_LadyOfTheWoods_FireTrailCollider_C [n: 1F3A51] [c: 00007FF46EEC3D38] [or: 000001F4B3647CC0] [sps: 00007FF46EE52028]
[000001F4B47FE380] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/LadyOfTheWoods/Abilities/GE_LadOfTheWoods_SummonBabies.GE_LadOfTheWoods_SummonBabies_C [n: 14AC8D] [c: 00007FF46EEC3D38] [or: 000001F4B4760560] [sps: 00007FF46F048738]
[000001F4A5B9A6F0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/LadyOfTheWoods/BP_LadyOfTheWoods.BP_LadyOfTheWoods_C [n: 100E12] [c: 00007FF46EEC3D38] [or: 000001F4B364A740] [sps: 000001F61524EAA0]
[000001F4A5B9EE30] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/LadyOfTheWoods/BP_LadyOfTheWoodsController.BP_LadyOfTheWoodsController_C [n: E3291] [c: 00007FF46EEC3D38] [or: 000001F4B3649BB0] [sps: 000001F61FE14330]
[000001F4A5B98350] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/LadyOfTheWoods/Projectiles/BP_BabyShieldProjectile.BP_BabyShieldProjectile_C [n: 23D1E2] [c: 00007FF46EEC3D38] [or: 000001F4B364E570] [sps: 000001F4A27D5C20]
[000001F4A5B93880] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/LadyOfTheWoods/Projectiles/BP_Projectile_LadyOfTheWoods_Baby.BP_Projectile_LadyOfTheWoods_Baby_C [n: 131404] [c: 00007FF46EEC3D38] [or: 000001F4B36493E0] [sps: 000001F4A27D5C20]
[000001F4AD64E380] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Monolith/AI/BP_Monolith.BP_Monolith_C [n: 35F9AE] [c: 00007FF46EEC3D38] [or: 000001F4AC5FE840] [sps: 000001F61524EAA0]
[000001F4AD64BFE0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Monolith/AI/BP_Monolith_Controller.BP_Monolith_Controller_C [n: 2F7A81] [c: 00007FF46EEC3D38] [or: 000001F4AC5FE890] [sps: 000001F61FE14330]
[000001F4B79306A0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Monolith/Cinematic/Intro/LevelSequence_Monolith_Intro.LevelSequence_Monolith_Intro_DirectorBP_C [n: 100E71] [c: 00007FF46EEC3D38] [or: 000001F4B5E6ED40] [sps: 00007FF46EECCB38]
[000001F4B8ACCE20] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/MothKnight/AI/BP_MothKnight.BP_MothKnight_C [n: 469947] [c: 00007FF46EEC3D38] [or: 000001F4B4112C20] [sps: 000001F61524EAA0]
[000001F4B8ACD1B0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/MothKnight/AI/BP_MothKnightController.BP_MothKnightController_C [n: 24A7B0] [c: 00007FF46EEC3D38] [or: 000001F4B4112B80] [sps: 000001F61FE14330]
[000001F4B402F8E0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/MothKnight/AI/GameplayEffects/GE_MothKnight_Shield_BreakDamageReduction.GE_MothKnight_Shield_BreakDamageReduction_C [n: 245FD8] [c: 00007FF46EEC3D38] [or: 000001F4B4112A90] [sps: 00007FF46F048738]
[000001F4B402C370] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/MothKnight/AI/GameplayEffects/GE_MothKnight_Shield_DamageReduction_Melee.GE_MothKnight_Shield_DamageReduction_Melee_C [n: 10E4B4] [c: 00007FF46EEC3D38] [or: 000001F4B4111D70] [sps: 00007FF46F048738]
[000001F4B402B8C0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/MothKnight/AI/GameplayEffects/GE_MothKnight_Shield_DamageReduction_Ranged.GE_MothKnight_Shield_DamageReduction_Ranged_C [n: 3BC855] [c: 00007FF46EEC3D38] [or: 000001F4B4111D20] [sps: 00007FF46F048738]
[000001F4B2530DC0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Offspring/Abilities/BP_Projectile_Offsrping.BP_Projectile_Offsrping_C [n: 27A44B] [c: 00007FF46EEC3D38] [or: 000001F4B411AF60] [sps: 000001F756ACCE20]
[000001F4B8D59190] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Offspring/BP_Offspring.BP_Offspring_C [n: 1DDA26] [c: 00007FF46EEC3D38] [or: 000001F4B6CC4340] [sps: 000001F61524EAA0]
[000001F4B8D59C40] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Offspring/BP_Offspring_Controller.BP_Offspring_Controller_C [n: 6CF43] [c: 00007FF46EEC3D38] [or: 000001F4B6CC43E0] [sps: 000001F61FE14330]
[000001F4B9186340] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/ParasiteGolem/Abilities/BP_ParasiteGolem_Projectile.BP_ParasiteGolem_Projectile_C [n: 1F3C49] [c: 00007FF46EEC3D38] [or: 000001F4B6CCA600] [sps: 000001F756ACCE20]
[000001F4BA82EE30] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/ParasiteGolem/Art/Cinematic/BP_ParasiteGolem_Cinematic.BP_ParasiteGolem_Cinematic_C [n: 47FAAD] [c: 00007FF46EEC3D38] [or: 000001F4B91BFF10] [sps: 000001F61524EAA0]
[000001F4BBD0CE20] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/ParasiteGolem/BP_ParasiteGolem.BP_ParasiteGolem_C [n: 1DDAB2] [c: 00007FF46EEC3D38] [or: 000001F4BBD60AB0] [sps: 000001F61524EAA0]
[000001F4BBD0D1B0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/ParasiteGolem/BP_ParasiteGolem_Controller.BP_ParasiteGolem_Controller_C [n: 33067A] [c: 00007FF46EEC3D38] [or: 000001F4BBD60BF0] [sps: 000001F61FE14330]
[000001F4BE06A360] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Swordman/Abilities/BP_SwordmanParasiteRootGroundAttack.BP_SwordmanParasiteRootGroundAttack_C [n: 398BE3] [c: 00007FF46EEC3D38] [or: 000001F4BE4E0F60] [sps: 00007FF46EE52028]
[000001F4C0FA06A0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Swordman/BP_Swordman.BP_Swordman_C [n: 457C9A] [c: 00007FF46EEC3D38] [or: 000001F4BE4EB780] [sps: 000001F61524EAA0]
[000001F4C0FAD1B0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Swordman/BP_Swordman_Controller.BP_Swordman_Controller_C [n: 222962] [c: 00007FF46EEC3D38] [or: 000001F4BE4EA6A0] [sps: 000001F61FE14330]
[000001F4C2D22DD0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Swordman/Cinematic/Intro/CIN_Swordman_Intro_01.CIN_Swordman_Intro_01_DirectorBP_C [n: 38FB27] [c: 00007FF46EEC3D38] [or: 000001F4C2840740] [sps: 00007FF46EECCB38]
[000001F74146DC60] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/TarGolem/BP_TarGolem.BP_TarGolem_C [n: 1E1E7A] [c: 00007FF46EEC3D38] [or: 000001F61C262630] [sps: 000001F61524EAA0]
[000001F61C277180] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/TarGolem/BP_TarGolemController.BP_TarGolemController_C [n: 47FB5A] [c: 00007FF46EEC3D38] [or: 000001F61C261780] [sps: 000001F61FE14330]
[000001F4C3A914E0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/TarGolem/BP_TarGolemParasiteController.BP_TarGolemParasiteController_C [n: 1E1E82] [c: 00007FF46EEC3D38] [or: 000001F4C32E8490] [sps: 000001F61FE14330]
[000001F4C3A91150] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/TarGolem/BP_TarGolem_Headless.BP_TarGolem_Headless_C [n: 2301DE] [c: 00007FF46EEC3D38] [or: 000001F4C32E8350] [sps: 000001F61524EAA0]
```

# Appendix C — verbatim dump lines for the native AI / spawn / damage classes

Named classes only (`eb-22-ev-native.sql`).

```
[00007FF46F02CD38] Class /Script/Sparta.AIDirectorSubsystem [n: 4751A] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7BF38]
[00007FF46F02E138] Class /Script/Sparta.BreakExecutionModifier [n: 47627] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F02E338]
[00007FF46F031138] Class /Script/Sparta.DamageExecutionModifier [n: 47774] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F02E338]
[00007FF46F033538] Class /Script/Sparta.ExecutionModifierCalculation [n: 4794D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F02E338] Class /Script/Sparta.ExecutionValueModifier [n: 47633] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F033D38] Class /Script/Sparta.HealExecutionModifier [n: 47993] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F02E338]
[00007FF46F033F38] Class /Script/Sparta.HitPayloadModifier [n: 4799F] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F042B38] Class /Script/Sparta.HordeCharacterData [n: 48BD2] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F042938]
[00007FF46F034138] Class /Script/Sparta.HordeClusterCenter [n: 479A9] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
[00007FF46F034338] Class /Script/Sparta.HordeSpawnerComponent [n: 479BA] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F034538] Class /Script/Sparta.HordeUnitComponent [n: 479D5] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFDBB38]
[00007FF46F039138] Class /Script/Sparta.PoiseExecutionModifier [n: 47DC1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F02E338]
[00007FF46F03B138] Class /Script/Sparta.SoftTargetingComponent [n: 4805A] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F03AF38] Class /Script/Sparta.SoftTargetingSettings [n: 4804E] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F03C338] Class /Script/Sparta.SpartaAICharacter [n: 48368] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F03C538]
[00007FF46F03C738] Class /Script/Sparta.SpartaAIController [n: 48510] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F006138]
[00007FF46F03C938] Class /Script/Sparta.SpartaAIPerceptionSystem [n: 4855D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFC0B38]
[00007FF46F03D938] Class /Script/Sparta.SpartaAnimNotifyState_HitCheck [n: 486C1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF9FB38]
[00007FF46F03E338] Class /Script/Sparta.SpartaAnimNotify_HitCheck [n: 4871F] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF9F938]
[00007FF46F041D38] Class /Script/Sparta.SpartaBreakExecution [n: 48B1D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F041F38]
[00007FF46F03C538] Class /Script/Sparta.SpartaCharacter [n: 48372] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F008F38]
[00007FF46F034938] Class /Script/Sparta.SpartaDamageCauser [n: 479EE] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52A30]
[00007FF46F044F38] Class /Script/Sparta.SpartaDamageDebugSubsystem [n: 48F72] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7C138]
[00007FF46F045138] Class /Script/Sparta.SpartaDamageExecution [n: 48F8B] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F041F38]
[00007FF46F045D38] Class /Script/Sparta.SpartaDeathAnimationSet [n: 48FF0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[00007FF46F045738] Class /Script/Sparta.SpartaDeathCondition [n: 48FC1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F045B38] Class /Script/Sparta.SpartaDeathCondition_ClosestBone [n: 48FDF] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F045738]
[00007FF46F045938] Class /Script/Sparta.SpartaDeathCondition_StrikeDirection [n: 48FCC] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F045738]
[00007FF46F046538] Class /Script/Sparta.SpartaEncounterManager [n: 490C0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
[00007FF46F061938] Class /Script/Sparta.SpartaEncounterRecipe [n: 4AFC7] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[00007FF46F046938] Class /Script/Sparta.SpartaEnemySubsystem [n: 490E4] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7BF38]
[00007FF46F041F38] Class /Script/Sparta.SpartaExecutionCalculation [n: 48B28] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF9BB38]
[00007FF46F048F38] Class /Script/Sparta.SpartaHealExecution [n: 493D3] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F041F38]
[00007FF46F066538] Class /Script/Sparta.SpartaHealthComponent [n: 4BBC8] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFDB338]
[00007FF46F065D38] Class /Script/Sparta.SpartaHealthSet [n: 4BB8D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F031538]
[00007FF46F049138] Class /Script/Sparta.SpartaHitApplier [n: 493DE] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52A30]
[00007FF46F049338] Class /Script/Sparta.SpartaHitPayload [n: 493E7] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F049538] Class /Script/Sparta.SpartaHitReactionAnimSet [n: 494D0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[00007FF46F049938] Class /Script/Sparta.SpartaHitTrace [n: 49506] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F049B38] Class /Script/Sparta.SpartaHitTraceComponent [n: 49587] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F049D38] Class /Script/Sparta.SpartaHitTraceShape [n: 49602] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F04A338] Class /Script/Sparta.SpartaHitTraceShape_Capsule [n: 49633] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F049D38]
[00007FF46F049F38] Class /Script/Sparta.SpartaHitTraceShape_Line [n: 4960D] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F049D38]
[00007FF46F04A138] Class /Script/Sparta.SpartaHitTraceShape_Sphere [n: 4961F] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F049D38]
[00007FF46F04A538] Class /Script/Sparta.SpartaHitTraceShapes_Box [n: 49649] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F049D38]
[00007FF46F04A738] Class /Script/Sparta.SpartaHitTraceSubsystem [n: 49656] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7BF38]
[00007FF46F049738] Class /Script/Sparta.SpartaHittable [n: 494FE] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52A30]
[00007FF46F04AD38] Class /Script/Sparta.SpartaHordeEncounter [n: 496DD] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
[00007FF46F04AF38] Class /Script/Sparta.SpartaHordeSpawner [n: 496E8] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F04B138]
[00007FF46F04F938] Class /Script/Sparta.SpartaMassActorSpawnerSubsystem [n: 49A97] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F029F38]
[00007FF46F042738] Class /Script/Sparta.SpartaPhysicalHitReactionData [n: 48B62] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEDA938]
[00007FF46F055F38] Class /Script/Sparta.SpartaPoiseExecution [n: 4A114] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F041F38]
[00007FF46F057538] Class /Script/Sparta.SpartaSaveInterface [n: 4A2B0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52A30]
[00007FF46F05B538] Class /Script/Sparta.SpartaTargetLockComponent [n: 4A902] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]
[00007FF46F05AB38] Class /Script/Sparta.SpartaTargetableComponent [n: 4A897] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEC2338]
[00007FF46F05AD38] Class /Script/Sparta.SpartaTargetableInterface [n: 4A8AD] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52A30]
[00007FF46F05AF38] Class /Script/Sparta.SpartaTargeter [n: 4A8BB] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F05B338] Class /Script/Sparta.SpartaTargeter_FrontalCone [n: 4A8F4] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F05AF38]
[00007FF46F05B138] Class /Script/Sparta.SpartaTargeter_UseOwner [n: 4A8E7] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F05AF38]
[00007FF46F060138] Class /Script/Sparta.SpartaWindedExecution [n: 4AEF2] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F041F38]
[00007FF46F061B38] Class /Script/Sparta.SpawnTrigger [n: 4AFD3] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
[00007FF46F062738] Class /Script/Sparta.TargetSelectionComponent [n: 4B099] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EFDB138]
[00007FF46F04B138] Class /Script/Sparta.UnitSpawner [n: 496F2] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE52028]
[00007FF46F033738] Class /Script/Sparta.WindedDamageBonus [n: 4795C] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F033538]
[00007FF46F063F38] Class /Script/Sparta.WindedExecutionModifier [n: 4B2F7] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F02E338]
[00007FF46F033938] Class /Script/Sparta.WindedPoiseDamageBonus [n: 47966] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F033538]
```

# Appendix D — verbatim dump lines for the damage-path functions

Every `Function` object named in §6.1–§6.4, plus their parameter rows for the three most important
signatures (`SpartaApplyHit`, `SpartaApplyHit` params, `USpartaHitPayload::ApplyPayload`).

```
[00007FF46F6E2A18] Function /Script/Sparta.AIDirectorSubsystem:EnableGlobalCooldown [n: 47525] [c: 00007FF46EE55538] [or: 00007FF46F02CD38] [f: 00007FF71C7C4A60]
[00007FF46F6E2AF8] Function /Script/Sparta.AIDirectorSubsystem:GetAIDirector [n: 47530] [c: 00007FF46EE55538] [or: 00007FF46F02CD38] [f: 00007FF71C7C5030]
[00007FF46F6E2BD8] Function /Script/Sparta.AIDirectorSubsystem:IsAbilityOnGlobalCooldown [n: 47538] [c: 00007FF46EE55538] [or: 00007FF46F02CD38] [f: 00007FF71C7C5160]
[00007FF46F6E3818] Function /Script/Sparta.BreakExecutionModifier:ApplyBreakExecutionModifier [n: 1360C] [c: 00007FF46EE55538] [or: 00007FF46F02E138] [f: 00007FF71C7C46E0]
[00007FF46F6E3B98] Function /Script/Sparta.DamageExecutionModifier:ApplyDamageExecutionModifier [n: 13634] [c: 00007FF46EE55538] [or: 00007FF46F031138] [f: 00007FF71C7C9390]
[00007FF46F6E3C78] Function /Script/Sparta.DamageExecutionModifier:ApplyPostDamageReductionModifier [n: 13643] [c: 00007FF46EE55538] [or: 00007FF46F031138] [f: 00007FF71C7C96D0]
[00007FF46F6E3738] Function /Script/Sparta.ExecutionValueModifier:CanApplyExecutionModifier [n: 13692] [c: 00007FF46EE55538] [or: 00007FF46F02E338] [f: 00007FF71C7CFF60]
[00007FF46F6E5958] Function /Script/Sparta.HealExecutionModifier:ApplyHealExecutionModifier [n: 136A0] [c: 00007FF46EE55538] [or: 00007FF46F033D38] [f: 00007FF71C7C96D0]
[00007FF46F6E5A38] Function /Script/Sparta.HitPayloadModifier:ApplyPayloadModifier [n: 136AE] [c: 00007FF46EE55538] [or: 00007FF46F033F38] [f: 00007FF71C7CFCC0]
[00007FF46F6E5B18] Function /Script/Sparta.HitPayloadModifier:CanApplyPayloadModifier [n: 136B9] [c: 00007FF46EE55538] [or: 00007FF46F033F38] [f: 00007FF71C7D0130]
[00007FF46F6E5BF8] Function /Script/Sparta.HordeClusterCenter:OnUnitDeath [n: 479B3] [c: 00007FF46EE55538] [or: 00007FF46F034138] [f: 00007FF71C7D0B10]
[00007FF46F3D4F48] Function /Script/Sparta.HordeSpawnerComponent:CharacterSpawned [n: 14768] [c: 00007FF46EE55538] [or: 00007FF46F034338] [f: 00007FF71C7D0360]
[00007FF46F3D5028] Function /Script/Sparta.HordeSpawnerComponent:OnEnemyDeath [n: 479C6] [c: 00007FF46EE55538] [or: 00007FF46F034338] [f: 00007FF71C7D0A80]
[00007FF46F3D51E8] Function /Script/Sparta.HordeSpawnerComponent:SetEnemyTarget [n: 479CD] [c: 00007FF46EE55538] [or: 00007FF46F034338] [f: 00007FF71C7D0BC0]
[00007FF46F3D52C8] Function /Script/Sparta.HordeSpawnerComponent:StartEncounter [n: 146BB] [c: 00007FF46EE55538] [or: 00007FF46F034338] [f: 00007FF71C7D0D20]
[00007FF46F6E5CD8] Function /Script/Sparta.HordeUnitComponent:OnDeath [n: 479DF] [c: 00007FF46EE55538] [or: 00007FF46F034538] [f: 00007FF71C7D09F0]
[00007FF46F6E9F58] Function /Script/Sparta.PoiseExecutionModifier:ApplyPoiseExecutionModifier [n: 137BE] [c: 00007FF46EE55538] [or: 00007FF46F039138] [f: 00007FF71C7C96D0]
[00007FF46F6ECB18] Function /Script/Sparta.SoftTargetingComponent:GetCameraForwardVector [n: 48066] [c: 00007FF46EE55538] [or: 00007FF46F03B138] [f: 00007FF71C7E17C0]
[00007FF46F6ECBF8] Function /Script/Sparta.SoftTargetingComponent:GetScoreBonusAndMulitplier [n: 1381E] [c: 00007FF46EE55538] [or: 00007FF46F03B138] [f: 00007FF7183D9710]
[00007FF46F6ECCD8] Function /Script/Sparta.SoftTargetingComponent:RunMultiSphereTrace [n: 48072] [c: 00007FF46EE55538] [or: 00007FF46F03B138] [f: 00007FF71C7E2040]
[00007FF46F6ECDB8] Function /Script/Sparta.SoftTargetingComponent:RunScoring [n: 4807D] [c: 00007FF46EE55538] [or: 00007FF46F03B138] [f: 00007FF71C7E21B0]
[00007FF46F6ECE98] Function /Script/Sparta.SoftTargetingComponent:SphereOverlapComplete [n: 48083] [c: 00007FF46EE55538] [or: 00007FF46F03B138] [f: 00007FF71C7E2CA0]
[00007FF46F6ECF78] Function /Script/Sparta.SoftTargetingComponent:UpdateMovementDirection [n: 1382C] [c: 00007FF46EE55538] [or: 00007FF46F03B138] [f: 00007FF7183D9710]
[00007FF46F6ED058] Function /Script/Sparta.SoftTargetingComponent:UpdateTargetingVector [n: 4808F] [c: 00007FF46EE55538] [or: 00007FF46F03B138] [f: 00007FF71C7E2DA0]
[00007FF46F6EF978] Function /Script/Sparta.SpartaAICharacter:BeginPlay [n: 484EC] [c: 00007FF46EE55538] [or: 00007FF46F03C338] [f: 00007FF71C7E7850]
[00007FF46F6EFA58] Function /Script/Sparta.SpartaAICharacter:IsSpawnFinished [n: 13886] [c: 00007FF46EE55538] [or: 00007FF46F03C338] [f: 00007FF71C7E91B0]
[00007FF46F6EFB38] Function /Script/Sparta.SpartaAICharacter:OnAggro [n: 484F2] [c: 00007FF46EE55538] [or: 00007FF46F03C338] [f: 00007FF71C7E9320]
[00007FF46F6EFC18] Function /Script/Sparta.SpartaAICharacter:OnDeaggro [n: 484F7] [c: 00007FF46EE55538] [or: 00007FF46F03C338] [f: 00007FF71C7E93E0]
[00007FF46F6EFCF8] Function /Script/Sparta.SpartaAICharacter:OnDeath [n: 479DF] [c: 00007FF46EE55538] [or: 00007FF46F03C338] [f: 00007FF71C7E9480]
[00007FF46F6EFDD8] Function /Script/Sparta.SpartaAICharacter:OnPreAggro [n: 484FD] [c: 00007FF46EE55538] [or: 00007FF46F03C338] [f: 00007FF71C7E9800]
[00007FF46F6EFEB8] Function /Script/Sparta.SpartaAICharacter:SpawnFromSpawnerFinished [n: 48503] [c: 00007FF46EE55538] [or: 00007FF46F03C338] [f: 00007FF71C7EA1A0]
[00007FF46F6EFF98] Function /Script/Sparta.SpartaAIController:ChangeSightRadius [n: 4851A] [c: 00007FF46EE55538] [or: 00007FF46F03C738] [f: 00007FF71C7E7870]
[00007FF46F6F0078] Function /Script/Sparta.SpartaAIController:DisableAvoidance [n: 48524] [c: 00007FF46EE55538] [or: 00007FF46F03C738] [f: 00007FF71C7E78F0]
[00007FF46F6F0158] Function /Script/Sparta.SpartaAIController:GetDynamicSubtree [n: 4852D] [c: 00007FF46EE55538] [or: 00007FF46F03C738] [f: 00007FF71C7E7F40]
[00007FF46F6F0238] Function /Script/Sparta.SpartaAIController:HandleAggro [n: 48537] [c: 00007FF46EE55538] [or: 00007FF46F03C738] [f: 00007FF71C7E8B30]
[00007FF46F6F0318] Function /Script/Sparta.SpartaAIController:HandleDeaggro [n: 4853E] [c: 00007FF46EE55538] [or: 00007FF46F03C738] [f: 00007FF71C7E8BD0]
[00007FF46F6F03F8] Function /Script/Sparta.SpartaAIController:HandlePreAggro [n: 48546] [c: 00007FF46EE55538] [or: 00007FF46F03C738] [f: 00007FF71C7E8C70]
[00007FF46F6F04D8] Function /Script/Sparta.SpartaAIController:OnPawnDeath [n: 4854E] [c: 00007FF46EE55538] [or: 00007FF46F03C738] [f: 00007FF71C7E9630]
[00007FF46F6F05B8] Function /Script/Sparta.SpartaAIController:OnPlayerDeath [n: 48555] [c: 00007FF46EE55538] [or: 00007FF46F03C738] [f: 00007FF71C7E96C0]
[00007FF46F6F0698] Function /Script/Sparta.SpartaAIController:SetDynamicSubtree [n: 3E408] [c: 00007FF46EE55538] [or: 00007FF46F03C738] [f: 00007FF71C7E9F80]
[00007FF46F6F1D58] Function /Script/Sparta.SpartaAnimNotifyState_HitCheck:GetTraceProfileOptions [n: 486D1] [c: 00007FF46EE55538] [or: 00007FF46F03D938] [f: 00007FF71C7ED840]
[00007FF46F6F1E38] Function /Script/Sparta.SpartaAnimNotifyState_HitCheck:Replace [n: 16F7F] [c: 00007FF46EE55538] [or: 00007FF46F03D938] [f: 00007FF71C7EDE90]
[00007FF46F6F1F18] Function /Script/Sparta.SpartaAnimNotify_HitCheck:GetTraceProfileOptions [n: 486D1] [c: 00007FF46EE55538] [or: 00007FF46F03E338] [f: 00007FF71C7ED840]
[00007FF46F3C9C90] Function /Script/Sparta.SpartaCharacter:GetCurrentTarget [n: 48396] [c: 00007FF46EE55538] [or: 00007FF46F03C538] [f: 00007FF71C7F3660]
[00007FF46F3CA3F8] Function /Script/Sparta.SpartaCharacter:GetTeamRelationship [n: 483F2] [c: 00007FF46EE55538] [or: 00007FF46F03C538] [f: 00007FF71C7F3910]
[00007FF46F3CAE78] Function /Script/Sparta.SpartaCharacter:OnDeathStarted [n: 48451] [c: 00007FF46EE55538] [or: 00007FF46F03C538] [f: 00007FF71C7F4250]
[00007FF46F3CBEF8] Function /Script/Sparta.SpartaCharacter:SetCurrentTarget [n: 4849E] [c: 00007FF46EE55538] [or: 00007FF46F03C538] [f: 00007FF71C7F5AA0]
[00007FF46F3CC278] Function /Script/Sparta.SpartaCharacter:SetTeam [n: 484BA] [c: 00007FF46EE55538] [or: 00007FF46F03C538] [f: 00007FF71C7F5D50]
[00007FF46F6E5DB8] Function /Script/Sparta.SpartaDamageCauser:GetDamageTags [n: 136DE] [c: 00007FF46EE55538] [or: 00007FF46F034938] [f: 00007FF71C7D0840]
[00007FF46F6FB058] Function /Script/Sparta.SpartaDamageDebugSubsystem:HandleDamageReceived [n: 48F80] [c: 00007FF46EE55538] [or: 00007FF46F044F38] [f: 00007FF71C806A10]
[00007FF46F6FB3D8] Function /Script/Sparta.SpartaDeathAnimationSet:GetDeathAnimation [n: 48FFD] [c: 00007FF46EE55538] [or: 00007FF46F045D38] [f: 00007FF71C806120]
[00007FF46F6FB2F8] Function /Script/Sparta.SpartaDeathCondition:Evaluate [n: 8CCA] [c: 00007FF46EE55538] [or: 00007FF46F045738] [f: 00007FF71C805D40]
[00007FF46F3CD938] Function /Script/Sparta.SpartaEncounterManager:OnEnemyDeath [n: 479C6] [c: 00007FF46EE55538] [or: 00007FF46F046538] [f: 00007FF71C806DA0]
[00007FF46F3CDA18] Function /Script/Sparta.SpartaEncounterManager:OnHordeEncounterCompleted [n: 490CC] [c: 00007FF46EE55538] [or: 00007FF46F046538] [f: 00007FF71C806FE0]
[00007FF46F6FC1D8] Function /Script/Sparta.SpartaEnemySubsystem:CacheEnemyHealthWidget [n: 490EF] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C805B00]
[00007FF46F6FC2B8] Function /Script/Sparta.SpartaEnemySubsystem:CacheWidgetFromComponent [n: 490FB] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C805B90]
[00007FF46F6FC398] Function /Script/Sparta.SpartaEnemySubsystem:GetSpawnedEnemiesInDist [n: 49108] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C806830]
[00007FF46F6FC478] Function /Script/Sparta.SpartaEnemySubsystem:OnCharacterAddedToWorld [n: 49115] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C806BB0]
[00007FF46F6FC558] Function /Script/Sparta.SpartaEnemySubsystem:OnCharacterDeath [n: 13B74] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C806C40]
[00007FF46F6FC638] Function /Script/Sparta.SpartaEnemySubsystem:OnCharacterDestroyed [n: 49122] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C806CD0]
[00007FF46F6FC718] Function /Script/Sparta.SpartaEnemySubsystem:OnEngagedEnemyDeaggro [n: 4912D] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C806E30]
[00007FF46F6FC7F8] Function /Script/Sparta.SpartaEnemySubsystem:OnEngagedEnemyTargetChanged [n: 49139] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C806ED0]
[00007FF46F6FC8D8] Function /Script/Sparta.SpartaEnemySubsystem:OnPlayerTargetedBy [n: 49148] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C807070]
[00007FF46F6FC9B8] Function /Script/Sparta.SpartaEnemySubsystem:PopEnemyHealthWidget [n: 49152] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C807100]
[00007FF46F6FCA98] Function /Script/Sparta.SpartaEnemySubsystem:WorldBeginTeardown [n: 4915D] [c: 00007FF46EE55538] [or: 00007FF46F046938] [f: 00007FF71C807520]
[00007FF46F72F078] Function /Script/Sparta.SpartaHealthComponent:FindHealthComponent [n: 4BBD4] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D64D0]
[00007FF46F72F158] Function /Script/Sparta.SpartaHealthComponent:GetCurrentHealthNormalized [n: 4BBDF] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D65A0]
[00007FF46F72F238] Function /Script/Sparta.SpartaHealthComponent:GetCurrentMaxHealth [n: 4BBED] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D6640]
[00007FF46F72F318] Function /Script/Sparta.SpartaHealthComponent:GetDeathState [n: 4BBF8] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D66F0]
[00007FF46F72F3F8] Function /Script/Sparta.SpartaHealthComponent:GetHealth [n: 4BC00] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D6710]
[00007FF46F72F4D8] Function /Script/Sparta.SpartaHealthComponent:GetHealthNormalized [n: 4BC06] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D6760]
[00007FF46F72F5B8] Function /Script/Sparta.SpartaHealthComponent:GetMaxHealth [n: 4BC11] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D6790]
[00007FF46F72F698] Function /Script/Sparta.SpartaHealthComponent:GetMaxShellHealth [n: 4BC18] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D67E0]
[00007FF46F72F778] Function /Script/Sparta.SpartaHealthComponent:GetShellHealth [n: 4BC22] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D6830]
[00007FF46F72F858] Function /Script/Sparta.SpartaHealthComponent:GetShellHealthNormalized [n: 4BC2A] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D6880]
[00007FF46F72F938] Function /Script/Sparta.SpartaHealthComponent:InitializeWithAbilitySystem [n: 4BC37] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D68B0]
[00007FF46F72FA18] Function /Script/Sparta.SpartaHealthComponent:IsDeadOrDying [n: 4BC46] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D6940]
[00007FF46F72FAF8] Function /Script/Sparta.SpartaHealthComponent:OnRep_DeathState [n: 4BC4E] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D6970]
[00007FF46F72FBD8] Function /Script/Sparta.SpartaHealthComponent:StartDeath [n: 4BC57] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71A732EC0]
[00007FF46F72FCB8] Function /Script/Sparta.SpartaHealthComponent:UninitializeFromAbilitySystem [n: 4BC5D] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71C8D69F0]
[00007FF46F72EEB8] Function /Script/Sparta.SpartaHealthSet:OnRep_Health [n: 4BB96] [c: 00007FF46EE55538] [or: 00007FF46F065D38] [f: 00007FF71C88FFF0]
[00007FF46F72EF98] Function /Script/Sparta.SpartaHealthSet:OnRep_MaxHealth [n: 4BB9D] [c: 00007FF46EE55538] [or: 00007FF46F065D38] [f: 00007FF71C890170]
[00007FF46F6FF818] Function /Script/Sparta.SpartaHitApplier:SpartaHitApplied [n: 13CF7] [c: 00007FF46EE55538] [or: 00007FF46F049138] [f: 00007FF71C80E230]
[00007FF46F6FF8F8] Function /Script/Sparta.SpartaHitPayload:ApplyPayload [n: 13D00] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80B410]
[00007FF46F6FF9D8] Function /Script/Sparta.SpartaHitPayload:ApplyPayloadInstigator [n: 13D07] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80B5F0]
[00007FF46F6FFAB8] Function /Script/Sparta.SpartaHitPayload:CalculateBreakDamage [n: 13D13] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80B9B0]
[00007FF46F6FFB98] Function /Script/Sparta.SpartaHitPayload:CalculateDamage [n: 13D1E] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80BA80]
[00007FF46F6FFC78] Function /Script/Sparta.SpartaHitPayload:CalculatePoiseDamage [n: 13D27] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80BB50]
[00007FF46F6FFD58] Function /Script/Sparta.SpartaHitPayload:CreateGameplayEffectContextHandle [n: 493F0] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80BFC0]
[00007FF46F6FFE38] Function /Script/Sparta.SpartaHitPayload:GetDuplicate [n: 49402] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80C5A0]
[00007FF46F6FFF18] Function /Script/Sparta.SpartaHitPayload:MergePayloads [n: 49409] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80D180]
[00007FF46F6FFFF8] Function /Script/Sparta.SpartaHitPayload:SetAdditionalEffects [n: 49411] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80D590]
[00007FF46F7000D8] Function /Script/Sparta.SpartaHitPayload:SetBreakDamage [n: 4941C] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80D690]
[00007FF46F7001B8] Function /Script/Sparta.SpartaHitPayload:SetBreakDamageDataTag [n: 49424] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80D710]
[00007FF46F700298] Function /Script/Sparta.SpartaHitPayload:SetBreakDamageEffect [n: 49430] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80D7A0]
[00007FF46F700378] Function /Script/Sparta.SpartaHitPayload:SetBreakDamageOption [n: 4943B] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80D850]
[00007FF46F700458] Function /Script/Sparta.SpartaHitPayload:SetChosenReactionTag [n: 49446] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80D8F0]
[00007FF46F700538] Function /Script/Sparta.SpartaHitPayload:SetCustomElementalStacks [n: 49451] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80D980]
[00007FF46F700618] Function /Script/Sparta.SpartaHitPayload:SetDamageDataTag [n: 4945E] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80DA10]
[00007FF46F7006F8] Function /Script/Sparta.SpartaHitPayload:SetDamageDealt [n: 49467] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80DAA0]
[00007FF46F7007D8] Function /Script/Sparta.SpartaHitPayload:SetDamageEffect [n: 4946F] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80DB20]
[00007FF46F7008B8] Function /Script/Sparta.SpartaHitPayload:SetHealthDamage [n: 49478] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80DBD0]
[00007FF46F700998] Function /Script/Sparta.SpartaHitPayload:SetPayloadTags [n: 49481] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80DC70]
[00007FF46F700A78] Function /Script/Sparta.SpartaHitPayload:SetPoiseBrokenEffect [n: 49489] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80DD50]
[00007FF46F700B58] Function /Script/Sparta.SpartaHitPayload:SetPoiseDamage [n: 49494] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80DE00]
[00007FF46F700C38] Function /Script/Sparta.SpartaHitPayload:SetPoiseDamageDataTag [n: 4949C] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80DE80]
[00007FF46F700D18] Function /Script/Sparta.SpartaHitPayload:SetPoiseDamageEffect [n: 494A8] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80DF10]
[00007FF46F700DF8] Function /Script/Sparta.SpartaHitPayload:SetPoiseDamageOption [n: 494B3] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80DFC0]
[00007FF46F700ED8] Function /Script/Sparta.SpartaHitPayload:SetReactionTag [n: 494BE] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80E060]
[00007FF46F700FB8] Function /Script/Sparta.SpartaHitPayload:SetStrikeDirection [n: 494C6] [c: 00007FF46EE55538] [or: 00007FF46F049338] [f: 00007FF71C80E180]
[00007FF46F701098] Function /Script/Sparta.SpartaHitReactionAnimSet:GetHitReactionAnimation [n: 494DD] [c: 00007FF46EE55538] [or: 00007FF46F049538] [f: 00007FF71C812E20]
[00007FF46F701178] Function /Script/Sparta.SpartaHitReactionAnimSet:HasReaction [n: 494EA] [c: 00007FF46EE55538] [or: 00007FF46F049538] [f: 00007FF71C813850]
[00007FF46F701258] Function /Script/Sparta.SpartaHitReactionAnimSet:HasReactionForDirection [n: 494F1] [c: 00007FF46EE55538] [or: 00007FF46F049538] [f: 00007FF71C8138F0]
[00007FF46F7014F8] Function /Script/Sparta.SpartaHitTrace:AddTraceShape [n: 4950E] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C8127B0]
[00007FF46F7015D8] Function /Script/Sparta.SpartaHitTrace:BeginContinuousCheck [n: 49516] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C8129E0]
[00007FF46F7016B8] Function /Script/Sparta.SpartaHitTrace:CheckOnce [n: 49521] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C812A20]
[00007FF46F701798] Function /Script/Sparta.SpartaHitTrace:CreateHitTrace [n: 49527] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C812A80]
[00007FF46F701878] Function /Script/Sparta.SpartaHitTrace:EndContinuousCheck [n: 4952F] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C812D70]
[00007FF46F701958] Function /Script/Sparta.SpartaHitTrace:GetHitTraceProfileOptions [n: 49539] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C8132B0]
[00007FF46F701A38] Function /Script/Sparta.SpartaHitTrace:GetSource [n: 49547] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C813610]
[00007FF46F701B18] Function /Script/Sparta.SpartaHitTrace:GetSourceCharacter [n: 4954D] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C813640]
[00007FF46F701BF8] Function /Script/Sparta.SpartaHitTrace:GetTraceData [n: 49557] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C8136A0]
[00007FF46F701CD8] Function /Script/Sparta.SpartaHitTrace:GetTraceDataAsType [n: 4955E] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C813750]
[00007FF46F701DB8] Function /Script/Sparta.SpartaHitTrace:SetSource [n: 49568] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C813FE0]
[00007FF46F701E98] Function /Script/Sparta.SpartaHitTrace:SetTraceData [n: 4956E] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C814140]
[00007FF46F701F78] Function /Script/Sparta.SpartaHitTrace:SetTracePrimitives [n: 49575] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C814220]
[00007FF46F702058] Function /Script/Sparta.SpartaHitTrace:SetTraceShapes [n: 4957F] [c: 00007FF46EE55538] [or: 00007FF46F049938] [f: 00007FF71C8142D0]
[00007FF46F702138] Function /Script/Sparta.SpartaHitTraceComponent:DisableHitProcessing [n: 49594] [c: 00007FF46EE55538] [or: 00007FF46F049B38] [f: 00007FF71C812D30]
[00007FF46F702218] Function /Script/Sparta.SpartaHitTraceComponent:EnableHitProcessing [n: 4959F] [c: 00007FF46EE55538] [or: 00007FF46F049B38] [f: 00007FF71C812D50]
[00007FF46F7022F8] Function /Script/Sparta.SpartaHitTraceComponent:EndContinuousTraceWithContext [n: 495AA] [c: 00007FF46EE55538] [or: 00007FF46F049B38] [f: 00007FF71C812D90]
[00007FF46F7023D8] Function /Script/Sparta.SpartaHitTraceComponent:IsHitProcessingEnabled [n: 495BA] [c: 00007FF46EE55538] [or: 00007FF46F049B38] [f: 00007FF71C8139C0]
[00007FF46F7024B8] Function /Script/Sparta.SpartaHitTraceComponent:PerformTrace [n: 495C6] [c: 00007FF46EE55538] [or: 00007FF46F049B38] [f: 00007FF71C813EC0]
[00007FF46F702598] Function /Script/Sparta.SpartaHitTraceComponent:StartContinuousTrace [n: 495CD] [c: 00007FF46EE55538] [or: 00007FF46F049B38] [f: 00007FF71C814560]
[00007FF46F702678] Function /Script/Sparta.SpartaHitTraceComponent:StartContinuousTraceWithContext [n: 495D8] [c: 00007FF46EE55538] [or: 00007FF46F049B38] [f: 00007FF71C8145F0]
[00007FF46F702758] Function /Script/Sparta.SpartaHitTraceComponent:StopContinuousTrace [n: 495E9] [c: 00007FF46EE55538] [or: 00007FF46F049B38] [f: 00007FF71C8146C0]
[00007FF46F702838] Function /Script/Sparta.SpartaHitTraceComponent:UnbindOnTraceHitFromObject [n: 495F4] [c: 00007FF46EE55538] [or: 00007FF46F049B38] [f: 00007FF71C814760]
[00007FF46F702AD8] Function /Script/Sparta.SpartaHitTraceShape_Capsule:MakeCapsule [n: 49642] [c: 00007FF46EE55538] [or: 00007FF46F04A338] [f: 00007FF71C813B40]
[00007FF46F702918] Function /Script/Sparta.SpartaHitTraceShape_Line:MakeLine [n: 4961A] [c: 00007FF46EE55538] [or: 00007FF46F049F38] [f: 00007FF71C813CC0]
[00007FF46F7029F8] Function /Script/Sparta.SpartaHitTraceShape_Sphere:MakeSphere [n: 4962D] [c: 00007FF46EE55538] [or: 00007FF46F04A138] [f: 00007FF71C813D50]
[00007FF46F702BB8] Function /Script/Sparta.SpartaHitTraceShapes_Box:MakeBox [n: 3541D] [c: 00007FF46EE55538] [or: 00007FF46F04A538] [f: 00007FF71C8139F0]
[00007FF46F701338] Function /Script/Sparta.SpartaHittable:GetHitResponse [n: 13D89] [c: 00007FF46EE55538] [or: 00007FF46F049738] [f: 00007FF71C813160]
[00007FF46F701418] Function /Script/Sparta.SpartaHittable:SpartaApplyHit [n: 13D91] [c: 00007FF46EE55538] [or: 00007FF46F049738] [f: 00007FF71C814380]
[00007FF46F3CDBB8] Function /Script/Sparta.SpartaHordeEncounter:OnEnemyDeath [n: 479C6] [c: 00007FF46EE55538] [or: 00007FF46F04AD38] [f: 00007FF71C819850]
[00007FF46F3CDD78] Function /Script/Sparta.SpartaHordeEncounter:SetEnemyTarget [n: 479CD] [c: 00007FF46EE55538] [or: 00007FF46F04AD38] [f: 00007FF71C8199B0]
[00007FF46F3CDE58] Function /Script/Sparta.SpartaHordeEncounter:StartEncounter [n: 146BB] [c: 00007FF46EE55538] [or: 00007FF46F04AD38] [f: 00007FF71C819BA0]
[00007FF46F703E18] Function /Script/Sparta.SpartaHordeSpawner:Spawn [n: 13E04] [c: 00007FF46EE55538] [or: 00007FF46F04AF38] [f: 00007FF71C819B70]
[00007FF46F705D98] Function /Script/Sparta.SpartaLevelScript:OnEncounterFinished [n: 13F42] [c: 00007FF46EE55538] [or: 00007FF46F04E538] [f: 00007FF71A677BD0]
[00007FF46F705E78] Function /Script/Sparta.SpartaLevelScript:OnEncounterStarted [n: 139F8] [c: 00007FF46EE55538] [or: 00007FF46F04E538] [f: 00007FF71C81D260]
[00007FF46F6F6198] Function /Script/Sparta.SpartaPhysicalHitReactionData:FindClosestBoneChainToHitLocation [n: 48B72] [c: 00007FF46EE55538] [or: 00007FF46F042738] [f: 00007FF71C7FB550]
[00007FF46F6F6278] Function /Script/Sparta.SpartaPhysicalHitReactionData:GetAllRootBones [n: 48B84] [c: 00007FF46EE55538] [or: 00007FF46F042738] [f: 00007FF71C7FB720]
[00007FF46F6F6358] Function /Script/Sparta.SpartaPhysicalHitReactionData:GetFirstWeightedBoneInChain [n: 48B8D] [c: 00007FF46EE55538] [or: 00007FF46F042738] [f: 00007FF71C7FB890]
[00007FF46F6F6438] Function /Script/Sparta.SpartaPhysicalHitReactionData:GetImpulseBonesInChain [n: 48B9C] [c: 00007FF46EE55538] [or: 00007FF46F042738] [f: 00007FF71C7FB9F0]
[00007FF46F6F6518] Function /Script/Sparta.SpartaPhysicalHitReactionData:GetLastWeightedBoneInChain [n: 48BA8] [c: 00007FF46EE55538] [or: 00007FF46F042738] [f: 00007FF71C7FBAD0]
[00007FF46F6F65F8] Function /Script/Sparta.SpartaPhysicalHitReactionData:GetRootBonesInChain [n: 48BB6] [c: 00007FF46EE55538] [or: 00007FF46F042738] [f: 00007FF71C7FBBB0]
[00007FF46F6F66D8] Function /Script/Sparta.SpartaPhysicalHitReactionData:HasBoneInChain [n: 48BC1] [c: 00007FF46EE55538] [or: 00007FF46F042738] [f: 00007FF71C7FBEC0]
[00007FF46F70EFB8] Function /Script/Sparta.SpartaSaveInterface:SaveLoaded [n: 14280] [c: 00007FF46EE55538] [or: 00007FF46F057538] [f: 00007FF71C835DE0]
[00007FF46F70F098] Function /Script/Sparta.SpartaSaveInterface:SessionReset [n: 14286] [c: 00007FF46EE55538] [or: 00007FF46F057538] [f: 00007FF71BBDCE60]
[00007FF46F70F178] Function /Script/Sparta.SpartaSaveInterface:SpartaPreSave [n: 1428D] [c: 00007FF46EE55538] [or: 00007FF46F057538] [f: 00007FF71C836210]
[00007FF46F715298] Function /Script/Sparta.SpartaTargetLockComponent:LockedTargetChanged [n: 4A910] [c: 00007FF46EE55538] [or: 00007FF46F05B538] [f: 00007FF71C83E3C0]
[00007FF46F715378] Function /Script/Sparta.SpartaTargetLockComponent:ShouldMoveTowardsTarget [n: 14348] [c: 00007FF46EE55538] [or: 00007FF46F05B538] [f: 00007FF71C83F440]
[00007FF46F715458] Function /Script/Sparta.SpartaTargetLockComponent:SwitchTarget [n: 4A91B] [c: 00007FF46EE55538] [or: 00007FF46F05B538] [f: 00007FF71C83F5B0]
[00007FF46F715538] Function /Script/Sparta.SpartaTargetLockComponent:SwitchTargetWithInputDirection [n: 4A922] [c: 00007FF46EE55538] [or: 00007FF46F05B538] [f: 00007FF71C83F6D0]
[00007FF46F715618] Function /Script/Sparta.SpartaTargetLockComponent:TargetSpecificCharacter [n: 14355] [c: 00007FF46EE55538] [or: 00007FF46F05B538] [f: 00007FF71B2BB3D0]
[00007FF46F7156F8] Function /Script/Sparta.SpartaTargetLockComponent:ToggleTargetLock [n: 4A932] [c: 00007FF46EE55538] [or: 00007FF46F05B538] [f: 00007FF71C83F800]
[00007FF46F7157D8] Function /Script/Sparta.SpartaTargetLockComponent:TransformVectorToTargetLockSpace [n: 4A93B] [c: 00007FF46EE55538] [or: 00007FF46F05B538] [f: 00007FF71C83F8B0]
[00007FF46F714658] Function /Script/Sparta.SpartaTargetableComponent:GetIsOnScreen [n: 4A8A5] [c: 00007FF46EE55538] [or: 00007FF46F05AB38] [f: 00007FF71C83DC00]
[00007FF46F714C78] Function /Script/Sparta.SpartaTargeter:ApplyTargetingOnce [n: 14325] [c: 00007FF46EE55538] [or: 00007FF46F05AF38] [f: 00007FF71C83D6E0]
[00007FF46F714D58] Function /Script/Sparta.SpartaTargeter:BeginContinuousTargeting [n: 1432F] [c: 00007FF46EE55538] [or: 00007FF46F05AF38] [f: 00007FF7192DAF00]
[00007FF46F714E38] Function /Script/Sparta.SpartaTargeter:ConsiderTarget [n: 4A8C3] [c: 00007FF46EE55538] [or: 00007FF46F05AF38] [f: 00007FF71C83D840]
[00007FF46F714F18] Function /Script/Sparta.SpartaTargeter:DoInstantTargeting [n: 4A8CB] [c: 00007FF46EE55538] [or: 00007FF46F05AF38] [f: 00007FF71C83DAB0]
[00007FF46F714FF8] Function /Script/Sparta.SpartaTargeter:EndContinuousTargeting [n: 1433C] [c: 00007FF46EE55538] [or: 00007FF46F05AF38] [f: 00007FF71A5EB240]
[00007FF46F7150D8] Function /Script/Sparta.SpartaTargeter:IsValidTarget [n: 4A8D5] [c: 00007FF46EE55538] [or: 00007FF46F05AF38] [f: 00007FF71C83E320]
[00007FF46F7151B8] Function /Script/Sparta.SpartaTargeter:TryGetSingleTarget [n: 4A8DD] [c: 00007FF46EE55538] [or: 00007FF46F05AF38] [f: 00007FF71C83F970]
[00007FF46F71F7F8] Function /Script/Sparta.SpawnTrigger:AssetsLoaded [n: 4AFDA] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71C850E30]
[00007FF46F71F8D8] Function /Script/Sparta.SpawnTrigger:ClearLoadedClasses [n: 49703] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71C8510B0]
[00007FF46F71F9B8] Function /Script/Sparta.SpawnTrigger:EndEncounter [n: 14698] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71A6C3F80]
[00007FF46F71FA98] Function /Script/Sparta.SpawnTrigger:LoadEnemyClasses [n: 4AFE1] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71C8518B0]
[00007FF46F71FB78] Function /Script/Sparta.SpawnTrigger:OnDifficultyChanged [n: 4AFEA] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71C851A70]
[00007FF46F71FC58] Function /Script/Sparta.SpawnTrigger:SaveEncounterState [n: 4AFF5] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71C851CA0]
[00007FF46F71FD38] Function /Script/Sparta.SpawnTrigger:ShouldLoadEnemyClassesOnBeginPlay [n: 1469F] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71C851F20]
[00007FF46F71FE18] Function /Script/Sparta.SpawnTrigger:SpawnTriggerLoaded [n: 146B1] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71A6C3FA0]
[00007FF46F71FEF8] Function /Script/Sparta.SpawnTrigger:StartEncounter [n: 146BB] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71A6C4FC0]
[00007FF46F71FFD8] Function /Script/Sparta.SpawnTrigger:StartSpawning [n: 146C3] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71AFB5540]
[00007FF46F7200B8] Function /Script/Sparta.SpawnTrigger:ValidateSpawners [n: 4AFFF] [c: 00007FF46EE55538] [or: 00007FF46F061B38] [f: 00007FF71C851F80]
[00007FF46F720EB8] Function /Script/Sparta.TargetSelectionComponent:AddTagModifier [n: 4B0A6] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C8549B0]
[00007FF46F720F98] Function /Script/Sparta.TargetSelectionComponent:BlockSwitching [n: 4B0AE] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C854F40]
[00007FF46F721078] Function /Script/Sparta.TargetSelectionComponent:ClearCurrentTarget [n: 4B0B6] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C8550B0]
[00007FF46F721158] Function /Script/Sparta.TargetSelectionComponent:ClearTagModifiers [n: 4B0C0] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C8550F0]
[00007FF46F721238] Function /Script/Sparta.TargetSelectionComponent:FindTargetWithPath [n: 4B0CA] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C855440]
[00007FF46F721318] Function /Script/Sparta.TargetSelectionComponent:ForgetAll [n: 3EE31] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C8556F0]
[00007FF46F7213F8] Function /Script/Sparta.TargetSelectionComponent:GetTagModifier [n: 4B0D4] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C8557F0]
[00007FF46F7214D8] Function /Script/Sparta.TargetSelectionComponent:OnActorPerceptionUpdated [n: 4B0DC] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C8558F0]
[00007FF46F7215B8] Function /Script/Sparta.TargetSelectionComponent:OnOwnerPossess [n: 4B0E9] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C855D80]
[00007FF46F721698] Function /Script/Sparta.TargetSelectionComponent:OnTargetDeath [n: 4B0F1] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C855E50]
[00007FF46F721778] Function /Script/Sparta.TargetSelectionComponent:OnTargetTeamChanged [n: 4B0F9] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C855EE0]
[00007FF46F721858] Function /Script/Sparta.TargetSelectionComponent:OnTeamChanged [n: 4B104] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C855FF0]
[00007FF46F721938] Function /Script/Sparta.TargetSelectionComponent:RecalculateTarget [n: 4B10C] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C856220]
[00007FF46F721A18] Function /Script/Sparta.TargetSelectionComponent:RemoveTagModifier [n: 4B116] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C856360]
[00007FF46F721AF8] Function /Script/Sparta.TargetSelectionComponent:RemoveTargetOverride [n: 4B120] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C856400]
[00007FF46F721BD8] Function /Script/Sparta.TargetSelectionComponent:SetTargetOverride [n: 4B12B] [c: 00007FF46EE55538] [or: 00007FF46F062738] [f: 00007FF71C8564B0]
[00007FF46F703718] Function /Script/Sparta.UnitSpawner:BeginSpawningEnemy [n: 496F9] [c: 00007FF46EE55538] [or: 00007FF46F04B138] [f: 00007FF71C854D70]
[00007FF46F7037F8] Function /Script/Sparta.UnitSpawner:CanSpawn [n: 14763] [c: 00007FF46EE55538] [or: 00007FF46F04B138] [f: 00007FF71C855080]
[00007FF46F7038D8] Function /Script/Sparta.UnitSpawner:CharacterSpawned [n: 14768] [c: 00007FF46EE55538] [or: 00007FF46F04B138] [f: 00007FF71A6C4FC0]
[00007FF46F7039B8] Function /Script/Sparta.UnitSpawner:ClearLoadedClasses [n: 49703] [c: 00007FF46EE55538] [or: 00007FF46F04B138] [f: 00007FF71C8550D0]
[00007FF46F703A98] Function /Script/Sparta.UnitSpawner:FinishSpawningEnemy [n: 4970D] [c: 00007FF46EE55538] [or: 00007FF46F04B138] [f: 00007FF71C855470]
[00007FF46F703B78] Function /Script/Sparta.UnitSpawner:LoadClasses [n: 49718] [c: 00007FF46EE55538] [or: 00007FF46F04B138] [f: 00007FF71C8558D0]
[00007FF46F703D38] Function /Script/Sparta.UnitSpawner:SpawnMe [n: 14779] [c: 00007FF46EE55538] [or: 00007FF46F04B138] [f: 00007FF71AE45430]
[00007FF46F703C58] Function /Script/Sparta.UnitSpawner:SpawnerLoaded [n: 14771] [c: 00007FF46EE55538] [or: 00007FF46F04B138] [f: 00007FF71A677BD0]
[00007FF46F724258] Function /Script/Sparta.WindedExecutionModifier:ApplyWindedExecutionModifier [n: 14789] [c: 00007FF46EE55538] [or: 00007FF46F063F38] [f: 00007FF71C7C96D0]
```
