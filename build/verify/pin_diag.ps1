$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$root = $AE::RootElement
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Warp([int]$x, [int]$y) { [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 120 }
function Down { [W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60 }
function Up   { [W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 250 }
function Key([byte]$vk) { [W32]::keybd_event($vk,0,0,[IntPtr]::Zero); [W32]::keybd_event($vk,0,2,[IntPtr]::Zero); Start-Sleep -Milliseconds 120 }

$proc = Get-Process ltool -ErrorAction Stop
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
Key 0x70
Start-Sleep -Milliseconds 1600
$wins = @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ })
Say ('top windows = ' + $wins.Count)
foreach ($w in $wins) {
  $r = $w.Current.BoundingRectangle
  Say ('window hwnd=' + $w.Current.NativeWindowHandle + ' rect=' + [int]$r.X + ',' + [int]$r.Y + ' ' + [int]$r.Width + 'x' + [int]$r.Height)
  $all = $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::IsControlElementProperty, $true)))
  Say ('  descendants = ' + $all.Count)
  foreach ($e in $all) {
    $er = $e.Current.BoundingRectangle
    Say ('   [' + $e.Current.ControlType.ProgrammaticName + '] "' + $e.Current.Name + '" @ ' + [int]$er.X + ',' + [int]$er.Y + ' ' + [int]$er.Width + 'x' + [int]$er.Height)
  }
}
# do the selection drag, then dump again
Warp 700 380; Down; Warp 1150 700; Up
Start-Sleep -Milliseconds 1200
$wins = @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ })
Say ('--- after drag: top windows = ' + $wins.Count)
foreach ($w in $wins) {
  $r = $w.Current.BoundingRectangle
  Say ('window hwnd=' + $w.Current.NativeWindowHandle + ' rect=' + [int]$r.X + ',' + [int]$r.Y + ' ' + [int]$r.Width + 'x' + [int]$r.Height)
  $all = $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::IsControlElementProperty, $true)))
  Say ('  descendants = ' + $all.Count)
  foreach ($e in $all) {
    $er = $e.Current.BoundingRectangle
    Say ('   [' + $e.Current.ControlType.ProgrammaticName + '] "' + $e.Current.Name + '" @ ' + [int]$er.X + ',' + [int]$er.Y + ' ' + [int]$er.Width + 'x' + [int]$er.Height)
  }
}
$vs = $root.FindAll($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
if ($vs.Count) {
  $r = $vs[0].Current.BoundingRectangle
  $bmp = [System.Drawing.Bitmap]::new([int]$r.Width, [int]$r.Height)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
  $g.Dispose()
  $bmp.Save('D:\project\cpp\ltool\build\verify\pin_diag.png', [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
}
Key 0x1B
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\pin_diag.txt', $out, (New-Object System.Text.UTF8Encoding($true)))
Write-Host 'done'
