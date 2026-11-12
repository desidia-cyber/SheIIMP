SELECT c.path, COALESCE(s.path,'') AS parent FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super) WHERE c.path LIKE '/Game/Sparta/Core/Weapons/Player/%' ORDER BY c.path
