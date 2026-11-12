# area-lab.ps1 -- ONE client, lock -> deploy -> lab.ps1 -> unlock, for the round-9
# real-area discovery runs (the actor census, the area/data-layer list, the tomb-zone
# selectors).  Uses the game's REAL save (lab.ps1 snapshots and restores it).
#   .\area-lab.ps1 -GameDir E:\Games\_MS2-coop-e -Tag disc1 -Cmds "info@4,areasweep@10,arealayers@6,areatomb@6"
param(
    [string]$GameDir = "E:\Games\_MS2-coop-e",
    [string]$Tag = "area-disc",
    [string]$Cmds = "info@4,areasweep@10",
    [int]$BootWait = 95
)
$ErrorActionPreference = 'Stop'
$ws = $PSScriptRoot
$dll = Join-Path $ws 'native\build\dxgi.dll'
. G:\Freespace\shell_coop\local\gamelock.ps1
$me = "r9-realarea"
Lock-Game $me
try {
    Get-Process | ? ProcessName -match 'MortalShell2|CrashReportClient' | % { try { Stop-Process -Id $_.Id -Force } catch {} }
    Start-Sleep 2
    & (Join-Path $ws 'deploy.ps1') -GameDir $GameDir -DllPath $dll
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "deploy failed ($LASTEXITCODE)" }
    & (Join-Path $ws 'lab.ps1') -GameDir $GameDir -Commands $Cmds -Tag $Tag -BootWait $BootWait
} finally {
    Get-Process | ? ProcessName -match 'MortalShell2|CrashReportClient' | % { try { Stop-Process -Id $_.Id -Force } catch {} }
    Unlock-Game $me
}
