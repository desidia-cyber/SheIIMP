SELECT type, path FROM obj WHERE type IN ('StaticMesh','SkeletalMesh','Skeleton') AND (path LIKE '/Game/Sparta/Core/Weapons/Player/%' OR path LIKE '/Game/Sparta/Weapons/%') ORDER BY path
