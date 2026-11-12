# R9 — gap analysis: shell-mate's game systems vs what the co-op mod syncs

Round 9, research sub-agent. **Read-only round**: no game was launched, no code edited, no save
touched, nothing in `research\ext\shell-mate` was executed (its JS was *grepped*, never run) and no
npm script was invoked. `wt\R9-COMMON.md` and `wt\R8-COMMON.md` rules obeyed.

Sources read:

| Source | Used for |
|---|---|
| `research\ext\shell-mate\` (data only, from https://github.com/JDennisUF/shell-mate) | every game system and data type the guide documents |
| `ms2-coop\docs\HANDOFF-REMAINING-SYNC.md` | the gap register G1–G12, store A/B/C/D model, the "keep per player" list |
| `ms2-coop\docs\DESIGN.md` | authority model, monotonic-health rule, puppet construction |
| `ms2-coop\docs\R6-*`, `R7-*`, `R8-*` reports | which lanes are *verified* (verdict PASS), not merely implemented |
| `ms2-coop\native\coop_state.h`, `native\src\*.cpp` | the wire keys and the code that carries them |
| `ms2-coop\run-two.ps1` | the harness verdict list (44 `=== NAME:` blocks) |
| `research\mine\sync-map.tsv` | offsets / signatures for the state a gap would have to read |

Conventions: **[FACT]** = a file:line or a report/verdict I read. **INFERRED** = my reasoning, not
proven by a run. **UNVERIFIED** = nobody has run it. Priorities are mine: **High** = a player
notices the absence within minutes of normal co-op play, or it blocks shared play; **Med** = it
matters in a fight or a menu but not every minute; **Low** = invisible to the partner or explicitly
per-player by design.

---

## 1. What shell-mate documents (the game-system inventory)

Counts below are regex counts over the data files (not the app's own claim, which disagrees in one
place — see the tarstone row).

### 1.1 Data types in the files

| File | Data type / shape | Size |
|---|---|---|
| `data.js:4` | `catalog = {shades, achievements, sources, shells, weapons, sidearms, tarstones, beacons, bosses, dungeons}` — one flat array of checklist entries per system | 6 291 lines |
| `model.js:3-13` | the 9 catalog groups rendered as tabs (shells, weapons, sidearms, tarstones, beacons, bosses, dungeons, shades, achievements) | — |
| `data.js` entry schema | `id, name, kind, region, scope(base/rotten-autumn), order, category, image, description, steps[], requirement, aliases[], sources[], relatedIds[], nearestBeacon(+Id), levelEffects[], fixedEffect, fragileDetails{effect,onBreak,durability}, descriptionSource, rewards, bossType, subtype` | 272 entries carry `region`+`scope`+`steps`+`aliases` |
| `shells.js:2` | 9 shell records: `kind:"shell"`, `shellName`, `maxShellPoints`, `signature[]` (2 strings), `skills[]` (12 names), `skillDetails[{name,description,levels[],tier}]`, `image` | 1 255 lines; 8 with `maxShellPoints`+`skillDetails`, tiers 1–4 |
| `extras.js:1-13` | `shades` — 16 records (8 base, 8 Rotten Autumn) `{category:<shell>, name:<shade>, steps[]}` | built by map, not literals |
| `extras.js:14-67` | `achievements` — 52 records `{name, description}` | one pipe-delimited string |
| `glossary.js:3-78` | **76 glossary terms** in groups: `Core combat`, `Conditions`, `<shell>’s abilities`; `keywordAliases:81` adds 15 surface forms | 83 lines |
| `tarstone-compatibility.json` | **87 stones** → `{slot: "melee"\|"sidearm"\|"both"\|"none", fits: [weapon/sidearm names]}` | 494 lines |
| `storage.js`, `app.js`, `loadout.js`, `theme.js`, `tarstone-art.js` | the checklist save (`shell-mate-progress-v2`), the planner save (`shell-mate-loadout`, `shell-mate-saved-loadouts-v1`), export/import JSON `version 2` | — |

### 1.2 Entries per system

| System (`kind`) | Count | Where |
|---|---|---|
| Shells | 9 (8 playable + prologue Harros) | `shells.js:2`, `data.js:134` |
| Melee weapons | 8, each with a `category` (Light sword, Paired weapons, Heavy axe, Greatsword, Hammer, Transforming weapon, Spear, Scythe) | `data.js:135-182` |
| Sidearms | 8 (Bazooka/Repeater/Crossbow/Lute/…) | `data.js:184-230` |
| Tarstones | **90** by `"kind":"tarstone"`; README:19 says **89** (77 base + 12 DLC) — the guide disagrees with itself | `data.js:233+` |
| ↳ by `category` | Combat 27, Support 22, Abilities 22, Infusions 17, Unknown 2 (= 90) | counted |
| ↳ by effect shape | 84 have `levelEffects` (rank 1..3), 8 have `fixedEffect`, 5 have `fragileDetails` | counted |
| Beacons | 68 (`subtype`: 64 `standard`, 4 `ghost`); some carry `alreadyCleansed` | counted, `app.js:403-407` |
| Bosses | 46, with `bossType`, `rewards`, fight tips (`app.js:416`) | counted |
| Dungeons | 52 (37 base + 15 Rotten Autumn) | counted |
| Shades | 16 (8 + 8 DLC) | `extras.js` |
| Achievements | 52 | `extras.js:14-67` |
| Glossary terms | 76 (+15 aliases) | `glossary.js` |

### 1.3 Rules the guide states as *rules* (not just locations)

* **Tarstone loadout**: up to **10 active stones** = 4 Support + 3 melee + 3 sidearm slots; the two
  Combat slots are one melee + one sidearm (not interchangeable); slots may be empty
  (`TARSTONE_EQUIPMENT_RULES.md:7-15`; implemented in `loadout.js:8`).
* **Category ≠ compatible gear**: category (Combat/Abilities/Infusions/Support) and the slot kind
  (`Abilities`/`Infusions`/`Combat`) and the compatible-gear list are three independent axes
  (`loadout.js:20,46-55`).
* **Stones are remembered per weapon**: switching back restores that weapon's stones
  (`loadout.js:29-45` `rememberGear`/`state.gearStones`) — the guide itself flags this as
  player-reported, not verified (`TARSTONE_EQUIPMENT_RULES.md:46`).
* **One copy per stone** (duplicate selection is stripped/warned: `loadout.js:42,322`).
* **Rank semantics**: at level 3 most stones only keep their final upgrade, 19 named stones keep
  several ranks as distinct (`loadout.js:161-173`); 17 infusion stones map to an element
  (`loadout.js:174-181`).
* **Shell points**: rank *n* of a skill costs *n* points and the total is capped by
  `maxShellPoints` (e.g. Tiel 40) — `loadout.js:191`, `app.js:190-191`.
* **Stat vocabulary**: Health / Shell Health / Resolve / Poise / Break (Break Damage → Riposte) /
  Stagger / Critical Chance / Critical Damage / Damage Reduction / Attack Speed (Warp) — glossary
  `core combat` group.
* **15 condition/stack systems**: Chaos, Bloodcurse, Burn, Curse, Fragile, Leech, Shock, Lightning,
  Perforation, Phantom, Poison, Stasis, Trauma, Warp, Weak.
* **Per-shell resource mechanics**: Lazlo `Heat Meter`/`Retribution`/`Armor`, Proxima
  `Specimen`/`Biosampler`/`Grafted Armor`, Tiel `Shadow`/`Shadow Strike`, Genessa
  `Stray`/`Duality`/`Faithful Doubles`, Gragu `Revered Heart`, Sariel `Pain`, Eredrim
  `Slaughterer`, Smert `Faith`/`Fight Stance`/`Miracle`.
* **Seals** decide the defensive action: Guard, Parry (Infinite Seal), Harden.
* **Night Mode** ("At Night") gates DLC shades/dungeons and some effects.
* Not documented at all by shell-mate: numeric base stats per shell, inventory, masks/clothing,
  level instances, fast travel, NG+, save structure. (Its own `PLAN.md:16` says accounts, shared
  saves and offline are out of scope.)

---

## 2. What the mod syncs today (the baseline this table compares against)

Wire line (`coop_state.h:22-36`, `native\src\coop_net.cpp`), 15 Hz hot + 2 Hz warm:

`t,id,sq,rdy` · `x,y,z,vx,vy,vz,yaw,pitch` · `anim,spd` · `mseq,mpos,mrate,mid` · `body,apart` ·
`wpn,wsock,wpns` · `lk` · `npc` · `pd` · `nhp` · `dead` · `ws` · `wt` · `wb` · `wq` · `wd` · `wl` ·
`wc` · `wx` · `wa` · `zn` · `bh` · `lv,bs,dstk,wr,rul` · `bsp` · `dl,ct,ci` · `drp`

Verified lanes (harness verdicts in `run-two.ps1`, 44 blocks): SHELL, WEAPON, PUPVIS, LOOK, ANIM,
DAMAGE, DEATH, DEATHCHAIN, DROPS, WORLD, PROGRESS, QUEST, QUEST2, LIFT, CHEST, STORE_A2(+selftest,
census), SPAWN, MIRROR, PEERHIT, AITARGET, STREAM, NPCFAR, FARENEMY, PERF, LEDGER, BREAK, WIRE,
ZONE, DIALOG, DIALOGVOTE, DIALOGCODEC, DIALOGREAL, CINE, PEERHP, PEERDEAD, BOSSPHASE, APPEAR,
WALLANIM, FIXRESET, WIRETEST, NPC, NATIVE, A, B.

---

## 3. The gap table

**Affects peer?** — `visual` = the partner's screen changes; `outcome` = only the shared fight
result changes; `shared` = the two worlds must agree for play to work; `no` = per player by design.

| # | System | What the game does (shell-mate) | Affects peer? | Synced now? (evidence) | Gap | Pri | How it could be tested |
|---|---|---|---|---|---|---|---|
| 1 | **Shell roster / shell claim** | 9 shells; claiming one is permanent; each has its own body, stats and skills | visual + shared | **Yes**: own body mesh path in `body`/`apart` → puppet mesh swap (`coop_puppet.cpp` `ApplyBodyMesh`); SHELL verdict; different shells verified end-to-end, `R8-SHELLS-REPORT.md:128-134` (A's puppet = `SK_Tiel`, bones 104). Claim flags: `BPSO_EridrimUnlockManager.{WardenJumped,WardenDefeated,ShellClaimed}` (`coop_world.cpp:130-131`). Shell *item identity* is only **lab**-writable (`coop_equip.cpp`), never published | the partner's shell is inferred from a mesh path; no shell tag on the wire, so UI/state (e.g. "friend is Smert") needs a string match on an asset path | Low | `equip shell <tag>` on B → A's `sync.txt` body path changes + screenshot (that exists); add a `shl` key = shell tag and assert A logs the tag |
| 2 | **Shell signature abilities** (2 per shell, `shells.js` `signature[]`) | each shell has two defining moves (Shadow Dash, Miracle, Retribution, Duality, Biosampler…) with their own VFX | visual | **Partly**: an ability that is a *montage* replays via `mseq`→`playMontage` (ANIM verdict); `R8-SHELLS-REPORT.md:147-154` states VFX **cannot** travel ("`look` carries mesh components and slot materials only — no particle/niagara channel") | any ability that is VFX/Niagara-only is invisible on the partner's screen; the ability's *state* (a shockwave, a decoy) has no channel | **High** | `mseq` is already logged per action; add a `fx` key (class + socket + lifetime) and a `result_fx.txt` verdict: B casts, A's probe counts a Niagara component on the puppet within 1 s |
| 3 | **Shell skill tree** (12 skills, tiers 1–4, ranks 1–3, rank *n* costs *n* points, cap `maxShellPoints`) | spending Shell Points changes the shell's numbers and sometimes adds behaviour (Shadow Clone, Poison Burst…) | outcome (+visual for behaviour-changing skills) | **No**: grep of `native\src\*.cpp` = 0 hits for `Skill`/`ShellPoint`; no row in the store-A typed table (`coop_world_ws2.cpp:132-141`) and `PlayerProgressionComponent.{ShellBuilds,SkillLevels,EquippedSkillInstances}` (`sync-map.tsv:252-266`) is unread | the friend's build is invisible and the *equipped-skill* set is the only way to know whether a behaviour FX should appear at all; no HUD/display either | Med | publish `ski` = equipped skill tags; test `cmd skillequip <skill>` on B, verdict that A logs the tag set and that the skill's montage/FX fires on A's puppet |
| 4 | **Bond tier / bonding points** (`Decked Out` = max bonding tier) | per-shell bond tier is bought with bonding points | no | **No**: `BPSO_Progression.{CurrentLevel,AvailablePoints,AssignedPoints,BondingPoints,GoldSpentAtShop}` (`sync-map.tsv:227-231`) not in either store-A table | none for co-op play; only a "friend's build" display would want it | Low | read-only: add the int row to the typed table (`kHost`) and assert the census prints it — purely telemetry |
| 5 | **Shell Shades** (16, `extras.js`; bought with Blackmarrow keys, changed at a Beacon) | swaps the shell's materials (and some shades an extra mesh) | visual | **Yes for the look**: the body-material signature `bm=` in `lk` carries a skin swap; `R8-SHELLS-REPORT.md:135-139` verified identical `MI_DarkBro_Obsidian_*` on B's own pawn and A's puppet (APPEAR skin step, PASS 5/5) | unlocked/active shade **sets** (`UnlockedSkins`, `ActiveShellSkins`, `sync-map.tsv:151-153`) are per player and unsynced; a shade that only changes a *mesh* travels via `body` only if the path changes (INFERRED) | Low | already covered by APPEAR; add one shade whose art is a distinct mesh and assert the `body` path change on A |
| 6 | **Shell Memories** (per-shell cutscenes; `Haunting Times`, `Seeking the Past`) | memory playback is a level sequence + progress counters | visual | **Partly**: cinematics `ci` (CINE PASS, `R7-FINAL-REPORT.md:88`); `BPSO_ShellMemories.{MemoryProgress,MemoriesWatched}` (`sync-map.tsv:312-313`) **not** synced | if one player watches a memory the other does not join it; progress stays per player (correct) but the *playback* is one-sided | Med | `cineplay` already exists; add a verdict that the *non-initiator* sees the sequence not just "idle puppet" |
| 7 | **Melee weapons** (8, with categories) | equip 1 of 8; upgrades at the Tarforge | visual | **Visual only**: `wpn`,`wsock`,`wpns` attach every rendered weapon mesh (WEAPON PASS); `ApplyWeapons` (`coop_appear.cpp:1238-1333`) sets mesh + socket + relative transform and **no materials** | weapon materials / infusion glow / upgrade VFX never arrive; which weapon the friend owns is not state | Low† (material half is row 12) | `weapon` op already swaps the hand weapon; add an assertion that the peer's weapon component count and socket match, and (for row 12) that its material names match |
| 8 | **Sidearms** (8) | same as weapons; one sidearm slot; ammo is an attribute | visual + outcome | **Visual yes** (`wpns`); **ammo yes** (`bh` carries `ammo,ammoMax`, `coop_state.h:2111`); PEERHP PASS | no per-sidearm identity, no reload/charge state | Low | `bodyset ammo <n>` on B → A's `sync.txt bh=` shows it (the verb already exists) |
| 9 | **Seals** (Guard / Parry / Harden; Infinite Seal) | the equipped seal decides the defensive action; Harden has a stone-form | visual | **No**: 0 hits for `harden`/`parry`/`stoneform` in `native\src`; `bh` has no defensive field (`coop_state.h:2105-2116`); HANDOFF G4 lists these as unpublished | the partner sees a body taking hits with no guard/harden pose and no stone-form — the single most "not co-op" moment in a fight | **High** | add 3 flags to `bh` (or a `dfn` key) and a `result_peerdef.txt` verdict: B holds Harden, A's probe reports the puppet's material/flag within 1 s |
| 10 | **Tarstone catalogue** (90, 4 categories) | 4/3/3 equipment slots + 3 support; each stone has 1–3 rank effects | outcome | **No** (data only) | not a runtime system — only the planner needs it | Low | n/a (data) |
| 11 | **Tarstone levels / ranks** (`levelEffects` 1–3, `fragileDetails`, 19 stones keep distinct upgrades) | a stone is upgraded with tarcore; ranks change its numbers | outcome | **No**: `TarstoneComponent.{TarstoneBuilds,TarstoneLevels,TarstoneSupportBuild,Equipped*TarstoneItemInstances}` (`sync-map.tsv:269-286`) is never read; 0 grep hits | per player by design (inventory), so the only loss is that the partner cannot see the *effect* — which shows up as HP deltas anyway | Low | n/a unless a display is wanted; then a read-only `tst` (ids+levels) with no apply |
| 12 | **Tarstone infusions** (17 stones → element: Bloodcurse, Poison, Lightning, Frost, Curse, Phantom, Stasis, Trauma, Burn) | the infused weapon/sidearm **looks** infused (material/VFX) and applies the element on hit | visual + outcome | **No**: the weapon mesh is attached with no materials (`coop_appear.cpp:1319-1327`); infusion element is not read anywhere | this is the one tarstone feature a partner *would* see, and it is exactly the channel that is missing (weapon materials) | Med | `equip infuse <stone>` on B; A's `pupdump`-style probe compares the puppet weapon component's `GetMaterial` names with B's own |
| 13 | **Tarstone slots / loadout** (10 slots, per-gear memory, one copy per stone) | the planner mirrors the game's per-weapon stone memory | outcome | **No**, and unreadable in principle until row 11 exists | per player by design; a "look at my build" feature needs a read-only publish | Low | read-only publish + HUD/tooltip; no apply |
| 14 | **Tarstone compatibility rules** (87 entries `{slot,fits}`) | some stones fit 1 weapon, some all of a type, some none | no | **No** (planner data; the game enforces it in its UI) | none — the server side of this is the game's own menu | Low | n/a |
| 15 | **Player stats: Health / Shell Health / Resolve / heal charges / PP / ammo / darkform / death** | 5 attribute sets drive the HUD and abilities | visual | **Yes**: `bh` = `[hp,hpMax,shp,shpMax,res,resMax,heal,healMax,pp,ppMax,ammo,ammoMax,dark,ds]` (`coop_state.h:2074-2077,2105-2116`), written into the puppet's own attribute sets; PEERHP + PEERDEAD PASS (`R7-FINAL-REPORT.md:89-90`) | the list is exactly the G4 set minus harden/block/parry and minus the shell-revive meter | Low | `bodyset <field> <v>` already exists (`coop_state.h:2138`) |
| 16 | **Poise / Armor / BreakResistance / MaxBreak / AttackSpeed / Riposte multiplier** | the defensive/offensive numbers tarstones and skills move | outcome | **No**: `SpartaHealthSet.{Poise,MaxPoise,Armor,BreakResistance,MaxBreakResistance}` and `SpartaCombatSet.{AttackSpeedBonus,RiposteDamageMultiplier}` (`sync-map.tsv:8,11-14,55-57`) are unread | invisible; only the resulting damage differs | Low | read-only counter; no wire slot needed for play |
| 17 | **Conditions on players** (Poison, Burn, Frost, Lightning, Curse, Bloodcurse, Fragile, Trauma, Phantom, Stasis, Warp, Weak, Shock, Perforation, Leech) | stacks live on the actor, show VFX, change behaviour (Frost freezes, Stasis slows, Curse reflects) | visual | **No**: no condition/GE read in `native\src`; HANDOFF G4 lists them as unpublished; the VFX channel does not exist (`R8-SHELLS-REPORT.md:147-154`) | your friend can be frozen/burning/cursed and look completely normal; there is no way for either player to react to the other's state | **High** | `bkadd <condition> <stacks>` lab verb → publish a bitmask in `bh`; verdict `result_peercond.txt`: A's probe reports the stack count and a screenshot of the VFX |
| 18 | **Per-shell resource meters** (Lazlo Heat/Retribution, Proxima Specimen, Tiel Shadow, Genessa Stray/Duality, Gragu Revered Heart, Sariel Pain, Eredrim Slaughterer, Smert Faith/Miracle) | a per-shell meter gates that shell's signature; Miracle also stops time | visual | **No**: `ShellAbilityAttributeSet.{Stacks,CurrentAmount,Cooldown,HealingMeter,MaxHealingMeter}` (`sync-map.tsv:66-70`) unread; only `dark` (darkform) is on the wire | the partner cannot see the friend's meter or the states that depend on it; **Miracle (time stop) is a global-feeling event that visibly does nothing on the other screen** | **High** (Med for meters, High for Miracle/time-stop) | `bodyset`-style probe on the meter; for Miracle, lab: B casts, A's `GetRealTimeSeconds` rate + enemy montage rate are sampled to show whether global dilation is local (INFERRED it is local) |
| 19 | **Level / EXP / stat allocation** (`PlayerExpComponent.PlayerLevel`, `CurrentEXP`; `PlayerSaveObject.ShellStats/WeaponStats/PlayerStats`) | levelling raises the shell's numbers | no | **No, deliberately** — `HANDOFF-REMAINING-SYNC.md:457-458` "keep per player (my read): `BPSO_Progression` (levels/points), inventory + gold, `PPItems`, tarstones"; R7-MAPS §1.3:85-88 restates the rule for `ActorNames` | none — this is the design | Low | none (rule) |
| 20 | **Beacons** (68: 64 standard + 4 Ghost; cleanse; "change shade at any Beacon") | cleansing a beacon makes it a fast-travel/spawn point and lights the world | shared | **Partly**: `BPSO_LandingArea_Boss_C.BeaconRisen`, `BPSO_TarGate.{GateClensed,PlayerClensed}`, `BPSO_GenessaSensor.SensorClensed` (`coop_world.cpp:125,139,145`) replay as store-A booleans; OWNED/peer-visible beacon state has no verdict of its own | the *player* record of cleansing (`BP_SpartaPlayerSaveObject.{UnlockedLandingAreas,CleansedLandingAreas}`, `sync-map.tsv:159-160`) is not synced, so fast-travel parity and "your friend cleansed it, why is mine dark" are open (INFERRED that the world flag alone does not grant travel) | **High** | lab: B cleanses a beacon → A's `sync.txt ws=` shows the key; then open A's fast-travel map and check the landing area (screenshot + a `landingdump` verb) |
| 21 | **Fast travel / landing areas** | travel between cleaned beacons; `AllowFastTravel` per point | shared | **No**: HANDOFF G9 lists it; `CanFastTravel` was `false` in the test save (`HANDOFF.md` §18); `BPFL_WorldStreaming.TeleportPlayerWithStreaming` CDO call is open question #8 | the two players travel independently and can end up in different zones with **all** world sync silently paused (that pause is implemented: `zn`/ZONE) | Med | `S_PostGolemFightSettings`/`S_UnlockFastTravel` exist on the controller (`sync-map.tsv:362`); lab: unlock, then `ft <area>` on B and assert A's `zn` matches within N s |
| 22 | **Dungeons** (52; 37 base + 15 Rotten Autumn; entered from the world or a Ghost Beacon) | each dungeon is a **level instance**; a Night-Mode-only subset | shared | **Partly**: `zn` publishes map+zone+level-instance hash and *pauses* every world apply when they differ (ZONE PASS, `R7-FINAL-REPORT.md:84`; guard implemented in `coop_body.cpp` `zone::Paused`); door/chalice/chest flags synced (`BPSO_DoorLockedDungeon.DoorOpened`, `BPSO_SlayerDungeonChalice.SealBroken`, `coop_world.cpp:118,143`) | **no co-entry**: nothing drives both players into the same instance (`BP_LevelInstanceSelector.LoadLevel` unread), so "come into this dungeon with me" is a manual walk and any mismatch = zero sync | **High** | publish the instance id (done) then test: B loads a dungeon, A presses a `joininst` verb that calls `SelectLevel/LoadLevel`; verdict `result_join.txt` = `zn` equal + one store-A key applies |
| 23 | **Bosses** (46; main/optional; `rewards`) | phase transitions at HP thresholds, arena gates, drops | shared | **Yes**: `bsp` phase events + hook (`R7-BOSS`, BOSSPHASE PASS `R7-FINAL-REPORT.md:91`); `npc/nhp/dead` health & death; gates `BPSO_BossGate.Unlocked`; rewards via `drp` (DROPS PASS) | boss *ability* visuals/projectiles beyond the montage; phase-ability FX on the non-owner (INFERRED partially covered by montage replay) | Low | `bossphase force 2` exists; add a screenshot verdict that a phase-change FX appears on the non-owner |
| 24 | **Night Mode** ("At Night" effects; night-only dungeons/shades) | a world-level time/state that changes spawns and gates content | shared | **INFERRED only**: 104 data layers are synced (`wd`, DL apply budget 2, `coop_state.h:246-256`) and night mode is plausibly a data layer — nothing in the docs states it, and no verdict covers it | if night mode is *not* a data layer, the two worlds differ in spawns and the partner's DLC shades/dungeons are unreachable | Med | `dlgdump`-style listing of the active data layers at night on one client; then compare `sync.txt` `wp=` `d:` counts between the two clients |
| 25 | **Shops / vendors** (Merrick stock expands; Glimpse Stone + Egon's Stone purchased; `BPSO_Egon.GloomDonated` synced) | stock is per shop actor; purchases move gold | shared | **Partly**: `BPSO_Egon_C.GloomDonated` is in the typed table (`coop_world_ws2.cpp:136`, `kMax`); `BPSO_ShopData_C.{CachedItemsMap,TotalGoldSpent,StolenItems}` is **not** (`HANDOFF-REMAINING-SYNC.md:221` and G1 at :277; no row in `sync-map.tsv`) | if stock is world state, one player's purchase diverges the shop for the other (INFERRED — nobody has counted `CachedItemsMap` at runtime) | Med | one-shot read-only probe: dump `CachedItemsMap` before/after a purchase on B and assert A's map changed or not — that settles whether it is shared at all |
| 26 | **Items in the world** (pickups, chests, flower chests, Ova) | a pickup is consumed once; chests give loot | shared | **Yes**: `BPSO_CraftingItemPickup.{ItemCollected,ItemInitialized}`, `BPSO_FlowerChest.ItemCollected`, `BPSO_Ovum.{Cleansed,OvumCollected}` (`coop_world.cpp:115-116,117,138`) + chest lid & loot replay `CS.grant` (CHEST PASS) | a world pickup consumed on one side is granted to nobody on the other (INFERRED) — only *chests* were built to duplicate loot | Low | `cstest` exists; add a `pickuptest` that walks a player onto a pickup, verdict that both inventories moved |
| 27 | **Enemy drops / loot** | each death rolls gold/gloom/items from `CharacterDeathData` | shared | **Yes**: owner rolls, peer grants (`drp`), mirrored deaths vetoed (DROPS PASS both directions, `R7-FINAL-REPORT.md:66`) | — | — | `dropsprobe`/`dropsgrant` exist |
| 28 | **Currency** (Gloom, Gold, Glimpses, Ova, Tarcores) | each currency is an **item stack**, not a scalar | no (per player) | **Per player by design** (`HANDOFF-REMAINING-SYNC.md:457-458`; R7-MAPS §1.3:86-88 "the same class of mistake as syncing inventory, gold or levels"); the partner's *wallet is never mirrored* — only drops are duplicated. Own death drop is local-only (`R8-MONEY`) | none — but there is no way for a player to *see* the friend's purse (HANDOFF's `pi` proposal) | Low | `dropswallet` already prints both wallets on one client |
| 29 | **Achievements** (52) | per-profile unlocks | no | **No, deliberately**: `EarnedAchievements` (`sync-map.tsv:162`) is player-save | none | Low | none (rule) |
| 30 | **Map fragments / world map** (`Sat Nav`) | fragments reveal the map; objectives are per player | no | **No**: `BPSO_WorldMap_C`, `BPSO_WordMapObjective_C` (133 live) unwatched — HANDOFF G1 defers the reveal-vs-completed split | per-player reveal is fine; "completed" objectives drive world markers the partner may want | Low | `mapsdump`-style census first (the decision G1 asked for) |
| 31 | **Lute** (Troubadour's Lute sidearm; `PlayedLuteTracks`, `Heavy Metal`) | a playable instrument with 6+ melodies | visual + audio | **Visual only**: the lute mesh rides in `wpns`; audio is per client by construction (R7-DIALOG notes audio must be muted on the non-owner to avoid double voice) | the friend sees a lute but hears nothing (INFERRED: melody is a local montage + local audio) | Low | lab: play a track on B, assert A shows the montage (already) and add an "audio cannot cross" note; a Wwise event forward is out of scope |
| 32 | **Death / severed / Shell Revive / co-op revive** | death severs you from the shell; Shell Revive stations refill a meter; single-player only has self-revive | shared | **Yes for the flow**: `death-chain.ps1` 4 deaths one session, `DCASE2/DHOLD/DWIPE` PASS (`R8-DEATHS-REPORT.md:274-275`); `lv/bs/dstk/wr/rul`; `BPSO_ShellRevive.StationUsed` synced (`coop_world.cpp:152`) | **partner→partner revive does not exist** (HANDOFF G9 "co-op revive has to be built on top"); the shell-revive meter (`ShellReviveCurrentProgress`) is not in `bh`, so "let me help you up" has no data | Med | add the meter to `bh`; verdict `result_help.txt`: B dead, A's `helpup` verb grants progress and B respawns — assert both sides see it |
| 33 | **Prologue / Harros** | the prologue shell and its `Harros` flag gate content | shared | **Yes**: `BPSO_PetrifiedCultistManager_C.Harros` (`coop_world.cpp:154`) | — | — | store-A2 census already prints it |
| 34 | **Pause / menus** (not shell-mate's data, listed for completeness) | menus are local; pause does not freeze the peer | no | **Yes for cosmetics**: `rdy` gate hides the body (`coop_state.h:78-82`); dialog/cinematic lock `dl`/`ci` (DIALOG, CINE PASS) | the friend's body just disappears while they are in a menu instead of standing idle | Low | already covered by `dlgfake`/`rdy` |
| 35 | **Persistence / divergence** | autosave at different moments per client | shared | **No**: the mod never calls `SaveToDisk`/`SetPreventFurtherSaves` (HANDOFF G11, 0 hits) | a synced change can survive on one side only after a quit | Med | apply a peer change → quit both → relaunch → compare store A + ledger (HANDOFF G11 test) |

† Row 7's priority is Low; the *material/infusion* half is counted once, in row 12 (Med), so the
top-10 list below does not double-count it.

---

## 4. What shell-mate calls a system but is **not** a sync concern

| Item | Why |
|---|---|
| Tarstone compatibility table, entry routes, nearest beacon, boss fight tips, region maps, `sources[]` | Static guide metadata; the game enforces its own rules. Nothing to replicate. |
| `alreadyCleansed` on a beacon, `scope` (base vs Rotten Autumn), DLC tagging | Guide-side bookkeeping (`PLAN.md:9`), not game state. |
| The checklist save (`shell-mate-progress-v2`) and the planner saves (`shell-mate-loadout*`) | Browser `localStorage`; a *guide's* progress, not the game's. Nothing the mod could or should touch. |
| Glossary terms, keyword aliases | Vocabulary for the guide's UI. Useful only as a **checklist of what to look for** — which is how I used it in rows 15–18. |

---

## 5. Top 10 gaps, ranked

1. **Player condition stacks + their VFX never reach the partner** (row 17, High). 15 condition
   systems exist; the wire has no bitmask and no VFX channel. Cheapest fix: a bitmask/stacks field
   in `bh`; the visuals need the `fx`/Niagara channel from row 2 anyway.
2. **Shell ability state and FX** (row 2, High). `mseq` only replays *montages*; R8 measured that
   the wire cannot carry a particle system. Every shell's identity is partly its FX.
3. **Harden / block / parry / stone-form** (row 9, High). Zero occurrences in `native\src`; the
   partner watches a body eat hits. Tiny wire cost (3 bits) — the largest feel-per-byte win.
4. **No co-entry into dungeon level instances** (row 22, High). `zn` proves both sides know they
   differ, then pauses *all* world sync. "Come with me" is a manual walk today.
5. **Beacon cleanse + fast-travel remain per player** (rows 20–21, High). The world-side flags are
   synced; the player-save landing-area arrays are not, so travel parity is unproven (INFERRED).
6. **Per-shell resource meters, and Miracle's time-stop** (row 18, High/Med). `ShellAbilityAttributeSet`
   is unread; a global-feeling event is invisible on the other screen.
7. **Partner→partner revive + the shell-revive meter** (row 32, Med). The death *flow* is the most
   verified lane in the project; the *rescue* half does not exist.
8. **Weapon materials → tarstone infusion visuals** (row 12, Med). `ApplyWeapons` deliberately
   attaches meshes with no materials; infusion is the one tarstone effect a partner would see.
9. **Equipped skill set / bond tier unpublished** (rows 3–4, Med). Without it, a HUD or an
   ability-parity pass cannot know what the friend can do.
10. **Shop stock divergence** (row 25, Med, INFERRED). `BPSO_ShopData.CachedItemsMap` is unread and
    nobody has counted its runtime size — one read-only probe settles whether stock is shared.

Honourable mentions: Night Mode (row 24 — settles or kills a class of "why is your world
different" reports), persistence/divergence (row 35), world-pickup duplication (row 26).

---

## 6. Explicitly INFERRED / still unknown

| Claim | Status | Experiment |
|---|---|---|
| A shade that changes a mesh reaches the peer via `body` only if the asset path changes | INFERRED | pick a shade with a distinct `SK_` mesh, `equip skin`, compare `body` |
| Cleansing a beacon in the world does **not** grant the partner fast travel (player-save array is authoritative) | INFERRED | cleanse on B, open A's map, `landingdump` |
| Night Mode is (or is not) a data layer | INFERRED | dump active data layers at night on one client, compare `wd` counts |
| `CachedItemsMap` is world state (shared stock) | INFERRED / UNKNOWN | one-shot read-only dump before/after a purchase |
| Miracle's time dilation is local, not a replicated world state | INFERRED | sample `GetRealTimeSeconds` and an enemy montage rate on both clients while B casts |
| Lute audio never crosses | INFERRED | play a track, listen/record on both |
| World pickups are single-consumer (the partner gets nothing) | INFERRED | `pickuptest` on a fresh fixture |
| Infusion stones change the weapon's *material* | INFERRED (from the guide's wording; the guide does not show it) | `equip infuse`, `pupdump`-style material list on the peer |

Nothing above was run in round 9. **Every "how it could be tested" cell is a proposal**, not a
result. No save, install or file outside this report was touched.

---

## 7. Harness pattern any of this should follow

`HANDOFF-REMAINING-SYNC.md:680-700` + `native\src\coop_state.h` conventions:

1. a `cmd.txt` verb in the owning module's `OnCommand` (e.g. `bstone equip <id>`);
2. a kill switch in `Toggles::Load` (`<X>_OFF.txt`) read once at load;
3. a `result_<x>.txt` verdict written by the **receiving** client;
4. one `sync.txt` counter line (`Status()`), because that is how every lane is debugged;
5. a `Wait-File` + `=== X:` block in `run-two.ps1` and the name added to `$pass`;
6. fixture reset first (`fixreset` / `wa2reset` / `ResetSaveObjectByGuid`) — the two permanently-red
   verdicts in the older suites are fixture problems, not code problems.
