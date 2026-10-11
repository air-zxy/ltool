$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class Ws {
    [DllImport("kernel32.dll")]
    public static extern bool SetProcessWorkingSetSize(IntPtr p, IntPtr min, IntPtr max);
}
'@
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$p = Get-Process ltool -ErrorAction SilentlyContinue
if (-not $p) { 'no ltool running'; exit 1 }
'before: pid=' + $p.Id + ' ws=' + [int]($p.WorkingSet64 / 1MB) + 'MB'
$null = [Ws]::SetProcessWorkingSetSize($p.Handle, [IntPtr](-1), [IntPtr](-1))
Start-Sleep -Seconds 2
$p.Refresh()
'after trim: ws=' + [int]($p.WorkingSet64 / 1MB) + 'MB'
Start-Sleep -Seconds 6
$null = Start-Process -FilePath $exe -ArgumentList '--show' -PassThru
Start-Sleep -Seconds 3
$null = Start-Process -FilePath $exe -ArgumentList '--show' -PassThru
Start-Sleep -Seconds 2
$p.Refresh()
'second show later: ws=' + [int]($p.WorkingSet64 / 1MB) + 'MB'
$log = Join-Path $env:APPDATA 'ltool\log.txt'
Get-Content $log -Encoding UTF8 -Tail 8
