param([Parameter(Mandatory = $true)][string]$Exe, [string]$Label = '')
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

function Get-Kids([object]$w, [object]$t) {
    return $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $t)))
}
function Invoke-Raise([object]$w) {
    for ($i = 0; $i -lt 6; $i++) {
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
    Start-Sleep -Milliseconds 400
}

$alive = Get-Process ltool -ErrorAction SilentlyContinue
if ($alive) { throw ('another ltool is running (pid ' + $alive.Id + '), refusing to fight over the single instance') }

$proc = Start-Process -FilePath $Exe -ArgumentList '--settings' -PassThru
Write-Host ("case=" + $Label + " pid=" + $proc.Id)
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 800
    if ($proc.HasExited) { throw 'process exited early' }
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
    if ($win) { break }
}
if (-not $win) { throw 'no settings window' }
Start-Sleep -Seconds 2
$w0 = [int]$win.Current.BoundingRectangle.Width
Write-Host ("width at open = " + $w0)

$cpp = $null
foreach ($it in (Get-Kids $win $CT::ListItem)) { if ($it.Current.Name.StartsWith('C++')) { $cpp = $it } }
if (-not $cpp) { throw 'no C++ entry' }
Invoke-Click $win $cpp

$max = $w0
$seen = @{}
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 1500
    if ($proc.HasExited) { throw 'process died' }
    $wid = [int]$win.Current.BoundingRectangle.Width
    if ($wid -gt $max) { $max = $wid }
    if (-not $seen.ContainsKey($wid)) { $seen[$wid] = $i * 1500 / 1000 }
    if ($i -eq 29) { }
}
$statusText = ''
foreach ($t in (Get-Kids $win $CT::Text)) {
    if ($t.Current.Name.Contains([string][char]0x00B7)) { $statusText = $t.Current.Name }
}
Write-Host ("distinct widths (width -> first seen sec): " + (($seen.GetEnumerator() | Sort-Object Name | ForEach-Object { "" + $_.Name + "@" + $_.Value + "s" }) -join ', '))
Write-Host ("max width = " + $max + "  delta = " + ($max - $w0))
Write-Host ("status label = " + $statusText)
$proc.Kill()
