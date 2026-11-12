-- EV lines: every enemy ability BP class
SELECT '[' || addr || '] ' || type || ' ' || path || COALESCE(rest,'')
FROM obj
WHERE type = 'BlueprintGeneratedClass'
  AND path LIKE '/Game/Sparta/Characters/Enemies/%'
  AND path GLOB '*/GA_*'
ORDER BY path
