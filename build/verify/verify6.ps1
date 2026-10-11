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
$S_ORG  = Ch @(0x6574, 0x7406)
$S_DONE = Ch @(0x5206, 0x6790, 0x5B8C, 0x6210)
$S_UP   = Ch @(0x4E0A, 0x4E00, 0x5C42)
$S_PATH = Ch @(0x5F53, 0x524D, 0x76EE, 0x5F55, 0xFF1A)

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\v6.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Get-Kids([object]$w, [object]$t) {
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Invoke-Raise([object]$w) {
    for ($i = 0; $i -lt 10; $i++) {
        [Win32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
        [Win32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [Win32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 300
        if ([Win32]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Invoke-Click([object]$w, [object]$el) {
    if (-not (Invoke-Raise $w)) { throw 'foreground not acquired' }
    $r = $el.Current.BoundingRectangle
    [Win32]::SetCursorPos([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)) | Out-Null
    Start-Sleep -Milliseconds 200
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    Say ('  clicked [' + $el.Current.Name + ']')
}
function Find-Text([object]$w, [string]$has) {
    foreach ($t in (Get-Kids $w $CT::Text)) { if ($t.Current.Name.Contains($has)) { return $t } }
    return $null
}
function Count-Type([object]$w, [object]$t) { return (Get-Kids $w $t).Count }
function Shoot([object]$w, [string]$name) {
    $r = $w.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save('D:\project\cpp\ltool\build\verify\' + $name, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Say ('  saved ' + $name)
}
function Wait-Analysis([object]$w, [int]$limitSec) {
    $t = 0
    while ($t -lt $limitSec) {
        Start-Sleep -Seconds 5
        $t += 5
        if ($proc.HasExited) { throw ('CRASHED t+' + $t + 's') }
        $el = Find-Text $w $S_DONE
        if ($el) { Say ('  analysis done t+' + $t + 's: [' + $el.Current.Name + ']'); return $true }
    }
    return $false
}

if (Get-Process ltool -ErrorAction SilentlyContinue) { throw 'another ltool running' }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Say ('pid=' + $proc.Id)
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    if ($proc.HasExited) { throw 'exited early' }
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
    if ($win) { break }
}
if (-not $win) { throw 'no window' }
Start-Sleep -Seconds 2

# 1) enter the page: nothing must auto-scan
$d = $null
foreach ($it in (Get-Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_DISK) { $d = $it } }
if (-not $d) { throw 'disk page entry missing' }
Invoke-Click $win $d
Start-Sleep -Seconds 8
$checks = Count-Type $win $CT::CheckBox
Say ('on entering page: checkbox rows = ' + $checks + '  (expect 0)')
$hint = Find-Text $win $S_ORG
Say ('idle hint present = ' + [bool]$hint + ' [' + $hint.Current.Name + ']')
Shoot $win 'v6_idle.png'
if ($checks -gt 0) { Say 'FAIL: page still auto-scans'; Flush; $proc.Kill(); exit 1 }

# 2) press 整理
$org = $null
foreach ($b in (Get-Kids $win $CT::Button)) { if ($b.Current.Name -eq $S_ORG) { $org = $b } }
if (-not $org) { throw 'organize button missing' }
Invoke-Click $win $org
Start-Sleep -Seconds 6
Say ('after 整理: checkbox rows = ' + (Count-Type $win $CT::CheckBox))
$ok = Wait-Analysis $win 600
Say ('analysis completed = ' + $ok)
Shoot $win 'v6_root.png'
Flush
if (-not $ok) { $proc.Kill(); exit 1 }

# 3) drill into PerfLogs, then go back up
$row = Find-Text $win 'PerfLogs'
if (-not $row) { Say 'FAIL PerfLogs row'; $proc.Kill(); Flush; exit 1 }
Invoke-Click $win $row
Start-Sleep -Seconds 2
$pl = Find-Text $win $S_PATH
Say ('path now: [' + $pl.Current.Name + ']')
$ok2 = Wait-Analysis $win 240
Shoot $win 'v6_drill.png'
$up = $null
foreach ($b in (Get-Kids $win $CT::Button)) { if ($b.Current.Name -eq $S_UP) { $up = $b } }
Invoke-Click $win $up
Start-Sleep -Seconds 3
$pl2 = Find-Text $win $S_PATH
Say ('after 上一层: [' + $pl2.Current.Name + ']')
Shoot $win 'v6_up.png'
Say ('alive=' + (-not $proc.HasExited))
Flush
