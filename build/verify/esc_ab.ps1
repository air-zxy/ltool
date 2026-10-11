$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Kb {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]

function Tap([int]$vk) {
    [Kb]::keybd_event([byte]$vk, 0, 0, [IntPtr]::Zero)
    [Kb]::keybd_event([byte]$vk, 0, 2, [IntPtr]::Zero)
}
function AltSpace() {
    [Kb]::keybd_event(0x12, 0, 0, [IntPtr]::Zero)
    Tap 0x20
    [Kb]::keybd_event(0x12, 0, 2, [IntPtr]::Zero)
}
function Win-Count([int]$pid2) {
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $pid2)
    return [System.Windows.Automation.AutomationElement]::RootElement.FindAll($TC::Children, $cond).Count
}

function Run-Case([string]$exe, [string]$label) {
    Get-Process ltool -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 2
    $proc = Start-Process -FilePath $exe -ArgumentList '--settings' -PassThru
    Start-Sleep -Seconds 6
    if ($proc.HasExited) { Write-Host ($label + ': died on its own'); return }
    Write-Host ($label + ': settings open, windows=' + (Win-Count $proc.Id))
    AltSpace
    Start-Sleep -Seconds 2
    $proc.Refresh()
    if ($proc.HasExited) { Write-Host ($label + ': EXITED right after Alt+Space'); return }
    Write-Host ($label + ': after Alt+Space windows=' + (Win-Count $proc.Id) + ' alive=True')
    Tap 0x1B   # Esc
    Start-Sleep -Seconds 3
    $proc.Refresh()
    Write-Host ($label + ': after Esc alive=' + (-not $proc.HasExited))
    if (-not $proc.HasExited) { $proc.Kill() }
}

Run-Case 'D:\software\ltool\ltool.exe' 'INSTALLED (buggy)'
Run-Case 'D:\project\cpp\ltool\build\Release\ltool.exe' 'NEW BUILD (fixed)'
