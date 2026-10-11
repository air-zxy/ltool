$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class Hk2 {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern int GetClassName(IntPtr h, StringBuilder s, int max);
    [DllImport("user32.dll")] static extern bool PostThreadMessage(uint tid, uint msg, IntPtr wp, IntPtr lp);
    public static uint GuiThread(uint target) {
        uint found = 0;
        EnumWindows((h, l) => {
            uint pid; GetWindowThreadProcessId(h, out pid);
            if (pid != target) return true;
            var sb = new StringBuilder(256);
            GetClassName(h, sb, 256);
            if (sb.ToString().StartsWith("Qt")) { found = pid; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static void Press(uint tid) {
        PostThreadMessage(tid, 0x0312, new IntPtr(1), new IntPtr((1 << 16) | 0x20));
    }
}
'@
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$log = Join-Path $env:APPDATA 'ltool\log.txt'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -PassThru
$t0 = Get-Date
$tid = 0
for ($i = 0; $i -lt 40; $i++) {
  if ($tid -eq 0) { $tid = [Hk2]::GuiThread([uint32]$p.Id) }
  if ($tid -ne 0) { [Hk2]::Press($tid) }
  Start-Sleep -Milliseconds 900
}
"guiThread=$tid  pressed 40 times over ~36s"
Get-Content $log -Encoding UTF8 | Where-Object { $_ -like '*show:*' -or $_ -like '*deep index*' -or $_ -like '*startup: hotkey*' } |
  Select-Object -Last 46 | ForEach-Object {
    $ts = [datetime]::ParseExact($_.Substring(1, 19), 'yyyy-MM-dd HH:mm:ss', $null)
    't+{0,5}s  {1}' -f [int](($ts - $t0).TotalSeconds), ($_ -replace '^\[[^\]]+\] ', '')
  }
