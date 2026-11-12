# [R6-SPAWN] lock -> deploy to C/D -> run one spawn-lab session -> unlock.
# One process, so the lock is taken once and always released (the lab script locks
# itself for its own run; this wrapper only holds it for the deploy).
param(
    [string]$Tag = "spawn",
    [int]$BootWait = 200,
    [int]$Secs = 60,
    [string]$SpawnPoint = "66992 -125076 -44454",
    [switch]$NoDeploy,
    [switch]$Phase2
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
} finally { Unlock-Game 'r6spawn' }
$args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$ws\spawn-lab.ps1",
          '-Tag', $Tag, '-BootWait', "$BootWait", '-Secs', "$Secs", '-SpawnPoint', $SpawnPoint)
if ($Phase2) { $args += '-Phase2' }
& powershell @args
