$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\tray5.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  Say ('saved ' + (Split-Path $name -Leaf))
}
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
# Everything below goes through UIA patterns: no cursor, no z-order, so nothing can
# land on a window the user is working in.
function Invoke($e, [string]$what) {
  if ($null -eq $e) { Say ("FAIL: " + $what + " 不存在"); throw 'missing' }
  try {
    $p = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
    $p.Invoke(); Say ('invoke ' + $what); return
  } catch { Say ('invoke 失败: ' + $what + ' - ' + $_.Exception.Message) }
  try {
    $s = $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
    $s.Select(); Say ('select ' + $what); return
  } catch { Say ('select 也失败: ' + $what) }
  throw 'no pattern'
}

$proc = Get-Process ltool -ErrorAction Stop
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Say ('pid = ' + $proc.Id)
$wins = @($root.FindAll($TC::Children, $cond))
$sw = @($wins | Where-Object { $_.Current.Name -match '设置' })
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
Say ('设置窗口: ' + $sw[0].Current.Name)

# ---- 语言页 一键安装 ----
$btn = Find (Ctrl $sw[0] $CT::Button) '一键安装'
Say ('一键安装 = ' + $btn.Count + ' 文字=' + $btn[0].Current.Name + ' 可用=' + $btn[0].Current.IsEnabled)
Invoke $btn[0] '一键安装'
Start-Sleep -Milliseconds 1200
$allBtns = @(Ctrl $root $CT::Button | Where-Object { $_.Current.ProcessId -eq $proc.Id })
$yes = Find $allBtns '^Yes$'
Say ('确认按钮 = ' + $yes.Count)
if ($yes.Count -eq 0) { Say 'FAIL: 确认弹窗没出现'; Save-Out; exit 6 }
Invoke $yes[0] '确认(Yes)'
Start-Sleep -Milliseconds 3000

# ---- 托盘：打开隐藏图标面板，连拍三张看图标在不在转 ----
$overflow = @(Find (Ctrl $root $CT::Button) '隐藏的图标')
Say ('溢出按钮 = ' + $overflow.Count)
Invoke $overflow[0] '托盘溢出'
Start-Sleep -Milliseconds 1200
$lt = @(Find (Ctrl $root $CT::Button) 'ltool')
Say ('托盘里的 ltool 图标 = ' + $lt.Count)
if ($lt.Count -eq 0) {
  foreach ($b in (Ctrl $root $CT::Button)) { if ($b.Current.BoundingRectangle.Y -gt 1250) { Say ('  底部按钮 [' + $b.Current.Name + ']') } }
  Say 'FAIL: 溢出面板里找不到 ltool'; Save-Out; exit 7
}
$lr = $lt[0].Current.BoundingRectangle
Say ('ltool 托盘图标 @ ' + [int]$lr.X + ',' + [int]$lr.Y + ' name=' + $lt[0].Current.Name)
Shot ([int]$lr.X - 70) ([int]$lr.Y - 55) 240 130 'D:\project\cpp\ltool\build\verify\tray_busy.png'
Start-Sleep -Milliseconds 600
Shot ([int]$lr.X - 70) ([int]$lr.Y - 55) 240 130 'D:\project\cpp\ltool\build\verify\tray_busy2.png'
Start-Sleep -Milliseconds 600
Shot ([int]$lr.X - 70) ([int]$lr.Y - 55) 240 130 'D:\project\cpp\ltool\build\verify\tray_busy3.png'
Invoke $lt[0] '托盘图标'
Start-Sleep -Milliseconds 2500
foreach ($w in (Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id })) { Say ('  窗口 "' + $w.Current.Name + '"') }
$logw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '安装日志' })
Say ('安装日志窗口 = ' + $logw.Count)
if ($logw.Count -eq 0) { Say 'FAIL: 点托盘没打开看台'; Save-Out; exit 8 }
$cancel = Find (Ctrl $logw[0] $CT::Button) '取消安装'
Say ('取消安装 = ' + $cancel.Count + ' 可用=' + $cancel[0].Current.IsEnabled)
$r = $logw[0].Current.BoundingRectangle
Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\logwin_busy.png'
Invoke $cancel[0] '取消安装'
Start-Sleep -Milliseconds 5000
$logw2 = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '安装日志' })
if ($logw2.Count) { $r = $logw2[0].Current.BoundingRectangle; Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\logwin_cancelled.png' }
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 8 | ForEach-Object { Say ('  log: ' + $_) }
Say ('D:\software\Maven 存在 = ' + (Test-Path 'D:\software\Maven'))
Save-Out
Write-Host 'done'
