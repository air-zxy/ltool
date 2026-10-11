$ErrorActionPreference = 'Continue'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Write-Output '=== Application Error events (ltool) ==='
$evs = Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'; StartTime=(Get-Date).AddDays(-2)} -ErrorAction SilentlyContinue |
  Where-Object { $_.Message -like '*ltool*' }
Write-Output ('count = ' + @($evs).Count)
foreach ($e in $evs | Select-Object -First 4) {
    Write-Output ('--- ' + $e.TimeCreated + ' ---')
    Write-Output $e.Message
}
Write-Output ''
Write-Output '=== WER reports for ltool (bucket / faulting module) ==='
$wer = Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Windows Error Reporting'; StartTime=(Get-Date).AddDays(-2)} -ErrorAction SilentlyContinue |
  Where-Object { $_.Message -like '*ltool*' }
foreach ($e in $wer | Select-Object -First 2) {
    Write-Output ('--- ' + $e.TimeCreated + ' ---')
    Write-Output $e.Message
}
Write-Output ''
Write-Output '=== WER ReportQueue / ReportArchive folders ==='
foreach ($d in @("$env:LOCALAPPDATA\Microsoft\Windows\WER\ReportQueue", "$env:LOCALAPPDATA\Microsoft\Windows\WER\ReportArchive", "$env:PROGRAMDATA\Microsoft\Windows\WER\ReportQueue")) {
    if (Test-Path $d) {
        Get-ChildItem $d -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '*ltool*' } | ForEach-Object {
            Write-Output ('  ' + $_.FullName)
            Get-ChildItem $_.FullName -File -ErrorAction SilentlyContinue | ForEach-Object { Write-Output ('    file: ' + $_.Name) }
            $rf = Join-Path $_.FullName 'Report.wer'
            if (Test-Path $rf) { Get-Content $rf | Where-Object { $_ -match 'App|Exception|Module|Offset|Version|Time' } | ForEach-Object { Write-Output ('    ' + $_) } }
        }
    }
}
Write-Output ''
Write-Output '=== crash dump list ==='
Get-ChildItem "$env:LOCALAPPDATA\CrashDumps" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 5 | ForEach-Object { Write-Output ('  ' + $_.Name + '  ' + $_.LastWriteTime + '  ' + [math]::Round($_.Length/1KB) + 'KB') }
