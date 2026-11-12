# Quest and story-progression map — every chain, quest-log and not

Date: **2026-11-09**. Build studied: `5.6.1-0+++Sparta-Depot+Day1+CL92086-MortalShell2`.
Purpose: the map the co-op sync needs. It lists every progression chain the game actually
carries, what game object owns each step, and whether the mod syncs it today.

Evidence labels used on every row:

| tag | meaning |
|---|---|
| `[GAME]` | read from the dumped objects/classes/assets (`research\raw`, `research\slices`, `Dumper7`) |
| `[RUN]` | observed in a real run log (`local\lab\*`, `local\r9main\*`) |
| `[DOC]` | stated by an existing repo report (HANDOFF, R9, NATIVE-PORT) |
| `[WIKI]` | wiki/guide claim, **not** proven from the game files (`research\wiki`, `R9-GAP-WIKI`) |
| `[INFERRED]` | conclusion from the above, not directly measured |
| `[UNKNOWN]` | no evidence yet |

Sync column: `YES` = implemented and verified; `PART` = implemented but unverified or
partial; `NO` = not implemented; `N/A` = per-player by design.

---

## 0. Verdict in one paragraph

The **quest-log** side is fully enumerated (exactly 6 rows in `DT_QuestData1`) and its state is
synced generically by quest id, stage index, objective id/count and turned-in flag. The
**step map** is not complete: the DataTable's stage arrays are never read at runtime, only one
row (`TielsQuest`) has a proven second authored stage, and the R9 "stages 3–5" walk of
`GorfQuest` was the direct-write fallback over empty slots. The **larger story** is not in the
quest log at all: it is carried by narrative tags, the narrative-events map, NPC relationship
counters, dialogue assets and ~50 `BPSO_*` save objects. The mod syncs the tag map, event map,
relationship map, `PlayerChoice` and many world flags; it does **not** sync shell-memory
progress, tutorial flags, landing-area/fast-travel state, map objectives, shop stock, or NG+.
The exact post-prologue tag/event vocabulary is still unknown because the lab save is the
prologue and a further-progress save was never provided.

---

## 1. Where progression actually lives

### 1.1 State carriers and today's sync

| What it holds | Owner object (runtime) | Persisted in | Sync today | Wire key |
|---|---|---|---|---|
| Quest definitions (6 rows) | `DT_QuestData1` (`QuestDetails`/`QuestStage`/`QuestObjectives`) | asset, read-only | N/A (static) | — |
| Active quests | `BPC_QuestLogComponent_C:CurrentActiveQuests` | `BPSO_QuestLogComponent_C` | **YES** | `a|<id>` |
| Completed quests | `:CompletedQuests` | same | **YES** | `c|<id>` |
| Tracked quest | `:CurrentTrackedQuest` | same | **YES** | `k|<id>` |
| Quest stage | `UQuestObject:CurrentQuestStage` + `CurrentStageDetails` | recreated from the log | **YES** | `s|<id>` (index+1) |
| Objective counters | `:CurrentQuestObjectiveProgress` (`TMap<FName,int32>`) | same | **YES** | `o|<id>|<stage>|<obj>` |
| Turned-in | `BP_QuestObject_C:IsTurnedIn` | same | **YES** | `u|<id>` |
| Story tag container | `:NarrativeTags` (`FGameplayTagContainer`) | `BPSO_QuestLogComponent_C:NarrativeTags @0xc0` | **PART** | `t|<tag>` (presence, add/remove) |
| NPC disposition | `:NPCRelationships` (`TMap<FString,int32>`) | same | **PART** | `r|<npc>` |
| Narrative-event counters | `:NarrativeEvents` (`TMap<FString,int32>`) | same | **PART** | `e|<event>` |
| Global branch tag | `:PlayerChoice` (`FGameplayTag`) | same | **YES** | dialogue outcome `ct` |
| Dialogue in flight / choices | `DlgContext`, `DialogueSubsystem.ActiveDialogues` | none (transient) | **PART** | `dlg`, `dlgchoice` |
| Shell memory progress | `BPSO_ShellMemories_C:MemoryProgress @0xb8`, `:MemoriesWatched @0x108` | save object | **NO** | — |
| Tutorials shown | `BP_SpartaPlayerSaveObject_C:ShownTutorials @0x640`, meta save `@0x80` | player save | **NO** | — |
| Landing areas / fast travel | `BP_SpartaPlayerSaveObject_C:UnlockedLandingAreas @0x550`, `:CleansedLandingAreas @0x5a0` | player save | **NO** | — |
| Map objectives / pins | `BPSO_WorldMap_C`, `BPSO_WordMapObjective_C` (5 bools) | save objects | **NO** | — |
| Shop stock | `BPSO_ShopData_C:CachedItemsMap @0xb8` | save object | **NO** | — |
| Boss / gate / objective flags | ~50 `BPSO_*` classes (see §6.4) | save objects | **YES/PART** | store A `ws`, store B `ws2` |
| Area identity / completion | `SpartaAreaSubsystem` (`ActiveArea`, `CompletedAreas`) | **no save object found** | **NO** | — |
| Run state / event instances | `SpartaRunManagerSubsystem` (`RunStateIntegers`, `AlreadySelectedEventInstances`) | UNKNOWN | **NO** | — |
| Run completion score | `SpartaCompletionSubsystem` | none | **NO** | — |
| Achievements | per-account | Steam | **N/A** | — |

`[GAME]` for the objects/offsets (`research\20-quests-progression.md` §1.3, §7–10;
`20-quests-progression-part2.md` §9–11); `[DOC]` for the wire keys (`coop_progress.cpp`,
`coop_dialog.cpp`, `coop_world.cpp`, `coop_world_ws2.cpp`).

