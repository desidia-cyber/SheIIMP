# 70 — Game flow, menus, and the moments a mod must pause sync

Area: **game flow, menus, and every moment where a co-op mod must pause sync or hand over control.**
Target: Mortal Shell II, UE 5.6 shipping build, UE4SS Lua modding.

All facts below come from the two indexed dumps in `research\raw\` (`index.sqlite3`, and the live
`ObjectDump_ingame.txt` where noted) and the UE4SS CXX headers in `research\raw\CXXHeaderDump\`.
Queries were run as documented in `scripts\QUERYING.md`
(`parse_dump.py classes|show|fun|find|raw`, `q.py ... scripts\q\fm-*.sql`).

## Conventions used in this file

| Convention | Meaning |
|---|---|
| Evidence blocks | Verbatim lines from `python scripts\parse_dump.py raw raw\index.sqlite3 <regex>`. Nothing else. |
| `[o: 0xNN]` | Member offset, exactly as the dump prints it (`[o: 38]` = `0x38`). |
| Compiler temporaries | Blueprint parameters named `CallFunc_*`, `K2Node_*`, `Temp_*`, `__Internal_*`, `ExecuteUbergraph_*`, `Goto_*`, `ReturnValue*` (except where `ReturnValue` *is* the return value) are **filtered out** of every signature table. Stated once here; this is the documented rule from `QUERYING.md`. |
| `UNKNOWN` | Not established. One line says what was tried. |
| `GUESS:` | Explicitly labelled interpretation, not a fact. |

The dump does **not** mark out-parameters. Where a parameter is the result of the function it is
shown as the engine named it (`ReturnValue`, `Success`, `OutTransform`, …).

Object-dump addresses are per-session; the raw lines are quoted with the addresses the dump has.
Do **not** hard-code them.

---

# 0. Map of the flow, and who owns each moment

| # | Moment | Owner (class) | Starts with | Ends / observable at |
|---|---|---|---|---|
| 1 | Boot → startup videos | `WBP_StartupVideos_C` | `Construct` | `OnComplete` / `OnPreComplete` delegates |
| 2 | Main menu visible | `WBP_MainMenu_C` (child of `BP_SpartaGameInstance_C`) | `Construct`, `OnMenuOpen(TabIndex, SubTabIndex)` | `OnMenuClose`, `GlobalEvent_OnMenuClose_C` |
| 3 | Continue | `WBP_MainMenu_C` | `ContinueGame` | `LoadMap` → `InitializeLoadingScreen` |
| 4 | New game | `WBP_MainMenu_C` | `StartNewGame(ActiveSlot, SkipPrologue)` | same as 3 |
| 5 | Load game | `WBP_MainMenu_C` | `LoadGame(ActiveSlot)` | same as 3 |
| 6 | Map actually loads | `BPFL_UI_C` | `TriggerLoadingScreen(Direction, ZOrder, OnCompleted, __WorldContext)` then `UGameplayStatics.OpenLevel` | `WBP_LoadingScreen_C:BroadcastCompletedEvent` → `BP_GlobalEvent_LoadingScreenCompleted_C` |
| 7 | In‑game pause / menu open | `BPFL_UI_C` + `BPC_UserInterfaceHandler_C` + `WBP_Menu_Game_C` | `EnableUIInput(..., PauseGame, ...)`, `SpartaPlayerController:SetShouldPerformFullTickWhenPaused` | `DisableUIInput(..., UnpauseGame, ...)`, `GlobalEvent_OnMenuClose_C` |
| 8 | Death | `GA_Death_Player_C` | `HandlePlayerDeath(GrantDeathState)` → `CreateDeathScreen` | `WBP_DeathScreen_C:Construct` |
| 9 | Respawn | `WBP_DeathScreen_C` | `OnDeathSelection` → `ResetWorld` → `CreateLoadingScreen` | `OnLoadingScreenCompleted` → `EndDeath` |
| 10 | Cutscene | `BP_CinematicPlayer_C` | `PlayCutscene` / `StartCinematicViaEvent(EventData)` / `TriggerCutsceneViaInteraction` | `BroadcastCinematicEnd`, `CinematicEndReached` (delegate `0x2e8`) |
| 11 | Credits | `BPFL_UI_C:PlayCredits` | `PlayCredits(...)` | `WBP_Credits_C:OnCreditsCompleted` (delegate `0x860`) |
| 12 | Game completion | `BPFL_UI_C:TriggerGameCompletionScreen` | `TriggerGameCompletionScreen(ZOrder, __WorldContext)` | `WBP_GameCompletion_C:OnMenuClose` |
| 13 | Session save/reset | `SpartaSaveSubSystem` | `SaveToDisk`, `SaveSession`, `ResetSession` | delegates `OnSaveStarted` / `OnSaveFinished` / `OnSaveGameLoaded` / `PostSessionReset` |
| 14 | World reset (respawn/transition) | `SpartaWorldGenSubsystem` | `BeginWorldReset()` | `UNKNOWN` (no completion delegate found; see §7) |

---

# 1. Main menu, Continue / New game / Load game / Quit

## 1.1 Native classes in the flow

| Class | Parent | Purpose (from names) | Sync relevance |
|---|---|---|---|
| `/Script/Sparta.SpartaGameInstance` | `/Script/CSGameFramework.CSGameInstance` | game instance; owns loading screen class, audio/VFX/lighting managers, entitlements | lives across level travel → the natural carrier for mod state |
| `/Script/Sparta.SpartaGameMode` | `/Script/Engine.GameModeBase` | has `bResumingSession`, `ShouldResumeRun`, `GetStartTransformFromSessionSave` | "am I resuming a run" is decided here |
| `/Script/Sparta.SpartaLocalPlayer` | `/Script/CommonGame.CommonLocalPlayer` | local player; settings, input mapping context | per‑player, not replicated |
| `/Script/Sparta.SpartaUIManagerSubsystem` | `/Script/CommonGame.GameUIManagerSubsystem` | UI manager subsystem; **declares no members of its own** | GUESS: layer stack comes from the CommonGame parent |
| `/Script/Sparta.SpartaSingleton` | `/Script/CoreUObject.Object` | global accessors: `QuestDataTable`, `EnemySpawnTypes` | process‑wide singleton |
| `/Script/Sparta.SpartaPlayerController` | `/Script/CSGameFramework.CSPlayerController` | player controller; `SetShouldPerformFullTickWhenPaused`, input binding, `OnChangeShellItem` | pause semantics + input hand‑off |
| `/Script/Sparta.SpartaWidget` | `/Script/UMG.UserWidget` | base class for several Sparta widgets (`WBP_ShaderCompiling`, `WBP_IndicatorBase`, `WBP_Prompt`, …) | — |
| `/Script/Sparta.StreamingScreenWidget` | `/Script/UMG.UserWidget` | base of `WBP_StreamingScreen_C`; only declares `OnStreamingFinished` | streaming‑screen boundary |
| `/Script/Sparta.SpartaCinematicAudioHandler` | `/Script/CoreUObject.Object` | `PlayCinematic` / `StopCinematic` / `OnCinematicStart` / `OnCinematicEnd` | cinematic boundary for audio |
| `/Script/Sparta.SpartaCompletionLibrary` | `/Script/Engine.BlueprintFunctionLibrary` | `SnapshotRunCompletion`, `GetRunCompletionResult`, `GetRunTimeSeconds`, … | end‑of‑run statistics |
| `/Script/Sparta.SpartaSaveSubSystem` | `/Script/Engine.GameInstanceSubsystem` | the whole save system (73 members, 5 delegates) | §1.9 |
| `/Script/Sparta.SpartaSaveGame` | `/Script/Engine.SaveGame` | save game object | — |
| `/Script/Sparta.SpartaSessionSaveObjectBase` | `/Script/Sparta.SpartaSaveObject` | per‑session save object base | — |
| `/Script/Sparta.SpartaWorldGenSubsystem` | `/Script/Engine.WorldSubsystem` | `BeginWorldReset`, difficulty, `AddTagToCurrentWorldState` | respawn / world reset |

Evidence:

```
[00007FF46F044338] Class /Script/Sparta.SpartaCinematicAudioHandler [n: 48E52] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F044738] Class /Script/Sparta.SpartaCompletionLibrary [n: 48E92] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE70138]
[00007FF46F047538] Class /Script/Sparta.SpartaGameInstance [n: 491A0] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F009338]
[00007FF46F047738] Class /Script/Sparta.SpartaGameMode [n: 491EC] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF4B338]
[00007FF46F055938] Class /Script/Sparta.SpartaPlayerController [n: 49FE4] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F009538]
[00007FF46F057138] Class /Script/Sparta.SpartaSaveGame [n: 4A29B] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF4A738]
[00007FF46F057B38] Class /Script/Sparta.SpartaSaveSubSystem [n: 4A2D5] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF5DF38]
[00007FF46F057D38] Class /Script/Sparta.SpartaSessionSaveObjectBase [n: 4A594] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F038338]
[00007FF46F058138] Class /Script/Sparta.SpartaSingleton [n: 4A5B1] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[00007FF46F05FD38] Class /Script/Sparta.SpartaWidget [n: 4AEC4] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEB3938]
[00007FF46F060338] Class /Script/Sparta.SpartaWorldGenSubsystem [n: 4AEFE] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7C138]
[00007FF46F062138] Class /Script/Sparta.StreamingScreenWidget [n: 4B055] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEB3938]
[00007FF46F064F38] Class /Script/Sparta.SpartaLocalPlayer [n: 4BAE5] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F007B38]
[00007FF46F069D38] Class /Script/Sparta.SpartaUIManagerSubsystem [n: 4C412] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F008338]
[00007FF46F06B338] Class /Script/Sparta.SpartaLoadingScreenSubsystem [n: 4C509] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF5DF38]
```

## 1.2 `/Script/Sparta.SpartaGameInstance` — properties

| Property | Type | Offset | Notes |
|---|---|---|---|
| `LoadingScreenClass` | `ClassProperty` | `0x218` | widget class used for loading screens |
| `bUseLoadingScreenDPIScaler` | `BoolProperty` | `0x220` | |
| `UIAudioHandler` | `ObjectProperty of /Script/Sparta.SpartaUIAudioHandler` | `0x248` | |
| `MusicManager` | `ObjectProperty of /Script/Sparta.SpartaMusicManager` | `0x250` | |
| `AmbienceManager` | `ObjectProperty of /Script/Sparta.SpartaAmbienceManager` | `0x258` | |
| `CinematicAudioHandler` | `ObjectProperty of /Script/Sparta.SpartaCinematicAudioHandler` | `0x260` | |
| `VfxManager` | `ObjectProperty of /Script/Sparta.SpartaVfxManager` | `0x268` | |
| `LightingManager` | `ObjectProperty of /Script/Sparta.SpartaLightingManager` | `0x270` | |
| `EntitlementsTable` | `ObjectProperty of /Script/Engine.DataTable` | `0x278` | |
| `PlatformEntitlementMap` | `MapProperty` (key `StrProperty`, value `StrProperty`) | `0x280` | |
| `OwnedEntitlements` | `ArrayProperty of StrProperty` | `0x2d0` | |
| `OnEntitlementsUpdated` | `MulticastInlineDelegateProperty` | `0x2f0` | **event** |
| `OnApplicationToBackground` | `MulticastInlineDelegateProperty` | `0x300` | **event** |
| `OnApplicationToForeground` | `MulticastInlineDelegateProperty` | `0x310` | **event** |
| `OnApplicationStateChanged` | `MulticastInlineDelegateProperty` | `0x320` | **event** |

## 1.3 `/Script/Sparta.SpartaGameInstance` — functions (full parameter lists)

| Function | Parameters (name: type) | Notes |
|---|---|---|
| `BeginLoadingScreen` | `MapName: StrProperty` | start of a loading screen |
| `EndLoadingScreen` | `InLoadedWorld: ObjectProperty of /Script/Engine.World` | end of a loading screen |
| `SetPlayStreamingPauseMovie` | `bPlayMovie: BoolProperty` | plays a movie while streaming is paused |
| `Get` | `WorldContextObject: ObjectProperty of /Script/CoreUObject.Object`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaGameInstance` | static accessor |
| `UserHasEntitlement` | `Entitlement: StructProperty of /Script/Engine.DataTableRowHandle`, `ReturnValue: BoolProperty` | |
| `RestartDLSSGGuardAndWait` | `Callback: DelegateProperty` | takes a delegate |
| `GetCachedDLSSGMode` | `ReturnValue: IntProperty` | |
| `PostInit` | — | |
| `BP_PostEngineInit` | — | Blueprint‑implementable init hook |
| `CreateUIAudioHandler` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaUIAudioHandler` | |
| `CreateMusicManager` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaMusicManager` | |
| `CreateAmbienceManager` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaAmbienceManager` | |
| `CreateCinematicAudioHandler` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaCinematicAudioHandler` | |
| `CreateVfxManger` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaVfxManager` | (sic: "Manger") |
| `CreateLightingManager` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaLightingManager` | |

`BeginLoadingScreen` / `EndLoadingScreen` are the only loading‑screen start/end pair on the
game instance. Note there is **no** `LoadMap`, `Quit`, `ContinueGame`, `StartNewGame` or
`LoadGame` function on `SpartaGameInstance` — those live on the widget/function library
(see §1.7, §1.8, §1.10).

## 1.4 `/Script/Sparta.SpartaGameMode` — full member list

