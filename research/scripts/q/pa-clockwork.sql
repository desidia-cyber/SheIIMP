SELECT COUNT(*) AS n FROM obj WHERE type='AnimMontage' AND path LIKE '%Clockwork%';;
SELECT path FROM obj WHERE type='AnimMontage' AND (path LIKE '%Scythe%' OR path LIKE '%Chainsaw%' OR path LIKE '%Grinder%') ORDER BY path LIMIT 20
