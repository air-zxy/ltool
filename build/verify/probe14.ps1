$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$img = 'D:\project\cpp\ltool\build\verify\cfg_top.png'
$log = New-Object System.Collections.Generic.List[string]

$p = Start-Process -FilePath $exe -ArgumentList @('--ocrpanel', $img) -PassThru
$cond = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $p.Id)
$dlg = $null
for ($i = 0; $i -lt 40; $i++) {
  Start-Sleep -Milliseconds 300
  foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.ClassName -eq 'QDialog') { $dlg = $w; break }
  }
  if ($dlg) { break }
}
if ($null -eq $dlg) { Write-Output 'NO-DIALOG'; exit 1 }
$log.Add('窗口标题(刚开)=' + $dlg.Current.Name)
# 等识别回填（标题会变成 "提取文字 · N 字 / Nms · 已复制到剪贴板"）
for ($i = 0; $i -lt 40; $i++) {
  Start-Sleep -Milliseconds 300
  if ($dlg.Current.Name -like '*剪贴板*' -or $dlg.Current.Name -like '*没认出来*' -or $dlg.Current.Name -like '*没有 OCR*') { break }
}
$log.Add('窗口标题(识别后)=' + $dlg.Current.Name)
$tc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Document)
$found = @($dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $tc))
$log.Add('Document 控件数=' + $found.Count)
foreach ($e in $found) {
  $vp = $null
  if ($e.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$vp)) {
    $txt = $vp.Current.Value
    $log.Add('文本框内容前 200 字=' + $txt.Substring(0, [Math]::Min(200, $txt.Length)).Replace("`n", ' / '))
  }
}
$log.Add('剪贴板里的内容前 120 字=' + ((Get-Clipboard -Raw) -replace "`r?`n", ' / ').Substring(0, 120))
$bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
foreach ($b in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
  $log.Add('按钮 [' + $b.Current.Name + '] 可用=' + $b.Current.Enabled)
  if ($b.Current.Name -eq '关闭') {
    $ip = $null
    if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke() }
  }
}
$log.Add('进程退出=' + $p.WaitForExit(5000))
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\probe14.txt', $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
