-- Members (properties then functions) for an explicit list of class paths.
-- Edit the IN (...) list per run.
SELECT substr(o.path,1,instr(o.path,':')-1) AS cls,
       o.type, o.path, o.off,
       CASE WHEN o.type='Function' THEN '' ELSE COALESCE(r.path,'') END AS refd
FROM obj o
LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE o.owner IN (SELECT addr FROM obj WHERE type='Class' AND path IN (
  '/Script/Sparta.SpartaItemDefinition',
  '/Script/Sparta.SpartaItemInstance',
  '/Script/Sparta.SpartaItemFragment'
))
ORDER BY o.owner, o.line
