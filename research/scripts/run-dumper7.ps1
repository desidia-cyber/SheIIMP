<#
  run-dumper7.ps1 - take one Dumper-7 SDK dump in the test install.

  Modes
    menu       main menu, no all-assets load; inject as soon as the lab reports
               the menu is ready  (fast sanity run: proves injection + engine
               detection + output layout)
    allassets  main menu with LoadAllAssetsBeforeDumpingObjects=1; the lab runs
               UE4SS `dumpobj` first (which loads every package) and Dumper-7 is
               injected right after DumpAllObjects() returns, i.e. with every
               asset loaded -- the same state the UE4SS CXX dump was taken in
               (research\00-sources.md section 1).  The 538 MB object dump this
               produces is deleted again; only the SDK is kept.

  Dumper-7 is a bare injected DLL: no launcher. This script
    * snapshots/restores the owner saves exactly like run-dump.ps1,
    * copies Dumper-7.dll + a Dumper-7.ini into <Win64> before launch and removes
      them afterwards,
    * starts ms2-coop\lab.ps1 with HOLD_MENU.txt (so the game stays on the menu),
    * injects with scripts\inject-dll.ps1 once the menu / the all-assets load is
      ready,
    * waits for the SDK directory to stop growing, kills the game, copies the SDK
      to research\raw\Dumper7 (allassets) or research\runs\<stamp>-dumper7-menu
      (menu), and restores the ini + saves.

  Usage:  .\run-dumper7.ps1 -Mode menu
          .\run-dumper7.ps1 -Mode allassets
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('menu', 'gameplay', 'allassets')][string]$Mode,
    [string]$GameDir = 'E:\Games\_MS2-coop-test',
    [string]$DllPath = 'G:\Freespace\shell_coop\tools\dumper7\Dumper-7.dll',
    [int]$BootWait = 150,
    [int]$QuietSeconds = 120,
    [int]$HardLimitMinutes = 60,
    [string]$Tag = ''
)

$ErrorActionPreference = 'Stop'
$repo = 'G:\Freespace\shell_coop'
$research = Join-Path $repo 'research'
$w64 = Join-Path $GameDir 'MortalShell2\Binaries\Win64'
$coop = Join-Path $w64 'coop'
$ini = Join-Path $w64 'UE4SS-settings.ini'
$dumpFile = Join-Path $w64 'UE4SS_ObjectDump.txt'
$d7Dll = Join-Path $w64 'Dumper-7.dll'
$d7Ini = Join-Path $w64 'Dumper-7.ini'
$d7Global = 'C:\Dumper-7'

if (-not (Test-Path $DllPath)) { throw "Dumper-7.dll not found: $DllPath" }
if (-not $Tag) { $Tag = $Mode }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outDir = Join-Path $research "runs\$stamp-dumper7-$Tag"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
New-Item -ItemType Directory -Force -Path $coop | Out-Null

# Dumper-7 output staging (absolute path, per run, inside the test install)
$sdkRoot = Join-Path $GameDir "Dumper7-out\$stamp"
New-Item -ItemType Directory -Force -Path $sdkRoot | Out-Null
$d7Log = Join-Path $sdkRoot 'dumper7.log'

function Log([string]$m) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Host $line
    Add-Content -Path (Join-Path $outDir 'run.log') -Value $line
}

