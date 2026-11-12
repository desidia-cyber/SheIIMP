-- Who references a given class (edit the LIKE pattern): every object whose type-ref points at it.
SELECT o.type, o.path, p.path AS owner_obj, pt.path AS owner_type
FROM obj o
JOIN obj r ON r.addr = UPPER(o.ref)
LEFT JOIN obj p ON p.addr = UPPER(o.owner)
LEFT JOIN obj pt ON pt.addr = UPPER(p.owner)
WHERE r.path LIKE '/Game/Sparta/UI/Player/DeathScreen/WBP_DeathScreen.WBP_DeathScreen_C'
ORDER BY o.path
