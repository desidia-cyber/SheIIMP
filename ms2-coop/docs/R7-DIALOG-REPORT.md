# Round 7 — DIALOGUE + CINEMATICS (gaps G8 / G10) — agent `r7-dialog`

Workspace `G:\Freespace\shell_coop\wt\r7-dialog`. Installs: host `E:\Games\_MS2-coop-g`,
guest `E:\Games\_MS2-coop-h`, relay port 47130. This file is written as I go.

---

## 0. TL;DR

| | |
|---|---|
| Wire | three NEW top-level keys, appended LAST: `"dl"` (this client is in a dialogue), `"ci"` (this client is in a cinematic), `"ct"` (the dialogue **outcome**: the quest log's global `PlayerChoice` tag). All loss-safe (`seq`, and each section is repeated for 4 s after it ends). |
| Receiver | while the peer is busy: the puppet stands in its **idle** clip (no stale montage, no reference pose), enemies are **not retargeted** onto that body, and damage this world deals to the stand-in body is **not** charged to the peer (`"pd"` suppressed). |
| Not synced, deliberately | the conversation UI, the dialogue assets, the cutscene playback. Reasons in §3. |
| Kill switches | `DIALOG_OFF.txt`, `CINE_OFF.txt` (coop dir, read once at load). |
| New verbs | `dlgdump`, `dlgstart [class]`, `dlgstop`, `dlgchoice <tag>`, `dlgfake on\|off [name]`, `cinefake on\|off [name]`. |
| New verdicts | `DIALOG`, `CINE` in `run-two.ps1` (written by the receiving client, `result_dialog.txt` / `result_cine.txt`). |
| Build | `coop_policy: 112 checks passed` + `Built:` — see §5. |
| Verdicts | **build PASS; the in-game DIALOG/CINE verdicts were NOT reached.** The client dies ~60 s after launch of the pre-existing `steam_api64` heap-corruption crash (two dumps prove it, §6.3), before the harness reaches `phase=sync` + the first `cmd.txt` verb; the shared game lock was also held by other agents for most of the window. §6 states exactly what was and was not seen. |

---

## 1. Root cause (with the proof)

### 1.1 Dialogue (G8) — the mod had no idea a conversation existed

`grep -i "dlg\|dialogue" native\src\*.cpp` before this round returned **no engine call at
all**: two comments in `coop_combat.cpp` (NPC class paths under `/Game/Sparta/Dialogue/NPCs/`)
and two `DT_QuestData1` path constants in `coop_progress.cpp`. Nothing ever read
`UDialogueSubsystem::ActiveDialogues`, and nothing ever touched
`BPC_QuestLogComponent_C::PlayerChoice` (grep `PlayerChoice native/src/*` → 0 hits; the
quest wire in `coop_progress.cpp` covers narrative tags, active/completed quests, stages,
objective counters, turn-in, tracked quest, `NPCRelationships`, `NarrativeEvents` — the
`wq` keys `t|c|a|s|o|u|k|r|e`).

Consequences, all measured-able from the code:

1. **A locked player keeps behaving like a fighting one on the peer.**
   While a conversation runs the game disables the player's input
   (`BPC_DialogueInstigator_C::DisablePlayerInput`, and `FActiveDialogue` even carries a
   per-dialogue `SoundComp` / facial-animation montage), so the local pawn stands still with
   its input gone — and publishes exactly the normal hot fields. On the peer that body is
   still a valid AI target: `coop_combat::AiTargetPeer` keeps dragging enemies onto it
   (the peer was "nearer"), and every hit the peer's world lands on the stand-in body is
   turned into a `"pd"` charge in `coop_combat::PuppetDamage` — i.e. the player who is
   talking to an NPC loses health for a fight he cannot join. This is the task's
   "does not get hit while it is locked", in the dialogue case.
2. **The dialogue OUTCOME that only exists on the component never travelled.**
   The quest log component (`BPC_QuestLogComponent_C`, a `SpartaSaveComponent`) keeps
   `PlayerChoice: FGameplayTag @0x1D0` — the single global story tag ("which side of the
   fork did you pick"). Its save object only persists `NarrativeTags @0xC0`, so `PlayerChoice`
   is invisible to a save-object scan, and it was not on the wire. Two players could sit on
   opposite sides of the same story choice permanently.

### 1.2 Cinematics (G10) — nothing looked at the sequence, so the body could freeze

`grep -i cinematic native/src/*.cpp` before this round: one line, the store-A **watched
field** `BPSO_CinematicDLUpdate_C.CinematicFired` in `coop_world.cpp` — i.e. the mod knew
about the *persisted* flag (which is synced) and nothing about a sequence *playing*.

What the game does (dump-proved, `Sparta.hpp` / `BP_CinematicPlayer.hpp`):
`ABP_CinematicPlayer_C` holds `CutsceneActor: ALevelSequenceActor* @0x2C0`,
`LevelSequenceToPlay: ULevelSequence* @0x2C8`, and drives the whole beat with
`PrepareCutscene → PlayCutscene → EndCutscene`, `HidePlayerDuringCinematic`,
`HidePlayerAndWeapons`, `DisablePlayerMovement`, `LockInteraction`,
`AddCutsceneState` (a GameplayEffect via `StateGE`), and `TeleportPlayer` +
`ResetPlayerMovement` at the end. So while a sequence runs the local pawn is **hidden with
its input and movement taken away and its pose driven by the sequence**.

On the peer that had two effects:

1. the montage/animation the sequence puts on the pawn was published as a normal montage
   and replayed on the puppet — the "frozen / T-posed friend" the brief calls out, and
2. the same `PuppetDamage → "pd"` charging as in 1.1: the friend is locked out of control
   and still takes damage from the other client's world.

---

## 2. What I decided to sync — and why

| Thing | Synced? | Why |
|---|---|---|
| "this player is in a dialogue" (`dl`) | **yes** | cheap (≤ 50 B), and it is what the peer's copy of the body needs to stop being a fighting body. |
| "this player is watching a cinematic" (`ci`) | **yes** | same, plus it is the only way the peer can know the pose it receives is not meaningful. |
| the dialogue **outcome** — `PlayerChoice` (`ct`) | **yes** | the one dialogue-set variable that the existing quest sync (`wq`) does not carry. 8 bytes = one FName, sent as a change event with a seq. |
| dialogue **choices / option index**, the conversation UI | **no** | would mean starting a `UDlgContext` on the peer, i.e. exactly the "force the peer into a dialogue UI" the task forbids. The participant list is per-world actors (`UDialogueSubsystem.SpawnedParticipants`, `UDialogueInstigatorComponent` on each NPC), the audio is per client (one `SoundComp` per `FActiveDialogue`), and `StartDialogue` with an empty-outcome path would lock the *peer's* input while the host is the one talking. Options the player picks already land where it matters: narrative tags (`AddNarrativeTag`, synced in `wq`) and quest rows. |
| the cutscene **playback** on the peer | **no** | a level sequence is a per-map authored asset with its own `ALevelSequenceActor`, camera cuts and binds; starting it remotely means taking the peer's camera and input for a beat it does not own, and the peer's own world state already triggers its own story beats (`BPSO_CinematicDLUpdate.CinematicFired` is synced in store A, so the *world effect* was already covered — only the video was not). The brief's requirement is the negative one: no frozen puppet, no cheap hits. That is what §3 implements. |
| player hide/unhide state, camera | **no** | both are local presentation; the peer draws its own puppet. |

---

## 3. Implementation (diff summary)

### New file: `native/src/coop_dialog.cpp` (~950 lines, style of `coop_body.cpp`)

* **Detectors** (2 Hz, `dialog::Tick2`):
  * `DetectDialogue()` — `ue::FindFirstOf("DialogueSubsystem")`, then
    `ActiveDialogues` (`TArray<FActiveDialogue>`, offset from `ue::PropOffset`, dump
    fallback `0xA0`), element size cross-checked against the dump (`0x88`, logged if it
    disagrees), fields `Instigator @+0x00`, `DialogueContext @+0x08`, `DialogueAsset @+0x10`,
    plus `UDlgContext::GetActiveNodeIndex()` / `GetOptionsNum()`. The instigator's owner is
    logged once per active sample so the "an active dialogue here is MY dialogue" claim
    stays checkable.
  * `DetectCinematic()` — `ue::FindAllOf("BP_CinematicPlayer_C")`, "playing" = a
    `CutsceneActor` that is a live object (that pointer is set by `PrepareCutscene`). The
    sequence's short name comes from `LevelSequenceToPlay`.
  * The detectors run **always on** and log every real episode (`dlg: DIALOGUE ACTIVE …`,
    `cine: sequence PLAYING …`), so a normal play session reports the truth even without
    the harness — the fakes below are only the *test* trigger.
* **Local state machine** — `LockState{active,kind,seq,name,node,opts}`; `seq` bumps on every
  transition so a lost/repeated packet is harmless; a lane is published while active and for
  `kLinger = 4 s` after it ends so the **end** always arrives.
* **Outcome** — `ReadChoice/WriteChoice` on `PlayerChoice` (property offset, `ue::MakeName`
  for the tag, read-back verified); `outSeq` bumps on change; `ct` is repeated for 4 s.
  Applying the peer's tag is verified by read-back and logged.
* **Receiver** — `PeerLocked()` (with a 5 s staleness window, and per-lane kill switches),
  `NoteSkip(0|1)` counters, and the two verdict writers `result_dialog.txt` /
  `result_cine.txt` (one PASS per episode, written by the **receiving** client).
* **Verbs** — `dlgdump`, `dlgstart`, `dlgstop`, `dlgchoice`, `dlgfake`, `cinefake` (§4).

### Edits

| File | What |
|---|---|
| `native/coop_state.h` | new section "6b" (`namespace dialog`: `LockState`, `State`, `Start/Tick2/OnPeerLine/Reset/Serialize/Status`, `LocalLocked/PeerLocked/PeerLockedFresh/PeerAge/PeerKind/NoteSkip`, the six cmd hooks); `Toggles::dlgEnabled/cineEnabled`; `State::dialog`. Placed **before** `State` because `State` holds a `dialog::State`. |
| `native/src/coop_net.cpp` | `Toggles::Load` rows `DIALOG_OFF.txt`/`CINE_OFF.txt`; `Publish()` appends `dialog::Serialize(&line)` **last** (frozen order untouched); `ApplyPeer()` calls `dialog::OnPeerLine(line)` in its own step (`dlgRead`) **before** the zone guard (a busy body is not a per-map actor name and must stay idle across maps); `Loop2` runs `dialog::Tick2()` unguarded; `ResetAll()` calls `dialog::Reset()`; `Start()` calls `dialog::Start()`; six cmd verbs; `sync.txt` gets the `dlg=` and `cine=` lines. |
| `native/src/coop_puppet.cpp` | `UpdateAnimation()`: while `dialog::PeerLocked()` hold the `idle` clip at rate 1 and return (before the montage/locomotion decision) — the "no frozen/T-posed puppet" half. |
| `native/src/coop_combat.cpp` | `AiTargetPeer()`: return (and count) while the peer is busy — no enemy is dragged onto a body that cannot fight. `PuppetDamage()`: return (and count) while the peer is busy — no `pd` charge for a player who cannot dodge; `body::Tick2` keeps rewriting the stand-in's health from the peer's own sample, so it recovers by itself. |
| `run-two.ps1` | the `DIALOG` and `CINE` verdict blocks (§5), `$dlgOk`/`$cineOk` folded into `$pass`, and the two verdict files cleared at the start. |
| `run-lab-dialog.ps1` (workspace root) | the single-client measurement run (§5). |

Wire additions (own keys, appended last — R7 wire rule):

```
,"dl":[<active>,<seq>,<node>,<opts>,"<dialogueName>"]     local dialogue
,"ci":[<active>,<seq>,"<sequenceName>"]                  local cinematic
,"ct":[<outSeq>,"<PlayerChoice>"]                        dialogue outcome
```

---

## 4. Measurement first — what the game actually does

Static proof (Dumper-7 headers under `research\raw\CXXHeaderDump\`; the runtime probe is
§6.3). **Answer to the brief's question first: there is no pause in either case — the game
takes the local player's *control* away, not the world's clock.**

### 4.1 Dialogue (a conversation)

* The state is `UDialogueSubsystem` (a `UWorldSubsystem`, `Sparta.hpp`):
  `ActiveDialogues: TArray<FActiveDialogue> @0x00A0`; struct size `0x88`, with
  `Instigator @+0x00` (`UDialogueInstigatorComponent*`), `DialogueContext @+0x08`
  (`UDlgContext*`), `DialogueAsset @+0x10` (`UDlgDialogue*`), `Tags @+0x18`,
  `BlockingTags @+0x38`, `AudioEventId @+0x58`, `SoundComp @+0x68`,
  `FacialAnimationInstance @+0x70`, `FacialAnimationMontage @+0x78`. Delegates
  `OnDlgStarted @0x30`, `OnDlgEnded @0x40`, `OnDlgNodeEnterEvent @0x60`,
  `OnDlgNodeExitEvent @0x70`, `OnAudioEventFinished @0x80`, `OnDlgAudioStarted @0x90`.
* Who starts it: `ABP_DialogueActor_C` (`Dialogue Instigator Component @0x358`) →
  `UBPC_DialogueInstigator_C` (`DlgDialogue @0x148`, `Participants @0x150`,
  `WasTriggered @0x162`, `bDisablePlayerInput @0x194`) →
  `StartDialogue(AdditionalTags, AdditionalInterruptTags, AdditionalBlockingTags, out Success)`
  → `UDialogueSubsystem::SpartaStartDialogue(...)`.
* **What the player loses:** `DisablePlayerInput()` (input map ignored: movement, attack,
  dodge), and the game's own `BP_IC_NotInDialogueCurrently` / `GE_BlockInput_DuringDialogue`
  are the gates the content uses. **No pause, no camera cut** — the exploration camera stays
  on the pawn, the NPC turns to face it.
* **Per client:** the audio is a `USoundWave`/`UAudioEvent` per active dialogue
  (`FActiveDialogue::SoundComp`, `OnDlgAudioStarted`) and the facial animation is a montage
  on the participant's anim instance. Starting the same dialogue on the peer would play the
  voice twice and lock the *peer's* input — the two reasons this round does not sync the
  conversation itself (§2).

### 4.2 Cinematic (a level sequence)

* `ABP_CinematicPlayer_C` (`BP_CinematicPlayer.hpp`): `CutsceneActor: ALevelSequenceActor* @0x2C0`,
  `LevelSequenceToPlay: ULevelSequence* @0x2C8`, `HidePlayerDuringCinematic @0x2D9`,
  `PlayerPositionAfterCinematic @0x2E0`, `CinematicEndReached @0x2E8`,
  `ThisCinematicTag @0x320`, `StateGE @0x3A0`, `Skippable @0x3B0`, and the functions that
  spell out the mechanics: `HidePlayerAndWeapons`, `DisablePlayerMovement`,
  `AddCutsceneState` (a GameplayEffect through `StateGE`), `LockInteraction`,
  `TeleportPlayer`, `ResetPlayerMovement`, `ShowHUD/HideHud`, `ForceStopCinematic`,
  `StartCinematicViaEvent`, `TriggerCutsceneViaInteraction`, `EndCutsceneBySkip`.
* **What the player loses:** the pawn is hidden with its weapons, input **and** movement are
  disabled, the camera is handed to the sequence (`CameraState_Cinematic`), the HUD is
  hidden, and a skip prompt is added when `Skippable`. On end: teleport to
  `PlayerPositionAfterCinematic`, movement and HUD restored. `APlayerController::SetCinematicMode
  (bInCinematicMode, bHidePlayer, bAffectsHUD, bAffectsMovement, bAffectsTurning)` is the
  engine-level counterpart (`Engine.hpp:10878`).
* **No pause either** — the world keeps running, which is precisely why the *peer's* copy of
  that body must stop taking hits (the watching player cannot dodge).


---

## 5. Test commands and their verdict lines

```powershell
# build (from the workspace copy of the tree)
cmd /c G:\Freespace\shell_coop\wt\r7-dialog\ms2-coop\native\build.bat test
#   -> coop_policy: 112 checks passed, 0 failed
#   -> PASS: the co-op death rule's truth table holds, mutants included
#   -> PASS: all 4 patterns match exactly once and resolve to the recovered addresses
#   -> Built: G:\Freespace\shell_coop\wt\r7-dialog\ms2-coop\native\build\dxgi.dll

# the full regression run on MY pair (host G / guest H, relay 47130), under the
# shared lock, deploying the current build to both first:
powershell -NoProfile -ExecutionPolicy Bypass -File G:\Freespace\shell_coop\wt\r7-dialog\run-r7-two.ps1
#   -> prints SHELL, WEAPON, PUPVIS, LOOK, ANIM, DAMAGE, DEATH, WORLD, PROGRESS,
#      QUEST, QUEST2, LIFT, CHEST, STORE_A2, SPAWN, MIRROR, PEERHIT, AITARGET,
#      STREAM, NPCFAR, PERF, LEDGER, ZONE, DIALOG, CINE, PEERHP, PEERDEAD, NPC,
#      A, B  and finally RESULT: PASS|FAIL
```

---

## 6. Results

(Verdict lines are quoted exactly as printed; a WARN/FAIL is reported as such.)

### 6.1 Build

```
coop_policy: 112 checks passed, 0 failed
PASS: the co-op death rule's truth table holds, mutants included
PASS: all 4 patterns match exactly once and resolve to the recovered addresses
Built: G:\Freespace\shell_coop\wt\r7-dialog\ms2-coop\native\build\dxgi.dll
```
DLL deployed for the lab run: sha256 `205a2b13d6da7dc7…` (deploy.ps1 reported it back).

### 6.2 Regression + DIALOG/CINE (run-two on G/H, port 47130) — **NOT MEASURED**

Honest status: **no in-game run happened in this session.** The shared run lock (one game at a
time on this PC) was held continuously by the other round-7 agents for the whole window I had
left after the implementation was buildable. The lock's own owner file,
`G:\Freespace\shell_coop\local\game.lock\owner.txt`, recorded, back to back:

```
r7-wire 2026-10-26T00:31:18   r7-maps 2026-10-26T00:40:54   r7-wire 2026-10-26T00:46:11
r7-maps 2026-10-26T00:49:41   r7-maps 2026-10-26T01:01:52   r7-drops 2026-10-26T01:22:55
r7-maps 2026-10-26T01:26:44   r7boss  2026-10-26T01:32:29   r7-drops 2026-10-26T01:50:55
r7-wire 2026-10-26T01:57:01   r7-maps 2026-10-26T02:02:03   r7-maps 2026-10-26T02:15:28
r7boss  2026-10-26T02:25:40   r7-drops 2026-10-26T02:31:57   (still held at 02:34)
```

My runs (`run-r7-two.ps1` → `run-two.ps1`, and the 5-minute `run-lab-dialog.ps1`) sat in
`Lock-Game 'r7-dialog'` for that whole period and were cancelled when the session window ran
out, so they measured nothing. **No PASS/FAIL line in this section is observed.** Everything in
§2/§3/§4 is static proof (headers, dumps, code); §6.4 is what that leaves open.

The two blocks are in `run-two.ps1` and are ready to run. The exact command:

```powershell
# NOTE: the deploy step writes into E:\Games\_MS2-coop-g|h, i.e. OUTSIDE the agent
# workspace.  Under the agent's file sandbox that copy is denied (measured: "Copy-Item :
# Access to the path '...dxgi.dll.rollback' is denied"); a normal shell has no such
# restriction, so run this from a plain PowerShell, not from inside a sandboxed agent.
powershell -NoProfile -ExecutionPolicy Bypass -File G:\Freespace\shell_coop\wt\r7-dialog\run-r7-two.ps1
# -> deploys the current build to G and H under the lock, then run-two.ps1 -GameA ... -GameB ...
#    -Port 47130 -BootWait 110, and prints (among the 25 existing verdicts):
#    === DIALOG: A dlg=me:.. peer:1 kind:1 name:FAKE_DIALOGUE ... idle:N pd:M ai:K ep:E mine:M out:X/Quest.R7.DialogOutcome
#    === DIALOG: PASS dialogue lock: peer dialogue 'FAKE_DIALOGUE' held N.Ns ; puppet idle samples=N ...
#    === DIALOG: PASS (peerBusySeen=True idlePuppet=True pdNotCharged=True [x -> x] lockEnded=True outcomeApplied=True)
#    === CINE: PASS (peerBusySeen=True idlePuppet=True pdNotCharged=True [x -> x] lockEnded=True)
#    === DIALOGREAL: A dlg=me:.. peer:..            (the REAL detector, see 6.3)
#    === DIALOGCODEC: PASS dialog wire codec: 15 checks, 0 failed
```

For the record, the pieces that *are* verified without a game run:

| Check | Result |
|---|---|
| build | **PASS** (`coop_policy: 112 checks passed`, `Built:`, AOB self-check PASS) — §6.1 |
| the wire codec (tokenizer, empty-name end section, quoted names, `Clean`, `PlayerChoice` round trip) | implemented as `dlgselftest` → `result_dialogselftest.txt`; **not executed** (needs one game run) |
| wire keys appended last, no existing key moved | by inspection: `dialog::Serialize` is the last call in `Publish()` — §3 |
| kill switches | by inspection: `Toggles::Load` rows + `PeerLocked()` per-lane gates — §3 |

### 6.3 Runtime probe (the real detectors) — **PARTIALLY MEASURED**

One single-client lab run did happen (`run-lab-dialog.ps1` → `lab.ps1`, host install G,
`G:\Freespace\shell_coop\local\lab\20261026-023542-r7dlg-measure\`, probe log copied to
`wt\r7-dialog\lab-probe.log`; the console transcript of both attempts is
`wt\r7-dialog\lab-dialog-tee.txt` / `lab-dialog-tee2.txt`). It answers the "does the detector work
at all" question, which is what the brief's "measure first" asks for:

```
[02:35:48][A] dialog: start (DIALOG_OFF=0 CINE_OFF=0) -- dialogue state, the PlayerChoice outcome and cinematics travel on "dl"/"ci"/"ct"
[02:36:34][A] dlg: DialogueSubsystem=DialogueSubsystem_2147480703_2147480704 ActiveDialogues@0xa0
[02:36:34][A] dlg: PlayerChoice = PlayerChoice.Empty
[02:36:39][A] cine: 3 cinematic player(s) present, none playing
```

and `sync.txt` of that run carried both new lines:

```
dlg=me:0 peer:0 kind:0 name:- seq:0 idle:0 pd:0 ai:0 ep:0 mine:0 out:0/PlayerChoice.Empty
cine=me:0 peer:0 name:- ep:0 on:1
```

What that proves:

* `ue::FindFirstOf("DialogueSubsystem")` resolves the world subsystem, and
  `PropOffset(ActiveDialogues)` is the dump's **0xA0** — the dialogue detector's gate is real.
* `PlayerChoice` reads as the live tag **`PlayerChoice.Empty`** — the outcome property, its
  offset and the `FName` → string path work (that is the value the game ships when nobody has
  chosen yet; a real choice replaces it).
* The cinematic detector sees **3 `BP_CinematicPlayer_C` actors** in the hub and correctly
  reports **none playing** (no `CutsceneActor`), which is the "no cinematic" state; the log is
  throttled to 8 lines, which is why it stops after a few samples.
* Both status lines reach `sync.txt` at 2 Hz, i.e. the harness can read them.

Then the client **crashed**, before `cmd.txt` was consumed, so `dlgdump`, `dlgstart`,
`dlgstop`, `dlgchoice` and `dlgselftest` produced no output and the DIALOG/CINE verdicts above
were not reached. The crash is the round-5 **known** one — dump
`%LOCALAPPDATA%\CrashDumps\MortalShell2-Win64-Shipping.exe.52996.dmp`, opened with cdb:

```
Last event: cf04.100c: Unknown exception - code c0000374
FAILURE_BUCKET_ID: HEAP_CORRUPTION_ACTIONABLE_BlockNotBusy_DOUBLE_FREE_c0000374_ntdll.dll!RtlpFreeHeapInternal
Timeline.Process.Start.DeltaSec: 62
STACK_TEXT: ... steam_api64!SteamInternal_FindOrCreateUserInterface ... ntdll!RtlFreeHeap
              -> ntdll!RtlpFreeHeapInternal -> RtlReportCriticalFailure
```

i.e. the documented double free **inside `steam_api64.dll`** on the game's own thread at 62 s
after process start, **with no `dxgi.dll` frame anywhere in the stack** (`README.md` "Crash
0xC0000374 (all dumps): … 47–62 s after launch"). It is not this module: at the moment of the
crash the mod had only *read* three things (the subsystem, `PlayerChoice`, the cinematic
players) and was still logging `play ready=false (loading screen)`.

**Second attempt, same wall.** `run-lab-dialog.ps1 -Tag r7dlg-measure2` (the lock was free at
02:44) reproduced the identical picture and then the identical crash:

```
[02:45:47][A] gameplay ready after 5s: BP_PlayerCharacter_C /Game/Sparta/Maps/Core_Levels/L_Core_World_01...
[02:45:48][A] play ready=false (loading screen)
[02:45:48][A] dlg: DialogueSubsystem=DialogueSubsystem_2147480703_2147480704 ActiveDialogues@0xa0
[02:45:48][A] cine: 3 cinematic player(s) present, none playing
[02:45:48][A] dlg: PlayerChoice = PlayerChoice.Empty
```

then nothing: `cmd.txt` was never consumed (`probe.log` mtime 02:45:48, `cmd.txt` written
02:49:03) and a new dump appeared - `MortalShell2-Win64-Shipping.exe.49364.dmp`, **02:45:55**,
60 s after launch again.

So on this machine, in this session, a client dies of the steam_api64 double free ~60-65 s after
launch, which is BEFORE the harness reaches `phase=sync` plus the first `cmd.txt` verb (the boot
path alone is 47-60 s of menu/streaming). That is the concrete blocker for every cmd-driven
verdict - mine and the older ones - and it is not caused by this round's code: both dumps predate
any `cmd` verb being consumed and carry no `dxgi.dll` frame. Closing it (or running on a machine
where it does not happen) is the first step before the DIALOG/CINE verdicts can pass.

### 6.4 What this means

Proven in game: the dialogue **gate** (subsystem + `ActiveDialogues @0xA0`), the outcome
**read** path (`PlayerChoice`), the cinematic **gate** (3 players, none playing), and that both
new `sync.txt` lines exist. **Not proven in game:** the peer reaction (idle puppet / no `pd`
charge / `result_dialog.txt` / `result_cine.txt`) and the cmd verbs, because the client died of
the pre-existing `steam_api64` heap-corruption crash (§6.3) before `cmd.txt` was read, and the
shared lock then never came back within the session window.

If the detector layout were wrong on a real conversation, the log says so directly
(`dlg: FActiveDialogue element size is 0x..` or `dlg: no DialogueSubsystem instance …`); the
fake path (`dlgfake`) exercises the wire + receiver half independently of it, which is why the
verdicts use it as the trigger.


---

## 7. Anything left open

1. **The two verdicts have never run** (§6.2). `DIALOG`/`CINE` are wired into `run-two.ps1`
   (`$pass` includes them), the blocks are self-contained, and the fakes are the trigger — one
   `run-r7-two.ps1` is all that is missing. Until it runs, treat the round's claims as
   "implemented + gate proven, effect unmeasured".
2. **The dialogue actor trigger is untested.** `dlgstart` assumes `ABP_DialogueActor_C` exposes
   `Dialogue Instigator Component` (dump: `@0x358`) and that `StartDialogue` accepts three empty
   `FGameplayTagContainer`s. If the call refuses, the log line says so
   (`dlgstart: %s has no Dialogue Instigator Component` / `StartDialogue called ok=.. Success=..`)
   and nothing else in the lane is affected.
3. **The "an active dialogue here is MY dialogue" assumption.** It is true for single-player
   semantics (the peer's world has its own idle NPCs and its own subsystem), but the instigator
   owner is only *logged*, not *checked*. If a future build starts an ambient/scripted dialogue
   for an NPC far away, the local side would report "busy" for a player who is not. The log line
   carries `instigatorOwner=` for exactly that case, and the fix would be a one-line owner
   comparison against `net::Pawn()`.
4. **Cinematic detection is single-signal** (`CutsceneActor != null`). It did not fire on the hub
   (3 actors, none playing — correct), but no real cinematic was observed in a test save. If a
   boss intro leaves `CutsceneActor` set after `EndCutscene`, the peer would keep the puppet idle
   (cosmetic only, no damage/health effect) until the pointer clears. A `LevelSequencePlayer`
   playback check or `WBP_SkipInput` presence would tighten it.
5. **The peer's *own* AI is not touched.** `AiTargetPeer` stops *redirecting* enemies onto a
   busy body and `PuppetDamage` stops charging the friend, but an enemy that already targeted
   the stand-in keeps attacking it in our world (its hits now cost the friend nothing). Making
   the local AI drop the target is a `TargetSelectionComponent` change in `coop_combat` and was
   left out to keep this lane contained.
6. **`PlayerChoice` is a single global tag.** It is synced as a last-writer-wins value with a
   seq, which is what the game itself does — but a real story fork writes it via
   `AddNarrativeTag`-adjacent code, so a hidden second source (the save object's own tag
   container) is not compared. The narrative tags themselves already travel in `wq`.
7. **The client crashed on the known `steam_api64` heap corruption** (dump 52996, §6.3) during
   the lab run. That crash predates this round and is listed in round 5's open problems; it is
   the reason the cmd verbs never ran. Nothing in the dump's stack belongs to `dxgi.dll`.

---

# Round 7b — owner's decisions: both players see/hear the dialogue and the cutscene, choices are a vote

Round 7's "the peer is a spectator" decision is **replaced** by the owner's round-7b
decision.  This section records the crash control, the new behaviour, the verdicts and
what is still not reached.  Everything above stays as the round-7 record.

## 7b.1 Crash control (was my module crashing the clients?)

The round-7 lab crashed twice at ~60 s with `c0000374` (a double free inside
`steam_api64.dll`).  Control experiment, run with the same lab script
(`run-lab-test.ps1`, one client, same install):

| DLL | result |
|---|---|
| main tree `ms2-coop\native\build\dxgi.dll` (sha `F65306B0…`) | **SURVIVED** — `clientsAlive=1 newDumps=0`, probe.log still written |
| my build (no kill switches) | **SURVIVED** — `clientsAlive=1 newDumps=0` |

So the round-7 crashes were the pre-existing, intermittent `steam_api64` heap
corruption (documented in round 5's README), not this module.  Every later two-client
run (`clientsAlive=2` / `alive=2`) and the four visual runs also survived; the full
suite now ends `RESULT: PASS`.  Evidence files: `control-main.txt`, `control-mine.txt`,
`control-dlgreal*.txt`.

## 7b.2 Cinematics: the peer plays the SAME sequence

`"ci"` now carries `[active, seq, elapsedMs, "sequenceName"]`.  On the peer,
`CinematicReplayTick` finds its own `BP_CinematicPlayer_C` whose `LevelSequenceToPlay`
matches that asset, clears `BroadcastEventAfterCinematic` (so the world effect still
happens exactly ONCE, through the existing sync), and calls the game's own
`PlayCutscene()`.  A late join seeks with
`UMovieSceneSequencePlayer::SetPlaybackPosition(FMovieSceneSequencePlaybackParams{Time,
PositionType=Time, UpdateMethod=Jump})` after `PlayCutscene`.  An end on either side
(`ForceStopCinematic`) ends it on both — the initiator's `"ci"` going to 0 sets
`g_cineStopPending` on the listener, and a listener that stops first publishes its own
`"ci":0`, which the initiator honours, so either player's skip ends it for both.
The detector now asks `SequencePlayer::IsPlaying()` instead of "CutsceneActor != null":
round 7's first run showed `me:1` for 20 s after a *successful* `ForceStopCinematic`,
because that pointer stays set.

New verbs: `cineplay [name]` (the game's own `PlayCutscene` on a real
`BP_CinematicPlayer_C`), `cinestop`, `cinehold <s> [name]` (restart-loops a short
sequence for screenshot passes — presentation only).

**Measured** (`result_cine.txt`, written by the receiving client):

```
=== CINE: PASS cinematic lock: peer 'LevelSequence_ShellKeeperIntro_CoreWorld' held 2.0s ;
         puppet idle samples=5, damage charges suppressed=5, replays started=1 (jumps=0, stops=0)
=== CINE: PASS (aPlaying=True bReplays=False replayCounter=True idlePuppet=True
         pdNotCharged=True [0 -> 0] endedBoth=True)
```

`bReplays=False` means "B's own `me:1` and `peer:1` were not seen in the SAME poll" —
the hub intro lasts ~2 s and the polls are 150 ms apart, so the two flags race; the
`replay:` counter (B started the sequence) and the screenshots below are the real
evidence.  **On screen** (`local\lab\20261026-043036-r7b-cine\`,
`wt\r7-dialog\sheet-cine.png`): A's window and B's window show the **same** Shell Keeper
intro framing in the same frames — B is playing A's cutscene, not standing in the hub.

## 7b.3 Dialogue: the peer hears the lines, sees the text, and votes

What the game holds (dump-proved): `UDlgDialogue::Nodes @0x170`;
`UDialogue_SpartaSpeechNode{Text @0xA0 (FText), SoundWave @0xD0, AudioEvent @0xF8,
SubtitleSpeakerName @0x114}`; `UDlgNode::Children @0x88` of
`FDlgEdge{TargetIndex @0x0, Text @0x30}`; `UDlgContext::ChooseOption(int32)` /
`ChooseSpeechSequenceOptionFromReplicated(int32)`; `WBP_HUD_Dialogue_C`
(`CRT_DialogueSubtitles @0x380`, `ClearSubtitles`).

Implementation, driven by `"dl":[active,seq,node,opts,"name","assetPath"]`:

* **Listener** (`ListenerTick`, 2 Hz): loads the SAME dialogue asset from the path the
  owner published, resolves the node to the one that actually SPEAKS (node 0 of
  `DLG_HubCultist02_Greeting` is a `DlgNode_Selector` with no text — measured:
  `line=- voice=-`; `SpeechNodeFor` follows the child edges up to 4 levels), then
  * plays the voice: `UGameplayStatics::SpawnSound2D` with the node's own
    `SoundWave`/`AudioEvent` (2D so it is audible wherever the listener stands),
  * shows the text: `UKismetTextLibrary::Conv_StringToText` (engine-allocated FString
    from `UKismetStringLibrary::Conv_NameToString`) into the dialogue HUD's subtitle
    block, plus `UKismetSystemLibrary::PrintString` as a second channel, re-applied every
    2 s so the line stays up until the next node.
* **Vote** (owner's rule): each client publishes `"dv":[dSeq,node,opt,"id"]`; the owner
  collects them per `(dSeq,node)`, and `ResolveVotes()` decides — most votes wins, a tie
  between different options goes to the HOST's vote (role A), no vote at all leaves the
  game's default (option 0).  The window closes when `expected` players have voted
  (`1 + (peer in zone ? 1 : 0)`; an out-of-zone peer abstains immediately) or after
  `[Rules] dialog_vote_timeout` (default 20 s).  The owner applies the winner with
  `ChooseSpeechSequenceOptionFromReplicated` **once**, with the intercept disabled.
  A local click is intercepted by a `HookPreSkipFn` on
  `UDlgContext.ChooseOption`/`ChooseSpeechSequenceOptionFromReplicated`: it records the
  vote and SKIPS the game's own advance, so no client advances its conversation
  directly.  `"dr":[dSeq,node,winner,"id:opt|…"]` publishes the result and every
  listener shows `==> A picked N` before the next line.
* New verbs: `dlgvote <1-based option>`, `dlgvotesim` (the six resolution cases),
  `dlgstart [class]` (the game's own `BPC_DialogueInstigator_C::StartDialogue`, with a
  teleport next to the NPC and `SkipParticipantsCheck`), `dlgstop`, `dlgchoice`,
  `dlgselftest`, `dlgfake [on] <assetPath>`, `cinefake`.

**Measured** — one full `run-two.ps1` ended **`RESULT: PASS`** with:

```
=== DIALOG: realConversation=False (dlgstart content-gated in this save; see REPORT)
=== DIALOG: A dlg=... peer:1 kind:1 name:DLG_HubCultist02_Greeting... node:0 opts:0 seq:1
                idle:8 pd:8 ai:8 ep:1 mine:0 listen:1 lines:1 vote:0/0 out:0/PlayerChoice.Empty
=== DIALOG: PASS dialogue: peer 'DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting' held 1.9s ;
                puppet idle samples=5, replayed lines=1, damage charges suppressed=5, ...
=== DIALOGVOTE: A PASS dialogue votes: 7 checks, 0 failed ; B PASS dialogue votes: 7 checks, 0 failed
=== DIALOGCODEC: PASS dialog wire codec: 21 checks, 0 failed
=== DIALOG: PASS (peerBusySeen=True realConversation=False idlePuppet=True listenLines=True
                pdNotCharged=True [0.1 -> 0.1] votes=True lockEnded=True)
```

and the listener's own log, on the **same run's asset** (lab run 4):

```
[B] dlg: LISTEN node=0 speaker=Disciple line=Welcome back, High Sester.
        voice=AE_VO_HubCultist_GreetGenessa_10_11 options=2
[B] dlg: voice AE_VO_HubCultist_GreetGenessa_10_11 -> playing (2D, listener)
```

So **B hears A's line and reads the same line text** (the audio event is the game's
own; the text is the asset's own FText, no string the mod invented).  `dlgvotesim`
checks the shipped resolver: same vote wins, tie→host (both orders), single vote on
timeout, nobody voted→0, majority beats host, out-of-range ignored — 7/7 on both
clients.

**What is NOT reached (honest):** the on-screen *subtitle* does not appear yet.  The
log says why: `dlg: WBP_HUD_Dialogue has no CRT_DialogueSubtitles -- no subtitle block`
(the instance the module finds has a null subtitle block — the real dialogue HUD is
built by the game when a conversation starts, and there is none here), and the
`PrintString` second channel drew nothing in the captured frames.  The text IS resolved
and applied to both channels every 2 s; the remaining gap is purely presentation, and
`sheet-dialog4.png` shows the frames without it.  Because no real conversation can be
started in this save (`StartDialogue` → `Success=0` even next to the NPC with
`SkipParticipantsCheck`, and `SpartaStartDialogue` returns ok but leaves
`ActiveDialogues` empty — both the comp's content gate and the subsystem's instigator
requirement), the DIALOG verdict drives the *same wire* with `dlgfake on <real asset>`,
so the listener half is exercised with real content while the owner half cannot be
produced by the game here.

## 7b.4 Verdicts and commands

```powershell
# control (crash): same lab script, one DLL at a time
powershell -NoProfile -ExecutionPolicy Bypass -File run-lab-test.ps1 `
    -DllPath G:\Freespace\shell_coop\ms2-coop\native\build\dxgi.dll -Tag main-control
powershell -NoProfile -ExecutionPolicy Bypass -File run-lab-test.ps1 `
    -DllPath G:\Freespace\shell_coop\wt\r7-dialog\ms2-coop\native\build\dxgi.dll -Tag mine-noflags

# full suite + the two new verdicts (host G / guest H, relay 47130)
powershell -NoProfile -ExecutionPolicy Bypass -File run-r7-two.ps1
#   -> ... === DIALOG: PASS ... === CINE: PASS ... RESULT: PASS

# the visual passes (screenshots of BOTH windows)
. \ms2-coop\visual-two.ps1 -GameA E:\Games\_MS2-coop-g -GameB E:\Games\_MS2-coop-h `
    -Port 47130 -BootWait 200 -Tag r7b-cine   -Cmds 'A=cinehold 40'
. \ms2-coop\visual-two.ps1 -GameA E:\Games\_MS2-coop-g -GameB E:\Games\_MS2-coop-h `
    -Port 47130 -BootWait 200 -Tag r7b-dialog -Cmds 'A=dlgfake on /Game/Sparta/Dialogue/Dialogues/HUB/TunnelCultist/DLG_HubCultist02_Greeting.DLG_HubCultist02_Greeting'
```

Screenshots (contact sheets built with System.Drawing, looked at):
`wt\r7-dialog\sheet-cine.png` (A and B playing the same sequence),
`sheet-dialog4.png` (B's window during A's line — text not yet painted),
`sheet-dialog.png` / `sheet-dialog2.png` / `sheet-dialog3.png` (earlier attempts).
Raw frames: `local\lab\20261026-043036-r7b-cine\`, `…-044743-r7b-dialog3\`,
`…-045437-r7b-dialog4\`.

## 7b.5 Diff summary (round 7b)

| File | Change |
|---|---|
| `src/coop_dialog.cpp` | cinematic replay (`FindCinematicActor`/`StartCinematicReplay` (BroadcastEventAfterCinematic=false + optional SetPlaybackPosition seek)/`StopCinematicReplay`, `CinematicReplayTick`), detector switched to `SequencePlayer::IsPlaying`, listener (`ListenerTick`, `SpeechNodeFor`, `NodeOptions`, `PlayVoice` 2D, `ShowSubtitle` via the HUD block + `PrintLine`), the vote engine (`ResolveVotes`, `OpenVoteWindowIfNeeded`, `ResolveVotesIfDue`, `ChoiceSkipForHook` + `InstallChoiceHook`, `SerializeVote`), `"dv"`/`"dr"` wire, verdict writers, verbs `dlgvote`/`dlgvotesim`/`cineplay`/`cinestop`/`cinehold`, `dlgfake <assetPath>`, `dlgstart` teleport + `SkipParticipantsCheck` + `SpartaStartDialogue` fallbacks |
| `coop_state.h` | `LockState::elapsedMs`, `State::peerPath`, verb declarations |
| `src/coop_net.cpp` | verbs `dlgvote`, `dlgvotesim`, `cineplay`, `cinestop`, `cinehold` |
| `run-two.ps1` | DIALOG driven by `dlgstart` → `dlgfake on <real asset>` with the listener-line, vote-sim and pd checks; CINE driven by a REAL `cineplay` + `cinestop` with the replay counter, idle and pd checks |
| `run-lab-test.ps1`, `run-r7-two.ps1` | the control lab and the locked one-command runner |
### 7b.6 Final run on the shipped build

`run-two.ps1 -GameA _MS2-coop-g -GameB _MS2-coop-h -Port 47130 -BootWait 110`, with the
last build deployed to both installs (sha256 `c3e9e32e5ded1524` → the listener fixes
included):

```
=== DIALOG: PASS (peerBusySeen=True realConversation=False idlePuppet=True listenLines=True
                 pdNotCharged=True [0.1 -> 0.1] votes=True lockEnded=True)
=== DIALOGVOTE: A PASS dialogue votes: 7 checks, 0 failed ; B PASS dialogue votes: 7 checks, 0 failed
=== DIALOGCODEC: PASS dialog wire codec: 21 checks, 0 failed
=== CINE: PASS (aPlaying=True bReplays=False replayCounter=True idlePuppet=True
                 pdNotCharged=True [0 -> 0] endedBoth=True)
RESULT: PASS
```

with `listen:1 lines:2` on the receiving client — two listener replays of the real
line, plus every other verdict (SHELL, WEAPON, PUPVIS, LOOK, ANIM, DAMAGE, DEATH,
WORLD, PROGRESS, QUEST, QUEST2, LIFT, CHEST, STORE_A2, STORE_A2SELFTEST, SPAWN,
MIRROR, PEERHIT, AITARGET, STREAM, NPCFAR, LEDGER, ZONE, PEERHP, PEERDEAD, A, B) PASS.

Remaining presentation gap is only the on-screen subtitle (§7b.3): the text is resolved
and handed to both channels every 2 s, the voice line plays, but the HUD dialogue
widget's subtitle block is not reachable on a client that has no local conversation.
---

# Round 7c — end-sync lag, the listener's on-screen subtitle, and the real-conversation start

The round-7b build was merged.  This round answers the three follow-ups.  **Read the
status column seriously: one of the three is delivered, one is implemented but not
proven on screen, and the third is blocked inside the game with the blocker named.**

| # | Item | Status |
|---|---|---|
| 1 | cinematic END lag ≤ ~0.5 s | **measured, improved, NOT proven at target** — 545 ms and 1170 ms were the numbers; three new mechanisms attack the lag, but the last two burst runs did not reproduce a clean A→B stop to re-measure |
| 2 | listener subtitle visible on B | **implemented against the game's own subtitle widget** (`WBP_Subtitle_Base_C`), **no screenshot proof yet** — the capture runs did not get B into the listening state |
| 3 | a REAL NPC conversation | **blocked, blocker measured**: participants/interface/tag/interaction all satisfied, `StartDialogue` still returns `Success=0` and `ActiveDialogues` stays empty |

## 7c.1 Cinematic END lag

### What the module does now (three changes)

1. **Detect the end at 15 Hz, not 2 Hz** — `FastCinematicEndPoll()` runs inside
   `dialog::Serialize` (every publish) and asks
   `UMovieSceneSequencePlayer::IsPlaying()`; the moment it goes false the state is
   cleared *and* `NoteLocalCinematicEnd()` stamps `WallMs()%1e8`.
2. **Publish the stop in the SAME packet** — `cinestop`, the natural end and the poll all
   go through `NoteLocalCinematicEnd()`, which bumps the seq, sets `g_mineEndedAt` and the
   stop stamp, so the next 15 Hz packet carries `"ci":[0,seq,<A's stop ms>,""]`.  The stamp
   is what makes the lag MEASURABLE instead of guessed (both clients run on this PC).
3. **Apply the peer's stop immediately** — the `ci` inactive branch stops the replay actor
   inside `OnPeerLine` (which runs on the game thread inside the 15 Hz apply step) and logs
   `cine: END applied <X>ms after the peer stopped it`.  Previously it was deferred to the
   2 Hz tick.
4. **A local hold can no longer delay it** — `dialog::PeerKeepAlive()` is a never-skipped
   15 Hz step (the same pattern as round 5's `DeathPeerKeepAlive`): if `ApplyPeer` has not
   run for 150 ms (a teleport hold, a world settle, a respawn) it parses the peer line
   itself.  This was the round-7c suspicion: `applyPeer` is `holdSkip`, and a cutscene end
   that lands inside a hold waits for the hold to expire.
5. A listener's replay is now cleared when the *initiator's* sequence ends (`g_mine` is
   dropped in the `ci` inactive branch), so the listener half cannot stay "busy" after a
   replay that the game already ended on its own.

`sync.txt` carries the measurement: `cinesync=lag:<ms> n:<count> meStop:<stamp>`.

### Measured (bursts of screenshots ~220 ms apart around the stop)

`run-burst.ps1` (new) boots both clients, plays `cinehold 40` on A, then `cinestop` and
screenshots B 14 times over ~3 s, then `dlgfake` for the subtitle half.

| burst | direction | measured from the logs |
|---|---|---|
| #1 `local\lab\20261026-053451-r7c-burst` | B→A (B's replay ended by itself, A applied B's stop) | **545 ms** (`[A] cine: END applied 545ms after the peer stopped it (stop stamp 89369877)`) |
| #2 `…-053754-r7c-burst2` | A→B (`[A] cine: END noted at 89554142 (cinestop verb)`) | **1170 ms** (`[B] cine: END applied 1170ms after the peer stopped it (stop stamp 89554142)`) |
| #3 `…-054213-r7c-burst3`, #4 `…-054457-r7c-burst4` | A→B, after the fixes | **not conclusive**: in #3/#4 B's replay actor was already gone (`replay:0/0/0`) when A stopped, so B had nothing to end and no lag line exists.  A still publishes the stop immediately (`cine: END noted … (cinestop verb)`) and B's `cinesync=lag:-1` shows the apply path never ran. |

So: the **code paths that the lag was made of are gone** (2 Hz detection, deferred apply,
hold-skipped apply), but the ~0.5 s target is **not proven**.  The next step is a re-run of
`run-burst.ps1` in a session where the timing lines up (B's replay must be alive when A
stops — the burst gives it 8 s, and the hub intro is ~2 s, so `cinehold` needs to restart B's
copy too; that is the one thing to fix: **the hold currently restarts only the initiator's
sequence**).  Frames: `end-B-00..13.png` in each burst dir; all 14 frames of burst #2 are
byte-identical (B's screen never changed inside the 3 s window), which is the visual
statement of the 1170 ms finding.

## 7c.2 The listener's subtitle

### Where the game actually paints it

`research\raw\CXXHeaderDump\WBP_Subtitle_Base.hpp`:

```
class UWBP_Subtitle_Base_C : public UUserWidget
    UTextBlock* Text_Subtitle                   // 0x0348
    void UpdateText(FText SubtitleText);
    void ToggleSubtitle(bool bIsVisible, FText SubtitleText);
```

That is the game's own subtitle line widget (plus the unused `USubtitleDisplaySubsystem` of
the GameSubtitles plugin and `WBP_HUD_Dialogue_C::CRT_DialogueSubtitles @0x380` from
round 7b).  New `ShowSubtitleFText(FTextRaw, ...)`:

1. `ue::FindAllOf("WBP_Subtitle_Base_C")` → `ToggleSubtitle(bIsVisible=true, SubtitleText=nodeFText)`,
   then `UpdateText(nodeFText)`, then the `Text_Subtitle->SetText(nodeFText)` property path;
2. `SetVisibility(3)` (visible, no input) + `AddToViewport(30)` when it is not in the viewport;
3. the FText is the **node's own FText read out of the dialogue asset** (`+0xA0`), never a
   string the mod builds — the same bytes the game would show;
4. the round-7b `PrintString` fallback stays, with the bug fixed: the world context is now
   the **player controller** (a bare `UWorld` was passed before, which is why nothing was
   drawn).

The line is resolved exactly like the game would (`SpeechNodeFor` follows a
`DlgNode_Selector`'s children to the node that carries text/voice — measured error line in
7b was `line=- voice=-` on node 0).

### Status

Implemented, built, and the voice half is proven from round 7b
(`dlg: voice AE_VO_HubCultist_GreetGenessa_10_11 -> playing (2D, listener)`).  **The
screenshot of the text is still missing**: in bursts #3/#4 B never reached the listening
state (`listen:0 lines:0`), because its own `mine` was still 1 from the replay at the moment
`dlgfake` ran — the fix for that is the `g_mine` clear added in 7c.1 item 5, which those runs
predate.  One `run-burst.ps1` on the current build is what produces the proof frame
(`dlg-B-*.png`); the run after the fix was #4 only.

## 7c.3 A real NPC conversation — the exact blocker

`dlgstart` now drives the interaction path and logs every gate.  Measured chain on
`BP_Cultist_HUB_C` (lab `local\lab\20261026-052745-r7c-gates`):

```
dlgstart: interaction path: HandleInteractEvent ok=1, SendInteractionGameplayEvent ok=1
dlgstart: IDlgDialogueParticipant present: pawn=1 npc=1
dlgstart: IsDialogueCurrentlyActive ran=1 -> 0
dlgstart: npc gating: CheckNarrativeTag=1 TagToCheck=Dialogue.Ruk.FirstTimeTop
dlgstart: HandleDefaultInteraction ok=1
dlgstart: after HandleDefaultInteraction active=0 asset=- instigator=- ctx=-
dlgstart: verify active=0 …(8 samples over 4 s)…
dlgstart: no conversation after the interaction path -- trying the direct start
dlgstart: AddParticipant(pawn) ok=1 ; participants 1 -> 1
dlgstart: CheckParticipants -> Success=1 (participants=1)
dlgstart: component prepared (SkipParticipantsCheck 1 -> 1, dialogue=Dlg_Hub_Cultist_Interact_2_3)
dlgstart: StartDialogue ok=1 Success=0
dlgstart: SpartaStartDialogue(participants=[BP_PlayerCharacter_C…]) ok=1
```

With `AddNarrativeTag(Dialogue.Ruk.FirstTimeTop)` + `bInteractable/IsTriggerable=true` the
result did not change (that run is the one whose client crashed at 60 s on the known
`steam_api64` corruption, so it has no log line to quote).

**What is NOT the blocker** (each measured): the participant list
(`GetParticipants 0 -> 1`, `CheckParticipants Success=1`), `SkipParticipantsCheck`, the
`IDlgDialogueParticipant` interface (present on both the pawn and the NPC), the interaction
component (`HandleInteractEvent`/`SendInteractionGameplayEvent`/`HandleDefaultInteraction`
all return 1), and the dialogue asset (the interaction switches it to
`Dlg_Hub_Cultist_Interact_2_3`, i.e. it *does* select the right dialogue).

**What IS the blocker:** the instigator's own `StartDialogue`/`ActivateDialogue` still
returns `Success=0` and `UDialogueSubsystem.ActiveDialogues` stays empty.  Since the
component runs, the asset is right and the tags/participants pass, the refusal is inside the
blueprint's `ActivateDialogue` path — most likely `bDisablePlayerInput`/`WaitForParticipants`
handling or a widget-side "interaction confirmed" state the component expects
(`BPC_InteractionHandler_C::OnInteraction` fires only from the real input binding).  The next
probe is to call `ActivateDialogue(three empty containers, out Success)` directly and to log
`WasTriggered`, `WaitForParticipants`, `bDisablePlayerInput` right before/after it; if
`ActivateDialogue` also returns 0, the gate is in the dialogue asset's own participant
mapping (`ParticipantsData` keyed by participant names) and the NPC must be given the
*player's* participant name.

## 7c.4 Files, verbs, commands

| File | Change |
|---|---|
| `src/coop_dialog.cpp` | `WallMs()`, `NoteLocalCinematicEnd()`, `FastCinematicEndPoll()` (15 Hz, `IsPlaying`), immediate stop apply in `OnPeerLine` + `cine: END applied <ms>` log, `PeerKeepAlive()`/`NoteApplyAt()`, stop stamp in `"ci"`, `cinesync=` status line, replay state cleared on the peer's end, `ShowSubtitleFText()` on `WBP_Subtitle_Base_C`, `PrintString` world context = player controller, `DialogueStartTick()` state machine (teleport → interaction → 4 s verify → direct start), `LogDlgGates()` (interface/tag/active/`HandleDefaultInteraction`), `AddNarrativeTag` + `bInteractable/IsTriggerable` before the direct call |
| `coop_state.h` | `NoteApplyAt`, `PeerKeepAlive` declarations |
| `src/coop_net.cpp` | `ApplyPeer` marks the clock; `dlgPeer` never-skipped 15 Hz step |
| `run-burst.ps1` (new) | lock+deploy+boot both clients, `cinehold 40` → `cinestop` with 14 screenshots ~220 ms apart on B, then `dlgfake` with 4 subtitle screenshots; copies both probe logs and `sync.txt` |

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File G:\Freespace\shell_coop\wt\r7-dialog\run-burst.ps1 -Tag r7c-burst5
```

## 7c.5 Regression

Build: `coop_policy: 112 checks passed` + `Built:` after every change in this round.
The last full `run-two.ps1` of the round-7c change set is the one whose result is quoted
below (if the line is missing, the change set is build-verified only and must be re-run
before a merge).
### 7c.5 (measured) Regression on the round-7c build

`run-two.ps1 -GameA _MS2-coop-g -GameB _MS2-coop-h -Port 47130 -BootWait 110`, deployed
sha256 `faa451155e6285eb…`:

```
=== DIALOG: PASS (peerBusySeen=True realConversation=False idlePuppet=True listenLines=True
                 pdNotCharged=True [0.1 -> 0.1] votes=True lockEnded=True)
=== DIALOGVOTE: A PASS dialogue votes: 7 checks, 0 failed ; B PASS dialogue votes: 7 checks, 0 failed
=== DIALOGCODEC: PASS dialog wire codec: 21 checks, 0 failed
=== CINE: PASS (aPlaying=True bReplays=False replayCounter=True idlePuppet=True
                 pdNotCharged=True [0 -> 0] endedBoth=True)
RESULT: PASS
```

Every other verdict PASSed in the same run (SHELL, WEAPON, PUPVIS, LOOK, ANIM, DAMAGE,
DEATH, WORLD, PROGRESS, QUEST, QUEST2, LIFT, CHEST, STORE_A2, STORE_A2SELFTEST, SPAWN,
MIRROR, PEERHIT, AITARGET, STREAM, NPCFAR, LEDGER, ZONE, PEERHP, PEERDEAD, A, B).

### 7c.6 What a follow-up must still do (in priority order)

1. **Re-measure the end lag with a live replay on B.** `run-burst.ps1`'s `cinehold`
   restarts only the initiator's copy; the listener's replay is a one-shot ~2 s sequence,
   so by the time A stops, B usually has nothing left to end (that is why bursts #3/#4
   have no lag line). Fix in the harness: `cinehold` must be sent to BOTH clients
   (`A=cinehold 40` then `B=cinehold 40`) so both copies are alive when A stops.
2. **The subtitle proof frame.** In `run-burst.ps1`, run the `dlgfake` half only after the
   cinematic is fully over on both (`cine` shows `me:0 peer:0`), then screenshot B.  The
   `WBP_Subtitle_Base_C` path is wired (`ToggleSubtitle`/`UpdateText` with the node's own
   FText) and logs `dlg: subtitle via WBP_Subtitle_Base_C -> shown|FAILED (N instances)`.
3. **`ActivateDialogue`** as the next dialogue probe: the interaction component runs and
   selects the right asset, participants/interface/tag all pass, yet
   `StartDialogue → Success=0`; calling `ActivateDialogue(…, out Success)` directly and
   logging `WasTriggered`/`WaitForParticipants`/`bDisablePlayerInput` splits the last two
   possibilities (a widget-side confirm state vs a participant-name mapping gate).
---

# Round 7d — the three proofs

Status: **1 done (measured + burst), 2 done (the line is on B's screen), 3 probed to the
exact refusing call and stopped there as instructed.**

## 7d.1 Cinematic END lag — both directions, under the target

Harness fix that made the measurement possible: `run-burst.ps1` now sends
`cinehold 40` to **both** clients (before, only the initiator's copy was alive when it
stopped, so there was nothing on the listener to end).

Same run, one stop event pair (`local\lab\20261026-062913-r7d-burst3`):

```
[A] cine: END noted at 92628589 (cinestop verb) -- publishing the stop
[B] cine: END applied 441ms after the peer stopped it (stop stamp 92628589)      <- A stops -> B stops
[B] cine: END noted at 92629077 (15 Hz poll) -- publishing the stop
[A] cine: END applied 461ms after the peer stopped it (stop stamp 92629077)      <- B ends -> A stops
```

The run before it (`…-062214-r7d-burst2`) measured **495 ms** and **371 ms** for the same
two legs.  So, twice in a row, **both directions are under 0.5 s**:

| direction | burst2 | burst3 |
|---|---|---|
| A stops → B out of the cutscene | **495 ms** | **441 ms** |
| B stops → A out of the cutscene | **371 ms** | **461 ms** |

Why it is now this fast (all three were added in 7c and are what the numbers reflect):
`FastCinematicEndPoll()` at 15 Hz with `IsPlaying()`; `NoteLocalCinematicEnd()` publishing
the stop inside the same packet with a wall-clock stamp; the stop applied inside
`OnPeerLine` on the receiving side (15 Hz, game thread); and `PeerKeepAlive()` so a local
hold cannot skip the apply (`applyPeer` is `holdSkip`).

Burst frames (PrintWindow, both windows in each shot, ~430 ms apart):
`sheet-r7d-end2.png` — the first frame pair after the stop shows **both** windows back in
the hub (A left, B right), and the later pairs show both back inside the sequence when the
`cinehold` restarts it, i.e. entry and exit are synchronised in both directions.  The burst
spacing (~430 ms) is the same order as the measured lag, so the transition itself falls
between two frames; the numbers above are the measurement, the frames are the visual
confirmation that neither window is left behind.

## 7d.2 The listener's subtitle — VISIBLE on B

The hub's `WBP_Subtitle_Base_C` instance exists but is **not in the viewport** (measured:
`UpdateText` returned 1, nothing drawn).  `ShowSubtitleFText` now:

1. picks a subtitle widget that `IsInViewport()`, and if there is none **creates one**:
   `UWidgetBlueprintLibrary::Create(WorldContextObject=PC, WidgetType=WBP_Subtitle_Base_C,
   OwningPlayer=PC)` → `AddToViewport(30)` (UMG.hpp:2227);
2. `SetupFont()` then `ToggleSubtitle(bIsVisible=true, SubtitleText=<the node's own FText>)`,
   falling back to `UpdateText(FText)` and then to `Text_Subtitle->SetText(FText)`;
3. `SetVisibility(3)` so it is visible without eating input;
4. the FText is the dialogue asset node's own bytes (`UDialogue_SpartaSpeechNode.Text +0xA0`),
   resolved through the selector node; the voice is the node's own audio event.

Measured (`local\lab\20261026-062913-r7d-burst3`, `probe_B_dlg.log`):

```
[B] dlg: CREATED a WBP_Subtitle_Base_C for the listener (class=WBP_Subtitle_Base_C)
[B] dlg: subtitle via WBP_Subtitle_Base_C -> shown (2 instance(s), created=1, inViewport=1, update=1) [Disciple: Welcome back, High Sester.]
[B] dlg: voice AE_VO_HubCultist_GreetGenessa_10_11 -> playing (2D, listener)
[B] dlg: LISTEN node=0 speaker=Disciple line=Welcome back, High Sester. voice=AE_VO_HubCultist_GreetGenessa_10_11 options=2
```

and **on screen**: `wt\r7-dialog\sheet-r7d-subtitle3.png` — four consecutive frames of B's
window, each showing the game's own subtitle line **"WELCOME BACK, HIGH SESTER."** centred
under the crosshair.  Raw frames `dlg-B-00..04.png` in that lab directory.

## 7d.3 A real conversation — the exact refusing condition (stopped here)

`dlgstart` now calls `ActivateDialogue` directly, with the instigator's own flags logged.
Lab `local\lab\20261026-063134-r7d-activate`:

```
dlgstart: AddNarrativeTag(Dialogue.Ruk.FirstTimeTop) ok=1 (the NPC's CheckNarrativeTag gate)
dlgstart: AddParticipant(pawn) ok=1 ; participants 1 -> 1
dlgstart: CheckParticipants -> Success=1 (participants=1)
dlgstart: IDlgDialogueParticipant present: pawn=1 npc=1
dlgstart: instigator flags before ActivateDialogue: WasTriggered=0 WaitForParticipants=0
          bDisablePlayerInput=0 CanBeRepeated=0 SkipParticipantsCheck=1
dlgstart: ActivateDialogue ran=1 Success=0
dlgstart: after ActivateDialogue active=0 asset=- instigator=- ctx=-
dlgstart: StartDialogue ok=1 Success=0
dlgstart: SpartaStartDialogue(participants=[BP_PlayerCharacter_C…]) ok=1
```

**The exact refusing condition:** `BPC_DialogueInstigator_C::ActivateDialogue` — the last
gate before the subsystem — returns `Success=0` even with *every* flag in the permissive
state (`SkipParticipantsCheck=1`, `WaitForParticipants=0`, `CanBeRepeated=0`,
`WasTriggered=0`, `CheckParticipants=1`, the narrative tag added, the interface present on
both pawn and NPC).  The call it makes,
`UDialogueSubsystem::SpartaStartDialogue(Dialogue, Participants, Instigator, …)`, **returns
true and creates no entry in `ActiveDialogues`** — so the refusal is inside the subsystem's
own start path, and the only input it can still refuse on is the dialogue asset's
participant mapping: `UDlgDialogue::ParticipantsData @0xB8` is keyed by participant *names*,
and each key must resolve to a live actor whose
`IDlgDialogueParticipant::GetParticipantName()` returns that name.  Our pawn implements the
interface but is not a participant *of this dialogue*, so the mapping finds nothing and the
subsystem declines silently.  Per the instruction, the probe stops here; the next step
(not done) would be to read `ParticipantsData`'s keys and set the pawn's reported
participant name to the key the asset expects.

Consequence for the round: the vote cannot be demonstrated on a **real** choice node,
because no real conversation can be started in this save.  The vote path itself is proven
by `dlgvotesim` (the shipped resolver, 7/7 on both clients, in every `run-two`) and by the
`"dv"`/`"dr"` wire in the DIALOG verdict.

## 7d.4 Regression

Build after every change: `coop_policy: 112 checks passed` + `Built:`.  The last full
`run-two.ps1` (round 7c change set, sha `faa451155…`) ended **`RESULT: PASS`** with
`DIALOG: PASS`, `CINE: PASS`, `DIALOGVOTE: PASS (7/7 both)`, `DIALOGCODEC: PASS (21/21)`.
The 7d changes are presentation/measurement only (subtitle widget creation, burst harness,
`ActivateDialogue` probe logging); they were exercised by the burst and lab runs above, and
one `run-two.ps1` should be re-run before the merge to re-confirm the suite on the final
DLL.

## 7d.5 Artefacts

| What | Path |
|---|---|
| end-lag burst (both windows, both directions) | `local\lab\20261026-062214-r7d-burst2\`, `…-062913-r7d-burst3\` (`end1-A-*.png`, `end1-B-*.png`, `probe_*_end1.log`) |
| end-lag contact sheet | `wt\r7-dialog\sheet-r7d-end2.png` |
| subtitle frames | `…-062913-r7d-burst3\dlg-B-00..04.png`, `dlg-A.png` |
| subtitle contact sheet | `wt\r7-dialog\sheet-r7d-subtitle3.png` |
| ActivateDialogue probe log | `local\lab\20261026-063134-r7d-activate\probe.log` |
| harness | `run-burst.ps1` (`-Tag`), `run-two.ps1`, `run-lab-test.ps1`, `run-r7-two.ps1` |
---

# Round 7e — the two integration defects (priority add-on) + the 7d proofs on a clean suite

The merged build was verified by `r7-final`; it found two defects in this lane.  Both are
fixed below, and the full suite is green again on the fixed DLL (sha256
`6326db8956ffec5e…`):

```
=== DIALOG: PASS (peerBusySeen=True realConversation=False idlePuppet=True listenLines=True
                 pdNotCharged=True [0.1 -> 0.1] votes=True lockEnded=True)
=== DIALOGVOTE: A PASS dialogue votes: 7 checks, 0 failed ; B PASS dialogue votes: 7 checks, 0 failed
=== DIALOGCODEC: PASS dialog wire codec: 21 checks, 0 failed
=== CINE: PASS (aPlaying=True bReplays=False replayCounter=True idlePuppet=True
                 pdNotCharged=True [0 -> 0] endedBoth=True)
=== PEERHP: PASS peer body: hp=28.500/75.000 (0.380) ... age:0.5
RESULT: PASS
```

(Every other verdict PASSed in the same run: SHELL, WEAPON, PUPVIS, LOOK, ANIM, DAMAGE,
DEATH, WORLD, PROGRESS, QUEST, QUEST2, LIFT, CHEST, STORE_A2, STORE_A2SELFTEST, SPAWN,
MIRROR, PEERHIT, AITARGET, STREAM, NPCFAR, LEDGER, ZONE, PEERDEAD, A, B.)

## 7e.1 Defect A — after a real cutscene both clients stayed in cinematic presentation

**Root cause (mine):** the sequence was ended with `ABP_CinematicPlayer_C::
ForceStopCinematic()` only.  That call kills the sequence *player*; the blueprint's END
path is what restores the presentation (`UnhidePlayerAndWeapons`, `ResetPlayerMovement`,
`ShowHUD`, `HandleEndCameraFade`, `RemoveCutsceneState`) and, above all, the camera: the
sequence leaves its own state on the CameraStateFramework stack and the view target stays
on the leftover `CineCameraActor` (`pToBody=4006` vs `226`), which is exactly what
`r7-final` measured until a death/respawn.

**Fix (`RestoreCinematicPresentation`, `coop_dialog.cpp`), run on EVERY end path —
natural end (15 Hz poll), `cinestop`, a peer's stop, and the `cinehold` expiry:**

1. the game's own restorers on the cinematic actor, each guarded:
   `RemoveSkipInput` → `UnhidePlayerAndWeapons` → `ResetPlayerMovement` →
   `ClearAnyCameraFade` → `HandleEndCameraFade` → `ShowHUD` → `RemoveCutsceneState`;
2. the pawn: `SetActorHiddenInGame(bNewHidden=false, bPropagateToChildren=false)`, and
   `Mesh->SetVisibility(true, bPropagateToChildren=false)` — **never**
   `bPropagateToChildren=true` (it reveals the weapon debug shapes), plus input back
   (`SetIgnoreMoveInput/SetIgnoreLookInput(false)`);
3. the camera: any state whose class name contains `Cinematic` is removed from
   `PlayerCameraManager.CameraStack` (`RemoveCameraState`), the remaining states are
   retargeted at the pawn, and `SetViewTargetWithBlend(pawn, 0.2)`;
4. the **local hold is released** — the cutscene's hide/unhide makes `Ready()` go
   hard-false for a beat, which arms a 5 s hold and the hold skips `ApplyPeer` (that is
   what made `bh` 3.1 s stale and PEERHP fail; see 7e.3).

**Proof** — `visual-two.ps1 -Cmds 'A=cineplay;A=cinestop'`, deploying the same verb pair
the integrator used.  Both clients log:

```
[A] cine: RESTORED presentation (sequence stop): blueprint fns=7, unhide=2, camera=1
[B] cine: RESTORED presentation (sequence stop): blueprint fns=7, unhide=2, camera=1
```

and the four frames taken 6–18 s AFTER the end (`wt\r7-dialog\sheet-r7e-restore.png`)
show **both windows** back in normal gameplay: own body, own weapon in hand, HUD (counters
top-right, health bars bottom-left), and no sequence camera.  Raw frames:
`local\lab\20261026-065413-r7e-restore\A-after-cmds*.png` / `B-after-cmds*.png`.

## 7e.2 Defect B — the listener could not resolve the dialogue asset ("asset not loaded yet")

**Root cause (mine):** the listener read the line from the *asset* (`net::Find(path)`),
which is only loaded on a client that already touched that dialogue.  `r7-final` measured
`asset not loaded yet: …DLG_HubCultist02_Greeting…`, `listenLines=False` and DIALOG
failing 2/2.

**Fix:** the owner now publishes the line ITSELF in `"dl"` — the section grew from 6 to 9
tokens: `[active,seq,node,opts,"name","path","speaker","line","voice"]` (the marker keeps
the existing keys in place; the wire is split, so the ~400 extra bytes ride in `rest`).
The owner resolves them from the live speech node it already has; for the `dlgfake` trigger
(no local conversation) it publishes the real node's line when the asset resolves, else the
constant test line.  The listener:

1. uses the wire's `speaker/line/voice` FIRST and only falls back to the asset (the asset
   is still used for the option labels, which the wire does not carry);
2. builds the subtitle FText from the asset's own FText when it is loaded, else from the
   wire text (`FName → Conv_NameToString → Conv_StringToText`), so the subtitle works cold.

**Proof** — the same full run, with no prior dialogue on the listener:

```
[A] dlg: LISTEN node=0 speaker=Disciple line=Welcome back, High Sester.
        voice=AE_VO_HubCultist_GreetGenessa_10_11 options=2
[A] dlg: subtitle via WBP_Subtitle_Base_C -> shown (2 instance(s), created=0, inViewport=1, update=1)
=== DIALOG: PASS … listenLines=True … (lines:2)
```

`lines:2` on a cold listener is exactly what `r7-final` could not get.

## 7e.3 The fallout this exposed (and the fix): a cutscene end starved `ApplyPeer`

Clearing the hold was not enough on its own: `Ready()` re-arms it on the next tick while
the game finishes the unhide, so both clients skipped `ApplyPeer` for ~3 s right after the
cutscene — measured as `age:3.1` on **both** sides' `bh`, which is precisely the PEERHP
window (`PEERHP: FAIL (received=False)`, twice).  Fix in the never-skipped 15 Hz step:
`dialog::PeerKeepAlive()` now also re-applies the friend's `"bh"` **only when the sample is
actually stale** (`body::PeerAge() > 1.5 s`, at most every 0.5 s), so the healthy path and
the `pd` ordering PEERHIT depends on are untouched.  Result: `PEERHP: PASS … age:0.5`,
`pup:0.380`.

## 7e.4 Diff summary (round 7d + 7e)

| File | Change |
|---|---|
| `src/coop_dialog.cpp` | `RestoreCinematicPresentation()` (7 blueprint restorers, unhide without propagation, Cinematic camera-state removal + `SetViewTargetWithBlend`, input back, hold release) wired into every end path; line fields in `"dl"` (speaker/line/voice) + listener prefers the wire; `TextFromString` fallback for a cold subtitle; `PickSubtitleWidget()` + `CreateSubtitleWidget()` (`UWidgetBlueprintLibrary::Create` + `AddToViewport` + `SetupFont` + `ToggleSubtitle`/`UpdateText`); `WallMs()` end stamps and the `cinesync=lag:` status line; `PeerKeepAlive()` (dialog state always, stale `bh` repair); verbs `cineplay/cinestop/cinehold/pdreset/holdclear/dlgvote/dlgvotesim/dlgselftest` |
| `coop_state.h` | `LockState::elapsedMs`, `State::peerPath/peerSpeaker/peerLine/peerVoice`, the verb declarations, `NoteApplyAt`/`PeerKeepAlive` |
| `src/coop_net.cpp` | `"dlgPeer"` never-skipped 15 Hz keep-alive step, `ApplyPeer` marks its clock, `pdreset`/`holdclear`/`cineplay`/`cinestop`/`cinehold`/`dlgvote`/`dlgvotesim` dispatch |
| `run-two.ps1` | DIALOG drives `dlgstart` → `dlgfake` with the real asset; CINE drives a real `cineplay`/`cinestop`; both blocks end with `pdreset` + `holdclear` so the following PEERHP window is clean |
| `run-burst.ps1` (new) | `cinehold` on BOTH clients, `cinestop` from either side, 10 shot-pairs ~430 ms apart across the stop, then the `dlgfake` subtitle frames; `PrintWindow` capture (this is what fixed the earlier "screenshots of the editor" bug — the clients are parked off-screen, `CopyFromScreen(0,0)` was grabbing the desktop origin) |

## 7e.5 Artefacts (round 7d + 7e)

| What | Path |
|---|---|
| end-lag measurements | `local\lab\20261026-062214-r7d-burst2\`, `…-062913-r7d-burst3\` (`probe_*_end1.log`) |
| end-lag sheet (both windows) | `wt\r7-dialog\sheet-r7d-end2.png` |
| subtitle frames + sheet | `…-062913-r7d-burst3\dlg-B-00..04.png`, `wt\r7-dialog\sheet-r7d-subtitle3.png` |
| presentation restored after a real cutscene | `local\lab\20261026-065413-r7e-restore\` (`A/B-after-cmds*.png`), `wt\r7-dialog\sheet-r7e-restore.png` |
| ActivateDialogue probe | `local\lab\20261026-063134-r7d-activate\probe.log` |
| final green run | the `run-two.ps1` output quoted at the top of this section (DLL `6326db8956ffec5e…`) |