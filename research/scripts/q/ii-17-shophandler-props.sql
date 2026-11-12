-- Properties of BP_ShopHandlerBase (+ its two delegate-rich companions) and of BP_Merchant
WITH target(addr, path) AS (
  SELECT addr, path FROM cls WHERE path IN (
    '/Game/Sparta/UI/Menu/Merchant/BP_ShopHandlerBase.BP_ShopHandlerBase_C',
    '/Game/Sparta/Dialogue/NPCs/BP_Merchant.BP_Merchant_C',
    '/Game/Sparta/Core/Player/Upgrades/Core/BP_UpgradePoolHandler.BP_UpgradePoolHandler_C'
  )
)
SELECT t.path AS cls, substr(o.path, instr(o.path,':')+1) AS name, o.type,
       COALESCE((SELECT r.path FROM obj r WHERE r.addr=UPPER(o.ref)),'') AS refd, o.off
FROM target t JOIN obj o ON o.owner=t.addr
WHERE o.type LIKE '%Property' AND o.type NOT LIKE 'Delegate%'
  AND substr(o.path, instr(o.path,':')+1) NOT LIKE '%\_GEN\_VARIABLE%' ESCAPE '\'
ORDER BY t.path, o.off
;;
-- Delegate properties (events) of the same classes
WITH target(addr, path) AS (
  SELECT addr, path FROM cls WHERE path IN (
    '/Game/Sparta/UI/Menu/Merchant/BP_ShopHandlerBase.BP_ShopHandlerBase_C',
    '/Game/Sparta/Dialogue/NPCs/BP_Merchant.BP_Merchant_C'
  )
)
SELECT t.path AS cls, substr(o.path, instr(o.path,':')+1) AS name, o.type, o.off
FROM target t JOIN obj o ON o.owner=t.addr
WHERE o.type LIKE '%Delegate%' ORDER BY t.path, o.off
