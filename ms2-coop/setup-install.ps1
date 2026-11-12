# Set up a Mortal Shell II install for co-op modding/testing — NATIVE module, no UE4SS.
# Deploys native\build\dxgi.dll into the install (disabling any UE4SS into
# _ue4ss_disabled\), writes coop_dir.txt and points the module's state dir at this
# install, and drops the per-install player id into the coop\ dir.
#
# Usage: .\setup-install.ps1 -GameDir "E:\Games\_MS2-coop-b"
#        .\setup-install.ps1 -GameDir "E:\Games\_MS2-coop-b" -PlayerId B -SkipNative
#
# -SkipNative skips the DLL copy (UE4SS is still disabled, coop_dir.txt still written).
# -SkipUe4ss is accepted and ignored: the native module does not use UE4SS, and deploy.ps1
# disables whatever UE4SS is left in the install.

param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [string]$PlayerId = "",
    [switch]$SkipNative,
    [switch]$SkipUe4ss
)

$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
$w64 = Join-Path $GameDir 'MortalShell2\Binaries\Win64'
$coop = Join-Path $w64 'coop'

if (-not (Test-Path $w64)) { throw "not a Mortal Shell II install (no Binaries\Win64): $GameDir" }
if ($SkipUe4ss) { Write-Host "note: UE4SS is not used by the native module (-SkipUe4ss accepted)" }

$deployArgs = @{ GameDir = $GameDir }
if ($SkipNative) { $deployArgs['SkipDll'] = $true }
& (Join-Path $src 'deploy.ps1') @deployArgs

if ($PlayerId) {
    # per-install player id so two clients can share one machine's network dir.
    # The module reads <coop>\playerid.txt (coop_net.cpp Start), not Mods\CoopProbe.
    New-Item -ItemType Directory -Force -Path $coop | Out-Null
    Set-Content -Path (Join-Path $coop 'playerid.txt') -Value $PlayerId -Encoding ASCII -NoNewline
    Write-Host "player id: $PlayerId ($(Join-Path $coop 'playerid.txt'))"
}

Write-Host "ready: $GameDir"
