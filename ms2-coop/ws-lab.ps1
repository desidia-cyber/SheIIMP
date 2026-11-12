# [R6-WORLD] two clients, the WORLD lane only: A runs `wstest` (open/flip a world key +
# pick up the nearest uncollected pickup) and B's `ws judge` is watched until it writes
# result_ws.txt.  Short enough to iterate on the mod instead of a full run-two.
#   .\ws-lab.ps1 [-Tag ws] [-Secs 25] [-Runs 1]
# Holds the run lock itself and restores the shared save folder.
param(
    [string]$GameA = "E:\Games\_MS2-coop-c",
    [string]$GameB = "E:\Games\_MS2-coop-d",
    [string]$Port = "47020",
    [int]$BootWait = 200,
    [int]$Secs = 25,
    [int]$PreWait = 40,
    [string]$Lane = 'ws',
    [string]$Tag = "ws",
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)
$ErrorActionPreference = 'Stop'
. 'G:\Freespace\shell_coop\local\gamelock.ps1'
Lock-Game 'r6spawn'
try {
$root = $PSScriptRoot
$realLocal = $env:LOCALAPPDATA
$outDir = Join-Path 'G:\Freespace\shell_coop\local\lab' ((Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Tag")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace Win32 -Name W -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetWindowPos(System.IntPtr hWnd, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
'@

function Stop-All {
    Get-Process | ? ProcessName -match 'MortalShell2|CrashReportClient' | % { try { Stop-Process -Id $_.Id -Force } catch {} }
    Get-CimInstance Win32_Process -Filter "Name like 'python%'" | ? { $_.CommandLine -match 'cooprelay' } | % { try { Stop-Process -Id $_.ProcessId -Force } catch {} }
}
function Hide-All {
    Get-Process | ? ProcessName -match 'MortalShell2-Win64' | % {
        if ($_.MainWindowHandle -ne 0) { [Win32.W]::SetWindowPos($_.MainWindowHandle, [IntPtr]::Zero, -30000, -30000, 0, 0, 0x15) | Out-Null } }
}
function Start-Client([string]$gameDir, [string]$localAppData) {
    $exe = Join-Path $gameDir 'MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe'
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe; $psi.Arguments = "MortalShell2 $ExtraArgs"; $psi.WorkingDirectory = $gameDir
    $psi.UseShellExecute = $true
    $env:LOCALAPPDATA = $localAppData; $p = [System.Diagnostics.Process]::Start($psi); $env:LOCALAPPDATA = $realLocal
    return $p
}
function Sync([string]$coop, [string]$key) {
    $f = Join-Path $coop 'sync.txt'; if (-not (Test-Path $f)) { return $null }
    foreach ($l in Get-Content $f) { if ($l -like "$key=*") { return $l.Substring($key.Length + 1) } }
    return $null
}
function Cmd([string]$coop, [string]$line) { [IO.File]::WriteAllText((Join-Path $coop 'cmd.txt'), "$line`n") }
function Copy-Logs([string]$id, [string]$coop, [string]$install) {
    Copy-Item (Join-Path $install 'MortalShell2\Binaries\Win64\ms2coop-native.log') (Join-Path $outDir "native_$id.log") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coop 'sync.txt') (Join-Path $outDir "sync_$id.txt") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coop 'result_ws.txt') (Join-Path $outDir "result_ws_$id.txt") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coop 'result_ws2.txt') (Join-Path $outDir "result_ws2_$id.txt") -Force -ErrorAction SilentlyContinue
}

Stop-All; Start-Sleep 3
$realSaveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
$snap = Join-Path $env:TEMP ("ms2saves_ws_" + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $snap | Out-Null
Copy-Item "$realSaveDir\*.sav" $snap -Force
$before = @(Get-ChildItem $realSaveDir -Filter *.sav | % Name)

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
foreach ($d in @($coopA, $coopB)) {
    Remove-Item (Join-Path $d 'result_ws.txt'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'sync.txt'), (Join-Path $d 'out_*.txt') -Force -ErrorAction SilentlyContinue
}
Set-Content (Join-Path $coopA 'playerid.txt') 'A' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopA 'role.txt') 'host' -Encoding ASCII -NoNewline
Set-Content (Join-Path $coopB 'playerid.txt') 'B' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopB 'role.txt') 'guest' -Encoding ASCII -NoNewline
"ledger before: " + ((Get-ChildItem (Join-Path $coopA 'ws_ledger_*.txt') -ErrorAction SilentlyContinue | ForEach-Object { "$($_.Name)=$($_.Length)B" }) -join ', ')

$pa = $null; $pb = $null
try {
    $pa = Start-Client $GameA (Join-Path $root 'local-appdata\A'); Start-Sleep 8
    $pb = Start-Client $GameB (Join-Path $root 'local-appdata\B')
    $relay = Join-Path $root 'relay\cooprelay.py'
    Start-Process python -ArgumentList @($relay, '--transport', 'udp', '--role', 'host', '--port', "$Port", '--player', 'A', '--dir', $coopA) -WindowStyle Hidden
    Start-Sleep 2
    Start-Process python -ArgumentList @($relay, '--transport', 'udp', '--role', 'client', '--host', '127.0.0.1', '--port', "$Port", '--player', 'B', '--dir', $coopB) -WindowStyle Hidden
    $until = (Get-Date).AddSeconds($BootWait)
    while ((Get-Date) -lt $until) {
        Hide-All; Start-Sleep -Milliseconds 700
        if ((Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') { break }
    }
    Write-Host "phase A=$(Sync $coopA 'phase') B=$(Sync $coopB 'phase')"
    # the store-A rows only count after kWsSettle (8 s) AND a full 14-tick scan pass
    # (~7 s), so wstest has nothing eligible if it fires right after sync
    Start-Sleep $PreWait
    if ($Lane -eq 'ws2') {
        Cmd $coopA 'wa2selftest'; Start-Sleep 2
        Cmd $coopA 'wa2census';   Start-Sleep 3
        Cmd $coopA 'wa2test'
        $wsFile = Join-Path $coopB 'result_ws2.txt'
    } else {
        Cmd $coopA 'wstest'
        $wsFile = Join-Path $coopB 'result_ws.txt'
    }
    $deadline = (Get-Date).AddSeconds($Secs)
    while (-not (Test-Path $wsFile) -and (Get-Date) -lt $deadline) { Hide-All; Start-Sleep -Milliseconds 700 }
    $verdict = if (Test-Path $wsFile) { (Get-Content $wsFile -Raw).Trim() } else { '(none)' }
    Write-Host "=== WORLD: $verdict"
    Write-Host "    B ws: $(Sync $coopB 'ws')"
    Write-Host "    B ws2: $(Sync $coopB 'ws2')"
    Copy-Logs 'A' $coopA $GameA
    Copy-Logs 'B' $coopB $GameB
    "ledger after: " + ((Get-ChildItem (Join-Path $coopA 'ws_ledger_*.txt') -ErrorAction SilentlyContinue | ForEach-Object { "$($_.Name)=$($_.Length)B" }) -join ', ')
}
finally {
    if ($pa -or $pb) { try { Copy-Logs 'A' $coopA $GameA; Copy-Logs 'B' $coopB $GameB } catch {} }
    Stop-All; Start-Sleep 2
    Get-ChildItem $realSaveDir -Filter *.sav | ? { $before -notcontains $_.Name } | Remove-Item -Force
    Copy-Item "$snap\*.sav" $realSaveDir -Force
    Write-Host "saves restored; out=$outDir"
}
}
finally { Unlock-Game 'r6spawn' }
