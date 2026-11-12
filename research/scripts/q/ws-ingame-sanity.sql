SELECT COUNT(*) AS obj_rows FROM obj;;
SELECT COUNT(*) AS cls_rows FROM cls;;
SELECT COUNT(*) AS persistent_rows FROM obj WHERE path LIKE '%:PersistentLevel%';;
SELECT COUNT(*) AS distinct_maps FROM (SELECT DISTINCT substr(path, 1, instr(path, ':PersistentLevel')) AS m FROM obj WHERE path LIKE '%:PersistentLevel%');;
SELECT substr(path, 1, instr(path, ':PersistentLevel')-1) AS map_path, COUNT(*) AS n
FROM obj WHERE path LIKE '%:PersistentLevel%'
GROUP BY map_path ORDER BY n DESC LIMIT 12
