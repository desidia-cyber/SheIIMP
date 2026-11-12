# Report template — MS2 co-op research

Every report in `research/` follows this shape. The reader is an engineer writing the
sync code, so the value of a report is *facts with evidence*, not prose.

## Rules

1. **No fact without evidence.** For every class/asset row, paste 1–3 lines copied
   verbatim from the dump (the `research\scripts\parse_dump.py raw` output is
   byte-identical to the original `UE4SS_ObjectDump*.txt` lines).
2. **Never guess silently.** A guess must be labelled `GUESS:` and be obviously a guess.
   If you could not find something, write `UNKNOWN` plus one line about what you tried.
3. Function signatures: **full parameter list, in dump order**, every parameter with
   name and type, including out parameters and `ReturnValue`.
4. Delegates: `MulticastInlineDelegateProperty` / `DelegateProperty` lines are events —
   give the property name, its offset and the delegate signature if the dump has the
   `DelegateFunction` / `Signature` objects.
5. Tables, not paragraphs. Group by kind (classes / data assets / structs / enums /
   components / functions).
6. Paths are copied from the dump, e.g.
   `/Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C`.
   Never shorten a path, never invent a path that is not in the dump.
7. Keep each file under ~3000 lines; split a big table into `-part2`, `-part3`.

## Table shapes

Class table:

| Class | Parent | Purpose (from names) | Sync relevance |
|---|---|---|---|
| `/Game/.../BPSO_Door.BPSO_Door_C` | `/Script/Sparta.BPSOSaveObject` | door | open/closed state |

Property table:

| Class | Property | Type | Offset | Notes |
|---|---|---|---|---|
| `BPSO_Door_C` | `bOpened` | `BoolProperty` | `0x2a0` | |

Function table:

| Class | Function | Parameters (name: type) | Notes |
|---|---|---|---|
| `SpartaSkeletalMeshComponent` | `SetSkeletalMeshAsset` | `NewMesh: ObjectProperty of SkeletalMesh` | |

Evidence block (verbatim dump lines, in a fenced code block):

```
[00000291B2C7FB00] StructProperty /Script/Sparta.PlayerAttributeSet:CurrentEXP [o: 30] ...
```