# ---------------------------------------------------------------- saves
$sg = "$env:LOCALAPPDATA\MortalShell2\Saved\SaveGames"
$saveBak = Join-Path $repo ("local\save-backups\" + $stamp + "-dumper7-" + $Tag)
New-Item -ItemType Directory -Force -Path $saveBak | Out-Null
Copy-Item "$sg\*" $saveBak -Force -Recurse
$before = Get-ChildItem $sg -File | Get-FileHash -Algorithm SHA256 |
    Sort-Object Path | ForEach-Object { "{0}  {1}" -f $_.Hash, (Split-Path $_.Path -Leaf) }
$before | Set-Content (Join-Path $outDir 'saves-before.txt') -Encoding ASCII
Log "saves snapshotted to $saveBak ($($before.Count) files)"

# ---------------------------------------------------------------- UE4SS ini (all-assets flag)
$iniOriginal = Get-Content $ini -Raw
Copy-Item $ini (Join-Path $outDir 'UE4SS-settings.ini.before') -Force
function Set-IniValue([string]$key, [string]$value) {
    $txt = Get-Content $ini -Raw
    $pattern = "(?m)^(\s*$key\s*=\s*).*$"
    if ($txt -match $pattern) { $txt = [regex]::Replace($txt, $pattern, ('${1}' + $value)) }
    else { throw "ini key not found: $key" }
    [System.IO.File]::WriteAllText($ini, $txt, (New-Object System.Text.UTF8Encoding($false)))
}
$objLoad = '0'
if ($Mode -eq 'allassets') { $objLoad = '1' }
Set-IniValue 'LoadAllAssetsBeforeDumpingObjects' $objLoad
Set-IniValue 'LoadAllAssetsBeforeGeneratingCXXHeaders' '0'
Log "UE4SS ini: LoadAllAssetsBeforeDumpingObjects=$objLoad LoadAllAssetsBeforeGeneratingCXXHeaders=0"

# ---------------------------------------------------------------- Dumper-7 config + dll
Copy-Item $DllPath $d7Dll -Force
Copy-Item $DllPath (Join-Path $outDir 'Dumper-7.dll') -Force
# ExecuteSDKTestScript=0: Dumper-7's built-in test compiles the SDK twice (MSVC
# and ClangCL). This machine has no ClangCL component, so the test blocks in a
# console ("Could not select Visual Studio toolchains ... for ClangCL", then
# "Press Enter to close...") after the SDK has already been written. The compile
# check is done separately with scripts\d7_sdk_build_check.py (MSVC only).
# See research\05-dumper7-vs-ue4ss.md.
$iniText = @"
[Settings]
SleepTimeout=2
ShouldAutoUnload=1
SDKGenerationPath=$sdkRoot
LogFilePath=$d7Log
ExecuteSDKTestScript=0
"@
[System.IO.File]::WriteAllText($d7Ini, $iniText, (New-Object System.Text.UTF8Encoding($false)))
Copy-Item $d7Ini (Join-Path $outDir 'Dumper-7.ini') -Force
$dllHash = (Get-FileHash $d7Dll -Algorithm SHA256).Hash
Log "Dumper-7.dll staged in Win64, SHA256 $dllHash"
Log "Dumper-7 SDK target: $sdkRoot"

# ---------------------------------------------------------------- hold flag / stale files
$holdFile = Join-Path $coop 'HOLD_MENU.txt'
if ($Mode -eq 'gameplay') { Remove-Item $holdFile -Force -ErrorAction SilentlyContinue } else { Set-Content -Path $holdFile -Value '1' -Encoding ASCII }
foreach ($f in @('dump_done_obj.txt', 'dump_done_cxx.txt', 'dump_done_sm.txt', 'menu_ready.txt', 'ready.txt', 'cmd.txt', 'lab.log')) {
    Remove-Item (Join-Path $coop $f) -Force -ErrorAction SilentlyContinue
}
Remove-Item $dumpFile -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $d7Global 'CppSDK') -Recurse -Force -ErrorAction SilentlyContinue

# ---------------------------------------------------------------- launch lab.ps1
# menu mode: one harmless command keeps the lab alive while we inject
# allassets: dumpobj loads every package, then blocks until the dump returned
$cmd = if ($Mode -eq 'allassets') { 'dumpobj@5400' } else { 'info@1800' }
$labArgs = @{
    Commands = $cmd
    GameDir  = $GameDir
    BootWait = $BootWait
    StepWait = 5400
    Tag      = "dumper7-$Tag"
}
$job = Start-Job -ScriptBlock {
    param($script, $a)
    & $script @a *>&1
} -ArgumentList (Join-Path $repo 'ms2-coop\lab.ps1'), $labArgs
Log "lab.ps1 started as job $($job.Id) (command '$cmd')"

