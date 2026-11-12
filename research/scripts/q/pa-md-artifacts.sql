SELECT '| `'||c.path||'` | `'||COALESCE(s.path,'')||'` | '||
       CASE WHEN (SELECT COUNT(*) FROM obj o WHERE o.owner=c.addr AND o.type NOT IN ('Function','DelegateFunction') AND o.path NOT LIKE '%UberGraphFrame%')=0
            THEN 'none' ELSE 'yes (count '||(SELECT COUNT(*) FROM obj o WHERE o.owner=c.addr AND o.type NOT IN ('Function','DelegateFunction') AND o.path NOT LIKE '%UberGraphFrame%')||')' END || ' |' AS row
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/Core/Player/Artifacts/%'
ORDER BY c.path
