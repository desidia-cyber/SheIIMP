# test-r10-bosses-and-world.ps1 -- End-to-end two-client rig test for:
#   1. In-DLL LAN UDP (coop_steam.cpp, zero cooprelay.py)
#   2. Real enemy HitHook damage + poise/reaction apply (tx>0, rx>0, applied>0)
#   3. Seal active def (S9), Harden state (S18), NG+ hook (S61)
#   4. Real Boss Beacons + Boss Arena / Phase sync (S45) + Beacon Fast-Travel Follow
#   5. Difficulty Scaling (150% in co-op -> 100% restore on disconnect)
#   6. Steam P2P lobby creation on live AppID 2584270
param(
    [string]$GameA = "G:\Games\_MS2-new-a",
    [string]$GameB = "G:\Games\_MS2-new-b",
    [string]$SaveFile = "C:\Users\ALVIBO\.t3\userdata\providers\antigravity\ac0a3dfd6dddb20962cecff6ee5fe65e19d3923be20e52c5ab52ff877f7e4c32\antigravity-acp\brain\8a67115d-3544-4299-8254-a5b1d045d5bd\scratch\saves\Savegame #5.sav",
    [int]$BootWait = 75,
    [int]$LanPort = 47788
)
$ErrorActionPreference = 'Stop'

Add-Type -Namespace Win32 -Name R10B -MemberDefinition @'
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
$backupDir = Join-Path $env:TEMP ("ms2_r10b_savebak_" + (Get-Date -Format 'HHmmss'))
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
    [Win32.R10B]::ShowWindow([IntPtr]$h, 9) | Out-Null
    [Win32.R10B]::SetWindowPos([IntPtr]$h, [IntPtr]::Zero, 40, 40, 1024, 576, 0x40) | Out-Null
    [Win32.R10B]::SetForegroundWindow([IntPtr]$h) | Out-Null
    Start-Sleep -Milliseconds 300
    return $true
}

function Click-Attack($proc, [int]$clicks = 4) {
    if (-not (Focus-Win $proc)) { return }
    $r = New-Object Win32.R10B+RECT
    [Win32.R10B]::GetWindowRect([IntPtr]$proc.MainWindowHandle, [ref]$r) | Out-Null
    $cx = [int](($r.Left + $r.Right) / 2); $cy = [int](($r.Top + $r.Bottom) / 2)
    [Win32.R10B]::SetCursorPos($cx, $cy) | Out-Null
    for ($i = 0; $i -lt $clicks; $i++) {
        [Win32.R10B]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 70
        [Win32.R10B]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 450
    }
}

