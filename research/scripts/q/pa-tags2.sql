SELECT path, type FROM obj WHERE path LIKE '%GameplayTagsList%';;
SELECT COUNT(*) n FROM obj WHERE path LIKE '%InputTag%';;
SELECT type, path FROM obj WHERE path LIKE '%InputTag%' ORDER BY path LIMIT 60;;
SELECT type, path FROM obj WHERE type LIKE '%GameplayTag%' AND path LIKE '/Game/%' ORDER BY path LIMIT 40
