<#
  run-dump.ps1 - take one UE4SS dump run in the test install and park the result.

  Modes
    obj     main menu, LoadAllAssetsBeforeDumpingObjects=1, DumpAllObjects()
    cxx     main menu, LoadAllAssetsBeforeGeneratingCXXHeaders=1, GenerateSDK()
    ingame  gameplay (Continue), LoadAllAssets*=0, DumpAllObjects()

  Everything goes through ms2-coop\lab.ps1 (see HANDOFF 2.3) with the CoopLab
  command channel.  For "obj"/"cxx" a HOLD_MENU.txt file stops the lab from
  pressing Continue (see the CoopLab header comment).

  Saves: a full copy of %LOCALAPPDATA%\MortalShell2\Saved\SaveGames is taken to
  research\save-backups\<stamp>\ before the run and compared byte-for-byte after.

  Usage:  .\run-dump.ps1 -Mode obj
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('obj', 'cxx', 'ingame')][string]$Mode,
    [string]$GameDir = 'E:\Games\_MS2-coop-test',
    [int]$BootWait = 130,
    [int]$QuietSeconds = 90,
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
$cxxDir = Join-Path $w64 'CXXHeaderDump'

if (-not $Tag) { $Tag = $Mode }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outDir = Join-Path $research "runs\$stamp-$Tag"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
New-Item -ItemType Directory -Force -Path $coop | Out-Null

function Log([string]$m) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Host $line
    Add-Content -Path (Join-Path $outDir 'run.log') -Value $line
}

# ---------------------------------------------------------------- saves
$sg = "$env:LOCALAPPDATA\MortalShell2\Saved\SaveGames"
$saveBak = Join-Path $repo ("local\save-backups\" + $stamp + "-rundump-" + $Tag)
New-Item -ItemType Directory -Force -Path $saveBak | Out-Null
Copy-Item "$sg\*" $saveBak -Force -Recurse
$before = Get-ChildItem $sg -File | Get-FileHash -Algorithm SHA256 |
    Sort-Object Path | ForEach-Object { "{0}  {1}" -f $_.Hash, (Split-Path $_.Path -Leaf) }
$before | Set-Content (Join-Path $outDir 'saves-before.txt') -Encoding ASCII
Log "saves snapshotted to $saveBak"

# ---------------------------------------------------------------- ini
$iniOriginal = Get-Content $ini -Raw
Copy-Item $ini (Join-Path $outDir 'UE4SS-settings.ini.before') -Force
function Set-IniValue([string]$key, [string]$value) {
    $txt = Get-Content $ini -Raw
    $pattern = "(?m)^(\s*$key\s*=\s*).*$"
    if ($txt -match $pattern) { $txt = [regex]::Replace($txt, $pattern, ('${1}' + $value)) }
    else { throw "ini key not found: $key" }
    [System.IO.File]::WriteAllText($ini, $txt, (New-Object System.Text.UTF8Encoding($false)))
}
$objLoad = '0'; $cxxLoad = '0'; $hold = $false
switch ($Mode) {
    'obj' { $objLoad = '1'; $hold = $true }
    'cxx' { $cxxLoad = '1'; $hold = $true }
    'ingame' { }
}
Set-IniValue 'LoadAllAssetsBeforeDumpingObjects' $objLoad
Set-IniValue 'LoadAllAssetsBeforeGeneratingCXXHeaders' $cxxLoad
Log "ini: LoadAllAssetsBeforeDumpingObjects=$objLoad LoadAllAssetsBeforeGeneratingCXXHeaders=$cxxLoad"

# ---------------------------------------------------------------- hold flag / stale files
$holdFile = Join-Path $coop 'HOLD_MENU.txt'
if ($hold) { Set-Content -Path $holdFile -Value '1' -Encoding ASCII } else { Remove-Item $holdFile -Force -ErrorAction SilentlyContinue }
foreach ($f in @('dump_done_obj.txt', 'dump_done_cxx.txt', 'dump_done_sm.txt', 'menu_ready.txt', 'ready.txt', 'cmd.txt', 'lab.log')) {
    Remove-Item (Join-Path $coop $f) -Force -ErrorAction SilentlyContinue
}
if ($Mode -ne 'ingame') { Remove-Item $dumpFile -Force -ErrorAction SilentlyContinue }
Remove-Item $cxxDir -Recurse -Force -ErrorAction SilentlyContinue

# ---------------------------------------------------------------- launch lab.ps1 in the background
$cmd = 'dumpobj'
if ($Mode -eq 'cxx') { $cmd = 'dumpcxx' }
$labArgs = @{
    Commands  = "$cmd@3600"
    GameDir   = $GameDir
    BootWait  = $BootWait
    StepWait  = 3600
    Tag       = "dump-$Tag"
}
$job = Start-Job -ScriptBlock {
    param($script, $a)
    & $script @a *>&1
} -ArgumentList (Join-Path $repo 'ms2-coop\lab.ps1'), $labArgs
Log "lab.ps1 started as job $($job.Id) (command '$cmd@3600')"

# ---------------------------------------------------------------- wait for the dump to stabilise
$target = if ($Mode -eq 'cxx') { $cxxDir } else { $dumpFile }
$deadline = (Get-Date).AddMinutes($HardLimitMinutes)
$lastSize = -1; $lastChange = Get-Date; $sawTarget = $false
function Get-TargetSize {
    param($p)
    if (-not (Test-Path $p)) { return -1 }
    $item = Get-Item $p
    if ($item.PSIsContainer) {
        return (Get-ChildItem $p -Recurse -File | Measure-Object -Property Length -Sum).Sum
    }
    return $item.Length
}
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 15
    $size = Get-TargetSize $target
    if ($size -ge 0) {
        if (-not $sawTarget) { $sawTarget = $true; Log "target appeared: $target" }
        if ($size -ne $lastSize) {
            $lastSize = $size; $lastChange = Get-Date
            Log ("target size now {0:N0} bytes" -f $size)
        }
    }
    $gameRunning = @(Get-Process | Where-Object ProcessName -match 'MortalShell2-Win64').Count -gt 0
    if (-not $gameRunning) { Log "game process gone"; break }
    if ($sawTarget -and ((Get-Date) - $lastChange).TotalSeconds -ge $QuietSeconds) {
        Log ("target stable for ${QuietSeconds}s -> killing the game")
        break
    }
    # the lab writes a done marker when the call returns; use it as a second signal
    $markerName = 'dump_done_obj.txt'
    if ($Mode -eq 'cxx') { $markerName = 'dump_done_cxx.txt' }
    $doneMarker = Join-Path $coop $markerName
    if (Test-Path $doneMarker) {
        $sinceDone = ((Get-Date) - (Get-Item $doneMarker).LastWriteTime).TotalSeconds
        if ($sinceDone -ge 20) { Log "done marker + 20s -> killing the game"; break }
    }
}

