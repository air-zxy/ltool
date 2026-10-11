$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct PT { public int x; public int y; }
public static class W8 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\tray8.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
function DiffCount([string]$a, [string]$b) {
  $m = [System.Drawing.Bitmap]::new($a); $n = [System.Drawing.Bitmap]::new($b)
  $c = 0
  for ($i = 0; $i -lt $m.Width; $i++) { for ($j = 0; $j -lt $m.Height; $j++) {
      $p1 = $m.GetPixel($i,$j); $p2 = $n.GetPixel($i,$j)
      if ([Math]::Abs($p1.R-$p2.R) + [Math]::Abs($p1.G-$p2.G) + [Math]::Abs($p1.B-$p2.B) -gt 24) { $c++ }
  } }
  $m.Dispose(); $n.Dispose(); return $c
}
function Inv($e, [string]$what) {
  $p = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
  $p.Invoke(); Say ('invoke ' + $what)
}
# 只点系统任务栏（explorer 拥有）的坐标，其它一律走 UIA Invoke
function ClickShell($e, [string]$what) {
  $r = $e.Current.BoundingRectangle
  $x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
  $pt = New-Object PT; $pt.x = $x; $pt.y = $y
  $at = [W8]::GetAncestor([W8]::WindowFromPoint($pt), 2)
  $wp = 0
  [W8]::GetWindowThreadProcessId($at, [ref]$wp) | Out-Null
  if ([int]$wp -ne [int](Get-Process explorer).Id) { Say ("中止：" + $what + " 不在 explorer 窗口上"); throw 'wrong owner' }
  [W8]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120
  [W8]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [W8]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 250
  Say ("click " + $what + " @ " + $x + "," + $y)
}

$prev = Get-Process ltool -ErrorAction SilentlyContinue
if ($prev) { Stop-Process -Id $prev.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\software\ltool\ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 6
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Say ('pid = ' + $proc.Id)
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
# 切到软件页：Qt 的列表项不认 Invoke（只认 currentRow 变化），所以还是得点，
# 但点之前把设置窗口提到前台、并确认坐标上的根窗口就是它
$swH = [IntPtr]$sw[0].Current.NativeWindowHandle
function ClickIn($e, [string]$what, [IntPtr]$owner) {
  $r = $e.Current.BoundingRectangle
  $x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
  $pt = New-Object PT; $pt.x = $x; $pt.y = $y
  $at = [W8]::GetAncestor([W8]::WindowFromPoint($pt), 2)
  if ($at -ne $owner) {
    [W8]::keybd_event(0x12,0,0,[IntPtr]::Zero); [W8]::keybd_event(0x12,0,2,[IntPtr]::Zero)
    [W8]::SetForegroundWindow($owner) | Out-Null
    Start-Sleep -Milliseconds 400
    $at = [W8]::GetAncestor([W8]::WindowFromPoint($pt), 2)
  }
  if ($at -ne $owner) { Say ("中止：" + $what + " 被别的窗口挡着"); throw 'occluded' }
  [W8]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120
  [W8]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [W8]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 300
  Say ("click " + $what + " @ " + $x + "," + $y)
}
$nav = Find (Ctrl $sw[0] $CT::ListItem) '^软件$'
ClickIn $nav[0] '软件导航' $swH
Start-Sleep -Milliseconds 1200
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
$seven = Find (Ctrl $sw[0] $CT::Text) '^7-Zip'
Say ('7-Zip 文本 = ' + $seven.Count)
if ($seven.Count -eq 0) { Say 'FAIL: 不在软件页/找不到 7-Zip'; Save-Out; exit 6 }
$sr = $seven[0].Current.BoundingRectangle
$dl = @(Ctrl $sw[0] $CT::Button | Where-Object { $_.Current.Name -eq '下载' -and [Math]::Abs($_.Current.BoundingRectangle.Y - $sr.Y) -lt 40 })
Say ('7-Zip 下载按钮 = ' + $dl.Count)

# 先把隐藏图标面板打开，再点火安装，然后连拍
$overflow = @(Find (Ctrl $root $CT::Button) '隐藏的图标')
ClickShell $overflow[0] '托盘溢出'
Start-Sleep -Milliseconds 900
$lt = @(Ctrl $root $CT::Button | Where-Object { $_.Current.ProcessId -ne $proc.Id -and $_.Current.Name -match 'ltool' })
if ($lt.Count -eq 0) { Say 'FAIL: 托盘里没有 ltool'; Save-Out; exit 7 }
$lr = $lt[0].Current.BoundingRectangle
$bx = [int]$lr.X - 4; $by = [int]$lr.Y - 4
Shot $bx $by 40 40 'D:\project\cpp\ltool\build\verify\t8_idle.png'
Inv $dl[0] '下载(7-Zip)'
$names = @()
for ($k = 0; $k -lt 10; $k++) {
  $f = 'D:\project\cpp\ltool\build\verify\t8_' + $k + '.png'
  Shot $bx $by 40 40 $f
  $names += $f
  Start-Sleep -Milliseconds 300
}
for ($k = 0; $k -lt 9; $k++) {
  Say ('帧 ' + $k + '→' + ($k+1) + ' 差异像素 = ' + (DiffCount $names[$k] $names[$k+1]))
}
Say ('idle→0 差异 = ' + (DiffCount 'D:\project\cpp\ltool\build\verify\t8_idle.png' $names[0]))
Save-Out
Write-Host 'done'
