SELECT c.type, c.path, s.path AS parent
FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '%WBP_MainMenu%' OR c.path LIKE '%WBP_SaveGameEntry%'
   OR c.path LIKE '%WBP_ConfirmationPrompt%' OR c.path LIKE '%WBP_GameLogo%'
   OR c.path LIKE '%WBP_StartupVideos%' OR c.path LIKE '%WBP_GamePaused%'
   OR c.path LIKE '%WBP_DeathScreen%' OR c.path LIKE '%WBP_Credits%'
   OR c.path LIKE '%WBP_ShaderCompiling%' OR c.path LIKE '%WBP_StreamingScreen%'
   OR c.path LIKE '%WBP_GameCompletion%' OR c.path LIKE '%WBP_LoadingScreen%'
   OR c.path LIKE '%StreamingScreenWidget%' OR c.path LIKE '%WBP_MainMenu_%'
ORDER BY c.path
