# Verify that the mod does not spawn shell processes while the game runs.
# Launches one client (native module: dxgi.dll next to the exe, UE4SS disabled) for
# ~60 s, samples cmd.exe every 400 ms, prints the max seen, then the module's own log
# (coop\probe.log) and the native loader log (ms2coop-native.log next to the exe).
param(
    [string]$GameDir = 'E:\Games\_MS2-coop-test'
)

$ErrorActionPreference = 'Stop'
$w64 = Join-Path $GameDir 'MortalShell2\Binaries\Win64'
$exe = Join-Path $w64 'MortalShell2-Win64-Shipping.exe'
$coop = Join-Path $w64 'coop'
$nativeLog = Join-Path $w64 'ms2coop-native.log'

if (-not (Test-Path $exe)) { throw "no shipping exe in $w64" }
if (-not (Test-Path (Join-Path $w64 'dxgi.dll'))) {
    throw "native module not deployed in $w64 - run: .\deploy.ps1 -GameDir `"$GameDir`""
}
if (-not (Test-Path (Join-Path $w64 'coop_dir.txt'))) {
    throw "coop_dir.txt missing in $w64 - run: .\deploy.ps1 -GameDir `"$GameDir`""
}
foreach ($bad in @('dwmapi.dll', 'UE4SS.dll')) {
    if (Test-Path (Join-Path $w64 $bad)) { Write-Host "WARN: $bad (UE4SS) still present in $w64; run .\deploy.ps1 to disable it" }
}

Get-Process | Where-Object ProcessName -match 'MortalShell2' | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
Start-Sleep -Seconds 2

$baseline = (Get-Process cmd -ErrorAction SilentlyContinue | Measure-Object).Count
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = 'MortalShell2 -windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport'
$psi.WorkingDirectory = $GameDir
$psi.UseShellExecute = $true
$psi.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Normal
$p = [System.Diagnostics.Process]::Start($psi)
Write-Host "launched pid $($p.Id); sampling cmd.exe for 60s (baseline $baseline)"

$maxCmd = 0
for ($i = 0; $i -lt 150; $i++) {
    $c = (Get-Process cmd -ErrorAction SilentlyContinue | Measure-Object).Count
    if ($c -gt $maxCmd) { $maxCmd = $c }
    Start-Sleep -Milliseconds 400
}
Write-Host "MAXCMD=$maxCmd"

$log = Join-Path $coop 'probe.log'
if (Test-Path $log) {
    Write-Host "=== mod log tail ==="
    Get-Content $log | Select-Object -Last 6
} else {
    Write-Host "no mod log yet"
}
Write-Host "=== native log tail ==="
if (Test-Path $nativeLog) {
    Get-Content $nativeLog | Select-Object -Last 10
} else {
    Write-Host "no ms2coop-native.log - the DLL never ran"
}

Get-Process | Where-Object ProcessName -match 'MortalShell2' | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
Get-Process | Where-Object ProcessName -match 'CrashReportClient' | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
Write-Host "stopped"
