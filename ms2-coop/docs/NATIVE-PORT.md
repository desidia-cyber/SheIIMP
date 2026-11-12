# NATIVE-PORT.md — porting `CoopMod/Scripts/main.lua` to a native C++ proxy DLL

Target: replace the UE4SS Lua mod (`ms2-coop\mod\CoopMod\Scripts\main.lua`, 4133 lines) with a
native C++17 DLL loaded into `MortalShell2-Win64-Shipping.exe`, built on the `ue::` engine-access
layer (`ue::Init`, `GetName/GetFullName/GetClassName`, `FindObject`, `FindClass`,
`FindFirstOf/FindAllOf`, `IsA`, `IsValid`, `GetOuter`, `GetClass`, `FindProperty`, `PropOffset`,
`Get<T>/Set<T>`, `FindFunction`, `Call`, `Params`, `HookPre/HookPost`, `EnqueueGameThread`,
`OnTick`). `FVector` is **double** in UE5.6.

The Python relay (`relay\cooprelay.py`, `relay\steamlink.py`) **stays exactly as it is**. The DLL
must speak the identical file/UDP/Steam protocol. Every byte on the wire is specified in §2.

Reference line numbers are `main.lua` as of 4133 lines (mtime 2026-10-22 17:27). They match the
`docs\HANDOFF-REMAINING-SYNC.md` appendix. If numbers drift, grep the function name — every
function name in this document is the exact Lua name.

All seven porting agents must also read `docs\DESIGN.md` (why each decision exists) and
`docs\HANDOFF.md` §5 ("engine facts worth not re-learning" — the crash list).

---

## 0. Non-negotiables (read this first)

1. **Never change the wire format.** Field names, order, number formatting (`%.1f`, `%.2f`,
   `%.3f`), caps, and the one-line-per-file rewrite rule are load-bearing. Two string comparisons
   on the receiving side (`WEAPON.applied ~= lastPeerWeapons`, `LOOK.applied ~= lastPeerLook`,
   `ms2-coop\mod\CoopMod\Scripts\main.lua:4102`, `:4113`) compare *packets*, so a formatting
   difference alone causes an endless re-apply loop.
2. **Never change `sync.txt`, `probe.log` or the `result_*.txt` file names/keys.** `run-two.ps1`
   is the regression suite (§2.6) and must keep passing unmodified except where §5 says otherwise.
3. **Never bring back a proven crash.** `HANDOFF-REMAINING-SYNC.md` §4.4 lists 14 things that
   crash or do nothing. The port reproduces the *working* path, not the ideal one.
4. All locale-dependent formatting must use the C locale (`.` decimal separator). The Lua
   `string.format` uses `%f` which is locale-dependent too, but the reference machine was
   English — pin it explicitly with `_configthreadlocale(_ENABLE_PER_THREAD_LOCALE)` or manual
   formatting.
5. Every loop step is individually guarded and logs `ERR <step>: …` (`main.lua:4004` `step()`).
   In C++ that becomes `try/catch(...)` (and optionally `__try/__except` for AVs, which is what
   actually killed the guest — see §4.13).

---

## 1. UE4SS / Lua-runtime facility inventory → `ue::` equivalent

Legend: **OK** = direct equivalent exists in the `ue::` API as given. **PARTIAL** = exists but
needs an extra primitive the `ue::` layer must expose. **GAP** = no equivalent; a native
mechanism must be added to `ue::` or the design must change.

### 1.1 Runtime / loop facilities

| Lua / UE4SS facility | Lines | `ue::` equivalent | Status / notes |
|---|---|---|---|
| `LoopAsync(ms, fn)` (1 Hz heartbeat) | 4023 | `ue::OnTick(fn(dt))` + software divider | **PARTIAL** — `OnTick` is a single game-thread callback; the port needs its own rate router (30/15/2/1 Hz). See §3 `coop::Tick`. |
| `LoopAsync(33)` → 30 Hz drive | 4062 | same router | PARTIAL |
| `LoopAsync(66)` → 15 Hz network | 4070 | same router | PARTIAL |
| `LoopAsync(500)` → 2 Hz world/boot | 4082 | same router | PARTIAL |
| `ExecuteInGameThread(fn)` (asset loader, game-thread mode) | 269, 4048 | `ue::EnqueueGameThread(fn)` | OK. Note: the loops themselves must **not** run on the game thread (`GAME_THREAD.txt` crashed both clients, `HANDOFF.md` §18). `OnTick` is the game thread — that is *different* from Lua's async thread and is a real risk; see §4.13. |
| `ExecuteWithDelay` | — | — | not used (0 call sites) |
| `RegisterHook` / `HookPre/Post` | — | `ue::HookPre/HookPost` | **not used** by `main.lua` (0 call sites). `CoopLab` used it and crashed once on many hooks (`HANDOFF.md` §14). The port does not need hooks for v1. |
| `NotifyOnNewObject` | — | none | **GAP**. Not used today: spawn detection polls `FindAllOf("UnitSpawner")` every 10 s (`main.lua:3822-3852`). Keep polling. |
| `RegisterKeyBind` | — | none | **GAP**, not used. Any future key bind needs a WndProc/input hook or `UInputComponent` injection. |
| `CreateInvalidObject` | — | none | **GAP**, not used — use `nullptr` and `ue::IsValid` guards. |
| `pcall` | 112 (`safe`), 4004 (`step`); `safe()` is called 176 times, `pcall` 126 times | C++ `try/catch(...)` | OK — every one of the 176 `safe()` sites becomes a guarded call; a single unguarded throw kills the tick. |
| `print` + `log()` | 89; `log()` is called 161 times | none | **GAP** (console only): `log()` writes `probe.log` and stdout. No UE4SS console exists in a shipping build; use `OutputDebugStringA` as well if a debugger is attached. |

### 1.2 Object discovery / identity

| Facility | Lines | `ue::` equivalent | Status |
|---|---|---|---|
| `FindAllOf(clsShortName)` | 185, 231, 562, 639, 712, 941, 1195, 1515, 2414, 2744, 3063, 3113, 3368, 3567, 3746, 3826 | `ue::FindAllOf` | OK. **Walks all of GObjects** — never on the 15/30 Hz path (`main.lua:630`). |
| `FindFirstOf(clsShortName)` | 2328, 2705, 2825, 3060 | `ue::FindFirstOf` | OK |
| `StaticFindObject(path)` | 281, 517, 676, 1829, 2698, 3162, 3163, 3521, 3655, 3706, 3927 | `ue::FindObject` | OK. Resolves `/Script/<Module>.<Class>`, `/Script/Sparta.Default__<Class>` (CDO), `/Game/...` assets. |
| `LoadAsset(path)` | 258-277 (`requestGameLoad`) | none | **GAP**. Native mechanism: `UAssetManager::GetStreamableManager().LoadSynchronous(FSoftObjectPath)` or `StaticLoadObject`. Gated off by `LOAD_ON.txt` in Lua because it was suspected in crashes; the native loader may be safe, but keep the gate and the 3-tries/2 s rate limit. |
| `obj:IsValid()` | 120, 137, 524, 1807, 2077 (via `isLive`), 2275 (`wsValid`), 3831, 3835, 3885 | `ue::IsValid` | OK. Mandatory before every call on a cached pointer (`HANDOFF.md` §18 crash root cause). |
| `obj:GetFullName()` | 128 (`fullname`) and 70 call sites | `ue::GetFullName` | OK. Also used as the **object identity key** — two wrappers of one UObject did not compare equal in Lua (`LOOK.same`, `main.lua:957`); in C++ pointers are stable, so `==` is correct and the string compare can go away, but keep `GetFullName` for tails/keys. |
| `obj:GetClass()` | 1274, 1784 | `ue::GetClass` / `ue::GetClassName` | OK |
| `obj:GetOuter()` | — | `ue::GetOuter` | not used (0 call sites) |
| `obj:GetFName():ToString()` | 2745, 3065, 3115, 3119, 3369, 3568 | `ue::GetName` (returns name) | **PARTIAL** — `ue::GetName` presumably resolves through the FName pool; every `GetFName()` use here wants the plain name string (actor owners, data-layer names, chest names). Confirm `ue::GetName` returns the same bytes as `FName::ToString()`. |
| `actorTail(fullname(o))` (`([%w_]+_C_%d+)$` then `([^%.%:]+)$`) | 76-81 | port as a string helper | OK — **must reproduce exactly**: it is the cross-client actor key for enemies (`docs\DESIGN.md` "Matching the peer's actors"). |
| `FName(str)` constructor | 823, 1165, 1831, 2733, 2734, 2768, 2769, 2774, 2914, 2919, 2951, 2978-2981, 3029-3036, 3120, 3128, 3147, 3171, 3180, 3221, 3222, 3239, 3249, 3707 | none | **GAP (critical)**. Every FName parameter of every game function needs this. Native mechanism: `FName::FName(const wchar_t*, EFindName)` by signature scan, or `ue::FindObject`-style `FName(FNameEntry)` via the FName pool (documented `FName::AppendString @ 0x13F8FA0`, `FNamePool @ 0xAF57380`). **Add `ue::MakeFName(std::string)`.** |
| `{ TagName = FName(id) }` FGameplayTag literal | 3029, 3030, 3128, 3147 | none | **GAP**. Struct construction from C++. Native: build an `FGameplayTag` by value (single `FName` member) — fine in C++, but `ue::Call` must accept a struct-by-value param. |
| `FVector` / `FRotator` / `FTransform` / `FQuat` literals | 809, 1133, 1169-1171, 1626, 1692, 1822, 2092, 2187, 1763, 1660 | `ue::FVector` (double) etc. | **PARTIAL** — the `ue::` layer must expose `FRotator`, `FTransform`, `FQuat` (or the port defines them) and `ue::Call`/`Get/Set` must accept them by value. |
| `out` tables (`out.OutValue`, `out[i]`, `K2_SetActorLocation(...,{},true)` out `{}`) | 2731-2739, 3164-3168, 1626, 1709, 1822, 2092, 1169-1171, 2162 | `ue::Params` (Set/Get by param name) | **PARTIAL** — needs out-params, struct out-params, and `FHitResult{}` placeholder values. `K2_SetActorLocation(loc, bSweep, OutSweepHitResult, bTeleport)` is called with `{}` for the hit result. |
| `TArray` access: `arr:GetArrayNum()`, `arr[i]` | 2803, 2831, 2839, 2850, 2943, 3195, 3537 | none | **GAP (critical)**. Native mechanism: reflect the `TArray` property and cast to `TArray<T>` (Data/Num/Max — 16 bytes on x64), or expose `ue::ArrayNum/ArrayAt`. Affects quest log, chest inventory, data-layer list, `K2_GetComponentsByClass` results. |
| `TMap:ForEach(fn(k,v))` + `k:get()/v:get()` | 2712-2724 (`ActorInts`/`ActorBools`), 2814 (`CurrentQuestObjectiveProgress`), 2865 (`NPCRelationships`/`NarrativeEvents`) | none | **GAP (critical)**. Native: iterate `TMap<FName, FSpartaMapInt>` etc. by layout, or find game accessors. Note `SpartaSaveSubSystem.SaveObjects:ForEach` **hard-crashes** (`HANDOFF.md` §14) — do not go near that map. |
| `iterArray` (unwrap `RemoteUnrealParam` then `ForEach`) | 681-695 | not needed | Lua-specific wrapper; in C++ the array is a real `TArray<UObject*>`. |
| `prop:ImportText("(BaseValue=%.3f,CurrentValue=%.3f)", ContainerPtrToValuePtr(hs), 0, hs)` | 1227-1241 | none | **GAP**. Native mechanism: `FProperty::ImportText` by signature, **or** write `FGameplayAttributeData{BaseValue, CurrentValue}` (two floats) directly at `ue::PropOffset(HealthSetProperty, "Health")`. The second is far simpler and is the recommended port. |
| `hs:Reflection():GetProperty("Health")` | 1235 | `ue::FindProperty` | OK (`FindProperty` + `PropOffset` covers it). |
| Property read by **runtime string** `o[field]` | 2422, 2503, 2509, 2747, 3555, 3556, 3724 | `ue::FindProperty` + `PropOffset` + raw read | **PARTIAL** — `ue::Get<T>` is compile-time-typed; the world-state scanner iterates a table of field names (`WS_KINDS`, `main.lua:2211`) whose type is `bool`. Port with `FindProperty` → check `FProperty` type → read `bool` at `PropOffset`. |
| Nested struct member write `mc.Velocity = {X,Y,Z}` | 1619, 1823 | `ue::Get<ue::UObject*>(obj,"CharacterMovement")` then `Set<FVector>` | OK |
| `obj.CharacterMovement.MovementMode`, `MaxAcceleration`, `bOrientRotationToMovement` | 551-556, 1790, 1793, 1808, 2095, 2148, 3760, 3976 | `Get<...>`/`Set<...>` | OK |
| `type(v) == "number"`, `tonumber`, `wsBool(v)` | 2268-2271, 3230 | property type introspection | PARTIAL — same as above. |

