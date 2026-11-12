# Mortal Shell II Co-op — installer (native module: dxgi.dll, no UE4SS)
# Copies the co-op proxy DLL (dxgi.dll) next to MortalShell2-Win64-Shipping.exe, moves
# any UE4SS in that folder into _ue4ss_disabled\ (nothing is deleted), and creates the
# mod's state dir plus the relay.
#
# Reversible: delete dxgi.dll (and dxgi.dll.rollback, if it was created), move the
# files in _ue4ss_disabled\ back where they were, and delete coop\ to undo everything.
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
    throw "no MortalShell2-Win64-Shipping.exe in $w64 - the proxy DLL must sit next to it"
}
if (-not (Test-Path $files)) { throw "package is incomplete: $files missing" }

Write-Host "Mortal Shell II co-op installer"
Write-Host "  game : $GameDir"

# --- native module (the proxy DLL) -------------------------------------------
$dllSrc = Join-Path $files 'dxgi.dll'
if (-not (Test-Path $dllSrc)) { throw "package is incomplete: files\dxgi.dll missing" }
$dllDst = Join-Path $w64 'dxgi.dll'
$rollback = Join-Path $w64 'dxgi.dll.rollback'
$newHash = (Get-FileHash $dllSrc -Algorithm SHA256).Hash.ToLowerInvariant()

if (Test-Path $dllDst) {
    $oldHash = (Get-FileHash $dllDst -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($oldHash -eq $newHash) {
        Write-Host "  native module already installed (same dxgi.dll)"
    } elseif (-not $Force) {
        Write-Host "  dxgi.dll already present and different (use -Force to overwrite)"
    } else {
        if (-not (Test-Path $rollback)) {
            Copy-Item $dllDst $rollback -Force
            Write-Host "  backup: dxgi.dll -> dxgi.dll.rollback (sha256 $oldHash)"
        }
        Copy-Item $dllSrc $dllDst -Force
        Write-Host "  native module installed (dxgi.dll)"
    }
} else {
    Copy-Item $dllSrc $dllDst -Force
    Write-Host "  native module installed (dxgi.dll)"
}

# --- disable UE4SS -----------------------------------------------------------
# The DLL is a proxy and the game must not load a second one (UE4SS's dwmapi.dll) or
# the old Lua mod. Everything is moved, never deleted, into _ue4ss_disabled\.
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

# --- state dir + relay -------------------------------------------------------
$coopDir = Join-Path $w64 'coop'
New-Item -ItemType Directory -Force -Path $coopDir | Out-Null
Set-Content -Path (Join-Path $w64 'coop_dir.txt') -Value ($coopDir.Replace('\', '/') + '/') -Encoding ASCII -NoNewline
Write-Host "  state dir: $coopDir"

$relaySrc = Join-Path $files 'relay'
if (-not (Test-Path $relaySrc)) { throw "package is incomplete: files\relay missing" }
$relayDst = Join-Path $coopDir 'relay'
New-Item -ItemType Directory -Force -Path $relayDst | Out-Null
Copy-Item (Join-Path $relaySrc '*') $relayDst -Force
Write-Host "  relay installed"

Write-Host ""
Write-Host "  module log: $(Join-Path $w64 'ms2coop-native.log') (next to the exe)"
Write-Host "  mod log   : $(Join-Path $coopDir 'probe.log')"

# --- prerequisites -----------------------------------------------------------
Write-Host ""
$py = Get-Command python -ErrorAction SilentlyContinue
if ($py) { Write-Host "  python: $($py.Source)" } else { Write-Host "  WARNING: python not found - the relay needs Python 3.8+ (python.org)" }
$steam = Get-Process steam -ErrorAction SilentlyContinue
if ($steam) { Write-Host "  steam : running" } else { Write-Host "  WARNING: Steam is not running - start it before playing" }

Write-Host ""
Write-Host "DONE. Next:  .\MSCoop.ps1 -Host     (then invite your friend with the lobby id)"
