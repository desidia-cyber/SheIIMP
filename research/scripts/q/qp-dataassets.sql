-- DataAsset / PrimaryDataAsset / DataTable objects under /Game (whole content tree)
SELECT o.type, o.path
FROM obj o
WHERE (o.type LIKE '%DataAsset' OR o.type='DataTable' OR o.type LIKE '%DataTable')
  AND o.path LIKE '/Game/Sparta/%'
ORDER BY o.type, o.path
