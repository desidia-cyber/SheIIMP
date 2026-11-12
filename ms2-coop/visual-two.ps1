# Two clients, side by side, with SCREENSHOTS of each window -- for checking what the
# puppet actually looks like (run-two's verdicts only read logs).
#   .\visual-two.ps1 [-Off "BODY_OFF.txt,ZONE_OFF.txt"] [-Tag name] [-Shots 6]
# B is teleported ~350 uu in front of A (A's camera looks along A's yaw), then walks
# (patrol) so A's view shows the puppet idle, moving and with its weapons; then A is
# brought in front of B for B's view. Kill-switch files in -Off are created in both
# coop dirs before launch and removed afterwards. Hold the game lock while running.
param(
    [string]$GameA = "E:\Games\_MS2-coop-c",
    [string]$GameB = "E:\Games\_MS2-coop-d",
    [string]$Off = "",
    [string]$Tag = "visual",
    [int]$Shots = 6,
    [int]$BootWait = 95,
    [int]$Port = 47020,
    [string]$Cmds = "",
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$realLocal = $env:LOCALAPPDATA
$outDir = Join-Path 'G:\Freespace\shell_coop\local\lab' ((Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Tag")
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
    $proc.Refresh()
    # A client that died (or has not created its window yet) hands back 0 -- and on
    # a process that already exited, MainWindowHandle is the EMPTY STRING, which
    # GetWindowRect then refuses ("Cannot convert argument hWnd ..."), aborting the
    # whole run before any verdict.  Treat all three as "no shot", never as fatal.
    $h = $proc.MainWindowHandle
    if (-not $h -or $h -eq [IntPtr]::Zero) { Write-Host "shot: no window ($path)"; return }
    try {
        $h = [IntPtr]$h
        $r = New-Object Win32.V+RECT; [Win32.V]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        if ($w -le 0 -or $ht -le 0) { Write-Host "shot: empty rect ($path)"; return }
        $bmp = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
        [Win32.V]::PrintWindow($h, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        Write-Host "shot: $path"
    } catch {
        Write-Host "shot: failed ($path): $($_.Exception.Message)"
    }
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

Stop-All; Start-Sleep 3
$realSaveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
$snap = Join-Path $env:TEMP ("ms2saves_v_" + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $snap | Out-Null
Copy-Item "$realSaveDir\*.sav" $snap -Force
$before = @(Get-ChildItem $realSaveDir -Filter *.sav | % Name)

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
$offFiles = @($Off -split ',' | ? { $_ })
foreach ($d in @($coopA, $coopB)) {
    Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'probe.log'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'peer_*.txt'), (Join-Path $d 'out_*.txt'), (Join-Path $d 'sync.txt'), (Join-Path $d 'heartbeat.txt') -Force -ErrorAction SilentlyContinue
    foreach ($o in $offFiles) { Set-Content (Join-Path $d $o) 'visual-two' }
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
    if ($Cmds) {
        # The mod reads cmd.txt and DELETES it.  Writing the next command on a fixed
        # timer overwrites one that has not been read yet and the command is LOST (this
        # ate `bossfake 1` on one boss-hold run and `die` on the next), so each write
        # waits for the file to disappear -- that is the consume acknowledgement.
        foreach ($c in $Cmds -split ';') {
            $kv = $c -split '=', 2
            $cf = Join-Path $(if ($kv[0] -eq 'A') { $coopA } else { $coopB }) 'cmd.txt'
            [IO.File]::WriteAllText($cf, $kv[1] + "`n")
            $due = (Get-Date).AddSeconds(10)
            while ((Get-Date) -lt $due -and (Test-Path $cf)) { Start-Sleep -Milliseconds 200 }
            Start-Sleep 2
        }
    }
    if ($Cmds) {
        # what both screens show right after the commands (e.g. B held in a boss fight:
        # B's camera must ride on A's puppet while A walks around)
        Start-Sleep 6
        [IO.File]::WriteAllText((Join-Path $coopA 'cmd.txt'), "patrol 12`n")
        for ($i = 1; $i -le 4; $i++) { Start-Sleep 2; Hide-All; Shot $pb (Join-Path $outDir "B-after-cmds$i.png"); Shot $pa (Join-Path $outDir "A-after-cmds$i.png") }
        Copy-Item (Join-Path $coopB 'sync.txt') (Join-Path $outDir 'sync_B_after_cmds.txt') -ErrorAction SilentlyContinue
    }

    # ---- A's view of B ----
    $a = Pose $coopA 'A'
    if ($a) {
        # the camera does not look along the actor yaw: try 8 directions around A
        foreach ($k in 0..7) {
            $q = @{ x = $a.x; y = $a.y; z = $a.z; yaw = $a.yaw + 45 * $k }
            $t = InFront $q 380; [IO.File]::WriteAllText((Join-Path $coopB 'cmd.txt'), "tp $t`n")
            Start-Sleep 4; Hide-All; Shot $pa (Join-Path $outDir ("A-sees-B-dir{0}.png" -f ($k * 45)))
        }
    }
    [IO.File]::WriteAllText((Join-Path $coopB 'cmd.txt'), "patrol 12`n")
    for ($i = 1; $i -le $Shots; $i++) { Start-Sleep -Milliseconds 1500; Hide-All; Shot $pa (Join-Path $outDir "A-sees-B-move$i.png") }
    Start-Sleep 8
    # ---- B's view of A ----
    $b = Pose $coopB 'B'
    if ($b) { $t = InFront $b 380; [IO.File]::WriteAllText((Join-Path $coopA 'cmd.txt'), "tp $t`n"); Write-Host "A -> $t (in front of B)" }
    Start-Sleep 6
    Shot $pb (Join-Path $outDir 'B-sees-A-idle.png')
    [IO.File]::WriteAllText((Join-Path $coopA 'cmd.txt'), "patrol 10`n")
    for ($i = 1; $i -le 3; $i++) { Start-Sleep -Milliseconds 1500; Hide-All; Shot $pb (Join-Path $outDir "B-sees-A-move$i.png") }
    Start-Sleep 2
    [IO.File]::WriteAllText((Join-Path $coopA 'cmd.txt'), "pupdump`n"); [IO.File]::WriteAllText((Join-Path $coopB 'cmd.txt'), "pupdump`n"); Start-Sleep 3
    foreach ($id in 'A', 'B') {
        $c = if ($id -eq 'A') { $coopA } else { $coopB }
        Copy-Item (Join-Path $c 'probe.log'), (Join-Path $c 'sync.txt') $outDir -ErrorAction SilentlyContinue
        Get-ChildItem $outDir -Filter probe.log | Rename-Item -NewName "probe_$id.log" -ErrorAction SilentlyContinue
        Get-ChildItem $outDir -Filter sync.txt | Rename-Item -NewName "sync_$id.txt" -ErrorAction SilentlyContinue
    }
    Write-Host "alive=$(@(Get-Process | ? ProcessName -match 'MortalShell2-Win64').Count)"
}
finally {
    Stop-All; Start-Sleep 2
    foreach ($d in @($coopA, $coopB)) { foreach ($o in $offFiles) { Remove-Item (Join-Path $d $o) -Force -ErrorAction SilentlyContinue } }
    Get-ChildItem $realSaveDir -Filter *.sav | ? { $before -notcontains $_.Name } | Remove-Item -Force
    Copy-Item "$snap\*.sav" $realSaveDir -Force
    Write-Host "saves restored; out=$outDir"
}
