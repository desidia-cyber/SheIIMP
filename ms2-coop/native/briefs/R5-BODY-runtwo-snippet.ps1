# ===========================================================================
# [R5-BODY] run-two verdicts -- paste this block into run-two.ps1 immediately
# after the LEDGER block (after the two "=== LEDGER:" Write-Hosts) and add
#   $zoneOk -and $peerHpOk -and $peerDeadOk
# to the $pass expression.  Nothing above it changes.
#
# Depends on helpers defined earlier in run-two.ps1: Get-SyncValue (line ~216)
# and Wait-File (line ~362), plus $coopA / $coopB / Hide-GameWindow.
#
# The three verdicts are written by the RECEIVING client (A):
#   coop\result_zone.txt    a mismatch paused the world-state dispatch and it
#                           resumed when the maps agreed again (ZONE)
#   coop\result_bh.txt      the friend's body state arrived and the puppet's own
#                           health set reports it (PEERHP)
#   coop\result_bhdead.txt  the friend's death played a DeathsLibrary clip on the
#                           puppet (PEERDEAD)
# ===========================================================================

function Get-ZoneField([string]$coopDir, [string]$field) {
    $z = Get-SyncValue $coopDir 'zone'
    if ($z -and ($z -match ($field + ':(\S+)'))) { return $matches[1] }
    return $null
}

# --- ZONE: force a map mismatch on the guest; the host must stop applying the
# guest's world state (and say so) and resume when the maps agree again.
$zoneFile = Join-Path $coopA 'result_zone.txt'
Remove-Item $zoneFile -Force -ErrorAction SilentlyContinue
$zoneBefore = Get-SyncValue $coopA 'zone'
[System.IO.File]::WriteAllText((Join-Path $coopB 'cmd.txt'), "zonefake /Game/Sparta/Maps/R5/Fake_ZoneTest`n")
$zoneMismatch = $false
$zmDeadline = (Get-Date).AddSeconds(12)
while ((Get-Date) -lt $zmDeadline) {
    $z = Get-SyncValue $coopA 'zone'
    if ($z -and $z.Contains('same:0') -and $z.Contains('paused:1')) { $zoneMismatch = $true; break }
    Hide-GameWindow; Start-Sleep -Milliseconds 700
}
$zoneAp0 = Get-ZoneField $coopA 'ap'
$zoneSk0 = Get-ZoneField $coopA 'sk'
Start-Sleep -Seconds 4
$zoneAp1 = Get-ZoneField $coopA 'ap'
$zoneSk1 = Get-ZoneField $coopA 'sk'
[System.IO.File]::WriteAllText((Join-Path $coopB 'cmd.txt'), "zonefake off`n")
$zoneResumed = $false
$zrDeadline = (Get-Date).AddSeconds(12)
while ((Get-Date) -lt $zrDeadline) {
    $z = Get-SyncValue $coopA 'zone'
    if ($z -and $z.Contains('same:1') -and $z.Contains('paused:0')) { $zoneResumed = $true; break }
    Hide-GameWindow; Start-Sleep -Milliseconds 700
}
Start-Sleep -Seconds 3
$zoneAp2 = Get-ZoneField $coopA 'ap'
$resZone = Wait-File $zoneFile 15
$zoneFrozen = ($null -ne $zoneAp0) -and ($zoneAp0 -eq $zoneAp1) -and ([int]$zoneSk1 -gt [int]$zoneSk0)
$zoneMoving = ($null -ne $zoneAp2) -and ([int]$zoneAp2 -gt [int]$zoneAp1)
$zoneOk = $zoneMismatch -and $zoneResumed -and $zoneFrozen -and $zoneMoving -and ($resZone -match '^PASS')
Write-Host "=== ZONE: before '$zoneBefore'"
Write-Host "=== ZONE: mismatch=$zoneMismatch (applies $zoneAp0 -> $zoneAp1 and skips $zoneSk0 -> $zoneSk1 while paused) resumed=$zoneResumed (applies -> $zoneAp2)"
Write-Host "=== ZONE: A $(Get-SyncValue $coopA 'zone')"
Write-Host "=== ZONE: B $(Get-SyncValue $coopB 'zone')"
Write-Host "=== ZONE: $resZone"
Write-Host "=== ZONE: $(if ($zoneOk) { 'PASS' } else { 'FAIL' })"

