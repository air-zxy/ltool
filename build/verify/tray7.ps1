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
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\tray7.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
function OwnHandle($e) {
  $w = $e
  for ($i = 0; $i -lt 12; $i++) {
    $h = [IntPtr]$w.Current.NativeWindowHandle
    if ($h -ne [IntPtr]::Zero) { return $h }
    $pp = $w.Parent
    if ($null -eq $pp) { break }
    $w = $pp
  }
  return [IntPtr]::Zero
}
function Invoke($e, [string]$what) {
  if ($null -eq $e) { Say ("FAIL: " + $what + " 不存在"); throw 'missing' }
  try { $p = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern); $p.Invoke(); Say ('invoke ' + $what); return }
  catch { Say ('invoke 不行: ' + $what) }
  try { $s = $e.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern); $s.Select(); Say ('select ' + $what); return }
  catch { Say ('select 也不行: ' + $what) }
  throw 'no pattern'
}
# 点之前确认坐标上的根窗口就是元素自己（或元素没有窗口句柄时是 explorer 的系统栏），
# 否则不点 —— 保证合成点击永远不会落到用户正在用的窗口上。
function ClickHere($e, [string]$what, [IntPtr]$owner = [IntPtr]::Zero) {
  if ($null -eq $e) { Say ("FAIL: " + $what + " 不存在"); throw 'missing' }
  $r = $e.Current.BoundingRectangle
  if ($r.Width -le 0) { Say ("FAIL: " + $what + " 没有矩形"); throw 'no rect' }
  $x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
  $pt = New-Object PT; $pt.x = $x; $pt.y = $y
  $at = [W32]::GetAncestor([W32]::WindowFromPoint($pt), 2)
  $mine = $owner
  if ($mine -ne [IntPtr]::Zero -and $at -ne $mine) {
    [W32]::keybd_event(0x12,0,0,[IntPtr]::Zero); [W32]::keybd_event(0x12,0,2,[IntPtr]::Zero)
    [W32]::SetForegroundWindow($mine) | Out-Null
    Start-Sleep -Milliseconds 400
    $at = [W32]::GetAncestor([W32]::WindowFromPoint($pt), 2)
  }
  if ($mine -eq [IntPtr]::Zero) {
    $wp = 0
    [W32]::GetWindowThreadProcessId($at, [ref]$wp) | Out-Null
    $explorer = (Get-Process explorer -ErrorAction SilentlyContinue).Id
    if ([int]$wp -ne [int]$explorer) { Say ("中止：" + $what + " 坐标上的窗口不属于 explorer"); throw 'wrong owner' }
  } elseif ($at -ne $mine) {
    Say ("中止：(" + $x + "," + $y + ") 上的根窗口是 " + $at + "，元素属于 " + $mine + "（" + $what + "）"); throw 'occluded'
  }
  [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 150
  [W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 300
  Say ("click " + $what + " @ " + $x + "," + $y)
}
function DiffCount([string]$a, [string]$b) {
  $m = [System.Drawing.Bitmap]::new($a); $n = [System.Drawing.Bitmap]::new($b)
  $c = 0
  for ($i = 0; $i -lt $m.Width; $i++) { for ($j = 0; $j -lt $m.Height; $j++) {
      $p1 = $m.GetPixel($i,$j); $p2 = $n.GetPixel($i,$j)
      if ([Math]::Abs($p1.R-$p2.R) + [Math]::Abs($p1.G-$p2.G) + [Math]::Abs($p1.B-$p2.B) -gt 30) { $c++ }
  } }
  $m.Dispose(); $n.Dispose(); return $c
}

$prev = Get-Process ltool -ErrorAction SilentlyContinue
if ($prev) { Say ('停掉在跑的 ltool pid=' + ($prev.Id -join ',')); Stop-Process -Id $prev.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 6
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Say ('pid = ' + $proc.Id)
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
$swH = [IntPtr]$sw[0].Current.NativeWindowHandle

# ---- 语言页：确认 Maven 不再进队列（插件数据已修） ----
ClickHere (Find (Ctrl $sw[0] $CT::ListItem) '^语言$')[0] '语言导航' $swH
Start-Sleep -Milliseconds 2500
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
foreach ($t in (Find (Ctrl $sw[0] $CT::Text) 'Maven|JDK')) { Say ('  状态: ' + $t.Current.Name) }
$btn = Find (Ctrl $sw[0] $CT::Button) '一键安装|组件已齐全'
if ($btn.Count) { Say ('  按钮: ' + $btn[0].Current.Name + ' 可用=' + $btn[0].Current.IsEnabled) }

# ---- 软件页：搜 7-Zip（本机已装，winget 只会升级/报最新），点下载 ----
ClickHere (Find (Ctrl $sw[0] $CT::ListItem) '^软件$')[0] '软件导航' $swH
Start-Sleep -Milliseconds 1500
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
$ed = @(Ctrl $sw[0] $CT::Edit | Where-Object { $_.Current.BoundingRectangle.Height -gt 10 })
Say ('搜索框 = ' + $ed.Count)
if ($ed.Count) {
  try { $v = $ed[0].GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern); $v.SetValue('7-Zip'); Say '搜索框已填 7-Zip' }
  catch { Say ('SetValue 失败: ' + $_.Exception.Message) }
}
Start-Sleep -Milliseconds 1500
$txt = @(Ctrl $sw[0] $CT::Text | Where-Object { $_.Current.ProcessId -eq $proc.Id })
$seven = Find $txt '^7-Zip'
Say ('7-Zip 文本 = ' + $seven.Count)
if ($seven.Count -eq 0) { foreach ($t in $txt) { if ($t.Current.Name.Length) { Say ('  text [' + $t.Current.Name.Substring(0,[Math]::Min(40,$t.Current.Name.Length)) + '] @ ' + [int]$t.Current.BoundingRectangle.Y) } }; Say 'FAIL: 过滤后找不到 7-Zip'; Save-Out; exit 6 }
$sr = $seven[0].Current.BoundingRectangle
$btns = @(Ctrl $sw[0] $CT::Button | Where-Object { $_.Current.Name -eq '下载' })
Say ('下载按钮总数 = ' + $btns.Count)
$pick = @($btns | Where-Object { $b = $_.Current.BoundingRectangle; [Math]::Abs($b.Y - $sr.Y) -lt 40 })
Say ('同一行的下载按钮 = ' + $pick.Count)
if ($pick.Count -eq 0) { Save-Out; exit 7 }
ClickHere $pick[0] '下载(7-Zip)' $swH
Start-Sleep -Milliseconds 2500

# ---- 托盘：打开隐藏图标面板，连拍三张看有没有在转 ----
$overflow = @(Find (Ctrl $root $CT::Button) '隐藏的图标')
ClickHere $overflow[0] '托盘溢出'
Start-Sleep -Milliseconds 1200
$lt = @(Ctrl $root $CT::Button | Where-Object { $_.Current.ProcessId -ne $proc.Id -and $_.Current.Name -match 'ltool' })
Say ('托盘里的 ltool 图标 = ' + $lt.Count)
if ($lt.Count -eq 0) { Say 'FAIL: 溢出面板里没有 ltool'; Save-Out; exit 8 }
$lr = $lt[0].Current.BoundingRectangle
Say ('ltool 图标 @ ' + [int]$lr.X + ',' + [int]$lr.Y + ' [' + $lt[0].Current.Name + ']')
$b = 'D:\project\cpp\ltool\build\verify\t7_'
Shot ([int]$lr.X - 6) ([int]$lr.Y - 6) 44 44 ($b + 'a.png')
Start-Sleep -Milliseconds 500
Shot ([int]$lr.X - 6) ([int]$lr.Y - 6) 44 44 ($b + 'b.png')
Start-Sleep -Milliseconds 500
Shot ([int]$lr.X - 6) ([int]$lr.Y - 6) 44 44 ($b + 'c.png')
Say ('帧差 a-b = ' + (DiffCount ($b + 'a.png') ($b + 'b.png')) + ' , b-c = ' + (DiffCount ($b + 'b.png') ($b + 'c.png')))
ClickHere $lt[0] '托盘图标'
Start-Sleep -Milliseconds 2500
$logw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '安装日志' })
Say ('安装日志窗口 = ' + $logw.Count)
if ($logw.Count -eq 0) { Say 'FAIL: 点托盘没打开看台'; Save-Out; exit 9 }
$cancel = Find (Ctrl $logw[0] $CT::Button) '取消安装'
Say ('取消安装 = ' + $cancel.Count + ' 可用=' + $cancel[0].Current.IsEnabled)
$r = $logw[0].Current.BoundingRectangle
Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\t7_log_before.png'
Invoke $cancel[0] '取消安装'
Start-Sleep -Seconds 6
$logw2 = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '安装日志' })
if ($logw2.Count) { $r = $logw2[0].Current.BoundingRectangle; Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\t7_log_after.png' }
Shot ([int]$lr.X - 6) ([int]$lr.Y - 6) 44 44 'D:\project\cpp\ltool\build\verify\t7_icon_after.png'
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 10 | ForEach-Object { Say ('  log: ' + $_) }
Save-Out
Write-Host 'done'
