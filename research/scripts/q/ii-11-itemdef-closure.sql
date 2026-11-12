-- Transitive closure of SpartaItemDefinition descendants (all item definition assets)
WITH RECURSIVE d(addr, path, depth) AS (
  SELECT addr, path, 0 FROM cls WHERE path='/Script/Sparta.SpartaItemDefinition'
  UNION
  SELECT c.addr, c.path, d.depth+1 FROM cls c JOIN d ON c.super=d.addr
)
SELECT d.path FROM d WHERE d.path LIKE '/Game/%' ORDER BY d.path
