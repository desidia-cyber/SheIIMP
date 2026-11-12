# [R7-BOSS] boss-phase lab: hold the run lock for the WHOLE run (deploy + the two
# game clients), deploy to E/F, then run one visual-two session that summons a boss
# on both clients, forces a phase change on the owner (A) through the game's own
# function, and dumps both sides.
#
# visual-two.ps1 is used (not run-two.ps1) because it already does the two things a
# boss lab needs: commands are written one at a time and waited for (cmd.txt is
# DELETED by the mod, so the file disappearing is the consume acknowledgement) and
# both windows are screenshot.  It does NOT take the lock itself, and it kills every
# MortalShell2 process on the machine -- which is why the lock is held here for the
# whole run, not just for the deploy (measured: releasing it before the run let
# another agent's script kill both clients 6 s after they started).
param(
    [string]$Tag = "bosslab",
    [int]$BootWait = 100,
    [string]$Cmds = "",
    [switch]$NoDeploy
)
$ErrorActionPreference = 'Continue'
$ws = 'G:\Freespace\shell_coop\wt\r7-boss\ms2-coop'
$gameA = 'E:\Games\_MS2-coop-e'
$gameB = 'E:\Games\_MS2-coop-f'
$port = 47120
$log = Join-Path $ws ("boss-lab-$Tag.out.txt")
. 'G:\Freespace\shell_coop\local\gamelock.ps1'

Lock-Game 'r7boss'
try {
    if (-not $NoDeploy) {
        foreach ($g in $gameA, $gameB) {
            & G:\Freespace\shell_coop\ms2-coop\deploy.ps1 -GameDir $g -DllPath "$ws\native\build\dxgi.dll" 2>&1 |
                Select-String -Pattern 'deployed|denied|error|ERROR' | Select-Object -First 4
        }
    }
    # R7-COMMON: delete the world-state ledgers before a run until the r7-wire fixture
    # reset is merged (a stale ledger re-applies keys from an earlier session).
    foreach ($g in $gameA, $gameB) {
        Get-ChildItem (Join-Path $g 'MortalShell2\Binaries\Win64\coop') -Filter 'ws_ledger_*.txt' -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue
    }

    $argv = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$ws\visual-two.ps1",
              '-GameA', $gameA, '-GameB', $gameB, '-Port', "$port", '-Tag', $Tag,
              '-BootWait', "$BootWait")
    if ($Cmds) { $argv += @('-Cmds', $Cmds) }
    & powershell @argv 2>&1 | Tee-Object -FilePath $log
} finally {
    Unlock-Game 'r7boss'
}
Write-Host "boss-lab: output at $log"
