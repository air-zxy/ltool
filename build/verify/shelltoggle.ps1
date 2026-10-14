$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$chkWord = [string][char]0x53F3 + [char]0x952E
$saveWord = [string][char]0x4FDD + [char]0x5B58

function Reg-Check($tag) {
  $out = & cmd.exe /c 'reg query "HKCU\Software\Classes\*\shell\ltool" /s 2>nul'
  $n = @($out | Where-Object { $_ -like 'HKEY_*' }).Count
  Write-Output ($tag + ': 我们的键行数 = ' + $n)
  $out | Where-Object { $_ -like 'HKEY_*' } | Select-Object -First 8 | ForEach-Object { Write-Output ('    ' + $_.Trim()) }
}

function Find-Dlg($procId) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $procId)
  foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
      [System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.ClassName -eq 'SettingsDialog') { return $w }
  }
  return $null
}

function Toggle-Shell($dlg) {
  $cc = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::CheckBox)
  foreach ($c in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cc)) {
    if ($c.Current.Name -like ('*' + $chkWord + '*')) {
      $tp = $null
      if ($c.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref]$tp)) {
        $tp.Toggle()
        return $c.Current.Name
      }
    }
  }
  return $null
}

function Click-Save($dlg) {
  $bc = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    if ($b.Current.Name -eq $saveWord) {
      $ip = $null
      if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) {
        $ip.Invoke()
        return $true
      }
    }
  }
  return $false
}

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
& cmd.exe /c 'reg delete "HKCU\Software\Classes\*\shell\ltool" /f >nul 2>&1'
& cmd.exe /c 'reg delete "HKCU\Software\Classes\Directory\shell\ltool" /f >nul 2>&1'
Reg-Check '初始（应为 0）'

$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', '2') -PassThru
Start-Sleep -Seconds 5
$dlg = Find-Dlg $p.Id
if (-not $dlg) { Write-Output 'no SettingsDialog'; exit 1 }
Write-Output ('勾上这一项: ' + (Toggle-Shell $dlg))
Write-Output ('点保存: ' + (Click-Save $dlg))
Start-Sleep -Seconds 2
Reg-Check '勾上并保存后'

$dlg = Find-Dlg $p.Id
$null = Toggle-Shell $dlg
$null = Click-Save $dlg
Start-Sleep -Seconds 2
Reg-Check '取消勾选再保存后'

$dlg = Find-Dlg $p.Id
$null = Toggle-Shell $dlg
$null = Click-Save $dlg
Start-Sleep -Seconds 2
Reg-Check '再勾回来（交付时保持开启，方便你直接右键看）'
Stop-Process -Id $p.Id -Force
