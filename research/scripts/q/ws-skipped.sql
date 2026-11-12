SELECT 'Rest_total' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '%Rest%' AND path NOT LIKE '/Script/%';;
SELECT c.path FROM cls c WHERE c.path LIKE '%Rest%' AND c.path NOT LIKE '/Script/%' ORDER BY c.path LIMIT 25;;
SELECT 'Switch_total' AS k, COUNT(*) AS n FROM cls WHERE path LIKE '%Switch%' AND path NOT LIKE '/Script/%';;
SELECT c.path FROM cls c WHERE c.path LIKE '%Switch%' AND c.path NOT LIKE '/Script/%' ORDER BY c.path LIMIT 20
