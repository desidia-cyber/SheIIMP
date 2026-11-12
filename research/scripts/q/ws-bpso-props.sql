SELECT substr(c.path, length('/Game/Sparta/')+1) AS cls, substr(o.path, length(c.path)+2) AS prop,
       o.type AS ptype, printf('0x%x', o.off) AS offpath, r.path AS refd
FROM obj c JOIN obj o ON o.owner = c.addr
LEFT JOIN obj r ON r.addr = UPPER(o.ref)
WHERE c.path LIKE '%BPSO\_%' ESCAPE '\'
  AND o.type NOT IN ('Function','DelegateFunction')
ORDER BY c.path, o.off, o.line
