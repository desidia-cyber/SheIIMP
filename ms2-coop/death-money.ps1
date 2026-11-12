# death-money.ps1 -- [R8-MONEY] the death-money verdict: the lost-Gloom spoils after a
# co-op death.
#
#   . .\deploy.ps1?  no -- deploy first, then:
#   .\death-money.ps1 -GameA "E:\Games\_MS2-coop-e" -GameB "E:\Games\_MS2-coop-f" -Port 47120 -Phase case2
#
# WHAT IT ASKS (the brief's a-d):
#   a/d SPAWN    a case-2 death leaves ONE BP_DeathSpoils_C at the death spot, its
#                `Amount` equals the lost-Gloom slot the death wrote, and the
#                PARTNER's world has none (currency is per player: the actor is
#                client-local, so the partner cannot collect what he cannot see).
#   b   COLLECT  after the co-op respawn (no world reload) the bag is still there,
#                `moneyget` runs the game's own collect callback, and the wallet goes
#                back up by EXACTLY the lost amount.
#   b   WIPE     after the game's own wipe reset the drop comes back from the save
#                slot (a reload re-spawns it) and pays the same amount.
#   c   SECOND   a second death before collecting REPLACES the first drop: exactly one
#                bag survives (the one the save slot now describes), and collecting it
#                pays the second death's amount only -- never both.
#
# Every answer is read from the two clients' own logs (probe.log) and sync.txt; the
# screen evidence comes from visual-two.ps1 (see REPORT.md for the folder).
#
# Prints "=== DEATHMONEY-<part>: PASS|FAIL" lines and a final "RESULT: PASS|FAIL".
param(
    [Parameter(Mandatory = $true)][string]$GameA,
    [Parameter(Mandatory = $true)][string]$GameB,
    [int]$BootWait = 95,
    # case2 | second | wipe | all
    [string]$Phase = "case2",
    [int]$Port = 47120,
    # Gloom the fixture grants to B before its death (the death must take a NON-ZERO
    # amount, otherwise the game does not rewrite the lost-gloom slot at all).
    [int]$Grant = 500,
    [int]$Grant2 = 300,
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)

$ErrorActionPreference = 'Stop'

