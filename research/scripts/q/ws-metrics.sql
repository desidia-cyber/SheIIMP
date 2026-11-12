SELECT 'save_objects_blueprint' AS metric, COUNT(*) AS n FROM cls
WHERE (path LIKE '/Game/%') AND (UPPER(super) IN (SELECT addr FROM obj WHERE path IN
 ('/Script/Sparta.SpartaSaveObject','/Script/Sparta.PlayerSaveObject','/Script/Sparta.PlayerProgressionSaveData','/Script/Sparta.SpartaSessionSaveObjectBase')
 OR UPPER(super) IN (SELECT c2.addr FROM cls c2 JOIN obj s2 ON s2.addr=UPPER(c2.super)
                     WHERE s2.path IN ('/Script/Sparta.SpartaSaveObject','/Script/Sparta.PlayerSaveObject')))
  AND path NOT LIKE '%/Default__%');;
SELECT 'with_save_component' AS metric, COUNT(DISTINCT c.path) AS n FROM obj o JOIN cls c ON c.addr=o.owner JOIN obj r ON r.addr=UPPER(o.ref)
WHERE r.path='/Script/Sparta.SpartaSaveComponent';;
SELECT 'bpso_named' AS metric, COUNT(*) AS n FROM cls WHERE path LIKE '%BPSO\_%' ESCAPE '\';;
SELECT 'bp_so_named' AS metric, COUNT(*) AS n FROM cls WHERE path LIKE '%BP\_SO\_%' ESCAPE '\'
