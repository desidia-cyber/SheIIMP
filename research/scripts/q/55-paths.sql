SELECT path FROM obj WHERE path LIKE '%E_ChestRewardtype%' OR path LIKE '%E_DungeonButton_VisualTypes%' OR path LIKE '%E_FlowerTypeMaterials%'
UNION ALL SELECT path FROM obj WHERE path LIKE '%Breakable_Battleground_Barrier%' AND type='BlueprintGeneratedClass'
UNION ALL SELECT path FROM obj WHERE path LIKE '%CraftingItemPickup_Common%' AND type='BlueprintGeneratedClass'
