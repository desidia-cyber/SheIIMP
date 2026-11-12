SELECT o.type, o.path FROM obj o
WHERE o.path LIKE '%:PersistentLevel.%' AND o.type IN ('BP_HubDoor_C','BP_HubDoubleDoor_C','BP_HubLift_C','BP_HubLiftLever_C','BP_EndDungeonRewardDoor_C','BP_DoorLockedDungeon_C','BP_Interactable_Lock_Door_C','BP_SpartaPlayerStart_C','BP_LandingArea_Checkpoint_C','BP_BreakableWall_C','BP_HiddenWall_C')
ORDER BY o.type, o.path LIMIT 20;;
SELECT o.path FROM obj o WHERE o.type='SpartaSaveComponent' AND o.path LIKE '%BP_HubDoor_C%' LIMIT 5;;
SELECT o.path FROM obj o WHERE o.type='SpartaSaveComponent' AND o.path LIKE '%BP_Interactable_Lock_Door_C%' LIMIT 5
