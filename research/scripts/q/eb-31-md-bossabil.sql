-- Markdown rows: boss family | ability count | ability basenames
SELECT '| `' || fam || '` | ' || COUNT(*) || ' | ' || GROUP_CONCAT(base, ', ' ORDER BY base) || ' |'
FROM (
  SELECT
    SUBSTR(c.path, 32, INSTR(SUBSTR(c.path,32), '/') - 1) AS fam,
    SUBSTR(c.path, INSTR(c.path, '/GA_') + 1) AS base
  FROM cls c
  WHERE c.type = 'BlueprintGeneratedClass'
    AND c.path LIKE '/Game/Sparta/Characters/Bosses/%'
    AND c.path GLOB '*/GA_*'
)
GROUP BY fam ORDER BY fam
