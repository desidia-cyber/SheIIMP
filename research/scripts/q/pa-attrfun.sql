-- functions (not properties) owned by the Sparta attribute sets
SELECT c.path AS klass, o.path AS fn, o.type
FROM obj c JOIN obj o ON o.owner=c.addr
WHERE c.path IN (
 '/Script/Sparta.SpartaAttributeSet','/Script/Sparta.SpartaHealthSet','/Script/Sparta.SpartaCombatSet',
 '/Script/Sparta.PlayerAttributeSet','/Script/Sparta.EnemyAttributeSet','/Script/Sparta.ShellAbilityAttributeSet',
 '/Script/Sparta.SidearmAttributeSet','/Script/Sparta.DarkPowerAttributeSet')
AND o.type IN ('Function','DelegateFunction','MulticastInlineDelegateProperty','MulticastSparseDelegateProperty','DelegateProperty')
ORDER BY c.path, o.line
