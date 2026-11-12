-- EV lines: named native AI / spawn / targeting / damage-path classes
SELECT '[' || addr || '] ' || type || ' ' || path || COALESCE(rest,'')
FROM obj
WHERE type = 'Class'
  AND path IN (
    '/Script/Sparta.SpartaAICharacter','/Script/Sparta.SpartaAIController',
    '/Script/Sparta.SpartaEnemySubsystem','/Script/Sparta.UnitSpawner',
    '/Script/Sparta.SpartaHordeSpawner','/Script/Sparta.HordeSpawnerComponent',
    '/Script/Sparta.HordeUnitComponent','/Script/Sparta.HordeCharacterData',
    '/Script/Sparta.HordeClusterCenter','/Script/Sparta.SpartaEncounterManager',
    '/Script/Sparta.SpartaHordeEncounter','/Script/Sparta.SpartaEncounterRecipe',
    '/Script/Sparta.SpawnTrigger','/Script/Sparta.SpartaMassActorSpawnerSubsystem',
    '/Script/Sparta.SpartaHittable','/Script/Sparta.SpartaHitApplier',
    '/Script/Sparta.SpartaDamageCauser','/Script/Sparta.SpartaHitPayload',
    '/Script/Sparta.SpartaHitTrace','/Script/Sparta.SpartaHitTraceComponent',
    '/Script/Sparta.SpartaHitTraceSubsystem','/Script/Sparta.SpartaHitTraceShape',
    '/Script/Sparta.SpartaHitTraceShape_Capsule','/Script/Sparta.SpartaHitTraceShape_Line',
    '/Script/Sparta.SpartaHitTraceShape_Sphere','/Script/Sparta.SpartaHitTraceShapes_Box',
    '/Script/Sparta.SpartaExecutionCalculation','/Script/Sparta.SpartaDamageExecution',
    '/Script/Sparta.SpartaBreakExecution','/Script/Sparta.SpartaPoiseExecution',
    '/Script/Sparta.SpartaHealExecution','/Script/Sparta.SpartaWindedExecution',
    '/Script/Sparta.ExecutionValueModifier','/Script/Sparta.ExecutionModifierCalculation',
    '/Script/Sparta.DamageExecutionModifier','/Script/Sparta.BreakExecutionModifier',
    '/Script/Sparta.PoiseExecutionModifier','/Script/Sparta.HealExecutionModifier',
    '/Script/Sparta.WindedExecutionModifier','/Script/Sparta.HitPayloadModifier',
    '/Script/Sparta.WindedDamageBonus','/Script/Sparta.WindedPoiseDamageBonus',
    '/Script/Sparta.SpartaDeathCondition','/Script/Sparta.SpartaDeathCondition_ClosestBone',
    '/Script/Sparta.SpartaDeathCondition_StrikeDirection','/Script/Sparta.SpartaDeathAnimationSet',
    '/Script/Sparta.SpartaHitReactionAnimSet','/Script/Sparta.SpartaPhysicalHitReactionData',
    '/Script/Sparta.SpartaHealthComponent','/Script/Sparta.SpartaHealthSet',
    '/Script/Sparta.SpartaAnimNotify_HitCheck','/Script/Sparta.SpartaAnimNotifyState_HitCheck',
    '/Script/Sparta.SpartaDamageDebugSubsystem','/Script/Sparta.SpartaCharacter',
    '/Script/Sparta.AIDirectorSubsystem','/Script/Sparta.SpartaAIPerceptionSystem',
    '/Script/Sparta.TargetSelectionComponent','/Script/Sparta.SpartaTargetLockComponent',
    '/Script/Sparta.SpartaTargeter','/Script/Sparta.SpartaTargeter_FrontalCone',
    '/Script/Sparta.SpartaTargeter_UseOwner','/Script/Sparta.SpartaTargetableComponent',
    '/Script/Sparta.SpartaTargetableInterface','/Script/Sparta.SoftTargetingComponent',
    '/Script/Sparta.SoftTargetingSettings','/Script/Sparta.SpartaSaveInterface'
  )
ORDER BY path

