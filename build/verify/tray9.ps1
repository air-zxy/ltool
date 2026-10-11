$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct PT { public int x; public int y; }
public static class W9 {
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
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\tray9.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
function Green([string]$name) {
  $b = [System.Drawing.Bitmap]::new($name); $c = 0
  for ($i = 0; $i -lt $b.Width; $i++) { for ($j = 0; $j -lt $b.Height; $j++) {
      $p = $b.GetPixel($i,$j)
      if ($p.G -gt 140 -and $p.G -gt $p.R + 30 -and $p.G -gt $p.B + 20) { $c++ }
  } }
  $b.Dispose(); return $c
}
function Inv($e, [string]$what) {
  $p = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern); $p.Invoke(); Say ('invoke ' + $what)
}
function ClickAt2($e, [string]$what, [IntPtr]$owner) {
  $r = $e.Current.BoundingRectangle
  $x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
  $pt = New-Object PT; $pt.x = $x; $pt.y = $y
  $at = [W9]::GetAncestor([W9]::WindowFromPoint($pt), 2)
  if ($owner -ne [IntPtr]::Zero -and $at -ne $owner) {
    [W9]::keybd_event(0x12,0,0,[IntPtr]::Zero); [W9]::keybd_event(0x12,0,2,[IntPtr]::Zero)
    [W9]::SetForegroundWindow($owner) | Out-Null; Start-Sleep -Milliseconds 400
    $at = [W9]::GetAncestor([W9]::WindowFromPoint($pt), 2)
  }
  if ($owner -eq [IntPtr]::Zero) {
    $wp = 0; [W9]::GetWindowThreadProcessId($at, [ref]$wp) | Out-Null
    if ([int]$wp -ne [int](Get-Process explorer).Id) { Say ("中止：" + $what + " 不在 explorer 上"); throw 'owner' }
  } elseif ($at -ne $owner) { Say ("中止：" + $what + " 被挡住"); throw 'occluded' }
  [W9]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120
  [W9]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [W9]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 250
  Say ("click " + $what + " @ " + $x + "," + $y)
}

$prev = Get-Process ltool -ErrorAction SilentlyContinue
if ($prev) { Stop-Process -Id $prev.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\software\ltool\ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 6
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Say ('pid = ' + $proc.Id)
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
$swH = [IntPtr]$sw[0].Current.NativeWindowHandle
ClickAt2 (Find (Ctrl $sw[0] $CT::ListItem) '^软件$')[0] '软件导航' $swH
Start-Sleep -Milliseconds 1500
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
# CC Switch：GitHub 发行版，没代理时下载要跑一阵，够我们观察转圈并取消
$row = Find (Ctrl $sw[0] $CT::Text) '^CC Switch'
Say ('CC Switch 行 = ' + $row.Count)
if ($row.Count -eq 0) { Say 'FAIL: 找不到 CC Switch 行'; Save-Out; exit 6 }
$rr = $row[0].Current.BoundingRectangle
$dl = @(Ctrl $sw[0] $CT::Button | Where-Object { $_.Current.Name -eq '下载' -and [Math]::Abs($_.Current.BoundingRectangle.Y - $rr.Y) -lt 40 })
Say ('它的下载按钮 = ' + $dl.Count)
Inv $dl[0] '下载(CC Switch)'
Start-Sleep -Milliseconds 800

$overflow = @(Find (Ctrl $root $CT::Button) '隐藏的图标')
ClickAt2 $overflow[0] '托盘溢出' ([IntPtr]::Zero)
Start-Sleep -Milliseconds 900
$lt = @(Ctrl $root $CT::Button | Where-Object { $_.Current.ProcessId -ne $proc.Id -and $_.Current.Name -match 'ltool' })
if ($lt.Count -eq 0) { Say 'FAIL: 托盘没有 ltool'; Save-Out; exit 7 }
$lr = $lt[0].Current.BoundingRectangle
$bx = [int]$lr.X - 4; $by = [int]$lr.Y - 4
for ($k = 0; $k -lt 6; $k++) {
  $f = 'D:\project\cpp\ltool\build\verify\t9_' + $k + '.png'
  Shot $bx $by 40 40 $f
  Say ('帧' + $k + ' 绿色像素=' + (Green $f))
  Start-Sleep -Milliseconds 400
}
ClickAt2 $lt[0] '托盘图标' ([IntPtr]::Zero)
Start-Sleep -Milliseconds 2500
$logw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '安装日志' })
Say ('安装日志窗口 = ' + $logw.Count)
if ($logw.Count -eq 0) { Say 'FAIL: 托盘点击没打开看台'; Save-Out; exit 8 }
$cancel = Find (Ctrl $logw[0] $CT::Button) '取消安装'
Say ('取消安装按钮 = ' + $cancel.Count + ' 可用=' + $cancel[0].Current.IsEnabled)
$r = $logw[0].Current.BoundingRectangle
Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\t9_log_before.png'
Inv $cancel[0] '取消安装'
Start-Sleep -Seconds 5
$logw2 = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '安装日志' })
if ($logw2.Count) { $r = $logw2[0].Current.BoundingRectangle; Shot ([int]$r.X) ([int]$r.Y) ([int]$r.Width) ([int]$r.Height) 'D:\project\cpp\ltool\build\verify\t9_log_after.png' }
for ($k = 0; $k -lt 2; $k++) {
  $f = 'D:\project\cpp\ltool\build\verify\t9_after_' + $k + '.png'
  Shot $bx $by 40 40 $f
  Say ('取消后帧' + $k + ' 绿色像素=' + (Green $f))
  Start-Sleep -Milliseconds 400
}
Get-Content "$env:APPDATA\ltool\log.txt" -Tail 8 | ForEach-Object { Say ('  log: ' + $_) }
Say ('CC Switch 目录 = ' + (Test-Path 'D:\software\CC Switch'))
Save-Out
Write-Host 'done'
