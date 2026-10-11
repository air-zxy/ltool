$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct PT { public int x; public int y; }
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$script:hwnd = [IntPtr]::Zero
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\tray_check.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  Say ('saved ' + (Split-Path $name -Leaf))
}
function Wins { return @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ }) }
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
function Raise {
  [W32]::keybd_event(0x12,0,0,[IntPtr]::Zero); [W32]::keybd_event(0x12,0,2,[IntPtr]::Zero)
  [W32]::SetForegroundWindow($script:hwnd) | Out-Null
  Start-Sleep -Milliseconds 400
  return ([W32]::GetForegroundWindow() -eq $script:hwnd)
}
# Click only when the point we are about to click is really owned by the window we
# expect. That is the only guard that keeps a synthetic click off the user's screen.
function Click($e, [string]$what, [IntPtr]$owner, [int]$minY = 0) {
  if ($null -eq $e) { Say ("FAIL: " + $what + " 不存在"); throw 'missing' }
  $r = $e.Current.BoundingRectangle
  if ($r.Width -le 0) { Say ("FAIL: " + $what + " 没有矩形"); throw 'no rect' }
  $x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
  if ($y -lt $minY) { Say ("FAIL: " + $what + " 位置异常 y=" + $y); throw 'bad pos' }
  if ($owner -ne [IntPtr]::Zero) {
    if (-not (Raise)) { Say ("中止：抬不起 " + $what + " 的窗口，不点击"); throw 'no foreground' }
  }
  $pt = New-Object PT
  $pt.x = $x; $pt.y = $y
  $at = [W32]::GetAncestor([W32]::WindowFromPoint($pt), 2)
  if ($owner -ne [IntPtr]::Zero -and $at -ne $owner) {
    Say ("中止：(" + $x + "," + $y + ") 上是 " + $at + "，不是 " + $owner + "（" + $what + "）"); throw 'occluded'
  }
  [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120
  [W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 300
  Say ("click " + $what + " @ " + $x + "," + $y)
}

$proc = Get-Process ltool -ErrorAction Stop
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Say ('pid = ' + $proc.Id)
Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' | Out-Null
Start-Sleep -Seconds 3
$sw = Find (Wins) '设置'
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
$swHwnd = [IntPtr]$sw[0].Current.NativeWindowHandle
$script:hwnd = $swHwnd
Say ('设置窗口: ' + $sw[0].Current.Name)

# ---- 软件页：挑一个本机已装好的包点「下载」，winget 只会回"已是最新"，不动系统 ----
# 先在搜索框里过滤出 7-Zip（列表很长，不过滤它就在可视区外面）
$sw = Find (Wins) '设置'
$edits = @(Ctrl $sw[0] $CT::Edit | Where-Object { $_.Current.BoundingRectangle.Height -gt 10 })
Say ('搜索框 = ' + $edits.Count)
if ($edits.Count -gt 0) {
  try {
    $vp = $edits[0].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
    $vp.SetValue('7-Zip')
    Say '已在搜索框输入 7-Zip'
    Start-Sleep -Milliseconds 1500
  } catch { Say ('ValuePattern 失败: ' + $_.Exception.Message) }
}
$sw = Find (Wins) '设置'
$txts = Ctrl $sw[0] $CT::Text
$row = Find $txts '^7-Zip'
if ($row.Count -eq 0) {
  $tab = @(Find (Ctrl $sw[0] $CT::ListItem) '^软件$' | Where-Object { $_.Current.BoundingRectangle.Height -gt 20 })
  Say ('软件 tab 候选 = ' + $tab.Count)
  if ($tab.Count -eq 0) { Say 'FAIL: 找不到软件导航项'; Save-Out; exit 6 }
  Click $tab[0] '软件 tab' $swHwnd
  Start-Sleep -Milliseconds 1800
  $sw = Find (Wins) '设置'
  $txts = Ctrl $sw[0] $CT::Text
  $row = Find $txts '^7-Zip'
}
Say ('文本条目 = ' + $txts.Count)
foreach ($t in $txts) { if ($t.Current.Name.Length) { Say ('  text [' + $t.Current.Name.Substring(0,[Math]::Min(46,$t.Current.Name.Length)) + '] @ ' + [int]$t.Current.BoundingRectangle.X + ',' + [int]$t.Current.BoundingRectangle.Y) } }
$btns = Ctrl $sw[0] $CT::Button
Say ('按钮 = ' + $btns.Count)
foreach ($b in $btns) { Say ('  btn [' + $b.Current.Name + '] @ ' + [int]$b.Current.BoundingRectangle.X + ',' + [int]$b.Current.BoundingRectangle.Y) }
if ($row.Count -eq 0) { Say 'FAIL: 找不到 7-Zip 标签'; Save-Out; exit 6 }
$rr = $row[0].Current.BoundingRectangle
$same = @($btns | Where-Object { $b = $_.Current.BoundingRectangle; $b.Y -ge ($rr.Y - 14) -and $b.Y -lt ($rr.Y + 34) -and $b.X -gt ($rr.X - 60) })
Say ('7-Zip 行内按钮 = ' + $same.Count)
if ($same.Count -eq 0) { Save-Out; exit 7 }
foreach ($b in $same) { Say ('  btn [' + $b.Current.Name + ']') }
Click $same[0] '下载按钮' $swHwnd
Start-Sleep -Milliseconds 2500
foreach ($t in (Find (Ctrl $sw[0] $CT::Text) '安装')) { Say ('  状态: ' + $t.Current.Name) }

# ---- 托盘：展开隐藏图标面板，连拍两张看图标有没有在转 ----
$overflow = @(Find (Ctrl $root $CT::Button) '隐藏的图标')
if ($overflow.Count -eq 0) { Say 'FAIL: 找不到托盘溢出按钮'; Save-Out; exit 8 }
Click $overflow[0] '托盘溢出' [IntPtr]::Zero 1300
Start-Sleep -Milliseconds 1200
$lt = Find (Ctrl $root $CT::Button) 'ltool'
Say ('托盘里的 ltool 图标 = ' + $lt.Count)
if ($lt.Count -eq 0) {
  foreach ($b in (Ctrl $root $CT::Button)) { if ($b.Current.BoundingRectangle.Y -gt 1250) { Say ('  底部按钮 [' + $b.Current.Name + '] @ ' + [int]$b.Current.BoundingRectangle.X) } }
  Say 'FAIL: 溢出面板里找不到 ltool'; Save-Out; exit 9
}
$lr = $lt[0].Current.BoundingRectangle
Say ('ltool 托盘图标 @ ' + [int]$lr.X + ',' + [int]$lr.Y + ' name=' + $lt[0].Current.Name)
Shot ([int]$lr.X - 70) ([int]$lr.Y - 55) 240 130 'D:\project\cpp\ltool\build\verify\tray_busy.png'
Start-Sleep -Milliseconds 700
Shot ([int]$lr.X - 70) ([int]$lr.Y - 55) 240 130 'D:\project\cpp\ltool\build\verify\tray_busy2.png'
Click $lt[0] '托盘图标' [IntPtr]::Zero 1000
Start-Sleep -Milliseconds 2200
foreach ($w in (Wins)) { Say ('  窗口 "' + $w.Current.Name + '"') }
$logw = @((Wins) | Where-Object { $_.Current.Name -match '安装日志' })
Say ('安装日志窗口 = ' + $logw.Count)
if ($logw.Count -eq 0) { Say 'FAIL: 点托盘没有打开看台'; Save-Out; exit 10 }
$script:hwnd = [IntPtr]$logw[0].Current.NativeWindowHandle
$cancel = Find (Ctrl $logw[0] $CT::Button) '取消安装'
Say ('取消安装按钮 = ' + $cancel.Count)
if ($cancel.Count -eq 0) { Save-Out; exit 11 }
Say ('按钮可用 = ' + $cancel[0].Current.IsEnabled)
$lr2 = $logw[0].Current.BoundingRectangle
Shot ([int]$lr2.X) ([int]$lr2.Y) ([int]$lr2.Width) ([int]$lr2.Height) 'D:\project\cpp\ltool\build\verify\logwin_busy.png'
Click $cancel[0] '取消安装' ([IntPtr]$logw[0].Current.NativeWindowHandle)
Start-Sleep -Milliseconds 4000
$logw2 = @((Wins) | Where-Object { $_.Current.Name -match '安装日志' })
if ($logw2.Count) { $r = $logw2[0].Current.BoundingRectangle; Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\logwin_cancelled.png' }
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 8 | ForEach-Object { Say ('  log: ' + $_) }
Save-Out
Write-Host 'done'
