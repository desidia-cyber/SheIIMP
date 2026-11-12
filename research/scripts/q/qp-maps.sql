-- every World / level-related object in /Game/Sparta/Maps
SELECT o.type, o.path
FROM obj o
WHERE o.path LIKE '/Game/Sparta/Maps/%'
  AND (o.type='World' OR o.type='LevelStreamingDynamic' OR o.type='LevelStreamingAlwaysLoaded'
    OR o.type='LevelStreamingPersistent' OR o.type='LevelStreaming' OR o.type='DataLayerAsset'
    OR o.type='WorldDataLayers' OR o.type='WorldPartition' OR o.type='DataTable' OR o.type='PrimaryDataAsset'
    OR o.type='DataAsset' OR o.type='BlueprintGeneratedClass' OR o.type='SpartaLevelStreamingDynamic'
    OR o.type='SpartaLevelStreamingDynamicHighPrio' OR o.type='WorldPartitionSubsystem')
ORDER BY o.type, o.path
