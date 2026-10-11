$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms
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

function Codes([string]$s) { ($s.ToCharArray() | ForEach-Object { [int]$_ }) -join ',' }
function Has-Code([string]$s, [int]$code) { ($s.ToCharArray() | ForEach-Object { [int]$_ }) -contains $code }

# build targets with explicit [char] concatenation (PS 5.1 quirk: -join in a fn returned empty)
$S_PAGE   = [string][char]0x43 + [char]0x76D8 + [char]0x6E05 + [char]0x7406            # C盘清理
$S_DONE   = [string][char]0x5206 + [char]0x6790 + [char]0x5B8C + [char]0x6210          # 分析完成
$S_DOING  = [string][char]0x5206 + [char]0x6790 + [char]0x4E2D                         # 分析中
$FEN      = 0x5206
'C盘清理=' + (Codes $S_PAGE) + '  分析完成=' + (Codes $S_DONE)
if ((Codes $S_PAGE) -ne '67,30424,28165,29702') { throw 'target string build failed' }

function Click-Element([object]$el) {
    $r = $el.Current.BoundingRectangle
    $cx = [int]($r.X + $r.Width / 2)
    $cy = [int]($r.Y + $r.Height / 2)
    $pt = New-Object System.Windows.Point($cx, $cy)
    try {
        $hit = [System.Windows.Automation.AutomationElement]::ElementFromPoint($pt)
        if ($hit.Current.ProcessId -ne $el.Current.ProcessId) {
            throw "hit-test at ($cx,$cy) hit another process: '" + (Codes $hit.Current.Name) + "'"
        }
    } catch [System.Management.Automation.MethodInvocationException] { 'hit-test-unavailable' }
    $old = New-Object Win32+PT
    [void][Win32]::GetCursorPos([ref]$old)
    [Win32]::SetCursorPos($cx, $cy) | Out-Null
    Start-Sleep -Milliseconds 150
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    [Win32]::SetCursorPos($old.X, $old.Y) | Out-Null
    'clicked codes=' + (Codes $el.Current.Name) + ' at (' + $cx + ',' + $cy + ')'
}

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
function Find-All([object]$win, [object]$ct) {
    $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition(
        $AE::ControlTypeProperty, $ct)))
}

$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$p = Start-Process -FilePath $exe -ArgumentList '--settings' -PassThru
'pid=' + $p.Id

$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    if ($p.HasExited) { 'PROCESS-EXITED code=0x{0:X8}' -f $p.ExitCode; exit 1 }
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $p.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
    if ($win) { break }
}
if (-not $win) { 'NO-WINDOW'; Stop-Process -Id $p.Id -Force; exit 1 }
'settings-window found'
[Win32]::SetForegroundWindow([IntPtr]$win.Current.NativeWindowHandle) | Out-Null
Start-Sleep -Milliseconds 600

$items = Find-All $win ([System.Windows.Automation.ControlType]::ListItem)
$page = $null
foreach ($it in $items) { if ($it.Current.Name -eq $S_PAGE) { $page = $it } }
'items=' + $items.Count + ' page-found=' + [bool]$page
if (-not $page) {
    for ($i = 0; $i -lt $items.Count; $i++) { '  [{0}] {1}' -f $i, (Codes $items[$i].Current.Name) }
    throw 'no C盘清理 page entry'
}
Click-Element $page
Start-Sleep -Seconds 8    # DiskPage ctor kicks off the cache-size scan

$btns = Find-All $win ([System.Windows.Automation.ControlType]::Button)
$analyze = $null
foreach ($b in $btns) { 'btn ' + (Codes $b.Current.Name); if (Has-Code $b.Current.Name $FEN) { $analyze = $b } }
if (-not $analyze) { Stop-Process -Id $p.Id -Force; throw 'no analyze button' }
Click-Element $analyze

$deadline = (Get-Date).AddMinutes(14)
$last = ''
$n = 0
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 10
    $n++
    if ($p.HasExited) { 'CRASHED after ' + ($n * 10) + 's code=0x{0:X8}' -f $p.ExitCode; exit 1 }
    $txts = Find-All $win ([System.Windows.Automation.ControlType]::Text)
    $status = ''
    $rows = 0
    foreach ($t in $txts) {
        $nm = $t.Current.Name
        if ($nm -like ('*' + $S_DONE + '*') -or $nm -like ('*' + $S_DOING + '*')) { $status = $nm }
        if ($nm.StartsWith([string][char]0x43 + [char]0x3A)) { $rows++ }
    }
    if ($status -ne $last) {
        't+' + ($n * 10) + 's status=' + (Codes $status) + ' bar-rows=' + $rows
        $last = $status
    }
    if ($status -like ('*' + $S_DONE + '*')) { 'ANALYSIS-COMPLETE; PROCESS ALIVE; bar-rows=' + $rows; break }
}
Stop-Process -Id $p.Id -Force
'alive-at-end=' + [bool](Get-Process -Id $p.Id -ErrorAction SilentlyContinue)
'DONE'
