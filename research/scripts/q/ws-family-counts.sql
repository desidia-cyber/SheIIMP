SELECT 'PLA_' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '%.PLA\_%' ESCAPE '\';;
SELECT 'LVA_' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '%.LVA\_%' ESCAPE '\';;
SELECT 'GlobalEvent_' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '%.GlobalEvent\_%' ESCAPE '\';;
SELECT 'GA_' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '%.GA\_%' ESCAPE '\';;
SELECT 'GE_DEM' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '%.GE\_%' ESCAPE '\' OR path LIKE '%.DEM\_%' ESCAPE '\';;
SELECT 'WBP_' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '%.WBP\_%' ESCAPE '\';;
SELECT 'LVASelector' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '%BP\_LevelInstanceSelector%' ESCAPE '\'
