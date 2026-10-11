$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$out = 'D:\project\cpp\ltool\build\verify\shredtest.out'
$f1 = 'D:\project\cpp\ltool\build\verify\shredme.bin'
$f2 = 'D:\project\cpp\ltool\build\verify\shredme2.bin'
$fs = [System.IO.File]::Create($f1)
$buf = New-Object byte[] (1MB)
1..5 | ForEach-Object { $fs.Write($buf, 0, $buf.Length) }
$fs.Close()
$fs2 = [System.IO.File]::Create($f2)
1..3 | ForEach-Object { $fs2.Write($buf, 0, $buf.Length) }
$fs2.Close()
"created: $((Get-Item $f1).Length) $((Get-Item $f2).Length)"
$null = Start-Process -FilePath $exe -ArgumentList @('--shredtest', $f1, 'yes') -Wait -NoNewWindow -RedirectStandardOutput $out
Get-Content $out -Encoding UTF8
$null = Start-Process -FilePath $exe -ArgumentList @('--shredtest', $f2, 'yes', 'deep') -Wait -NoNewWindow -RedirectStandardOutput $out
Get-Content $out -Encoding UTF8
"still there: f1=$([bool](Test-Path $f1)) f2=$([bool](Test-Path $f2))"
$d = 'D:\project\cpp\ltool\build\verify\shreddir'
New-Item -ItemType Directory -Force -Path "$d\sub" | Out-Null
Set-Content -Path "$d\a.txt" -Value 'hello'
Set-Content -Path "$d\sub\b.txt" -Value 'world'
$null = Start-Process -FilePath $exe -ArgumentList @('--shredtest', $d, 'yes') -Wait -NoNewWindow -RedirectStandardOutput $out
Get-Content $out -Encoding UTF8
"dir still there: $([bool](Test-Path $d))"
$null = Start-Process -FilePath $exe -ArgumentList @('--shredtest', $f1) -Wait -NoNewWindow -RedirectStandardOutput $out
Get-Content $out -Encoding UTF8
tail -6 "$env:APPDATA\ltool\log.txt"