# --- PEERHP: the guest writes its own body state; the host must receive it
# within 3 s and put it into the puppet's own health set.  The wire carries
# ABSOLUTE attribute values, so compare FRACTIONS, never the raw numbers.
$bhFile = Join-Path $coopA 'result_bh.txt'
[System.IO.File]::WriteAllText((Join-Path $coopB 'cmd.txt'), "bodyset hp 0.37`nbodyset res 0.5`nbodyset heal 1`n")
$peerHpSeen = $false
$bhDeadline = (Get-Date).AddSeconds(3)
while ((Get-Date) -lt $bhDeadline) {
    $bh = Get-SyncValue $coopA 'bh'
    if ($bh -and ($bh -match 'peer:hp([\d.]+)/([\d.]+)')) {
        $php = [double]$matches[1]; $pmax = [double]$matches[2]
        if (($pmax -gt 0) -and ([Math]::Abs($php / $pmax - 0.37) -le 0.05)) { $peerHpSeen = $true; break }
    }
    Hide-GameWindow; Start-Sleep -Milliseconds 300
}
$resBh = Wait-File $bhFile 12
$bhFracOk = $false
if ($resBh -and ($resBh -match 'hp=([\d.]+)/([\d.]+)')) {
    $rhp = [double]$matches[1]; $rmax = [double]$matches[2]
    if (($rmax -gt 0) -and ([Math]::Abs($rhp / $rmax - 0.37) -le 0.05)) { $bhFracOk = $true }
}
$peerHpOk = $peerHpSeen -and $bhFracOk -and ($resBh -match '^PASS')
Write-Host "=== PEERHP: A $(Get-SyncValue $coopA 'bh')"
Write-Host "=== PEERHP: B $(Get-SyncValue $coopB 'bh')"
Write-Host "=== PEERHP: $resBh"
Write-Host "=== PEERHP: $(if ($peerHpOk) { 'PASS' } else { 'FAIL' }) (received=$peerHpSeen puppetMatches=$bhFracOk)"

# --- PEERDEAD: the guest dies through the game's own StartDeath; the host's
# puppet has to play the death clip (and hold it).  This is the LAST check on
# purpose: a player death may take the guest's world down with it.
# The receiver rewrites its verdict file for EVERY death episode (a peer can die
# and be revived several times in a run), so a write that happened AFTER this
# test triggered one is required -- an earlier, spontaneous death must not count.
$deadFile = Join-Path $coopA 'result_bhdead.txt'
$deadT0 = Get-Date
[System.IO.File]::WriteAllText((Join-Path $coopB 'cmd.txt'), "bodyset ds 1`n")
$peerDeadSeen = $false
$pdDeadline = (Get-Date).AddSeconds(20)
while ((Get-Date) -lt $pdDeadline) {
    $bh = Get-SyncValue $coopA 'bh'
    if ($bh -and $bh.Contains('pdf:1')) { $peerDeadSeen = $true; break }
    Hide-GameWindow; Start-Sleep -Milliseconds 500
}
$resDead = $null
$deadDeadline = (Get-Date).AddSeconds(15)
while ((Get-Date) -lt $deadDeadline) {
    if ((Test-Path $deadFile) -and ((Get-Item $deadFile).LastWriteTime -gt $deadT0)) {
        $resDead = (Get-Content $deadFile -Raw).Trim(); break
    }
    Hide-GameWindow; Start-Sleep -Milliseconds 400
}
$peerDeadOk = $peerDeadSeen -and ($resDead -match '^PASS')
Write-Host "=== PEERDEAD: A $(Get-SyncValue $coopA 'bh')"
Write-Host "=== PEERDEAD: B $(Get-SyncValue $coopB 'bh')"
Write-Host "=== PEERDEAD: $resDead"
Write-Host "=== PEERDEAD: $(if ($peerDeadOk) { 'PASS' } else { 'FAIL' }) (clipOnPuppet=$peerDeadSeen)"
