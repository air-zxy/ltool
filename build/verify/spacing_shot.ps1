$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class WR {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$prev = Get-Process ltool -ErrorAction SilentlyContinue
if ($prev) { Stop-Process -Id $prev.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 8
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
$sw = @($root.FindAll($TC::Children, $cond) | Where-Object { $_.Current.Name -match '设置' })
if ($sw.Count -eq 0) { Write-Host 'FAIL: 设置窗口没开'; exit 5 }
$h = [IntPtr]$sw[0].Current.NativeWindowHandle
[WR]::keybd_event(0x12,0,0,[IntPtr]::Zero); [WR]::keybd_event(0x12,0,2,[IntPtr]::Zero)
[WR]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 700
Write-Host ('前台 = ' + [WR]::GetForegroundWindow() + ' 期望 = ' + $h)
$r = $sw[0].Current.BoundingRectangle
$bmp = [System.Drawing.Bitmap]::new([int]$r.Width, [int]$r.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save('D:\project\cpp\ltool\build\verify\list_spacing.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host 'saved list_spacing.png'
