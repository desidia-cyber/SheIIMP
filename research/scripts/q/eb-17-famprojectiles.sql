-- Family -> projectile / damage-area actor classes under that family
SELECT fam || ' :: ' || GROUP_CONCAT(base, ' ')
FROM (
  SELECT
    CASE WHEN c.path LIKE '/Game/Sparta/Characters/Enemies/%'
         THEN SUBSTR(c.path, 33, INSTR(SUBSTR(c.path,33), '/') - 1)
         ELSE 'BOSS/' || SUBSTR(c.path, 32, INSTR(SUBSTR(c.path,32), '/') - 1) END AS fam,
    SUBSTR(c.path, INSTR(c.path, '/BP_') + 1) AS base
  FROM cls c
  WHERE c.type = 'BlueprintGeneratedClass'
    AND c.path LIKE '%Projectile%'
    AND ( c.path LIKE '/Game/Sparta/Characters/Enemies/%' OR c.path LIKE '/Game/Sparta/Characters/Bosses/%' )
)
GROUP BY fam ORDER BY fam
