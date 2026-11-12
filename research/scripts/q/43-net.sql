SELECT type, COUNT(*) AS n FROM obj
WHERE (path LIKE '/Script/Sparta.%' OR path LIKE '/Game/Sparta/%')
  AND (path LIKE '%:Server%' OR path LIKE '%:Client%' OR path LIKE '%:Multicast%' OR path LIKE '%:ClientAuth%')
GROUP BY type
