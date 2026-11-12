SELECT 'main' AS k, COUNT(DISTINCT substr(path,1,instr(path,':PersistentLevel')-1)) AS n FROM obj
WHERE path LIKE '/Game/Sparta/Maps/Core_Levels/L_Core_World_01.L_Core_World_01:%';;
SELECT 'generated' AS k, COUNT(DISTINCT substr(path,1,instr(path,':PersistentLevel')-1)) AS n FROM obj
WHERE path LIKE '/Game/Sparta/Maps/Core_Levels/L_Core_World_01/_Generated_/%';;
SELECT 'script' AS k, COUNT(DISTINCT substr(path,1,instr(path,':PersistentLevel')-1)) AS n FROM obj
WHERE path LIKE '/Script/%:PersistentLevel%'
