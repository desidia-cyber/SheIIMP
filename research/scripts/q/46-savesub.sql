SELECT COUNT(*) AS n_saveobject_subclasses FROM cls c WHERE c.super IS NOT NULL
AND UPPER(c.super) IN (SELECT addr FROM obj WHERE path LIKE '%SpartaSaveObject')
