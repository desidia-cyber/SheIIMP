# Build one contact sheet from a lab directory's PNGs so the frames can be LOOKED at in
# one image (label each tile with its file name).
#   .\contact-far.ps1 -Dir G:\Freespace\shell_coop\local\lab\<stamp>-<tag> [-Cols 3] [-Width 420]
param(
    [Parameter(Mandatory = $true)][string]$Dir,
    [int]$Cols = 3,
    [int]$Width = 420,
    [string]$Out = ""
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$files = Get-ChildItem $Dir -Filter *.png | Sort-Object Name
if ($files.Count -eq 0) { throw "no PNGs in $Dir" }
if (-not $Out) { $Out = Join-Path $Dir 'contact-sheet.png' }

$tiles = @()
foreach ($f in $files) {
    $img = [System.Drawing.Image]::FromFile($f.FullName)
    $h = [int]($img.Height * ($Width / $img.Width))
    $tiles += [pscustomobject]@{ Name = $f.Name; Img = $img; W = $Width; H = $h }
}
$label = 22
$rows = [math]::Ceiling($tiles.Count / $Cols)
$cellH = ($tiles | Measure-Object -Property H -Maximum).Maximum + $label
$sheet = New-Object System.Drawing.Bitmap ($Cols * $Width), ($rows * $cellH)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear([System.Drawing.Color]::FromArgb(24, 24, 24))
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$font = New-Object System.Drawing.Font 'Consolas', 11
$brush = [System.Drawing.Brushes]::White
for ($i = 0; $i -lt $tiles.Count; $i++) {
    $c = $i % $Cols; $r = [math]::Floor($i / $Cols)
    $x = $c * $Width; $y = $r * $cellH
    $g.DrawString($tiles[$i].Name, $font, $brush, [single]$x, [single]($y + 4))
    $g.DrawImage($tiles[$i].Img, $x, $y + $label, $tiles[$i].W, $tiles[$i].H)
}
$g.Dispose()
$sheet.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()
foreach ($t in $tiles) { $t.Img.Dispose() }
Write-Host "contact sheet: $Out ($($tiles.Count) tiles)"
