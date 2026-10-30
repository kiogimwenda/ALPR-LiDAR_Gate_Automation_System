param([string]$Out = "$env:TEMP\gc-shot.png")
. "$PSScriptRoot\common.ps1"
$h = Get-GcWindow
Snap-Window $h $Out; "saved $Out"
