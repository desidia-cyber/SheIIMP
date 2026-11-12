-- every struct deriving from TableRowBase (i.e. a DataTable row struct)
SELECT c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE s.path LIKE '%TableRowBase'
ORDER BY c.path
