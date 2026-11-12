# [R9-QUEST] open the JOURNAL on screen and photograph it, on BOTH clients.
#
# quest-stages.ps1 opens the game's own menu through
# BPC_UserInterfaceHandler::HandleGameMenu(SubTabIndex, AllowClose) -- the call
# IA_Menu_Game makes -- but the SubTabIndex argument does not move the visible tab
# (all six values land on INVENTORY).  This script therefore drives the UI the way a
# player does: REAL synthetic input to the focused window (keybd_event/mouse_event,
# the same path as SendInput) and a screenshot after every input, so the journal page
# can be looked at instead of inferred from the data.
#
#   .\journal-shot.ps1                 # open menu, Right x5
#   .\journal-shot.ps1 -ClickTabs      # open menu, then click the tab strip
#
# Writes to G:\Freespace\shell_coop\local\lab\<stamp>-journalshot\.
param(
    [string]$GameA = "E:\Games\_MS2-coop-g",
    [string]$GameB = "E:\Games\_MS2-coop-h",
    [int]$Port = 47130,
    [int]$BootWait = 100,
    [string]$Key = "RIGHT",
    [int]$Presses = 5,
    [switch]$ClickTabs,
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$who = 'r9-quest'
$realLocal = $env:LOCALAPPDATA
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outDir = Join-Path 'G:\Freespace\shell_coop\local\lab' "$stamp-journalshot"
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace Win32 -Name J -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PrintWindow(System.IntPtr hWnd, System.IntPtr hdcBlt, uint nFlags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr hWnd, out RECT lpRect);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetWindowPos(System.IntPtr hWnd, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetForegroundWindow(System.IntPtr hWnd);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool ShowWindow(System.IntPtr hWnd, int cmd);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern void keybd_event(byte vk, byte scan, uint flags, System.UIntPtr extra);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool SetCursorPos(int x, int y);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern void mouse_event(uint flags, int dx, int dy, uint data, System.UIntPtr extra);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left, Top, Right, Bottom; }
'@
Add-Type -AssemblyName System.Drawing

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
$logA = Join-Path $GameA 'MortalShell2\Binaries\Win64\ms2coop-native.log'
$logB = Join-Path $GameB 'MortalShell2\Binaries\Win64\ms2coop-native.log'
$VK = @{ RIGHT = 0x27; LEFT = 0x25; E = 0x45; Q = 0x51; TAB = 0x09; J = 0x4A }

function Stop-All {
    Get-Process | ? ProcessName -match 'MortalShell2|CrashReportClient' | % { try { Stop-Process -Id $_.Id -Force } catch {} }
    $pids = @()
    try {
        $out = cmd /c "wmic process where `"name='python.exe'`" get processid,commandline 2>nul"
        foreach ($l in @($out)) { if ($l -match 'cooprelay' -and $l -match '(\d+)\s*$') { $pids += [int]$matches[1] } }
    } catch {}
    try { Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue | % { $pids += [int]$_.OwningProcess } } catch {}
    foreach ($p in ($pids | Sort-Object -Unique)) { if ($p -gt 0) { try { Stop-Process -Id $p -Force -ErrorAction SilentlyContinue } catch {} } }
    Start-Sleep 2
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
function Send-Cmd([string]$coop, [string]$verb) {
    $cf = Join-Path $coop 'cmd.txt'
    [IO.File]::WriteAllText($cf, "$verb`n")
    $due = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $due -and (Test-Path $cf)) { Start-Sleep -Milliseconds 150 }
}
function Shot($proc, [string]$path) {
    try {
        $proc.Refresh(); $h = $proc.MainWindowHandle
        if (-not $h -or $h -eq [IntPtr]::Zero) { return }
        $h = [IntPtr]$h
        $r = New-Object Win32.J+RECT; [Win32.J]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        if ($w -le 0 -or $ht -le 0) { return }
        $bmp = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
        [Win32.J]::PrintWindow($h, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        Write-Host "shot: $path"
    } catch { Write-Host "shot failed: $($_.Exception.Message)" }
}
# The window is parked off-screen by the other scripts, so it is moved back to a fixed
# place first -- that is what makes "image coordinate -> screen coordinate" exact:
# the screenshot IS the window rect, so screen = (40,40) + image.
function Focus-Window($proc) {
    $proc.Refresh(); $h = $proc.MainWindowHandle
    if (-not $h -or $h -eq [IntPtr]::Zero) { return $false }
    $h = [IntPtr]$h
    [Win32.J]::ShowWindow($h, 9) | Out-Null                       # SW_RESTORE
    [Win32.J]::SetWindowPos($h, [IntPtr]::Zero, 40, 40, 1040, 615, 0x40) | Out-Null
    [Win32.J]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 350
    return $true
}
function Press-Key($proc, [string]$name, [int]$times = 1) {
    $vk = $VK[$name]
    if (-not $vk) { Write-Host "unknown key $name"; return }
    if (-not (Focus-Window $proc)) { Write-Host "no window for key"; return }
    for ($i = 0; $i -lt $times; $i++) {
        [Win32.J]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
        [Win32.J]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 500
    }
}
function Click-At($proc, [int]$x, [int]$y) {
    if (-not (Focus-Window $proc)) { Write-Host "no window for click"; return }
    [Win32.J]::SetCursorPos(40 + $x, 40 + $y) | Out-Null
    Start-Sleep -Milliseconds 250
    [Win32.J]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)     # LEFTDOWN
    Start-Sleep -Milliseconds 90
    [Win32.J]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)     # LEFTUP
    Start-Sleep -Milliseconds 800
}
function Widget-Count([string]$coop) {
    Remove-Item (Join-Path $coop 'result_qwidget.txt') -Force -ErrorAction SilentlyContinue
    Send-Cmd $coop 'qwidget'
    Start-Sleep 1
    return @(Get-Content (Join-Path $coop 'result_qwidget.txt') -ErrorAction SilentlyContinue | ? { $_.Trim() }).Count
}