### 1.2 Quest-log wire keys in detail

Published every 2 Hz by `WqScan` (`native\src\coop_progress.cpp:1318`):

* `a|<QuestID>` — in `CurrentActiveQuests`
* `c|<QuestID>` — in `CompletedQuests`
* `k|<QuestID>` — `CurrentTrackedQuest`
* `s|<QuestID>` — `CurrentQuestStage + 1` for every live `BP_QuestObject_C`
* `o|<QuestID>|<stage>|<ObjectiveID>` — non-zero entry of `CurrentQuestObjectiveProgress`
* `u|<QuestID>` — `IsTurnedIn`
* `t|<Tag>` — one key per entry of `NarrativeTags`
* `r|<NPC>` — one int per `NPCRelationships` entry
* `e|<Event>` — one int per `NarrativeEvents` entry

Presence keys (`t`, `a`, `c`, `k`) travel as `1` / `-1`; `r`/`e` are ints. Apply order is map
order; the R9 fix lets a late joiner adopt a turned-in quest's stage/counters without replaying
the reward path (`WqApplyQuest`, `coop_progress.cpp:1524-1733`). `QUESTSTAGE_OFF.txt` restores
the pre-fix behaviour.

### 1.3 The non-quest-log carriers

Three different mechanisms carry "story", and they are easy to confuse:

1. **Narrative tags** (`NarrativeTags`, a gameplay-tag container). Written by
   `BPC_QuestLogComponent_C:AddNarrativeTag/RemoveNarrativeTag`, by world triggers
   (`BP_Trigger_AddNarrativeTag`) and by dialogue events (`BP_DE_AddNarrativeTag`). Read by
   dialogue (`BP_DC_NarrativeTagCheck` / `_DoesNotHaveTag`), by level selection
   (`Con_CheckNarrativeTags`) and by item fragments (`IF_AddNarrativeTag`). Reset by
   `ResetForNewGamePlus` against a retain whitelist. **This is the main story-flag system.**
2. **Narrative-event counters** (`NarrativeEvents`, string→int) plus a separate
   `BPC_NarrativeEventsTracker` timing map (`BPSO_NarrativeEventsTracker_C`,
   `TMap<GameplayTag,F_NarrativeEventFloatTracking>`). The counters sync as `e|`; the **timers
   do not**.
3. **Dialogue** (976 `DlgDialogue` assets). Dialogue is where most named chains branch: nodes
   run `BP_DE_*` events (add tag, give item, broadcast global event, open another dialogue) and
   are gated by `BP_DC_*` conditions (narrative tag, item, boss defeated, gold, shell, weapon).
   `[GAME]` (`20-quests-progression.md` §6.5–6.6).

---

## 2. Quest-log quests — `DT_QuestData1`

### 2.1 Census

`[RUN]` `qlist` returns exactly **6 rows** (twice, on two installs):

```
TestQuest
LocationQuest
AutoQuest
VlasQuest
GorfQuest
TielsQuest
```

`[GAME]` `DT_QuestData1` is the only quest DataTable in the build; there is no `DT_QuestData2`
and no quest DataAsset (`research\slices\qp-datatables.txt:219`, `qp-dataassets.txt:477`).
`AddNewQuest` accepts arbitrary names, so the log can hold ids that are not rows
`[DOC]` (`HANDOFF.md:354`).

### 2.2 Per-quest cards

All text below is read from the live `CurrentStageDetails` and the journal widgets, so it is the
game's own text `[RUN]` (`local\lab\20261102-121119-queststages`, `...-163306`, `local\r9main\g-questA.log`).

**`TestQuest`** — developer test row.
| stage index (display) | stage name | description | objectives |
|---|---|---|---|
| 0 (1) | `Chapter 1: It Begins` | `The First Chapter of the Quest.` | `TQ_Stage_01` ×2 "Interact With Objects"; `KillBrigand` ×1 "Kill the Brigand"; `BP_QuestLocation3` ×2 "Reach Location" |
| 1 (2) | terminal | — | all complete, `turned=1` |

**`LocationQuest`** — location/system quest.
| index | name | description | objectives |
|---|---|---|---|
| 0 | `Stage 1` | `Steppeth thy feet on the holy ground` | `ReachLocation` ×1 "Reach the Location" |
| 1 | terminal | — | `1/1`, `turned=1` |

**`AutoQuest`** — auto-triggered location quest.
| index | name | description | objectives |
|---|---|---|---|
| 0 | `Chapter 1: It Begins` | `Reach the nearby location` | `BP_QuestLocation` ×1 "Reach the Location" |
| 1 | terminal | — | `1/1`, `turned=1` |

**`VlasQuest`** — hub quest (Vlas).
| index | name | description | objectives |
|---|---|---|---|
| 0 | `Chapter 1: Ill-Gotten Gains` | `First you must find Vlas's wares so he can reest…` | `VlasQuest_Item_Ch_01` ×3 "Retrieve Vlas's Wares", class `BP_QuestObjective_Interact` |
| 1 | terminal (same name, `IsComplete=1`) | — | `3/3`, `turned=1` |

**`GorfQuest`** — hub quest (Gorf).
| index | name | description | objectives |
|---|---|---|---|
| 0 | `Chapter 1: What Ales You?` | `Gorf has asked you to bring him an ale to sate h…` | `GorfQuest_Item_01` ×1 "Get Gorf an Ale", class `BP_QuestObjective_Interact` |
| 1 | empty name/desc/objectives | — | — |
| 2–4 | **synthetic**, produced by `qnext`'s direct `CurrentQuestStage` write | empty | empty |

