$ErrorActionPreference = 'Stop'
$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class PopProbe {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern int GetClassName(IntPtr h, StringBuilder s, int max);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr h, uint flags);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern void mouse_event(uint flags, uint dx, uint dy, uint data, IntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l; public int t; public int r; public int b; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x; public int y; }
    public static System.Collections.Generic.List<string> Windows(uint target) {
        var list = new System.Collections.Generic.List<string>();
        EnumWindows((h, l) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid != target || !IsWindowVisible(h)) return true;
            var sb = new StringBuilder(256); GetClassName(h, sb, 256);
            RECT r; GetWindowRect(h, out r);
            list.Add(h.ToInt64() + " " + sb + " (" + r.l + "," + r.t + " " + (r.r - r.l) + "x" + (r.b - r.t) + ")");
            return true;
        }, IntPtr.Zero);
        return list;
    }
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
    public static bool SafeClick(IntPtr hwnd, int x, int y) {
        for (int i = 0; i < 8; i++) {
            keybd_event(0x12, 0, 0, IntPtr.Zero); keybd_event(0x12, 0, 2, IntPtr.Zero);
            SetForegroundWindow(hwnd);
            var pt = new POINT { x = x, y = y };
            if (GetAncestor(WindowFromPoint(pt), 2) == hwnd) {
                SetCursorPos(x, y);
                mouse_event(2, 0, 0, 0, IntPtr.Zero);
                mouse_event(4, 0, 0, 0, IntPtr.Zero);
                return true;
            }
            System.Threading.Thread.Sleep(150);
        }
        return false;
    }
    public static void PressEsc() { keybd_event(0x1B, 0, 0, IntPtr.Zero); keybd_event(0x1B, 0, 2, IntPtr.Zero); }
}
'@
Add-Type -TypeDefinition $src
Add-Type -AssemblyName System.Drawing
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', '1', '1', 'software') -PassThru
Start-Sleep -Seconds 8
$main = [PopProbe]::Biggest([uint32]$p.Id)
$r = New-Object PopProbe+RECT
[void][PopProbe]::GetWindowRect($main, [ref]$r)
$before = [PopProbe]::Windows([uint32]$p.Id)
$null = [PopProbe]::SafeClick($main, ($r.l + 1268), ($r.t + 190))
Start-Sleep -Milliseconds 800
$after = [PopProbe]::Windows([uint32]$p.Id)
$report = New-Object System.Collections.Generic.List[string]
$report.Add('windows before:'); $before | ForEach-Object { $report.Add('  ' + $_) }
$report.Add('windows after click:'); $after | ForEach-Object { $report.Add('  ' + $_) }
$new = @($after | Where-Object { $b = $_; -not ($before -contains $b) })
$report.Add('new windows: ' + $new.Count)
foreach ($n in $new) {
  $num = [int64]($n.Split(' ')[0])
  $hh = [IntPtr]::new($num)
  $rr = New-Object PopProbe+RECT
  [void][PopProbe]::GetWindowRect($hh, [ref]$rr)
  $w = [int]($rr.r - $rr.l); $ht = [int]($rr.b - $rr.t)
  if ($w -lt 20 -or $ht -lt 20) { continue }
  $bmp = [System.Drawing.Bitmap]::new($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][PopProbe]::PrintWindow($hh, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save('D:\project\cpp\ltool\build\verify\popup.png', [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  $report.Add('  captured popup -> popup.png')
}
[PopProbe]::PressEsc()
Start-Sleep -Milliseconds 300
[System.IO.File]::WriteAllLines('D:\project\cpp\ltool\build\verify\popup_report.txt', $report, (New-Object System.Text.UTF8Encoding($true)))
Stop-Process -Id $p.Id -Force
'report written'
