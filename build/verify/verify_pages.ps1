$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
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

$S_PAGE_DISK  = [string][char]0x43 + [char]0x76D8 + [char]0x6E05 + [char]0x7406   # C盘清理
$S_PAGE_NET   = [string][char]0x7F51 + [char]0x7EDC                              # 网络
$S_DONE       = [string][char]0x5206 + [char]0x6790 + [char]0x5B8C + [char]0x6210 # 分析完成
$S_DOING      = [string][char]0x5206 + [char]0x6790 + [char]0x4E2D                # 分析中
$S_FEN        = [string][char]0x5206                                             # 分
$report = [System.Collections.Generic.List[string]]::new()
function Rep([string]$s) { $report.Add($s); Write-Host $s }

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
function Find-All([object]$win, [object]$ct) {
    $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition(
        $AE::ControlTypeProperty, $ct)))
}
function Click([object]$el) {
    $r = $el.Current.BoundingRectangle
    $cx = [int]($r.X + $r.Width / 2); $cy = [int]($r.Y + $r.Height / 2)
    $old = New-Object Win32+PT
    [void][Win32]::GetCursorPos([ref]$old)
    [Win32]::SetCursorPos($cx, $cy) | Out-Null
    Start-Sleep -Milliseconds 150
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    [Win32]::SetCursorPos($old.X, $old.Y) | Out-Null
    Rep ("  click at ($cx,$cy) rect=" + [int]$r.X + "," + [int]$r.Y + " " + $el.Current.Name)
}
function Select-Page([object]$win, [string]$name) {
    foreach ($it in (Find-All $win ([System.Windows.Automation.ControlType]::ListItem))) {
        if ($it.Current.Name -eq $name) { Click $it; return $true }
    }
    return $false
}

$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$p = Start-Process -FilePath $exe -ArgumentList '--settings' -PassThru
Rep ('pid=' + $p.Id)

$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    if ($p.HasExited) { Rep ('PROCESS-EXITED code=0x{0:X8}' -f $p.ExitCode); break }
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $p.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
    if ($win) { break }
}
if (-not $win) { Rep 'FAIL no settings window'; exit 1 }
[Win32]::SetForegroundWindow([IntPtr]$win.Current.NativeWindowHandle) | Out-Null
Start-Sleep -Milliseconds 600
Rep 'settings window ok'

# ---- phase 1: C盘清理 -> 分析 ----
if (-not (Select-Page $win $S_PAGE_DISK)) { Rep 'FAIL disk page not found'; Stop-Process -Id $p.Id -Force; exit 1 }
Start-Sleep -Seconds 8
$analyze = $null
foreach ($b in (Find-All $win ([System.Windows.Automation.ControlType]::Button))) {
    if ($b.Current.Name -eq $S_FEN -or $b.Current.Name.Contains($S_FEN)) { $analyze = $b }
}
if (-not $analyze) {
    foreach ($b in (Find-All $win ([System.Windows.Automation.ControlType]::Button))) { Rep ('  btn: ' + $b.Current.Name) }
    Rep 'FAIL analyze button not found'; Stop-Process -Id $p.Id -Force; exit 1
}
Rep ('analyze button: ' + $analyze.Current.Name)
Click $analyze

$deadline = (Get-Date).AddMinutes(14)
$status = ''; $rows = 0; $t0 = 0
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 10
    $t0 += 10
    if ($p.HasExited) { Rep ('FAIL CRASHED at t+' + $t0 + 's code=0x{0:X8}' -f $p.ExitCode); break }
    $st = ''; $cnt = 0
    foreach ($tx in (Find-All $win ([System.Windows.Automation.ControlType]::Text))) {
        $nm = $tx.Current.Name
        if ($nm.Contains($S_DONE) -or $nm.Contains($S_DOING)) { $st = $nm }
        if ($nm.StartsWith('C:\')) { $cnt++ }
    }
    if ($st -ne $status) { Rep ('  t+' + $t0 + 's ' + $st + '  bar-rows=' + $cnt); $status = $st }
    $rows = $cnt
    if ($status.Contains($S_DONE)) { Rep ('PASS analysis finished, process alive, bar-rows=' + $rows); break }
}

# ---- phase 2: 网络 ----
if (-not (Select-Page $win $S_PAGE_NET)) { Rep 'FAIL network page not found' }
else {
    Start-Sleep -Seconds 3
    Rep '--- network page: list items ---'
    foreach ($it in (Find-All $win ([System.Windows.Automation.ControlType]::ListItem))) {
        if ($it.Current.Name) { Rep ('  item: ' + $it.Current.Name) }
    }
    Rep '--- network page: texts ---'
    foreach ($tx in (Find-All $win ([System.Windows.Automation.ControlType]::Text))) {
        $nm = $tx.Current.Name
        if ($nm) { Rep ('  text: ' + $nm.Replace("`n", ' / ')) }
    }
    Rep '--- network page: buttons ---'
    foreach ($b in (Find-All $win ([System.Windows.Automation.ControlType]::Button))) {
        Rep ('  btn: ' + $b.Current.Name)
    }
}
Stop-Process -Id $p.Id -Force
[System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\pages_dump.txt',
    ($report -join "`r`n"), [System.Text.Encoding]::UTF8)
Rep 'written pages_dump.txt'