The R9 report's "stages 2 to 5" are that direct-write fallback over empty slots. They are **not
authored content**, and the report's multi-stage claim for `GorfQuest` should be read as
"index sync was exercised", not "four stages were mapped". `[RUN]` + `[INFERRED]`
(`local\r9main\g-questA.log:19,25,31` — the three `direct set` lines; `qnext` guard at
`coop_progress.cpp:3235-3254`; `HANDOFF.md:382`).

**`TielsQuest`** — the only row with a proven second authored stage.
| index | name | description | objectives |
|---|---|---|---|
| 0 | `Chapter 1: The Request` | `Reconvene with Genessa` | `TQ_Obj_MeetGenessa` ×1 "Reconvene with Genessa", native `QuestObjective` |
| 1 | `Chapter 2: The Letter` | `Deliver the Letter to Young Genessa` | `TQ_Obj_DeliverLetter` ×1 "Deliver the Letter to Young Genessa" |
| 2+ | `[UNKNOWN]` — never walked past 2 | | |

### 2.3 What the quest table still hides

1. **Authored stage count per row.** `QuestDetails.Stages` reads `0` on the live object and
   `GetQuestDetails` cannot be called with params from Lua, so no run has ever known how many
   stages a row has `[DOC]` (`HANDOFF.md:372`). The `qstage` header prints `stage=<cur+1>/0`.
2. **Row-level fields**: `QuestName`, `LogDescription`, `TrackingDescription`, `bIsMainQuest`,
   `bAutoAccept` — all read empty on the live object (`qn='' journal='' track='' stageNames=[]`).
3. **Reward payload**: `XPReward`, `ItemRewards`, `CurrencyReward`, `EnemyRewards`,
   `RelationshipReward`. Sync consequence: the "peer ahead → hear objectives" path lets the game
   apply rewards locally, while the direct stage/counter write skips them, so rewards can
   diverge depending on which path fires `[DOC]` (`HANDOFF.md:376`).
4. **Optional/bonus flags**: `bOptional`/`bBonusXP` are read to filter objective hearing but are
   never sent, so the peer cannot know an objective was optional or bonus-XP.
5. **Quest-object classes**: only `BP_QuestObjective_Interact_C` and
   `BP_QuestObjective_KillBrigands_C` plus the native `QuestObjective` are known. The table is
   data-driven (`ObjectiveClass`), so other rows may use classes not seen yet.

---

## 3. Story progression that is not in the quest log

### 3.1 Prologue (the only chain observed in a real save)

`[RUN]` the test save's `NarrativeTags` contains these nine keys (they appear verbatim in the
`t|` sync log, so they are also the vocabulary the sync actually moves):

| tag | what it records | evidence |
|---|---|---|
| `Prologue.HarrosClaimed` | Harros Shell claimed in the prologue | `[RUN]` |
| `Prologue.FlowerCrownRecieved` | flower crown received (prologue ritual) | `[RUN]` |
| `World.Prologue_Post_Intro` | world state after the intro sequence | `[RUN]` |
| `Dialogue.Cultist.TowerIntro` | cultist tower intro dialogue seen | `[RUN]` |
| `Dialogue.NPC.Shellkeeper.IntroComplete` | Shellkeeper (Zhirelle) intro done | `[RUN]` |
| `Dialogue.Ruk.TarGolem` | Ruk's Tar Golem dialogue seen | `[RUN]` |
| `Dialogue.Genessa.PrepareForFirstCombat` | Genessa training/first-combat dialogue | `[RUN]` |
| `event.npc.pillowcultist.eggstranding.recievedegg` | pillow-cultist egg event received | `[RUN]` |
| `Character.Unlocked.Map` | map unlocked | `[RUN]` |

Steps represented: prologue wake → claim Harros → flower-crown ritual → tower intro → Shellkeeper
intro → Ruk/Tar Golem briefing → Genessa first-combat training → reach Marrow Keep / map.
`[WIKI]` puts Harros→Marrow Keep in the first hour and the Tar Golem at the end of the prologue
(`R9-GAP-WIKI.md` A.1 stage 0–1). Sync: **PART** — tags are mirrored both ways, but this is the
only tag set ever exercised; nothing later in the game has been observed.

### 3.2 Main quest — the Ova hunt → Unfound Path → Zmey → NG+

`[WIKI]` (`R9-GAP-WIKI.md` A.1; sources: `/mortal-shell-2-unfound-path/`, `/beacons/`,
`/mortal-shell-2-marrow-keep/`, `/mortal-shell-2-endgame-last-boss/`). Every ordering claim is
`[INFERRED]` unless the wiki states it. The main quest is **not** a `DT_QuestData1` row; it is
carried by Ova counts, area/gate flags, boss kills and narrative tags.

