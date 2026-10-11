$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int X; public int Y; }
}
"@
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$S_DISK = Ch @(0x43, 0x76D8, 0x6E05, 0x7406)
$S_ORG  = Ch @(0x6574, 0x7406)
$S_TOTAL = Ch @(0x5408, 0x8BA1)          # "total"
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\ui6.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Get-Kids([object]$w, [object]$t) {
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Click-IfForeground([object]$w, [object]$el) {
    if ([Win32]::GetForegroundWindow() -ne [IntPtr]$w.Current.NativeWindowHandle) {
        Say '  skipped: settings window is not foreground (no focus stealing)'
        return $false
    }
    $r = $el.Current.BoundingRectangle
    $prev = New-Object Win32+PT
    [void][Win32]::GetCursorPos([ref]$prev)
    [Win32]::SetCursorPos([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)) | Out-Null
    Start-Sleep -Milliseconds 200
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    [Win32]::SetCursorPos($prev.X, $prev.Y) | Out-Null
    Say ('  clicked [' + $el.Current.Name + ']')
    return $true
}
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

if (Get-Process ltool -ErrorAction SilentlyContinue) { throw 'another ltool running' }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Say ('pid=' + $proc.Id)
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 600
    if ($proc.HasExited) { throw 'exited early' }
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
    if ($win) { break }
}
if (-not $win) { throw 'no window' }
Say ('foreground = ' + ([Win32]::GetForegroundWindow() -eq [IntPtr]$win.Current.NativeWindowHandle))

$d = $null
foreach ($it in (Get-Kids $win $CT::ListItem)) { if ($it.Current.Name -eq $S_DISK) { $d = $it } }
if (-not $d) { throw 'disk entry missing' }
if (-not (Click-IfForeground $win $d)) { Shoot $win 'ui6_failed.png'; Flush; exit 2 }
Start-Sleep -Seconds 6
Say ('checkbox rows on entering page = ' + (Get-Kids $win $CT::CheckBox).Count + ' (expect 0)')
Shoot $win 'ui6_idle.png'

$org = $null
foreach ($b in (Get-Kids $win $CT::Button)) { if ($b.Current.Name -eq $S_ORG) { $org = $b } }
if (-not $org) { throw 'organize button missing' }
if (-not (Click-IfForeground $win $org)) { Flush; exit 2 }
$t = 0
while ($t -lt 600) {
    Start-Sleep -Seconds 5
    $t += 5
    if ($proc.HasExited) { throw ('CRASHED t+' + $t) }
    $st = ''
    foreach ($x in (Get-Kids $win $CT::Text)) { if ($x.Current.Name.Contains($S_TOTAL)) { $st = $x.Current.Name } }
    if ($st) { Say ('t+' + $t + 's status [' + $st + ']'); break }
}
Start-Sleep -Seconds 3
Shoot $win 'ui6_root.png'
Say ('checkbox rows after = ' + (Get-Kids $win $CT::CheckBox).Count)
Flush
Write-Host 'done'
