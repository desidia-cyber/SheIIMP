-- Functions + full ordered parameter lists for shop / merchant / drop / forge blueprints.
-- Compiler temporaries filtered per QUERYING.md (Temp_, CallFunc_, K2Node_, __Internal_,
-- ExecuteUbergraph_, Goto_, eb_, bpf_).
WITH target(addr, path) AS (
  SELECT addr, path FROM cls WHERE path IN (
    '/Game/Sparta/UI/Menu/Merchant/BP_ShopHandlerBase.BP_ShopHandlerBase_C',
    '/Game/Sparta/UI/Menu/Merchant/BPFL_Shops.BPFL_Shops_C',
    '/Game/Sparta/Dialogue/NPCs/BP_Merchant.BP_Merchant_C',
    '/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Interactable_ShopItem.BP_Interactable_ShopItem_C',
    '/Game/Sparta/Core/Ability/GA_DeathBase.GA_DeathBase_C',
    '/Game/Sparta/Items/Pickups/Core/BP_RewardData.BP_RewardData_C',
    '/Game/Sparta/Items/Pickups/Core/BP_RD_PickUpItem.BP_RD_PickUpItem_C',
    '/Game/Sparta/Items/Pickups/Core/BP_RD_Currency.BP_RD_Currency_C',
    '/Game/Sparta/Items/Pickups/BP_PickupBase.BP_PickupBase_C',
    '/Game/Sparta/Items/Pickups/BP_CraftingItemPickup.BP_CraftingItemPickup_C',
    '/Game/Sparta/Environments/ItemPool/BP_ItemGiver.BP_ItemGiver_C',
    '/Game/Sparta/Dialogue/Blueprints/DialogueEvents/BP_DE_GainItem.BP_DE_GainItem_C',
    '/Game/Sparta/Dialogue/Blueprints/DialogueEvents/BP_DE_GiveSpecificItem.BP_DE_GiveSpecificItem_C',
    '/Game/Sparta/Dialogue/Blueprints/DialogueEvents/BP_DE_LoseItem.BP_DE_LoseItem_C',
    '/Game/Sparta/Core/Player/Upgrades/Core/BPFL_Tarstones.BPFL_Tarstones_C',
    '/Game/Sparta/Core/Player/Upgrades/Core/BPFL_Upgrades.BPFL_Upgrades_C',
    '/Game/Sparta/Core/Player/Upgrades/Tarforge/BPFL_Tarforge.BPFL_Tarforge_C',
    '/Game/Sparta/Core/Player/Upgrades/Core/BP_UpgradePoolHandler.BP_UpgradePoolHandler_C',
    '/Game/Sparta/Core/Common/Interactions/Blueprints/BP_Tarforge.BP_Tarforge_C'
  )
)
SELECT t.path AS cls, substr(f.path, instr(f.path,':')+1) AS fn,
  COALESCE((SELECT group_concat(sig, ' , ') FROM (
      SELECT (substr(o.path, length(f.path)+2) || ': ' || o.type ||
              CASE WHEN r.path IS NOT NULL THEN ' of ' || r.path ELSE '' END) AS sig
      FROM obj o LEFT JOIN obj r ON r.addr=UPPER(o.ref)
      WHERE o.owner=f.addr ORDER BY o.line)),'') AS params
FROM target t JOIN obj f ON f.owner=t.addr AND f.type='Function'
WHERE substr(f.path, instr(f.path,':')+1) NOT LIKE 'Temp\_%' ESCAPE '\'
  AND substr(f.path, instr(f.path,':')+1) NOT LIKE 'CallFunc\_%' ESCAPE '\'
  AND substr(f.path, instr(f.path,':')+1) NOT LIKE 'K2Node\_%' ESCAPE '\'
  AND substr(f.path, instr(f.path,':')+1) NOT LIKE '\_\_Internal\_%' ESCAPE '\'
  AND substr(f.path, instr(f.path,':')+1) NOT LIKE 'ExecuteUbergraph%'
  AND substr(f.path, instr(f.path,':')+1) NOT LIKE 'Goto\_%' ESCAPE '\'
ORDER BY t.path,
  CASE substr(f.path, instr(f.path,':')+1)
    WHEN 'Template_Shop' THEN 0 WHEN 'OnShopItemBought' THEN 1 WHEN 'OnShopItemSold' THEN 2
    WHEN 'Purchase' THEN 3 WHEN 'Sell' THEN 4 ELSE 5 END,
  substr(f.path, instr(f.path,':')+1)
