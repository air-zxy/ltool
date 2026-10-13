$exe = (Get-ChildItem 'D:\project\cpp\ltool\installer\ltool-*-setup.exe' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { "killing ltool pid=$($_.Id)"; Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 800
$p = Start-Process -FilePath $exe -ArgumentList @('/VERYSILENT','/NORESTART','/SUPPRESSMSGBOXES','/LOG=D:\project\cpp\ltool\build\verify\setup.log') -Wait -PassThru
"setup exit=" + $p.ExitCode
$i = Get-Item 'D:\software\ltool\ltool.exe'
"installed mtime=" + $i.LastWriteTime + " size=" + $i.Length
"hash installed=" + (Get-FileHash -Algorithm SHA256 'D:\software\ltool\ltool.exe').Hash
"hash built    =" + (Get-FileHash -Algorithm SHA256 'D:\project\cpp\ltool\build\Release\ltool.exe').Hash
