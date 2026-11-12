SELECT CASE
  WHEN c.path LIKE '/Game/Sparta/Core/Common/Interactions/%' OR c.path LIKE '/Game/Sparta/Core/Interaction/%' THEN '1 Core Interaction(s)'
  WHEN c.path LIKE '/Game/Sparta/Blueprints/%' THEN '2 Blueprints (DungeonScripts)'
  WHEN c.path LIKE '/Game/Sparta/Characters/SpawnSystem/%' THEN '3 Characters/SpawnSystem'
  WHEN c.path LIKE '/Game/Sparta/Core/AI/%' OR c.path LIKE '/Game/Sparta/Dialogue/NPCs/%' OR c.path LIKE '/Game/Sparta/Maps/Playgrounds/%' THEN '4 NPCs'
  WHEN c.path LIKE '/Game/Sparta/Environments/%' THEN '5 Environments (elevators, item pool)'
  WHEN c.path LIKE '/Game/Sparta/Items/%' THEN '6 Items/Pickups'
  WHEN c.path LIKE '/Game/Sparta/Core/World/%' THEN '7 Core/World (streaming, gates, spline)'
  WHEN c.path LIKE '/Game/Sparta/Maps/LevelDesignTools/%' THEN '8 Maps/LevelDesignTools (BlueprintTools)'
  WHEN c.path LIKE '/Game/Sparta/Dialogue/Blueprints/%' THEN '9 Dialogue/Blueprints (Interactables)'
  WHEN c.path LIKE '/Game/Sparta/Core/Player/%' OR c.path LIKE '/Game/Sparta/UI/%' THEN '10 Player / UI'
  WHEN c.path LIKE '/Script/%' THEN '11 native'
  ELSE '12 other' END AS grp, COUNT(DISTINCT c.path) AS n
FROM obj o JOIN cls c ON c.addr=o.owner JOIN obj r ON r.addr=UPPER(o.ref)
WHERE r.path='/Script/Sparta.SpartaSaveComponent'
GROUP BY grp ORDER BY grp
