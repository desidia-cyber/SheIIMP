# rig.ps1 -- Fast, persistent hot-session two-client co-op test rig for Mortal Shell II.
# Boots Host A and Guest B ONCE, keeps them LAN-linked, and runs real-gameplay test
# suites back-to-back without rebooting. Owner saves are always snapshotted on `start`
# and restored on `stop` (or on fatal boot failure).
#
# Usage:
#   .\rig.ps1 -Action start [-SaveNum 5] [-Tag session1]
#   .\rig.ps1 -Action status
#   .\rig.ps1 -Action cmd -A "bossdump" -B "bossdump" -Pattern "bossdump:"
#   .\rig.ps1 -Action suite -Suites atlas,dlg_vote,shells_look,world_interact,combat_fx,dungeon,boss_tour
#   .\rig.ps1 -Action stop
param(
    [ValidateSet('start', 'stop', 'status', 'cmd', 'suite')]
    [string]$Action = 'status',
    [string]$GameA = "G:\Games\_MS2-new-a",
    [string]$GameB = "G:\Games\_MS2-new-b",
    [int]$SaveNum = 5,
    [int]$LanPort = 47811,
    [int]$BootWait = 150,
    [string]$Tag = "rig",
    [string]$A = "",
    [string]$B = "",
    [int]$WaitSec = 2,
    [string]$Pattern = "",
    [string[]]$Suites = @('atlas'),
    [switch]$AutoStop
)

$ErrorActionPreference = 'Stop'

$RepoRoot    = "G:\Freespace\shell_coop"
$SessionFile = Join-Path $RepoRoot "local\rig-session.json"
$AtlasFile   = Join-Path $RepoRoot "local\rig-atlas.json"
$SaveDir     = Join-Path $env:LOCALAPPDATA "MortalShell2\Saved\SaveGames"
$SavesSource = "C:\Users\ALVIBO\.t3\userdata\providers\antigravity\ac0a3dfd6dddb20962cecff6ee5fe65e19d3923be20e52c5ab52ff877f7e4c32\antigravity-acp\brain\8a67115d-3544-4299-8254-a5b1d045d5bd\scratch\saves"
$CoopA       = Join-Path $GameA "MortalShell2\Binaries\Win64\coop"
$CoopB       = Join-Path $GameB "MortalShell2\Binaries\Win64\coop"

Add-Type -Namespace Win32 -Name RigShot -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PrintWindow(System.IntPtr hWnd, System.IntPtr hdcBlt, uint nFlags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT lpRect);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
'@
Add-Type -AssemblyName System.Drawing

function NLog([string]$game) { Join-Path $game "MortalShell2\Binaries\Win64\ms2coop-native.log" }

function SyncVal([string]$coop, [string]$key) {
    $f = Join-Path $coop "sync.txt"
    if (-not (Test-Path $f)) { return $null }
    try {
        foreach ($l in [IO.File]::ReadAllLines($f)) {
            if ($l.StartsWith("$key=")) { return $l.Substring($key.Length + 1) }
        }
    } catch {}
    return $null
}

function LogLen([string]$game) {
    $p = NLog $game
    if (-not (Test-Path $p)) { return 0 }
    return (Get-Item $p).Length
}

function GrabSince([string]$game, [long]$offset, [string]$pat = "") {
    $p = NLog $game
    if (-not (Test-Path $p)) { return @() }
    $fs = [IO.File]::Open($p, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        if ($offset -lt $fs.Length) { $fs.Seek($offset, [IO.SeekOrigin]::Begin) | Out-Null }
        else { return @() }
        $sr = New-Object System.IO.StreamReader($fs)
        $text = $sr.ReadToEnd()
        $lines = $text -split "`r?`n" | Where-Object { $_ -ne '' }
        if ($pat) { return @($lines | Where-Object { $_ -match $pat }) }
        return @($lines)
    } finally { $fs.Close() }
}

function GrabLast([string]$game, [string]$pat, [int]$last = 15) {
    $p = NLog $game
    if (-not (Test-Path $p)) { return @() }
    return @(Select-String -Path $p -Pattern $pat -ErrorAction SilentlyContinue | Select-Object -Last $last | ForEach-Object { $_.Line })
}

function Send-Cmd([string]$coop, [string]$text, [int]$wait = 1) {
    $f = Join-Path $coop "cmd.txt"
    $tmp = Join-Path $coop "cmd.tmp"
    [IO.File]::WriteAllText($tmp, $text + "`n")
    for ($try = 0; $try -lt 8; $try++) {
        try { Move-Item -LiteralPath $tmp -Destination $f -Force; break } catch { Start-Sleep -Milliseconds 120 }
    }
    $due = (Get-Date).AddSeconds(12)
    while ((Get-Date) -lt $due) {
        if (-not (Test-Path -LiteralPath $f)) { break }
        Start-Sleep -Milliseconds 150
    }
    if ($wait -gt 0) { Start-Sleep $wait }
}

function Send-Both([string]$text, [int]$wait = 1) {
    Send-Cmd $CoopA $text 0
    Send-Cmd $CoopB $text $wait
}

function Load-Session {
    if (-not (Test-Path $SessionFile)) { return $null }
    return (Get-Content $SessionFile -Raw | ConvertFrom-Json)
}

function Clients-Alive($sess) {
    if (-not $sess) { return $false }
    try {
        $a = Get-Process -Id $sess.pidA -ErrorAction Stop
        $b = Get-Process -Id $sess.pidB -ErrorAction Stop
        return (-not $a.HasExited) -and (-not $b.HasExited)
    } catch { return $false }
}

