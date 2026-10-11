$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$exe = 'D:\software\ltool\ltool.exe'
$chkWord = [string][char]0x53F3 + [char]0x952E
$saveWord = [string][char]0x4FDD + [char]0x5B58

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
& cmd.exe /c 'reg delete "HKCU\Software\Classes\*\shell\ltool" /f >nul 2>&1'
& cmd.exe /c 'reg delete "HKCU\Software\Classes\Directory\shell\ltool" /f >nul 2>&1'
Write-Output ('清空后行数 = ' + @(& cmd.exe /c 'reg query "HKCU\Software\Classes\*\shell\ltool" /s 2>nul' | Where-Object { $_ -like 'HKEY_*' }).Count)

$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', '6') -PassThru
Start-Sleep -Seconds 5
$cond = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$dlg = $null
foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    [System.Windows.Automation.TreeScope]::Children, $cond)) {
  if ($w.Current.ClassName -eq 'SettingsDialog') { $dlg = $w; break }
}
if (-not $dlg) { Write-Output 'no SettingsDialog'; exit 1 }
$cc = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
  [System.Windows.Automation.ControlType]::CheckBox)
foreach ($c in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cc)) {
  if ($c.Current.Name -like ('*' + $chkWord + '*')) {
    $tp = $null
    if ($c.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$tp)) { $tp.Toggle(); break }
  }
}
$bc = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
  [System.Windows.Automation.ControlType]::Button)
foreach ($b in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
  if ($b.Current.Name -eq $saveWord) {
    $ip = $null
    if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke() }
    break
  }
}
Start-Sleep -Seconds 2
Write-Output '注册表里的命令（应指向 D:\software\ltool\ltool.exe）：'
& cmd.exe /c 'reg query "HKCU\Software\Classes\*\shell\ltool\whohas\command" /ve'  | ForEach-Object { Write-Output ('  ' + $_.Trim()) }
& cmd.exe /c 'reg query "HKCU\Software\Classes\Directory\shell\ltool" /s' | Select-Object -First 3 | ForEach-Object { Write-Output ('  ' + $_.Trim()) }
Stop-Process -Id $p.Id -Force
Start-Sleep -Milliseconds 500
Start-Process -FilePath $exe | Out-Null
'installed instance relaunched'
