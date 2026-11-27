# test-r10-npc-boss.ps1 -- NPC conversation + boss engagement on the two-client rig.
#   NPC: host walks to a hub NPC and presses the game's interact action (in-DLL injection).
#   Boss: both players go to BP_DwarfBoss_C and attack it with injected light attacks.
#   No OS input is sent: every key/click goes through the `inject` verb.
# Owner saves are copied to local\snapshots first and restored in finally.
param(
    [string]$GameA = "G:\Games\_MS2-new-a",
    [string]$GameB = "G:\Games\_MS2-new-b",
    [string]$SaveFile = "C:\Users\ALVIBO\.t3\userdata\providers\antigravity\ac0a3dfd6dddb20962cecff6ee5fe65e19d3923be20e52c5ab52ff877f7e4c32\antigravity-acp\brain\8a67115d-3544-4299-8254-a5b1d045d5bd\scratch\saves\Savegame #5.sav",
    [int]$BootWait = 150,
    [int]$LanPort = 47799,
    [string]$Boss = "246900 -537918 -61918",
    [string]$Tag = "r10_npcboss",
    [switch]$SkipNpc,
    [switch]$SkipBoss,
    [ValidateSet('A', 'B')][string]$HoldWho = 'A'
)
$ErrorActionPreference = 'Stop'


$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
$saveDir = Join-Path $env:LOCALAPPDATA 'MortalShell2\Saved\SaveGames'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupDir = "G:\Freespace\shell_coop\local\snapshots\saves-$stamp-$Tag"
$outDir = Join-Path "G:\Freespace\shell_coop\local\lab" "$stamp-$Tag"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$pA = $null; $pB = $null

function Stop-All {
    Get-Process | Where-Object { $_.ProcessName -match 'MortalShell2|CrashReportClient' -and ($_.Path -like 'G:\Games\_MS2-new-*' -or $_.Path -like 'G:\Games\_MS2-new-*') } | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
    Start-Sleep 2
}
function Sync([string]$coop, [string]$key) {
    $f = Join-Path $coop 'sync.txt'
    if (-not (Test-Path $f)) { return $null }
    try { foreach ($l in [IO.File]::ReadAllLines($f)) { if ($l.StartsWith("$key=")) { return $l.Substring($key.Length + 1) } } } catch {}
    return $null
}
function NLog([string]$game) { Join-Path $game 'MortalShell2\Binaries\Win64\ms2coop-native.log' }
function Grab([string]$game, [string]$pattern, [int]$last = 20) {
    $p = NLog $game
    if (-not (Test-Path $p)) { return @() }
    return @(Select-String -Path $p -Pattern $pattern -ErrorAction SilentlyContinue | Select-Object -Last $last | ForEach-Object { $_.Line })
}
function Cmd([string]$coop, [string]$text, [int]$waitSec = 2) {
    $f = Join-Path $coop 'cmd.txt'; $tmp = Join-Path $coop 'cmd.tmp'
    [IO.File]::WriteAllText($tmp, $text + "`n")
    for ($try = 0; $try -lt 5; $try++) { try { Move-Item -LiteralPath $tmp -Destination $f -Force; break } catch { Start-Sleep -Milliseconds 150 } }
    $due = (Get-Date).AddSeconds(12)
    while ((Get-Date) -lt $due) { if (-not (Test-Path -LiteralPath $f)) { break }; Start-Sleep -Milliseconds 200 }
    if ($waitSec -gt 0) { Start-Sleep $waitSec }
}
function Prep-Coop([string]$coop, [string]$id, [string]$role) {
    New-Item -ItemType Directory -Force -Path $coop | Out-Null
    foreach ($pat in 'out_*.txt', 'peer_*.txt', 'ws_ledger_*.txt') { Get-ChildItem $coop -Filter $pat -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue }
    foreach ($f in 'probe.log', 'sync.txt', 'cmd.txt') { Remove-Item (Join-Path $coop $f) -Force -ErrorAction SilentlyContinue }
    [IO.File]::WriteAllText((Join-Path $coop 'playerid.txt'), $id)
    [IO.File]::WriteAllText((Join-Path $coop 'role.txt'), $role)
    [IO.File]::WriteAllText((Join-Path $coop 'AUTO_CONTINUE.txt'), "1")
}
function Start-Client([string]$gameDir) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = Join-Path $gameDir 'MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe'
    $psi.Arguments = "MortalShell2 -windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport $env:MS2_EXTRA_ARGS"
    $psi.WorkingDirectory = $gameDir; $psi.UseShellExecute = $true
    return [System.Diagnostics.Process]::Start($psi)
}
function Alive { return (-not $pA.HasExited) -and (-not $pB.HasExited) }
function Stand-Together {
    Cmd $coopA "enemytp" 3
    $line = (Grab $GameA 'enemytp: (?!facing)' 1) -join ''
    Write-Host "  A: $line"
    $pos = Sync $coopA 'selfpos'
    if ($pos) { Cmd $coopB "tp $pos" 3 }
    Cmd $coopB "enemytp" 3
    Write-Host ("  B: " + ((Grab $GameB 'enemytp: (?!facing)' 1) -join ''))
}

