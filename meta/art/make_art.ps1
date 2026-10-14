<#
  Generates the Wii U menu icon and splash screens in meta/ (Windows only, uses System.Drawing).
    icon.png        128x128  HOME Menu / Aroma icon
    tv-splash.png   1280x720 shown on the TV while the app loads
    drc-splash.png  854x480  shown on the GamePad while the app loads
  Original placeholder artwork: no Valve logos or game art (they're trademarked / copyrighted).
  powershell -ExecutionPolicy Bypass -File meta\art\make_art.ps1
#>
Add-Type -AssemblyName System.Drawing
$meta = Split-Path $PSScriptRoot

function New-Art([int]$w, [int]$h, [string]$path, [bool]$isIcon) {
	$bmp = New-Object System.Drawing.Bitmap $w, $h
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$g.SmoothingMode = 'AntiAlias'
	$g.TextRenderingHint = 'AntiAliasGridFit'

	$rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
	$top = [System.Drawing.Color]::FromArgb(255, 34, 30, 26)
	$bottom = [System.Drawing.Color]::FromArgb(255, 120, 52, 10)
	$bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, $top, $bottom, 90
	$g.FillRectangle($bg, $rect)

	# orange ring
	$ringSize = [Math]::Min($w, $h) * ($(if ($isIcon) { 0.78 } else { 0.42 }))
	$cx = $w / 2; $cy = $(if ($isIcon) { $h / 2 } else { $h * 0.40 })
	$penW = [Math]::Max(3, $ringSize * 0.07)
	$pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 255, 150, 30)), $penW
	$g.DrawEllipse($pen, [float]($cx - $ringSize / 2), [float]($cy - $ringSize / 2), [float]$ringSize, [float]$ringSize)

	# "HL" monogram inside the ring
	$fontSize = $ringSize * 0.38
	$font = New-Object System.Drawing.Font 'Arial Black', ([float]$fontSize), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
	$fmt = New-Object System.Drawing.StringFormat
	$fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
	$white = [System.Drawing.Brushes]::White
	$g.DrawString('HL', $font, $white, [float]$cx, [float]$cy, $fmt)

	if (-not $isIcon) {
		$titleFont = New-Object System.Drawing.Font 'Arial', ([float]($h * 0.075)), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
		$subFont = New-Object System.Drawing.Font 'Arial', ([float]($h * 0.035)), ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
		$g.DrawString('Half-Life', $titleFont, $white, [float]$cx, [float]($h * 0.74), $fmt)
		$grey = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 220, 200, 180))
		$g.DrawString('Xash3D FWGS for Wii U  -  loading...', $subFont, $grey, [float]$cx, [float]($h * 0.84), $fmt)
	}

	$g.Dispose()
	$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
	$bmp.Dispose()
	Write-Host "Wrote $path"
}

New-Art 128 128 (Join-Path $meta 'icon.png') $true
New-Art 1280 720 (Join-Path $meta 'tv-splash.png') $false
New-Art 854 480 (Join-Path $meta 'drc-splash.png') $false
