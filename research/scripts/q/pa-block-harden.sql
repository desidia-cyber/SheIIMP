SELECT c.path, s.path AS parent FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '%/GA\_%' ESCAPE '\' AND c.path LIKE '/Game/Sparta/%'
  AND (c.path LIKE '%Harden%' OR c.path LIKE '%Block%' OR c.path LIKE '%Parry%' OR c.path LIKE '%Riposte%')
ORDER BY c.path
