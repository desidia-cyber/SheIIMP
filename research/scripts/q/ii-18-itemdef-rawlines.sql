-- Verbatim dump lines (same reconstruction as `parse_dump.py raw`) of every item-definition class.
WITH RECURSIVE d(addr, path, depth) AS (
  SELECT addr, path, 0 FROM cls WHERE path='/Script/Sparta.SpartaItemDefinition'
  UNION
  SELECT c.addr, c.path, d.depth+1 FROM cls c JOIN d ON c.super=d.addr
)
SELECT ('[' || o.addr || '] ' || o.type || ' ' || o.path || COALESCE(o.rest,'')) AS dump_line
FROM obj o JOIN d ON d.addr=o.addr
WHERE o.path LIKE '/Game/%'
ORDER BY o.path
