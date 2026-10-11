$ErrorActionPreference = 'Stop'
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$out = 'D:\project\cpp\ltool\build\verify\fwname.out'
$null = Start-Process -FilePath $exe -ArgumentList @('--fwtry', 'D:\project\cpp\ltool\build\Release\ltool.exe', 'BFF 版本 1.0.0') -Wait -RedirectStandardOutput $out
Get-Content $out -Encoding UTF8
$ps = @'
$f = New-Object -ComObject HNetCfg.FwPolicy2
foreach ($r in $f.Rules) {
  if ($r.Name -eq 'BFF') {
    'RULE name=' + $r.Name + ' | dir=' + $r.Direction + ' act=' + $r.Action + ' on=' + $r.Enabled + ' | desc=' + $r.Description
  }
}
'@
[System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\fwname_read.ps1', $ps, (New-Object System.Text.UTF8Encoding($true)))
& powershell -NoProfile -ExecutionPolicy Bypass -File 'D:\project\cpp\ltool\build\verify\fwname_read.ps1'
