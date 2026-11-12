# 05 — Dumper-7 SDK dumps, cross-checked against the UE4SS dump

A second, independent SDK dump of the same binary, made with **Dumper-7**, and a mechanical
diff against the UE4SS CXX dump that `10-…`–`80-…` were written from.

The first attempt with the upstream **v7.1.0 release DLL** produced an SDK only at the main
menu; the two heavier states failed. Both failures were then traced to concrete upstream bugs
and fixed in a **local build of the same source tree** (§3), after which *all three* states
dump successfully — including an **all-assets dump with 16 040 classes**.

| Question | Answer | Evidence |
|---|---|---|
| Did Dumper-7 run on this game? | **Yes** — five runs on `E:\Games\_MS2-coop-test` on 2026-10-22 | `research\runs\20261022-17*…`, `…-1803…`, `…-1954…`, `…-2007…`, `…-2014…` |
| Did the upstream release DLL work? | Only at the **main menu**; all-assets → `GObjects couldn't be found` + `0xC0000409`; gameplay → stall after `Detected 4 GWorld` | §2.2, §2.3 |
| Do the failures have a root cause? | **Yes, three of them**, all in Dumper-7's own code and all fixed (§3) | §3 |
| Does a patched build dump everything? | **Yes** — all-assets dump: 18 893 files / 239.4 MB / 16 040 classes, generation 56 387 ms | `raw\Dumper7_allassets`, run `20261022-200712` |
| Do the dumps agree with UE4SS about layout? | **Yes** — 78 740 property slots compared in the all-assets pair, **0 conflicts**; parents 0; class sizes 0 (only alignment rounding) | §7 |
| Did Dumper-7 gain function RVAs / vtables? | **No** — `*_functions.cpp` resolves through `Class->GetFunction`; `0` matches for `0x[0-9A-Fa-f]{6,}` in `Sparta_functions.cpp` | §9 |

---

## 1. Tool and provenance

### 1.1 Upstream release (used for runs 1–3)

| Fact | Value |
|---|---|
| Project | Dumper-7, <https://github.com/Encryqed/Dumper-7> |
| Release | **v7.1.0**, published 2026-10-16 |
| Binary | `tools\dumper7\Dumper-7.dll`, 1 564 672 B, SHA-256 `5B13729169C69A8FEAE5EB8813977619C3F54729F50362E453FBE68476FBADB9`, unsigned |

### 1.2 Local patched build (used for runs 4–5)

