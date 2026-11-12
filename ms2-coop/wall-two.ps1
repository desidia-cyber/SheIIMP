# Two clients, screenshots of the OBSERVER's view while the other player goes
# through the squeeze traversal (round 8, "through the broken wall").
#
#   .\wall-two.ps1 [-GameA E:\Games\_MS2-coop-i] [-GameB E:\Games\_MS2-coop-j] [-Port 47140]
#
# B is moved to 8 positions around A (A's camera does NOT look along A's yaw, so the
# direction is searched as in visual-two.ps1); at every position B enters the game's
# own squeeze montage (`wallplay go`: UAnimInstance::Montage_Play with
# GA_Traversal_SqueezeThrough_NoWeaponStow_C's Montage) and A takes a burst of
# screenshots, so at least one row shows the puppet playing the traversal.
# At the end one contact sheet (sheet.png) is built and both coop logs are copied.
# Hold the game lock while running.
param(
    [string]$GameA = "E:\Games\_MS2-coop-i",
    [string]$GameB = "E:\Games\_MS2-coop-j",
    [int]$Port = 47140,
    [string]$Tag = "wall",
    [int]$BootWait = 120,
    [int]$Shots = 4,
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$realLocal = $env:LOCALAPPDATA
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outDir = Join-Path 'G:\Freespace\shell_coop\local\lab' ($stamp + "-$Tag")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace Win32 -Name V -MemberDefinition @'
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
        if ($_.MainWindowHandle -ne 0) { [Win32.V]::SetWindowPos($_.MainWindowHandle, [IntPtr]::Zero, -30000, -30000, 0, 0, 0x15) | Out-Null } }
}
function Shot([System.Diagnostics.Process]$proc, [string]$path) {
    $proc.Refresh(); $h = $proc.MainWindowHandle
    if ($h -eq 0) { Write-Host "shot: no window"; return }
    $r = New-Object Win32.V+RECT; [Win32.V]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
    if ($w -le 0 -or $ht -le 0) { return }
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [Win32.V]::PrintWindow($h, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc); $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
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
function Pose([string]$coop, [string]$id) {
    $f = Join-Path $coop "out_$id.txt"; if (-not (Test-Path $f)) { return $null }
    $t = Get-Content $f -Raw
    if ($t -match '"x":(-?[\d.]+),"y":(-?[\d.]+),"z":(-?[\d.]+).*?"yaw":(-?[\d.]+)') {
        return @{ x = [double]$matches[1]; y = [double]$matches[2]; z = [double]$matches[3]; yaw = [double]$matches[4] } }
    return $null
}
function InFront($p, [double]$d) {
    $r = $p.yaw * [math]::PI / 180.0
    return "{0:F0} {1:F0} {2:F0}" -f ($p.x + [math]::Cos($r) * $d), ($p.y + [math]::Sin($r) * $d), ($p.z + 50)
}
# write one command and wait for the module to CONSUME it (it deletes cmd.txt)
function Send([string]$coop, [string]$cmd) {
    $cf = Join-Path $coop 'cmd.txt'
    [IO.File]::WriteAllText($cf, $cmd + "`n")
    $due = (Get-Date).AddSeconds(12)
    while ((Get-Date) -lt $due -and (Test-Path $cf)) { Start-Sleep -Milliseconds 200 }
}

Stop-All; Start-Sleep 3
$realSaveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
$snap = Join-Path $env:TEMP ("ms2saves_w_" + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $snap | Out-Null
Copy-Item "$realSaveDir\*.sav" $snap -Force
$before = @(Get-ChildItem $realSaveDir -Filter *.sav | % Name)

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
foreach ($d in @($coopA, $coopB)) {
    Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'probe.log'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'peer_*.txt'), (Join-Path $d 'out_*.txt'), (Join-Path $d 'sync.txt'), (Join-Path $d 'heartbeat.txt') -Force -ErrorAction SilentlyContinue
    foreach ($o in @('WALLANIM_OFF.txt')) { Remove-Item (Join-Path $d $o) -Force -ErrorAction SilentlyContinue }
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
    Start-Sleep 8

    # ---- A watches B squeeze, from 8 directions around A ----
    $a = Pose $coopA 'A'
    if (-not $a) { throw "no pose from A" }
    for ($k = 0; $k -lt 8; $k++) {
        $q = @{ x = $a.x; y = $a.y; z = $a.z; yaw = $a.yaw + 45 * $k }
        $t = InFront $q 420
        Send $coopB "tp $t"
        Start-Sleep 3
        Hide-All; Shot $pa (Join-Path $outDir ("A-sees-B-dir{0}-still.png" -f ($k * 45)))
        Send $coopB "wallplay go"
        for ($i = 1; $i -le $Shots; $i++) {
            Start-Sleep -Milliseconds 420
            Hide-All
            Shot $pa (Join-Path $outDir ("A-sees-B-trav{0}-{1}.png" -f ($k * 45), $i))
            if ($i -eq 2) { Shot $pb (Join-Path $outDir ("B-self-trav{0}.png" -f ($k * 45))) }
        }
        Start-Sleep -Milliseconds 900
    }
    # the sync lines carry the counters the verdict is about
    foreach ($id in 'A', 'B') {
        $c = if ($id -eq 'A') { $coopA } else { $coopB }
        Copy-Item (Join-Path $c 'probe.log') (Join-Path $outDir "probe_$id.log") -ErrorAction SilentlyContinue
        Copy-Item (Join-Path $c 'sync.txt') (Join-Path $outDir "sync_$id.txt") -ErrorAction SilentlyContinue
    }
    Write-Host "alive=$(@(Get-Process | ? ProcessName -match 'MortalShell2-Win64').Count)"
}
finally {
    Stop-All; Start-Sleep 2
    Get-ChildItem $realSaveDir -Filter *.sav | ? { $before -notcontains $_.Name } | Remove-Item -Force
    Copy-Item "$snap\*.sav" $realSaveDir -Force
    Write-Host "saves restored"
}

# ---- contact sheet: one row per direction, one column per frame ----
try {
    $files = @(Get-ChildItem $outDir -Filter 'A-sees-B-trav*.png' | Sort-Object Name)
    if ($files.Count -gt 0) {
        $cw = 320; $ch = 180
        $cols = 4; $rows = [math]::Ceiling($files.Count / $cols)
        $sheet = New-Object System.Drawing.Bitmap ($cw * $cols), ($ch * $rows)
        $g = [System.Drawing.Graphics]::FromImage($sheet)
        $g.Clear([System.Drawing.Color]::Black)
        for ($i = 0; $i -lt $files.Count; $i++) {
            $img = [System.Drawing.Image]::FromFile($files[$i].FullName)
            $x = ($i % $cols) * $cw; $y = [math]::Floor($i / $cols) * $ch
            $g.DrawImage($img, (New-Object System.Drawing.Rectangle $x, $y, $cw, $ch))
            $img.Dispose()
        }
        $g.Dispose()
        $sheet.Save((Join-Path $outDir 'sheet.png'), [System.Drawing.Imaging.ImageFormat]::Png)
        $sheet.Dispose()
        Write-Host "sheet: $(Join-Path $outDir 'sheet.png') ($($files.Count) frames)"
    }
} catch { Write-Host "sheet failed: $_" }
Write-Host "out=$outDir"
