SELECT COUNT(*) AS all_skeletal_mesh_sockets FROM obj WHERE type='SkeletalMeshSocket';;
SELECT COUNT(*) AS all_static_mesh_sockets FROM obj WHERE type='StaticMeshSocket';;
SELECT COUNT(*) AS pwsms_sockets FROM obj WHERE type='StaticMeshSocket' AND path LIKE '/Game/Sparta/Weapons/Player/%';;
SELECT COUNT(*) AS player_wp_classes FROM cls WHERE path LIKE '/Game/Sparta/Core/Weapons/Player/%' AND path LIKE '%WP\_%' ESCAPE '\';;
SELECT COUNT(*) AS shell_bp_classes FROM cls WHERE path LIKE '/Game/Sparta/Core/Characters/Player/%' AND (path LIKE '%/BP\_Shell\_%' ESCAPE '\' OR path LIKE '%/BP\_Darkform\_%' ESCAPE '\' OR path LIKE '%/BP_DarkForm_%');;
SELECT COUNT(*) AS slots FROM obj WHERE type='SkeletalMeshSocket' AND path LIKE '%/Game/Sparta/Weapons/Player/%'
