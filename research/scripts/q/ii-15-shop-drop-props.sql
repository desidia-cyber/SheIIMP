-- Properties (declared members, with offset + resolved type) of the shop / drop / forge blueprints.
-- Compiler temporaries and SCS/generated sub-objects are excluded via the type filter.
WITH target(addr, path) AS (
  SELECT addr, path FROM cls WHERE path IN (
    '/Game/Sparta/Core/SaveObjects/BPSO_ShopData.BPSO_ShopData_C',
    '/Game/Sparta/Core/SaveObjects/BPSO_Tarforge.BPSO_Tarforge_C',
    '/Game/Sparta/Core/SaveObjects/BPSO_CraftingItemPickup.BPSO_CraftingItemPickup_C',
    '/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_ShopItem.BP_Interactable_ShopItem_C',
    '/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Tarforge.BP_Tarforge_C',
    '/Game/Sparta/Items/Pickups/Core/BP_RewardData.BP_RewardData_C',
    '/Game/Sparta/Items/Pickups/Core/BP_RD_PickUpItem.BP_RD_PickUpItem_C',
    '/Game/Sparta/Items/Pickups/Core/BP_RD_Currency.BP_RD_Currency_C',
    '/Game/Sparta/Items/Pickups/Core/BP_RD_Artifact.BP_RD_Artifact_C',
    '/Game/Sparta/Items/Pickups/Core/BP_RD_Tarstone.BP_RD_Tarstone_C',
    '/Game/Sparta/Items/Pickups/BP_PickupBase.BP_PickupBase_C',
    '/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C',
    '/Game/Sparta/Items/Pickups/Core/IF_UsableInventoryItem.IF_UsableInventoryItem_C',
    '/Game/Sparta/Environments/ItemPool/BP_ItemGiver.BP_ItemGiver_C',
    '/Game/Sparta/Dialogue/Blueprints/DialogueEvents/BP_DE_GainItem.BP_DE_GainItem_C',
    '/Game/Sparta/Dialogue/Blueprints/DialogueEvents/BP_DE_GiveSpecificItem.BP_DE_GiveSpecificItem_C',
    '/Game/Sparta/Dialogue/Blueprints/DialogueEvents/BP_DE_LoseItem.BP_DE_LoseItem_C'
  )
)
SELECT t.path AS cls, substr(o.path, instr(o.path,':')+1) AS name, o.type,
       COALESCE((SELECT r.path FROM obj r WHERE r.addr=UPPER(o.ref)),'') AS refd, o.off
FROM target t JOIN obj o ON o.owner=t.addr
WHERE o.type LIKE '%Property'
  AND substr(o.path, instr(o.path,':')+1) NOT LIKE '%\_GEN\_VARIABLE%' ESCAPE '\'
ORDER BY t.path, o.off
