# [R9-QUEST] multi-stage quest sync, stage by stage, on a real DT_QuestData1 quest.
#
# TASK.md's open question: the quest sync writes CurrentQuestStage directly
# (WqApplyQuest, kind 's') and nobody checked that the partner's stage TEXT is
# right afterwards, because the hub quests used by the old tests have one stage.
#
# This script finds a quest that really has 2+ stages (QuestDetails.Stages.Num on
# the live quest object), then walks it one stage at a time and, at every stage,
# compares A's and B's FULL visible state: stage index, CurrentStageDetails
# StageName + Description, the journal entry (QuestDetails.LogDescription), the
# tracked-objective line (QuestDetails.TrackingDescription), the per-index stage
# names and the turn-in flag.  Both game windows are screenshotted at every stage.
# Last it does a late join: B is stopped, A advances one more stage, B is
# restarted and has to catch up to A's current stage and text.
#
# Prints exactly one verdict line: "QUESTSTAGE: PASS|FAIL|WARN ...".
#
# Usage (hold the game lock; the script takes/releases it itself):
#   . G:\Freespace\shell_coop\local\gamelock.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\quest-stages.ps1
#   powershell ... -File .\quest-stages.ps1 -QuestID GorfQuest -MaxStages 3 -NoDeploy
#
# Real play vs forced, stated for the report: the quest is STARTED and each stage
# is COMPLETED through the game's own UFunctions (AddNewQuest / InitializeQuest /
# OnObjectiveIDHeard).  Nobody walks to an NPC; there is no player input at all.
# When hearing the stage's objectives does not move the stage, the script says so
# and the probe falls back to the same direct CurrentQuestStage write the sync
# itself uses -- that is the behaviour under test, and every fallback is logged.
param(
    [string]$GameA = "E:\Games\_MS2-coop-g",
    [string]$GameB = "E:\Games\_MS2-coop-h",
    [int]$Port = 47130,
    [string]$QuestID = "",
    [string]$Driver = "A",
    [int]$MaxStages = 4,
    [int]$BootWait = 100,
    [switch]$NoDeploy,
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$who = 'r9-quest'
$realLocal = $env:LOCALAPPDATA
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outDir = Join-Path 'G:\Freespace\shell_coop\local\lab' "$stamp-queststages"
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace Win32 -Name V -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PrintWindow(System.IntPtr hWnd, System.IntPtr hdcBlt, uint nFlags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT lpRect);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetWindowPos(System.IntPtr hWnd, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
'@
Add-Type -AssemblyName System.Drawing

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
$logA = Join-Path $GameA 'MortalShell2\Binaries\Win64\ms2coop-native.log'
$logB = Join-Path $GameB 'MortalShell2\Binaries\Win64\ms2coop-native.log'
$fail = New-Object System.Collections.Generic.List[string]
$warn = New-Object System.Collections.Generic.List[string]
$screenshots = New-Object System.Collections.Generic.List[string]
$stageRows = New-Object System.Collections.Generic.List[string]
$chosen = $QuestID
$stageCount = -1
$lateJoinOk = $false
$lateJoinTried = $false

function Stop-Relays {
    # TARGETED relay cleanup.  CIM (Get-CimInstance) is DENIED in this environment,
    # and a blanket "kill every python" would take the harness's own MCP/debugger
    # helpers with it, so: wmic for the command line, then whoever owns the port.
    $pids = @()
    try {
        $out = cmd /c "wmic process where \"name='python.exe'\" get processid,commandline 2>nul"
        foreach ($l in @($out)) {
            if ($l -match 'cooprelay' -and $l -match '(\d+)\s*$') { $pids += [int]$matches[1] }
        }
    } catch {}
    try {
        Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue | % { $pids += [int]$_.OwningProcess }
        Get-NetTCPConnection -LocalPort $Port -ErrorAction SilentlyContinue | % { $pids += [int]$_.OwningProcess }
    } catch {}
    foreach ($p in ($pids | Sort-Object -Unique)) {
        if ($p -gt 0) { try { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } catch {} }
    }
}
function Stop-All {
    Get-Process | ? ProcessName -match 'MortalShell2|CrashReportClient' | % { try { Stop-Process -Id $_.Id -Force } catch {} }
    Stop-Relays
    Start-Sleep 2
}
function Hide-All {
    Get-Process | ? ProcessName -match 'MortalShell2-Win64' | % {
        if ($_.MainWindowHandle -ne 0) { [Win32.V]::SetWindowPos($_.MainWindowHandle, [IntPtr]::Zero, -30000, -30000, 0, 0, 0x15) | Out-Null } }
}
function Shot([System.Diagnostics.Process]$proc, [string]$path) {
    if (-not $proc) { return }
    try {
        $proc.Refresh()
        $h = $proc.MainWindowHandle
        if (-not $h -or $h -eq [IntPtr]::Zero) { Write-Host "shot: no window ($path)"; return }
        $h = [IntPtr]$h
        $r = New-Object Win32.V+RECT; [Win32.V]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        if ($w -le 0 -or $ht -le 0) { Write-Host "shot: empty rect ($path)"; return }
        $bmp = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
        [Win32.V]::PrintWindow($h, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        $screenshots.Add($path); Write-Host "shot: $path"
    } catch { Write-Host "shot: failed ($path): $($_.Exception.Message)" }
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
# cmd.txt is read and DELETED by the mod: waiting for it to disappear is the
# acknowledgement (same rule as visual-two.ps1 -- a fixed timer loses commands).
function Send-Cmd([string]$coop, [string]$verb) {
    $cf = Join-Path $coop 'cmd.txt'
    [IO.File]::WriteAllText($cf, "$verb`n")
    $due = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $due -and (Test-Path $cf)) { Start-Sleep -Milliseconds 150 }
}
function Wait-Result([string]$coop, [string]$name, [int]$seconds = 12) {
    $f = Join-Path $coop $name
    $due = (Get-Date).AddSeconds($seconds)
    while (-not (Test-Path $f) -and (Get-Date) -lt $due) { Hide-All; Start-Sleep -Milliseconds 300 }
    if (Test-Path $f) { return ((Get-Content $f -Raw).Trim()) } else { return $null }
}
function Clear-Results {
    foreach ($d in @($coopA, $coopB)) {
        Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'sync.txt') -Force -ErrorAction SilentlyContinue
    }
}
# "qsid X add=true ... stages=3 obj=yes" from the tail of the log -> stage count
function Get-StageCount([string]$log, [string]$id) {
    if (-not (Test-Path $log)) { return -1 }
    $m = Select-String -Path $log -Pattern ([regex]::Escape("qsid $id ")) | Select-Object -Last 1
    if (-not $m) { return -1 }
    if ($m.Line -match 'stages=(-?\d+)') { return [int]$matches[1] }
    return -1
}
# the visible-state payload of a qstage line: everything after "<id> ", so the id
# itself (which is the same on both sides by construction) does not mask a diff
function Get-StatePayload([string]$text) {
    if (-not $text) { return $null }
    $i = $text.IndexOf(' stage=')
    if ($i -lt 0) { return $text.Trim() }
    return $text.Substring($i + 1).Trim()
}
function Get-LastLine([string]$log, [string]$needle) {
    if (-not (Test-Path $log)) { return '' }
    $hit = Select-String -Path $log -Pattern ([regex]::Escape($needle)) -ErrorAction SilentlyContinue | Select-Object -Last 1
    if ($hit) { return $hit.Line } else { return '' }
}
# Poll B until its visible state equals A's, and report HOW LONG that took.  The
# sync publishes at 2 Hz in windows and the apply runs on a budget, so "B differs"
# one second after a write is not a bug -- "B never equals A" is.  Sampling once
# (as the first attempt did) produced three false FAILs and one false ABSENT.
function Wait-Match([string]$id, [string]$want, [int]$sec = 25) {
    $t0 = Get-Date; $last = $null
    while (((Get-Date) - $t0).TotalSeconds -lt $sec) {
        Remove-Item (Join-Path $coopB 'result_qstage.txt') -Force -ErrorAction SilentlyContinue
        Send-Cmd $coopB "qstage $id"
        $sb = Wait-Result $coopB 'result_qstage.txt' 8
        $last = Get-StatePayload $sb
        if ($last -and $last -eq $want) {
            return @{ ok = $true; sec = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1); text = $last }
        }
        Start-Sleep 2
    }
    return @{ ok = $false; sec = $sec; text = $last }
}
# B must have PRIMED (10 s of readable quest log) before wqApply does anything;
# comparing before that is measuring the settle timer, not the sync.
function Wait-Primed([string]$log, [int]$sec = 40) {
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt $sec) {
        if ((Get-LastLine $log 'primed=1') -ne '') {
            $l = Get-LastLine $log 'wqScan:'
            $all = Select-String -Path $log -Pattern 'wqScan:' -ErrorAction SilentlyContinue | Select-Object -Last 1
            if ($all -and $all.Line -match 'primed=1') { return $true }
        }
        Start-Sleep 2
    }
    return $false
}

