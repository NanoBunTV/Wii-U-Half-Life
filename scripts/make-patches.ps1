<#
.SYNOPSIS
  Saves the Wii U changes made inside external/ as patch files in patches/.

.DESCRIPTION
  Engine and game changes are made directly in the external/ clones, then exported with this script
  so they can be committed (external/ itself is ignored by git). Run it after every change to
  external/. New files inside external/ are included (git add -N marks them for the diff).

  .\scripts\make-patches.ps1
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$ext = Join-Path $root 'external'
$patches = Join-Path $root 'patches'
New-Item -ItemType Directory -Force $patches | Out-Null

$repos = @(
	@{ Dir = 'xash3d-fwgs';                         Patch = 'xash3d-fwgs.patch' },
	@{ Dir = 'xash3d-fwgs/3rdparty/library_suffix'; Patch = 'library_suffix.patch' },
	@{ Dir = 'xash3d-fwgs/3rdparty/mainui';         Patch = 'mainui.patch' },
	@{ Dir = 'hlsdk-portable';                      Patch = 'hlsdk-portable.patch' },
	@{ Dir = 'hlsdk-portable/freevgui';             Patch = 'freevgui.patch' }
)

foreach ($r in $repos) {
	$dir = Join-Path $ext $r.Dir
	$out = Join-Path $patches $r.Patch
	# include untracked files, but not submodule changes (they get their own patch)
	& git -C $dir add -N . 2>$null
	$diff = & git -C $dir diff --ignore-submodules=all --binary
	if ($diff) {
		($diff -join "`n") + "`n" | Set-Content -NoNewline -Encoding utf8NoBOM $out
		$files = (& git -C $dir diff --ignore-submodules=all --name-only).Count
		Write-Host ("{0,-28} {1} file(s)" -f $r.Patch, $files)
	}
	elseif (Test-Path $out) {
		Remove-Item $out
		Write-Host ("{0,-28} no changes (removed)" -f $r.Patch)
	}
}
