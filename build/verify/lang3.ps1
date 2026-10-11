$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct PT { public int x; public int y; }
public static class WM {
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
function Save-Out { [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\lang3.txt', $out, (New-Object System.Text.UTF8Encoding($true))) }
trap { Save-Out; break }
function Ctrl($el, $type) { return @($el.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type))) | ForEach-Object { $_ }) }
function Find($list, [string]$re) { return @($list | Where-Object { $_.Current.Name -match $re }) }
$proc = Get-Process ltool -ErrorAction Stop
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
if ($sw.Count -eq 0) { Say 'FAIL: 设置窗口没开'; Save-Out; exit 5 }
$swH = [IntPtr]$sw[0].Current.NativeWindowHandle
[WM]::keybd_event(0x12,0,0,[IntPtr]::Zero); [WM]::keybd_event(0x12,0,2,[IntPtr]::Zero)
[WM]::SetForegroundWindow($swH) | Out-Null
Start-Sleep -Milliseconds 600
if ([WM]::GetForegroundWindow() -ne $swH) { Say 'FAIL: 抬不起设置窗口，不点'; Save-Out; exit 6 }
$chip = @(Find (Ctrl $sw[0] $CT::CheckBox) '③ 配好环境变量')
Say ('chip = ' + $chip.Count)
$r = $chip[0].Current.BoundingRectangle
$x = [int]($r.X + $r.Width/2); $y = [int]($r.Y + $r.Height/2)
$pt = New-Object PT; $pt.x = $x; $pt.y = $y
$at = [WM]::GetAncestor([WM]::WindowFromPoint($pt), 2)
if ($at -ne $swH) { Say ("FAIL: (" + $x + "," + $y + ") 上是 " + $at); Save-Out; exit 7 }
[WM]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 150
[WM]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
[WM]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 1200
Say 'clicked 步骤③'
$sw = @(Ctrl $root $CT::Window | Where-Object { $_.Current.ProcessId -eq $proc.Id -and $_.Current.Name -match '设置' })
$bmp = [System.Drawing.Bitmap]::new(1140, 790)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$rr = $sw[0].Current.BoundingRectangle
$g.CopyFromScreen([int]$rr.X, [int]$rr.Y, 0, 0, $bmp.Size)
$g.Dispose(); $bmp.Save('D:\project\cpp\ltool\build\verify\lang3_step3.png', [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
Say 'saved lang3_step3.png'
# 顺便读一下版本下拉里还有几个条目（JBR/JRE 应该被排除）
Say '第3步文本：'
foreach ($t in (Find (Ctrl $sw[0] $CT::Text) 'PATH|当前使用')) { Say ('  ' + $t.Current.Name) }
Save-Out
Write-Host 'done'