| # | step | game state that represents it | sync today |
|---|---|---|---|
| 0 | prologue as Harros, Tar Golem tutorial | `BPSO_TarGolemLevelScript.StoneSealClaimed`; `BPSO_PetrifiedCultistManager.Harros`; prologue tags | **YES** (store A) |
| 1 | wake in Marrow Keep as the Harbinger | `World.Prologue_Post_Intro`; landing-area unlock | **PART** (tag yes, player landing area no) |
| 2 | Siphon Ova at the Gloom Siphon | Ova is an item stack (per player) | **N/A** (per player) |
| 3 | ~5 further Ova → Mether's Breath | Ova count / `ID_MethersFlesh` path; `WBP_LTMP_MethersBreath` tutorial flag | **PART** (tutorial flag no) |
| 4 | clear Corrupted Gates (6; 3 beacons + boss + Ovum each) | `BPSO_BossGate.Unlocked`; `BPSO_LandingArea_Boss.BeaconRisen`; `BPSO_FC_CorruptionCore.Cleansed`; `BPSO_TarGate.*`; `BPSO_Ovum.*` | **YES/PART** (flags yes; arena entry no) |
| 5 | ~160 Ova → talk to Milos → Unfound Path | Ova count (per player); dialogue with `BP_UnderMether_Ghost`; `GlobalEvent_...` | **PART** (state via tags/dialogue; count not shared) |
| 6 | three guardian routes: Malborn Offspring / The Monolith / Orrem | boss health/death (`npc`/`nhp`/`dead`), `BPSO_BossGate.Unlocked` | **YES** (health/death/gates) |
| 7 | sever the Corrupted Ovum behind each guardian | `BPSO_FC_CorruptionCore.Cleansed` | **YES** (store A) |
| 8 | Hidden Nave beacon; torch; Endless Core chest (+16 cap) | `BPSO_EvilStatue.*`; `BPSO_FC_TarRoots.*`; chest store A | **YES/PART** |
| 9 | craft Seedbearer's Scripture from three Glands (Depleted/Thawed/Faded), at night | item picks `ID_SacredGland_Fire/Snow/Cathedral`; `ID_PP_UniqueScripture`; night state | **PART** (items local; night not synced) |
| 10 | kill Zmey, the Unbidden | `BPSO_FC_FinalBossManager.EndStrandingCompleted`, `AllCorruptionCoresClensed`, `ClensedCores`/`TotalCoresNeeded` | **YES** (store A/B) |
| 11 | ending; Send Ova starts NG+ | `GlobalEvent_CreditsFinished`; `ResetForNewGamePlus`; `SendOva` | **NO** (dangerous, see §3.6) |

### 3.3 Named NPC side quests

These are dialogue/narrative-tag chains, not quest-log rows. The wiki publishes every step for
only two of them (`Heart of Vatra`, `Sester's Censer`) `[WIKI]`.

**Heart of Vatra — Gragu** `[WIKI]` (`/shells/gragu/`, `/mortal-shell-2-all-shell-locations/`)
1. Get Mether's Breath (fast-travel unlock point).
2. Talk to Gragu in the One-Legged Wolf Tavern.
3. East along the cliff, cross the chasm, climb the temple grounds.
4. Take the Heart from the corrupted statue and survive the petrified enemies.
5. Return it to Gragu (he dies).
6. Claim the body → Gragu Shell + `Heartless`.

Game objects `[GAME]`: `BP_NPC_Gragu_C`, `ID_Attachable_Heart_C` / `BP_Attachable_Item_Heart_C`,
`GlobalEvent_ShellUnlocked`, Gragu memory folders. No dedicated `BPSO_*` was found for the chain;
the step state is tags + item ownership. Sync: **PART** (tags/items; shell unlock is per-player;
item hand-in is local — S67).

**Rickart's Journal — Ruk** `[WIKI]` (`/mortal-shell-2-new-game-plus/`, hotfix notes)
1. Obtain the Journal.
2. Deliver it to Ruk (he stands at the top of Marrow Keep after the Rotten Autumn fix).
3. Quest resolves; NG+ lockout was fixed 6 Sep.

Game objects `[GAME]`: `GlobalEvent_RukDialogueTrigger`, `GlobalEvent_RukLauncherDlg`,
`GlobalEvent_SpawnRuk_TielShellUnlock`; `BP_NPC_Rickart_SAM1/3/4` memory actors. Sync:
**PART** (tags; item hand-in local).

**Sester's Censer — Genessa** `[WIKI]` (`/mortal-shell-2-all-shell-locations/`)
1. Outskirts of Mammon → jump east.
2. Interact among the red specters.
3. Beat Sester Secundus.
4. Take Sester's Censer.
5. Give it to Genessa in Marrow Keep → Genessa Shell + bond memories.

Game objects `[GAME]`: `GlobalEvent_TombZone_GenessaUnlock_Complete`,
`BP_GlobalEvent_GM3_S3_TakeCenserDialogueEnded`, `BPSO_GenessaSensor.SensorClensed`,
`BPSO_EvilStatue.*`, the `TheCenser` memory folder (17 assets). Sync: **PART** (sensor/statue
flags yes; shell/memories per player; hand-in local).

**Baghead — Moonshine + throne** `[WIKI]` (`/mortal-shell-2-new-game-plus/`)
1. Accept the moonshine quest (dialogue).
2. Collect/finish the moonshine objective.
3. Complete the dialogue sequence.
4. Accept the throne → achievement `Baghead`.

Game objects `[GAME]`: `Dlg_Baghead_Interact_MoonshineQuestStart/Main/Complete`,
`GlobalEvent_BagheadMoonshineQuestStart`, `GlobalEvent_BagheadQuestComplete`,
`GlobalEvent_Baghead_Ending`, `GlobalEvent_Baghead_EndingLeaves`,
`BP_CustomPickup_BagheadMoonshine` (a `SpartaSave` component at `0x310`), `ID_BagheadMoonshine`,
`BP_Behavior_UnlockBagheadMask`. Sync: **PART** (tags/global events; the custom pickup is not in
store A, so its own flag is not copied; mask unlock is per-player).

**Smert — blood offerings** `[WIKI]` (`/mortal-shell-2-all-shell-locations/`)
1. At Prophet's Rest, "Make Blood Offering" at every circle.
2. Each offering throws you out of your Shell; reclaim it before the next.
3. Reward: Smert Shell.

Game objects `[GAME]`: `BP_Interaction_RitualUnlockSmertOffering`, `ID_KS_SmertBrassKnuckles`,
Smert memory folders (6 sequences). Sync: **N/A** (per-player shell; the ritual interaction is a
world actor, not a watched save flag).

