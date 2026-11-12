@echo off
REM ---------------------------------------------------------------------------
REM Build dxgi.dll -- the Mortal Shell II native co-op core (no UE4SS).
REM
REM A proxy DLL plus MinHook, linked into one file.  MinHook builds statically for
REM x64 (upstream defaults BUILD_SHARED_LIBS to OFF), so its four sources are
REM compiled straight in; there is no separate library to ship.
REM
REM The exported surface is ENTIRELY forwarders, generated from the real
REM System32\dxgi.dll into proxy.def by tools\gen_proxy_def.py.  That is why the
REM linker is given /DEF and nothing is marked __declspec(dllexport): a proxy that
REM also exports its own symbols changes what other modules see when they resolve
REM the name.
REM
REM /MT is not a style choice.  This file is loaded as dxgi.dll, which d3d11,
REM d3d12, the DLSS runtime and the FSR runtime all import, so it must be
REM self-contained: with /MD the module would need ucrtbase/vcruntime loaded
REM during the same import resolution that is loading us.
REM
REM NOTE ON STYLE: this file uses `goto` rather than parenthesised if-blocks.
REM `%ProgramFiles(x86)%` contains a ')' which closes such a block early -- that
REM bug silently skips the environment setup and then reports "vswhere not found".
REM ---------------------------------------------------------------------------
setlocal

set HERE=%~dp0
set SRC=%HERE%src
set OUT=%HERE%build
set MH=%HERE%thirdparty\minhook

REM Expanded at top level on purpose -- see the note above.
set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%SRC%\proxy.cpp"    goto :no_src
if not exist "%SRC%\ms2_ue.cpp"   goto :no_src
if not exist "%SRC%\exports.h"    goto :no_def
if not exist "%SRC%\fwd_symbols.c" goto :no_def
if not exist "%HERE%proxy.def"    goto :no_def
if not exist "%MH%\src\hook.c"    goto :no_minhook

REM Bring in the MSVC environment unless we are already inside one.
if defined VSCMD_ARG_TGT_ARCH goto :have_msvc
if not exist "%VSWHERE%" goto :no_vswhere
if not exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" goto :use_vswhere
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :no_vcvars
goto :have_msvc

:use_vswhere
set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :no_vstools
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto :no_vstools
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :no_vcvars

:have_msvc
if not exist "%OUT%" mkdir "%OUT%"

REM ---------------------------------------------------------------------------
REM Every C/C++ file in src\ is part of the DLL: proxy.cpp (DllMain + forwarder
REM check), ms2_ue.cpp (the reflection core), ms2_log.cpp, coop_math.cpp and the
REM eight coop_<module>.cpp.  They are listed by GLOB into a response file rather
REM than by name, so a new module is picked up by copying it into src\ -- nothing
REM to remember and no module can be silently left out of the link.
REM
REM tests\*.cpp are NOT in src\, so they can never leak into the DLL.
REM ---------------------------------------------------------------------------
set "SRCLIST=%OUT%\sources.rsp"
if exist "%SRCLIST%" del "%SRCLIST%"
for %%F in ("%SRC%\*.cpp") do >>"%SRCLIST%" echo "%%~fF"
for %%F in ("%SRC%\*.c")   do >>"%SRCLIST%" echo "%%~fF"
if not exist "%SRCLIST%" goto :no_src
echo Sources:
for /f "usebackq delims=" %%L in ("%SRCLIST%") do echo   %%~L

echo Building dxgi.dll (proxy + UE5.6 reflection core + co-op modules) ...
cl /nologo /LD /O2 /EHsc /W3 /MT /GS /std:c++20 /permissive- ^
   /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
   "@%SRCLIST%" ^
   "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde64.c" ^
   /I"%HERE%." /I"%SRC%" /I"%MH%\include" /I"%MH%\src" ^
   /Fe:"%OUT%\dxgi.dll" /Fo:"%OUT%\\" /Fd:"%OUT%\dxgi.pdb" ^
   /link /DEF:"%HERE%proxy.def" /INCREMENTAL:NO /MAP:"%OUT%\dxgi.map"
