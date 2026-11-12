SELECT path FROM obj WHERE type='Package' AND path LIKE '/Game/Sparta/Characters/Humans/Player/Animations/ABPL_Locomotion%' ORDER BY path;;
SELECT type, path FROM obj WHERE path LIKE '/Game/Sparta/Characters/Humans/Player/Animations/ABP_Player%' AND type IN ('AnimBlueprintGeneratedClass','AnimBlueprint') ORDER BY path
