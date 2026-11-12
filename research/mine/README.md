# research\mine — the MS2 co-op sync data mine

Everything in this folder is **derived from dumps that already existed** on 2026-10-22. No game
process was started, no pak was unpacked, and **no file under `ms2-coop\` was modified** to produce
any of it.

* **Start here → [`SYNC-DATA-MINE.md`](SYNC-DATA-MINE.md)** — the master document: every class, member
  offset, byte-level read, function signature, enum value, asset and count needed to sync each area,
  each with its proof line. 28 sections, ~1 560 lines.
* [`sync-map.md`](sync-map.md) / [`sync-map.tsv`](sync-map.tsv) — the curated target map (837 entries)
  in machine-readable form.
* The rest is raw material; the manifest table is §26 of the master document.

## What is in here

| File group | Rows | What it is |
|---|---|---|
| `SYNC-DATA-MINE.md` | ~1 560 lines | the master document |
| `sync-map.tsv` / `.md` | 837 | area → class → member → offset or signature → proof line |
| `saveobjects-matrix.tsv` / `.md` | 57 | every save-object class with its full field list + offsets |
| `topic-01..19-*.props.tsv` | 8 071 | every property of every class in the topic, with offset + dump line |
| `topic-01..19-*.funcs.tsv` | 14 665 | every function with ordered parameters + UFunction flags |
| `topic-*.structs.tsv` | per topic | struct layouts referenced by those properties |
| `enums.tsv` / `enums-sparta.tsv` | 17 705 / 498 | enum members with explicit values |
| `structs.tsv` / `structs-sparta.tsv` / `structs-ue4ss.tsv` | 43 842 / 669 / 43 920 | struct fields from two independent dumps |
| `structs-crosscheck-conflicts.tsv`, `classprops-crosscheck-conflicts.tsv` | 0 / 0 | proof that the two dumps agree on every offset they share |
| `funcflags.tsv` | 51 566 | `BlueprintCallable` / `Static` / `BlueprintPure` … per function (1 766 Sparta) |
| `native-classes.tsv` / `.md` | 504 | every `/Script/Sparta.*` UClass with its super chain |
| `savecomponent-owners.tsv` / `.md` | 130 | every class that owns a `USpartaSaveComponent` |
| `live-objects.tsv` | 9 566 | per-class object count in the live gameplay dump |
| `datatables.tsv` / `.md` | 260 | every DataTable asset (row names need probe P1) |
| `item-assets.tsv` / `.md` | 393 | the item-definition id space |
| `wire/out_A.txt`, `wire/out_B.txt` | 2 850 / 4 155 B | the real published payloads, byte-exact |
| `wire-fields.tsv` / `wire-field-sizes.tsv` | 61 / 31 | the wire schema and per-key byte cost |
| `hashes.tsv` | 79 | SHA-256 of every artefact and of both indices |
| `manifest.json` | — | file list with row counts |

## How to regenerate (Windows PowerShell, from `G:\Freespace\shell_coop\research`)

```powershell
python scripts\mine.py enums          # enums.tsv
python scripts\mine.py structs        # structs.tsv
python scripts\mine.py flags          # funcflags.tsv
python scripts\mine_cxx_structs.py    # structs-ue4ss.tsv
python scripts\mine.py gen            # the 19 topic files + sync-targets.tsv
python scripts\mine_extra.py          # Sparta views, class lists, live census, matrix, hashes
python scripts\mine_map.py            # sync-map.tsv + sync-map.md
python scripts\mine_wire.py           # wire/*.pretty.json + wire-fields.tsv
python scripts\tsv2md.py <file.tsv>   # render any artefact as a markdown table
python scripts\validate_mine.py       # consistency checks (must end with ALL CHECKS PASSED)
```

Ad-hoc lookups (the tool the whole mine was built with):

```powershell
python scripts\mine.py cls  <regex>        # classes matching a path regex
python scripts\mine.py show <class path>   # members of one class, with offsets
python scripts\mine.py tree <class path>   # inheritance chain, members per level
python scripts\mine.py fun  <class regex>  # functions with ordered parameters
```

## Verification status

| Check | Result |
|---|---|
| Dumper-7 vs UE4SS struct offsets (21 356 shared fields) | **0 conflicts** |
| Dumper-7 vs UE4SS class property offsets (2 197 shared properties) | **0 conflicts** |
| `validate_mine.py` (encoding, table widths, artefact references, dump-line ranges, TSV widths) | **ALL CHECKS PASSED** |
| Residual validator warnings (2) | cross-class name collisions inside the checker's own heuristic: `AbilitySystemComponent` exists on both `SpartaCharacter` (0xAA8) and `SpartaHealthComponent` (0x118); `NarrativeTags` exists on both `BPC_QuestLogComponent_C` (0x258) and `BPSO_QuestLogComponent_C` (0xC0). All four offsets were confirmed by hand against the dump |

## What is *not* in here

Ten questions cannot be answered by static mining; they are listed with exact probes in §25 and with
ready-to-paste snippets in §28 of `SYNC-DATA-MINE.md`. The two that block the most work are DataTable
row names (P1, solved by calling `UDataTableFunctionLibrary::GetDataTableRowNames` on its CDO) and the
gameplay-tag string table (P2).
