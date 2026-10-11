$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Hk3 {
    [DllImport("user32.dll")] static extern bool PostThreadMessage(uint tid, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("kernel32.dll")] static extern bool SetProcessWorkingSetSize(IntPtr p, IntPtr min, IntPtr max);
    public static bool TrimNow(IntPtr h) { return SetProcessWorkingSetSize(h, new IntPtr(-1), new IntPtr(-1)); }
    public static bool Press(uint tid) {
        return PostThreadMessage(tid, 0x0312, new IntPtr(1), new IntPtr((1 << 16) | 0x20));
    }
}
'@
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$log = Join-Path $env:APPDATA 'ltool\log.txt'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$before = (@(Get-Content $log -Encoding UTF8 | Where-Object { $_ -like '*show:*' })).Count
$p = Start-Process -FilePath $exe -PassThru
$q = Get-Process -Id $p.Id
$tid = [uint32]$q.Threads[0].Id
'trim=' + ($args -contains 'trim') + ' pid=' + $p.Id + ' guiThread=' + $tid
for ($i = 0; $i -lt 40; $i++) {
  if (($i -eq 20) -and ($args -contains 'trim')) {
    $q = Get-Process ltool -ErrorAction SilentlyContinue
    if ($q) { $null = [Hk3]::TrimNow($q.Handle); 'trimmed at press 20' }
  }
  $r = [Hk3]::Press($tid); if ($i -lt 3) { "press $i posted=$r" }
  Start-Sleep -Milliseconds 900
}
Start-Sleep -Seconds 2
Get-Content $log -Encoding UTF8 | Where-Object { $_ -like '*show:*' -or $_ -like '*deep index*' } |
  Select-Object -Last 42 | ForEach-Object { $_ -replace '^\[2026-10-06 ', 't=' -replace '\] show:', ' show:' }