if errorlevel 1 goto :build_failed

REM A zero-byte DLL is what a failed link leaves behind and Test-Path cannot tell
REM it apart from success, so check the size.
for %%F in ("%OUT%\dxgi.dll") do if %%~zF LSS 16384 goto :build_too_small

REM Prove the export table is what this file says it is: every export a forwarder,
REM none of our own symbols leaked into it.  If this fails the DLL still works on
REM this machine but is not a faithful proxy.
echo.
echo Verifying the export table ...
call :check_exports
if errorlevel 1 goto :exports_bad

REM ---------------------------------------------------------------------------
REM THE AOB SELF-CHECK, built and run on every build when a game exe is findable.
REM
REM It runs the DLL's OWN scanner over the real MortalShell2-Win64-Shipping.exe and
REM asserts that each pattern matches exactly once and resolves to the address that
REM was recovered independently.  This is the check that caught a hand-wildcarded
REM AppendString pattern matching 27 places -- a bug that would have been invisible
REM in the game (names would simply have been wrong) and cost hours in a session.
REM
REM Point MS2_EXE at any MortalShell2-Win64-Shipping.exe to check a different build.
REM ---------------------------------------------------------------------------
if /i not "%1"=="test" goto :tests_skipped

echo.
echo Building and running the API contract test ...
cl /nologo /EHsc /W3 /O2 /MT /std:c++20 /I"%HERE%." /I"%SRC%" ^
   "%HERE%tests\test_api.cpp" "%SRC%\ms2_ue.cpp" "%SRC%\ms2_log.cpp" ^
   "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde64.c" ^
   /I"%MH%\include" /I"%MH%\src" /Fe:"%OUT%\test_api.exe" /Fo:"%OUT%\\" >"%OUT%\test_api.build.txt" 2>&1
if errorlevel 1 goto :api_failed
"%OUT%\test_api.exe"
if errorlevel 1 goto :api_failed

REM [R5-DEATH] begin -- the pure co-op death policy has its own truth table.
REM It is engine-free (coop_policy.h only), so it builds and runs without the game.
echo.
echo Building and running the co-op death policy test ...
cl /nologo /EHsc /W3 /O2 /MT /std:c++20 /I"%SRC%" ^
   "%HERE%tests\test_coop_policy.cpp" /Fe:"%OUT%\test_coop_policy.exe" /Fo:"%OUT%\\" >"%OUT%\test_coop_policy.build.txt" 2>&1
if errorlevel 1 goto :policy_nocompile
"%OUT%\test_coop_policy.exe"
if errorlevel 1 goto :policy_failed
REM [R5-DEATH] end

echo.
echo Building and running the AOB self-check ...
cl /nologo /EHsc /W3 /O2 /MT /std:c++20 /I"%SRC%" ^
   "%HERE%tests\test_scan.cpp" "%SRC%\ms2_ue.cpp" "%SRC%\ms2_log.cpp" ^
   "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde64.c" ^
   /I"%MH%\include" /I"%MH%\src" /Fe:"%OUT%\test_scan.exe" /Fo:"%OUT%\\" >"%OUT%\test_scan.build.txt" 2>&1
if errorlevel 1 goto :scan_nocompile
if not defined MS2_EXE set "MS2_EXE=E:\Games\_MS2-coop-test\MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe"
if not exist "%MS2_EXE%" goto :scan_noexe
"%OUT%\test_scan.exe" "%MS2_EXE%"
if errorlevel 1 goto :scan_failed
echo.
echo Building the load test (proves the module survives a host with no UE code) ...
cl /nologo /EHsc /W3 /O2 /MT "%HERE%tests\test_load.cpp" /Fe:"%OUT%\test_load.exe" /Fo:"%OUT%\\" >nul
if errorlevel 1 goto :load_nocompile
echo   built -- run "%OUT%\test_load.exe" from %OUT% to exercise it (takes ~25 s)

:tests_skipped

echo.
echo Built: %OUT%\dxgi.dll
dir /b "%OUT%\dxgi.dll"
endlocal
exit /b 0

