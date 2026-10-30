# winmax.ps1 -State max|restore — maximise or restore the Gate Control window (only that window).
param([string]$State = "max")
. "$PSScriptRoot\common.ps1"
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class WMax { [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd); }
"@
$h = Get-GcWindow
[void][WMax]::ShowWindow($h, $(if ($State -eq "max") { 3 } else { 9 }))
"$State"
