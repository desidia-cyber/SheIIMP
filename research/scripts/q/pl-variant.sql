SELECT type, COUNT(*) n FROM obj WHERE path LIKE '/Game/Sparta/%Variant%' GROUP BY type ORDER BY n DESC LIMIT 20;;
SELECT type, path FROM obj WHERE (type LIKE '%ItemDefinition%' OR type LIKE '/Game/%.C') AND path LIKE '/Game/Sparta/%' AND (path LIKE '%ID_WP_%' OR path LIKE '%Infus%' OR path LIKE '%Upgrade%') ORDER BY path LIMIT 60
