# Round 5 — BODY agent (`<you>` = body, copy = `G:\Freespace\shell_coop\wt\body\ms2-coop\`)

Read `native\briefs\R5-COMMON.md` first (in your copy). Two features: **zone identity + mismatch guard** and
**the friend's body state (health, death, resources)**. Gap register: `docs\HANDOFF-REMAINING-SYNC.md` G9 (zone
row "Level instances") and G4; data: `research\mine\SYNC-DATA-MINE.md` §7 (attribute byte map, health component),
§8 (BPSO_PlayerSession_C), §17.3/17.5 (zones, level instances).
Put the logic in a new `native\src\coop_body.cpp` (and `coop_zone` part in it or `coop_zone.cpp`), declarations
in a marked block of `coop_state.h`, minimal marked hooks in `coop_net.cpp`.

## A. Zone identity and mismatch guard
Problem: every world/quest key is a per-map actor name; if the players are in different maps, nothing syncs and
nothing says so.
1. Publish `"zn":"<persistentMap>|<zoneId>|<liHash>"` at 2 Hz content (the field may ride every publish):
   - persistentMap = the world's package path (e.g. `/Game/.../Hub`), from the UWorld;
   - zoneId = `USpartaZoneData.ZoneId` (FName @0x30) of the highest-priority active zone volume
     (`USpartaZoneVolumeManager.ZoneVolumeStack @0x40`, or `HighestPriority…`), empty if none — informational;
   - liHash = a short hash of the sorted names of loaded level instances / streamed sub-levels that come from
     `BP_LevelInstanceSelector_C` selections (also log the plain list in sync.txt / log on change).
2. Expose in coop_state.h: `namespace zone { bool SameMap(); bool PeerKnown(); const std::string& Local();
   const std::string& Peer(); }`.
3. Guard: when `PeerKnown() && !SameMap()`, pause every *apply* of peer world state (store A/B, ledger, quests,
   data layers, lifts, chests, enemy mirroring/ownership/spawn replay/aitarget, peer damage) and hide the puppet;
   keep publishing. Implement the gate centrally in coop_net.cpp's router / apply dispatch (marked blocks), not
   inside other modules' files. Log `zone mismatch: <local> vs <peer> -- world apply paused` once per change and
   `zone match: <map> -- world apply resumed`; sync.txt line `zone=me:<..> peer:<..> same:<0|1> li:<same|diff>`.
4. Level-instance divergence (same map, different liHash): investigate whether the two clients can pick
   different dungeon variants from the same `BP_LevelInstanceSelector_C` (roguelite selection via
   `USpartaRunManagerSubsystem.LevelInstanceSelected`). If yes, try a host-authoritative fix: guest loads the
   host's chosen instance (`LoadLevel`/`SelectLevel` on the matching selector) — lab-verify one selector; if it is
   not safely doable, only detect + report (`li:diff` + log), and write down what you found.
5. Test verb `zonefake <string|off>` overrides the local map string (forces a mismatch). Verdict `ZONE`:
   both clients report same:1 at start; with `zonefake` on B, A logs the pause and its apply counters stop
   moving; after `zonefake off` they resume. Kill switch `ZONE_OFF.txt` (no guard, still publish).

## B. The friend's body state
Nothing about the player's body crosses the wire today except transform/look/anim/weapons.
1. Publish `"bh":[hp,hpMax,shp,shpMax,res,resMax,heal,healMax,pp,ppMax,ammo,ammoMax,dark,ds]`, values refreshed at
   2 Hz (and immediately when ds changes), 3 decimals max:
   health/shell health/resolve from `USpartaHealthSet` (via `ASpartaCharacter.HealthComponent @0x6A0` →
   `HealthSet @0x120`; CurrentValue = member+0xC), heal charges + PP from `USpartaPlayerAttributeSet`,
   sidearm ammo from `USidearmAttributeSet`, dark form (`BPSO_PlayerSession_C.IsInDarkform` or the pawn's
   dark-form state — find the live source), `ds` = `USpartaHealthComponent.DeathState @0x128` (0/1/2).
   Prefer the health component's getters (`GetHealth`, `GetShellHealth`, `GetDeathState`…) where they exist;
   for attribute sets locate them on the ASC (`AbilitySystemComponent @0xAA8`) — verify every offset live before
   trusting it, log the first read.
2. Expose `namespace body { struct State {...}; const State& Local(); const State& Peer(); double PeerAge(); }`.
3. Receiver:
   - write the peer's health/shell health/max values into the PUPPET's own health set (the puppet is
     `puppet::Actor()`; `combat::SetHealthNorm` shows how health is written today) so the game's own UI/AI see it —
     but make sure this does not fight the existing `pd` (peer damage) / PEERHIT logic; read coop_combat.cpp first;
   - **peer death visual**: when the peer's `ds` goes ≥1, play a player death montage on the puppet (instead of
     hiding/freezing it; find the death montage the player's death ability uses, e.g. via the `mseq` the line
     already carries during death, `/Game/MS1/Characters/DeathsLibrary/...`), keep the body lying until ds returns
     to 0, then restore normal puppet drive. The DEATH agent (parallel) handles respawn/hold rules and will
     publish `"lv"`; you only do the visual from `ds`;
   - sync.txt line `body=me:hp<..>/<..> shp.. res.. heal.. ds.. peer:… age:<s>`.
4. Test verbs: `bodyset <field> <value>` (writes my own attribute through the proven ImportText/attribute route,
   e.g. `bodyset hp 0.37` as a fraction of max) and `bodydump`. Verdicts: `PEERHP` (B sets hp/resolve/heal, A
   receives within 3 s within tolerance, and A's puppet health set matches), `PEERDEAD` (B's ds≥1 → A's puppet plays
   the death montage; use the DEATH agent's `die` verb only if present — otherwise drive ds through the game's own
   `StartDeath` on B and restore B after; coordinate nothing, just make the verdict self-contained).
   Kill switch `BODY_OFF.txt`.
