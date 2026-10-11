$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32 {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
}
"@
function Codes([string]$s) { if ($null -eq $s) { return '<null>' }; ($s.ToCharArray() | ForEach-Object { [int]$_ }) -join ',' }
$S_NET = [string][char]0x7F51 + [char]0x7EDC
Write-Host ('target codes = ' + (Codes $S_NET))

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]

$pid2 = (Get-Process ltool | Select-Object -First 1).Id
Write-Host ('attaching pid=' + $pid2)
$cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $pid2)
$win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
if (-not $win) { throw 'no window for that pid' }
[Win32]::SetForegroundWindow([IntPtr]$win.Current.NativeWindowHandle) | Out-Null
Start-Sleep -Milliseconds 500

$items = $win.FindAll($TC::Descendants,
    (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem)))
Write-Host ('listitems=' + $items.Count)
$pick = $null
for ($i = 0; $i -lt $items.Count; $i++) {
    $nm = $items[$i].Current.Name
    $eq = ($nm -eq $S_NET)
    Write-Host ('  [' + $i + '] eq=' + $eq + ' codes=' + (Codes $nm))
    if ($eq -and -not $pick) { $pick = $items[$i] }
}
if (-not $pick) { Write-Host 'PICK FAILED by name; trying index 9'; $pick = $items[9] }

$r = $pick.Current.BoundingRectangle
$cx = [int]($r.X + $r.Width / 2); $cy = [int]($r.Y + $r.Height / 2)
Write-Host ('click at ' + $cx + ',' + $cy)
[Win32]::SetCursorPos($cx, $cy) | Out-Null
Start-Sleep -Milliseconds 200
[Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
[Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Seconds 3

$txts = $win.FindAll($TC::Descendants,
    (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Text)))
$out = [System.Collections.Generic.List[string]]::new()
$out.Add('--- texts after clicking network page ---')
foreach ($t in $txts) { if ($t.Current.Name) { $out.Add('  ' + $t.Current.Name.Replace("`n", ' / ')) } }
$items2 = $win.FindAll($TC::Descendants,
    (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem)))
$out.Add('--- listitems after click ---')
for ($i = 0; $i -lt $items2.Count; $i++) {
    $out.Add('  [' + $i + '] ' + (Codes $items2[$i].Current.Name) + ' | ' + $items2[$i].Current.Name)
}
[System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\net_probe.txt',
    ($out -join "`r`n"), [System.Text.Encoding]::UTF8)

$rr = $win.Current.BoundingRectangle
$bmp = New-Object System.Drawing.Bitmap([int]$rr.Width, [int]$rr.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen([int]$rr.X, [int]$rr.Y, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save('D:\project\cpp\ltool\build\verify\network_page.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host 'wrote net_probe.txt + network_page.png'
