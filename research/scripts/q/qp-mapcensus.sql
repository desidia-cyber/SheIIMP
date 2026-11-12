-- object-type census under /Game/Sparta/Maps
SELECT o.type, COUNT(*) AS n
FROM obj o
WHERE o.path LIKE '/Game/Sparta/Maps/%'
GROUP BY o.type
ORDER BY n DESC
