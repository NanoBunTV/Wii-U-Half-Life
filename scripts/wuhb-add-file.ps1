<#
.SYNOPSIS
  Adds (or replaces) one file inside a .wuhb, e.g. the boot sound in meta/.

.DESCRIPTION
  wuhbtool only puts the icon and splash images into the bundle's meta/ folder, but Aroma's Wii U Menu
  plugin (homebrew_on_menu) serves any meta/*.btsnd from the bundle too, so the boot sound has to be added
  afterwards. A .wuhb is a RomFS ("WUHB" magic); this reads its tree and writes it back with the new file,
  laid out exactly like wut-tools' wuhbtool (RomFSService.cpp, entities/*.cpp). A file "x.gz" is read as
  "x" by the bundle reader (WUHBUtilsModule), so the boot sound can stay compressed.

  .\scripts\wuhb-add-file.ps1 -Wuhb build\halflife.wuhb -Path meta/bootSound.btsnd.gz -Source meta\bootSound.btsnd.gz
  .\scripts\wuhb-add-file.ps1 -Wuhb build\halflife.wuhb -List
#>
param(
	[Parameter(Mandatory)][string]$Wuhb,
	[string]$Path,
	[string]$Source,
	[switch]$List
)
$ErrorActionPreference = 'Stop'

$EMPTY = [uint32]::MaxValue
$M32 = [uint64]4294967295 # 0xFFFFFFFF is -1 (int32) in PowerShell

function Get-U32([byte[]]$b, [long]$o) { return ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o + 1] -shl 16) -bor ([uint32]$b[$o + 2] -shl 8) -bor [uint32]$b[$o + 3] }
function Get-U64([byte[]]$b, [long]$o) { return ([uint64](Get-U32 $b $o) -shl 32) -bor [uint64](Get-U32 $b ($o + 4)) }
function Set-U32([byte[]]$b, [long]$o, [uint32]$v) { $b[$o] = [byte](($v -shr 24) -band 0xFF); $b[$o + 1] = [byte](($v -shr 16) -band 0xFF); $b[$o + 2] = [byte](($v -shr 8) -band 0xFF); $b[$o + 3] = [byte]($v -band 0xFF) }
function Set-U64([byte[]]$b, [long]$o, [uint64]$v) { Set-U32 $b $o ([uint32]($v -shr 32)); Set-U32 $b ($o + 4) ([uint32]($v -band $M32)) }
function Align([long]$v, [long]$a) { return [long](($v + $a - 1) -band -bnot ($a - 1)) }

function Get-HashCount([int]$n) {
	if ($n -lt 3) { return 3 }
	if ($n -lt 19) { return $n -bor 1 }
	$c = $n
	while ($c % 2 -eq 0 -or $c % 3 -eq 0 -or $c % 5 -eq 0 -or $c % 7 -eq 0 -or $c % 11 -eq 0 -or $c % 13 -eq 0 -or $c % 17 -eq 0) { $c++ }
	return $c
}

function Get-PathHash([uint32]$parent, [string]$name) {
	[uint64]$h = $parent -bxor 123456789
	foreach ($ch in [Text.Encoding]::UTF8.GetBytes($name.ToUpperInvariant())) {
		$h = (($h -shr 5) -bor ($h -shl 27)) -band $M32
		$h = $h -bxor $ch
	}
	return [uint32]$h
}

# ---- read ----
$bytes = [IO.File]::ReadAllBytes((Resolve-Path $Wuhb))
if ([Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'WUHB') { throw "$Wuhb is not a WUHB file" }
$dirTableOfs = Get-U64 $bytes 0x18
$fileTableOfs = Get-U64 $bytes 0x38
$partOfs = Get-U64 $bytes 0x48

function Read-Dir([uint32]$off, [string]$name) {
	$e = $dirTableOfs + $off
	$node = [pscustomobject]@{ Name = $name; Dirs = [Collections.Generic.List[object]]::new(); Files = [Collections.Generic.List[object]]::new(); Off = 0 }
	$child = Get-U32 $bytes ($e + 8)
	while ($child -ne $EMPTY) {
		$ce = $dirTableOfs + $child
		$len = Get-U32 $bytes ($ce + 20)
		$node.Dirs.Add((Read-Dir $child ([Text.Encoding]::UTF8.GetString($bytes, $ce + 24, $len))))
		$child = Get-U32 $bytes ($ce + 4)
	}
	$file = Get-U32 $bytes ($e + 12)
	while ($file -ne $EMPTY) {
		$fe = $fileTableOfs + $file
		$dataOfs = Get-U64 $bytes ($fe + 8); $size = Get-U64 $bytes ($fe + 16)
		$len = Get-U32 $bytes ($fe + 28)
		$data = [byte[]]::new($size); [Array]::Copy($bytes, [long]($partOfs + $dataOfs), $data, 0, [long]$size)
		$node.Files.Add([pscustomobject]@{ Name = [Text.Encoding]::UTF8.GetString($bytes, $fe + 32, $len); Data = $data; Off = 0; DataOff = 0 })
		$file = Get-U32 $bytes ($fe + 4)
	}
	return $node
}
$root = Read-Dir 0 ''

if ($List) {
	function Show([object]$d, [string]$prefix) {
		foreach ($s in $d.Dirs) { Show $s "$prefix$($s.Name)/" }
		foreach ($f in $d.Files) { '{0,10}  {1}{2}' -f $f.Data.Length, $prefix, $f.Name }
	}
	Show $root '/'
	return
}
if (-not $Path -or -not $Source) { throw 'Give -Path and -Source (or -List)' }

# ---- add / replace ----
$parts = $Path.Trim('/').Split('/')
$dir = $root
for ($i = 0; $i -lt $parts.Length - 1; $i++) {
	$next = $dir.Dirs | Where-Object { $_.Name -eq $parts[$i] } | Select-Object -First 1
	if (-not $next) {
		$next = [pscustomobject]@{ Name = $parts[$i]; Dirs = [Collections.Generic.List[object]]::new(); Files = [Collections.Generic.List[object]]::new(); Off = 0 }
		$dir.Dirs.Add($next)
	}
	$dir = $next
}
$fileName = $parts[-1]
$newData = [IO.File]::ReadAllBytes((Resolve-Path $Source))
$existing = $dir.Files | Where-Object { $_.Name -eq $fileName } | Select-Object -First 1
if ($existing) { $existing.Data = $newData }
else { $dir.Files.Add([pscustomobject]@{ Name = $fileName; Data = $newData; Off = 0; DataOff = 0 }) }

# ---- lay out (wuhbtool's order: directories pre-order, files after their subdirectories) ----
$script:dirOff = 0x18; $script:fileOff = 0; $script:part = 0
# wuhbtool counts the root twice (RomFSService.cpp starts at 1 and the root adds itself again), which
# gives one more hash bucket and an unused 0x18 bytes at the end of the dir table: copied for identical output
$script:numDirs = 2; $script:numFiles = 0
function Set-DirOffsets([object]$d) {
	foreach ($s in $d.Dirs) {
		$s.Off = $script:dirOff
		$script:dirOff += 0x18 + (Align ([Text.Encoding]::UTF8.GetByteCount($s.Name)) 4)
		$script:numDirs++
		Set-DirOffsets $s
	}
}
function Set-FileOffsets([object]$d) {
	foreach ($s in $d.Dirs) { Set-FileOffsets $s }
	foreach ($f in $d.Files) {
		$script:part = Align $script:part 0x10
		$f.DataOff = $script:part
		$script:part += $f.Data.Length
		$f.Off = $script:fileOff
		$script:fileOff += 0x20 + (Align ([Text.Encoding]::UTF8.GetByteCount($f.Name)) 4)
		$script:numFiles++
	}
}
$root.Off = 0
Set-DirOffsets $root
Set-FileOffsets $root

$dirHashCount = Get-HashCount $script:numDirs
$fileHashCount = Get-HashCount $script:numFiles
$dirHash = [uint32[]]::new($dirHashCount); for ($i = 0; $i -lt $dirHashCount; $i++) { $dirHash[$i] = $EMPTY }
$fileHash = [uint32[]]::new($fileHashCount); for ($i = 0; $i -lt $fileHashCount; $i++) { $fileHash[$i] = $EMPTY }
$dirTable = [byte[]]::new($script:dirOff + 0x18)
$fileTable = [byte[]]::new($script:fileOff)

function Write-DirEntry([object]$d, [uint32]$parentOff, [string]$name) {
	$o = $d.Off
	Set-U32 $dirTable $o $parentOff
	Set-U32 $dirTable ($o + 8) $(if ($d.Dirs.Count) { $d.Dirs[0].Off } else { $EMPTY })
	Set-U32 $dirTable ($o + 12) $(if ($d.Files.Count) { $d.Files[0].Off } else { $EMPTY })
	$h = Get-PathHash $parentOff $name
	Set-U32 $dirTable ($o + 16) $dirHash[$h % $dirHashCount]
	$dirHash[$h % $dirHashCount] = $o
	$nb = [Text.Encoding]::UTF8.GetBytes($name)
	Set-U32 $dirTable ($o + 20) $nb.Length
	[Array]::Copy($nb, 0, $dirTable, $o + 24, $nb.Length)
}
function Write-Entries([object]$d) {
	for ($i = 0; $i -lt $d.Dirs.Count; $i++) {
		$s = $d.Dirs[$i]
		Write-DirEntry $s $d.Off $s.Name
		Set-U32 $dirTable ($s.Off + 4) $(if ($i + 1 -lt $d.Dirs.Count) { $d.Dirs[$i + 1].Off } else { $EMPTY })
		Write-Entries $s
	}
	for ($i = 0; $i -lt $d.Files.Count; $i++) {
		$f = $d.Files[$i]; $o = $f.Off
		Set-U32 $fileTable $o $d.Off
		Set-U32 $fileTable ($o + 4) $(if ($i + 1 -lt $d.Files.Count) { $d.Files[$i + 1].Off } else { $EMPTY })
		Set-U64 $fileTable ($o + 8) $f.DataOff
		Set-U64 $fileTable ($o + 16) $f.Data.Length
		$h = Get-PathHash $d.Off $f.Name
		Set-U32 $fileTable ($o + 24) $fileHash[$h % $fileHashCount]
		$fileHash[$h % $fileHashCount] = $o
		$nb = [Text.Encoding]::UTF8.GetBytes($f.Name)
		Set-U32 $fileTable ($o + 28) $nb.Length
		[Array]::Copy($nb, 0, $fileTable, $o + 32, $nb.Length)
	}
}
Write-DirEntry $root 0 ''
Set-U32 $dirTable 4 $EMPTY
Write-Entries $root

# ---- write ----
$PartitionOfs = 0x200
$dirHashOfs = Align ($script:part + $PartitionOfs) 4
$dirTableOfsNew = $dirHashOfs + 4 * $dirHashCount
$fileHashOfs = $dirTableOfsNew + $dirTable.Length
$fileTableOfsNew = $fileHashOfs + 4 * $fileHashCount
$out = [byte[]]::new($fileTableOfsNew + $fileTable.Length)

[Array]::Copy([Text.Encoding]::ASCII.GetBytes('WUHB'), 0, $out, 0, 4)
Set-U32 $out 4 0x50
Set-U64 $out 0x08 $dirHashOfs; Set-U64 $out 0x10 (4 * $dirHashCount)
Set-U64 $out 0x18 $dirTableOfsNew; Set-U64 $out 0x20 $dirTable.Length
Set-U64 $out 0x28 $fileHashOfs; Set-U64 $out 0x30 (4 * $fileHashCount)
Set-U64 $out 0x38 $fileTableOfsNew; Set-U64 $out 0x40 $fileTable.Length
Set-U64 $out 0x48 $PartitionOfs

function Write-Data([object]$d) {
	foreach ($s in $d.Dirs) { Write-Data $s }
	foreach ($f in $d.Files) { [Array]::Copy($f.Data, 0, $out, [long]($PartitionOfs + $f.DataOff), $f.Data.Length) }
}
Write-Data $root
for ($i = 0; $i -lt $dirHashCount; $i++) { Set-U32 $out ($dirHashOfs + 4 * $i) $dirHash[$i] }
[Array]::Copy($dirTable, 0, $out, $dirTableOfsNew, $dirTable.Length)
for ($i = 0; $i -lt $fileHashCount; $i++) { Set-U32 $out ($fileHashOfs + 4 * $i) $fileHash[$i] }
[Array]::Copy($fileTable, 0, $out, $fileTableOfsNew, $fileTable.Length)

[IO.File]::WriteAllBytes((Resolve-Path $Wuhb), $out)
Write-Host "$Wuhb`: $(if ($existing) { 'replaced' } else { 'added' }) $Path ($($newData.Length) bytes)"
