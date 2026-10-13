$ErrorActionPreference = 'Stop'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Output ("stopping pid " + $_.Id)
    Stop-Process -Id $_.Id -Force
}
Start-Sleep -Milliseconds 800
$p = Start-Process -FilePath (Get-ChildItem 'D:/project/cpp/ltool/installer/ltool-*-setup.exe' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName `
    -ArgumentList '/VERYSILENT', '/NORESTART', '/SUPPRESSMSGBOXES' -PassThru -Wait
Write-Output ("installer exit=" + $p.ExitCode)
