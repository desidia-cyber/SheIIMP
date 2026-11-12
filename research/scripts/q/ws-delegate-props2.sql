SELECT c.path AS cls, substr(o.path, length(c.path)+2) AS delegate_name, o.type AS dtype, printf('0x%x', o.off) AS off
FROM obj o JOIN cls c ON c.addr=o.owner
WHERE o.type IN ('MulticastInlineDelegateProperty','MulticastSparseDelegateProperty','DelegateProperty')
  AND (c.path LIKE '%BP_InteractableBase.BP_InteractableBase_C' OR c.path LIKE '%BP_Interactable_Chest.BP_Interactable_Chest_C'
       OR c.path LIKE '%BP_Spawner.BP_Spawner_C' OR c.path LIKE '%BP_PlayerController.BP_PlayerController_C'
       OR c.path LIKE '%BPC_InteractionHandler.BPC_InteractionHandler_C' OR c.path LIKE '%BP_Tarforge.BP_Tarforge_C'
       OR c.path LIKE '%BP_HubDoor.BP_HubDoor_C' OR c.path LIKE '%BP_Interactable_Chest_General.BP_Interactable_Chest_General_C'
       OR c.path LIKE '%BP_STH_Entrance.BP_STH_Entrance_C')
ORDER BY c.path, o.off
