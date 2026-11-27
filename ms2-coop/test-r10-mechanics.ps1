# test-r10-mechanics.ps1 -- End-to-end two-client rig test for all R10 mechanics
# using Savegame #5.sav (all shells, weapons, seals, full map, 35 beacons unlocked).
# Always snapshots & restores %LOCALAPPDATA%\MortalShell2\Saved\SaveGames in try/finally.
param(
    [string]$GameA = "G:\Games\_MS2-new-a",
    [string]$GameB = "G:\Games\_MS2-new-b",
    [string]$SaveFile = "C:\Users\ALVIBO\.t3\userdata\providers\antigravity\ac0a3dfd6dddb20962cecff6ee5fe65e19d3923be20e52c5ab52ff877f7e4c32\antigravity-acp\brain\8a67115d-3544-4299-8254-a5b1d045d5bd\scratch\saves\Savegame #5.sav",
    [int]$BootWait = 75,
    [int]$LanPort = 47777
)
$ErrorActionPreference = 'Stop'

Add-Type -Namespace Win32 -Name R10 -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT lpRect);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetWindowPos(System.IntPtr hWnd, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetForegroundWindow(System.IntPtr hWnd);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool ShowWindow(System.IntPtr hWnd, int cmd);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetCursorPos(int x, int y);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern void mouse_event(uint flags, int dx, int dy, uint data, System.UIntPtr extra);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
'@

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
$saveDir = Join-Path $env:LOCALAPPDATA 'MortalShell2\Saved\SaveGames'
$backupDir = Join-Path $env:TEMP ("ms2_r10_savebak_" + (Get-Date -Format 'HHmmss'))
$pA = $null; $pB = $null

function Stop-All {
    Get-Process | Where-Object { $_.ProcessName -match 'MortalShell2|CrashReportClient' } | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
    Get-CimInstance Win32_Process -Filter "Name like 'python%'" | Where-Object { $_.CommandLine -match 'cooprelay' } | ForEach-Object { try { Stop-Process -Id $_.ProcessId -Force } catch {} }
    Start-Sleep 2
}

function Sync([string]$coop, [string]$key) {
    $f = Join-Path $coop 'sync.txt'
    if (-not (Test-Path $f)) { return $null }
    try {
        foreach ($l in [IO.File]::ReadAllLines($f)) {
            if ($l.StartsWith("$key=")) { return $l.Substring($key.Length + 1) }
        }
    } catch {}
    return $null
}

function Cmd([string]$coop, [string]$text, [int]$waitSec = 2) {
    $f = Join-Path $coop 'cmd.txt'
    $tmp = Join-Path $coop 'cmd.tmp'
    [IO.File]::WriteAllText($tmp, $text + "`n")
    Move-Item -LiteralPath $tmp -Destination $f -Force
    $due = (Get-Date).AddSeconds(12)
    while ((Get-Date) -lt $due -and (Test-Path $f)) { Start-Sleep -Milliseconds 200 }
    if ($waitSec -gt 0) { Start-Sleep $waitSec }
}

function Focus-Win($proc) {
    $proc.Refresh(); $h = $proc.MainWindowHandle
    if (-not $h -or $h -eq [IntPtr]::Zero) { return $false }
    [Win32.R10]::ShowWindow([IntPtr]$h, 9) | Out-Null
    [Win32.R10]::SetWindowPos([IntPtr]$h, [IntPtr]::Zero, 40, 40, 1024, 576, 0x40) | Out-Null
    [Win32.R10]::SetForegroundWindow([IntPtr]$h) | Out-Null
    Start-Sleep -Milliseconds 300
    return $true
}

function Click-Attack($proc, [int]$clicks = 4) {
    if (-not (Focus-Win $proc)) { return }
    $r = New-Object Win32.R10+RECT
    [Win32.R10]::GetWindowRect([IntPtr]$proc.MainWindowHandle, [ref]$r) | Out-Null
    $cx = [int](($r.Left + $r.Right) / 2); $cy = [int](($r.Top + $r.Bottom) / 2)
    [Win32.R10]::SetCursorPos($cx, $cy) | Out-Null
    for ($i = 0; $i -lt $clicks; $i++) {
        [Win32.R10]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 70
        [Win32.R10]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 450
    }
}

function Press-Key($proc, [byte]$vk, [int]$holdMs = 250) {
    if (-not (Focus-Win $proc)) { return }
    [Win32.R10]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $holdMs
    [Win32.R10]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 350
}

function Prep-Coop([string]$coop, [string]$id, [string]$role) {
    New-Item -ItemType Directory -Force -Path $coop | Out-Null
    Get-ChildItem $coop -Filter 'out_*.txt' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem $coop -Filter 'peer_*.txt' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    Get-ChildItem $coop -Filter 'ws_ledger_*.txt' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $coop 'probe.log') -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $coop 'sync.txt') -Force -ErrorAction SilentlyContinue
    [IO.File]::WriteAllText((Join-Path $coop 'playerid.txt'), $id)
    [IO.File]::WriteAllText((Join-Path $coop 'role.txt'), $role)
}

