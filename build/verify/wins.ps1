$ErrorActionPreference = 'Stop'
$cs = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class EW {
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
    public static List<string> Dump(string classFilter) {
        var list = new List<string>();
        EnumWindows((h, l) => {
            var sb = new StringBuilder(256);
            GetClassName(h, sb, 256);
            string cls = sb.ToString();
            if (cls.IndexOf(classFilter, StringComparison.OrdinalIgnoreCase) < 0) return true;
            uint pid; GetWindowThreadProcessId(h, out pid);
            var tsb = new StringBuilder(256); GetWindowText(h, tsb, 256);
            RECT rc; GetWindowRect(h, out rc);
            list.Add("pid=" + pid + " hwnd=" + h + " class=" + cls + " title=" + tsb.ToString()
                + " vis=" + IsWindowVisible(h) + " rect=" + rc.l + "," + rc.t + "," + rc.r + "," + rc.b);
            return true;
        }, IntPtr.Zero);
        return list;
    }
}
'@
Add-Type -TypeDefinition $cs
$lines = @()
foreach ($f in @('Capture', 'Qt', 'SearchWindow', 'PinWindow')) {
    foreach ($l in [EW]::Dump($f)) { $lines += $l }
}
[System.IO.File]::WriteAllLines('D:/project/cpp/ltool/build/verify/wins.txt', $lines,
    (New-Object System.Text.UTF8Encoding($false)))
Write-Output ('windows: ' + $lines.Count)
