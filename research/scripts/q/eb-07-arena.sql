-- Arena / gate / fog-wall / volume / trigger classes under Bosses and Maps
SELECT c.type || ' | ' || c.path || ' | ' || COALESCE(s.path,'?')
FROM cls c LEFT JOIN obj s ON s.addr = UPPER(c.super)
WHERE ( c.path LIKE '/Game/Sparta/Characters/Bosses/%' OR c.path LIKE '/Game/Sparta/Maps/%' )
  AND ( c.path LIKE '%Volume%' OR c.path LIKE '%Trigger%' OR c.path LIKE '%Arena%'
     OR c.path LIKE '%Gate%' OR c.path LIKE '%Fog%' OR c.path LIKE '%Wall%' )
ORDER BY c.path