function Shot([int]$procId, [string]$outDir, [string]$name) {
    try {
        $proc = Get-Process -Id $procId -ErrorAction Stop
        $h = [IntPtr]$proc.MainWindowHandle
        if ($h -eq [IntPtr]::Zero) { return }
        $r = New-Object Win32.RigShot+RECT
        [Win32.RigShot]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        if ($w -le 0 -or $ht -le 0) { return }
        $bmp = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
        [Win32.RigShot]::PrintWindow($h, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose()
        $path = Join-Path $outDir ("shot-{0}-{1}.png" -f (Get-Date -Format 'HHmmss'), $name)
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        Write-Host "    shot: $path"
    } catch {}
}

function Stop-RigClients {
    Get-Process | Where-Object { $_.ProcessName -match 'MortalShell2|CrashReportClient' -and ($_.Path -like 'G:\Games\_MS2-new-*' -or -not $_.Path) } | ForEach-Object {
        try { Stop-Process -Id $_.Id -Force } catch {}
    }
    Start-Sleep 2
}

function Restore-Saves($sess) {
    if ($sess -and $sess.backupDir -and (Test-Path $sess.backupDir)) {
        if (-not $sess.hadWs -and (Test-Path (Join-Path $SaveDir 'WorldState_0.sav'))) {
            Remove-Item (Join-Path $SaveDir 'WorldState_0.sav') -Force -ErrorAction SilentlyContinue
        }
        Copy-Item (Join-Path $sess.backupDir '*') $SaveDir -Recurse -Force
        Write-Host "Restored owner saves from $($sess.backupDir)"
    }
    foreach ($c in @($CoopA, $CoopB)) {
        Remove-Item (Join-Path $c 'AUTO_CONTINUE.txt') -Force -ErrorAction SilentlyContinue
    }
}

function Prep-Coop([string]$coop, [string]$id, [string]$role) {
    New-Item -ItemType Directory -Force -Path $coop | Out-Null
    foreach ($pat in 'out_*.txt', 'peer_*.txt', 'ws_ledger_*.txt') {
        Get-ChildItem $coop -Filter $pat -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    }
    foreach ($f in 'probe.log', 'sync.txt', 'cmd.txt') {
        Remove-Item (Join-Path $coop $f) -Force -ErrorAction SilentlyContinue
    }
    [IO.File]::WriteAllText((Join-Path $coop 'playerid.txt'), $id)
    [IO.File]::WriteAllText((Join-Path $coop 'role.txt'), $role)
    [IO.File]::WriteAllText((Join-Path $coop 'AUTO_CONTINUE.txt'), "1")
}

function Start-Client([string]$gameDir) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = Join-Path $gameDir 'MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe'
    $psi.Arguments = "MortalShell2 -windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
    $psi.WorkingDirectory = $gameDir
    $psi.UseShellExecute = $true
    return [System.Diagnostics.Process]::Start($psi)
}

function Wait-StreamActor([string]$coop, [string]$game, [string]$sweepPat, [int]$maxSec = 25) {
    $dl = (Get-Date).AddSeconds($maxSec)
    while ((Get-Date) -lt $dl) {
        $off = LogLen $game
        Send-Cmd $coop "areasweep $sweepPat" 1
        $hits = GrabSince $game $off 'areasweep: \[\d+\]'
        if ($hits.Count -gt 0) { return $hits }
        Start-Sleep 1
    }
    return @()
}

