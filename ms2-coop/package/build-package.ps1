# Build the friend-installable co-op package (NATIVE module, no UE4SS).
#   .\build-package.ps1                 -> dist\ms2-coop-<date>.zip
# Assembling: native\build\dxgi.dll, the relay, install/play scripts, README.

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

$stage = Join-Path $env:TEMP ("ms2coop_stage_" + (Get-Date -Format 'HHmmss'))
$files = Join-Path $stage 'files'
New-Item -ItemType Directory -Force -Path $files | Out-Null

# the proxy DLL (native\README.md §Deploy: dxgi.dll next to MortalShell2-Win64-Shipping.exe)
Copy-Item $nativeDll (Join-Path $files 'dxgi.dll') -Force
Write-Host "staged: dxgi.dll (sha256 $dllHash)"

# relay (unchanged by the port)
Copy-Item (Join-Path $srcRoot 'relay') (Join-Path $files 'relay') -Recurse
Get-ChildItem (Join-Path $files 'relay') -Filter '__pycache__' -Recurse -Directory -ErrorAction SilentlyContinue |
    Remove-Item -Recurse -Force

# scripts + readme
Copy-Item (Join-Path $root 'Install.ps1') $stage
Copy-Item (Join-Path $root 'MSCoop.ps1') $stage
if (Test-Path (Join-Path $root 'README.md')) { Copy-Item (Join-Path $root 'README.md') $stage }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$zip = Join-Path $OutDir ("ms2-coop-" + (Get-Date -Format 'yyyyMMdd-HHmm') + ".zip")
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -CompressionLevel Optimal
Remove-Item $stage -Recurse -Force

$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "package: $zip ($size MB)"
Write-Host "  dxgi.dll sha256: $dllHash"
Write-Host "  sha256: $((Get-FileHash $zip -Algorithm SHA256).Hash)"
