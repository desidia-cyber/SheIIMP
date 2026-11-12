# [R5-WORLD] run-two.ps1 snippet -- the STORE_A2 verdict (gap G1/G1b, store A2).
#
# Merge instructions for the integrator (three mechanical edits, all additive):
#   1. add the three new files to the remove list near the top of run-two.ps1:
#        (Join-Path $coopA 'result_ws2.txt'), (Join-Path $coopB 'result_ws2.txt'),
#        (Join-Path $coopA 'ws2_census.txt'), (Join-Path $coopA 'result_ws2selftest.txt'),
#        (Join-Path $coopA 'ws2_wa2test.txt')
#   2. paste the block below immediately after the CHEST section
#      (i.e. after `Write-Host "=== CHEST: $resChest"`).
#   3. extend the pass line:
#        if (-not $StreamOnly) { $pass = $pass -and ... -and ($resWs2 -match '^PASS') }
#
# The verdict is written by the RECEIVING client (coopB\result_ws2.txt) through
# coop::world::Ws2Tick -> Judge(), and it only counts a store-A2 row: a boolean on
# one of the 7 classes (or 5 fields) round 5 added, plus at least one typed int
# applied out of the "wt" section.

# store A2 (gap G1 + G1b): the host first resets its fixtures through the save
# subsystem's own API (ResetSaveObjectByGuid, falling back to ResetSaveObjectByClass)
# so the verb is re-runnable, then sets one boolean per new save-object class, bumps
# one int per typed class and appends one string-array element; the guest has to
# replay the boolean through "ws", apply the ints and union the array through "wt".
[System.IO.File]::WriteAllText((Join-Path $coopA 'cmd.txt'), "wa2selftest`n")
Start-Sleep -Seconds 2
[System.IO.File]::WriteAllText((Join-Path $coopA 'cmd.txt'), "wa2census`n")
Start-Sleep -Seconds 3
[System.IO.File]::WriteAllText((Join-Path $coopA 'cmd.txt'), "wa2test`n")
$ws2File = Join-Path $coopB 'result_ws2.txt'
$ws2Deadline = (Get-Date).AddSeconds(90)
while (-not (Test-Path $ws2File) -and (Get-Date) -lt $ws2Deadline) { Hide-GameWindow; Start-Sleep -Milliseconds 700 }
$resWs2 = if (Test-Path $ws2File) { (Get-Content $ws2File -Raw).Trim() } else { $null }
Write-Host "=== STORE_A2: A $(Get-SyncValue $coopA 'ws2') ; B $(Get-SyncValue $coopB 'ws2')"
Write-Host "=== STORE_A2: $resWs2"
$stFile = Join-Path $coopA 'result_ws2selftest.txt'
Write-Host "=== STORE_A2SELFTEST: $(if (Test-Path $stFile) { (Get-Content $stFile | Select-Object -First 1) } else { '-' })"
$censusFile = Join-Path $coopA 'ws2_census.txt'
Write-Host "=== STORE_A2CENSUS: $(if (Test-Path $censusFile) { (Get-Content $censusFile | Select-String -Pattern 'instances=' | Measure-Object).Count } else { 0 }) census rows (ws2_census.txt)"
