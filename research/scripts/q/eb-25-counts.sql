-- counts: all enemy BPs, and enemy BPs excluding GA_* (using GLOB, no LIKE escaping)
SELECT 'all' AS what, COUNT(*) AS n FROM obj
  WHERE type='BlueprintGeneratedClass' AND path LIKE '/Game/Sparta/Characters/Enemies/%'
UNION ALL
SELECT 'non-GA (NOT GLOB)', COUNT(*) FROM obj
  WHERE type='BlueprintGeneratedClass' AND path LIKE '/Game/Sparta/Characters/Enemies/%'
    AND path NOT GLOB '*/GA_*'
UNION ALL
SELECT 'all bosses', COUNT(*) FROM obj
  WHERE type='BlueprintGeneratedClass' AND path LIKE '/Game/Sparta/Characters/Bosses/%'
UNION ALL
SELECT 'bosses non-GA', COUNT(*) FROM obj
  WHERE type='BlueprintGeneratedClass' AND path LIKE '/Game/Sparta/Characters/Bosses/%'
    AND path NOT GLOB '*/GA_*'
UNION ALL
SELECT 'GA in enemies', COUNT(*) FROM cls
  WHERE type='BlueprintGeneratedClass' AND path LIKE '/Game/Sparta/Characters/Enemies/%' AND path GLOB '*/GA_*'
UNION ALL
SELECT 'GA in bosses', COUNT(*) FROM cls
  WHERE type='BlueprintGeneratedClass' AND path LIKE '/Game/Sparta/Characters/Bosses/%' AND path GLOB '*/GA_*'
