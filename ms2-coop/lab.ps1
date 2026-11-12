# Run a scripted session against the NATIVE co-op module (dxgi.dll), in the background,
# off-screen. There is no CoopLab any more and no Mods\mods.txt: the DLL reads the same
# coop\cmd.txt harness the Lua mod did, so commands are written there.
#
# Usage:
#   .\lab.ps1 -Commands "patrol 14@8,info@2,aitest@6" [-BootWait 95]
#   .\lab.ps1 -CommandFile G:\Freespace\shell_coop\local\lab\steps.txt
#
# The module must already be deployed to this install (.\deploy.ps1 -GameDir <dir>):
# dxgi.dll next to the exe, UE4SS moved to _ue4ss_disabled\, coop_dir.txt written.
# The game window is parked off-screen (never minimized: this title does not build its
# main-menu widget while minimized). Saves are snapshotted before the run and restored
# after.
#
# "Ready" replaces CoopLab's coop\ready.txt: the native module writes coop\heartbeat.txt
# at 1 Hz (coop_net.cpp Heartbeat) once net::Start() has run, and coop\sync.txt carries
# phase=<menu|world|summon|adopt|sync>. The run waits for the heartbeat, then for
# phase=sync (in gameplay with a puppet) without failing the run if it is slow.

param(
    [string]$Commands = "",
    [string]$CommandFile = "",
    [string]$GameDir = "E:\Games\_MS2-coop-test",
    [int]$BootWait = 95,
    [int]$StepWait = 8,
    [string]$Tag = "lab",
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)

