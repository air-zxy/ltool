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
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\tray4.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
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
function Click($e, [string]$what, [int]$minY = 0) {
  if ($null -eq $e) { Say ("FAIL: " + $what + " 不存在"); throw 'missing' }
  $r = $e.Current.BoundingRectangle
  if ($r.Width -le 0) { Say ("FAIL: " + $what + " 没有矩形"); throw 'no rect' }
  $x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
  if ($y -lt $minY) { Say ("FAIL: " + $what + " 位置异常 y=" + $y); throw 'bad pos' }
  if (-not (Raise)) { Say ("中止：抬不起窗口，不点 " + $what); throw 'no foreground' }
  $pt = New-Object PT; $pt.x = $x; $pt.y = $y
  $at = [W32]::GetAncestor([W32]::WindowFromPoint($pt), 2)
  if ($at -ne $script:hwnd) { Say ("中止：(" + $x + "," + $y + ") 上是 " + $at + " 不是 " + $script:hwnd + "（" + $what + "）"); throw 'occluded' }
  [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120
  [W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 300
  Say ("click " + $what + " @ " + $x + "," + $y)
}

$proc = Get-Process ltool -ErrorAction Stop
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Say ('pid = ' + $proc.Id)
$sw = Find (Wins) '设置'
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
$swH = [IntPtr]$sw[0].Current.NativeWindowHandle
$script:hwnd = $swH

# ---- 语言页：一键安装（当前只有 Maven 缺）----
$btn = Find (Ctrl $sw[0] $CT::Button) '一键安装'
if ($btn.Count -eq 0) {
  Click (Find (Ctrl $sw[0] $CT::ListItem) '^语言$')[0] '语言 tab'
  Start-Sleep -Milliseconds 1200
  $sw = Find (Wins) '设置'
  $btn = Find (Ctrl $sw[0] $CT::Button) '一键安装'
}
Say ('一键安装按钮 = ' + $btn.Count + ' 文字=' + $btn[0].Current.Name + ' 可用=' + $btn[0].Current.IsEnabled)
Click $btn[0] '一键安装'
Start-Sleep -Milliseconds 1200
# 确认弹窗
$dlg = @(Wins | Where-Object { $_.Current.NativeWindowHandle -ne $swH })
Say ('弹窗数 = ' + $dlg.Count)
$yes = $null
foreach ($d in $dlg) { $yes = @(Find (Ctrl $d $CT::Button) '^(是|Yes)') ; if ($yes.Count) { $script:hwnd = [IntPtr]$d.Current.NativeWindowHandle; break } }
if (-not $yes) { foreach ($d in $dlg) { foreach ($b in (Ctrl $d $CT::Button)) { Say ('  dlg btn [' + $b.Current.Name + ']') } }; Say 'FAIL: 没有确认弹窗'; Save-Out; exit 6 }
Click $yes[0] '确认安装'
Start-Sleep -Milliseconds 2500
$script:hwnd = $swH
foreach ($t in (Find (Ctrl $sw[0] $CT::Text) '一键安装|安装')) { Say ('  状态: ' + $t.Current.Name) }

# ---- 托盘：隐藏图标面板里连拍两张，看图标在不在转 ----
$overflow = @(Find (Ctrl $root $CT::Button) '隐藏的图标')
$script:hwnd = [IntPtr]::Zero
$oh = $overflow[0].Current.BoundingRectangle
Say ('溢出按钮 @ ' + [int]$oh.X + ',' + [int]$oh.Y)
[W32]::SetCursorPos([int]($oh.X + $oh.Width/2), [int]($oh.Y + $oh.Height/2)) | Out-Null
Start-Sleep -Milliseconds 150
[W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
[W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 1200
$lt = Find (Ctrl $root $CT::Button) 'ltool'
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
[W32]::SetCursorPos([int]($lr.X + $lr.Width/2), [int]($lr.Y + $lr.Height/2)) | Out-Null
Start-Sleep -Milliseconds 150
[W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
[W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 2200
foreach ($w in (Wins)) { Say ('  窗口 "' + $w.Current.Name + '"') }
$logw = @((Wins) | Where-Object { $_.Current.Name -match '安装日志' })
Say ('安装日志窗口 = ' + $logw.Count)
if ($logw.Count -eq 0) { Say 'FAIL: 点托盘没打开看台'; Save-Out; exit 8 }
$script:hwnd = [IntPtr]$logw[0].Current.NativeWindowHandle
$cancel = Find (Ctrl $logw[0] $CT::Button) '取消安装'
Say ('取消安装按钮 = ' + $cancel.Count + ' 可用=' + $cancel[0].Current.IsEnabled)
$r = $logw[0].Current.BoundingRectangle
Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\logwin_busy.png'
Click $cancel[0] '取消安装'
Start-Sleep -Milliseconds 4000
$logw2 = @((Wins) | Where-Object { $_.Current.Name -match '安装日志' })
if ($logw2.Count) { $r = $logw2[0].Current.BoundingRectangle; Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\logwin_cancelled.png' }
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 8 | ForEach-Object { Say ('  log: ' + $_) }
Say ('mvn 是否装上: ' + (Test-Path 'D:\software\Maven'))
Save-Out
Write-Host 'done'
