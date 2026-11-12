SELECT type, COUNT(*) AS n FROM obj WHERE type IN ('LevelSequence','AnimSequence','Sequencer') OR (path LIKE '%/LS_%' AND type='LevelSequence') GROUP BY type
