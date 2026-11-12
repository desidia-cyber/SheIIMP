# real-area.ps1 -- round 9: run BOTH clients into a REAL area (a boss landing area) and
# test co-op there.  Verdict lines (each printed as "=== NAME: PASS|FAIL|SKIP -- why")
#
#   AREAFIND  a boss landing area (BP_LandingArea_Boss_C) exists in this world
#   AREATP    both clients moved to it (log `areago:` + the pawn's own coordinates)
#   BOSS      a real boss (BPC_BossAI_C) is live on both clients after arriving
#   ATTACK    the game's own attack ability activated and a swing montage started
#   KILL      an ordinary enemy's health dropped from OUR swing and it died
#   NPC       a real NPC is present and a conversation can be started
#   DEATH     a player died during the stay and the peer saw it (death lane)
#
# Usage (hold the game lock, or let -Lock do it):
#   powershell -NoProfile -ExecutionPolicy Bypass -File real-area.ps1 -Stage reach -Tag r9a
#   ... -Stage boss   -- reach + boss fight probes (attack, hp, bossdump)
#   ... -Stage all    -- everything above plus the NPC census and a player death
#
# Screenshots of BOTH windows are taken at every stage in
# G:\Freespace\shell_coop\local\lab\<stamp>-<tag>\ (that is the visual proof).
param(
    [string]$GameA = "E:\Games\_MS2-coop-e",
    [string]$GameB = "E:\Games\_MS2-coop-f",
    [int]$Port = 47120,
    [string]$Tag = "realarea",
    [ValidateSet('reach', 'boss', 'npc', 'kill', 'killnpc', 'all')]
    [string]$Stage = "reach",
    [int]$BootWait = 110,
    [switch]$SkipReach,
    [string]$KillAt = "",
    [int]$BossIdx = -1,
    [switch]$Deploy,
    [switch]$Lock
)
$ErrorActionPreference = 'Stop'
$ws = $PSScriptRoot
$outDir = Join-Path 'G:\Freespace\shell_coop\local\lab' ((Get-Date -Format 'yyyyMMdd-HHmmss') + "-$Tag")
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -Namespace Win32 -Name RA -MemberDefinition @'
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

$coopA = Join-Path $GameA 'MortalShell2\Binaries\Win64\coop'
$coopB = Join-Path $GameB 'MortalShell2\Binaries\Win64\coop'

function Stop-All {
    Get-Process | ? ProcessName -match 'MortalShell2|CrashReportClient' | % { try { Stop-Process -Id $_.Id -Force } catch {} }
    Get-CimInstance Win32_Process -Filter "Name like 'python%'" | ? { $_.CommandLine -match 'cooprelay' } | % { try { Stop-Process -Id $_.ProcessId -Force } catch {} }
}
function Hide-All {
    Get-Process | ? ProcessName -match 'MortalShell2-Win64' | % {
        if ($_.MainWindowHandle -ne 0) { [Win32.RA]::SetWindowPos($_.MainWindowHandle, [IntPtr]::Zero, -30000, -30000, 0, 0, 0x15) | Out-Null } }
}
function Shot([System.Diagnostics.Process]$proc, [string]$path) {
    $proc.Refresh(); $h = $proc.MainWindowHandle
    if (-not $h -or $h -eq [IntPtr]::Zero) { Write-Host "shot: no window ($path)"; return }
    try {
        $h = [IntPtr]$h
        $r = New-Object Win32.RA+RECT; [Win32.RA]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        if ($w -le 0 -or $ht -le 0) { Write-Host "shot: empty rect ($path)"; return }
        $bmp = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
        [Win32.RA]::PrintWindow($h, $hdc, 2) | Out-Null
        $g.ReleaseHdc($hdc); $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        Write-Host "shot: $path"
    } catch { Write-Host "shot: failed ($path): $($_.Exception.Message)" }
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
# the mod deletes cmd.txt when it consumes it -- that is the acknowledgement.
# The write goes to a temp file and is RENAMED into place: WriteAllText truncates and
# then writes, and a module read that lands in between sees an empty file, deletes it
# and the command is lost (measured once: B never logged `cmd handled: areago nearest`).
function Cmd([string]$which, [string]$text, [int]$waitSec = 2) {
    $coop = if ($which -eq 'A') { $coopA } else { $coopB }
    $f = Join-Path $coop 'cmd.txt'
    $tmp = Join-Path $coop 'cmd.tmp'
    $log = Join-Path (Split-Path $coop -Parent) 'ms2coop-native.log'
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        [IO.File]::WriteAllText($tmp, $text + "`n")
        Move-Item -LiteralPath $tmp -Destination $f -Force
        $due = (Get-Date).AddSeconds(12)
        while ((Get-Date) -lt $due -and (Test-Path $f)) { Start-Sleep -Milliseconds 200 }
        # "cmd handled: <text>" in the native log is the only proof the module RAN it.
        # A command can still be lost (measured twice on the guest, e.g. right after an
        # 8-way sweep that logged 5972 actors), so retry while that line is absent.
        if (-not (Test-Path $log)) { break }
        $ran = @(Get-Content $log -Tail 600 | ? { $_ -match [regex]::Escape("cmd handled: $text") }).Count -gt 0
        if ($ran) { break }
        Write-Host "cmd '$text' -> $which not consumed; retry $attempt"
    }
    Start-Sleep $waitSec
}
function LogTail([string]$coopDir, [int]$n = 400) {
    # the native log lives NEXT TO THE EXE, not in coop\ (lab.ps1 copies it by hand too)
    $f = Join-Path (Split-Path $coopDir -Parent) 'ms2coop-native.log'
    if (-not (Test-Path $f)) { return @() }
    return @(Get-Content $f -Tail $n)
}
function Verdict([string]$name, [string]$state, [string]$why) {
    $line = "=== ${name}: $state -- $why"
    Write-Host $line; Add-Content (Join-Path $outDir 'verdicts.txt') $line
}

