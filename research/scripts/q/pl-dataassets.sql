SELECT type, COUNT(*) n FROM obj WHERE path LIKE '/Game/Sparta/%Data%' AND type NOT LIKE '%Property' GROUP BY type ORDER BY n DESC
