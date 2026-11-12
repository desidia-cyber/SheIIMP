SELECT c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE (c.path LIKE '%Beacon%' OR c.path LIKE '%Respawn%' OR c.path LIKE '%PlayerStart%'
       OR c.path LIKE '%Checkpoint%' OR c.path LIKE '%Bonfire%' OR c.path LIKE '%Sanctuary%'
       OR c.path LIKE '%Shrine%' OR c.path LIKE '%RestPoint%' OR c.path LIKE '%Rest\_%' ESCAPE '\')
ORDER BY c.path
