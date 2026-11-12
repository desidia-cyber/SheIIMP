-- full census of DataAsset-ish objects under /Game/Sparta
SELECT o.type, COUNT(*) AS n FROM obj o
WHERE (o.type LIKE '%DataAsset' OR o.type LIKE '%DataTable')
  AND o.path LIKE '/Game/Sparta/%'
GROUP BY o.type ORDER BY n DESC
;;
-- assets of the specific classes used by quests/dialogue/maps/zones
SELECT o.type, o.path FROM obj o
WHERE o.type IN ('SpartaZoneData','SpartaAreaId','SpartaActivityType','SpartaProgressionDataAsset',
                 'SpartaWeaponProgressionData','SpartaCompletionSettings','WorldGenMapSettings',
                 'SpartaMapTileSet','SpartaWorldGenSubsystem','DataLayerAsset','SpartaEventDataAsset')
ORDER BY o.type, o.path
