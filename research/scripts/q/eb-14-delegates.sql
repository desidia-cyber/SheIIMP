-- Delegate properties (game events) on the AI / damage / spawn classes, with offsets
SELECT oc.path || ' | ' || o.path || ' | ' || o.type || ' | ' || COALESCE(o.off,'')
FROM obj o JOIN obj oc ON oc.addr = o.owner
WHERE o.type IN ('MulticastInlineDelegateProperty','MulticastSparseDelegateProperty','DelegateProperty')
  AND ( oc.path LIKE '/Script/Sparta.SpartaAICharacter' OR oc.path LIKE '/Script/Sparta.SpartaAIController'
     OR oc.path LIKE '/Script/Sparta.SpartaHealthComponent' OR oc.path LIKE '/Script/Sparta.SpartaEnemySubsystem'
     OR oc.path LIKE '/Script/Sparta.SpartaHitTraceComponent' OR oc.path LIKE '/Script/Sparta.TargetSelectionComponent'
     OR oc.path LIKE '/Script/Sparta.SpartaTargetLockComponent' OR oc.path LIKE '/Script/Sparta.SpartaTargeter'
     OR oc.path LIKE '/Script/Sparta.SpartaEncounterManager' OR oc.path LIKE '/Script/Sparta.SpawnTrigger'
     OR oc.path LIKE '/Script/Sparta.UnitSpawner' OR oc.path LIKE '/Script/Sparta.HordeSpawnerComponent'
     OR oc.path LIKE '/Script/Sparta.SpartaHordeEncounter' OR oc.path LIKE '/Script/Sparta.SpartaCharacter'
     OR oc.path LIKE '/Script/Sparta.SpartaHittable' OR oc.path LIKE '/Script/Sparta.SpartaHitApplier'
     OR oc.path LIKE '/Script/Sparta.SpartaTargetableComponent' OR oc.path LIKE '/Script/Sparta.HordeClusterCenter'
     OR oc.path LIKE '/Script/Sparta.SpartaAnimInstance' )
ORDER BY oc.path, o.line
