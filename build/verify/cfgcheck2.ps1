$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\software\ltool\ltool.exe'
$log = New-Object System.Collections.Generic.List[string]

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 800
$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', '6') -PassThru
Start-Sleep -Seconds 5
$c = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $p.Id)
$dlg = $null
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c)) {
  if ($w.Current.ClassName -eq 'SettingsDialog') { $dlg = $w; break }
}
if ($null -eq $dlg) { Write-Output 'no SettingsDialog'; exit 1 }

$cc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::CheckBox)
foreach ($e in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cc)) {
  if ($e.Current.IsOffscreen) { continue }
  $tp = $null
  $state = '?'
  if ($e.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$tp)) { $state = $tp.Current.ToggleState }
  $log.Add(('复选框 [' + $e.Current.Name + '] 勾选=' + $state))
}
$tc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Text)
foreach ($e in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $tc)) {
  if ($e.Current.IsOffscreen) { continue }
  $n = $e.Current.Name
  if ($n -like '*右键*' -or $n -like '*进度*' -or $n -like '*当前：*') { $log.Add(('文本 [' + $n + ']')) }
}
# 只关窗口，绝不点保存（用 WindowPattern.Close，等价于点 X）
$wp = $null
if ($dlg.TryGetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern, [ref]$wp)) {
  $wp.Close()
  $log.Add('已用 WindowPattern.Close 关掉设置页（没点保存）')
} else {
  $log.Add('拿不到 WindowPattern')
}
Start-Sleep -Milliseconds 800
if (-not $p.WaitForExit(1500)) { Stop-Process -Id $p.Id -Force; $log.Add('没关掉，强杀了自己起的进程') }
Start-Sleep -Milliseconds 500
Start-Process -FilePath $exe | Out-Null
$log.Add('主实例已重新拉起')
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\cfgcheck2.txt', $log, (New-Object System.Text.UTF8Encoding($true)))
Add-Type -AssemblyName System.Windows.Forms
Write-Output 'written'
