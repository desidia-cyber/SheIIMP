-- Functions of one class (by path substring) with parameters, in dump order.
-- Usage: change the LIKE pattern below.
SELECT f.path AS fn,
       p.path AS param, p.type AS ptype, p.off AS poff, r.path AS pref
FROM obj f
JOIN obj p ON p.owner = f.addr
LEFT JOIN obj r ON r.addr = UPPER(p.ref)
WHERE f.type='Function'
  AND f.owner = (SELECT addr FROM obj WHERE path LIKE '%SpartaGameInstance' AND type='Class' LIMIT 1)
ORDER BY f.line, p.line
