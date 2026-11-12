-- persistent-level / non-generated World objects (real map packages)
SELECT o.path
FROM obj o
WHERE o.type='World' AND o.path LIKE '/Game/Sparta/Maps/%'
  AND o.path NOT LIKE '%/_Generated_/%'
ORDER BY o.path
;;
-- WorldPartition level-streaming objects (first 40, shows the streaming class the map uses)
SELECT o.type, COUNT(*) AS n
FROM obj o
WHERE o.path LIKE '/Game/Sparta/Maps/%'
  AND (o.type LIKE 'WorldPartition%' OR o.type LIKE '%LevelStreaming%')
GROUP BY o.type ORDER BY n DESC
