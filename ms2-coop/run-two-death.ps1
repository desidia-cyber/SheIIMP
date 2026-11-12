# run-two-death.ps1 -- the DEATH agent's two-client run: the three death verdicts.
#
#   .\deploy.ps1 -GameDir "E:\Games\_MS2-coop-test"
#   .\deploy.ps1 -GameDir "E:\Games\_MS2-coop-b"
#   .\run-two-death.ps1 -GameA "E:\Games\_MS2-coop-test" -GameB "E:\Games\_MS2-coop-b"
#
# This is run-two.ps1's scaffolding (two clients, one relay, off-screen windows,
# the save snapshot) with the death sequence as the subject:
#
#   DCASE2  B dies while A is alive and no boss is live -> B is put back on its feet
#           in the LIVE world (no world reset on either client), stacks=1, and B's
#           max health is ~90 % of what it was.
#   DHOLD   bossfake 1 on B, then B dies -> B is HELD (no respawn) and the camera
#           is on the partner's body (the local puppet); bossfake 0 -> B respawns.
#   DWIPE   both die -> the HOST's world reset happens (A's worldresets grows), the
#           guest does NOT run its own (B's worldresets is unchanged), B comes back
#           once A is up, and both have 0 stacks.
#
# Prints "=== DCASE2: PASS|FAIL" style lines and a final "RESULT: PASS|FAIL".
# The verdict block is also kept as native\briefs\R5-DEATH-runtwo-snippet.ps1 so an
# integrator can merge it into run-two.ps1.
param(
    [Parameter(Mandatory = $true)][string]$GameA,
    [Parameter(Mandatory = $true)][string]$GameB,
    [int]$BootWait = 80,
    # all | case2 | hold | wipe.  ONE VERDICT PER SESSION is the normal way to run
    # this: a SECOND death in the same session is not producible (S_KillMe answers ok
    # and then does nothing from the second call on -- measured on all four clients),
    # and the brief allows separate runs for that reason.
    [string]$Phase = "all",
    [int]$Port = 47010,
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
Add-Type -Namespace Win32 -Name W -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetWindowPos(System.IntPtr hWnd, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
'@
function Hide-GameWindow {
    Get-Process | Where-Object ProcessName -match 'MortalShell2-Win64' | ForEach-Object {
        if ($_.MainWindowHandle -ne 0) {
            [Win32.W]::SetWindowPos($_.MainWindowHandle, [IntPtr]::Zero, -30000, -30000, 0, 0, 0x0001 -bor 0x0010 -bor 0x0004) | Out-Null
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
    $argList = @('MortalShell2') + ($ExtraArgs -split '\s+' | Where-Object { $_ })
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
    $psi.WorkingDirectory = $gameDir
    $psi.UseShellExecute = $true
    $psi.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Normal
    $realLocal = $env:LOCALAPPDATA
    $env:LOCALAPPDATA = $localAppData
    $p = [System.Diagnostics.Process]::Start($psi)
    $env:LOCALAPPDATA = $realLocal
    Write-Host "started $gameDir (pid $($p.Id))"
}

$root = $PSScriptRoot
Start-ReporterKiller
Stop-All
Start-Sleep -Seconds 3

# --- the owner's saves are never touched: snapshot and restore -----------------
$realSaveDir = Join-Path $env:LOCALAPPDATA "MortalShell2\Saved\SaveGames"
$saveSnap = Join-Path $env:TEMP ("ms2saves_" + (Get-Date -Format "HHmmss"))
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
    Remove-Item (Join-Path $d 'result.txt'), (Join-Path $d 'probe.log'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'peer_*.txt'), (Join-Path $d 'out_*.txt'), (Join-Path $d 'relay_*.log') -Force -ErrorAction SilentlyContinue
    # The lab override and the kill switch must never leak into a two-client run:
    # SOLO_DEATH.txt would make a solo death look like case 2, DEATH_OFF.txt would
    # turn every rule off.
    Remove-Item (Join-Path $d 'SOLO_DEATH.txt'), (Join-Path $d 'DEATH_OFF.txt') -Force -ErrorAction SilentlyContinue
}
Set-Content -Path (Join-Path $coopA 'playerid.txt') -Value 'A' -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coopA 'role.txt') -Value 'host' -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coopB 'playerid.txt') -Value 'B' -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coopB 'role.txt') -Value 'guest' -Encoding ASCII -NoNewline

