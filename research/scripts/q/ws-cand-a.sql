SELECT c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path NOT LIKE '/Script/%'
  AND (c.path LIKE '%Door%' OR c.path LIKE '%Gate%' OR c.path LIKE '%Lever%'
       OR c.path LIKE '%Elevator%' OR c.path LIKE '%Lift%' OR c.path LIKE '%Chest%'
       OR c.path LIKE '%Checkpoint%' OR c.path LIKE '%Bonfire%' OR c.path LIKE '%Fog%'
       OR c.path LIKE '%Barrier%' OR c.path LIKE '%Ladder%' OR c.path LIKE '%Trap%'
       OR c.path LIKE '%Loot%' OR c.path LIKE '%Shortcut%' OR c.path LIKE '%Shrine%'
       OR c.path LIKE '%SaveGame%')
ORDER BY c.path
