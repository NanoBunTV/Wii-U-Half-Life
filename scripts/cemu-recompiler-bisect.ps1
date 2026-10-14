<#
  Finds the code that Cemu's PPC recompiler mis-handles, by binary search.

  Cemu's hidden --ppcrec-lower-addr / --ppcrec-upper-addr options restrict the recompiler to an
  address range; everything outside it runs in the interpreter. A run "passes" when the engine
  reaches -Marker in Cemu's log, and "fails" when Cemu crashes first. The search narrows down the
  range that has to be recompiled for the crash to happen, then prints the functions in it.

  .\scripts\cemu-recompiler-bisect.ps1 [-Wuhb build\halflife.wuhb] [-Marker 'gamestartup'] [-TimeoutSec 25]
#>
param(
	[string]$Wuhb = (Join-Path $PSScriptRoot '..\build\halflife.wuhb'),
	[string]$Elf = (Join-Path $PSScriptRoot '..\build\halflife.elf'),
	[string]$Cemu = 'E:\ROMs and Emulators\_Emulators\Cemu (Wii U)\Cemu.exe',
	[string]$Marker = 'gamestartup',
	[int]$TimeoutSec = 25,
	[uint32]$MinSize = 0x40
)

$ErrorActionPreference = 'Stop'
$Wuhb = (Resolve-Path $Wuhb).Path
$Elf = (Resolve-Path $Elf).Path
$cemuLog = Join-Path $env:APPDATA 'Cemu\log.txt'
$bin = 'F:\Programs\devkitPro\devkitPPC\bin'

# .text range from the ELF
$sec = & "$bin\powerpc-eabi-readelf.exe" -SW $Elf | Select-String '\s\.text\s'
if ($sec.Line -notmatch '\.text\s+PROGBITS\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)') { throw 'no .text section' }
$textLo = [Convert]::ToUInt32($Matches[1], 16)
$textHi = $textLo + [Convert]::ToUInt32($Matches[2], 16)

# returns 'pass', 'crash' or 'timeout'
function Test-Range([uint32]$lo, [uint32]$hi) {
	$cemuArgs = @('--ppcrec-lower-addr', ('0x{0:x8}' -f $lo), '--ppcrec-upper-addr', ('0x{0:x8}' -f $hi), '-g', "`"$Wuhb`"")
	$p = Start-Process -FilePath $Cemu -ArgumentList $cemuArgs -PassThru
	$result = 'timeout'
	for ($i = 0; $i -lt $TimeoutSec * 2; $i++) {
		Start-Sleep -Milliseconds 500
		if ($p.HasExited) { $result = 'crash'; break }
		if ((Test-Path $cemuLog) -and (Select-String -Path $cemuLog -Pattern $Marker -SimpleMatch -Quiet)) { $result = 'pass'; break }
	}
	if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $p.WaitForExit() }
	Start-Sleep -Milliseconds 500
	Write-Host ('  0x{0:x8}-0x{1:x8} ({2,8} bytes): {3}' -f $lo, $hi, ($hi - $lo), $result)
	return $result
}

Write-Host ('.text 0x{0:x8}-0x{1:x8}' -f $textLo, $textHi)
if ((Test-Range $textLo $textHi) -ne 'crash') { Write-Host 'Full recompilation does not crash: nothing to bisect.'; return }

$lo = $textLo; $hi = $textHi
while (($hi - $lo) -gt $MinSize) {
	$mid = [uint32]((($lo + $hi) / 2) -band 0xFFFFFFFC)
	if ((Test-Range $lo $mid) -eq 'crash') { $hi = $mid; continue }
	if ((Test-Range $mid $hi) -eq 'crash') { $lo = $mid; continue }
	Write-Host 'Neither half crashes alone (the bug needs code from both halves). Stopping here.'
	break
}

Write-Host ('Smallest crashing range: 0x{0:x8}-0x{1:x8}' -f $lo, $hi)
$syms = & "$bin\powerpc-eabi-nm.exe" -n -C $Elf | ForEach-Object {
	if ($_ -match '^([0-9a-f]{8}) [tTwW] (.+)$') { [pscustomobject]@{ Addr = [Convert]::ToUInt32($Matches[1], 16); Name = $Matches[2] } }
}
$before = $syms | Where-Object { $_.Addr -le $lo } | Select-Object -Last 1
$inside = $syms | Where-Object { $_.Addr -gt $lo -and $_.Addr -lt $hi }
Write-Host 'Functions:'
foreach ($s in @($before) + @($inside)) { if ($s) { Write-Host ('  0x{0:x8} {1}' -f $s.Addr, $s.Name) } }
