SELECT substr(c.path, length('/Game/Sparta/')+1) AS cls,
       substr(f.path, length(c.path)+2) AS func,
       substr(p.path, length(f.path)+2) AS param,
       p.type AS ptype,
       r.path AS refd
FROM cls c
JOIN obj f ON f.owner = c.addr AND f.type = 'Function'
JOIN obj p ON p.owner = f.addr
LEFT JOIN obj r ON r.addr = UPPER(p.ref)
WHERE c.path LIKE '/Game/Sparta/Core/SaveObjects/BPSO_%'
   OR c.path LIKE '/Game/Sparta/SaveObjects/BPSO_%'
   OR c.path LIKE '/Game/Sparta/Core/Interaction/SaveObjects/%'
   OR c.path LIKE '/Game/Sparta/Dialogue/QuestSystem/BPSO_%'
   OR c.path LIKE '/Game/Sparta/UI/World/Map/Blueprints/BPSO_%'
ORDER BY c.path, f.line, p.line
