CREATE VIEW IF NOT EXISTS cls AS
  SELECT addr, type, path, super FROM obj
  WHERE type = 'Class' OR type LIKE '%GeneratedClass';;
SELECT type, COUNT(*) AS n FROM cls GROUP BY type ORDER BY n DESC
