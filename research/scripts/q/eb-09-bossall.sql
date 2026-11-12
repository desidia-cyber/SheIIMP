-- everything with 'Boss' in the path that is a class (native or BP)
SELECT c.type || ' | ' || c.path || ' | ' || COALESCE(s.path,'?')
FROM cls c LEFT JOIN obj s ON s.addr = UPPER(c.super)
WHERE c.path LIKE '%Boss%' AND c.type NOT LIKE '%Property'
ORDER BY c.path
