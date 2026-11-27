# test-r10-probe.ps1 -- boot ONE client into the world and run a list of lab verbs.
#   -Cmds file: one lab verb per line; "sleep N" waits N seconds; "#" lines are skipped.
#   Every verb is written to coop\cmd.txt (in-DLL, no OS input). Owner saves are copied
#   to local\snapshots first and restored in finally.
param(
    [string]$Game = "G:\Games\_MS2-new-a",
    [Parameter(Mandatory = $true)][string]$Cmds,
    [string]$SaveFile = "C:\Users\ALVIBO\.t3\userdata\providers\antigravity\ac0a3dfd6dddb20962cecff6ee5fe65e19d3923be20e52c5ab52ff877f7e4c32\antigravity-acp\brain\8a67115d-3544-4299-8254-a5b1d045d5bd\scratch\saves\Savegame #5.sav",
    [string]$Tag = "r10_probe"
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
function Cmd([string]$text) {
    $f = Join-Path $coop 'cmd.txt'; $tmp = Join-Path $coop 'cmd.tmp'
    [IO.File]::WriteAllText($tmp, $text + "`n")
    for ($try = 0; $try -lt 5; $try++) { try { Move-Item -LiteralPath $tmp -Destination $f -Force; break } catch { Start-Sleep -Milliseconds 150 } }
    $due = (Get-Date).AddSeconds(12)
    while ((Get-Date) -lt $due) { if (-not (Test-Path -LiteralPath $f)) { break }; Start-Sleep -Milliseconds 200 }
}
# Passive capture only: PrintWindow into a bitmap. No focus change, no input.
Add-Type -Namespace Win32 -Name PW -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PrintWindow(System.IntPtr hWnd, System.IntPtr hdcBlt, uint nFlags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT lpRect);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
'@
Add-Type -AssemblyName System.Drawing
function Shot($proc, [string]$name) {
    try {
        $proc.Refresh(); $h = [IntPtr]$proc.MainWindowHandle
        if ($h -eq [IntPtr]::Zero) { Write-Host "shot: no window"; return }
        $r = New-Object Win32.PW+RECT; [Win32.PW]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        if ($w -le 0 -or $ht -le 0) { return }
        $bmp = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
        [Win32.PW]::PrintWindow($h, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose()
        $path = Join-Path $outDir ("shot-{0}-{1}.png" -f (Get-Date -Format 'HHmmss'), $name)
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        Write-Host "shot: $path"
    } catch { Write-Host "shot failed: $($_.Exception.Message)" }
}
function Stop-Ours {
    Get-Process | Where-Object { $_.ProcessName -match 'MortalShell2|CrashReportClient' -and $_.Path -like 'G:\Games\_MS2-new-*' } | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
    Start-Sleep 2
}
$seeded = $false; $hadWs = $false; $p = $null
try {
    Stop-Ours
    if (Test-Path $saveDir) { Copy-Item (Join-Path $saveDir '*') $backupDir -Recurse -Force }
    $hadWs = Test-Path (Join-Path $saveDir 'WorldState_0.sav')
    Copy-Item $SaveFile (Join-Path $saveDir 'WorldState_0.sav') -Force; $seeded = $true
    foreach ($pat in 'out_*.txt', 'peer_*.txt', 'ws_ledger_*.txt') { Get-ChildItem $coop -Filter $pat -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue }
    foreach ($f in 'sync.txt', 'cmd.txt', 'probe.log') { Remove-Item (Join-Path $coop $f) -Force -ErrorAction SilentlyContinue }
    [IO.File]::WriteAllText((Join-Path $coop 'AUTO_CONTINUE.txt'), "1")
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = Join-Path $bin 'MortalShell2-Win64-Shipping.exe'
    $psi.Arguments = "MortalShell2 -windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport $env:MS2_EXTRA_ARGS"
    $psi.WorkingDirectory = $Game; $psi.UseShellExecute = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 300) {
        if ($p.HasExited) { throw "client exited during boot (code $($p.ExitCode))" }
        if ((Sync 'ready') -eq '1') { break }
        Start-Sleep 2
    }
    Write-Host ("ready after {0:N0}s" -f ((Get-Date) - $t0).TotalSeconds)
    Start-Sleep 8
    foreach ($line in Get-Content $Cmds) {
        $l = $line.Trim()
        if (-not $l -or $l.StartsWith('#')) { continue }
        if ($p.HasExited) { Write-Host "client EXITED (code $($p.ExitCode))"; break }
        if ($l -match '^shot\s+(\S+)$') { Shot $p $Matches[1]; continue }
        if ($l -match '^sleep\s+([\d.]+)$') { Start-Sleep -Milliseconds ([int]([double]$Matches[1] * 1000)); continue }
        Write-Host "> $l"
        Cmd $l
        Start-Sleep -Milliseconds 300
    }
    Start-Sleep 2
    Write-Host ("RESULT: alive={0}" -f (-not $p.HasExited))
} finally {
    Copy-Item (Join-Path $bin 'ms2coop-native.log') (Join-Path $outDir 'native.log') -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coop 'sync.txt') (Join-Path $outDir 'sync.txt') -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coop 'probe.log') (Join-Path $outDir 'probe.log') -Force -ErrorAction SilentlyContinue
    Stop-Ours
    Remove-Item (Join-Path $coop 'AUTO_CONTINUE.txt') -Force -ErrorAction SilentlyContinue
    if ($seeded) {
        if (-not $hadWs) { Remove-Item (Join-Path $saveDir 'WorldState_0.sav') -Force -ErrorAction SilentlyContinue }
        Copy-Item (Join-Path $backupDir '*') $saveDir -Recurse -Force
        Write-Host "Restored owner saves from $backupDir"
    }
    Write-Host "Logs: $outDir"
}
