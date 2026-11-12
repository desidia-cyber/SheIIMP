SELECT o.type, o.path, o.off FROM obj o WHERE o.owner IN (SELECT addr FROM obj WHERE path LIKE '%BP_SO_SavedKeys_C' AND type='BlueprintGeneratedClass') ORDER BY o.line
