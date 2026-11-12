-- Damage-path functions on ASpartaCharacter, filtered by name keywords
SELECT oc.path || ' :: ' || f.path || ' ('
  || COALESCE((SELECT GROUP_CONCAT(p.type || ' ' || p.path, ' ; ' ORDER BY p.line)
               FROM obj p WHERE p.owner = f.addr AND p.type LIKE '%Property'), '')
  || ')'
FROM obj f JOIN obj oc ON oc.addr = f.owner
WHERE f.type='Function' AND oc.path='/Script/Sparta.SpartaCharacter'
  AND ( f.path LIKE '%Hit%' OR f.path LIKE '%Damage%' OR f.path LIKE '%Death%' OR f.path LIKE '%Die%'
     OR f.path LIKE '%Stagger%' OR f.path LIKE '%Parry%' OR f.path LIKE '%Riposte%' OR f.path LIKE '%Poise%'
     OR f.path LIKE '%Hurt%' OR f.path LIKE '%Health%' OR f.path LIKE '%Kill%' OR f.path LIKE '%Break%'
     OR f.path LIKE '%Execut%' OR f.path LIKE '%Winded%' OR f.path LIKE '%Target%' OR f.path LIKE '%Aggro%' )
ORDER BY f.line
