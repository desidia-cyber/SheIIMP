-- every Function whose own name matches travel/teleport/level-load/respawn vocabulary
SELECT o.path
FROM obj o
WHERE o.type='Function'
  AND (o.path LIKE '%:Travel%' OR o.path LIKE '%:Teleport%' OR o.path LIKE '%:LoadMap%'
    OR o.path LIKE '%:OpenLevel%' OR o.path LIKE '%:SetLevel%' OR o.path LIKE '%:StreamingLevel%'
    OR o.path LIKE '%:Respawn%' OR o.path LIKE '%:FastTravel%' OR o.path LIKE '%:Warp%'
    OR o.path LIKE '%:ZoneTransition%' OR o.path LIKE '%:Transition%' OR o.path LIKE '%:LoadLevel%'
    OR o.path LIKE '%:UnloadLevel%' OR o.path LIKE '%:Shelleport%' OR o.path LIKE '%:ShellPort%'
    OR o.path LIKE '%:Seamless%' OR o.path LIKE '%:MapLoad%' OR o.path LIKE '%:LoadStreamLevel%'
    OR o.path LIKE '%:RequestLevel%' OR o.path LIKE '%:ChangeLevel%' OR o.path LIKE '%:GoToMap%')
ORDER BY o.path