function Start-Rig {
    $existing = Load-Session
    if (Clients-Alive $existing) {
        Write-Host "Rig already running (PIDs A=$($existing.pidA), B=$($existing.pidB), out=$($existing.outDir))"
        return $existing
    }
    if ($existing) { Restore-Saves $existing; Remove-Item $SessionFile -Force -ErrorAction SilentlyContinue }

    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backupDir = Join-Path $RepoRoot "local\snapshots\saves-$stamp-$Tag"
    $outDir = Join-Path $RepoRoot "local\lab\$stamp-$Tag"
    New-Item -ItemType Directory -Force -Path $backupDir, $outDir | Out-Null

    Stop-RigClients
    $hadWs = Test-Path (Join-Path $SaveDir 'WorldState_0.sav')
    if (Test-Path $SaveDir) { Copy-Item (Join-Path $SaveDir '*') $backupDir -Recurse -Force }
    $saveFile = Join-Path $SavesSource ("Savegame #{0}.sav" -f $SaveNum)
    if (-not (Test-Path $saveFile)) { throw "Save file not found: $saveFile" }
    Copy-Item $saveFile (Join-Path $SaveDir 'WorldState_0.sav') -Force
    Write-Host "Backed up owner saves -> $backupDir ; seeded Savegame #$SaveNum"

    $sess = [ordered]@{
        stamp     = $stamp
        tag       = $Tag
        saveNum   = $SaveNum
        lanPort   = $LanPort
        backupDir = $backupDir
        hadWs     = $hadWs
        outDir    = $outDir
        pidA      = 0
        pidB      = 0
    }
    ($sess | ConvertTo-Json) | Set-Content $SessionFile

    try {
        Prep-Coop $CoopA 'A' 'host'
        Prep-Coop $CoopB 'B' 'guest'
        $t0 = Get-Date
        $pA = Start-Client $GameA
        $sess.pidA = $pA.Id
        ($sess | ConvertTo-Json) | Set-Content $SessionFile

        $dlA = (Get-Date).AddSeconds($BootWait + 60)
        while ((Get-Date) -lt $dlA) {
            if ($pA.HasExited) { throw "Host A exited during boot (code $($pA.ExitCode))" }
            if ((SyncVal $CoopA 'ready') -eq '1') { break }
            Start-Sleep 2
        }
        if ((SyncVal $CoopA 'ready') -ne '1') { throw "Host A boot timeout" }
        Write-Host ("Host A ready in {0:F0}s; waiting 8s before Guest B..." -f ((Get-Date) - $t0).TotalSeconds)
        Start-Sleep 8

        $pB = Start-Client $GameB
        $sess.pidB = $pB.Id
        ($sess | ConvertTo-Json) | Set-Content $SessionFile

        $dlB = (Get-Date).AddSeconds($BootWait + 90)
        while ((Get-Date) -lt $dlB) {
            if ($pA.HasExited -or $pB.HasExited) { throw "Client exited during boot (A=$($pA.HasExited) B=$($pB.HasExited))" }
            if ((SyncVal $CoopB 'ready') -eq '1') { break }
            Start-Sleep 2
        }
        if ((SyncVal $CoopB 'ready') -ne '1') { throw "Guest B boot timeout" }

        Send-Cmd $CoopA "lanhost $LanPort" 1
        Send-Cmd $CoopB "lanjoin 127.0.0.1 $LanPort" 3
        $dl = (Get-Date).AddSeconds(35)
        while ((Get-Date) -lt $dl) {
            if ((SyncVal $CoopA 'peerready') -eq '1' -and (SyncVal $CoopB 'peerready') -eq '1' -and (SyncVal $CoopA 'phase') -eq 'sync') { break }
            Start-Sleep 1
        }
        Send-Both "labgod 1" 0
        Send-Both "labhp 5000" 1
        Write-Host ("RIG READY in {0:F0}s | A phase={1} peer={2} | B phase={3} peer={4}" -f `
            ((Get-Date) - $t0).TotalSeconds, (SyncVal $CoopA 'phase'), (SyncVal $CoopA 'peerready'), (SyncVal $CoopB 'phase'), (SyncVal $CoopB 'peerready'))
        return (Load-Session)
    } catch {
        Write-Host "Boot failed: $($_.Exception.Message) -- cleaning up & restoring saves"
        Stop-RigClients
        Restore-Saves $sess
        Remove-Item $SessionFile -Force -ErrorAction SilentlyContinue
        throw
    }
}

function Stop-Rig {
    $sess = Load-Session
    Stop-RigClients
    if ($sess) {
        try {
            if (Test-Path (NLog $GameA)) { Copy-Item (NLog $GameA) (Join-Path $sess.outDir 'native-A.log') -Force }
            if (Test-Path (NLog $GameB)) { Copy-Item (NLog $GameB) (Join-Path $sess.outDir 'native-B.log') -Force }
            if (Test-Path (Join-Path $CoopA 'sync.txt')) { Copy-Item (Join-Path $CoopA 'sync.txt') (Join-Path $sess.outDir 'sync-A.txt') -Force }
            if (Test-Path (Join-Path $CoopB 'sync.txt')) { Copy-Item (Join-Path $CoopB 'sync.txt') (Join-Path $sess.outDir 'sync-B.txt') -Force }
        } catch {}
        Restore-Saves $sess
        Remove-Item $SessionFile -Force -ErrorAction SilentlyContinue
        Write-Host "Rig stopped. Logs archived in $($sess.outDir)"
    } else {
        foreach ($c in @($CoopA, $CoopB)) { Remove-Item (Join-Path $c 'AUTO_CONTINUE.txt') -Force -ErrorAction SilentlyContinue }
        Write-Host "No active rig session file; ensured clients stopped and AUTO_CONTINUE removed."
    }
}

function Show-Status {
    $sess = Load-Session
    $alive = Clients-Alive $sess
    Write-Host ("Session: alive={0} pidA={1} pidB={2} save=#{3}" -f $alive, $(if($sess){$sess.pidA}else{'-'}), $(if($sess){$sess.pidB}else{'-'}), $(if($sess){$sess.saveNum}else{'-'}))
    foreach ($pair in @(@('A', $CoopA), @('B', $CoopB))) {
        $lbl = $pair[0]; $c = $pair[1]
        Write-Host ("  [{0}] phase={1} ready={2} peer={3} pos={4}" -f $lbl, (SyncVal $c 'phase'), (SyncVal $c 'ready'), (SyncVal $c 'peerready'), (SyncVal $c 'selfpos'))
        Write-Host ("       bscale={0} | hithook={1} | dlg={2}" -f (SyncVal $c 'bscale'), (SyncVal $c 'hithook'), (SyncVal $c 'dlg'))
        Write-Host ("       partb={0} | ws={1} | wp={2} | fx={3}" -f (SyncVal $c 'partb'), (SyncVal $c 'ws'), (SyncVal $c 'wp'), (SyncVal $c 'fx'))
    }
}

# ============================================================================
# REAL-GAMEPLAY SUITES (executed on the already-running hot pair)
# ============================================================================
function Run-Suite-Atlas($sess) {
    Write-Host "`n================ SUITE: ATLAS ================"
    $atlas = [ordered]@{ timestamp = (Get-Date -Format 's'); categories = [ordered]@{} }
    $cats = [ordered]@{
        beacons   = "BP_LandingArea"
        tombs     = "LevelInstanceSelector"
        bossgates = "BossGate"
        bosses    = "Boss"
        npcs      = "BP_NPC_"
        chests    = "Chest"
        lifts     = "Lift Elevator MovingObject Drawbridge"
        puzzles   = "Puzzle PressurePlate Bell Lantern Illusory LightCounter"
        doors     = "HubDoor DoorLocked TarGate"
    }
    foreach ($k in $cats.Keys) {
        $off = LogLen $GameA
        Send-Cmd $CoopA ("areasweep " + $cats[$k]) 1
        $lines = GrabSince $GameA $off 'areasweep: '
        $atlas.categories[$k] = @($lines)
        $hitCount = ($lines | Where-Object { $_ -match 'areasweep: \[\d+\]' }).Count
        Write-Host ("  {0,-10}: {1} actors" -f $k, $hitCount)
    }
    $offL = LogLen $GameA
    Send-Cmd $CoopA "arealayers" 1
    $atlas.layers = @(GrabSince $GameA $offL 'arealayers: ')
    Write-Host ("  layers    : {0} lines" -f $atlas.layers.Count)

    $offT = LogLen $GameA
    Send-Cmd $CoopA "areatomb" 1
    $atlas.tombDetails = @(GrabSince $GameA $offT 'areatomb: ')
    Write-Host ("  tombData  : {0} lines" -f $atlas.tombDetails.Count)

    ($atlas | ConvertTo-Json -Depth 4) | Set-Content $AtlasFile
    Write-Host "  Saved atlas -> $AtlasFile"
}

function Run-Suite-ShellsLook($sess) {
    Write-Host "`n================ SUITE: SHELLS & LOOK (real equipment) ================"
    $offA = LogLen $GameA; $offB = LogLen $GameB
    Send-Cmd $CoopA "equip dump" 1
    Send-Cmd $CoopB "equip dump" 1
    $dA = GrabSince $GameA $offA 'equip: '
    Write-Host "  A initial equip: $($dA.Count) lines"
    $dA | Select-Object -First 6 | ForEach-Object { Write-Host "    $_" }

    # Switch A to next shell + next skin + next mask, B to a different shell
    $offA = LogLen $GameA; $offB = LogLen $GameB
    Send-Cmd $CoopA "equip shell next" 3
    Send-Cmd $CoopA "equip skin next" 2
    Send-Cmd $CoopA "equip mask next" 2
    Send-Cmd $CoopB "equip shell next" 2
    Send-Cmd $CoopB "equip shell next" 3
    Start-Sleep 3
    Send-Both "pupcheck" 1
    Send-Both "bodydump" 1
    $chkA = GrabSince $GameA $offA 'equip: |pupcheck:|bodydump:|body:'
    $chkB = GrabSince $GameB $offB 'equip: |pupcheck:|bodydump:|body:'
    Write-Host "  A equip/puppet:"
    $chkA | Select-Object -Last 10 | ForEach-Object { Write-Host "    A: $_" }
    Write-Host "  B equip/puppet:"
    $chkB | Select-Object -Last 10 | ForEach-Object { Write-Host "    B: $_" }
    Shot $sess.pidA $sess.outDir 'shells-A'
    Shot $sess.pidB $sess.outDir 'shells-B'

    # Dark Form on Host A
    $offA = LogLen $GameA; $offB = LogLen $GameB
    Send-Cmd $CoopA "equip darkform" 4
    Send-Cmd $CoopB "pupcheck" 1
    Write-Host "  DarkForm check:"
    GrabSince $GameA $offA 'equip: |dark' | Select-Object -Last 5 | ForEach-Object { Write-Host "    A: $_" }
    GrabSince $GameB $offB 'pupcheck:|body:|look:' | Select-Object -Last 5 | ForEach-Object { Write-Host "    B: $_" }
}

function Run-Suite-DlgVote($sess) {
    Write-Host "`n================ SUITE: REAL DIALOGUE + VOTING ================"
    $offA = LogLen $GameA; $offB = LogLen $GameB
    Send-Cmd $CoopA "areasweep BP_NPC_ShellKeeper" 1
    Send-Cmd $CoopA "areago nearest" 2
    Send-Cmd $CoopA "interdump BP_NPC_ShellKeeper go" 3
    for ($w = 0; $w -lt 8; $w++) {
        $o = LogLen $GameA
        Send-Cmd $CoopA "interdump BP_NPC_ShellKeeper" 1
        if ((GrabSince $GameA $o 'focus=1').Count -gt 0) { break }
    }
    Send-Cmd $CoopA "inject IA_Interact_Pressed 0.15" 4
    if (-not ((SyncVal $CoopA 'dlg') -match '^me:1')) {
        Send-Cmd $CoopA "interdump BP_NPC_ShellKeeper call IA_Interact_Pressed" 4
    }
    Write-Host ("  Opened? A dlg={0} | B dlg={1}" -f (SyncVal $CoopA 'dlg'), (SyncVal $CoopB 'dlg'))
    if ((SyncVal $CoopA 'dlg') -match '^me:1') {
        Send-Both "dlgdump" 1
        GrabSince $GameA $offA 'dlgdump:|dlg:|dialog:' | Select-Object -Last 6 | ForEach-Object { Write-Host "    A: $_" }
        GrabSince $GameB $offB 'dlgdump:|dlg:|dialog:|listen' | Select-Object -Last 6 | ForEach-Object { Write-Host "    B: $_" }
        # Step through linear speech nodes (options=1) until we hit a real choice node (options>=2)
        $hitChoice = $false
        for ($step = 0; $step -lt 8; $step++) {
            $dA = SyncVal $CoopA 'dlg'
            if (-not ($dA -match '^me:1')) { break }
            $oD = LogLen $GameA
            Send-Cmd $CoopA "dlgdump" 1
            $dl = (GrabSince $GameA $oD 'options=(\d+)') -join ' '
            if ($dl -match 'options=(\d+)' -and [int]$Matches[1] -ge 2) {
                $hitChoice = $true
                Write-Host "  Reached real choice node with $($Matches[1]) options!"
                $offV = LogLen $GameA; $offVB = LogLen $GameB
                Send-Cmd $CoopB "dlgvote 1" 1
                Send-Cmd $CoopA "dlgvote 1" 3
                GrabSince $GameA $offV 'vote|resolve|ChooseOption|dlg:|PlayerChoice' | Select-Object -Last 8 | ForEach-Object { Write-Host "    A(vote): $_" }
                GrabSince $GameB $offVB 'vote|resolve|dlg:|listen|PlayerChoice' | Select-Object -Last 8 | ForEach-Object { Write-Host "    B(vote): $_" }
                break
            }
            Send-Cmd $CoopA "dlgchoice 0" 2
        }
        if (-not $hitChoice) { Write-Host "  Note: dialogue ended/linear (no >=2 option node in first 8 steps)" }
        Send-Cmd $CoopA "dlgstop" 2
    }
}

function Run-Suite-WorldInteract($sess) {
    Write-Host "`n================ SUITE: REAL WORLD INTERACTIONS (Chest / Door / Lift / Breakable) ================"
    # 1. Chest
    $offA = LogLen $GameA; $offB = LogLen $GameB
    Send-Cmd $CoopA "areasweep BP_Interactable_Chest" 1
    $chits = GrabSince $GameA $offA 'areasweep: \[\d+\]'
    Write-Host "  Chests nearby: $($chits.Count)"
    if ($chits.Count -gt 0) {
        Send-Cmd $CoopA "areago nearest" 2
        $pos = SyncVal $CoopA 'selfpos'
        if ($pos) { Send-Cmd $CoopB "tp $pos" 2 }
        Send-Cmd $CoopA "interdump Chest go" 2
        Send-Cmd $CoopA "inject IA_Interact_Pressed 0.15" 4
        Write-Host ("  After chest interact: A chest={0} | B chest={1}" -f (SyncVal $CoopA 'chest'), (SyncVal $CoopB 'chest'))
        GrabSince $GameB $offB 'chest:|grant|CS' | Select-Object -Last 8 | ForEach-Object { Write-Host "    B: $_" }
    }
    # 2. Lift
    $offA = LogLen $GameA; $offB = LogLen $GameB
    Send-Cmd $CoopA "areasweep BP_HubLift" 1
    $lhits = GrabSince $GameA $offA 'areasweep: \[\d+\]'
    Write-Host "  Lifts nearby: $($lhits.Count)"
    if ($lhits.Count -gt 0) {
        Send-Cmd $CoopA "areago nearest" 2
        $pos = SyncVal $CoopA 'selfpos'
        if ($pos) { Send-Cmd $CoopB "tp $pos" 2 }
        Send-Cmd $CoopA "interdump Lift go" 2
        Send-Cmd $CoopA "inject IA_Interact_Pressed 0.15" 4
        Write-Host ("  After lift interact: A lift={0} | B lift={1}" -f (SyncVal $CoopA 'lift'), (SyncVal $CoopB 'lift'))
        GrabSince $GameB $offB 'lift:|LF' | Select-Object -Last 6 | ForEach-Object { Write-Host "    B: $_" }
    }
    # 3. Breakables
    $offA = LogLen $GameA; $offB = LogLen $GameB
    Send-Cmd $CoopA "breakdump" 1
    GrabSince $GameA $offA 'break' | Select-Object -Last 5 | ForEach-Object { Write-Host "    A(brk): $_" }
}

function Run-Suite-CombatFx($sess) {
    Write-Host "`n================ SUITE: COMBAT, SCALING, FX & HITSTUN ================"
    $offA = LogLen $GameA; $offB = LogLen $GameB
    Write-Host ("  Scaling: A={0} | B={1}" -f (SyncVal $CoopA 'bscale'), (SyncVal $CoopB 'bscale'))
    Send-Cmd $CoopA "enemytp" 2
    $pos = SyncVal $CoopA 'selfpos'
    if ($pos) { Send-Cmd $CoopB "tp $pos" 2 }
    Send-Cmd $CoopB "enemytp" 2
    # Test Harden / Guard / Parry / Attack / Special inputs on A & B
    Send-Cmd $CoopA "inject IA_Harden 0.5" 1
    Send-Cmd $CoopB "inject IA_LockOn 0.1" 0
    Start-Sleep -Milliseconds 500
    for ($i = 0; $i -lt 3; $i++) { Send-Cmd $CoopB "inject IA_Attack_Primary 0.12" 0; Start-Sleep -Milliseconds 850 }
    Send-Cmd $CoopB "inject IA_LockOn 0.1" 1
    Write-Host ("  HitHook: A={0} | B={1}" -f (SyncVal $CoopA 'hithook'), (SyncVal $CoopB 'hithook'))
    Write-Host ("  FX/Grabs: A={0} | B={1}" -f (SyncVal $CoopA 'fx_grabs'), (SyncVal $CoopB 'fx_grabs'))
    GrabSince $GameA $offA 'hithook:|fx_grabs:|prj|vfx|grb' | Select-Object -Last 10 | ForEach-Object { Write-Host "    A: $_" }
    GrabSince $GameB $offB 'hithook:|fx_grabs:|prj|vfx|grb' | Select-Object -Last 10 | ForEach-Object { Write-Host "    B: $_" }
}

function Run-Suite-Dungeon($sess) {
    Write-Host "`n================ SUITE: LEVEL INSTANCE / DUNGEON VARIANT SYNC ================"
    $offA = LogLen $GameA; $offB = LogLen $GameB
    # Teleport near BP_LevelInstanceSelector_Proximity_C from atlas
    Send-Both "tp 38335 -168901 -44160" 12
    Write-Host ("  partb: A={0} | B={1}" -f (SyncVal $CoopA 'partb'), (SyncVal $CoopB 'partb'))
    GrabSince $GameB $offB 'partb: synced dungeon|LoadLevel' | Select-Object -Last 6 | ForEach-Object { Write-Host "    B: $_" }
}

function Run-Suite-HardenNgp($sess) {
    Write-Host "`n================ SUITE: HARDEN FREEZE/UNFREEZE + BREAKABLE + BOSS ARENA PULL ================"
    # 1. Harden freeze & unfreeze using exact Enhanced Input actions
    $offB = LogLen $GameB
    Send-Cmd $CoopA "inject IA_Action_StoneForm_Press 0.1" 0
    Send-Cmd $CoopA "inject IA_Action_StoneForm_Hold 1.8" 2
    Send-Cmd $CoopA "inject IA_Action_StoneForm_Release 0.1" 2
    Write-Host ("  After Harden: A partb={0} | B partb={1}" -f (SyncVal $CoopA 'partb'), (SyncVal $CoopB 'partb'))
    GrabSince $GameB $offB 'partb: puppet Harden' | ForEach-Object { Write-Host "    B: $_" }

    # 2. Real breakable destruction (CartJars near hub)
    $offBrkA = LogLen $GameA; $offBrkB = LogLen $GameB
    Send-Both "tp 42398 -136790 -41900" 3
    Send-Cmd $CoopA "inject IA_Attack_Primary 0.12" 1
    Send-Cmd $CoopA "inject IA_Attack_Primary 0.12" 2
    Send-Both "breakdump" 1
    Write-Host ("  Breakables: A={0} | B={1}" -f (SyncVal $CoopA 'brk'), (SyncVal $CoopB 'brk'))
    GrabSince $GameB $offBrkB 'brk:|break' | Select-Object -Last 6 | ForEach-Object { Write-Host "    B(brk): $_" }

    # 3. Boss Arena Partner Pull (Host A enters BP_DwarfBoss_C, Guest B stands 2500 uu outside)
    Write-Host "`n  --- Testing Boss Arena Partner Pull at BP_DwarfBoss_C ---"
    $offPullB = LogLen $GameB
    Send-Cmd $CoopA "tp 246900 -537918 -61918" 0
    Send-Cmd $CoopB "tp 249400 -537918 -61918" 35
    $posB0 = SyncVal $CoopB 'selfpos'
    Write-Host "  Guest B before boss aggro: pos=$posB0"
    Send-Cmd $CoopA "areasweep BP_CinematicPlayer" 1
    Send-Cmd $CoopA "areacomp 0 TriggerArea go" 40
    Send-Cmd $CoopA "bossdump" 1
    $posB1 = SyncVal $CoopB 'selfpos'
    Write-Host "  Host A boss: $((GrabLast $GameA 'bossdump: BP_' 1) -join '')"
    Write-Host "  Guest B after boss aggro:  pos=$posB1 | partb=$(SyncVal $CoopB 'partb')"
    GrabSince $GameB $offPullB 'partb: pulled local player' | ForEach-Object { Write-Host "    B(pull): $_" }
}

function Run-Suite-BossTour($sess) {
    Write-Host "`n================ SUITE: MULTI-BOSS TOUR + ARENA PULL + PHASE CHECK ================"
    # All 6 boss landing areas + FinalBoss (Zmey) discovered by atlas on Savegame #5
    $targets = @(
        @{ name = "BossArea0";  pos = "-85116 -458831 -53493" },
        @{ name = "BossArea1";  pos = "45252 -536896 -61907" },
        @{ name = "BossArea3";  pos = "88863 385696 -61579" },
        @{ name = "BossArea4";  pos = "-48933 381739 -61670" },
        @{ name = "BossArea5";  pos = "221179 383226 -61941" },
        @{ name = "FinalBoss";  pos = "-845308 -100377 -26035" }
    )
    foreach ($t in $targets) {
        if (-not (Clients-Alive $sess)) { throw "Client died before $($t.name)" }
        Write-Host "`n  --- Visiting $($t.name) at ($($t.pos)) ---"
        $offA = LogLen $GameA; $offB = LogLen $GameB
        Send-Both ("tp " + $t.pos) 2
        # Smart streaming poll: wait up to 22s until a boss or cinematic player appears
        $found = $false
        for ($w = 0; $w -lt 11; $w++) {
            $o = LogLen $GameA
            Send-Cmd $CoopA "bossdump" 1
            $bl = GrabSince $GameA $o 'bossdump: BP_'
            if ($bl.Count -gt 0) {
                $found = $true
                Send-Cmd $CoopB "bossdump" 1
                Write-Host ("    A: " + ($bl -join ' | '))
                Write-Host ("    B: " + ((GrabLast $GameB 'bossdump: BP_' 2) -join ' | '))
                break
            }
            Start-Sleep 1
        }
        if (-not $found) {
            # Sweep for boss/trigger actors in this cell so we know what lives here
            $oS = LogLen $GameA
            Send-Cmd $CoopA "areasweep Boss Trigger Cinematic" 1
            $sh = GrabSince $GameA $oS 'areasweep: class'
            Write-Host ("    No BPC_BossAI_C yet; cell classes: " + ($sh -join '; '))
        }
    }
}

function Run-Suite-Quests($sess) {
    Write-Host "`n================ SUITE: ALL 6 DT_QuestData1 QUESTS SYNC GAUNTLET ================"
    $quests = @('TestQuest', 'LocationQuest', 'AutoQuest', 'VlasQuest', 'GorfQuest', 'TielsQuest')
    $pass = 0; $fail = 0
    foreach ($q in $quests) {
        $offA = LogLen $GameA; $offB = LogLen $GameB
        Send-Cmd $CoopA "qsid $q" 2
        Send-Cmd $CoopA "qnext $q" 3
        $oQA = LogLen $GameA; $oQB = LogLen $GameB
        Send-Cmd $CoopA "qstage $q" 1
        Send-Cmd $CoopB "qstage $q" 1
        $sA = (GrabSince $GameA $oQA "qstage $q ") -join ' | '
        $sB = (GrabSince $GameB $oQB "qstage $q ") -join ' | '
        $stA = if ($sA -match 'stage=(\d+/\d+)') { $Matches[1] } else { '?' }
        $stB = if ($sB -match 'stage=(\d+/\d+)') { $Matches[1] } else { '?' }
        $ok = ($stA -ne '?' -and $stA -eq $stB)
        if ($ok) { ++$pass } else { ++$fail }
        Write-Host ("  [{0}] {1,-14} | stage A={2} B={3}" -f $(if($ok){'PASS'}else{'FAIL'}), $q, $stA, $stB)
    }
    Write-Host ("  Quests summary: {0}/{1} PASS | wp A={2} | B={3}" -f $pass, $quests.Count, (SyncVal $CoopA 'wp'), (SyncVal $CoopB 'wp'))
}

function Run-Suite-BossGauntlet($sess) {
    Write-Host "`n================ SUITE: 34-BOSS REAL SPAWN / PHASE / KILL GAUNTLET ================"
    $bossPaths = @(
        "/Game/Sparta/Characters/Bosses/Dwarf/BP_DwarfBoss.BP_DwarfBoss_C",
        "/Game/Sparta/Characters/Bosses/FinalBoss/BP_FinalBoss.BP_FinalBoss_C",
        "/Game/Sparta/Characters/Bosses/HeadBoss/BP_HeadBoss.BP_HeadBoss_C",
        "/Game/Sparta/Characters/Bosses/Hexapod/BP_Hexapod.BP_Hexapod_C",
        "/Game/Sparta/Characters/Bosses/LadyOfTheWoods/BP_LadyOfTheWoods.BP_LadyOfTheWoods_C",
        "/Game/Sparta/Characters/Bosses/Monolith/AI/BP_Monolith.BP_Monolith_C",
        "/Game/Sparta/Characters/Bosses/MothKnight/AI/BP_MothKnight.BP_MothKnight_C",
        "/Game/Sparta/Characters/Bosses/Offspring/BP_Offspring.BP_Offspring_C",
        "/Game/Sparta/Characters/Bosses/ParasiteGolem/BP_ParasiteGolem.BP_ParasiteGolem_C",
        "/Game/Sparta/Characters/Bosses/Swordman/BP_Swordman.BP_Swordman_C",
        "/Game/Sparta/Characters/Bosses/TarGolem/BP_TarGolem.BP_TarGolem_C",
        "/Game/Sparta/Characters/Bosses/TarGolem/BP_TarGolem_Headless.BP_TarGolem_Headless_C",
        "/Game/Sparta/Characters/Bosses/BigBottle/BP_BigBottle_Ghost.BP_BigBottle_Ghost_C",
        "/Game/Sparta/Characters/Enemies/Aristocrat/BP_LordVellen_MB.BP_LordVellen_MB_C",
        "/Game/Sparta/Characters/Enemies/Aristocrat/BP_SicarioTheKeeper.BP_SicarioTheKeeper_C",
        "/Game/Sparta/Characters/Enemies/BallistaHead/BP_BallistaHead_Miniboss.BP_BallistaHead_Miniboss_C",
        "/Game/Sparta/Characters/Enemies/Batushka/BP_Batushka_MiniBoss.BP_Batushka_MiniBoss_C",
        "/Game/Sparta/Characters/Enemies/Brigands/BrigElite/BrigElite_Fire/BP_Brigand_Elite_Fire_MiniBoss.BP_Brigand_Elite_Fire_MiniBoss_C",
        "/Game/Sparta/Characters/Enemies/Brigands/BrigElite/BrigElite_Winter/BP_Brigand_Elite_Winter_MiniBoss.BP_Brigand_Elite_Winter_MiniBoss_C",
        "/Game/Sparta/Characters/Enemies/CannibalKnight/BP_Cannibal_Knight_MiniBoss.BP_Cannibal_Knight_MiniBoss_C",
        "/Game/Sparta/Characters/Enemies/HeadTree/BP_HeadTree.BP_HeadTree_C",
        "/Game/Sparta/Characters/Enemies/HutchbackCarrier/BP_Hutchback_Miniboss.BP_Hutchback_Miniboss_C",
        "/Game/Sparta/Characters/Enemies/Miner/BP_MinerKing.BP_MinerKing_C",
        "/Game/Sparta/Characters/Enemies/MS1_HeavyCultist/BP_HeavyCultist_MiniBoss.BP_HeavyCultist_MiniBoss_C",
        "/Game/Sparta/Characters/Enemies/MS1_TwinSisters/SpearDagger/BP_TwinSister_SpearDagger_Miniboss.BP_TwinSister_SpearDagger_Miniboss_C",
        "/Game/Sparta/Characters/Enemies/Shepherd/BP_Shepherd.BP_Shepherd_C",
        "/Game/Sparta/Characters/Enemies/Sicario/BP_Sicario_MiniBoss.BP_Sicario_MiniBoss_C",
        "/Game/Sparta/Characters/Enemies/StoneCrab/BP_StoneCrab_MB.BP_StoneCrab_MB_C",
        "/Game/Sparta/Characters/Enemies/TarredCorpse/BP_TarredCorpse_MinibossNecromancer.BP_TarredCorpse_MinibossNecromancer_C",
        "/Game/Sparta/Characters/Enemies/TarredVestige/BP_TarredVestige_MiniBoss.BP_TarredVestige_MiniBoss_C",
        "/Game/Sparta/Characters/Enemies/TheHead/BP_TheHeadKing.BP_TheHeadKing_C",
        "/Game/Sparta/Characters/Enemies/Vampire/BP_VampireKnight.BP_VampireKnight_C",
        "/Game/Sparta/Characters/Enemies/Vampire/BP_VampireKnight_Part2.BP_VampireKnight_Part2_C",
        "/Game/Sparta/Characters/Enemies/Wraith/BP_Wraith_MiniBoss.BP_Wraith_MiniBoss_C"
    )
    $passed = 0; $failed = 0; $results = @()
    # Move both players to a wide-open flat courtyard so even giant bosses (FinalBoss/HeadBoss/Offspring)
    # pass UE5 summon capsule checks on BOTH Host A and Guest B.
    Write-Host "  Moving both players to open arena courtyard (246900 -537918 -61918)..."
    Send-Both "tp 246900 -537918 -61918" 28
    Send-Both "bossdespawn" 2
    foreach ($bp in $bossPaths) {
        if (-not (Clients-Alive $sess)) { throw "Client died during boss gauntlet at $bp" }
        $short = ($bp -split '\.')[-1]
        $offA = LogLen $GameA; $offB = LogLen $GameB
        Send-Both "bossspawn $bp" 4
        $spawnA = (GrabSince $GameA $offA 'bossphase spawn: live') -join ' '
        $spawnB = (GrabSince $GameB $offB 'bossphase spawn: live') -join ' '
        $hasBossComp = ($spawnA -match 'live BP_' -and $spawnA -notmatch 'live-nonboss')
        $p2Ok = $true; $p3Ok = $true
        $t2 = 0.0; $t3 = 0.0
        if ($spawnA -match 't2=([0-9.]+)') { $t2 = [double]$Matches[1] }
        if ($spawnA -match 't3=([0-9.]+)') { $t3 = [double]$Matches[1] }
        if ($hasBossComp) {
            if ($t2 -gt 0.01) {
                $oP = LogLen $GameB
                Send-Cmd $CoopA "bossphase force 2" 2
                $p2Ok = ((GrabSince $GameB $oP 'APPLIED phase 2').Count -gt 0)
            }
            if ($t3 -gt 0.01) {
                $oP3 = LogLen $GameB
                Send-Cmd $CoopA "bossphase force 3" 2
                $p3Ok = ((GrabSince $GameB $oP3 'APPLIED phase 3').Count -gt 0)
            }
        }
        # Kill boss on Host A -> verify Event.Death / StartDeath / drops sync on Guest B
        $killB = $false
        if ($spawnA -match 'live') {
            $oK = LogLen $GameB
            Send-Cmd $CoopA "kill $short" 3
            $killB = ((GrabSince $GameB $oK 'StartDeath:|Event\.Death|killTail .*ok=true|drops: granted').Count -gt 0)
        }
        Send-Both "bossdespawn" 2
        $ok = ($spawnA -match 'live' -and $spawnB -match 'live' -and $p2Ok -and $p3Ok)
        if ($ok) { ++$passed } else { ++$failed }
        $line = ("  [{0}] {1,-38} | boss={2} t2={3:F2}({4}) t3={5:F2}({6}) killSync={7}" -f $(if($ok){'PASS'}else{'FAIL'}), $short, $(if($hasBossComp){1}else{0}), $t2, $p2Ok, $t3, $p3Ok, $killB)
        Write-Host $line
        $results += $line
    }
    Write-Host ("  BOSS GAUNTLET SUMMARY: {0}/{1} PASS, {2} FAIL" -f $passed, $bossPaths.Count, $failed)
}

# ============================================================================
# DISPATCH
# ============================================================================
switch ($Action) {
    'start'  { Start-Rig | Out-Null }
    'stop'   { Stop-Rig }
    'status' { Show-Status }
    'cmd'    {
        $sess = Load-Session
        if (-not (Clients-Alive $sess)) { throw "Rig is not running. Run: .\rig.ps1 -Action start" }
        $offA = LogLen $GameA; $offB = LogLen $GameB
        if ($A) { Send-Cmd $CoopA $A $(if ($B) { 0 } else { $WaitSec }) }
        if ($B) { Send-Cmd $CoopB $B $WaitSec }
        if ($A) { GrabSince $GameA $offA $Pattern | ForEach-Object { Write-Host "  [A] $_" } }
        if ($B) { GrabSince $GameB $offB $Pattern | ForEach-Object { Write-Host "  [B] $_" } }
    }
    'suite'  {
        $sess = Start-Rig
        try {
            $flatSuites = @($Suites | ForEach-Object { $_ -split ',' } | Where-Object { $_ -ne '' })
            foreach ($s in $flatSuites) {
                if (-not (Clients-Alive $sess)) { throw "A client died before suite '$s'" }
                switch ($s) {
                    'atlas'          { Run-Suite-Atlas $sess }
                    'shells_look'    { Run-Suite-ShellsLook $sess }
                    'dlg_vote'       { Run-Suite-DlgVote $sess }
                    'world_interact' { Run-Suite-WorldInteract $sess }
                    'combat_fx'      { Run-Suite-CombatFx $sess }
                    'dungeon'        { Run-Suite-Dungeon $sess }
                    'harden_ngp'     { Run-Suite-HardenNgp $sess }
                    'boss_tour'      { Run-Suite-BossTour $sess }
                    'boss_gauntlet'  { Run-Suite-BossGauntlet $sess }
                    'quests'         { Run-Suite-Quests $sess }
                    default          { Write-Host "Unknown suite: $s" }
                }
            }
        } finally {
            if ($AutoStop) { Stop-Rig }
        }
    }
}
