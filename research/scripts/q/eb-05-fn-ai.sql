-- All functions (+ full param lists, dump order) of the given class family
SELECT oc.path || ' :: ' || f.path || ' ('
  || COALESCE((SELECT GROUP_CONCAT(p.type || ' ' || p.path, ' ; ' ORDER BY p.line)
               FROM obj p WHERE p.owner = f.addr AND p.type LIKE '%Property'), '')
  || ')' AS sig
FROM obj f
JOIN obj oc ON oc.addr = f.owner
WHERE f.type = 'Function'
  AND ( oc.path = '/Script/Sparta.SpartaAICharacter'
     OR oc.path = '/Script/Sparta.SpartaAIController'
     OR oc.path = '/Script/Sparta.SpartaEnemySubsystem'
     OR oc.path = '/Script/Sparta.UnitSpawner'
     OR oc.path = '/Script/Sparta.HordeSpawnerComponent'
     OR oc.path = '/Script/Sparta.HordeUnitComponent'
     OR oc.path = '/Script/Sparta.HordeCharacterData'
     OR oc.path = '/Script/Sparta.SpartaEncounterManager'
     OR oc.path = '/Script/Sparta.SpartaEncounterRecipe'
     OR oc.path = '/Script/Sparta.SpawnTrigger'
     OR oc.path = '/Script/Sparta.SpartaHordeEncounter'
     OR oc.path = '/Script/Sparta.SpartaHordeSpawner'
     OR oc.path = '/Script/Sparta.HordeClusterCenter'
     OR oc.path = '/Script/Sparta.SpartaMassActorSpawnerSubsystem' )
ORDER BY oc.path, f.line
