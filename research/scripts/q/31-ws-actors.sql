SELECT type, COUNT(*) AS n FROM obj WHERE path LIKE '%:PersistentLevel.%'
  AND (type LIKE '%Door%' OR type LIKE '%Lever%' OR type LIKE '%Chest%' OR type LIKE '%Pickup%'
       OR type LIKE '%Interact%' OR type LIKE '%Gate%' OR type LIKE '%Checkpoint%' OR type LIKE '%Shrine%'
       OR type LIKE '%Rest%' OR type LIKE '%Bonfire%' OR type LIKE '%Obelisk%' OR type LIKE '%Item%')
GROUP BY type ORDER BY n DESC LIMIT 60
