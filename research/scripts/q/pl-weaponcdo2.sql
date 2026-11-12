SELECT o.type, COUNT(*) n FROM obj o WHERE o.path LIKE '/Game/Sparta/Core/Weapons/Player/%Default__%' GROUP BY o.type ORDER BY n DESC;;
SELECT o.path FROM obj o WHERE o.path LIKE '/Game/Sparta/Core/Weapons/Player/%Default__%' AND (o.type LIKE 'SpartaWeaponComponent%' OR o.type LIKE '%Effects%' OR o.type LIKE '%Ghost%') ORDER BY o.path LIMIT 120
