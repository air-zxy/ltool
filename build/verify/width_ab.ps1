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
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\width_ab.txt', ($log -join "`r`n"), [System.Text.Encoding]::UTF8) }
function Kids([object]$w, [object]$t) {
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Try-Raise([object]$w) {
    for ($i = 0; $i -lt 6; $i++) {
        [Win32]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
        [Win32]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
        [Win32]::SetForegroundWindow([IntPtr]$w.Current.NativeWindowHandle) | Out-Null
        Start-Sleep -Milliseconds 350
        if ([Win32]::GetForegroundWindow() -eq [IntPtr]$w.Current.NativeWindowHandle) { return $true }
    }
    return $false
}
function Click([object]$w, [object]$el) {
    if (-not (Try-Raise $w)) { throw 'foreground not acquired; refusing to click' }
    $r = $el.Current.BoundingRectangle
    [Win32]::SetCursorPos([int]($r.X + $r.Width / 2), [int]($r.Y + $r.Height / 2)) | Out-Null
    Start-Sleep -Milliseconds 200
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400
    Say ('  clicked [' + $el.Current.Name + ']')
}
function Width-Of([object]$w, [string]$tag) {
    $r = $w.Current.BoundingRectangle
    Say ('  width ' + $tag + ' = ' + [int]$r.Width + ' x ' + [int]$r.Height)
    return [int]$r.Width
}

function Run-Case([string]$exe, [string]$label) {
    Say ('=== ' + $label + ' : ' + $exe + ' ===')
    Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { $_.Kill() }
    Start-Sleep -Seconds 2
    $proc = Start-Process -FilePath $exe -ArgumentList '--settings' -PassThru
    $win = $null
    for ($i = 0; $i -lt 25; $i++) {
        Start-Sleep -Milliseconds 800
        if ($proc.HasExited) { throw ($label + ' process exited early') }
        $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
        $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
        if ($win) { break }
    }
    if (-not $win) { throw ($label + ' no settings window') }
    Start-Sleep -Seconds 2
    $w0 = Width-Of $win 'on open (language page default)'
    $cpp = $null
    foreach ($it in (Kids $win $CT::ListItem)) {
        if ($it.Current.Name.StartsWith('C++')) { $cpp = $it }
    }
    if (-not $cpp) { throw ($label + ' no C++ entry: ' + ((Kids $win $CT::ListItem | ForEach-Object { $_.Current.Name }) -join '/')) }
    Click $win $cpp
    Start-Sleep -Seconds 6
    $w1 = Width-Of $win 'after clicking C++'
    Say ('  DELTA = ' + ($w1 - $w0) + ' px')
    $proc.Kill()
    return ($w1 - $w0)
}

$d_old = Run-Case 'D:\software\ltool\ltool.exe' 'OLD (pre-fix installed binary)'
$d_new = Run-Case 'D:\project\cpp\ltool\build\Release\ltool.exe' 'NEW (fixed build)'
Say ('RESULT old-delta=' + $d_old + ' new-delta=' + $d_new)
Flush
