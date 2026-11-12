SELECT c.type, c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Script/%'
  AND (c.path LIKE '%Item%' OR c.path LIKE '%Inventory%' OR c.path LIKE '%Tarstone%'
    OR c.path LIKE '%Artifact%' OR c.path LIKE '%Armor%' OR c.path LIKE '%Sidearm%'
    OR c.path LIKE '%Seal%' OR c.path LIKE '%Currency%' OR c.path LIKE '%Upgrade%'
    OR c.path LIKE '%Shop%' OR c.path LIKE '%Merchant%' OR c.path LIKE '%Vendor%'
    OR c.path LIKE '%Forge%' OR c.path LIKE '%Craft%' OR c.path LIKE '%Loot%'
    OR c.path LIKE '%Drop%' OR c.path LIKE '%Pickup%' OR c.path LIKE '%Weapon%')
ORDER BY c.path
