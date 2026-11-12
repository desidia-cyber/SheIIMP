-- one row per player-relevant GA_ blueprint class: name, parent, own non-temp properties
SELECT substr(c.path, instr(c.path,'/')+1) AS fullpath,
       c.path AS path,
       s.path AS parent,
       (SELECT group_concat(substr(o.path, instr(o.path,':')+1), ' | ')
          FROM obj o
         WHERE o.owner = c.addr
           AND o.type NOT IN ('Function','DelegateFunction')
           AND o.path NOT LIKE '%UberGraphFrame%') AS props
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/Core/Player/Ability/%' AND c.path LIKE '%/GA\_%' ESCAPE '\'
  AND c.path NOT LIKE '/Game/Sparta/Core/Player/Ability/Interactions/%'
ORDER BY c.path
