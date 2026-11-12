SELECT type, COUNT(*) AS n FROM obj WHERE type LIKE '%GeneratedClass' GROUP BY type ORDER BY n DESC
