# Mortal Shell II Co-op v1.1 — standalone drop-in installer (dxgi.dll only, no UE4SS, no Python)
# Copies dxgi.dll next to MortalShell2-Win64-Shipping.exe and moves any conflicting UE4SS files
# into _ue4ss_disabled\ (nothing is deleted).
#
# Usage:
#   .\Install.ps1 -GameDir "E:\Games\Mortal Shell II"
#   .\Install.ps1 -GameDir "E:\Games\Mortal Shell II" -Force

param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$files = Join-Path $root 'files'
$w64 = Join-Path $GameDir 'MortalShell2\Binaries\Win64'

if (-not (Test-Path $w64)) { throw "Not a Mortal Shell II install (no MortalShell2\Binaries\Win64): $GameDir" }
if (-not (Test-Path (Join-Path $w64 'MortalShell2-Win64-Shipping.exe'))) {
    throw "No MortalShell2-Win64-Shipping.exe in $w64 - dxgi.dll must sit next to it"
}
if (-not (Test-Path $files)) { throw "Package is incomplete: $files missing" }

Write-Host "Mortal Shell II Co-op v1.1 Installer"
Write-Host "  game : $GameDir"

# --- standalone native proxy DLL ---
$dllSrc = Join-Path $files 'dxgi.dll'
if (-not (Test-Path $dllSrc)) { throw "Package is incomplete: files\dxgi.dll missing" }
$dllDst = Join-Path $w64 'dxgi.dll'
$rollback = Join-Path $w64 'dxgi.dll.rollback'
$newHash = (Get-FileHash $dllSrc -Algorithm SHA256).Hash.ToLowerInvariant()

if (Test-Path $dllDst) {
    $oldHash = (Get-FileHash $dllDst -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($oldHash -eq $newHash) {
        Write-Host "  dxgi.dll already up to date (sha256 $newHash)"
    } elseif (-not $Force) {
        Write-Host "  dxgi.dll already present and different (re-run with -Force to overwrite)"
    } else {
        if (-not (Test-Path $rollback)) {
            Copy-Item $dllDst $rollback -Force
            Write-Host "  backup: dxgi.dll -> dxgi.dll.rollback (sha256 $oldHash)"
        }
        Copy-Item $dllSrc $dllDst -Force
        Write-Host "  installed: dxgi.dll (sha256 $newHash)"
    }
} else {
    Copy-Item $dllSrc $dllDst -Force
    Write-Host "  installed: dxgi.dll (sha256 $newHash)"
}

# --- disable conflicting UE4SS proxy if present ---
$disabled = Join-Path $w64 '_ue4ss_disabled'
foreach ($name in @('dwmapi.dll', 'UE4SS.dll', 'UE4SS-settings.ini', 'UE4SS.log', 'ue4ss', 'UE4SS_Signatures', 'UE4SS_SDK_Backends', 'Mods')) {
    $item = Get-Item -LiteralPath (Join-Path $w64 $name) -ErrorAction SilentlyContinue
    if (-not $item) { continue }
    New-Item -ItemType Directory -Force -Path $disabled | Out-Null
    $dst = Join-Path $disabled $item.Name
    if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
    Move-Item -LiteralPath $item.FullName -Destination $dst -Force
    Write-Host ("  ue4ss disabled: {0} -> _ue4ss_disabled\{0}" -f $item.Name)
}

Write-Host ""
Write-Host "DONE! Launch Mortal Shell II normally and press F9 in-game to open the CO-OP menu."
Write-Host "  - Steam P2P: set the same Co-op Password on both PCs, then Host / Join."
Write-Host "  - LAN UDP  : switch Network to LAN UDP, set host IP & port, then Host / Join."
