SELECT substr(c.path, length('/Game/Sparta/')+1) AS cls, substr(f.path, length(c.path)+2) AS func
FROM cls c JOIN obj f ON f.owner = c.addr AND f.type = 'Function'
WHERE c.path LIKE '%BPSO\_%' ESCAPE '\' OR c.path LIKE '%BP\_SO\_%' ESCAPE '\'
ORDER BY c.path, f.line
