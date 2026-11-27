# test-r10-solo-boot.ps1 -- does ONE client boot into the world and stay up?
#   Optionally swaps in another dxgi.dll for the run (restored in finally).
#   Owner saves are copied to local\snapshots first and restored in finally.
param(
    [string]$Game = "G:\Games\_MS2-new-a",
    [string]$Dll = "",
    [string]$SaveFile = "C:\Users\ALVIBO\.t3\userdata\providers\antigravity\ac0a3dfd6dddb20962cecff6ee5fe65e19d3923be20e52c5ab52ff877f7e4c32\antigravity-acp\brain\8a67115d-3544-4299-8254-a5b1d045d5bd\scratch\saves\Savegame #5.sav",
    [int]$StayUp = 60,
    [string]$Tag = "r10_soloboot"
)
$ErrorActionPreference = 'Stop'
$bin = Join-Path $Game 'MortalShell2\Binaries\Win64'
$coop = Join-Path $bin 'coop'
$saveDir = Join-Path $env:LOCALAPPDATA 'MortalShell2\Saved\SaveGames'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupDir = "G:\Freespace\shell_coop\local\snapshots\saves-$stamp-$Tag"
$outDir = Join-Path "G:\Freespace\shell_coop\local\lab" "$stamp-$Tag"
New-Item -ItemType Directory -Force -Path $outDir, $backupDir | Out-Null
function Sync([string]$key) {
    $f = Join-Path $coop 'sync.txt'
    if (-not (Test-Path $f)) { return $null }
    try { foreach ($l in [IO.File]::ReadAllLines($f)) { if ($l.StartsWith("$key=")) { return $l.Substring($key.Length + 1) } } } catch {}
    return $null
}
$swapped = $false; $seeded = $false; $hadWs = $false; $p = $null
try {
    Get-Process | Where-Object { $_.ProcessName -match 'MortalShell2|CrashReportClient' -and ($_.Path -like 'G:\Games\_MS2-new-*' -or $_.Path -like 'G:\Games\_MS2-new-*') } | ForEach-Object { Stop-Process -Id $_.Id -Force }
    if ($Dll) {
        Copy-Item (Join-Path $bin 'dxgi.dll') (Join-Path $outDir 'dxgi.current.dll') -Force
        Copy-Item $Dll (Join-Path $bin 'dxgi.dll') -Force; $swapped = $true
        Write-Host "Swapped in $Dll"
    }
    if (Test-Path $saveDir) { Copy-Item (Join-Path $saveDir '*') $backupDir -Recurse -Force }
    $hadWs = Test-Path (Join-Path $saveDir 'WorldState_0.sav')
    Copy-Item $SaveFile (Join-Path $saveDir 'WorldState_0.sav') -Force; $seeded = $true
    foreach ($f in 'sync.txt', 'cmd.txt') { Remove-Item (Join-Path $coop $f) -Force -ErrorAction SilentlyContinue }
    [IO.File]::WriteAllText((Join-Path $coop 'AUTO_CONTINUE.txt'), "1")
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = Join-Path $bin 'MortalShell2-Win64-Shipping.exe'
    $psi.Arguments = "MortalShell2 -windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport $env:MS2_EXTRA_ARGS"
    $psi.WorkingDirectory = $Game; $psi.UseShellExecute = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $t0 = Get-Date; $readyAt = $null
    while (((Get-Date) - $t0).TotalSeconds -lt 300) {
        if ($p.HasExited) { break }
        if (-not $readyAt -and (Sync 'ready') -eq '1') { $readyAt = Get-Date; Write-Host ("ready after {0:N0}s" -f ($readyAt - $t0).TotalSeconds) }
        if ($readyAt -and ((Get-Date) - $readyAt).TotalSeconds -ge $StayUp) { break }
        Start-Sleep 2
    }
    $res = if ($p.HasExited) { "EXITED after {0:N0}s (code {1})" -f ((Get-Date) - $t0).TotalSeconds, $p.ExitCode } elseif ($readyAt) { "UP: in world, alive $StayUp s after ready" } else { "NOT READY in 300s" }
    Write-Host "RESULT: $res"
} finally {
    Copy-Item (Join-Path $bin 'ms2coop-native.log') (Join-Path $outDir 'native.log') -Force -ErrorAction SilentlyContinue
    Get-Process | Where-Object { $_.ProcessName -match 'MortalShell2|CrashReportClient' -and ($_.Path -like 'G:\Games\_MS2-new-*' -or $_.Path -like 'G:\Games\_MS2-new-*') } | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
    Start-Sleep 2
    Remove-Item (Join-Path $coop 'AUTO_CONTINUE.txt') -Force -ErrorAction SilentlyContinue
    if ($swapped) { Copy-Item (Join-Path $outDir 'dxgi.current.dll') (Join-Path $bin 'dxgi.dll') -Force; Write-Host "Restored current dxgi.dll" }
    if ($seeded) {
        if (-not $hadWs) { Remove-Item (Join-Path $saveDir 'WorldState_0.sav') -Force -ErrorAction SilentlyContinue }
        Copy-Item (Join-Path $backupDir '*') $saveDir -Recurse -Force
        Write-Host "Restored owner saves from $backupDir"
    }
    Write-Host "Logs: $outDir"
}
