-- Classes whose path hints at currency
SELECT c.type, c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '%Gold%' OR c.path LIKE '%Currency%' OR c.path LIKE '%Money%'
   OR c.path LIKE '%Soul%' OR c.path LIKE '%Tarcore%' OR c.path LIKE '%Coin%'
ORDER BY c.path
;;
-- functions with currency-sounding names, any class
SELECT p.path AS owner, o.path AS fn
FROM obj o JOIN obj p ON p.addr=o.owner
WHERE o.type='Function'
  AND (o.path LIKE '%:AddGold%' OR o.path LIKE '%:SpendGold%' OR o.path LIKE '%:GetGold%'
    OR o.path LIKE '%:SetGold%' OR o.path LIKE '%:AddCurrency%' OR o.path LIKE '%:Spend%'
    OR o.path LIKE '%:Currency%' OR o.path LIKE '%:Money%')
ORDER BY o.path