### 1.3 File / time / string facilities

| Facility | Lines | Native equivalent |
|---|---|---|
| `io.open(p,"r")` + `f:read("*l")` + close | 33-39, 103-109, 2050-2052, 2081-2085, 2333-2349 | `std::ifstream` binary, read one line |
| `io.open(p,"a")` + `write` + `flush` (probe.log) | 91-93 | `std::ofstream` append, **flush every line** (the harness tails this file live) |
| `io.open(p,"w")` + `write` + `close` (out_/sync/heartbeat/result) | 96-101 | truncate + write + close, single `WriteFile`; **must not** leave a partial line (the relay reads the whole file) |
| `f:lines()` | 2084, 2336 | `std::getline` loop |
| `os.remove` | 2085 (cmd.txt), 4130-4131 (stale out/peer) | `DeleteFileA`/`std::filesystem::remove` |
| `os.time()` (wall seconds, 1 s resolution) | 225, 497-499, 551, 1551-1558, 1967, 1977, 2026, 2048-2049, 2064, 2102, 3703, 4103-4116 | `std::chrono::system_clock` seconds, or `time(nullptr)` |
| `os.date("%H:%M:%S")` | 83 | `GetLocalTime` |
| `os.clock()` (CPU time!) | 1740, 1753 (`patrol`) | **must become real time** — `os.clock()` is process CPU time and runs several times faster than real time (`main.lua:217`, `HANDOFF.md` §10). Patrol duration was tuned against real seconds, so use `coop::net::Wall()`. |
| `GSTATICS:GetRealTimeSeconds(WORLD)` (`rt()`) | 221-226 | `UGameplayStatics::GetRealTimeSeconds(World)` via `ue::Call` on the CDO, or `UWorld::RealTimeSeconds`. **The world pointer must be refreshed after every map load** (`main.lua:4096`) — `net::Now()` is that clock. |
| `string.format("%.1f/%.2f/%.3f/%d/%s")` | throughout, all wire output | `snprintf` (C locale) — must produce byte-identical text |
| `s:gmatch/gsub/match/find/sub/byte/lower` | throughout, incl. the wire parsers | `std::string` + manual scanning; keep the same grammar (§2.5) |
| `#s`, `table.concat`, `table.sort` | throughout | `std::string::size`, manual join, `std::sort` |
| `LOOK.hash` (djb2: `h=5381; h=(h*33+byte)%2^32`) | 863-867 | must be bit-identical (it is in `sync.txt`) |
| `wsHash` (FNV-1a 32-bit) | 2262-2266 | must be bit-identical (it **is** the wire key for world state) |
| `os.getcwd()` — unusable in the sandbox | 31-32 (comment) | the DLL can use `GetModuleFileName(hModule)` — a real improvement, but keep the three relative probes as fallbacks (§2.1) |

### 1.4 Gameplay calls with **no direct `ue::` equivalent** (must be designed)

These are the hard gaps. Each needs a decision before the owning module is written.

