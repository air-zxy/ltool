$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Hk {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern int GetClassName(IntPtr h, StringBuilder s, int max);
    [DllImport("user32.dll")] static extern bool PostThreadMessage(uint tid, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("kernel32.dll")] static extern bool SetProcessWorkingSetSize(IntPtr p, IntPtr min, IntPtr max);
    public static uint ThreadOf(uint target) {
        uint tid = 0;
        EnumWindows((h, l) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid == target) {
                var sb = new StringBuilder(256);
                GetClassName(h, sb, 256);
                if (sb.ToString().StartsWith("Qt")) { tid = pid; tid = GetWindowThreadProcessId(h, out pid); return false; }
            }
            return true;
        }, IntPtr.Zero);
        return tid;
    }
    public static bool Press(uint tid) {
        // WM_HOTKEY, id=1, lParam = (MOD_ALT << 16) | VK_SPACE
        return PostThreadMessage(tid, 0x0312, new IntPtr(1), new IntPtr((1 << 16) | 0x20));
    }
    public static bool Trim(IntPtr h) { return SetProcessWorkingSetSize(h, new IntPtr(-1), new IntPtr(-1)); }
}
'@
$log = Join-Path $env:APPDATA 'ltool\log.txt'
function Wait-Show([int]$mark) {
  $t0 = Get-Date
  $lines = Get-Content $log -Encoding UTF8
  $new = $lines | Where-Object { $_ -like '*show:*' }
  if ($new.Count -gt $mark) { return [int](((Get-Date) - $t0).TotalMilliseconds) }
  for ($i = 0; $i -lt 200; $i++) {
    Start-Sleep -Milliseconds 25
    $lines = Get-Content $log -Encoding UTF8
    $new = $lines | Where-Object { $_ -like '*show:*' }
    if ($new.Count -gt $mark) { return [int](((Get-Date) - $t0).TotalMilliseconds) }
  }
  return -1
}
$p = Get-Process ltool -ErrorAction SilentlyContinue | Sort-Object StartTime -Descending | Select-Object -First 1
if (-not $p) { 'no ltool running'; exit 1 }
$tid = [Hk]::ThreadOf([uint32]$p.Id)
'target pid=' + $p.Id + ' guiThread=' + $tid
$mark = (@(Get-Content $log -Encoding UTF8 | Where-Object { $_ -like '*show:*' }).Count)
'warm presses:'
1..3 | ForEach-Object {
  $null = [Hk]::Press($tid)
  $ms = Wait-Show $mark
  $mark++
  '  press -> show line appeared in ' + $ms + ' ms'
  Start-Sleep -Seconds 3
}
$null = [Hk]::Trim($p.Handle)
Start-Sleep -Seconds 3
$p.Refresh()
'after external trim: ws=' + [int]($p.WorkingSet64 / 1MB) + 'MB'
$null = [Hk]::Press($tid)
$ms = Wait-Show $mark
'cold press -> show line appeared in ' + $ms + ' ms'
Get-Content $log -Encoding UTF8 -Tail 6
