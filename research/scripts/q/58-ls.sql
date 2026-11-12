SELECT 'all' AS scope, COUNT(*) AS n FROM obj WHERE type='LevelSequence'
UNION ALL SELECT '/Game/Sparta', COUNT(*) FROM obj WHERE type='LevelSequence' AND path LIKE '/Game/Sparta/%'
UNION ALL SELECT '/Game (any)', COUNT(*) FROM obj WHERE type='LevelSequence' AND path LIKE '/Game/%'
