# How to query the raw data (read this before writing a report)

All raw material lives in `G:\Freespace\shell_coop\research\raw\`:

| File | What it is |
|---|---|
| `ObjectDump_allassets.txt` | 2 170 742-line UE4SS object dump, taken at the **main menu** with `LoadAllAssetsBeforeDumpingObjects = 1` (565 MB source, `research\runs\20261022-060536-obj\`). Every UObject in the build. |
| `index.sqlite3` | SQLite index of the file above, 2 153 654 rows, built by `research\scripts\parse_dump.py`. **Use this, do not grep the 565 MB text file.** |
| `ObjectDump_ingame.txt` | second dump taken in gameplay (`research\runs\...-ingame\`) — use it to see which objects actually exist in a live world (level actors, spawned components). |
| `CXXHeaderDump\*.hpp` | 10 415 UE4SS CXX-generator headers: class blocks with **member offsets**, struct layouts, function parameter lists in declaration order. `Sparta.hpp` (8 319 lines) = the whole native `Sparta` module; `Sparta_enums.hpp` = every enum; one small `.hpp` per Blueprint class (`BPSO_Gate.hpp`, `WBP_MainMenu.hpp`, …). |

## The object dump's line format

```
[000001F9EC0B1900] NiagaraDataChannelHandler_Islands /Game/Sparta/Maps/...:NiagaraDataChannelHandler_Islands_2147455356 [n: 39A79] [c: 00007FF46EF73738] [or: 000001F4BFC22A10]
```

First field = the object's class name as text. The important values:

* `Class` — a native C++ class (`/Script/Sparta.SpartaHealthComponent`).
* `BlueprintGeneratedClass`, `WidgetBlueprintGeneratedClass`, `AnimBlueprintGeneratedClass` —
  **Blueprint classes**. This is how every `/Game/...` class appears. Do not filter on `Class` only.
* `Function` — a UFUNCTION of the class named before the `':'`.
* `*Property` — a UPROPERTY (member of the class) **or** a parameter of the function named before the `':'`.
  A parameter is simply a property whose owner is a function.
* Asset objects appear under their own class name, e.g. `AnimSequence /Game/.../A_Shared_Idle_H.A_Shared_Idle_H`,
  `DataTable`, `SkeletalMesh`, `StaticMesh`, `Blueprint`, `CurveTable`, `GameplayTag`.

## Tool 1 — `research\scripts\parse_dump.py`

```
python scripts\parse_dump.py classes raw\index.sqlite3 --regex "door|gate" [--under /Game/Sparta]
      -> TYPE<tab>PATH<tab>PARENT-CLASS for every class (native + Blueprint), with its parent class
python scripts\parse_dump.py show raw\index.sqlite3 <path-substring>
      -> for every object whose path contains the substring: parent, properties (type + offset),
         functions with ALL their parameters in dump order
python scripts\parse_dump.py fun raw\index.sqlite3 <regex-on-class-path>
      -> only functions with their full parameter lists
python scripts\parse_dump.py find raw\index.sqlite3 <regex-on-path> [--types Class,Function,...]
      -> object paths only
python scripts\parse_dump.py raw raw\index.sqlite3 <regex-on-path> [--limit N]
      -> the ORIGINAL dump lines (byte-identical).  *** this is your evidence source ***
python scripts\parse_dump.py members raw\index.sqlite3 <path-substring>
```

Run these from `G:\Freespace\shell_coop\research`. Note the tool is run with Windows PowerShell:
avoid `|` to a file with `>` (that writes UTF-16); use `| Set-Content -Encoding UTF8`.

## Tool 2 — `research\scripts\q.py` (raw SQL)

```
python scripts\q.py raw\index.sqlite3 scripts\q\my-query.sql [--limit N]
```

Put SQL in a `.sql` file (PowerShell mangles inline quoting). Tables:

```
obj(addr TEXT, type TEXT, path TEXT, owner TEXT, super TEXT, ref TEXT, off INTEGER, rest TEXT, line INTEGER)
cls -- view: every class-like object, columns (addr, type, path, super)
```

* `owner` = address of the Outer of the row: for a member it is the class/struct/function address.
* `super` = `[sps:]` SuperStruct (a class' parent). Join `obj s ON s.addr = UPPER(c.super)`.
* `ref`  = the referenced type: StructProperty's struct, ObjectProperty's class, ArrayProperty's inner, EnumProperty's enum.
* `off`  = `[o: xx]` member offset (hex int).
* `line` = line number in the dump (use `ORDER BY line` for dump order).
* Separate statements with `;;`.

Example — every save object with its parent:

```sql
SELECT c.path, s.path AS parent FROM cls c LEFT JOIN obj s ON s.addr=UPPER(c.super)
WHERE c.path LIKE '/Game/Sparta/SaveObjects/%' ORDER BY c.path
```

Example — a class' members with resolved types:

```sql
SELECT o.type, o.path, o.off, r.path AS refd FROM obj o LEFT JOIN obj r ON r.addr=UPPER(o.ref)
WHERE o.owner = (SELECT addr FROM obj WHERE path='/Script/Sparta.SpartaHealthComponent' AND type='Class')
ORDER BY o.line
```

## Reading functions correctly

* Real parameters are the properties owned by the `Function` object. Their order = dump order.
* Blueprint functions also carry **compiler temporaries**: their names start with `Temp_`,
  `CallFunc_`, `K2Node_`, `__Internal_`, `ExecuteUbergraph_`, `Goto_`, `ReturnValue` handled normally,
  `eb_`/`bpf_` prefixes. **Filter them out** and say in the report that compiler temporaries were
  filtered (this is a documented rule, not a guess).
* The object dump knows the *type* of each parameter (`IntProperty`, `BoolProperty`,
  `ObjectProperty of /Script/Engine.Actor`, `StructProperty of /Script/Sparta.FHitPayload`, …).
  It does **not** mark out-parameters. Use `CXXHeaderDump` for declaration order and the header
  comment `(size: ...)` for layout; a parameter the engine named `ReturnValue` is the return value.
* Delegates: `MulticastInlineDelegateProperty`, `MulticastSparseDelegateProperty`,
  `DelegateProperty` rows are game events/bindable delegates — always list them for a class that
  has them.

## CXX header layout

```
class USpartaHealthComponent : public UGameFrameworkComponent
{
    FSpartaHealthComponentOnDamageReceived OnDamageReceived;   // 0x00B8 (size: 0x10)
    void SpartaDamageReceived(class USpartaHealthComponent* HealthComponent, float Value, class AActor* Instigator, const FGameplayEffectSpec& EffectSpec);
    ...
};
```

Properties come first with offsets, then the functions. Struct layouts are at the top of
`Sparta.hpp`, enums in `Sparta_enums.hpp`. Blueprint classes have their own small header.

## Evidence rules

* Copy evidence lines from `parse_dump.py raw` output **exactly**, 1–3 per class/asset entry.
* A path you write down must be a path that appears in the dump (or a `.hpp` file name).
* Anything you could not establish: write `UNKNOWN` and one line about what you tried
  (the query you ran / the pattern you used). Never fill a gap with a plausible guess.
* Guesses that are clearly guesses get the prefix `GUESS:` inside the text.
