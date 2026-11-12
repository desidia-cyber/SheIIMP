SELECT o.type, o.path, p.path AS owner FROM obj o JOIN obj p ON p.addr=o.owner
WHERE o.type IN ('DelegateFunction','ScriptStruct') AND (o.path LIKE '%AbilitySystemComponentOn%' OR o.path LIKE '%MontageInterrupterComponentOn%' OR o.path LIKE '%InputComponentOnInputQueued%' OR o.path LIKE '%InputInteractionOn%')
ORDER BY o.path
