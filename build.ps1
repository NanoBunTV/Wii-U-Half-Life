<#
.SYNOPSIS
  Builds Half-Life for Wii U (halflife.rpx + halflife.wuhb) with devkitPro's Wii U toolchain.

.DESCRIPTION
  GNU make can't build from a path containing spaces, so the project folder is temporarily mapped to a
  free drive letter with `subst`, CMake runs there, and the mapping is removed afterwards.

  .\build.ps1            configure (first time) and build
  .\build.ps1 clean      delete the build folder
  .\build.ps1 -Jobs 4    limit parallel jobs (default: number of CPU cores)
  .\build.ps1 -DebugBuild  Debug build (-O0, separate build-debug folder)
#>
param(
	[Parameter(Position = 0)][ValidateSet('build', 'clean', 'reconfigure')][string]$Action = 'build',
	[int]$Jobs = [Environment]::ProcessorCount,
	[switch]$DebugBuild
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# devkitPro location: DEVKITPRO env var is in MSYS form (/opt/devkitpro), so find the Windows path
$dkp = $null
foreach ($candidate in @($env:DEVKITPRO_WIN, 'F:\Programs\devkitPro', 'C:\devkitPro')) {
	if ($candidate -and (Test-Path (Join-Path $candidate 'msys2\usr\bin\bash.exe'))) { $dkp = $candidate; break }
}
if (-not $dkp) {
	$make = Get-Command make.exe -ErrorAction SilentlyContinue
	if ($make) { $dkp = (Resolve-Path (Join-Path (Split-Path $make.Source) '..\..\..')).Path }
}
if (-not $dkp) { throw 'devkitPro not found. Set DEVKITPRO_WIN to its folder (e.g. F:\Programs\devkitPro).' }
$bash = Join-Path $dkp 'msys2\usr\bin\bash.exe'

$buildDir = if ($DebugBuild) { 'build-debug' } else { 'build' }
$buildType = if ($DebugBuild) { 'Debug' } else { 'RelWithDebInfo' }

if ($Action -eq 'clean') {
	Remove-Item -Recurse -Force (Join-Path $root $buildDir) -ErrorAction SilentlyContinue
	Write-Host "Removed $buildDir"
	return
}

# map the project folder to a free drive letter
$used = (Get-PSDrive -PSProvider FileSystem).Name
$letter = @('W', 'X', 'Y', 'Z', 'V', 'U', 'T', 'S', 'R', 'Q') | Where-Object { $used -notcontains $_ } | Select-Object -First 1
if (-not $letter) { throw 'No free drive letter for subst.' }

subst "${letter}:" "$root" | Out-Null
try {
	$msysRoot = "/$($letter.ToLower())"
	$cfg = ''
	if ($Action -eq 'reconfigure' -or -not (Test-Path (Join-Path $root "$buildDir\CMakeCache.txt"))) {
		$cfg = "cmake -S $msysRoot -B $msysRoot/$buildDir -G 'Unix Makefiles' -DCMAKE_TOOLCHAIN_FILE=`$DEVKITPRO/cmake/WiiU.cmake -DCMAKE_BUILD_TYPE=$buildType && "
	}
	$cmd = "source /etc/profile.d/devkit-env.sh 2>/dev/null; export PATH=`$DEVKITPPC/bin:`$DEVKITPRO/tools/bin:`$PATH; $cfg cmake --build $msysRoot/$buildDir -j $Jobs"
	& $bash -lc $cmd
	if ($LASTEXITCODE -ne 0) { throw "Build failed (exit code $LASTEXITCODE)" }

	# boot sound: wuhbtool can't add it, so it goes into each bundle's meta/ folder afterwards (replaced if a
	# bundle wasn't rebuilt and already has it)
	$bootSound = Join-Path $root 'meta\bootSound.btsnd.gz'
	foreach ($bundle in 'halflife', 'bshift', 'opfor') {
		$wuhb = Join-Path $root "$buildDir\$bundle.wuhb"
		if (Test-Path $wuhb) {
			& (Join-Path $root 'scripts\wuhb-add-file.ps1') -Wuhb $wuhb -Path 'meta/bootSound.btsnd.gz' -Source $bootSound | Out-Null
		}
	}

	Write-Host ''
	Write-Host "Built: $root\$buildDir\halflife.wuhb, bshift.wuhb (Blue Shift), opfor.wuhb (Opposing Force)" -ForegroundColor Green
}
finally {
	subst "${letter}:" /D | Out-Null
}
