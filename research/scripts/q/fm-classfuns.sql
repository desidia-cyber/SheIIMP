-- All functions owned by a class whose path LIKE a pattern (pattern edited per run).
SELECT f.path
FROM obj f
WHERE f.type='Function'
  AND f.owner IN (SELECT addr FROM obj WHERE path LIKE '%BPFL_UI.BPFL_UI_C' AND type LIKE '%GeneratedClass')
ORDER BY f.line
