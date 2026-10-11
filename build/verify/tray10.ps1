$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct PT { public int x; public int y; }
public static class WA {
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
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\tray10.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
function Shot([int]$x,[int]$y,[int]$w,[int]$h,[string]$name) {
  $bmp = [System.Drawing.Bitmap]::new([Math]::Max(1,$w), [Math]::Max(1,$h))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
function Inv($e, [string]$what) {
  $p = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern); $p.Invoke(); Say ('invoke ' + $what)
}
function ClickIn($e, [string]$what, [IntPtr]$owner) {
  $r = $e.Current.BoundingRectangle
  $x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
  $pt = New-Object PT; $pt.x = $x; $pt.y = $y
  $at = [WA]::GetAncestor([WA]::WindowFromPoint($pt), 2)
  if ($owner -ne [IntPtr]::Zero -and $at -ne $owner) {
    [WA]::keybd_event(0x12,0,0,[IntPtr]::Zero); [WA]::keybd_event(0x12,0,2,[IntPtr]::Zero)
    [WA]::SetForegroundWindow($owner) | Out-Null; Start-Sleep -Milliseconds 400
    $at = [WA]::GetAncestor([WA]::WindowFromPoint($pt), 2)
  }
  if ($owner -eq [IntPtr]::Zero) {
    $wp = 0; [WA]::GetWindowThreadProcessId($at, [ref]$wp) | Out-Null
    if ([int]$wp -ne [int](Get-Process explorer).Id) { Say ("中止：" + $what + " 不属于 explorer"); throw 'owner' }
  } elseif ($at -ne $owner) { Say ("中止：" + $what + " 被挡住"); throw 'occluded' }
  [WA]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120
  [WA]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [WA]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 250
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
ClickIn (Find (Ctrl $sw[0] $CT::ListItem) '^软件$')[0] '软件导航' $swH
Start-Sleep -Milliseconds 1500
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
$row = Find (Ctrl $sw[0] $CT::Text) '^7-Zip'
$rr = $row[0].Current.BoundingRectangle
$dl = @(Ctrl $sw[0] $CT::Button | Where-Object { $_.Current.Name -eq '下载' -and [Math]::Abs($_.Current.BoundingRectangle.Y - $rr.Y) -lt 40 })
Say ('7-Zip 下载按钮 = ' + $dl.Count)

# 先把隐藏图标面板打开，并锁定我们自己的托盘图标元素（矩形很小、名字含 ltool）
ClickIn @(Find (Ctrl $root $CT::Button) '隐藏的图标')[0] '托盘溢出' ([IntPtr]::Zero)
Start-Sleep -Milliseconds 900
$cand = @(Ctrl $root $CT::Button | Where-Object { $_.Current.ProcessId -ne $proc.Id -and $_.Current.Name -match 'ltool' -and $_.Current.BoundingRectangle.Width -lt 48 -and $_.Current.BoundingRectangle.Y -lt 1390 })
Say ('候选托盘图标 = ' + $cand.Count)
foreach ($c in $cand) { Say ('   [' + $c.Current.Name + '] @ ' + [int]$c.Current.BoundingRectangle.X + ',' + [int]$c.Current.BoundingRectangle.Y) }
if ($cand.Count -eq 0) { Say 'FAIL: 找不到托盘图标元素'; Save-Out; exit 7 }
$icon = $cand[0]
$ir = $icon.Current.BoundingRectangle
Say ('用图标元素 @ ' + [int]$ir.X + ',' + [int]$ir.Y + ' 名字=[' + $icon.Current.Name + ']')
Inv $dl[0] '下载(7-Zip)'
$seen = @{}
for ($k = 0; $k -lt 60; $k++) {
  $nm = ''
  try { $nm = $icon.GetCurrentPropertyValue($AE::NameProperty) } catch { $nm = '<gone>' }
  if (-not $seen.ContainsKey([string]$nm)) {
    $seen[[string]$nm] = $k
    Say ('  +' + ($k*100) + 'ms 名字=[' + $nm + ']')
    if ($nm -match '安装') {
      Shot ([int]$ir.X - 4) ([int]$ir.Y - 4) 40 40 'D:\project\cpp\ltool\build\verify\t10_busy.png'
    }
  }
  Start-Sleep -Milliseconds 100
}
Say ('共看到 ' + $seen.Count + ' 种名字；带"安装"的帧 = ' + [bool]($seen.Keys -match '安装'))
Save-Out
Write-Host 'done'
