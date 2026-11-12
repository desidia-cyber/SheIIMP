-- EV lines: every enemy-side BlueprintGeneratedClass except GA_* abilities
SELECT '[' || addr || '] ' || type || ' ' || path || COALESCE(rest,'')
FROM obj
WHERE type = 'BlueprintGeneratedClass'
  AND path LIKE '/Game/Sparta/Characters/Enemies/%'
  AND path NOT LIKE '%/GA\_%' ESCAPE '\'
ORDER BY path
