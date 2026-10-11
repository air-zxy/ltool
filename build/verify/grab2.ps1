$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing

$cs = @'
using System;
using System.Runtime.InteropServices;
public static class Win2 {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
}
'@
Add-Type -TypeDefinition $cs

$pidToGrab = [int] $args[0]
$out = $args[1]

Start-Process -FilePath 'D:/software/ltool/ltool.exe' -ArgumentList '--show' | Out-Null
Start-Sleep -Milliseconds 1200

$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $pidToGrab)
foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.IsOffscreen) { continue }
    $h = [IntPtr] $w.Current.NativeWindowHandle
    $r = New-Object Win2+RECT
    [void][Win2]::GetWindowRect($h, [ref] $r)
    $wd = $r.r - $r.l
    $ht = $r.b - $r.t
    Write-Output ("hwnd=" + $h + " size=" + $wd + "x" + $ht)
    $bmp = New-Object System.Drawing.Bitmap($wd, $ht, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][Win2]::PrintWindow($h, $hdc, 2)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output ("saved " + $out)
    break
}
