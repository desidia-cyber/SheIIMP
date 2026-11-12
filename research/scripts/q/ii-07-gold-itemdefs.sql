-- item definitions (blueprint classes under Items) whose name looks like a currency
SELECT path FROM cls WHERE path LIKE '/Game/Sparta/Items/%' AND (
  path LIKE '%Gold%' OR path LIKE '%Tarcore%' OR path LIKE '%Gloom%' OR path LIKE '%Resolve%'
  OR path LIKE '%Ovum%' OR path LIKE '%Glimpse%' OR path LIKE '%Specimen%' OR path LIKE '%Undersap%')
ORDER BY path
;;
-- S_AddGold / S_AddTarcores / IncrementCumulativeCurrency signatures
SELECT p.path AS owner, o.path AS fn FROM obj o JOIN obj p ON p.addr=o.owner
WHERE o.type='Function' AND (o.path LIKE '%:S_Add%' OR o.path LIKE '%IncrementCumulativeCurrency%')
ORDER BY o.path
;;
-- params of those functions
SELECT o.path, o.type, o.off, r.path AS refd FROM obj o LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE o.owner IN (SELECT addr FROM obj WHERE type='Function' AND (path LIKE '%:S_AddGold%' OR path LIKE '%:S_AddTarcores%' OR path LIKE '%IncrementCumulativeCurrency%'))
ORDER BY o.owner, o.line
