SELECT type, COUNT(*) n FROM obj WHERE type LIKE '%GameplayTag%' GROUP BY type;;
SELECT path FROM obj WHERE type='GameplayTag' AND (path LIKE '%Input%' OR path LIKE '%Ability%') ORDER BY path LIMIT 120
