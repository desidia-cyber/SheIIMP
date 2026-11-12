-- user-defined (Blueprint) structs under /Game, with parent struct when known
SELECT o.path, s.path AS parent
FROM obj o LEFT JOIN obj s ON s.addr = UPPER(o.super)
WHERE o.type='ScriptStruct' AND o.path LIKE '/Game/%'
ORDER BY o.path
