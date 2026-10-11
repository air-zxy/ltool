$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$out = 'D:\project\cpp\ltool\build\verify\adscan.out'
$p = Start-Process -FilePath $exe -ArgumentList '--adscan' -Wait -PassThru -NoNewWindow -RedirectStandardOutput $out
Get-Content $out -Encoding UTF8
