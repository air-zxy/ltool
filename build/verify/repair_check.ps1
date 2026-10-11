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
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$S_FIX   = Ch @(0x4FEE, 0x590D)                 # repair nav item
$S_ALL   = Ch @(0x4E00, 0x952E, 0x4FEE, 0x590D) # "one-click fix" button
$S_NO    = Ch @(0x5426)                         # "No" button
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = [System.Windows.Automation.AutomationElement]::RootElement
function Say([string]$s) { Write-Host ('| ' + $s) }
function Kids([object]$w, [object]$t) { return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t))) }
function Foreground([object]$w) {
    for ($i = 0; $i -lt 8; $i++) {
        [W32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero); [W32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [W32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 250
        if ([W32]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Click([object]$el) {
    $r = $el.Current.BoundingRectangle
    [W32]::SetCursorPos([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)) | Out-Null
    Start-Sleep -Milliseconds 150
    [W32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 60
    [W32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 500
    Say ('  clicked [' + $el.Current.Name + ']')
}
function Shoot([object]$w, [string]$name) {
    $r = $w.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose(); $bmp.Save('D:\project\cpp\ltool\build\verify\' + $name, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Say ('  saved ' + $name)
}

$old = Get-Process ltool -ErrorAction SilentlyContinue
if ($old) { Stop-Process -Id $old.Id -Force; Start-Sleep -Seconds 2 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 600
    if ($proc.HasExited) { throw 'exited early' }
    $win = $root.FindFirst($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    if ($win) { break }
}
if (-not $win) { throw 'no settings window' }
if (-not (Foreground $win)) { Say 'FAIL: no foreground, aborting without clicks'; exit 3 }
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_FIX) { Click $it; break } }
Start-Sleep -Seconds 3
[void](Foreground $win)

$btn = $null
foreach ($b in (Kids $win $CT::Button)) { if ($b.Current.Name -like ($S_ALL + '*')) { $btn = $b; break } }
if (-not $btn) { Say 'FAIL: 一键修复 button missing'; exit 4 }
Say ('button = [' + $btn.Current.Name + ']  enabled = ' + $btn.Current.Enabled)
foreach ($t in (Kids $win $CT::Text)) { if ($t.Current.Name.Length -gt 6) { Say ('TEXT: ' + $t.Current.Name) } }
Shoot $win 'repair_page.png'

if ($btn.Current.Enabled) {
    Click $btn
    Start-Sleep -Milliseconds 900
    $no = $null
    foreach ($b in (Kids $win $CT::Button)) { if ($b.Current.Name.StartsWith($S_NO)) { $no = $b; break } }
    if ($no) {
        Say 'confirm dialog appeared -> clicking 否 (no install will run)'
        Click $no
    } else {
        Say 'WARN: no confirm dialog found'
    }
}
Start-Sleep -Milliseconds 600
Say ('winget running = ' + [bool](Get-Process winget -ErrorAction SilentlyContinue))
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2 }
Start-Process 'D:\software\ltool\ltool.exe' | Out-Null
Start-Sleep -Seconds 3
Say ('relaunched installed pid=' + ((Get-Process ltool -ErrorAction SilentlyContinue).Id -join ','))