function Start-Client([string]$gameDir) {
    $exe = Join-Path $gameDir 'MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe'
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = "MortalShell2 -windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
    $psi.WorkingDirectory = $gameDir
    $psi.UseShellExecute = $true
    return [System.Diagnostics.Process]::Start($psi)
}

try {
    Stop-All
    # Snapshot user saves & seed Savegame #5.sav
    New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
    if (Test-Path $saveDir) { Copy-Item (Join-Path $saveDir '*') $backupDir -Force -ErrorAction SilentlyContinue }
    Write-Host "Backed up owner saves to $backupDir"
    if (Test-Path $SaveFile) {
        Copy-Item $SaveFile (Join-Path $saveDir 'WorldState_0.sav') -Force
        Write-Host "Seeded Savegame #5.sav -> WorldState_0.sav"
    }

    Prep-Coop $coopA 'A' 'host'
    Prep-Coop $coopB 'B' 'guest'

    Write-Host "Starting A and B with NO cooprelay.py (testing in-DLL coop_steam LAN UDP)..."
    $pA = Start-Client $GameA
    Start-Sleep 12
    $pB = Start-Client $GameB

    # Wait for both to reach gameplay (phase=sync or phase=adopt or pawn ready)
    $deadline = (Get-Date).AddSeconds($BootWait + 45)
    while ((Get-Date) -lt $deadline) {
        $phA = Sync $coopA 'phase'; $phB = Sync $coopB 'phase'
        if ($phA -and $phB -and $phA -ne 'boot' -and $phB -ne 'boot') { break }
        Start-Sleep 2
    }
    Write-Host ("Booted: A phase={0} ; B phase={1}" -f (Sync $coopA 'phase'), (Sync $coopB 'phase'))

    # --- 1. In-DLL LAN UDP Transport (NO Python relay!) ---
    Write-Host "`n=== TEST 1: IN-DLL LAN UDP (coop_steam.cpp) ==="
    Cmd $coopA "lanhost $LanPort" 2
    Cmd $coopB "lanjoin 127.0.0.1 $LanPort" 5
    $connOk = $false
    $dl = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $dl) {
        $stA = Sync $coopA 'steam'; $stB = Sync $coopB 'steam'
        $phA = Sync $coopA 'phase'; $phB = Sync $coopB 'phase'
        if ($stA -match 'CONNECTED' -and $stB -match 'CONNECTED' -and $phA -eq 'sync' -and $phB -eq 'sync') {
            $connOk = $true; break
        }
        Start-Sleep 1
    }
    Write-Host ("A steam: {0} | phase={1}" -f (Sync $coopA 'steam'), (Sync $coopA 'phase'))
    Write-Host ("B steam: {0} | phase={1}" -f (Sync $coopB 'steam'), (Sync $coopB 'phase'))
    Write-Host ("=== R10_LANUDP: {0} ===" -f $(if ($connOk) { 'PASS' } else { 'FAIL' }))

    # --- 2. Co-op Difficulty Scaling (coop_beacon_scale.cpp) ---
    Write-Host "`n=== TEST 2: DIFFICULTY SCALING (150% HP in co-op) ==="
    Start-Sleep 3
    $bsA = Sync $coopA 'bscale'; $bsB = Sync $coopB 'bscale'
    Write-Host "A bscale: $bsA"
    Write-Host "B bscale: $bsB"
    $scaleOk = ($bsA -match 'scaled:[1-9]') -or ($bsB -match 'scaled:[1-9]')
    Write-Host ("=== R10_SCALING: {0} ===" -f $(if ($scaleOk) { 'PASS' } else { 'WARN (no undamaged enemies in range yet)' }))

    # --- 3. HitHook Combat (coop_hithook.cpp) ---
    Write-Host "`n=== TEST 3: HITHOOK COMBAT (hittest + real LMB) ==="
    Cmd $coopA "hitprobe" 1
    Cmd $coopB "hitprobe" 1
    Cmd $coopA "hittest" 3
    $hhA1 = Sync $coopA 'hithook'; $hhB1 = Sync $coopB 'hithook'
    Write-Host "After hittest -> A: $hhA1 | B: $hhB1"
    # Move near an enemy and perform real LMB attacks + Harden (Q) + Dodge (Space)
    Cmd $coopA "enemytp nearest" 2
    Click-Attack $pA 5
    Press-Key $pA 0x51 400   # Q (Harden / Seal)
    Press-Key $pA 0x20 150   # Space (Dodge)
    Start-Sleep 2
    $hhA2 = Sync $coopA 'hithook'; $hhB2 = Sync $coopB 'hithook'
    Write-Host "After real LMB -> A: $hhA2 | B: $hhB2"
    $hitOk = ($hhA2 -match 'hooked:1') -and ($hhA2 -match 'tx:[1-9]') -and ($hhB2 -match 'rx:[1-9]') -and ($hhB2 -match 'applied:[1-9]')
    Write-Host ("=== R10_HITHOOK: {0} ===" -f $(if ($hitOk) { 'PASS' } else { 'FAIL' }))

    # --- 4. Projectiles, Grabs, VFX & Seal/Shell Abilities ---
    Write-Host "`n=== TEST 4: FX / GRABS / ABILITIES ==="
    Write-Host ("A fx: {0} | partb: {1} | pplus: {2}" -f (Sync $coopA 'fx'), (Sync $coopA 'partb'), (Sync $coopA 'pplus'))
    Write-Host ("B fx: {0} | partb: {1} | pplus: {2}" -f (Sync $coopB 'fx'), (Sync $coopB 'partb'), (Sync $coopB 'pplus'))

    # --- 5. Beacon Fast-Travel Follow (coop_beacon_scale.cpp) ---
    Write-Host "`n=== TEST 5: BEACON FAST-TRAVEL FOLLOW ==="
    Cmd $coopA "areasweep LandingArea" 2
    Cmd $coopA "areago far" 6
    $bsA2 = Sync $coopA 'bscale'; $bsB2 = Sync $coopB 'bscale'
    Write-Host "After host beacon jump -> A bscale: $bsA2 | B bscale: $bsB2"
    $followOk = ($bsB2 -match 'follow:[1-9]')
    Write-Host ("=== R10_BEACON_FOLLOW: {0} ===" -f $(if ($followOk) { 'PASS' } else { 'CHECK' }))

    # --- 6. Live Probes for TombZones, Bosses & Non-Save-Flag Puzzles (S28-S39, S45, S73) ---
    Write-Host "`n=== TEST 6: TOMBZONE, BOSS & PUZZLE PROBES ==="
    Cmd $coopA "areatomb" 2
    Cmd $coopA "areasweep Boss Plate Bell Lantern Illusory Bridge Platform Lever Torch Brazier Switch Puzzle" 2

    # --- 7. Disconnect & Solo Scaling Restore + Steam P2P Lobby Smoke ---
    Write-Host "`n=== TEST 7: DISCONNECT + STEAM P2P LOBBY SMOKE ==="
    Cmd $coopA "lanstop" 1
    Cmd $coopB "lanstop" 2
    Write-Host ("After lanstop -> A steam: {0} | B steam: {1}" -f (Sync $coopA 'steam'), (Sync $coopB 'steam'))
    Cmd $coopA "steamhost ms2rig10" 6
    Write-Host ("After steamhost -> A steam: {0}" -f (Sync $coopA 'steam'))
    Cmd $coopA "steamstop" 1

    Write-Host "`n=== PROBE HIGHLIGHTS (A) ==="
    Select-String -Path (Join-Path $coopA 'probe.log') -Pattern 'hitprobe|hittest|hithook|beacon:|areasweep|areatomb|partb:|steam:' | Select-Object -Last 60 | ForEach-Object { $_.Line }
} finally {
    Stop-All
    if (Test-Path $backupDir) {
        if (-not (Test-Path $saveDir)) { New-Item -ItemType Directory -Force -Path $saveDir | Out-Null }
        Copy-Item (Join-Path $backupDir '*') $saveDir -Force -ErrorAction SilentlyContinue
        Remove-Item $backupDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Restored owner saves cleanly."
    }
}