# ---------------------------------------------------------------- wait until injection point
$injectAfter = switch ($Mode) { 'allassets' { Join-Path $coop 'dump_done_obj.txt' } 'gameplay' { Join-Path $coop 'ready.txt' } default { Join-Path $coop 'menu_ready.txt' } }
$deadline = (Get-Date).AddMinutes($HardLimitMinutes)
$injected = $false
$gamePid = 0
while (-not $injected -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 10
    $proc = @(Get-Process -Name 'MortalShell2-Win64-Shipping' -ErrorAction SilentlyContinue)
    if ($proc.Count -eq 0) {
        $proc = @(Get-Process | Where-Object { $_.ProcessName -like 'MortalShell2*' })
    }
    if ($proc.Count -eq 0) {
        Start-Sleep -Seconds 20
        $proc = @(Get-Process | Where-Object { $_.ProcessName -like 'MortalShell2*' })
        if ($proc.Count -eq 0) { Log "game process not found yet, still waiting"; continue }
    }
    $gamePid = ($proc | Sort-Object StartTime -Descending | Select-Object -First 1).Id
    if (Test-Path $injectAfter) {
        Log "injection point reached: $(Split-Path $injectAfter -Leaf) (pid $gamePid)"
        if ($Mode -eq 'gameplay') { Log 'gameplay: letting the level settle for 20s before injecting'; Start-Sleep -Seconds 20 }
        Start-Sleep -Seconds 5
        & (Join-Path $PSScriptRoot 'inject-dll.ps1') -ProcessId $gamePid -DllPath $d7Dll 2>&1 |
            ForEach-Object { Log ("inject: " + $_) }
        $injected = $true
    } else {
        Log "waiting for $(Split-Path $injectAfter -Leaf) (pid $gamePid)"
    }
}
if (-not $injected) { Log "ERROR: never reached the injection point" }

# ---------------------------------------------------------------- wait for the SDK to stabilise
function Get-DirSize([string]$p) {
    if (-not (Test-Path $p)) { return -1 }
    return (Get-ChildItem $p -Recurse -File -ErrorAction SilentlyContinue | Measure-Object -Property Length -Sum).Sum
}
$lastSize = -1; $lastChange = Get-Date; $sawSdk = $false
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 15
    $size = Get-DirSize $sdkRoot
    $gameRunning = @(Get-Process | Where-Object ProcessName -match 'MortalShell2').Count -gt 0
    if ($size -gt 0 -and -not $sawSdk) {
        $sawSdk = $true
        Log "SDK output appeared: $sdkRoot"
    }
    if ($size -ne $lastSize) { $lastSize = $size; $lastChange = Get-Date; Log ("SDK size now {0:N0} bytes" -f $size) }
    if (Test-Path $d7Log) {
        $tail = (Get-Content $d7Log -Tail 3 -ErrorAction SilentlyContinue) -join ' | '
        Add-Content -Path (Join-Path $outDir 'dumper7-tail.log') -Value ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $tail)
    }
    if (-not $gameRunning) { Log "game process gone"; break }
    if ($sawSdk -and $lastSize -gt 0) {
        $autoUnloaded = @(Get-Process | Where-Object ProcessName -match 'MortalShell2') | Out-Null
        $dllLoaded = $false
        try { $dllLoaded = @((Get-Process -Id $gamePid -ErrorAction Stop).Modules | Where-Object ModuleName -eq 'Dumper-7.dll').Count -gt 0 } catch {}
        if ((-not $dllLoaded) -and ((Get-Date) - $lastChange).TotalSeconds -ge 30) {
            Log "Dumper-7.dll unloaded from the game and the SDK stopped growing -> done"
            break
        }
    }
    if ($sawSdk -and ((Get-Date) - $lastChange).TotalSeconds -ge $QuietSeconds) {
        Log ("SDK stable for ${QuietSeconds}s -> killing the game")
        break
    }
}

