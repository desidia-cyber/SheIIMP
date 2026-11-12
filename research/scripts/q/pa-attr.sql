-- every property of the Sparta attribute sets, resolved type + offset
SELECT c.path AS klass, o.path AS member, o.type, o.off, r.path AS refd
FROM obj c
JOIN obj o ON o.owner = c.addr
LEFT JOIN obj r ON r.addr = UPPER(o.ref)
WHERE c.type='Class' AND c.path IN (
 '/Script/Sparta.SpartaAttributeSet','/Script/Sparta.SpartaHealthSet','/Script/Sparta.SpartaCombatSet',
 '/Script/Sparta.PlayerAttributeSet','/Script/Sparta.EnemyAttributeSet','/Script/Sparta.ShellAbilityAttributeSet',
 '/Script/Sparta.SidearmAttributeSet','/Script/Sparta.DarkPowerAttributeSet')
ORDER BY c.path, o.line