Start-Client $GameA $localA
Start-Sleep -Seconds 8
Start-Client $GameB $localB
$relay = Join-Path $root 'relay\cooprelay.py'
Start-Process -FilePath 'python' -ArgumentList @($relay, '--transport', 'udp', '--role', 'host', '--port', "$Port", '--player', 'A', '--dir', $coopA) -WindowStyle Hidden
Start-Sleep -Seconds 2
Start-Process -FilePath 'python' -ArgumentList @($relay, '--transport', 'udp', '--role', 'client', '--host', '127.0.0.1', '--port', "$Port", '--player', 'B', '--dir', $coopB) -WindowStyle Hidden

$until = (Get-Date).AddSeconds($BootWait)
while ((Get-Date) -lt $until) { Hide-GameWindow; Start-Sleep -Milliseconds 700 }

# =============================================================================
# THE VERDICT BLOCK (also in native\briefs\R5-DEATH-runtwo-snippet.ps1)
# =============================================================================
function Get-SyncValue([string]$coopDir, [string]$key) {
    $f = Join-Path $coopDir 'sync.txt'
    if (-not (Test-Path $f)) { return $null }
    foreach ($line in Get-Content $f) { if ($line -match "^$key=(.*)$") { return $matches[1] } }
    return $null
}
# "death=me:1(alive) case:2 victim:2 serial:1 stacks:1 peer:1(seen) boss:0 ..."
function Get-StatusField([string]$coopDir, [string]$field) {
    $f = Join-Path $coopDir 'sync.txt'
    if (-not (Test-Path $f)) { return $null }
    $t = Get-Content $f -Raw
    if (-not $t) { return $null }
    if ($t -match "$field[=:]([^\s,]+)") { return $matches[1] }
    return $null
}
function Get-DeathField([string]$coopDir, [string]$field) {
    $d = Get-SyncValue $coopDir 'death'
    if (-not $d) { return $null }
    if ($d -match "$field`:(\S+)") { return $matches[1] }
    return $null
}
function Send-Cmd([string]$coopDir, [string]$cmd) {
    [System.IO.File]::WriteAllText((Join-Path $coopDir 'cmd.txt'), $cmd + "`n")
}
function Wait-Until([int]$secs, [scriptblock]$cond) {
    $end = (Get-Date).AddSeconds($secs)
    while ((Get-Date) -lt $end) {
        Hide-GameWindow
        if (& $cond) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}
function Show-Death([string]$tag, [string]$coopDir) {
    Write-Host "--- $tag death: $(Get-SyncValue $coopDir 'death')"
    Write-Host "--- $tag       $(Get-SyncValue $coopDir 'deathstate') maxhp=$(Get-SyncValue $coopDir 'maxhp')"
}

# --- the link: both clients in gameplay and each seeing the other -------------
$linked = Wait-Until 150 {
    ((Get-SyncValue $coopA 'phase') -eq 'sync') -and ((Get-SyncValue $coopB 'phase') -eq 'sync') -and
    (Get-SyncValue $coopA 'peerbody') -and (Get-SyncValue $coopB 'peerbody') -and
    ((Get-DeathField $coopB 'peer') -ne '0(none)')
}
Write-Host "=== LINK: $(if ($linked) { 'PASS both clients in gameplay, peer seen' } else { 'FAIL (no link)' })"

# -----------------------------------------------------------------------------
# DCASE2 -- B dies, A alive, no boss: live-world respawn + one stack + 90 % max HP
# -----------------------------------------------------------------------------
if ($Phase -eq 'all' -or $Phase -eq 'case2') {
Show-Death 'A' $coopA
Show-Death 'B' $coopB
$wrA0 = [int](Get-StatusField $coopA 'worldresets'); if (-not $wrA0) { $wrA0 = 0 }
$wrB0 = [int](Get-StatusField $coopB 'worldresets'); if (-not $wrB0) { $wrB0 = 0 }
$hpB0 = [double](Get-StatusField $coopB 'maxhp')
Write-Host "=== DCASE2 baseline: B maxhp=$hpB0 worldresets A=$wrA0 B=$wrB0"

Send-Cmd $coopB 'die'
$case2Seen = Wait-Until 25 { (Get-DeathField $coopB 'case') -eq '2' }
Write-Host "=== DCASE2 case: $(if ($case2Seen) { 'PASS the death was decided as case 2' } else { "FAIL ($(Get-SyncValue $coopB 'death'))" })"
$case2Alive = Wait-Until 45 { (Get-DeathField $coopB 'me') -eq '1(alive)' -and (Get-DeathField $coopB 'pending') -eq '0' }
$wrA1 = [int](Get-StatusField $coopA 'worldresets'); if (-not $wrA1) { $wrA1 = 0 }
$wrB1 = [int](Get-StatusField $coopB 'worldresets'); if (-not $wrB1) { $wrB1 = 0 }
$stk2 = Get-DeathField $coopB 'stacks'
$hpB1 = [double](Get-StatusField $coopB 'maxhp')
$ratio = if ($hpB0 -gt 0) { $hpB1 / $hpB0 } else { 0 }
Write-Host "=== DCASE2: B $(Get-SyncValue $coopB 'death') maxhp $hpB0 -> $hpB1 (ratio $([math]::Round($ratio,3))) worldresets A $wrA0->$wrA1 B $wrB0->$wrB1"
$case2Ok = $case2Seen -and $case2Alive -and ($wrA1 -eq $wrA0) -and ($wrB1 -eq $wrB0) -and
           ($stk2 -eq '1') -and ($ratio -gt 0.85) -and ($ratio -lt 0.95)
Write-Host "=== DCASE2: $(if ($case2Ok) { 'PASS B respawned in the live world, no reset, stacks=1, max health ~90%' } else { 'FAIL' })"
} else { $case2Ok = $true }

# -----------------------------------------------------------------------------
# DHOLD -- bossfake 1, B dies: held + camera on the partner's body; bossfake 0 -> respawned
# -----------------------------------------------------------------------------
if ($Phase -eq 'all' -or $Phase -eq 'hold') {
Send-Cmd $coopB 'bossfake 1'
Start-Sleep -Seconds 2
Send-Cmd $coopB 'die'
$holdSeen = Wait-Until 25 { (Get-DeathField $coopB 'case') -eq '3' -and (Get-DeathField $coopB 'pending') -eq '1' }
# A walks away so "the camera follows A" cannot be confused with "the camera is on B's body"
Send-Cmd $coopA 'patrol 12'
$viewOk = Wait-Until 40 { (Get-StatusField $coopB 'viewpuppet') -eq '1' }
Show-Death 'B(hold)' $coopB
Write-Host "=== DHOLD: $(if ($holdSeen) { 'PASS B is held (case 3, pending)' } else { "FAIL ($(Get-SyncValue $coopB 'death'))" })"
# The spectate camera retargets the CameraStateFramework stack to A's puppet and drops
# CameraState_Death_C; viewpuppet=1 means the manager's RENDERED POV is next to the
# puppet (and nearer to it than to B's own body).
Write-Host "=== DHOLDCAM: $(if ($viewOk) { 'PASS the rendered camera follows the partner' } else { "FAIL the camera does not follow the partner (viewpuppet=$(Get-StatusField $coopB 'viewpuppet'))" })"
Send-Cmd $coopB 'bossfake 0'
$holdReleased = Wait-Until 45 { (Get-DeathField $coopB 'me') -eq '1(alive)' -and (Get-DeathField $coopB 'pending') -eq '0' }
Show-Death 'B(after hold)' $coopB
Write-Host "=== DHOLD release: $(if ($holdReleased) { 'PASS B respawned once the fight was over' } else { 'FAIL' })"
$nlogB = Join-Path (Split-Path $coopB -Parent) 'ms2coop-native.log'
$camBack = [bool](Select-String -Path $nlogB -Pattern 'COOP DEATH camera: back on the player \([1-9]' -Quiet)
Write-Host "=== DHOLDCAM release: $(if ($camBack) { 'PASS the camera is back on B' } else { 'FAIL no camera restore logged' })"
$holdOk = $holdSeen -and $holdReleased -and $viewOk -and $camBack
Write-Host "=== DHOLD: $(if ($holdOk) { 'PASS' } else { 'FAIL' })"
} else { $holdOk = $true }

# -----------------------------------------------------------------------------
# DWIPE -- both die: the HOST's reset runs, the guest's does not, the guest follows
# -----------------------------------------------------------------------------
if ($Phase -eq 'all' -or $Phase -eq 'wipe') {
$stack2 = [int](Get-DeathField $coopB 'stacks')
$wrA2 = [int](Get-StatusField $coopA 'worldresets'); if (-not $wrA2) { $wrA2 = 0 }
$wrB2 = [int](Get-StatusField $coopB 'worldresets'); if (-not $wrB2) { $wrB2 = 0 }
Write-Host "=== DWIPE baseline: worldresets A=$wrA2 B=$wrB2 stacks B=$stack2"
Send-Cmd $coopA 'die'
Send-Cmd $coopB 'die'
$bothDown = Wait-Until 20 {
    $da = Get-SyncValue $coopA 'death'; $db = Get-SyncValue $coopB 'death'
    ($da -match 'case:1') -or ($db -match 'case:1')
}
Show-Death 'A(wipe)' $coopA
Show-Death 'B(wipe)' $coopB
Write-Host "=== DWIPE case: $(if ($bothDown) { 'PASS a party wipe was seen' } else { 'FAIL (no case 1 seen)' })"
$hostReset = Wait-Until 60 { ([int](Get-StatusField $coopA 'worldresets')) -gt $wrA2 }
$guestFollow = Wait-Until 90 {
    ((Get-DeathField $coopA 'me') -eq '1(alive)') -and ((Get-DeathField $coopB 'me') -eq '1(alive)') -and
    ((Get-DeathField $coopA 'pending') -eq '0') -and ((Get-DeathField $coopB 'pending') -eq '0')
}
$wrA3 = [int](Get-StatusField $coopA 'worldresets'); if (-not $wrA3) { $wrA3 = 0 }
$wrB3 = [int](Get-StatusField $coopB 'worldresets'); if (-not $wrB3) { $wrB3 = 0 }
$stkA = Get-DeathField $coopA 'stacks'
$stkB = Get-DeathField $coopB 'stacks'
Show-Death 'A(after wipe)' $coopA
Show-Death 'B(after wipe)' $coopB
Write-Host "=== DWIPE: host reset A worldresets $wrA2->$wrA3 ; guest did NOT reset its own (B $wrB2->$wrB3)"
Write-Host "=== DWIPE: both alive=$guestFollow stacks A=$stkA B=$stkB"
$wipeOk = $bothDown -and $hostReset -and $guestFollow -and ($wrB3 -eq $wrB2) -and ($stkA -eq '0') -and ($stkB -eq '0')
Write-Host "=== DWIPE: $(if ($wipeOk) { 'PASS host reset, guest followed, both alive, 0 stacks' } else { 'FAIL' })"
} else { $wipeOk = $true }

# =============================================================================
# The evidence: the death lines of both clients (the run's own probe.logs).
foreach ($d in @($coopA, $coopB)) {
    Write-Host "--- $d probe.log (death lines) ---"
    $lg = Join-Path $d 'probe.log'
    if (Test-Path $lg) {
        Select-String -Path $lg -Pattern 'COOP DEATH|death\(|death:|RESPAWN|MISSING REVIVE|publishing lv|peer lv|veto' |
            Select-Object -Last 40 | ForEach-Object { $_.Line }
    }
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$evidence = Join-Path 'G:\Freespace\shell_coop\local\lab' "$stamp-death-two"
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
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

$pass = $linked -and $case2Ok -and $holdOk -and $wipeOk
if ($pass) { Write-Host "RESULT: PASS" } else { Write-Host "RESULT: FAIL" }
