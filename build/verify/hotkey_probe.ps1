$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Kbd {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$p = Get-Process ltool
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $p.Id)
function Wins { return @($AE::RootElement.FindAll($TC::Children, $cond) | ForEach-Object { $_ }) }
function Tap([byte]$vk) {
  [Kbd]::keybd_event(0x12,0,0,[IntPtr]::Zero)   # Alt down
  [Kbd]::keybd_event($vk,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [Kbd]::keybd_event($vk,0,2,[IntPtr]::Zero)
  [Kbd]::keybd_event(0x12,0,2,[IntPtr]::Zero)
}
function Report([string]$tag) {
  $w = Wins
  Write-Host ("$tag 窗口数=" + $w.Count + " 前台=" + [Kbd]::GetForegroundWindow())
  foreach ($x in $w) {
    $r = $x.Current.BoundingRectangle
    Write-Host ("   hwnd=" + $x.Current.NativeWindowHandle + " " + [int]$r.Width + "x" + [int]$r.Height + " [" + $x.Current.Name + "]")
  }
  return $w
}
Report "起始(设置页开着)" | Out-Null
Tap 0x20
Start-Sleep -Milliseconds 900
$w = Report "Alt+Space 之后"
if ($w.Count -lt 2) {
  Write-Host "→ 设置页开着时呼不出搜索框，等 1.5 s 再看一次"
  Start-Sleep -Milliseconds 1500
  Report "1.5s 后" | Out-Null
}
