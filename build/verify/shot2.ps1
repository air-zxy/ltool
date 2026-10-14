$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing

$cs = @'
using System;
using System.Runtime.InteropServices;
public static class Grab4 {
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
    public static IntPtr Biggest(uint target) {
        IntPtr best = IntPtr.Zero; long area = 0;
        EnumWindows((h, l) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid != target || !IsWindowVisible(h)) return true;
            RECT r; GetWindowRect(h, out r);
            long a = (long)(r.r - r.l) * (r.b - r.t);
            if (a > area) { area = a; best = h; }
            return true;
        }, IntPtr.Zero);
        return best;
    }
}
'@
Add-Type -TypeDefinition $cs

$exe = $args[0]
$png = $args[1]
$emb = $args[2]

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 800
$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', '1', '0', $emb) -PassThru
Start-Sleep -Seconds 5

$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$want = [string][char]0x5F00 + [char]0x59CB + [char]0x5206 + [char]0x6790   # "kai shi fen xi"
foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.ClassName -ne 'SettingsDialog') { continue }
    $btnCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Button)
    foreach ($b in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $btnCond)) {
        if ($b.Current.IsOffscreen) { continue }
        if ($b.Current.Name -ne $want) { continue }
        $ip = $null
        if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref] $ip)) {
            $ip.Invoke()
            Write-Output 'invoked the analyze button'
            break
        }
    }
    break
}
Start-Sleep -Seconds 4

$h = [Grab4]::Biggest([uint32]$p.Id)
if ($h -eq [IntPtr]::Zero) { Write-Output 'no window found'; exit 1 }
$r = New-Object Grab4+RECT
[void][Grab4]::GetWindowRect($h, [ref] $r)
$bmp = New-Object System.Drawing.Bitmap(($r.r - $r.l), ($r.b - $r.t), [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][Grab4]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ('saved ' + $png)