| Member | Type | Offset | Notes |
|---|---|---|---|
| `StreamingScreenClass` | `ClassProperty` | `0x340` | class of `WBP_StreamingScreen_C` (GUESS: from name + the widget's parent `StreamingScreenWidget`) |
| `bResumingSession` | `BoolProperty` | `0x358` | "we are resuming, not starting fresh" |
| `ConsoleExtensionComponent` | `ObjectProperty of /Script/CSUtility.CSConsoleExtensionComponent` | `0x360` | |

| Function | Parameters |
|---|---|
| `ShouldResumeRun` | `ReturnValue: BoolProperty` |
| `GetStartTransformFromSessionSave` | `OutTransform: StructProperty of /Script/CoreUObject.Transform`, `ReturnValue: BoolProperty` |
| `GetInstance` | `WorldContextObject: ObjectProperty of /Script/CoreUObject.Object`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaGameMode` |

`BP_SpartaGameMode_C` (`/Game/Sparta/Core/GameModes/BP_SpartaGameMode_C`, parent
`/Script/Sparta.SpartaGameMode`) adds the player‑start selection used by respawn and travel:
`ChoosePlayerStart`, `FindCustomPlayerStart`, `SelectCustomPlayerStart`, `GetAllPlayerStart`,
`GetCachedSpartaPlayerStarts`, `GetCurrentCustomPlayerStart`, `GetDefaultPlayerStartName`,
`BroadcastPlayerStartFound`.

## 1.5 `SpartaLocalPlayer`, `SpartaUIManagerSubsystem`, `SpartaSingleton`

`/Script/Sparta.SpartaLocalPlayer` (parent `/Script/CommonGame.CommonLocalPlayer`):

| Member | Type | Offset |
|---|---|---|
| `SharedSettings` | `ObjectProperty of /Script/Sparta.SpartaSettingsShared` | `0x300` |
| `InputMappingContext` | `ObjectProperty of /Script/EnhancedInput.InputMappingContext` | `0x338` |
| `LastBoundPC` | `WeakObjectProperty of /Script/Engine.PlayerController` | `0x340` |

| Function | Parameters |
|---|---|
| `GetSharedSettings` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSettingsShared` |
| `GetLocalSettings` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaGameUserSettings` |
| `OnCompletedAudioDeviceSwap` | `SwapResult: StructProperty of /Script/AudioMixer.SwapAudioOutputResult` |

`/Script/Sparta.SpartaSingleton` (parent `/Script/CoreUObject.Object`):

| Member | Type | Offset |
|---|---|---|
| `QuestDataTable` | `ObjectProperty of /Script/Engine.DataTable` | `0x28` |
| `EnemySpawnTypes` | `ObjectProperty of /Script/Engine.DataTable` | `0x30` |

| Function | Parameters |
|---|---|
| `GetInstance` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSingleton` |
| `GetInstanceUnchecked` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSingleton` |

`/Script/Sparta.SpartaUIManagerSubsystem` has **no members of its own** in the dump — it is a
pure `CommonGame.GameUIManagerSubsystem` subclass. Nothing about it can be established beyond the
inheritance, so any UI layer API must be looked up on the CommonGame parent.
UNKNOWN: the CommonGame layer push/pop functions for `/Script/CommonGame.GameUIManagerSubsystem`
were not enumerated in this pass (would need `parse_dump.py show raw\index.sqlite3 /Script/CommonGame.GameUIManagerSubsystem`).

Evidence for all three classes is in the block at the end of §1.1 (the class-declaration lines for
`SpartaLocalPlayer`, `SpartaUIManagerSubsystem` and `SpartaSingleton`). There is no additional
evidence to cite: `SpartaUIManagerSubsystem` has exactly zero member rows, which is itself the
finding.

## 1.6 Widget Blueprint classes under `/Game/Sparta/UI/`

Enumerated with SQL, not guessed (`scripts\q\fm-widgets.sql`,
`SELECT c.type,c.path,s.path FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super) WHERE c.type='WidgetBlueprintGeneratedClass' …`).
The full `/Game/Sparta/UI/**` widget list is ~200 entries; the flow‑relevant ones are:

| Class path | Parent | Role |
|---|---|---|
| `/Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C` | `/Script/UMG.UserWidget` | main menu root |
| `/Game/Sparta/UI/MainMenu/WBP_MGT_MainScreen.WBP_MGT_MainScreen_C` | `/Game/Sparta/UI/Menu/WBP_Menu_Game_Tab.WBP_Menu_Game_Tab_C` | Continue / New game / Load / Options buttons |
| `/Game/Sparta/UI/MainMenu/WBP_MGT_SaveSlots.WBP_MGT_SaveSlots_C` | `WBP_Menu_Game_Tab_C` | slot picker |
| `/Game/Sparta/UI/MainMenu/WBP_MGT_SaveRecovery.WBP_MGT_SaveRecovery_C` | `WBP_Menu_Game_Tab_C` | save recovery |
| `/Game/Sparta/UI/MainMenu/WBP_SaveGameEntry.WBP_SaveGameEntry_C` | `/Game/Sparta/UI/Core/Navigation/WBP_Navigable.WBP_Navigable_C` | one save entry row |
| `/Game/Sparta/UI/MainMenu/WBP_GameLogo.WBP_GameLogo_C` | `/Script/UMG.UserWidget` | logo (`AdjustScale`, `PreConstruct`) |
| `/Game/Sparta/UI/Loading/WBP_StartupVideos.WBP_StartupVideos_C` | `/Script/UMG.UserWidget` | publisher/logo videos |
| `/Game/Sparta/UI/Loading/WBP_LoadingScreenBase.WBP_LoadingScreenBase_C` | `/Script/UMG.UserWidget` | loading‑screen base, declares only `OnCompleted__DelegateSignature` |
| `/Game/Sparta/UI/Loading/WBP_LoadingScreen.WBP_LoadingScreen_C` | `WBP_LoadingScreenBase_C` | the normal loading screen |
| `/Game/Sparta/UI/Loading/WBP_LoadingScreen_FadeToBlack.WBP_LoadingScreen_FadeToBlack_C` | `WBP_LoadingScreenBase_C` | fade variant |
| `/Game/Sparta/UI/Loading/WBP_LS_FadeToBlack_AllowPauseMenu.WBP_LS_FadeToBlack_AllowPauseMenu_C` | `WBP_LoadingScreen_FadeToBlack_C` | fade + pause menu allowed |
| `/Game/Sparta/UI/Loading/WBP_LS_TransitionBase.WBP_LS_TransitionBase_C` | `/Script/UMG.UserWidget` | generic screen transition (`FadeIn`, `FadeOut`, `ClearImmediately`) |
| `/Game/Sparta/UI/Loading/WBP_ShaderCompiling.WBP_ShaderCompiling_C` | `/Script/Sparta.SpartaWidget` | shader compile screen, delegate `OnCompilationComplete` |
| `/Game/Sparta/UI/Loading/WBP_MGT_ShaderCompilation.WBP_MGT_ShaderCompilation_C` | `WBP_Menu_Game_Tab_C` | shader‑compile menu tab |
| `/Game/Sparta/UI/Menu/WBP_StreamingScreen.WBP_StreamingScreen_C` | `/Script/Sparta.StreamingScreenWidget` | streaming screen |
| `/Game/Sparta/UI/Menu/WBP_GamePaused.WBP_GamePaused_C` | `/Script/UMG.UserWidget` | "PAUSED" overlay |
| `/Game/Sparta/UI/Menu/WBP_PauseMenuBackground.WBP_PauseMenuBackground_C` | `/Script/UMG.UserWidget` | pause‑menu background art |
| `/Game/Sparta/UI/Menu/WBP_Menu_Game.WBP_Menu_Game_C` | `/Script/UMG.UserWidget` | in‑game menu root (Main / Options) |
| `/Game/Sparta/UI/Menu/WBP_Menu_Game_Tab.WBP_Menu_Game_Tab_C` | `/Script/UMG.UserWidget` | base for every menu "tab" |
| `/Game/Sparta/UI/Menu/WBP_Menu_Option_Tab.WBP_Menu_Option_Tab_C` | `/Script/UMG.UserWidget` | base for option tabs |
| `/Game/Sparta/UI/Menu/WBP_MGT_Main.WBP_MGT_Main_C` | `WBP_Menu_Game_Tab_C` | menu main page |
| `/Game/Sparta/UI/Menu/WBP_MGT_Options.WBP_MGT_Options_C` | `WBP_Menu_Game_Tab_C` | menu options page |
| `/Game/Sparta/UI/Menu/Credits/WBP_Credits.WBP_Credits_C` | `WBP_Menu_Game_Tab_C` | credits, delegate `OnCreditsCompleted` |
| `/Game/Sparta/UI/Menu/Credits/WBP_GameCompletion.WBP_GameCompletion_C` | `WBP_Menu_Game_Tab_C` | run‑completion screen |
| `/Game/Sparta/UI/Menu/Misc/WBP_ConfirmationPromptBase.WBP_ConfirmationPromptBase_C` | `WBP_Menu_Game_Tab_C` | prompt base, delegates `OnOptionConfirmed`, `OnPromptClosed` |
| `/Game/Sparta/UI/Menu/Misc/WBP_ConfirmationPrompt_Default.WBP_ConfirmationPrompt_Default_C` | `WBP_ConfirmationPromptBase_C` | |
| `/Game/Sparta/UI/Menu/Misc/WBP_ConfirmationPrompt_Dismissable.WBP_ConfirmationPrompt_Dismissable_C` | `WBP_ConfirmationPromptBase_C` | delegate `OnDismissed` |
| `/Game/Sparta/UI/Menu/Misc/WBP_ConfirmationPrompt_MultiOptions.WBP_ConfirmationPrompt_MultiOptions_C` | `WBP_ConfirmationPromptBase_C` | delegates `OnMultiOptionConfirmed`, `OnMultiOptionClosed` |
| `/Game/Sparta/UI/Menu/Misc/WBP_ConfirmationPrompt_ReadText.WBP_ConfirmationPrompt_ReadText_C` | `WBP_ConfirmationPromptBase_C` | delegate `OnCloseStarted` |
| `/Game/Sparta/UI/Menu/Misc/WBP_ConfirmationPrompt_Requirement.WBP_ConfirmationPrompt_Requirement_C` | `WBP_ConfirmationPromptBase_C` | |
| `/Game/Sparta/UI/Menu/Misc/WBP_ConfirmationPrompt_ShellStory.WBP_ConfirmationPrompt_ShellStory_C` | `WBP_ConfirmationPromptBase_C` | |
| `/Game/Sparta/UI/Player/DeathScreen/WBP_DeathScreen.WBP_DeathScreen_C` | `/Script/UMG.UserWidget` | death screen |
| `/Game/Sparta/UI/Player/DeathScreen/WBP_MGT_DeathSelection.WBP_MGT_DeathSelection_C` | `WBP_Menu_Game_Tab_C` | HUB / beacon respawn picker |
| `/Game/Sparta/UI/Core/Navigation/WBP_InputListener.WBP_InputListener_C` | `/Script/UMG.UserWidget` | input listener used by every menu (`OnInputTriggered`) |

Full delegate signatures for the `WBP_ConfirmationPrompt_*` family are in §2.7, together with the
`BPFL_UI_C:Create*` functions that spawn them.

Evidence (class declarations, verbatim):

```
[000001F4C8B9A180] WidgetBlueprintGeneratedClass /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C [n: 25AF51] [c: 00007FF46EEC3B38] [or: 000001F4C7050E70] [sps: 00007FF46EEB3938]
[000001F4C8B90BE0] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Menu/WBP_GamePaused.WBP_GamePaused_C [n: 422A06] [c: 00007FF46EEC3B38] [or: 000001F4C7056000] [sps: 00007FF46EEB3938]
[000001F4CDF41BA0] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Menu/WBP_Menu_Game_Tab.WBP_Menu_Game_Tab_C [n: 267F89] [c: 00007FF46EEC3B38] [or: 000001F4C635BE10] [sps: 00007FF46EEB3938]
[000001F615114300] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Menu/WBP_Menu_Game.WBP_Menu_Game_C [n: 43476C] [c: 00007FF46EEC3B38] [or: 000001F4C7051AA0] [sps: 00007FF46EEB3938]
[000001F606FA0FD0] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Loading/WBP_LS_TransitionBase.WBP_LS_TransitionBase_C [n: 40BF58] [c: 00007FF46EEC3B38] [or: 000001F4C7051550] [sps: 00007FF46EEB3938]
[000001F606FAF820] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Menu/Credits/WBP_GameCompletion.WBP_GameCompletion_C [n: 12394C] [c: 00007FF46EEC3B38] [or: 000001F4C70524F0] [sps: 000001F4CDF41BA0]
[000001FA58B913C0] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Player/DeathScreen/WBP_DeathScreen.WBP_DeathScreen_C [n: 48B470] [c: 00007FF46EEC3B38] [or: 000001F9F75E82B0] [sps: 00007FF46EEB3938]
```

## 1.7 `WBP_MainMenu_C` — properties and the Continue/New/Load/LoadMap functions

| Property | Type | Offset |
|---|---|---|
| `UberGraphFrame` | `StructProperty of /Script/Engine.PointerToUberGraphFrame` | `0x340` |
| `WS_MainMenu` | `ObjectProperty of /Game/Sparta/UI/Core/Navigation/BP_WidgetSwitcher.BP_WidgetSwitcher_C` | `0x348` |
| `WBP_Tutorials` | `ObjectProperty of /Game/Sparta/UI/Tutorials/WBP_LargeTutorialContainer.WBP_LargeTutorialContainer_C` | `0x350` |
| `WBP_MGT_SaveSlots` | `ObjectProperty of /Game/Sparta/UI/MainMenu/WBP_MGT_SaveSlots.WBP_MGT_SaveSlots_C` | `0x358` |
| `WBP_MGT_Options` | `ObjectProperty of /Game/Sparta/UI/Menu/WBP_MGT_Options.WBP_MGT_Options_C` | `0x360` |
| `WBP_MGT_MainScreen` | `ObjectProperty of /Game/Sparta/UI/MainMenu/WBP_MGT_MainScreen.WBP_MGT_MainScreen_C` | `0x368` |
| `Overlay_Root` | `ObjectProperty of /Script/UMG.Overlay` | `0x370` |
| `Overlay_Main` | `ObjectProperty of /Script/UMG.Overlay` | `0x378` |
| `MapToLoad` | `NameProperty` | `0x380` |
| `DefaultMap` | `NameProperty` | `0x388` |
| `bPlayingAnimatic` | `BoolProperty` | `0x390` |

Only the last three offsets were captured verbatim (see the evidence block); the widget‑pointer
offsets are read off the same `show` listing in the order the dump prints them.

Functions (compiler temporaries filtered):

| Function | Parameters |
|---|---|
| `ContinueGame` | *(none)* |
| `LoadGame` | `ActiveSlot: IntProperty` |
| `StartNewGame` | `ActiveSlot: IntProperty`, `SkipPrologue: BoolProperty` |
| `LoadMap` | `LoadedIndex: IntProperty` |
| `InitializeLoadingScreen` | `MapToOpen: NameProperty` |
| `OnSaveSlotConfirmed` | `MenuType: IntProperty`, `Index: IntProperty`, `SkipPrologue: BoolProperty` |
| `OnMenuOpen` | `TabIndex: IntProperty`, `SubTabIndex: IntProperty` |
| `OnMenuClose` | *(none)* |
| `IsMenuOpen` | `ReturnValue: BoolProperty` |
| `GetMenuCloseEvent` | `ReturnValue: ByteProperty` |
| `SetMenuCloseEvent` | `NewValue: ByteProperty` |
| `GetRootPanel` | `ReturnValue: ObjectProperty of /Script/UMG.PanelWidget` |
| `UpdateMenuVisibility` | `NewValue: BoolProperty` |
| `UpdateSubTabIndex` | `NewIndex: IntProperty` |
| `HandlePromptNavigation` | `bIsOnScreen: BoolProperty` |
| `UpdateNavigationForPrompt` | `bPromptOnScreen: BoolProperty` |
| `TryTriggerTelemetry` | `Triggered: BoolProperty` |
| `TelemetryPopUpClosed` | `SelectedOption: IntProperty` |
| `TriggerDLCPopups` | `TutHandler: ObjectProperty of /Game/Sparta/UI/Tutorials/Blueprints/BPO_TutorialHandler.BPO_TutorialHandler_C` |
| `UnbindDLCPopups` | *(none)* |
| `PlayVideo` | `MediaSource: ObjectProperty of /Script/MediaAssets.MediaSource`, `Subtitles: ObjectProperty of /Script/Engine.DataTable`, `AudioStart: ClassProperty`, `AudioEnd: ClassProperty`, `bSkippable: BoolProperty`, `RemoveOnEnd: BoolProperty`, `bShouldPauseGame: BoolProperty`, `bShouldUnpauseOnEnd: BoolProperty`, `bHasBuiltInAudio: BoolProperty`, `OnEndEvent: DelegateProperty`, `Slot: ObjectProperty of /Script/UMG.OverlaySlot` |
| `StartAnimatic` | `Slot: ObjectProperty of /Script/UMG.OverlaySlot` |
| `StartOutroAnimatic` | `Slot: ObjectProperty of /Script/UMG.OverlaySlot` |
| `OnAnimaticEnd` | *(none)* |
| `OnOutroAnimaticEnd` | *(none)* |
| `CreditsEnd` | *(none)* |
| `OnFadeOutCompleted` | *(none)* |
| `Construct` / `Destruct` | *(none)* |

Note `PlayVideo` carries `bShouldPauseGame` **and** `bShouldUnpauseOnEnd` — the main menu itself
can pause/unpause the game while a movie plays.

`WBP_MainMenu_C` implements the generic menu interface `BPI_Menu_Generic_C`
(`GetMenuCloseEvent`, `SetMenuCloseEvent`, `IsMenuOpen`, `OnMenuOpen`, `OnMenuClose`,
`GetRootPanel`, `UpdateMenuVisibility`, `UpdateSubTabIndex` — confirmed by the interface's own
function list and by the widget declaring the same eight functions).

Evidence:

```
[000001F4EB553C70] NameProperty /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:MapToLoad [o: 380] [n: 63B395] [c: 00007FF721DE90B0] [owr: 000001F4C8B9A180]
[000001F4EB553C00] NameProperty /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:DefaultMap [o: 388] [n: 63B30A] [c: 00007FF721DE90B0] [owr: 000001F4C8B9A180]
[000001F61737B080] BoolProperty /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:bPlayingAnimatic [o: 390] [n: 63B250] [c: 00007FF721DE8D80] [owr: 000001F4C8B9A180]
[000001F4EB538A00] Function /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:ContinueGame [n: 63B2E6] [c: 00007FF46EE55538] [or: 000001F4C8B9A180] [f: 00007FF7183D9710]
[000001F4EB53C200] Function /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:LoadGame [n: 5BC38A] [c: 00007FF46EE55538] [or: 000001F4C8B9A180] [f: 00007FF7183D9710]
[000001F4EB53B700] Function /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:StartNewGame [n: 63B43E] [c: 00007FF46EE55538] [or: 000001F4C8B9A180] [f: 00007FF7183D9710]
[000001F4EB53C100] Function /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:LoadMap [n: 63B390] [c: 00007FF46EE55538] [or: 000001F4C8B9A180] [f: 00007FF7183D9710]
[000001F4EB53C500] Function /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:InitializeLoadingScreen [n: 63B32C] [c: 00007FF46EE55538] [or: 000001F4C8B9A180] [f: 00007FF7183D9710]
[000001F4EB538900] Function /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:CreditsEnd [n: 63B304] [c: 00007FF46EE55538] [or: 000001F4C8B9A180] [f: 00007FF7183D9710]
```

`WBP_MainMenu_C` is **a child of the game instance object**, i.e. it is created by
`BP_SpartaGameInstance_C` and lives under it in `/Engine/Transient`, and opening the menu spawns a
`GlobalEvent_OnMenuOpen_C`:

```
[000001F4A61AF910] GlobalEvent_OnMenuOpen_C /Engine/Transient.SpartaGameEngine_2147482593:BP_SpartaGameInstance_C_2147482225.WBP_MainMenu_C_2147481668.GlobalEvent_OnMenuOpen_C_2147480854 [n: 1A705F] [c: 000001F4D4597510] [or: 000001F6A16D95B0]
```

## 1.8 `BP_SpartaGameInstance_C` — the state store that survives level changes

`/Game/Sparta/Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C`,
parent `/Script/Sparta.SpartaGameInstance`. Selected properties (all `BoolProperty` unless noted):

| Property | Type | Offset | Sync relevance |
|---|---|---|---|
| `WorldGenRef` | `ObjectProperty of /Script/Sparta.SpartaWorldGenSubsystem` | `0x398` | world‑gen handle |
| `WasGameStartedFromMainMenu` | `BoolProperty` | `0x3a1` | **explicit "we came from the menu" flag** |
| `SpartaSettingsSaveObject` | `ObjectProperty` (`pc: 00007FF46F87AEE0`) | `0x3a8` | settings save object |
| `GameSettingsSaveSlot` | `StrProperty` | `0x3b0` | |
| `LastUsedTraversalDestinations` | `ArrayProperty of SoftObjectProperty` | `0x3c0` | travel history |
| `LastVisitedTraversalDestination` | `SoftObjectProperty` (`pc: 000001F4DCD21C00`) | `0x3e0` | travel target |
| `LoadingScreenIndex` | `IntProperty` | `0x408` | which loading art to show |
| `ForceSiphon` | `BoolProperty` | `0x418` | |
| `SuccessfulRun` | `BoolProperty` | `0x428` | run outcome |
| `ShellMemoriesSaveObjectLoaded` | `BoolProperty` | `0x429` | |
| `AchievementsUpdatedThisSession` | `BoolProperty` | `0x42a` | |
| `InBossFight` | `BoolProperty` | `0x42b` | boss‑fight flag |
| `Boss` | `ObjectProperty` (`pc: 00007FF46EE52028`, = `/Script/Engine.Actor`) | `0x430` | current boss actor |
| `JustDiedHubEventCheck` | `BoolProperty` | `0x440` | set around death |
| `UseFadeToBlack` | `BoolProperty` | `0x4a0` | transition style |
| `CanSpawnAtCameraLocation` | `BoolProperty` | `0x4a1` | |

Functions include: `ReceiveInit`, `ReceiveShutdown`, `PostInit`, `BP_PostEngineInit`,
`GetUseFadeToBlack`, `SetUseFadeToBlack`, `GetInBossFight`, `SetInBossFight`, `GetSuccessfulRun`,
`SetSuccessfulRun`, `GetForceSiphon`, `SetForceSiphon`, `GetLoadingScreenIndex`,
`SetLoadingScreenIndex`, `GetSettingsSaveObject`, `SaveGameSettings`, `LoadGameSettings`,
`InitializeAudio`, `CreateUIAudioHandler`, `CreateMusicManager`, `CreateAmbienceManager`,
`CreateCinematicAudioHandler`, `CreateVfxManger`, `CreateLightingManager`,
`TelemetryStartRecording`, `TelemetryStopRecording`.

Evidence:

```
[000001F4C2854330] BlueprintGeneratedClass /Game/Sparta/Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C [n: 361438] [c: 00007FF46EEC3D38] [or: 000001F4D1C14340] [sps: 00007FF46F047538]
[000001F621EC6000] BoolProperty /Game/Sparta/Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C:WasGameStartedFromMainMenu [o: 3A1] [n: 6575B2] [c: 00007FF721DE8D80] [owr: 000001F4C2854330]
[000001F621EC5B00] BoolProperty /Game/Sparta/Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C:SuccessfulRun [o: 428] [n: 65DD2F] [c: 00007FF721DE8D80] [owr: 000001F4C2854330]
[000001F621EC5980] BoolProperty /Game/Sparta/Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C:InBossFight [o: 42B] [n: 59ABDA] [c: 00007FF721DE8D80] [owr: 000001F4C2854330]
[000001F621EC5880] BoolProperty /Game/Sparta/Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C:JustDiedHubEventCheck [o: 440] [n: 65DBE6] [c: 00007FF721DE8D80] [owr: 000001F4C2854330]
[000001F621E4A600] Function /Game/Sparta/Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C:ReceiveInit [n: 9FDE] [c: 00007FF46EE55538] [or: 000001F4C2854330] [f: 00007FF7183D9710]
[000001F621E4E700] Function /Game/Sparta/Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C:ReceiveShutdown [n: 9FE5] [c: 00007FF46EE55538] [or: 000001F4C2854330] [f: 00007FF7183D9710]
[000001F621E4E000] Function /Game/Sparta/Core/GameInstance/BP_SpartaGameInstance.BP_SpartaGameInstance_C:SetSuccessfulRun [n: 59ABFD] [c: 00007FF46EE55538] [or: 000001F4C2854330] [f: 00007FF7183D9710]
```

## 1.9 `SpartaSaveSubSystem` — the Continue / Load / New‑game backing

| Delegate | Type | Offset |
|---|---|---|
| `OnSaveGameLoaded` | `MulticastInlineDelegateProperty` | `0x30` |
| `PostSessionReset` | `MulticastInlineDelegateProperty` | `0x40` |
| `OnSaveSystemInitialized` | `MulticastInlineDelegateProperty` | `0x50` |
| `OnSaveStarted` | `MulticastInlineDelegateProperty` | `0x60` |
| `OnSaveFinished` | `MulticastInlineDelegateProperty` | `0x70` |

| Property | Type | Offset |
|---|---|---|
| `SaveObjects` | `MapProperty` (`Guid` → `/Script/Sparta.SpartaSaveObject`) | `0xb8` |
| `CurrentOnDiskWorldStateSave` | `ObjectProperty of /Script/Sparta.SpartaSaveGame` | `0x108` |
| `SaveInterfaces` | `ArrayProperty of ObjectProperty` | `0x110` |
| `SessionSaveObject` | `ObjectProperty of /Script/Sparta.SpartaSaveObject` | `0x120` |
| `RuntimeSessionSaveObject` | `ObjectProperty of /Script/Sparta.SpartaSaveObject` | `0x128` |
| `MetaData` | `ObjectProperty of /Script/Sparta.SpartaMetaSave` | `0x140` |
| `bSaveLoaded` | `BoolProperty` | `0x14c` |
| `bApplySkipProloguePresetOnNextLoad` | `BoolProperty` | `0x14e` |

Flow‑critical functions (all parameters):

| Function | Parameters |
|---|---|
| `SaveToDisk` | `SaveEvents: StructProperty of /Script/GameplayTags.GameplayTagContainer`, `bSaveImmediately: BoolProperty`, `bSaveAsync: BoolProperty` |
| `SaveSession` | *(none)* |
| `SaveMetaData` | *(none)* |
| `ResetSession` | *(none)* |
| `ResetAllSaveData` | *(none)* |
| `ResetAutoSaveTimer` | *(none)* |
| `AutoSave` | *(none)* |
| `EnableAutoSave` / `DisableAutoSave` | *(none)* |
| `EnableSaveProfileBackup` / `DisableSaveProfileBackup` / `BackupActiveSaveProfile` | *(none)* |
| `SetPreventFurtherSaves` | `bPreventSaving: BoolProperty` |
| `ChangeActiveSaveProfileSlot` | `SlotIndex: IntProperty` |
| `DeleteSaveProfile` | `ProfileSlot: IntProperty` |
| `DeleteActiveSaveProfile` | *(none)* |
| `RecoverSave` | `RecoverySlotName: StrProperty` |
| `RequestSkipPrologueStart` / `StopSkipPrologueRequest` | *(none)* |
| `IsProcessingPrologueSkip` | `ReturnValue: BoolProperty` |
| `GetIsSaveLoaded` | `ReturnValue: BoolProperty` |
| `IsAsyncSaving` | `ReturnValue: BoolProperty` |
| `HasValidSessionSave` | `ReturnValue: BoolProperty` |
| `HasValidSaveForActiveProfileSlot` | `ReturnValue: BoolProperty` |
| `HasValidSaveForProfileSlot` | `Slot: IntProperty`, `ReturnValue: BoolProperty` |
| `HasAnyValidSaveProfile` | `ReturnValue: BoolProperty` |
| `HasValidMetaData` | `ReturnValue: BoolProperty` |
| `GetActiveSaveProfileSlotIndex` | `ReturnValue: IntProperty` |
| `GetActiveSaveProfileSlotName` | `ReturnValue: StrProperty` |
| `GetSessionSave` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveObject` |
| `GetCurrentWorldStateSave` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveGame` |
| `GetMetaData` | `ReturnValue: ObjectProperty of /Script/Sparta.SpartaMetaSave` |
| `GetPlayerSaveObjectFromSlotIndex` | `SlotIndex: IntProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.PlayerSaveObject` |
| `GetPlayerSaveObjectFromSlotName` | `SlotName: StrProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.PlayerSaveObject` |
| `GetSaveObjectByClassFromSlotIndex` | `SlotIndex: IntProperty`, `SaveObjectClass: ClassProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveObject` |
| `GetSaveObjectByClassFromSlotName` | `SlotName: StrProperty`, `SaveObjectClass: ClassProperty`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveObject` |
| `ResetSaveObjectByGuid` | `GuidString: StrProperty`, `ReturnValue: BoolProperty` |
| `ResetSaveObjectByClass` | `SaveObjectClass: ClassProperty`, `ReturnValue: IntProperty` |
| `SubscribeSaveInterface` / `UnsubscribeSaveInterface` | `Object: ObjectProperty of /Script/CoreUObject.Object` |
| `AsyncSaveFinished` | `SlotName: StrProperty`, `PlatformId: IntProperty`, `bSuccess: BoolProperty` |
| `OnEndPlayMap` | *(none)* |
| `GetInstance` | `WorldContextObject: ObjectProperty of /Script/CoreUObject.Object`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaSaveSubSystem` |
| `WriteCurrentSaveToTextFile` | *(none)* |
| `ClearCache` | *(none)* |
| `SetValidActiveSaveProfileSlot` | *(none)* |

**Delegate signatures** (resolved through the `df:` refs):

| Delegate | Signature object | Signature parameters |
|---|---|---|
| `OnSaveGameLoaded` | `/Script/Sparta.SpartaSaveLoadedSignature__DelegateSignature` (`DelegateFunction`) | `Context: StructProperty of /Script/Sparta.SpartaSaveContext` |
| `PostSessionReset` | `/Script/Sparta.SpartaSaveInitializedSignature__DelegateSignature` (`DelegateFunction`) | *(none)* |
| `OnSaveSystemInitialized` | `/Script/Sparta.SpartaSaveInitializedSignature__DelegateSignature` | *(none)* |
| `OnSaveStarted` | `/Script/Sparta.SpartaSaveInitializedSignature__DelegateSignature` | *(none)* |
| `OnSaveFinished` | `/Script/Sparta.SpartaSaveInitializedSignature__DelegateSignature` | *(none)* |

Note that `PostSessionReset`, `OnSaveSystemInitialized`, `OnSaveStarted` and `OnSaveFinished` share
one signature object — all four are parameterless.

Evidence:

```
[000001F4ACDF4F00] MulticastInlineDelegateProperty /Script/Sparta.SpartaSaveSubSystem:OnSaveGameLoaded [o: 30] [n: 5891D4] [c: 00007FF721DE9010] [owr: 00007FF46F057B38] [df: 00007FF46F3D3EC8]
[000001F4ACDF4F80] MulticastInlineDelegateProperty /Script/Sparta.SpartaSaveSubSystem:PostSessionReset [o: 40] [n: 5891CB] [c: 00007FF721DE9010] [owr: 00007FF46F057B38] [df: 00007FF46F3D3DE8]
[000001F4ACDF5080] MulticastInlineDelegateProperty /Script/Sparta.SpartaSaveSubSystem:OnSaveStarted [o: 60] [n: 5891B6] [c: 00007FF721DE9010] [owr: 00007FF46F057B38] [df: 00007FF46F3D3DE8]
[000001F4ACDF5100] MulticastInlineDelegateProperty /Script/Sparta.SpartaSaveSubSystem:OnSaveFinished [o: 70] [n: 5891AE] [c: 00007FF721DE9010] [owr: 00007FF46F057B38] [df: 00007FF46F3D3DE8]
[00007FF46F3D3DE8] DelegateFunction /Script/Sparta.SpartaSaveInitializedSignature__DelegateSignature [n: 525F97] [c: 00007FF46EE56338] [or: 00007FF46F17CD68]
[00007FF46F3D3EC8] DelegateFunction /Script/Sparta.SpartaSaveLoadedSignature__DelegateSignature [n: 525FB1] [c: 00007FF46EE56338] [or: 00007FF46F17CD68]
[000001F4A6CB5980] StructProperty /Script/Sparta.SpartaSaveLoadedSignature__DelegateSignature:Context [o: 0] [n: 4E2BA5] [c: 00007FF721DF9890] [owr: 00007FF46F3D3EC8] [ss: 00007FF46F3D03C8]
```

## 1.10 `BPFL_SaveLoad_C` — the Blueprint‑level save/load/travel helper

`/Game/Sparta/Core/Player/BPFL_SaveLoad.BPFL_SaveLoad_C`,
parent `/Script/Engine.BlueprintFunctionLibrary`. Every function takes a trailing
`__WorldContext: ObjectProperty of /Script/CoreUObject.Object` (a Blueprint function‑library
hidden parameter); it is listed once here and not repeated per row.

| Function | Parameters (besides `__WorldContext`) |
|---|---|
| `SaveGame` | `Immediately: BoolProperty`, `Save: BoolProperty` |
| `SaveSession` | *(none)* |
| `ResetSession` | *(none)* |
| `ShouldResumeSession` | `ShouldResumeSession: BoolProperty` |
| `IsSaveLoaded` | `ReturnValue: BoolProperty` |
| `SetRespawnPlayerStart` | `Value: NameProperty`, `Save: BoolProperty` |
| `GetRespawnPlayerStart` | `Success: BoolProperty`, `ReturnValue: NameProperty` |
| `SetProloguePlayerStart` | `Value: NameProperty`, `Save: BoolProperty` |
| `GetProloguePlayerStart` | `Success: BoolProperty`, `ReturnValue: NameProperty` |
| `GetAllHUBPlayerStarts` | `ReturnValue: ArrayProperty of NameProperty` |
| `GetHUBPlayerStart` | `ReturnValue: NameProperty` |
| `GetAreaNameFromPlayerStartTag` | `PlayerStartTag: NameProperty`, `Success: BoolProperty`, `AreaName: TextProperty` |
| `IsPrologueCompleted` | `ReturnValue: BoolProperty` |
| `IsHarrosClaimed` | `ReturnValue: BoolProperty` |
| `IsNewGamePlus` | `ReturnValue: BoolProperty` |
| `SetNewGamePlusLevel` | `NewGamePlus: IntProperty` |
| `GetNewGamePlusLevel` | `NewGamePlus: IntProperty` |
| `SetIntroCinematicWatched` | *(none)* |
| `IsIntroCinematicWatched` | `ReturnValue: BoolProperty` |
| `SaveTutorialState` / `SaveMetaTutorialState` | `TutorialClass: ClassProperty`, `TutorialName: NameProperty` |
| `CheckTutorialSaveState` | `ClassName: NameProperty`, `ReturnValue: BoolProperty` |
| `CheckMetaTutorialSaveState` | `ClassName: NameProperty`, `ReturnValue: BoolProperty` |
| `SetGatesRevealed` | `BiomeTag: StructProperty of /Script/GameplayTags.GameplayTag` |
| `GetGatesRevealed` | `BiomeTag: StructProperty of /Script/GameplayTags.GameplayTag`, `ReturnValue: BoolProperty` |
| `WriteMyBoolPlayerSave` | `ActorName: NameProperty`, `Value: BoolProperty` |
| `ReadMyBoolPlayerSave` | `ActorName: NameProperty`, `Value: BoolProperty` |
| `GetPlayerSaveObjectFromCurrentSave` | `Success: BoolProperty` |
| `UpdateSoftItemStatus` / `CheckSoftItemStatus` | `SoftItem: SoftClassProperty` / `+ ReturnValue: BoolProperty` |

Evidence:

```
[000001F4C2853160] BlueprintGeneratedClass /Game/Sparta/Core/Player/BPFL_SaveLoad.BPFL_SaveLoad_C [n: 3EAA6C] [c: 00007FF46EEC3D38] [or: 000001F4CEED29A0] [sps: 00007FF46EE70138]
```

## 1.11 Quit

**No Sparta‑owned quit function exists.** No `Quit`, `QuitGame`, `RequestExit`, `ExitGame` function
appears on any `/Script/Sparta.*` class.
Tried: `parse_dump.py find raw\index.sqlite3 "(^|:)(\w*Travel\w*|OpenLevel|LoadLevel|ChangeLevel|ServerTravel|ClientTravel|LoadMap|SetGamePhase|SetPhase|GamePhase)$" --types Function`
and a `classes --regex "SpartaGameInstance|SpartaGameMode|…"` sweep.
The only quit‑related assets are UI logic objects:
`/Game/Sparta/UI/Core/Navigation/BP_UIL_QuitFromMainMenu.BP_UIL_QuitFromMainMenu_C` and
`/Game/Sparta/UI/Core/Navigation/BP_UIL_QuitFromPauseMenu.BP_UIL_QuitFromPauseMenu_C`, both
parented to `/Game/Sparta/UI/Core/Navigation/BP_UILogic.BP_UILogic_C`, which is parented to
`/Script/Sparta.SpartaWorldObject`. GUESS: they call `UKismetSystemLibrary:QuitGame`; that call is
not visible in the object dump (only the Blueprint's own members are dumped).

---

# 2. Pause

## 2.1 There is no `SetPause` wrapper — pause is a parameter on the UI‑input helpers

Searching every `Function` object for `SetPause|IsPaused|SetGamePaused|PauseGame|UnpauseGame|TogglePause|ShowPauseMenu|OpenPauseMenu|ClosePauseMenu`
returns **only engine/plugin functions plus one Sparta ability function**. The block below is the
`parse_dump.py find` output (`TYPE<tab>PATH`, not a raw dump line) so the absence is auditable:

```
Function	/Game/Sparta/Core/Components/BPC_PhotographyMode.BPC_PhotographyMode_C:TogglePause
Function	/Script/AIModule.BrainComponent:IsPaused
Function	/Script/CommonUI.CommonVideoPlayer:IsPaused
Function	/Script/ControlRig.ControlRigReplay:IsPaused
Function	/Script/Engine.AudioComponent:SetPaused
Function	/Script/Engine.GameplayStatics:SetGamePaused
Function	/Script/MediaAssets.MediaPlayer:IsPaused
Function	/Script/MovieScene.MovieSceneSequencePlayer:IsPaused
Function	/Script/MovieSceneCapture.UserDefinedCaptureProtocol:OnPauseCapture
Function	/Script/Niagara.NiagaraComponent:IsPaused
Function	/Script/Niagara.NiagaraComponent:SetPaused
Function	/Script/Niagara.NiagaraPreviewGrid:SetPaused
Function	/Script/Sparta.SpartaAbilitySystemComponent:PauseGameplayEffectDurationCountdowns
```

(`parse_dump.py find raw\index.sqlite3 "(^|:)(SetPause|IsPaused|SetGamePaused|PauseGame|UnpauseGame|TogglePause|ShowPauseMenu|OpenPauseMenu|ClosePauseMenu|OnPause|OnUnpause|PauseState|bIsPaused|bPaused)" --types Function`)

So the game pauses through **`BPFL_UI_C`** and through engine `UGameplayStatics.SetGamePaused`
(`/Script/Engine.GameplayStatics:SetGamePaused` is present in the dump).

## 2.2 The two functions a mod must intercept

| Owner | Function | Parameters (full) |
|---|---|---|
| `/Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C` | `EnableUIInput` | `InWidgetToFocus: ObjectProperty of /Script/UMG.Widget`, `MouseLockMode: EnumProperty of /Script/Engine.EMouseLockMode`, `AddInputMapping: BoolProperty`, `ShowCursor: BoolProperty`, **`PauseGame: BoolProperty`**, `HidePlayerHUD: BoolProperty`, `__WorldContext: ObjectProperty of /Script/CoreUObject.Object` |
| `/Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C` | `DisableUIInput` | `RemoveInputMapping: BoolProperty`, **`UnpauseGame: BoolProperty`**, `HideCursor: BoolProperty`, `ShowPlayerHUD: BoolProperty`, `__WorldContext: ObjectProperty of /Script/CoreUObject.Object` |

Additional pause‑adjacent helpers on the same library:

| Function | Parameters |
|---|---|
| `WidgetStartupLogic` | `bIsOpening: BoolProperty`, `bIsInMenu: BoolProperty`, `bShouldHideHUD: BoolProperty`, `SoundToPlay: ObjectProperty of /Script/Engine.SoundBase`, `WidgetReference: ObjectProperty of /Game/Sparta/UI/Menu/WBP_Menu_Game_Tab.WBP_Menu_Game_Tab_C`, `InputListner: ObjectProperty of /Game/Sparta/UI/Core/Navigation/WBP_InputListener.WBP_InputListener_C`, `__WorldContext` |
| `HideHud` | `HUDLayer: ByteProperty`, `__WorldContext` |
| `ShowHUD` | `HUDLayer: ByteProperty`, `__WorldContext` |
| `BlockUIInput_All` | `__WorldContext` (applies a GameplayEffect to the player) |
| `UnblockUIInput_All` | `__WorldContext` (removes effects by tag) |
| `UpdateGameMenuBlockState` | `Option: ByteProperty`, `__WorldContext` |
| `UpdateCharacterMenuBlockState` | `Option: ByteProperty`, `__WorldContext` |

`/Script/Sparta.SpartaPlayerController` also exposes the pause‑tick switch:

| Function | Parameters |
|---|---|
| `/Script/Sparta.SpartaPlayerController:SetShouldPerformFullTickWhenPaused` | `bEnabled: BoolProperty` |
| `/Script/Sparta.SpartaPlayerController:GetShouldPerformFullTickWhenPaused` | `ReturnValue: BoolProperty` |

Evidence:

```
[000001F622E32000] BoolProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:EnableUIInput:PauseGame [o: B] [n: 637753] [c: 00007FF721DE8D80] [owr: 000001F606FB1600]
[000001F622E30500] BoolProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:DisableUIInput:UnpauseGame [o: 1] [n: 63773B] [c: 00007FF721DE8D80] [owr: 000001F606FB1700]
[000001F622E30180] ObjectProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:EnableUIInput:InWidgetToFocus [o: 0] [n: 4FC160] [c: 00007FF721DE9420] [owr: 000001F606FB1600] [pc: 00007FF46EEB2D38]
```

## 2.3 `WBP_GamePaused_C`

| Member | Type | Offset |
|---|---|---|
| `Text_GamePaused` | `ObjectProperty of /Script/UMG.TextBlock` | `0x348` |
| `ScaleBox_UserSettings_Prompt` | `ObjectProperty of /Script/UMG.ScaleBox` | `0x350` |
| `ScaleBox_UserSettings` | `ObjectProperty of /Script/UMG.ScaleBox` | `0x358` |
| `Overlay_GamePaused` | `ObjectProperty of /Script/UMG.Overlay` | `0x360` |
| `MyPrompts` | `ObjectProperty of /Game/Sparta/UI/Core/WBP_PromptsContainer.WBP_PromptsContainer_C` | `0x368` |
| `Has` | `BoolProperty` | `0x370` |
| `Forced` | `ByteProperty` | `0x371` |

Functions: `Construct()`, `PreConstruct(IsDesignTime: BoolProperty)`,
`RefreshUIScale(bHasForcedScale: BoolProperty, ForcedScale: ByteProperty)`,
`SetUIScale(UI_Scale: ByteProperty)`,
`OnEventFired_4F03B841444ACD9914C25CA97A87E48E(EventData: ObjectProperty of /Script/CoreUObject.Object)`.

It has **no menu‑open/close interface** and subscribes in `Construct` to exactly one
GlobalEventSubsystem event via `CreateProxyObjectForSubscribeToEvent`; the concrete event class is
**UNKNOWN** (the event class is only visible in the transient proxy object, not as a
property of the widget).
Tried: `parse_dump.py find raw\index.sqlite3 "WBP_GamePaused"` — the widget is not referenced by
any other dumped object either, so whatever spawns it lives outside the dumped members.

Evidence:

```
[000001F4C8B90BE0] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Menu/WBP_GamePaused.WBP_GamePaused_C [n: 422A06] [c: 00007FF46EEC3B38] [or: 000001F4C7056000] [sps: 00007FF46EEB3938]
```

## 2.4 The menu contract every menu widget implements: `BPI_Menu_Generic_C`

`/Game/Sparta/UI/Menu/BPI_Menu_Generic.BPI_Menu_Generic_C`, parent `/Script/CoreUObject.Interface`
— i.e. a **Blueprint interface** (`sps: 00007FF46EE52A30` = `/Script/CoreUObject.Interface`).
Functions:

| Function | Parameters |
|---|---|
| `OnMenuOpen` | `TabIndex: IntProperty`, `SubTabIndex: IntProperty` |
| `OnMenuClose` | *(none)* |
| `IsMenuOpen` | `ReturnValue: BoolProperty` |
| `GetMenuCloseEvent` | `ReturnValue: ByteProperty` (an `Enum_MenuCloseEvent`) |
| `SetMenuCloseEvent` | `NewValue: ByteProperty` |
| `GetRootPanel` | `ReturnValue: ObjectProperty of /Script/UMG.PanelWidget` |
| `UpdateMenuVisibility` | `NewValue: BoolProperty` |
| `UpdateSubTabIndex` | `NewIndex: IntProperty` |

Evidence:

```
[000001F4CDF7EE30] BlueprintGeneratedClass /Game/Sparta/UI/Menu/BPI_Menu_Generic.BPI_Menu_Generic_C [n: 240E12] [c: 00007FF46EEC3D38] [or: 000001F4C705AF10] [sps: 00007FF46EE52A30]
[000001F4CE192500] Function /Game/Sparta/UI/Menu/BPI_Menu_Generic.BPI_Menu_Generic_C:OnMenuOpen [n: 63077B] [c: 00007FF46EE55538] [or: 000001F4CDF7EE30] [f: 00007FF7183D9710]
[000001F4C7500550] IntProperty /Game/Sparta/UI/Menu/BPI_Menu_Generic.BPI_Menu_Generic_C:OnMenuOpen:TabIndex [o: 0] [n: 519DF7] [c: 00007FF721DE91F0] [owr: 000001F4CE192500]
[000001F4C75004E0] IntProperty /Game/Sparta/UI/Menu/BPI_Menu_Generic.BPI_Menu_Generic_C:OnMenuOpen:SubTabIndex [o: 4] [n: 630792] [c: 00007FF721DE91F0] [owr: 000001F4CE192500]
```

`/Game/Sparta/UI/Enums/Enum_MenuCloseEvent.Enum_MenuCloseEvent` is a `UserDefinedEnum`, but its
**entries are not in the dump** → the enum's values are UNKNOWN.
Tried: `parse_dump.py raw raw\index.sqlite3 "Enum_MenuCloseEvent"` → only the `Package` and
`UserDefinedEnum` lines.

## 2.5 The event bus that announces menu open/close: GlobalEventSubsystem

`GlobalEvent` (`/Script/GlobalEventSubsystem.GlobalEvent`, parent `/Script/CoreUObject.Object`,
**declares no members itself**), broadcast through
`/Script/GlobalEventSubsystem.GESFunctions`:

| Function | Parameters |
|---|---|
| `SubscribeToEvent` | `Object: ObjectProperty of /Script/CoreUObject.Object`, `EventClass: ClassProperty`, `Filter: StructProperty of /Script/GameplayTags.GameplayTag`, `Event: DelegateProperty`, `ReturnValue: BoolProperty` |
| `UnsubscribeFromEvent` | `Object`, `EventClass: ClassProperty`, `Filter: StructProperty of /Script/GameplayTags.GameplayTag`, `ReturnValue: BoolProperty` |
| `UnsubscribeFromAll` | `Object: ObjectProperty of /Script/CoreUObject.Object`, `ReturnValue: IntProperty` |
| `IsSubscribed` | `Object`, `EventClass`, `Filter`, `ReturnValue: BoolProperty` |
| `GetSubscribers` | `EventClass: ClassProperty`, `Filter`, `ReturnValue: ArrayProperty` |
| `GetSubscribersByClass` | `SubscriberClass: ClassProperty`, `EventClass: ClassProperty`, `Filter`, `ReturnValue: ArrayProperty` |
| `BroadcastEvent` | `EventData: ObjectProperty of /Script/CoreUObject.Object`, `EventClass: ClassProperty`, `Filter`, `ReturnValue: IntProperty` |
| `BroadcastEventNoData` | `EventClass: ClassProperty`, `Filter`, `ReturnValue: IntProperty` |

The menu/cinematic/loading events are **Blueprint subclasses of `GlobalEvent`**, so a mod can
subscribe to any of them by class:

| Event class | Parent | Payload |
|---|---|---|
| `/Game/Sparta/Core/GES/GlobalEvent_OnMenuOpen.GlobalEvent_OnMenuOpen_C` | `/Script/GlobalEventSubsystem.GlobalEvent` | `UserWidget: ObjectProperty of /Script/UMG.UserWidget` @ `0x28` |
| `/Game/Sparta/Core/GES/GlobalEvent_OnMenuClose.GlobalEvent_OnMenuClose_C` | `GlobalEvent` | `UserWidget` @ `0x28` |
| `/Game/Sparta/Core/GES/GlobalEvent_OnMenuClose_Cancelled.GlobalEvent_OnMenuClose_Cancelled_C` | `GlobalEvent` | `UserWidget` @ `0x28` |
| `/Game/Sparta/Core/GES/GlobalEvent_Cinematic_Start.GlobalEvent_Cinematic_Start_C` | `GlobalEvent` | `CinematicPlayer: ObjectProperty of /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C` @ `0x28` |
| `/Game/Sparta/Core/GES/GlobalEvent_Cinematic_End.GlobalEvent_Cinematic_End_C` | `GlobalEvent` | `CinematicPlayer` @ `0x28` |
| `/Game/Sparta/Core/GES/GlobalEvent_Cinematic_EnableTrigger.GlobalEvent_Cinematic_EnableTrigger_C` | `GlobalEvent` | — |
| `/Game/Sparta/Core/GES/GlobalEvent_CreditsFinished.GlobalEvent_CreditsFinished_C` | `GlobalEvent` | — |
| `/Game/Sparta/UI/Loading/BP_GlobalEvent_LoadingScreenCreated.BP_GlobalEvent_LoadingScreenCreated_C` | `GlobalEvent` | — |
| `/Game/Sparta/UI/Loading/BP_GlobalEvent_LoadingScreenCompleted.BP_GlobalEvent_LoadingScreenCompleted_C` | `GlobalEvent` | — |
| `/Game/Sparta/UI/Loading/BP_GlobalEvent_StreamingScreenCompleted.BP_GlobalEvent_StreamingScreenCompleted_C` | `GlobalEvent` | — |

Evidence:

```
[00007FF46EFCC338] Class /Script/GlobalEventSubsystem.GlobalEvent [n: 3FA0C] [c: 00007FF46EE55138] [or: 00007FF46F17BE18] [sps: 00007FF46EE50070]
[000001F4D4597510] BlueprintGeneratedClass /Game/Sparta/Core/GES/GlobalEvent_OnMenuOpen.GlobalEvent_OnMenuOpen_C [n: 1A705F] [c: 00007FF46EEC3D38] [or: 000001F4D015D260] [sps: 00007FF46EFCC338]
[000001F4D0142D00] ObjectProperty /Game/Sparta/Core/GES/GlobalEvent_OnMenuOpen.GlobalEvent_OnMenuOpen_C:UserWidget [o: 28] [n: 29DCE] [c: 00007FF721DE9420] [owr: 000001F4D4597510] [pc: 00007FF46EEB3938]
[000001F4D4DA86E0] BlueprintGeneratedClass /Game/Sparta/Core/GES/GlobalEvent_OnMenuClose.GlobalEvent_OnMenuClose_C [n: CA8A9] [c: 00007FF46EEC3D38] [or: 000001F4D31FA240] [sps: 00007FF46EFCC338]
[000001F8A7F2B8C0] BlueprintGeneratedClass /Game/Sparta/Core/GES/GlobalEvent_OnMenuClose_Cancelled.GlobalEvent_OnMenuClose_Cancelled_C [n: 19E5E0] [c: 00007FF46EEC3D38] [or: 000001F8A7FC26D0] [sps: 00007FF46EFCC338]
[000001F8ACDA2780] ObjectProperty /Game/Sparta/Core/GES/GlobalEvent_OnMenuClose_Cancelled.GlobalEvent_OnMenuClose_Cancelled_C:UserWidget [o: 28] [n: 29DCE] [c: 00007FF721DE9420] [owr: 000001F8A7F2B8C0] [pc: 00007FF46EEB3938]
[000001F4EB562DD0] BlueprintGeneratedClass /Game/Sparta/Core/GES/GlobalEvent_Cinematic_Start.GlobalEvent_Cinematic_Start_C [n: 247664] [c: 00007FF46EEC3D38] [or: 000001F4CEED04C0] [sps: 00007FF46EFCC338]
[000001F61737AF80] ObjectProperty /Game/Sparta/Core/GES/GlobalEvent_Cinematic_Start.GlobalEvent_Cinematic_Start_C:CinematicPlayer [o: 28] [n: 652A08] [c: 00007FF721DE9420] [owr: 000001F4EB562DD0] [pc: 000001F4EB562A40]
```

## 2.6 The in‑game menu root `WBP_Menu_Game_C` and the "display menu" (3‑D character) system

`/Game/Sparta/UI/Menu/WBP_Menu_Game.WBP_Menu_Game_C` (parent `/Script/UMG.UserWidget`):
`WBP_Menu_Options` `0x348`, `WBP_Menu_Main` `0x350`, `Overlay_Main` `0x358`,
`BP_WS_GameMenu` `0x360`; functions `GetMenuCloseEvent`, `GetRootPanel`, `IsMenuOpen`,
`StartAudio`, `StopAudio`, `UpdateSubTabIndex(NewIndex)`, `UpdateMenuVisibility(NewValue)`,
`SetMenuCloseEvent(NewValue)`, `Construct`, `OnMenuOpen(TabIndex, SubTabIndex)`, `OnMenuClose`.

The character/inventory/progression menus are **actors** spawned by
`BPC_UserInterfaceHandler_C` (`/Game/Sparta/UI/Core/BPC_UserInterfaceHandler.BPC_UserInterfaceHandler_C`,
parent `/Script/Engine.ActorComponent`):

| Function | Parameters |
|---|---|
| `ActivateDisplayMenu` | `Tag: StructProperty of /Script/GameplayTags.GameplayTag`, `Success: BoolProperty` |
| `DeactivateDisplayMenu` | *(none)* |
| `RemoveDisplayMenu` | *(none)* |
| `GetActiveDisplayMenu` | `ReturnValue: ObjectProperty of /Game/Sparta/UI/Menu/Blueprints/BP_DisplayMenuBase.BP_DisplayMenuBase_C` |
| `GetDisplayMenuFromTag` | `Tag: StructProperty of /Script/GameplayTags.GameplayTag`, `ReturnValue: ObjectProperty of /Game/Sparta/UI/Menu/Blueprints/BP_DisplayMenuBase.BP_DisplayMenuBase_C` |
| `SpawnDisplayMenu` | `Class: SoftClassProperty`, `OverrideLocation: BoolProperty`, `CustomLocation: StructProperty`, `Rotation: StructProperty`, `MenuActor: ObjectProperty of /Game/Sparta/UI/Menu/Blueprints/BP_DisplayMenuBase.BP_DisplayMenuBase_C`, `MenuClass: ClassProperty` |
| `NoActiveMenu` | `ReturnValue: BoolProperty` |

Convenience wrappers exist in `BPFL_UI_C`: `ActivateDisplayMenu(Tag, Success, __WorldContext)`,
`RemoveActiveDisplayMenu(__WorldContext)`, `GetActiveDisplayMenu(Success, ReturnValue, __WorldContext)`,
`IsInDisplayMenu(MenuTag, ReturnValue, __WorldContext)`,
`HasNoActivePrimaryMenu(ReturnValue, __WorldContext)`, `ResetActivePrimaryMenu(__WorldContext)`,
`UpdateActivePrimaryMenu(NewWidget, __WorldContext)`.

The display‑menu actor hierarchy (`/Game/Sparta/UI/Menu/Blueprints/`),
all parented to `BP_DisplayMenuBase_C`, which implements `BPI_DisplayMenu_C`:
`BP_DisplayMenu_Abilities`, `BP_DisplayMenu_Calibration`, `BP_DisplayMenu_ChangeEquipment`,
`BP_DisplayMenu_ChangeSkins`, `BP_DisplayMenu_Character`, `BP_DisplayMenu_GameCompletion`,
`BP_DisplayMenu_Progression`, `BP_DisplayMenu_Shell`. Each implements
`ActivateDisplayMenu`, `DeactivateDisplayMenu`, `Show`, `Hide`, `AddCamera`/`AddMenuCamera`,
`RemoveCamera`, `GetCameraActor`, `SwitchActiveCamera`, `ResetDisplayVector`, `ResetDisplayYaw`.
Opening/closing one broadcasts `BP_GlobalEvent_DisplayMenu_Open_C` /
`BP_GlobalEvent_DisplayMenu_Close_C`.

Evidence:

```
[000001F4CDF7FC70] BlueprintGeneratedClass /Game/Sparta/UI/Menu/Blueprints/BP_DisplayMenuBase.BP_DisplayMenuBase_C [n: 375075] [c: 00007FF46EEC3D38] [or: 000001F4C635AD80] [sps: 00007FF46EE52028]
[000001F4C3FD78A0] BlueprintGeneratedClass /Game/Sparta/UI/Menu/Blueprints/BPI_DisplayMenu.BPI_DisplayMenu_C [n: 170944] [c: 00007FF46EEC3D38] [or: 000001F4C7059D40] [sps: 00007FF46EE52A30]
[000001F622F3F300] StructProperty /Game/Sparta/UI/Core/BPC_UserInterfaceHandler.BPC_UserInterfaceHandler_C:ActivateDisplayMenu:Tag [o: 0] [n: 4E5A4A] [c: 00007FF721DF9890] [owr: 000001F61C3BF400] [ss: 00007FF46F3890A8]
[000001F622F3F280] BoolProperty /Game/Sparta/UI/Core/BPC_UserInterfaceHandler.BPC_UserInterfaceHandler_C:ActivateDisplayMenu:Success [o: 8] [n: 579B7D] [c: 00007FF721DE8D80] [owr: 000001F61C3BF400]
[000001F6230E0100] ObjectProperty /Game/Sparta/UI/Core/BPC_UserInterfaceHandler.BPC_UserInterfaceHandler_C:GetActiveDisplayMenu:ReturnValue [o: 0] [n: 4CF54F] [c: 00007FF721DE9420] [owr: 000001F61C3BF800] [pc: 000001F4CDF7FC70]
```

(`00007FF46F3890A8` = `/Script/GameplayTags.GameplayTag` ScriptStruct, verified with
`parse_dump.py raw raw\index.sqlite3 "Script/GameplayTags\.GameplayTag$" --types ScriptStruct`.)

## 2.7 Modal prompts that carry their own `bPauseGame`

Every "create a modal" function on `BPFL_UI_C` takes a **`bPauseGame: BoolProperty`** plus a pair of
delegates. These are the most frequent pause/unpause transitions in normal play.

| Function | Pause flag (offset) | Close / input delegates (offset → signature) |
|---|---|---|
| `CreateConfirmationPrompt` | `bPauseGame` `0x10` | `InputEvent` `0x58` → `OnOptionConfirmed__DelegateSignature(OptionIndex: int)`; `ClosedEvent` `0x68` → `OnPromptClosed__DelegateSignature(OptionIndex: int)` |
| `CreateConfirmationPromptMultiOptions` | `bPauseGame` `0x10` | `InputEvent` `0x58` → `OnMultiOptionConfirmed__DelegateSignature(OptionID: Name, OptionIndex: int)`; `ClosedEvent` `0x68` → `OnMultiOptionClosed__DelegateSignature(OptionID: Name, OptionIndex: int)` |
| `CreateConfirmationPromptWithRequirement` | `bPauseGame` `0x10` | `InputEvent` `0x70` → `OnOptionConfirmed__DelegateSignature(OptionIndex)`; `ClosedEvent` `0x80` → `OnPromptClosed__DelegateSignature(OptionIndex)` |
| `CreateShellStoryPrompt` | `bPauseGame` `0x10` | `InputEvent` `0x78` → `OnOptionConfirmed__DelegateSignature(OptionIndex)`; `ClosedEvent` `0x88` → `OnPromptClosed__DelegateSignature(OptionIndex)` |
| `CreateReadText` | `bPauseGame` `0x18` | `CloseStart` `0x48` → `WBP_ConfirmationPrompt_ReadText_C:OnCloseStarted__DelegateSignature()`; `CloseComplete` `0x58` → `WBP_Menu_Game_Tab_C:OnClosed__DelegateSignature()` |
| `CreateDismissableMessage` | `bPauseGame` `0x10` | `DimissEvent` `0x48` (sic: `Dimiss`) → `WBP_ConfirmationPrompt_Dismissable_C:OnDismissed__DelegateSignature()`; `ClosedEvent` `0x58` → `WBP_Menu_Game_Tab_C:OnClosed__DelegateSignature()` |
| `CreateInspectWidget` | `bPauseGame` `0x18` | `CloseStarted` `0x28` → `WBP_Inspect_C:OnCloseStarted__DelegateSignature()`; `CloseComplete` `0x38` → `WBP_Menu_Game_Tab_C:OnClosed__DelegateSignature()` |
| `CreateObjectiveHint` | `bPauseGame` `0x59`, `bIsInMenu` `0x58` | `WidgetClosedEvent` `0x48` → `WBP_ObjectiveHint_C:OnHintClosed__DelegateSignature()` |

Full delegate-signature inventory (all resolved through the `df:` addresses):

| Signature object | Parameters |
|---|---|
| `WBP_ConfirmationPromptBase_C:OnOptionConfirmed__DelegateSignature` | `OptionIndex: IntProperty` |
| `WBP_ConfirmationPromptBase_C:OnPromptClosed__DelegateSignature` | `OptionIndex: IntProperty` |
| `WBP_ConfirmationPrompt_MultiOptions_C:OnMultiOptionConfirmed__DelegateSignature` | `OptionID: NameProperty`, `OptionIndex: IntProperty` |
| `WBP_ConfirmationPrompt_MultiOptions_C:OnMultiOptionClosed__DelegateSignature` | `OptionID: NameProperty`, `OptionIndex: IntProperty` |
| `WBP_ConfirmationPrompt_ReadText_C:OnCloseStarted__DelegateSignature` | *(none)* |
| `WBP_ConfirmationPrompt_Dismissable_C:OnDismissed__DelegateSignature` | *(none)* |
| `WBP_Menu_Game_Tab_C:OnClosed__DelegateSignature` | *(none)* |
| `WBP_Inspect_C:OnCloseStarted__DelegateSignature` | *(none)* |
| `WBP_ObjectiveHint_C:OnHintClosed__DelegateSignature` | *(none)* |
| `WBP_LoadingScreenBase_C:OnCompleted__DelegateSignature` | *(none)* |
| `WBP_Credits_C:OnCreditsCompleted__DelegateSignature` | *(none)* |
| `WBP_MGT_SaveSlots_C:OnSlotConfirmed__DelegateSignature` | `MenuType: IntProperty`, `Index: IntProperty`, `SkipPrologue: BoolProperty` |
| `WBP_MGT_MainScreen_C:OpenOptionsMenu__DelegateSignature`, `…OpenLoadGameMenu…`, `…OpenNewGameMenu…`, `…OnContinueCommitted…`, `…OnEndSequenceConfirmed…`, `…OnReplayIntroAnimaticConfirmed…` | *(none)* |

`CloseComplete` on `CreateReadText` / `CreateInspectWidget`, and `ClosedEvent` on
`CreateDismissableMessage`, all point at the same parameterless
`WBP_Menu_Game_Tab_C:OnClosed__DelegateSignature`.

The non‑delegate parameters of these functions are UI content (titles, body text, `ConstructionType`,
`Position`, `ZOrder`, `ParentPanel`, `OptionalLogic`, …) — they do not affect sync. The full
parameter lists are recoverable with
`python scripts\parse_dump.py show raw\index.sqlite3 BPFL_UI.BPFL_UI_C`.

Evidence:

```
[000001F622E0B600] BoolProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:CreateConfirmationPrompt:bPauseGame [o: 10] [n: 63BBA1] [c: 00007FF721DE8D80] [owr: 000001F606FB1900]
[000001F622E0D680] DelegateProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:CreateConfirmationPrompt:InputEvent [o: 58] [n: 1C60] [c: 00007FF721DE8E70] [owr: 000001F606FB1900] [df: 000001F607942E00]
[000001F622E0D600] DelegateProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:CreateConfirmationPrompt:ClosedEvent [o: 68] [n: 63C007] [c: 00007FF721DE8E70] [owr: 000001F606FB1900] [df: 000001F607942F00]
[000001F622E0EA00] BoolProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:CreateReadText:bPauseGame [o: 18] [n: 63BBA1] [c: 00007FF721DE8D80] [owr: 000001F606FB1C00]
[000001F622E0E880] DelegateProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:CreateReadText:CloseStart [o: 48] [n: 63C24D] [c: 00007FF721DE8E70] [owr: 000001F606FB1C00] [df: 000001F60794F300]
[000001F622E0EF80] BoolProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:CreateObjectiveHint:bPauseGame [o: 59] [n: 63BBA1] [c: 00007FF721DE8D80] [owr: 000001F606FB1B00]
[000001F622E0D800] BoolProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:CreateInspectWidget:bPauseGame [o: 18] [n: 63BBA1] [c: 00007FF721DE8D80] [owr: 000001F60654FC00]
[000001F622E0DF00] DelegateProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:CreateDismissableMessage:DimissEvent [o: 48] [n: 63C0F1] [c: 00007FF721DE8E70] [owr: 000001F606FB1F00] [df: 000001F4EB539700]
[000001F620623420] IntProperty /Game/Sparta/UI/Menu/Misc/WBP_ConfirmationPromptBase.WBP_ConfirmationPromptBase_C:OnOptionConfirmed__DelegateSignature:OptionIndex [o: 0] [n: 5164A5] [c: 00007FF721DE91F0] [owr: 000001F607942E00]
[000001F614C50860] NameProperty /Game/Sparta/UI/Menu/Misc/WBP_ConfirmationPrompt_MultiOptions.WBP_ConfirmationPrompt_MultiOptions_C:OnMultiOptionConfirmed__DelegateSignature:OptionID [o: 0] [n: 649700] [c: 00007FF721DE90B0] [owr: 000001F60794DB00]
[000001F614C507F0] IntProperty /Game/Sparta/UI/Menu/Misc/WBP_ConfirmationPrompt_MultiOptions.WBP_ConfirmationPrompt_MultiOptions_C:OnMultiOptionConfirmed__DelegateSignature:OptionIndex [o: 8] [n: 5164A5] [c: 00007FF721DE91F0] [owr: 000001F60794DB00]
```

---

# 3. Death screen and respawn

## 3.1 Where the mod *sees* death first: the health component

`/Script/Sparta.SpartaHealthComponent` (`/Script/Sparta.SpartaHealthComponent`, native class):

| Function | Parameters |
|---|---|
| `StartDeath` | *(none)* |
| `GetDeathState` | `ReturnValue: EnumProperty of /Script/Sparta.ESpartaDeathState` |
| `IsDeadOrDying` | `ReturnValue: BoolProperty` |
| `OnRep_DeathState` | `OldDeathState: EnumProperty of /Script/Sparta.ESpartaDeathState` |
| `GetHealth` / `GetShellHealth` / `GetMaxHealth` / `GetMaxShellHealth` | `ReturnValue: FloatProperty` |
| `GetHealthNormalized` / `GetCurrentHealthNormalized` / `GetShellHealthNormalized` | `ReturnValue: FloatProperty` |
| `FindHealthComponent` | `Actor: ObjectProperty of /Script/Engine.Actor`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaHealthComponent` |
| `InitializeWithAbilitySystem` | `InASC: ObjectProperty of /Script/Sparta.SpartaAbilitySystemComponent` |
| `UninitializeFromAbilitySystem` | *(none)* |

`ESpartaDeathState` (from `raw\CXXHeaderDump\Sparta_enums.hpp` lines 315‑320):

```
enum class ESpartaDeathState {
    NotDead = 0,
    DeathStarted = 1,
    DeathFinished = 2,
    ESpartaDeathState_MAX = 3,
};
```

Evidence (the enum above is a header excerpt, `raw\CXXHeaderDump\Sparta_enums.hpp` lines 315‑320;
these are the verbatim dump lines for the component):

```
[00007FF46F72FBD8] Function /Script/Sparta.SpartaHealthComponent:StartDeath [n: 4BC57] [c: 00007FF46EE55538] [or: 00007FF46F066538] [f: 00007FF71A732EC0]
[000001F4ACE47280] EnumProperty /Script/Sparta.SpartaHealthComponent:GetDeathState:ReturnValue [o: 0] [n: 4CF54F] [c: 00007FF721DE7380] [owr: 00007FF46F72F318] [em: 00007FF46F3DA588]
[000001F4ACE47080] EnumProperty /Script/Sparta.SpartaHealthComponent:OnRep_DeathState:OldDeathState [o: 0] [n: 58A673] [c: 00007FF721DE7380] [owr: 00007FF46F72FAF8] [em: 00007FF46F3DA588]
[000001F4ACE47100] BoolProperty /Script/Sparta.SpartaHealthComponent:IsDeadOrDying:ReturnValue [o: 0] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 00007FF46F72FA18]
```

## 3.2 `GA_Death_Player_C` — the ability that actually runs the death screen

`/Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C`,
parent `/Game/Sparta/Core/Ability/GA_DeathBase.GA_DeathBase_C`.

| Property | Type | Offset |
|---|---|---|
| `DeathMontages` | `ArrayProperty of /Script/Engine.AnimMontage` | `0x5e0` |
| `WBP_DeathScreen` | `ObjectProperty of /Script/UMG.UserWidget` | `0x5f0` |
| `PlayMontage` | `BoolProperty` | `0x5f8` |
| `MyEventData` | `StructProperty of /Script/GameplayAbilities.GameplayEventData` | `0x600` |
| `MyCommittedFromEvent` | `BoolProperty` | `0x6b0` |
| `bDroppedWeapons` | `BoolProperty` | `0x6b1` |
| `DeathScreenWidgetClass` | `SoftClassProperty` | `0x6b8` |
| `CachedScreenWidgetClass` | `ClassProperty` | `0x6e0` |

| Function | Parameters |
|---|---|
| `HandlePlayerDeath` | `GrantDeathState: BoolProperty` |
| `CreateDeathScreen` | *(none — loads `DeathScreenWidgetClass` and calls `CreateWidget`)* |
| `BeginDeathScreenAsyncLoad` | *(none)* |
| `OnLoaded_4D3F25CE44FD1C6F9F7F5D8B0870045B` | `Loaded: ClassProperty` |
| `UnPossess` | *(none — result read into `Success`)* |
| `EndDeath` | *(none)* |
| `DropOwningWeapons` | `WeaponSlots: StructProperty of /Script/GameplayTags.GameplayTagContainer`, `CustomLifeSpan: DoubleProperty` |
| `ClearAbilities` | *(none)* |
| `RemoveApplicableWidgets` | `WidgetsToCheck: ArrayProperty of ObjectProperty of /Script/UMG.UserWidget` |
| `GetDeathMontage` | `Success: BoolProperty`, `Montage: ObjectProperty of /Script/Engine.AnimMontage` |
| `InPairedAnimation` | `ReturnValue: BoolProperty` |
| `WaitForDismemberment`, `HandleWeaponsDrop`, `DisableCollision`, `TriggerDissolve`, `HandlePairedAnimationDeath` | *(none)* |
| `EventReceived_184FDD68452534CB914D95964557014B` | `Payload: StructProperty of /Script/GameplayAbilities.GameplayEventData` |
| `SpartaAbilityCommitted` | `CommittedFromEvent: BoolProperty`, `EventData: StructProperty of /Script/GameplayAbilities.GameplayEventData` |
| `K2_OnEndAbility` | `bWasCancelled: BoolProperty` |

`GA_Death_Player_C` is the **only** object that references `WBP_DeathScreen` (`parse_dump.py find`
for `WBP_DeathScreen` returns `ObjectProperty /Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C:WBP_DeathScreen`
and nothing else outside the widget's own tree).

Evidence:

```
[000001F61D753C10] BlueprintGeneratedClass /Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C [n: 84B0D] [c: 00007FF46EEC3D38] [or: 000001F4D1C19430] [sps: 000001F615241870]
[000001F61FE52B80] ObjectProperty /Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C:WBP_DeathScreen [o: 5F0] [n: 204999] [c: 00007FF721DE9420] [owr: 000001F61D753C10] [pc: 00007FF46EEB3938]
[000001F61FE52900] SoftClassProperty /Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C:DeathScreenWidgetClass [o: 6B8] [n: 66E4A4] [c: 00007FF721DF97A0] [owr: 000001F61D753C10] [mc: 00007FF46EEB3938]
[000001F61FE52580] BoolProperty /Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C:HandlePlayerDeath:GrantDeathState [o: 0] [n: 667D38] [c: 00007FF721DE8D80] [owr: 000001F61FDFB800]
[000001F61FDFC000] Function /Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C:CreateDeathScreen [n: 66E48A] [c: 00007FF46EE55538] [or: 000001F61D753C10] [f: 00007FF7183D9710]
[000001F61FDFBD00] Function /Game/Sparta/Core/Player/Ability/GA_Death_Player.GA_Death_Player_C:EndDeath [n: 667D16] [c: 00007FF46EE55538] [or: 000001F61D753C10] [f: 00007FF7183D9710]
```

## 3.3 `WBP_DeathScreen_C`

Parent `/Script/UMG.UserWidget`. Properties:

| Property | Type | Offset |
|---|---|---|
| `Overlay_Main` | `ObjectProperty of /Script/UMG.Overlay` | `0x348` |
| `DeathSelection` | `ObjectProperty of /Game/Sparta/UI/Player/DeathScreen/WBP_MGT_DeathSelection.WBP_MGT_DeathSelection_C` | `0x350` |
| `ARB_Reveal` | `ObjectProperty of /Script/Sparta.AnimatableRetainerBox` | `0x358` |
| `Anim_FadeIn_Default` | `ObjectProperty of /Script/UMG.WidgetAnimation` | `0x360` |
| `Anim_FadeIn_Selection` | `ObjectProperty of /Script/UMG.WidgetAnimation` | `0x368` |
| `PlaySpeed` | `FloatProperty` | `0x370` |

| Function | Parameters |
|---|---|
| `Construct` | *(none)* |
| `OnDeathSelection` | *(none)* |
| `ResetWorld` | *(none — calls `SpartaWorldGenSubsystem` + a `BPI_Sparta_Game_Instance` cast)* |
| `CreateLoadingScreen` | *(none)* |
| `OnLoadingScreenCompleted` | *(none)* |
| `ClearWeatherOnDeath` | *(none)* |
| `BroadcastEvent` | *(none)* |
| `OnEffectEnd` | *(none)* |
| `HasSelection` | `ReturnValue: BoolProperty`, `CurrentRespawnPlayerStart: NameProperty` |
| `Finished_A29387214F3AC58888A687A63F14544C` / `Finished_16F279414E4F6F21CBCADFA21E2309E6` | *(animation finished callbacks)* |

`HasSelection` reads the respawn point through `BPFL_SaveLoad_C:GetRespawnPlayerStart` and
`GetAllHUBPlayerStarts`, and `IsPrologueCompleted` (visible as
`HasSelection:CallFunc_GetRespawnPlayerStart_ReturnValue`,
`HasSelection:CallFunc_GetAllHUBPlayerStarts_ReturnValue`,
`HasSelection:CallFunc_IsPrologueCompleted_ReturnValue`).

Evidence:

```
[000001FA58B913C0] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Player/DeathScreen/WBP_DeathScreen.WBP_DeathScreen_C [n: 48B470] [c: 00007FF46EEC3B38] [or: 000001F9F75E82B0] [sps: 00007FF46EEB3938]
[000001F9E7336F00] BoolProperty /Game/Sparta/UI/Player/DeathScreen/WBP_DeathScreen.WBP_DeathScreen_C:HasSelection:ReturnValue [o: 0] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 000001F9E731C300]
[000001FA183A4D80] NameProperty /Game/Sparta/UI/Player/DeathScreen/WBP_DeathScreen.WBP_DeathScreen_C:HasSelection:CurrentRespawnPlayerStart [o: 4] [n: A650E1] [c: 00007FF721DE90B0] [owr: 000001F9E731C300]
```

## 3.4 The respawn selector `WBP_MGT_DeathSelection_C`

Parent `/Game/Sparta/UI/Menu/WBP_Menu_Game_Tab.WBP_Menu_Game_Tab_C`. Functions:
`GetWidgetsToScale`, `TryInvalidateMapObjective`, `ActivateNavigation`, `DisableUIInput`,
`EnableUIInput`, `OnMenuOpen(TabIndex, SubTabIndex)`, `OnMenuClose`,
`BndEvt__WBP_MGT_DeathSelection_WBP_IL_DeathSelection_K2Node_ComponentBoundEvent_0_OnInputTriggered__DelegateSignature`,
`BndEvt__WBP_MGT_DeathSelection_ButtonBeacon_K2Node_ComponentBoundEvent_3_OnNavigableStateChanged__DelegateSignature`,
`BndEvt__WBP_MGT_DeathSelection_ButtonHUB_K2Node_ComponentBoundEvent_4_OnNavigableStateChanged__DelegateSignature`,
`BndEvt__WBP_MGT_DeathSelection_BP_HB_Options_K2Node_ComponentBoundEvent_1_OnNavigableSelected__DelegateSignature`,
`BndEvt__WBP_MGT_DeathSelection_BP_HB_Options_K2Node_ComponentBoundEvent_2_OnNavigableConfirmed__DelegateSignature`.

It binds the widget‑local delegates `OnNavigableSelected` / `OnNavigableConfirmed` /
`OnNavigableStateChanged` / `OnInputTriggered` — i.e. the "HUB or last beacon?" choice is made here.

## 3.5 The world side: `SpartaWorldGenSubsystem` and `BP_DeathManager_C`

`/Script/Sparta.SpartaWorldGenSubsystem` (parent `/Script/Engine.WorldSubsystem`):

| Function | Parameters |
|---|---|
| `BeginWorldReset` | *(none)* — the world‑reset entry point |
| `AddTagToCurrentWorldState` | `Tag: StructProperty of /Script/GameplayTags.GameplayTag` |
| `SetDifficulty` | `BiomeTag: StructProperty of /Script/GameplayTags.GameplayTag`, `NewDifficulty: IntProperty` |
| `IncreaseDifficulty` | `BiomeTag`, `IncreaseBy: IntProperty`, `ReturnValue: IntProperty` |
| `GetDifficulty` | `BiomeTag`, `ReturnValue: IntProperty` |
| `OnDataLayerRuntimeStateChanged` | `DataLayer: ObjectProperty of /Script/Engine.DataLayerInstance`, `State: EnumProperty of /Script/Engine.EDataLayerRuntimeState` |

`WBP_DeathScreen_C:ResetWorld` fetches a `SpartaWorldGenSubsystem`
(`ResetWorld:CallFunc_GetWorldSubsystem_ReturnValue` is typed `ObjectProperty of /Script/Sparta.SpartaWorldGenSubsystem`)
and the `GameInstance` cast to `BPI_Sparta_Game_Instance`. Which specific subsystem function it calls
is not visible (Blueprint call targets are not dumped) — GUESS: it is `BeginWorldReset`, because
that is the only reset‑shaped function the subsystem exposes.

`/Game/Sparta/Core/World/FogArea/BP_DeathManager.BP_DeathManager_C` (parent
`/Script/Engine.Actor`) is the local death bookkeeping actor: `OnDeath`, `BossDefeated`,
`AddKillCounter`, `AddTowerCounter`, `AddItem`, `AddTower`, `StartCountingClock`,
`HandleUnderglandRespawn`, `ShowStatsAfterDefeatingBoss`, `CreateAndFeedDataToStatScreen`,
`ReceiveEndPlay`.

Evidence:

```
[00007FF46F060338] Class /Script/Sparta.SpartaWorldGenSubsystem [n: 4AEFE] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE7C138]
[000001F8D3B1C700] BlueprintGeneratedClass /Game/Sparta/Core/World/FogArea/BP_DeathManager.BP_DeathManager_C [n: 202F65] [c: 00007FF46EEC3D38] [or: 000001F8C38402E0] [sps: 00007FF46EE52028]
```

## 3.6 Shell revive (a *different* mechanic from death — note for the sync author)

`/Game/Sparta/Core/Player/Ability/ShellRevive/` contains a full ability family that is **not** the
death screen: `GA_ShellReviveBase_C` (`ShellReviveStart`, `ShellReviveInitialize`,
`ShellReviveComplete`, `ShellReviveFX`, `ShellReviveRecall`, `ReviveShell`),
`GA_ShellRevive_Genessa_C`, `GA_ShellRevive_SavedByTheShell_C`,
`BPSO_ShellRevive_C` (save object), `GE_BlockShellRevive_C`, `GE_ShellReviveEfficiency_C`,
`GE_ModifyShellReviveProgress_C`, `GE_ResetShellReviveProgress_C`,
`BP_ShellReviveIndicator_C`, `ShellReviveStationPayloadObject_C`,
`/Game/Sparta/Core/Player/Components/BPC_Player_Health.BPC_Player_Health_C:ReviveShell`.
GUESS: `ReviveShell` on the health component is the Blueprint entry that starts a shell revive;
the ability graph itself is not visible in the object dump.

---

# 4. Loading screens, streaming, shader compiling

## 4.1 `SpartaLoadingScreenSubsystem`

`/Script/Sparta.SpartaLoadingScreenSubsystem`, parent `/Script/Engine.GameInstanceSubsystem`.
Only **4** members — the whole subsystem is a class holder:

| Member | Type | Offset |
|---|---|---|
| `OnLoadingScreenWidgetChanged` | `MulticastInlineDelegateProperty` | `0x30` |
| `LoadingScreenWidgetClass` | `ClassProperty` | `0x40` |

| Function | Parameters |
|---|---|
| `SetLoadingScreenContentWidget` | `NewWidgetClass: ClassProperty` |
| `GetLoadingScreenContentWidget` | `ReturnValue: ClassProperty` |

There is **no** `LoadStart` / `LoadComplete` / `PreLoad` / `PostLoad` on this subsystem.
The actual start/end is on `SpartaGameInstance` (`BeginLoadingScreen(MapName)` /
`EndLoadingScreen(InLoadedWorld)`) and on the widgets (§4.3).

Evidence:

```
[00007FF46F06B338] Class /Script/Sparta.SpartaLoadingScreenSubsystem [n: 4C509] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EF5DF38]
[000001F4ACE4F180] MulticastInlineDelegateProperty /Script/Sparta.SpartaLoadingScreenSubsystem:OnLoadingScreenWidgetChanged [o: 30] [n: 58AB37] [c: 00007FF721DE9010] [owr: 00007FF46F06B338] [df: 00007FF46F3CE398]
```

## 4.2 `BPFL_UI_C` — loading screen and fade API

| Function | Parameters |
|---|---|
| `TriggerLoadingScreen` | `Direction: ByteProperty`, `ZOrder: IntProperty`, `OnCompleted: DelegateProperty` (signature `/Game/Sparta/UI/Loading/WBP_LoadingScreenBase.WBP_LoadingScreenBase_C:OnCompleted__DelegateSignature`), `__WorldContext` |
| `TriggerFadeToBlack` | `OverrideWidget: ClassProperty`, `Color: StructProperty of /Script/CoreUObject.LinearColor`, `Direction: ByteProperty`, `duration: DoubleProperty`, `Hold: BoolProperty`, `HoldDuration: DoubleProperty`, `ZOrder: IntProperty`, `OnCompleted: DelegateProperty` |
| `FadeInScreenTransition` | `OverrideWidget: ClassProperty`, `ConstructType: ByteProperty`, `ZOrder: IntProperty`, `OnCompleted: DelegateProperty`, `duration: FloatProperty`, `__WorldContext` |
| `FadeOutScreenTransition` | `OnCompleted: DelegateProperty`, `ClearImmediately: BoolProperty`, `duration: FloatProperty`, `__WorldContext` |
| `GetCurrentScreenTransition` | `Success: BoolProperty`, `ReturnValue: ObjectProperty of /Game/Sparta/UI/Loading/WBP_LS_TransitionBase.WBP_LS_TransitionBase_C` |
| `RunDemoEnd` | `ZOrder: IntProperty`, `__WorldContext` |

Evidence:

```
[000001F606FB2300] Function /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:TriggerLoadingScreen [n: 637C38] [c: 00007FF46EE55538] [or: 000001F606B65890] [f: 00007FF7183D9710]
[000001F622E59300] ByteProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:TriggerLoadingScreen:Direction [o: 0] [n: 4C631E] [c: 00007FF721DE8DD0] [owr: 000001F606FB2300]
[000001F622E59280] DelegateProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:TriggerLoadingScreen:OnCompleted [o: 8] [n: 570F5B] [c: 00007FF721DE8E70] [owr: 000001F606FB2300] [df: 000001F4CE194B00]
[000001F622E57900] DelegateProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:TriggerFadeToBlack:OnCompleted [o: 3C] [n: 570F5B] [c: 00007FF721DE8E70] [owr: 000001F606FB5D00] [df: 000001F4CE194B00]
```

## 4.3 Loading‑screen widgets and their events

`WBP_LoadingScreenBase_C` declares only one thing:

| Member | Type | Offset |
|---|---|---|
| `OnCompleted__DelegateSignature` | `Function` (delegate signature, no parameters) | — |

`WBP_LoadingScreen_C` (parent `WBP_LoadingScreenBase_C`) functions:
`Construct`, `Destruct`, `PreConstruct`, `OnInitialized`, `InitializeFade`, `InitializeArtwork`,
`UpdateFade`, `BroadcastCreatedEvent`, `BroadcastCompletedEvent`,
`SequenceEvent__ENTRYPOINTWBP_LoadingScreen`.

`WBP_LoadingScreen_FadeToBlack_C` (parent `WBP_LoadingScreenBase_C`) functions:
`Construct`, `Initialize`, `UpdateColor`, `CalculatePlaybackSpeed`, `CompleteFadeIn`,
`OnFadeInCompleted`, `OnFadeOutCompleted`, `PlayFadeOut`.

`WBP_LS_TransitionBase_C` (parent `/Script/UMG.UserWidget`) functions:
`Construct`, `Initialize`, `FadeIn`, `FadeOut`, `UpdateFadeOut`, `ClearImmediately`, `Remove`,
`FadeInSpeed`, `FadeOutSpeed`, `FadeInAudio`, `FadeOutAudio`, `GetFadeInAnimation`,
`GetFadeOutAnimation`, `FadeOutCompleted__DelegateSignature`, `FadeInCompleted__DelegateSignature`.

`WBP_LS_FadeToBlack_AllowPauseMenu_C` (parent `WBP_LoadingScreen_FadeToBlack_C`) — the variant
that explicitly *keeps the pause menu usable*, functions `Construct`, `PreConstruct`, `Tick`,
`Initialize`, `OnFadeOutCompleted`.

Evidence:

```
[000001F606FAF430] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Loading/WBP_LoadingScreen_FadeToBlack.WBP_LoadingScreen_FadeToBlack_C [n: 141C20] [c: 00007FF46EEC3B38] [or: 000001F4C7056FF0] [sps: 000001F4CDF4DC90]
[000001F615AE07F0] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Loading/WBP_LoadingScreen.WBP_LoadingScreen_C [n: 3B7012] [c: 00007FF46EEC3B38] [or: 000001F4C7058080] [sps: 000001F4CDF4DC90]
[000001F615F60300] Function /Game/Sparta/UI/Loading/WBP_LoadingScreen.WBP_LoadingScreen_C:BroadcastCreatedEvent [n: 64DA33] [c: 00007FF46EE55538] [or: 000001F615AE07F0] [f: 00007FF7183D9710]
[000001F615F60400] Function /Game/Sparta/UI/Loading/WBP_LoadingScreen.WBP_LoadingScreen_C:BroadcastCompletedEvent [n: 64DA26] [c: 00007FF46EE55538] [or: 000001F615AE07F0] [f: 00007FF7183D9710]
[000001F61917F820] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Loading/WBP_LS_FadeToBlack_AllowPauseMenu.WBP_LS_FadeToBlack_AllowPauseMenu_C [n: 351665] [c: 00007FF46EEC3B38] [or: 000001F4C7056CD0] [sps: 000001F606FAF430]
```

## 4.4 Shader compiling

`/Game/Sparta/UI/Loading/WBP_ShaderCompiling.WBP_ShaderCompiling_C`, parent
`/Script/Sparta.SpartaWidget`.

| Member | Type | Offset |
|---|---|---|
| `OnCompilationComplete` | `MulticastInlineDelegateProperty` | `0x398` (signature `OnCompilationComplete__DelegateSignature`, no parameters) |

Functions: `PreConstruct`, `OnInitialized`, `Tick`, `AdjustScale`, `UpdateCompiling`,
`TriggerCompletion`, `OnCompilationComplete__DelegateSignature`.

Related menu tab: `/Game/Sparta/UI/Loading/WBP_MGT_ShaderCompilation.WBP_MGT_ShaderCompilation_C`
(parent `WBP_Menu_Game_Tab_C`), which carries `OnMenuOpen(TabIndex, SubTabIndex)` / `OnMenuClose`
and calls `IsMenuOpen`.

Evidence:

```
[000001F4BA367A20] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Loading/WBP_ShaderCompiling.WBP_ShaderCompiling_C [n: 344520] [c: 00007FF46EEC3B38] [or: 000001F4C4650B00] [sps: 00007FF46F05FD38]
[000001F4B87C2F00] MulticastInlineDelegateProperty /Game/Sparta/UI/Loading/WBP_ShaderCompiling.WBP_ShaderCompiling_C:OnCompilationComplete [o: 398] [n: 684DDD] [c: 00007FF721DE9010] [owr: 000001F4BA367A20] [df: 000001F4BA405D00]
[000001F4B8C7D600] Function /Game/Sparta/UI/Loading/WBP_ShaderCompiling.WBP_ShaderCompiling_C:TriggerCompletion [n: 6850B7] [c: 00007FF46EE55538] [or: 000001F4BA367A20] [f: 00007FF7183D9710]
```

## 4.5 Streaming screen

`/Script/Sparta.StreamingScreenWidget`, parent `/Script/UMG.UserWidget`, declares exactly one
function: `OnStreamingFinished()` (no parameters).

`/Game/Sparta/UI/Menu/WBP_StreamingScreen.WBP_StreamingScreen_C` (parent
`/Script/Sparta.StreamingScreenWidget`) overrides/implements `Construct`, `InitializeFade`,
`InitializeArtwork`, `OnStreamingFinished`.

`SpartaGameMode.StreamingScreenClass` (`ClassProperty`, `0x340`) is the hook that selects which
streaming‑screen widget class the game mode uses.

Evidence:

```
[00007FF46F062138] Class /Script/Sparta.StreamingScreenWidget [n: 4B055] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EEB3938]
[000001F4B0257A20] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Menu/WBP_StreamingScreen.WBP_StreamingScreen_C [n: 8AF07] [c: 00007FF46EEC3B38] [or: 000001F4C635DF30] [sps: 00007FF46F062138]
[000001F4DDCACF00] Function /Game/Sparta/UI/Menu/WBP_StreamingScreen.WBP_StreamingScreen_C:OnStreamingFinished [n: 1470F] [c: 00007FF46EE55538] [or: 000001F4B0257A20] [f: 00007FF7183D9710]
```

## 4.6 Startup videos and game logo

`/Game/Sparta/UI/Loading/WBP_StartupVideos.WBP_StartupVideos_C`, parent `/Script/UMG.UserWidget`.

| Member | Type | Offset |
|---|---|---|
| `OnComplete` | `MulticastInlineDelegateProperty` (signature `OnComplete__DelegateSignature`, no parameters) | `0x370` |
| `OnPreComplete` | `MulticastInlineDelegateProperty` (signature `OnPreComplete__DelegateSignature`, no parameters) | `0x388` |

Functions: `Construct`, `GetVideoSize`, `SkipVideos`, `OnCSLogoEnd`, `OnPlaystackLogoEnd`,
`OnWaveweaverLogoEnd`.

`/Game/Sparta/UI/MainMenu/WBP_GameLogo.WBP_GameLogo_C`, parent `/Script/UMG.UserWidget`, functions:
`PreConstruct`, `AdjustScale`.

Evidence:

```
[000001F4BA36FC10] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Loading/WBP_StartupVideos.WBP_StartupVideos_C [n: 3C9592] [c: 00007FF46EEC3B38] [or: 000001F4C6A13A30] [sps: 00007FF46EEB3938]
[000001F4B87C3880] MulticastInlineDelegateProperty /Game/Sparta/UI/Loading/WBP_StartupVideos.WBP_StartupVideos_C:OnComplete [o: 370] [n: 54DDF0] [c: 00007FF721DE9010] [owr: 000001F4BA36FC10] [df: 000001F4B9296D00]
[000001F4B87C3980] MulticastInlineDelegateProperty /Game/Sparta/UI/Loading/WBP_StartupVideos.WBP_StartupVideos_C:OnPreComplete [o: 388] [n: 684AC8] [c: 00007FF721DE9010] [owr: 000001F4BA36FC10] [df: 000001F4B6FC7200]
[000001F4B1EC3730] WidgetBlueprintGeneratedClass /Game/Sparta/UI/MainMenu/WBP_GameLogo.WBP_GameLogo_C [n: 2521F7] [c: 00007FF46EEC3B38] [or: 000001F4C705C360] [sps: 00007FF46EEB3938]
[000001F4B1E35700] Function /Game/Sparta/UI/MainMenu/WBP_GameLogo.WBP_GameLogo_C:AdjustScale [n: 6309DF] [c: 00007FF46EE55538] [or: 000001F4B1EC3730] [f: 00007FF7183D9710]
```

---

# 5. Cutscenes / sequencers

## 5.1 Native handler

`/Script/Sparta.SpartaCinematicAudioHandler`, parent `/Script/CoreUObject.Object`:

| Property | Type | Offset |
|---|---|---|
| `m_CurrentAudioData` | `ObjectProperty of /Script/Sparta.SpartaCinematicAudioData` | `0x28` |
| `m_AudioComponent` | `ObjectProperty of /Script/Sparta.SpartaAudioComponent` | `0x30` |
| `m_MusicBlocker` | `ObjectProperty of /Script/Sparta.SpartaMusicBlockerHandle` | `0x38` |

| Function | Parameters |
|---|---|
| `PlayCinematic` | `in_Data: ObjectProperty of /Script/Sparta.SpartaCinematicAudioData` |
| `StopCinematic` | `in_FadeOutDuration: FloatProperty`, `in_FaderCurve: EnumProperty of /Script/Engine.EAudioFaderCurve` |
| `OnCinematicStart` | *(none)* |
| `OnCinematicEnd` | `bForceStop: BoolProperty` |
| `IsPlayingCinematic` | `ReturnValue: BoolProperty` |
| `Init` / `Deinit` | *(none)* |
| `GetCinematicAudioHandler` | `in_WCO: ObjectProperty of /Script/CoreUObject.Object`, `ReturnValue: ObjectProperty of /Script/Sparta.SpartaCinematicAudioHandler` |

Blueprint subclass in use: `/Game/Sparta/Core/Audio/BP_CinematicAudioHandler.BP_CinematicAudioHandler_C`.
It is created by `SpartaGameInstance:CreateCinematicAudioHandler` /
`BP_SpartaGameInstance_C:CreateCinematicAudioHandler`.

Evidence:

```
[00007FF46F044338] Class /Script/Sparta.SpartaCinematicAudioHandler [n: 48E52] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE50070]
[000001F4ACDA1780] ObjectProperty /Script/Sparta.SpartaCinematicAudioHandler:m_CurrentAudioData [o: 28] [n: 58787C] [c: 00007FF721DE9420] [owr: 00007FF46F044338] [pc: 00007FF46F044138]
[000001F4ACDA1A00] ObjectProperty /Script/Sparta.SpartaCinematicAudioHandler:PlayCinematic:in_Data [o: 0] [n: 524D44] [c: 00007FF721DE9420] [owr: 00007FF46F6F9DF8] [pc: 00007FF46F044138]
[000001F4ACDA1A80] BoolProperty /Script/Sparta.SpartaCinematicAudioHandler:OnCinematicEnd:bForceStop [o: 0] [n: 58785C] [c: 00007FF721DE8D80] [owr: 00007FF46F6F9C38]
[000001F4ACDA1B00] BoolProperty /Script/Sparta.SpartaCinematicAudioHandler:IsPlayingCinematic:ReturnValue [o: 0] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 00007FF46F6F9B58]
```

## 5.2 The one Blueprint actor that plays cutscenes: `BP_CinematicPlayer_C`

`/Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C`,
parent `/Script/Engine.Actor`. **This is the single cutscene driver**: 45 functions, one actor
placed in the level per cutscene.

Properties (declared, in offset order):

| Property | Type | Offset |
|---|---|---|
| `TriggerArea` | `ObjectProperty of /Script/Engine.BoxComponent` | `0x2b0` |
| `CutsceneActor` | `ObjectProperty of /Script/LevelSequence.LevelSequenceActor` | `0x2c0` |
| `LevelSequenceToPlay` | `ObjectProperty of /Script/LevelSequence.LevelSequence` | `0x2c8` |
| `SpawnedEnemyAtEnd` | `ObjectProperty of /Game/Sparta/Characters/SpawnSystem/BP_Spawner.BP_Spawner_C` | `0x2d0` |
| `PlayCinematicOnSpawn` | `BoolProperty` | `0x2d8` |
| `HidePlayerDuringCinematic` | `BoolProperty` | `0x2d9` |
| `UseTriggerToPlay` | `BoolProperty` | `0x2da` |
| `PlayerPositionAfterCinematic` | `ObjectProperty of /Script/Engine.TargetPoint` | `0x2e0` |
| **`CinematicEndReached`** | **`MulticastInlineDelegateProperty`** | **`0x2e8`** |
| `BroadcastEventAfterCinematic` | `BoolProperty` | `0x2f8` |
| `EventToBroadcast` | `ClassProperty` | `0x300` |
| `StartOnBroadcastEvent` | `ClassProperty` | `0x308` |
| `SaveFileCheckForPlayOnSpawn` | `NameProperty` | `0x310` |
| `ForceCinematic` | `BoolProperty` | `0x318` |
| `ShowHudOnEnd` | `BoolProperty` | `0x319` |
| `FadePlayerCameraAtStart` | `BoolProperty` | `0x31a` |
| `UnFadePlayerCameraAtEnd` | `BoolProperty` | `0x31b` |
| `SetPlayerMovementModeAtEnd` | `BoolProperty` | `0x31c` |
| `CheckForNarrativeBool` | `BoolProperty` | `0x31d` |
| `OnlyPlayOnce` | `BoolProperty` | `0x31e` |
| `ThisCinematicTag` | `StructProperty of /Script/GameplayTags.GameplayTag` | `0x320` |
| `AlreadyPlayedBehaviors` | `ArrayProperty of /Script/Sparta.ExecutableBehavior` | `0x328` |
| `PlayerBoolToCheck` | `NameProperty` | `0x338` |
| `StartBehaviors` | `ArrayProperty of /Script/Sparta.ExecutableBehavior` | `0x340` |
| `EndBehaviors` | `ArrayProperty of /Script/Sparta.ExecutableBehavior` | `0x350` |
| `FireEventsOnSkip` | `BoolProperty` | `0x360` |
| `SpawnTriggerToEnableWhenSequenceDisabled` | `SoftObjectProperty of /Game/Sparta/Characters/SpawnSystem/BP_SpawnTrigger.BP_SpawnTrigger_C` | `0x368` |
| `TriggerActivated` | `BoolProperty` | `0x390` |
| `WBP_SkipInput` | `ObjectProperty of /Game/Sparta/UI/Core/WBP_SkipInput.WBP_SkipInput_C` | `0x398` |
| `StateGE` | `StructProperty of /Script/GameplayAbilities.ActiveGameplayEffectHandle` | `0x3a0` |
| `AudioData` | `ObjectProperty of /Script/Sparta.SpartaCinematicAudioData` | `0x3a8` |
| `Skippable` | `BoolProperty` | `0x3b0` |
| `ResetDoOnce` | `BoolProperty` | `0x3b1` |
| `FadeInDuration` | `FloatProperty` | `0x3b4` |
| `FadeOutDuration` | `FloatProperty` | `0x3b8` |
| `WeatherId` | `Int64Property` | `0x3c0` |
| `Weather` | `ObjectProperty of /Script/Sparta.SpartaWeatherPreset` | `0x3c8` and `0x3d0` (two properties share the printed name) |

Functions that START a cutscene (all take **no** parameters unless noted):

| Function | Parameters | Note |
|---|---|---|
| `PlayCutscene` | *(none)* | main entry |
| `PrepareCutscene` | *(none)* | |
| `StartCinematicViaEvent` | `EventData: ObjectProperty of /Script/CoreUObject.Object` | started by a GlobalEvent |
| `TriggerCutsceneViaInteraction` | *(none)* | |
| `PlayToEndAndFireEvents` | *(none)* | |

Functions that END a cutscene:

| Function | Parameters | Note |
|---|---|---|
| `EndCutscene` | *(none)* | |
| `EndCutsceneByTimer` | *(none)* | |
| `EndCutsceneBySkip` | *(none)* | |
| `OnSkipEvent` | *(none)* | |
| `ForceStopCinematic` | *(none)* | |

Broadcast functions (these fire the global events):

| Function | Parameters |
|---|---|
| `BroadcastCinematicStart` | *(none)* — spawns `GlobalEvent_Cinematic_Start_C` |
| `BroadcastCinematicEnd` | *(none)* — spawns `GlobalEvent_Cinematic_End_C` |
| `BroadcastEndEvent` | *(none)* |
| `CinematicEndReached__DelegateSignature` | *(none)* — the signature of the `0x2e8` multicast delegate |

Support functions: `AddCutsceneState`, `RemoveCutsceneState`, `LockInteraction`, `UnlockInteraction`,
`DisablePlayerMovement`, `ResetPlayerMovement`, `HidePlayerAndWeapons`, `UnhidePlayerAndWeapons`,
`TeleportPlayer`, `HideHud`, `ShowHUD`, `HandleStartCameraFade`, `HandleEndCameraFade`,
`ClearAnyCameraFade`, `StartAudio`, `AddSkipInput`, `RemoveSkipInput`, `AddWeatherToStack`,
`RemoveWeatherFromStack`, `HandleEnemySpawn`, `SaveCinematic`, `CheckForCinematicPlayed`,
`CheckNarrativeTagAndSetState`, `ExecuteStartBehaviors`, `ExecuteEndBehaviors`,
`ExecuteAlreadyPlayedBehaviors`, `EnableTrigger`, `ResetDoOnceLogic`, `ResetPlayTriggerDoOnce`,
`ResetPlayCutsceneDoOnce`, `ResetCinematicEndDoOnce`, `ReceiveBeginPlay`,
`OnEventFired_F984C41B4CD4FA8DF0F8668843789466(EventData)`,
`OnLoaded_831E15B041EF08EBF40AD0A3F146956E(Loaded)`.

Note that `PlayCutscene`, `EndCutscene`, `BroadcastCinematicStart`, `BroadcastCinematicEnd`,
`PrepareCutscene`, `EndCutsceneByTimer`, `EndCutsceneBySkip`, `ForceStopCinematic`,
`TriggerCutsceneViaInteraction` and `CinematicEndReached__DelegateSignature` all have **zero
parameters** (the dump shows no member objects owned by those functions); only
`StartCinematicViaEvent` has one (`EventData`).

Evidence:

```
[000001F4EB562A40] BlueprintGeneratedClass /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C [n: 3EB396] [c: 00007FF46EEC3D38] [or: 000001F4CEED01A0] [sps: 00007FF46EE52028]
[000001F61737F980] MulticastInlineDelegateProperty /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C:CinematicEndReached [o: 2E8] [n: 6536B7] [c: 00007FF721DE9010] [owr: 000001F4EB562A40] [df: 000001F4EB53DE00]
[000001F4EB53E200] Function /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C:BroadcastCinematicStart [n: 6534AC] [c: 00007FF46EE55538] [or: 000001F4EB562A40] [f: 00007FF7183D9710]
[000001F4EB53E300] Function /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C:BroadcastCinematicEnd [n: 653830] [c: 00007FF46EE55538] [or: 000001F4EB562A40] [f: 00007FF7183D9710]
[000001F4EB53C900] Function /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C:PlayCutscene [n: 6535B4] [c: 00007FF46EE55538] [or: 000001F4EB562A40] [f: 00007FF7183D9710]
[000001F4EB53DA00] Function /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C:EndCutscene [n: 653860] [c: 00007FF46EE55538] [or: 000001F4EB562A40] [f: 00007FF7183D9710]
[000001F4EB53F300] Function /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C:StartCinematicViaEvent [n: 6535C4] [c: 00007FF46EE55538] [or: 000001F4EB562A40] [f: 00007FF7183D9710]
[000001F61737EB00] ObjectProperty /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C:StartCinematicViaEvent:EventData [o: 0] [n: 524EDF] [c: 00007FF721DE9420] [owr: 000001F4EB53F300] [pc: 00007FF46EE50070]
[000001F4EB53DE00] Function /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C:CinematicEndReached__DelegateSignature [n: 6534E9] [c: 00007FF46EE55538] [or: 000001F4EB562A40] [f: 00007FF7183D9710]
```

## 5.3 Every `LevelSequence` asset under `/Game/Sparta/` (52)

Enumerated with SQL (`scripts\q\fm-sequences-list.sql`:
`SELECT type,path FROM obj WHERE path LIKE '/Game/Sparta/%' AND type IN ('LevelSequence','ActorSequence','CameraAnimationSequence')`)
and counted with `scripts\q\fm-sequences-count.sql` (`LevelSequence` = **52**).

| # | Asset path |
|---|---|
| 1 | `/Game/Sparta/Blueprints/DungeonScripts/LS_SiegeIntro.LS_SiegeIntro` |
| 2 | `/Game/Sparta/Characters/Bosses/Dwarf/Cinematic/Intro/CIN_DwarfBoss_01.CIN_DwarfBoss_01` |
| 3 | `/Game/Sparta/Characters/Bosses/FinalBoss/Cinematic/Intro/LS_FinalBoss_Intro.LS_FinalBoss_Intro` |
| 4 | `/Game/Sparta/Characters/Bosses/FinalBoss/Cinematic/LS_FinalBoss_MemoryPreview.LS_FinalBoss_MemoryPreview` |
| 5 | `/Game/Sparta/Characters/Bosses/FinalBoss/Cinematic/Outro/LS_FinalBoss_Outro_02.LS_FinalBoss_Outro_02` |
| 6 | `/Game/Sparta/Characters/Bosses/Monolith/Cinematic/Intro/LevelSequence_Monolith_Intro.LevelSequence_Monolith_Intro` |
| 7 | `/Game/Sparta/Characters/Bosses/Swordman/Cinematic/Intro/CIN_Swordman_Intro_01.CIN_Swordman_Intro_01` |
| 8 | `/Game/Sparta/Cutscenes/FlowerCult_Intro/LS_CultParentsAndRuk_Intro.LS_CultParentsAndRuk_Intro` |
| 9 | `/Game/Sparta/Cutscenes/FlowerCult_Intro/LS_Prologue_Darkbro_Wakeup_02.LS_Prologue_Darkbro_Wakeup_02` |
| 10 | `/Game/Sparta/Cutscenes/FlowerCult_Intro/LS_Prologue_StoneformedVillagers_02.LS_Prologue_StoneformedVillagers_02` |
| 11 | `/Game/Sparta/Cutscenes/Good_Ending/Good_Ending_Master.Good_Ending_Master` |
| 12 | `/Game/Sparta/Cutscenes/Good_Ending/Good_Ending_Start.Good_Ending_Start` |
| 13 | `/Game/Sparta/Cutscenes/Good_Ending/Lute_Playing.Lute_Playing` |
| 14 | `/Game/Sparta/Cutscenes/Good_Ending/Vista_01.Vista_01` |
| 15 | `/Game/Sparta/Cutscenes/Good_Ending/Vista_02.Vista_02` |
| 16 | `/Game/Sparta/Cutscenes/Good_Ending/Vista_03.Vista_03` |
| 17 | `/Game/Sparta/Cutscenes/Good_Ending/Vista_04.Vista_04` |
| 18 | `/Game/Sparta/Cutscenes/Good_Ending/Vista_05.Vista_05` |
| 19 | `/Game/Sparta/Cutscenes/HUB/GloomSiphonSurpriseEggOpen.GloomSiphonSurpriseEggOpen` |
| 20 | `/Game/Sparta/Cutscenes/HUB/Sequence_GloomSiphonSurpriseEggCuts.Sequence_GloomSiphonSurpriseEggCuts` |
| 21 | `/Game/Sparta/Cutscenes/Hexapod/Intro/Hexapod_Intro_MASTER.Hexapod_Intro_MASTER` |
| 22 | `/Game/Sparta/Cutscenes/Hexapod/Intro/Shot_01/Hexapod_Shot_01.Hexapod_Shot_01` |
| 23 | `/Game/Sparta/Cutscenes/Hexapod/IntroV2/LS_Cinematic_Hexapod_Intro_V3.LS_Cinematic_Hexapod_Intro_V3` |
| 24 | `/Game/Sparta/Cutscenes/LadyOfTheWood/Intro/LadyOfTheWood_Intro_MASTER.LadyOfTheWood_Intro_MASTER` |
| 25 | `/Game/Sparta/Cutscenes/LadyOfTheWood/Intro/Shot01/LS_LOTW_Shot01.LS_LOTW_Shot01` |
| 26 | `/Game/Sparta/Cutscenes/LadyOfTheWood/Intro/Shot02/LS_LOTW_Shot02.LS_LOTW_Shot02` |
| 27 | `/Game/Sparta/Cutscenes/LadyOfTheWood/Intro/Shot03/LS_LOTW_Shot03.LS_LOTW_Shot03` |
| 28 | `/Game/Sparta/Cutscenes/LadyOfTheWood/Intro/Shot04/LS_LOTW_Shot04.LS_LOTW_Shot04` |
| 29 | `/Game/Sparta/Cutscenes/Memories/Genessa/TielHug/LS_Tiel_Genessa_Hug.LS_Tiel_Genessa_Hug` |
| 30 | `/Game/Sparta/Cutscenes/Memories/Gragu/TheTavernAlienHeart/LS_Gragu_TheTavern_AlienHeart.LS_Gragu_TheTavern_AlienHeart` |
| 31 | `/Game/Sparta/Cutscenes/MothKnight/Shot_01/MothKnight_Shot_01.MothKnight_Shot_01` |
| 32 | `/Game/Sparta/Cutscenes/MouthBoss/LS_MouthBoss_Intro.LS_MouthBoss_Intro` |
| 33 | `/Game/Sparta/Cutscenes/NPCs/ShopkeeperDogRevenge/LS_Shopkeeper_DogRevenge.LS_Shopkeeper_DogRevenge` |
| 34 | `/Game/Sparta/Cutscenes/Offspring/LS_Offspring_Intro.LS_Offspring_Intro` |
| 35 | `/Game/Sparta/Cutscenes/ParasiteGolem/Intro/LS_ParasiteGolem_PhaseTransition.LS_ParasiteGolem_PhaseTransition` |
| 36 | `/Game/Sparta/Cutscenes/TarGolem/Intro/Intro_02/LS_TagGolem_Headless_Intro_02.LS_TagGolem_Headless_Intro_02` (sic: `TagGolem`) |
| 37 | `/Game/Sparta/Cutscenes/TarGolem/Intro/Shot01/LS_TG_Shot01.LS_TG_Shot01` |
| 38 | `/Game/Sparta/Cutscenes/TarGolem/Intro/Shot01/LS_TG_Shot02.LS_TG_Shot02` |
| 39 | `/Game/Sparta/Cutscenes/TarGolem/Intro/Shot01/LS_TG_Shot03.LS_TG_Shot03` |
| 40 | `/Game/Sparta/Cutscenes/TarGolem/Intro/Shot01/LS_TG_Shot04.LS_TG_Shot04` |
| 41 | `/Game/Sparta/Cutscenes/TarGolem/Intro/TarGolem_Intro_MASTER.TarGolem_Intro_MASTER` |
| 42 | `/Game/Sparta/Cutscenes/TarGolem/Outro/Shot01/Tar_Golem_Outro_Shot_Anim_02.Tar_Golem_Outro_Shot_Anim_02` |
| 43 | `/Game/Sparta/Cutscenes/TarGolem/Outro/TarGolem_Outro_MASTER_02.TarGolem_Outro_MASTER_02` |
| 44 | `/Game/Sparta/Dialogue/Sequences/Hub/FinalStairs/FinalStairsLightsSequence.FinalStairsLightsSequence` |
| 45 | `/Game/Sparta/Dialogue/Sequences/Hub/FinalStairs/FinalStairsSequence.FinalStairsSequence` |
| 46 | `/Game/Sparta/Dialogue/Sequences/Hub/ShellKeeper/LevelSequence_ShellKeeperIntro_CoreWorld.LevelSequence_ShellKeeperIntro_CoreWorld` |
| 47 | `/Game/Sparta/Environments/Milestone_05.Milestone_05` |
| 48 | `/Game/Sparta/FX/VFX/SkyDive/LS_Launcher_Activation.LS_Launcher_Activation` |
| 49 | `/Game/Sparta/FX/VFX/SkyDive/LS_Launcher_Skydive2.LS_Launcher_Skydive2` |
| 50 | `/Game/Sparta/Maps/AnimZoo/PoseMaker/LevelSequence_Poses.LevelSequence_Poses` |
| 51 | `/Game/Sparta/Maps/MAPS/TheForest/Sequences/Hollows_HotWheels.Hollows_HotWheels` |
| 52 | `/Game/Sparta/Testing/PlaceholderIntro.PlaceholderIntro` |

Also present under `/Game/Sparta/`: **23 `ActorSequence`** objects (embedded sequences inside
Blueprint actors, e.g. `BP_ActorSequence_C:ActorSequence_GEN_VARIABLE.Sequence`,
`BP_NarrativeEvent_C:…`, `BP_EnemyDuel_C:…`, `BP_ShellMemory_C:…` and one per shell‑memory
Blueprint) and one `CameraAnimationSequence`
(`/Game/Sparta/FX/VFX/Abilities/Handshake/AS_CameraHandshake.AS_CameraHandshake`).

Evidence (the full list above came from the SQL file named, which reads the same `obj` table; these
are the verbatim dump lines for one representative asset and for the `LevelSequence` class):

```
[000001F8C3EFE200] LevelSequence /Game/Sparta/Cutscenes/TarGolem/Intro/TarGolem_Intro_MASTER.TarGolem_Intro_MASTER [n: 46BE60] [c: 00007FF46EECBD38] [or: 000001F8E9B5EF20]
[00007FF46EECBD38] Class /Script/LevelSequence.LevelSequence [n: 11C7C] [c: 00007FF46EE55138] [or: 00007FF46F17B5F8] [sps: 00007FF46EE89738]
```

## 5.4 Classes that *play* sequences — `LevelSequenceDirector` Blueprints

Sequences are driven by generated `/Script/LevelSequence.LevelSequenceDirector` subclasses. The
dump contains **exactly 23** of them under `/Game/Sparta/`
(`parse_dump.py find raw\index.sqlite3 "DirectorBP_C$" --types BlueprintGeneratedClass`):

| # | Director class (verified `sps:` → parent) |
|---|---|
| 1 | `/Game/Sparta/Characters/Bosses/Dwarf/Cinematic/Intro/CIN_DwarfBoss_01.CIN_DwarfBoss_01_DirectorBP_C` |
| 2 | `/Game/Sparta/Characters/Bosses/FinalBoss/Cinematic/Intro/LS_FinalBoss_Intro.LS_FinalBoss_Intro_DirectorBP_C` |
| 3 | `/Game/Sparta/Characters/Bosses/Monolith/Cinematic/Intro/LevelSequence_Monolith_Intro.LevelSequence_Monolith_Intro_DirectorBP_C` |
| 4 | `/Game/Sparta/Characters/Bosses/Swordman/Cinematic/Intro/CIN_Swordman_Intro_01.CIN_Swordman_Intro_01_DirectorBP_C` |
| 5 | `/Game/Sparta/Cutscenes/FlowerCult_Intro/LS_Prologue_Darkbro_Wakeup_02.LS_Prologue_Darkbro_Wakeup_02_DirectorBP_C` |
| 6 | `/Game/Sparta/Cutscenes/FlowerCult_Intro/LS_Prologue_StoneformedVillagers_02.LS_Prologue_StoneformedVillagers_02_DirectorBP_C` |
| 7 | `/Game/Sparta/Cutscenes/Hexapod/IntroV2/LS_Cinematic_Hexapod_Intro_V3.LS_Cinematic_Hexapod_Intro_V3_DirectorBP_C` |
| 8 | `/Game/Sparta/Cutscenes/LadyOfTheWood/Intro/LadyOfTheWood_Intro_MASTER.LadyOfTheWood_Intro_MASTER_DirectorBP_C` |
| 9 | `/Game/Sparta/Cutscenes/LadyOfTheWood/Intro/Shot01/LS_LOTW_Shot01.LS_LOTW_Shot01_DirectorBP_C` |
| 10 | `/Game/Sparta/Cutscenes/Memories/Gragu/TheTavernAlienHeart/LS_Gragu_TheTavern_AlienHeart.LS_Gragu_TheTavern_AlienHeart_DirectorBP_C` |
| 11 | `/Game/Sparta/Cutscenes/MothKnight/Shot_01/MothKnight_Shot_01.MothKnight_Shot_01_DirectorBP_C` |
| 12 | `/Game/Sparta/Cutscenes/MouthBoss/LS_MouthBoss_Intro.LS_MouthBoss_Intro_DirectorBP_C` |
| 13 | `/Game/Sparta/Cutscenes/NPCs/ShopkeeperDogRevenge/LS_Shopkeeper_DogRevenge.LS_Shopkeeper_DogRevenge_DirectorBP_C` |
| 14 | `/Game/Sparta/Cutscenes/ParasiteGolem/Intro/LS_ParasiteGolem_PhaseTransition.LS_ParasiteGolem_PhaseTransition_DirectorBP_C` |
| 15 | `/Game/Sparta/Cutscenes/TarGolem/Intro/Shot01/LS_TG_Shot04.LS_TG_Shot04_DirectorBP_C` |
| 16 | `/Game/Sparta/Cutscenes/TarGolem/Intro/TarGolem_Intro_MASTER.TarGolem_Intro_MASTER_DirectorBP_C` |
| 17 | `/Game/Sparta/Cutscenes/TarGolem/Outro/TarGolem_Outro_MASTER_02.TarGolem_Outro_MASTER_02_DirectorBP_C` |
| 18 | `/Game/Sparta/Dialogue/Sequences/Hub/FinalStairs/FinalStairsLightsSequence.FinalStairsLightsSequence_DirectorBP_C` |
| 19 | `/Game/Sparta/Dialogue/Sequences/Hub/FinalStairs/FinalStairsSequence.FinalStairsSequence_DirectorBP_C` |
| 20 | `/Game/Sparta/Dialogue/Sequences/Hub/ShellKeeper/LevelSequence_ShellKeeperIntro_CoreWorld.LevelSequence_ShellKeeperIntro_CoreWorld_DirectorBP_C` |
| 21 | `/Game/Sparta/FX/VFX/SkyDive/LS_Launcher_Activation.LS_Launcher_Activation_DirectorBP_C` |
| 22 | `/Game/Sparta/FX/VFX/SkyDive/LS_Launcher_Skydive2.LS_Launcher_Skydive2_DirectorBP_C` |
| 23 | `/Game/Sparta/Testing/PlaceholderIntro.PlaceholderIntro_DirectorBP_C` |

Sequence event entry points (these are the "cutscene started / cutscene finished" hooks a mod can
bind at sequence granularity). Verified names:

| Director | Begin event | End event |
|---|---|---|
| `CIN_DwarfBoss_01_DirectorBP_C` | `SequenceEvent_Start` | `SequenceEvent_End` |
| `LS_FinalBoss_Intro_DirectorBP_C` | `SequenceEvent_Start` | `SequenceEvent_End` |
| `LevelSequence_Monolith_Intro_DirectorBP_C` | `SequenceEvent_Start` | `SequenceEvent_End` |
| `CIN_Swordman_Intro_01_DirectorBP_C` | `SequenceEvent_CineBegin` | `SequenceEvent_CineEnd` |
| `LS_Cinematic_Hexapod_Intro_V3_DirectorBP_C` | `SequenceEvent_Begin` | `SequenceEvent_End` |
| `TarGolem_Outro_MASTER_02_DirectorBP_C` | `SequenceEvent_BeginCinematic` | `SequenceEvent_EndCinematic` |
| the other 17 directors | UNKNOWN names | UNKNOWN names |

UNKNOWN for the other 17: the object dump only exposes the function lists of classes it loaded as
members; the remaining directors' `SequenceEvent_*` names were not enumerated in this pass.
Tried: `parse_dump.py find raw\index.sqlite3 "Cinematic" --types Function` (which surfaced the six
directors listed above) and `parse_dump.py find raw\index.sqlite3 "DirectorBP_C$" --types BlueprintGeneratedClass`
(which gives the class list but not their functions). Re-run
`python scripts\parse_dump.py fun raw\index.sqlite3 "DirectorBP_C"` to enumerate all 23 at once.

Every director also declares two `SequenceEvent__ENTRYPOINT<DirectorName>` /
`SequenceEvent__ENTRYPOINT<DirectorName>_0` functions (one per authored event); neither takes
parameters. All `SequenceEvent_*` functions take **no** parameters (the dump shows no member rows
owned by them).

Evidence:

```
[000001F4C2D22DD0] BlueprintGeneratedClass /Game/Sparta/Characters/Bosses/Swordman/Cinematic/Intro/CIN_Swordman_Intro_01.CIN_Swordman_Intro_01_DirectorBP_C [n: 38FB27] [c: 00007FF46EEC3D38] [or: 000001F4C2840740] [sps: 00007FF46EECCB38]
[000001F4C2CE8B00] Function /Game/Sparta/Characters/Bosses/Swordman/Cinematic/Intro/CIN_Swordman_Intro_01.CIN_Swordman_Intro_01_DirectorBP_C:SequenceEvent_CineBegin [n: 749225] [c: 00007FF46EE55538] [or: 000001F4C2D22DD0] [f: 00007FF7183D9710]
[000001F4C2CE8A00] Function /Game/Sparta/Characters/Bosses/Swordman/Cinematic/Intro/CIN_Swordman_Intro_01.CIN_Swordman_Intro_01_DirectorBP_C:SequenceEvent_CineEnd [n: 749232] [c: 00007FF46EE55538] [or: 000001F4C2D22DD0] [f: 00007FF7183D9710]
[00007FF46EECCB38] Class /Script/LevelSequence.LevelSequenceDirector [n: 2C4BD] [c: 00007FF46EE55138] [or: 00007FF46F17B5F8] [sps: 00007FF46EE50070]
```

## 5.5 Cinematic global events a mod can subscribe to

| Event class | Broadcast when |
|---|---|
| `/Game/Sparta/Core/GES/GlobalEvent_Cinematic_Start.GlobalEvent_Cinematic_Start_C` | `BP_CinematicPlayer_C:BroadcastCinematicStart` (verified: that function's `CallFunc_SpawnObject_ReturnValue` is typed `GlobalEvent_Cinematic_Start_C`) | payload `CinematicPlayer: BP_CinematicPlayer_C` @ `0x28` |
| `/Game/Sparta/Core/GES/GlobalEvent_Cinematic_End.GlobalEvent_Cinematic_End_C` | `BP_CinematicPlayer_C:BroadcastCinematicEnd` (same check, `…_End_C`) | payload `CinematicPlayer: BP_CinematicPlayer_C` @ `0x28` |
| `/Game/Sparta/Core/GES/GlobalEvent_Cinematic_EnableTrigger.GlobalEvent_Cinematic_EnableTrigger_C` | — |
| `/Game/Sparta/Core/GES/GlobalEvent_Cinematic_StartSequence_Prologue_DarkbroWakeup.GlobalEvent_Cinematic_StartSequence_Prologue_DarkbroWakeup_C` | prologue wake‑up sequence |
| `/Game/Sparta/Core/GES/GlobalEvent_Cinematic_StartSequence_BossOvum_2.GlobalEvent_Cinematic_StartSequence_BossOvum_2_C` | |
| `/Game/Sparta/Core/GES/GlobalEvent_Cinematic_StartSequence_Memory.GlobalEvent_Cinematic_StartSequence_Memory_C` | shell‑memory sequences |
| `/Game/Sparta/Core/GES/GlobalEvent_ShellMemoryPlaySequence.GlobalEvent_ShellMemoryPlaySequence_C` | |
| `/Game/Sparta/Core/GES/GlobalEvent_StartFinalStairsSequence.GlobalEvent_StartFinalStairsSequence_C` | |
| `/Game/Sparta/Core/GES/GlobalEvent_Cinematic_StartRukLookat.GlobalEvent_Cinematic_StartRukLookat_C` / `…_StopRukLookat…` | look‑at control during a cinematic |
| `/Game/Sparta/Core/GES/GlobalEvent_SwordmanCinematicEnd.GlobalEvent_SwordmanCinematicEnd_C` | Swordman intro finished |
| `/Game/Sparta/Core/GES/GlobalEvent_Prologue_WakeUpCinematicEnds.GlobalEvent_Prologue_WakeUpCinematicEnds_C` | prologue wake‑up finished |
| `/Game/Sparta/Core/GES/GlobalEvent_Prologue_TarGolemCinematicStart.GlobalEvent_Prologue_TarGolemCinematicStart_C` | |
| `/Game/Sparta/Core/GES/GlobalEvent_Midpoint_CinematicStart.GlobalEvent_Midpoint_CinematicStart_C` (parent `GlobalEvent_LargeNotify_C`) | |
| `/Game/Sparta/Core/GES/GlobalEvent_FC_Monolith_CinematicStart…`, `…_FC_Offspring_CinematicStart…`, `…_FC_ParasiteGolem_CinematicStart…` (parent `GlobalEvent_LargeNotify_C`) | |

The music/ambience managers already listen to these:
`/Game/Sparta/Core/Audio/BP_MusicManager.BP_MusicManager_C:OnInGameCinematicStart` /
`:OnInGameCinematicEnd`, `/Game/Sparta/Core/Audio/BP_AmbienceManager.BP_AmbienceManager_C:OnOpeningCinematicStart` /
`:OnOpeningCinematicEnd` — a working example of the subscribe pattern for a mod.

Evidence for the `BroadcastCinematicStart` / `BroadcastCinematicEnd` → event mapping:

```
[000001F61737C280] ObjectProperty /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C:BroadcastCinematicStart:CallFunc_SpawnObject_ReturnValue [o: 0] [n: 61EA35] [c: 00007FF721DE9420] [owr: 000001F4EB53E200] [pc: 000001F4EB562DD0]
[000001F61737C300] ObjectProperty /Game/Sparta/Core/World/FogArea/BP_CinematicPlayer.BP_CinematicPlayer_C:BroadcastCinematicEnd:CallFunc_SpawnObject_ReturnValue [o: 0] [n: 61EA35] [c: 00007FF721DE9420] [owr: 000001F4EB53E300] [pc: 000001F4EB561C00]
```

Evidence:

```
[000001F4D4597510] BlueprintGeneratedClass /Game/Sparta/Core/GES/GlobalEvent_OnMenuOpen.GlobalEvent_OnMenuOpen_C [n: 1A705F] [c: 00007FF46EEC3D38] [or: 000001F4D015D260] [sps: 00007FF46EFCC338]
```

## 5.6 LevelSequence / player classes referenced by name in the dump

```
[00007FF46EECBD38] Class /Script/LevelSequence.LevelSequence [n: 11C7C] [c: 00007FF46EE55138] [or: 00007FF46F17B5F8] [sps: 00007FF46EE89738]
[00007FF46EECC338] Class /Script/LevelSequence.LevelSequenceActor [n: 2C416] [c: 00007FF46EE55138] [or: 00007FF46F17B5F8] [sps: 00007FF46EE52028]
[00007FF46EECCF38] Class /Script/LevelSequence.LevelSequencePlayer [n: 2C4FF] [c: 00007FF46EE55138] [or: 00007FF46F17B5F8] [sps: 00007FF46EE89938]
```

`BP_CinematicPlayer_C` uses `CutsceneActor: LevelSequenceActor` + `LevelSequenceToPlay: LevelSequence`,
and one of its functions carries a local `SequencePlayer: ObjectProperty of /Script/LevelSequence.LevelSequencePlayer`
and `CinematicDuration: FloatProperty` (from `show`: `PRP SequencePlayer : ObjectProperty of
/Script/LevelSequence.LevelSequencePlayer [o:0x8]`, inside `EndCutsceneByTimer`'s frame).

---

# 6. Credits, ending, game completion

## 6.1 `BPFL_UI_C:PlayCredits` — the one entry point

| Parameter | Type |
|---|---|
| `CreditsSong` | `ObjectProperty of /Script/Engine.SoundBase` |
| `bTotalFadeOut` | `BoolProperty` |
| `bPlayMenuMusic` | `BoolProperty` |
| **`bShouldPauseGame`** | `BoolProperty` |
| **`bShouldUnpauseGame`** | `BoolProperty` |
| `bIsInMenu` | `BoolProperty` |
| `bIsMainEnding` | `BoolProperty` |
| `OnCompleted` | `DelegateProperty` (signature = `WBP_Credits_C:OnCreditsCompleted__DelegateSignature`) |
| `__WorldContext` | `ObjectProperty of /Script/CoreUObject.Object` |
| `OutWidget` | `ObjectProperty of /Game/Sparta/UI/Menu/Credits/WBP_Credits.WBP_Credits_C` |

Evidence:

```
[000001F60654E800] Function /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:PlayCredits [n: 63B417] [c: 00007FF46EE55538] [or: 000001F606B65890] [f: 00007FF7183D9710]
[000001F622E51100] BoolProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:PlayCredits:bShouldPauseGame [o: A] [n: 628294] [c: 00007FF721DE8D80] [owr: 000001F60654E800]
[000001F622E53180] BoolProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:PlayCredits:bShouldUnpauseGame [o: B] [n: 63BCEC] [c: 00007FF721DE8D80] [owr: 000001F60654E800]
[000001F622E53000] DelegateProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:PlayCredits:OnCompleted [o: 10] [n: 570F5B] [c: 00007FF721DE8E70] [owr: 000001F60654E800] [df: 000001F615F65000]
```

## 6.2 `WBP_Credits_C`

`/Game/Sparta/UI/Menu/Credits/WBP_Credits.WBP_Credits_C`, parent
`/Game/Sparta/UI/Menu/WBP_Menu_Game_Tab.WBP_Menu_Game_Tab_C`.

| Member | Type | Offset |
|---|---|---|
| `OnCreditsCompleted` | `MulticastInlineDelegateProperty` | `0x860` |

Functions: `Construct`, `PreConstruct`, `Tick`, `OnMenuOpen(TabIndex, SubTabIndex)`, `OnMenuClose`,
`PopulateCredits`, `HandleCreditsScroll`, `HandleCursorVisibility`, `CreditsStartupLogic`,
`GetTargetOffset`, `InitializeSkipButton`, `HandleMusic`, `CloseCredits`, `AutoScrollEvent`,
`OnCreditsCompleted__DelegateSignature`,
`BndEvt__WBP_Credits_WBP_SkipInput_K2Node_ComponentBoundEvent_1_OnSkipCompleted__DelegateSignature`,
`BndEvt__WBP_Credits_WBP_SkipInput_PauseInsensitive_1_K2Node_ComponentBoundEvent_2_OnSkipCompleted__DelegateSignature`.

Note the second skip‑input widget is `WBP_SkipInput_PauseInsensitive_C`
(`/Game/Sparta/UI/Core/WBP_SkipInput_PauseInsensitive.WBP_SkipInput_PauseInsensitive_C`,
parent `/Game/Sparta/UI/Core/WBP_SkipInput.WBP_SkipInput_C`) — i.e. skipping the credits works
even while the game is paused.

Evidence:

```
[000001F615AEFC10] WidgetBlueprintGeneratedClass /Game/Sparta/UI/Menu/Credits/WBP_Credits.WBP_Credits_C [n: 185E46] [c: 00007FF46EEC3B38] [or: 000001F4C7055510] [sps: 000001F4CDF41BA0]
[000001F4C05C3E80] MulticastInlineDelegateProperty /Game/Sparta/UI/Menu/Credits/WBP_Credits.WBP_Credits_C:OnCreditsCompleted [o: 860] [n: 63BACD] [c: 00007FF721DE9010] [owr: 000001F615AEFC10] [df: 000001F615F65000]
[000001F615F65000] Function /Game/Sparta/UI/Menu/Credits/WBP_Credits.WBP_Credits_C:OnCreditsCompleted__DelegateSignature [n: 645EBE] [c: 00007FF46EE55538] [or: 000001F615AEFC10] [f: 00007FF7183D9710]
[000001F615F67C00] Function /Game/Sparta/UI/Menu/Credits/WBP_Credits.WBP_Credits_C:CloseCredits [n: 645562] [c: 00007FF46EE55538] [or: 000001F615AEFC10] [f: 00007FF7183D9710]
```

## 6.3 Credits music states

| Class | Parent | Members |
|---|---|---|
| `/Script/Sparta.SpartaMusicState_Credits` | `/Script/Sparta.SpartaMusicState` | `m_CreditsSwitchGroup: ObjectProperty of /Script/WaveWeaver.AudioSwitchGroup` `0x38`; `m_CreditsSwitch: StructProperty of /Script/WaveWeaver.AudioSwitchState` `0x40`; `m_ExitBehavior: EnumProperty of /Script/Sparta.ESpartaMusicCreditsExit` `0x68` |
| `/Script/Sparta.SpartaMusicStateContext_Credits` | `/Script/Sparta.SpartaMusicStateContext` | `m_ExitBehavior` `0x28`; `m_CreditsSwitchGroup` `0x30`; `m_CreditsSwitch` `0x38` |

Blueprint instances: `/Game/Sparta/Audio/Music/SpartaMusicStateContext_MainCredits.SpartaMusicStateContext_MainCredits_C`
and `/Game/Sparta/Audio/Music/SpartaMusicStateContext_BagheadCredits.SpartaMusicStateContext_BagheadCredits_C`,
both parented to `/Script/Sparta.SpartaMusicStateContext_Credits`; plus
`/Game/Sparta/Core/Audio/Music/States/BP_MusicState_Cinematic.BP_MusicState_Cinematic_C`
(parent `/Script/Sparta.SpartaMusicState_Cinematic`).

Evidence:

```
[00007FF46F054138] Class /Script/Sparta.SpartaMusicStateContext_Credits [n: 49EB4] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F052D38]
[00007FF46F054338] Class /Script/Sparta.SpartaMusicState_Credits [n: 49EC5] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46F052F38]
[000001F4ACDBC080] EnumProperty /Script/Sparta.SpartaMusicState_Credits:m_ExitBehavior [o: 68] [n: 588A6F] [c: 00007FF721DE7380] [owr: 00007FF46F054338] [em: 00007FF46F3D97B8]
```

## 6.4 Game completion

| Owner | Function | Parameters |
|---|---|---|
| `BPFL_UI_C` | `TriggerGameCompletionScreen` | `ZOrder: IntProperty`, `__WorldContext: ObjectProperty of /Script/CoreUObject.Object`, `OutWidget: ObjectProperty of /Game/Sparta/UI/Menu/Credits/WBP_GameCompletion.WBP_GameCompletion_C` |
| `BPFL_UI_C` | `HandlePostCreditState` | `__WorldContext: ObjectProperty of /Script/CoreUObject.Object` |
| `/Script/Sparta.SpartaCompletionLibrary` | `SnapshotRunCompletion` | `WorldContextObject: ObjectProperty of /Script/CoreUObject.Object`, `ReturnValue: StructProperty of /Script/Sparta.SpartaCompletionResult` |
| `/Script/Sparta.SpartaCompletionLibrary` | `GetRunCompletionResult` | `WorldContextObject`, `ReturnValue: StructProperty of /Script/Sparta.SpartaCompletionResult` |
| `/Script/Sparta.SpartaCompletionLibrary` | `GetRunTimeText` | `WorldContextObject`, `ReturnValue: TextProperty` |
| `/Script/Sparta.SpartaCompletionLibrary` | `GetRunTimeSeconds` | `WorldContextObject`, `ReturnValue: FloatProperty` |
| `/Script/Sparta.SpartaCompletionLibrary` | `GetRunCompletionPercent` | `WorldContextObject`, `ReturnValue: FloatProperty` |
| `/Script/Sparta.SpartaCompletionLibrary` | `GetSurvivalOddsPercent` | `WorldContextObject`, `ReturnValue: FloatProperty` |

`WBP_GameCompletion_C` functions: `Construct`, `CreateDisplayMenu`, `BuildCompletionTime`,
`BuildSurvivalRate`, `TriggerEffect`, `TriggerCompletionTimeAudio`, `TriggerSuriveRateAudio`,
`OnFadeCompleted`, `OnMenuOpen(TabIndex, SubTabIndex)`, `OnMenuClose`,
`SequenceEvent__ENTRYPOINTWBP_GameCompletion(+_0,_1)`,
`BndEvt__WBP_GameCompletion_WBP_IL_GameCompletion_Continue_K2Node_ComponentBoundEvent_0_OnInputTriggered__DelegateSignature`.
It calls `SnapshotRunCompletion`, `FadeInScreenTransition`, `FadeOutScreenTransition`,
`TriggerFadeToBlack` and `ActivateDisplayMenu`.

Evidence:

```
[000001F60654C700] Function /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:TriggerGameCompletionScreen [n: 63BCD1] [c: 00007FF46EE55538] [or: 000001F606B65890] [f: 00007FF7183D9710]
[000001F622E60E10] IntProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:TriggerGameCompletionScreen:ZOrder [o: 0] [n: 519BE1] [c: 00007FF721DE91F0] [owr: 000001F60654C700]
[000001F622E57480] ObjectProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:TriggerGameCompletionScreen:OutWidget [o: 10] [n: 63A879] [c: 00007FF721DE9420] [owr: 000001F60654C700] [pc: 000001F606FAF820]
[000001F60654FA00] Function /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:HandlePostCreditState [n: 63BF7E] [c: 00007FF46EE55538] [or: 000001F606B65890] [f: 00007FF7183D9710]
[000001F622E3EC80] ObjectProperty /Game/Sparta/UI/Core/BPFL_UI.BPFL_UI_C:HandlePostCreditState:__WorldContext [o: 0] [n: 5F4220] [c: 00007FF721DE9420] [owr: 000001F60654FA00] [pc: 00007FF46EE50070]
```

## 6.5 The credits/completion call chain

`BP_CinematicBehavior_StartCredits_C` is a level Behavior (`/Script/Sparta.ExecutableBehavior`)
whose `Execute()` starts the credits and whose `OnCreditsComplete()` handles the end:

| Owner | Function | Parameters |
|---|---|---|
| `/Game/Sparta/Core/World/Levels/Behavior/BP_CinematicBehavior_StartCredits.BP_CinematicBehavior_StartCredits_C` | `Execute` | *(none — inherited Behavior entry point)* |
| same | `OnCreditsComplete` | *(none)* |

`/Game/Sparta/Dialogue/Blueprints/Interactables/BP_GloomSiphon.BP_GloomSiphon_C` has
`OnCreditsEnd()` (calls `TriggerGameCompletionScreen`, result read into
`OnCreditsEnd:CallFunc_TriggerGameCompletionScreen_OutWidget`) and `OnGameCompletionClosed()`.
`/Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C` has `CreditsEnd()` and
`OnOutroAnimaticEnd()` (result read into `OnOutroAnimaticEnd:CallFunc_PlayCredits_OutWidget`).

Evidence:

```
[000001F8C6A9A360] BlueprintGeneratedClass /Game/Sparta/Core/World/Levels/Behavior/BP_CinematicBehavior_StartCredits.BP_CinematicBehavior_StartCredits_C [n: 39649D] [c: 00007FF46EEC3D38] [or: 000001F8C3848800] [sps: 00007FF46F033138]
```

## 6.6 "ReturnFromEndStranding"

**UNKNOWN.** No object path contains `ReturnFromEndStranding`.
Tried: `parse_dump.py find raw\index.sqlite3 "ReturnFromEndStranding|EndStranding|GameCompletion|CreditsFinished"`.
What *does* exist around the End Stranding area:
`/Game/Sparta/Core/SaveObjects/BPSO_FC_FinalBossManager.BPSO_FC_FinalBossManager_C:EndStrandingCompleted` (`BoolProperty`),
`/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/BP_LevelScript_FinalBossManager.BP_LevelScript_FinalBossManager_C`
with `EndStrandingEnd`, `TeleportToEndStranding`, `EndStrandingLinkedDungeon` (`SoftObjectProperty`),
`EndStrandingTeleportLocation` (`SoftObjectProperty`),
`/Game/Sparta/Core/World/Levels/Events/Event_EndStranding.Event_EndStranding` (`SpartaEventDataAsset`),
`/Game/Sparta/Maps/LevelDesignTools/BlueprintTools/TestZones/DungeonZones/FinalCavern/Zone_Dungeon_Event_EndStranding.Zone_Dungeon_Event_EndStranding` (`SpartaZoneData`),
and the map `/Game/Sparta/Maps/Playgrounds/LI_End_FinalArea.LI_End_FinalArea`.

---

# 7. Every Sparta function that changes game phase / session state

Search method for this section: for each class below, `parse_dump.py show raw\index.sqlite3 <class>`
and filter function names containing `Save|Load|Travel|ChangeLevel|SetState|GamePhase|Session|Reset|Init|Shutdown`.

## 7.1 `SpartaGameInstance` / `BP_SpartaGameInstance_C`

| Function | Parameters | Phase effect |
|---|---|---|
| `PostInit` | — | GameInstance init |
| `BP_PostEngineInit` | — | Blueprint engine‑init hook |
| `BeginLoadingScreen` | `MapName: StrProperty` | **load starts** |
| `EndLoadingScreen` | `InLoadedWorld: ObjectProperty of /Script/Engine.World` | **load ends** |
| `SetPlayStreamingPauseMovie` | `bPlayMovie: BoolProperty` | streaming held |
| `ReceiveInit` (BP) | — | GameInstance init (BP) |
| `ReceiveShutdown` (BP) | — | **GameInstance shutdown** |
| `SetLoadingScreenIndex` (BP) | — | picks loading art |
| `SetSuccessfulRun` (BP) | — | run outcome |
| `SetInBossFight` (BP) | — | boss‑fight phase |
| `SetForceSiphon` (BP) | — | |
| `LoadGameSettings` (BP) | — | settings load |
| `SaveGameSettings` (BP) | — | settings save |
| `TelemetryStartRecording` / `TelemetryStopRecording` (BP) | — | |

## 7.2 `SpartaSaveSubSystem` / `BPFL_SaveLoad_C`

See §1.9 and §1.10 — `SaveToDisk`, `SaveSession`, `ResetSession`, `AutoSave`,
`SetPreventFurtherSaves`, `ChangeActiveSaveProfileSlot`, `RecoverSave`,
`ResetSaveObjectByGuid`, `ResetSaveObjectByClass`, plus the delegates `OnSaveStarted` /
`OnSaveFinished` / `OnSaveGameLoaded` / `PostSessionReset` / `OnSaveSystemInitialized`.
Blueprint layer: `BPFL_SaveLoad_C:SaveGame(Immediately, Save)`, `:SaveSession`,
`:ResetSession`, `:ShouldResumeSession`.

## 7.3 `SpartaGameMode` / `BP_SpartaGameMode_C`

| Function | Parameters |
|---|---|
| `ShouldResumeRun` | `ReturnValue: BoolProperty` |
| `GetStartTransformFromSessionSave` | `OutTransform: Transform`, `ReturnValue: BoolProperty` |
| `bResumingSession` | property `0x358` |
| `BP_SpartaGameMode_C:ChoosePlayerStart` / `:FindCustomPlayerStart` / `:SelectCustomPlayerStart` / `:GetAllPlayerStart` / `:GetCachedSpartaPlayerStarts` / `:GetCurrentCustomPlayerStart` / `:GetDefaultPlayerStartName` / `:BroadcastPlayerStartFound` | no declared params in the dump |

## 7.4 `SpartaWorldGenSubsystem`

`BeginWorldReset()`, `AddTagToCurrentWorldState(Tag)`, `SetDifficulty(BiomeTag, NewDifficulty)`,
`IncreaseDifficulty(BiomeTag, IncreaseBy → int)`, `GetDifficulty(BiomeTag → int)`,
`OnDataLayerRuntimeStateChanged(DataLayer, State)`.
There is **no** completion delegate on this class.

## 7.5 `SpartaUIManagerSubsystem` / `BPC_UserInterfaceHandler_C` / `BPFL_UI_C`

Covered in §2 — `ActivateDisplayMenu`, `DeactivateDisplayMenu`, `RemoveDisplayMenu`,
`EnableUIInput`, `DisableUIInput`, `WidgetStartupLogic`, `HideHud`, `ShowHUD`,
`BlockUIInput_All`, `UnblockUIInput_All`, `UpdateGameMenuBlockState`,
`UpdateCharacterMenuBlockState`, `ResetActivePrimaryMenu`, `UpdateActivePrimaryMenu`.

## 7.6 `SpartaPlayerController`

`SetShouldPerformFullTickWhenPaused(bEnabled)`, `GetShouldPerformFullTickWhenPaused()`,
`SetShell(Shell)`, `GetShell()`, `GetDarkform()`,

Note this class has **no** `Travel`, `Respawn`, `Possess` or `UnPossess` override in the dump.

## 7.7 Fast travel

Fast travel is a **gameplay ability**, not a level change:

| Owner | Function | Parameters |
|---|---|---|
| `/Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_FastTravel.GA_Traversal_FastTravel_C` | `OnPreFastTravelNotify` | *(none)* |
| same | `OnFastTravelNotify` | *(none)* |
| same | `OnFadeToBlack` | *(none)* |
| same | `AddCameraState` / `RemoveCameraState` | *(none)* |
| `/Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_FastTravel_HUB.GA_Traversal_FastTravel_HUB_C` | `OnFastTravelNotify` | *(none)* |
| `/Game/Sparta/Core/Player/Ability/Traversal/GA_Traversal_FastTravel_HUB_LoseGloom.GA_Traversal_FastTravel_HUB_LoseGloom_C` | `OnPreFastTravelNotify` | *(none)* |
| `/Game/Sparta/UI/Menu/LandingArea/WBP_MGT_FastTravel.WBP_MGT_FastTravel_C` | `OnFastTravelEvent__DelegateSignature`, `GatherFastTravelOptions`, `GetAreaTravelPoints` | — |
| `/Game/Sparta/Core/World/Launch/BP_FastTravelPoint.BP_FastTravelPoint_C` | `CanFastTravel`, `EligibleForFastTravel` | — |
| `/Game/Sparta/Core/World/Launch/BP_LandingAreaBase.BP_LandingAreaBase_C` | `CanFastTravel`, `EligibleForFastTravel`, `HasFastTravelMenuOption`, `SetPlayerStart`, `InitializePlayerStart`, `CachePlayerStartTransform`, `GetDesiredPlayerStartLocation`, `GetDesiredPlayerStartRotation`, `OnMenuCloseStarted`, `OnMenuCloseCompleted` | — |

Level/dungeon streaming is done by:
`/Game/Sparta/Core/World/Levels/BP_DungeonEventSelector.BP_DungeonEventSelector_C:LoadLevel`,
`/Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector.BP_LevelInstanceSelector_C:LoadLevel`,
`/Game/Sparta/Core/World/Levels/BP_LevelInstanceSelector_RestSite.BP_LevelInstanceSelector_RestSite_C:LoadLevel`.

Evidence:

```
[000001F4EB53B700] Function /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:StartNewGame [n: 63B43E] [c: 00007FF46EE55538] [or: 000001F4C8B9A180] [f: 00007FF7183D9710]
[000001F4EB53C100] Function /Game/Sparta/UI/MainMenu/WBP_MainMenu.WBP_MainMenu_C:LoadMap [n: 63B390] [c: 00007FF46EE55538] [or: 000001F4C8B9A180] [f: 00007FF7183D9710]
```

## 7.8 `GlobalEventSubsystem` as the universal phase bus

Already listed in §2.5. The subsystem is an `EngineSubsystem` with a single property
`SubscribedEvents: SetProperty` at `0x80`, and `GESFunctions` is the sole public API.
Any mod handover can be driven from `BroadcastEvent` / `SubscribeToEvent` on a subclass of
`GlobalEvent`.

Evidence:

```
[00007FF46EFCC138] Class /Script/GlobalEventSubsystem.GESFunctions [n: 3F9B9] [c: 00007FF46EE55138] [or: 00007FF46F17BE18] [sps: 00007FF46EE70138]
[00007FF46F679778] Function /Script/GlobalEventSubsystem.GESFunctions:SubscribeToEvent [n: 3F9EE] [c: 00007FF46EE55538] [or: 00007FF46EFCC138] [f: 00007FF71BB275B0]
[000001F4ABBE9500] ClassProperty /Script/GlobalEventSubsystem.GESFunctions:SubscribeToEvent:EventClass [o: 0] [n: 58107B] [c: 00007FF721DE8E20] [owr: 00007FF46F679778] [mc: 00007FF46EFCC338]
[000001F4ABBE9580] StructProperty /Script/GlobalEventSubsystem.GESFunctions:SubscribeToEvent:Filter [o: 8] [n: 168E4] [c: 00007FF721DF9890] [owr: 00007FF46F679778] [ss: 00007FF46F3890A8]
[000001F4ABBE9600] DelegateProperty /Script/GlobalEventSubsystem.GESFunctions:SubscribeToEvent:Event [o: 10] [n: 4CC088] [c: 00007FF721DE8E70] [owr: 00007FF46F679778] [df: 00007FF46F679238]
[000001F4ABBE9680] BoolProperty /Script/GlobalEventSubsystem.GESFunctions:SubscribeToEvent:ReturnValue [o: 20] [n: 4CF54F] [c: 00007FF721DE8D80] [owr: 00007FF46F679778]
[00007FF46EFCC538] Class /Script/GlobalEventSubsystem.GlobalEventSubsystem [n: 11E46] [c: 00007FF46EE55138] [or: 00007FF46F17BE18] [sps: 00007FF46EE77938]
```

---

# 8. Moments the mod MUST pause sync / hand over control

Every row cites the fact established above. "Observable" = something a UE4SS Lua mod can hook or
subscribe to; "Blocking" = the game is paused / the player has no authority.

| # | Moment | Where the mod must act | Exact function / delegate a mod can observe | Blocking? | Evidence |
|---|---|---|---|---|---|
| 1 | Startup videos | before any world | `WBP_StartupVideos_C:OnComplete` (M.I.D. `0x370`), `:OnPreComplete` (M.I.D. `0x388`), `:SkipVideos` | no world yet | §4.6 |
| 2 | Main menu opens | stop/reset sync | `BPI_Menu_Generic_C:OnMenuOpen(TabIndex, SubTabIndex)`; `GlobalEvent_OnMenuOpen_C` (`UserWidget` @`0x28`) | no world | §2.4, §2.5 |
| 3 | Main menu closes | resume/start sync | `BPI_Menu_Generic_C:OnMenuClose()`; `GlobalEvent_OnMenuClose_C`; `GlobalEvent_OnMenuClose_Cancelled_C` | — | §2.5 |
| 4 | Continue pressed | **re-root** every synced object | `WBP_MainMenu_C:ContinueGame()` | loading | §1.7 |
| 5 | New game started | **hard reset + full resync** | `WBP_MainMenu_C:StartNewGame(ActiveSlot, SkipPrologue)`; `WBP_MGT_SaveSlots_C:OnSlotConfirmed(MenuType, Index, SkipPrologue)` (`0x410`) | loading | §1.7 |
| 6 | Load game | re-root from the save | `WBP_MainMenu_C:LoadGame(ActiveSlot)` | loading | §1.7 |
| 7 | Save slot chosen | `WBP_MainMenu_C:OnSaveSlotConfirmed(MenuType, Index, SkipPrologue)` | | | §1.7 |
| 8 | Map load starts | **suspend all entity sync** | `BPFL_UI_C:TriggerLoadingScreen(Direction, ZOrder, OnCompleted, __WorldContext)`; `SpartaGameInstance:BeginLoadingScreen(MapName)` | yes | §4.2, §1.3 |
| 9 | Loading screen created | UI only | `WBP_LoadingScreen_C:BroadcastCreatedEvent` → `BP_GlobalEvent_LoadingScreenCreated_C` | yes | §4.3 |
| 10 | Map load finished | **re-baseline world state** | `WBP_LoadingScreen_C:BroadcastCompletedEvent` → `BP_GlobalEvent_LoadingScreenCompleted_C`; `SpartaGameInstance:EndLoadingScreen(InLoadedWorld)`; `WBP_LoadingScreenBase_C:OnCompleted__DelegateSignature` | yes | §4.3, §1.3 |
| 11 | Fade to black / transition | hold input | `BPFL_UI_C:TriggerFadeToBlack(OverrideWidget, Color, Direction, duration, Hold, HoldDuration, ZOrder, OnCompleted, __WorldContext)`; `FadeInScreenTransition(...)`; `FadeOutScreenTransition(OnCompleted, ClearImmediately, duration, __WorldContext)` | partially | §4.2 |
| 12 | Shader compile screen | block join | `WBP_ShaderCompiling_C:OnCompilationComplete` (`0x398`), `:TriggerCompletion` | yes | §4.4 |
| 13 | Streaming screen | block join | `StreamingScreenWidget:OnStreamingFinished()`; `WBP_StreamingScreen_C:OnStreamingFinished` → `BP_GlobalEvent_StreamingScreenCompleted_C` | yes | §4.5 |
| 14 | Pause / in‑game menu opens | **pause sync** | `BPFL_UI_C:EnableUIInput(..., PauseGame, ...)`; `WidgetStartupLogic(bIsOpening, bIsInMenu, bShouldHideHUD, SoundToPlay, WidgetReference, InputListner, __WorldContext)`; `BPC_UserInterfaceHandler_C:ActivateDisplayMenu(Tag, Success)` | yes | §2.2, §2.6 |
| 15 | Unpause / menu closes | resume sync | `BPFL_UI_C:DisableUIInput(..., UnpauseGame, ...)`; `BPC_UserInterfaceHandler_C:DeactivateDisplayMenu()`; `RemoveDisplayMenu()`; `WBP_Menu_Game_C:OnMenuClose()` | — | §2.2, §2.6 |
| 16 | Modal prompt with `bPauseGame` | **pause sync** | `BPFL_UI_C:CreateConfirmationPrompt(…, bPauseGame, …, InputEvent, ClosedEvent, …)`; `CreateConfirmationPromptMultiOptions`; `CreateConfirmationPromptWithRequirement`; `CreateShellStoryPrompt`; `CreateReadText(…, bPauseGame, …, CloseStart, CloseComplete, …)`; `CreateDismissableMessage(…, bPauseGame, …, DimissEvent, ClosedEvent, …)`; `CreateObjectiveHint(…, bPauseGame, …, WidgetClosedEvent, …)`; `CreateInspectWidget(…, bPauseGame, …, CloseStarted, CloseComplete, …)` | yes | §2.7 |
| 17 | Prompt confirmed / closed | resume | `WBP_ConfirmationPromptBase_C:OnOptionConfirmed` (`0x3b8`, `OptionIndex: int`), `:OnPromptClosed` (`0x3e0`, `OptionIndex: int`); `WBP_ConfirmationPrompt_MultiOptions_C:OnMultiOptionConfirmed` (`0x6f0`, `OptionID: Name, OptionIndex: int`), `:OnMultiOptionClosed` (`0x700`, same); `WBP_ConfirmationPrompt_ReadText_C:OnCloseStarted` (`0x6a8`, no params); `WBP_ConfirmationPrompt_Dismissable_C:OnDismissed` (`0x698`, no params) | — | §2.7 |
| 18 | Player death starts | **stop writing player state** | `SpartaHealthComponent:StartDeath()`; `SpartaHealthComponent:GetDeathState()` → `ESpartaDeathState` (`NotDead`/`DeathStarted`/`DeathFinished`); `SpartaHealthComponent:OnRep_DeathState(OldDeathState)` | yes | §3.1 |
| 19 | Death screen opens | hand over control | `GA_Death_Player_C:HandlePlayerDeath(GrantDeathState)` → `:CreateDeathScreen`; `WBP_DeathScreen_C:Construct`; `GA_Death_Player_C:WBP_DeathScreen` (`0x5f0`) | yes | §3.2, §3.3 |
| 20 | Respawn point chosen | — | `WBP_DeathScreen_C:OnDeathSelection`; `WBP_MGT_DeathSelection_C:OnMenuOpen(TabIndex, SubTabIndex)`; `BPFL_SaveLoad_C:GetRespawnPlayerStart(Success, ReturnValue)` | yes | §3.3, §3.4 |
| 21 | World reset | **drop all world sync** | `WBP_DeathScreen_C:ResetWorld` (obtains `SpartaWorldGenSubsystem`); the only reset‑shaped function on it is `SpartaWorldGenSubsystem:BeginWorldReset()` — GUESS that this is the call | yes | §3.3, §3.5 |
| 22 | Respawn loading screen | suspend sync | `WBP_DeathScreen_C:CreateLoadingScreen` → `:OnLoadingScreenCompleted` | yes | §3.3 |
| 23 | Death ability ends | resume | `GA_Death_Player_C:EndDeath`; `:K2_OnEndAbility(bWasCancelled)` | — | §3.2 |
| 24 | **Cutscene starts** | **pause sync** | `BP_CinematicPlayer_C:PlayCutscene` / `PrepareCutscene` / `StartCinematicViaEvent(EventData)` / `TriggerCutsceneViaInteraction` / `BroadcastCinematicStart` → `GlobalEvent_Cinematic_Start_C` | yes (player movement disabled) | §5.2, §5.5 |
| 25 | **Cutscene ends** | resume sync | `BP_CinematicPlayer_C:EndCutscene` / `EndCutsceneByTimer` / `EndCutsceneBySkip` / `OnSkipEvent` / `ForceStopCinematic` / `BroadcastCinematicEnd` → `GlobalEvent_Cinematic_End_C`; multicast `CinematicEndReached` at `0x2e8` | — | §5.2, §5.5 |
| 26 | Sequence‑level start/end | granular cutscene marks | `…_DirectorBP_C:SequenceEvent_Start` / `SequenceEvent_End` (also `CineBegin`/`CineEnd`, `BeginCinematic`/`EndCinematic`, `Begin`/`End`) | yes | §5.4 |
| 27 | Cinematic audio | audio handover | `SpartaCinematicAudioHandler:OnCinematicStart()` / `:OnCinematicEnd(bForceStop)` / `:IsPlayingCinematic()` / `:PlayCinematic(in_Data)` / `:StopCinematic(in_FadeOutDuration, in_FaderCurve)` | — | §5.1 |
| 28 | **Credits start** | **pause sync, hide HUD** | `BPFL_UI_C:PlayCredits(CreditsSong, bTotalFadeOut, bPlayMenuMusic, bShouldPauseGame, bShouldUnpauseGame, bIsInMenu, bIsMainEnding, OnCompleted, __WorldContext)`; `BP_CinematicBehavior_StartCredits_C:Execute` | optional (`bShouldPauseGame`) | §6.1, §6.5 |
| 29 | **Credits end** | resume | `WBP_Credits_C:OnCreditsCompleted` (`0x860`) / `:CloseCredits` / `:OnCreditsCompleted__DelegateSignature`; `BP_CinematicBehavior_StartCredits_C:OnCreditsComplete`; `GlobalEvent_CreditsFinished_C` | — | §6.2, §6.5 |
| 30 | Game completion screen | end of run | `BPFL_UI_C:TriggerGameCompletionScreen(ZOrder, __WorldContext, OutWidget)`; `WBP_GameCompletion_C:CreateDisplayMenu` / `:OnMenuClose`; `SpartaCompletionLibrary:SnapshotRunCompletion(WorldContextObject → SpartaCompletionResult)` | yes | §6.4 |
| 31 | Post‑credits state | resume/finish | `BPFL_UI_C:HandlePostCreditState(__WorldContext)` | — | §6.4 |
| 32 | Session save begins/ends | **do not mutate synced state mid‑save** | `SpartaSaveSubSystem:OnSaveStarted` (`0x60`), `:OnSaveFinished` (`0x70`), `:OnSaveGameLoaded` (`0x30`), `:PostSessionReset` (`0x40`), `:OnSaveSystemInitialized` (`0x50`) | — | §1.9 |
| 33 | Explicit save | — | `SpartaSaveSubSystem:SaveToDisk(SaveEvents, bSaveImmediately, bSaveAsync)`; `BPFL_SaveLoad_C:SaveGame(Immediately, Save)`; `SpartaSaveSubSystem:SetPreventFurtherSaves(bPreventSaving)` | — | §1.9, §1.10 |
| 34 | Session reset | **re‑root** | `SpartaSaveSubSystem:ResetSession()`, `:ResetAllSaveData()`; `BPFL_SaveLoad_C:ResetSession` | — | §1.9, §1.10 |
| 35 | Save profile switch | re‑root | `SpartaSaveSubSystem:ChangeActiveSaveProfileSlot(SlotIndex)`, `:DeleteSaveProfile(ProfileSlot)`, `:RecoverSave(RecoverySlotName)` | — | §1.9 |
| 36 | Resume‑vs‑new decision | inform sync direction | `SpartaGameMode:ShouldResumeRun()`; `SpartaGameMode:GetStartTransformFromSessionSave(OutTransform)`; `SpartaGameMode:bResumingSession` (`0x358`); `BPFL_SaveLoad_C:ShouldResumeSession` | — | §1.4, §1.10 |
| 37 | Came‑from‑menu flag | gate sync start | `BP_SpartaGameInstance_C:WasGameStartedFromMainMenu` (`0x3a1`) | — | §1.8 |
| 38 | Fast travel | suspend entity sync | `GA_Traversal_FastTravel_C:OnPreFastTravelNotify` → `:OnFadeToBlack` → `:OnFastTravelNotify` | briefly yes | §7.7 |
| 39 | Dungeon level streaming | re‑baseline | `BP_DungeonEventSelector_C:LoadLevel`, `BP_LevelInstanceSelector_C:LoadLevel`, `BP_LevelInstanceSelector_RestSite_C:LoadLevel` | — | §7.7 |
| 40 | GameInstance shutdown | flush / leave | `BP_SpartaGameInstance_C:ReceiveShutdown` | — | §7.1 |

---

# 9. Open questions / UNKNOWNs

| # | UNKNOWN | What was tried |
|---|---|---|
| 1 | *(resolved — see §1.9)* the save delegates' signatures are `SpartaSaveLoadedSignature__DelegateSignature(Context: SpartaSaveContext)` and `SpartaSaveInitializedSignature__DelegateSignature()` | `parse_dump.py raw raw\index.sqlite3 "Sparta.SpartaSave(Loaded\|Initialized)Signature__DelegateSignature(:.*)?$"` |
| 2 | Values of `/Game/Sparta/UI/Enums/Enum_MenuCloseEvent.Enum_MenuCloseEvent` | `parse_dump.py raw raw\index.sqlite3 "Enum_MenuCloseEvent"` → only `Package` + `UserDefinedEnum`; the dump does not list enum entries. |
| 3 | Which global event `WBP_GamePaused_C` subscribes to in `Construct` | `parse_dump.py find raw\index.sqlite3 "WBP_GamePaused"` → the event class only exists inside the transient `SubscribeToEventCallbackProxy`, not as a widget property. |
| 4 | What spawns `WBP_GamePaused_C` / `WBP_DeathScreen_C`'s owning UIManager layer | `WBP_GamePaused` is referenced by no other dumped object; `WBP_DeathScreen` only by `GA_Death_Player_C:WBP_DeathScreen`. |
| 5 | `SpartaUIManagerSubsystem` layer API (push/pop of UI layers) | The class declares **no** members; the parent `/Script/CommonGame.GameUIManagerSubsystem` was not enumerated in this pass. |
| 6 | Any quit/exit function | `parse_dump.py find` for `Quit\|RequestExit\|ExitGame` — nothing on `/Script/Sparta.*`. Only `BP_UIL_QuitFromMainMenu_C` / `BP_UIL_QuitFromPauseMenu_C` UI logic objects exist. |
| 7 | `ReturnFromEndStranding` | `parse_dump.py find raw\index.sqlite3 "ReturnFromEndStranding\|EndStranding\|…"` → no such object. Related objects listed in §6.6. |
| 8 | `SpartaWorldGenSubsystem:BeginWorldReset` completion | The class has no delegates at all (`show` shows only 5 functions, 0 properties). |
| 9 | Parameter list of `WBP_LoadingScreenBase_C:OnCompleted__DelegateSignature` | It is a `Function` object with **no** parameter rows → the signature takes no arguments. Not really unknown, but worth stating explicitly because `TriggerLoadingScreen` / `FadeInScreenTransition` / `TriggerFadeToBlack` all use it as their `OnCompleted` delegate. |
| 10 | Which class actually calls `UGameplayStatics.SetGamePaused` | The call is inside Blueprint graphs (`EnableUIInput` / `DisableUIInput` bodies); the object dump exposes parameters, not the call targets of an existing bytecode body. GUESS: those two functions call it. |
| 11 | `SequenceEvent_*` names of the 17 directors not listed in §5.4 | `parse_dump.py find … "Cinematic" --types Function` surfaced only six directors' events. Re-run `python scripts\parse_dump.py fun raw\index.sqlite3 "DirectorBP_C"`. |
| 12 | `Weather` duplicated at `0x3c8` and `0x3d0` in `BP_CinematicPlayer_C` | Two properties carry the printed name `Weather` (`ObjectProperty of /Script/Sparta.SpartaWeatherPreset`); which is which cannot be disambiguated from the dump. |

## 9.1 Report artefacts

`research\70-flow-menus.md` — this file.

SQL helpers written for this report (all under `research\scripts\q\`):

| File | Purpose |
|---|---|
| `fm-widgets.sql` | all `WidgetBlueprintGeneratedClass` under `/Game/Sparta/UI/` and `/Game/Sparta/Widgets/` with parents |
| `fm-menuwidgets.sql` | the named flow widgets (WBP_MainMenu, WBP_SaveGameEntry, WBP_ConfirmationPrompt_*, WBP_GameLogo, WBP_StartupVideos, WBP_GamePaused, WBP_DeathScreen, WBP_Credits, WBP_ShaderCompiling, WBP_StreamingScreen, WBP_GameCompletion, WBP_LoadingScreen*) |
| `fm-classfuns.sql` | every `Function` owned by one class (path pattern edited per run) |
| `fm-funcparams.sql` | one class' functions with parameters in dump order |
| `fm-sequences-count.sql` | count of sequence asset types under `/Game/Sparta/` |
| `fm-sequences-list.sql` | all `LevelSequence` / `ActorSequence` / `CameraAnimationSequence` paths |
| `fm-who-references.sql` | reverse lookup: objects whose type‑ref points at a class |
| `fm-dupcheck.sql` | duplicate‑row check for one path |
| `fm-resolve-addr.sql` | resolve raw `[ss:]`/`[pc:]`/`[df:]` addresses to object paths |
