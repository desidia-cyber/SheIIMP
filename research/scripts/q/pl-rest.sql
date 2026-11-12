SELECT type, path, off, rest FROM obj WHERE path LIKE '%SkeletalMeshSocket_0' LIMIT 4;;
SELECT type, path, '['||rest||']' FROM obj WHERE rest IS NOT NULL AND rest != '' AND type='NameProperty' LIMIT 3
