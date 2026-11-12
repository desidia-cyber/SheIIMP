SELECT c.path AS cls, substr(o.path, length(c.path)+2) AS prop, o.type AS ptype, printf('0x%x', o.off) AS off, r.path AS refd
FROM obj o JOIN cls c ON c.addr=o.owner LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE c.path LIKE '%BP_LevelInstanceSelector.BP_LevelInstanceSelector_C' OR c.path LIKE '%BP_DungeonEventSelector.BP_DungeonEventSelector_C'
ORDER BY c.path, o.off