$script:AtkIA = ''; $script:UseIA = ''
function Find-IA([string]$game, [string[]]$prefer) {
    $lines = (Grab $game 'ialist: ' 60) -join ' '
    $names = @([regex]::Matches($lines, '(IA_[A-Za-z0-9_]+)\(\d\)') | ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
    foreach ($p in $prefer) { $hit = $names | Where-Object { $_ -match $p } | Select-Object -First 1; if ($hit) { return $hit } }
    return ''
}
function Attack([string]$coop, [int]$n = 3) {
    # lock on to the enemy enemytp faced (control rotation), swing, then toggle lock off
    Cmd $coop "inject IA_LockOn 0.1" 0; Start-Sleep -Milliseconds 600
    for ($i = 0; $i -lt $n; $i++) { Cmd $coop "inject $script:AtkIA 0.12" 0; Start-Sleep -Milliseconds 900 }
    Cmd $coop "inject IA_LockOn 0.1" 0; Start-Sleep -Milliseconds 400
}
Add-Type -Namespace Win32 -Name PW2 -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PrintWindow(System.IntPtr hWnd, System.IntPtr hdcBlt, uint nFlags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT lpRect);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
'@
Add-Type -AssemblyName System.Drawing
# passive capture only (PrintWindow); no focus change, no input
function Shot($proc, [string]$name) {
    try {
        $proc.Refresh(); $h = [IntPtr]$proc.MainWindowHandle
        if ($h -eq [IntPtr]::Zero) { return }
        $r = New-Object Win32.PW2+RECT; [Win32.PW2]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        if ($w -le 0 -or $ht -le 0) { return }
        $bmp = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
        [Win32.PW2]::PrintWindow($h, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose()
        $path = Join-Path $outDir ("shot-{0}-{1}.png" -f (Get-Date -Format 'HHmmss'), $name)
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        Write-Host "  shot: $path"
    } catch { Write-Host "  shot failed: $($_.Exception.Message)" }
}
function DlgOpen([string]$coop) { return ((Sync $coop 'dlg') -match '^me:1') }
function BossLine([string]$game) { return ((Grab $game 'bossdump: (?!n=)' 2) -join ' | ') }

$seeded = $false; $hadWs = $false
try {
    Write-Host "Output dir: $outDir"
    Stop-All
    New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
    if (Test-Path $saveDir) { Copy-Item (Join-Path $saveDir '*') $backupDir -Recurse -Force }
    $hadWs = Test-Path (Join-Path $saveDir 'WorldState_0.sav')
    Copy-Item $SaveFile (Join-Path $saveDir 'WorldState_0.sav') -Force; $seeded = $true
    Write-Host "Saves backed up to $backupDir; seeded test save"

    Prep-Coop $coopA 'A' 'host'
    Prep-Coop $coopB 'B' 'guest'
    $pA = Start-Client $GameA
    # the guest starts only once the host is in the world: two clients loading at
    # once crashed the host inside steam_api64 (heap double free, 13:37 / 13:41 runs)
    $dlA = (Get-Date).AddSeconds($BootWait + 60)
    while ((Get-Date) -lt $dlA) {
        if ($pA.HasExited) { throw "Host exited during boot" }
        if ((Sync $coopA 'ready') -eq '1') { break }
        Start-Sleep 2
    }
    Start-Sleep 10
    $pB = Start-Client $GameB
    $deadline = (Get-Date).AddSeconds($BootWait + 90)
    while ((Get-Date) -lt $deadline) {
        if (-not (Alive)) { throw ("Client exited during boot: A={0} B={1}" -f $pA.HasExited, $pB.HasExited) }
        if ((Sync $coopA 'ready') -eq '1' -and (Sync $coopB 'ready') -eq '1') { break }
        Start-Sleep 2
    }
    Cmd $coopA "lanhost $LanPort" 2
    Cmd $coopB "lanjoin 127.0.0.1 $LanPort" 5
    $dl = (Get-Date).AddSeconds(40)
    while ((Get-Date) -lt $dl) {
        if ((Sync $coopA 'peerready') -eq '1' -and (Sync $coopB 'peerready') -eq '1' -and (Sync $coopA 'phase') -eq 'sync') { break }
        Start-Sleep 1
    }
    Write-Host ("Linked: A phase={0} peerready={1} | B phase={2} peerready={3}" -f (Sync $coopA 'phase'), (Sync $coopA 'peerready'), (Sync $coopB 'phase'), (Sync $coopB 'peerready'))

    Cmd $coopA "labgod 1" 0
    Cmd $coopB "labgod 1" 0
    Cmd $coopA "ialist" 2
    Grab $GameA 'ialist: ' 14 | ForEach-Object { Write-Host "  A: $_" }
    $script:AtkIA = Find-IA $GameA @('^IA_Attack_Primary$', '^IA_Attack_Primary_Unblocked$', 'LightAttack')
    $script:UseIA = Find-IA $GameA @('^IA_Interact_Down$', '^IA_Interact_Debug_Pressed$', '^IA_Interact$', 'Interact', '^IA_Use$')
    Write-Host "  attack IA: $script:AtkIA ; interact IA: $script:UseIA"

    # ---------------------------------------------------------------- NPC
    Write-Host "`n=== NPC conversation (host) ==="
    $npcOk = $false
    $npcList = if ($SkipNpc) { @() } else { @('BP_NPC_ShellKeeper', 'BP_NPC_Shopkeeper_Dog', 'BP_NPC_Genessa_Training') }
    foreach ($npc in $npcList) {
        if (-not (Alive)) { break }
        Write-Host "--- $npc"
        Cmd $coopA "areasweep $npc" 1
        Cmd $coopA "areago nearest" 3
        # the NPC only takes focus while the player stands INSIDE its focus box and is not
        # falling (probe 2: focus=1 handles=1 about 5 s after stepping into FocusCollision)
        Cmd $coopA "interdump $npc go" 3
        # wait until the game itself says the NPC has focus (landed, inside the box)
        $focus = $false
        for ($w = 0; $w -lt 10 -and -not $focus; $w++) {
            Cmd $coopA "interdump $npc" 1
            $focus = [bool]((Grab $GameA 'interdump: \[0\]' 1) -join '' -match 'focus=1')
        }
        Grab $GameA 'interdump: ' 3 | ForEach-Object { Write-Host "  A: $_" }
        Write-Host "  focus=$focus"
        Cmd $coopA "inject IA_Interact_Pressed 0.15" 4
        if (-not (DlgOpen $coopA)) { Cmd $coopA "inject IA_Interact_Pressed 0.15" 4 }
        if (-not (DlgOpen $coopA)) {
            # the handler's own entry point (what the bound input reaches)
            Cmd $coopA "interdump $npc call IA_Interact_Pressed" 5
            Grab $GameA 'interdump: call|HandleInteractEvent' 3 | ForEach-Object { Write-Host "  A: $_" }
        }
        Grab $GameA 'inject: |dlg: |dialog: ' 8 | ForEach-Object { Write-Host "  A: $_" }
        Write-Host ("  A dlg={0} | B dlg={1}" -f (Sync $coopA 'dlg'), (Sync $coopB 'dlg'))
        if (DlgOpen $coopA) {
            $npcOk = $true
            Start-Sleep 3
            Shot $pA 'npc-A'; Shot $pB 'npc-B'
            Write-Host ("  B sees: {0}" -f (Sync $coopB 'dlg'))
            Cmd $coopB "dlgdump" 1
            Grab $GameB 'dlg: |dialog: |listen|dlgdump: dlg' 10 | ForEach-Object { Write-Host "  B: $_" }
            Start-Sleep 6
            Shot $pA 'npc-A2'; Shot $pB 'npc-B2'
            Write-Host ("  later: A dlg={0} | B dlg={1}" -f (Sync $coopA 'dlg'), (Sync $coopB 'dlg'))
            Cmd $coopA "dlgstop" 3
            Grab $GameA 'dlgstop' 1 | ForEach-Object { Write-Host "  A: $_" }
            Start-Sleep 6
            Write-Host ("  after stop: A dlg={0} | B dlg={1}" -f (Sync $coopA 'dlg'), (Sync $coopB 'dlg'))
            Shot $pB 'npc-B3'
            break
        }
    }
    Write-Host ("=== NPC: {0} ===" -f $(if ($npcOk) { 'OPENED' } else { 'NOT OPENED' }))

    # ---------------------------------------------------------------- Boss
    if ($SkipBoss) { Write-Host "Boss skipped"; return }
    Write-Host "`n=== Boss (BP_DwarfBoss_C) ==="
    Cmd $coopA "tp $Boss" 2
    Cmd $coopB "tp $Boss" 2
    Write-Host "  waiting 35 s for the boss cell"
    Start-Sleep 35
    # enter the arena the way a player does: through BP_CinematicPlayer's TriggerArea
    # (probe 2: CIN_DwarfBoss_01 plays, then BPC_BossAI aggro=1)
    # lab only: a 75 HP test pawn dies to boss hits that bypass the payload veto
    Cmd $coopA "labhp 5000" 1
    Cmd $coopB "labhp 5000" 1
    Grab $GameA 'labhp: ' 1 | ForEach-Object { Write-Host "  A: $_" }
    Cmd $coopA "areasweep BP_CinematicPlayer" 1
    Cmd $coopB "areasweep BP_CinematicPlayer" 1
    Cmd $coopA "areacomp 0 TriggerArea go" 0
    Cmd $coopB "areacomp 0 TriggerArea go" 1
    Grab $GameA 'areacomp: ' 2 | ForEach-Object { Write-Host "  A: $_" }
    Grab $GameB 'areacomp: ' 2 | ForEach-Object { Write-Host "  B: $_" }
    Write-Host "  waiting 40 s for the intro cinematic"
    Start-Sleep 40
    Grab $GameA 'cine: |LOCAL BUSY' 4 | ForEach-Object { Write-Host "  A: $_" }
    Grab $GameB 'cine: |LOCAL BUSY|PEER' 6 | ForEach-Object { Write-Host "  B: $_" }
    Cmd $coopA "bossdump" 2
    Cmd $coopB "bossdump" 2
    Write-Host ("  A: " + (BossLine $GameA))
    Write-Host ("  B: " + (BossLine $GameB))
    function Hp([string]$game) { $l = (Grab $game 'bossdump: BP_' 1) -join ''; if ($l -match 'phase=(\d) hp=([\d.]+)') { return "phase=$($Matches[1]) hp=$($Matches[2])" }; return '?' }
    function Dump { Cmd $coopA "bossdump" 0; Cmd $coopB "bossdump" 2; return ("A[{0}] B[{1}]" -f (Hp $GameA), (Hp $GameB)) }
    function Swing([string]$coop, [int]$bursts) {
        # re-close the distance before every burst: the boss walks off between them
        for ($b = 0; $b -lt $bursts -and (Alive); $b++) { Cmd $coop "enemytp DwarfBoss" 2; Attack $coop 3; Start-Sleep 1 }
    }
    $other = if ($HoldWho -eq 'A') { 'B' } else { 'A' }
    $coopHeld = if ($HoldWho -eq 'A') { $coopA } else { $coopB }
    $gameHeld = if ($HoldWho -eq 'A') { $GameA } else { $GameB }
    $coopWin = if ($other -eq 'A') { $coopA } else { $coopB }

    # ---- 1. HP sync: the guest's hits, the host applies them, the guest's bar follows
    Write-Host "`n--- 1. guest hits -> host HP -> guest bar"
    Write-Host ("  before: " + (Dump))
    Swing $coopB 4
    Write-Host ("  right after B's hits: " + (Dump))
    Start-Sleep 3
    Write-Host ("  3 s later: " + (Dump))
    Grab $GameA 'R10-BOSSDMG' 3 | ForEach-Object { Write-Host "  A: $_" }

    # ---- 2. phase 2 from the guest's hits (host put just above the gate)
    Write-Host "`n--- 2. phase 2 from the guest's hits"
    Cmd $coopA "bosshp 0.76" 2
    Grab $GameA 'bosshp: ' 1 | ForEach-Object { Write-Host "  A: $_" }
    for ($k = 0; $k -lt 4 -and (Alive); $k++) {
        Swing $coopB 2
        if ((Hp $GameA) -match 'phase=2') { break }
        Cmd $coopA "bossdump" 1
        if ((Hp $GameA) -match 'phase=2') { break }
    }
    Start-Sleep 4
    Write-Host ("  after: " + (Dump))
    Grab $GameA 'R10-BOSSFIGHT|bossphase: (publish|phase)' 4 | ForEach-Object { Write-Host "  A: $_" }
    Grab $GameB 'bossphase' 4 | ForEach-Object { Write-Host "  B: $_" }

    # ---- 3. hold until the partner wins: $HoldWho dies, the other kills the boss
    Write-Host "`n--- 3. $HoldWho dies in the fight, $other kills the boss"
    Cmd $coopHeld "die" 3
    Start-Sleep 12
    Grab $gameHeld 'COOP DEATH #|BOSS HOLD|COOP DEATH pending' 4 | ForEach-Object { Write-Host "  ${HoldWho}: $_" }
    Write-Host ("  {0} death={1}" -f $HoldWho, (Sync $coopHeld 'death'))
    Cmd $coopA "bosshp 0.015" 2
    Grab $GameA 'bosshp: ' 1 | ForEach-Object { Write-Host "  A: $_" }
    for ($k = 0; $k -lt 8 -and (Alive); $k++) {
        Swing $coopWin 2
        Cmd $coopA "bossdump" 1
        $curHp = Hp $GameA
        if ($curHp -eq '?' -or $curHp -match 'hp=0\.00') { break }
    }
    Write-Host ("  after the kill attempt: " + (Dump))
    Start-Sleep 20
    Grab $GameA 'R10-BOSSDMG|killed|startDeath|boss.*dead' 4 | ForEach-Object { Write-Host "  A: $_" }
    Grab $gameHeld 'recase|hold released|COOP DEATH respawn|respawned|back on|WIPE|world changed' 8 | ForEach-Object { Write-Host "  ${HoldWho}: $_" }
    Write-Host ("  A death={0}" -f (Sync $coopA 'death'))
    Write-Host ("  B death={0}" -f (Sync $coopB 'death'))
    Write-Host ("  final: " + (Dump))
    Write-Host ("Alive at end: A={0} B={1}" -f (-not $pA.HasExited), (-not $pB.HasExited))
} finally {
    foreach ($x in @(@('A', $GameA, $coopA), @('B', $GameB, $coopB))) {
        Copy-Item (NLog $x[1]) (Join-Path $outDir ("native_" + $x[0] + ".log")) -Force -ErrorAction SilentlyContinue
        Copy-Item (Join-Path $x[2] 'sync.txt') (Join-Path $outDir ("sync_" + $x[0] + ".txt")) -Force -ErrorAction SilentlyContinue
    }
    Stop-All
    Remove-Item (Join-Path $coopA 'AUTO_CONTINUE.txt') -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $coopB 'AUTO_CONTINUE.txt') -Force -ErrorAction SilentlyContinue
    if ($seeded) {
        if (-not $hadWs) { Remove-Item (Join-Path $saveDir 'WorldState_0.sav') -Force -ErrorAction SilentlyContinue }
        Copy-Item (Join-Path $backupDir '*') $saveDir -Recurse -Force
        Write-Host "Restored owner saves from $backupDir"
    }
    Write-Host "Logs: $outDir"
}
