SELECT o.type AS obj_class, COUNT(*) AS n FROM obj o
WHERE o.path LIKE '%:PersistentLevel%' AND (o.type LIKE 'BPSO\_%' ESCAPE '\' OR o.type LIKE 'BP\_SO\_%' ESCAPE '\'
  OR o.type LIKE 'BP\_SpartaPlayerSaveObject%' ESCAPE '\' OR o.type LIKE 'BP\_PlayerSession%' ESCAPE '\')
GROUP BY o.type ORDER BY n DESC;;
SELECT o.type, o.path FROM obj o
WHERE o.path LIKE '%:PersistentLevel%' AND (o.type='BP_HubDoor_C' OR o.type='BP_HubDoubleDoor_C' OR o.type='BP_HubLift_C'
  OR o.type='BP_Interactable_Chest_C' OR o.type='BP_EndDungeonRewardDoor_C' OR o.type='BP_STH_Entrance_Gate_C'
  OR o.type='SpartaSaveComponent' OR o.type='BP_DoorLockedDungeon_C' OR o.type='BP_BreakableWall_C')
ORDER BY o.type, o.path LIMIT 30;;
SELECT o.type, o.path FROM obj o WHERE o.type IN ('SpartaSaveSubSystem','SpartaSaveGame','DataLayersSave','BP_SpartaSingleton_C') ORDER BY o.type LIMIT 20
