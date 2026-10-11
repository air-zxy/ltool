$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing

$cs = @'
using System;
using System.Runtime.InteropServices;
public static class Grab3 {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
}
'@
Add-Type -TypeDefinition $cs

$exe = $args[0]
$outPng = $args[1]

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 800
$argList = @('--settingspage') + @($args[1..($args.Count - 1)])
$p = Start-Process -FilePath $exe -ArgumentList $argList -PassThru
Start-Sleep -Seconds 5

$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.ClassName -ne 'SettingsDialog') { continue }
    $h = [IntPtr] $w.Current.NativeWindowHandle
    $r = New-Object Grab3+RECT
    [void][Grab3]::GetWindowRect($h, [ref] $r)
    $wd = $r.r - $r.l
    $ht = $r.b - $r.t
    Write-Output ("dialog hwnd=" + $h + " " + $wd + "x" + $ht + " offscreen=" + $w.Current.IsOffscreen)
    $bmp = New-Object System.Drawing.Bitmap($wd, $ht, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][Grab3]::PrintWindow($h, $hdc, 2)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $bmp.Save($outPng, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output ("saved " + $outPng)
    break
}
