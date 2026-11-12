SELECT 'ALL_GA' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '%/GA_%';;
SELECT 'Game_Sparta_GA' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/%/GA\_%' ESCAPE '\';;
SELECT type, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/%GA\_%' ESCAPE '\' GROUP BY type;;
SELECT substr(path, 1, 40) AS folder, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/%GA\_%' ESCAPE '\' GROUP BY folder ORDER BY n DESC
