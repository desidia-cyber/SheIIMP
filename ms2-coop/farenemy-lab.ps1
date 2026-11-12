# [R8-FARENEMY] Two clients, one spawner, one question: does the HOST's replayed copy
# of the guest's far-away enemy stay alive?
#
#   .\farenemy-lab.ps1 [-Tag far] [-Secs 40] [-SpawnPoint "66992 -125076 -44454"]
#                      [-Shots] [-NoDeploy]
#
# Phase 1 (repro, fix OFF):  the guest teleports onto a proximity spawner ~20k from
#   the host's player; the host replays it.  `farfix off` on the host reproduces the
#   round-6 behaviour (the enemy is destroyed ~1 s after it spawns).  The A log lines
#   `farenemy spawn/gone/endplay` say WHY it went (life span, EndPlayReason).
# Phase 2 (fix ON): `farhook` on the host (the destroy veto), the guest steps out of the sphere and
#   back in (that re-arms its own self-trigger), and the host's next replay produces an
#   enemy that must stay for the whole 30 s window (result_farenemy.txt PASS on A).
# Phase 3 (-Shots): with the enemy alive, the host's player is teleported around it and
#   the window is captured from 8 directions -- one of them has it in frame.
#
# Holds the run lock itself (gamelock.ps1) and restores the shared save folder.
param(
    [string]$GameA = "E:\Games\_MS2-coop-g",
    [string]$GameB = "E:\Games\_MS2-coop-h",
    [string]$Port = "47130",
    [string]$SpawnPoint = "66992 -125076 -44454",
    [int]$BootWait = 200,
    [int]$Secs = 40,
    [string]$Tag = "far",
    [switch]$Shots,
    [switch]$SkipBaseline,
    [int]$PreWait = 0,
    [switch]$NoDeploy,
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)
$ErrorActionPreference = 'Stop'
. 'G:\Freespace\shell_coop\local\gamelock.ps1'
# Same lock file and protocol as Lock-Game, but a 0.2 s retry instead of 30: five agents
# share this PC and the gaps between their sessions are often only a few seconds wide
# (measured: r8-wall held it 14:45-15:50, r8-shells took it 2 s after that release).  The
# directory is the same, so Unlock-Game still releases it.
function Lock-Fast([string]$who) {
    $dir = 'G:\Freespace\shell_coop\local\game.lock'
    for ($try = 1; $try -le 60000; $try++) {
        try {
            New-Item -ItemType Directory $dir -ErrorAction Stop | Out-Null
            Set-Content (Join-Path $dir 'owner.txt') "$who $(Get-Date -Format s)"
            Write-Host "LOCK acquired by $who (attempt $try)"
            return
        } catch { Start-Sleep -Milliseconds 200 }
    }
    throw 'could not acquire the game lock'
}
Lock-Fast 'r8farenemy'
try {
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
public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
'@
Add-Type -AssemblyName System.Drawing

function Stop-All {
    Get-Process | ? ProcessName -match 'MortalShell2|CrashReportClient' | % { try { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue } catch {} }
    # WMI can answer "Access denied" on this box (it did, 14:39, and killed a whole
    # lab run at the Stop step); the relays are best-effort cleanup, not a verdict.
    try {
        Get-CimInstance Win32_Process -Filter "Name like 'python%'" -ErrorAction SilentlyContinue |
            ? { $_.CommandLine -match 'cooprelay' } |
            % { try { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue } catch {} }
    } catch {}
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
    if ($w -le 0 -or $ht -le 0) { Write-Host "shot: empty rect"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [Win32.V]::PrintWindow($h, $hdc, 2) | Out-Null
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
function Pose([string]$coop, [string]$id) {
    $f = Join-Path $coop "out_$id.txt"; if (-not (Test-Path $f)) { return $null }
    $t = Get-Content $f -Raw
    if ($t -match '"x":(-?[\d.]+),"y":(-?[\d.]+),"z":(-?[\d.]+)') {
        return "{0} {1} {2}" -f $matches[1], $matches[2], $matches[3] }
    return $null
}
# The mod reads cmd.txt and DELETES it, so wait for the consume before the next one.
function Cmd([string]$coop, [string]$line) {
    $f = Join-Path $coop 'cmd.txt'
    [IO.File]::WriteAllText($f, "$line`n")
    $due = (Get-Date).AddSeconds(8)
    while ((Get-Date) -lt $due -and (Test-Path $f)) { Start-Sleep -Milliseconds 150 }
}
function Copy-Logs([string]$id, [string]$coop, [string]$install) {
    Copy-Item (Join-Path $install 'MortalShell2\Binaries\Win64\ms2coop-native.log') (Join-Path $outDir "native_$id.log") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coop 'probe.log') (Join-Path $outDir "probe_$id.log") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coop 'sync.txt') (Join-Path $outDir "sync_$id.txt") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $coop 'result_farenemy.txt') (Join-Path $outDir "result_farenemy_$id.txt") -Force -ErrorAction SilentlyContinue
}
# The newest farenemy lines of a probe.log, printed as they appear.
function FarLines([string]$coop, [int]$n = 12) {
    $f = Join-Path $coop 'probe.log'; if (-not (Test-Path $f)) { return }
    Select-String -Path $f -Pattern 'farenemy|farfix|farhook|fardump' | Select-Object -Last $n |
        ForEach-Object { Write-Host ("    " + $_.Line) }
}

