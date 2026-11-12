-- Native Sparta AI / character / spawner / damage classes
SELECT c.type || ' | ' || c.path
FROM cls c
WHERE c.path LIKE '/Script/Sparta.%'
  AND ( c.path LIKE '%AICharacter%' OR c.path LIKE '%AIController%' OR c.path LIKE '%EnemySubsystem%'
     OR c.path LIKE '%Spawner%' OR c.path LIKE '%Horde%' OR c.path LIKE '%Encounter%'
     OR c.path LIKE '%SpawnTrigger%' OR c.path LIKE '%UnitSpawn%' )
ORDER BY c.path
