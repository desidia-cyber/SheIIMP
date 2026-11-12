SELECT type, path FROM obj WHERE type='Skeleton' AND path LIKE '/Game/Sparta/Characters/Humans/%' ORDER BY path;;
SELECT type, path FROM obj WHERE type IN ('SkeletalMesh','Skeleton') AND path LIKE '/Game/Sparta/Characters/Mannequins/%' ORDER BY path;;
SELECT COUNT(*) n FROM obj WHERE type='SkeletalMeshSocket' AND path LIKE '/Game/Sparta/Characters/Humans/_Shared/SKEL_Human_Skeleton%';;
SELECT COUNT(*) n FROM obj WHERE type='Skeleton'
