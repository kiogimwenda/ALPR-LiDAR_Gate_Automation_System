param([string]$Cmd)
. "$PSScriptRoot\common.ps1"
Send-Gc $Cmd; "sent $Cmd"
