SELECT sub.path AS skeleton, COUNT(*) n FROM obj s JOIN obj sub ON sub.path LIKE sub.path WHERE 0;;
SELECT substr(path,1,instr(path,':')) AS skeleton, COUNT(*) n FROM obj WHERE type='SkeletalMeshSocket' AND path LIKE '/Game/Sparta/Weapons/Player/%' GROUP BY skeleton ORDER BY skeleton
