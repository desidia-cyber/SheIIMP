SELECT type, COUNT(*) n FROM obj WHERE path LIKE '/Game/Sparta/Items/Shells/%' GROUP BY type ORDER BY n DESC
