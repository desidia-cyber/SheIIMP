-- every BPSO_* save-object class anywhere under /Game, with its parent class
SELECT c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Game/%/BPSO_%' OR c.path LIKE '/Game/%/BPO_%'
ORDER BY c.path
