-- count DlgDialogue assets and group them by folder
SELECT SUBSTR(o.path, 1, LENGTH(o.path) - LENGTH(REPLACE(o.path,'/',''))) AS dummy, o.path
FROM obj o WHERE o.type='DlgDialogue' ORDER BY o.path
;;
SELECT COUNT(*) AS n FROM obj WHERE type='DlgDialogue'
