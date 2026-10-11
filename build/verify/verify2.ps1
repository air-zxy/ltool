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

$S_PAGE_DISK = [string][char]0x43 + [char]0x76D8 + [char]0x6E05 + [char]0x7406   # C盘清理
$S_PAGE_NET  = [string][char]0x7F51 + [char]0x7EDC                               # 网络
$S_DONE      = [string][char]0x5206 + [char]0x6790 + [char]0x5B8C + [char]0x6210 # 分析完成
$S_DOING     = [string][char]0x5206 + [char]0x6790 + [char]0x4E2D                # 分析中
$S_FEN       = [string][char]0x5206                                             # 分

$report = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $report.Add($s); Write-Host $s }
function Flush-Report {
    [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\pages_dump.txt',
        ($report -join "`r`n"), [System.Text.Encoding]::UTF8)
}

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]

function Find-Children([object]$win, [object]$type) {
    $win.FindAll($TC::Descendants,
        (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $type)))
}
function Click-At([object]$el) {
    $r = $el.Current.BoundingRectangle
    $cx = [int]($r.X + $r.Width / 2)
    $cy = [int]($r.Y + $r.Height / 2)
    $prev = New-Object Win32+PT
    [void][Win32]::GetCursorPos([ref]$prev)
    [Win32]::SetCursorPos($cx, $cy) | Out-Null
    Start-Sleep -Milliseconds 150
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    [Win32]::SetCursorPos($prev.X, $prev.Y) | Out-Null
    Say ("  click (" + $cx + "," + $cy + ") name=" + $el.Current.Name)
}
function Pick-Page([object]$win, [string]$name) {
    foreach ($it in (Find-Children $win $CT::ListItem)) {
        if ($it.Current.Name -eq $name) { Click-At $it; return $true }
    }
    return $false
}
function Shoot([object]$win, [string]$path) {
    $r = $win.Current.BoundingRectangle
    $bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Say ("  shot " + $path + " " + [int]$r.Width + "x" + [int]$r.Height)
}

$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$p = Start-Process -FilePath $exe -ArgumentList '--settings' -PassThru
Say ('pid=' + $p.Id)

$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    if ($p.HasExited) { Say ('PROCESS-EXITED code=0x{0:X8}' -f $p.ExitCode); Flush-Report; exit 1 }
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $p.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
    if ($win) { break }
}
if (-not $win) { Say 'FAIL no window'; Flush-Report; exit 1 }
[Win32]::SetForegroundWindow([IntPtr]$win.Current.NativeWindowHandle) | Out-Null
Start-Sleep -Milliseconds 700
Say 'settings window ok'

# ---- network page ----
if (-not (Pick-Page $win $S_PAGE_NET)) { Say 'FAIL network page'; Flush-Report; exit 1 }
Start-Sleep -Seconds 3
Say '--- adapter list (UIA names) ---'
foreach ($it in (Find-Children $win $CT::ListItem)) {
    if ($it.Current.Name) { Say ('  item: ' + $it.Current.Name) }
}
Say '--- right pane texts ---'
foreach ($tx in (Find-Children $win $CT::Text)) {
    if ($tx.Current.Name) { Say ('  text: ' + $tx.Current.Name.Replace("`n", ' / ')) }
}
Shoot $win 'D:\project\cpp\ltool\build\verify\network_page.png'

# ---- disk page -> analyze ----
if (-not (Pick-Page $win $S_PAGE_DISK)) { Say 'FAIL disk page'; Flush-Report; exit 1 }
Start-Sleep -Seconds 8
$analyze = $null
foreach ($b in (Find-Children $win $CT::Button)) {
    if ($b.Current.Name.Contains($S_FEN)) { $analyze = $b }
}
if (-not $analyze) {
    foreach ($b in (Find-Children $win $CT::Button)) { Say ('  btn: ' + $b.Current.Name) }
    Say 'FAIL analyze button'; Flush-Report; exit 1
}
Say ('analyze button: ' + $analyze.Current.Name)
Click-At $analyze

$deadline = (Get-Date).AddMinutes(14)
$status = ''
$t = 0
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 10
    $t += 10
    if ($p.HasExited) { Say ('FAIL CRASHED t+' + $t + 's code=0x{0:X8}' -f $p.ExitCode); break }
    $st = ''
    $rows = 0
    foreach ($tx in (Find-Children $win $CT::Text)) {
        $nm = $tx.Current.Name
        if ($nm.Contains($S_DONE) -or $nm.Contains($S_DOING)) { $st = $nm }
        if ($nm.StartsWith('C:\')) { $rows++ }
    }
    if ($st -ne $status) { Say ('  t+' + $t + 's ' + $st + '  bar-rows=' + $rows); $status = $st }
    if ($status.Contains($S_DONE)) {
        Say ('PASS analysis finished, process alive, bar-rows=' + $rows)
        Shoot $win 'D:\project\cpp\ltool\build\verify\disk_page.png'
        break
    }
}
Flush-Report
Say ('instance-still-running=' + [bool](Get-Process -Id $p.Id -ErrorAction SilentlyContinue))
Say 'DONE'
