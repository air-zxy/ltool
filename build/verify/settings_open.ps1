$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$root = $AE::RootElement
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add($s) }
function Warp([int]$x, [int]$y) { [W32]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 150 }
function Down { [W32]::mouse_event(0x0002,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60 }
function Up   { [W32]::mouse_event(0x0004,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 250 }
function ClickAt([int]$x,[int]$y) { Warp $x $y; Down; Up }
function Key([byte]$vk) { [W32]::keybd_event($vk,0,0,[IntPtr]::Zero); [W32]::keybd_event($vk,0,2,[IntPtr]::Zero); Start-Sleep -Milliseconds 120 }
function Dump($w, [string]$tag) {
  $r = $w.Current.BoundingRectangle
  Say ("=== " + $tag + ' "' + $w.Current.Name + '" ' + [int]$r.X + ',' + [int]$r.Y + ' ' + [int]$r.Width + 'x' + [int]$r.Height)
  $all = $w.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::IsControlElementProperty, $true)))
  foreach ($e in $all) {
    $er = $e.Current.BoundingRectangle
    if ($er.Width -le 0) { continue }
    $t = $e.Current.ControlType.ProgrammaticName.Replace('ControlType.','')
    if ($t -eq 'Button' -or $t -eq 'TabItem' -or $t -eq 'CheckBox' -or $t -eq 'Edit' -or $t -eq 'ListItem' -or $t -eq 'Text' -or $t -eq 'Custom') {
      Say ("   [$t] """ + $e.Current.Name + """ @ " + [int]$er.X + ',' + [int]$er.Y + ' ' + [int]$er.Width + 'x' + [int]$er.Height)
    }
  }
}

$proc = Get-Process ltool -ErrorAction Stop
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
$wins = @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ })
Say ('pid ' + $proc.Id + ' windows = ' + $wins.Count)
Dump $wins[0] 'launcher'
# gear button opens settings
# the launcher's only button is the settings gear (see the earlier dump)
$gear = @($wins[0].FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Button))))
if ($gear.Count -eq 0) { Say 'FAIL: gear not found'; [System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\set2_dump.txt', $out, (New-Object System.Text.UTF8Encoding($true))); exit 5 }
$gr = $gear[0].Current.BoundingRectangle
ClickAt ([int]($gr.X + $gr.Width/2)) ([int]($gr.Y + $gr.Height/2))
Start-Sleep -Milliseconds 1500
$wins = @($root.FindAll($TC::Children, $cond) | ForEach-Object { $_ })
Say ('after gear: windows = ' + $wins.Count)
foreach ($w in $wins) { Dump $w 'win' }
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\set2_dump.txt', $out, (New-Object System.Text.UTF8Encoding($true)))
Write-Host done
