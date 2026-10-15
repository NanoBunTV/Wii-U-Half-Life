<#
.SYNOPSIS
  Downloads Xash3D FWGS and hlsdk-portable (master for Half-Life, the bshift and opfor branches for
  Blue Shift and Opposing Force) into external/ at the pinned commits and applies the
  Wii U patches from patches/.

.DESCRIPTION
  external/ isn't part of this repository: it holds upstream source plus our patches. The commits
  below are the ones the port was last built and tested with; bump them deliberately (see
  Docs/BUILDING.md, "Updating the engine").

  .\scripts\fetch-deps.ps1          clone (if missing), check out the pinned commits, apply patches
  .\scripts\fetch-deps.ps1 -Force   also throw away local changes in external/ first
#>
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$ext = Join-Path $root 'external'
$patches = Join-Path $root 'patches'

# repo folder (relative to external/), URL, pinned commit, patch file
$repos = @(
	@{ Dir = 'xash3d-fwgs';                         Url = 'https://github.com/FWGS/xash3d-fwgs.git';    Commit = 'e0ece3d95241fc71af89f93336c2bab2273976d3'; Patch = 'xash3d-fwgs.patch' },
	@{ Dir = 'xash3d-fwgs/3rdparty/library_suffix'; Url = $null;                                        Commit = $null;                                       Patch = 'library_suffix.patch' },
	@{ Dir = 'xash3d-fwgs/3rdparty/mainui';         Url = $null;                                        Commit = $null;                                       Patch = 'mainui.patch' },
	@{ Dir = 'hlsdk-portable';                      Url = 'https://github.com/FWGS/hlsdk-portable.git'; Commit = '6c168fc79d95c8ffb934fa37aaafbbd424e03c1a'; Patch = 'hlsdk-portable.patch' },
	@{ Dir = 'hlsdk-portable/freevgui';             Url = $null;                                        Commit = $null;                                       Patch = 'freevgui.patch' },
	@{ Dir = 'hlsdk-bshift';                        Url = 'https://github.com/FWGS/hlsdk-portable.git'; Commit = '701bcef1a0ebac49e93ecff9e2c7724ff0b4278d'; Patch = 'hlsdk-bshift.patch' },
	@{ Dir = 'hlsdk-opfor';                         Url = 'https://github.com/FWGS/hlsdk-portable.git'; Commit = '109b4821281c0e62e20fa23b96589fca2ec78ca0'; Patch = 'hlsdk-opfor.patch' }
)

function Invoke-Git { & git.exe @args; if ($LASTEXITCODE -ne 0) { throw "git $args failed" } }

New-Item -ItemType Directory -Force $ext | Out-Null

foreach ($r in $repos | Where-Object { $_.Url }) {
	$dir = Join-Path $ext $r.Dir
	if (-not (Test-Path (Join-Path $dir '.git'))) {
		Write-Host "Cloning $($r.Url)"
		Invoke-Git clone $r.Url $dir
	}
	if ($Force) {
		Invoke-Git -C $dir reset --hard
		Invoke-Git -C $dir submodule foreach --recursive 'git reset --hard'
	}
	Invoke-Git -C $dir fetch origin $r.Commit
	Invoke-Git -C $dir checkout --detach $r.Commit
	Invoke-Git -C $dir submodule update --init --recursive
}

foreach ($r in $repos) {
	$dir = Join-Path $ext $r.Dir
	$patch = Join-Path $patches $r.Patch
	if (-not (Test-Path $patch) -or (Get-Item $patch).Length -eq 0) { continue }

	# already applied? (reverse-applies cleanly)
	& git -C $dir apply --reverse --check $patch 2>$null
	if ($LASTEXITCODE -eq 0) { Write-Host "Already applied: $($r.Patch)"; continue }

	Write-Host "Applying $($r.Patch)"
	Invoke-Git -C $dir apply --whitespace=nowarn $patch
}

Write-Host 'Done. Build with .\build.ps1' -ForegroundColor Green
