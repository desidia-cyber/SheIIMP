# ms2coop-native — the UE5.6 reflection core, without UE4SS

A native replacement for the part of UE4SS the Mortal Shell II co-op mod actually
used. It is a **proxy DLL** (`dxgi.dll`) plus a small UE 5.6 reflection layer:
GObjects, FName lookups, property offsets, `ProcessEvent`, and the hook /
game-thread-queue / per-frame-loop trio the Lua mod called `RegisterHook`,
`ExecuteInGameThread` and `LoopAsync`.

It exists because every crash dump this workstream has taken has UE4SS on the
stack (`UE4SS!RC::Unreal::HookUStructLink`), so the mod needs a version that can
run with UE4SS absent.

```
native/
  build.bat              the only build entry point
  proxy.def              generated: 19 forwarders to System32\dxgi.dll
  src/
    proxy.cpp            DllMain (thread only), worker thread, forwarder check
    ms2_ue.h             the API other modules compile against
    ms2_ue.cpp           the reflection core
    ms2_log.h/.cpp       ms2coop-native.log
    ms2_ue_test.h        test-only entry point (see tests/)
    exports.h            generated: the 19 export names
    fwd_symbols.c        generated: link.exe needs these (see the file's comment)
  tests/
    test_api.cpp         the header's signatures are the ones the co-op lanes use
    test_scan.cpp        runs the DLL's own AOB scanner over the real game exe
    test_load.cpp        proves LoadLibrary + the mismatch path does not crash
  thirdparty/minhook/    vendored, compiled straight in
  tools/                 the scripts that produced every number below
```

## Build

```bat
cd ms2-coop\native
build.bat            :: just the DLL
build.bat test       :: DLL + test_api + the AOB self-check + the load test binary
```

`build.bat` finds MSVC itself (it calls `vcvars64.bat` directly, falling back to
`vswhere`). Output: `build\dxgi.dll`. A clean build is warning-free at `/W3`.

`build.bat test` builds and runs two of the three tests and **fails the build** if
either fails:

* `test_api` takes the address of every declared function into a variable of the
  exact expected type, so a signature that drifts away from the contract the
  co-op lanes are written against becomes a compile error here rather than an
  error in somebody else's file. It also pins `sizeof(FName) == 8`,
  `sizeof(FString) == 16`, `sizeof(FVector) == 24` (UE5 doubles) and
  `sizeof(FTransform) == 80`.
* `test_scan` runs the module's own scanner over the real exe and fails if a
  pattern does not match exactly once and resolve to the recovered address — a
  pattern that matched twice would otherwise load fine and silently do nothing.
  Point `MS2_EXE` at another `MortalShell2-Win64-Shipping.exe` to check a
  different build.
* `test_load` is built but not run by the script (it takes ~25 s). Run
  `build\test_load.exe` from `build\`.

Note: `vcvars64.bat` itself prints `'vswhere.exe' is not recognized` on this
machine. That is Microsoft's script, not this one; the compiler path works.

## Deploy (TEST instance only)

Never the real install.

1. Copy `build\dxgi.dll` into
   `E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\`.
2. **Remove or disable UE4SS there**, or the two will fight over the process:
   `dwmapi.dll`, `UE4SS.dll`, `UE4SS-settings.ini`, `UE4SS_Signatures\`,
   `UE4SS_SDK_Backends\`, and `Mods\` (or rename `Mods\enabled.txt` to
   `enabled.txt.off`). The game must not load UE4SS at all.
3. Start the game normally. No launcher flags, no injector.
4. Read `ms2coop-native.log` next to the exe.

To undo: delete `dxgi.dll` (and restore the UE4SS files if you want them back).
`ms2coop-native.ini` is generated next to the exe on first run; it is optional.

### What the log must say (this is the acceptance test)

```
init: .text 0x... .. 0x... (134.9 MB)
init: ProcessEvent  = 0x...  [aob]
init: AppendString  = 0x...  [aob]
init: EngineTick    = 0x...  [aob]
init: GObjects      = 0x...  [aob]
object array: MaxElements=... NumElements=... MaxChunks=... NumChunks=...
object array: layout check -- chunk-table-at-0 scored 64, flat-first-chunk scored 0 (of 64)
ProcessEvent HOOKED at 0x...
UGameEngine::Tick HOOKED at 0x...
init: READY -> objects=aob pe=aob names=aob tick=aob | N objects, layout 5.6(chunk-at-0), hooked=yes
---- self-test (+10s, game thread) ----
  self-test: PropOffset(Actor CDO, "RootComponent")    = 0x1B8  (SDK 0x1B8) MATCH
  self-test: PropOffset(Actor CDO, "PrimaryActorTick") = 0x28   (SDK 0x28)  MATCH
  self-test: ProcessEvent ACTIVE, 123456 calls so far, tick hook ACTIVE
  self-test: ProcessEvent #1  ReceiveTick  40812 calls
  ...
