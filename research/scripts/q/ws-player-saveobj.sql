SELECT c.path, s.path AS parent FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '%SpartaSettingsSaveObject%' OR c.path LIKE '%BP_SpartaPlayerSaveObject%' OR c.path LIKE '%BP_SpawnerSessionSaveObject%' OR c.path LIKE '%BP_SpartaSingleton%';;
SELECT c.path AS cls, substr(o.path, length(c.path)+2) AS prop, o.type AS ptype, printf('0x%x', o.off) AS off, r.path AS refd
FROM obj o JOIN cls c ON c.addr=o.owner LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE (c.path LIKE '%BP_SpartaPlayerSaveObject%' OR c.path LIKE '%SpartaSettingsSaveObject%' OR c.path LIKE '%BP_SpawnerSessionSaveObject%' OR c.path LIKE '%SpartaSettingsSaveObject%')
ORDER BY c.path, o.off
