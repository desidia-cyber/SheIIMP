-- Native Sparta damage / hit / poise / death classes
SELECT c.type || ' | ' || c.path
FROM cls c
WHERE c.path LIKE '/Script/Sparta.%'
  AND ( c.path LIKE '%Hit%' OR c.path LIKE '%Damage%' OR c.path LIKE '%Poise%' OR c.path LIKE '%Parry%'
     OR c.path LIKE '%Riposte%' OR c.path LIKE '%Stagger%' OR c.path LIKE '%Death%' OR c.path LIKE '%Health%'
     OR c.path LIKE '%Execution%' OR c.path LIKE '%Winded%' OR c.path LIKE '%Break%' OR c.path LIKE '%Heal%'
     OR c.path LIKE '%Hurt%' )
ORDER BY c.path
