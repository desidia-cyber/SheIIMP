SELECT c.path AS actor_class, substr(o.path, length(c.path)+2) AS save_component_prop, printf('0x%x', o.off) AS off
FROM obj o
JOIN cls c ON c.addr = o.owner
JOIN obj r ON r.addr = UPPER(o.ref)
WHERE r.path = '/Script/Sparta.SpartaSaveComponent'
ORDER BY c.path
