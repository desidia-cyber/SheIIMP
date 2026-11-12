SELECT type, COUNT(*) AS n FROM obj WHERE path LIKE '%:PersistentLevel.%' AND type NOT LIKE '%Property' AND type NOT LIKE '%Component'
GROUP BY type ORDER BY n DESC LIMIT 45
