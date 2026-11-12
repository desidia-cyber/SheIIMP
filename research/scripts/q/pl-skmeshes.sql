SELECT type, path FROM obj WHERE type IN ('SkeletalMesh','Skeleton','PhysicsAsset','PoseAsset') AND path LIKE '/Game/Sparta/Characters/Shells/%' ORDER BY type, path
