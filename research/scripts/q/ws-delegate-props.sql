SELECT c.path AS cls, substr(o.path, length(c.path)+2) AS delegate_name, o.type AS dtype, printf('0x%x', o.off) AS off
FROM obj o JOIN cls c ON c.addr=o.owner
WHERE o.type IN ('MulticastInlineDelegateProperty','MulticastSparseDelegateProperty','DelegateProperty')
  AND (c.path LIKE '%BP_HubLift%' OR c.path LIKE '%BP_InteractableBase.C%' OR c.path LIKE '%BPO_STH%'
       OR c.path LIKE '%BP_VFX_Breakable.C%' OR c.path LIKE '%BP_Spawner.C%' OR c.path LIKE '%BP_HiddenWall%'
       OR c.path LIKE '%BP_Interactable_Chest.C%' OR c.path LIKE '%BP_HubDoor%' OR c.path LIKE '%BP_LandingArea%'
       OR c.path LIKE '%BP_PlayerController.C%' OR c.path LIKE '%BP_Tarforge%' OR c.path LIKE '%BPC_InteractionHandler.C%')
ORDER BY c.path, o.off
