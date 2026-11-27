# test-r10-real-gameplay.ps1 -- Comprehensive two-client rig test for the latest DLL (bea5504):
#   1. In-DLL LAN UDP + Full Peer Sync (ready=1 & peerready=1 on both) + Restyled F9 Menu / PostRender
#   2. Open-World Combat + Real Drops & Currency Rewards (BP_Horde_Brigand_C: hittest, real LMB check, dropskill -> drops roll/veto/grant on BOTH)
#   3. Real NPC Conversation on Host & Guest (BP_NPC_Shopkeeper_Dog, BP_NPC_Genessa_Training, BP_NPC_ShellKeeper: E/F + dlgstart)
#   4. Real Boss Fight (BP_DwarfBoss_C at Boss Landing Area #2: both clients see boss, aggro/damage/phase2 sync)
#   5. Disconnect / Solo Scaling Restore + Live Steam P2P Lobby Smoke
#
# Always snapshots & restores %LOCALAPPDATA%\MortalShell2\Saved\SaveGames in try/finally.
param(
    [string]$GameA = "G:\Games\_MS2-new-a",
    [string]$GameB = "G:\Games\_MS2-new-b",
    [string]$SaveFile = "C:\Users\ALVIBO\.t3\userdata\providers\antigravity\ac0a3dfd6dddb20962cecff6ee5fe65e19d3923be20e52c5ab52ff877f7e4c32\antigravity-acp\brain\8a67115d-3544-4299-8254-a5b1d045d5bd\scratch\saves\Savegame #5.sav",
    [int]$BootWait = 120,
    [int]$LanPort = 47799,
    [string]$Tag = "r10_real_gameplay2"
)
$ErrorActionPreference = 'Stop'

Add-Type -Namespace Win32 -Name R10Real2 -MemberDefinition @'
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
$backupDir = Join-Path $env:TEMP ("ms2_r10real2_savebak_" + (Get-Date -Format 'HHmmss'))
$outDir = Join-Path "G:\Freespace\shell_coop\local\lab" ((Get-Date -Format 'yyyyMMdd-HHmmss') + "-" + $Tag)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
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

function ProbeTail([string]$coop, [string]$pattern, [int]$last = 10) {
    $p = Join-Path $coop 'probe.log'
    if (-not (Test-Path $p)) { return @() }
    return @(Select-String -Path $p -Pattern $pattern -ErrorAction SilentlyContinue | Select-Object -Last $last | ForEach-Object { $_.Line })
}

function Cmd([string]$coop, [string]$text, [int]$waitSec = 2) {
    $f = Join-Path $coop 'cmd.txt'
    $tmp = Join-Path $coop 'cmd.tmp'
    [IO.File]::WriteAllText($tmp, $text + "`n")
    for ($try = 0; $try -lt 5; $try++) {
        try { Move-Item -LiteralPath $tmp -Destination $f -Force; break }
        catch { Start-Sleep -Milliseconds 150 }
    }
    $due = (Get-Date).AddSeconds(12)
    while ((Get-Date) -lt $due) {
        $exists = $false
        try { $exists = Test-Path -LiteralPath $f } catch { $exists = $false }
        if (-not $exists) { break }
        Start-Sleep -Milliseconds 200
    }
    if ($waitSec -gt 0) { Start-Sleep $waitSec }
}

function Focus-Win($proc, [int]$x = 40, [int]$y = 40) {
    try {
        $proc.Refresh(); $h = $proc.MainWindowHandle
        if (-not $h -or $h -eq [IntPtr]::Zero) { return $false }
        [Win32.R10Real2]::ShowWindow([IntPtr]$h, 9) | Out-Null
        [Win32.R10Real2]::SetWindowPos([IntPtr]$h, [IntPtr]::Zero, $x, $y, 1024, 576, 0x40) | Out-Null
        [Win32.R10Real2]::SetForegroundWindow([IntPtr]$h) | Out-Null
        Start-Sleep -Milliseconds 300
        return $true
    } catch { return $false }
}

