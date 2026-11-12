-- Markdown rows: enemy family | ability count | ability basenames
SELECT '| `' || fam || '` | ' || COUNT(*) || ' | ' || GROUP_CONCAT(base, ', ' ORDER BY base) || ' |'
FROM (
  SELECT
    SUBSTR(c.path, 33, INSTR(SUBSTR(c.path,33), '/') - 1) AS fam,
    SUBSTR(c.path, INSTR(c.path, '/GA_') + 1) AS base
  FROM cls c
  WHERE c.type = 'BlueprintGeneratedClass'
    AND c.path LIKE '/Game/Sparta/Characters/Enemies/%'
    AND c.path GLOB '*/GA_*'
)
GROUP BY fam ORDER BY fam