Get-Process | Where-Object ProcessName -match 'MortalShell2|CrashReportClient' | ForEach-Object {
    try { Stop-Process -Id $_.Id -Force; Log "killed $($_.ProcessName) ($($_.Id))" } catch {}
}

$jobOut = Receive-Job $job -Keep 2>&1 | Out-String
$jobOut | Set-Content (Join-Path $outDir 'lab-runner.out.txt') -Encoding UTF8
Stop-Job $job -ErrorAction SilentlyContinue
Remove-Job $job -Force -ErrorAction SilentlyContinue
Log "lab.ps1 job stopped"

# ---------------------------------------------------------------- collect
if (Test-Path $sdkRoot) {
    $files = Get-ChildItem $sdkRoot -Recurse -File -ErrorAction SilentlyContinue
    Log ("SDK staging holds {0:N0} files, {1:N0} bytes" -f $files.Count, ($files | Measure-Object Length -Sum).Sum)
    if ($Mode -ne 'menu') {
        $destName = 'raw\Dumper7_gameplay'
        if ($Mode -eq 'allassets') { $destName = 'raw\Dumper7_allassets' }
        $dest = Join-Path $research $destName
        if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item "$sdkRoot\*" $dest -Recurse -Force
        Log "copied SDK to $dest"
    } else {
        Copy-Item $sdkRoot $outDir -Recurse -Force
        Log "copied SDK into $outDir"
    }
    Copy-Item $d7Log (Join-Path $outDir 'dumper7.log') -Force -ErrorAction SilentlyContinue
} else {
    Log "WARN: no SDK staging directory was created"
}
Copy-Item (Join-Path $coop '*') $outDir -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $w64 'UE4SS.log') (Join-Path $outDir 'UE4SS.log') -Force -ErrorAction SilentlyContinue
if (Test-Path $d7Log) { Copy-Item $d7Log (Join-Path $outDir 'dumper7.log') -Force }

# ---------------------------------------------------------------- restore ini, dll, holds, saves
[System.IO.File]::WriteAllText($ini, $iniOriginal, (New-Object System.Text.UTF8Encoding($false)))
Remove-Item $holdFile -Force -ErrorAction SilentlyContinue
Remove-Item $d7Dll -Force -ErrorAction SilentlyContinue
Remove-Item $d7Ini -Force -ErrorAction SilentlyContinue
Remove-Item $dumpFile -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $d7Global 'CppSDK') -Recurse -Force -ErrorAction SilentlyContinue
Log "UE4SS ini restored, Dumper-7.dll/ini removed, object dump deleted"

$after = Get-ChildItem $sg -File | Get-FileHash -Algorithm SHA256 |
    Sort-Object Path | ForEach-Object { "{0}  {1}" -f $_.Hash, (Split-Path $_.Path -Leaf) }
$after | Set-Content (Join-Path $outDir 'saves-after.txt') -Encoding ASCII
$diff = Compare-Object $before $after
if ($diff) {
    Log "SAVES CHANGED after the run -> restoring from $saveBak"
    Copy-Item "$saveBak\*" $sg -Force -Recurse
    $recheck = Get-ChildItem $sg -File | Get-FileHash -Algorithm SHA256 |
        Sort-Object Path | ForEach-Object { "{0}  {1}" -f $_.Hash, (Split-Path $_.Path -Leaf) }
    $still = Compare-Object $before $recheck
    if ($still) { Log "STILL DIFFERENT: $($still | Out-String)" } else { Log "saves restored to the pre-run state" }
} else {
    Log "saves unchanged"
}
Get-Process | Where-Object ProcessName -match 'MortalShell2|CrashReportClient|cooprelay' | ForEach-Object {
    try { Stop-Process -Id $_.Id -Force; Log "killed leftover $($_.ProcessName)" } catch {}
}
Log "DONE - artifacts in $outDir"

