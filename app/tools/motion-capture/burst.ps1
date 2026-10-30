# burst.ps1 -Out <dir> -Frames 45 -Actions "3:tab 1;9:tab 3"
# Grabs the window (PrintWindow) as fast as possible up to -Fps, sending
# automation commands at the given frame indices. Writes PNGs plus
# timestamps.csv (unix ms of every frame and command) for timing analysis.
param([string]$Out, [int]$Frames = 45, [string]$Actions = "", [double]$Fps = 30, [double]$Scale = 0.5)
. "$PSScriptRoot\common.ps1"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$h = Get-GcWindow
$acts = @{}
foreach ($a in ($Actions -split ';' | Where-Object { $_ })) { $p = $a -split ':', 2; $acts[[int]$p[0]] = $p[1] }
$bmps = New-Object System.Collections.ArrayList; $times = New-Object System.Collections.ArrayList
[void]$times.Add("kind,index,unix_ms,detail")
$sw = [System.Diagnostics.Stopwatch]::StartNew(); $period = 1000.0 / $Fps
for ($i = 0; $i -lt $Frames; $i++) {
  while ($sw.Elapsed.TotalMilliseconds -lt $i * $period) { }
  if ($acts.ContainsKey($i)) { Send-Gc $acts[$i]; [void]$times.Add("command,$i,$(UnixMs),$($acts[$i])") }
  $t = UnixMs
  $bmp = Grab-Window $h
  [void]$bmps.Add($bmp); [void]$times.Add("frame,$i,$t,")
}
for ($i = 0; $i -lt $bmps.Count; $i++) {
  $b = $bmps[$i]
  if ($Scale -ne 1) { $s = New-Object System.Drawing.Bitmap $b, ([int]($b.Width * $Scale)), ([int]($b.Height * $Scale)); $b.Dispose(); $b = $s }
  $b.Save((Join-Path $Out ("f{0:D3}.png" -f $i)), [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
}
$times | Set-Content (Join-Path $Out "timestamps.csv")
if (Test-Path "$CmdFile.log") { Copy-Item "$CmdFile.log" (Join-Path $Out "app-commands.log") }
"captured $Frames frames in $([int]$sw.Elapsed.TotalMilliseconds) ms -> $Out"
