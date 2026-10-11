$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out PT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int X; public int Y; }
}
"@

# ASCII-only source: PS 5.1 reads BOM-less files as ANSI and trailing CJK comments can eat the newline
$S_PAGE_DISK = [string][char]0x43 + [char]0x76D8 + [char]0x6E05 + [char]0x7406
$S_PAGE_NET  = [string][char]0x7F51 + [char]0x7EDC
$S_DONE      = [string][char]0x5206 + [char]0x6790 + [char]0x5B8C + [char]0x6210
$S_DOING     = [string][char]0x5206 + [char]0x6790 + [char]0x4E2D
$S_FEN       = [string][char]0x5206

$report = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $report.Add($s); Write-Host $s }
function Flush-Report {
    [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\report.txt',
        ($report -join "`r`n"), [System.Text.Encoding]::UTF8)
}

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]

function Get-Children([object]$w, [object]$type) {
    return $w.FindAll($TC::Descendants,
        (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type)))
}
function Click-At([object]$el) {
    $r = $el.Current.BoundingRectangle
    $cx = [int]($r.X + $r.Width / 2)
    $cy = [int]($r.Y + $r.Height / 2)
    $prev = New-Object Win32+PT
    [void][Win32]::GetCursorPos([ref]$prev)
    [Win32]::SetCursorPos($cx, $cy) | Out-Null
    Start-Sleep -Milliseconds 200
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    [Win32]::SetCursorPos($prev.X, $prev.Y) | Out-Null
    Say ('  click (' + $cx + ',' + $cy + ') name=' + $el.Current.Name)
}
function Pick-Menu([object]$w, [string]$name) {
    $kids = Get-Children $w $CT::ListItem
    for ($i = 0; $i -lt $kids.Count; $i++) {
        if ($kids[$i].Current.Name -eq $name) { Click-At $kids[$i]; return $true }
    }
    return $false
}
function Shoot([object]$w, [string]$path) {
    $r = $w.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Say ('  saved ' + $path)
}

$p = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--settings' -PassThru
Say ('pid=' + $p.Id)
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    if ($p.HasExited) { Say ('EXITED code=0x{0:X8}' -f $p.ExitCode); Flush-Report; exit 1 }
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $p.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
    if ($win) { break }
}
if (-not $win) { Say 'FAIL no window'; Flush-Report; exit 1 }
[Win32]::SetForegroundWindow([IntPtr]$win.Current.NativeWindowHandle) | Out-Null
Start-Sleep -Milliseconds 800
Say 'window ok'

if (-not (Pick-Menu $win $S_PAGE_NET)) { Say 'FAIL net menu'; Flush-Report; exit 1 }
Start-Sleep -Seconds 3
Shoot $win 'D:\project\cpp\ltool\build\verify\net2.png'

if (-not (Pick-Menu $win $S_PAGE_DISK)) { Say 'FAIL disk menu'; Flush-Report; exit 1 }
Start-Sleep -Seconds 8
$analyze = $null
$btns = Get-Children $win $CT::Button
for ($i = 0; $i -lt $btns.Count; $i++) {
    Say ('  btn[' + $i + '] ' + $btns[$i].Current.Name)
    if ($btns[$i].Current.Name.Contains($S_FEN)) { $analyze = $btns[$i] }
}
if (-not $analyze) { Say 'FAIL analyze btn'; Flush-Report; exit 1 }
Click-At $analyze

$status = ''
$t = 0
$rows = 0
while ($t -lt 840) {
    Start-Sleep -Seconds 10
    $t += 10
    if ($p.HasExited) { Say ('CRASHED t+' + $t + 's code=0x{0:X8}' -f $p.ExitCode); break }
    $st = ''
    $cnt = 0
    $txts = Get-Children $win $CT::Text
    for ($i = 0; $i -lt $txts.Count; $i++) {
        $nm = $txts[$i].Current.Name
        if ($nm.Contains($S_DONE) -or $nm.Contains($S_DOING)) { $st = $nm }
        if ($nm.StartsWith('C:\')) { $cnt++ }
    }
    $rows = $cnt
    if ($st -ne $status) { Say ('  t+' + $t + 's ' + $st + ' rows=' + $cnt); $status = $st }
    if ($status.Contains($S_DONE)) { Say ('PASS analysis done, alive, rows=' + $cnt); Shoot $win 'D:\project\cpp\ltool\build\verify\disk2.png'; break }
}
Flush-Report
Say ('instance-running=' + [bool](Get-Process -Id $p.Id -ErrorAction SilentlyContinue))
