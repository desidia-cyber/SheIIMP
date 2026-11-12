-- EV lines: every boss-side BlueprintGeneratedClass except GA_*
SELECT '[' || addr || '] ' || type || ' ' || path || COALESCE(rest,'')
FROM obj
WHERE type = 'BlueprintGeneratedClass'
  AND path LIKE '/Game/Sparta/Characters/Bosses/%'
  AND path NOT LIKE '%/GA\_%' ESCAPE '\'
ORDER BY path
