# Research job: Mortal Shell II co-op, the facts needed to sync everything

You are doing data collection for a co-op mod for Mortal Shell II (Unreal Engine 5.6, shipping build,
no game netcode). The mod is a UE4SS Lua mod plus a small relay process. Another engineer will
write the sync code from your results. **Your job is to find and write down facts, not to write
the mod.** Be exhaustive and mechanical. Never guess: every fact you write must have evidence
(a dump line, a file path, or a log line from a run). If something is unclear, write "UNKNOWN"
and say what you tried.

## Hard rules (breaking any of these is worse than doing nothing)

1. Never modify anything under `G:\Freespace\dollsMP` (read it if useful, never write).
2. Never touch the real game install `E:\Games\Mortal Shell II`. Only use the test copies
   `E:\Games\_MS2-coop-test` and `E:\Games\_MS2-coop-b`.
3. Never touch the real saves in `%LOCALAPPDATA%\MortalShell2\Saved\SaveGames`. Before any game
   run, copy that folder to `G:\Freespace\shell_coop\local\save-backups\<timestamp>\`, and after
   the run compare file hashes and restore if they changed.
4. Do not edit `G:\Freespace\shell_coop\ms2-coop\mod\CoopMod\Scripts\main.lua` (the shipped mod).
   For in-game probes use only `ms2-coop\mod\CoopLab\Scripts\main.lua` and `ms2-coop\lab.ps1`
   (read `ms2-coop\docs\HANDOFF.md` sections 2.3 and 5 first; section 5 lists engine calls that
   crash or freeze the game - never use them).
5. Save Lua files as UTF-8 **without BOM** (a BOM stops UE4SS loading the mod).
6. Kill every game, CrashReportClient and `cooprelay.py` process you start when you finish.
7. All output goes in `G:\Freespace\shell_coop\research\` (create it). Do not put results
   anywhere else.

## Step 0: build the raw sources (do these first, everything else greps them)

1. **Full object dump with every asset loaded.** In `E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\UE4SS-settings.ini`
   set `[ObjectDumper] LoadAllAssetsBeforeDumpingObjects = 1`. Start the game, wait at the **main menu**
   (do NOT continue into the game after dumping: it crashes), trigger the UE4SS object dump
   (keybind from `Mods\Keybinds\Scripts\main.lua`, usually Ctrl+J), wait for
   `UE4SS_ObjectDump.txt` to stop growing, close the game, set the setting back to 0.
   Copy the dump to `research\raw\ObjectDump_allassets.txt`.
2. **C++ header dump** (class layouts with offsets): same idea with the CXX header generator
   (`[CXXHeaderGenerator]`, keybind usually Ctrl+H, `LoadAllAssetsBeforeGeneratingCXXHeaders = 1`
   at the main menu only). Copy the output folder to `research\raw\CXXHeaderDump\`.
3. **In-game dump**: load into gameplay normally (setting back to 0), stand next to the first
   bonfire/checkpoint, make a normal object dump, copy to `research\raw\ObjectDump_ingame.txt`.
4. Write `research\00-sources.md`: what each raw file is, its size, when and how it was made.

Useful line format in the dump: `[address] ClassName /Path/To/Object.Object_C:Member [...]`.
`Function` lines are callable functions, `*Property` lines are fields; properties right after a
function are its parameters (with `[o: offset]`).

## Step 1..8: one report file per area

For **every** area below, produce a Markdown file with tables. For each class or asset write:
full path, parent class, the relevant properties (name, type), the relevant functions with their
**full parameter list in order** (name and type of every parameter, including out and return
parameters), delegates/events (`MulticastInlineDelegateProperty`), and 1-3 evidence lines copied
from the dump. Group by kind. Add a short "how it probably works" note only when the names make
it obvious, and label it as a guess.

1. **`10-world-state.md` - things in the world that change and stay changed.** Doors, gates,
   levers, elevators, shortcuts, breakable walls, chests, item pickups, ladders, bridges,
   bonfires/checkpoints/rest points, fog walls, boss arena gates. Search names containing:
   Door, Gate, Lever, Switch, Elevator, Lift, Shortcut, Chest, Pickup, Loot, Interact,
   Checkpoint, Bonfire, Shrine, Rest, Fog, Barrier, Breakable, Destructible, `BPSO_`,
   `WorldState`, `Persistent`, `SaveObject`, `Saveable`. Also: how the game records "this was
   opened/picked up" - find the save-object/world-state classes, their maps/arrays/tags,
   and every function that adds or reads such a record. List the GameplayTag names used for world
   state (search `WorldStateTags`, `GameplayTag`, `Tag.` strings).
2. **`20-quests-progression.md`.** Quest/objective/story/dialogue/NPC-state classes and data
   assets (search Quest, Objective, Story, Progress, Dialogue, Dialog, Conversation, NPC,
   Merchant, Vendor, Journal, Lore, Tutorial, Unlock, Flag, Milestone, Ending, Credits),
   DataTables and DataAssets that define them, the save game classes and their fields
   (search `SaveGame`, `MetaSave`, `WorldState`), what is stored per player vs per world.
   Also list every level/map (`/Game/Sparta/Maps/...`, `World` objects) with its streaming
   sublevels, and every travel/teleport/fast-travel/level-load function.
3. **`30-player-look.md`.** Every shell (playable body): mesh path, anim BP, skeleton, materials,
   and how the game switches shell (functions, components). Every player weapon: weapon
   blueprint class (`WP_*`), its `SM_Weapon` static mesh path, any skeletal mesh, the sockets it
   uses when drawn and stowed (search `Socket_` names), upgrade/infusion visual variants
   (`WeaponVariants`), and weapon-specific locomotion sets (folders under
   `/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/` and per weapon). Armor,
   cosmetics, glow/effects, hardening/stone form visuals, health/resolve visual states.
4. **`40-player-actions.md`.** Every player ability (`GA_*` classes): its name, the montage(s)
   it plays (paths), cost/attribute it uses, and how it is triggered (input tag / function).
   Cover: light/heavy/charged attacks, combos, dodge/roll/dash, parry, harden, block, kick,
   ranged weapons (shotgun, crossbow, ballistazooka...), seals/magic, consumables/healing,
   item use, interaction (open/loot/pray/kneel), shell swap, death, revive (`GA_ShellRevive*`),
   rest. Also the player's attribute set (`SpartaHealthSet` and others: every attribute name).
5. **`50-enemies-bosses.md`.** Every enemy and boss class (`BP_*` deriving from the enemy/AI
   base), its AI controller, behaviour tree/state tree asset, health component, abilities,
   montages, projectiles it spawns, aggro/perception/target functions (search Target, Aggro,
   Perception, Sense, Threat, Focus), spawn/respawn on rest functions, and for bosses: arena
   volume/trigger classes, fight start/end functions and events, boss health bar widget, phases,
   reward/drop on death. Also the damage path: every function containing Hit, Damage, Hurt,
   Stagger, Poise, Parry, Riposte, Death, Die (class, name, full parameter list), especially
   `SpartaHittable` / `SpartaApplyHit` and the hit payload struct with all its fields.
6. **`60-items-inventory.md`.** Item data assets/tables, inventory component(s) and their
   add/remove/use functions, currency (tar/money equivalents), drops, merchants, upgrades,
   crafting.
7. **`70-flow-menus.md`.** Main menu, continue/new game, pause, death screen, respawn,
   loading screens, cutscenes/sequencers (search LevelSequence, Cinematic, Cutscene),
   credits. For each: classes and the functions/events that start and end it (these are the
   moments the mod must pause sync or hand over control).
8. **`80-anim-catalog.md`.** A CSV-like table of every `AnimSequence` and `AnimMontage` under
   `/Game/Sparta/Characters/` (path, owning character folder, guessed category from the path:
   locomotion/attack/dodge/hit/death/idle/interact/other). Script this (Python is fine), do not
   do it by hand.

## Step 9: verify in game (only after steps 1-8)

Using `ms2-coop\lab.ps1` and the CoopLab mod, for the 10 most important objects you found
(a door, a lever, a chest, an item pickup, a checkpoint, one enemy, one boss trigger, the
inventory component, a shell swap, a level travel), read their key properties before and after
doing the action in game, and log the values. Write `90-verified.md`: object, action, property
values before/after, the log lines. If the lab cannot do an action, say so and skip it.

## Final deliverable

`research\README.md` with: the list of files, for each area the 5 most useful classes/functions
for syncing it across two players, and a list of open questions. Keep every file under ~3000
lines; split big tables into `-part2` files. Put helper scripts in `research\scripts\`.