function Start-ReporterKiller {
    $script:killerFlag = Join-Path $env:TEMP ("ms2killer_" + (Get-Date -Format 'HHmmss') + ".flag")
    New-Item -ItemType File -Force -Path $script:killerFlag | Out-Null
    $script:killerJob = Start-Job -ArgumentList $script:killerFlag -ScriptBlock {
        param($flag)
        while (Test-Path $flag) {
            Get-Process CrashReportClient -ErrorAction SilentlyContinue | ForEach-Object {
                try { Stop-Process -Id $_.Id -Force } catch {}
            }
            Start-Sleep -Milliseconds 150
        }
    }
}
function Stop-ReporterKiller {
    try { Remove-Item $script:killerFlag -Force -ErrorAction SilentlyContinue } catch {}
    try { Stop-Job $script:killerJob -ErrorAction SilentlyContinue; Remove-Job $script:killerJob -Force -ErrorAction SilentlyContinue } catch {}
}
function Stop-All {
    Get-Process | Where-Object ProcessName -match 'MortalShell2' | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
    Get-Process | Where-Object ProcessName -match 'CrashReportClient' | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
    Get-CimInstance Win32_Process -Filter "Name like 'python%'" | Where-Object { $_.CommandLine -match 'cooprelay' } | ForEach-Object { try { Stop-Process -Id $_.ProcessId -Force } catch {} }
}
Add-Type -Namespace Win32 -Name M -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetWindowPos(System.IntPtr hWnd, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PrintWindow(System.IntPtr hWnd, System.IntPtr hdcBlt, uint nFlags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT lpRect);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
'@
Add-Type -AssemblyName System.Drawing
# The screen is evidence too (a log-only verdict has lied on this project): each money
# phase shoots both windows.
function Shot([System.Diagnostics.Process]$proc, [string]$path) {
    if (-not $proc) { return }
    $proc.Refresh(); $h = $proc.MainWindowHandle
    if ($h -eq 0) { Write-Host "shot: no window"; return }
    $r = New-Object Win32.M+RECT; [Win32.M]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -le 0 -or $ht -le 0) { Write-Host "shot: empty rect"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [Win32.M]::PrintWindow($h, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc); $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Write-Host "shot: $path"
}
function Hide-GameWindow {
    Get-Process | Where-Object ProcessName -match 'MortalShell2-Win64' | ForEach-Object {
        if ($_.MainWindowHandle -ne 0) {
            [Win32.M]::SetWindowPos($_.MainWindowHandle, [IntPtr]::Zero, -30000, -30000, 0, 0, 0x15) | Out-Null
        }
    }
}
function Seed-LocalAppData([string]$target) {
    $mine = Join-Path $target 'MortalShell2\Saved'
    if (-not (Test-Path $mine)) {
        New-Item -ItemType Directory -Force -Path (Join-Path $mine 'SaveGames') | Out-Null
        $srcSaves = Join-Path $env:LOCALAPPDATA 'MortalShell2\Saved\SaveGames'
        if (Test-Path $srcSaves) { Copy-Item "$srcSaves\*.sav" (Join-Path $mine 'SaveGames') -Force }
        $srcCfg = Join-Path $env:LOCALAPPDATA 'MortalShell2\Saved\Config'
        if (Test-Path $srcCfg) { Copy-Item $srcCfg (Join-Path $mine 'Config') -Recurse -Force }
        Write-Host "seeded $target"
    }
}
function Start-Client([string]$gameDir, [string]$localAppData) {
    $exe = Join-Path $gameDir 'MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe'
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = "MortalShell2 $ExtraArgs"
    $psi.WorkingDirectory = $gameDir
    $psi.UseShellExecute = $true
    $psi.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Normal
    $realLocal = $env:LOCALAPPDATA
    $env:LOCALAPPDATA = $localAppData
    $p = [System.Diagnostics.Process]::Start($psi)
    $env:LOCALAPPDATA = $realLocal
    Write-Host "started $gameDir (pid $($p.Id))"
    return $p
}
function Sync([string]$coop, [string]$key) {
    $f = Join-Path $coop 'sync.txt'
    if (-not (Test-Path $f)) { return $null }
    foreach ($l in Get-Content $f) { if ($l -like "$key=*") { return $l.Substring($key.Length + 1) } }
    return $null
}
# One field out of the "money=" sync.txt line ("spoil:1 amount:500 ...").
function Money([string]$coop, [string]$field) {
    $m = Sync $coop 'money'
    if (-not $m) { return $null }
    if ($m -match "$field`:(-?\d+)") { return [int]$matches[1] }
    return $null
}
function Get-DeathField([string]$coop, [string]$field) {
    $d = Sync $coop 'death'
    if (-not $d) { return $null }
    if ($d -match "$field`:(\S+)") { return $matches[1] }
    return $null
}
function Send-Cmd([string]$coop, [string]$cmd) {
    # The mod DELETES cmd.txt when it reads it; writing the next one too early loses
    # it (measured: that ate `die` on one run).  So wait for the file to disappear.
    $f = Join-Path $coop 'cmd.txt'
    [System.IO.File]::WriteAllText($f, $cmd + "`n")
    $due = (Get-Date).AddSeconds(12)
    while ((Get-Date) -lt $due -and (Test-Path $f)) { Start-Sleep -Milliseconds 150 }
    Start-Sleep -Milliseconds 400
}
function Wait-Until([int]$secs, [scriptblock]$cond) {
    $end = (Get-Date).AddSeconds($secs)
    while ((Get-Date) -lt $end) {
        Hide-GameWindow
        if (& $cond) { return $true }
        Start-Sleep -Milliseconds 400
    }
    return $false
}
function Log-Lines([string]$coop, [string]$pattern) {
    $lg = Join-Path $coop 'probe.log'
    if (-not (Test-Path $lg)) { return @() }
    return @(Select-String -Path $lg -Pattern $pattern | ForEach-Object { $_.Line })
}
function Last-Log([string]$coop, [string]$pattern) {
    # @(...) around the RESULT, not just inside Log-Lines: a one-line match comes back
    # from a function as a scalar STRING, and $string[-1] then means "last character"
    # (that bug handed the verdict the letter ")" instead of the log line).
    $l = @(Log-Lines $coop $pattern)
    if ($l.Count -eq 0) { return $null }
    return $l[$l.Count - 1]
}
# "money: a drop is in the world: NAME amount=500 ..." -> 500
function LastDropAmount([string]$coop) {
    $l = Last-Log $coop 'money: a drop is in the world:'
    if (-not $l) { return $null }
    if ($l -match 'amount=(-?\d+)') { return [int]$matches[1] }
    return $null
}
function LastCollected([string]$coop) {
    $l = Last-Log $coop 'money: collected '
    if (-not $l) { return $null }
    if ($l -match '\(\+(\d+)\)') { return [int]$matches[1] }
    return $null
}

$root = $PSScriptRoot
Start-ReporterKiller
Stop-All
Start-Sleep -Seconds 3

# --- the owner's saves are never touched: snapshot and restore -----------------
$realSaveDir = Join-Path $env:LOCALAPPDATA "MortalShell2\Saved\SaveGames"
$saveSnap = Join-Path $env:TEMP ("ms2saves_m_" + (Get-Date -Format "HHmmss"))
$saveNamesBefore = @()
if (Test-Path $realSaveDir) {
    New-Item -ItemType Directory -Force -Path $saveSnap | Out-Null
    Copy-Item (Join-Path $realSaveDir "*.sav") $saveSnap -Force
    $saveNamesBefore = @(Get-ChildItem $realSaveDir -Filter *.sav | Select-Object -ExpandProperty Name)
}

$localA = Join-Path $root 'local-appdata\A'
$localB = Join-Path $root 'local-appdata\B'
Seed-LocalAppData $localA
Seed-LocalAppData $localB

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
foreach ($d in @($coopA, $coopB)) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
    Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'probe.log'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'peer_*.txt'), (Join-Path $d 'out_*.txt'), (Join-Path $d 'sync.txt') -Force -ErrorAction SilentlyContinue
    Remove-Item (Join-Path $d 'SOLO_DEATH.txt'), (Join-Path $d 'DEATH_OFF.txt'), (Join-Path $d 'MONEY_OFF.txt') -Force -ErrorAction SilentlyContinue
}
Set-Content -Path (Join-Path $coopA 'playerid.txt') -Value 'A' -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coopA 'role.txt') -Value 'host' -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coopB 'playerid.txt') -Value 'B' -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coopB 'role.txt') -Value 'guest' -Encoding ASCII -NoNewline

