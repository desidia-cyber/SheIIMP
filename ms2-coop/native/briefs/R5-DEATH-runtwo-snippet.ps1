# R5-DEATH -- the death verdict block, for merging into run-two.ps1.
#
# The integrator's job: paste the block below after run-two.ps1's existing per-feature
# checks (after the two clients are linked and in gameplay), then add $deathOk to
# run-two.ps1's $pass expression at the bottom:
#
#     $pass = ... -and $deathOk
#
# It needs three things that already exist in run-two.ps1: $coopA, $coopB and
# Hide-GameWindow, plus Get-SyncValue.  It defines its own helpers so a merge cannot
# collide.  THIS TEXT IS THE BLOCK THAT RAN: run-two-death.ps1 -Phase <name> is the
# three-phase wrapper around exactly this code (each phase in its own session, because
# a SECOND death in one session is not producible -- see R5-DEATH-FINDINGS.md section 7).
#
# Verdicts as last measured (this agent's own runs):
#   DCASE2  PASS   (twice)
#   DHOLD   partial: hold PASS, release PASS, camera FAIL (viewpuppet=0)
#   DWIPE   PASS

function Get-StatusField([string]$coopDir, [string]$field) {
    $f = Join-Path $coopDir 'sync.txt'
    if (-not (Test-Path $f)) { return $null }
    $t = Get-Content $f -Raw
    if (-not $t) { return $null }
    if ($t -match "$field[=:]([^\s,]+)") { return $matches[1] }
    return $null
}
function Get-DeathField([string]$coopDir, [string]$field) {
    $d = Get-SyncValue $coopDir 'death'
    if (-not $d) { return $null }
    if ($d -match "$field`:(\S+)") { return $matches[1] }
    return $null
}
function Send-Cmd([string]$coopDir, [string]$cmd) {
    [System.IO.File]::WriteAllText((Join-Path $coopDir 'cmd.txt'), $cmd + "`n")
}
function Wait-Until([int]$secs, [scriptblock]$cond) {
    $end = (Get-Date).AddSeconds($secs)
    while ((Get-Date) -lt $end) {
        Hide-GameWindow
        if (& $cond) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}
function Show-Death([string]$tag, [string]$coopDir) {
    Write-Host "--- $tag death: $(Get-SyncValue $coopDir 'death')"
    Write-Host "--- $tag       $(Get-SyncValue $coopDir 'deathstate') maxhp=$(Get-SyncValue $coopDir 'maxhp')"
}

# --- the link: both clients in gameplay and each seeing the other -------------
$linked = Wait-Until 150 {
    ((Get-SyncValue $coopA 'phase') -eq 'sync') -and ((Get-SyncValue $coopB 'phase') -eq 'sync') -and
    (Get-SyncValue $coopA 'peerbody') -and (Get-SyncValue $coopB 'peerbody') -and
    ((Get-DeathField $coopB 'peer') -ne '0(none)')
}
Write-Host "=== LINK: $(if ($linked) { 'PASS both clients in gameplay, peer seen' } else { 'FAIL (no link)' })"

# -----------------------------------------------------------------------------
# DCASE2 -- B dies, A alive, no boss: live-world respawn + one stack + 90 % max HP
# -----------------------------------------------------------------------------
if ($Phase -eq 'all' -or $Phase -eq 'case2') {
Show-Death 'A' $coopA
Show-Death 'B' $coopB
$wrA0 = [int](Get-StatusField $coopA 'worldresets'); if (-not $wrA0) { $wrA0 = 0 }
$wrB0 = [int](Get-StatusField $coopB 'worldresets'); if (-not $wrB0) { $wrB0 = 0 }
$hpB0 = [double](Get-StatusField $coopB 'maxhp')
Write-Host "=== DCASE2 baseline: B maxhp=$hpB0 worldresets A=$wrA0 B=$wrB0"

Send-Cmd $coopB 'die'
$case2Seen = Wait-Until 25 { (Get-DeathField $coopB 'case') -eq '2' }
Write-Host "=== DCASE2 case: $(if ($case2Seen) { 'PASS the death was decided as case 2' } else { "FAIL ($(Get-SyncValue $coopB 'death'))" })"
$case2Alive = Wait-Until 45 { (Get-DeathField $coopB 'me') -eq '1(alive)' -and (Get-DeathField $coopB 'pending') -eq '0' }
$wrA1 = [int](Get-StatusField $coopA 'worldresets'); if (-not $wrA1) { $wrA1 = 0 }
$wrB1 = [int](Get-StatusField $coopB 'worldresets'); if (-not $wrB1) { $wrB1 = 0 }
$stk2 = Get-DeathField $coopB 'stacks'
$hpB1 = [double](Get-StatusField $coopB 'maxhp')
$ratio = if ($hpB0 -gt 0) { $hpB1 / $hpB0 } else { 0 }
Write-Host "=== DCASE2: B $(Get-SyncValue $coopB 'death') maxhp $hpB0 -> $hpB1 (ratio $([math]::Round($ratio,3))) worldresets A $wrA0->$wrA1 B $wrB0->$wrB1"
$case2Ok = $case2Seen -and $case2Alive -and ($wrA1 -eq $wrA0) -and ($wrB1 -eq $wrB0) -and
           ($stk2 -eq '1') -and ($ratio -gt 0.85) -and ($ratio -lt 0.95)
Write-Host "=== DCASE2: $(if ($case2Ok) { 'PASS B respawned in the live world, no reset, stacks=1, max health ~90%' } else { 'FAIL' })"
} else { $case2Ok = $true }

# -----------------------------------------------------------------------------
# DHOLD -- bossfake 1, B dies: held + camera on the partner's body; bossfake 0 -> respawned
# -----------------------------------------------------------------------------
if ($Phase -eq 'all' -or $Phase -eq 'hold') {
Send-Cmd $coopB 'bossfake 1'
Start-Sleep -Seconds 2
Send-Cmd $coopB 'die'
$holdSeen = Wait-Until 25 { (Get-DeathField $coopB 'case') -eq '3' -and (Get-DeathField $coopB 'pending') -eq '1' }
$viewOk = Wait-Until 15 { (Get-StatusField $coopB 'viewpuppet') -eq '1' }
Show-Death 'B(hold)' $coopB
Write-Host "=== DHOLD: $(if ($holdSeen) { 'PASS B is held (case 3, pending)' } else { "FAIL ($(Get-SyncValue $coopB 'death'))" })"
Write-Host "=== DHOLD: $(if ($viewOk) { 'PASS the view target is the partner puppet' } else { "FAIL viewpuppet=$(Get-StatusField $coopB 'viewpuppet')" })"
Send-Cmd $coopB 'bossfake 0'
$holdReleased = Wait-Until 45 { (Get-DeathField $coopB 'me') -eq '1(alive)' -and (Get-DeathField $coopB 'pending') -eq '0' }
Show-Death 'B(after hold)' $coopB
Write-Host "=== DHOLD release: $(if ($holdReleased) { 'PASS B respawned once the fight was over' } else { 'FAIL' })"
$holdOk = $holdSeen -and $viewOk -and $holdReleased
Write-Host "=== DHOLD: $(if ($holdOk) { 'PASS' } else { 'FAIL' })"
} else { $holdOk = $true }

# -----------------------------------------------------------------------------
# DWIPE -- both die: the HOST's reset runs, the guest's does not, the guest follows
# -----------------------------------------------------------------------------
if ($Phase -eq 'all' -or $Phase -eq 'wipe') {
$stack2 = [int](Get-DeathField $coopB 'stacks')
$wrA2 = [int](Get-StatusField $coopA 'worldresets'); if (-not $wrA2) { $wrA2 = 0 }
$wrB2 = [int](Get-StatusField $coopB 'worldresets'); if (-not $wrB2) { $wrB2 = 0 }
Write-Host "=== DWIPE baseline: worldresets A=$wrA2 B=$wrB2 stacks B=$stack2"
Send-Cmd $coopA 'die'
Send-Cmd $coopB 'die'
$bothDown = Wait-Until 20 {
    $da = Get-SyncValue $coopA 'death'; $db = Get-SyncValue $coopB 'death'
    ($da -match 'case:1') -or ($db -match 'case:1')
}
Show-Death 'A(wipe)' $coopA
Show-Death 'B(wipe)' $coopB
Write-Host "=== DWIPE case: $(if ($bothDown) { 'PASS a party wipe was seen' } else { 'FAIL (no case 1 seen)' })"
$hostReset = Wait-Until 60 { ([int](Get-StatusField $coopA 'worldresets')) -gt $wrA2 }
$guestFollow = Wait-Until 90 {
    ((Get-DeathField $coopA 'me') -eq '1(alive)') -and ((Get-DeathField $coopB 'me') -eq '1(alive)') -and
    ((Get-DeathField $coopA 'pending') -eq '0') -and ((Get-DeathField $coopB 'pending') -eq '0')
}
$wrA3 = [int](Get-StatusField $coopA 'worldresets'); if (-not $wrA3) { $wrA3 = 0 }
$wrB3 = [int](Get-StatusField $coopB 'worldresets'); if (-not $wrB3) { $wrB3 = 0 }
$stkA = Get-DeathField $coopA 'stacks'
$stkB = Get-DeathField $coopB 'stacks'
Show-Death 'A(after wipe)' $coopA
Show-Death 'B(after wipe)' $coopB
Write-Host "=== DWIPE: host reset A worldresets $wrA2->$wrA3 ; guest did NOT reset its own (B $wrB2->$wrB3)"
Write-Host "=== DWIPE: both alive=$guestFollow stacks A=$stkA B=$stkB"
$wipeOk = $bothDown -and $hostReset -and $guestFollow -and ($wrB3 -eq $wrB2) -and ($stkA -eq '0') -and ($stkB -eq '0')
Write-Host "=== DWIPE: $(if ($wipeOk) { 'PASS host reset, guest followed, both alive, 0 stacks' } else { 'FAIL' })"
} else { $wipeOk = $true }

