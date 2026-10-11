$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

# ASCII-only script body: non-ASCII in a no-BOM .ps1 breaks PS5.1 line parsing
$cs = @'
using System;
using System.Runtime.InteropServices;
public static class Cap {
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int idx);
    [DllImport("user32.dll")] public static extern int SetWindowLong(IntPtr h, int idx, int val);
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
}
'@
Add-Type -TypeDefinition $cs

$exe = $args[0]
$outFile = $args[1]

$GWL_EXSTYLE = -20
$WS_EX_TRANSPARENT = 0x20
$WS_EX_LAYERED = 0x80000

function KeyDown([byte]$vk, [byte]$scan, [uint32]$flags) {
    [Cap]::keybd_event($vk, $scan, $flags, [UIntPtr]::Zero) | Out-Null
}

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -ArgumentList '--show' -PassThru
Start-Sleep -Seconds 8

$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$search = [IntPtr]::Zero
$overlay = [IntPtr]::Zero
foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    $h = [IntPtr] $w.Current.NativeWindowHandle
    if ($w.Current.ClassName -eq 'SearchWindow') { $search = $h }
    if ($w.Current.ClassName -eq 'CaptureOverlay') { $overlay = $h }
}
$out = @("pid=$($p.Id) search=$search overlay=$overlay")
if ($search -eq [IntPtr]::Zero) { $out += 'FAIL: no SearchWindow handle'; }
$out += ("before F1: search visible=" + [Cap]::IsWindowVisible($search))

$r = New-Object Cap+RECT
[void][Cap]::GetWindowRect($search, [ref] $r)
$cx = [int](($r.l + $r.r) / 2)
$cy = [int](($r.t + $r.b) / 2)
$out += ("search rect=" + $r.l + "," + $r.t + ".." + $r.r + "," + $r.b + " center=$cx,$cy")

KeyDown 0x70 0x3B 1     # F1 down, KEYEVENTF_EXTENDEDKEY
KeyDown 0x70 0x3B 3     # F1 up   | KEYEVENTF_KEYUP
Start-Sleep -Milliseconds 1500

$overlay2 = [IntPtr]::Zero
foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.ClassName -eq 'CaptureOverlay') { $overlay2 = [IntPtr] $w.Current.NativeWindowHandle }
}
$out += ("after F1: overlay=$overlay2 search visible=" + [Cap]::IsWindowVisible($search))

if ($overlay2 -ne [IntPtr]::Zero) {
    # same trick the snap code uses: make the overlay click-through, then hit-test
    $ex = [Cap]::GetWindowLong($overlay2, $GWL_EXSTYLE)
    [void][Cap]::SetWindowLong($overlay2, $GWL_EXSTYLE, ($ex -bor $WS_EX_TRANSPARENT -bor $WS_EX_LAYERED))
    $pt = New-Object Cap+POINT
    $pt.x = $cx
    $pt.y = $cy
    $hit = [Cap]::WindowFromPoint($pt)
    [void][Cap]::SetWindowLong($overlay2, $GWL_EXSTYLE, $ex)
    $hitCls = ''
    foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
        if ([IntPtr] $w.Current.NativeWindowHandle -eq $hit) { $hitCls = $w.Current.ClassName }
    }
    $out += ("WindowFromPoint(center) = $hit class='" + $hitCls + "' equalsSearch=" + ($hit -eq $search))
}

KeyDown 0x1B 0x01 0     # Esc down
KeyDown 0x1B 0x01 2     # Esc up -- closes the overlay
Start-Sleep -Milliseconds 1200
$out += ("after Esc: search visible=" + [Cap]::IsWindowVisible($search))

[System.IO.File]::WriteAllLines($outFile, $out,
    (New-Object System.Text.UTF8Encoding($false)))
Write-Output 'wrote cap.txt'