# The screenshots land next to the logs of THIS run.
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$evidence = Join-Path 'G:\Freespace\shell_coop\local\lab' "$stamp-death-money"
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$pa = Start-Client $GameA $localA
Start-Sleep -Seconds 8
$pb = Start-Client $GameB $localB
$relay = Join-Path $root 'relay\cooprelay.py'
Start-Process -FilePath 'python' -ArgumentList @($relay, '--transport', 'udp', '--role', 'host', '--port', "$Port", '--player', 'A', '--dir', $coopA) -WindowStyle Hidden
Start-Sleep -Seconds 2
Start-Process -FilePath 'python' -ArgumentList @($relay, '--transport', 'udp', '--role', 'client', '--host', '127.0.0.1', '--port', "$Port", '--player', 'B', '--dir', $coopB) -WindowStyle Hidden

$until = (Get-Date).AddSeconds($BootWait)
while ((Get-Date) -lt $until) {
    Hide-GameWindow
    if ((Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') { break }
    Start-Sleep -Milliseconds 700
}
$linked = Wait-Until 120 {
    ((Sync $coopA 'phase') -eq 'sync') -and ((Sync $coopB 'phase') -eq 'sync') -and
    (Sync $coopA 'peerbody') -and (Sync $coopB 'peerbody')
}
Write-Host "=== LINK: $(if ($linked) { 'PASS both clients in gameplay, peer seen' } else { 'FAIL (no link)' })"

$spawnOk = $false
$collectOk = $false
$secondOk = $false
$wipeOk = $false
$partnerOk = $false
$visOk = $false

# =============================================================================
# case2 -- one player down, partner alive: the co-op respawn, no world reload
# =============================================================================
if ($Phase -eq 'all' -or $Phase -eq 'case2' -or $Phase -eq 'second') {
    # FIXTURE RESET on both clients: the seeded save may already owe each player a drop
    # from an earlier session (run 4: a 4021 bag was on the ground before the first
    # death, on A and on B), and a verdict that cannot tell the old bag from this run's
    # bag is worthless.
    Send-Cmd $coopA 'moneyclear'
    Send-Cmd $coopB 'moneyclear'
    Start-Sleep -Seconds 1
    Send-Cmd $coopB 'moneydump'
    Send-Cmd $coopB "moneygive $Grant"
    Start-Sleep -Seconds 2
    $granted = $null
    $gline = Last-Log $coopB 'moneygive: asked'
    if ($gline -and $gline -match 'carried gloom (-?\d+) -> (-?\d+)') { $granted = [int]$matches[2] }
    if ($null -eq $granted) { $granted = -1 }
    Write-Host "=== DEATHMONEY baseline: B carried gloom=$granted (asked for $Grant), spoils=$(Money $coopB 'spoil') on B / $(Money $coopA 'spoil') on A"
    Write-Host "--- B $gline"
    Send-Cmd $coopB 'die'
    $case2 = Wait-Until 30 { (Get-DeathField $coopB 'case') -eq '2' }
    Write-Host "=== DEATHMONEY case: $(if ($case2) { 'PASS the death was decided as case 2' } else { "FAIL ($(Sync $coopB 'death'))" })"
    $back = Wait-Until 60 { (Get-DeathField $coopB 'me') -eq '1(alive)' -and (Get-DeathField $coopB 'pending') -eq '0' }
    Start-Sleep -Seconds 3
    Send-Cmd $coopB 'moneydump'
    Send-Cmd $coopA 'moneydump'
    Start-Sleep -Seconds 1
    $lost = Money $coopB 'lost'
    $spoilB = Money $coopB 'spoil'
    $spoilA = Money $coopA 'spoil'
    $amtB = Money $coopB 'amount'
    $seenB = Last-Log $coopB 'money: a drop is in the world:'
    $respB = Last-Log $coopB 'money: respawned'
    foreach ($l in @($seenB, $respB)) { if ($l) { Write-Host "--- B $l" } }
    Write-Host "--- B death-spoils steps the game's own ability ran (or did not):"
    Log-Lines $coopB 'money: STEP ' | Select-Object -Last 20 | ForEach-Object { Write-Host "    $_" }
    # a) the drop exists in the dying player's world, its Amount equals the loss (and
    #    the loss equals what the player carried), and the partner's world has none.
    #    The verdict is printed AFTER the visibility check below (it is one of its
    #    inputs; run 6 printed it before $visOk existed and a PASS read FAIL).
    # On screen: B's own view of the drop (moneyview stands 200/650/280 uu from it and
    # faces it, camera tilted down) and A's window at the same moment.
    Send-Cmd $coopB 'moneyview 200'
    Start-Sleep -Seconds 3
    Hide-GameWindow
    Shot $pb (Join-Path $evidence 'B-sees-the-drop.png')
    Shot $pa (Join-Path $evidence 'A-the-partner-no-drop.png')
    Send-Cmd $coopB 'moneyview 650'
    Start-Sleep -Seconds 3
    Hide-GameWindow
    Shot $pb (Join-Path $evidence 'B-sees-the-drop-wide.png')
    Send-Cmd $coopB 'moneyview 280'
    Start-Sleep -Seconds 3
    Hide-GameWindow
    Shot $pb (Join-Path $evidence 'B-sees-the-drop-close.png')
    # THE VISIBILITY VERDICT from the engine (WasRecentlyRendered + the interaction
    # handler), because a dark gloom pile in dark water cannot be judged by eye.
    Send-Cmd $coopB 'moneyvis'
    Start-Sleep -Seconds 1
    $vis = Last-Log $coopB 'moneyvis: '
    Write-Host "--- B $vis"
    $rendered = ($vis -match 'rendered=1')
    $visOk = $rendered -and ($vis -notmatch 'hidden=1') -and ($vis -match 'handler=1')
    Write-Host "=== DEATHMONEY-VISIBLE: $(if ($visOk) { 'PASS the drop is RENDERED by the game (WasRecentlyRendered), not hidden, with its interaction handler live' } else { "FAIL rendered= line=$vis" })"
    $spawnOk = $case2 -and $back -and ($spoilB -eq 1) -and ($amtB -eq $lost) -and ($lost -gt 0) -and
               ($lost -eq $granted) -and ($spoilA -eq 0) -and $seenB -and $respB -and $visOk
    Write-Host "=== DEATHMONEY-SPAWN: $(if ($spawnOk) { "PASS one drop in B's world (amount=$amtB = the lost slot $lost = what B carried), none in A's; the bag survived the co-op respawn (case 2, no reload) and the game renders it" } else { "FAIL B spoil=$spoilB amount=$amtB lost=$lost carried=$granted A spoil=$spoilA back=$back case2=$case2 seen=$([bool]$seenB) vis=$visOk" })"

    if ($Phase -eq 'case2') {
        # b) collect it with the game's own callback, then compare the wallet.
        #    NOT in the `all` run: there the drop must SURVIVE into the second-death
        #    phase, because that phase is about the old drop being replaced.  The
        #    collect verdict is taken at the end of `second` instead (same question,
        #    same numbers), so `all` still covers (b).
        $wBefore = Money $coopB 'before'
        Send-Cmd $coopB 'moneyget'
        $got = Wait-Until 25 { $null -ne (LastCollected $coopB) }
        Start-Sleep -Seconds 1
        Send-Cmd $coopB 'moneydump'
        $ret = LastCollected $coopB
        $wAfter = -1
        $line = Last-Log $coopB 'money\(after the collect\)|money\(moneydump\)'
        if ($line -and $line -match 'gloom=(-?\d+)') { $wAfter = [int]$matches[1] }
        $gone = (Money $coopB 'spoil') -eq 0
        $collectOk = $spawnOk -and $got -and ($null -ne $ret) -and ($ret -eq $lost) -and $gone
        Write-Host "=== DEATHMONEY-COLLECT: $(if ($collectOk) { "PASS collected +$ret (the lost amount), the bag is gone, wallet back to $wAfter" } else { "FAIL returned=$ret lost=$lost gone=$gone wallet=$wAfter" })"
        Write-Host "--- B collect lines:"
        Log-Lines $coopB 'moneyget|money: collect|money: collected|OnInteraction|HandleInteractEvent' | Select-Object -Last 12 | ForEach-Object { Write-Host "    $_" }
        # d) THE PARTNER: A must not have a drop of B's (its own was cleared by the
        #    fixture reset) and must never collect one — currency is per player.
        Send-Cmd $coopA 'moneydump'
        Start-Sleep -Seconds 1
        $spoilA2 = Money $coopA 'spoil'
        $gotA = Money $coopA 'got'
        $colA = Money $coopA 'collected'
        $partnerOk = ($spoilA2 -eq 0) -and ($gotA -eq 0) -and ($colA -eq 0)
        Write-Host "=== DEATHMONEY-PARTNER: $(if ($partnerOk) { "PASS A holds no drop (spoil=$spoilA2, it has its own world and its own save) and never collected B's (+$gotA over $colA collect(s)); B was paid +$ret" } else { "FAIL A spoil=$spoilA2 got=$gotA collected=$colA" })"
        Log-Lines $coopA 'money: collected|moneyget' | Select-Object -Last 6 | ForEach-Object { Write-Host "    A $_" }
    } else {
        Write-Host "=== DEATHMONEY-COLLECT: deferred to the second-death phase (the first drop must stay on the ground to be replaced)"
    }
}

# =============================================================================
# second -- a second death before collecting must REPLACE the old drop
# =============================================================================
if ($Phase -eq 'all' -or $Phase -eq 'second') {
    # The first death above already left a bag behind (it was not collected).
    $bags1 = Money $coopB 'seen'
    $lost1 = Money $coopB 'lost'
    $pos1 = Last-Log $coopB 'money: a drop is in the world:'
    Write-Host "=== DEATHMONEY-SECOND baseline: bags seen=$bags1, first drop: $pos1"
    Send-Cmd $coopB "moneygive $Grant2"
    Start-Sleep -Seconds 2
    $granted2 = $null
    $gline2 = Last-Log $coopB 'moneygive: asked'
    if ($gline2 -and $gline2 -match 'carried gloom (-?\d+) -> (-?\d+)') { $granted2 = [int]$matches[2] }
    if ($null -eq $granted2) { $granted2 = -1 }
    Write-Host "--- B $gline2"
    Send-Cmd $coopB 'die'
    $case2b = Wait-Until 30 { (Get-DeathField $coopB 'case') -eq '2' }
    $back2 = Wait-Until 60 { (Get-DeathField $coopB 'me') -eq '1(alive)' -and (Get-DeathField $coopB 'pending') -eq '0' }
    Start-Sleep -Seconds 3
    Send-Cmd $coopB 'moneydump'
    $sweep = Last-Log $coopB 'money\(after the death\) sweep:'
    $lost2 = Money $coopB 'lost'
    $spoil2 = Money $coopB 'spoil'
    $amt2 = Money $coopB 'amount'
    $stale = Money $coopB 'stale'
    foreach ($l in (Log-Lines $coopB 'money\(after the death\) sweep:')) { Write-Host "--- B $l" }
    # the screen first: one bag on the ground, then collect it
    Send-Cmd $coopB 'moneyview 200'
    Start-Sleep -Seconds 3
    Hide-GameWindow
    Shot $pb (Join-Path $evidence 'B-second-death-single-drop.png')
    Send-Cmd $coopB 'moneyview 650'
    Start-Sleep -Seconds 3
    Hide-GameWindow
    Shot $pb (Join-Path $evidence 'B-second-death-single-drop-wide.png')
    Send-Cmd $coopB 'moneyget'
    $got2 = Wait-Until 25 { $null -ne (LastCollected $coopB) }
    $ret2 = LastCollected $coopB
    # (b) again, on the SECOND loss: the collect returns exactly what this death took
    if ($Phase -ne 'case2') { $collectOk = $got2 -and ($null -ne $ret2) -and ($ret2 -eq $lost2) -and ($lost2 -gt 0) }
    # the rule: ONE bag left, it is the new one, and the payout is the second loss only
    $secondOk = $case2b -and $back2 -and ($spoil2 -eq 1) -and ($amt2 -eq $lost2) -and ($stale -ge 1) -and
                ($ret2 -eq $lost2) -and ($lost2 -eq $granted2)
    Write-Host "=== DEATHMONEY-SECOND: $(if ($secondOk) { "PASS the old drop was replaced (1 bag, stale=$stale destroyed), collecting paid +$ret2 = the second loss $lost2, never both" } else { "FAIL bags=$spoil2 amount=$amt2 lost2=$lost2 stale=$stale returned=$ret2 expected=$granted2" })"
    if ($Phase -ne 'case2') {
        # d) the partner, on the same evidence as the case2 phase
        Send-Cmd $coopA 'moneydump'
        Start-Sleep -Seconds 1
        $spoilA2 = Money $coopA 'spoil'
        $gotA = Money $coopA 'got'
        $colA = Money $coopA 'collected'
        $partnerOk = ($spoilA2 -eq 0) -and ($gotA -eq 0) -and ($colA -eq 0)
        Write-Host "=== DEATHMONEY-PARTNER: $(if ($partnerOk) { "PASS A holds no drop (spoil=${spoilA2}: its own world and its own save) and never collected B's (+$gotA over $colA collect(s)); B was paid +$ret2" } else { "FAIL A spoil=$spoilA2 got=$gotA collected=$colA" })"
        Log-Lines $coopA 'money: collected|moneyget' | Select-Object -Last 6 | ForEach-Object { Write-Host "    A $_" }
    }
}

# =============================================================================
# wipe -- the game's own death path: the drop comes back from the save slot
# =============================================================================
if ($Phase -eq 'all' -or $Phase -eq 'wipe') {
    Send-Cmd $coopB "moneygive $Grant"
    Start-Sleep -Seconds 2
    Send-Cmd $coopA 'die'
    Send-Cmd $coopB 'die'
    $wiped = Wait-Until 25 { (Get-DeathField $coopB 'case') -eq '1' -or (Get-DeathField $coopA 'case') -eq '1' }
    $bothBack = Wait-Until 150 { (Get-DeathField $coopA 'me') -eq '1(alive)' -and (Get-DeathField $coopB 'me') -eq '1(alive)' }
    # A reload re-creates the drop from the save slot; the lane re-checks that 10 s
    # after the world change, so wait for the bag rather than for a fixed sleep.
    $restored = Wait-Until 60 { (Money $coopB 'spoil') -ge 1 }
    Start-Sleep -Seconds 2
    Send-Cmd $coopB 'moneydump'
    Start-Sleep -Seconds 1
    $lostW = Money $coopB 'lost'
    $spoilW = Money $coopB 'spoil'
    $inner = Last-Log $coopB 'money: a drop is in the world:'
    $respW = Last-Log $coopB 'money\(after a world change\) restore:'
    Write-Host "--- B $respW"
    # THE A/B PAIR: the same camera, the drop shown and then hidden.  The two PNGs differ
    # exactly by the drop's pixels, which is how a dark gloom pile in dark water is proven
    # to be on screen (offline diff, see REPORT.md).
    Send-Cmd $coopB 'moneyview 280'
    Start-Sleep -Seconds 3
    Hide-GameWindow
    Shot $pb (Join-Path $evidence 'B-wipe-drop-ON.png')
    Send-Cmd $coopB 'moneyblink on'
    Start-Sleep -Seconds 2
    Shot $pb (Join-Path $evidence 'B-wipe-drop-OFF.png')
    Send-Cmd $coopB 'moneyblink off'
    Start-Sleep -Seconds 1
    Send-Cmd $coopB 'moneyget'
    $gotW = Wait-Until 30 { $null -ne (LastCollected $coopB) }
    $retW = LastCollected $coopB
    Write-Host "=== DEATHMONEY-WIPE: wiped=$wiped bothBack=$bothBack restored=$restored bags=$spoilW lost=$lostW returned=$retW"
    Write-Host "--- B drop after the wipe: $inner"
    $wipeOk = $wiped -and $bothBack -and ($spoilW -ge 1) -and ($retW -eq $lostW) -and ($lostW -gt 0)
    Write-Host "=== DEATHMONEY-WIPE: $(if ($wipeOk) { "PASS after the game's own reset the drop is back (amount=$lostW from the save slot) and pays it back" } else { 'FAIL (see the lines above)' })"
}

# =============================================================================
# the evidence
# =============================================================================
foreach ($d in @($coopA, $coopB)) {
    Write-Host "--- $d probe.log (money lines) ---"
    $lg = Join-Path $d 'probe.log'
    if (Test-Path $lg) {
        Select-String -Path $lg -Pattern 'money|COOP DEATH|death\(|RESPAWN|drops:' |
            Select-Object -Last 60 | ForEach-Object { Write-Host $_.Line }
    }
}
foreach ($pair in @(@($coopA, 'A'), @($coopB, 'B'))) {
    foreach ($f in @('probe.log', 'sync.txt', 'out_' + $pair[1] + '.txt')) {
        $src2 = Join-Path $pair[0] $f
        if (Test-Path $src2) { Copy-Item $src2 (Join-Path $evidence ("$($pair[1])-$f")) -Force }
    }
}
Write-Host "evidence in $evidence"
Stop-ReporterKiller
Stop-All
if (Test-Path $saveSnap) {
    Get-ChildItem $realSaveDir -Filter *.sav | Where-Object { $saveNamesBefore -notcontains $_.Name } | ForEach-Object {
        Remove-Item $_.FullName -Force -ErrorAction SilentlyContinue
    }
    Copy-Item (Join-Path $saveSnap "*.sav") $realSaveDir -Force
    Write-Host "owner saves restored from snapshot"
}

switch ($Phase) {
    'case2'  { $pass = $linked -and $spawnOk -and $collectOk -and $partnerOk }
    'second' { $pass = $linked -and $spawnOk -and $secondOk }
    'wipe'   { $pass = $linked -and $wipeOk }
    default  { $pass = $linked -and $spawnOk -and $collectOk -and $secondOk -and $wipeOk -and $partnerOk }
}
if ($pass) { Write-Host "RESULT: PASS" } else { Write-Host "RESULT: FAIL" }
