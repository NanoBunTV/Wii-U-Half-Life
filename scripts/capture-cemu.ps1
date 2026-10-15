<#
  Saves an image of every Cemu window (main window, dialogs, GamePad view) to a folder.
  Only Cemu's own windows are captured (PrintWindow), not the rest of the desktop.
  .\scripts\capture-cemu.ps1 -OutDir <folder>
#>
param(
	[string]$OutDir = (Join-Path $PSScriptRoot '..\build\cemu-capture'),
	[string]$Cemu = $null # only this Cemu.exe (when another project's Cemu runs too)
)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class CemuWin {
	public delegate bool EnumProc(IntPtr h, IntPtr l);
	[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
	[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
	[DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
	public static List<IntPtr> ForPid(uint pid) {
		var list = new List<IntPtr>();
		EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) list.Add(h); return true; }, IntPtr.Zero);
		return list;
	}
}
'@

# Without this, Windows display scaling makes GetWindowRect return scaled-down sizes and the image
# only contains the top-left part of the window.
[void][CemuWin]::SetProcessDPIAware()

New-Item -ItemType Directory -Force $OutDir | Out-Null
$n = 0
foreach ($proc in Get-Process Cemu -ErrorAction SilentlyContinue) {
	if ($Cemu -and $proc.Path -ne (Resolve-Path $Cemu).Path) { continue }
	foreach ($h in [CemuWin]::ForPid([uint32]$proc.Id)) {
		$r = New-Object CemuWin+RECT
		[void][CemuWin]::GetWindowRect($h, [ref]$r)
		$w = $r.R - $r.L; $ht = $r.B - $r.T
		if ($w -le 0 -or $ht -le 0) { continue }
		$sb = New-Object System.Text.StringBuilder 256
		[void][CemuWin]::GetWindowText($h, $sb, 256)
		$bmp = New-Object System.Drawing.Bitmap $w, $ht
		$g = [System.Drawing.Graphics]::FromImage($bmp)
		$hdc = $g.GetHdc()
		[void][CemuWin]::PrintWindow($h, $hdc, 2) # PW_RENDERFULLCONTENT
		$g.ReleaseHdc($hdc); $g.Dispose()
		$file = Join-Path $OutDir ("cemu_{0}.png" -f $n++)
		$bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
		Write-Host "$file  ($w x $ht)  '$($sb.ToString())'"
	}
}
if ($n -eq 0) { Write-Host 'No Cemu windows found.' }
