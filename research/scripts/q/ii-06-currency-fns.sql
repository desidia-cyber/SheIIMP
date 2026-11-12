-- functions whose own name mentions Gold/Currency/Tarcoin/Tarcore
SELECT p.path AS owner_path, p.type AS owner_type, o.path AS fn
FROM obj o JOIN obj p ON p.addr=o.owner
WHERE o.type='Function' AND (o.path LIKE '%Gold%' OR o.path LIKE '%Currency%')
ORDER BY o.path
;;
-- structs / classes whose name mentions Currency
SELECT type, path FROM obj WHERE path LIKE '%CurrencyData%' OR path LIKE '%.FCurrency%' OR path LIKE '%CurrencyTable%'
ORDER BY path
;;
-- every object (any type) with 'Tarcore' or 'Tarcoin' or 'Futilium' in the path
SELECT type, path FROM obj WHERE path LIKE '%Tarcore%' OR path LIKE '%Tarcoin%'
ORDER BY path LIMIT 120
