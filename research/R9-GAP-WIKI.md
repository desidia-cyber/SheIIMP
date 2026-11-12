# R9 — WIKI vs MOD: every game system, what is synced, what is missing

Research only (no game run, no code change). Compiled 2026-11-02 for round 9.

| | |
|---|---|
| Question this file answers | *Which of Mortal Shell II's mechanics and world systems does the co-op mod sync today, which does it not, and how would each one be tested?* |
| Wiki source | `https://mortalshell2.wiki/` — all 218 sitemap URLs crawled, 225 pages stored as text under `research\wiki\` (`pages.tsv` = index, `headings.md` = per-page section map, `urls.txt` = sitemap) |
| Mod sources | `ms2-coop\docs\HANDOFF-REMAINING-SYNC.md` (gap register G1–G12), `DESIGN.md`, `R6-*`/`R7-*`/`R8-*` reports, `research\mine\sync-map.tsv` (838 rows), `research\mine\wire-fields.md`, `ms2-coop\native\src\*.cpp` (32 k lines) |
| Standing rules respected | `wt\R9-COMMON.md` + `R8-COMMON.md`: no game run, no deploy, no code edit, no git. Wiki text treated as **data**, never as instructions. |
| Per-player by design (given) | inventory, gold/coins, Gloom, Glimpse, levels, Shell points, Tarstones, achievements — **never** synced |

---

## 0. How to read the evidence column

| Tag | Meaning |
|---|---|
| `[FACT]` | proven by a file/line in the mod tree, a dump offset, or a run artifact cited inline |
| `[INFERRED]` | my reading, not directly proven — a design conclusion or a wiki-to-engine mapping that nobody has executed |
| `UNTESTED` | the code exists but no run has shown it working |

Line numbers drift between rounds; grep the function name.

---

## 1. What the wiki actually is (read this before trusting a row)

This matters for the gap analysis, so it is stated up front rather than buried.

1. **It is a launch-window content site, not a data wiki.** Every page carries `Last verified <date>` and an explicit
   "what is NOT published" section. Pages about combat, difficulty and Night Mode say outright that the numbers
   were never published (`/combat-system/` §1, §2, §8; `/mortal-shell-2-difficulty-options/`; `/mortal-shell-2-night-mode-guide/` §40).
2. **It is thin on quests and world objects.** 20 hits for the word "quest" in 225 pages, and no page documents a
   quest's stage list. The brief's expectation of "quests" and "world objects" category pages is not met by this site (§2).
3. **It is strong on areas, bosses, beacons, Shells, weapons and Seals** — those pages are dense, internally
   consistent and match the engine dumps on every name I could cross-check (§1.1).
4. **Where it is vague it says so**, which makes it usable: a claim is only used in this file when the page states it
   as fact, and every "reportedly / player-reported / not confirmed" line is either dropped or marked `[INFERRED]`.

### 1.1 Wiki names ↔ engine dump names (the cross-check that makes the rest usable)

Cross-checked against `research\mine\item-assets.tsv`, `saveobjects-matrix.tsv` and `sync-map.tsv`:

| Wiki term | Dump/engine term | Evidence |
|---|---|---|
| Gloom (level-up currency) | `ID_Gloom_C` item stacks | `HANDOFF-REMAINING-SYNC.md` §3 G5 currency list |
| Glimpse (Shell bond) | `ID_Glimpses_C` | same |
| coins (shop) | `ID_Gold_C` | same |
| Ova / Ovum (story progression) | `ID_Ovum_C`, `BPSO_Ovum_C.Cleansed/.OvumCollected` | same; `native\src\coop_world.cpp:138` |
| Tarcore / Tarforge smelting | `ID_Tarcore_C`, `BPSO_Tarforge_C.StoredTarcores` | `coop_world_ws2.cpp:137` |
| Harden (Vatra's Seal) | `GE_Harden_Active`, `GE_Harden_Perfect`, `bInStoneForm`, `GA_Harden_Original_C` | `R8-COMMON`-era research §3 G4 table |
| 49 beacons / checkpoints | `BP_FastTravelPoint_C`, `ASpartaPlayerStart.EligibleForFastTravel`, `BPSO_LandingArea_Boss_C.BeaconRisen` | `HANDOFF` §3 G9; `coop_world.cpp:145` |
| Corrupted Gate | `BPSO_TarGate_C.GateClensed/.PlayerClensed` | `coop_world.cpp:125` |
| Shell Memories / bond | `BPSO_ShellMemories_C.MemoryProgress/.MemoriesWatched` | `HANDOFF` §2.3 (unwatched — see S36) |
| quest log | `BPC_QuestLogComponent_C` (`CurrentActiveQuests`, `CurrentQuestStage`, `PlayerChoice`) | `coop_progress.cpp:50-58` |

That table is the load-bearing assumption of this file: **the wiki's vocabulary is the game's vocabulary**, so a wiki
system can be matched to a sync mechanism. `[INFERRED]` in the sense that nobody has run this mapping in game.

---

## 2. The brief's category pages do not exist — what I used instead

I fetched the main page, followed every link, then pulled the sitemap (`/sitemap-0.xml`, 218 URLs) and crawled all of it.
The site has **exactly four section directories**: `bosses/`, `shells/`, `weapons/`, and `guide/page/1..21` (a paginated
duplicate listing of every article). Everything else is a flat article slug.

The eleven categories named in the brief, tested directly:

| Requested category | HTTP result |
|---|---|
| `/mechanics/`, `/areas/`, `/enemies/`, `/npcs/`, `/quests/`, `/items/`, `/tarstones/`, `/world-objects/`, `/puzzles/`, `/shortcuts/` | **404 each** |

Mapping used instead (`[FACT]`, from `headings.md` + the crawl):

| Requested category | Used instead |
|---|---|
| mechanics | `/combat-system/`, `/mortal-shell-2-beginner-combat-tips/`, `/mortal-shell-2-resolve-not-stamina/`, `/mortal-shell-2-harden-still-works/`, `/mortal-shell-2-seals-guide/`, `/mortal-shell-2-executions-heal/`, `/mortal-shell-2-leveling-system/`, `/mortal-shell-2-shell-abilities-guide/`, `/mortal-shell-2-tarstones-gloom-upgrades/`, `/mortal-shell-2-difficulty-options/`, `/mortal-shell-2-adaptive-difficulty/`, `/mortal-shell-2-night-mode-guide/`, `/mortal-shell-2-turn-off-the-sun/` |
| areas | 7 region walkthroughs: `/mortal-shell-2-glutted-mire/`, `-sanguine-caverns/`, `-prisoners-domain/`, `-withered-shoals/`, `-conquered-temple/`, `-faded-citadel/`, `-unfound-path/` + `/mortal-shell-2-marrow-keep/`, `/mortal-shell-2-first-loop-marrow-keep/`, `/map/`, `/mortal-shell-2-region-map-guide/` |
| bosses | `/bosses/` (index, 23 named) + 25 `/bosses/<slug>/` pages + `/mortal-shell-2-minibosses-list/` + `/mortal-shell-2-endgame-last-boss/` |
| enemies | only via the boss/miniboss tables and the area walkthroughs' enemy notes — **no enemy bestiary exists** |
| NPCs | `/mortal-shell-2-marrow-keep/` (hub roster), `/tiel/`, `/magdalena/`, `/eredrim/`, `/proxima/`, `shells/*` (Gragu's quest), `/mortal-shell-2-methers-severance/` (Zhirelle) |
| quests | no page. Reconstructed from patches + Shell unlock routes + `/mortal-shell-2-unfound-path/` — see Appendix A |
| items | `/weapons/` (16 pages), `/mortal-shell-2-all-weapon-locations/`, `/mortal-shell-2-gloom-farming-guide/`, `/beacons/` (reward table) |
| tarstones | `/mortal-shell-2-tarstones-gloom-upgrades/`, `/mortal-shell-2-sheep-tarstone/`, `/rotten-autumn-tarstone-build-reset/`, `/mortal-shell-2-seals-guide/` |
| world objects | only inside area walkthroughs (bells, lanterns, plates, levers, illusory walls, breakable walls) |
| puzzles | `/bosses/the-wandering-shepherd/` (sheep puzzle boss), area walkthrough sections (three bells, three candles, statue climb, lantern order, pressure-plate blocks) |
| shortcuts | `/mortal-shell-2-map-fast-travel/`, area walkthrough "Shortcut:" lines, `/mortal-shell-2-prisoners-domain/` §5 |
| — | `/beacons/` (**49 beacons**) is the single most important page on the site for co-op and has no counterpart in the brief |

---

## 3. The system inventory and gap table

Every mechanic the wiki documents, one row each. 81 rows in seven groups. "Synced now?" cites the mechanism, not a
promise; "Gap" is what a second player would actually notice.

### A. Presence, body and animation

| # | System | What the game does (page) | Synced now? (evidence) | Gap | Pri | How it could be tested |
|---|---|---|---|---|---|---|
| S1 | Player transform + locomotion | Free movement, no stamina gate (`/combat-system/`) | **Yes.** `x,y,z,vx,vy,vz,yaw,pitch` + `anim,spd` at 15 Hz; `driveTick` snap/ease (`HANDOFF` §1.2; `coop_state.h:176-200`) | — | — | `run-two.ps1` verdict `A`/`B` + a walk-and-sprint visual pair |
| S2 | Shell body (8 Shells, Harros) | Possess a body, each with its own mesh (`/mortal-shell-2-all-shell-locations/`) | **Yes.** `body`,`apart` wire fields; **two different Shells on the two clients VERIFIED** | — | — | `R8-SHELLS-REPORT.md` §5 item 1 (screenshots) |
| S3 | Skins / shades (Rotten Autumn adds shades per Shell) | Cosmetic swap on the Shell (`/mortal-shell-2-rotten-autumn/`) | **Yes.** `LOOK.capture` materials; real material swaps **VERIFIED** | — | — | `R8-SHELLS` §5 item 2 |
| S4 | Clothing / armour pieces | Attachable armour meshes (`/shells/`) | **Yes** (was a real bug, fixed in R8) | — | — | `R8-SHELLS` §5 item 3 |
| S5 | Masks (pumpkin helmet etc.) | Attachable head mesh (`/mortal-shell-2-pumpkin-helmet/`) | **Partly.** Loader verified; mask clause qualified | a mask whose mesh the peer never loaded is the fragile case | Low | `R8-SHELLS` §5 item 5 |
| S6 | Dark form | Second form, extra meshes + VFX | **Partly.** Materials + extra meshes VERIFIED; **VFX NOT POSSIBLE** | dark-form visual effects never appear on the peer | Low | `R8-SHELLS` §5 item 4 |
| S7 | Weapons: 8 melee | One melee weapon equipped at a time (`/weapons/`) | **Yes.** `wpn`,`wsock`,`wpns` (every rendered mesh) | — | — | `WEAPON` verdict |
| S8 | Weapons: 8 sidearms | Ranged slot, ammo (`/mortal-shell-2-sidearms-and-ranged-combat/`) | **Yes** for the mesh; ammo is in `bh` (S10) | — | — | `WEAPON` + `PEERHP` |
| S9 | **Seal slot (4 Seals)** | The block button's verb: Untarnished = Perfect Guard, Infinite = Parry, Slayer = sustain, Vatra = Harden (`/mortal-shell-2-seals-guide/`) | **No.** `"bh"` is a fixed **14-number** array with no slot for it (`coop_body.cpp:1223`); the only seal reference anywhere is a *diagnostic* read of `GetActiveSealItemDef` in `bodyprobe` (`coop_death.cpp:1579`), and `coop_equip.cpp` drives `ChangeShell/Skin/Mask/Clothing/DarkForm*` only — there is no `ChangeSeal` | **the friend's defensive verb differs from yours** — a parry you expect them to make is a guard, or a Harden; unreadable in a fight | **High** | publish the seal's item-def name in `bh`; visual: equip different Seals on each client, screenshot the puppet mid-block |
| S10 | Action / ability montages | Weapon arts, Shell abilities (`Staggering Blow`, `Revered Heart`, `Rekindle`, Tiel's clones, Genessa's doubles) (`/shells/gragu/`, `/mortal-shell-2-shell-abilities-guide/`) | **Yes** for any montage the local pawn plays (`mseq,mpos,mrate,mid`) | abilities that are **not** a montage (a spawned clone/double, a projectile) are not mirrored | Med | `ANIM` verdict; then `shellability` verb on A, screenshot B |
| S11 | Traversal animations (squeeze, vault, break-wall) | Fixed animations on interact (`/mortal-shell-2-prisoners-domain/` §2) | **Yes.** `WALLANIM` verified — the real chain, both sides | — | — | `R8-WALL-REPORT.md` §5.1 |
| S12 | Sheep transformation | Being hit by the Shepherd turns you into a sheep (`/bosses/the-wandering-shepherd/`) | **UNTESTED.** It is a body change → the mesh half should follow `body`; the loss of moveset is local | the peer may see a normal Shell while you are a sheep `[INFERRED]` | Low | force the transform (`cmd` verb), screenshot the puppet |

### B. Combat state of the *other player*

| # | System | What the game does (page) | Synced now? (evidence) | Gap | Pri | How it could be tested |
|---|---|---|---|---|---|---|
| S13 | Health / shell health | Two-tier health; beacon restores it (`/beacons/`) | **Yes.** `"bh"` — health, shell health published at 2 Hz, written into the puppet's attribute sets (`coop_body.cpp:29-42`) | — | — | `PEERHP` verdict (`HANDOFF` §1.3) |
| S14 | Resolve | The ability/energy resource; restores at a beacon (`/mortal-shell-2-resolve-not-stamina/`) | **Yes.** inside `"bh"` | — | — | `PEERHP` |
| S15 | Heal charges | Consumable heal (`/mortal-shell-2-beginner-combat-tips/`) | **Yes.** `HealCurrentCharges` (`coop_body.cpp:810`) | — | — | `PEERHP` |
| S16 | PP resource | Ability resource | **Yes.** `coop_body.cpp:712` | — | — | `PEERHP` |
| S17 | Sidearm ammo | Finite rounds | **Yes.** `CurrentAmmo/MaxAmmo` (`coop_body.cpp:816`) | — | — | `PEERHP` |
| S18 | **Harden / Perfect Guard / Parry held state** | Harden = stone for one hit; guard is held; parry is a window (`/mortal-shell-2-harden-still-works/`) | **No.** `grep Harden` in `native\src` → **0 hits**; `"bh"` carries 14 numbers and none of them is a stone-form or a held-block bit (`coop_body.cpp:1223`) | you cannot see that your friend is hardened, guarding or mid-parry — the three states that decide whether a boss combo is safe | **High** | extend `"bh"` with a state bitfield (`bInStoneForm`, `GA_ActiveBlock_C` held, parry window); visual: A hardens, screenshot B's puppet |
| S19 | Posture / Break meter on enemies | Posture shatters → critical/riposte (`/combat-system/` §2) | **No.** only enemy health is mirrored (`nhp`); posture is not | the friend sees a boss at 60 % health that is actually about to break — the punish window is invisible | Med | add `brk` normalised posture to the `npc` entry; `MIRROR` + a `posture` verb |
| S20 | Executions / critical finishers | Posture break → critical strike; a modifier makes them heal (`/mortal-shell-2-executions-heal/`) | **No** as an event. The montage *would* travel in `mseq` if it is a montage; the health gain would arrive in `bh` | the peer sees an animation but no reaction on the victim | Med | `exec` verb: break posture on A, execute, screenshot B's victim |
| S21 | Hit reactions / flinch | `SpartaApplyHit` payload drives reaction, VFX, strike direction | **No.** mirrored health is a *number write*, which raises no damage event (`R7-BOSS` §1) | enemies you hit do not flinch on your friend's screen; their enemies do not flinch on yours | Med | `R7-BOSS` §1 named this exactly; a `TriggerDamage`/reaction-montage replay is the cheap route |
| S22 | Player death (visible) | Death screen + death animation (`/what-to-do-after-you-die/`) | **Yes.** `ds` in `"bh"`; peer death plays a `DeathsLibrary` clip on the puppet and holds it | — | — | `DEATH` verdict; `R8-DEATHS` |
| S23 | Death → co-op respawn / hold / wipe | N/A in single player; the mod's own rule (`/what-to-do-after-you-die/` says death is not a file wipe) | **Yes.** three-case policy: wipe→host resets, case 2→respawn with debuff, case 3→held during the partner's boss fight (`coop_policy.h:101-162`, 112 unit checks) | — | — | `DEATHCHAIN` verdict + `native\tests\test_coop_policy.cpp` |
| S24 | Gloom dropped on death, recoverable | Death drops Gloom; you walk back and pick it up (`/what-to-do-after-you-die/`) | **Yes**, per player (`R8-MONEY-REPORT.md` §3.1 `EnsureDrop()`) | — (per player by design) | — | `MONEY` / `R8-MONEY` §4 |
| S25 | Respawn point = last cleansed beacon | A beacon "sets your revival point" (`/beacons/`) | **No, deliberately.** It lives in store-B `ActorNames` under `Sparta_Player` (`RespawnPlayerStart`); `R7-MAPS-REPORT.md` §1.2 shows 2 player-owned entries and **no sync was built, by design** | each player respawns at *their own* last beacon — after a hold or a respawn the two players can be in different regions with nothing announcing it | **High** | see S31; publish the respawn point in the zone line and draw it for the friend |

### C. World state, containers and props

| # | System | What the game does (page) | Synced now? (evidence) | Gap | Pri | How it could be tested |
|---|---|---|---|---|---|---|
| S26 | Doors, gates, boss gates, tar gates | Physical blockers opened by a save flag (`/mortal-shell-2-map-fast-travel/`) | **Yes.** 36 store-A kinds incl. `BP_HubDoor_C`, `BPSO_Gate_C`, `BPSO_TarGate_C`, `BPSO_BossGate_C` (`coop_world.cpp:107-165`) | the *live flip* of a gate the peer is standing next to is field-set-only (the actor shows it next load) | Low | `WORLD` verdict |
| S27 | Chests and shared loot | Chests hold gear/tarstones; some need lights or candles (`/beacons/`, `/mortal-shell-2-glutted-mire/` §2) | **Yes.** `wc` lid + shared loot; `CS.grant` | — | — | `CHEST` verdict |
| S28 | Light-locked chest (N lights) | `BP_LockedLightChest_C` counter | **Partly** `[INFERRED]`: the final open state is a save flag; the **per-light counter** has no save (`HANDOFF` §3 G3) | the friend sees a different number of lights lit | Low | `light` verb on A, read the counter on B |
| S29 | Flower chest (hit-count chest) | `HitsReceived/RequiredHits` | **Partly**: open state synced, progress toward breaking not | run-in-progress is invisible | Low | as S28 |
| S30 | Lifts / elevators | Hub lift, wooden elevators, `StartingFloor` | **Yes.** `wl` + typed `BPSO_WoodenElevator_C.StartingFloor` (host wins) | `HasBrokenOnce` and the moving-mesh animation are weaker than the state | Low | `LIFT` verdict |
| S31 | **Moving objects / drawbridges / platforms** | `BPSO_MovingObject_C.MovedToEnd`, `BPI_LiftCall` | **State only.** Flag is in `kKinds`, but `coop_world_ws2.cpp:2134` says `MovedToEnd` is **deliberately not set** because flipping it does not move the mesh; `TriggerMovingActor` is lab-gated | a drawbridge/platform stays in the old position on the peer while the flag says moved | Med | `TriggerMovingActor` under a kill switch; screenshot both sides |
| S32 | Breakables (92 `BP_VFX_*`: barrels, pots, carts) | Destroyed by damage, no save (`/mortal-shell-2-glutted-mire/` §1 explosive barrels) | **Yes** (implemented + `BREAK: PASS`), on-screen before/after **WARN** | visual proof is weak; a prop can be hidden rather than broken | Med | `break-lab.ps1` pair; needs a real `CustomBreak` or accepted hidden-only |
| S33 | Pressure plates | `BP_PressurePlateOpenDoor_C.Opened` | **No.** `grep PressurePlate` → 0 hits | a plate the friend pressed does not open your door | Med | watch `Opened` as a new store-A kind; `plate` verb |
| S34 | Bear traps | Disarmed by the player (`/mortal-shell-2-withered-shoals/` §2) | **Yes.** `BPSO_BearTrap_C.IsDisarmed` | — | — | `WORLD` |
| S35 | Falling statue / falling corpse (shot down) | Shoot to drop (`/mortal-shell-2-prisoners-domain/`) | **Yes.** `BPSO_FallingStatue_C.StatueFallen`, `_FallingCorpse_C.ShotDown` | — | — | `WORLD` + `STORE_A2` |
| S36 | **Keys, locked doors, locked chests** | Keys gate doors/chests; NG+ keeps quest keys (`/mortal-shell-2-new-game-plus/`) | **Yes.** typed union arrays `SavedKeyIDs`, `SavedDoorIDs`, `SavedChestIDs` (`coop_world_ws2.cpp:139-141`) | — | — | `STORE_A2` |
| S37 | Data layers (corruption / cinematic DLs) | 104 layers in the hub alone; DLs gate geometry and props (`HANDOFF` §1.2) | **Partly.** `wd` with 2 applies/tick, 10 s/layer, 15 000 uu gate; **one layer** ever verified | 103 layers unverified; a switch can leave the friend's world in the other state | Med | `LAYERS` + a `dl <name>` verb per layer family |
| S38 | Corruption growth in the overworld | Mether's Breath clears it (`/mortal-shell-2-map-fast-travel/`) | **No.** no growth actor in `kKinds` | the friend's route stays blocked while yours opens `[INFERRED]` | Med | find the growth actor class in `CXXHeaderDump`, watch its flag |
| S39 | Illusory walls / breakable walls / squeeze cracks / vaults | Revealed by a hit or an interact (`/mortal-shell-2-glutted-mire/` §1, `/mortal-shell-2-prisoners-domain/` §5) | **Partly.** the *traversal animation* syncs (`WALLANIM`); the wall's own removal is not a save flag | the friend may see the wall still there after you walk through `[INFERRED]` | Med | `wall` verb, screenshot both sides of the wall |

### D. Enemies, encounters and bosses

| # | System | What the game does (page) | Synced now? (evidence) | Gap | Pri | How to test |
|---|---|---|---|---|---|---|
| S40 | Enemy presence + ownership handover | Ordinary AI enemies roam and respawn at a beacon (`/beacons/`) | **Yes.** `npc` (16 within 4 000 uu), owner/mirror with timeout (`kMaxNpc`, `kMirrorTimeout` `coop_state.h:203-208`) | — | — | `MIRROR`, `NPC`, `NPCFAR` |
| S41 | Enemy health + death | — | **Yes.** `nhp`, `dead` | health only ever *lowers* except the owner's own reset | Low | `DAMAGE`, `DEATH` |
| S42 | Enemy far-cull | The game destroys an enemy no local player is near | **Yes, fixed.** far-cull veto (`coop_farenemy.cpp`, `R8-FARENEMY-REPORT.md` §2.4) | — | — | `R8-FARENEMY` §5.3 |
| S43 | Enemy drops (Gloom/gold/items) | `CharacterDeathData`: `GloomOnDeath`, `GoldOnDeath`, `GoldDropChance`, `ItemDropTable` | **Yes, owner rolls, peer is granted** (`S_AddGold`/`S_AddGloom`/`AddItemByClass`, `coop_drops.cpp:1022`, `:473`) | — | — | `DROPS` verdict |
| S44 | Boss phases (2nd, 3rd) | Phase thresholds by health %, phase abilities + camera + HUD (`/bosses/sir-isaac-the-scholar-prince/` "phase two (around 40 % health)") | **Yes.** post-hooks on `ShouldStartSecondPhase/ThirdPhase`, own key `bsp`, applied with the game's own function (`R7-BOSS-REPORT.md` §2) | — | — | `BOSSPHASE` |
| S45 | **Boss arena entry / fog wall** | Walking into the arena triggers `ApproachingGate()`, the fog wall, the fight starts (`/bosses/`) | **No.** `grep FogWall` → 0; only `BPSO_BossGate_C.Unlocked` (a save flag) syncs | the friend is not pulled into the arena and may not see the fog wall — the fight starts for one player | **High** | call `ApproachingGate()` on the peer's gate; screenshot both sides of the fog |
| S46 | Boss death + reward | Boss drops Ova/Tarstone/unique gear (`/bosses/` "What bosses drop") | **Yes** via S43/S47 (reward granted to each player's own inventory) | double-reward is possible if both clients run their own drop table — owner-rolls avoids it | Low | `DROPS` asserts the peer's delta equals the owner's |
| S47 | Horde encounters / shared-health-bar packs (Gloombound Legion) | Several enemies share one health bar (`/mortal-shell-2-prisoners-domain/` §3) | **Partly.** each enemy is mirrored separately; the *shared bar* is an encounter-local construct | the friend can kill "one" of a shared-bar pack and see no change `[INFERRED]` | Med | spawn the Legion, hit one, read both sides |
| S48 | Spawners / horde triggers | `BP_Spawner_C`, `BP_Spawner_Proximity_C`, `SpartaHordeEncounter.StartEncounter` | **Partly.** only `BP_Spawner_Proximity_C` is replayed (`spawnReplay`, `R6-SPAWN`) | an encounter that fires from a trigger volume only fires on one side | Med | `SPAWN` verdict; `R6-SPAWN-REPORT.md` |
| S49 | Session spawn state | `BP_SpawnerSessionSaveObject_C` (`WasKilled`, `DidSpawn`) | **No** (unwatched, `HANDOFF` §2.3) | the two sides disagree about whether an encounter already fired | Low | add to `kKinds`; `STORE_A2` |
| S50 | Caged / imprisoned enemies | Grisha cage, cage key (`/bosses/` miniboss table) | **Partly**: `BPSO_GrishaCage_C.CageOpened` synced; `OpenType` tag not | — | Low | `WORLD` |

### E. Progression, economy and menus

| # | System | What the game does (page) | Synced now? (evidence) | Gap | Pri | How to test |
|---|---|---|---|---|---|---|
| S51 | **Beacons: cleanse / rest / respawn** (49) | Corrupted beacon = bare checkpoint; cleansing runs a short dungeon and claims an Ovum; a beacon restores health+Resolve, respawns regular enemies, sets the revival point (`/beacons/`) | **Partly.** the **Ovum** itself syncs (`BPSO_Ovum_C.Cleansed/.OvumCollected`), so the world-side reward lands; the **beacon's own cleansed state** lives in the per-player save (`BP_SpartaPlayerSaveObject_C.UnlockedLandingAreas 0x550` / `CleansedLandingAreas 0x5a0`) and is not synced | the two players walk a different beacon network; "So Fresh, So Clean" (46 cleanses) is a solo chore for each | **High** | a `beacon` section publishing `[landingAreaName, cleansed]`, applied to the local player save; `BEACON` verdict + both beacon menus on screen |
| S52 | **Fast travel (Mether's Breath)** | 5 Ova → Mether's Breath turns every cleansed beacon into a fast-travel point; icons added 6 Sep; warnings 13 Oct (`/mortal-shell-2-map-fast-travel/`) | **No.** `grep FastTravel` → 0; `TeleportPlayerWithStreaming` → 0 | one player teleports across the world and the other is left standing; no way to follow | **High** | `BPFL_WorldStreaming:TeleportPlayerWithStreaming` from the CDO (the `HANDOFF` §3 G9 proposal) behind a kill switch; `TRAVEL` verdict |
| S53 | Return to Marrow Keep | A menu item on any cleansed beacon (`/beacons/`) | **No** | same as S52, single destination | High | part of S52 |
| S54 | Bone Portals / Bone Gates / jump gates / Stone Portals | Fixed, one-way or two-way teleports between regions (`/mortal-shell-2-map-fast-travel/` "The travel network") | **No.** these are level-script teleports, not save flags | the friend does not come along through a Bone Portal | Med | publish a sequence id per portal; teleport the peer via the same entry point as S52 |
| S55 | **Level up at beacons (Harbinger stats)** | Spend Gloom for permanent main-stat growth (`/mortal-shell-2-leveling-system/`) | **Not synced, by design** (level is per player) | none intended — but the *beacon menu* difference is visible (S51) | — | assert it stays un-synced in `run-two.ps1` |
| S56 | Shells: body, bond (Glimpse), Shell Points, Memories | 8 Shells; bond raises with Glimpse; Memories are story entries; `Seeking the Past` needs all (`/mortal-shell-2-shell-abilities-guide/`, `/mortal-shell-2-all-shell-locations/`) | **Body/mesh yes** (S2). **Bond/points/memories no** — `BPSO_ShellMemories_C` unwatched, `grep ShellMemor|Bond` → 0 | per-player by design for the numbers; but "same memories watched" is a plausible shared goal and is not synced | Low | decide ownership first, then a `mem` section |
| S57 | Shell severance / respec | `Mether's Severance` from Merrick, 5 000 coins, refunds Glimpse (`/mortal-shell-2-methers-severance/`) | **Not synced, by design** (economy) | — | — | — |
| S58 | Weapons upgrade path / Tarforge | Muradean Actuator + Obsidian Lathe gate upgrades; +16 cap; Endless Core removes the cap; smelting costs Gloom (`/mortal-shell-2-marrow-keep/`, `/mortal-shell-2-unfound-path/`) | **Not synced, by design** for the weapon; `StoredTarcores` (the Tarforge pool) **is** synced host-wins | the *pool* is shared but the *weapon level* is not — worth confirming that split is intended | Low | `wpTest`-style verb on the pool only |
| S59 | Tarstones + Etching Needles | Weapon/sidearm stones; `Stoned` = find them all (`/mortal-shell-2-tarstones-gloom-upgrades/`) | **Not synced, by design** | — | — | — |
| S60 | **Shop (Merrick) stock** | Consumables, maps, and `Rare Findings` (Mether's Severance 5 000) refreshed by progress (`/mortal-shell-2-marrow-keep/`) | **No.** `BPSO_ShopData_C` unwatched (`HANDOFF` §2.3) | the two players see different stock; a map or Severance one can buy the other cannot | Med | `STORE_A2` for `CachedItemsMap`; decide read-only telem vs shared |
| S61 | Ova Siphon → Mether's Breath → Send Ova (NG+) | Siphon Ova; ~5 more earns Mether's Breath; `Send Ova` starts NG+ with **no confirmation prompt** (`/mortal-shell-2-new-game-plus/`) | **No.** `grep 'SendOva\|Send Ova\|NewGamePlus\|NgPlus'` in `native\src` → **0 hits** | **dangerous**: one player can start New Game Plus while the other is mid-run, and there is no prompt to interrupt it | **Med-High** | publish `["ngplus",seq]` and warn the peer; never auto-apply |
| S62 | Milos / Unfound Path unlock (~160 Ova) | Ova count gates the endgame door (`/mortal-shell-2-unfound-path/`) | **No** — Ova count is per player | the friend cannot open the endgame for you, or vice versa `[INFERRED]` | Med | read `BPSO_*` Ova counter, publish it read-only |

### F. Story, quests and dialogue

| # | System | What the game does (page) | Synced now? (evidence) | Gap | Pri | How to test |
|---|---|---|---|---|---|---|
| S63 | Quest log: add/complete/stage/counter/turn-in/tracked | Quests advance by NPC dialogue and world actions (`/mortal-shell-2-methers-severance/` shows a 5-step NPC chain) | **Yes.** `wq` keys `t\|c\|a\|s\|o\|u\|k\|r\|e`; `CurrentQuestStage`, `TurnInQuest` (`coop_progress.cpp:1389,1541,1657`) | **only ever exercised on one-stage hub quests** — `R9` `r9-quest` `TASK.md`: "nobody checked that the partner's stage TEXT is right afterwards, because hub quests have one stage" | **High** | `r9-quest`'s `quest-stages.ps1` / `QUESTSTAGE` verdict — the round-9 work in flight |
| S64 | Dialogue (start, lines, choices) | DlgSystem: `SpartaStartDialogue`, nodes, `ChooseSpeechSequenceOptionFromReplicated` (`/mortal-shell-2-marrow-keep/` NPCs) | **Yes.** `dlg` + choices + `PlayerChoice` written on the peer (`coop_dialog.cpp:1990,2036,544`) | verified only with a forced dialogue in the hub; a **real** conversation by the game's own interaction is `r9-realarea` item 3 | Med | `r9-realarea` `TASK.md` item 3 |
| S65 | `PlayerChoice` (one global story tag) | A single story-wide choice recorded on the quest log | **Yes.** written and logged (`coop_dialog.cpp:544`) | last writer wins — with two players it can flip | Low | `DIALOG` verdict |
| S66 | Quest NPC persistence (greeting, ever-killed) | An NPC greets once; a dead NPC stays dead (`/mortal-shell-2-new-game-plus/` "killed NPCs return" in NG+) | **Yes.** `BPSO_NPC_C.HasTriggeredInitialGreeting/.WasEverKilled` (`coop_world.cpp:150`) | — | — | `STORE_A2` |
| S67 | NPC quests (Appendix A) | Heart of Vatra (Gragu), Rickart's Journal (Ruk), Sester's Censer (Genessa), Baghead's throne | **Partly.** the quest *state* syncs (S63); the **item hand-in** and the NPC's own reactions are local | a hand-in done by one player may leave the other's quest stranded at the previous stage `[INFERRED]` | High | per-quest script driving each stage + a screenshot of both journals |
| S68 | Narratives / narrative tags / events | Story beats flagged as narrative tags | **Yes.** add + removal, verified in game (`HANDOFF` §4.1) | — | — | `QUEST`/`QUEST2` |
| S69 | World map objectives / map pins | `BPSO_WorldMap`, `BPSO_WordMapObjective` (133 live), markers cost 3 Glimpse for a Shell | **No.** both classes unwatched | the friend's map does not show the same objectives/pins | Med | decide read-only; `map` section; both maps on screen |
| S70 | Cinematics | `BP_CinematicPlayer_C:PlayCutscene()`, 52 LevelSequences; `BPSO_CinematicDLUpdate` is a save flag | **Yes.** `cine` + `CINE` verdict; the world effect syncs through store A | — | — | `CINE` |
| S71 | Ending / credits / `EndStrandingCompleted` | Final boss → ending → NG+ (`/mortal-shell-2-endgame-last-boss/`) | **Partly.** `BPSO_FC_FinalBossManager.EndStrandingCompleted` syncs; the ending sequence itself does not | one player sees the ending while the other is still fighting `[INFERRED]` | Med | part of S61 |

### G. Session, world identity and options

| # | System | What the game does (page) | Synced now? (evidence) | Gap | Pri | How to test |
|---|---|---|---|---|---|---|
| S72 | **Zone / area identity** | 6 Corrupted Gates + Ruins/Fainweald + 80+ dungeons; the world is "compact and interconnected" (`/mortal-shell-2-region-map-guide/`) | **Guard only.** `zn` = `"<persistentMap>\|<zoneId>\|<liHash>"`; when it mismatches, world apply **pauses** (`coop_body.cpp:4-27`) | nothing moves the guest to the host's map — a paused sync *is* the gap, silently | **High** | `ZONE` verdict exists; extend it to a `travel` that actually follows (`SelectLevel/LoadLevel` is `Array_Random`-broken, `coop_body.cpp:596-611`) |
| S73 | Level instances (dungeon variants) | `BP_LevelInstanceSelector_C.LoadLevel`, 371 map packages (`HANDOFF` §3 G9) | **No** — `SelectLevel()` picks with `Array_Random` and cannot be steered (`coop_body.cpp:596`) | the friend cannot be brought into the *same* dungeon variant | High | load by explicit soft-object path instead of `SelectLevel` |
| S74 | Night Mode (Thestus + Gloombound Flame) | Light the Flame on Marrow Keep's roof, Thestus calls the night; more health/damage/speed, more Gloom (`/mortal-shell-2-night-mode-guide/`, `/mortal-shell-2-marrow-keep/`) | **No.** `grep Night` → 1 unrelated hit | one player in Night Mode and the other not: different enemy stats in the same world | Med | a world-flag section; assert both `sync.txt` show the same mode |
| S75 | Difficulty modifiers (special items) / adaptive difficulty | No Easy/Normal/Hard menu; special items make a run easier or harder (`/mortal-shell-2-difficulty-options/`, `/mortal-shell-2-adaptive-difficulty/`) | **No.** `grep Difficulty` → 0 functional | asymmetric difficulty in one shared world | Med | same mechanism as S74 |
| S76 | Rotten Autumn content (15 dungeons, 10 Tarstones, shades, Overkill, photo mode, ghost notes) | Free update, Oct 20 2026 (`/mortal-shell-2-fifteen-dungeons/`) | **Not specifically handled.** Loads/streaming are generic; a new dungeon is a new level instance (S73) | the new dungeons are untested in co-op at all | Med | `r9-realarea` in one of the 15 |
| S77 | Photo mode / Overkill | Menu toggles, screenshot-only (`/mortal-shell-2-photo-mode-overkill/`) | **No, and should not be** — cosmetic, local | none | Low | — |
| S78 | Achievements / tutorials | 53 achievements; missable ones flagged (`/mortal-shell-2-achievements/`, `/mortal-shell-2-missable-achievements/`) | **Not synced, by design** (per player) | the friend may earn an achievement you cannot in this session — acceptable | Low | assert un-synced |
| S79 | Pause / menu / idle | Single-player pause is local | **Partly.** `rdy:0` hides the peer body while not in gameplay | no "friend is in a menu" indication on screen | Low | `rdy` gate is already the mechanism; add a HUD line |
| S80 | Save persistence / divergence | Autosave; `WorldState_Backup_0_*` files (`/mortal-shell-2-new-game-plus/` carry-over table) | **No.** the mod never calls `SaveToDisk` (`HANDOFF` §3 G11) | quit/relaunch: only covered keys converge; every gap above diverges permanently | Med | `PERSIST` proposal in `HANDOFF` §5 |
| S81 | Session handshake / version / >2 players | One campaign, no netcode | **No.** relay is a latest-value file pair, no handshake, no version, 2 players max (`HANDOFF` §3 G12) | a build mismatch half-syncs instead of refusing | Med | `hello`/`welcome` with build + save slot + mod version |

---

## 4. Ranked gap summary (what a second player actually notices)

`[INFERRED]` in ordering: it is my judgement from the wiki's own emphasis (beacons and fast travel get four pages
between them) plus the mod's own phase ordering in `HANDOFF` §6.

| Rank | Gap | Rows | Why this order |
|---|---|---|---|
| 1 | **Beacon network is per player** (cleanse, respawn point, fast travel, return to keep) | S25, S51, S52, S53 | every single session starts and ends at a beacon; the wiki calls them "how you progress, fast travel and level up" |
| 2 | **No way to follow your friend into another area / dungeon** | S72, S73, S54 | the zone guard *pauses* sync instead of moving anyone; multi-stage quests and bosses live behind it |
| 3 | **Boss arena entry / fog wall** | S45 | the fight starts for one player; the other is outside a wall they cannot cross |
| 4 | **Seal slot + Harden/guard/parry state** | S9, S18 | the defensive verb differs between players and is invisible; every boss fight is misread without it |
| 5 | **Quest correctness on real multi-stage quests** | S63, S67 | stage *number* syncs, stage *text/journal* never verified — a wrong objective is worse than none |
| 6 | **Horde encounters / shared-health packs / non-proximity spawners** | S47, S48, S49 | all six regions are built out of these |
| 7 | **Puzzle state** (plates, bells, lanterns, illusory/breakable walls, lights) | S33, S38, S39, S28, S29 | the wiki's puzzles are all *multi-step world object* puzzles, exactly the state store A does not hold |
| 8 | **Moving objects (drawbridges, platforms)** | S31 | flagged in the code as deliberately unfinished |
| 9 | **NG+ / Send Ova has no confirmation and no sync** | S61, S71 | the one irreversible action in the game |
| 10 | **Night Mode / difficulty modifiers / shop stock / map objectives** | S74, S75, S60, S69 | divergence you can see but that does not block play |

### 4.1 Kept per-player on purpose — do not "fix" these

`S55` level/Harbinger stats, `S56` bond/Shell points, `S57` severance, `S58` weapon levels, `S59` Tarstones,
`S60`-partly shop purchases, `S78` achievements, plus inventory/gold/Gloom/Glimpse/coins generally
(`R8-COMMON.md`: "Inventory, gold, levels stay per player (never sync them)").
`S24` (Gloom drop) already behaves this way and is correct: each death drops to its own player.

---

## 5. Appendix A — multi-stage quests

The wiki publishes **no quest page and no stage list**. What follows is assembled from the pages that mention a
quest by name plus the patch notes. Every ordering claim is `[INFERRED]` unless the page states the step.

### A.1 The main quest — the Ova hunt (the only genuinely multi-stage chain the wiki documents)

Sources: `/mortal-shell-2-unfound-path/`, `/mortal-shell-2-map-fast-travel/`, `/beacons/`,
`/mortal-shell-2-marrow-keep/`, `/mortal-shell-2-new-game-plus/`, `/story-and-lore/` ("the Ova hunt is the main
quest from the first hour").

| Stage | What happens | Gate | Source |
|---|---|---|---|
| 0 | Prologue as Harros, ending at the Tar Golem tutorial boss | scripted | `/bosses/tar-golem/` |
| 1 | Wake in Marrow Keep as the Harbinger; Harros is gone | — | `/mortal-shell-2-first-loop-marrow-keep/` |
| 2 | Siphon Ova at the Gloom Siphon | — | `/mortal-shell-2-marrow-keep/` |
| 3 | **~5 further Ova → Mether's Breath** (fast travel + clears corrupted growth) | 5 Ova | `/mortal-shell-2-map-fast-travel/` |
| 4 | Clear Corrupted Gates (6 of them, each 3 beacons, boss at the end, Ovum in the arena) | Mether's Breath recommended, not required | `/beacons/`, Appendix C |
| 5 | **~160 Ova → talk to Milos → Unfound Path opens** beneath Marrow Keep | ~160 Ova; *does not* require all six gates | `/mortal-shell-2-unfound-path/` |
| 6 | Three guardian routes, one guardian each: Malborn Offspring / The Monolith / Orrem | — | `/mortal-shell-2-unfound-path/` |
| 7 | Sever the **Corrupted Ovum** behind each guardian (these beacons have corruption sources instead of Ova) | all three | same |
| 8 | Hidden Nave beacon cleanses from outside; take the torch; **Endless Core** chest removes the +16 weapon cap | 3 severed | same |
| 9 | Craft **Seedbearer's Scripture** from the three Glands: Depleted (Withered Shoals), Thawed (Conquered Temple), Faded (Faded Citadel) — at the Forgotten Tower near Sester's Refuge, at night | 3 glands + night | same; `/mortal-shell-2-withered-shoals/` §4, `-conquered-temple/` §2, `-faded-citadel/` §4 |
| 10 | Kill **Zmey, the Unbidden** | — | `/bosses/zmey/` |
| 11 | Ending; keep exploring; **Send Ova** at the Gloom Siphon starts NG+ — **no confirmation prompt** | — | `/mortal-shell-2-new-game-plus/` |

This chain is the best test target for `r9-quest`: stages 3→5 are a **count**, stage 6→7 is an **ordered set**, and
stage 8→9 is an **item craft**. Three different stage shapes on one quest.

### A.2 Named NPC side quests (all multi-step, none documented as stages)

| Quest | Steps the wiki gives | Reward | Source |
|---|---|---|---|
| **Heart of Vatra** (Gragu) | Mether's Breath → talk to Gragu in the One-Legged Wolf Tavern → east along the cliff, cross the chasm, climb the temple grounds → take the Heart from the corrupted statue and survive the petrified enemies → return it to Gragu (he dies) → claim the body | Gragu Shell + `Heartless` | `/shells/gragu/`, `/mortal-shell-2-all-shell-locations/` |
| **Rickart's Journal** (Ruk) | obtain the Journal → deliver to Ruk. 6 Sep fixed a **quest lockout in NG+**; Rotten Autumn fixed Ruk not appearing (he now always stands at the top of Marrow Keep) | quest resolution only | `/mortal-shell-2-new-game-plus/`, `/mortal-shell-2-rotten-autumn-hotfix-1-guide/`, `/sideways-killer-ritual-dungeon-recovery/` |
| **Sester's Censer** (Genessa) | Outskirts of Mammon → jump east → interact among the red specters → beat **Sester Secundus** → take Sester's Censer → give it to Genessa in Marrow Keep → her Shell + bond Memories | Genessa Shell, memories | `/mortal-shell-2-all-shell-locations/`, miniboss table |
| **Baghead's throne** | complete Baghead's dialogue sequence and accept his throne; 6 Sep fixed a **quest lockout in NG+** | — | `/mortal-shell-2-new-game-plus/`, `/mortal-shell-2-ng-plus-checklist/` |
| **Smert's blood offerings** | at Prophet's Rest, "Make Blood Offering" at **every** circle; each one throws you out of your Shell — reclaim before the next | Smert Shell | `/mortal-shell-2-all-shell-locations/` |
| **Milos → Unfound Path** | see A.1 stage 5 | endgame access | `/mortal-shell-2-unfound-path/` |
| **Thestus → Night Mode** | carry the Gloombound Flame (from Great Arbiter of Flesh, south of Widow's Overlook) to the lantern on top of Marrow Keep → talk to Thestus | Night Mode | `/mortal-shell-2-marrow-keep/`, `/mortal-shell-2-night-mode-guide/` |

**Honest note:** two of these (`Heart of Vatra`, `Sester's Censer`) are the only side quests whose *every* step is
published. The pattern that matters for co-op is that each is a **sequence of world actions by one player** with an
**item hand-in** at the end — which is precisely the shape `S63`/`S67` has never been tested on.

---

## 6. Appendix B — bosses, order and stages

23 named encounters on `/bosses/` plus the two-part miniboss table on `/mortal-shell-2-minibosses-list/`
(Fainweald/prologue and Ruins of Mammon). Phases are as the pages state.

### B.1 Story order

`[INFERRED]` where the wiki gives no explicit order; the six Corrupted Gates are explicitly **not** a forced order
once Mether's Breath is up (`/mortal-shell-2-first-loop-marrow-keep/`: "The six gates are not a forced order").

| # | Boss | Area | Stages / phases | Reward |
|---|---|---|---|---|
| 1 | **Tar Golem** | Disciple's Grotto (prologue) | scripted tutorial; parry axe swings → Riposte; red jump slam unblockable | `Vatra's Seal` (Harden) from Harros' stone form; 2 achievements |
| 2 | **Magdalena, the Lady of the Woods** | Glutted Mire | one phase; at **half health** the cart breaks down and she summons babies **once** | Magdalena's Memento + Magdalena's Glade beacon (Ova) |
| 3 | **The Lost Child** | Sanguine Caverns | phase one; **below ~60 %** triple bite / instant hand strike / jaw inhale | Lost Clotstone + Child's Feeding Grounds beacon + Blackmarrow Key chest |
| 4 | **The Nameless Captive** | Prisoner's Domain | phase one (four horizontal swings); **phase two** head spin — parry all 7 hits for `Peter's Perfect Parry` | Captive's Scabstone + Captive's End |
| 5 | **Hexapod** | Withered Shoals | phase one; **phase two** projectiles (use the skeletons as cover) | Hexapod Core + Hexapod's Burrow beacon |
| 6 | **Droeg, the Conquerer** | Conquered Temple | club sweeps → delayed smash → **curls up and fires orbs** | Conqueror's Reward |
| 7 | **Sir Isaac, the Scholar-Prince** | Faded Citadel | **phase one** sword+shield; **phase two at ~40 %** four swords, double hits | Scholar's Wormstone + gate Ova |
| 8 | **Zmey, the Unbidden** | Hidden Nave (Unfound Path) | triple stomp (Harden the first, roll out); Seedbearer's Scripture / Paradoxical Scripture | ending → NG+ |

Per-gate bosses the wiki lists as "named patch encounters" rather than story order:
The Monolith, The Warden, Orrem the Reclaimed, Malborn Offspring, Vrannic the Grand Illusionist,
The Wandering Shepherd → **The Tarblighted Shepherd** (phase two at ~half health), Vellen High Lord of Mammon,
Larreth the Brigand King, The Silent Sester (no published route), Sariel the Endless (**two separate fights**:
platform, then Chamber of Becoming — phase two requires smashing four tombstones or he revives forever),
Prophet of Profane Infinities, Subjugated Guardian, Bloodcursed Lithopod, Tainted Vestige.

### B.2 Unfound Path guardians (the only 3-way branching section)

| Route | Beacons | Guardian | Reward |
|---|---|---|---|
| Left | Receiving Hall → Faithful's Shelter | **Malborn Offspring** (lightning phase; bring a Galvanized Shard) | Offspring Trophy (5 000 coins) |
| Centre-left | Brether's Court → Obsidian Vault | **The Monolith** | Monolith Fragment (5 000 coins) |
| Right | Bridge of Procession → Empty Cradle | **Orrem** (form 1 Discarded Golem → form 2 Orrem) | Orrem's Head (5 000 coins) |
| Final | The Hidden Nave | **Zmey** | ending, NG+ |

**Co-op relevance:** these three bosses are the only place the mod's `BOSSPHASE` sync has a *guaranteed* 2-phase
script to test against, and `r9-realarea` is set up to fight a real boss in a real area — this table is the route list
for that task.

---

## 7. Appendix C — areas, order and stages

49 beacons total (`/beacons/`), 46 requiring cleansing for `So Fresh, So Clean`; three start cleansed:
**Marrow Keep, Widow's Overlook, Outskirts of Mammon**.

| Area | Entry beacon | Gate beacons (3, last appears only after the boss) | Boss | Hazards / gimmick |
|---|---|---|---|---|
| **Prologue — Disciple's Pass** | Disciple's Grotto, Village Outskirts | — | Tar Golem | tutorial; Tainted Vestige (Burnt Effigy = fire resist) |
| **Fainweald** (11 beacons) | Marrow Keep (hub) | Stonebled, Mushroom Village, Nochtean, Mushroom Village, Outskirts of Nochte, Blackridge Pass, Widow's Overlook, Gloomshade Grove, Citadel of Penance, One Legged Wolf | — (Shell and miniboss region) | the hub ring |
| **Ruins of Mammon** (9 beacons) | Outskirts of Mammon | Sester's Gate, Gate of Infinity, Castigator's Keep, Abbey Entrance, Abandoned Outpost, Gate of Mammon, The Silent Steps, High Lord's Courtyard | Vellen, Sariel, Gloombound Wraith routes | vertical; Bone Portals |
| **Glutted Mire** | Mushroom Village Gate | Seeping Mire, Sunken Village, Magdalena's Glade | Magdalena | poison, arrow tripwires, eels; **3-candle chest**; illusory wall |
| **Sanguine Caverns** | Stonebled Gate | Pulsing Caves, Sanguine Shoreline, Child's Feeding Grounds | The Lost Child | collapsing bridges, exploding cocoons, eels, long drop |
| **Prisoner's Domain** | Nochtean Gate | Hangman's Scaffold, Executioner's Pulpit, Slumwater Drain, Captive's End | The Nameless Captive | factions fighting each other; **jars/statue climb**, **locks on cell gates**, **pressure plate → Ova gate**, breakable marked wall |
| **Withered Shoals** | Gate of Mammon | Ossinite Falls, Nameless Pass, Hexapod's Burrow | Hexapod | waterlogged; snails, eels, **bear traps**; **the lift with no lever (lever in a cart of logs)** |
| **Conquered Temple** | Sester's Gate | Winterglass Lake, Ovate's Chamber, Conqueror's Hoard | Droeg | frost everywhere (Martyr's Ash); **ring 3 bells** (two on the ground, one hidden in fog); giant elevator; white spring |
| **Faded Citadel** | Gate of Infinity (needs **Voltaic Essence** from the Starved Harridan) | Citadel Annex, Citadel Atrium, Scholar-Prince's Chamber | Sir Isaac | **blue lanterns = enemies in their light cannot be damaged**; bookshelf puzzle; giant bug ride |
| **Unfound Path** (endgame) | The Hidden Nave | Receiving Hall, Faithful's Shelter, Brether's Court, Obsidian Vault, Bridge of Procession, Empty Cradle, Chamber of the Forsaken | the 3 guardians → Zmey | three routes; Corrupted Ovum instead of Ova |
| **Disciple's Pass / Disciple's Grotto** | Village Outskirts, Disciple's Grotto | — | Tar Golem | prologue return |

**Co-op relevance:** every "gimmick" column above is a *multi-step world-object state* — bells rung in an order,
lanterns put out in an order, three specific candles lit, a statue climbed by breaking jars, a lever hidden in a cart.
None of those is a single save bool, which is why `S33/S38/S39` sit at rank 7 rather than lower.

---

## 8. Appendix D — what is inferred, and what would settle each thing

| # | Inference | What settles it |
|---|---|---|
| I1 | Wiki vocabulary maps onto engine classes one-to-one | `research\raw\CXXHeaderDump\*.hpp` on the 6 area names + 4 seal names — a static check, no run |
| I2 | Beacon cleansed state is per-player (`UnlockedLandingAreas`/`CleansedLandingAreas` in `BP_SpartaPlayerSaveObject_C`) | read the field on two clients with different beacon progress (`HANDOFF` §2.3 lists the offsets) |
| I3 | Fast travel leaves the other player behind | a two-client run: A fast-travels, screenshot B |
| I4 | A shared-health-bar horde breaks under per-enemy mirroring | spawn the Gloombound Legion, damage one, read both sides |
| I5 | Illusory/breakable walls stay visible on the peer | `WALLANIM` already replays the body; screenshot the wall itself |
| I6 | Map objective / pin state diverges | compare `BPSO_WorldMap` + the 133 `BPSO_WordMapObjective` instances on both clients |
| I7 | Item hand-in strands a mirrored quest | drive one hand-in quest (Sester's Censer is shortest) and screenshot both journals |
| I8 | Ending/NG+ runs on one side only | `BPSO_FC_FinalBossManager.EndStrandingCompleted` on both clients after a kill |

Two experiments from `HANDOFF` §7 would move several rows at once and are worth naming here because nothing in the
wiki contradicts them:
**#2** call a BlueprintFunctionLibrary CDO (`BPFL_Player_Equipment`, `BPFL_WorldStreaming`) — unblocks S52/S53/S54;
**#4** bind a multicast delegate from native — unblocks S19/S21/S45 quality.

---

## 9. Limitations of this file

1. **No run was made.** Every "Synced now?" cell cites code, a report, or a measured artifact. Rows marked
   `UNTESTED` were not executed by anyone.
2. **The wiki cannot settle a gap.** It documents intent, not implementation. Where it and the dumps disagree the
   dumps win; the one place I actively relied on the wiki is the *beacon* and *fast-travel* structure (`/beacons/`,
   `/mortal-shell-2-map-fast-travel/`), which is corroborated by `BPSO_LandingArea_Boss_C`, `BP_FastTravelPoint_C`
   and `ASpartaPlayerStart.EligibleForFastTravel` in the dumps.
3. **Two round-9 questions are still open by design** and are the top of the ranked list rather than settled here:
   real-area co-op (`wt\r9-realarea\TASK.md`) and multi-stage quest text (`wt\r9-quest\TASK.md`).
4. **Crawl record:** 225 pages stored, 218 URLs in the sitemap, 12 category probes returned 404, 2 link targets
   returned 404 (`/gloom`, `/mortal-shell-2-rotten-autumn-update-guide`). Crawl/extract scripts and all page text are
   under `research\wiki\` — data only, never executed.
