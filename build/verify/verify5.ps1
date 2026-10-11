$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$S_DISK = Ch @(0x43, 0x76D8, 0x6E05, 0x7406)
$S_SCAN = Ch @(0x626B, 0x63CF, 0x5927, 0x5C0F)
$S_FEN  = [string][char]0x5206
$S_DONE = Ch @(0x5206, 0x6790, 0x5B8C, 0x6210)
$S_UP   = Ch @(0x4E0A, 0x4E00, 0x5C42)

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\drill_report.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Kids([object]$w, [object]$t) {
    return $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Raise([object]$w) {
    for ($i = 0; $i -lt 8; $i++) {
        [Win32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
        [Win32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [Win32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([Win32]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Click([object]$w, [object]$el) {
    if (-not (Raise $w)) { throw 'foreground not acquired, refusing to click' }
    $r = $el.Current.BoundingRectangle
    $cx = [int]($r.X + $r.Width / 2); $cy = [int]($r.Y + $r.Height / 2)
    [Win32]::SetCursorPos($cx, $cy) | Out-Null
    Start-Sleep -Milliseconds 200
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    Say ('  click (' + $cx + ',' + $cy + ') [' + $el.Current.Name + ']')
}
function Find-Text([object]$w, [string]$contains) {
    foreach ($t in (Kids $w $CT::Text)) { if ($t.Current.Name.Contains($contains)) { return $t } }
    return $null
}
function Find-Button([object]$w, [string]$name, [bool]$partial) {
    foreach ($b in (Kids $w $CT::Button)) {
        if ($partial) { if ($b.Current.Name.Contains($name)) { return $b } }
        elseif ($b.Current.Name -eq $name) { return $b }
    }
    return $null
}
function Wait-Done([object]$w, [int]$limitSec) {
    $t = 0
    while ($t -lt $limitSec) {
        Start-Sleep -Seconds 5
        $t += 5
        if ($proc.HasExited) { throw ('CRASHED at t+' + $t + 's code=0x' + ('{0:X8}' -f $proc.ExitCode)) }
        $el = Find-Text $w $S_DONE
        if ($el) { Say ('  done at t+' + $t + 's: [' + $el.Current.Name + ']'); return $el.Current.Name }
    }
    throw 'timeout waiting for analysis'
}
function Shoot([object]$w, [string]$path) {
    $r = $w.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    Say ('  saved ' + $path)
}

$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Say ('pid=' + $proc.Id)
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    if ($proc.HasExited) { throw ('exited code=0x' + ('{0:X8}' -f $proc.ExitCode)) }
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
    if ($win) { break }
}
if (-not $win) { throw 'no settings window' }
Say ('window ok, foreground=' + (Raise $win))

# 1) open disk page and run the top-level analysis
$diskItem = $null
foreach ($it in (Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_DISK) { $diskItem = $it } }
if (-not $diskItem) { throw 'disk menu item not found' }
Click $win $diskItem
Start-Sleep -Seconds 6
if (-not (Find-Button $win $S_SCAN $false)) { throw 'disk page not visible' }
Say 'disk page visible'
$an = Find-Button $win $S_FEN $true
if (-not $an) { throw 'analyze button not found' }
Click $win $an
$top = Wait-Done $win 600
Shoot $win 'D:\project\cpp\ltool\build\verify\drill1_root.png'

# 2) drill into the small "boot" folder
$S_PATH = Ch @(0x5F53, 0x524D, 0x76EE, 0x5F55, 0xFF1A)   # "current dir:" label prefix
function Find-PathLabel([object]$w) {
    foreach ($t in (Kids $w $CT::Text)) { if ($t.Current.Name.Contains($S_PATH)) { return $t } }
    return $null
}
$boot = Find-Text $win 'PerfLogs'
if (-not $boot) { throw 'PerfLogs row not found' }
Click $win $boot
Start-Sleep -Seconds 2
$pl = Find-PathLabel $win
if (-not $pl) { throw 'path label missing' }
Say ('path label: [' + $pl.Current.Name + ']')
if ($pl.Current.Name -notlike '*C:\boot*') { throw 'did not drill into C:\boot' }
$sub = Wait-Done $win 300
Shoot $win 'D:\project\cpp\ltool\build\verify\drill2_boot.png'

# 3) go back up
$up = Find-Button $win $S_UP $false
if (-not $up) { throw 'up button not found' }
Click $win $up
Start-Sleep -Seconds 3
$pl2 = Find-PathLabel $win
Say ('after up: [' + $pl2.Current.Name + ']')
if ($pl2.Current.Name -like '*PerfLogs*') { throw 'up did not leave C:\boot' }
Shoot $win 'D:\project\cpp\ltool\build\verify\drill3_up.png'
Say ('alive=' + (-not $proc.HasExited))
Flush
Write-Host 'ALL PASS'
