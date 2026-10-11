$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$script:pid2 = 0
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s); Write-Host ('| ' + $s) }
function Warp([int]$x, [int]$y) { [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 150 }
function Down { [W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60 }
function Up   { [W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 250 }
function ClickAt([int]$x,[int]$y) { Warp $x $y; Down; Up }
function Key([byte]$vk) { [W32]::keybd_event($vk,0,0,[IntPtr]::Zero); [W32]::keybd_event($vk,0,2,[IntPtr]::Zero); Start-Sleep -Milliseconds 120 }
function AssertLtoolFg([IntPtr]$h, [string]$what) {
  $fp = [uint32]0
  [W32]::GetWindowThreadProcessId([W32]::GetForegroundWindow(), [ref]$fp) | Out-Null
  if ([int]$fp -ne $script:pid2) {
    [W32]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 350
    $fp = [uint32]0
    [W32]::GetWindowThreadProcessId([W32]::GetForegroundWindow(), [ref]$fp) | Out-Null
  }
  if ([int]$fp -ne $script:pid2) { Say ("中止：前台不是 ltool（" + $what + "）") ; throw 'not foreground' }
}
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
function ClickEl($e, [switch]$Taskbar) {
  $r = $e.Current.BoundingRectangle
  if ($r.Width -le 0) { Say '跳过：元素没有矩形'; return }
  # 每一次点击前都要确认前台是 ltool 自己的窗口 —— 否则合成鼠标事件会落到
  # 用户正在用的浏览器/游戏上（上一版脚本就是因为只在部分点击前做了检查而误点了一次）。
  # 只有系统任务栏/溢出面板例外：那是固定位置的系统控件，不会碰到用户内容。
  if (-not $Taskbar) {
    $fp = [uint32]0
    [W32]::GetWindowThreadProcessId([W32]::GetForegroundWindow(), [ref]$fp) | Out-Null
    if ([int]$fp -ne $script:pid2) {
      [W32]::SetForegroundWindow($script:hwnd) | Out-Null
      Start-Sleep -Milliseconds 400
      $fp = [uint32]0
      [W32]::GetWindowThreadProcessId([W32]::GetForegroundWindow(), [ref]$fp) | Out-Null
    }
    if ([int]$fp -ne $script:pid2) { Say '中止：无法把 ltool 带到前台，不点击'; throw 'not foreground' }
  }
  ClickAt ([int]($r.X + $r.Width/2)) ([int]($r.Y + $r.Height/2))
}
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\tray_check.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }

$proc = Get-Process ltool -ErrorAction Stop
$script:pid2 = $proc.Id
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Say ('pid = ' + $proc.Id)

$sw = Find (Wins) '设置'
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
Say ('设置窗口: ' + $sw[0].Current.Name)
$script:hwnd = [IntPtr]$sw[0].Current.NativeWindowHandle

# ---- 软件页：点一个"已经装好"的包的下载按钮，winget 只会报已是最新，不改系统 ----
ClickEl (Find (Ctrl $sw[0] $CT::ListItem) '^软件$')[0]
Start-Sleep -Milliseconds 1500
$rows = Ctrl $sw[0] $CT::ListItem
$row = Find $rows '7-Zip'
if ($row.Count -eq 0) { Say 'FAIL: 找不到 7-Zip 行'; foreach ($r in $rows) { Say ('  row ' + $r.Current.Name) }; Save-Out; exit 6 }
$rr = $row[0].Current.BoundingRectangle
Say ('7-Zip 行 @ ' + [int]$rr.X + ',' + [int]$rr.Y + ' ' + [int]$rr.Width + 'x' + [int]$rr.Height)
$btns = Ctrl $sw[0] $CT::Button
$same = @($btns | Where-Object { $b = $_.Current.BoundingRectangle; $b.Y -ge ($rr.Y - 6) -and $b.Y -lt ($rr.Y + $rr.Height + 6) -and $b.X -gt ($rr.X + 300) })
Say ('同一行的按钮 = ' + $same.Count)
if ($same.Count -eq 0) { Say 'FAIL: 行内没有按钮'; foreach ($b in $btns) { Say ('  btn [' + $b.Current.Name + '] @ ' + [int]$b.Current.BoundingRectangle.X + ',' + [int]$b.Current.BoundingRectangle.Y) }; Save-Out; exit 7 }
foreach ($b in $same) { Say ('  btn [' + $b.Current.Name + ']') }
ClickEl $same[0]
Start-Sleep -Milliseconds 1500
Say ('状态行: ' + ((Find (Ctrl $sw[0] $CT::Text) '安装') | ForEach-Object { $_.Current.Name } | Select-Object -First 2) )

# ---- 托盘：展开隐藏图标，看图标是不是在转，然后点它打开看台 ----
$overflow = @(Find (Ctrl $root $CT::Button) '隐藏的图标')
Say ('隐藏图标按钮 = ' + $overflow.Count)
if ($overflow.Count -eq 0) { Say 'FAIL: 找不到托盘溢出按钮'; Save-Out; exit 8 }
ClickEl $overflow[0] -Taskbar
Start-Sleep -Milliseconds 900
$fly = @(Ctrl $root $CT::Window)
$lt = Find (Ctrl $root $CT::Button) 'ltool'
Say ('托盘里的 ltool 图标 = ' + $lt.Count)
if ($lt.Count -eq 0) {
  foreach ($b in (Ctrl $root $CT::Button)) { if ($b.Current.BoundingRectangle.Y -gt 1000) { Say ('  底部按钮 [' + $b.Current.Name + ']') } }
  Say 'FAIL: 溢出面板里找不到 ltool'; Save-Out; exit 9
}
$lr = $lt[0].Current.BoundingRectangle
Say ('ltool 托盘图标 @ ' + [int]$lr.X + ',' + [int]$lr.Y)
Shot ([int]$lr.X - 60) ([int]$lr.Y - 45) 220 120 'D:\project\cpp\ltool\build\verify\tray_busy.png'
ClickEl $lt[0] -Taskbar
Start-Sleep -Milliseconds 1800
foreach ($w in (Wins)) { Say ('  窗口 "' + $w.Current.Name + '"') }
$cancel = Find (Ctrl $root $CT::Button) '取消安装'
Say ('取消安装按钮 = ' + $cancel.Count)
if ($cancel.Count -eq 0) { Say 'FAIL: 看台里没有取消安装按钮'; Save-Out; exit 10 }
Say ('按钮可用 = ' + $cancel[0].Current.IsEnabled)
$lw = $root
$shot = @((Wins) | Where-Object { $_.Current.Name -match '安装日志' })
if ($shot.Count) { $r = $shot[0].Current.BoundingRectangle; Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\logwin_busy.png' }
ClickEl $cancel[0]
Start-Sleep -Milliseconds 3000
if ($shot.Count) { $r = $shot[0].Current.BoundingRectangle; Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\logwin_cancelled.png' }
Say '已点取消，看 log.txt：'
Save-Out
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 12 | ForEach-Object { Say ('  log: ' + $_) }
Save-Out
