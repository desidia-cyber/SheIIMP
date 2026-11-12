-- resolve container inner types for the quest-log component + related structs
SELECT o.path, o.off, r.path AS refd
FROM obj o LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE (o.path LIKE '/Game/Sparta/Dialogue/QuestSystem/BPC_QuestLogComponent.BPC_QuestLogComponent_C:%'
       AND (o.type LIKE '%Property'))
   OR o.path LIKE '/Script/Sparta.SpartaMapBool%'
   OR o.path LIKE '/Script/Sparta.SpartaMapInt%'
   OR o.path LIKE '/Script/Sparta.SpartaMapFloat%'
   OR o.path LIKE '/Script/Sparta.SpartaMapName%'
   OR o.path LIKE '/Game/Sparta/Dialogue/QuestSystem/FF_%'
   OR o.path LIKE '/Script/Sparta.FF_NarrativeEventFloatTracking%'
ORDER BY o.path
