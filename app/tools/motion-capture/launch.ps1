param([int]$Width = 1440, [int]$Height = 900, [string]$Config = "Debug")
. "$PSScriptRoot\common.ps1"
Get-Process GateControl -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
$env:GC_AUTOMATION_FILE = $CmdFile
Remove-Item $CmdFile, "$CmdFile.log" -ErrorAction SilentlyContinue
Start-Process "C:\Users\User\AppData\Local\GateControl\build\bin\GateControl.App\$Config\net10.0-windows10.0.19041.0\win-x64\GateControl.exe"
for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 500; try { $h = Get-GcWindow; break } catch {} }
[W32]::ShowWindow($h, 9) | Out-Null
[W32]::MoveWindow($h, 40, 20, $Width, $Height, $true) | Out-Null
"launched $h"
