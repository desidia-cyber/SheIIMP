# test-r10-hitstun.ps1 -- does the GUEST's real hit stagger an enemy on the HOST?
#   Host A and guest B stand at the same open-world brigand (host owns it, B sees a mirror).
#   1. Baseline: A swings (injected attack) -> "hitlook local" shows the montage a real hit plays.
#   2. For each reaction mode M (hitmode M on A): B swings (injected attack) -> A logs
#      "hithook: remote hit ... mode=M" and "hitlook rx mode=M ... [REACTED]".
#   3. Lab: A `hitreact M <tag>` on the nearest enemy with the tag a real hit used.
# Owner saves are copied to local\snapshots first and restored in finally.
param(
    [string]$GameA = "G:\Games\_MS2-new-a",
    [string]$GameB = "G:\Games\_MS2-new-b",
    [string]$SaveFile = "C:\Users\ALVIBO\.t3\userdata\providers\antigravity\ac0a3dfd6dddb20962cecff6ee5fe65e19d3923be20e52c5ab52ff877f7e4c32\antigravity-acp\brain\8a67115d-3544-4299-8254-a5b1d045d5bd\scratch\saves\Savegame #5.sav",
    [int]$BootWait = 150,
    [int]$LanPort = 47799,
    [string]$Modes = "3,1,2",
    [string]$Camp = "66992 -125076 -44454",
    [string]$Tag = "r10_hitstun",
    [string]$Target = "Brigand"
)
$ErrorActionPreference = 'Stop'

# No OS input: attacks go through the in-DLL `inject` verb (Enhanced Input injection).

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
$script:AtkIA = ''
function Find-IA([string]$game, [string[]]$prefer) {
    $lines = (Grab $game 'ialist: ' 40) -join ' '
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
function Alive { return (-not $pA.HasExited) -and (-not $pB.HasExited) }
function Stand-Together {
    Cmd $coopA "enemytp $Target" 3
    $line = (Grab $GameA 'enemytp: (?!facing)' 1) -join ''
    Write-Host "  A: $line"
    $pos = Sync $coopA 'selfpos'
    if ($pos) { Cmd $coopB "tp $pos" 3 }
    Cmd $coopB "enemytp $Target" 3
    Write-Host ("  B: " + ((Grab $GameB 'enemytp: (?!facing)' 1) -join ''))
}

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
    Write-Host ("Booted: A ready={0} | B ready={1}" -f (Sync $coopA 'ready'), (Sync $coopB 'ready'))

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
    Grab $GameA 'ialist: ' 12 | ForEach-Object { Write-Host "  A: $_" }
    $script:AtkIA = Find-IA $GameA @('^IA_Attack_Primary$', '^IA_Attack_Primary_Unblocked$', 'LightAttack')
    Write-Host "  attack input action: $script:AtkIA"
    Cmd $coopA "tp $Camp" 2
    Cmd $coopB "tp $Camp" 8
    Cmd $coopA "enemydump" 2
    Grab $GameA 'enemydump' 6 | ForEach-Object { Write-Host "  A: $_" }

    Write-Host "`n=== 1. Baseline: host's own real hits ==="
    Stand-Together
    Attack $coopA 1
    Start-Sleep 2
    Grab $GameA 'inject: |hithook: local hit|hitlook local' 16 | ForEach-Object { Write-Host "  A: $_" }

    $tagLine = (Grab $GameA 'hithook: local hit' 1) -join ''
    $react = 'Event.Reaction.Hit.Light'
    if ($tagLine -match 'chosen=(\S+)' -and $Matches[1] -ne '') { $react = $Matches[1] }
    Write-Host "  real reaction tag: $react"

    foreach ($m in ($Modes -split ',')) {
        if (-not (Alive)) { Write-Host "A client exited; stopping"; break }
        Write-Host "`n=== 2. Guest real hits, host reaction mode $m ==="
        Cmd $coopA "hitmode $m" 1
        Stand-Together
        Attack $coopB 3
        Start-Sleep 2
        Grab $GameB 'inject: |hithook: local hit' 8 | ForEach-Object { Write-Host "  B: $_" }
        Grab $GameA "hithook: remote hit|hitlook rx mode=$m" 10 | ForEach-Object { Write-Host "  A: $_" }
        Write-Host ("  A hithook={0}" -f (Sync $coopA 'hithook'))
        Write-Host ("  B hithook={0}" -f (Sync $coopB 'hithook'))
    }

    Write-Host "`n=== 3. Lab reactions on the host's nearest enemy ==="
    foreach ($m in ($Modes -split ',')) {
        if (-not (Alive)) { break }
        Cmd $coopA "hitreact $m $react" 2
        Grab $GameA "hitreact: mode=$m|hitlook lab mode=$m" 3 | ForEach-Object { Write-Host "  A: $_" }
    }
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
