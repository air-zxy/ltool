$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
}
"@
$S_PAGE_DISK = [string][char]0x43 + [char]0x76D8 + [char]0x6E05 + [char]0x7406
$S_DONE  = [string][char]0x5206 + [char]0x6790 + [char]0x5B8C + [char]0x6210
$S_DOING = [string][char]0x5206 + [char]0x6790 + [char]0x4E2D
$S_FEN   = [string][char]0x5206
$S_SCAN  = [string][char]0x626B + [char]0x63CF + [char]0x5927 + [char]0x5C0F

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\disk_report.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Kids([object]$w, [object]$t) {
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Raise([object]$w) {
    [Win32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 400
}
function Click-At([object]$w, [object]$el) {
    Raise $w
    $r = $el.Current.BoundingRectangle
    $cx = [int]($r.X + $r.Width / 2); $cy = [int]($r.Y + $r.Height / 2)
    [Win32]::SetCursorPos($cx, $cy) | Out-Null
    Start-Sleep -Milliseconds 200
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    Say ('  click (' + $cx + ',' + $cy + ') name=' + $el.Current.Name + ' fg=' + [Win32]::GetForegroundWindow() + ' hwnd=' + $w.Current.NativeWindowHandle)
}

$proc = Get-Process ltool -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Say 'no instance'; Flush; exit 1 }
Say ('pid=' + $proc.Id)
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
$win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
if (-not $win) { Say 'no window (dialog closed?)'; Flush; exit 1 }
Raise $win

$found = $false
for ($try = 1; $try -le 4 -and -not $found; $try++) {
    Say ('attempt ' + $try)
    foreach ($it in (Kids $win $CT::ListItem)) {
        if ($it.Current.Name -eq $S_PAGE_DISK) { Click-At $win $it; break }
    }
    Start-Sleep -Seconds 6
    foreach ($b in (Kids $win $CT::Button)) {
        if ($b.Current.Name -eq $S_SCAN) { $found = $true }
    }
    Say ('  scan-size button visible=' + $found)
}
if (-not $found) { Say 'FAIL disk page never became visible'; Flush; exit 1 }

$analyze = $null
foreach ($b in (Kids $win $CT::Button)) { if ($b.Current.Name.Contains($S_FEN)) { $analyze = $b } }
if (-not $analyze) { Say 'FAIL analyze button'; Flush; exit 1 }
Say ('clicking: ' + $analyze.Current.Name)
Click-At $win $analyze

$status = ''; $t = 0; $rows = 0
while ($t -lt 780) {
    Start-Sleep -Seconds 10
    $t += 10
    if ($proc.HasExited) { Say ('CRASHED t+' + $t + 's code=0x{0:X8}' -f $proc.ExitCode); break }
    $st = ''; $cnt = 0
    foreach ($x in (Kids $win $CT::Text)) {
        $nm = $x.Current.Name
        if ($nm.Contains($S_DONE) -or $nm.Contains($S_DOING)) { $st = $nm }
        if ($nm.StartsWith('C:\')) { $cnt++ }
    }
    $rows = $cnt
    if ($st -ne $status) { Say ('  t+' + $t + 's [' + $st + '] rows=' + $cnt); $status = $st }
    if ($status.Contains($S_DONE)) { Say ('PASS analysis finished, process alive, rows=' + $cnt); break }
}
Flush
