$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
# 只关我自己开的那一个（标题是探针目录里的文件名），别碰用户自己开的资源管理器窗口
$target = [string][char]0x8BF4 + [char]0x660E + '.txt (2)'
$also = @('expmenu', 'menuprobe')
$n = 0
foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)) {
  if ($w.Current.ClassName -ne 'CabinetWClass') { continue }
  $name = ''
  try { $name = $w.Current.Name } catch { continue }
  if ($name.StartsWith($target) -or ($also | Where-Object { $name -like ('*' + $_ + '*') })) {
    $wp = $null
    if ($w.TryGetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern, [ref]$wp)) {
      $wp.Close()
      $n++
      Write-Output ('closed: ' + $name)
    }
  }
}
if ($n -eq 0) { Write-Output '没有我要找的那个窗口（可能已经关了）' }
