# Deploy the MS2 co-op NATIVE module into a test game install. No UE4SS.
#
#   .\deploy.ps1 -GameDir "E:\Games\_MS2-coop-test"                 # deploy / update
#   .\deploy.ps1 -GameDir "E:\Games\_MS2-coop-test" -RestoreUE4SS   # undo, put UE4SS back
#
# native\build\dxgi.dll is the proxy DLL (see native\README.md §Deploy): it must sit
# in <GameDir>\MortalShell2\Binaries\Win64, next to MortalShell2-Win64-Shipping.exe,
# and the game must not load UE4SS at the same time (two proxies in one process).
# So this script, in order:
#   1. moves every UE4SS artefact into <Win64>\_ue4ss_disabled\ (nothing is deleted);
#   2. copies native\build\dxgi.dll into <Win64> (a different pre-existing dxgi.dll is
#      saved once as dxgi.dll.rollback before it is replaced);
#   3. writes <Win64>\coop_dir.txt and creates the coop\ state dir, exactly as before.
# It only ever touches <GameDir>; the build the owner plays from is never written.
#
# -Enable and -Probe are accepted and ignored: they were the old mods.txt switches,
# and old invocations (setup-install.ps1, docs/HANDOFF.md §2.2) still pass -Enable.
# -RestoreUE4SS moves the UE4SS files back and removes dxgi.dll (and the generated
# ms2coop-native.ini); the coop\ state dir is left alone.

param(
    [Parameter(Mandatory = $true)][string]$GameDir,
    [switch]$Enable,          # no-op, kept so old callers do not throw
    [switch]$Probe,           # no-op, kept so old callers do not throw
    [switch]$RestoreUE4SS,
    [switch]$SkipDll,         # disable UE4SS + write coop_dir.txt only
    [string]$DllPath = ""
)

$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
$w64 = Join-Path $GameDir 'MortalShell2\Binaries\Win64'
$exe = Join-Path $w64 'MortalShell2-Win64-Shipping.exe'
if (-not (Test-Path $w64)) { throw "not a Mortal Shell II install: $GameDir" }
if (-not (Test-Path $exe)) { throw "no shipping exe in $w64 (checked $exe)" }

$disabled = Join-Path $w64 '_ue4ss_disabled'
$dll = if ($DllPath) { $DllPath } else { Join-Path $src 'native\build\dxgi.dll' }
$rollback = Join-Path $w64 'dxgi.dll.rollback'

# Everything UE4SS puts next to the exe. Mods\ carries CoopMod/CoopLab, so it goes too:
# the Lua mod must not run alongside the DLL.
$ue4ssNames = @(
    'dwmapi.dll',              # UE4SS's own proxy
    'UE4SS.dll',
    'UE4SS-settings.ini',
    'UE4SS.log',
    'ue4ss',                   # UE4SS 3.x support dir
    'UE4SS_Signatures',
    'UE4SS_SDK_Backends',
    'Mods'                     # CoopMod, CoopLab, CoopProbe, mods.txt
)

function Get-Sha256([string]$path) {
    try { return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    catch { return $null }
}

function Move-ToDisabled([string]$name) {
    $item = Get-Item -LiteralPath (Join-Path $w64 $name) -ErrorAction SilentlyContinue
    if (-not $item) { return $false }
    New-Item -ItemType Directory -Force -Path $disabled | Out-Null
    $dst = Join-Path $disabled $item.Name
    if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Recurse -Force }
    Move-Item -LiteralPath $item.FullName -Destination $dst -Force
    Write-Host ("ue4ss disabled: {0} -> _ue4ss_disabled\{0}" -f $item.Name)
    return $true
}

# ---------------------------------------------------------------- restore -----
if ($RestoreUE4SS) {
    if (Test-Path -LiteralPath (Join-Path $w64 'dxgi.dll')) {
        Remove-Item -LiteralPath (Join-Path $w64 'dxgi.dll') -Force
        Write-Host "removed: dxgi.dll (native module)"
    }
    if (Test-Path -LiteralPath $rollback) {
        Move-Item -LiteralPath $rollback -Destination (Join-Path $w64 'dxgi.dll') -Force
        Write-Host "restored: dxgi.dll.rollback -> dxgi.dll"
    }
    Remove-Item -LiteralPath (Join-Path $w64 'ms2coop-native.ini') -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $disabled) {
        foreach ($item in @(Get-ChildItem -LiteralPath $disabled -Force)) {
            $dst = Join-Path $w64 $item.Name
            if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Recurse -Force }
            Move-Item -LiteralPath $item.FullName -Destination $dst -Force
            Write-Host ("ue4ss restored: {0}" -f $item.Name)
        }
        try { Remove-Item -LiteralPath $disabled -Force } catch {}
    }
    Write-Host "coop dir left in place: $(Join-Path $w64 'coop')"
    Write-Host "restore done: $GameDir"
    return
}

# ----------------------------------------------------------- deploy: ue4ss ----
$moved = 0
foreach ($name in $ue4ssNames) { if (Move-ToDisabled $name) { $moved++ } }
if ($moved -eq 0) { Write-Host "ue4ss: nothing to disable in $w64" }
else { Write-Host "ue4ss disabled: $moved item(s) kept in $disabled (deploy.ps1 -RestoreUE4SS puts them back)" }

# ------------------------------------------------------------- deploy: dll ----
if (-not $SkipDll) {
    if (-not (Test-Path -LiteralPath $dll)) {
        throw "native DLL not found: $dll - build it first: cd native; .\build.bat"
    }
    if ((Split-Path $dll -Leaf) -ne 'dxgi.dll') {
        throw "the proxy DLL must be named dxgi.dll (the name the exe imports): $dll"
    }
    $newHash = Get-Sha256 $dll
    $target = Join-Path $w64 'dxgi.dll'
    if (Test-Path -LiteralPath $target) {
        $oldHash = Get-Sha256 $target
        if ($oldHash -ne $newHash) {
            if (-not (Test-Path -LiteralPath $rollback)) {
                Copy-Item -LiteralPath $target -Destination $rollback -Force
                Write-Host "backup: dxgi.dll -> dxgi.dll.rollback (sha256 $oldHash)"
            } else {
                Write-Host "backup: dxgi.dll.rollback already exists, not overwritten"
            }
        }
    }
    Copy-Item -LiteralPath $dll -Destination $target -Force
    Write-Host "deployed: $target"
    Write-Host "  sha256: $newHash"
} else {
    Write-Host "dll: skipped (-SkipDll)"
}

# ------------------------------------------------------- coop dir (as before) --
# transport/state dir next to the exe, so two installs never share it.
# This file is the DLL's only config input (coop_net.cpp ResolveCoopDir).
$coopDir = Join-Path $w64 'coop'
New-Item -ItemType Directory -Force -Path $coopDir | Out-Null
Set-Content -Path (Join-Path $w64 'coop_dir.txt') -Value ($coopDir.Replace('\', '/') + '/') -Encoding ASCII -NoNewline
Write-Host "state dir: $coopDir"
Write-Host "done: $GameDir"
