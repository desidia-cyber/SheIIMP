# [R8-FARENEMY] Regression test for the veto, part 2: BOTH players die (party wipe) AFTER a
# far-away replayed enemy exists.  The game's own death+reset must still be able to clear and
# respawn enemies -- the veto must be RELEASED for it, or the reset leaves duplicates or stale
# copies behind.
#
# This is the main agent's review item: the first revision vetoed every destroy of a co-op
# enemy while the peer was present, which also blocked the wipe/reset, checkpoint-rest,
# level-change and corpse paths.  The narrowed veto only answers for the far-cull case
# (nobody HERE is near it, the peer's body IS with it) and releases for death/wipe/hold/
# settle/not-sync/no-claim/stale-peer.
#
#   .\wipe-far.ps1 [-Tag wipefar] [-BootWait 200]
#
# Measures, per claimed spawner, the number of characters standing at it (`near=` of
# `spawndump claim`) and the enemy the spawner owns, BEFORE the wipe and AFTER the reset, and
# counts the `farenemy veto` lines inside the wipe window (must be 0) plus the `farenemy cull`
# candidates (the evidence of what the game asked for during the reset).
param(
    [string]$GameA = "E:\Games\_MS2-coop-g",
    [string]$GameB = "E:\Games\_MS2-coop-h",
    [string]$Port = "47130",
    [string]$SpawnPoint = "66992 -125076 -44454",
    [int]$BootWait = 200,
    [string]$Tag = "wipefar",
    [string]$ExtraArgs = "-windowed -ResX=1024 -ResY=576 -nosound -nosplash -nocrashreport"
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$realLocal = $env:LOCALAPPDATA
$outDir = Join-Path 'G:\Freespace\shell_coop\local\lab' ((Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Tag")
New-Item -ItemType Directory -Force $outDir | Out-Null

. 'G:\Freespace\shell_coop\local\gamelock.ps1'
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
function SyncField([string]$coop, [string]$lineKey, [string]$field) {
    $f = Join-Path $coop 'sync.txt'; if (-not (Test-Path $f)) { return $null }
    foreach ($l in Get-Content $f) {
        if ($l -like "$lineKey=*") { if ($l -match "$field`:([0-9]+)") { return [int]$matches[1] } }
    }
    return $null
}
function Pose([string]$coop, [string]$id) {
    $f = Join-Path $coop "out_$id.txt"; if (-not (Test-Path $f)) { return $null }
    $t = Get-Content $f -Raw
    if ($t -match '"x":(-?[\d.]+),"y":(-?[\d.]+),"z":(-?[\d.]+)') {
        return "{0} {1} {2}" -f $matches[1], $matches[2], $matches[3] }
    return $null
}
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
}
# The lines a claim/spawner produced in the last `secs` seconds of a client's probe.log.
function CountLines([string]$path, [string]$pattern) {
    if (-not (Test-Path $path)) { return 0 }
    return (Select-String -Path $path -Pattern $pattern -ErrorAction SilentlyContinue).Count
}

Stop-All; Start-Sleep 3
$realSaveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'
$snap = Join-Path $env:TEMP ("ms2saves_wf_" + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force $snap | Out-Null
Copy-Item "$realSaveDir\*.sav" $snap -Force
$before = @(Get-ChildItem $realSaveDir -Filter *.sav | % Name)

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

    $logA = Join-Path $coopA 'probe.log'
    # ---- 1. a far-away replayed enemy exists (the case the veto protects) --------------
    # keep the guest AT the spawner and out of the brigands' reach (the narrowed veto only
    # answers while the peer's body is with the enemy; +1200 uu is still inside the 4 791 uu
    # trigger sphere), so the far enemy exists when the wipe happens
    $sp = @($SpawnPoint -split '\s+' | ForEach-Object { [double]$_ })
    $hover = "{0:F0} {1:F0} {2:F0}" -f $sp[0], $sp[1], ($sp[2] + 1200)
    Cmd $coopB "tp $SpawnPoint"
    $spawned = $false
    $due = (Get-Date).AddSeconds(60)
    while ((Get-Date) -lt $due) {
        Cmd $coopB "tp $hover"
        if ((CountLines $logA 'farenemy spawn') -gt 0) { $spawned = $true; break }
        Hide-All; Start-Sleep -Milliseconds 700
    }
    for ($k = 0; $k -lt 3; $k++) { Cmd $coopB "tp $hover"; Start-Sleep -Milliseconds 400 }
    Write-Host "far enemy spawned on A: $spawned"
    Start-Sleep 3
    Cmd $coopA 'spawndump claim'; Start-Sleep 2
    Shot $pa (Join-Path $outDir 'A-before-wipe.png')
    $beforeDump = @(Select-String -Path $logA -Pattern '\[A\] spawndump: claim' | Select-Object -Last 8 | ForEach-Object { $_.Line.Trim() })
    $veto0 = CountLines $logA 'farenemy veto'
    $wrA0 = SyncField $coopA 'death' 'worldresets'
    if ($null -eq $wrA0) { $wrA0 = 0 }
    $nearBefore = @($beforeDump | ForEach-Object { if ($_ -match 'near=(\d+)') { [int]$matches[1] } })
    Write-Host "=== WIPE baseline: vetoes=$veto0 worldresets=$wrA0 near=$($nearBefore -join ',')"
    foreach ($l in $beforeDump) { Write-Host "    $l" }

    # ---- 2. PARTY WIPE: both clients die (CHAINWIPE in death-chain.ps1) ----------------
    $tWipe = Get-Date
    Cmd $coopA 'die'
    Cmd $coopB 'die'
    $resetDone = $false
    $due = (Get-Date).AddSeconds(150)
    while ((Get-Date) -lt $due) {
        Hide-All; Start-Sleep -Milliseconds 800
        $wr = SyncField $coopA 'death' 'worldresets'
        if ($null -ne $wr -and $wr -gt $wrA0) { $resetDone = $true; break }
    }
    Write-Host "host world reset seen: $resetDone (worldresets $(SyncField $coopA 'death' 'worldresets'))"
    # the game's reset ends with a loading screen; wait for both to be alive and in sync
    $due = (Get-Date).AddSeconds(120)
    while ((Get-Date) -lt $due) {
        Hide-All; Start-Sleep -Milliseconds 800
        $da = Sync $coopA 'death'; $db = Sync $coopB 'death'
        if ($da -match 'me:1\(alive\)' -and $db -match 'me:1\(alive\)' -and
            (Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') { break }
    }
    Start-Sleep 5
    Shot $pa (Join-Path $outDir 'A-after-wipe.png')
    Shot $pb (Join-Path $outDir 'B-after-wipe.png')

    # ---- 3. what the reset did: enemies per spawner, vetoes inside the window ----------
    Cmd $coopA 'spawndump claim'; Start-Sleep 2
    $afterDump = @(Select-String -Path $logA -Pattern '\[A\] spawndump: claim' | Select-Object -Last 8 | ForEach-Object { $_.Line.Trim() })
    $veto1 = CountLines $logA 'farenemy veto'
    $windowStart = $tWipe.ToString('HH:mm:ss')
    $vetoInWindow = @(Select-String -Path $logA -Pattern 'farenemy veto' -ErrorAction SilentlyContinue |
        Where-Object { $_.Line -match '^\[(..:..:..)\]' -and $matches[1] -ge $windowStart }).Count
    $cullInWindow = @(Select-String -Path $logA -Pattern 'farenemy cull' -ErrorAction SilentlyContinue |
        Where-Object { $_.Line -match '^\[(..:..:..)\]' -and $matches[1] -ge $windowStart })
    $nearAfter = @($afterDump | ForEach-Object { if ($_ -match 'near=(\d+)') { [int]$matches[1] } })
    $enemiesAfter = @($afterDump | Where-Object { $_ -match 'enemy=(BP_[A-Za-z_]+_C_\S+)' }).Count
    Write-Host "=== WIPE after: vetoes_total=$veto1 vetoes_in_window=$vetoInWindow reset=$resetDone"
    Write-Host "=== WIPE cull candidates in the window: $($cullInWindow.Count)"
    foreach ($c in $cullInWindow) { Write-Host "    $($c.Line.Trim())" }
    Write-Host "=== WIPE near before=[$($nearBefore -join ',')] after=[$($nearAfter -join ',')] claims_with_enemy_after=$enemiesAfter"
    foreach ($l in $afterDump) { Write-Host "    $l" }

    # PASS = the reset happened, no veto fired inside it, and no spawner area ended up with
    # more characters than it had before (no duplicate/stale copies).
    $maxBefore = if ($nearBefore.Count) { ($nearBefore | Measure-Object -Maximum).Maximum } else { 0 }
    $maxAfter = if ($nearAfter.Count) { ($nearAfter | Measure-Object -Maximum).Maximum } else { 0 }
    $noDup = ($maxAfter -le $maxBefore)
    $ok = $spawned -and $resetDone -and ($vetoInWindow -eq 0) -and $noDup
    $verdict = ("{0} party wipe after a far enemy: spawned={1} host_reset={2} vetoes_in_reset={3} " +
                "near_before_max={4} near_after_max={5} candidates_in_reset={6}") -f
               ($(if ($ok) { 'PASS' } else { 'FAIL' })), $spawned, $resetDone, $vetoInWindow,
               $maxBefore, $maxAfter, $cullInWindow.Count
    Write-Host "=== WIPE: $verdict"
    Set-Content (Join-Path $coopA 'result_wipe.txt') $verdict

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