| Fact | Value |
|---|---|
| Source | `git clone --depth 1 https://github.com/Encryqed/Dumper-7.git` → `tools\dumper7-src`, commit **`90d1f390d26e31a1f71d29af56c6bd2f240b7493`** |
| Generator | CMake 4.4.3, `-G "Visual Studio 17 2022" -A x64`, MSVC 19.35.32217.1 |
| Language mode | `/std:c++latest` (the tree uses a C++23 *explicit object member function* in `CollisionManager.h:146`; `/std:c++20`, which the repo's `cmake/CompilerFlags.cmake` sets, is rejected by MSVC 19.35 — patch 5) |
| Binary | `tools\dumper7\Dumper-7-patched.dll`, 4 409 856 B, SHA-256 `09D774F705BCD375A46CF25C83929AA75178E99F95C92DDA338E5D170329524D` |
| Patches | 6 files, `tools\dumper7-patches.diff` (19 700 B) — listed in §3 |
| Injection | own injector `research\scripts\inject-dll.ps1` (LoadLibraryW via CreateRemoteThread; remote `kernel32.dll` base resolved with `EnumProcessModulesEx`/`GetModuleFileNameEx`, so per-process ASLR does not matter) |
| Orchestration | `research\scripts\run-dumper7.ps1` — snapshots/restores the owner saves exactly like `run-dump.ps1`, stages `Dumper-7.dll` + `Dumper-7.ini` into `…\Binaries\Win64`, drives `ms2-coop\lab.ps1`, injects, waits for the SDK to stabilise, kills the game, collects, restores ini/saves |

`Dumper-7.ini` used by the runner:

```ini
[Settings]
SleepTimeout=2
ShouldAutoUnload=1
SDKGenerationPath=<absolute staging dir>
LogFilePath=<same dir>/dumper7.log
ExecuteSDKTestScript=0
```

Only the **test copy** `E:\Games\_MS2-coop-test` was used; the real install
`E:\Games\Mortal Shell II` was never started. No game file is patched: the DLL and the ini are
added next to the exe and removed again by the runner (`run.log`: `UE4SS ini restored,
Dumper-7.dll/ini removed`).

---

## 2. The five runs

| # | Mode | DLL | Result | Time | Where |
|---|---|---|---|---|---|
| 1 | main menu | upstream v7.1.0 | **OK** | 25 082.8 ms | `runs\20261022-175220-dumper7-menu\` |
| 2 | menu + all-assets load | upstream v7.1.0 | **FAIL** — `GObjects couldn't be found`, game fastfailed | — | `runs\20261022-175635-dumper7-allassets\` |
| 3 | gameplay | upstream v7.1.0 | **FAIL** — stall after `Detected 4 GWorld` | — | `runs\20261022-180334-dumper7-gameplay\` |
| 4 | menu + all-assets load | patched build | **OK** | 56 387 ms | `runs\20261022-200712-dumper7-allassets\` |
| 5 | gameplay | patched build | **OK** | 27 244.7 ms | `runs\20261022-201423-dumper7-gameplay\` |

(Run 4 was preceded by `runs\20261022-195414-dumper7-allassets\`: the same mode with only
patches 1–3, killed by the runner's quiet-timer after `CppSDK/` was complete but the extra
generators were still running — that run is what showed patch 4 was needed.)

### 2.1 Run 1 — main menu (upstream DLL), success

`research\runs\20261022-175220-dumper7-menu\run.log`:

```
[17:53:01] injection point reached: menu_ready.txt (pid 44572)
injected Dumper-7.dll into pid 44572; hModule=0x1F7A0000
[17:53:22] SDK size now 105,770,960 bytes
[17:53:37] SDK size now 201,110,372 bytes
[17:54:08] Dumper-7.dll unloaded from the game and the SDK stopped growing -> done
[17:54:30] saves unchanged
```

`dumper7.log` (same folder): `Found FChunkedFixedUObjectArray GObjects at offset 0xb03aef0`,
`Generating SDK took (25082.8ms)`, `Press F6 to unload`.

### 2.2 Run 2 — all-assets state (upstream DLL), failure and what it said

`dumper7.log`, in full (175 B):

```
Started Generation [Dumper-7]!
Dumper-7 by me, you & him
Searching for GObjects...
GObjects couldn't be found, please overwrite the offset in Generator.cpp.
```

Windows recorded the death 79 s later (Event ID 1000, Application log):

```
Faulting application name: MortalShell2-Win64-Shipping.exe, version: 5.6.1.0,
Faulting module name: ucrtbase.dll, Exception code: 0xc0000409, Fault offset: 0x000000000007286e
```

`0xC0000409` is `STATUS_STACK_BUFFER_OVERRUN` — the MSVC `__fastfail` path — which is why UE4SS
wrote no crash dump and the runner only saw "game process gone".

### 2.3 Run 3 — gameplay (upstream DLL), stall

`dumper7.log` (1 800 B, flushed only when the process was finally killed): every engine offset
resolved, then the last line

```
Detected 4 GWorld
```

after which nothing happened for 20 minutes while the game stayed responsive
(`Responding: True`, ~5 GB working set) and `Dumper-7.dll` stayed loaded.

### 2.4 Runs 4 and 5 — patched build, both succeed

`research\runs\20261022-200712-dumper7-allassets\dumper7.log`:

```
Found FChunkedFixedUObjectArray GObjects at offset 0xb03aef0
…
GWorld-Offset (pinned): 0xAD4E7F0
…
GameName: MortalShell2
GameVersion: 5.6.1-0+++Sparta-Depot+Day1+CL92086
Generating SDK took (56387ms)
Press F6 to unload
```

`research\runs\20261022-201423-dumper7-gameplay\dumper7.log`:

```
GWorld-Offset (pinned): 0xAD4E7F0
…
Generating SDK took (27244.7ms)
Press F6 to unload
```

Both runs ended with the dumper unloading itself (`Dumper-7.dll unloaded from the game and the
SDK stopped growing -> done`), i.e. generation completed without the kill-timer being needed.

### 2.5 Save protection

Every run snapshots `%LOCALAPPDATA%\MortalShell2\Saved\SaveGames` and hash-compares afterwards.
The two gameplay runs make the game write `WorldState_0.sav`; the runner restored it in place
(`SAVES CHANGED after the run -> restoring …`, then the file is byte-identical again) and the
game-created `WorldState_Backup_0_0.sav` was moved to
`local\save-backups\20261022-201423-dumper7-gameplay\extras-created-by-game\`. The folder is
back on the 06:01 baseline (§12).

---

## 3. Root causes and the patches (`tools\dumper7-patches.diff`)

All three failures are in Dumper-7's own code; nothing in the game is patched.

### 3.1 Patch 1 — the GObjects validator rejected any array with more than 20 chunks

`Dumper/Engine/Private/Unreal/ObjectArray.cpp`, `IsAddressValidGObjects(uintptr_t,
FChunkedFixedUObjectArrayLayout)`:

```cpp
	if (NumChunks > 0x14 || NumChunks < 0x1)          // upstream
		return false;
```

A chunked `FUObjectArray` grows in 65 536-element chunks (the engine default, and the value the
menu run resolves to). The all-assets session holds **2 170 742 objects** (line count of the
UE4SS dump of exactly that state), i.e. ≈ 33.1 chunks → `NumChunks` ≈ 34 > `0x14` = 20. Every
candidate was therefore rejected, the scan fell through to *"GObjects couldn't be found"*, and
the failure path is `Sleep(10000); exit(1)` — calling `exit()` from an injected thread while the
game is running is what produced the `0xC0000409` fastfail.

Patch:

```cpp
	/* Upstream allowed at most 0x14 chunks (about 1.3M objects at 0x10000 per chunk). A session
	   that loads every asset reaches ~2.1M objects / ~34 chunks, so every candidate was
	   rejected and the scan reported "GObjects couldn't be found". Raised to 0x1000. */
	if (NumChunks > 0x1000 || NumChunks < 0x1)
		return false;
```

Plus a fallback: if the scan still fails, the pinned offset `0xb03aef0` is validated against
every known chunked layout and used (`GObjects scan failed; using the pinned offset …`). It is
never reached in practice — the scan now succeeds — but it turns a hard crash into a checked
fallback.

### 3.2 Patch 2 — `InitGWorld()` scans the whole address space once per UWorld

`Dumper/Engine/Private/OffsetFinder/Offsets.cpp`:

```cpp
	for (UEObject Obj : ObjectArray())
	{
		if (Obj.HasAnyFlags(EObjectFlags::ClassDefaultObject) || !Obj.IsA(UWorld))
			continue;

		/* Try to find a pointer to the word, aka UWorld** GWorld */
		auto Results = Platform::FindAllAlignedValuesInProcess(Obj.GetAddress());
```

`FindAllAlignedValuesInProcess` walks the entire process address space. At the menu there is
exactly one interesting `UWorld`, and it resolves in a second; in gameplay the object array
contains several (`Detected 4 GWorld` in run 3), and each additional one costs another full
memory scan — that is the "hang".

Patch: try the pinned module offset first and validate it against the live array
(`Platform::IsBadReadPtr` + `UEObject(...).IsA(UWorld)`), and give the scan a budget of three
expensive scans if the pin does not validate:

```cpp
	if constexpr (D7GamePins::GWorldOffset != 0)
	{
		void** const Pinned = reinterpret_cast<void**>(Platform::GetModuleBase() + static_cast<uintptr_t>(D7GamePins::GWorldOffset));
		if (!Platform::IsBadReadPtr(Pinned) && !Platform::IsBadReadPtr(*Pinned))
		{
			UEObject PinnedWorld(*Pinned);
			if (!Platform::IsBadReadPtr(*reinterpret_cast<void**>(PinnedWorld.GetAddress())) && PinnedWorld.IsA(UWorld))
			{
				Off::InSDK::World::GWorld = D7GamePins::GWorldOffset;
				std::cerr << std::format("GWorld-Offset (pinned): 0x{:X}\n\n", Off::InSDK::World::GWorld);
				return;
			}
		}
		std::cerr << "GWorld pin did not validate against the live array; falling back to the scan\n\n";
	}
```

The pinned value (`0xAD4E7F0`) is the one the *upstream* run printed at the menu — read off this
exact binary, not guessed.

### 3.3 Patch 3 — the extra generators run after `CppSDK/` and iterate every object

`Dumper/main.cpp` runs four generators in sequence; the first (`CppGenerator`) writes `CppSDK/`,
the others (`MappingGenerator`, `IDAMappingGenerator`, `DumpspaceGenerator`) iterate the whole
object array again. With 2.1 M objects those took so long that the first patched all-assets run
looked stuck: `CppSDK/` was complete (all six final files written 19:59:23) while
`IDAMappings/` still held **0 files** at 20:02:43 and the dumper was still resident.

Patch: `if constexpr (!D7GamePins::bSkipExtraGenerators)`. We consume only `CppSDK/`.

### 3.4 Patch 4 — skip the two `GObjects` text dumps

`Generator::Generate<>()` writes `GObjects-Dump.txt` and `GObjects-Dump-WithProperties.txt`
before the SDK. In the all-assets session those were **292.5 MB and 317.9 MB** and duplicate the
UE4SS object dumps we already have (`00-sources.md` A/C). Patch:
`if (!bDumpedGObjects && !D7GamePins::bSkipGObjectsDumpFiles)`.

### 3.5 Patch 5 — build the tree with `/std:c++latest`

`cmake/CompilerFlags.cmake` sets `/std:c++20`, but `CollisionManager.h:146` uses a deducing-this
member function (`decltype(auto) GetNameReplacements(this auto&& Self, …)`), which MSVC 19.35
only accepts under `/std:c++latest`. Patch: `CMAKE_CXX_STANDARD 23` + `/std:c++latest`.

### 3.6 All pins in one place

`Dumper/D7GamePins.h` (new file) documents where each constant came from:

```cpp
	/* both values were READ OFF a successful Dumper-7 v7.1.0 run on this exact executable
	   (research\runs\20261022-175220-dumper7-menu\dumper7.log):
	       Found FChunkedFixedUObjectArray GObjects at offset 0xb03aef0
	       GWorld-Offset: 0xAD4E7F0 */
	constexpr std::int32_t GObjectsOffset = 0xb03aef0;
	constexpr std::int32_t GWorldOffset   = 0xAD4E7F0;
	constexpr int          MaxGWorldScans = 3;
	constexpr bool         bSkipGObjectsDumpFiles = true;
	constexpr bool         bSkipExtraGenerators   = true;
```

---

## 4. What Dumper-7 detected in this binary

From `research\runs\20261022-200712-dumper7-allassets\dumper7.log` (identical to the menu run
for every value, which is itself a consistency check):

| Item | Value |
|---|---|
| GObjects layout | `FChunkedFixedUObjectArray` at module offset **`0xb03aef0`** |
| `FUObjectItem` size | `0x18` |
| `FName::AppendString` | offset **`0x13F8FA0`** |
| `FNamePool` (GNames, lazy-init) | offset **`0xAF57380`** |
| `UObject` offsets | `Flags 0x8`, `Index 0xC`, `Class 0x10`, `Name 0x18`, `Outer 0x20` |
| `UStruct` / `UClass` | `SuperStruct 0x40`, `Children 0x48`, `ChildProperties 0x50`, `Size 0x58`, `MinAlignment 0x5C`, `StructBaseChain 0x30`, `CastFlags 0xD8`, `ClassDefaultObject 0x110`, `ImplementedInterfaces 0x1D8` |
| `UFunction` | `FunctionFlags 0xB0`, `ExecFunction 0xD8` |
| Property system | `FProperty` (`Game uses FProperty system`), `UPropertySize: 0x70`, `Offset_Internal 0x44`, `ElementSize 0x34`, `ArrayDim 0x30`, `PropertyFlags 0x38` |
| ProcessEvent | `PE-Offset: 0x1619320`, **`PE-Index: 0x4C`** |
| `FindFunctionChecked` | `0x1615240` |
| GWorld | **`0xAD4E7F0`** |
| FText | `TextSize 0x10`, `TextDatOffset 0x0`, `InTextDataStringOffset 0x20` |
| Engine / build | `GameVersion: 5.6.1-0+++Sparta-Depot+Day1+CL92086` |

Two items settle open questions from `00-sources.md` §1: the build tag
`++Sparta-Depot+Day1+CL92086` that `ms2-coop\docs\DESIGN.md` carried over is **proven** by a dump
line, and the engine is `5.6.1` (UE4SS itself only printed `5.6`). `PE-Index 0x4C` and the
`GWorld` offset are the two values a native-side hook needs.

---

## 5. Output per mode

`raw\Dumper7` (run 1, menu), `raw\Dumper7_allassets` (run 4), `raw\Dumper7_gameplay` (run 5) —
all three are `<CppSDK>` directories of the same shape:

| Item | menu | all-assets | gameplay |
|---|---:|---:|---:|
| files in `CppSDK` | 5 365 | 18 894* | 7 406 |
| size | 128.4 MB | 239.4 MB | 151.5 MB |
| `SDK\*_classes.hpp` (packages) | 2 329 | 9 893 | 3 367 |
| `SDK\*_structs.hpp` | 412 | 836 | 484 |
| `SDK\*_functions.cpp` | 1 323 | 4 106 | 1 791 |
| `SDK\*_parameters.hpp` | 1 291 | 4 050 | 1 755 |
| `SDK.hpp` | 202 241 B | 789 444 B | 287 355 B |
| `Assertions.inl` | 47 336 542 B | 96 619 333 B | 58 030 759 B |
| generation time | 25 082.8 ms | 56 387 ms | 27 244.7 ms |

\* 18 893 generated files + the `SDKTest.py` copy the build check needs (§6).

Per-class shape (verbatim from `raw\Dumper7_allassets\…\CppSDK\SDK\Sparta_classes.hpp`):

```cpp
// Class Sparta.SpartaSaveComponent
// 0x0068 (0x0120 - 0x00B8)
class USpartaSaveComponent : public UActorComponent, public ISpartaSaveInterface
{
public:
	TSubclassOf<class USpartaSaveObject>          SaveObjectClass;   // 0x00C0(0x0008)(Edit, BlueprintVisible, ZeroConstructor, NoDestructor, UObjectWrapper, HasGetValueTypeHash, NativeAccessSpecifierPublic)
	ESaveObjectProtocol                           SaveProtocol;      // 0x00F8(0x0001)(Edit, BlueprintVisible, ZeroConstructor, DisableEditOnInstance, IsPlainOldData, …)
	struct FGuid                                  Guid;              // 0x00FC(0x0010)(Edit, ZeroConstructor, EditConst, IsPlainOldData, NonTransactional, …)
	uint8                                         Pad_10C[0x4];       // 0x010C(0x0004)(Fixing Size After Last Property [ Dumper-7 ])
	class USpartaSaveObject*                      RuntimeData;       // 0x0110(0x0008)(ZeroConstructor, NoDestructor, HasGetValueTypeHash, NativeAccessSpecifierPrivate)
```

---

## 6. The built-in SDK build test

Dumper-7's release ends by running `CppSDK\SDKTest.py`, which builds the SDK **twice** — MSVC
and `-T ClangCL`. On this machine the second half cannot even be configured and the script stops
in a console:

```
SDK build test failed.
Could not select Visual Studio toolchains: No compatible Visual Studio 2022 or 2026 installation found for ClangCL. Install the Desktop development with C++ tools, and for ClangCL also the 'C++ Clang Compiler for Windows' + 'MSBuild support for LLVM (clang-cl)' toolset components. CMake must support the installed Visual Studio version (3.21+ for VS 2022; 4.2+ for VS 2026).

Press Enter to close...
```

Cause (`SDKTest.py:269-306`): `find_vs_toolchain()` asks `vswhere` for
`Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset` when the toolset is `ClangCL`; the machine
has only `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`. The SDK is written **before** that
test runs, so the failure is cosmetic — but it blocks a headless run, hence
`ExecuteSDKTestScript=0`.

The MSVC half is run separately with `research\scripts\d7_sdk_build_check.py`, which re-uses
Dumper-7's own `CMakeLists.txt` and `TestMain.cpp` (read out of `SDKTest.py`, nothing rewritten):

| SDK | result | log |
|---|---|---|
| menu (`raw\Dumper7`) | **compiled**: configure ok, build ok, `SDKBuildTest.dll` 25 600 B, 0 `error C`, 19 `warning C4369` (the game's own `enum …_MAX` values that overflow their integer type) | `local\sdkbuild\menu\SDKTest-MSVC\Logs\MSVC.log` |
| all-assets (`raw\Dumper7_allassets`) | **compiled**: same generator, 4 TUs against the 96.6 MB `Assertions.inl`, `SDKBuildTest.dll` 25 600 B, 0 `error C`, 19 `warning C4369` | `local\sdkbuild\allassets\SDKTest-MSVC\Logs\MSVC.log` |

The patched build does not ship `SDKTest.py` (its `bShouldGenerateSDKCompilationTestScript`
constant is `false`), so the all-assets `CppSDK` carries a copy of the script the upstream release
generated; the embedded `CMakeLists`/`TestMain` templates do not depend on the dump.

If the built-in test is wanted, install the VS component **"C++ Clang Compiler for Windows"**
(a machine change, not a repo change; not done here).

---

## 7. Cross-check against the UE4SS dump (`scripts\d7_compare.py`)

```
python research\scripts\d7_compare.py --d7 <CppSDK>\SDK --ue4ss research\raw\CXXHeaderDump --json <out>
```

Method (this is not a string diff):

* a **slot** is `offset → name`. UE4SS prints the raw `FName` (`Audio (HandL)`, `Weather Preset`),
  Dumper-7 prints the sanitised C++ identifier (`Audio__HandL_`, `Weather_Preset`), and Dumper-7
  renames the member-alias-frame to `UberGraphFrame_<Class>` where UE4SS prints `UberGraphFrame`;
  slots are therefore matched by **offset**, and a differing name is reported as a rendering
  difference, never as a layout conflict;
* **bitfields** (`uint8 bHidden : 1; // 0x0058(0x0001)(BitIndex: 0x07, …)`) are excluded from the
  slot comparison: Dumper-7 keeps the storage byte plus a `BitIndex`, UE4SS emits one pseudo-bool
  per bit at consecutive byte offsets;
* **class size**: UE4SS prints `// Size: 0xNNNN` (raw `UStruct::Size`), Dumper-7 prints
  `// 0xOWN (0xTOTAL - 0xBASE)` rounded to the class alignment; a delta equal to
  `roundup(size, 8|16)` counts as alignment, not as a disagreement;
* **class declarations**: Dumper-7 prefixes some with `SDK_ALIGN(0x10)` or `alignas(0x08)` and
  qualifies collided bases as `Namespace::Class`; the parser normalises both (this alone moved the
  all-assets class count from 15 833 to 16 040, so an earlier revision of this file undercounted).

### 7.1 Numbers

| Metric | menu (`raw\Dumper7`) | all-assets (`raw\Dumper7_allassets`) | gameplay (`raw\Dumper7_gameplay`) |
|---|---:|---:|---:|
| UE4SS classes (reference) | 16 070 | 16 070 | 16 070 |
| Dumper-7 classes | 8 525 | **16 040** | 9 441 |
| classes in both | 8 495 | **15 995** | 9 409 |
| **property slots compared** | 31 670 | **78 740** | 37 140 |
| **slot conflicts (same offset, different layout)** | **0** | **0** | **0** |
| bitfield members (compared separately) | 1 862 | 1 862 | 1 862 |
| parent-class mismatches | 0 | **0** | 2 (see below) |
| class-size mismatches, real | 0 | **0** | 2 (see below) |
| class-size mismatches, alignment rounding | 497 | 1 953 | 708 |
| class-size, interface rendering (`0x8` vs `0x28`) | 376 | 388 | 381 |
| same slot, name rendered differently | 563 | 3 265 | 893 |
| — member-alias-frame | 339 | 1 871 | 531 |
| — parentheses/space in the FName | 52 | 1 077 | 126 |
| — trailing `_` disambiguation | 57 | 74 | 60 |
| — other (collision suffixes) | 115 | 243 | 176 |
| slot only in Dumper-7 | 22 | 29 | 95 |
| slot only in UE4SS (bitfield pseudo-slots) | 583 | 583 | 671 |
| classes only in UE4SS | 7 575 | 75 | 6 661 |
| classes only in Dumper-7 | 30 | 45 | 32 |

JSON: `research\slices\d7-vs-ue4ss.json` (menu), `…-allassets.json`, `…-gameplay.json`.

The 2 parent / 2 size differences in the **gameplay** dump are the same two classes
(`ABP_Candles_Cluster_Med_01_C/_02_C`): the gameplay dump sees them parented to
`ABP_LightFixture_C` at `0x618/0x608`, the menu-time UE4SS dump as `AActor` children at `0x408`.
They only exist in the gameplay state (they are in the `only in UE4SS` list of the all-assets
pair), so this is a *state* difference between the two sources, not a dumper disagreement —
`GUESS:` the level uses a differently-parented variant of the same class name.

### 7.2 Enum values

`research\scripts\d7_enum_compare.py` compares the `enum class` blocks of Dumper-7's
`Sparta_structs.hpp` against UE4SS' `Sparta_enums.hpp`. All shared enumerator maps are
**identical, value by value**:

| SDK | Dumper-7 enums | UE4SS enums | shared | identical | differing |
|---|---:|---:|---:|---:|---:|
| menu | 105 | 100 | 98 | **98** | 0 |
| all-assets | 104 | 100 | 97 | **97** | 0 |

Names present only on the UE4SS side: `ECompareOperator`, `ETagMatchType`, `TargetTeam` (user
enums with `NewEnumerator` values that neither dumper resolves into display names).

---

## 8. Key classes: every offset the reports quote is confirmed by a second dumper

`d7_compare.py --key …` against the all-assets SDK (UE4SS size → Dumper-7 size, property slots
per class):

| Class | size | slots |
|---|---|---|
| `USpartaSaveComponent` | `0x0120` → `0x0120` | 9 → 9 |
| `USpartaHitPayload` | `0x0158` → `0x0158` | 27 → 27 |
| `USpartaSaveGame` | `0x0218` → `0x0218` | 7 → 7 |
| `USpartaSaveSubSystem` | `0x0158` → `0x0158` | 13 → 13 |
| `UPlayerInventoryComponent` | `0x00B8` → `0x00B8` | 0 → 0 |
| `USpartaItemManagerComponent` | `0x0248` → `0x0248` | 3 → 3 |
| `USpartaItemInstance` | `0x01E8` → `0x01E8` | 3 → 3 |
| `UPlayerSaveObject` | `0x0338` → `0x0338` | 11 → 11 |
| `UBP_SpartaPlayerSaveObject_C` | `0x0AB0` → `0x0AB0` | 59 → 59 |
| `UGA_PlayMontageBase_C` (gameplay-only class, absent from the menu SDK) | `0x061C` → `0x0620` | 12 → 12 |

Because the slot comparison is global (§7.1) and shows no conflicts, every offset in
`10-…`–`80-…` that came from `raw\CXXHeaderDump` now has a second, independent source — including
the class sizes.

### 8.1 Cross-check caught a wrong class name

Dumper-7 prints `class UPlayerInventoryComponent final : public UActorComponent` and the object
dump has
`[00007FF46F037F38] Class /Script/Sparta.PlayerInventoryComponent [n: 47C28] [c: 00007FF46EE55138] [or: 00007FF46F17CD68] [sps: 00007FF46EE71738]`.
The earlier reports called it `USpartaPlayerInventoryComponent` (a module-prefix artefact).
Corrected in `60-items-inventory.md`, `90-verified.md` and `README.md`; the facts (empty class,
size `0xB8`, zero members, no `PlayerInventoryComponent:*` property rows) are unchanged.

### 8.2 The slots that exist only in Dumper-7 (29 in the all-assets pair)

23 of them are exactly the fields Dumper-7 logs as its own `Off::…` scan results, and they are
**not** reflected properties (no `Property` object with that path exists in `raw\index.sqlite3`,
checked with SQL):

```
UClass::CastFlags 0xD8, UClass::ClassDefaultObject 0x110, UStruct::BaseChain 0x30,
UStruct::SuperStruct 0x40, UStruct::Children 0x48, UStruct::ChildProperties 0x50,
UStruct::Size 0x58, UStruct::MinAlignment 0x5C, UField::Next 0x28,
UFunction::FunctionFlags 0xB0, UFunction::ExecFunction 0xD8,
UEnum::Names 0x40, UDataTable::RowMap 0x30, ULevel::Actors 0xA0
```

A co-op implementation must not treat them as UPROPERTYs. The remaining 6 are Blueprint variables
that UE4SS dropped (no member at that offset in its header), e.g.
`UBP_CO_ItemStackCheck_C::Comparison @0x44` (UE4SS jumps from `ComparisonAmount @0x40` to
`OwnedAmount @0x48`) and `UWBP_QuestObjectiveNotification_C::Box_Objective_0 @0x3F0`
(UE4SS has `Box_Objectives @0x358`, `Box_Objective @0x360`). `GUESS:` name-collision duplicates
that UE4SS's generator drops.

---

## 9. What Dumper-7 adds, and what it does **not** give

Adds:

1. **padding** (`Pad_…[n]`) — every byte of every class is accounted for (7 124 `Pad_` entries in
   the menu SDK alone);
2. **property flags** per member (`Edit, BlueprintVisible, Net, Interp, NoDestructor,
   NativeAccessSpecifierPrivate, …`);
3. **`*_parameters.hpp`**: one struct per UFunction with all parameters in order and
   `Parm`/`OutParm`/`ReturnValue` markers — the cleanest source for parameter order;
4. **`Assertions.inl`**: 47–97 MB of `static_assert(offsetof/sizeof)` — every claimed offset is
   checked by the compiler when the SDK is built (§6);
5. **declared base lists** including implemented interfaces (UE4SS prints only the class parent);
6. **enum definitions with values** in `_structs.hpp` (independent confirmation of
   `Sparta_enums.hpp`, §7.2).

Does **not** give:

* **function RVAs or vtables.** `Sparta_functions.cpp` contains
  `if (Func == nullptr) Func = Class->GetFunction("SpartaSaveComponent", "Save");` and `0`
  matches for `0x[0-9A-Fa-f]{6,}` in that file. Native non-UFUNCTION entry points remain
  reachable only through memory scanning (the `GWorld`/`PE-Index` offsets in §4 are the
  exception — they are printed in the log but not shipped in the SDK);
* **Blueprint bytecode or graph logic** — the SDK has declarations, not implementations;
* the `NewEnumeratorN` **display names** of user enums: Dumper-7 reads the same `UEnum`, so they
  stay `NewEnumeratorN`; the localized `FText` names are in neither dump;
* the **`Mappings/`** and **`IDAMappings/`** directories in our runs — patch 4 skips those
  generators because they iterate 2.1 M objects after the SDK is already written (§3.3). Build
  the upstream tree unpatched if those files are wanted.

---

## 10. Coverage: what each dump contains

| Source | Classes | State when taken |
|---|---:|---|
| UE4SS CXX dump (`raw\CXXHeaderDump`) | 16 070 | main menu + `LoadAllAssetsBeforeGeneratingCXXHeaders=1` |
| Dumper-7, all-assets (`raw\Dumper7_allassets`) | **16 040** | same state (injected right after UE4SS' `DumpAllObjects()` returned, i.e. with every package loaded) |
| Dumper-7, gameplay (`raw\Dumper7_gameplay`) | 9 441 | prologue gameplay after `ready.txt` |
| Dumper-7, menu (`raw\Dumper7`) | 8 525 | plain main menu |

The all-assets pair is a like-for-like comparison: 15 995 classes in both, 78 740 slots, no
conflicts. The 75 classes that only UE4SS has, and the 45 that only Dumper-7 has, are name
pairs, not missing content:

* `X` (Dumper-7) against `DEPRECATED_X` (UE4SS) and vice versa — e.g. `ABP_ShellMemorySpawner_C`
  vs `ADEPRECATED_BP_ShellMemorySpawner_C`, `UDataLayer` vs `UDEPRECATED_DataLayer`,
  `UObjectChooser_Asset` vs `UDEPRECATED_ObjectChooser_Asset`. Both dumpers see one of the two
  names for the same class; neither name is missing from the game.
* `UDefault__*` classes (`UDefault__Class`, `UDefault__BlueprintGeneratedClass`, …) — UE4SS
  emits the class-default-object classes, Dumper-7 does not.
* a handful of BP classes only present in one of the two states (`ABP_Candles_Cluster_*`,
  `APLA_*`, `AAbilitySystemTestPawn`).

---

## 11. Reproduce

```powershell
# 1. upstream DLL (runs 1-3) — https://github.com/Encryqed/Dumper-7/releases/download/v7.1.0/Dumper-7.dll
#    -> tools\dumper7\Dumper-7.dll

# 2. patched build (runs 4-5)
git clone --depth 1 https://github.com/Encryqed/Dumper-7.git tools\dumper7-src
cd tools\dumper7-src
git apply ..\dumper7-patches.diff
cmake -S . -B out\build\vs2022 -G "Visual Studio 17 2022" -A x64
cmake --build out\build\vs2022 --config Release
#    -> out\build\vs2022\bin\Release\Dumper-7.dll   (SHA-256 09D774F7…524D)

# 3. dumps (test copy only; the runner restores the ini and the saves itself)
cd G:\Freespace\shell_coop\research\scripts
& .\run-dumper7.ps1 -Mode menu                                        # -> raw\Dumper7
& .\run-dumper7.ps1 -Mode allassets -DllPath ..\..\tools\dumper7\Dumper-7-patched.dll   # -> raw\Dumper7_allassets
& .\run-dumper7.ps1 -Mode gameplay  -DllPath ..\..\tools\dumper7\Dumper-7-patched.dll   # -> raw\Dumper7_gameplay

# 4. checks
python d7_sdk_build_check.py --sdk ..\raw\Dumper7_allassets\5.6.1-0+++Sparta-Depot+Day1+CL92086-MortalShell2\CppSDK --out ..\..\local\sdkbuild\allassets
python d7_compare.py --d7 ..\raw\Dumper7_allassets\5.6.1-0+++Sparta-Depot+Day1+CL92086-MortalShell2\CppSDK\SDK --ue4ss ..\raw\CXXHeaderDump --json ..\slices\d7-vs-ue4ss-allassets.json
python d7_enum_compare.py ..\raw\Dumper7_allassets\5.6.1-0+++Sparta-Depot+Day1+CL92086-MortalShell2\CppSDK\SDK\Sparta_structs.hpp ..\raw\CXXHeaderDump\Sparta_enums.hpp
```

---

## 12. State left behind

* owner saves: the three files are **back on the 06:01 baseline**, byte-identical to
  `local\save-backups\20261022-060155\hashes-before.txt` (`MetaSaveGame.sav 0C94B2DC…`,
  `SpartaGameSettings.sav 47095B2B…`, `WorldState_0.sav 96EDA857…`). The two gameplay runs made
  the game rewrite `WorldState_0.sav`; the runner restored each time, and the game-created
  `WorldState_Backup_0_0.sav` was moved to
  `local\save-backups\20261022-201423-dumper7-gameplay\extras-created-by-game\`;
* no `MortalShell2`, `CrashReportClient` or `cooprelay` process; `Dumper-7.dll` and
  `Dumper-7.ini` removed from `…\Binaries\Win64`; `LoadAllAssetsBefore*` both `0`;
* run folders kept: `research\runs\20261022-175220-dumper7-menu\`, `…-175635-dumper7-allassets\`,
  `…-180334-dumper7-gameplay\`, `…-195414-dumper7-allassets\`, `…-200712-dumper7-allassets\`,
  `…-201423-dumper7-gameplay\` (each with `run.log`, `dumper7.log`, `UE4SS.log`, ini, save
  hashes);
* build/test work dirs: `tools\dumper7-src\` (patched tree), `local\sdkbuild\{menu,allassets}\`;
* repo files added for this exercise: `tools\dumper7\Dumper-7.dll`,
  `tools\dumper7\Dumper-7-patched.dll`, `tools\dumper7-src\`, `tools\dumper7-patches.diff`,
  `research\scripts\{run-dumper7,inject-dll}.ps1`, `research\scripts\d7_{compare,enum_compare,sdk_build_check}.py`;
  no game file, no `CoopMod` file and no `CoopLab` file was modified.

---

## 13. Open / unknown

| # | Unknown | Status |
|---|---|---|
| 1 | The exact object/chunk counts the fixed validator sees in the all-assets state | derived, not printed: 2 170 742 objects / 65 536 per chunk ⇒ ≈34 chunks > the old cap of 20. Adding a log line would make it direct; the arithmetic is what the fix is based on |
| 2 | Why `ABP_Candles_Cluster_Med_01_C/_02_C` have a different parent and size in gameplay than in the menu-time UE4SS dump | state difference (§7.1); the gameplay-side variant is a level-specific class — `GUESS:` |
| 3 | Whether the upstream tree would also dump all-assets with only patch 1 (i.e. without the pinned `GWorld`) | run 4 was taken with all patches; with the `GWorld` budget of 3 scans the scan-only path is capped at ~3 × one full scan, so it would probably finish, but it was not measured |
| 4 | The 2 BP variables only Dumper-7 reports (`Comparison`, `Box_Objective_0`) | `GUESS:` name-collision duplicates that UE4SS drops (§8.2) |
| 5 | Function RVAs for native non-UFUNCTION code | not available from either dumper (§9) |
