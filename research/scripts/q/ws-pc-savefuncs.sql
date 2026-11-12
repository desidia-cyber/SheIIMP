SELECT substr(f.path, length(c.path)+2) AS func
FROM cls c JOIN obj f ON f.owner = c.addr AND f.type='Function'
WHERE c.path = '/Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C'
  AND (f.path LIKE '%Save%' OR f.path LIKE '%Guid%' OR f.path LIKE '%S\_%' ESCAPE '\')
ORDER BY f.line;;
SELECT substr(f.path, length(c.path)+2) AS func, substr(p.path, length(f.path)+2) AS param, p.type AS ptype, r.path AS refd
FROM cls c JOIN obj f ON f.owner = c.addr AND f.type='Function'
LEFT JOIN obj p ON p.owner=f.addr
LEFT JOIN obj r ON r.addr=UPPER(p.ref)
WHERE c.path = '/Game/Sparta/Core/Player/BP_PlayerController.BP_PlayerController_C'
  AND (f.path LIKE '%Save%' OR f.path LIKE '%Guid%')
ORDER BY f.line, p.line