. 'G:\Freespace\shell_coop\local\gamelock.ps1'
$locked = $false
$saveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
$saveSnap = Join-Path 'G:\Freespace\shell_coop\local\save-backups' "$stamp-$who-journal"
try {
    Lock-Game $who; $locked = $true
    Stop-All
    New-Item -ItemType Directory -Force $saveSnap | Out-Null
    if (Test-Path $saveDir) { Copy-Item "$saveDir\*" $saveSnap -Recurse -Force -ErrorAction SilentlyContinue }

    $relay = Join-Path $root 'relay\cooprelay.py'
    Remove-Item $logA, $logB -Force -ErrorAction SilentlyContinue
    foreach ($d in @($coopA, $coopB)) { Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'sync.txt') -Force -ErrorAction SilentlyContinue }
    Set-Content (Join-Path $coopA 'playerid.txt') 'A' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopA 'role.txt') 'host' -Encoding ASCII -NoNewline
    Set-Content (Join-Path $coopB 'playerid.txt') 'B' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopB 'role.txt') 'guest' -Encoding ASCII -NoNewline

    $pa = Start-Client $GameA (Join-Path $root 'local-appdata\A'); Start-Sleep 8
    $pb = Start-Client $GameB (Join-Path $root 'local-appdata\B')
    Start-Process python -ArgumentList @($relay, '--transport', 'udp', '--role', 'host', '--port', "$Port", '--player', 'A', '--dir', $coopA) -WindowStyle Hidden
    Start-Sleep 2
    Start-Process python -ArgumentList @($relay, '--transport', 'udp', '--role', 'client', '--host', '127.0.0.1', '--port', "$Port", '--player', 'B', '--dir', $coopB) -WindowStyle Hidden

    $until = (Get-Date).AddSeconds($BootWait)
    while ((Get-Date) -lt $until) {
        Start-Sleep -Milliseconds 700
        if ((Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') { break }
    }
    Write-Host "phase A=$(Sync $coopA 'phase') B=$(Sync $coopB 'phase')"
    if ((Sync $coopA 'phase') -ne 'sync') { throw "A did not sync" }
    Start-Sleep 6

    # a tracked quest with real text, so the journal has something to show
    Send-Cmd $coopA "qsid VlasQuest"
    Start-Sleep 4
    Send-Cmd $coopA "qnext VlasQuest"
    Start-Sleep 8

    Send-Cmd $coopA "qlog 0"
    Send-Cmd $coopB "qlog 0"
    Start-Sleep 4
    Shot $pa (Join-Path $outDir 'A-tab0.png'); Shot $pb (Join-Path $outDir 'B-tab0.png')

    if ($ClickTabs) {
        # Tab strip at the top of the menu.  Candidate points read off the first
        # screenshot: MAP text ~x=310, then the two icon tabs ~x=580 and ~x=640, y=50.
        $pts = @(@(310, 50), @(580, 50), @(640, 50))
        $n = 0
        foreach ($pt in $pts) {
            $n++
            Click-At $pa $pt[0] $pt[1]
            Click-At $pb $pt[0] $pt[1]
            Shot $pa (Join-Path $outDir ("A-click{0}-{1}_{2}.png" -f $n, $pt[0], $pt[1]))
            Shot $pb (Join-Path $outDir ("B-click{0}-{1}_{2}.png" -f $n, $pt[0], $pt[1]))
            $ca = Widget-Count $coopA; $cb = Widget-Count $coopB
            Write-Host ("  click ({0},{1}) -> journal rows A={2} B={3}" -f $pt[0], $pt[1], $ca, $cb)
        }
    } else {
        for ($k = 1; $k -le $Presses; $k++) {
            Press-Key $pa $Key 1
            Press-Key $pb $Key 1
            Start-Sleep 1
            Shot $pa (Join-Path $outDir ("A-tab{0}.png" -f $k))
            Shot $pb (Join-Path $outDir ("B-tab{0}.png" -f $k))
            $ca = Widget-Count $coopA; $cb = Widget-Count $coopB
            Write-Host ("  {0} press {1} -> journal rows A={2} B={3}" -f $Key, $k, $ca, $cb)
        }
    }

    Copy-Item $logA (Join-Path $outDir 'ms2coop-native-A.log') -Force -ErrorAction SilentlyContinue
    Copy-Item $logB (Join-Path $outDir 'ms2coop-native-B.log') -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coopA 'result_qwidget.txt') (Join-Path $outDir 'journal-widget-A.txt') -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coopB 'result_qwidget.txt') (Join-Path $outDir 'journal-widget-B.txt') -Force -ErrorAction SilentlyContinue
    Write-Host "out=$outDir"
}
finally {
    try { Stop-All } catch { Write-Host "cleanup: $($_.Exception.Message)" }
    if ($locked) { try { Unlock-Game $who } catch {} }
    try {
        Get-ChildItem $saveDir -Filter *.sav -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
        Copy-Item "$saveSnap\*" $saveDir -Recurse -Force -ErrorAction SilentlyContinue
        Write-Host "saves restored"
    } catch { Write-Host "save restore failed: $($_.Exception.Message)" }
}
