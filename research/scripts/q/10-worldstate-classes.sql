SELECT c.path, c.type, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE (c.path LIKE '%Door%' OR c.path LIKE '%Gate%' OR c.path LIKE '%Lever%' OR c.path LIKE '%Elevator%'
    OR c.path LIKE '%Lift%' OR c.path LIKE '%Chest%' OR c.path LIKE '%Pickup%' OR c.path LIKE '%Interact%'
    OR c.path LIKE '%Checkpoint%' OR c.path LIKE '%Shrine%' OR c.path LIKE '%Fog%' OR c.path LIKE '%Barrier%'
    OR c.path LIKE '%Breakable%' OR c.path LIKE '%Destructible%' OR c.path LIKE '%Shortcut%')
  AND c.path NOT LIKE '/Script/%'
  AND c.path NOT LIKE '%/CameraShakes/%' AND c.path NOT LIKE '%/CameraStates/%'
ORDER BY c.path
