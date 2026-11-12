SELECT c.path, s.path AS parent FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Script/Sparta.%' AND (c.path LIKE '%Ability%' OR c.path LIKE '%Attribute%')
ORDER BY c.path
