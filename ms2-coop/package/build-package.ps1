# Build the standalone v1.0 friend-installable co-op package (drop-in dxgi.dll: no UE4SS, no Python relay).
#   .\build-package.ps1                 -> dist\ms2-coop-v1.0-<date>.zip
# Assembling: native\build\dxgi.dll, Install.ps1, README.md.

param(
    [string]$OutDir = (Join-Path $PSScriptRoot 'dist')
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$srcRoot = Split-Path $root -Parent      # ms2-coop
$nativeDll = Join-Path $srcRoot 'native\build\dxgi.dll'

if (-not (Test-Path $nativeDll)) {
    throw "native DLL not built: $nativeDll - run: cd `"$(Join-Path $srcRoot 'native')`"; .\build.bat"
}

$dllHash = (Get-FileHash $nativeDll -Algorithm SHA256).Hash.ToLowerInvariant()

$stage = Join-Path $env:TEMP ("ms2coop_v10_stage_" + (Get-Date -Format 'HHmmss'))
$files = Join-Path $stage 'files'
New-Item -ItemType Directory -Force -Path $files | Out-Null

# Standalone native proxy DLL (drop-in next to MortalShell2-Win64-Shipping.exe)
Copy-Item $nativeDll (Join-Path $files 'dxgi.dll') -Force
Write-Host "staged: dxgi.dll (sha256 $dllHash)"

# Installer + README
Copy-Item (Join-Path $root 'Install.ps1') $stage
if (Test-Path (Join-Path $root 'README.md')) { Copy-Item (Join-Path $root 'README.md') $stage }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$zip = Join-Path $OutDir ("ms2-coop-v1.0-" + (Get-Date -Format 'yyyyMMdd-HHmm') + ".zip")
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force

$size = [math]::Round((Get-Item $zip).Length / 1MB, 2)
Write-Host "package: $zip ($size MB)"
Write-Host "  dxgi.dll sha256: $dllHash"
Write-Host "  zip sha256     : $((Get-FileHash $zip -Algorithm SHA256).Hash)"
