# [R6-SPAWN] lock -> deploy C/D -> N consecutive run-two sessions -> unlock.
# Each run is a FRESH process (the harness scripts collide on Add-Type otherwise)
# and every run's console output + verdict lines are saved under local\lab\.
#
# The lock is held for the WHOLE batch: run-two.ps1 kills every MortalShell2
# process and shares the save folder, so a second session must not start between
# runs (an earlier version of this wrapper unlocked after the deploy -- see
# REPORT.md ¬ß7).
param(
    [int]$Runs = 3,
    [int]$BootWait = 200,
    [string]$Tag = "r6",
    [switch]$NoDeploy
)
$ErrorActionPreference = 'Continue'
$ws = 'G:\Freespace\shell_coop\wt\r6-spawn\ms2-coop'
. 'G:\Freespace\shell_coop\local\gamelock.ps1'
Lock-Game 'r6spawn'
try {
    if (-not $NoDeploy) {
        foreach ($g in 'E:\Games\_MS2-coop-c', 'E:\Games\_MS2-coop-d') {
            & G:\Freespace\shell_coop\ms2-coop\deploy.ps1 -GameDir $g -DllPath "$ws\native\build\dxgi.dll" 2>&1 |
                Select-String -Pattern 'deployed|denied|error' | Select-Object -First 3
        }
    }
    for ($i = 1; $i -le $Runs; $i++) {
        $out = "G:\Freespace\shell_coop\local\lab\run-two-$Tag-$i.txt"
        Write-Host "=== RUN $i -> $out"
        $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$ws\run-two.ps1",
                  '-GameA', 'E:\Games\_MS2-coop-c', '-GameB', 'E:\Games\_MS2-coop-d',
                  '-Port', '47020', '-BootWait', "$BootWait")
        & powershell @args 2>&1 | Tee-Object -FilePath $out | Select-String -Pattern '^=== (SPAWN|MIRROR)|^RESULT|^=== WARNING'
        # the whole native logs of the run that just finished (overwritten by the next boot)
        foreach ($p in @(@{ id = 'A'; g = 'E:\Games\_MS2-coop-c' }, @{ id = 'B'; g = 'E:\Games\_MS2-coop-d' })) {
            Copy-Item (Join-Path $p.g 'MortalShell2\Binaries\Win64\ms2coop-native.log') `
                "G:\Freespace\shell_coop\local\lab\native-$Tag-$i-$($p.id).log" -Force -ErrorAction SilentlyContinue
        }
        Write-Host "=== RUN $i done"
        # the third boot of a batch is the one the game's own steam_api64 crash hits
        # (documented HANDOFF-R5 Åò3.9: 47-62 s after launch, no dxgi frame); let the
        # machine settle before the next pair of clients starts
        if ($i -lt $Runs) { Start-Sleep -Seconds 45 }
    }
} finally { Unlock-Game 'r6spawn' }
