SELECT c.path, s.path AS parent FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Script/Sparta.%' AND (c.path LIKE '%Input%' OR c.path LIKE '%Set%' OR c.path LIKE '%Notify%' OR c.path LIKE '%Montage%' OR c.path LIKE '%Task%')
ORDER BY c.path
