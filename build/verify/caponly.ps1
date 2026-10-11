$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$cs = @'
using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public static class Cap5 {
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
    public static List<IntPtr> Dialogs() {
        var list = new List<IntPtr>();
        EnumWindows((h, l) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid == 0 || !IsWindowVisible(h)) return true;
            RECT rc; GetWindowRect(h, out rc);
            if ((rc.r - rc.l) > 900 && (rc.b - rc.t) > 500) {
                string exe = "";
                try { exe = System.Diagnostics.Process.GetProcessById((int)pid).ProcessName; } catch { }
                if (exe == "ltool") list.Add(h);
            }
            return true;
        }, IntPtr.Zero);
        return list;
    }
}
'@
Add-Type -TypeDefinition $cs
$out = $args[0]
$hs = [Cap5]::Dialogs()
Write-Output ('windows=' + $hs.Count)
if ($hs.Count -eq 0) { exit 1 }
$h = $hs[0]
$r = New-Object Cap5+RECT
[void][Cap5]::GetWindowRect($h, [ref] $r)
$bmp = New-Object System.Drawing.Bitmap(($r.r - $r.l), ($r.b - $r.t), [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][Cap5]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ("saved $out  rect=" + $r.l + "," + $r.t + " " + ($r.r - $r.l) + "x" + ($r.b - $r.t))
