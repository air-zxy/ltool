$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing

$cs = @'
using System;
using System.Runtime.InteropServices;
public static class Win {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
}
'@
Add-Type -TypeDefinition $cs

$pidToGrab = [int] $args[0]
$out = $args[1]

$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $pidToGrab)
$wins = $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)
foreach ($w in $wins) {
    if ($w.Current.IsOffscreen) { continue }
    $h = [IntPtr] $w.Current.NativeWindowHandle
    $r = New-Object Win+RECT
    [void][Win]::GetWindowRect($h, [ref] $r)
    $wd = $r.r - $r.l
    $ht = $r.b - $r.t
    Write-Output ("hwnd=" + $h + " size=" + $wd + "x" + $ht)
    $bmp = New-Object System.Drawing.Bitmap($wd, $ht, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][Win]::PrintWindow($h, $hdc, 2)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output ("saved " + $out)
    break
}
