# [R7-BREAK2] VISUAL confirmation that BOTH clients really SHATTER the same breakable.
#
# Round 7's first version made the receiver's copy disappear (state + hidden + no
# collision); the owner wants the real thing on both sides.  This run:
#   1. puts both pawns in the barrel cluster,
#   2. reads each pawn's pose (out_<id>.txt carries x/y/z/yaw) and the nearest intact
#      breakable from A's probe.log (`breakdump` prints `d=.. at=(x y z)`),
#   3. teleports each pawn 450 uu from that breakable ALONG ITS OWN YAW, so the barrel
#      sits in front of the third-person camera,
#   4. takes BEFORE shots on both windows, breaks the barrel on A through the game's
#      own FilteredHit chain, then takes a BURST of both windows every 300 ms for
#      ~2.4 s (the fracture, the dust and the debris all live in that window),
#   5. dumps `breakstate`/`breakdump` on both clients afterwards.
#
# The contact sheet is the deliverable: log verdicts have lied on this project.
# Usage: .\break-lab.ps1 [-Tag break] [-BootWait 200]     (hold the game lock)
param(
    [string]$GameA = "E:\Games\_MS2-coop-i",
    [string]$GameB = "E:\Games\_MS2-coop-j",
    [string]$Tag = "break",
    [int]$BootWait = 200,
    [int]$Port = 47140,
    [string]$Spot = "45205 -130560 -41900",
    [string]$SpotB = "45396 -131050 -41900",
    [int]$Burst = 8,
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$realLocal = $env:LOCALAPPDATA
$outDir = Join-Path 'G:\Freespace\shell_coop\local\lab' ((Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Tag")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace Win32 -Name B -MemberDefinition @'
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

function Stop-All {
    Get-Process | ? ProcessName -match 'MortalShell2|CrashReportClient' | % { try { Stop-Process -Id $_.Id -Force } catch {} }
    Get-CimInstance Win32_Process -Filter "Name like 'python%'" | ? { $_.CommandLine -match 'cooprelay' } | % { try { Stop-Process -Id $_.ProcessId -Force } catch {} }
}
function Hide-All {
    Get-Process | ? ProcessName -match 'MortalShell2-Win64' | % {
        if ($_.MainWindowHandle -ne 0) { [Win32.B]::SetWindowPos($_.MainWindowHandle, [IntPtr]::Zero, -30000, -30000, 0, 0, 0x15) | Out-Null } }
}
function Shot([System.Diagnostics.Process]$proc, [string]$path) {
    $proc.Refresh(); $h = $proc.MainWindowHandle
    if ($h -eq 0) { Write-Host "shot: no window"; return }
    $r = New-Object Win32.B+RECT; [Win32.B]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -le 0 -or $ht -le 0) { Write-Host "shot: bad rect"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [Win32.B]::PrintWindow($h, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc); $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Write-Host "shot: $path"
}
function Start-Client([string]$gameDir, [string]$localAppData) {
    $exe = Join-Path $gameDir 'MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe'
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe; $psi.Arguments = "MortalShell2 $ExtraArgs"; $psi.WorkingDirectory = $gameDir
    $psi.UseShellExecute = $true
    $env:LOCALAPPDATA = $localAppData; $p = [System.Diagnostics.Process]::Start($psi); $env:LOCALAPPDATA = $realLocal
    return $p
}
function Sync([string]$coop, [string]$key) {
    $f = Join-Path $coop 'sync.txt'; if (-not (Test-Path $f)) { return $null }
    foreach ($l in Get-Content $f) { if ($l -like "$key=*") { return $l.Substring($key.Length + 1) } }
    return $null
}
# The mod reads cmd.txt and DELETES it; wait for that so two commands cannot collide.
function Cmd([string]$coop, [string]$text, [int]$wait = 3) {
    [IO.File]::WriteAllText((Join-Path $coop 'cmd.txt'), $text + "`n")
    $due = (Get-Date).AddSeconds(15)
    while ((Get-Date) -lt $due -and (Test-Path (Join-Path $coop 'cmd.txt'))) { Start-Sleep -Milliseconds 200 }
    Start-Sleep -Seconds $wait
}
function Pose([string]$coop, [string]$id) {
    $f = Join-Path $coop "out_$id.txt"; if (-not (Test-Path $f)) { return $null }
    $t = Get-Content $f -Raw
    if ($t -match '"x":(-?[\d.]+),"y":(-?[\d.]+),"z":(-?[\d.]+).*?"yaw":(-?[\d.]+)') {
        return @{ x = [double]$matches[1]; y = [double]$matches[2]; z = [double]$matches[3]; yaw = [double]$matches[4] }
    }
    return $null
}
# the nearest intact breakable from A's own log: "breakdump <cls> <name> key=.. d=NN at=(x y z) intact"
function NearestBarrel([string]$coop) {
    $f = Join-Path $coop 'probe.log'; if (-not (Test-Path $f)) { return $null }
    $best = $null; $bd = 1e12
    foreach ($l in Get-Content $f) {
        if ($l -notmatch 'breakdump .*d=(\d+) at=\((-?\d+) (-?\d+) (-?\d+)\)') { continue }
        $d = [double]$matches[1]
        if ($d -lt $bd) { $bd = $d; $best = @{ x = [double]$matches[2]; y = [double]$matches[3]; z = [double]$matches[4]; d = $d } }
    }
    return $best
}
# a standing spot 450 uu from the target ALONG THE PAWN'S OWN YAW, so the third-person
# camera (which sits behind the pawn and looks along it) has the target in front
function StandInFront($pose, $barrel, [double]$d) {
    $r = $pose.yaw * [math]::PI / 180.0
    return "{0:F0} {1:F0} {2:F0}" -f ($barrel.x - [math]::Cos($r) * $d), ($barrel.y - [math]::Sin($r) * $d), $pose.z
}

Stop-All; Start-Sleep 3
$realSaveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
$snap = Join-Path $env:TEMP ("ms2saves_b_" + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $snap | Out-Null
Copy-Item "$realSaveDir\*.sav" $snap -Force
$before = @(Get-ChildItem $realSaveDir -Filter *.sav | % Name)

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
foreach ($d in @($coopA, $coopB)) {
    Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'probe.log'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'peer_*.txt'), (Join-Path $d 'out_*.txt'), (Join-Path $d 'sync.txt'), (Join-Path $d 'heartbeat.txt') -Force -ErrorAction SilentlyContinue
}
Set-Content (Join-Path $coopA 'playerid.txt') 'A' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopA 'role.txt') 'host' -Encoding ASCII -NoNewline
Set-Content (Join-Path $coopB 'playerid.txt') 'B' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopB 'role.txt') 'guest' -Encoding ASCII -NoNewline

try {
    $pa = Start-Client $GameA (Join-Path $root 'local-appdata\A'); Start-Sleep 8
    $pb = Start-Client $GameB (Join-Path $root 'local-appdata\B')
    $relay = Join-Path $root 'relay\cooprelay.py'
    Start-Process python -ArgumentList @($relay, '--transport', 'udp', '--role', 'host', '--port', "$Port", '--player', 'A', '--dir', $coopA) -WindowStyle Hidden
    Start-Sleep 2
    Start-Process python -ArgumentList @($relay, '--transport', 'udp', '--role', 'client', '--host', '127.0.0.1', '--port', "$Port", '--player', 'B', '--dir', $coopB) -WindowStyle Hidden
    $until = (Get-Date).AddSeconds($BootWait)
    while ((Get-Date) -lt $until) {
        Hide-All; Start-Sleep -Milliseconds 700
        if ((Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') { break }
    }
    Write-Host "phase A=$(Sync $coopA 'phase') B=$(Sync $coopB 'phase')"
    Start-Sleep 6

    # 1. into the barrel cluster (these props stand ~170 uu ABOVE the spawn floor: a
    #    teleport below their own Z falls straight through the map -- measured, so aim
    #    slightly above their origin)
    Cmd $coopA "tp $Spot" 14
    Cmd $coopB "tp $SpotB" 14
    Cmd $coopA "breakdump all" 4
    $barrel = NearestBarrel $coopA
    if (-not $barrel) { Write-Host "no breakable in A's log -- aborting the aiming step" }
    else {
        Write-Host ("barrel at {0} {1} {2} (d={3})" -f $barrel.x, $barrel.y, $barrel.z, $barrel.d)
        $poseA = Pose $coopA 'A'; $poseB = Pose $coopB 'B'
        if ($poseA) {
            $sa = StandInFront $poseA $barrel 450
            Cmd $coopA "tp $sa" 8
            Write-Host "A -> $sa (450 uu from the barrel, along A's yaw $($poseA.yaw))"
        }
        if ($poseB) {
            $sb = StandInFront $poseB $barrel 450
            Cmd $coopB "tp $sb" 8
            Write-Host "B -> $sb (450 uu from the barrel, along B's yaw $($poseB.yaw))"
        }
    }
    Hide-All
    Shot $pa (Join-Path $outDir 'A-before1.png'); Start-Sleep -Milliseconds 300
    Shot $pa (Join-Path $outDir 'A-before2.png'); Start-Sleep -Milliseconds 300
    Shot $pb (Join-Path $outDir 'B-before1.png'); Start-Sleep -Milliseconds 300
    Shot $pb (Join-Path $outDir 'B-before2.png')

    # 2. break it on A through the game's own chain, and burst BOTH windows
    Cmd $coopA "breakdump all" 3
    [IO.File]::WriteAllText((Join-Path $coopA 'cmd.txt'), "brkbreak nearest`n")
    for ($i = 1; $i -le $Burst; $i++) {
        Shot $pa (Join-Path $outDir ("A-break{0:D2}.png" -f $i))
        Shot $pb (Join-Path $outDir ("B-break{0:D2}.png" -f $i))
        Start-Sleep -Milliseconds 300
    }
    Start-Sleep 2
    Shot $pa (Join-Path $outDir 'A-after.png')
    Shot $pb (Join-Path $outDir 'B-after.png')

    # 3. the state of the object on both sides
    Cmd $coopA "brkstate nearest" 3
    Cmd $coopB "brkstate nearest" 3
    Cmd $coopA "breakdump all" 3
    Cmd $coopB "breakdump all" 3

    Write-Host "=== brk A $(Sync $coopA 'brk')"
    Write-Host "=== brk B $(Sync $coopB 'brk')"
    foreach ($id in 'A', 'B') {
        $c = if ($id -eq 'A') { $coopA } else { $coopB }
        Copy-Item (Join-Path $c 'probe.log') $outDir -Force -ErrorAction SilentlyContinue
        Get-ChildItem $outDir -Filter probe.log | Rename-Item -NewName "probe_$id.log" -ErrorAction SilentlyContinue
        Copy-Item (Join-Path $c 'sync.txt') (Join-Path $outDir "sync_$id.txt") -Force -ErrorAction SilentlyContinue
        Copy-Item (Join-Path $c 'result_break.txt') (Join-Path $outDir "result_break_$id.txt") -Force -ErrorAction SilentlyContinue
    }
    Write-Host "alive=$(@(Get-Process | ? ProcessName -match 'MortalShell2-Win64').Count)"
}
finally {
    Stop-All; Start-Sleep 2
    Get-ChildItem $realSaveDir -Filter *.sav | ? { $before -notcontains $_.Name } | Remove-Item -Force
    Copy-Item "$snap\*.sav" $realSaveDir -Force
    Write-Host "saves restored; out=$outDir"
}