Stop-All; Start-Sleep 3
$realSaveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
$snap = Join-Path $env:TEMP ("ms2saves_fe_" + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $snap | Out-Null
Copy-Item "$realSaveDir\*.sav" $snap -Force
$before = @(Get-ChildItem $realSaveDir -Filter *.sav | % Name)

if (-not $NoDeploy) {
    # A dying client still maps dxgi.dll, so the copy can answer ACCESS_DENIED for a
    # few seconds after it was killed (measured: 14:42, this aborted a whole lab run).
    # Retry until the file in the install really is the one that was built.
    $srcHash = (Get-FileHash (Join-Path $root 'native\build\dxgi.dll')).Hash
    foreach ($g in @($GameA, $GameB)) {
        $target = Join-Path $g 'MortalShell2\Binaries\Win64\dxgi.dll'
        $ok = $false
        for ($try = 1; $try -le 8 -and -not $ok; $try++) {
            try {
                & (Join-Path $root 'deploy.ps1') -GameDir $g -DllPath (Join-Path $root 'native\build\dxgi.dll') | Out-Null
            } catch {
                Write-Host "deploy attempt $try -> $g failed: $($_.Exception.Message)"
            }
            if ((Test-Path $target) -and ((Get-FileHash $target -ErrorAction SilentlyContinue).Hash -eq $srcHash)) {
                $ok = $true
            } else {
                Start-Sleep 5
            }
        }
        Write-Host "deploy -> $g : $(if ($ok) { 'ok (hash matches)' } else { 'FAILED' })"
        if (-not $ok) { throw "deploy to $g failed 8 times" }
    }
}

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'
foreach ($d in @($coopA, $coopB)) {
    Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'probe.log'), (Join-Path $d 'cmd.txt'), (Join-Path $d 'peer_*.txt'), (Join-Path $d 'out_*.txt'), (Join-Path $d 'sync.txt'), (Join-Path $d 'heartbeat.txt'), (Join-Path $d 'FARENEMY_OFF.txt') -Force -ErrorAction SilentlyContinue
}
Set-Content (Join-Path $coopA 'playerid.txt') 'A' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopA 'role.txt') 'host' -Encoding ASCII -NoNewline
Set-Content (Join-Path $coopB 'playerid.txt') 'B' -Encoding ASCII -NoNewline; Set-Content (Join-Path $coopB 'role.txt') 'guest' -Encoding ASCII -NoNewline

