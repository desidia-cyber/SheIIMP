-- EV lines: every boss ability BP class
SELECT '[' || addr || '] ' || type || ' ' || path || COALESCE(rest,'')
FROM obj
WHERE type = 'BlueprintGeneratedClass'
  AND path LIKE '/Game/Sparta/Characters/Bosses/%'
  AND path GLOB '*/GA_*'
ORDER BY path