$ErrorActionPreference = 'Stop'
$repo = 'G:\Freespace\shell_coop'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outDir = Join-Path $repo "local\lab\$stamp-$Tag"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$w64 = Join-Path $GameDir 'MortalShell2\Binaries\Win64'
$coop = Join-Path $w64 'coop'
$exe = Join-Path $w64 'MortalShell2-Win64-Shipping.exe'
$nativeLog = Join-Path $w64 'ms2coop-native.log'
if (-not (Test-Path $exe)) { throw "no shipping exe in $w64" }
if (-not (Test-Path (Join-Path $w64 'dxgi.dll'))) {
    throw "native module not deployed in $w64 - run: .\deploy.ps1 -GameDir `"$GameDir`""
}
foreach ($bad in @('dwmapi.dll', 'UE4SS.dll')) {
    if (Test-Path (Join-Path $w64 $bad)) {
        Write-Host "WARN: $bad (UE4SS) is still in $w64; run .\deploy.ps1 to disable it - two proxies in one process"
    }
}
New-Item -ItemType Directory -Force -Path $coop | Out-Null

# ---- state dir: the DLL's only config input ----
Set-Content -Path (Join-Path $w64 'coop_dir.txt') -Value ($coop.Replace('\', '/') + '/') -Encoding ASCII -NoNewline
Set-Content -Path (Join-Path $coop 'playerid.txt') -Value 'A' -Encoding ASCII -NoNewline

Add-Type -Namespace Win32 -Name W -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool ShowWindow(System.IntPtr hWnd, int nCmdShow);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetWindowPos(System.IntPtr hWnd, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PrintWindow(System.IntPtr hWnd, System.IntPtr hdcBlt, uint nFlags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT lpRect);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
'@
Add-Type -AssemblyName System.Drawing

function Save-WindowShot([string]$path) {
    $proc = Get-Process | Where-Object ProcessName -match 'MortalShell2-Win64' | Select-Object -First 1
    if (-not $proc -or $proc.MainWindowHandle -eq 0) { Write-Host "shot: no window"; return }
    $h = $proc.MainWindowHandle
    $r = New-Object Win32.W+RECT
    [Win32.W]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -le 0 -or $ht -le 0) { Write-Host "shot: bad rect"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [Win32.W]::PrintWindow($h, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    # how much colour is in the frame? all-black means PrintWindow captured nothing
    $sample = @()
    for ($x = 0; $x -lt $w; $x += [math]::Max(1, [int]($w / 16))) {
        for ($y = 0; $y -lt $ht; $y += [math]::Max(1, [int]($ht / 9))) {
            $c = $bmp.GetPixel($x, $y)
            $sample += ($c.R + $c.G + $c.B)
        }
    }
    $mean = ($sample | Measure-Object -Average).Average
    $bmp.Dispose()
    Write-Host ("shot: {0} ok={1} {2}x{3} meanRGB={4:N0}" -f (Split-Path $path -Leaf), $ok, $w, $ht, $mean)
}

function Hide-GameWindow {
    Get-Process | Where-Object ProcessName -match 'MortalShell2-Win64' | ForEach-Object {
        if ($_.MainWindowHandle -ne 0) {
            [Win32.W]::SetWindowPos($_.MainWindowHandle, [IntPtr]::Zero, -30000, -30000, 0, 0, 0x0001 -bor 0x0010 -bor 0x0004) | Out-Null
        }
    }
}
function Stop-Game {
    Get-Process | Where-Object ProcessName -match 'MortalShell2' | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
    Get-Process | Where-Object ProcessName -match 'CrashReportClient' | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
}
function Get-SyncValue([string]$coopDir, [string]$key) {
    $f = Join-Path $coopDir 'sync.txt'
    if (-not (Test-Path $f)) { return $null }
    foreach ($line in Get-Content $f) { if ($line -match "^$key=(.*)$") { return $matches[1] } }
    return $null
}

# ---- save protection ----
$sg = "$env:LOCALAPPDATA\MortalShell2\Saved\SaveGames"
$snap = Join-Path $env:TEMP ("ms2saves_" + $stamp)
if (Test-Path $sg) {
    New-Item -ItemType Directory -Force -Path $snap | Out-Null
    Copy-Item "$sg\*.sav" $snap -Force
    (Get-ChildItem "$sg\*.sav" | Get-FileHash -Algorithm SHA256 | ForEach-Object { "$($_.Hash)  $(Split-Path $_.Path -Leaf)" }) |
        Set-Content (Join-Path $outDir 'saves-before.txt') -Encoding ASCII
}
function Restore-Saves {
    if (Test-Path $snap) {
        Get-ChildItem $snap -Filter *.sav | ForEach-Object { Copy-Item $_.FullName $sg -Force }
        Get-ChildItem $sg -Filter *.sav | Where-Object { -not (Get-ChildItem $snap -Filter $_.Name -ErrorAction SilentlyContinue) } |
            ForEach-Object { try { Remove-Item $_.FullName -Force } catch {} }
        Write-Host "saves restored"
    }
}

# ---- launch ----
Stop-Game
Start-Sleep -Seconds 2
# coop\cmd.txt and the per-run status files; result_ledger.txt is persistent state and stays
foreach ($f in @('cmd.txt', 'probe.log', 'sync.txt', 'heartbeat.txt', 'result_anim.txt')) {
    Remove-Item (Join-Path $coop $f) -Force -ErrorAction SilentlyContinue
}

$argList = @('MortalShell2') + ($ExtraArgs -split '\s+' | Where-Object { $_ })
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
$psi.WorkingDirectory = $GameDir
$psi.UseShellExecute = $true
$killerFlag = Join-Path $env:TEMP ("ms2killer_" + $stamp + ".flag")
New-Item -ItemType File -Force -Path $killerFlag | Out-Null
$killerJob = Start-Job -ArgumentList $killerFlag -ScriptBlock {
    param($flag)
    while (Test-Path $flag) {
        Get-Process CrashReportClient -ErrorAction SilentlyContinue | ForEach-Object { try { Stop-Process -Id $_.Id -Force } catch {} }
        Start-Sleep -Milliseconds 150
    }
}
$p = [System.Diagnostics.Process]::Start($psi)
Write-Host "launched pid $($p.Id); boot wait ${BootWait}s; out=$outDir"

$deadline = (Get-Date).AddSeconds($BootWait)
while ((Get-Date) -lt $deadline) { Hide-GameWindow; Start-Sleep -Milliseconds 700 }

# wait for the native module's 1 Hz heartbeat (menu + shader compile can take a while
# on a cold start), then for phase=sync -- in gameplay with a puppet
$hb = Join-Path $coop 'heartbeat.txt'
$hbDeadline = (Get-Date).AddSeconds(240)
while (-not (Test-Path $hb) -and (Get-Date) -lt $hbDeadline) {
    Hide-GameWindow; Start-Sleep -Milliseconds 800
}
if (Test-Path $hb) { Write-Host "native module alive (heartbeat.txt)" } else { Write-Host "WARN: native module never wrote heartbeat.txt" }

$syncDeadline = (Get-Date).AddSeconds(90)
while ((Get-Date) -lt $syncDeadline) {
    if ((Get-SyncValue $coop 'phase') -eq 'sync') { break }
    Hide-GameWindow; Start-Sleep -Milliseconds 800
}
$phase = Get-SyncValue $coop 'phase'
if ($phase -eq 'sync') { Write-Host "native module in gameplay (phase=sync)" }
else { Write-Host "WARN: phase is '$phase', not sync yet" }

# ---- commands ----
$steps = @()
if ($CommandFile) {
    $steps = @(Get-Content $CommandFile | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('#') })
} elseif ($Commands) {
    $steps = @($Commands -split ',')
}
$stepIndex = 0
foreach ($raw in $steps) {
    $cmd = $raw.Trim()
    if (-not $cmd) { continue }
    $wait = $StepWait
    if ($cmd -match '^(.*)@(\d+)$') { $cmd = $matches[1].Trim(); $wait = [int]$matches[2] }
    $stepIndex++
    Write-Host "-> $cmd (wait ${wait}s)"
    [System.IO.File]::WriteAllText((Join-Path $coop 'cmd.txt'), $cmd + "`n")
    $until = (Get-Date).AddSeconds($wait)
    while ((Get-Date) -lt $until) { Hide-GameWindow; Start-Sleep -Milliseconds 500 }
    Save-WindowShot (Join-Path $outDir ("step{0:D2}.png" -f $stepIndex))
}

