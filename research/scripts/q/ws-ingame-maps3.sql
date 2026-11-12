SELECT substr(path,1,instr(path,':PersistentLevel')-1) AS lvl, COUNT(*) AS n
FROM obj WHERE path LIKE '/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:%:PersistentLevel%' OR path LIKE '/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:PersistentLevel%'
GROUP BY lvl ORDER BY n DESC;;
SELECT substr(path,1,instr(path,':PersistentLevel')-1) AS lvl, COUNT(*) AS n
FROM obj WHERE path LIKE '/Script/%:PersistentLevel%'
GROUP BY lvl
