# [R6-SPAWN] Two clients, ONE spawner: the guest teleports onto a proximity spawner
# (what run-two's SPAWN lane does) and both sides dump the same spawner's gate
# properties (BP_Spawner.hpp) every few seconds -- so "the trigger ran but no enemy
# appeared" turns into the field that kept it from appearing.
#
#   .\spawn-lab.ps1 [-Tag spawn] [-Secs 60] [-SpawnPoint "66992 -125076 -44454"] [-Phase2]
#
# Phase 1: B (guest) tp's onto the spawner; A (host) replays from its own position
#          (~20k away) -- the run-two situation.
# Phase 2 (-Phase2): A tp's ONTO the same spawner (real overlap, A is the player who
#          walked in), and phase 3 tp's A away again -- the three states that
#          separate "the game destroys enemies no player is near" from "the replay
#          path is wrong".
# Holds the run lock itself (gamelock.ps1) and restores the shared save folder.
param(
    [string]$GameA = "E:\Games\_MS2-coop-c",
    [string]$GameB = "E:\Games\_MS2-coop-d",
    [string]$Port = "47020",
    [string]$SpawnPoint = "66992 -125076 -44454",
    [int]$BootWait = 200,
    [int]$Secs = 40,
    [string]$Tag = "spawn",
    [switch]$Phase2,
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

Add-Type -Namespace Win32 -Name V -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetWindowPos(System.IntPtr hWnd, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PrintWindow(System.IntPtr hWnd, System.IntPtr hdcBlt, uint nFlags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT lpRect);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
'@
Add-Type -AssemblyName System.Drawing

function Stop-All {
    Get-Process | ? ProcessName -match 'MortalShell2|CrashReportClient' | % { try { Stop-Process -Id $_.Id -Force } catch {} }
    Get-CimInstance Win32_Process -Filter "Name like 'python%'" | ? { $_.CommandLine -match 'cooprelay' } | % { try { Stop-Process -Id $_.ProcessId -Force } catch {} }
}
function Hide-All {
    Get-Process | ? ProcessName -match 'MortalShell2-Win64' | % {
        if ($_.MainWindowHandle -ne 0) { [Win32.V]::SetWindowPos($_.MainWindowHandle, [IntPtr]::Zero, -30000, -30000, 0, 0, 0x15) | Out-Null } }
}
function Shot([System.Diagnostics.Process]$proc, [string]$path) {
    $proc.Refresh(); $h = $proc.MainWindowHandle
    if ($h -eq 0) { Write-Host "shot: no window"; return }
    $r = New-Object Win32.V+RECT; [Win32.V]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -le 0 -or $ht -le 0) { Write-Host "shot: empty rect"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [Win32.V]::PrintWindow($h, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc); $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Write-Host "shot: $path"
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
function Pose([string]$coop, [string]$id) {
    $f = Join-Path $coop "out_$id.txt"; if (-not (Test-Path $f)) { return $null }
    $t = Get-Content $f -Raw
    if ($t -match '"x":(-?[\d.]+),"y":(-?[\d.]+),"z":(-?[\d.]+)') {
        return "{0} {1} {2}" -f $matches[1], $matches[2], $matches[3] }
    return $null
}
function Cmd([string]$coop, [string]$line) { [IO.File]::WriteAllText((Join-Path $coop 'cmd.txt'), "$line`n") }
function Copy-Logs([string]$id, [string]$coop, [string]$install) {
    Copy-Item (Join-Path $install 'MortalShell2\Binaries\Win64\ms2coop-native.log') (Join-Path $outDir "native_$id.log") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coop 'probe.log') (Join-Path $outDir "probe_$id.log") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coop 'sync.txt') (Join-Path $outDir "sync_$id.txt") -Force -ErrorAction SilentlyContinue
}
function Watch([string]$coopA, [string]$coopB, [int]$secs, [int]$step, [string]$when) {
    $t0 = Get-Date
    for ($i = 1; $i -le [math]::Ceiling($secs / $step); $i++) {
        Start-Sleep $step; Hide-All
        Cmd $coopA 'spawndump claim'
        if ($i % 2 -eq 0) { Cmd $coopB 'spawndump spawned' }
        # a death respawn teleports a player, and that would explain a spawner
        # losing its player -- print where both players are, every step
        Write-Host ("[{0}] t+{1}s A@{2} B@{3} A {4}" -f $when, [int]((Get-Date) - $t0).TotalSeconds,
            (Pose $coopA 'A'), (Pose $coopB 'B'), (Sync $coopA 'enemy'))
    }
}

Stop-All; Start-Sleep 3
$realSaveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
$snap = Join-Path $env:TEMP ("ms2saves_sp_" + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $snap | Out-Null
Copy-Item "$realSaveDir\*.sav" $snap -Force
$before = @(Get-ChildItem $realSaveDir -Filter *.sav | % Name)

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
foreach ($d in @($coopA, $coopB)) {
    Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'probe.log'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'peer_*.txt'), (Join-Path $d 'out_*.txt'), (Join-Path $d 'sync.txt'), (Join-Path $d 'heartbeat.txt') -Force -ErrorAction SilentlyContinue
}
Set-Content (Join-Path $coopA 'playerid.txt') 'A' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopA 'role.txt') 'host' -Encoding ASCII -NoNewline
Set-Content (Join-Path $coopB 'playerid.txt') 'B' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopB 'role.txt') 'guest' -Encoding ASCII -NoNewline

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
    Start-Sleep 8
    $aHome = Pose $coopA 'A'
    Write-Host "A home: $aHome"
    Cmd $coopA 'spawndump nearest'; Start-Sleep 3; Cmd $coopB 'spawndump nearest'; Start-Sleep 3

    # ---- phase 1: B on the spawner, A 20k away (the run-two situation) ----
    Cmd $coopB "tp $SpawnPoint"
    Start-Sleep 1
    Cmd $coopA 'spawnprobe 45'      # which of the spawner's BP steps run on the host
    Start-Sleep 3
    Watch $coopA $coopB $Secs 6 'far'

    # ---- phase 2: the destroy veto (see SpawnDestroyHook) ----
    # The replayed enemy is destroyed at FULL health ~1 s after it spawns, and only
    # on the client with no player there.  If that destroy goes through the game's
    # own Blueprint-callable Actor.K2_DestroyActor, a ProcessEvent skip hook stops it
    # and the copy survives; the log says either way.
    if ($Phase2) {
        Cmd $coopA 'spawnveto 1'
        Start-Sleep 2
        Cmd $coopA 'spawnfire claim'
        Start-Sleep 3
        Cmd $coopA 'spawnfire claim'
        Start-Sleep 2
        Watch $coopA $coopB 24 6 'veto'

        # ---- phase 3: A near the spawner -- the floor/streaming state a real
        # player standing there would produce (this is the control) ----
        $near = ($SpawnPoint -split ' ') | ForEach-Object { [double]$_ }
        $nearPt = "{0:F0} {1:F0} {2:F0}" -f ($near[0] + 1200), ($near[1] + 1200), ($near[2] + 120)
        Cmd $coopA "tp $nearPt"
        Start-Sleep 2
        Watch $coopA $coopB 24 6 'near'
        Shot $pa (Join-Path $outDir 'A-near-1.png')

        # ---- phase 4: A leaves again -- does the enemy survive its player leaving? ----
        if ($aHome) { Cmd $coopA "tp $aHome" }
        Start-Sleep 3
        Watch $coopA $coopB 18 6 'away'
        Shot $pa (Join-Path $outDir 'A-away-1.png')
    }

    Shot $pa (Join-Path $outDir 'A-final-1.png')
    Shot $pb (Join-Path $outDir 'B-final-1.png')
    Copy-Logs 'A' $coopA $GameA
    Copy-Logs 'B' $coopB $GameB
    Write-Host "alive=$(@(Get-Process | ? ProcessName -match 'MortalShell2-Win64').Count)"
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
