<#
.SYNOPSIS
  Starts the current build in Cemu and shows the engine's log lines as they arrive.

.DESCRIPTION
  .\scripts\run-cemu.ps1                    run build\halflife.wuhb with Cemu's default (recompiler) CPU mode
  .\scripts\run-cemu.ps1 -Interpreter       single-core interpreter (slow, but exact; see Docs/DEBUGGING.md)
  .\scripts\run-cemu.ps1 -EngineArgs '-dev 2'   write sd:/halflife/cmdline.txt first (engine arguments)
  .\scripts\run-cemu.ps1 -Seconds 60        close Cemu after 60 s (default: leave it running)

  Engine output appears in Cemu's log as "[OSConsole]" lines; that needs Cemu's "Coreinit Logging"
  option (Debug > Logging), which is logflag 131072 in %APPDATA%\Cemu\settings.xml.
#>
param(
	[string]$Wuhb = (Join-Path $PSScriptRoot '..\build\halflife.wuhb'),
	[string]$Cemu = 'E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\Cemu.exe',
	[string]$SdCard = 'E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\sdcard',
	[string]$EngineArgs = $null,
	[switch]$Interpreter,
	[int]$Seconds = 0
)

$ErrorActionPreference = 'Stop'
$Wuhb = (Resolve-Path $Wuhb).Path
$log = Join-Path $env:APPDATA 'Cemu\log.txt'

if ($PSBoundParameters.ContainsKey('EngineArgs')) {
	$cmdline = Join-Path $SdCard 'halflife\cmdline.txt'
	Set-Content -LiteralPath $cmdline -Value $EngineArgs -NoNewline
	Write-Host "cmdline.txt: '$EngineArgs'"
}

$cemuArgs = @()
if ($Interpreter) { $cemuArgs += '--force-interpreter' }
$cemuArgs += @('-g', "`"$Wuhb`"")

$start = Get-Date
$p = Start-Process -FilePath $Cemu -ArgumentList $cemuArgs -PassThru
Start-Sleep -Seconds 3 # Cemu recreates log.txt at startup

$shown = 0
while (-not $p.HasExited) {
	if ($Seconds -gt 0 -and ((Get-Date) - $start).TotalSeconds -ge $Seconds) { Stop-Process -Id $p.Id; break }
	if (Test-Path $log) {
		$lines = @(Get-Content -LiteralPath $log | Where-Object { $_ -match '\[OSConsole\]' })
		for ($i = $shown; $i -lt $lines.Count; $i++) { Write-Host $lines[$i] }
		$shown = $lines.Count
	}
	Start-Sleep -Milliseconds 500
}

if ($p.HasExited -and $p.ExitCode -ne 0 -and $p.ExitCode -ne 1) {
	Write-Host ("Cemu exited with code 0x{0:X8} (0xC0000005 = the emulated program hit unmapped memory, see Docs/DEBUGGING.md)" -f $p.ExitCode) -ForegroundColor Yellow
}
