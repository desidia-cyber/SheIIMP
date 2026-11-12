-- Family -> GA_* ability classes (basenames), enemies only
SELECT fam || ' :: ' || GROUP_CONCAT(base, ' ')
FROM (
  SELECT
    SUBSTR(c.path, 33, INSTR(SUBSTR(c.path,33), '/') - 1) AS fam,
    SUBSTR(c.path, INSTR(c.path, '/GA_') + 1) AS base
  FROM cls c
  WHERE c.type = 'BlueprintGeneratedClass'
    AND c.path LIKE '/Game/Sparta/Characters/Enemies/%/GA\_%' ESCAPE '\'
)
GROUP BY fam ORDER BY fam
