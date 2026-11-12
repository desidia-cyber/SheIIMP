SELECT COUNT(*) AS non_coreworld_persistent FROM obj
WHERE path LIKE '%:PersistentLevel%' AND path NOT LIKE '/Game/Sparta/Maps/Core_Levels/L_Core_World_01%';;
SELECT substr(path, 1, instr(path, ':PersistentLevel')-1) AS map_path, COUNT(*) AS n
FROM obj WHERE path LIKE '%:PersistentLevel%' AND path NOT LIKE '/Game/Sparta/Maps/Core_Levels/L_Core_World_01%'
GROUP BY map_path ORDER BY n DESC LIMIT 15;;
SELECT COUNT(*) AS non_game_persistent FROM obj
WHERE path LIKE '%:PersistentLevel%' AND path NOT LIKE '/Game/%'
