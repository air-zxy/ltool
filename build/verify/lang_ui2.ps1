$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct PT { public int x; public int y; }
public static class WL {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
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
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\langui2.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
function Shot($w, [string]$name) {
  $h = [IntPtr]$w.Current.NativeWindowHandle
  [WL]::keybd_event(0x12,0,0,[IntPtr]::Zero); [WL]::keybd_event(0x12,0,2,[IntPtr]::Zero)
  [WL]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 500
  if ([WL]::GetForegroundWindow() -ne $h) { Say ("警告：抬不起窗口，" + $name + " 可能拍到别人的界面") }
  $r = $w.Current.BoundingRectangle
  $bmp = [System.Drawing.Bitmap]::new([int]$r.Width, [int]$r.Height)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  Say ('saved ' + (Split-Path $name -Leaf))
}
# 点之前把设置窗口抬到前台，并确认坐标上的根窗口就是它
function ClickEl($e, [string]$what, [IntPtr]$owner) {
  $r = $e.Current.BoundingRectangle
  if ($r.Width -le 0) { Say ("FAIL: " + $what + " 没有矩形"); throw 'no rect' }
  $x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
  $pt = New-Object PT; $pt.x = $x; $pt.y = $y
  $at = [WL]::GetAncestor([WL]::WindowFromPoint($pt), 2)
  if ($at -ne $owner) {
    [WL]::keybd_event(0x12,0,0,[IntPtr]::Zero); [WL]::keybd_event(0x12,0,2,[IntPtr]::Zero)
    [WL]::SetForegroundWindow($owner) | Out-Null; Start-Sleep -Milliseconds 400
    $at = [WL]::GetAncestor([WL]::WindowFromPoint($pt), 2)
  }
  if ($at -ne $owner) { Say ("中止：" + $what + " 被别的窗口挡住"); throw 'occluded' }
  [WL]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120
  [WL]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
  [WL]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 300
  Say ("click " + $what + " @ " + $x + "," + $y)
}
function Chips($sw) { return @(Ctrl $sw $CT::CheckBox | Where-Object { $_.Current.Name -match '^[①②③]' }) }
# 步骤 chip 是 checkable 的 → UIA 里是 CheckBox，走 Toggle/Invoke，不碰鼠标
function PressChip($e, [string]$what) {
  try {
    $t = $e.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern)
    $t.Toggle(); Say ('toggle ' + $what); return
  } catch { Say ('toggle 不行: ' + $what) }
  try {
    $p = $e.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
    $p.Invoke(); Say ('invoke ' + $what); return
  } catch { Say ('invoke 不行: ' + $what) }
  ClickEl $e $what ([IntPtr]$script:swH)
}

$prev = Get-Process ltool -ErrorAction SilentlyContinue
if ($prev) { Stop-Process -Id $prev.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 8
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
$swH = [IntPtr]$sw[0].Current.NativeWindowHandle
$script:swH = $swH
Say ('窗口 = ' + $sw[0].Current.Name)
$cs = Chips $sw[0]
Say ('步骤 chip 数 = ' + $cs.Count)
foreach ($c in $cs) { Say ('  chip [' + $c.Current.Name + ']') }
Shot $sw[0] 'D:\project\cpp\ltool\build\verify\lang2_step1.png'
PressChip $cs[1] '步骤②'
Start-Sleep -Milliseconds 900
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
Shot $sw[0] 'D:\project\cpp\ltool\build\verify\lang2_step2.png'
$cs = Chips $sw[0]
PressChip $cs[2] '步骤③'
Start-Sleep -Milliseconds 900
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
Shot $sw[0] 'D:\project\cpp\ltool\build\verify\lang2_step3.png'
Save-Out
Write-Host 'done'
