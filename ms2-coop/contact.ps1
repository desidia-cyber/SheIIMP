# R6-BODY contact sheet: every screenshot in a lab folder laid out in a grid with its
# file name, so a whole run can be LOOKED at in one image (log verdicts have lied).
#   .\contact.ps1 -Dir G:\Freespace\shell_coop\local\lab\<stamp>-r6body
param(
    [Parameter(Mandatory = $true)][string]$Dir,
    [string]$Out = "",
    [string]$Filter = "*.png",
    [int]$Cols = 3,
    [int]$CellW = 520
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$files = @(Get-ChildItem (Join-Path $Dir $Filter) | Sort-Object Name)
if (-not $files) { throw "no $Filter in $Dir" }
if (-not $Out) { $Out = Join-Path $Dir ("contact-" + $Filter.Replace('*', '').Replace('.', '') + ".png") }
$rows = [int][Math]::Ceiling($files.Count / $Cols)
$probe = [System.Drawing.Image]::FromFile($files[0].FullName)
$cellH = [int]($CellW * $probe.Height / $probe.Width) + 18
$probe.Dispose()
$sheet = New-Object System.Drawing.Bitmap ($Cols * $CellW), ($rows * $cellH)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear([System.Drawing.Color]::Black)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$font = New-Object System.Drawing.Font 'Consolas', 11
$brush = [System.Drawing.Brushes]::Yellow
for ($i = 0; $i -lt $files.Count; $i++) {
    $x = ($i % $Cols) * $CellW
    $y = [int][Math]::Floor($i / $Cols) * $cellH
    $img = [System.Drawing.Image]::FromFile($files[$i].FullName)
    $g.DrawImage($img, $x, $y + 18, $CellW, $cellH - 18)
    $img.Dispose()
    $g.DrawString($files[$i].Name, $font, $brush, $x + 3, $y + 1)
}
$g.Dispose()
$sheet.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
Write-Host "contact sheet: $Out ($($files.Count) images)"
