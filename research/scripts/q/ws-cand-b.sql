SELECT c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path NOT LIKE '/Script/%' AND (c.path LIKE '%/SaveObjects/%' OR c.path LIKE '%.BP_SO\_%' ESCAPE '\' OR c.path LIKE '%.BP\_SO\_%' ESCAPE '\')
ORDER BY c.path
