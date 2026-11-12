-- exact class paths for the shop / drop / forge / upgrade blueprints
SELECT c.path, s.path AS parent FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '%ShopHandler%' OR c.path LIKE '%BPSO_ShopData%' OR c.path LIKE '%BP_Merchant%'
   OR c.path LIKE '%BP_ItemGiver%' OR c.path LIKE '%BP_DE_GainItem%' OR c.path LIKE '%BP_DE_GiveSpecificItem%'
   OR c.path LIKE '%BP_DE_LoseItem%' OR c.path LIKE '%BP_RewardData%' OR c.path LIKE '%BP_RD_PickUpItem%'
   OR c.path LIKE '%BP_PickupBase%' OR c.path LIKE '%GA_DeathBase%' OR c.path LIKE '%BP_CraftingItemPickup%'
   OR c.path LIKE '%BP_Tarforge%' OR c.path LIKE '%BP_UpgradePoolHandler%' OR c.path LIKE '%BPFL_Shops%'
   OR c.path LIKE '%BPFL_Tarstones%' OR c.path LIKE '%BPFL_Tarforge%' OR c.path LIKE '%BPFL_Upgrades%'
   OR c.path LIKE '%IF_UsableInventoryItem%' OR c.path LIKE '%BP_Interactable_ShopItem%'
   OR c.path LIKE '%BPSO_Tarforge%' OR c.path LIKE '%BPSO_CraftingItemPickup%' OR c.path LIKE '%BP_Tarstone%'
   OR c.path LIKE '%BP_InteractableBase_LootCrate%' OR c.path LIKE '%BP_ChainSarcophagusDrop%'
   OR c.path LIKE '%BP_SingleArrowDrop%' OR c.path LIKE '%BP_RD_Artifact%' OR c.path LIKE '%BP_RD_Tarstone%'
   OR c.path LIKE '%BP_ArtifactsComponent%' OR c.path LIKE '%BPC_TarstoneComponent%' OR c.path LIKE '%BP_UnlockWeapon%'
   OR c.path LIKE '%BP_KeepSakeVendor%' OR c.path LIKE '%BP_ShopItemRandomizer%'
ORDER BY c.path
