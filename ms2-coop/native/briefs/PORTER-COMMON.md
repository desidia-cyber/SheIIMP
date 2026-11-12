# Common brief for every porting agent (read fully)

Goal: Mortal Shell II (UE 5.6) co-op mod is being rewritten from a UE4SS Lua mod
(G:\Freespace\shell_coop\ms2-coop\mod\CoopMod\Scripts\main.lua, READ-ONLY reference) into a native C++
proxy DLL with NO UE4SS. Approach mirrors G:\Freespace\dollsMP\mod\native\src (a working UE4.27 example
of the same idea; useful for patterns: reflection calls, polling, SEH guards).

Read first: G:\Freespace\shell_coop\ms2-coop\docs\NATIVE-PORT.md (the porting spec — your module's section,
the wire contract, the fragile-behaviours list) and G:\Freespace\shell_coop\ms2-coop\native\coop_state.h
(the shared contract between modules — implement YOUR namespace's functions exactly as declared there;
read other modules' state only through what the header exposes).

Engine-access layer (written in parallel by another agent in native\src\ms2_ue.h; if it already exists,
read it and conform to it exactly; if not, code against this API and the integrator will reconcile):
namespace ue {
  bool Init(); std::string GetName(UObject*); std::string GetFullName(UObject*); std::string GetClassName(UObject*);
  UObject* FindObject(const char* pathOrName); UClass* FindClass(const char* shortName);
  UObject* FindFirstOf(const char* cls); std::vector<UObject*> FindAllOf(const char* cls);
  bool IsA(UObject*, UClass*); bool IsValid(UObject*); UObject* GetOuter(UObject*); UClass* GetClass(UObject*);
  FProperty* FindProperty(UStruct*, const char*); int32_t PropOffset(UObject*, const char* prop);
  template<class T> bool Get(UObject*, const char* prop, T& out); template<class T> bool Set(UObject*, const char* prop, const T&);
  UFunction* FindFunction(UObject*, const char* name); bool Call(UObject*, const char* fn, void* params);
  struct Params { Params(UFunction*); template<class T> void Set(const char*, T); template<class T> T Get(const char*);
                  void SetRaw(const char*, const void*, size_t); void GetRaw(const char*, void*, size_t); void* data(); };
  using HookFn = void(*)(UObject* self, UFunction* fn, void* params);
  void HookPre(const char* fnFullName, HookFn); void HookPost(const char* fnFullName, HookFn);
  void EnqueueGameThread(std::function<void()>); void OnTick(std::function<void(float dt)>);  // game thread
  // additions (gaps from NATIVE-PORT.md) — use these names:
  struct FName { int32_t ComparisonIndex; int32_t Number; };  FName MakeFName(const char*); std::string ToString(FName);
  template<class T> struct TArray { T* Data; int32_t Num; int32_t Max; };   // read via Get<TArray<T>>
  struct FString { wchar_t* Data; int32_t Num; int32_t Max; }; std::string ToUtf8(const FString&);
  std::string TextToString(void* ftext);              // via KismetTextLibrary.Conv_TextToString
  bool MapForEach(UObject*, const char* prop, std::function<bool(void* key, void* value)>);  // TMap/TSet iteration, read-only
  UObject* GetCDO(UClass*);  UObject* GetWorld(); UObject* GetPlayerController(); UObject* GetPawn();
  bool ConsoleCommand(const char* cmd);               // via KismetSystemLibrary.ExecuteConsoleCommand
  FVector/FRotator/FQuat/FTransform are double-precision UE5 layouts.
}
Anything the engine exposes as a UFunction (AddComponentByClass, K2_SetActorLocation, Kismet libraries,
LoadAsset_Blocking, ...) is called through ue::Call / ue::Params on the object or the library CDO.

Rules:
- Write ONLY your own file(s): native\src\coop_<module>.cpp (and coop_<module>_*.h if needed). Do not edit
  coop_state.h, ms2_ue.h or other modules' files. If the contract is missing something you need, put it in a
  clearly marked "// CONTRACT-REQUEST:" block at the top of your .cpp and in your final report.
- Faithful port: same behaviour, rates, thresholds, log lines (exact text that run-two.ps1/lab.ps1 grep for),
  same wire format. Do not "improve" behaviour. Keep the fragile-behaviour notes from NATIVE-PORT.md.
- Crash safety: never dereference engine pointers without ue::IsValid; this mod exists to stop crashes.
  Log via the logging function declared in coop_state.h (or `coop::Log(const char* fmt, ...)` if none).
- C++17, MSVC. Compile-check your file: if native\src\ms2_ue.h exists, run
  `"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && cl /c /std:c++17 /EHsc /W3 /I native /I native\src native\src\coop_<module>.cpp`
  from G:\Freespace\shell_coop\ms2-coop and fix errors in your file. If it doesn't exist yet, create a private
  stub native\src\_stub_ue_<module>.h with the API above to compile against (delete it when done or leave it
  clearly named as a stub), and say so.
- Work fast. Final report: files written, compile result, every Lua function you ported (name -> C++ fn),
  anything not ported and why, CONTRACT-REQUESTs, and any uncertainty about engine behaviour.
