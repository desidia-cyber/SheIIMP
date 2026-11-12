SELECT c.path AS actor_class, substr(o.path, length(c.path)+2) AS prop, printf('0x%x', o.off) AS off, r.path AS save_object
FROM obj o
JOIN cls c ON c.addr = o.owner
JOIN obj r ON r.addr = UPPER(o.ref)
WHERE (r.path LIKE '%BPSO\_%' ESCAPE '\' OR r.path LIKE '%BP\_SO\_%' ESCAPE '\')
ORDER BY r.path, c.path
