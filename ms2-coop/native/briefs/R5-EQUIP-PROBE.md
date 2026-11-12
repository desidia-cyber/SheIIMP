# Round 5 — equipment probe (SYNC-DATA-MINE P5), 2026-10-25

Verb `equipprobe [apply|revert|dfapply|dfrevert]`, `native\src\coop_probe.cpp`. Three lab runs on A:
`local\lab\20261025-083601-equipprobe`, `...-084016-equipprobe2`, `...-084432-equipprobe3`.

## Result: the CDO route works, reads and writes, no crash
- `FindObject("/Game/Sparta/Core/Player/BPFL_Player_Equipment.Default__BPFL_Player_Equipment_C")` resolves
  (class `BPFL_Player_Equipment_C`) and `ue::Call(cdo, fn, params)` executes its functions with the pawn as
  `__WorldContext`.
- Reads returned real data: `GetNumberOfWeaponsUnlocked` → unlocked=1 total=8; `GetUnlockedMasks(CharacterId)`
  → success=1 `[Sparta.Item.Mask.FlowerCrown]`; `GetDarkFormMask(DarkFormID)` → the current mask.
- Write: `ChangeDarkFormMask(CharacterId.Player.Darkform.StrongOne, Sparta.Item.Mask.FlowerCrown, Save=false)`
  → read-back `mask now Sparta.Item.Mask.FlowerCrown`; then `ChangeDarkFormMask(..., None)` → `mask now None`.
  The game kept running; no fault logged.
- Tags are passed as a bare FName (`ue::MakeFName("Sparta.Item.Mask.FlowerCrown")` / read back via ToString) —
  FGameplayTag = {FName TagName}. FGameplayTagContainer out-params are readable as TArray<FName> at +0.

## Caveats
- The test save's character is in dark form (`ASpartaCharacter.CharacterId` = `CharacterId.Player.Darkform.StrongOne`),
  no shell equipped, so `GetShellSkin`/`GetShellMask`/`GetAllSkinsForShell` return None/empty here. Shell
  functions are untested on a shelled character.
- Visual not confirmed: the off-screen camera is behind the character and the before/after screenshots show no
  obvious crown. The equipment STATE changed (read-back); whether the mesh attaches immediately or on the next
  refresh is open. Next step for equipment sync: compare the pawn's attached components before/after, or watch
  the look (`lk`) signature on the peer.
- `Save=false` was used throughout; the owner's save was restored by lab.ps1.

## Implication
Real equipment sync (shell / skin / mask / clothing / weapon / dark form) is possible through
`BPFL_Player_Equipment_C`, keyed by gameplay-tag names, instead of the current visual-only mesh copy. Applied to
the PUPPET it would need the puppet to be a player character with a CharacterId; applied to our own pawn it would
be wrong (per-player equipment). So the likely design is: publish the peer's equipment tags, and have the puppet
render them (either via these functions on a puppet that is a BP_PlayerCharacter_C, or via
`GetShellItemDefinition`/`GetMaskObjects` to resolve tag → mesh for the existing look path).
