SELECT type, COUNT(*) AS n FROM obj WHERE path LIKE '/Game/Sparta/Items/%' GROUP BY type ORDER BY n DESC
;;
SELECT path FROM obj WHERE path LIKE '/Game/Sparta/Items/%' ORDER BY path
