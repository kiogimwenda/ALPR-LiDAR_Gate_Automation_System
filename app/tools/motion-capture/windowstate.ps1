# windowstate.ps1 — is the app window foreground / covered? (read-only; changes nothing)
. "$PSScriptRoot\common.ps1"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public static class W33 {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
}
"@
$h = Get-GcWindow; $fg = [W33]::GetForegroundWindow(); $sb = New-Object System.Text.StringBuilder 256; [W33]::GetWindowText($fg, $sb, 256) | Out-Null
"app foreground: $($fg -eq $h); foreground window: '$($sb.ToString())'; minimised: $([W33]::IsIconic($h))"