```

If a line says `[rva(build-specific)]` the AOB scan missed and the fallback RVA was
used — it will work on this build and is the first thing to look at after a game
update. If it says `UNRESOLVED` or `MISMATCH`, nothing else will work.

## The proxy target, and why

`dxgi.dll`. The shipping exe statically imports it (2 imports, from
`tools/pe_imports.py` against the real exe), so Windows maps the file from the
executable's own directory **before the game's entry point runs**: no injection,
no UE4SS, no `dwmapi.dll`. `pe_imports.py`'s proxy-slot check reports `dxgi.dll`
as `USABLE` — it is not a KnownDLL, so the exe-directory copy wins over System32.

`dwmapi.dll` (what UE4SS used, 44 exports) and `version.dll` were the
alternatives. `dxgi.dll` has the smallest export surface of the three — 19, every
one a forwarder — and fewer exports means fewer ways to break one of the many
modules that resolve that name (d3d11, d3d12, the DLSS and FSR runtimes). The
`/DEF` is generated from `System32\dxgi.dll` by `tools/gen_proxy_def.py`, and
`build.bat` verifies with `tools/pe_export_table.py` that the result is
`forwarders: 19  real code: 0`. The module exports **nothing of its own**.

## Offsets — every value, and where it came from

Two independent sources, and they agree:

* **SDK** — the Dumper-7 output in `research/raw/Dumper7/SDK/` generated *from
  this exact exe* (`5.6.1-0+++Sparta-Depot+Day1+CL92086-MortalShell2`). Its
  `Basic.hpp` even has an `Offsets` block.
* **UE4SS log** — `[PS] Found ...` / `MEMBER OFFSETS START` lines in the sessions
  kept at `local\lab\20261022-142616-sp15\UE4SS.log`.

### Globals

| global | RVA | how it was established |
|---|---|---|
| `GObjects` (= `&FUObjectArray::ObjObjects`, 5.6's `TUObjectArray`) | `0x0B03AEF0` | `Offsets::GObjects` in the SDK **and** `[PS] Found GUObjectArray: 0x7ff721dfaee0` + the recovered base `0x7FF716DC0000` → `0x0B03AEE0`, which is `GObjects - 0x10` (`FUObjectArray` is 0x10 bytes of header before `ObjObjects`). Two sources, one address. |
| `ProcessEvent` | `0x01619320` | `Offsets::ProcessEvent` **and** `[PS] Finding ProcessEvent` **and** `tools/recover_base.py` (see below) |
| `ProcessEvent` vtable index | `0x4C` (76) | `Offsets::ProcessEventIdx`. **Re-measured at run time** against a live object's vtable (`VerifyProcessEventIndex`), and corrected+logged if it differs. |
| `FName::AppendString` | `0x013F8FA0` | `Offsets::AppendString`; the SDK calls it as `void(*)(const FName*, FString&)` |
| `UGameEngine::Tick` | `0x03DA7B30` | `[PS] GameEngine::Tick address (vtable: …; scan: …)` — both agree — and it is a `.pdata` function start |
| `GMalloc` | `0x0AF4B3C0` | `[PS] Found GMalloc` (not used by this module; recorded for whoever needs it) |
| `GNatives` | `0x0B039970` | `[PS] Found GNatives` (not used; recorded) |
| `FName::ToString` | `0x01405610` | `[PS] Found FName::ToString` (not used: `AppendString` is cheaper and needs no sret) |
| `GNames` | `0x0AF57380` | `Offsets::GNames` (SDK calls it a fallback only; not used) |
| `GWorld` | `0x0AD4E7F0` | `Offsets::GWorld` (not used; recorded) |

**How the base was recovered** (`tools/recover_base.py`). UE4SS's log gives
*absolute* addresses for a real session, whose differences are facts about the
binary but whose absolute values depend on ASLR. Two more facts pin it down: the
exe's `.pdata` unwind table names every function's begin RVA, and the section
headers say which ranges are code and which are writable data. So for each
64K-aligned candidate base in the window the section layout allows, the induced
RVAs of twelve logged function addresses must all land on `.pdata` function
starts, and the three logged globals must all land in `.data`. Exactly one base
satisfies all fifteen constraints: `0x7FF716DC0000` — the same value a crash dump
(`crash_2026_10_22_19_59_25.dmp`) independently reports for that session.

### AOB patterns

Each is verified by `tests/test_scan.cpp`, which runs the **DLL's own scanner**
over the real exe: `matches=1` and the resolved address equals the RVA above.
This is not decoration — the first hand-wildcarded `AppendString` pattern matched
27 places and was caught by that test.

| target | pattern |
|---|---|
| `ProcessEvent` | `40 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 00 01 00 00 48 8D 6C 24 30` |
| `AppendString` | `48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 20 80 3D 63 E1 B5 09 00 48 8B` |
| `EngineTick` | `40 55 53 41 54 41 55 41 57 48 8D 6C 24 E0 48 81 EC 20 01 00 00 0F 29 B4` |
| `GObjects` | `48 8B 05 ?? ?? ?? ?? 48 8B 0C C8 48 8D 0C D1 EB 06 45 33 FF 41 8B CF BA` |

The `GObjects` pattern is the engine's own object-array accessor
(`mov rax,[GObjects]` then `mov rcx,[rax+rcx*8]`), so it is anchored on the code
that *uses* the global rather than on a prologue a rebuild would move — and it is
what proves the array's first field is the chunk table (see below).
`AppendString` is the one exact (un-wildcarded) pattern; wildcarding its
`cmp byte ptr [rip+X], 0` displacement cost uniqueness.

### Members

| struct | offsets |
|---|---|
| `UObject` (0x28) | VTable `0x00`, ObjectFlags `0x08`, InternalIndex `0x0C`, ClassPrivate `0x10`, NamePrivate `0x18`, OuterPrivate `0x20` |
| `UField` | Next `0x28` |
| `UStruct` (0xB0) | BaseChain `0x30`, SuperStruct `0x40`, **Children** `0x48` (UFunctions), **ChildProperties** `0x50` (FProperties), Size `0x58`, MinAlignment `0x5C` |
| `UClass` (0x200) | CastFlags `0xD8`, ClassDefaultObject `0x110` |
| `UFunction` (0xE0) | FunctionFlags `0xB0`, NumParms `0xB4`, **ParmsSize `0xB6`**, ReturnValueOffset `0xB8`, EventGraphFunction `0xC8`, EventGraphCallOffset `0xD0`, ExecFunction `0xD8` |
| `FField` (0x30) | VTable `0x00`, ClassPrivate `0x08`, Owner `0x10`, Next `0x18`, Name `0x20`, ObjFlags `0x28` |
| `FProperty` (0x70) | ArrayDim `0x30`, ElementSize `0x34`, PropertyFlags `0x38`, **Offset_Internal `0x44`**; `FBoolProperty::ByteOffset` `0x71` |
| `FUObjectItem` (0x18) | Object `0x00`, Flags `0x08` |
| `TUObjectArray` | Objects (chunk table) `0x00`, legacy flat chunk `0x08`, MaxElements `0x10`, NumElements `0x14`, MaxChunks `0x18`, NumChunks `0x1C`; `ElementsPerChunk = 0x10000` |

UE5 differences this code respects: `FProperty`, not `UProperty`; properties hang
off `UStruct::ChildProperties` (`FField` chain), functions off `UStruct::Children`
(`UField` chain); `Offset_Internal` was renamed from `Offset`; `FVector`,
`FRotator`, `FQuat` and `FTransform` are **double** precision.

`ParmsSize` at `0xB6` is the one value not read directly: it sits between two
values UE4SS did measure (`ReturnValueOffset = 0xB8`, `EventGraphCallOffset
= 0xD0`), which brackets the standard UE5 layout (`NumParms 0xB4`,
`ParmsSize 0xB6`). The self-test logs `ParmsSize` indirectly through any call the
integrator makes; a wrong value shows up as an obviously wrong parameter, not a
silent one.

### The object array shape

`GObjects + 0x00` is the **chunk table** (`FUObjectItem**`), not a flat first
chunk — that is what the engine's own accessor does with it (above), and what the
Dumper-7 SDK's `TUObjectArray::GetByIndex` assumes. Pre-5.4 UE put a flat chunk
there and the table at `+0x08`, so `ValidateArray()` does not assume either: it
scores both layouts by the array's own invariant (*object #i must report internal
index i*) and logs which one won.

## API surface (what the co-op code compiles against)

`ms2_ue.h` is the contract; the names are the ones the integrator is already
writing against (`Init`, `GetName`, `GetFullName`, `GetClassName`, `FindObject`,
`FindClass`, `FindFirstOf`, `FindAllOf`, `IsA`, `IsValid`, `GetOuter`, `GetClass`,
`FindProperty`, `PropOffset`, `Get<T>`/`Set<T>`, `FindFunction`, `Call`, `Params`,
`HookPre`, `HookPost`, `EnqueueGameThread`, `OnTick`, plus `FName`/`FString`/
`TArray`/`FVector`/`FRotator`/`FTransform`).

Beyond that list, and because the co-op lanes asked for them while this was being
written: `GetBool`/`SetBool` (mask-aware — a UE bool property is one BIT of a
byte, so a plain byte read returns three unrelated flags), `GetCDO`,
`MakeFName`/`MakeName`, `GetWorld`, `PropSize`, `ReadMem`/`WriteMem`,
`PlausiblePtr`, `ProcessEventCount`, `GetResolved`, `SelfTest`.

Deliberately **not** provided, because they belong to the feature lanes and a
guess would be worse than their workaround: `ConsoleCommand`, `GetPawn`,
`GetPlayerController`, `FText` conversion (`TextToString`), `TMap` iteration
(`MapForEach`/`MapForEachRaw`), `FreeTArray`, and the object-array free helpers.
The RVAs those need are in the table above where they are known.

`GetWorld()` is the one function backed by an RVA with **no signature**: no AOB
pattern for `GWorld` was recovered, and the Dumper-7 SDK calls its own `GWorld`
offset "decoration". It validates what it reads (plausible pointer, `IsValid`,
class name `World`) and returns `nullptr` rather than a bad pointer, but treat it
as less certain than everything else here.

Behaviour worth knowing:

* `FindClass` / `FindObject` / `FindAllOf` are **O(objects)** (a few tens of ms on
  a populated array). They cache their results; they are for setup, not per frame.
  `GetName`/`GetClassName`/`PropOffset` are per-object and cheap (`PropOffset` is
  cached by (class, name)).
* `IsValid` means: the slot in GUObjectArray still points at this object, its
  internal index agrees, and neither the item flags (`Garbage`, `PendingKill`,
  `BeginDestroyed`, `FinishDestroyed`) nor `EObjectFlags::MirroredGarbage` mark it
  dead. `FindAllOf` additionally skips CDOs and archetypes.
* `HookPre`/`HookPost` resolve the `UFunction` **once**, at registration, and log
  a name that does not resolve instead of never firing silently. Accepts
  `/Script/Engine.Actor.ReceiveBeginPlay`, `Actor.ReceiveBeginPlay`, or a bare
  `ReceiveBeginPlay`.
* `OnTick` runs on the game thread from a detour on `UGameEngine::Tick`. If that
  detour cannot be installed, it falls back to clock-paced calls from inside the
  `ProcessEvent` detour, and the log says so.
* Every engine read is SEH-guarded: a wrong offset returns false and logs, it does
  not fault. That is why the `__try` blocks live in tiny POD-only helpers — MSVC
  refuses `__try` in a function that needs C++ unwinding (C2712).
* `MakeName("Foo")` builds an `FName` by calling the game's own
  `UKismetStringLibrary::Conv_StringToName` through `ProcessEvent`, so the name
  pool layout stays on the engine's side. Calling `FName::FName(wchar_t*)` at its
  address was rejected: the prologue takes three registers and the convention
  could not be settled from the binary, and an 8-byte write through a guessed
  pointer is not something SEH can undo.

## Verified, and not verified

**Verified here:**

* The DLL builds with MSVC 2022 (`/W3`, warning-free) and its export table is
  `forwarders: 19  real code: 0`.
* Loading it into a process with **no UE code** does not crash: `DllMain` returns,
  the worker runs, every global fails to resolve, and it says so. That is the
  path that a bad offset would take, so it is the one worth exercising without the
  game. (`tests/test_load.cpp`.)
* All four AOB patterns match exactly once in the real exe and resolve to the
  addresses that two independent sources give. (`tests/test_scan.cpp`.)
* The addresses themselves were recovered three ways that agree (SDK offsets,
  UE4SS's probe log, and the `.pdata` base recovery).

**NOT verified — needs one run of the real game:**

* The array validation, the `ChildProperties` walk and the property offsets
  against **live** memory. The self-test checks `Actor.RootComponent == 0x1B8` and
  `Actor.PrimaryActorTick == 0x28` against the SDK; that is the first thing to read.
* That `MinHook` can install both detours in this build, and the `ProcessEvent`
  call count after 10 s.
* That no module rejects the proxied `dxgi.dll`. The forwarders are complete by
  construction and checked at run time, but only a real launch proves the graphics
  stack is happy.
* `UGameEngine::Tick`'s prototype is assumed to be `(float, bool)`; the ABI for a
  member function (`this` in rcx, float in xmm1, bool in dl) is what the detour
  declares, so a different signature would misread `idle`. If `OnTick` never
  fires, this is the reason, and the `ProcessEvent` fallback covers for it.

---

# Integration notes (the eight co-op modules are now in the DLL)

`build.bat` / `build.bat test` now build **every** `src\*.cpp` (by glob, into
`build\sources.rsp`) into `build\dxgi.dll`, so `proxy.cpp`, `ms2_ue.cpp`,
`ms2_log.cpp`, `coop_math.cpp` and the eight `coop_<module>.cpp` are one binary.
Current state: **0 errors, 0 warnings at /W3**, `test_api` passes (it now also
pins the co-op half of the contract, see below), and the AOB self-check passes on
the real exe. The export table is unchanged: `forwarders: 19  real code: 0` — the
co-op modules export nothing, so the proxy stays transparent.

## What each file had to change

| file | change |
|---|---|
| `coop_state.h` | `class UObject;` → `struct UObject;` (MSVC mangles the two class-keys differently, which breaks the link; it was also C4099 in every TU). Added `StrStr`, `StrStrMap` (string→string, insertion ordered) and made `CombatState::spawnAlias` one of them — the Lua's `SPN_ALIAS[enemy tail] = "SP_<spawner tail>"` is a pair, a `StrSet` cannot hold it. Added `net::ReadText`, `local::RefreshChars`, `combat::KillTail` and `puppet::StartTravel / StartTp / MovementMode / OwnStreamingIncomplete` (the four that used to live in a private `coop_puppet_extra.h`, now deleted) and `coop::Start`. |
| `src/ms2_ue.h` | Moved the `ReadMem` / `WriteMem` / `PlausiblePtr` declarations **above** `ue::Get/ue::Set`: a non-dependent name inside a template is looked up at its point of definition, so under `/permissive-` the header did not compile at all. Declared the additions: `ToString(FName)`, `ToUtf8`, `TextToString`, `MapForEach`, `MapForEachRaw` (both forms), `MapAddNameInt`, `MapAddStrInt`. `Get`/`Set`/`GetBool`/`SetBool` now reject an object that `IsValid` does not know, because a cached pointer from an earlier frame can name a destroyed object whose memory is still mapped (SEH cannot see that). |
| `src/ms2_ue.cpp` | `NOMINMAX` + `WIN32_LEAN_AND_MEAN` and `#undef GetClassName` around `windows.h` (winuser.h's macro renamed `ue::GetClassName`). Implemented the string/FText/TMap API below. **Re-reads the object array's counts on a 100 ms throttle** (`RefreshArrayCounts`): `ValidateArray()` is a one-time snapshot, and every actor created afterwards (the local pawn, the summoned puppet, everything that streams in) sits above it — with the stale count `IsValid` rejects them and `FindAllOf` never returns them, i.e. the whole mod would quietly decide nothing exists. **Lookup misses are now remembered for 2 s instead of forever** for the same reason (BP classes, CDOs and the save subsystem appear when a map loads, after `Init()`). `Call` refuses a UFunction that declares parameters when no parameter block was passed (it used to reach the engine's thunk with a null frame). |
| `src/coop_math.cpp` | NEW — section 1 of `coop_state.h`, a verbatim port of `main.lua` 170 / 391 / 870-905. `NormDeg` uses Lua's `%` (floor-mod, always non-negative) then folds to (-180, 180]; `fmod`/`remainder` would keep the sign and the puppet yaw easing would take the long way round across the seam. |
| `src/proxy.cpp` | After `ue::Init()` succeeds it calls `coop::Start()` (from the worker thread, on purpose: `Start` only does file IO and registers the tick; every UObject is touched later, on the game thread). On a failed init the module stays off and the log says so. The extra 30 s heartbeat and the self-test are untouched. |
| `src/coop_net.cpp` | `Publish()` no longer touches `sq` — `local::Capture()` is the single owner of that increment, and `Publish` now calls it **before** the ready check so the not-ready shape still bumps it exactly once (main.lua:1567 / 1583). `CmdTp` / `CmdTravel` call `puppet::StartTp` / `StartTravel` instead of writing `PuppetState` (the "owner writes" rule holds again); the `tp -> x y z` log stays here, `travel ...` stays in puppet. `HelperCdo` retries a miss instead of caching a null CDO forever. |
| `src/coop_local.cpp` | `local::RefreshChars()` is a public symbol now (declared in the header); the SPN_ALIAS re-derivation was deleted — it reads `state().combat.spawnAlias` directly. Only module-local redeclarations removed. |
| `src/coop_combat.cpp` | `spawnAlias` is written as a real map (`Set(et, key)`), `RefreshChars()` forwards to `local::RefreshChars()` (one definition), `KillTail` matches the new declaration. No logic was rewritten. |
| `src/coop_world.cpp` | `CallNoArg` allocates a `ue::Params` block instead of passing `nullptr`: a UFunction with a ReturnValue has `ParmsSize > 0` and ProcessEvent writes into it. (`OpenDoor`/`PickUp`/`ActivateLift` are probably void, so this is belt-and-braces.) |
| `src/coop_puppet.cpp` | `coop_puppet_extra.h` include dropped (folded into `coop_state.h`). Patrol already used `net::Now()`, verified. |
| `src/coop_appear.cpp` | Removed the `ReadMem`/`WriteMem` and `MakeFName`/`ToString` local-declaration workarounds (the real ones exist now). |
| `tests/test_api.cpp` | Also includes `coop_state.h` and pins the co-op contract with `static_assert`s in unevaluated operands (so it still links only `ms2_ue.cpp`): every new `ue::` function, every `coop::` math function, `coop::Start`, `net::ReadText`, `local::RefreshChars`, `combat::KillTail`, the four puppet entry points, and `CombatState::spawnAlias` being a `StrStrMap`. |

## Behaviour decisions worth knowing

* **TMap iteration** (`ue::MapForEach` / `MapForEachRaw`). UE5.6's `TMap<K,V>` is a
  `TSet<TPair<K,V>>`, i.e. a `TSparseArray` of
  `TSparseArrayElementOrFreeListLink<TPair>`: elements at `+0x00` (`ptr,num,max`),
  the allocation-flag bit array at `+0x10` (`ptr,num,max` then `NumBits` at
  `+0x20`), element stride = `Align(sizeof(pair)) + 8` (`HashNextId`, `HashIndex`).
  Key/value offsets and sizes come from `FMapProperty` (`MapLayout` `0x70/0x74`,
  `KeyProp` `0x78`, `ValueProp` `0x80`) for the property form, and from the caller
  for the raw form. Safety: the header is validated before use, an element is only
  touched when its allocation-flag bit is set (or, if the flags do not read sanely,
  when its key decodes as a real `FName`/`FString`), the stride is picked by probing
  which candidate decodes as real keys, and every read goes through SEH. A wrong
  stride or a stale map therefore loses rows; it cannot fault. **This is the single
  least-verified piece in the layer** — if store B / quest log / data layers stay
  empty in the first session, put a breakpoint on the `"not MapProperty"` warning.
* **`MapAddNameInt` / `MapAddStrInt` do not insert.** Growing a `TMap` needs the
  game's allocator plus a rehash, and there is no reachable, verified Malloc/Free
  pair for this build (the same reason `FreeTArray` is not provided and why
  `coop_world`'s armoury array is deliberately leaked). They find the key and
  overwrite its value, and log once per property and return false when the key is
  absent, so the caller's failure path runs instead of a silent no-op. Every map the
  Lua adds to in this build is pre-seeded by the save/quest system, so this is
  believed to cover 2919 / 3011 / 3224; it is the one place where behaviour could
  differ from the Lua.
* **CDOs are valid objects.** `ue::IsValid` (and therefore `ue::Get/Set/Call`) accepts
  class default objects — only `FindAllOf` filters them. That is what
  `SpartaWorldStateHelper`'s CDO path needs (`coop_progress` 2710-2790); the module's
  note that "IsValid rejects CDOs" was a misreading, but its belt-and-braces
  `ue::PlausiblePtr` check is kept.
* **One `sq` owner.** `local::Capture()` increments it, exactly once per publish, on
  both the ready and not-ready paths (Lua `SEND_SEQ`).
* **`MontageSig` still has no `at`.** `PeerState::at` is the packet clock and
  `net::ApplyPeer` sets it on exactly the condition that installs the montage
  (verified against main.lua:1963-1972), so `coop_puppet` reads `state().peer.at`.
* `GetBool`/`SetBool` were already mask-aware (`FBoolProperty` `ByteOffset` `0x71`,
  `FieldMask` `0x73`) and are unchanged apart from the `IsValid` guard.

## Not resolved / known gaps

* **Compiler merge, not a runtime run.** Everything above is compile- and
  link-verified, and the load test (`build\test_load.exe`) proves the DLL still
  survives a host with no UE code and stays off. Nothing here has been run against
  the game in this pass.
* `ue::FreeTArray` is still absent (leaks: `K2_GetComponentsByClass` per apply,
  `GetAllItems` per chest snapshot, one `FString` per `TextToString`). Freeing with
  a mismatched allocator is a crash; leaking a few hundred pointers every 500 ms is
  not, and the Lua leaked them too.
* `ue::PropOffsetPath` / `ue::ResolveWeakPtr` were not added — nothing calls them and
  guessing a chained property path adds risk without a caller.
* `MortalShell2`'s `FFieldClass::Name` at `+0x00` is assumed (used only to confirm a
  property really is a `MapProperty`); if that offset is wrong the code logs once and
  falls back to the `KeyProp`/`ValueProp` geometry, so it degrades rather than breaks.

