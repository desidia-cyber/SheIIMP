-- parents of the currency-ish item classes
SELECT c.path, s.path AS parent FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/Items/%' AND (c.path LIKE '%ID_%' OR c.path LIKE '%Currency%')
ORDER BY c.path
;;
-- all class parents under /Game/Sparta/Items/
SELECT s.path AS parent, COUNT(*) AS n FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/Items/%' GROUP BY parent ORDER BY n DESC
;;
-- asset (Package) families under /Game/Sparta/Items/
SELECT COUNT(*) AS n FROM obj WHERE type='Package' AND path LIKE '/Game/Sparta/Items/%'
