# gpuload.ps1 — read-only: 3D-engine utilisation per process over a few seconds (top users).
param([int]$Seconds = 4)
$samples = Get-Counter '\GPU Engine(*engtype_3D)\Utilization Percentage' -SampleInterval 1 -MaxSamples $Seconds -ErrorAction SilentlyContinue
$by = @{}
foreach ($s in $samples) { foreach ($c in $s.CounterSamples) { if ($c.InstanceName -match 'pid_(\d+)_') { $p = [int]$Matches[1]; $by[$p] = [Math]::Max([double]($by[$p]), $c.CookedValue) } } }
$by.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 6 | ForEach-Object {
  $n = (Get-Process -Id $_.Key -ErrorAction SilentlyContinue).ProcessName; "{0,-28} pid {1,-7} peak 3D {2:N0}%" -f $n, $_.Key, $_.Value }
