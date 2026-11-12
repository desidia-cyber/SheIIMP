-- Family -> GA_* ability classes (basenames), bosses only
SELECT fam || ' :: ' || GROUP_CONCAT(base, ' ')
FROM (
  SELECT
    'BOSS/' || SUBSTR(c.path, 32, INSTR(SUBSTR(c.path,32), '/') - 1) AS fam,
    SUBSTR(c.path, INSTR(c.path, '/GA_') + 1) AS base
  FROM cls c
  WHERE c.type = 'BlueprintGeneratedClass'
    AND c.path LIKE '/Game/Sparta/Characters/Bosses/%/GA\_%' ESCAPE '\'
)
GROUP BY fam ORDER BY fam
