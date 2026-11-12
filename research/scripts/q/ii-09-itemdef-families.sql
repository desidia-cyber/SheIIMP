-- Item-definition families: group by 3rd-5th path level under /Game
SELECT
  CASE
    WHEN path LIKE '/Game/Sparta/Items/Pickups/%' THEN '/Game/Sparta/Items/Pickups/*'
    WHEN path LIKE '/Game/Sparta/Items/Shop/%' THEN '/Game/Sparta/Items/Shop/*'
    WHEN path LIKE '/Game/Sparta/Items/Upgrades/%' THEN '/Game/Sparta/Items/Upgrades/*'
    WHEN path LIKE '/Game/Sparta/Items/Tarstones/%' THEN '/Game/Sparta/Items/Tarstones/*'
    ELSE '/Game/Sparta/Items/<other>'
  END AS family, COUNT(*) AS n
FROM cls c JOIN obj s ON s.addr=UPPER(c.super)
WHERE s.path='/Script/Sparta.SpartaItemDefinition'
GROUP BY family ORDER BY n DESC
;;
-- top-level second-level dirs of item definitions (strip last two segments)
SELECT COUNT(*) AS n FROM cls c JOIN obj s ON s.addr=UPPER(c.super) WHERE s.path='/Script/Sparta.SpartaItemDefinition'
