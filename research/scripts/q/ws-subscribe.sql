SELECT o.type, o.path FROM obj o WHERE o.path LIKE '%SubscribeSaveInterface%' ORDER BY o.line;;
SELECT o.type, o.path FROM obj o WHERE o.path LIKE '%SaveToDisk%' ORDER BY o.line LIMIT 40;;
SELECT o.type, o.path FROM obj o WHERE o.path LIKE '%ResetSaveObjectByGuid%' ORDER BY o.line LIMIT 20