Get-Process | Where-Object ProcessName -match 'MortalShell2|CrashReportClient' | ForEach-Object {
    try { Stop-Process -Id $_.Id -Force; Log "killed $($_.ProcessName) ($($_.Id))" } catch {}
}

# lab.ps1 is sitting inside a very long step wait (the dump call blocks the lab
# thread anyway), so stop it here; collection and the save restore are done below.
$jobOut = Receive-Job $job -Keep 2>&1 | Out-String
$jobOut | Set-Content (Join-Path $outDir 'lab-runner.out.txt') -Encoding UTF8
Stop-Job $job -ErrorAction SilentlyContinue
Remove-Job $job -Force -ErrorAction SilentlyContinue
Log "lab.ps1 job stopped after the game exited"

# ---------------------------------------------------------------- collect
if ($Mode -eq 'cxx') {
    if (Test-Path $cxxDir) {
        $dest = Join-Path $research 'raw\CXXHeaderDump'
        if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
        Copy-Item $cxxDir $dest -Recurse -Force
        Log ("copied CXXHeaderDump ({0:N0} files) to {1}" -f (Get-ChildItem $dest -Recurse -File).Count, $dest)
    } else { Log "WARN: no CXXHeaderDump folder" }
} else {
    if (Test-Path $dumpFile) {
        $dest = if ($Mode -eq 'ingame') { Join-Path $research 'raw\ObjectDump_ingame.txt' } else { Join-Path $research 'raw\ObjectDump_allassets.txt' }
        Copy-Item $dumpFile $dest -Force
        Log ("copied dump ({0:N0} bytes) to {1}" -f (Get-Item $dest).Length, $dest)
        # keep a per-run copy too, so a later overwrite cannot destroy the evidence
        Copy-Item $dumpFile (Join-Path $outDir (Split-Path $dest -Leaf)) -Force
    } else { Log "WARN: no dump file" }
}
Copy-Item (Join-Path $coop '*') $outDir -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $w64 'UE4SS.log') (Join-Path $outDir 'UE4SS.log') -Force -ErrorAction SilentlyContinue

# ---------------------------------------------------------------- restore ini + holds
[System.IO.File]::WriteAllText($ini, $iniOriginal, (New-Object System.Text.UTF8Encoding($false)))
Remove-Item $holdFile -Force -ErrorAction SilentlyContinue
Log "ini restored"

# ---------------------------------------------------------------- save check
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
