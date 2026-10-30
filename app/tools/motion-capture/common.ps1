Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  public struct RECT { public int L, T, R, B; }
  public static void Click(int x, int y) { SetCursorPos(x, y); mouse_event(2,0,0,0,UIntPtr.Zero); System.Threading.Thread.Sleep(30); mouse_event(4,0,0,0,UIntPtr.Zero); }
}
"@
[W32]::SetProcessDPIAware() | Out-Null
function Get-GcWindow {
  $p = Get-Process GateControl -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if (-not $p) { throw "GateControl not running" }
  return $p.MainWindowHandle
}
function Get-GcRect($h) { $r = New-Object W32+RECT; [W32]::GetWindowRect($h, [ref]$r) | Out-Null; return $r }
function Find-ByName($h, [string]$name) {
  $root = [System.Windows.Automation.AutomationElement]::FromHandle($h)
  $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $name)
  return $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
}
function Click-Name($h, [string]$name) {
  $e = Find-ByName $h $name
  if (-not $e) { throw "element '$name' not found" }
  $b = $e.Current.BoundingRectangle
  [W32]::Click([int]($b.X + $b.Width / 2), [int]($b.Y + $b.Height / 2))
}
# PrintWindow with PW_RENDERFULLCONTENT: renders the window even when other
# windows cover it, so captures never include (or disturb) anything else.
function Grab-Window($h) {
  $r = Get-GcRect $h
  $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
  [W32]::PrintWindow($h, $hdc, 2) | Out-Null
  $g.ReleaseHdc($hdc); $g.Dispose(); return $bmp
}
function Snap-Window($h, [string]$path) {
  $bmp = Grab-Window $h; $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
$CmdFile = Join-Path $env:TEMP "gc-cmd.txt"
function Send-Gc([string]$cmd) { [System.IO.File]::AppendAllText($CmdFile, "$cmd`n") }
function UnixMs { [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }
