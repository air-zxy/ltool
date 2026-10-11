$cands = @("$env:TEMP\MSI374ef.LOG") + @(Get-ChildItem $env:TEMP -Filter 'MSI*.LOG' -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
foreach ($p in $cands) {
    if (-not (Test-Path $p)) { continue }
    $b = [IO.File]::ReadAllBytes($p)
    $isUtf16 = ($b[0] -eq 0xFF -and $b[1] -eq 0xFE)
    $t = [IO.File]::ReadAllText($p, [Text.Encoding]::Unicode)
    if (-not $isUtf16) { $t = [IO.File]::ReadAllText($p) }
    $needles = 'MainEngineThread is returning', 'Doing action', 'Error 1', 'Return value 3', 'sufficient privileges', 'Another process is an installer', 'verify file hash'
    $hits = @($t -split "`n" | Where-Object { $line = $_.Trim(); ($needles | Where-Object { $line -match [regex]::Escape($_) }).Count -gt 0 })
    Write-Host ($p + '  bytes=' + $b.Length + '  utf16=' + $isUtf16 + '  hits=' + $hits.Count)
    ($hits | Select-Object -Last 8 | ForEach-Object { Write-Host ('   ' + $_.Trim()) })
}
