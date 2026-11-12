SELECT '| `'||c.path||'` | `'||COALESCE(s.path,'')||'` | '||
       CASE WHEN (SELECT COUNT(*) FROM obj o WHERE o.owner=c.addr AND o.type NOT IN ('Function','DelegateFunction') AND o.path NOT LIKE '%UberGraphFrame%')=0
            THEN 'none' ELSE 'see 8.2' END || ' |' AS row
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/Core/Player/Ability/Input/%'
ORDER BY c.path
