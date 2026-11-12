-- exact reflection paths of the targeting / perception / director / save classes
SELECT path FROM cls
WHERE path LIKE '/Script/Sparta.%'
  AND ( path LIKE '%Target%' OR path LIKE '%Perception%' OR path LIKE '%Director%'
     OR path LIKE '%SoftTargeting%' OR path LIKE '%SaveInterface%' )
ORDER BY path
