# Builds one comparison image per level from two mobile_audit.ps1 runs: PC before | PC after on the top row,
# mobile before | mobile after below. Written to Saved/MobileAudit/<After>/sheets/<map>.png (and all.png, every
# level stacked at a smaller scale).
#   .\Tools\ue\mobile_contact_sheet.ps1 -Before before-shots -After after
param(
    [Parameter(Mandatory = $true)][string]$Before,
    [Parameter(Mandatory = $true)][string]$After,
    # Width of each cell in the per-level sheets (the overview uses half).
    [int]$CellWidth = 960
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\common.ps1"
Add-Type -AssemblyName System.Drawing

$root = "$KartProject\Saved\MobileAudit"
$out = "$root\$After\sheets"
New-Item -ItemType Directory -Force $out | Out-Null

function Get-Cell([string]$Path, [int]$Width) {
    $cellHeight = [int]($Width * 9 / 16)
    $cell = New-Object System.Drawing.Bitmap $Width, $cellHeight
    $g = [System.Drawing.Graphics]::FromImage($cell)
    $g.Clear([System.Drawing.Color]::FromArgb(24, 24, 32))
    if (Test-Path $Path) {
        $img = [System.Drawing.Image]::FromFile($Path)
        # Fit inside the 16:9 cell, keeping the shot's own aspect (phones are wider than 16:9).
        $scale = [Math]::Min($Width / $img.Width, $cellHeight / $img.Height)
        $w = [int]($img.Width * $scale); $h = [int]($img.Height * $scale)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.DrawImage($img, [int](($Width - $w) / 2), [int](($cellHeight - $h) / 2), $w, $h)
        $img.Dispose()
    }
    $g.Dispose()
    return $cell
}

function New-Sheet([string]$Map, [int]$Width) {
    $labels = @(
        @("$Before\${Map}_pc.png", "PC before"), @("$After\${Map}_pc.png", "PC after"),
        @("$Before\${Map}_mobile.png", "Mobile before"), @("$After\${Map}_mobile.png", "Mobile after"))
    $cellHeight = [int]($Width * 9 / 16)
    $title = 28
    $sheet = New-Object System.Drawing.Bitmap ($Width * 2), ($cellHeight * 2 + $title)
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.Clear([System.Drawing.Color]::Black)
    $font = New-Object System.Drawing.Font 'Segoe UI', 13, ([System.Drawing.FontStyle]::Bold)
    $g.DrawString($Map, $font, [System.Drawing.Brushes]::White, 6, 3)
    for ($i = 0; $i -lt 4; $i++) {
        $cell = Get-Cell "$root\$($labels[$i][0])" $Width
        $x = ($i % 2) * $Width; $y = $title + [Math]::Floor($i / 2) * $cellHeight
        $g.DrawImage($cell, $x, $y)
        $g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(170, 0, 0, 0))), $x, $y, 150, 24)
        $g.DrawString($labels[$i][1], $font, [System.Drawing.Brushes]::White, $x + 4, $y + 1)
        $cell.Dispose()
    }
    $g.Dispose()
    return $sheet
}

$maps = Get-ChildItem "$root\$After" -Filter '*_mobile.png' | ForEach-Object { $_.Name -replace '_mobile\.png$', '' }
foreach ($map in $maps) {
    $sheet = New-Sheet $map $CellWidth
    $sheet.Save("$out\$map.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $sheet.Dispose()
    Write-Output "$out\$map.png"
}
