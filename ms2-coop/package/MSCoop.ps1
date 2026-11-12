# Mortal Shell II Co-op — play launcher (native module: dxgi.dll, no UE4SS)
#
#   Host:  .\MSCoop.ps1 -Host -GameDir "E:\Games\Mortal Shell II"
#   Join:  .\MSCoop.ps1 -Join -Lobby <id> -GameDir "E:\Games\Mortal Shell II"
#
# Starts this machine's relay first (Steam lobby), then the game, and keeps both
# alive. Close the console window to stop everything.
#
# Session identity is the mod's A/B contract: the native module pairs A with B
# (coop_net.cpp Start: peer = (me == "A") ? "B" : "A"), so -Host writes playerid A and
# -Join writes B. It also writes role.txt (host / guest) in the coop dir, which the
# module reads before anything else; without it both sides default to host and both
# claim every enemy / push host-only data layers.

param(
    # $Host is a read-only automatic variable in PowerShell, so the switch is declared
    # as HostRole with an alias: callers still pass -Host.
    [Alias('Host')][switch]$HostRole,
    [switch]$Join,
    [string]$Lobby = "",
    [string]$GameDir = "",
    [string]$PlayerId = ""
)

$ErrorActionPreference = 'Stop'

if (-not $HostRole -and -not $Join) { Write-Host "Use -Host or -Join -Lobby <id>"; exit 1 }
if ($Join -and -not $Lobby) { Write-Host "-Join needs -Lobby <id> (the host prints it)"; exit 1 }

# --- locate the game ---------------------------------------------------------
function Find-GameDir {
    $candidates = @(
        'E:\Games\Mortal Shell II',
        'D:\Games\Mortal Shell II',
        'C:\Program Files (x86)\Steam\steamapps\common\Mortal Shell II',
        'C:\Program Files\Epic Games\MortalShell2'
    )
    foreach ($c in $candidates) { if (Test-Path (Join-Path $c 'MortalShell2\Binaries\Win64')) { return $c } }
    return $null
}
if (-not $GameDir) {
    $GameDir = Find-GameDir
    if (-not $GameDir) { Write-Host "Game folder not found - pass -GameDir ""<path>"""; exit 1 }
}
$w64 = Join-Path $GameDir 'MortalShell2\Binaries\Win64'
$coop = Join-Path $w64 'coop'
$relay = Join-Path $coop 'relay\cooprelay.py'
if (-not (Test-Path $w64)) { throw "not a Mortal Shell II install: $GameDir" }
if (-not (Test-Path (Join-Path $w64 'dxgi.dll'))) {
    Write-Host "Native module missing (dxgi.dll not in $w64) - run .\Install.ps1 -GameDir ""$GameDir"" first"
    exit 1
}
foreach ($bad in @('dwmapi.dll', 'UE4SS.dll')) {
    if (Test-Path (Join-Path $w64 $bad)) {
        Write-Host "WARNING: $bad (UE4SS) is still in the game folder; run .\Install.ps1 to disable it"
    }
}
if (-not (Test-Path $relay)) { Write-Host "Relay missing - run .\Install.ps1 -GameDir ""$GameDir"" first"; exit 1 }
if (-not (Test-Path (Join-Path $w64 'coop_dir.txt'))) {
    Write-Host "coop_dir.txt missing - run .\Install.ps1 -GameDir ""$GameDir"" first"
    exit 1
}
New-Item -ItemType Directory -Force -Path $coop | Out-Null

# --- identity: A/B for the mod, host/guest for the role ----------------------
$expected = if ($HostRole) { 'A' } else { 'B' }
if (-not $PlayerId) { $PlayerId = $expected }
switch ($PlayerId.Trim().ToUpperInvariant()) {
    'HOST'  { $PlayerId = 'A' }
    'GUEST' { $PlayerId = 'B' }
    'A'     { $PlayerId = 'A' }
    'B'     { $PlayerId = 'B' }
    default {
        Write-Host "Player id must be A or B (the mod pairs A with B): '$PlayerId'"
        exit 1
    }
}
if ($PlayerId -ne $expected) {
    Write-Host "-Host is player A and -Join is player B (the mod pairs A with B); -PlayerId $PlayerId does not match"
    exit 1
}
$role = if ($HostRole) { 'host' } else { 'guest' }
Set-Content -Path (Join-Path $coop 'playerid.txt') -Value $PlayerId -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coop 'role.txt') -Value $role -Encoding ASCII -NoNewline
Write-Host "identity: player=$PlayerId role=$role"

$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) { Write-Host "Python 3 is required for the relay: https://www.python.org/downloads/"; exit 1 }

# --- relay -------------------------------------------------------------------
$relayArgs = @($relay, '--transport', 'steam', '--player', $PlayerId, '--dir', ('"' + $coop + '"'), '--game', ('"' + $GameDir + '"'))
if ($HostRole) { $relayArgs += @('--role', 'host') } else { $relayArgs += @('--role', 'client', '--lobby', $Lobby) }

Write-Host "starting relay..."
$relayProc = Start-Process -FilePath $py.Source -ArgumentList $relayArgs -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 4
if ($relayProc.HasExited) {
    Write-Host "relay failed to start - see $(Join-Path $coop "relay_$PlayerId.log")"
    exit 1
}

$lobbyOut = $null
$lobbyFile = Join-Path $coop "relay_$PlayerId.log"
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 500
    if (Test-Path $lobbyFile) {
        $m = Select-String -Path $lobbyFile -Pattern 'LOBBY=(\d+)' | Select-Object -Last 1
        if ($m) { $lobbyOut = $m.Matches[0].Groups[1].Value; break }
    }
}

if ($HostRole) {
    if ($lobbyOut) {
        Write-Host ""
        Write-Host "=================================================="
        Write-Host "  LOBBY ID: $lobbyOut"
        Write-Host "  Tell your friend to run:"
        Write-Host "     .\MSCoop.ps1 -Join -Lobby $lobbyOut"
        Write-Host "=================================================="
        Write-Host ""
    } else {
        Write-Host "No lobby id yet - check $lobbyFile"
    }
} else {
    Write-Host "joined (or joining) lobby $Lobby - check $lobbyFile"
}

# --- game --------------------------------------------------------------------
Write-Host "starting Mortal Shell II..."
$exe = Join-Path $w64 'MortalShell2.exe'
$rootExe = Join-Path $GameDir 'MortalShell2.exe'
$launch = if (Test-Path $rootExe) { $rootExe } else { $exe }
if (-not (Test-Path $launch)) {
    Write-Host "game launcher not found (looked for $rootExe and $exe)"
    try { Stop-Process -Id $relayProc.Id -Force -ErrorAction SilentlyContinue } catch {}
    exit 1
}
$game = Start-Process -FilePath $launch -WorkingDirectory $GameDir -PassThru

Write-Host ""
Write-Host "Both windows are running. Close this window to stop the session."
try {
    $game.WaitForExit()
} finally {
    Write-Host "game exited - stopping relay"
    try { Stop-Process -Id $relayProc.Id -Force -ErrorAction SilentlyContinue } catch {}
}

