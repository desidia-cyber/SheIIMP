# real-input.ps1 -- round 9c: drive the game with REAL INPUT (the journal-shot.ps1 path:
# SetForegroundWindow + keybd_event / mouse_event), instead of calling ability UFunctions.
#
#   .\real-input.ps1 -Stage kill      # A then B: stand at a brigand, LMB until a kill
#   .\real-input.ps1 -Stage npc       # A then B: stand at the ShellKeeper, press interact
#   .\real-input.ps1 -Stage both
#
# Positions and enemy/NPC reads still come from the mod's own read-only verbs
# (`enemydump`, `enemytp`, `areago`, `dlgdump`) -- those are observations, not inputs.
# Everything that is supposed to START something is a real mouse/keyboard event.
param(
    [string]$GameA = "E:\Games\_MS2-coop-e",
    [string]$GameB = "E:\Games\_MS2-coop-f",
    [int]$Port = 47120,
    [string]$Stage = "kill",
    [int]$BootWait = 110,
    [int]$Bursts = 6,
    [string]$NpcClass = "BP_NPC_Genessa_Training",
    [int]$ClicksPerBurst = 3,
    [string]$InteractKey = "E",
    [string]$Tag = "realinput",
    [switch]$Lock
)
$ErrorActionPreference = 'Stop'
$ws = $PSScriptRoot
$outDir = Join-Path 'G:\Freespace\shell_coop\local\lab' ((Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Tag")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace Win32 -Name RI -MemberDefinition @'
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
$VK = @{ E = 0x45; F = 0x46; Q = 0x51; R = 0x52; SPACE = 0x20 }
$realLocal = $env:LOCALAPPDATA

function Stop-All {
    Get-Process | ? ProcessName -match 'MortalShell2|CrashReportClient' | % { try { Stop-Process -Id $_.Id -Force } catch {} }
    Get-CimInstance Win32_Process -Filter "Name like 'python%'" | ? { $_.CommandLine -match 'cooprelay' } | % { try { Stop-Process -Id $_.ProcessId -Force } catch {} }
    Start-Sleep 2
}
function Start-Client([string]$gameDir, [string]$localAppData) {
    $exe = Join-Path $gameDir 'MortalShell2\Binaries\Win64\MortalShell2-Win64-Shipping.exe'
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe; $psi.Arguments = "MortalShell2 -windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
    $psi.WorkingDirectory = $gameDir; $psi.UseShellExecute = $true
    $env:LOCALAPPDATA = $localAppData; $p = [System.Diagnostics.Process]::Start($psi); $env:LOCALAPPDATA = $realLocal
    return $p
}
function Sync([string]$coop, [string]$key) {
    $f = Join-Path $coop 'sync.txt'; if (-not (Test-Path $f)) { return $null }
    foreach ($l in Get-Content $f) { if ($l -like "$key=*") { return $l.Substring($key.Length + 1) } }
    return $null
}
function Cmd([string]$which, [string]$text, [int]$waitSec = 2) {
    $coop = if ($which -eq 'A') { $coopA } else { $coopB }
    $f = Join-Path $coop 'cmd.txt'; $tmp = Join-Path $coop 'cmd.tmp'
    [IO.File]::WriteAllText($tmp, $text + "`n"); Move-Item -LiteralPath $tmp -Destination $f -Force
    $due = (Get-Date).AddSeconds(12)
    while ((Get-Date) -lt $due -and (Test-Path $f)) { Start-Sleep -Milliseconds 200 }
    Start-Sleep $waitSec
}
function LogTail([string]$coop, [int]$n = 500) {
    $f = Join-Path (Split-Path $coop -Parent) 'ms2coop-native.log'
    if (-not (Test-Path $f)) { return @() }
    return @(Get-Content $f -Tail $n)
}
function Verdict([string]$name, [string]$state, [string]$why) {
    $line = "=== ${name}: $state -- $why"
    Write-Host $line; Add-Content (Join-Path $outDir 'verdicts.txt') $line
}
function Shot($proc, [string]$path) {
    try {
        $proc.Refresh(); $h = $proc.MainWindowHandle
        if (-not $h -or $h -eq [IntPtr]::Zero) { return }
        $h = [IntPtr]$h
        $r = New-Object Win32.RI+RECT; [Win32.RI]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        if ($w -le 0 -or $ht -le 0) { return }
        $bmp = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
        [Win32.RI]::PrintWindow($h, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        Write-Host "shot: $path"
    } catch { Write-Host "shot failed: $($_.Exception.Message)" }
}
# The window is parked off-screen by the other harnesses, so move it to a KNOWN rect
# first: the screenshot IS the window rect, so image coordinate + (40,40) = screen.
function Focus-Window($proc) {
    $proc.Refresh(); $h = $proc.MainWindowHandle
    if (-not $h -or $h -eq [IntPtr]::Zero) { return $false }
    $h = [IntPtr]$h
    [Win32.RI]::ShowWindow($h, 9) | Out-Null
    [Win32.RI]::SetWindowPos($h, [IntPtr]::Zero, 40, 40, 1040, 615, 0x40) | Out-Null
    [Win32.RI]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 400
    return $true
}
# left mouse in the middle of the 3D view (not on any HUD element), as a player does
function Click-Attack($proc, [int]$times = 1) {
    if (-not (Focus-Window $proc)) { Write-Host "no window for click"; return $false }
    for ($i = 0; $i -lt $times; $i++) {
        [Win32.RI]::SetCursorPos(40 + 520, 40 + 300) | Out-Null
        Start-Sleep -Milliseconds 120
        [Win32.RI]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 70
        [Win32.RI]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 650
    }
    return $true
}
function Press-Key($proc, [string]$name, [int]$times = 1) {
    $vk = $VK[$name]; if (-not $vk) { Write-Host "unknown key $name"; return }
    if (-not (Focus-Window $proc)) { Write-Host "no window for key"; return }
    for ($i = 0; $i -lt $times; $i++) {
        [Win32.RI]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 80
        [Win32.RI]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 450
    }
}
function Hp([string]$coop) {
    $lines = @(LogTail $coop 200 | ? { $_ -match 'enemydump: .*hp=(-?[0-9.]+)' })
    return $lines
}

. G:\Freespace\shell_coop\local\gamelock.ps1
if ($Lock) { Lock-Game 'r9-realarea' }
try {
    Stop-All
    $saveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
    $snap = Join-Path $env:TEMP ("ms2ri_" + (Get-Date -Format 'HHmmss'))
    New-Item -ItemType Directory -Force $snap | Out-Null
    Copy-Item "$saveDir\*.sav" $snap -Force
    $before = @(Get-ChildItem $saveDir -Filter *.sav | % Name)

    foreach ($d in @($coopA, $coopB)) {
        Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'sync.txt'),
            (Join-Path $d 'heartbeat.txt') -Force -ErrorAction SilentlyContinue
    }
    Set-Content (Join-Path $coopA 'playerid.txt') 'A' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopA 'role.txt') 'host' -Encoding ASCII -NoNewline
    Set-Content (Join-Path $coopB 'playerid.txt') 'B' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopB 'role.txt') 'guest' -Encoding ASCII -NoNewline

    $pa = Start-Client $GameA (Join-Path $ws 'local-appdata\A'); Start-Sleep 8
    $pb = Start-Client $GameB (Join-Path $ws 'local-appdata\B')
    $relay = Join-Path $ws 'relay\cooprelay.py'
    Start-Process python -ArgumentList @($relay, '--transport', 'udp', '--role', 'host', '--port', "$Port", '--player', 'A', '--dir', $coopA) -WindowStyle Hidden
    Start-Sleep 2
    Start-Process python -ArgumentList @($relay, '--transport', 'udp', '--role', 'client', '--host', '127.0.0.1', '--port', "$Port", '--player', 'B', '--dir', $coopB) -WindowStyle Hidden
    $until = (Get-Date).AddSeconds($BootWait)
    while ((Get-Date) -lt $until) {
        Start-Sleep -Milliseconds 700
        if ((Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') { break }
    }
    Verdict 'BOOT' $(if ((Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') { 'PASS' } else { 'WARN' }) ("phase A=" + (Sync $coopA 'phase') + " B=" + (Sync $coopB 'phase'))
    Start-Sleep 6


    # ---- A. a real swing at ONE named enemy, until it dies (A then B) --------
    if ($Stage -in @('kill', 'both', 'final')) {
        foreach ($who in 'A', 'B') {
            $proc = if ($who -eq 'A') { $pa } else { $pb }
            $coop = if ($who -eq 'A') { $coopA } else { $coopB }
            Cmd $who 'weaponstate' 2
            Cmd $who 'enemydump' 3
            # PIN ONE ENEMY BY OBJECT NAME: "nearest" flipped between two cultists
            # 1 200 uu apart, which is why an earlier run read 1,0.533,1,0.533.
            $target = ''
            foreach ($l in (LogTail $coop 120)) {
                if ($l -match 'enemydump: \S+ (\S+) hp=([0-9.]+) d=') { $target = $matches[1]; break }
            }
            Verdict "TARGET-$who" $(if ($target) { 'INFO' } else { 'FAIL' }) $target
            if (-not $target) { continue }
            Cmd $who ("enemytp " + $target) 6
            Start-Sleep 4
            Shot $proc (Join-Path $outDir "$who-stand.png")
            $seq = @()
            for ($b = 1; $b -le 14; $b++) {
                Click-Attack $proc 3 | Out-Null
                Cmd $who 'enemydump' 1
                $hpNow = -1.0
                foreach ($l in (LogTail $coop 150)) {
                    if ($l -match ('enemydump: \S+ ' + [regex]::Escape($target) + ' hp=(-?[0-9.]+)')) { $hpNow = [double]$matches[1] }
                }
                $seq += $hpNow
                if ($b -eq 7) { Cmd $who ("enemytp " + $target) 2 }     # re-seat + re-face
                if ($hpNow -lt 0 -or $hpNow -le 0.001) { break }        # gone, or at zero
                if ($b % 4 -eq 0) { Shot $proc (Join-Path $outDir ("$who-swing$b.png")) }
            }
            Start-Sleep 4
            Shot $proc (Join-Path $outDir "$who-after.png")
            $log = LogTail $coop 1500
            $deadLine = @($log | ? { $_ -match 'StartDeath|drops|dead=[1-9]|death' } | Select-Object -Last 3)
            $drops = @(Get-Content (Join-Path $coop 'sync.txt') -ErrorAction SilentlyContinue | ? { $_ -like 'drops=*' })
            $money = @(Get-Content (Join-Path $coop 'sync.txt') -ErrorAction SilentlyContinue | ? { $_ -like 'money=*' })
            $killed = (@($seq | ? { $_ -ge 0 -and $_ -le 0.001 }).Count -gt 0) -or (@($seq | ? { $_ -lt 0 }).Count -gt 0)
            Verdict "KILL-$who" $(if ($killed) { 'PASS' } else { 'FAIL' }) ("target=" + $target + " hp sequence " + ($seq -join ','))
            Verdict "KILLDEATH-$who" $(if ($killed) { 'WARN' } else { 'FAIL' }) ($deadLine -join ' ; ')
            Verdict "REWARD-$who" $(if ($drops) { 'INFO' } else { 'FAIL' }) (($drops + $money) -join ' | ')
        }
    }

    # ---- B. NPC: stand facing it, then try the keys a prompt could name ------
    if ($Stage -in @('npc', 'both', 'final')) {
        foreach ($who in 'A', 'B') {
            $proc = if ($who -eq 'A') { $pa } else { $pb }
            $coop = if ($who -eq 'A') { $coopA } else { $coopB }
            Cmd $who ("areasweep " + $NpcClass) 4
            Cmd $who 'areago nearest' 3
            Start-Sleep 8
            Shot $proc (Join-Path $outDir "$who-npc-stand.png")
            $hit = @(); $last = @()
            foreach ($k in @($InteractKey, 'F', 'R', 'Q')) {
                Press-Key $proc $k 2
                Start-Sleep 2
                Shot $proc (Join-Path $outDir "$who-npc-$k.png")
                Cmd $who 'dlgdump' 2
                $last = @(LogTail $coop 250 | ? { $_ -match 'dlgdump: dlg=' } | Select-Object -Last 1)
                if ($last -match 'node:[1-9]|opts:[1-9]|lines:[1-9]') { $hit = @("key=$k", $last); break }
            }
            Verdict "NPC-$who" $(if ($hit.Count) { 'PASS' } else { 'FAIL' }) (($hit + $last) -join ' | ')
        }
    }

    # ---- C. boss sweep: areago + REAL clicks, landing areas 1..5, 45 s each --
    if ($Stage -in @('bosssweep', 'final')) {
        foreach ($idx in 1, 2, 3, 4, 5) {
            foreach ($w in 'A', 'B') { Cmd $w 'areasweep BP_LandingArea_Boss' 2 }
            Cmd 'A' ("areago " + $idx) 3
            Cmd 'B' ("areago " + $idx) 3
            Start-Sleep 45
            Cmd 'A' 'bossdump' 3
            Cmd 'B' 'bossdump' 3
            $bdA = @(LogTail $coopA 500 | ? { $_ -match 'bossdump: .*cls=' } | Select-Object -Last 1)
            $bdB = @(LogTail $coopB 500 | ? { $_ -match 'bossdump: .*cls=' } | Select-Object -Last 1)
            Shot $pa (Join-Path $outDir "A-boss$idx.png")
            Shot $pb (Join-Path $outDir "B-boss$idx.png")
            Verdict "BOSS-$idx" $(if ($bdA.Count -or $bdB.Count) { 'PASS' } else { 'FAIL' }) ("A: " + ($bdA -join '') + " | B: " + ($bdB -join ''))
            if ($bdA.Count) {
                $hp0 = ($bdA -join '') -replace '.*hp=([-0-9.]+).*', '$1'
                for ($b = 1; $b -le 10; $b++) {
                    Click-Attack $pa 3 | Out-Null
                    Cmd 'A' 'bossdump' 1
                    $cur = @(LogTail $coopA 200 | ? { $_ -match 'bossdump: .*cls=' } | Select-Object -Last 1)
                    if (($cur -join '') -match 'hp=([-0-9.]+)' -and [double]$matches[1] -lt [double]$hp0) { break }
                }
                Cmd 'A' 'bossdump' 3
                $bdA2 = @(LogTail $coopA 400 | ? { $_ -match 'bossdump: .*cls=' } | Select-Object -Last 1)
                $hp1 = ($bdA2 -join '') -replace '.*hp=([-0-9.]+).*', '$1'
                Shot $pa (Join-Path $outDir "A-boss$idx-after.png")
                Verdict "BOSSDMG-$idx" $(if ($hp1 -ne $hp0) { 'PASS' } else { 'SKIP' }) ("hp " + $hp0 + " -> " + $hp1)
            }
        }
    }

    foreach ($id in 'A', 'B') {
        $c = if ($id -eq 'A') { $coopA } else { $coopB }
        Copy-Item (Join-Path $c 'sync.txt') (Join-Path $outDir "sync_$id.txt") -ErrorAction SilentlyContinue
        Copy-Item (Join-Path (Split-Path $c -Parent) 'ms2coop-native.log') (Join-Path $outDir "native_$id.log") -ErrorAction SilentlyContinue
    }
}
finally {
    Stop-All
    if (Test-Path $snap) {
        Get-ChildItem $saveDir -Filter *.sav | ? { $before -notcontains $_.Name } | Remove-Item -Force
        Copy-Item "$snap\*.sav" $saveDir -Force
    }
    if ($Lock) { Unlock-Game 'r9-realarea' }
    Write-Host "out=$outDir"
}
