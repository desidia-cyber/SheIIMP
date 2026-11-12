# Round 9 sync plan

Sources: `research\R9-GAP-SHELLMATE.md` (35 rows), `research\R9-GAP-WIKI.md` (81 rows, S-numbers below), real-area and quest
tests (`wt\r9-realarea\REPORT.md`, `wt\r9-quest\REPORT.md`).
The wiki has no quest or mechanics pages and marks some content as unpublished, so priorities come from the wiki's structure plus
the mod's code. Gaps not yet measured in game are marked PROBE: run the probe before writing the fix.

Unchanged rules: inventory, gold, Gloom, levels, bond and Shell points, weapon levels, tarstones and achievements stay per player.
Death logic stays close to DollsMP. Every new behaviour gets a kill switch and a `run-two` verdict.

## What the real tests showed

| Area | State | Consequence for the plan |
|---|---|---|
| Real mouse attacks | Work on both players; kills proven on both | All combat tests now use `real-input.ps1`. Ability calls do not start a swing |
| Drops after a real kill | Lane never ran (`rolled:0`) | Either hub cultists have no loot, or a player kill misses the hooks the lab kill used. Settle first |
| Boss | `BP_DwarfBoss_C` seen on both at landing area 2, same thresholds; health did not move, `aggro=0` | The fight was never engaged. Nothing about a live boss fight is proven yet |
| NPC | Real `E` press at the NPC opens no dialogue; `StartDialogue Success=0` | Conversations are blocked by a game content check in this save, or the wrong key; the prompt was never seen |
| Multi-stage quest | Stage, objectives and text match both ways (A and B driving, g/h and C/D for A); late-join bug fixed | Done, except viewing the journal tab on screen and the C/D fault under Known leftovers |

## Order of work

Each phase unblocks the next.

### Phase 1: real test ground
1. A save that is further along. Without it, boss and NPC tests stay blocked. See the owner decision below.
2. Engage `BP_DwarfBoss_C`: walk into the arena by real input, find what sets aggro. This also answers arena entry (S45).
3. Find the NPC dialogue check: read the narrative tag `StartDialogue` wants, and the real interact key.
4. Settle the drop question: kill a known loot enemy with real input; compare with the lab kill path.
5. Journal tab: drive the menu's virtual cursor.

### Phase 2: play together across the map (biggest player-facing gap)
1. **Follow the partner (S72, S73, S54).** Today the zone guard only pauses sync when players are in different zones. Add a "travel
   to partner" action, and bring both players into the same dungeon variant. `SelectLevel()` picks at random, so the host's choice
   must be forced on the guest.
2. **Beacons (S25, S51-S53).** Share the cleansed state; each player keeps their own respawn point. Needs owner decision below.
3. **Boss arena entry (S45).** When one player crosses the fog wall, bring the other in or let them cross; the current sync only covers
   the `BossGate.Unlocked` flag. Decide after Phase 1 item 2 shows what the game does.

### Phase 3: read the partner in combat
1. Harden, guard, parry and stone form (S18, seal slot S9): add a small event wire key; show the state on the puppet.
2. Hit reactions (S21): mirrored health is a number write and raises no damage event. Play the reaction on the copy.
3. Enemy posture and break (S19).
4. Player conditions and their VFX (15 conditions, none on the wire). Replay effects by class, because R8 found no channel for
   Niagara effects to copy.
5. Shell ability FX and Shell resource meters (Heat, Shadow, Pain, Faith, Specimen; Miracle's time stop). PROBE each: `mseq` already
   replays the montages, so only the effects that are missing need code.
6. Tarstone infusion look on weapons: `ApplyWeapons` attaches meshes without materials.

### Phase 4: shared encounters and world objects
1. Horde packs, shared health bars, non-proximity spawners (S47-S49).
2. Puzzles (S28, S29, S33, S38, S39): plates, bells, lanterns, illusory walls, light counters. These are not save flags, so store A
   cannot carry them. Each object type needs an event.
3. Moving objects (S31): `MovedToEnd` is not written because flipping it does not move the mesh. Call the object's own move function.
4. Quest item hand-ins and NPC reactions (S67).

### Phase 5: session safety and small divergence
1. Send Ova and NG+ (S61, S71): the one irreversible action. Add a partner confirmation before it runs.
2. Partner revive and the Shell revive meter. Only if it fits the DollsMP death rule; see owner decision.
3. Night Mode, difficulty modifiers (S74, S75), map objectives (S69), shop stock (S60, PROBE: one count settles whether it differs).
4. Sheep transformation (S12): test only; the body sync may already cover it.

### Known leftovers
- C/D install pair: with B driving, quest data did not reach the host (host `wq peer=0`), 1 failed run and 2 startup aborts. The same merged DLL passed with B driving on g/h, so the merge is not the cause. Host log for the walk was overwritten by the late-join restart; rerun on C/D keeping that log.
- `QUESTSTAGE_OFF.txt`: unproven. Both off runs ran on C/D and failed in the same way as the fault above, so they say nothing about the switch. Retest on g/h.
- Harness: `quest-stages.ps1` takes the game lock itself and leaves a relay running; `Start-Process -Wait` hangs on it.
- Appearance stutter: 15-50 ms per update, up to about 1 s at first load. Fix with background loading, in Phase 3, when more effects load.
- `QUESTSTAGE_OFF.txt` and other round 9 lab verbs ship in the DLL, like earlier rounds' verbs.

## How to run it
- One agent per item, own install pair and port, as in rounds 8 and 9. At most 3 game-running agents at once: lock waits cost more
  than they save.
- Combat, boss and NPC work must prove results with `real-input.ps1`, not lab damage. The final check in each phase is a screenshot on
  both sides.
- Merge and run `run-two` + `death-chain` on C/D after each phase.

## Owner decisions needed
1. **Test save.** Provide a save past the early game, or let agents build one with lab tools? Your own save stays untouched either way;
   a copy in a test install only. A real save is the faster route to bosses and NPC conversations.
2. **Beacons.** Recommended: cleansing is shared; each player keeps their own respawn beacon.
3. **Follow the partner.** Recommended: a manual "travel to partner" action at a beacon, not an automatic pull.
4. **Partner revive.** It is not in DollsMP's death rule as we use it. Recommended: leave it out for now.
