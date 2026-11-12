SELECT 'Core/Player/Ability' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Core/Player/Ability/%';;
SELECT 'Core/Player/Upgrades' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Core/Player/Upgrades/%';;
SELECT 'Core/Player/Artifacts' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Core/Player/Artifacts/%';;
SELECT 'Core/Player/other' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Core/Player/%' AND path NOT LIKE '/Game/Sparta/Core/Player/Ability/%' AND path NOT LIKE '/Game/Sparta/Core/Player/Upgrades/%' AND path NOT LIKE '/Game/Sparta/Core/Player/Artifacts/%';;
SELECT 'Core/Characters/Player' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Core/Characters/Player/%';;
SELECT 'Core/Tarstones' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Core/Tarstones/%';;
SELECT 'Core/Ability' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Core/Ability/%';;
SELECT 'Weapons' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Weapons/%';;
SELECT 'Characters/Shells' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Characters/Shells/%';;
SELECT 'Characters/Player' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Characters/Player/%';;
SELECT 'Core/Weapons' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Core/Weapons/%';;
SELECT 'Core/Sidearm' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Core/Sidearm/%';;
SELECT 'Core/Seals' AS area, COUNT(*) AS n FROM cls WHERE path LIKE '/Game/Sparta/Core/Seals/%'
