-- Count LevelSequence assets under /Game/Sparta/ (type name as it appears in the dump)
SELECT type, COUNT(*) AS n FROM obj
WHERE path LIKE '/Game/Sparta/%'
  AND (type LIKE '%LevelSequence%' OR type LIKE '%Sequence%')
GROUP BY type ORDER BY n DESC