$pa = $null; $pb = $null
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

    # ---- what both clients have BEFORE anything is spawned: the streaming sources
    # and the world-partition query, i.e. the "unloaded cell" hypothesis' evidence
    Write-Host "=== phase 0: sources (A then B)"
    Cmd $coopA 'fardump'; Start-Sleep 2; FarLines $coopA 20
    Cmd $coopB 'fardump'; Start-Sleep 2; FarLines $coopB 20

    $aHome = Pose $coopA 'A'
    Write-Host "A home: $aHome"
    if ($PreWait -gt 0) {
        # run-two reaches its SPAWN lane ~5 min into the session; this reproduces "the
        # spawn happens LATE" without the rest of run-two, so the two can be told apart
        Write-Host "=== pre-wait $PreWait s before the spawn (idle, both clients live)"
        $pw = (Get-Date).AddSeconds($PreWait)
        while ((Get-Date) -lt $pw) { Hide-All; Start-Sleep -Milliseconds 800 }
        Write-Host "=== pre-wait done: A $(Sync $coopA 'phase') B $(Sync $coopB 'phase')"
    }

    if (-not $SkipBaseline) {
    # ---- phase 1: reproduce it with the fix OFF -----------------------------
    Write-Host "=== phase 1: fix OFF, B teleports onto the spawner"
    Cmd $coopA 'farfix off'
    # the veto is installed AUTOMATICALLY by default now, so the baseline needs it off
    Cmd $coopA 'farhook off'
    # the ProcessEvent probe on the host, armed with the names a NATIVE destroy's
    # callers could still reach through Blueprint (a gameplay ability, the safe-zone
    # despawn, a level-unload path): R6's patterns did not include these
    Cmd $coopA 'spawnprobe 70 Despawn|Destroy|EndPlay|LifeSpan|Kill|Hidden|Dormant|Streaming|Unload|Significance|SafeZone|Pool|Deactivate|Disable'
    Cmd $coopB "tp $SpawnPoint"
    $t0 = Get-Date
    for ($i = 1; $i -le 5; $i++) {
        Start-Sleep 5; Hide-All
        Cmd $coopA 'fardump'
        Start-Sleep 1
        Write-Host ("[repro t+{0}s] A@{1} B@{2} A {3}" -f [int]((Get-Date) - $t0).TotalSeconds,
            (Pose $coopA 'A'), (Pose $coopB 'B'), (Sync $coopA 'enemy'))
        FarLines $coopA 6
    }

    }
    # ---- phase 2: the fix ON, a fresh enemy ---------------------------------
    Write-Host "=== phase 2: VETO (farhook), the guest steps out of the sphere and back in"
    Cmd $coopA 'farhook'
    # 900 uu north of the spawn point: outside the ~4791 uu sphere?  No -- outside is
    # further, so use 6 000 uu to leave it, then come back (that re-arms the guest's
    # own self-trigger, which is what makes a NEW enemy).
    $sp = @($SpawnPoint -split '\s+' | ForEach-Object { [double]$_ })
    $out = "{0:F0} {1:F0} {2:F0}" -f $sp[0], ($sp[1] + 6000), $sp[2]
    Cmd $coopB "tp $out"; Start-Sleep 4
    Cmd $coopB "tp $SpawnPoint"
    # [R8-FARENEMY] keep the guest AT the enemy and out of the brigands' reach: the narrowed
    # veto only answers while the peer's body is with the enemy (dPeer < 4000), and a guest
    # standing in the camp is dead in ~10-20 s (its respawn moves it 12k away and legitimately
    # releases the veto).  +1200 uu is still inside the spawner's 4 791 uu sphere and out of
    # melee reach; `tp` hovers for 6 s, so it is re-sent every 4 s.
    $sp2 = @($SpawnPoint -split '\s+' | ForEach-Object { [double]$_ })
    $hover = "{0:F0} {1:F0} {2:F0}" -f $sp2[0], $sp2[1], ($sp2[2] + 1200)
    $t1 = Get-Date
    for ($i = 1; $i -le [math]::Ceiling($Secs / 5); $i++) {
        Cmd $coopB "tp $hover"
        Start-Sleep 5; Hide-All
        Cmd $coopA 'fardump'
        Start-Sleep 1
        Write-Host ("[fix t+{0}s] A {1}" -f [int]((Get-Date) - $t1).TotalSeconds, (Sync $coopA 'enemy'))
        FarLines $coopA 4
    }
    $farVerdict = $null
    if (Test-Path (Join-Path $coopA 'result_farenemy.txt')) { $farVerdict = (Get-Content (Join-Path $coopA 'result_farenemy.txt') -Raw).Trim() }
    Write-Host "=== FARENEMY(verdict file): $farVerdict"

    # ---- phase 3: the host-side picture ------------------------------------
    if ($Shots) {
        Write-Host "=== phase 3: the host's player walks around the enemy for the picture"
        Cmd $coopA 'farhook'
        foreach ($deg in 0, 45, 90, 135, 180, 225, 270, 315) {
            $r = $deg * [math]::PI / 180.0
            $px = $sp[0] + [math]::Cos($r) * 1500
            $py = $sp[1] + [math]::Sin($r) * 1500
            $pt = "{0:F0} {1:F0} {2:F0}" -f $px, $py, ($sp[2] + 60)
            Cmd $coopA "tp $pt"
            Start-Sleep 3; Hide-All
            Shot $pa (Join-Path $outDir ("A-near-enemy-dir{0}.png" -f $deg))
        }
        Cmd $coopA 'fardump'; Start-Sleep 2; FarLines $coopA 12
        Shot $pb (Join-Path $outDir 'B-at-spawner-final.png')
    }

    Shot $pa (Join-Path $outDir 'A-final.png')
    Shot $pb (Join-Path $outDir 'B-final.png')
    Copy-Logs 'A' $coopA $GameA
    Copy-Logs 'B' $coopB $GameB
    Write-Host "alive=$(@(Get-Process | ? ProcessName -match 'MortalShell2-Win64').Count)"
}
finally {
    try { Copy-Logs 'A' $coopA $GameA; Copy-Logs 'B' $coopB $GameB } catch {}
    Stop-All; Start-Sleep 2
    Get-ChildItem $realSaveDir -Filter *.sav | ? { $before -notcontains $_.Name } | Remove-Item -Force
    Copy-Item "$snap\*.sav" $realSaveDir -Force
    Write-Host "saves restored; out=$outDir"
}
}
finally { Unlock-Game 'r8farenemy' }
