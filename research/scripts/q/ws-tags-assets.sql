SELECT type, path FROM obj
WHERE path LIKE '/Game/%Tag%' AND (type LIKE '%GeneratedClass' OR type IN ('DataTable','CurveTable','PrimaryDataAsset','DataAsset','GameplayTag','Object'))
ORDER BY path LIMIT 200