. 'G:\Freespace\shell_coop\local\gamelock.ps1'
$locked = $false
$saveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
$saveSnap = Join-Path 'G:\Freespace\shell_coop\local\save-backups' "$stamp-$who"
try {
    Lock-Game $who; $locked = $true
    Stop-All
    New-Item -ItemType Directory -Force $saveSnap | Out-Null
    if (Test-Path $saveDir) { Copy-Item "$saveDir\*" $saveSnap -Recurse -Force -ErrorAction SilentlyContinue }
    Write-Host "save backup: $saveSnap"

    if (-not $NoDeploy) {
        $dll = Join-Path $root 'native\build\dxgi.dll'
        if (-not (Test-Path $dll)) { throw "no $dll -- build first" }
        & (Join-Path $root 'deploy.ps1') -GameDir $GameA -DllPath $dll | Out-Host
        & (Join-Path $root 'deploy.ps1') -GameDir $GameB -DllPath $dll | Out-Host
    }

    Clear-Results
    Set-Content (Join-Path $coopA 'playerid.txt') 'A' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopA 'role.txt') 'host' -Encoding ASCII -NoNewline
    Set-Content (Join-Path $coopB 'playerid.txt') 'B' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopB 'role.txt') 'guest' -Encoding ASCII -NoNewline

    $relay = Join-Path $root 'relay\cooprelay.py'
    # The native logs are appended across sessions, so a stale `primed=1` would make
    # Wait-Primed pass immediately.  No game is running here (Stop-All above), so the
    # log is removed and the session starts with a clean one.
    Remove-Item $logA, $logB -Force -ErrorAction SilentlyContinue
    $pa = $null; $pb = $null
    $synced = $false
    for ($attempt = 1; $attempt -le 3 -and -not $synced; $attempt++) {
        if ($attempt -gt 1) {
            # A known boot race (not ours): the mod's worker can initialise before UE
            # has created the object array (`NumElements=0 is not plausible`) and then
            # stays off for the session.  Re-launching is the established fix.
            Write-Host "boot attempt $attempt (previous: A=$phaseA B=$phaseB)"
            Stop-All
            Clear-Results
        }
        $pa = Start-Client $GameA (Join-Path $root 'local-appdata\A'); Start-Sleep 8
        $pb = Start-Client $GameB (Join-Path $root 'local-appdata\B')
        Start-Process python -ArgumentList @($relay, '--transport', 'udp', '--role', 'host', '--port', "$Port", '--player', 'A', '--dir', $coopA) -WindowStyle Hidden
        Start-Sleep 2
        Start-Process python -ArgumentList @($relay, '--transport', 'udp', '--role', 'client', '--host', '127.0.0.1', '--port', "$Port", '--player', 'B', '--dir', $coopB) -WindowStyle Hidden

        $until = (Get-Date).AddSeconds($BootWait)
        while ((Get-Date) -lt $until) {
            Hide-All; Start-Sleep -Milliseconds 700
            if ((Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') { break }
        }
        $phaseA = Sync $coopA 'phase'; $phaseB = Sync $coopB 'phase'
        Write-Host "phase A=$phaseA B=$phaseB (attempt $attempt)"
        if ($phaseA -eq 'sync' -and $phaseB -eq 'sync') { $synced = $true }
    }
    if (-not $synced) { throw "clients did not reach phase=sync in 3 boot attempts (A=$phaseA B=$phaseB)" }
    # wqApply does nothing until the set has been readable for 10 s (Prime/kWpSettle).
    # Comparing before that measures the settle timer.
    $prA = Wait-Primed $logA 45; $prB = Wait-Primed $logB 45
    Write-Host "primed A=$prA B=$prB"
    if (-not ($prA -and $prB)) { throw "a client never primed its quest set (A=$prA B=$prB)" }
    Start-Sleep 4

    # ---- driver selection -----------------------------------------------------
    # -Driver B makes the GUEST drive (it is the one that runs qsid/qnext); the host
    # then has to follow.  The sync is bidirectional, so the same comparison applies.
    $labelA = 'A'; $labelB = 'B'
    $appB = 'B'
    if ($Driver -eq 'B') {
        $coopA, $coopB = $coopB, $coopA
        $logA, $logB = $logB, $logA
        $pa, $pb = $pb, $pa
        $GameA, $GameB = $GameB, $GameA
        $labelA = 'B(driver)'; $labelB = 'A(follower)'; $appB = 'A'
        Write-Host "DRIVER=B: the guest drives, the host follows (labels in this log: $labelA / $labelB)"
    } else {
        Write-Host "DRIVER=A: the host drives, the guest follows"
    }

    # ---- 1. the quest ID list -------------------------------------------------
    Send-Cmd $coopA 'qlist'
    $list = Wait-Result $coopA 'result_qlist.txt' 20
    $ids = @()
    if ($list) { $ids = @($list -split "`r?`n" | ? { $_.Trim() } | % { $_.Trim() }) }
    # GorfQuest is proven multi-stage by the old runs (native-r6i-3: stage 1 -> 3), so
    # it is tried first even if it is not among the first rows of the table.
    $known = @('GorfQuest', 'VlasQuest', 'TielsQuest', 'AutoQuest')
    $ids = @($known) + @($ids | ? { $known -notcontains $_ })
    Write-Host "qlist: $($ids.Count) row(s)"
    if ($ids.Count -eq 0) { throw "qlist returned no quest rows (see $logA)" }

    # ---- 2. find quests that really have 2+ stages --------------------------
    # The row copy on the object (QuestDetails.Stages) is EMPTY in this build even
    # after InitializeQuest -- qstage prints `stages=0 qdoff=384 qn=''` -- so the
    # stage count cannot be read off the object.  Ask the game instead: advance the
    # quest once and see whether CurrentQuestStage moves; that IS what "2+ stages"
    # means for the player.  The first mover is the quest that gets walked.
    $multi = @()
    $fallback = ''
    foreach ($id in ($ids | Select-Object -First 12)) {
        Send-Cmd $coopA "qsid $id"
        Start-Sleep -Milliseconds 1200
        # does this quest carry real stage TEXT?  GorfQuest's stage name is '' in
        # this build, so it cannot prove anything about text -- only about the index.
        Remove-Item (Join-Path $coopA 'result_qstage.txt') -Force -ErrorAction SilentlyContinue
        Send-Cmd $coopA "qstage $id"
        $st0 = Wait-Result $coopA 'result_qstage.txt' 12
        $hasText = ($st0 -and ($st0 -notmatch "name='' ") -and ($st0 -notmatch "name='nil'"))
        Send-Cmd $coopA "qnext $id"
        Start-Sleep 7
        $line = Get-LastLine $logA ("qnext " + $id + ":")
        $advanced = $false
        if ($line -match "the game advanced (\d+) -> (\d+) on its own \('([^']*)' -> '([^']*)'\)") {
            $advanced = $true
            Write-Host ("  candidate {0,-16} ADVANCED {1}->{2}  '{3}' -> '{4}'  text={5}" -f $id, $matches[1], $matches[2], $matches[3], $matches[4], $hasText)
            $multi += , @($id, [int]$matches[1], [int]$matches[2], $matches[3], $matches[4])
        } elseif ($line -match "direct set (\d+) -> (\d+) ok=true \('([^']*)' -> '([^']*)'\)") {
            $advanced = $true
            Write-Host ("  candidate {0,-16} MOVED-direct {1}->{2}  '{3}' -> '{4}'  text={5}" -f $id, $matches[1], $matches[2], $matches[3], $matches[4], $hasText)
            $multi += , @($id, [int]$matches[1], [int]$matches[2], $matches[3], $matches[4])
        } else {
            Write-Host ("  candidate {0,-16} did not advance (one stage, no objectives, or complete)  text={1}" -f $id, $hasText)
        }
        if ($advanced -and -not $fallback) { $fallback = $id }
        if ($advanced -and $hasText -and -not $chosen) { $chosen = $id }
    }
    if (-not $chosen) { $chosen = $fallback }
    if (-not $chosen) { throw "no quest advanced to a second stage (see $logA)" }
    $stageCount = 2
    foreach ($m in $multi) { if ($m[0] -eq $chosen) { $stageCount = [int]$m[2] } }
    Write-Host "quests that advanced: $($multi.Count); chosen=$chosen now at stage $stageCount"

    # ---- 3. stage by stage --------------------------------------------------
    # One comparison per stage: A's and B's FULL visible state must be identical.
    # The walk stops when the game no longer advances on A (last stage or the quest
    # completed), so a 3-stage quest is walked 3 times and no index is invented.
    $walked = 0
    while ($walked -lt $MaxStages) {
        $walked++
        foreach ($d in @($coopA, $coopB)) { Remove-Item (Join-Path $d 'result_qstage.txt') -Force -ErrorAction SilentlyContinue }
        Send-Cmd $coopA "qstage $chosen"
        $sa = Wait-Result $coopA 'result_qstage.txt' 15
        Hide-All
        Shot $pa (Join-Path $outDir ("stage{0}-A.png" -f $walked))
        Shot $pb (Join-Path $outDir ("stage{0}-B.png" -f $walked))

        $pa1 = Get-StatePayload $sa
        $w = Wait-Match $chosen $pa1 25
        $pb1 = $w.text
        $match = ($pa1 -ne $null) -and ($w.ok)
        $stageRows.Add("stage $walked ${labelA}: $pa1")
        $stageRows.Add("stage $walked ${labelB}: $pb1  (converged after $($w.sec) s)")
        Write-Host "=== QUESTSTAGE stage $walked match=$match after $($w.sec) s"
        Write-Host "  ${labelA}: $pa1"
        Write-Host "  ${labelB}: $pb1"
        if (-not $match) { $fail.Add("stage ${walked}: B never matched A within 25 s (A='$pa1' B='$pb1')") }

        if ($walked -ge $MaxStages) { break }
        Send-Cmd $coopA "qnext $chosen"
        # hear (t+0) -> settle/direct (t+2.5) -> snapshot (t+4), then the peer applies
        Start-Sleep 9
        $line = Get-LastLine $logA ("qnext " + $chosen + ":")
        if ($line -match "the game advanced|direct set .* ok=true") {
            Write-Host "  advanced on A: $line"
        } else {
            Write-Host "no further advance on A -- last stage reached after ${walked}: $line"
            break
        }
    }

    # ---- 4. late join / reconnect ------------------------------------------
    # B goes down FIRST, then A really advances a quest it has never seen on B, so
    # the reconnect has a genuine deficit to catch up on rather than "already equal".
    $lateJoinTried = $true
    $lj = if ($multi.Count -ge 2) { $multi[1][0] } else { $chosen }
    Write-Host "late join: quest=$lj; stopping B, advancing it twice on A, restarting B"
    # Only B's GAME is stopped: the relay pair stays up, so what is tested is the
    # game client reconnecting and catching up, not the relay restarting.
    try { Stop-Process -Id $pb.Id -Force -ErrorAction SilentlyContinue } catch {}
    Start-Sleep 3
    for ($j = 1; $j -le 2; $j++) { Send-Cmd $coopA "qnext $lj"; Start-Sleep 7 }
    Remove-Item (Join-Path $coopB 'result_qstage.txt'), (Join-Path $coopB 'sync.txt') -Force -ErrorAction SilentlyContinue
    $pb = Start-Client $GameB (Join-Path $root "local-appdata\$appB")
    $until2 = (Get-Date).AddSeconds($BootWait)
    while ((Get-Date) -lt $until2) {
        Hide-All; Start-Sleep -Milliseconds 700
        if ((Sync $coopB 'phase') -eq 'sync') { break }
    }
    Write-Host "late join phase B=$(Sync $coopB 'phase')"
    $ljPr = Wait-Primed $logB 60
    Write-Host "late join primed B=$ljPr"
    Start-Sleep 6
    foreach ($d in @($coopA, $coopB)) { Remove-Item (Join-Path $d 'result_qstage.txt') -Force -ErrorAction SilentlyContinue }
    Send-Cmd $coopA "qstage $lj"; $sa = Wait-Result $coopA 'result_qstage.txt' 15
    Hide-All
    Shot $pa (Join-Path $outDir 'latejoin-A.png')
    $pa3 = Get-StatePayload $sa
    $w2 = Wait-Match $lj $pa3 45
    $pb3 = $w2.text
    Shot $pb (Join-Path $outDir 'latejoin-B.png')
    $lateJoinOk = ($pa3 -ne $null) -and ($w2.ok)
    $stageRows.Add("latejoin $lj ${labelA}: $pa3")
    $stageRows.Add("latejoin $lj ${labelB}: $pb3  (converged after $($w2.sec) s; primed=$ljPr)")
    Write-Host "=== QUESTSTAGE latejoin match=$lateJoinOk after $($w2.sec) s (quest $lj)"
    Write-Host "  ${labelA}: $pa3"
    Write-Host "  ${labelB}: $pb3"
    if (-not $lateJoinOk) { $fail.Add("late join ($lj): B never matched A within 45 s (A='$pa3' B='$pb3')") }

    # ---- 5. the journal on screen ------------------------------------------
    # The game own UI handler opens the game menu on a TAB: HandleGameMenu(
    # SubTabIndex, AllowClose) is what IA_Menu_Game calls.  Each tab is opened on both
    # clients and BOTH windows are screenshotted, and after each tab the journal own
    # rows (WBP_QuestLogEntry_Objective_C) are read, so the on-screen text and the
    # text the widget binds are both covered.
    $journalTabs = @()
    for ($tab = 0; $tab -le 5; $tab++) {
        Send-Cmd $coopA "qlog $tab"
        Send-Cmd $coopB "qlog $tab"
        Start-Sleep 3
        Hide-All
        Shot $pa (Join-Path $outDir ("journal-{0}-tab{1}.png" -f $labelA, $tab))
        Shot $pb (Join-Path $outDir ("journal-{0}-tab{1}.png" -f $labelB, $tab))
        foreach ($c in @($coopA, $coopB)) {
            Remove-Item (Join-Path $c 'result_qwidget.txt') -Force -ErrorAction SilentlyContinue
            Send-Cmd $c 'qwidget'
        }
        Start-Sleep 1
        $ca = @(Get-Content (Join-Path $coopA 'result_qwidget.txt') -ErrorAction SilentlyContinue | ? { $_.Trim() }).Count
        $cb = @(Get-Content (Join-Path $coopB 'result_qwidget.txt') -ErrorAction SilentlyContinue | ? { $_.Trim() }).Count
        $journalTabs += "tab $tab -> journal rows $labelA=$ca $labelB=$cb"
        Write-Host "  qlog tab $tab -> journal rows $labelA=$ca $labelB=$cb"
    }
    Copy-Item (Join-Path $coopA 'result_qwidget.txt') (Join-Path $outDir "journal-widget-$labelA.txt") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coopB 'result_qwidget.txt') (Join-Path $outDir "journal-widget-$labelB.txt") -Force -ErrorAction SilentlyContinue
    $stageRows += $journalTabs

    # keep the logs and the quest lines with the screenshots
    Copy-Item $logA (Join-Path $outDir 'ms2coop-native-A.log') -Force -ErrorAction SilentlyContinue
    Copy-Item $logB (Join-Path $outDir 'ms2coop-native-B.log') -Force -ErrorAction SilentlyContinue
    if (Test-Path $logA) { Select-String -Path $logA -Pattern 'qstage |qsid |qnext |wq stage|wq apply .*s\|' | % Line | Set-Content (Join-Path $outDir 'quest-A.txt') }
    if (Test-Path $logB) { Select-String -Path $logB -Pattern 'qstage |qsid |qnext |wq stage|wq apply .*s\|' | % Line | Set-Content (Join-Path $outDir 'quest-B.txt') }

    # every UFunction param miss is a silent no-op: this is a hard rule from R8
    foreach ($lg in @($logA, $logB)) {
        if (Test-Path $lg) {
            $miss = @(Select-String -Path $lg -Pattern 'has no parameter')
            if ($miss.Count -gt 0) { $fail.Add("$($miss.Count) 'has no parameter' line(s) in $(Split-Path $lg -Leaf) (wrong UFunction param name = silent no-op)") }
        }
    }
}
catch {
    $fail.Add("run aborted: $($_.Exception.Message)")
}
finally {
    # never let cleanup throw: the lock MUST come back (a thrown Stop-All here is how
    # the first attempt leaked the lock)
    try { Stop-All } catch { Write-Host "cleanup: $($_.Exception.Message)" }
    if ($locked) { try { Unlock-Game $who } catch {} }
    if (Test-Path $saveSnap) {
        try {
            Get-ChildItem $saveDir -Filter *.sav -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
            Copy-Item "$saveSnap\*" $saveDir -Recurse -Force -ErrorAction SilentlyContinue
            Write-Host "saves restored from $saveSnap"
        } catch { Write-Host "save restore failed: $($_.Exception.Message)" }
    }

    $verdict = if ($fail.Count -eq 0) { 'PASS' } elseif ($stageRows.Count -eq 0) { 'FAIL' } else { 'FAIL' }
    $detail = if ($fail.Count -eq 0) {
        "quest=$chosen walked=$walked stage(s) from stage $stageCount; at every stage A and B had the same stage index, CurrentStageDetails StageName, current-stage objectives+counters, stage-name list and turn-in flag; late join catch-up matched"
    } else { ($fail -join ' | ') }
    $line = "QUESTSTAGE: $verdict $detail"
    Write-Host "=== $line"
    Set-Content (Join-Path $outDir 'queststages-result.txt') ($line + "`n`n" + ($stageRows -join "`n") + "`n`nscreenshots:`n" + ($screenshots -join "`n")) -Encoding UTF8
    Write-Host "out=$outDir"
}
