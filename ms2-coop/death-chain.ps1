# death-chain.ps1 -- SEVERAL deaths in ONE session, with no world reload between them.
#
#   . G:\Freespace\shell_coop\local\gamelock.ps1 ; Lock-Game 'r8-deaths'
#   .\deploy.ps1 -GameDir "E:\Games\_MS2-coop-test" -DllPath .\native\build\dxgi.dll
#   .\deploy.ps1 -GameDir "E:\Games\_MS2-coop-b"    -DllPath .\native\build\dxgi.dll
#   .\death-chain.ps1 -GameA "E:\Games\_MS2-coop-test" -GameB "E:\Games\_MS2-coop-b" -Port 47110
#
# WHY (round 8 brief): the co-op death used to be one death per session.  `S_KillMe`
# answers ok=true and then does nothing from the second call on, so every verdict that
# needed a death ran in its own session -- and nobody ever measured what a SECOND death
# in the same live world does to the camera state and the death screen.  This run chains
# them:
#
#   CHAIN1   B dies (case 2) -> co-op respawn;  camera stack dumped before/after
#   CHAIN2   B dies AGAIN     -> co-op respawn;  proves a second death is possible
#   CHAIN3   A dies           -> co-op respawn;  the other direction
#   CHAINWIPE  both die       -> party wipe -> the HOST's own game death and reset
#   CHAINCAM the camera stack, measured: (a) no death camera is left on the stack after
#            a co-op respawn, (b) the game's death camera is PRESENT (or put back) for
#            the wipe, so the game's death screen has its camera.
#
# Verdicts are read from each client's probe.log (`camstack(...)`, `COOP DEATH #n`,
# `death ability`, `death camera present/restore`) and every death is SCREENSHOTTED
# (PrintWindow of the real game windows, same mechanism as visual-two.ps1) into
# G:\Freespace\shell_coop\local\lab\<stamp>-deathchain\ for a human look.
param(
    [Parameter(Mandatory = $true)][string]$GameA,
    [Parameter(Mandatory = $true)][string]$GameB,
    [int]$Port = 47110,
    [int]$BootWait = 150,
    # 5..20 s.  The death is detected at once; the camera stack settles within ~1.5 s.
    [int]$DeathSettle = 3,
    [string]$Tag = "deathchain",
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$realLocal = $env:LOCALAPPDATA
$outDir = Join-Path 'G:\Freespace\shell_coop\local\lab' ((Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Tag")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace Win32 -Name D -MemberDefinition @'
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
function Hide-GameWindow {
    Get-Process | Where-Object ProcessName -match 'MortalShell2-Win64' | ForEach-Object {
        if ($_.MainWindowHandle -ne 0) {
            [Win32.D]::SetWindowPos($_.MainWindowHandle, [IntPtr]::Zero, -30000, -30000, 0, 0, 0x0001 -bor 0x0010 -bor 0x0004) | Out-Null
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
    return $p
}
function Shot([string]$id, [string]$name) {
    $proc = Get-Process | Where-Object ProcessName -match 'MortalShell2-Win64' | Where-Object { $_.Id -eq $script:pids[$id] } | Select-Object -First 1
    if (-not $proc) { Write-Host "shot: $id not running"; return }
    $proc.Refresh(); $h = $proc.MainWindowHandle
    if ($h -eq 0) { Write-Host "shot: $id has no window"; return }
    $r = New-Object Win32.D+RECT; [Win32.D]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -le 0 -or $ht -le 0) { Write-Host "shot: $id has a zero-size window"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [Win32.D]::PrintWindow($h, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc); $g.Dispose()
    $path = Join-Path $outDir "$name-$id.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Write-Host "shot: $path"
}
function Sync([string]$coop, [string]$key) {
    $f = Join-Path $coop 'sync.txt'; if (-not (Test-Path $f)) { return $null }
    foreach ($l in Get-Content $f) { if ($l -like "$key=*") { return $l.Substring($key.Length + 1) } }
    return $null
}
function Get-StatusField([string]$coop, [string]$field) {
    $f = Join-Path $coop 'sync.txt'
    if (-not (Test-Path $f)) { return $null }
    $t = Get-Content $f -Raw
    if (-not $t) { return $null }
    if ($t -match "$field[=:]([^\s,]+)") { return $matches[1] }
    return $null
}
function Get-DeathField([string]$coop, [string]$field) {
    $d = Sync $coop 'death'
    if (-not $d) { return $null }
    if ($d -match "$field`:(\S+)") { return $matches[1] }
    return $null
}
# Writing on a timer OVERWRITES a command that has not been read yet (visual-two learned
# this the hard way): wait for cmd.txt to disappear -- that is the consume ack.
function Send-Cmd([string]$coop, [string]$cmd, [int]$waitSec = 12) {
    $cf = Join-Path $coop 'cmd.txt'
    for ($i = 0; $i -lt 60 -and (Test-Path $cf); $i++) { Start-Sleep -Milliseconds 200 }
    [System.IO.File]::WriteAllText($cf, $cmd + "`n")
    $due = (Get-Date).AddSeconds($waitSec)
    while ((Get-Date) -lt $due -and (Test-Path $cf)) { Start-Sleep -Milliseconds 150 }
    if (Test-Path $cf) { Write-Host "WARN: '$cmd' was never consumed" }
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
function DeathSerial([string]$coop) { return (Get-DeathField $coop 'serial') }
function Log-Line([string]$coop, [string]$pattern, [int]$last = 6) {
    $lg = Join-Path $coop 'probe.log'
    if (-not (Test-Path $lg)) { return @() }
    return @(Select-String -Path $lg -Pattern $pattern | Select-Object -Last $last | ForEach-Object { $_.Line })
}
function Log-Count([string]$coop, [string]$pattern) {
    $lg = Join-Path $coop 'probe.log'
    if (-not (Test-Path $lg)) { return 0 }
    return @(Select-String -Path $lg -Pattern $pattern).Count
}
function CamStack([string]$coop, [string]$why) {
    # The module's `camstack` verb writes result_camstack.txt; the log line is the same.
    Remove-Item (Join-Path $coop 'result_camstack.txt') -Force -ErrorAction SilentlyContinue
    Send-Cmd $coop "camstack $why"
    $f = Join-Path $coop 'result_camstack.txt'
    $due = (Get-Date).AddSeconds(6)
    while (-not (Test-Path $f) -and (Get-Date) -lt $due) { Start-Sleep -Milliseconds 200 }
    if (Test-Path $f) { return (Get-Content $f -Raw).Trim() } else { return $null }
}

# =============================================================================
# boot
# =============================================================================
Start-ReporterKiller
Stop-All
Start-Sleep -Seconds 3

$realSaveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
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
    Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'probe.log'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'peer_*.txt'), (Join-Path $d 'out_*.txt'), (Join-Path $d 'relay_*.log'), (Join-Path $d 'sync.txt'), (Join-Path $d 'heartbeat.txt') -Force -ErrorAction SilentlyContinue
    # The lab override and the kill switches must never leak in: SOLO_DEATH.txt would
    # make a solo death look like case 2, DEATH_OFF.txt turns every rule off, and
    # DEATHABILITY_OFF.txt would keep the round-8 death-ability bug in place.
    Remove-Item (Join-Path $d 'SOLO_DEATH.txt'), (Join-Path $d 'DEATH_OFF.txt'), (Join-Path $d 'DEATHABILITY_OFF.txt') -Force -ErrorAction SilentlyContinue
}
Set-Content -Path (Join-Path $coopA 'playerid.txt') -Value 'A' -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coopA 'role.txt') -Value 'host' -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coopB 'playerid.txt') -Value 'B' -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coopB 'role.txt') -Value 'guest' -Encoding ASCII -NoNewline

$script:pids = @{}
$pa = Start-Client $GameA $localA
$script:pids['A'] = $pa.Id
Start-Sleep -Seconds 8
$pb = Start-Client $GameB $localB
$script:pids['B'] = $pb.Id
$relay = Join-Path $root 'relay\cooprelay.py'
Start-Process -FilePath 'python' -ArgumentList @($relay, '--transport', 'udp', '--role', 'host', '--port', "$Port", '--player', 'A', '--dir', $coopA) -WindowStyle Hidden
Start-Sleep -Seconds 2
Start-Process -FilePath 'python' -ArgumentList @($relay, '--transport', 'udp', '--role', 'client', '--host', '127.0.0.1', '--port', "$Port", '--player', 'B', '--dir', $coopB) -WindowStyle Hidden

# Teardown lives in one place: a run that cannot link must NOT go on to "die" people in a
# half-booted world (that is how the first attempt produced a bogus solo wipe at 16:08).
function Stop-Run([string]$why) {
    Write-Host "=== ABORT: $why"
    Stop-ReporterKiller
    Stop-All
    if (Test-Path $saveSnap) {
        Get-ChildItem $realSaveDir -Filter *.sav | Where-Object { $saveNamesBefore -notcontains $_.Name } |
            Remove-Item -Force -ErrorAction SilentlyContinue
        Copy-Item (Join-Path $saveSnap "*.sav") $realSaveDir -Force
        Write-Host "owner saves restored from snapshot"
    }
    Write-Host "RESULT: FAIL"
    exit 1
}
function Client-Alive([string]$id) {
    return [bool](Get-Process -Id $script:pids[$id] -ErrorAction SilentlyContinue)
}

$until = (Get-Date).AddSeconds($BootWait)
$bootDead = $null
while ((Get-Date) -lt $until) {
    Hide-GameWindow; Start-Sleep -Milliseconds 700
    if (-not (Client-Alive 'A')) { $bootDead = 'A'; break }
    if (-not (Client-Alive 'B')) { $bootDead = 'B'; break }
    if ((Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') { break }
}

if ($bootDead) { Stop-Run "client $bootDead exited during the boot wait (this is the known Steam crash 0xC0000374 or an external kill; nothing was measured)" }

$linked = Wait-Until 120 {
    ((Sync $coopA 'phase') -eq 'sync') -and ((Sync $coopB 'phase') -eq 'sync') -and
    (Sync $coopA 'peerbody') -and (Sync $coopB 'peerbody') -and
    ((Get-DeathField $coopB 'peer') -ne '0(none)')
}
Write-Host "=== LINK: $(if ($linked) { 'PASS both clients in gameplay, peer seen' } else { 'FAIL (no link)' })"
if (-not $linked) { Stop-Run "no link: phase A=$(Sync $coopA 'phase') B=$(Sync $coopB 'phase') peer-body A=$(Sync $coopA 'peerbody') B=$(Sync $coopB 'peerbody')" }

# The camera stack BEFORE any death: the control for every later measurement.
$csA0 = CamStack $coopA 'baseline'
$csB0 = CamStack $coopB 'baseline'
Write-Host "=== CHAINCAM baseline A: $csA0"
Write-Host "=== CHAINCAM baseline B: $csB0"
Shot A '0-baseline'
Shot B '0-baseline'

$results = [ordered]@{}
$camNotes = New-Object System.Collections.ArrayList

# -----------------------------------------------------------------------------
# one co-op death: die, wait for the case, screenshot the death, wait for the
# co-op respawn, screenshot the body back, read the camera stack.
# `$who` = A or B.
# -----------------------------------------------------------------------------
function Invoke-ChainDeath([string]$who, [int]$index) {
    $coop = if ($who -eq 'A') { $coopA } else { $coopB }
    $serialBefore = [int](DeathSerial $coop)
    $wrBefore = [int](Get-StatusField $coop 'worldresets')
    Write-Host "--- $who death #$index (serial before $serialBefore, worldresets $wrBefore)"
    Send-Cmd $coop 'dabl'
    Send-Cmd $coop 'die'
    $sawCase2 = Wait-Until 25 { (Get-DeathField $coop 'case') -eq '2' }
    $sawSerial = Wait-Until 25 { [int](DeathSerial $coop) -gt $serialBefore }
    # A client that died mid-run must stop the run, not silently turn the next death into
    # a solo wipe.
    if (-not (Client-Alive 'A') -or -not (Client-Alive 'B')) {
        Stop-Run "a client exited during death #$index (A alive=$(Client-Alive 'A') B alive=$(Client-Alive 'B'))"
    }
    Start-Sleep -Seconds $DeathSettle
    Shot $who "$index-death"
    $stackDown = CamStack $coop "death-$index"
    $alive = Wait-Until 60 { (Get-DeathField $coop 'me') -eq '1(alive)' -and (Get-DeathField $coop 'pending') -eq '0' }
    Start-Sleep -Seconds 3
    Shot $who "$index-respawned"
    $stackUp = CamStack $coop "respawned-$index"
    $wrAfter = [int](Get-StatusField $coop 'worldresets')
    $serialAfter = [int](DeathSerial $coop)
    return @{ case2 = $sawCase2; serial = $sawSerial; alive = $alive; wr = $wrAfter; wrBefore = $wrBefore;
              stackDown = $stackDown; stackUp = $stackUp; serialAfter = $serialAfter }
}

# -----------------------------------------------------------------------------
# CHAIN1/2/3
# -----------------------------------------------------------------------------
$r1 = Invoke-ChainDeath 'B' 1
$r2 = Invoke-ChainDeath 'B' 2
$r3 = Invoke-ChainDeath 'A' 3

function Verdict-Line([string]$tag, [hashtable]$r, [string]$who) {
    $camOk = ($r.stackUp -match 'death=0')
    $noReset = ($r.wr -eq $r.wrBefore)
    $ok = $r.case2 -and $r.serial -and $r.alive -and $noReset
    Write-Host "=== ${tag}case: $(if ($r.case2) { 'PASS the death was decided as case 2' } else { 'FAIL no case 2' })"
    Write-Host "=== ${tag}serial: $($r.serial) (death serial after the death = $($r.serialAfter), worldresets $($r.wrBefore) -> $($r.wr))"
    Write-Host "=== ${tag}camera while down:  $($r.stackDown)"
    Write-Host "=== ${tag}camstatus: $(if ($r.stackDown -match 'death=[1-9]') { 'the game pushed a death camera for THIS death' } else { 'no death camera on the stack while down' })"
    Write-Host "=== ${tag}camera after respawn: $($r.stackUp)"
    Write-Host "=== ${tag}cam: $(if ($camOk) { 'PASS no death camera is left on the stack after the co-op respawn' } else { 'FAIL a death camera state is still on the stack' })"
    Write-Host "=== ${tag}: $(if ($ok) { "PASS $who died (case 2), respawned in the LIVE world, no world reset" } else { 'FAIL case2=' + $r.case2 + ' serial=' + $r.serial + ' alive=' + $r.alive + ' noReset=' + $noReset })"
    return ($ok -and $camOk)
}

$chain1 = Verdict-Line 'CHAIN1 ' $r1 'B'
$chain2 = Verdict-Line 'CHAIN2 ' $r2 'B (second death in the SAME session)'
$chain3 = Verdict-Line 'CHAIN3 ' $r3 'A'

# -----------------------------------------------------------------------------
# CHAINWIPE -- both die: the game's own death and the host's reset
# -----------------------------------------------------------------------------
$wrA0 = [int](Get-StatusField $coopA 'worldresets'); if (-not $wrA0) { $wrA0 = 0 }
$wrB0 = [int](Get-StatusField $coopB 'worldresets'); if (-not $wrB0) { $wrB0 = 0 }
Write-Host "=== CHAINWIPE baseline: worldresets A=$wrA0 B=$wrB0"
Send-Cmd $coopA 'camstack before-wipe'
Send-Cmd $coopB 'camstack before-wipe'
Send-Cmd $coopA 'die'
Send-Cmd $coopB 'die'
$bothDown = Wait-Until 25 {
    ($null -ne (Log-Line $coopA 'COOP DEATH #' 1)) -or ((Get-DeathField $coopA 'case') -eq '1') -or
    ((Get-DeathField $coopB 'case') -eq '1')
}
$wipeCase = Wait-Until 30 { (Get-DeathField $coopA 'case') -eq '1' -or (Get-DeathField $coopB 'case') -eq '1' }
# The game's death screen + death camera: the host is the one whose death is NOT
# suppressed, so its window is the one that must show the game's own death framing.
# R5 measured the screen late (CreateLoadingScreen ~8 s after the death, the in-place
# reset ~1.2 s after that), so the frames are taken across that window rather than once.
Start-Sleep -Seconds 2
Shot A '4-wipe-2s'
Shot B '4-wipe-2s'
$stackHostWipe = CamStack $coopA 'host-wipe'
Start-Sleep -Seconds 2
Shot A '4-wipe-4s'
Start-Sleep -Seconds 3
Shot A '4-wipe-7s'
Shot B '4-wipe-7s'
Start-Sleep -Seconds 2
Shot A '4-wipe-9s'
$hostReset = Wait-Until 90 { ([int](Get-StatusField $coopA 'worldresets')) -gt $wrA0 }
$bothUp = Wait-Until 120 {
    ((Get-DeathField $coopA 'me') -eq '1(alive)') -and ((Get-DeathField $coopB 'me') -eq '1(alive)') -and
    ((Get-DeathField $coopA 'pending') -eq '0') -and ((Get-DeathField $coopB 'pending') -eq '0')
}
Start-Sleep -Seconds 4
Shot A '4-wipe-after'
Shot B '4-wipe-after'
# The host's reset owns a loading screen; "back in gameplay" is only true once it is gone,
# and the log verdict alone has lied about this before (round 5's "no body, no UI").
Start-Sleep -Seconds 8
Shot A '4-wipe-settled'
Shot B '4-wipe-settled'
$hudA = Sync $coopA 'ready'
$playA = @(Log-Line $coopA 'play (ready|ready=false)' 3)
Write-Host "=== CHAINWIPE settled: A ready=$hudA ; A log: $($playA -join ' | ')"
$wrA1 = [int](Get-StatusField $coopA 'worldresets'); if (-not $wrA1) { $wrA1 = 0 }
$wrB1 = [int](Get-StatusField $coopB 'worldresets'); if (-not $wrB1) { $wrB1 = 0 }
$stkA = Get-DeathField $coopA 'stacks'
$stkB = Get-DeathField $coopB 'stacks'
Write-Host "=== CHAINWIPE: both down=$bothDown case1=$wipeCase host reset A $wrA0->$wrA1 (guest B $wrB0->$wrB1) both alive=$bothUp stacks A=$stkA B=$stkB"
Write-Host "=== CHAINWIPE host camera at the wipe: $stackHostWipe"

# The camera evidence, read from the logs (log-based half of the verdict):
#  * the host's unsuppressed death must have found (or put back) the game's death camera,
#  * no death camera may be left on the stack after a co-op respawn.
$presentA = Log-Count $coopA "death camera is present|camera restore"
$restoredA = Log-Count $coopA "camera restore \("
$warnA = Log-Count $coopA "NO death camera on the stack"
$stackAfterRow = Log-Line $coopA 'camstack\(after the co-op respawn\)' 4
$deathCamsAfterRespawn = @($stackAfterRow | Where-Object { $_ -notmatch 'death=0' }).Count
Write-Host "=== CHAINCAM: host death-camera checks=$presentA (restores=$restoredA, missing=$warnA) ; co-op respawns leaving a death camera=$deathCamsAfterRespawn"
$chainCam = ($warnA -eq 0) -and ($deathCamsAfterRespawn -eq 0) -and ($presentA -ge 1) -and ($stackHostWipe -match 'death=[1-9]')
Write-Host "=== CHAINCAM: $(if ($chainCam) { 'PASS the game still gets its death camera on a real death, and none is left behind after a co-op respawn' } else { 'FAIL (missing=' + $warnA + ' leftBehind=' + $deathCamsAfterRespawn + ' checks=' + $presentA + ' hostWipe=' + $stackHostWipe + ')' })"

$chainWipe = $bothDown -and $wipeCase -and $hostReset -and $bothUp -and ($wrB1 -eq $wrB0) -and ($stkA -eq '0') -and ($stkB -eq '0')
Write-Host "=== CHAINWIPE: $(if ($chainWipe) { 'PASS the game ran its own death and the host reset, the guest followed, both alive with 0 stacks' } else { 'FAIL' })"

# -----------------------------------------------------------------------------
# the evidence: logs + screenshots
# -----------------------------------------------------------------------------
foreach ($pair in @(@($coopA, 'A'), @($coopB, 'B'))) {
    foreach ($f in @('probe.log', 'sync.txt')) {
        $src2 = Join-Path $pair[0] $f
        if (Test-Path $src2) { Copy-Item $src2 (Join-Path $outDir "$($pair[1])-$f") -Force }
    }
}
Write-Host "screenshots + logs in $outDir"
foreach ($d in @($coopA, $coopB)) {
    $lg = Join-Path $d 'probe.log'
    if (Test-Path $lg) {
        Select-String -Path $lg -Pattern 'COOP DEATH #|COOP DEATH camera|COOP DEATH death ability|camstack|die:' |
            Select-Object -Last 45 | ForEach-Object { Write-Host $_.Line }
    }
}

Stop-ReporterKiller
Stop-All
if (Test-Path $saveSnap) {
    Get-ChildItem $realSaveDir -Filter *.sav | Where-Object { $saveNamesBefore -notcontains $_.Name } |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $saveSnap "*.sav") $realSaveDir -Force
    Write-Host "owner saves restored from snapshot"
}

$pass = $linked -and $chain1 -and $chain2 -and $chain3 -and $chainWipe -and $chainCam
if ($pass) { Write-Host "RESULT: PASS" } else { Write-Host "RESULT: FAIL" }
