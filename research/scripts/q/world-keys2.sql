SELECT o.type, o.path, o.off FROM obj o WHERE o.owner IN (SELECT addr FROM obj WHERE (path LIKE '%BP_SO_SavedLockedDoors_C' OR path LIKE '%BPSO_SavedLockedChests_C') AND (type='BlueprintGeneratedClass')) ORDER BY o.line;;
SELECT o.type, o.path FROM obj o WHERE o.owner IN (SELECT addr FROM obj WHERE path='/Script/Sparta.SpartaSaveSubSystem' AND type='Class') AND o.type='Function' ORDER BY o.line
