SELECT c.path, c.type, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '%BPSO\_%' ESCAPE '\'
ORDER BY c.path
