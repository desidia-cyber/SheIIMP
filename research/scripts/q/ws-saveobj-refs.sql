SELECT substr(o.owner,1,0) AS dummy, p.path AS owner_path, substr(o.path, length(p.path)+2) AS prop, o.type AS ptype, r.path AS save_obj_class
FROM obj o
JOIN obj p ON p.addr = o.owner
JOIN obj r ON r.addr = UPPER(o.ref)
WHERE r.path LIKE '%BPSO\_%' ESCAPE '\' OR r.path LIKE '%BP\_SO\_%' ESCAPE '\'
ORDER BY r.path, p.path
