SELECT 'Upgrades' AS area, substr(c.path, length(rtrim(c.path, replace(c.path,'/','')))+1) AS name,
       substr(s.path, length(rtrim(s.path, replace(s.path,'/','')))+1) AS parent,
       (SELECT COUNT(*) FROM obj o WHERE o.owner=c.addr AND o.type NOT IN ('Function','DelegateFunction') AND o.path NOT LIKE '%UberGraphFrame%') AS nprops
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/Core/Player/Upgrades/%' AND c.path LIKE '%/GA\_%' ESCAPE '\'
ORDER BY c.path
