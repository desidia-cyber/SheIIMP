SELECT f.path AS fn, p.path AS param, p.type AS ptype, r.path AS refd, p.off AS off
FROM obj f
JOIN obj p ON UPPER(p.owner) = f.addr
LEFT JOIN obj r ON r.addr = UPPER(p.ref)
WHERE f.type='Function'
  AND f.path LIKE '/Game/Sparta/Core/Player/BPFL_Player_Equipment.BPFL_Player_Equipment_C:%'
  AND p.type LIKE '%Property'
  AND p.path NOT LIKE '%:Temp_%' AND p.path NOT LIKE '%:CallFunc_%' AND p.path NOT LIKE '%:K2Node_%'
  AND p.path NOT LIKE '%:__Internal_%' AND p.path NOT LIKE '%:Goto_%' AND p.path NOT LIKE '%:eb_%'
  AND p.path NOT LIKE '%:bpf_%'
ORDER BY f.line, p.line
