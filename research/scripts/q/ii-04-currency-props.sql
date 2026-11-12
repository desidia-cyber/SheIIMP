-- Currency hunt: every property (non-function-member) whose member name hints at currency
SELECT o.type, o.path, o.off
FROM obj o
WHERE o.type LIKE '%Property'
  AND (o.path LIKE '%:Gold%' OR o.path LIKE '%:Currency%' OR o.path LIKE '%:Money%'
    OR o.path LIKE '%:Tarcoin%' OR o.path LIKE '%:Soul%' OR o.path LIKE '%:Coin%'
    OR o.path LIKE '%:Tarcore%' OR o.path LIKE '%:Scrap%' OR o.path LIKE '%:Futilium%')
ORDER BY o.path
