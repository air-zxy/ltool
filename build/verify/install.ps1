$ErrorActionPreference = 'Stop'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Output ("stopping pid " + $_.Id)
    Stop-Process -Id $_.Id -Force
}
Start-Sleep -Milliseconds 800
$p = Start-Process -FilePath 'D:/project/cpp/ltool/installer/ltool-0.4.0-setup.exe' `
    -ArgumentList '/VERYSILENT', '/NORESTART', '/SUPPRESSMSGBOXES' -PassThru -Wait
Write-Output ("installer exit=" + $p.ExitCode)
