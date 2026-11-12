SELECT type, COUNT(*) AS n FROM obj WHERE type LIKE '%GameplayTag%' GROUP BY type;;
SELECT COUNT(*) AS tag_objs FROM obj WHERE type='GameplayTag';;
SELECT type, path FROM obj WHERE type='GameplayTag' LIMIT 12