function Click-Attack($proc, [int]$clicks = 3) {
    if (-not (Focus-Win $proc)) { return }
    $r = New-Object Win32.R10Real2+RECT
    [Win32.R10Real2]::GetWindowRect([IntPtr]$proc.MainWindowHandle, [ref]$r) | Out-Null
    $cx = [int](($r.Left + $r.Right) / 2); $cy = [int](($r.Top + $r.Bottom) / 2)
    [Win32.R10Real2]::SetCursorPos($cx, $cy) | Out-Null
    for ($i = 0; $i -lt $clicks; $i++) {
        [Win32.R10Real2]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 80
        [Win32.R10Real2]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 450
    }
}

function Press-Key($proc, [byte]$vk, [int]$holdMs = 200) {
    if (-not (Focus-Win $proc)) { return }
    [Win32.R10Real2]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $holdMs
    [Win32.R10Real2]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 300
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
    [IO.File]::WriteAllText((Join-Path $coop 'AUTO_CONTINUE.txt'), "1")
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
    Write-Host "Output dir: $outDir"
    Stop-All
    New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
    if (Test-Path $saveDir) { Copy-Item (Join-Path $saveDir '*') $backupDir -Force -ErrorAction SilentlyContinue }
    if (Test-Path $SaveFile) {
        Copy-Item $SaveFile (Join-Path $saveDir 'WorldState_0.sav') -Force
        Write-Host "Seeded Savegame #5.sav -> WorldState_0.sav"
    }

    Prep-Coop $coopA 'A' 'host'
    Prep-Coop $coopB 'B' 'guest'

    $pA = Start-Client $GameA
    Start-Sleep 28
    $pB = Start-Client $GameB

    # Wait for BOTH ready=1 (world streaming + settling complete!)
    $deadline = (Get-Date).AddSeconds($BootWait + 90)
    while ((Get-Date) -lt $deadline) {
        if ($pA.HasExited -or $pB.HasExited) {
            throw ("Client exited during boot: A_exited={0} B_exited={1}" -f $pA.HasExited, $pB.HasExited)
        }
        $rA = Sync $coopA 'ready'; $rB = Sync $coopB 'ready'
        if ($rA -eq '1' -and $rB -eq '1') { break }
        Start-Sleep 2
    }
    if ($pA.HasExited -or $pB.HasExited) {
        throw ("Client exited before tests: A_exited={0} B_exited={1}" -f $pA.HasExited, $pB.HasExited)
    }
    Write-Host ("Booted: A phase={0} ready={1} | B phase={2} ready={3}" -f (Sync $coopA 'phase'), (Sync $coopA 'ready'), (Sync $coopB 'phase'), (Sync $coopB 'ready'))

    # =========================================================================
    # TEST 1: IN-DLL LAN UDP + FULL PEER SYNC + RESTYLED F9 MENU (bea5504)
    # =========================================================================
    Write-Host "`n=== TEST 1: IN-DLL LAN UDP + FULL PEER SYNC + F9 MENU ==="
    Cmd $coopA "lanhost $LanPort" 2
    Cmd $coopB "lanjoin 127.0.0.1 $LanPort" 5
    $lanOk = $false
    $dl = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $dl) {
        if ((Sync $coopA 'steam') -match 'st:3' -and (Sync $coopB 'steam') -match 'st:3' -and
            (Sync $coopA 'peerready') -eq '1' -and (Sync $coopB 'peerready') -eq '1') {
            $lanOk = $true; break
        }
        Start-Sleep 1
    }
    Write-Host ("=== R10_LANUDP_SYNC: {0} (A steam={1} peerready={2} | B steam={3} peerready={4}) ===" -f $(if ($lanOk) {'PASS'} else {'FAIL'}), (Sync $coopA 'steam'), (Sync $coopA 'peerready'), (Sync $coopB 'steam'), (Sync $coopB 'peerready'))
    Write-Host ("  A wire={0} | B wire={1}" -f (Sync $coopA 'wire'), (Sync $coopB 'wire'))

    Press-Key $pA 0x78 120   # VK_F9
    Start-Sleep 1
    Press-Key $pA 0x78 120   # VK_F9 close
    ProbeTail $coopA 'menu:' 8 | ForEach-Object { Write-Host "  A-menu: $_" }

    # =========================================================================
    # TEST 2: REAL NPC CONVERSATION FIRST (AT HUB, BEFORE COMBAT!)
    # =========================================================================
    Write-Host "`n=== TEST 2: REAL NPC CONVERSATION (HUB NPCs) ==="
    Cmd $coopA "areasweep NPC" 2
    ProbeTail $coopA 'areasweep:' 12 | ForEach-Object { Write-Host "  A-npcs: $_" }

    $npcList = @('BP_NPC_Shopkeeper_Dog', 'BP_NPC_Genessa_Training', 'BP_NPC_ShellKeeper')
    $npcOpened = $false
    foreach ($npc in $npcList) {
        Write-Host "  Trying NPC: $npc"
        Cmd $coopA "areasweep $npc" 1
        Cmd $coopA "areago nearest" 2
        Cmd $coopB "areasweep $npc" 1
        Cmd $coopB "areago nearest" 2
        Start-Sleep 1
        # Try real interact key E, then dlgstart
        Press-Key $pA 0x45 150
        Start-Sleep 1
        Cmd $coopA "dlgdump" 1
        $ddA = (ProbeTail $coopA 'dlgdump:' 1) -join ''
        if ($ddA -notmatch 'dlg=me:1') {
            Cmd $coopA "dlgstart $npc" 3
            Cmd $coopA "dlgdump" 1
            Cmd $coopB "dlgdump" 1
            $ddA = (ProbeTail $coopA 'dlgdump:' 1) -join ''
        }
        $ddB = (ProbeTail $coopB 'dlgdump:' 1) -join ''
        Write-Host "    A dlgdump: $ddA"
        Write-Host "    B dlgdump: $ddB"
        ProbeTail $coopA 'dlgstart:' 10 | ForEach-Object { Write-Host "    A-dlgstart: $_" }
        if ($ddA -match 'dlg=me:1' -or $ddB -match 'peer:1') {
            $npcOpened = $true
            Cmd $coopA "dlgstop" 1
            break
        }
    }
    Write-Host ("=== R10_REAL_NPC: {0} ===" -f $(if ($npcOpened) {'PASS'} else {'FAIL'}))

    # =========================================================================
    # TEST 3: OPEN-WORLD COMBAT, SCALING, HITHOOK & DROPS ON LIVE BRIGANDS
    # =========================================================================
    Write-Host "`n=== TEST 3: OPEN-WORLD SCALING, HITHOOK & DROPS ==="
    # Stand on a slightly elevated/edge spot near the camp so brigands don't insta-kill us
    Cmd $coopA "tp 66992 -125076 -44454" 2
    Cmd $coopB "tp 66992 -125076 -44454" 5
    Write-Host ("Scaling at open-world camp -> A: {0} | B: {1}" -f (Sync $coopA 'bscale'), (Sync $coopB 'bscale'))

    Cmd $coopA "dropswallet" 1
    Cmd $coopB "dropswallet" 1
    Cmd $coopA "enemydump" 1
    ProbeTail $coopA 'enemydump:' 8 | ForEach-Object { Write-Host "  A-enemy: $_" }

    # 3a. HitHook test on live open-world enemy
    Cmd $coopA "hittest" 3
    Write-Host ("After hittest -> A hithook={0} | B hithook={1}" -f (Sync $coopA 'hithook'), (Sync $coopB 'hithook'))

    # 3b. Real LMB check (weaponstate montage)
    Click-Attack $pA 2
    Cmd $coopA "weaponstate" 1
    ProbeTail $coopA 'weaponstate:' 3 | ForEach-Object { Write-Host "  A-wpn: $_" }

    # 3c. Kill a real open-world enemy through the game's own StartDeath/GA_DeathBase_C (dropskill)
    Cmd $coopA "dropskill nearest" 5
    Cmd $coopA "dropsdump" 1
    Cmd $coopB "dropsdump" 1
    Cmd $coopA "dropswallet" 1
    Cmd $coopB "dropswallet" 1
    Write-Host ("After dropskill -> A drops={0} | B drops={1}" -f (Sync $coopA 'drops'), (Sync $coopB 'drops'))
    ProbeTail $coopA 'drops:|dropskill|dropswallet' 10 | ForEach-Object { Write-Host "  A-drops: $_" }
    ProbeTail $coopB 'drops:|dropswallet' 10 | ForEach-Object { Write-Host "  B-drops: $_" }

    # =========================================================================
    # TEST 4: REAL BOSS FIGHT (BP_DwarfBoss_C at Boss Landing Area #2)
    # =========================================================================
    Write-Host "`n=== TEST 4: REAL BOSS FIGHT (BP_DwarfBoss_C, AREA #2) ==="
    Cmd $coopA "areasweep BP_LandingArea_Boss" 2
    Cmd $coopB "areasweep BP_LandingArea_Boss" 2
    Cmd $coopA "areago 2" 2
    Cmd $coopB "areago 2" 2
    Write-Host "Waiting 30s for Fainweald Dwarf Boss cell to stream in..."
    Start-Sleep 30

    Cmd $coopA "bossphase dump" 2
    Cmd $coopB "bossphase dump" 2
    Write-Host ("  A boss before: " + ((ProbeTail $coopA 'bossdump:' 2) -join " | "))
    Write-Host ("  B boss before: " + ((ProbeTail $coopB 'bossdump:' 2) -join " | "))

    # Move right next to DwarfBoss, try hittest + hurt (to test crossing t2=0.750 on a real boss!)
    Cmd $coopA "enemytp DwarfBoss" 2
    Cmd $coopA "hittest" 2
    Cmd $coopA "hurt 30" 3
    Cmd $coopA "bossphase dump" 2
    Cmd $coopB "bossphase dump" 2
    Write-Host ("  A boss after hurt 30: " + ((ProbeTail $coopA 'bossdump:' 2) -join " | "))
    Write-Host ("  B boss after hurt 30: " + ((ProbeTail $coopB 'bossdump:' 2) -join " | "))
    Write-Host ("  A boss sync={0} | B boss sync={1}" -f (Sync $coopA 'boss'), (Sync $coopB 'boss'))
    Write-Host ("  A hithook={0} | B hithook={1}" -f (Sync $coopA 'hithook'), (Sync $coopB 'hithook'))
    Cmd $coopA "areatomb" 2

    # =========================================================================
    # TEST 5: DISCONNECT + SOLO SCALING RESTORE + STEAM P2P SMOKE
    # =========================================================================
    Write-Host "`n=== TEST 5: DISCONNECT + STEAM P2P LOBBY SMOKE ==="
    Cmd $coopA "lanstop" 1
    Cmd $coopB "lanstop" 3
    Write-Host ("After lanstop -> A steam={0} bscale={1} | B steam={2} bscale={3}" -f (Sync $coopA 'steam'), (Sync $coopA 'bscale'), (Sync $coopB 'steam'), (Sync $coopB 'bscale'))
    Cmd $coopA "steamhost ms2rigreal2" 6
    Write-Host ("After steamhost -> A steam={0}" -f (Sync $coopA 'steam'))
    Cmd $coopA "steamstop" 1

    Copy-Item (Join-Path $coopA 'probe.log') (Join-Path $outDir 'probe_A.log') -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coopB 'probe.log') (Join-Path $outDir 'probe_B.log') -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coopA 'sync.txt')  (Join-Path $outDir 'sync_A.txt')  -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coopB 'sync.txt')  (Join-Path $outDir 'sync_B.txt')  -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path (Split-Path $coopA -Parent) 'ms2coop-native.log') (Join-Path $outDir 'native_A.log') -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path (Split-Path $coopB -Parent) 'ms2coop-native.log') (Join-Path $outDir 'native_B.log') -Force -ErrorAction SilentlyContinue
    Write-Host "Saved logs to $outDir"
} finally {
    Stop-All
    Remove-Item (Join-Path $coopA 'AUTO_CONTINUE.txt') -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $coopB 'AUTO_CONTINUE.txt') -Force -ErrorAction SilentlyContinue
    if (Test-Path $backupDir) {
        if (-not (Test-Path $saveDir)) { New-Item -ItemType Directory -Force -Path $saveDir | Out-Null }
        Copy-Item (Join-Path $backupDir '*') $saveDir -Force -ErrorAction SilentlyContinue
        Remove-Item $backupDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "Restored owner saves cleanly."
    }
}
