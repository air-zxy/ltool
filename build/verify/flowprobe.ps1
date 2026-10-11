$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\software\ltool\ltool.exe'
$zip = 'D:\project\cpp\ltool\build\verify\shtest\ex\in.zip'
$wClose = [string][char]0x5173 + [char]0x95ED

$p = Start-Process -FilePath $exe -ArgumentList @('--extract', $zip) -PassThru
Write-Output ('pid=' + $p.Id)
$cond = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $p.Id)
$deadline = (Get-Date).AddSeconds(20)
$wins = @()
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 400
  $wins = @($Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond))
  if ($wins.Count -gt 0) { break }
}
Write-Output ('top-level windows=' + $wins.Count)
foreach ($w in $wins) {
  Write-Output ('WINDOW class=' + $w.Current.ClassName + ' name=[' + $w.Current.Name + ']')
  foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
    $t = ''
    try { $t = $e.Current.Name } catch { $t = '?' }
    Write-Output ('   ' + $e.Current.ControlType.ProgrammaticName + ' | ' + $e.Current.ClassName + ' | ' + $t)
  }
}
foreach ($w in $wins) {
  $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    if ($b.Current.Name -eq $wClose) {
      $ip = $null
      if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke() }
    }
  }
}
Start-Sleep -Milliseconds 600
Write-Output ('exited=' + $p.WaitForExit(2000))
