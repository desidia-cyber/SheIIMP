SELECT type, COUNT(*) n FROM obj WHERE path LIKE '/Game/Sparta/Core/Weapons/%' OR path LIKE '/Game/Sparta/Weapons/%' GROUP BY type ORDER BY n DESC