---

# First live run (2026-10-25): what actually broke

The section above was written from a compiler, not from the game. This one is from
sessions on the test installs. Every item was a real failure observed in
`ms2coop-native.log` / `coop\probe.log`, and the fix is in the source now.

## The object array never validated — `ItemAt` was missing an indirection

`TUObjectArray::Objects` (`GObjects + 0x00`) is the **chunk table**; the item array is
`table[chunk]`. `ItemAt()` read `GObjects+0` and used *that* as the chunk, so
`ObjectAt(i)` returned the table's own words as "objects". Symptom: `layout check --
chunk-table-at-0 scored 0`, `FName::AppendString probe -> "<fname 1892024432:32756>"`,
and every class/object lookup quietly wrong. Fixed by doing the two-step read (the
legacy shape keeps chunk 0 inline, so the branches differ).

## `IsClassObject` rejected every blueprint class

Comparing `ClassOf(o)`'s name to `"Class"` is only true for native classes; a
`WBP_MainMenu_C`/`BP_PlayerCharacter_C` has class `WidgetBlueprintGeneratedClass` /
`BlueprintGeneratedClass`. `FindAllOf("WBP_MainMenu_C")` was therefore always empty,
`ContinueGame` was never called and the module sat in the menu phase forever. The test
is now the class-chain walk to the class named `Class` (which still rejects `UFunction`,
`UStruct`, `UEnum`).

## FName strings carried a trailing `?`

`FName::AppendString` writes an `FString` whose `Num` **includes** the NUL terminator,
and `Narrow()` turned that NUL into `'?'`. Every name compared as `"Actor?"`,
`"World?"`, … so no name ever matched. `Narrow()` now stops at the first NUL.

## `FindObject` could never match a full path

It compared `want` against `GetFullName(o)`, which prefixes the class name
(`"KismetSystemLibrary /Script/Engine.Default__KismetSystemLibrary"`), while `want` is
the path alone. Every `FindObject("/Script/…")` returned null — console commands
(`nu summon`), the Kismet CDOs, puppet body assets. There is now an `ObjectPath()`
helper (outer chain + name, no class prefix) used by both.

## `DiscoverClassFNames` retried without a throttle

One failed discovery was retried from every `IsClassObject`/`IsFunctionObject` call, in
per-object loops: 12 786 log lines and the module never left `Init()`. Retries are now
throttled to one per 2 s and log only the first few failures.

## Cached world / PlayerController pointed at recycled memory

After `ContinueGame`, the menu world and its controller are destroyed and their
addresses reused. `ue::IsValid` still said "yes" (the slot's index matched), so
`g_world` named a light-preset world and `g_pc` a Niagara component, and
`Pawn()` read "Pawn" off whatever lived there — boot then waited for gameplay forever.
Caches now remember the object's **class pointer** and re-resolve when it differs
(`WorldOk()` / `PcOk()`), and `ue::ResetLookupCaches()` (called on every world change)
drops the class/object caches and the `IsChildOf` memo.

## Health writes went to the wrong bytes

`FGameplayAttributeData` in *this* build is 0x10 bytes with an 8-byte prefix, then
`BaseValue` at `+0x08` and `CurrentValue` at `+0x0C` (Dumper-7
`GameplayAbilities_structs.hpp`). The port assumed `+0x00/+0x04` and "resolved" the
slots from the *MaxHealth* property's window, so a write of 37.5 left `GetHealth` at 75
and `GetCurrentHealthNormalized` at 1.0 — the DAMAGE verdict could never pass. Writing
`+8/+12` is exact; the resolver is kept only as a comment-record.

## `MeshKind` could never match a full name

Lua's `name:match("^%a*StaticMeshComponent")` backtracks, so
`"StaticMeshComponent /Game/…"` matches with an empty prefix. The C port skipped the
leading letters *and then* compared, which always ate the class name: every look part
was skipped as `kind`. LOOK therefore never carried the mask/skin/clothing part. The
test is now "the class token ends with the component class name".

## `localWeaponInfo` used `break` where Lua's `break` means *skip this mesh*

Lua's `for … do repeat … until true end` turns `break` into "next component"
(`break` leaves the `repeat`). The port's component loop used a real `break`, so the
first mesh without an asset path (every weapon's skeletal `WeaponMesh`) ended the whole
list: `wpn` stayed empty and WEAPON failed. It is `continue` now.

## Cleared risks from the section above

* `UGameEngine::Tick` `(float, bool)` is right: `OnTick` fires every frame and the
  heartbeat's accumulated `dt` tracks the engine. (`heartbeat N: … dt 0.016 s`.)
* `UFunction::ParmsSize` at `0xB6` is right: `Params: … ParmsSize=24 NumParms=3
  ReturnValueOffset=16` for `GameplayStatics.GetPlayerController`, and the calls return
  real objects.
* `MakeFName`/`Conv_StringToName` works (`ParamsSize=24`, returns real names). It is now
  also the fast path in `FindClass`, with a string verification so a wrong index falls
  back instead of silently returning an unrelated class.
* `Actor.RootComponent == 0x1B8` and `PrimaryActorTick == 0x28` MATCH on live memory, and
  the array validates `64/64` on the 5.6 chunk-table layout.

## Still open after these runs

* **Frame hitches.** `perf=near:400.00ms` in `sync.txt` (the Lua reported 45 ms) and
  occasional multi-second frames (`heartbeat … dt 13.752 s`). `FindAllOf` still walks
  ~140 k objects per call and several modules call it at 2 Hz; the `IsChildOf` memo and
  the cheaper liveness test in `FindAllOf` cut it but did not finish the job. This is
  the next thing to work on.
* The two-client test still fails some verdicts; see the run log in
  `local\test\run-two-*.log` and the "what still fails" list in the hand-off.

---

# Second pass (2026-10-25): the freezes, measured

The section above says "next thing to work on" about the frame hitches. This one has the
numbers and the fix, and it also records one thing that got WORSE, so nobody has to
rediscover it.

## What the profiler is

`ms2_ue.cpp` grew a small profiler (`ue::ProfTicks/ProfRecord/ProfDump`, declared in
`ms2_ue.h`). Every engine lookup and every `net::Step` records itself **only when it took
≥ 30 ms**, with the object-array slots it walked and the name resolutions it performed:

```
PROF slow FindAllOf("BP_PlayerCharacter_C") took 251.1 ms (objs=701905 names=7950; ...)
PROF table (heartbeat): 26 entries, worst first [max | calls | total]
PROF   t2 loop     max 6315.4 ms  x74  total 54318.5 ms  objs=11176008 names=354183
```

`objs=` is the counter that separates the two candidate explanations: 140 329 slots = one
walk of the array, 11 176 008 = eighty of them (a scan nested inside a per-object loop).
The table is dumped from the 30 s heartbeat (`proxy.cpp`), so a single log file carries
the whole profile.

## Culprits, with the measured cost

From `local\test\20261025-022331-prof-a2` (single client, 150 s, pre-fix):

| what | measured | why |
|---|---|---|
| `FindAllOf` | 200-300 ms **per call**, **704 calls** in 150 s = **130 s** of the game thread | it walked all ~140 k slots every time |
| `t2 loop` | **6315 ms** worst, 1.4 s average | its per-actor loops call `FindAllOf` → ~80 nested walks in one tick |
| `appear` / `myLook` / `lookSilence` / `ready` | 3773 / 2337 / 1757 / 1169 ms | same cause, one `FindAllOf` per actor/part |
| `FindFirstOf("DataLayerManager")` | 258 ms x4 | a lookup MISS re-scanning the array every 2 s TTL, forever |
| `FindClass(<name>)` | 50-90 ms | the "fast path" never matched: `Conv_StringToName` and the object's `NamePrivate` hold DIFFERENT FName indices for the same text ("WBP_MainMenu_C" → 6863537 vs 2449538), so every lookup fell through to a string scan |

Result: `heartbeat 2: dt 12.342 s`, `heartbeat 3: dt 12.450 s` (one frame each), and
`perf=near:400.00ms(10)` in `sync.txt`.

## The fix (`src/ms2_ue.cpp`)

* **An object index** built from the array once and extended incrementally as slots are
  added (`ObjIndex`, `IndexSyncLocked`): `byClass[class pointer] → slots`,
  `byName[FName key] → slots`, plus a cached descendant-class list per base class.
  `FindAllOf`/`FindFirstOf` now cost `O(instances)`, not `O(140 000)`.
* **Slot indices, not raw pointers**, are stored, and every candidate is re-read and
  re-checked (internal index, class, name, liveness flags) at query time — a stale entry
  can only DROP an object, never invent one. `ResetLookupCaches()` rebuilds from slot 0 on
  a world change.
* **Names are compared as integers** (`NameKey` = ComparisonIndex:Number) in every scan.
* **Learned name keys** (`g_nameKeyOf`): the first lookup of a name still costs one string
  scan, and it records the object-side FName index so every later lookup (and every lookup
  after a map load) is an index query.
* **Negative cache with exponential backoff** (2→4→8→16→32 s, capped at 30 s) that also
  refuses to rescan while the object array has not grown — a name that never exists no
  longer costs a full walk every 2 s.
* **`IsChildOf` memo stays a linear table** (4096 rows now, was 512). An open-addressed
  probe was tried and reverted: see the open issue below.
* **TMAP patch applied** (`briefs/TMAP-PATCH.diff`): `FMapProperty` in this build has
  `KeyProperty` at 0x70 and `ValueProperty` at 0x78 and **no** `FScriptMapLayout`, so
  store B's map walk used to fail silently (`wp=b:0/0`). The value offset inside
  `TPair` is now derived from the two `ElementSize`s, and a refusal is logged once.

`coop_net.cpp` also gained the `StepProf` scope in `net::Step` (same table) and the
`SPN_ALIAS` lookup in `CollectNpcsNear()` (main.lua:1279).

## After

Same machine, same save, single client (`local\test\20261025-022331-fix-a1`,
`…-023757-fix-a2`):

```
heartbeat 1: 22298 ProcessEvent calls, dt 0.016 s
heartbeat 2: 45867 ProcessEvent calls, dt 0.016 s
heartbeat 3: 69297 ProcessEvent calls, dt 0.016 s      (60 fps, no stall)
PROF   FindAllOf  max 0.9 ms  x40   (was 300 ms x704)
```

No step above 10 ms after the first 2 s of boot. The remaining one-off costs are the
first `FindClass` of each name (one 50-90 ms string scan, once per name per process).

## OPEN ISSUE introduced by this pass — SOLVED in the third pass (see below)

The reasoning in this section is preserved because it shows what was ruled out, but
its conclusion ("the widget instance genuinely is not in `GObjects`") was WRONG, and
the cross-check that produced it was run before the widget existed (the widget is
created ~32-35 s in; the checks ran at ~2 s and ~20 s). The cause is slot reuse —
see *Third pass* at the end of this file.

After these changes, **the module no longer gets past the menu phase in a single-client
run**: `menu=0` for 200+ s while the game shows the attract/background level
(`world=World /Temp/Game/…L_Main_Menu_Background_01_LevelInstance_…`). The A/B evidence is
unambiguous — the pre-index DLL (`local\test\20261025-020440-perf-b1\dxgi.dll`) reaches
`menu ready after 35s` → `gameplay ready` → `phase -> sync` on the same save minutes
apart, while this build does not:

```
ab-old  (pre-index DLL)   menu ready after 35s | ContinueGame ok=true | phase -> sync   OK
ab-old2 (pre-index DLL)   menu ready after 33s | ContinueGame ok=true | phase -> sync   OK
ab-new2 / ab-fix3 / ab-fix4 / nocross / orderfix (this tree)   menu=0 … at 20/40/…/200 s
```

Ruled out by measurement, not by opinion:

* The index does **not** miss the widget. A full walk of the array (`SlowScanAllOf`, the
  old `FindAllOf` body, run as a cross-check) agrees with the index, and counting objects
  whose `ClassPrivate` is exactly the class gives `exactClass=1` — the class default
  object and nothing else. The widget instance genuinely is not in `GObjects` in these
  runs.
* Result ORDER is not it: the buckets are sorted back into array order now.
* The profiler and the cross-check walks are not it: removing them changed nothing (the
  `nocross`/`orderfix` runs above).
* The TMAP patch and the `coop_combat`/`coop_progress` versions are not it: the failures
  predate both.
* Extra game-thread cost is not it: the failing runs make FEWER slow calls than the
  passing ones (the pre-index build spent 400 ms per `FindAllOf` and still reached the
  menu).

So the remaining suspect is something in the new engine layer that is invisible to a
read-only probe. The first experiment narrowed it: **`FindClass`/`FindObject` no longer
call `MakeName` (`Conv_StringToName`) at all** — they use the learned key or one scan, and
that alone changed the game back to its normal boot world
(`world=World /Game/Sparta/Maps/MAPS/Menu/L_MainMenu.L_MainMenu`, matching the pre-index
build, instead of the `/Temp/…L_Main_Menu_Background_01_LevelInstance_…` the earlier
builds reported). So a `ProcessEvent` into `UKismetStringLibrary::Conv_StringToName` per
lookup *does* perturb the game's start-up (it also adds every probe to the FName pool).
`menu` is still 0 after that (`firstBucket=1`: only the class default object), so the
widget itself is still missing — the next step is to find what *else* the new layer does
that the old one did not. Candidates, in order: the `ProfScope` timers on `GetWorld`
(called once per frame from `net::Frame`), and the extra `ms2::Log` traffic during boot.

---

# Third pass (2026-10-25): the menu regression — the index never looked at RECYCLED slots

## The cause

`IndexSyncLocked()` extended the index over `[scanned, NumElements)` and then never
looked below `scanned` again:

```cpp
int32_t i = g_idx.scanned;          // only NEW slots are visited
while (i < total) { ... IndexSlotLocked(...); i = last; }
g_idx.scanned = total;
```

UE **recycles freed `GUObjectArray` slots**. The menu widget is created ~32 s in and it
does not land above the high-water mark — it takes a slot that was empty or freed when
the index was built. So the index had no entry for it, `FindAllOf("WBP_MainMenu_C")`
returned nothing but the class default object (`firstBucket=1`), and `Boot()` sat in
`Phase::Menu` forever. A full walk found the widget the whole time, which is exactly
what the A/B run showed (the pre-index DLL walked, so it worked).

The fixed build says so out loud, once per sweep that found anything:

```
index: 280 slot(s) re-used by new objects (objects=140329 scanned=140329)
index: 15  slot(s) re-used by new objects (objects=140329 scanned=140329)
[02:54:30][A] menu ready after 32s
[02:54:30][A] ContinueGame ok=true
[02:54:30][A] phase -> world
```

Why the earlier cross-check "agreed with the index": both the index and the
`SlowScanAllOf` cross-check were run at ~2 s and ~20 s, and the widget does not exist
yet at either time (it appears ~32-35 s in, on every build — the pre-index DLL's log
also says `boot: waiting for menu (20s) menu=0`, then `menu ready after 34s`). Two
empty results "agreeing" is not evidence.

## The fix (`src/ms2_ue.cpp`)

1. **Per-slot records + a verification sweep.** The index keeps what it believes sits
   in every slot (`slotObj`, `slotCls`, `slotName`). `IndexVerifyLocked()` sweeps the
   item table once per 100 ms — one bulk read per chunk (~1.5 MB), then 140 k pointer
   compares in plain C, well under 1 ms — and re-indexes every slot whose object
   pointer changed. That is what makes a recycled slot visible, and it is also what
   makes a stale bucket entry harmless: an entry is dropped exactly when the slot no
   longer records the class/name the bucket was built for (an array compare, no engine
   reads), so compaction can never drop a live object. The previous "compaction" dropped
   entries that failed a liveness read, which could lose an object forever.
2. **No more churn-triggered full rebuilds.** They cost ~300 ms each and during
   streaming they fired every few seconds (that was the remaining `t2 loop` 1.8-3.3 s
   spike). Buckets now self-clean through (1), so the index is only rebuilt when the
   object array shrinks.
3. **The fallback class scan walks class objects only** (`classSlots`, ~8.5 k of 140 k).
   A name that never exists (`BPSO_EridrimUnlockManager_C`) used to cost a full string
   scan of the array — one `FName::AppendString` per object, 90-330 ms, `PROF FindClass
   max 329.7 ms x116` = 18.8 s of game thread in one two-client run. The negative cache
   also no longer lets "the array grew" bypass the backoff (during streaming it grows
   every frame, which defeated the backoff completely); the cap is 8 s and a world
   change clears the table.
4. **`NameToString` caches the text per thread.** FName → string is immutable, and one
   `appear` step was resolving 188 664 names for 80 647 objects (745 ms). Now it is a
   hash lookup.
5. **Array order** — every query sorts slots ascending, so "the first hit" means the
   lowest internal index, exactly like the walk these replace. Bucket insertion order
   is not array order once slots are recycled.

## After (two-client run, `local\test\run-two-out-slotfix4.txt`, both clients `menu ready` → `phase -> sync`)

```
PROF   FindAllOf     max 218.7 ms  x2      (was 329.9 ms x599, 55.3 s total)
PROF   indexSync     max 214.5 ms  x2      the two whole-array builds (boot + world change)
PROF   t2 loop       max 868.3 ms  x484    (was 3310.7 ms x210)
heartbeat 1 (boot)   dt  2.68 s            A: the menu->world frame; B: 3.27 s.  Same shape
heartbeat 2..17      dt  0.035-0.088 s     A; 0.009-0.016 s B   (one 0.193 s outlier on A)
```

`FindAllOf` is only ≥30 ms in the two calls that carry the index build; every other call
in a 6-minute session stayed under the 30 ms logging threshold, and the profiler's
`descendants`/`indexSync` rows now say where the remaining time goes.

A second 60-100 ms-per-call cost showed up in the first fixed run and is fixed the same
way: `DescendantsOfLocked` asked `IsChildOf` once per class per query, and `IsChildOf`'s
memo is a 4096-row LINEAR table that is full after a few thousand questions — so every
miss was a 4096-entry scan, ~40 ms per query (`PROF FindAllOf max 343.7 ms x267, objs=5`).
The index now keeps `parentOf` (class -> SuperStruct) and answers the question in memory.

Remaining one-off costs, both at a map load and both measured, not guessed:

* **~215-240 ms** for the whole-array index rebuild after `ResetLookupCaches` (one per
  world change, now its own `indexSync` PROF row). Spread it across frames if a single
  0.24 s frame ever matters.
* the first `FindClass` of a name the index has no learned key for resolves ~8.5 k class
  names (~5-10 ms), once per name per process.

---

# Fourth pass (2026-10-25): the frame cost, measured, and where it actually was

The two-client run reported `perf=near:113.89ms` on the host (A) against the Lua's
~45 ms, and `30.04ms` on the guest. This pass answers what that number is, adds the
instrumentation that separates "the game is slow" from "we made it slow", and removes
the costs that WERE ours.

## What `perf=near:` measures (it is not our cost)

`coop_combat.cpp FrameTime()` (main.lua:3926-3935) calls
`UGameplayStatics::GetWorldDeltaSeconds(pawn)` **once per enemy tick (2 Hz)**, adds it to
`ftNear*` or `ftFar*` depending on whether the peer's body is more than 30 000 uu away,
and `Status()` prints the mean. So:

* it is the **game's own frame time** at that moment — the engine's delta, which includes
  every millisecond the module spends on the game thread *plus* everything the game,
  the renderer and the other instance do;
* it is **not** module CPU time, and a module that costs 2 ms in a 16 ms frame moves it
  by the whole 2 ms, while a module that costs nothing in a 113 ms frame is 2 % of it;
* `dt > 1 s` samples are dropped (both the Lua and the port), and the count in brackets
  is how many samples went into the mean. `run-two.ps1` only *prints* it (the `PERF:`
  line); it is not a verdict.

## The module's own cost, per frame and per step (new, always on)

`ms2_ue.cpp` grew a second profiler next to the slow-only `PROF` table. `CostScope` /
`CostStepRecord` / `CostSeriesRecord` record **every** call (not only ≥ 30 ms) into
4-buckets-per-octave histograms, so avg / p95 / max are free to keep; the 30 s heartbeat
in `proxy.cpp` prints one window and resets it:

```
COST window (heartbeat) -- every call, not just the slow ones
COST   frame dt    n=1317   avg   22.81 ms  p95<=  49.15  max   74.62 ms
COST   mod tick    n=1317   avg    1.24 ms  p95<=   4.09  max   29.42 ms
COST   PE detour   n=1317   avg    1.05 ms  p95<=   1.79  max    3.44 ms
COST   detour own  n=1317   avg    0.05 ms  p95<=   0.11  max    0.24 ms
COST   engine tick n=1317   avg    5.28 ms  p95<=   8.19  max   18.34 ms
COST   t2 loop     n=60     avg   11.34 ms  p95<=  24.58  max   29.39 ms  total 680.52 ms
COST   appear      n=60     avg    4.13 ms  p95<=  10.24  max   14.60 ms  total 247.56 ms
```

* `frame dt` is the engine delta the tick hook was handed — comparable with `perf=near`,
  but every frame, and with a p95.
* `mod tick` is the whole of `net::Frame` (every 30/15/2/1 Hz step), i.e. **our** cost on
  the game thread, per frame.
* `PE detour` is time inside the ProcessEvent detour, `detour own` is that minus the
  original call. `detour own` is the number that answers "does the hook cost anything":
  it is 0.05-0.07 ms per frame (0.3 % of a 16-22 ms frame) with ~4 000 ProcessEvent
  calls/s.
* every named step (`t2 loop`, `appear`, `combat`, `indexSweep`, …) appears with its
  window total, so "which step costs the most per 30 s" is a fact, not a guess.

### Game-only baseline: `COOP_OFF.txt`

Put `COOP_OFF.txt` in the coop dir: every hook stays installed (detours, cost table,
heartbeat) and `net::Frame` returns immediately, so the module does no work at all and
the log still carries the frame series. `Start()` logs the switch. Measured on the test
install with both instances up:

| | game only (`COOP_OFF`) | module on, before this pass | module on, after |
|---|---|---|---|
| `frame dt` avg / p95 | 16.5-19.3 ms / 20-29 ms | 16.6-22.6 ms / 20-29 ms | 20.2-22.8 ms / 41-49 ms |
| `mod tick` avg | 0.00 ms | **2.42 ms** | **1.24 ms** |
| `PE detour` / `detour own` avg | 0.06-0.12 / 0.00-0.07 ms | 0.91 / 0.05 ms | 1.05 / 0.05 ms |
| our work per 30 s window | 0 | ≈ 4.2 s | ≈ 1.6 s |
| `perf=near` for the run | — | 38.78 ms | 31.83 ms |

(The frame-dt rows are from different sessions and the game's own cost moves ±30 %
between them — with two clients, the relay and streaming on one PC that is expected. The
robust numbers are `mod tick` and the window totals, which are our own work.)

## Culprits, with the measured cost (all fixed)

| what | measured before | why | fix |
|---|---|---|---|
| file writes on the game thread | 2.2 ms for **one tiny write** (`hb loop` row); ~0.5-2 s per 30 s window including a `FlushFileBuffers` per `probe.log` line | `CreateFile`+`WriteFile`+`CloseHandle` per call, 15 packets/s + 2 sync files/s + 1 heartbeat + ~10 log lines/s | `coop_net.cpp`: one writer thread, FIFO queue, whole-file writes to the same path coalesce; `result_*.txt` stay synchronous (they are the harness handshake) |
| `Tail()` built the whole object path | 1.7-2.1 ms per 15 Hz tick (refreshChars over every `Character`) | `GetFullName` = outer-chain walk + a string per segment, for a name whose two patterns are anchored at the END | `coop_net.cpp`: read the object's own name; the last path segment *is* the object name (no `.`/`:` inside one) |
| `FindObject` full-path misses | **154-263 ms per call**, 28 calls in one two-client run | a path ending in its separator (`…/SK_DarkForm_Bulk.`) can never match, yet each call walked ~140 k slots and resolved ~91 k names; full paths were never negatively cached | `ms2_ue.cpp`: empty-leaf fail-fast + the same negative cache (2 s → 8 s, growth re-allows) the bare-name path uses |
| `ue::Params` buffer | 2 kernel calls + a demand-zero page **per engine call** | `VirtualAlloc`/`VirtualFree` for a block that lives microseconds | `ms2_ue.cpp`: per-thread free list by size class |
| `PropOffsetStruct` | a `std::string` key (two heap allocations), a mutex and a hash on **every** `Get`/`Set`/parameter access | the question and the answer never change | `ms2_ue.cpp`: per-thread direct-mapped front cache keyed by (struct pointer, name literal) |
| `FindFunction` | a string per UFunction of the class per call (~10 µs on a blueprint with ~100 functions) | called per component per tick (`GetOwner`, `IsVisible`, `NumBones`, …) | `ms2_ue.cpp`: per-thread (class, name) memo, dropped by `g_lookupEpoch` on a world change |
| `GetOwner()` per skinned mesh | `myLook` 8.6-10.3 ms per 2 Hz tick, `appear` 9.2-11.8 | a ProcessEvent for **every** SkeletalMeshComponent in the world | `coop_appear/local/puppet.cpp`: a component's Owner *is* its Outer — compare that, keep the call as the fallback (`myLook` 2.6 ms, `appear` 4.1 ms) |
| index verification sweep | 0.88-0.93 ms per sweep, 10×/s | full 140 k-slot / 3.4 MB read each time | `ms2_ue.cpp`: one quarter per sweep (ring in 400 ms), its own `indexSweep` COST row |
| `DrainQueue` | a mutex on **every** ProcessEvent call | the queue is empty almost always | `ms2_ue.cpp`: atomic depth, lock only when non-zero |

One correctness fix came with the perf work, in the same file: **`ue::MapForEach` /
`MapForEachRaw` read the wrong `TBitArray` layout.** This build keeps the allocation
bits INLINE for arrays of up to 128 elements (words at `+0x10`, `NumBits` at `+0x28`,
`MaxBits` at `+0x2C`); the walker read a heap pointer at `+0x10` and a bit count at
`+0x20`, so its flag test never applied and the fallback then validated each key as an
`FName` — while `NPCRelationships`/`NarrativeEvents` are keyed by `FString`. Every row of
those maps was invisible to every module (`wqScan: r|e|` stayed 0, `MapAddStrInt` said
"key is not in the map" for a key that was there). `ReadMapInfo` now reads the measured
layout (inline words, else the heap pointer with its count) and keeps the pre-UE5 shape as
a fallback; it agrees row for row with `ms2_map.cpp`'s `ReadMapView`, which is the
authority the quest lane measured. `wqScan` then reports `r=7` instead of `r=0` on the
same save.

The detour itself was already fine (O(hooks) integer compare, no string work, and
`HookPre`/`HookPost` have no call sites), which the `detour own` row now proves instead of
asserting.

## After

Same box, one client at the hub, other instance up, integrated build:

* `mod tick` 2.42 → **1.24 ms/frame**; our work per 30 s window 4.2 → **1.6 s** (~5 % of
  the game thread instead of ~14 %);
* the 2 Hz tick is 11.3 ms instead of 21.7-23.9, its worst case 29 ms instead of 278;
* no step above 30 ms in steady state except the deliberate one-off index rebuild at a
  map load (~90-115 ms) and the first `FindObject`/`FindClass` of a name per process;
* `perf=near` 38.78 → 31.83 ms in the same one-client scenario (the game-only baseline in
  that scenario is ~16.5-20 ms, so most of what is left is the game's own cost with the
  second body in the world).

**What this does NOT claim.** The host's 113.89 ms in the two-client run was mostly the
*game*, not this module: with the module doing nothing at all, the same install measures
16.5-19.3 ms at the hub, and the host's frame time in that run was ~9 fps while both
clients streamed two areas. What the module can do about that is stop adding to it, which
is what the numbers above measure. The remaining lever on the host is the world it keeps
loaded for the far body (the STREAM feature), not the module's tick.

## The final two-client run (integrated build)

`run-two.ps1` against both test installs, DLL sha256
`97c4ca85d6f498a8a040ac394b5eef037ccaf0d3f48f260bf48095ef52428946` on both:
**RESULT: PASS** — SHELL, WEAPON, LOOK (on and off), ANIM, DAMAGE, DEATH, WORLD, PROGRESS,
QUEST, QUEST2, LIFT, CHEST, SPAWN, MIRROR, PEERHIT, AITARGET, STREAM, NPCFAR, and both
puppet-tracking (A/B) verdicts pass. (LEDGER has no verdict in this run by design: the
brief deletes `coop\ws_ledger_*.txt` first, so there is no ledger from an earlier session
to re-apply.)

`PERF:` in that run prints `A near:92.81ms(334) far:93.90ms(146)`, `B near:85.38ms(273)
far:95.63ms(148)` — taken immediately after the 60 s travel, i.e. with BOTH areas loaded
on both clients (host working set 6389 -> 6635 MB). The cost table for that same window
says what that is made of:

```
A   frame 82.15 ms   mod tick 6.06 ms (7.4 %)   PE detour 2.67 / own 0.10   engine tick 10.45
B   frame 102.27 ms  mod tick 8.77 ms (8.6 %)   PE detour 5.16 / own 0.15   engine tick 20.51
```

So at the worst moment of the run the module is 6-9 ms of an 82-102 ms frame; the game's
own tick is 10-20 ms and the rest is streaming and rendering. The same measurement at the
hub with a peer present was `mod tick` 1.24 ms of a 22.8 ms frame (5.4 %). Before this
pass the module was 2.42 ms of a 16.6 ms frame (14.6 %).

A single-client smoke run of the same build on each instance (3.6 min each, menu -> world
-> sync) ended with the process alive, no new dump, no `Saved\Crashes` folder and no
Application error event for the exe — the "died in the menu" report against an earlier
build did not reproduce.







# Round 5 (2026-10-25): zone guard, friend's body, co-op death, store-A types, equipment probe

Full detail: `..\docs\HANDOFF-R5.md` and `briefs\R5-*.md`. Summary:

| Feature | Files | Verdicts (run-two / run-two-death on C/D) |
|---|---|---|
| Zone id + mismatch guard (wire `zn`) | `src/coop_body.cpp`, guard in `coop_net.cpp` ApplyPeer | ZONE |
| Friend's health/shell/resolve/heals/pp/ammo/death state (wire `bh`) | `src/coop_body.cpp` | PEERHP, PEERDEAD |
| Store-A save types + fields (typed section `wt`) | `src/coop_world.cpp`, `src/coop_world_ws2.cpp` | STORE_A2 |
| Co-op death, DollsMP policy (`lv`,`bs`,`dstk`,`wr`,`rul`) | `src/coop_policy.h`, `src/coop_death.cpp`, `tests/test_coop_policy.cpp` (112 checks) | DCASE2, DHOLD, DHOLDCAM, DWIPE |
| Equipment probe (`BPFL_Player_Equipment_C` CDO reads+writes work) | `src/coop_probe.cpp` (`equipprobe`) | lab only |

## Engine-layer bugs found this round (they broke things while log verdicts passed)
1. `RefreshArrayCounts` 100 ms throttle made brand-new objects look invalid (puppet got no weapon meshes).
2. `SetAnimationMode` parameter is `InAnimationMode` (puppet stood in its reference pose since the port).
3. `ue::FTransform` is 0x60 in UE5 (Translation @0x20, Scale3D @0x40) — was 0x50, so every spawned component had scale (1,1,0).
4. `MakeName` latched one boot-time failure for the whole session (sockets/tags/hook names became `None`, intermittently). Now retried every 2 s; log line `self-test: MakeFName("Actor") … MATCH`.
5. `net::World()` could be a recycled object slot — `FindAllOf` now re-checks each slot's current class.
6. Death liveness went stale during a local hold (ApplyPeer skipped) and a boss hold became a wipe — `DeathPeerKeepAlive` runs on a never-skipped step.
7. Puppet gear pins (`coop_appear.cpp` RecordPins/VerifyPins) re-attach any weapon/look part that moved off its socket.
8. `Params::offset_of` logs `Params: <fn> has no parameter '<name>'` — grep for it after every change.

## The spectate camera (boss hold) — how it finally works
This game's camera is **CameraStateFramework**: `BP_PlayerCameraManager_C : ACSCameraManager`, whose `CameraStack`
holds `UCSCameraState` objects (Exploration, Combat, LockOn, Sprint, Falling, Death, …). The player pawn has **no**
CameraComponent; `SetViewTargetWithBlend` is reverted every frame and `BlueprintUpdateCamera` is never called. While
held, `coop_death.cpp` (`CamToPuppet`) calls `UCSCameraState::SetCameraTargetActor(puppet)` on every state and
`RemoveCameraState(CameraState_Death_C)` (the fixed death framing); on release `CamRestore` targets our pawn again.
Proof is the manager's rendered POV (`GetCameraLocation`) next to the puppet and nearer it than our body
(status `viewpuppet=1`). The hold also removes the loading screen and vetoes `WBP_DeathScreen_C.CreateLoadingScreen`:
the reset behind it is vetoed, so a loading screen that got up stayed up for the whole hold.

## Test hygiene learned this round
- Look at the screen: `visual-two.ps1` (screenshots; `-Cmds 'B=bossfake 1;B=die'` adds `B-after-cmds*.png`), `pupdump [self]`, `pupcheck` (PUPVIS).
- `windows.h` defines `near`/`far` — never name a variable that.
- Crash 0xC0000374 (all dumps): a double free inside `steam_api64.dll` on the game's own thread, 47–62 s after launch,
  no `dxgi.dll` frame, also seen with UE4SS. Not mod code; most likely two copies of the game under one Steam account.