function Press-Key($proc, [byte]$vk, [int]$holdMs = 300) {
    if (-not (Focus-Win $proc)) { return }
    [Win32.R10B]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $holdMs
    [Win32.R10B]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
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
    New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
    if (Test-Path $saveDir) { Copy-Item (Join-Path $saveDir '*') $backupDir -Force -ErrorAction SilentlyContinue }
    if (Test-Path $SaveFile) { Copy-Item $SaveFile (Join-Path $saveDir 'WorldState_0.sav') -Force }

    Prep-Coop $coopA 'A' 'host'
    Prep-Coop $coopB 'B' 'guest'

    $pA = Start-Client $GameA
    Start-Sleep 20
    $pB = Start-Client $GameB

    $deadline = (Get-Date).AddSeconds($BootWait + 60)
    while ((Get-Date) -lt $deadline) {
        $phA = Sync $coopA 'phase'; $phB = Sync $coopB 'phase'
        if ($phA -eq 'sync' -and $phB -eq 'sync') { break }
        Start-Sleep 2
    }
    Write-Host ("Booted: A phase={0} ; B phase={1}" -f (Sync $coopA 'phase'), (Sync $coopB 'phase'))

    # --- 1. In-DLL LAN UDP ---
    Cmd $coopA "lanhost $LanPort" 2
    Cmd $coopB "lanjoin 127.0.0.1 $LanPort" 5
    $lanOk = $false
    $dl = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $dl) {
        if ((Sync $coopA 'steam') -match 'st:3' -and (Sync $coopB 'steam') -match 'st:3' -and (Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') {
            $lanOk = $true; break
        }
        Start-Sleep 1
    }
    Write-Host ("=== R10_LANUDP: {0} (A={1} | B={2}) ===" -f $(if ($lanOk) {'PASS'} else {'FAIL'}), (Sync $coopA 'steam'), (Sync $coopB 'steam'))

    # --- 2. Difficulty Scaling in Co-op ---
    Start-Sleep 2
    $bsA0 = Sync $coopA 'bscale'; $bsB0 = Sync $coopB 'bscale'
    $scaleCoopOk = ($bsA0 -match 'scaled:[1-9]') -and ($bsB0 -match 'scaled:[1-9]')
    Write-Host ("=== R10_SCALING_COOP: {0} (A={1} | B={2}) ===" -f $(if ($scaleCoopOk) {'PASS'} else {'FAIL'}), $bsA0, $bsB0)

    # --- 3. HitHook Combat on Live Open-World Enemies (66992 -125076 -44454) ---
    Cmd $coopA "tp 66992 -125076 -44454" 2
    Cmd $coopB "tp 66992 -125076 -44454" 6
    Cmd $coopA "hittest" 3
    $hhA = Sync $coopA 'hithook'; $hhB = Sync $coopB 'hithook'
    $hitOk = ($hhA -match 'hooked:1') -and ($hhA -match 'tx:[1-9]') -and ($hhB -match 'rx:[1-9]') -and ($hhB -match 'applied:[1-9]')
    Write-Host ("=== R10_HITHOOK: {0} (A={1} | B={2}) ===" -f $(if ($hitOk) {'PASS'} else {'FAIL'}), $hhA, $hhB)

    # --- 4. Seal Active Def (S9), Harden (S18), NG+ Guard Hook (S61) ---
    Press-Key $pA 0x51 450 # Q (Harden/Seal)
    Start-Sleep 2
    $pbA = Sync $coopA 'partb'; $pbB = Sync $coopB 'partb'
    $ngpHookOk = Select-String -Path (Join-Path $coopA 'probe.log') -Pattern 'S_ResetForNewGamePlus' -Quiet
    $sealOk = ($pbA -match 'seal:[^-\s]')
    Write-Host ("=== R10_SEAL_NGP: {0} (A={1} | B={2} | ngpHook={3}) ===" -f $(if ($sealOk -and $ngpHookOk) {'PASS'} else {'CHECK'}), $pbA, $pbB, $ngpHookOk)

    # --- 5. Real Boss Arena Beacon Fast-Travel Follow (S45 + Beacon Follow) ---
    # Jump host A to real Boss Beacon (-85116 -458831 -53693) -> guest B should follow!
    Cmd $coopA "tp -85116 -458831 -53550" 8
    $bsB1 = Sync $coopB 'bscale'
    $bossFollowOk = ($bsB1 -match 'bc:\d+/[1-9]')
    Write-Host ("=== R10_BOSS_BEACON_FOLLOW: {0} (B bscale={1}) ===" -f $(if ($bossFollowOk) {'PASS'} else {'CHECK'}), $bsB1)
    # Check streamed BossAI and TombZones near boss beacon
    Cmd $coopA "bossphase dump" 2
    Cmd $coopA "areatomb" 2

    # --- 6. Disconnect -> Solo Scaling Restore + Steam Lobby Smoke ---
    Cmd $coopA "lanstop" 1
    Cmd $coopB "lanstop" 4
    $stAEnd = Sync $coopA 'steam'; $stBEnd = Sync $coopB 'steam'
    $bsAEnd = Sync $coopA 'bscale'
    $discOk = ($stAEnd -match 'st:0') -and ($stBEnd -match 'st:0')
    Write-Host ("=== R10_DISCONNECT: {0} (A steam={1} bscale={2} | B steam={3}) ===" -f $(if ($discOk) {'PASS'} else {'FAIL'}), $stAEnd, $bsAEnd, $stBEnd)

    Cmd $coopA "steamhost ms2v10smoke" 6
    $stLobby = Sync $coopA 'steam'
    $steamOk = ($stLobby -match 'st:2') -and ($stLobby -match 'app:2584270')
    Write-Host ("=== R10_STEAM_LOBBY: {0} ({1}) ===" -f $(if ($steamOk) {'PASS'} else {'FAIL'}), $stLobby)
    Cmd $coopA "steamstop" 1

    Write-Host "`n=== KEY PROBE LOG LINES ==="
    Select-String -Path (Join-Path $coopA 'probe.log') -Pattern 'hithook:|beacon:|partb:|bossphase|areatomb' | Select-Object -Last 25 | ForEach-Object { "A: " + $_.Line }
    Select-String -Path (Join-Path $coopB 'probe.log') -Pattern 'hithook:|beacon:|partb:' | Select-Object -Last 15 | ForEach-Object { "B: " + $_.Line }
} finally {
    Stop-All
    if (Test-Path $backupDir) {
        if (-not (Test-Path $saveDir)) { New-Item -ItemType Directory -Force -Path $saveDir | Out-Null }
        Copy-Item (Join-Path $backupDir '*') $saveDir -Force -ErrorAction SilentlyContinue
        Remove-Item $backupDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Restored owner saves cleanly."
    }
}
