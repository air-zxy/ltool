$d = Get-ChildItem "$env:LOCALAPPDATA\Packages\Microsoft.DesktopAppInstaller_8wekyb3d8bbwe\LocalState\DiagOutputDir" -Filter *.log -ErrorAction SilentlyContinue |
     Sort-Object LastWriteTime -Descending | Select-Object -First 3
if (-not $d) { Write-Host 'no winget msi logs on this machine'; exit }
foreach ($f in $d) {
    $b = [IO.File]::ReadAllBytes($f.FullName)
    Write-Host ($f.Name + '  ' + $b.Length + 'B  first2=' + ('{0:X2} {1:X2}' -f $b[0], $b[1]))
    $t = [IO.File]::ReadAllText($f.FullName)
    $hits = @($t -split "`n" | Where-Object { $_ -match 'MainEngineThread is returning|Return value 3|Error 1\d{3}' })
    Write-Host ('  hits=' + $hits.Count + '  sample=' + (($hits | Select-Object -First 2 | ForEach-Object { $_.Trim() }) -join ' || '))
}
