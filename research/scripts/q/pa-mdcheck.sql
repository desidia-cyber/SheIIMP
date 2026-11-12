SELECT 'Sidearm' AS area, SUM(CASE WHEN path LIKE '%/GA\_%' ESCAPE '\' THEN 1 ELSE 0 END) AS ga, COUNT(*) AS total FROM cls WHERE path LIKE '/Game/Sparta/Core/Player/Ability/Sidearm/%';;
SELECT 'Shells' AS area, SUM(CASE WHEN path LIKE '%/GA\_%' ESCAPE '\' THEN 1 ELSE 0 END) AS ga, COUNT(*) AS total FROM cls WHERE path LIKE '/Game/Sparta/Core/Characters/Player/%';;
SELECT 'Tarstones' AS area, SUM(CASE WHEN path LIKE '%/GA\_%' ESCAPE '\' THEN 1 ELSE 0 END) AS ga, COUNT(*) AS total FROM cls WHERE path LIKE '/Game/Sparta/Core/Tarstones/%';;
SELECT 'Artifacts' AS area, SUM(CASE WHEN path LIKE '%/GA\_%' ESCAPE '\' THEN 1 ELSE 0 END) AS ga, COUNT(*) AS total FROM cls WHERE path LIKE '/Game/Sparta/Core/Player/Artifacts/%';;
SELECT 'Upgrades' AS area, SUM(CASE WHEN path LIKE '%/GA\_%' ESCAPE '\' THEN 1 ELSE 0 END) AS ga, COUNT(*) AS total FROM cls WHERE path LIKE '/Game/Sparta/Core/Player/Upgrades/%';;
SELECT 'Interactions' AS area, SUM(CASE WHEN path LIKE '%/GA\_%' ESCAPE '\' THEN 1 ELSE 0 END) AS ga, COUNT(*) AS total FROM cls WHERE path LIKE '/Game/Sparta/Core/Player/Ability/Interactions/%';;
SELECT 'PPItems' AS area, SUM(CASE WHEN path LIKE '%/GA\_%' ESCAPE '\' THEN 1 ELSE 0 END) AS ga, COUNT(*) AS total FROM cls WHERE path LIKE '/Game/Sparta/Items/Pickups/PermProgression/%';;
SELECT 'Input' AS area, SUM(CASE WHEN path LIKE '%/GA\_%' ESCAPE '\' THEN 1 ELSE 0 END) AS ga, COUNT(*) AS total FROM cls WHERE path LIKE '/Game/Sparta/Core/Player/Ability/Input/%'
