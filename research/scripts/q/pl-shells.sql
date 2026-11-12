SELECT type, COUNT(*) AS n FROM obj WHERE path LIKE '/Game/Sparta/Characters/Shells/%' GROUP BY type ORDER BY n DESC
