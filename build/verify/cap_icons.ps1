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
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = [System.Windows.Automation.AutomationElement]::RootElement
function Say([string]$s) { Write-Host ('| ' + $s) }
function Warp([int]$x, [int]$y) { [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120 }
function Down { [W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60 }
function Up   { [W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 250 }
function Key([byte]$vk) { [W32]::keybd_event($vk,0,0,[IntPtr]::Zero); [W32]::keybd_event($vk,0,2,[IntPtr]::Zero); Start-Sleep -Milliseconds 120 }

$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -PassThru
Start-Sleep -Seconds 4
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Key 0x70            # F1 = 截图
Start-Sleep -Milliseconds 1500
if ($root.FindAll($TC::Children, $cond).Count -eq 0) { Say 'FAIL: overlay did not open'; exit 2 }
Warp 700 380; Down
Warp 900 500; Warp 1150 700; Up
Start-Sleep -Milliseconds 900
$btns = $root.FindAll($TC::Children, $cond)
$items = @()
foreach ($w in $btns) {
    $found = $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Button)))
    foreach ($f in $found) { $items += $f }
}
if ($items.Count -eq 0) { Say 'FAIL: no toolbar buttons found'; Key 0x1B; exit 3 }
$rects = @($items | ForEach-Object { $_.Current.BoundingRectangle })
$x1 = [int](($rects | ForEach-Object { $_.X } | Measure-Object -Minimum).Minimum)
$y1 = [int](($rects | ForEach-Object { $_.Y } | Measure-Object -Minimum).Minimum)
$x2 = [int](($rects | ForEach-Object { $_.X + $_.Width } | Measure-Object -Maximum).Maximum)
$y2 = [int](($rects | ForEach-Object { $_.Y + $_.Height } | Measure-Object -Maximum).Maximum)
Say ('toolbar bbox = ' + $x1 + ',' + $y1 + ' .. ' + $x2 + ',' + $y2)
$pad = 14
$x1 = 660; $y1 = 694; $x2 = 1180; $y2 = 746   # 固定覆盖整条工具栏（选区固定拖在 700,380-1150,700）
$w = ($x2 - $x1)
$h = ($y2 - $y1)
$bmp = [System.Drawing.Bitmap]::new([int]$w, [int]$h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($x1, $y1, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save('D:\project\cpp\ltool\build\verify\cap_toolbar.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Say ('saved cap_toolbar.png  (' + (($x2 - $x1) + 2 * $pad) + 'x' + (($y2 - $y1) + 2 * $pad) + ')')
Key 0x1B
Start-Sleep -Milliseconds 500
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process 'D:\software\ltool\ltool.exe' | Out-Null
Start-Sleep -Seconds 3
Say ('relaunched installed pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