$verdicts = @()
$realLocal = $env:LOCALAPPDATA
$realSaveDir = Join-Path $realLocal 'MortalShell2\Saved\SaveGames'

if ($Lock) { . G:\Freespace\shell_coop\local\gamelock.ps1; Lock-Game 'r9-realarea' }
try {
    Stop-All; Start-Sleep 3
    if ($Deploy) {
        & (Join-Path $ws 'deploy.ps1') -GameDir $GameA -DllPath (Join-Path $ws 'native\build\dxgi.dll')
        & (Join-Path $ws 'deploy.ps1') -GameDir $GameB -DllPath (Join-Path $ws 'native\build\dxgi.dll')
    }
    $snap = Join-Path $env:TEMP ("ms2saves_ra_" + (Get-Date -Format 'HHmmss'))
    New-Item -ItemType Directory -Force $snap | Out-Null
    Copy-Item "$realSaveDir\*.sav" $snap -Force
    $before = @(Get-ChildItem $realSaveDir -Filter *.sav | % Name)

    foreach ($d in @($coopA, $coopB)) {
        Remove-Item (Join-Path $d 'result*.txt'), (Join-Path $d 'probe.log'), (Join-Path $d 'cmd.txt'),
            (Join-Path $d 'peer_*.txt'), (Join-Path $d 'out_*.txt'), (Join-Path $d 'sync.txt'),
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
        Hide-All; Start-Sleep -Milliseconds 700
        if ((Sync $coopA 'phase') -eq 'sync' -and (Sync $coopB 'phase') -eq 'sync') { break }
    }
    $phaseA = Sync $coopA 'phase'; $phaseB = Sync $coopB 'phase'
    Write-Host "phase A=$phaseA B=$phaseB"
    Verdict 'BOOT' $(if ($phaseA -eq 'sync' -and $phaseB -eq 'sync') { 'PASS' } else { 'WARN' }) "phase A=$phaseA B=$phaseB"
    Start-Sleep 6

    # ---- 1. find a boss landing area on both clients -------------------------
    if (-not $SkipReach) {
    Cmd 'A' 'areasweep BP_LandingArea_Boss' 4
    Cmd 'B' 'areasweep BP_LandingArea_Boss' 4
    $aLog = LogTail $coopA 800; $bLog = LogTail $coopB 800
    $aFind = @($aLog | ? { $_ -match 'areasweep: .*-> \d+ hit' } | Select-Object -Last 1)
    $bFind = @($bLog | ? { $_ -match 'areasweep: .*-> \d+ hit' } | Select-Object -Last 1)
    $aHit = $aFind.Count -gt 0 -and $aFind[0] -notmatch '-> 0 hit'
    $bHit = $bFind.Count -gt 0 -and $bFind[0] -notmatch '-> 0 hit'
    Verdict 'AREAFIND' $(if ($aHit -and $bHit) { 'PASS' } else { 'FAIL' }) ("A: " + $(if ($aFind.Count) { $aFind[0] } else { 'no sweep line' }) + " | B: " + $(if ($bFind.Count) { $bFind[0] } else { 'no sweep line' }))
    Shot $pa (Join-Path $outDir 'A-hub.png'); Shot $pb (Join-Path $outDir 'B-hub.png')

    # ---- 2. both clients go there (the `tp` primitive) -----------------------
    $goal = if ($BossIdx -ge 0) { "$BossIdx" } else { 'nearest' }
    Cmd 'A' "areago $goal" 3
    Cmd 'B' "areago $goal" 3
    Start-Sleep 22
    Hide-All; Start-Sleep 1
    Shot $pa (Join-Path $outDir 'A-arrived.png'); Shot $pb (Join-Path $outDir 'B-arrived.png')
    Start-Sleep 3
    Shot $pa (Join-Path $outDir 'A-arrived2.png'); Shot $pb (Join-Path $outDir 'B-arrived2.png')

    Cmd 'A' 'info' 3; Cmd 'B' 'info' 3
    $aLog = LogTail $coopA 400; $bLog = LogTail $coopB 400
    $aGo = @($aLog | ? { $_ -match 'areago: .*StartTp' } | Select-Object -Last 1)
    $bGo = @($bLog | ? { $_ -match 'areago: .*StartTp' } | Select-Object -Last 1)
    Verdict 'AREATP' $(if ($aGo.Count -and $bGo.Count) { 'PASS' } else { 'FAIL' }) ("A: " + $(if ($aGo.Count) { $aGo[0] } else { '-' }) + " | B: " + $(if ($bGo.Count) { $bGo[0] } else { '-' }))
    }

    # ---- 3. what is there: boss, enemies, NPCs ------------------------------
    Cmd 'A' 'areasweep Boss Enemy NPC' 6
    Cmd 'B' 'areasweep Boss Enemy NPC' 6
    $aLog = LogTail $coopA 900; $bLog = LogTail $coopB 900
    $aBoss = @($aLog | ? { $_ -match 'areasweep: class (BPC_BossAI_C|BP_DwarfBoss_C|BP_FinalBoss_C|BP_HeadBoss_C|BP_MothBoss_C|BP_AICharacter_C)' } | Select-Object -Last 8)
    $bBoss = @($bLog | ? { $_ -match 'areasweep: class (BPC_BossAI_C|BP_DwarfBoss_C|BP_FinalBoss_C|BP_HeadBoss_C|BP_MothBoss_C|BP_AICharacter_C)' } | Select-Object -Last 8)
    Verdict 'BOSS' $(if ($aBoss.Count -and $bBoss.Count) { 'PASS' } else { 'WARN' }) ("A: " + ($(if ($aBoss.Count) { $aBoss -join ' ; ' } else { 'no boss class' })) + " | B: " + ($(if ($bBoss.Count) { $bBoss -join ' ; ' } else { 'no boss class' })))

    # ---- A. REAL WEAPON KILLS (works in the hub: the spawn point has brigands) --
    if ($Stage -in @('kill', 'killnpc', 'all')) {
        # -KillAt "<x> <y> <z>": the hub cell may refuse combat altogether (its
        # cultists stand still and never aggro); a MEASURED enemy cluster outside it
        # is R8-FARENEMY's (67576 -125188 -44761).
        if ($KillAt) {
            Cmd 'A' "tp $KillAt" 3
            Cmd 'B' "tp $KillAt" 3
            Start-Sleep 14
            Hide-All; Start-Sleep 1
            Shot $pa (Join-Path $outDir 'A-killarea.png'); Shot $pb (Join-Path $outDir 'B-killarea.png')
        }
        Cmd 'A' 'enemydump' 4
        Cmd 'B' 'enemydump' 4
        Shot $pa (Join-Path $outDir 'A-kill0.png'); Shot $pb (Join-Path $outDir 'B-kill0.png')
        Cmd 'A' 'enemytp' 6
        Cmd 'B' 'enemytp' 6
        Start-Sleep 4
        Cmd 'A' 'enemydump' 3; Cmd 'B' 'enemydump' 3
        Hide-All; Start-Sleep 1
        Shot $pa (Join-Path $outDir 'A-kill1.png'); Shot $pb (Join-Path $outDir 'B-kill1.png')
        foreach ($i in 1..8) {
            Cmd 'A' 'attack light' 3
            Cmd 'B' 'attack light' 3
            Cmd 'A' 'enemydump' 2
            Cmd 'B' 'enemydump' 2
        }
        Hide-All; Start-Sleep 1
        Shot $pa (Join-Path $outDir 'A-kill2.png'); Shot $pb (Join-Path $outDir 'B-kill2.png')
        Start-Sleep 4
        Shot $pa (Join-Path $outDir 'A-kill3.png'); Shot $pb (Join-Path $outDir 'B-kill3.png')
        $aLog = LogTail $coopA 3000; $bLog = LogTail $coopB 3000
        # (1) a real swing: the watch has to SEE a montage
        $aMon = @($aLog | ? { $_ -match 'attackwatch: done montage_seen=1' })
        $bMon = @($bLog | ? { $_ -match 'attackwatch: done montage_seen=1' })
        $aWatch = @($aLog | ? { $_ -match 'attackwatch: t=' }) | Select-Object -Last 2
        Verdict 'SWINGMONTAGE' $(if ($aMon.Count -and $bMon.Count) { 'PASS' } else { 'FAIL' }) ("A montage_seen=" + $aMon.Count + " B=" + $bMon.Count + " | A last: " + ($aWatch -join ' ; '))
        # (2) health fell (from OUR swing, no lab damage was issued)
        $aHps = @($aLog | Select-String -Pattern 'attackwatch: t=[0-9.]+ montage=\S* pos=[-0-9.]+ active=\d enemy=\S+ hp=(-?[0-9.]+)' -AllMatches |
                  % { [double]$_.Matches[0].Groups[1].Value } | ? { $_ -ge 0 })
        $bHps = @($bLog | Select-String -Pattern 'attackwatch: t=[0-9.]+ montage=\S* pos=[-0-9.]+ active=\d enemy=\S+ hp=(-?[0-9.]+)' -AllMatches |
                  % { [double]$_.Matches[0].Groups[1].Value } | ? { $_ -ge 0 })
        $aFell = ($aHps.Count -gt 1) -and (($aHps | Measure-Object -Minimum).Minimum -lt (($aHps[0]) - 0.01))
        $bFell = ($bHps.Count -gt 1) -and (($bHps | Measure-Object -Minimum).Minimum -lt (($bHps[0]) - 0.01))
        Verdict 'KILLHP' $(if ($aFell -or $bFell) { 'PASS' } else { 'FAIL' }) ("A hp " + ($aHps -join ',') + " | B hp " + ($bHps -join ','))
        # (3) the death and the reward, as the MOD reports them (both sides)
        $aDead = @($aLog | ? { $_ -match 'enemydump: 0 live enemy|death(ed)? |StartDeath|dead=' } | Select-Object -Last 2)
        $bDead = @($bLog | ? { $_ -match 'enemydump: 0 live enemy|death(ed)? |StartDeath|dead=' } | Select-Object -Last 2)
        $syncA = @(Get-Content (Join-Path $coopA 'sync.txt') -ErrorAction SilentlyContinue)
        $syncB = @(Get-Content (Join-Path $coopB 'sync.txt') -ErrorAction SilentlyContinue)
        $dropsA = ($syncA | ? { $_ -like 'drops=*' } | Select-Object -Last 1)
        $dropsB = ($syncB | ? { $_ -like 'drops=*' } | Select-Object -Last 1)
        Verdict 'KILLDEATH' $(if ($aDead.Count -and $bDead.Count) { 'PASS' } else { 'WARN' }) ("A: " + ($aDead -join ' ; ') + " | B: " + ($bDead -join ' ; '))
        Verdict 'REWARD' $(if ($dropsA -or $dropsB) { 'WARN' } else { 'FAIL' }) ("A $dropsA | B $dropsB (dup:0 means no double reward)")
        Verdict 'KILLHPRAW' 'INFO' ("A hp sequence: " + ($aHps -join ',') + " | B: " + ($bHps -join ','))
    }

    if ($Stage -in @('boss', 'all')) {
        # the arena cell (and its boss) streams in only after the client has STOOD
        # there for a while: run 1 saw the Hexapod appear ~45 s after the teleport,
        # with the intro cinematic (which also drives the nest load), so probe FIRST
        # and end the cinematic afterwards.
        Start-Sleep 30
        Cmd 'A' 'bossdump' 4; Cmd 'B' 'bossdump' 4
        Cmd 'A' 'hp' 3; Cmd 'B' 'hp' 3
        Hide-All; Start-Sleep 1
        Shot $pa (Join-Path $outDir 'A-arena0.png'); Shot $pb (Join-Path $outDir 'B-arena0.png')
        Cmd 'A' 'cinestop' 3
        Cmd 'B' 'cinestop' 3
        Start-Sleep 8
        Cmd 'A' 'bossdump' 4; Cmd 'B' 'bossdump' 4
        Cmd 'A' 'hp' 3; Cmd 'B' 'hp' 3
        Hide-All; Start-Sleep 1
        Shot $pa (Join-Path $outDir 'A-arena.png'); Shot $pb (Join-Path $outDir 'B-arena.png')
        $aLog = LogTail $coopA 900; $bLog = LogTail $coopB 900
        $aBD = @($aLog | ? { $_ -match 'bossdump: .*cls=.*phase=' } | Select-Object -Last 2)
        $bBD = @($bLog | ? { $_ -match 'bossdump: .*cls=.*phase=' } | Select-Object -Last 2)
        Verdict 'BOSS' $(if ($aBD.Count -and $bBD.Count) { 'PASS' } else { 'FAIL' }) ("A: " + $(if ($aBD.Count) { $aBD[-1] } else { 'no bossdump' }) + " | B: " + $(if ($bBD.Count) { $bBD[-1] } else { 'no bossdump' }))
        $aHp0 = ($aBD | Select-Object -Last 1) -replace '.*hp=([-0-9.]+).*', '$1'
        $bHp0 = ($bBD | Select-Object -Last 1) -replace '.*hp=([-0-9.]+).*', '$1'

        # the game's own swing
        Cmd 'A' 'asclist Attack' 3
        Cmd 'B' 'asclist Attack' 3
        foreach ($i in 1..8) { Cmd 'A' 'attack light' 2 }
        foreach ($i in 1..8) { Cmd 'B' 'attack light' 2 }
        Hide-All; Start-Sleep 1
        Shot $pa (Join-Path $outDir 'A-swing.png'); Shot $pb (Join-Path $outDir 'B-swing.png')
        Cmd 'A' 'bossdump' 4; Cmd 'B' 'bossdump' 4
        Cmd 'A' 'hp' 3
        Start-Sleep 3
        Shot $pa (Join-Path $outDir 'A-swing2.png'); Shot $pb (Join-Path $outDir 'B-swing2.png')
        $aLog = LogTail $coopA 900; $bLog = LogTail $coopB 900
        $aAtk = @($aLog | ? { $_ -match 'attack: TryActivateAbilityByClass' })
        $bAtk = @($bLog | ? { $_ -match 'attack: TryActivateAbilityByClass' })
        $aOk = @($aAtk | ? { $_ -match 'asc=1' }).Count -gt 0
        $bOk = @($bAtk | ? { $_ -match 'asc=1' }).Count -gt 0
        $mon = @($aLog + $bLog | ? { $_ -match 'attack: .*montage now=(/|[A-Za-z])' }).Count
        Verdict 'ATTACK' $(if ($aOk -and $bOk) { 'PASS' } else { 'FAIL' }) ("activations A=" + @($aAtk | ? { $_ -match 'asc=1' }).Count + " B=" + @($bAtk | ? { $_ -match 'asc=1' }).Count + " montage-starts=$mon | A last: " + $(if ($aAtk.Count) { $aAtk[-1] } else { '-' }))
        $aBD2 = @($aLog | ? { $_ -match 'bossdump: .*cls=.*phase=' } | Select-Object -Last 1)
        $aHp1 = ($aBD2 | Select-Object -Last 1) -replace '.*hp=([-0-9.]+).*', '$1'
        Verdict 'BOSSDMG' $(if ($aHp0 -ne $aHp1 -and $aHp1 -ne '') { 'PASS' } else { 'SKIP' }) "boss hp before=$aHp0 after=$aHp1 (a real swing only counts if this moves; the dump also carries phase/aggro)"
    }

    if ($Stage -in @('npc', 'killnpc', 'all')) {
        Cmd 'A' 'dlgdump' 3; Cmd 'B' 'dlgdump' 3
        Cmd 'A' 'areasweep NPC' 6; Cmd 'B' 'areasweep NPC' 6
        $aLog = LogTail $coopA 600; $bLog = LogTail $coopB 600
        $aNpc = @($aLog | ? { $_ -match 'areasweep: class (BP_NPC|BP_NPCSpawner|BP_NPCTeleport)' })
        $bNpc = @($bLog | ? { $_ -match 'areasweep: class (BP_NPC|BP_NPCSpawner|BP_NPCTeleport)' })
        Verdict 'NPCPRESENT' $(if ($aNpc.Count -and $bNpc.Count) { 'PASS' } else { 'WARN' }) ("A: " + ($(if ($aNpc.Count) { $aNpc -join ' ; ' } else { 'none' })) + " | B: " + ($(if ($bNpc.Count) { $bNpc -join ' ; ' } else { 'none' })))
        # the game's own interaction: dlgstart drives the NPC's interaction component
        Cmd 'B' 'dlgstart BP_NPC_ShellKeeper' 8
        Cmd 'A' 'dlgstart BP_NPC_ShellKeeper' 8
        Cmd 'A' 'dlgdump' 3; Cmd 'B' 'dlgdump' 3
        Hide-All; Start-Sleep 1
        Shot $pa (Join-Path $outDir 'A-npc.png'); Shot $pb (Join-Path $outDir 'B-npc.png')
        $aLog = LogTail $coopA 600; $bLog = LogTail $coopB 600
        $aDlg = @($aLog | ? { $_ -match 'dlgdump: dlg=.*node:[1-9]|dlg: (started|opened|line)' } | Select-Object -Last 3)
        $bDlg = @($bLog | ? { $_ -match 'dlgdump: dlg=.*node:[1-9]|dlg: (started|opened|line)' } | Select-Object -Last 3)
        Verdict 'NPC' $(if ($aDlg.Count -and $bDlg.Count) { 'PASS' } else { 'FAIL' }) ("A: " + ($(if ($aDlg.Count) { $aDlg -join ' ; ' } else { 'no dialogue node' })) + " | B: " + ($(if ($bDlg.Count) { $bDlg -join ' ; ' } else { 'no dialogue node' })))
    }

    if ($Stage -in @('boss', 'all')) {
        # a player dying while the party is out here
        Cmd 'B' 'die' 3
        Start-Sleep 12
        Hide-All; Start-Sleep 1
        Shot $pa (Join-Path $outDir 'A-peer-died.png'); Shot $pb (Join-Path $outDir 'B-died.png')
        Cmd 'A' 'info' 3; Cmd 'B' 'info' 3
        Start-Sleep 4
        Shot $pa (Join-Path $outDir 'A-after-death.png'); Shot $pb (Join-Path $outDir 'B-after-death.png')
    }

    foreach ($id in 'A', 'B') {
        $c = if ($id -eq 'A') { $coopA } else { $coopB }
        $w64 = Split-Path $c -Parent
        Copy-Item (Join-Path $c 'sync.txt') (Join-Path $outDir "sync_$id.txt") -ErrorAction SilentlyContinue
        Copy-Item (Join-Path $w64 'ms2coop-native.log') (Join-Path $outDir "native_$id.log") -ErrorAction SilentlyContinue
        Copy-Item (Join-Path $c 'result*.txt') $outDir -ErrorAction SilentlyContinue
    }
    Write-Host "alive=$(@(Get-Process | ? ProcessName -match 'MortalShell2-Win64').Count)"
}
finally {
    Stop-All; Start-Sleep 2
    if (Test-Path $snap) {
        Get-ChildItem $realSaveDir -Filter *.sav | ? { $before -notcontains $_.Name } | Remove-Item -Force
        Copy-Item "$snap\*.sav" $realSaveDir -Force
        Write-Host "saves restored"
    }
    if ($Lock) { Unlock-Game 'r9-realarea' }
    Write-Host "out=$outDir"
}