**Thestus — Night Mode** `[WIKI]` (`/mortal-shell-2-marrow-keep/`, `/mortal-shell-2-night-mode-guide/`)
1. Carry the Gloombound Flame from the Great Arbiter of Flesh (south of Widow's Overlook) to the
   lantern on top of Marrow Keep.
2. Talk to Thestus → Night Mode.

Game objects `[GAME]`: `BP_Pickup_Thestus_Flame` / `ID_ThestusFlame`,
`BP_Interaction_ThestusLamp`. Sync: **NO** (night mode itself is not synced — S74).

**Milos → Unfound Path** `[WIKI]` (`/mortal-shell-2-unfound-path/`)
1. Collect ~160 Ova.
2. Talk to Milos / the Under-Mether ghost beneath Marrow Keep.
3. Unfound Path opens.

Game objects `[GAME]`: `BP_UnderMether_Ghost`, `BP_NPC_Ghost_Undermether*`,
`BP_Interaction_Ghost_Ovum*`. Sync: **PART** (dialogue/state; Ova count per player).

**Egon / Lazlo — "Feed Me"** `[WIKI]` (achievement decode)
1. Offer Egon Gloom until he passes away.
2. Unlock Lazlo.

Game objects `[GAME]`: `BPSO_Egon.GloomDonated` (store B, `kMax`), 115 Egon dialogue assets,
Lazlo memory folders. Sync: **YES** for `GloomDonated`; the shell unlock is per-player.

**Hub / vendor / tutorial chains**
| NPC / chain | game object | sync |
|---|---|---|
| Shellkeeper (Zhirelle) bonding | `BP_NPC_ShellKeeper_C`; `BPSO_Progression.BondingPoints` | **N/A** (per player) |
| Blacksmith gear | `BP_Blacksmith_C`; shop/progression | **N/A** |
| Merchant Merrick (maps, fragments) | `BP_NPC_Merrick_PM*`; `BPSO_ShopData` | **NO** (shop stock) |
| FrogMomma potions | 56 `DlgDialogue`, `ID_GrishaHeart` | **N/A** (items local) |
| Grisha / GrishaHunter | `BP_GrishaEvent_01/02`, `BPSO_GrishaCage.CageOpened`, `BPSO_GrishaGhost.BossDefeated` | **YES** (store A) |
| Tavern arena | `BPSO_TavernArena.WagerPlaced/RewardGiven/ArenaCombatLVL` | **YES** (store A/B) |
| Prophet | `BP_NPC_TheProphet_GRM4_S1` | **PART** (dialogue) |

**Vlas / Gorf / Tiel** are the three named rows in `DT_QuestData1` (see §2.2). They are hub
side-quests; the "quest log" covers them and only them.

### 3.4 Shell memories — the largest non-quest-log story block

`[GAME]` 367 dialogue assets live under `ShellMemories`; the memory **sequences** group into
8 shells and **43** named chains (folder-derived; each folder is one memory with S1/S2/S3
slices):

| shell | memory chains |
|---|---|
| **Tiel** | `AnOldFriend`, `EmberOfRebirth`, `TheBottle`, `TheCure`, `TheRash` |
| **Proxima** | `ALifeForALife_05`, `TheEndeavor_06`, `TheExchange_03`, `TheFinalExchange_07`, `TheWeapon_04` |
| **Eredrim** | `AMercy`, `ImmaculatePath`, `PeacaAtLast` (folder spelling; class names say `PeaceAtLast`), `TheBattle`, `TheFaithful` |
| **Smert** | `TheAtonement_SMM5`, `TheCommunion_SMM3`, `TheConfession_SMM4`, `TheRedemption_SMM7`, `TheRevelation_SMM1`, `TheVision_SMM6` |
| **Gragu** | `PickYourPoison`, `TheChild`, `TheDivine`, `TheParasite`, `TheTavern` |
| **Sariel** | `TheCrow_06`, `TheDead_03`, `TheExperiment_02`, `TheGuardian_05`, `TheResignation_04`, `TheSearch_01` |
| **Sester Genessa** | `HighSesterSanctum`, `ReturnToFallgrim`, `TheCenser`, `TheFuneral`, `TheReunion` |
| **Lazlo** | `TheExecution`, `TheFeast`, `TheJusticiar`, `TheMassacre`, `ThePrisoner`, `TheTruth` |

State: `BPSO_ShellMemories_C:MemoryProgress` (`TMap<GameplayTag,int32>`) and
`:MemoriesWatched` (`TSet<FName>`) `[GAME]`. Trigger path: `BP_ShellMemory_C` +
`GlobalEvent_ShellMemoryHandler_Start/End`, teleport locators and per-memory cinematic cameras
`[GAME]`. Sync: **NO** — memories are per-player. Watching one is a local cinematic; the
`cine` sync covers cutscene playback, not the memory-progress save. Achievements
`Haunting Times` (first memory) and `Seeking the Past` (all memories) are per-account.

### 3.5 Bosses, gates, beacons, areas

**Boss encounters** `[WIKI]` (`/bosses/`, `R9-GAP-WIKI.md` B.1–B.2). 23 named encounters on the
bosses hub; 8 in story order, the rest gate/miniboss encounters.

| order | boss | area | phase note |
|---|---|---|---|
| 1 | Tar Golem | Disciple's Grotto | scripted tutorial; parry → riposte |
| 2 | Magdalena, the Lady of the Woods | Glutted Mire | one phase; cart breaks at half HP, babies once |
| 3 | The Lost Child | Sanguine Caverns | below ~60 %: triple bite / hand strike / jaw inhale |
| 4 | The Nameless Captive | Prisoner's Domain | phase two head spin; 7 parries for `Peter's Perfect Parry` |
| 5 | Hexapod | Withered Shoals | phase two projectiles |
| 6 | Droeg, the Conquerer | Conquered Temple | club → delayed smash → orbs |
| 7 | Sir Isaac, the Scholar-Prince | Faded Citadel | phase two at ~40 %: four swords |
| 8 | Zmey, the Unbidden | Hidden Nave | final; ending → NG+ |
| gate | The Monolith, The Warden, Orrem the Reclaimed, Malborn Offspring, Vrannic, The Wandering Shepherd / Tarblighted Shepherd, Vellen, Larreth, The Silent Sester, Sariel the Endless, Prophet of Profane Infinities, Subjugated Guardian, Bloodcursed Lithopod, Tainted Vestige | per-gate / miniboss | Sariel has two fights; phase two needs four tombstones smashed |

State: health/death (`npc`/`nhp`/`dead`), phase events (`bsp`), `BPSO_BossGate.Unlocked`,
`BPSO_LandingArea_Boss.BeaconRisen`, `BPSO_BossDLManager.CurrentBoss`,
`BPSO_HutchbackBossTrigger.HutchbackDefeated` `[GAME]`/`[DOC]`. Sync: **YES** for health, death,
phase and the gate flags; **PART** for boss ability/projectile FX and phase FX on the non-owner;
**NO** for arena entry / fog-wall pull (S45).

**Beacons and areas** `[WIKI]` (`/beacons/`, Appendix C): 49 beacons total, 46 required for
`So Fresh, So Clean`, 3 start cleansed (Marrow Keep, Widow's Overlook, Outskirts of Mammon).
Areas: Prologue/Disciple's Pass, Fainweald (hub ring), Ruins of Mammon, Glutted Mire, Sanguine
Caverns, Prisoner's Domain, Withered Shoals, Conquered Temple, Faded Citadel, Unfound Path.
Plus 6 Corrupted Gates and 80+ dungeons (15 added by Rotten Autumn) `[WIKI]`/`[DOC]`.

State: **world** flags sync (`BPSO_LandingArea_Boss.BeaconRisen`, `BPSO_TarGate.*`,
`BPSO_GenessaSensor.*`, `BPSO_Ovum.*`); **player** travel records
(`UnlockedLandingAreas`/`CleansedLandingAreas`) do **not**. Sync: **PART** — the beacon may look
cleansed on both, but fast-travel parity is open (S25/S51–53). Zone identity is guarded (`zn`),
but nothing moves a player to the partner's map/dungeon variant (S72/S73).

### 3.6 Endings, NG+, and the irreversible step

`[GAME]`/`[DOC]`:
* `BPSO_FC_FinalBossManager.EndStrandingCompleted` — the ending flag; synced via store A.
* `GlobalEvent_CreditsFinished`; `BPFL_UI_C:PlayCredits(... bIsMainEnding ...)`;
  `BP_CinematicBehavior_StartCredits_C`. Ending cutscenes: `Good_Ending_Master`,
  `Good_Ending_Start`, `Lute_Playing`, `Vista_01..05` (`research\70-flow-menus.md:1319-1326`).
* `SendOva` / `ResetForNewGamePlus(NarrativeTagsToCheckRetain, RetainedNarrativeTags)` —
  starts NG+ with **no confirmation prompt** `[WIKI]`/`[DOC]`.
* `BPSO_CinematicDLUpdate.CinematicFired` — synced (store A).

Sync: **NO** for Send Ova / NG+ (S61/S71 — flagged dangerous). `EndStrandingCompleted` and the
cinematic flag sync, but one player can still start NG+ while the other is mid-run.

### 3.7 Collectibles, achievements, tutorials

* `[WIKI]` 53 achievements; 16 hidden, 11 of them boss kills. Six collectible sets: Map
  Fragments (`Sat Nav`), Lute Tracks (`Heavy Metal`), Ova (`Bring My Ova Back to Me`), Beacons
  (`So Fresh, So Clean`), Tarstones (`Stoned`), Shell Memories (`Seeking the Past`). 8 Shells and
  13 weapons/sidearms are the "Unlock X" set. Completion: `This is Not an Achievement`.
* State: per-account; `Research\wiki\mortal-shell-2-achievements.txt`.
* Tutorials: `BPO_TutorialHandler_C` (`MyTag`, `TutorialList`, `OnComplete`), shown state in
  `BP_SpartaPlayerSaveObject.ShownTutorials` / `BP_SpartaMetaSave.ShownMetaTutorials`. Sync:
  **NO** (per player). Tutorials are also progression gates in practice (Mether's Breath, Ovum
  tutorial, gate/harbinger tutorials via `GlobalEvent_*Tutorial*`).

---

## 4. Chain → state → sync coverage (one row per chain)

| chain | where the steps live | what syncs today | what does not |
|---|---|---|---|
| `DT_QuestData1` rows (6) | quest log stage/objective/turned | stage index, counters, turned, tracked, active/completed; verified one-stage + one 2nd stage | authored stage count, row fields, rewards, optional/bonus |
| Prologue | narrative tags (9 observed) | tag presence both ways | nothing observed after the prologue |
| Main Ova hunt | Ova count + area/gate/Ovum flags + tags | most `BPSO_*` flags, bosses, `EndStrandingCompleted` | Ova count (per player), Milos hand-off, NG+ |
| Heart of Vatra (Gragu) | dialogue + tags + item + shell unlock | tags/items seen by generic systems | hand-in, shell unlock (per player) |
| Rickart's Journal (Ruk) | dialogue + tags + item | tags | hand-in |
| Sester's Censer (Genessa) | dialogue + memory + `BPSO_GenessaSensor`/`EvilStatue` | sensor/statue flags, tags | hand-in, shell/memories |
| Baghead (Moonshine/throne) | dialogue + global events + custom pickup + mask | tags/global events | custom pickup flag, mask (per player) |
| Smert offerings | interaction + shell unlock | tags only | ritual/shell (per player) |
| Thestus / Night Mode | item + lamp interaction | tags/items | night mode (S74) |
| Milos / Unfound Path | Ova count + dialogue | tags/dialogue | Ova count |
| Egon / Lazlo | `BPSO_Egon.GloomDonated` | `GloomDonated` (store B) | Lazlo unlock (per player) |
| Shell memories (43) | `BPSO_ShellMemories` | — | all of it (per player) |
| Bosses / gates | health, death, `bsp`, `BPSO_BossGate`, `BPSO_BossDLManager` | health/death/phase/gates | arena entry, some FX |
| Beacons / areas | world flags + player landing lists | world flags | player travel records, fast travel |
| Endings / NG+ | `EndStrandingCompleted`, credits, Send Ova | ending flag, cinematic flag | Send Ova / NG+ |
| Map objectives | `BPSO_WorldMap`, `BPSO_WordMapObjective` | — | all of it (S69) |
| Shop / vendor | `BPSO_ShopData` | — | all of it (S60) |
| Tutorials | player save `ShownTutorials` | — | all of it (per player) |

---

## 5. What is still unknown, and the probe that closes each hole

Ordered by value for "perfect progression sync":

1. **The complete `DT_QuestData1` row map (stages, objective lists, rewards).**
   Probe: read `UDataTable::RowMap` for `DT_QuestData1` and dump each `QuestDetails` row
   (the struct layout is already known: `QuestDetails @0x8/0x18/0x28/0x38/0x40/0x50`,
   `QuestStage @0x0`, `QuestObjectives @0x0/0x10/0x20/0x28/0x30/0x34/0x35`, size `0x38`).
   This is the only way to get the authored stage count without walking, and it avoids the
   phantom-stage trap in §2.2. `GetDataTableRowNames` already works at runtime; the row values
   are the missing half.
2. **Post-prologue narrative-tag vocabulary.** The sync already publishes every `t|` key it
   sees; the test save only has nine. Probe: walk a late save through one milestone per chain
   and diff the `wqScan`/apply log. Alternative: parse the 976 `DlgDialogue` assets for
   `BP_DE_AddNarrativeTag` / `BP_DE_RemoveGameplayTag` arguments and
   `BP_DC_NarrativeTagCheck` operands (asset JSON, not the object dump).
3. **Narrative-event keys.** `NarrativeEvents` has never been observed non-empty. Probe: same
   late-save diff, or the dialogue-node dump like (2). Note the **timers**
   (`BPSO_NarrativeEventsTracker`) are a separate map and are not synced at all.
4. **Dialogue step graph per chain.** Each `DlgDialogue` asset has nodes with `BP_DE_*` events
   and `BP_DC_*` conditions; that graph *is* the step list for Baghead, Gragu, Ruk, Genessa,
   Smert, Thestus, Milos, Egon/Lazlo and every memory. Probe: dump node list + node events +
   conditions to JSON per asset, then group by NPC folder.
5. **NPC-side quest offer/turn-in state.** `BPC_QuestInstigatorComponent_C` (`Display`,
   `DisplayRewards`, `Interact`) is the object a player interacts with; it reaches the quest log
   through `GetOwningSaveComponent`. Probe: read its owning actor and the quest it offers on a
   real save, and confirm whether the offer/turn-in is a tag, a quest-log entry, or local
   dialogue state.
6. **Shell-memory progress.** Decide whether to sync `MemoryProgress`/`MemoriesWatched`
   (currently per player). If yes, it is a straightforward save-object copy like the quest log.
7. **Landing-area/fast-travel parity.** Decide whether to copy the player save's
   `UnlockedLandingAreas`/`CleansedLandingAreas` (currently not). This is what makes a beacon
   *usable* rather than merely *lit*.
8. **Map objectives (S69) and shop stock (S60).** Both are unwatched `BPSO_*` objects; each is
   a small typed-table addition once the scope (read-only vs shared) is decided.
9. **NG+ / Send Ova.** Add a partner confirmation and a published `ngplus` event before
   anything else; currently one player can reset the run.
10. **Real NPC dialogue start.** Still `StartDialogue Success=0` on the test save, so none of
    the dialogue-driven steps above can be exercised end-to-end until the save/key issue is
    solved (`R9-REALAREA-REPORT`, R9 §3).

---

## 6. Appendix — asset inventory used by this map

### 6.1 Dialogue assets (`[GAME]`, `research\20-quests-progression.md` §6.5)

976 real `DlgDialogue` assets (977 rows minus the CDO). Folder counts:

`ShellMemories 367`, `Egon 115`, `HUB 65`, `FrogMomma 56`, `Vlas 50`, `Gorf 45`,
`Blacksmith 35`, `Merchant 31`, `Ruk 29`, `Genessa 20`, `Tavern 17`, `Intro 16`,
`ImmortalBrigand 15`, `Shellkeeper 15`, `UnderMether 11`, `GrishaHunter 8`, `Thestus 7`,
`Villagers 7`, `Grisha 7`, `Baghead 6`, `Gragu 5`, `Brigands 4`, `CrazedHorde 2`,
`Wormfish 1`, `Dog 1`, `Developers/rscha 29` (test).

Quest-named dialogue assets are only the three Baghead Moonshine files
(`Dlg_Baghead_Interact_MoonshineQuestStart/Main/Complete`); the other chains are named by NPC
folder, not by "Quest".

### 6.2 Dialogue events and conditions (`[GAME]`, §6.6)

`BP_DE_*` (31): `ActivatePlayerAbility`, `AddGameplayTag`, `AddNarrativeTag`,
`AddTagByCharacterID`, `BlockMovement`, `BlockPlayerInput`, `BroadcastGlobalEvent`, `GainItem`,
`GiveGameplayAbility`, `GiveSeal`, `GiveSpecificItem`, `GiveWeaponMap`, `LoseItem`,
`OpenDialogueAsset`, `PlayFacialAnimation`, `PlayMontageFromCollection`, `PlayMontageNPC`,
`PlayMontageNPCOfClass`, `PlayMontageOnParticipant`,
`PlayMontageOnParticipant_WithFollowUpMontage`, `PlaySound`, `PlayerEmote`,
`RemoveGameplayTag`, `RevealSidearmObjective`, `RevealWeaponObjective`,
`SendEventToCustomParticipant`, `SendEventToParticipant`, `SetCameraView`, `SetPlayerWalk`,
`UnblockMovement`, `UnblockPlayerInput`.

`BP_DC_*`: `CheckBossDefeated`, `CheckEgonMood`, `CheckForSpecificShellUnlocked`,
`CheckPlayerGold`, `DoesNotHaveItem`, `HasItem`, `NarrativeTagCheck`,
`NarrativeTagCheck_DoesNotHaveTag`, `PlayerNotShellCheck`, `PlayerShellCheck`,
`PlayerWeaponCheck`.

### 6.3 Global events used by story chains (`[GAME]`, `qp-classes.txt`)

`GlobalEvent_Baghead_Ending`, `_EndingLeaves`, `_BagheadMoonshineQuestStart`,
`_BagheadQuestComplete`, `_CreditsFinished`, `_OvumTutorial_End`, `_RukDialogueTrigger`,
`_RukLauncherDlg`, `_SpawnRuk_TielShellUnlock`, `_TombZone_GenessaUnlock_Complete`,
`_TutorialNotify_Removed_Genessa`, `_GateTutorial`, `_HarbingerTutorial`,
`_LandingAreaUnlocked`, `_RevealObjective`, `_RevealShellObjective`,
`_RevealSidearmObjective`, `_RevealWeaponObjective`, `_ShellUnlocked`, `_SealUnlocked`,
`_WeaponUnlocked`, `_SidearmUnlocked`, `_MaskUnlocked`, `_SkinUnlocked`, `_ClothingUnlocked`,
`_MerchantGiveItem`, `_MakeNPCInteractable`, `_MakeNPCNotInteractable`, `_NPC_MoveTrigger`,
`_TriggerFollowUpDlg`, `_TurnNPCHostile_*`, `_ShellMemoryHandler_Start/End`, and the
per-memory `BP_GlobalEvent_*` entries (Eredrim/Genessa/Gragu/Smert/Sariel/Proxima/Lazlo).

### 6.4 Story-relevant save objects (`[GAME]`, `20-quests-progression-part2.md` §11)

Synced via store A (`coop_world.cpp:107-165`) or store B (`coop_world_ws2.cpp:131-142`):

`BPSO_BossGate`, `BPSO_Gate`, `BPSO_CloseAndOpenGate`, `BPSO_ShepherdSheepGate`, `BPSO_TarGate`,
`BPSO_BossDLManager`, `BPSO_LandingArea_Boss`, `BPSO_FC_FinalBossManager`, `BPSO_FC_CorruptionCore`,
`BPSO_FC_TarRoots`, `BPSO_Ovum`, `BPSO_EvilStatue`, `BPSO_GenessaSensor`, `BPSO_GrishaCage`,
`BPSO_GrishaGhost`, `BPSO_HutchbackBossTrigger`, `BPSO_TarGolemLevelScript`,
`BPSO_CinematicDLUpdate`, `BPSO_EridrimUnlockManager`, `BPSO_NPC`, `BPSO_ShellRevive`,
`BPSO_PetrifiedCultistManager`, `BPSO_TavernArena`, `BPSO_SlayerDungeonChalice`,
`BPSO_Egon.GloomDonated`, `BPSO_ShellGhost.StateIndex`, `BPSO_Tarforge.StoredTarcores`,
`BPSO_WorldMap`/`BPSO_WordMapObjective` (**not** synced), `BPSO_ShopData` (**not** synced),
`BPSO_ShellMemories` (**not** synced), `BPSO_QuestLogComponent` (via `t|`).
Player-save fields **not** synced: `UnlockedLandingAreas`, `CleansedLandingAreas`,
`ShownTutorials`, `ShownMetaTutorials`, `BPSO_Progression.*`, `BPSO_PlayerSession.*`.

### 6.5 Repo evidence files

* `research\20-quests-progression.md` — quest classes, quest log, narrative tags, NPC state.
* `research\20-quests-progression-part2.md` — save objects, DataTables, per-player vs world.
* `research\R9-GAP-WIKI.md` — Appendix A (quest/story chains), B (bosses), C (areas).
* `research\wiki\mortal-shell-2-achievements.txt` — 53 achievements + hidden decodes.
* `ms2-coop\docs\R9-QUEST-REPORT.md` — what was actually exercised.
* `ms2-coop\docs\HANDOFF.md` §15–16 — quest add/complete and stage/counter facts.
* `local\r9main\g-questA.log`, `local\lab\20261102-*\ms2coop-native-*.log` — the observed stage text.
* `ms2-coop\native\src\coop_progress.cpp` — `WqScan` / `WqApply` / `WqApplyQuest`.
* `ms2-coop\native\src\coop_dialog.cpp` — dialogue, choices, `PlayerChoice`.
* `ms2-coop\native\src\coop_world.cpp`, `coop_world_ws2.cpp` — store A / store B tables.
