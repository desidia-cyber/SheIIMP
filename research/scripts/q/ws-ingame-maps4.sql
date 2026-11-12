SELECT substr(path,1,instr(path,':PersistentLevel')-1) AS lvl, COUNT(*) AS n, MIN(path) AS example
FROM obj
WHERE path LIKE '/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:%'
GROUP BY lvl ORDER BY n DESC
