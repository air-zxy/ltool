$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$wClose = [string][char]0x5173 + [char]0x95ED
$log = New-Object System.Collections.Generic.List[string]

# 1) 把我自己弹出来的进度对话框用"关闭"按钮正常关掉
foreach ($p in @(Get-Process ltool -ErrorAction SilentlyContinue)) {
  $c = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $p.Id)
  foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c)) {
    $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
    foreach ($b in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
      if ($b.Current.Name -eq $wClose) {
        $ip = $null
        if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) {
          $ip.Invoke()
          $log.Add('closed dialog pid=' + $p.Id + ' title=' + $w.Current.Name)
          break
        }
      }
    }
  }
}
Start-Sleep -Milliseconds 800

# 2) 我点"打开位置"弹出来的资源管理器窗口，按标题关掉（只关我自己开的那些目录）
$roots = @('probe7', 'probe8', 'finalsmoke', 'shtest', 'artest', 'probe5', 'probe2', 'probe3')
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
  $n = ''
  try { $n = $w.Current.Name } catch { continue }
  if ($w.Current.ClassName -ne 'CabinetWClass') { continue }
  foreach ($r in $roots) {
    if ($n -like ('*' + $r + '*')) {
      $wp = $null
      if ($w.TryGetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern, [ref]$wp)) {
        $wp.Close()
        $log.Add('closed explorer window: ' + $n)
      }
      break
    }
  }
}
Start-Sleep -Milliseconds 600
$log.Add('ltool 还剩 ' + @(Get-Process ltool -ErrorAction SilentlyContinue).Count + ' 个进程')
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\_cleanup_report.txt', $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