| What Lua does | Lines | Why it has no equivalent | Native mechanism needed |
|---|---|---|---|
| `summon /Game/Sparta/Core/Player/BP_PlayerCharacter.BP_PlayerCharacter_C` via `KismetSystemLibrary:ExecuteConsoleCommand(world, text, pc)` | 176-182, 589 | console/Exec is not in the `ue::` API | `APlayerController::ConsoleCommand(FString, bWriteToLog)` or `UEngine::Exec(UWorld*, const TCHAR*, FOutputDevice&)` by signature. **Do not** replace it with `SpawnActor`: `summon` is the proven path (`docs\DESIGN.md` "Constraints and risks"). |
| `actor:AddComponentByClass(cls, true, xf, false)` (creates the peer's weapon meshes and every look part) | 813, 1150 | component creation | Either (a) call the `AActor::AddComponentByClass` UFunction through `ue::Call` (needs `UClass*`, `bool`, `FTransform`, `bool`, out `UActorComponent*`), or (b) `NewObject<UActorComponent>` + `RegisterComponent`. **This is a blocking gap for §3 module 4.** |
| `comp:K2_DestroyComponent(comp)` | 789, 1105, 1128 | — | UFunction, works through `ue::Call` once params are right. |
| `IMeshComponent`/`UStaticMeshComponent`/`USkeletalMeshComponent` class objects via `StaticFindObject("/Script/Engine.<Name>")` | 674-679 | — | `ue::FindClass` OK. |
| `Spawning actors` / `StaticConstructObject` | — | — | **not used** by `main.lua`; do not introduce it (`summon` of an enemy class crashes, `HANDOFF-REMAINING-SYNC.md` §4.4 #12) |
| `TMap` writes: `q.NPCRelationships:Add(name, v)`, `o.CurrentQuestObjectiveProgress:Add(FName, int)` | 3011, 2919, 3224 | TMap | `TMap::Add` by template instantiation, or the game's own setter if one exists |
| `K2_GetComponentsByClass(cls)` → array | 520, 719, 730, 1018, 1025, 1038 | — | UFunction returning `TArray<UActorComponent*>`; needs the TArray gap fixed |
| `Widget:IsInViewport()`, `pc:IsMoveInputIgnored()`, `pc:K2_GetPawn()`, `pc.Pawn`, `pawn.Controller` | 1519, 1538, 162, 1536 | — | all plain UFunction/property access → OK |
| `BP_Spawner_Proximity_..._ComponentBeginOverlapSignature__DelegateSignature(sp.PlayerTrigger, pawn, pawn.CapsuleComponent, 0, false, {})` | 3868-3869 | it is a **Blueprint-generated event function** with 6 params incl. an `FHitResult`-ish last arg | `ue::Call` by full function name with 6 params is enough (the delegate signature function is a normal UFunction). The last `{}` is an empty struct — must be accepted. |
| `obj.SK_Chest:PlayAnimation(obj.ChestMontage, false)` and `SetPosition(1000.0,false)` | 3558-3559, 3709 | — | UFunctions on `USkeletalMeshComponent` → OK |
| `TargetSelectionComponent:SetTargetOverride(puppet, 3.0)` | 2162, 3886 | — | OK |
| `ctrl.BrainComponent:StopLogic("coop mirror")` / `RestartLogic()` | 1789, 1810, 1820 | — | OK (nested property chain) |
| Delegates (`OnDamageReceived`, `OnDeathStarted`, `OnSecondPhaseStarted`, `OnSaveGameLoaded`, …) | — | **not bound in `main.lua`**; no delegate API in `ue::` | **GAP**: stays polled (2 Hz). Do not attempt `ProcessEvent` on delegate `Broadcast` in v1. |
| `FText` / `FString::ToString()` on `GetBoneName(i)`, `GetAttachParentSocketName()`, `GetAttachSocketName()` | 296, 717, 970, 1031, 1165 | `ue::GetName` covers FName; `FText` does not appear | Use `ue::GetName` on the FName-returning variants; avoid `FText`. |
| `ExecuteConsoleCommand` inside `runConsole` for the `console` cmd | 176-182, 2119 | — | same as `summon` |

### 1.5 Summary — the API gaps to close (this is the list the coordinator needs)

1. **`ue::MakeFName(std::string)`** — FName construction. Blocks every game call that takes an FName (quests, store B, sockets, montage slot names).
2. **World/pawn context** — `ue::GetWorld()`, `ue::GetPlayerController()`, `ue::GetPawn()`. The Lua gets these from `UEHelpers`; the `ue::` API as specified has no world accessor at all, and `rt()` needs the world for `GetRealTimeSeconds`.
3. **`TArray` access** (`ArrayNum`/`ArrayAt<T>`) and **`TMap` iteration/Add** — blocks quest log, inventory, store B, data layers.
4. **`ue::Call` must support struct-by-value params and out-params** (`FTransform`, `FGameplayTag`, `FHitResult{}`, out `UObject*`, out `float`/`bool`).
5. **Component creation** (`AddComponentByClass` / `NewObject` + `RegisterComponent`) — blocks peer weapons and the whole look system.
6. **Console command** (`ConsoleCommand`/`Exec`) — blocks `summon` and therefore the puppet.
7. **Asset loading** (`LoadSynchronous`) — optional (was disabled), but needed for the "peer uses an asset we never loaded" case.
8. **`FProperty::ImportText`** or a direct `FGameplayAttributeData` write — blocks health writes (`setHealthNorm`). Prefer the direct offset write.
9. **`FName`/`FText` → string** — `ue::GetName` must give the same bytes as `FName::ToString()`.
10. **No `NotifyOnNewObject`, no key binds, no delegate binding, no `SpawnActor`** — none of these are needed by the current feature set; keep polling. State that explicitly so nobody "improves" it into a crash.

---

## 2. Wire contract (frozen)

The relay is unchanged. `relay\cooprelay.py` requires, per direction, **one file containing exactly
one line of valid JSON** (§2.3). Any parse error is logged as `bad json from mod (N bytes)` and the
packet is dropped (`cooprelay.py:191-196`).

### 2.1 Directory resolution (`coop_dir.txt`)

`main.lua:33-54`:

```
readFirstLine(p): first line of p, or nil
probe, in order:
  "coop_dir.txt"                              -- CWD = Binaries\Win64
  "MortalShell2/Binaries/Win64/coop_dir.txt"  -- CWD = game root
  "./MortalShell2/Binaries/Win64/coop_dir.txt"
first hit with length > 1 wins;  backslashes -> '/',  append '/' if missing
fallback  DIR = "coop/"
```

`deploy.ps1:41` writes `coop_dir.txt` **next to the exe** (`MortalShell2\Binaries\Win64\`) with
forward slashes, a trailing `/`, ASCII, **without a trailing newline**:

```
E:/Games/_MS2-coop-test/MortalShell2/Binaries/Win64/coop/
```

The DLL must accept both `\` and `/` and both with/without the trailing separator. Keep the three
relative probes as fallbacks (the DLL's CWD is the game root, like the Lua's), but prefer
`GetModuleFileNameW(hModule)` + strip the filename.

`E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\coop_dir.txt` (live) contains exactly that
backslash-free form.

### 2.2 Files in the coop dir

| File | Direction | Written by | Rate | Content |
|---|---|---|---|---|
| `out_<ME>.txt` | mod → relay | `sendOwn` (`main.lua:1566-1606`) | **15 Hz**, full rewrite | one JSON line + `\n` (§2.3) |
| `peer_<PEER>.txt` | relay → mod | `cooprelay.py:80-88` (tmp + `os.replace`) | ≤30 Hz | one JSON line + `\n` |
| `playerid.txt` | config | deploy/run-two/MSCoop | once | `A` / `B` / `HOST` / `GUEST` — first line, no newline |
| `role.txt` | config | **`run-two.ps1` only** (`:128-129`) | once | `host` or `guest`; whitespace stripped (`main.lua:66`). **The friend package never writes it** (`MSCoop.ps1` writes only `playerid.txt`), so both packaged clients default to `host` — see §5.7. |
| `cmd.txt` | harness → mod | `run-two.ps1`, `lab.ps1`, `drive.ps1` | on demand | one command per line; **the mod deletes the file after reading all lines** (`main.lua:2085`) |
| `probe.log` | mod → humans | `log()` (`main.lua:85-94`) | per event | appended, flushed per line: `[HH:MM:SS][<ME>] <msg>` |
| `sync.txt` | mod → harness | `writeSyncStatus` (`main.lua:3968-3988`) | 2 Hz, full rewrite | `key=value` lines, see §2.6 |
| `heartbeat.txt` | mod → harness | `LoopAsync(1000)` (`main.lua:4023-4028`) | 1 Hz, full rewrite | `t=`, `hb=`, `t30=`, `t15=`, `t2=`, `phase=` |
| `puppet.txt` | mod → humans | `bootTick` (`main.lua:599`) | once | full name of the adopted puppet + `\n` |
| `result.txt` | mod → harness | `judge` (`main.lua:2036-2038`); `RUNNING\n` at boot (`:4133`) | once | `PASS\|FAIL puppet tracks peer (dist %.1f, %d/30 good; moves %d, snaps %d)` |
| `result_anim.txt` | mod | `animInstrument` (`:2011-2014`) | once | `PASS puppet locomotion: sequence states idle=%d walk=%d sprint=%d, moving-while-animated=%d ticks` |
| `result_npc.txt` | mod | `applyNPCs` (`:1881-1883`) | once | `PASS\|FAIL npc mirror after host move %d/20 within 30u (last %.1f, %s)` |
| `result_ws.txt` | mod | `wsJudge` (`:2549-2551`) | once | `PASS world state: door %s opened, %s collected (live=%d base=%d fail=%d)` |
| `result_wp.txt` | mod | `wpJudge` (`:3297-3299`) | once | `PASS world progression: …` |
| `result_qs.txt` | mod | `qsJudge` (`:3198-3200`) | once | `PASS\|FAIL quest %s added then completed (active now=%s, wq=%d/%d)` |
| `result_qs2.txt` | mod | `qs2Judge` (`:3276-3278`) | once | `PASS\|FAIL quest state %s: …` |
| `result_lift.txt` | mod | `LF.judge` (`:3439-3441`) | once | `PASS lift %s followed the peer to floor %d …` |
| `result_chest.txt` | mod | `CS.judge` (`:3685-3687`) | once | `PASS chest %s: lid replayed (anim pos %.2f), loot %s %d -> %d …` |
| `result_stream.txt` | mod | `streamTick` (`:3799-3803`) | once | `PASS\|FAIL remote area streamed around peer %.0f uu away…` |
| `result_spawn.txt` | mod | `spawnerScan` (`:3844-3845`) | once | `PASS enemy spawned by the peer appeared here too: %s -> %s` |
| `result_pd.txt` | mod | `applyPeerDamage` (`:3922`) | per hit (rewrite) | `PASS peer-side hit applied to own body: %.3f -> %.3f` |
| `result_ai.txt` | mod | `aitest` (`:2165-2166`) | once | `PASS\|FAIL enemy %s targets the peer's body after SetTargetOverride (current %s)` |
| `result_ledger.txt` | mod | `WSL.tick` (`:2394-2396`) | once | `PASS ledger: %s.%s (%s) re-applied from the ledger on load …` |
| `ws_ledger_<slot>.txt` | mod (persisted) | `WSL.save` (`:2355-2361`) | ≤ every 5 s when dirty | one 8-hex key per line, sorted |
| `relay_<id>.log` | relay | `cooprelay.py` | — | relay log |
| `<X>_OFF.txt`, `LOAD_ON.txt`, `GAME_THREAD.txt` | config | harness | once at load | kill switches (§2.7) |

### 2.3 The state line — exact format

Two shapes. **Only `t:"p"` is ever produced.** No `hello`/`bye` (the relay emits its own
`{"t":"hello"}` on UDP join and ignores it, `cooprelay.py:110`, `:197`).

**(a) not ready** (`main.lua:1569`) — written when `PLAY_READY == false`, still at 15 Hz:

```json
{"t":"p","id":"A","sq":17,"rdy":0}
```

**(b) full** (`main.lua:1599-1604`), one line, no spaces except inside strings:

```json
{"t":"p","id":"%s","sq":%d,"x":%.2f,"y":%.2f,"z":%.2f,"vx":%.1f,"vy":%.1f,"vz":%.1f,"yaw":%.1f,"pitch":%.1f,"anim":"%s","spd":%.1f,"mseq":"%s","mpos":%.3f,"mrate":%.3f,"mid":%d,"body":"%s","apart":"%s","wpn":"%s","wsock":"%s","wpns":%s,"lk":"%s"%s%s%s%s}
```

Field order is exactly as above. Trailing `%s` groups, in order:
`npcJSON()+",\"pd\":%.3f"` , `nhpJSON()` , `deadJSON()` , `WS.payload+WP.payload+LF.payload+CS.payload`
(the last is `""` when `WS_OFF.txt` exists).

Field table:

| Key | Type / format | Source | Notes |
|---|---|---|---|
| `t` | `"p"` | const | relay only checks `t=="hello"` |
| `id` | `"A"`/`"B"` | `ME` | relay drops packets whose `id` equals its own `--player` |
| `sq` | `%d` | `SEND_SEQ`, +1 per publish **including the not-ready shape** | freshness/`live` detection |
| `x,y,z` | `%.2f` | pawn `K2_GetActorLocation` | |
| `vx,vy,vz` | `%.1f` | pawn `GetVelocity`; if `abs(vx)+abs(vy) < 1` and the position moved, a **synthesised** velocity from the position delta × 15 (`main.lua:1583-1588`) | teleport/cutscene handling |
| `yaw`,`pitch` | `%.1f` | pawn `K2_GetActorRotation` | roll is not sent |
| `anim` | `idle`/`walk`/`run`/`action` | speed thresholds 30/250, `action` when a montage is playing (`:1593-1596`) | informational; the receiver animates from `vx/vy` |
| `spd` | `%.1f` | `sqrt(vx²+vy²)` | |
| `mseq` | string | montage **resolved to the segment animation path** (`ownMontage`, `:1485-1505`) | `""` when no montage |
| `mpos` | `%.3f` | position inside that sequence (`AnimStartTime + local_t`) | |
| `mrate` | `%.3f` | `segmentRate * Montage_GetPlayRate` | |
| `mid` | `%d` | increments whenever the montage object changes **or the play head goes backwards** (`:1492`) | receiver uses it in the anim state key |
| `body` | string | own shell mesh asset path (`/Game/...`), from `GetSkeletalMeshAsset()` or `SkeletalMeshAsset`/`SkinnedAsset`/`SkeletalMesh` (`:619`) | `""` when unknown |
| `apart` | string | own AnimBP path (`GetAnimClass`) | informational only |
| `wpn` | string | first rendered weapon mesh asset path | |
| `wsock` | string | the socket that weapon hangs from (`GetAttachParentSocketName`) | |
| `wpns` | **array section** (§2.4) | every rendered weapon mesh | `[]` when none |
| `lk` | **quoted string** (§2.4) | look signature | `""` when disabled |
| `npc` | array section | owned enemies | max 16 (`MAX_NPC`, `:1253`) |
| `pd` | `%.3f` | **cumulative** damage the peer's body took (`PD.sent`) | monotonic |
| `nhp` | array section | lowest normalized health seen per enemy | ≤16 |
| `dead` | array section | dead enemy tails | max 40 emitted (`:1370`) |
| `ws` | array section | store A keys | live flips + rotating window of 40 (`WS_WINDOW`) |
| `wb` | array section | store B ints/bools | window 12 |
| `wq` | array section | quest log | window 16 |
| `wd` | array section | data-layer states, **host only** (guest sends nothing) | window 16 |
| `wl` | array section | lifts | all known lifts |
| `wc` | array section | chest events | all events kept 48 s |

Measured size: A = 2852 B, B = 4157 B at 15 Hz ≈ 60 KB/s per direction, **one UDP datagram**,
therefore above the 1472-byte MTU and fragmented. Keep the caps; do not grow the packet.

### 2.4 Array sections and their grammars

`section(line, key)` (`main.lua:1434-1451`) finds `"<key>":` and then the matching `[...]` by
bracket depth. It is a **depth counter, not a JSON parser** — nested arrays work, but a `[` or `]`
inside a quoted string would break it. Current payloads never contain brackets in strings; keep it
that way.

Per-section grammars (the receiver's `gmatch` patterns, i.e. what the port must emit):

| Section | Format (byte grammar) | Emitter |
|---|---|---|
| `npc` | `["<tail>",%.1f,%.1f,%.1f,%.3f,%d,%.1f,%.1f,%.1f,%.1f,"<montagePath>"]` joined by `,` | `npcJSON` `:1332-1354` |
| `nhp` | `["<tail>",%.3f]` | `nhpJSON` `:1356-1363` |
| `dead` | `"<tail>"` | `deadJSON` `:1365-1373` |
| `wpns` | `["<assetPath>","<S\|K>","<socket>",%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]` (loc x,y,z then rot pitch,yaw,roll) | `localWeaponInfo` `:776-777` |
| `ws` | `["<hex8>",<0\|1>]` | `wsBuildPayload` `:2455-2469` |
| `wb`,`wq`,`wd` | `["<name>",<int>,<0\|1 live>]` | `wpSection` `:2653-2670` |
| `wl` | `["<name>",<seq>,<floor>,<0\|1 active>]` | `LF.build` `:3446-3454` |
| `wc` | `["<name>",<seq>,<count>,<0\|1 done>,"<path=n;path=n>"]` | `CS.build` `:3611-3617` |

The `lk` **string** grammars (`main.lua:1013`, `:1117-1145`):

```
lk = "bm=<mat>,<mat>..;p=<S|K>|<asset>|<socket>|lx,ly,lz|qx,qy,qz,qw|sx,sy,sz|<0|1 leader>|<mat>,..;p=.."
```

- `bm=` body-slot materials, comma separated, may be `""` (`bm=`).
- `p=` a part; fields separated by `|`, **exactly 8 fields**; transforms with `%.2f` (loc), `%.5f`
  (quat), `%.3f` (scale); final field = material list (`""` allowed).
- socket `@root` means "attach to the actor root with socket `None`" (`:1161-1164`).
- receiver keys parts by the whole `p=...` string, so **the string must be stable**; parts are
  `table.sort`ed (`:1043`) before hashing.

### 2.5 Parsing the peer line (what the port must reproduce, bug-for-bug)

`applyPeer` (`main.lua:1936-1983`):

1. `readLine("peer_<PEER>.txt")` — first line only; `nil`/`""` → no-op.
2. **substring** search for `"rdy":0` (`:1942`) — if found, `lastPeer.live = false` and **return**
   (no other field is read, no section applied). Port: parse `"rdy"` as an integer and test `==0`
   — behaviourally identical and safer, but the *effect* must be identical: nothing else updates.
3. `parseKV` (`:1426-1432`): for every `"<word>"\s*:\s*<number>` pair, plus the `"id"` string.
   Numbers are `[%-%d%.eE]+`; `x` missing → return (`:1947`).
4. String fields are extracted with `'"body"%s*:%s*"([^"]*)"'` etc. (`:1950-1959`).
5. `applyPeerDamage(m.pd)` (`:1960`).
6. `fresh = (m.sq == nil) or (m.sq ~= lastPeer.sq)`; `at = fresh ? rt() : lastPeer.at` (`:1963-1964`).
7. `live` latches once `sq` changes (`:1966`).
8. `mseq` handling (`:1969-1976`): a non-empty `mseq` (len > 4) sets `PEER_MONTAGE` **only when the
   packet is fresh or PEER_MONTAGE is nil**; an empty `mseq` clears it. This is what keeps the
   montage play head advancing off the packet clock, not per-packet.
9. `pcall` each of `applyNPCs`, `applyHealth`, `applyDead`, and (if world state on)
   `wsReadPeer`, `wpReadPeer`, `liftReadPeer`, `csReadPeer` (`:1979-1982`).

### 2.6 What `sync.txt` must contain (the harness reads it)

Written at 2 Hz, in this exact order (`main.lua:3977-3987` then the status functions):

```
t=<ME>
phase=<menu|world|summon|adopt|sync>
role=<host|guest>
puppet=<GetFullName(puppet)>
puppetpos=%.1f %.1f %.1f
selfpos=%.1f %.1f %.1f
movemode=<puppet.CharacterMovement.MovementMode>
puppetspeed=%.1f
peerspd=%.1f
anim=<ANIM.state|nil>
animidle=%d
animwalk=%d
animsprint=%d
animmoves=%d
body=<own shell path>
peerbody=<peer shell path>
wpn=<own weapon path>
peerwpn=<peer weapon path>
wpnapplied=<WEAPON.applied, i.e. the whole peer wpns string>
look=<LOOK.hash(LOOK.mine)>
lookparts=%d
peerlook=<LOOK.hash(lastPeerLook)>
lookapplied=<LOOK.hash(LOOK.applied)>
peerlookparts=%d
lookmissing=%d
gameloads=%d
hp=<tail=0.500,... sorted>
dead=<tail,... sorted>
ws=seen:%d true:%d peer:%d live:%d base:%d fail:%d scans:%d
ledger=<state> keys:%d good:%d bad:%d applied:%d
wp=b:%d/%d+%d!%d q:%d/%d+%d!%d d:%d/%d+%d!%d
lift=n:%d seq:%d peer:%d applied:%d fail:%d
chest=n:%d seq:%d events:%d applied:%d loot:%d queue:%d fail:%d
stream=src:%s n0:%s n1:%s npcown:%d npcmirror:%d
enemy=driven:%d taken:%d released:%d montages:%d spawnask:%d spawnok:%d retarget:%d pdsent:%.3f pdapplied:%d
perf=near:%.2fms(%d) far:%.2fms(%d)
```

`run-two.ps1` `Get-SyncValue` matches `^<key>=(.*)$` per line, so **keys must stay at line start**.

### 2.7 `cmd.txt` command set (`runCommands`, `main.lua:2080-2196`)

Every command is one line; the file is read fully then **deleted**; each command logs
`cmd handled: <line>` (`:2194`).

| Command | Args | Effect (line) |
|---|---|---|
| `tp` | `x y z` | teleport own pawn, then `CharacterMovement:SetMovementMode(5,0)` (hover) and `TP_HOLD` for 6 s (`:2088-2099`, `:3756-3762`) |
| `lookadd` | `<attachable class path>` | queues `BPC_AttachableItems:AddItem(cls)` via `lookTestTick` (`:2100-2104`, `:2061-2078`) |
| `lookdel` | — | `RemoveMe()` + hide the added attachable (`:2105-2112`) |
| `info` | — | logs phase/puppet/peer/drive counters/dead count (`:2113-2117`) |
| `console` | `<text>` | `runConsole(text)` → `ExecuteConsoleCommand` (`:2118-2119`) |
| `kill` | — | `killTail(collectNPCs()[1].tail, "cmd")` → `StartDeath` (`:2120-2122`) |
| `hurt` | `[frac=0.5]` | `setHealthNorm(nearest enemy, frac)` (`:2123-2134`) |
| `patrol` | `[secs=10]` | circle walk of the own pawn, r=260, 260 uu/s (`:2135`, `:1733-1746`) |
| `wstest` | — | open nearest closed door + pick up nearest pickup (`:2136`, `:2570-2594`) |
| `wptest` | — | store B chest write + tag drop/re-add + data-layer flip (`:2137`, `:3110-3140`) |
| `qstest` | — | add a quest from `DT_QuestData1`, complete it after 12 s (`:2138`, `:3159-3174`) |
| `qs2test` | — | add+track a quest, relationship +2, objective at 6 s, stage at 12 s, complete+turn in at 30 s (`:2139`, `:3210-3228`) |
| `lifttest` | — | `ActivateLift()` on the nearest resting lift (`:2140`, `:3470-3479`) |
| `cstest` | — | open the nearest unopened chest: `TimesUsed=1`, store B write, lid animation, +1 item (`:2141`, `:3695-3720`) |
| `travel` | `x y z [secs=60]` | straight-line hover trip (`:2142-2152`, `:1653-1666`) |
| `pdtest` | — | `setHealthNorm(puppet, 0.9)` → produces `pd` for the peer (`:2153-2156`) |
| `aitest` | — | `TargetSelectionComponent:SetTargetOverride(puppet, 10.0)` + `result_ai.txt` (`:2157-2169`) |
| `hp` | — | logs normalized health of the 6 nearest enemies (`:2170-2175`) |
| `npctp` | `x y z` | teleports the nearest enemy and logs `npctp -> <tail> at x y z` (`:2176-2192`) |

**Log lines the harness greps** (exact, from `run-two.ps1`):

| Pattern | Scope | Emitted by |
|---|---|---|
| `^(PASS\|FAIL)` in `result.txt` | A and B | `judge` `:2035-2038` |
| `^<key>=(.*)$` in `sync.txt` (see §2.6) | A and B | `writeSyncStatus` + status fns |
| regex `npctp -> (\S+)` in `probe.log` | B (guest) | `runCommands` `:2189` |
| literal `mirroring peer-owned enemy <tail>` in `probe.log` | A | `applyNPCs` `:1856` |
| last 15 lines of `probe.log` (no pattern) | A and B | `log()` |

Verdict files that are **gated** in `run-two.ps1`'s final `$pass` (a missing or non-`PASS` file
fails the run): `result_anim.txt` (B), `result_ws.txt` (B), `result_wp.txt` (B), `result_spawn.txt`
(A), `result_pd.txt` (B), `result_stream.txt` (A).

Verdict files that are only **printed** (a red one does not fail the run today — do not rely on
them being green): `result_qs.txt` (B), `result_qs2.txt` (B), `result_lift.txt` (B),
`result_chest.txt` (B), `result_ai.txt` (A), `result_ledger.txt` (A and B), `result_npc.txt` (B).

`lab.ps1` greps **nothing** in the mod log. It depends on: `Mods\mods.txt` (it rewrites it),
`coop_dir.txt`, `coop\playerid.txt` = `A`, `coop\ready.txt` (**written by `CoopLab`, not by
`CoopMod`**), `coop\cmd.txt`, `result_anim.txt`, `result_kill.txt`, `bones.txt`, `enemies.txt`,
`hc.txt`, `prop.txt`, `lab.log`, and it copies `UE4SS.log` out of `Binaries\Win64`. All of that is
Lua-mod infrastructure — see §5.

`run-two.ps1`'s final gate (line 402-403) is:

```
RESULT: PASS  requires
  result.txt A ^PASS, result.txt B ^PASS,
  SHELL   (A body == B peerbody and B body == A peerbody, both non-empty)
  WEAPON  (A peerwpn == B wpn and A wpnapplied contains B wpn)
  LOOK    (lookparts/look move on B and A's lookapplied/peerlookparts follow, then revert)
  STREAM  ^PASS, NPCFAR (mirroring line), SPAWN ^PASS, MIRROR (A enemy taken:>0), PEERHIT ^PASS
  and when not -StreamOnly: DAMAGE (A hp matches =0.[0-6]NN), DEATH (B dead non-empty),
  ANIM ^PASS, WORLD ^PASS, PROGRESS ^PASS
```

`WORLD` is a known permanent red in the current saves (`HANDOFF.md` §14) — that is a fixture
problem, and the port must not "fix" it by weakening anything.

### 2.8 Kill switches (`main.lua`, read **once at load**, file presence = switch)

| File | Effect | Line |
|---|---|---|
| `ANIM_OFF.txt` | no puppet animation | 383 |
| `WEAPON_OFF.txt` | no peer weapon visuals | 663 |
| `WS_OFF.txt` | no world state at all (A+B+quests+layers+lifts+chests) | 669 |
| `STREAM_OFF.txt` | no remote streaming sources | 511 |
| `LOOK_OFF.txt` | no look sync | 855 |
| `LOAD_ON.txt` | **enables** the game-thread asset loader | 255 |
| `WS_LEDGER_OFF.txt` | no store-A ledger | 2304 |
| `QS_OFF.txt` / `DL_OFF.txt` | no quests / no data layers | 2624-2625 |
| `LIFT_OFF.txt` / `CHEST_OFF.txt` | no lift / chest sync | 3359, 3514 |
| `MIRROR_OFF.txt` | no non-owner enemy mirroring | 3814 |
| `GAME_THREAD.txt` | runs the loops on the game thread (**crashed both clients**) | 4041 |

The DLL must read the same file names from the same directory, at the same moment (module init),
and expose them in one `Toggles` struct.

---

## 3. Module decomposition (7 modules)

Design rules for the split:

- One module = one `.cpp` (+ its slice of `coop_state.h`). Modules never include each other's
  headers; they talk through `coop::State` (`coop_state.h`) and the declared functions.
- **Only `coop_net` touches files and the wire.** Only `coop_local` reads the own player. Only
  `coop_puppet` moves the puppet. Only `coop_appear` creates/destroys components. Only
  `coop_combat` writes health. Only `coop_world` / `coop_progress` write save-object state.
- Every module exposes one `void TickN(double dt)` per rate it needs, and its **named packet
  writers** (`SerializeNpc`, `SerializeWs`, … — see the list in §3.8) if it contributes to the
  state line. There is no generic `Serialize`: the frozen field order interleaves modules.
- Every module has `void Reset()` (called on map load / world teardown) that drops cached
  `ue::UObject*` — the single most important crash defence (`HANDOFF.md` §18).

| # | Module | Lua lines | ≈ lines | Tick | One-line job |
|---|---|---|---|---|---|
| 1 | `coop_net` — session, IO, wire, commands, status, loops | 24-153, 1409-1451, 1566-1606, 1936-1983, 2055-2196, 3957-3988, 3989-4133 | 590 | 15 Hz publish/apply, 2 Hz status, 1 Hz heartbeat | the only module that touches the filesystem and the packet |
| 2 | `coop_local` — own player capture + readiness | 154-187, 609-651, 652-705, 706-785, 1453-1564, 1984-2054 | 400 | 15 Hz (capture/montage), 2 Hz (body, ready) | what *I* am: transform, velocity, montage, shell, weapons, ready gate, verdict |
| 3 | `coop_puppet` — peer body, summon/adopt, drive, streaming | 188-491, 492-606, 1607-1765, 3738-3811 | 650 | 30 Hz drive, 2 Hz boot/stream | owns the peer's body actor and everything that moves or streams it |
| 4 | `coop_appear` — peer weapons + full look | 652-671, 673-700, 702-835, 836-1191 | 540 | 2 Hz | rebuild the peer's visible stuff as plain engine components |
| 5 | `coop_combat` — health, enemies, mirror, damage, spawn replay | 1192-1408, 1766-1935, 3808-3956 | 535 | 15 Hz (apply), 2 Hz (sample/scan) | enemy list, monotonic health, death, mirror drive, hits on the puppet |
| 6 | `coop_world` — store A, ledger, lifts, chests | 2197-2602, 3344-3737 | 800 | 2 Hz | persistent world booleans + lift/chest replay |
| 7 | `coop_progress` — store B, quests, data layers | 2603-3343 | 741 | 2 Hz | named save data, quest log, data-layer states |

Shared base layer (lives in `coop_net`, i.e. module 1, because it is the only writer of the
packet and everything depends on it): DIR/ME/ROLE resolution, `log`, `writeFile`, `readLine`,
`Now()`, `Wall()`, `FullName`, `Valid`, `Tail`, `AssetPath`, `GetPawn`, `GetPC`, `GetWorld`,
`Loc`, `Dist`, the `ue::FindClass` cache, `ue::FindObject` + missing-asset bookkeeping, and the
step guard.

---

### 3.1 `coop_net` — session / transport / commands / status / loops

**Lua ranges:** `24-153` (config, DIR, ME, ROLE, tuning, `actorTail`, `now`, `log`, `writeFile`,
`readLine`, `safe`, `fullname`, `validActor`, `isLive`, forward-declared globals), `154-187`
(`getWorld/getPC/getPawn/locOf/dist/runConsole/playerChars` — the base layer the port keeps here),
`1409-1451` (`peerFile`, `parseKV`, `section`), `1566-1606` (`sendOwn`), `1936-1983` (`applyPeer`),
`2055-2196` (`lookTestTick`, `runCommands`), `3957-3988` (`streamStatus`, `writeSyncStatus`),
`3989-4133` (guards, `step`, heartbeat, `HB`/`onGame`/`runLoop`, the three loops, boot emit,
stale-file cleanup, `=== CoopMod start ===`).

**Functions to port:** `readFirstLine`, `log`, `writeFile`, `readLine`, `safe`, `fullname`,
`validActor`, `isLive`, `getWorld`, `getPC`, `getPawn`, `locOf`, `dist`, `runConsole`,
`playerChars`, `peerFile`, `parseKV`, `section`, `sendOwn`, `applyPeer`, `lookTestTick`,
`runCommands`, `streamStatus`, `writeSyncStatus`, `localHold`, `step`, `onGame`, `runLoop`.

**Owns:** `MODNAME`, `DIR`, `ME`, `PEER`, `ROLE`, `SEND_SEQ`, `lastPeer*` (the parsed packet store,
shared read-only with modules), `lastPeerFile`, `peerSeenAt`, `LOOK_ADD`/`LOOK_TEST`/`LOOK_ADDED`,
`GAME_THREAD`, `PENDING`, `HB`, guard tables `SETTLE_SKIP`/`HOLD_SKIP`, `LOCAL_HOLD`,
`WORLD_SETTLE`.

**Reads:** every other module's packet writers; `state().puppet.phase` (the phase is written by
`coop_puppet` through `net::EnterPhase` / `net::PhaseSeconds`, and read here at 2 Hz);
`coop_local`'s hot fields; `coop_combat`'s `pdSent`; each module's `Status()`.

**UE surface:** `KismetSystemLibrary:ExecuteConsoleCommand(world, text, pc)`;
`AWorldPartitionStreamingSourceComponent` (`bStreamingSourceEnabled`); `WBP_MainMenu_C:ContinueGame`;
`FindAllOf("BP_PlayerCharacter_C")`, `FindAllOf("WBP_MainMenu_C")`, `FindAllOf("WBP_LoadingScreen_C")`,
`FindAllOf("WBP_LoadingScreenBase_C")`, `FindAllOf("WBP_LoadingScreen_FadeToBlack_C")`,
`FindAllOf("StaticMeshActor")`, `FindAllOf("Character")` (via `refreshChars`);
`UEHelpers.GetWorld/GetPlayerController/GetKismetSystemLibrary`; `APawn:K2_GetActorLocation/GetVelocity`;
`APlayerController:K2_GetPawn/IsMoveInputIgnored`; `ACharacter`; `GetComponentByClass` (streaming
sources on the pawn).

**Ticks:** 15 Hz (`sendOwn`, `applyPeer`, `peerWatchdog`), 2 Hz (`runCommands`, `writeSyncStatus`
+ the stream verdict part), 1 Hz (`heartbeat.txt`). The 30 Hz loop is registered here and
dispatches to `coop_puppet`.

**Ticket for this module:** implement `Tick` (30/15/2/1 Hz off `ue::OnTick`), `Publish`, `ApplyPeer`,
`Section/KvNum/KvStr`, `RunCommands`, `WriteSyncStatus`, `Heartbeat`, all file IO, all kill
switches, `Log`, `FullName/Valid/Tail/AssetPath`, `GetWorld/GetPC/GetPawn`, `Now/Wall`,
`Step(label, guard, fn)`, `EnterPhase`, `Reset()`.

---

### 3.2 `coop_local` — own player capture, montage resolution, readiness, verdict

**Lua ranges:** `154-187` (shared helpers, see 3.1), `609-627` (`computeMyBody`), `628-651` (trait
caches `tickChars`/`tickTails`/`SPN_ALIAS`/`refreshChars`), `652-671` (`WEAPON` table +
`WEAPON_SPAWN_ENABLED`), `673-700` (`engineClass`, `iterArray`, `assetPath`), `702-785`
(`localWeaponInfo`), `1453-1482` (`segmentsOf`), `1484-1505` (`OWN_M`, `ownMontage`), `1507-1525`
(`PLAY_READY`, `widgetOnScreen`), `1526-1564` (`readyTick`), `1984-2017` (`animSamples`,
`animInstrument`), `2018-2041` (`judge`), `2043-2054` (`peerWatchdog`).

**Functions:** `computeMyBody`, `refreshChars`, `engineClass`, `iterArray`, `assetPath`,
`localWeaponInfo`, `segmentsOf`, `ownMontage`, `widgetOnScreen`, `readyTick`, `animInstrument`,
`judge`, `peerWatchdog`.

**Owns:** `MYBODY`, `MYPART`, `MYWPN`, `MYWSOCK`, `MYWPNS`, `tickChars`, `tickTails`, `SPN_ALIAS`,
`WPN_LOGGED`, `PLAY_READY`, `READY_SINCE`, `READY_WHY`, `OWN_M`, `montageSegs`, `montageWarned`,
`animSamples`, `animMoveTicks`, `animStillTicks`, `animVerdictWritten`, `goodTicks`, `totalTicks`,
`sentPos`, `verdictWritten`, `lastPeerWarn`, `CLS` (class cache).

**Reads:** `net::Now/Wall`, `state().peer` (for `judge`), `puppet::Actor()` (to exclude the puppet
from `tickChars` and from the pawn check), `state().puppet.driveInputs`/`driveTeleports` (for the
`result.txt` verdict).

**UE surface:** `APawn:K2_GetActorLocation/K2_GetActorRotation/GetVelocity`;
`USkeletalMeshComponent` (`GetNumBones`, `GetSkeletalMeshAsset`, `SkeletalMeshAsset`, `SkinnedAsset`,
`SkeletalMesh`, `GetAnimClass`, `GetAnimInstance`); `ASpartaWeapon` (`HoldingCharacter`,
`GetAttachParentActor`, `GetAttachParentSocketName`, `K2_GetRootComponent`, `WeaponMesh`,
`WeaponComponents`, `AdditionalWeapon`, `K2_GetComponentsByClass`);
`USceneComponent`/`UMeshComponent` (`IsVisible`, `bHiddenInGame`, `RelativeLocation`,
`RelativeRotation`, `StaticMesh`, `SetStaticMesh`); `UAnimMontage` (`SlotAnimTracks` →
`AnimTrack.AnimSegments` → `AnimReference`, `StartPos`, `AnimStartTime`, `AnimEndTime`,
`AnimPlayRate`, `LoopingCount`); `UAnimInstance:Montage_GetPosition/Montage_GetPlayRate`;
`ACharacter:GetCurrentMontage`, `Mesh`; `APlayerController:Pawn/K2_GetPawn/IsMoveInputIgnored`;
`APawn:Controller`; `UUserWidget:IsInViewport/IsVisible`; `UWorld:GetFullName` (for `L_MainMenu`);
`AWorldPartitionStreamingSourceComponent:IsStreamingCompleted`;
`UGameplayStatics:GetRealTimeSeconds` (via `net::Now`).

**Ticks:** 15 Hz (`refreshChars`, capture, `judge`, `peerWatchdog`, `animInstrument`); 2 Hz
(`computeMyBody`, `readyTick`, `fillHealthCache` is module 5); on-change (`localWeaponInfo`).

**Note:** `refreshChars` builds `tickChars`/`tickTails` fresh every 15 Hz tick — it is the reason
`FindAllOf("Character")` appears only there. `spawnerScan` (module 5) writes `SPN_ALIAS`, which
`refreshChars` reads. Keep that one-way dependency (module 5 → module 2), never the reverse.

---

### 3.3 `coop_puppet` — peer body, summon/adopt, drive, streaming

**Lua ranges:** `188-491` (body/anim block: `ANIMBONES`, `ANIM_MESH`, `PUPPET_MESH`, `LOCO`,
`SEQ_PATHS`, `SEQ`, `ANIM`, `GSTATICS`, `WORLD`, `rt`, `skinnedMeshesOf`, `missingAssets`,
`GAMELOAD`, `requestGameLoad`, `findObjectByPath`/`findAsset`, `footBonesOnMesh`, `BODY_STATE`,
`APPLIED_BODY`, `PUPPET_HIDDEN`, `BODY_ATTEMPTS`, `puppetMeshComponent`, `applyBodyMesh`,
`ensurePuppetBody`, `syncBodyChoice`, `animOff`, `PEER_MONTAGE`, `normDeg`, `playSeq`, `setRate`,
`playMontage`, `updatePuppetAnimation`), `492-606` (`S`, `enter`, `pt`, `STREAM`, `streamSources`,
`streamSetup`, `setupPuppet`, `bootTick`), `1607-1765` (`peerSpeed`, `driveTeleports`,
`driveInputs`, `stuckTicks`, `clearInput`, `teleportPuppet`, `PUPPET_SPEED`, `prevPuppetPos`,
`lastDriveT`, `peerEverSeen`, `setPuppetHidden`, `PUPPET_YAW`, `travelTick`, `driveTick`, `PATROL`,
`cmdPatrol`, `patrolTick`), `3738-3811` (`actorsNear`, `streamTick`).

**Functions:** all of the above.

**Owns:** the puppet actor pointer, `BODY_STATE`, `APPLIED_BODY`, `PUPPET_HIDDEN`,
`BODY_ATTEMPTS`, `PUPPET_MESH`, `ANIM_MESH`, `ANIMBONES`, `ANIM`, `SEQ`, `PEER_MONTAGE`, `S`
(phase machine), `STREAM`, `GAMELOAD`, `missingAssets`, `PUPPET_YAW`, `PUPPET_SPEED`,
`prevPuppetPos`, `lastDriveT`, `peerEverSeen`, `driveInputs`, `driveTeleports`, `PATROL`, `TRAVEL`,
`TP_HOLD`, `WORLD`, `GSTATICS`, `animOff`.

**Reads:** `state().peer` (`pos`, `vel`, `rot`, `at`, `live`, `sq`), `state().local.body`
(the peer's shell fallback is `state().peer.body`, our own is `state().local.body`), and — from
module 5 only, never the reverse — `coop_combat` reads `puppet::Actor()`.

**UE surface:** `BP_PlayerCharacter_C` (the summoned body); `USparta/SkeletalMeshComponent`:
`GetNumBones`, `GetBoneName`, `SetAnimInstanceClass`, `SetSkeletalMeshAsset`, `SetVisibility`,
`SetAnimationMode`, `GetAnimInstance`, `RootMotionMode`, `SetAnimation`, `Play`, `SetPlayRate`,
`SetPosition`, `GetPosition`, `PlayAnimation`; `ACharacter:SetActorHiddenInGame`,
`K2_SetActorLocation`, `K2_SetActorRotation`, `GetOwner`; `UCharacterMovementComponent`
(`SetMovementMode`, `MovementMode`, `Velocity`, `MaxAcceleration`, `bOrientRotationToMovement`,
`ConsumeMovementInputVector`); `UWorldPartitionStreamingSourceComponent`
(`EnableStreamingSource`, `DisableStreamingSource`, `bStreamingSourceEnabled`,
`IsStreamingCompleted`); `StaticFindObject("/Script/Engine.WorldPartitionStreamingSourceComponent")`;
`UGameplayStatics:GetRealTimeSeconds`; `FindObject` for every `/Game/...` sequence asset
(`A_Shared_Idle_H`, `A_Shared_Locomotion_Walk_Fwd_H2`, `_Walk_Back_H2`, `_Walk_Left_H2`,
`_Walk_Right_H2`, `A_Shared_Locomotion_Sprint_800_H2` under
`/Game/Sparta/Characters/Shells/_Shared/Animation/Locomotion/`).

**Ticks:** 30 Hz (`driveTick`, `travelTick`, `patrolTick`); 2 Hz (`bootTick` until phase `sync`,
`ensurePuppetBody`/`syncBodyChoice`, `streamTick`).

**Critical ordering inside `driveTick`** (`main.lua:1668-1728`): phase check → hidden if no packet →
hidden if stale (>3 s) or `!live` → unhide only when `BODY_STATE=="ready"` → yaw easing →
extrapolate → snap (>400) or ease (k=0.35) → speed telemetry → `updatePuppetAnimation(vx, vy,
PUPPET_YAW, tnow)` in a `pcall`. Every step matters; see §4.

---

### 3.4 `coop_appear` — peer weapons + full look

**Lua ranges:** `652-671` (toggles read), `673-700` (`engineClass`, `iterArray`, `assetPath` —
shared with module 2; keep one copy in `coop_net` or duplicate as `static`), `702-785`
(`localWeaponInfo` — the *sender* half stays in module 2; the *parser* is here), `786-792`
(`clearPeerWeapons`), `794-835` (`applyPeerWeapons`), `836-1191` (the whole look module:
`LOOK` table, `ATTACH_CLASSES`, `LOOK.hash`, `rotQuat`, `qmul`, `qrot`, `relXf`, `compose`,
`matPath`, `matsOf`, `hasLeader`, `LOOK.attachablesOf`, `sameObj`, `chainToBody`, `LOOK.capture`,
`LOOK.tick`, `LOOK.silencePuppet`, `parseVec`, `applyMats`, `LOOK.clear`, `LOOK.apply`).

**Functions:** `clearPeerWeapons`, `applyPeerWeapons`, `rotQuat`, `qmul`, `qrot`, `relXf`,
`compose`, `matPath`, `matsOf`, `hasLeader`, `LOOK.attachablesOf`, `sameObj`, `chainToBody`,
`LOOK.capture`, `LOOK.tick`, `LOOK.silencePuppet`, `parseVec`, `applyMats`, `LOOK.clear`,
`LOOK.apply`.

**Owns:** `WEAPON`, `LOOK` (all fields incl. `comps`, `pendMats`, `applied`, `bodyKey`, `mine`,
`cand`, `candN`, `nparts`, `peerParts`, `missing`, `retryAt`, `silenced`, `hiddenOwn`),
`WEAPON_SPAWN_ENABLED`, `LOOK.enabled`.

**Reads:** `puppet::Actor()`, `puppet::Mesh()`, `state().puppet.appliedBody` (re-applies body
materials when the shell changes), `state().peer.weapons` / `state().peer.look`,
`state().local.wpn` / `wsock` / `wpns`, `net::Find` (asset resolution + `missingAssets`),
`net::Now/Wall`, `net::Log`.

**UE surface:** `AActor:AddComponentByClass(cls, bManualAttachment, FTransform, bDeferredFinish)`,
`K2_DestroyComponent`, `K2_GetComponentsByClass`, `K2_GetRootComponent`,
`K2_AttachToComponent(parent, FName socket, LocationRule, RotationRule, ScaleRule, bWeldSimulatedBodies)`,
`K2_SetRelativeTransform`, `K2_SetRelativeLocationAndRotation`; `USkeletalMeshComponent`
(`SetSkeletalMeshAsset`, `SetLeaderPoseComponent`, `LeaderPoseComponent`),
`UStaticMeshComponent` (`SetStaticMesh`, `StaticMesh`), `UMeshComponent`
(`SetMaterial`, `GetMaterial`, `GetNumMaterials`, `SetCollisionEnabled`, `SetVisibility`,
`IsVisible`, `bHiddenInGame`, `RelativeLocation`, `RelativeRotation`, `RelativeScale3D`);
`USceneComponent:GetAttachParent/GetAttachSocketName/GetAttachParentActor`;
`BP_Attachable_Item_C`/`_Mask_`/`_Clothing_`/`_Heart_`, `BP_AIM_Frog_C`, `BP_AIM_BagHead_C`,
`BP_AIM_Mango_C`, `BP_AIM_FlowerCrown_C`, `BP_Gragu_Helmet_C`, `BP_AIC_Robe_C`
(`Pawn`, `OwnerSpartaCharacter`, `bHidden`, `SetActorHiddenInGame`, `RemoveMe`, `AddItem`);
`BPC_AttachableItems` (`UnsubscribeFromGlobalEvents`, `AddItem`); `UMaterialInterface:Parent`
(`MaterialInstanceDynamic` → parent); `FindAllOf("SpartaWeapon")`,
`FindAllOf("SkeletalMeshComponent")`, `FindAllOf(<each attachable class>)`.

**Ticks:** 2 Hz (`LOOK.tick`, `LOOK.silencePuppet`, `applyPeerWeapons` when `lastPeerWeapons`
changed, `LOOK.apply` when `lastPeerLook` changed, `lookTestTick` while a `lookadd` is pending).

**Hard constraints:** cap 24 parts (`:992`), 16 materials per component (`:919`), 400 bones
scanned (`:295`), retry `applyPeerWeapons` every 5 s and `LOOK.apply` every 3 s on failure
(`:4106`, `:4116`). `LOOK.apply` is *diff by string*: parts are keyed by the full `p=...` string,
so re-emitting an identical string must produce an identical string.

---

### 3.5 `coop_combat` — health, enemy list, mirroring, damage, spawn replay

**Lua ranges:** `1192-1249` (`hcCache`, `fillHealthCache`, `healthCompOf`, `healthNormOf`,
`setHealthNorm`, `startDeath`), `1250-1408` (`MAX_NPC`, `NPC_RADIUS`, `NPC_MARGIN`, `PEER_CLAIMS`,
`deadTails`, `hpSeen`, `collectNPCs`, `hpString`, `NPC_OWNED`, `OWNED_TAILS`, `NPC_LAST`,
`montagePath`, `npcJSON`, `nhpJSON`, `deadJSON`, `sampleCombat`, `killTail`), `1766-1891` (`MIR`,
`MIR_SKIP`, `MIR_STATS`, `mirrorTake`, `mirrorRelease`, `mirrorDrive`, `applyNPCs`),
`1892-1935` (`applyHealth`, `applyDead`), `3808-3956` (`MIRROR_ENABLED`, `SPN`, `AIT`, `PD`, `FT`,
`spawnerScan`, `spawnReplay`, `aiTargetPeer`, `puppetDamage`, `applyPeerDamage`, `frameTime`,
`enemyTick`).

**Functions:** all of the above.

**Owns:** `hcCache`, `hpSeen`, `deadTails`, `PEER_CLAIMS`, `OWNED_TAILS`, `NPC_LAST`, `NPC_OWNED`,
`NPC_MIRRORED`, `MIR`, `MIR_SKIP`, `MIR_STATS`, `mirroredOnce`, `npcTicks`, `npcGood`, `npcVerdict`,
`npcFirst`, `npcMoved`, `SPN`, `SPN_ALIAS` (write side), `AIT`, `PD`, `FT`, `ENEMY_TRACE`,
`MIRROR_ENABLED`.

**Reads:** `state().local.tickChars/tickTails`, `state().peer` (position, `live`, `body`),
`puppet::Actor()`, `state().local.pos` (own location), `net::Now/Wall`,
`state().toggles` (`wsEnabled`, `mirrorEnabled`), `state().local.role` (host/guest ownership bias).

**UE surface:** `SpartaHealthComponent` (`GetOwner`, `GetMaxHealth`, `GetHealth`,
`GetCurrentHealthNormalized`, `IsDeadOrDying`, `StartDeath`, `HealthSet`); `SpartaHealthSet`
(`Health` = `FGameplayAttributeData`); `UProperty:ImportText`/`Reflection()`/`GetProperty`/
`ContainerPtrToValuePtr` (only for the health write); `FindAllOf("SpartaHealthComponent")`,
`FindAllOf("Character")` (through `refreshChars`); `ACharacter:GetClass/K2_GetActorLocation/
K2_GetActorRotation/GetVelocity/GetCurrentMontage/GetController`; `AController:BrainComponent`
(`StopLogic`, `RestartLogic`); `TargetSelectionComponent` (`CurrentTarget`,
`SetTargetOverride(actor, secs)`); `UCharacterMovementComponent:SetMovementMode/MovementMode/
Velocity`; `APawn:PlayAnimMontage/StopAnimMontage/K2_SetActorLocation/K2_SetActorRotation`;
`FindAllOf("UnitSpawner")` + `SpawnedEnemy`; the spawner trigger event
`BndEvt__BP_Spawner_Proximity_Sphere_K2Node_ComponentBoundEvent_1_ComponentBeginOverlapSignature__DelegateSignature(PlayerTrigger, pawn, pawn.CapsuleComponent, 0, false, {})`;
`UGameplayStatics:GetWorldDeltaSeconds`.

**Ticks:** 15 Hz (`applyNPCs`, `applyHealth`, `applyDead` — from `applyPeer`); 2 Hz
(`fillHealthCache`, `sampleCombat`, `enemyTick` → `spawnerScan` every 2 s but the spawner list only
every 10 s, `spawnReplay`, `aiTargetPeer`, `puppetDamage`, `frameTime`).

**Notes:** the health write is the only place a *value* is written into GAS. Keep the
`GetMaxHealth()>0` precondition and the `n<=0 && !dead` rejection (`:1215-1223`) — they are what
stops a freshly spawned actor from being declared dead.

---

### 3.6 `coop_world` — store A, ledger, lifts, chests

**Lua ranges:** `2197-2602` (`WS_KINDS`, `WS` table, `wsHash`, `wsBool`, `wsValid`, `wsMarkTrue`,
`WSL` + its six functions, `wsScan`, `wsBuildPayload`, `wsReadPeer`, `wsOwnerActor`, `wsApplyOne`,
`wsApply`, `wsJudge`, `wsTick`, `wsTest`, `wsStatus`), `3344-3496` (`LF`, `LIFT_ENABLED`,
`LF.cabinZ/scan/apply/judge/build`, `liftReadPeer`, `liftTest`, `liftTick`, `liftStatus`),
`3497-3737` (`CS`, `CHEST_ENABLED`, `CS.im/count/snapshot/lid/scan/build`, `csReadPeer`, `CS.grant`,
`CS.judge`, `csTest`, `chestTick`, `chestStatus`).

**Functions:** all of the above.

**Owns:** `WS_KINDS`, `WS`, `WSL`, `LF`, `LIFT_ENABLED`, `CS`, `CHEST_ENABLED`, `WS_ENABLED`.

**Reads:** `state().local.pos` (distances for the test hooks), `net::Find`, `net::Now/Wall`,
`net::Log`, and `state().progress.wb`-derived input: module 7 calls
`world::ChestRefreshFromStoreB()` when a store-B `TimesUsed` arrives (one-way 7 → 6).

**UE surface:** `FindAllOf(<28 WS_KINDS classes>)`, `USpartaSaveComponent`,
`UObject:GetOwningActor/GetFName`, actor properties `DoorIsOpen`, `MyBool`, `ItemCollected`,
`DoorOpened`, `Unlocked`, `GateOpened`, `CombatEncCompleted`, `GateClensed`, `IsDisarmed`,
`WasEverKilled`, `BossDefeated`, `HutchbackDefeated`, `WardenJumped`, `WardenDefeated`,
`EndStrandingCompleted`, `AllCorruptionCoresClensed`, `Cleansed`, `T1Cleansed`, `T2Cleansed`,
`T3Clensed`, `CenterClensed`, `SensorClensed`, `CageOpened`, `ShotDown`, `StatueFallen`,
`SealBroken`, `StoneSealClaimed`, `BeaconRisen`, `CinematicFired`; `BP_HubDoor_C:OpenDoor`;
`BP_*_Pickup_C:PickUp`, `SetActorHiddenInGame`, `SetActorEnableCollision`;
`SpartaSaveSubSystem:GetActiveSaveProfileSlotIndex`; `BP_HubLift_C` (`bLiftActive`, `TargetFloor`,
`SM_Lift:K2_GetComponentLocation`, `ActivateLift`); `BP_Interactable_Chest_C` (`TimesUsed`,
`RewardGiven`, `SK_Chest:PlayAnimation/SetPosition/GetPosition`, `ChestMontage`,
`LoadChestState`); `SpartaItemManagerComponent` (`GetAllItems`, `GetTotalItemCountByDefinition`,
`AddItemByClass`, `ItemDef`) via `GetComponentByClass`; `SpartaWorldStateHelper` (only the CDO for
`WriteAnInt` in `cstest`).

**Ticks:** 2 Hz only (`wsTick` = ledger tick + **two** `wsScan` calls + `wsApply` + payload build +
`wsJudge`, then `wpTick` is **module 7**, `liftTick`, `chestTick`). Note `wsTick` calls `wpTick`,
`liftTick`, `chestTick` (`:2556-2566`) — in the port make the 2 Hz dispatcher call
`world::Tick2()` and `progress::Tick2()` explicitly so the modules stay independent.

**Budgets (do not remove):** `wsApply` 6/tick (`:2525`); the chest code has no apply budget but
gates everything on an 8 s settle; `wsScan` is **one class per call** and is called twice per tick
(`:2411`, `:2558-2559`) — the class list has 28 entries, so a full pass takes 14 ticks = 7 s.
Keep it.

---

### 3.7 `coop_progress` — store B, quest log, data layers

**Lua ranges:** `2603-3343` (whole block: `WP` table, `QS_ENABLED`/`DL_ENABLED`, `wpStr`,
`wpPut`, `WP_SETTLE`, `wpPrime`, `wpSection`, `wpRead`, `wpObj`, `wpHelper`, `wbScan`, `wbRead`,
`wbRefreshActor`, `wbApply`, `wqObj`, `wqProgress`, `WQ_PRESENCE`, `wqScan`, `wqAdopt`,
`WQ_STAGE_WAIT`, `wqApplyQuest`, `wqApply`, `wdScan`, `wdApply`, `wpTest`, `wpTestFollowUp`,
`qsTest`, `qsTestFollowUp`, `qsJudge`, `qs2Test`, `qs2TestFollowUp`, `qs2Judge`, `wpJudge`,
`wpStep`, `wpTick`, `wpReadPeer`, `wpStatus`).

**Functions:** all of the above.

**Owns:** `WP` (`b`, `q`, `d` sets, `helper`, `sub`, `qlog`, `dlm`, `payload`, `test`, `qtest`,
`q2test`, `seenTag`), `QS_ENABLED`, `DL_ENABLED`, `WP_SETTLE` (10), `WS_LIVE_SECS` (20, shared
constant with module 6), `WQ_STAGE_WAIT` (8).

**Reads:** `state().local.pos` + `puppet::Actor()` location (the 15 000 uu data-layer gate,
`:3082-3083`), `net::Find` / `net::NameOf`, `net::Now`, `net::Log`, `state().local.role`
(host publishes `wd`, guest applies it), `state().world` (only through
`world::ChestRefreshFromStoreB`).

**UE surface:** `SpartaSaveSubSystem:GetCurrentWorldStateSave/GetActiveSaveProfileSlotIndex`;
`USpartaSaveGame:ActorInts/ActorBools` (`TMap<FName, FSpartaMapInt/Bool>` → `.Map:ForEach`);
`/Script/Sparta.Default__SpartaWorldStateHelper` — `WriteAnInt(pawn, FName owner, FName key, int v,
bool bModify)`, `ReadAnInt(ctx, FName, FName, out OutValue)`, `WriteABool`, `ReadABool`;
`FindFirstOf("BPC_QuestLogComponent_C")` — `NarrativeTags.GameplayTags` (array of `{TagName}`),
`CurrentActiveQuests` (array of FName), `CompletedQuests`, `ActiveQuests` (array of
`BP_QuestObject_C`), `CurrentTrackedQuest`, `NPCRelationships` (TMap<FName,int>),
`NarrativeEvents` (TMap<FName,int>), `AddNewQuest`, `CompleteQuest`, `TurnInQuest`,
`AddNarrativeTag({TagName=FName}, false)`, `RemoveNarrativeTag`, `TrackQuests`;
`BP_QuestObject_C` — `QuestID`, `CurrentQuestStage`, `CurrentQuestObjectiveProgress` (TMap),
`IsTurnedIn`, `CurrentStageDetails.Objectives` (`ObjectiveID`, `Quantity`, `bOptional`),
`CurrentStageDetails.StageName`, `OnObjectiveIDHeard(FName)`, `InitializeQuest()`;
`FindFirstOf("DataLayerManager")` — `GetDataLayerInstanceRuntimeState(inst)`,
`SetDataLayerInstanceRuntimeState(inst, state, bool)`;
`FindAllOf("DataLayerInstanceWithAsset")` + `DataLayerAsset:GetFName()`;
`FindAllOf("BP_Interactable_Chest_C")` (in `wbRefreshActor`);
`/Game/Sparta/Dialogue/QuestSystem/DT_QuestData1.DT_QuestData1` +
`/Script/Engine.Default__DataTableFunctionLibrary:GetDataTableRowNames(dt, out)`.

**Ticks:** 2 Hz, and internally `wpTick` calls `wbScan`+`wbApply` every tick, `wqScan`+`wqApply`
every tick, `wdScan` **every other** tick (`wpTickN % 2`), `wdApply` every tick.

**Budgets:** `wbApply` 6/tick, `wqApply` 4/tick, `wdApply` 2/tick, 10 s cooldown per data layer.

---

### 3.8 The shared header contract — `native\coop_state.h`

Written next to this document (`ms2-coop\native\coop_state.h`). It is the *only* thing modules
share. Rules:

- Includes only `<cstdint> <string> <vector> <functional>`; `ue::UObject` is forward-declared.
  No `std::map`/`unordered_map` in the header — modules keep their own indices in the `.cpp` and
  expose `std::vector`s (lookups are small: ≤ a few hundred keys).
- **`coop::State& state()`** is the one global; every sub-struct is public so a module can *read*
  another module's state. Writes are restricted by convention (documented per struct) and by the
  fact that only the owning module's `TickN()` mutates them.
- **`coop::net`** owns the packet: each module implements its **named writers** —
  `combat::SerializeNpc/Pd/Nhp/Dead`, `world::SerializeWs/Lift/Chest`,
  `progress::Serialize` (wb, wq, wd in that order) — each of which appends its `,"key":[...]` (or
  nothing). `net::Publish()` calls them in the frozen order of §2.3 and writes the file. Splitting
  `world` into three writers is deliberate: the frozen order interleaves it with `progress`
  (`ws`, `wb`, `wq`, `wd`, `wl`, `wc`).
- **Peer-line dispatch:** modules implement `OnPeerLine(const std::string& line)`; `net::ApplyPeer()`
  calls them in the frozen order (`combat` → `world` → `progress`).
- **Rate router:** `coop::Tick::Every(hz, label, fn)` then `coop::Tick::Frame(dt)` from
  `ue::OnTick`. Each callback is wrapped in `coop::Step(label, guard, fn)`.
- **Guards:** `coop::Guard::Hold` (skip while `LocalHold()`) and `coop::Guard::Settle` (skip while
  `WorldSettle()`), matching `HOLD_SKIP`/`SETTLE_SKIP` (`main.lua:3995-4002`).
- **`Reset()`** on every module; called by `net` when the world object changes (map load).

The header as shipped compiles standalone with `cl /std:c++17 /W4 /WX` (MSVC 14.35, this machine)
and needs nothing but a translation unit that defines `coop::state()`, `coop::toggles()`,
`coop::ResetAll()` and the module functions it calls. The `ue::` conversion declarations
(`ToVec3`, `FromVec3`, …) are intentionally *declaration-only* so the header never includes the
engine layer; whoever writes the `ue::` adapter implements them.

---

## 4. Fragile / timing-sensitive behaviours (preserve, do not "improve")

| # | Behaviour | Why it is fragile | Lua ref |
|---|---|---|---|
| 1 | `SetAnimInstanceClass(nullptr)` **before** `SetSkeletalMeshAsset(shell)` | swapping the mesh with a live AnimBP crashes in `memcpy` during anim re-init | 328-348 |
| 2 | Animation is `SetAnimationMode(1,true)` **once**, then `SetAnimation(seq)`, `Play(loop)`, `SetPlayRate(x)` | `PlayAnimation()` alone hung a client twice | 397-429 |
| 3 | `RootMotionMode = 1` set **once** after the mode switch | otherwise root-motion clips (attacks/dodges/sprint) drag the mesh off the capsule | 408-411 |
| 4 | The puppet is **positioned directly**; never `SetMovementMode(1)` + `AddMovementInput` | the movement tick crashes for a possession-less pawn (3/3) | 1632-1634 |
| 4b | **Dead code, do not implement:** `TELEPORT_DIST`, `STOP_DIST`, `MAX_SPEED`, `MIN_SPEED`, `STUCK_TICKS` (`:70-74`) and the locals `stuckTicks`/`lastDist` (`:1612-1613`, only ever reset in `teleportPuppet`) are leftovers of the abandoned movement-input drive. They are declared and never read. A porter who "implements the tuning constants" re-creates the crash. (`kStuckTicks` reads like a stuck-detection tick count — there is no stuck detection; the body snaps only on `dist > 400`.) | a stale design that the code already abandoned | 70-74, 1612-1613, 1622-1630 |
| 5 | Puppet stays hidden until (a) a packet exists, (b) `live`, (c) packet age ≤3 s, (d) `BODY_STATE=="ready"` | a mesh swap must not un-hide it; a stale file from a previous session must not draw a body | 1668-1685, 4130-4131 |
| 6 | `PLAY_READY` needs **5 s** of all soft checks; lost only on hard signals; `os.time` not the world clock | the pawn exists 2-4 s before the loading screen; the world clock restarts each map | 1526-1564 |
| 7 | `rt()` = `GetRealTimeSeconds(World)`; `os.clock()` is CPU time (runs fast) | every play-head/extrapolation/timer uses `rt()` | 221-226 |
| 8 | Extrapolation: `age = clamp(now - packetAt, 0, 0.25)`; snap if `dist > 400`; else `k = 0.35` per 30 Hz tick | 15 Hz input against a 30 Hz drive; too much easing = lag, too little = jitter | 1695-1713 |
| 9 | Yaw: shortest arc, ease 0.45/tick, **snap** if the absolute delta > 120° | prevents spinning through 180° on a teleport | 1687-1690 |
| 10 | `at` advances only on a **fresh** `sq` | the montage play head and extrapolation must not advance on a re-read of the same file | 1963-1964 |
| 11 | Montage: `expected = pos + clamp(now-at,0,0.5)*rate`; re-seek only if the drift > 0.12; state key includes `mid` and path | re-seeking every packet stutters; not re-seeking on a re-trigger desyncs | 431-450 |
| 12 | `ANIM.state` string keys (`walk_l`, `m:<id>:<path>`) drive the "did the clip change" test | a re-emitted identical key must not restart the clip | 402-422, 454-491 |
| 13 | Look: publish only after **two** consecutive equal captures (2 Hz) | a one-frame state during a shell swap would otherwise be broadcast | 1050-1061 |
| 14 | Look/weapon apply: retry at 3 s / 5 s, never tighter | an unloaded asset is retried by the game itself; hammering `FindObject` + `AddComponentByClass` is what crashed clients | 4106, 4116 |
| 15 | Store A: ignore a value until the object is **8 s** old | a freshly loaded door reads `DoorIsOpen=true` for ~3 s before its save applies | 2250, 2427-2433 |
| 16 | Store B / quests / layers need a **10 s** prime before anything is published as "live" | the log fills in after load; unprimed, the guest re-applied 8 tags and 7 layers at boot | 2644-2651, 2882 |
| 17 | Ledger is trusted after 10 samples or 45 s; dropped if `bad >= 10 && bad > good` | a different playthrough in the same slot must not poison the world | 2370-2387 |
| 18 | Apply budgets: ws 6, wb 6, wq 4, wd 2 per 2 Hz tick | a fresh baseline can be hundreds of keys; unbudgeted it freezes a frame | 2525, 2758, 2994, 3084 |
| 19 | Rotating windows (ws 40, wb 12, wq 16, wd 16) + "live" rings (20 s) | the relay keeps only the latest line, so the whole set must come around again for a late/restarted peer | 2245-2247, 2455-2469, 2653-2670 |
| 20 | Health is monotonic with `0.02` adoption epsilon and `0.004` local-damage epsilon | two independent worlds must converge, never oscillate | 1389, 1905 |
| 21 | `healthNormOf` returns nil while `GetMaxHealth()<=0` or (`n<=0` and not dead) | a fresh actor reports 0 before attributes init; treating that as death kills enemies at spawn | 1211-1225 |
| 22 | Respawn rule: peer `hp >= 0.9` while `deadTails[tail]` and not dead → clear the dead flag | otherwise one wipe leaves enemies permanently dead on one side | 1914-1920 |
| 23 | Enemy hand-back after **2 s** of no publication, and `MIR_SKIP` for classes containing `NPC` | friendly NPCs must keep their own AI | 3951-3954, 1783-1786 |
| 24 | `StopLogic` on the enemy brain is **retried** every packet until the controller exists | a freshly spawned enemy gets its controller a moment later | 1819-1821 |
| 25 | Movement mode 0 on mirrored enemies; restore the *saved* mode on release | gravity/braking fight the written positions | 1790-1793, 1808 |
| 26 | AI retarget only when `dp < d && dp < 3000` and the current target is valid and ≠ the puppet | avoids stealing aggro from a closer enemy | 3881-3894 |
| 27 | `TP_HOLD`: hover (`SetMovementMode(5)`) for 6 s after a teleport, then restore mode 1 | without it a far teleport falls through unloaded ground and the game's fall recovery teleports the player back | 2093-2096, 3756-3762 |
| 28 | Streaming sources re-asserted every 10 s; **non-blocking only** | the blocking source stalls this client whenever the peer runs into unloaded cells | 3764-3776, 534 |
| 29 | Stream verdict needs the peer 30k+ away, not moving, for 25 s | measuring while it moves reads a half-loaded cell | 3781-3805 |
| 30 | Look caps: 24 parts, 16 materials, 400 bones | unbounded scans of a 164-bone mesh stall the tick | 992, 919, 295 |
| 31 | `actorsNear` counts **`StaticMeshActor` only** | `FindAllOf("Actor")` while cells stream froze the Lua thread | 3744-3751 |
| 32 | Boot timeouts: menu 300 s, gameplay 300 s, summon→adopt 20 s (then re-summon) | a cold start compiles shaders for 60 s+ | 570, 580, 604 |
| 33 | Adopt = name diff against a baseline snapshot of `FindAllOf("BP_PlayerCharacter_C")` | this is how `summon`'s new actor is found without spawning anything | 583-605 |
| 34 | Stale `out_<ME>.txt` and `peer_<PEER>.txt` deleted at boot | a leftover packet from a previous session would draw a body | 4130-4131 |
| 35 | `deadJSON` order is nondeterministic (`pairs`) and capped at 40 | the harness only counts entries; do not "fix" the order into something the harness depends on | 1365-1373 |
| 36 | `collectNPCs` `table.sort` tie-break is `(d<=R, min(d,dp))` and Lua's sort is **unstable** | `npc[1]` is the "nearest enemy" that `kill`/`hurt`/`aitest` act on; a different order changes which enemy the test hits | 1312-1315 |
| 37 | `section()` is a bracket-depth scan, not a parser | a `[`/`]` inside a string would truncate the section; never put one there | 1434-1451 |
| 38 | `"rdy":0` is a **substring** test | any line containing it (even inside `mseq`) hides the body | 1942 |
| 39 | Wire numbers use `%.1f/%.2f/%.3f/%.5f` and are compared as strings on the receiving side | different rounding = forever-dirty apply | 1013, 1346, 1599 |
| 40 | `LOOK.hash` (djb2) and `wsHash` (FNV-1a) are on the wire/status | a different hash breaks apply-diffing and the world-state key match | 863, 2262 |
| 41 | `WORLD_SETTLE = rt() + 1.5` after every data-layer switch | actors get freed during the switch; all actor work pauses | 3099, 4006-4013 |
| 42 | `LOCAL_HOLD = os.time() + 3` on a hard ready loss or own death; `HOLD_SKIP` pauses everything **except** `sendOwn` | `sendOwn` is what tells the peer to hide our body | 1551, 3923, 4000-4002 |
| 43 | One pending game-thread job per loop key (`PENDING`) | an unbounded queue of `ExecuteInGameThread` jobs is what killed clients | 4044-4053 |
| 44 | `weak` identity by name: `fullname()` is used as the map key everywhere | C++ pointers are stable, but the *keys* are names; keep names for the wire, pointers for calls | 117-139, 957 |
| 45 | `refreshChars` rebuilds the whole actor cache at 15 Hz; every other scan is 2 Hz or split | `FindAllOf` walks GObjects | 630-651 |

---

## 5. Scripts that must change (description only — do not edit here)

The DLL replaces `dwmapi.dll` (or another proxy DLL name) in
`<GameDir>\MortalShell2\Binaries\Win64\`, next to `MortalShell2-Win64-Shipping.exe`.

### 5.1 `ms2-coop\deploy.ps1` (42 lines) — must change

- Delete lines 16-23 (create `<w64>\Mods`, copy every `mod\*` dir into it) and 25-36
  (`mods.txt` with `CoopMod : 1`, `CoopLab : 0`, `CoopProbe : n`).
- Add: copy the built DLL (`native\build\<proxy>.dll`) to `$w64`, i.e.
  `Copy-Item (Join-Path $src 'native\build\ms2coop.dll') $w64 -Force`.
  Keep a `-Rollback`/backup of any pre-existing DLL of the same name (the UE4SS-era
  `dwmapi.dll` from Steam/Windows must not be clobbered blindly — record the original name and
  hash, as `dollsMP\mod\native\deploy.ps1` does with `.rollback` files).
- Keep lines 38-42 verbatim: `coop\` creation and `coop_dir.txt` (forward slashes, trailing `/`,
  `-NoNewline`). **This file is the DLL's only config input and `run-two.ps1` does not create it.**
- Rename the switches: `-Enable` (`mods.txt` enable) and `-Probe` (`CoopProbe`) are meaningless.
  Either drop them and update every caller, or keep them as no-ops so old invocations do not
  throw (`$ErrorActionPreference = 'Stop'` means an unknown parameter is a hard error, so
  `-Enable` must remain accepted while `run-two`/`lab`/`setup-install` still pass it).
- Callers to fix in the same change: `setup-install.ps1:34`
  (`& deploy.ps1 -GameDir $GameDir -Enable`), `HANDOFF.md` §2.2 commands.

### 5.2 `ms2-coop\lab.ps1` (203 lines) — must change

- Lines 34-39 rewrite `<w64>\Mods\mods.txt` (`CoopMod:0 CoopProbe:0 CoopLab:1`). `Get-Content`
  on a missing file throws → this script **fails immediately** once `Mods\` is gone. Replace with
  a native equivalent: e.g. write `coop\lab_on.txt` and have the DLL run its lab command set, or
  build a separate `ms2coop-lab.dll` from the same tree.
- Line 194 copies `<w64>\UE4SS.log` — gone. Replace with the DLL's log (recommended:
  keep `coop\probe.log` as the single log name so `verify-quiet.ps1` and `drive.ps1` keep working,
  plus an optional `coop\native.log` for loader diagnostics).
- Lines 144-149 wait for `coop\ready.txt`, which `CoopLab` wrote, not `CoopMod`. The native lab
  must create it (same name, same semantics: "in gameplay with a puppet").
- Lines 172-201 wait for `result_anim.txt` and print `lab.log`, `bones.txt`, `enemies.txt`,
  `hc.txt`, `prop.txt`. Those are `CoopLab` artefacts; either the native lab reproduces them or
  the script is retargeted. **Decide before deleting `CoopLab`** — it is the only engine
  experiment bench and the handoff recommends it for every new slice.

### 5.3 `ms2-coop\run-two.ps1` (416 lines) — must **not** change (except one risk)

This is the regression suite. It never touches `Mods\`, it only writes/reads the `coop\` dir, so it
keeps working as-is. Two things to watch:

- Line 125 deletes `result.txt`, `probe.log`, `cmd.txt`, `peer_*.txt`, `out_*.txt`, `relay_*.log`
  and lines 130-131 delete most `result_*.txt` — but **not** `result_anim.txt`,
  `result_ai.txt`, `result_npc.txt`. A stale `result_anim.txt` from a previous run can pass `ANIM`
  trivially. This is a pre-existing hole; the native port should not make it worse (write
  `result_anim.txt` only in the same conditions).
- It passes `-Enable` to nothing but `deploy.ps1` (called separately), so the rename in §5.1 must
  keep `-Enable` accepted.

### 5.4 `ms2-coop\setup-install.ps1` (44 lines) — must change

Lines 21-29 install UE4SS (`dwmapi.dll`, `ue4ss\*`, `UE4SS-settings.ini`,
`UE4SS_Signatures\*.lua`) guarded by `-SkipUe4ss`. Replace with a DLL copy guarded by a new
`-SkipNative` (keep `-SkipUe4ss` as an accepted no-op). Line 34's `-Enable` follows §5.1.
Lines 36-42 write `playerid.txt` into `Mods\CoopProbe\Scripts` — dead path; the DLL reads
`coop\playerid.txt` (`run-two.ps1:127,132` already writes it there), so drop or repoint.

### 5.5 `ms2-coop\package\Install.ps1` (98 lines) — must change

- Lines 25-58 (the whole `--- UE4SS ---` block: proxy copy, `UE4SS.dll`, `Mods`,
  `UE4SS_SDK_Backends`, `UE4SS-settings.ini`, `UE4SS_Signatures`) → copy the proxy DLL only.
- Lines 60-75 (`files\mods` → `<w64>\Mods`, `mods.txt` with `CoopMod : 1`) → delete.
- Keep lines 77-88 verbatim: `coop\` creation, `coop_dir.txt`, relay install into `coop\relay`.
  `MSCoop.ps1` looks for `coop\relay\cooprelay.py` — that path must not move.
- The uninstall note in the header comment (lines 3-4: "delete UE4SS.dll, dwmapi.dll,
  UE4SS-settings.ini, …") must be rewritten to name only the DLL and its `.rollback` backup.

### 5.6 `ms2-coop\package\build-package.ps1` (45 lines) — must change

- Line 12-14: `$ue4ssDist = ...\tools\ue4ss\dist` and the hard throw → replace with the native
  build output (`ms2-coop\native\build\*.dll`), and fail loudly if it is missing.
- Lines 20-23: stop staging `dwmapi.dll` + `ue4ss` + the `MortalShell2` config.
- Lines 26-27: stop copying `mod\` into `files\mods` and stop removing `mods\CoopProbe`.
- Keep lines 30-35 (relay, `Install.ps1`, `MSCoop.ps1`, `README.md`).

### 5.7 `ms2-coop\package\MSCoop.ps1` (103 lines) — **two latent bugs to fix**

Both are pre-existing (they exist in the current Lua mod too), and both will look like sync bugs
after the port, so fix them in the same change.

1. **Player id.** It writes `playerid.txt` as `HOST`/`GUEST` (lines 43-46) while the mod derives the
   peer as `(ME == "A") and "B" or "A"` (`main.lua:1421`). A packaged session therefore has
   `ME="HOST"` and `PEER="A"`: the guest writes `out_GUEST.txt` and the host reads
   `peer_A.txt`, which nobody writes. Either keep the A/B contract in the DLL *and* make
   `MSCoop.ps1` write `A`/`B` (mapping `-Host` → `A`, `-Join` → `B`), or make the DLL derive the
   peer from the relay's actual traffic (`out_*`/`peer_*` pairing) instead of from the id.
2. **Role.** Nothing in the package writes `role.txt`; only `run-two.ps1:128-129` does. Both
   packaged clients therefore fall back to `ROLE = "host"` (`main.lua:63-67`), which makes the
   guest publish `wd` (data layers, host-only) and take the host branch of the enemy-ownership
   bias in `collectNPCs` (`:1290-1299`) — i.e. both sides claim every enemy inside 4 000 uu and
   both push data-layer states. `MSCoop.ps1 -Host` must write `role.txt = host` and `-Join`
   `role.txt = guest` into the coop dir, next to `playerid.txt`.

### 5.8 Scripts that need **no** change

- `ms2-coop\drive.ps1` — writes `coop\cmd.txt`, reads `coop\result.txt`, `coop\probe.log`, and the
  legacy `coop\state.txt` (prints "(none)" when absent). Keep `probe.log` and `result.txt` names.
- `ms2-coop\verify-quiet.ps1` — reads `coop\probe.log` tail and counts `cmd.exe`. Keep `probe.log`.
- `ms2-coop\run-steam-smoke.ps1` — relay only.
- `ms2-coop\relay\*.py` — untouched.
- `docs\*.md` — must be *updated* (file map, "mod loader works: UE4SS" row in `DESIGN.md`, the
  §5.12 UTF-8-BOM fact which no longer applies, the §3 file map in `HANDOFF.md`) but no behaviour
  depends on them.

### 5.9 Do not forget in the DLL itself

- Export forwarding: a proxy DLL must export everything the real DLL exports, or the game fails to
  load. `dollsMP\mod\native\proxy.def` + `src\exports.h` + `src\fwd_symbols.c` are the proven
  pattern for that (same machine, same toolchain). Reuse them.
- Log flush per line (`probe.log` is tailed live by the harness).
- All paths relative to the *module* directory, not the CWD, with the three legacy probes as
  fallback (§2.1).

---

## 6. Porting order (suggested, dependency-safe)

| Step | Module | Why first / depends on |
|---|---|---|
| 0 | `coop_net` skeleton: DIR/ME/ROLE, log, file IO, `Tick` router, `Step`, `Now/Wall`, `FullName/Valid/Tail`, `ue::Init` | everything |
| 1 | `coop_local` + `coop_net` publish/apply of `rdy` + transform-only packet | proves the wire end-to-end with `run-two.ps1` `A`/`B` verdicts |
| 2 | `coop_puppet` boot/summon/adopt + drive (no anim) | needs 1; gives the `result.txt` verdict |
| 3 | `coop_puppet` anim (sequences + montage) | needs 2; `ANIM` verdict |
| 4 | `coop_appear` weapons, then look | needs 2/3; `WEAPON` then `LOOK` verdicts |
| 5 | `coop_combat` health/dead, then mirror/drive, then spawn replay/AI/PD | needs 1; `DAMAGE`, `DEATH`, `MIRROR`, `PEERHIT`, `AITARGET`, `SPAWN` |
| 6 | `coop_world` store A + ledger, then lifts, then chests | needs 1; `WORLD`, `LEDGER`, `LIFT`, `CHEST` |
| 7 | `coop_progress` store B, quests, layers | needs 6 (shared helpers); `PROGRESS`, `QUEST`, `QUEST2` |
| 8 | `coop_puppet` streaming + verdict | needs 2; `STREAM`, `NPCFAR` |

Acceptance for every step: `run-two.ps1` prints the same verdict set with `0 ERR lines`, and
`sync.txt` carries the same keys with the same meanings.
