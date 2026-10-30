# ui.ps1 -Dump | -Invoke <name> [-Index n] | -Set <name> -Value <text> | -Select <combo name> -Value <item>
# UI Automation only (no mouse): Invoke a button, set a field, pick a combo item, dump the tree.
param([switch]$Dump, [string]$Invoke, [int]$Index = 0, [string]$Set, [string]$Select, [string]$Value)
. "$PSScriptRoot\common.ps1"
$h = Get-GcWindow
$root = [System.Windows.Automation.AutomationElement]::FromHandle($h)
function All-ByName([string]$n) {
  $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $n)
  return $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)
}
if ($Dump) {
  $all = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
  foreach ($e in $all) { $c = $e.Current; if ($c.Name -or $c.AutomationId) { "{0} | {1} | {2} | {3}" -f $c.ControlType.ProgrammaticName, $c.Name, $c.AutomationId, $c.BoundingRectangle } }
}
if ($Invoke) {
  $els = All-ByName $Invoke
  # list items (action sheets) are chosen by selecting them
  $sel = @($els | Where-Object { $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::ListItem -and ($_.GetSupportedPatterns() -contains [System.Windows.Automation.SelectionItemPattern]::Pattern) })
  if ($sel.Count -gt 0) { $sel[0].GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); "selected $Invoke"; exit 0 }
  $btn = @($els | Where-Object { $_.GetSupportedPatterns() -contains [System.Windows.Automation.InvokePattern]::Pattern })
  if ($btn.Count -le $Index) { "no invokable '$Invoke'"; exit 1 }
  $btn[$Index].GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); "invoked $Invoke"
}
if ($Set) {
  $e = (All-ByName $Set) | Select-Object -First 1
  $e.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern).SetValue($Value); "set $Set"
}
if ($Select) {
  $combo = (All-ByName $Select) | Select-Object -First 1
  if ($Select -eq "*combo") {
    $cc = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ControlTypeProperty, [System.Windows.Automation.ControlType]::ComboBox)
    $combo = @($root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cc) | Where-Object { -not $_.Current.IsOffscreen }) | Select-Object -First 1
  }
  $ec = $combo.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern); $ec.Expand(); Start-Sleep -Milliseconds 400
  $item = (All-ByName $Value) | Where-Object { $_.GetSupportedPatterns() -contains [System.Windows.Automation.SelectionItemPattern]::Pattern } | Select-Object -First 1
  $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select(); try { $ec.Collapse() } catch {}; "selected $Value"
}
