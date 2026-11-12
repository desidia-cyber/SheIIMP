-- instances of the specific DataAsset classes, with counts
SELECT o.type, COUNT(*) AS n FROM obj o
WHERE o.type IN ('SpartaZoneData','SpartaAreaId','SpartaActivityType','SpartaProgressionDataAsset',
                 'SpartaWeaponProgressionData','SpartaCompletionSettings','WorldGenMapSettings',
                 'SpartaMapTileSet','DataLayerAsset','SpartaEventDataAsset','CSOptionSetDataAsset')
GROUP BY o.type ORDER BY n DESC
;;
SELECT o.path FROM obj o WHERE o.type IN ('SpartaProgressionDataAsset','SpartaCompletionSettings',
       'WorldGenMapSettings','SpartaMapTileSet','SpartaZoneData','SpartaAreaId','SpartaActivityType')
ORDER BY o.type, o.path
