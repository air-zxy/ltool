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
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\tray6.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
function Root($e) {
  $w = $e
  while ($true) {
    $pp = $w.Parent
    if ($null -eq $pp) { break }
    if ($pp.Current.ControlType -ne $CT::Window) { break }
    $w = $pp
  }
  return $w
}
function Invoke($e, [string]$what) {
  if ($null -eq $e) { Say ("FAIL: " + $what + " 不存在"); throw 'missing' }
  try { $p = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern); $p.Invoke(); Say ('invoke ' + $what); return }
  catch { Say ('invoke 不行: ' + $what) }
  try { $s = $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern); $s.Select(); Say ('select ' + $what); return }
  catch { Say ('select 也不行: ' + $what) }
  throw 'no pattern'
}
# 点之前先确认"这个坐标上的根窗口就是元素自己那个根窗口"，否则不点。
function ClickHere($e, [string]$what) {
  if ($null -eq $e) { Say ("FAIL: " + $what + " 不存在"); throw 'missing' }
  $r = $e.Current.BoundingRectangle
  if ($r.Width -le 0) { Say ("FAIL: " + $what + " 没有矩形"); throw 'no rect' }
  $x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
  $pt = New-Object PT; $pt.x = $x; $pt.y = $y
  $at = [W32]::GetAncestor([W32]::WindowFromPoint($pt), 2)
  $mine = [IntPtr](Root $e).Current.NativeWindowHandle
  if ($at -ne $mine) { Say ("中止：(" + $x + "," + $y + ") 上的根窗口是 " + $at + "，元素属于 " + $mine + "（" + $what + "）"); throw 'occluded' }
  [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 150
  [W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 300
  Say ("click " + $what + " @ " + $x + "," + $y)
}
function DiffCount([string]$a, [string]$b, [int]$x0, [int]$y0, [int]$w, [int]$h) {
  $m = [System.Drawing.Bitmap]::new($a); $n = [System.Drawing.Bitmap]::new($b)
  $c = 0
  for ($i = 0; $i -lt $w; $i++) { for ($j = 0; $j -lt $h; $j++) {
      $p1 = $m.GetPixel($i,$j); $p2 = $n.GetPixel($i,$j)
      if ([Math]::Abs($p1.R-$p2.R) + [Math]::Abs($p1.G-$p2.G) + [Math]::Abs($p1.B-$p2.B) -gt 30) { $c++ }
  } }
  $m.Dispose(); $n.Dispose(); return $c
}

# --settings 只在冷启动时生效（单实例转发不带这个参数），所以先停掉再带参数起
$prev = Get-Process ltool -ErrorAction SilentlyContinue
if ($prev) { Say ('停掉在跑的 ltool pid=' + ($prev.Id -join ',')); Stop-Process -Id $prev.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 6
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Say ('pid = ' + $proc.Id)
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
Say ('设置窗口: ' + $sw[0].Current.Name)

# ---- 语言页 一键安装（Maven 缺失）----
$btn = Find (Ctrl $sw[0] $CT::Button) '一键安装'
if ($btn.Count -eq 0) {
  ClickHere (Find (Ctrl $sw[0] $CT::ListItem) '^语言$')[0] '语言导航'
  Start-Sleep -Milliseconds 1200
  $sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
  $btn = Find (Ctrl $sw[0] $CT::Button) '一键安装'
}
Say ('一键安装 = ' + $btn[0].Current.Name + ' 可用=' + $btn[0].Current.IsEnabled)
Invoke $btn[0] '一键安装'
Start-Sleep -Milliseconds 1200
$yes = @(Ctrl $root $CT::Button | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -eq 'Yes' })
Say ('确认按钮 = ' + $yes.Count)
if ($yes.Count -eq 0) { Say 'FAIL: 没有确认弹窗'; Save-Out; exit 6 }
Invoke $yes[0] '确认(Yes)'
Start-Sleep -Seconds 7

# ---- 托盘：打开隐藏图标面板，连拍三张 ----
$overflow = @(Find (Ctrl $root $CT::Button) '隐藏的图标')
Say ('溢出按钮 = ' + $overflow.Count)
ClickHere $overflow[0] '托盘溢出'
Start-Sleep -Milliseconds 1200
$lt = @(Ctrl $root $CT::Button | Where-Object { $_.Current.ProcessId -ne $proc.Id -and $_.Current.Name -match 'ltool' })
Say ('托盘里的 ltool 图标 = ' + $lt.Count)
foreach ($x in $lt) { Say ('   [' + $x.Current.Name + '] @ ' + [int]$x.Current.BoundingRectangle.X + ',' + [int]$x.Current.BoundingRectangle.Y) }
if ($lt.Count -eq 0) { Say 'FAIL: 溢出面板里没有 ltool'; Save-Out; exit 7 }
$lr = $lt[0].Current.BoundingRectangle
$bx = [int]$lr.X - 70; $by = [int]$lr.Y - 55
Shot $bx $by 240 130 'D:\project\cpp\ltool\build\verify\t6_a.png'
Start-Sleep -Milliseconds 600
Shot $bx $by 240 130 'D:\project\cpp\ltool\build\verify\t6_b.png'
Start-Sleep -Milliseconds 600
Shot $bx $by 240 130 'D:\project\cpp\ltool\build\verify\t6_c.png'
Say ('托盘图标帧差 a-b = ' + (DiffCount 'D:\project\cpp\ltool\build\verify\t6_a.png' 'D:\project\cpp\ltool\build\verify\t6_b.png' 0 0 240 130))
Say ('托盘图标帧差 b-c = ' + (DiffCount 'D:\project\cpp\ltool\build\verify\t6_b.png' 'D:\project\cpp\ltool\build\verify\t6_c.png' 0 0 240 130))
ClickHere $lt[0] '托盘图标'
Start-Sleep -Milliseconds 2500
foreach ($w in (Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id })) { Say ('  窗口 "' + $w.Current.Name + '"') }
$logw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '安装日志' })
Say ('安装日志窗口 = ' + $logw.Count)
if ($logw.Count -eq 0) { Say 'FAIL: 点托盘没打开看台'; Save-Out; exit 8 }
$cancel = Find (Ctrl $logw[0] $CT::Button) '取消安装'
Say ('取消安装 = ' + $cancel.Count + ' 可用=' + $cancel[0].Current.IsEnabled)
$r = $logw[0].Current.BoundingRectangle
Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\t6_log_before.png'
Invoke $cancel[0] '取消安装'
Start-Sleep -Seconds 6
$logw2 = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '安装日志' })
if ($logw2.Count) { $r = $logw2[0].Current.BoundingRectangle; Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\t6_log_after.png' }
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 10 | ForEach-Object { Say ('  log: ' + $_) }
Say ('D:\software\Maven 存在 = ' + (Test-Path 'D:\software\Maven'))
Save-Out
Write-Host 'done'