:scan_failed
echo BUILD FAILED: the AOB self-check did not pass.
echo   The patterns in %SRC%\ms2_ue.cpp do not resolve to the recovered addresses
echo   on "%MS2_EXE%".  Do NOT ship this DLL: it would load and then silently fail
echo   to find the engine globals.
exit /b 1

:api_failed
echo BUILD FAILED: tests\test_api.cpp does not compile against src\ms2_ue.h.
echo   That means the header's SIGNATURES changed -- the co-op lanes are written
echo   against them, so this must be fixed before anything else. Compiler output:
type "%OUT%\test_api.build.txt"
exit /b 1

REM [R5-DEATH] begin
:policy_nocompile
echo BUILD FAILED: tests\test_coop_policy.cpp does not compile against src\coop_policy.h.
type "%OUT%\test_coop_policy.build.txt"
exit /b 1

:policy_failed
echo BUILD FAILED: the co-op death policy's truth table is broken.
echo   A failing check above is a rule that no longer matches the owner's three-case
echo   rule.  Do NOT ship this DLL.  Re-run: "%OUT%\test_coop_policy.exe"
exit /b 1
REM [R5-DEATH] end

:scan_noexe
echo NOTE: no game exe at "%MS2_EXE%" -- skipped the AOB self-check.
echo       Set MS2_EXE to a MortalShell2-Win64-Shipping.exe to run it.
goto :tests_skipped

:scan_nocompile
echo WARNING: tests\test_scan.cpp did not compile, so the AOB self-check was NOT run.
echo   The DLL above is built and is not affected. Compiler output:
type "%OUT%\test_scan.build.txt"
goto :tests_skipped

:load_nocompile
echo WARNING: tests\test_load.cpp did not compile.
goto :tests_skipped

REM ---------------------------------------------------------------------------
:check_exports
if not exist "%HERE%tools\pe_export_table.py" goto :exports_skip
python "%HERE%tools\pe_export_table.py" "%OUT%\dxgi.dll" >"%OUT%\exports.txt" 2>&1
if errorlevel 1 goto :exports_skip
findstr /C:"forwarders:" "%OUT%\exports.txt" >nul
if errorlevel 1 goto :exports_skip
type "%OUT%\exports.txt"
echo (full table in %OUT%\exports.txt)
exit /b 0

:exports_skip
echo NOTE: tools\pe_export_table.py not present or python unavailable -- skipped the
echo       export-table check.  The DLL is built; verify with dumpbin /exports.
exit /b 0

REM ---------------------------------------------------------------------------
:exports_bad
echo BUILD FAILED: the export table is not the expected set of forwarders.
echo   Send %OUT%\exports.txt.  A proxy with a missing export breaks whichever
echo   module resolves that name next, and the failure surfaces elsewhere.
exit /b 1

:no_src
echo ERROR: %SRC% is missing proxy.cpp / ms2_ue.cpp / ms2_log.cpp.
exit /b 1

:no_def
echo ERROR: proxy.def, src\exports.h or src\fwd_symbols.c is missing.
echo        regenerate them with:
echo          python tools\gen_proxy_def.py --dll C:\Windows\System32\dxgi.dll ^
echo              --library dxgi --out proxy.def --header src\exports.h ^
echo              --cstubs src\fwd_symbols.c
exit /b 1

:no_minhook
echo ERROR: MinHook not vendored at %MH%
echo        copy it from G:\Freespace\dollsMP\tools\thirdparty\minhook
exit /b 1

:no_vswhere
echo ERROR: vswhere not found at "%VSWHERE%"
exit /b 1

:no_vstools
echo ERROR: no Visual Studio with the C++ toolset was found.
exit /b 1

:no_vcvars
echo ERROR: vcvars64.bat failed.
exit /b 1

:build_too_small
echo BUILD FAILED: %OUT%\dxgi.dll is smaller than 16 KB, which is what a failed
echo   link leaves behind.  Treating it as failure rather than shipping a stub.
exit /b 1

:build_failed
echo BUILD FAILED
exit /b 1
