SELECT type, COUNT(*) AS n FROM obj WHERE path LIKE '%:PersistentLevel.%' AND (type LIKE '%Boss%' OR type LIKE '%Encounter%' OR type LIKE '%Aren%')
GROUP BY type ORDER BY n DESC LIMIT 20
