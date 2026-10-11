$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }

$proc = Get-Process ltool -ErrorAction Stop
Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' | Out-Null
Start-Sleep -Seconds 4
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
$wins = @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ })
Say ('windows of pid ' + $proc.Id + ' = ' + $wins.Count)
foreach ($w in $wins) {
  $r = $w.Current.BoundingRectangle
  Say ('WINDOW "' + $w.Current.Name + '" ' + [int]$r.X + ',' + [int]$r.Y + ' ' + [int]$r.Width + 'x' + [int]$r.Height)
  $all = $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::IsControlElementProperty, $true)))
  Say ('  controls = ' + $all.Count)
  $i = 0
  foreach ($e in $all) {
    if ($i -gt 120) { Say '  ...truncated'; break }
    $er = $e.Current.BoundingRectangle
    if ($er.Width -le 0 -or $er.Height -le 0) { $i++; continue }
    if ($e.Current.ControlType -eq $CT::Button -or $e.Current.ControlType -eq $CT::TabItem -or $e.Current.ControlType -eq $CT::CheckBox -or $e.Current.ControlType -eq $CT::Edit -or $e.Current.ControlType -eq $CT::ListItem -or $e.Current.ControlType -eq $CT::Text) {
      Say ('   [' + $e.Current.ControlType.ProgrammaticName.Replace('ControlType.','') + '] "' + $e.Current.Name + '" @ ' + [int]$er.X + ',' + [int]$er.Y + ' ' + [int]$er.Width + 'x' + [int]$er.Height)
    }
    $i++
  }
}
# also dump tray-area buttons so we can locate ltool's icon.
# NOTE: no Chinese literals in this file -- PS 5.1 reads BOM-less UTF-8 as ANSI.
Say '--- tray (y>1300, x>1700) ---'
$allBtns = $root.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Button)))
foreach ($e in $allBtns) {
  $er = $e.Current.BoundingRectangle
  if ($er.Width -le 0 -or $er.Height -le 0) { continue }
  if ($er.Y -gt 1300 -and $er.X -gt 1700) {
    Say ('   btn "' + $e.Current.Name + '" @ ' + [int]$er.X + ',' + [int]$er.Y + ' ' + [int]$er.Width + 'x' + [int]$er.Height)
  }
}
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\settings_dump.txt', $out, (New-Object System.Text.UTF8Encoding($true)))
Write-Host done
