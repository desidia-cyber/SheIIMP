-- basename sanity check
SELECT substr(c.path, length(rtrim(c.path, replace(c.path,'/','')))+1) AS name,
       substr(s.path, length(rtrim(s.path, replace(s.path,'/','')))+1) AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/Core/Ability/%' ORDER BY c.path
