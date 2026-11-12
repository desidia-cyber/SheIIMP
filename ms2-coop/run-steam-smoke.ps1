# Steam transport smoke test: load the game's own steam_api64.dll, init AppID 480,
# create a friends-only lobby, log the lobby id, then stop.
# Transport only: the relay and the coop dir protocol are unchanged by the move from
# UE4SS to the native dxgi.dll module, so nothing here involves the game DLL or a mod.
# Usage: .\run-steam-smoke.ps1 [-Seconds 20] [-Game "E:\Games\Mortal Shell II"]
param(
    [int]$Seconds = 20,
    [string]$Game = 'E:\Games\Mortal Shell II',
    [string]$WorkDir = (Join-Path $PSScriptRoot 'steam-smoke')
)

$ErrorActionPreference = 'Stop'
$relay = Join-Path $PSScriptRoot 'relay\cooprelay.py'
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$coop = Join-Path $WorkDir 'coop'
New-Item -ItemType Directory -Force -Path $coop | Out-Null
Remove-Item (Join-Path $coop 'relay_A.log') -Force -ErrorAction SilentlyContinue

$out = Join-Path $WorkDir 'stdout.txt'
$err = Join-Path $WorkDir 'stderr.txt'
Remove-Item $out, $err -Force -ErrorAction SilentlyContinue

Write-Host "starting relay (steam, host) for ${Seconds}s..."
$p = Start-Process -FilePath 'python' `
    -ArgumentList @($relay, '--transport', 'steam', '--role', 'host', '--player', 'A', '--dir', ('"' + $coop + '"'), '--game', ('"' + $Game + '"')) `
    -WorkingDirectory $WorkDir -RedirectStandardOutput $out -RedirectStandardError $err -PassThru -WindowStyle Hidden

Start-Sleep -Seconds $Seconds
if (-not $p.HasExited) { try { Stop-Process -Id $p.Id -Force } catch {} }
Write-Host "relay exited: $($p.HasExited) code=$($p.ExitCode)"

Write-Host "=== stdout ==="
if (Test-Path $out) { Get-Content $out }
Write-Host "=== stderr ==="
if (Test-Path $err) { Get-Content $err }
Write-Host "=== relay log ==="
$log = Join-Path $coop 'relay_A.log'
if (Test-Path $log) { Get-Content $log }
