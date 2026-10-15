<#
  Builds the Wii U Menu art in meta/ from the owner's source files in meta/source (Windows only, uses
  System.Drawing):
    icon.png / icon-bshift.png / icon-opfor.png   128x128  Wii U Menu icons (hl.png, bs.png, of.png)
    tv-splash.png                                  1280x720 shown on the TV while the app loads (intro.png)
    drc-splash.png                                 854x480  shown on the GamePad while the app loads (intro.png)
    bootSound.btsnd.gz                             boot sound (intro.wav), added to each .wuhb by
                                                   scripts/wuhb-add-file.ps1 (build.ps1 runs it)
  powershell -ExecutionPolicy Bypass -File meta\art\make_art.ps1
#>
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$meta = Split-Path $PSScriptRoot
$src = Join-Path $meta 'source'

function Save-Scaled([string]$from, [string]$to, [int]$w, [int]$h) {
	$in = [System.Drawing.Image]::FromFile((Join-Path $src $from))
	try {
		$out = New-Object System.Drawing.Bitmap $w, $h
		$g = [System.Drawing.Graphics]::FromImage($out)
		$g.InterpolationMode = 'HighQualityBicubic'
		$g.PixelOffsetMode = 'HighQuality'
		$g.SmoothingMode = 'HighQuality'
		$g.Clear([System.Drawing.Color]::Black)
		# keep the aspect ratio, centred on black (854x480 isn't exactly 16:9)
		$scale = [Math]::Min($w / $in.Width, $h / $in.Height)
		$dw = [int][Math]::Round($in.Width * $scale); $dh = [int][Math]::Round($in.Height * $scale)
		$g.DrawImage($in, [int](($w - $dw) / 2), [int](($h - $dh) / 2), $dw, $dh)
		$g.Dispose()
		$out.Save((Join-Path $meta $to), [System.Drawing.Imaging.ImageFormat]::Png)
		$out.Dispose()
	}
	finally { $in.Dispose() }
	Write-Host "Wrote meta/$to"
}

Save-Scaled 'hl.png' 'icon.png' 128 128
Save-Scaled 'bs.png' 'icon-bshift.png' 128 128
Save-Scaled 'of.png' 'icon-opfor.png' 128 128
Save-Scaled 'intro.png' 'tv-splash.png' 1280 720
Save-Scaled 'intro.png' 'drc-splash.png' 854 480

# .btsnd: big-endian u32 type (0 = play once) and u32 loop start (0), then 48 kHz 16-bit stereo PCM,
# big-endian (format as read by vgmstream's btsnd.c; at most 960000 samples, 20 s)
$wav = [IO.File]::ReadAllBytes((Join-Path $src 'intro.wav'))
if ([Text.Encoding]::ASCII.GetString($wav, 0, 4) -ne 'RIFF' -or [Text.Encoding]::ASCII.GetString($wav, 8, 4) -ne 'WAVE') { throw 'intro.wav is not a WAV file' }
$pos = 12; $fmtOk = $false; $data = $null
while ($pos + 8 -le $wav.Length) {
	$id = [Text.Encoding]::ASCII.GetString($wav, $pos, 4)
	$size = [BitConverter]::ToInt32($wav, $pos + 4)
	if ($id -eq 'fmt ') {
		$format = [BitConverter]::ToInt16($wav, $pos + 8); $channels = [BitConverter]::ToInt16($wav, $pos + 10)
		$rate = [BitConverter]::ToInt32($wav, $pos + 12); $bits = [BitConverter]::ToInt16($wav, $pos + 22)
		if ($format -ne 1 -or $channels -ne 2 -or $rate -ne 48000 -or $bits -ne 16) {
			throw "intro.wav must be 48000 Hz 16-bit stereo PCM (is format $format, $channels ch, $rate Hz, $bits bit)"
		}
		$fmtOk = $true
	}
	elseif ($id -eq 'data') { $data = [byte[]]::new($size); [Array]::Copy($wav, $pos + 8, $data, 0, $size) }
	$pos += 8 + $size + ($size % 2)
}
if (-not $fmtOk -or -not $data) { throw 'intro.wav: fmt or data chunk missing' }
if ($data.Length / 4 -gt 960000) { throw 'intro.wav is longer than 20 seconds, the boot sound limit' }

$btsnd = [byte[]]::new(8 + $data.Length) # header all zero: type 0, no loop
for ($i = 0; $i + 1 -lt $data.Length; $i += 2) { $btsnd[8 + $i] = $data[$i + 1]; $btsnd[9 + $i] = $data[$i] }

$outPath = Join-Path $meta 'bootSound.btsnd.gz'
$fs = [IO.File]::Create($outPath)
$gz = New-Object IO.Compression.GZipStream($fs, [IO.Compression.CompressionLevel]::Optimal)
$gz.Write($btsnd, 0, $btsnd.Length); $gz.Dispose(); $fs.Dispose()
Write-Host ("Wrote meta/bootSound.btsnd.gz ({0:N1} s)" -f ($data.Length / 4 / 48000))