# the module writes its own verdict (coop_local.cpp: result_anim.txt = PASS when the
# puppet really animates); give it time to finish the experiment
$verdict = Join-Path $coop 'result_anim.txt'
$vDeadline = (Get-Date).AddSeconds(45)
while (-not (Test-Path $verdict) -and (Get-Date) -lt $vDeadline) { Hide-GameWindow; Start-Sleep -Milliseconds 600 }

# a short burst of frames, useful to see whether the puppet is actually animating
for ($i = 1; $i -le 4; $i++) {
    Save-WindowShot (Join-Path $outDir ("burst{0}.png" -f $i))
    Start-Sleep -Milliseconds 350
}

# grace period for verdicts
$grace = (Get-Date).AddSeconds(10)
while ((Get-Date) -lt $grace) { Hide-GameWindow; Start-Sleep -Milliseconds 500 }

Stop-Game
try { Remove-Item $killerFlag -Force -ErrorAction SilentlyContinue } catch {}
try { Stop-Job $killerJob -ErrorAction SilentlyContinue; Remove-Job $killerJob -Force -ErrorAction SilentlyContinue } catch {}
Restore-Saves

# ---- collect ----
Copy-Item (Join-Path $coop '*') $outDir -Force -ErrorAction SilentlyContinue
# the native loader/engine log lives next to the exe (UE4SS.log's replacement)
if (Test-Path $nativeLog) { Copy-Item $nativeLog (Join-Path $outDir 'ms2coop-native.log') -Force }
if (Test-Path (Join-Path $coop 'probe.log')) {
    Write-Host "=== probe.log (tail) ==="
    Get-Content (Join-Path $coop 'probe.log') | Select-Object -Last 40
}
if (Test-Path (Join-Path $outDir 'ms2coop-native.log')) {
    Write-Host "=== ms2coop-native.log (tail) ==="
    Get-Content (Join-Path $outDir 'ms2coop-native.log') | Select-Object -Last 40
}
if (Test-Path (Join-Path $outDir 'sync.txt')) {
    Write-Host "=== sync.txt ==="
    Get-Content (Join-Path $outDir 'sync.txt') | Select-Object -First 80
}
foreach ($f in @('result_anim.txt', 'result_ws.txt', 'result_wp.txt', 'result_qs.txt', 'result_qs2.txt', 'result_lift.txt', 'result_chest.txt', 'result_ai.txt', 'result_npc.txt', 'result_pd.txt', 'result_spawn.txt', 'result_stream.txt', 'result_ledger.txt')) {
    $fp = Join-Path $outDir $f
    if (Test-Path $fp) { Write-Host "=== $f ==="; Get-Content $fp | Select-Object -First 80 }
}
Write-Host "results in $outDir"
