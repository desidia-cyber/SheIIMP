-- every DataTable object in the game content, with its row-struct where the dump proves it
SELECT o.path, o.ref, r.path AS rowstruct
FROM obj o LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE o.type='DataTable' AND o.path LIKE '/Game/%'
ORDER BY o.path
