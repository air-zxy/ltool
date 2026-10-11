$ErrorActionPreference = 'Continue'
$log = Join-Path $env:APPDATA 'ltool\log.txt'
Write-Output '--- log lines from 2026-10-04 12:0x onward ---'
Get-Content $log | Where-Object { $_ -match '^\[2026-10-04 12:' } | ForEach-Object { Write-Output $_ }
Write-Output ''
Write-Output '--- current ltool processes ---'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Output ('  pid=' + $_.Id + ' start=' + $_.StartTime + ' path=' + $_.Path + ' ws=' + [math]::Round($_.WorkingSet64/1MB,1) + 'MB')
}
Write-Output ''
Write-Output '--- crash dumps for ltool (last 3) ---'
Get-ChildItem "$env:LOCALAPPDATA\CrashDumps" -Filter 'ltool*' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 3 | ForEach-Object { Write-Output ('  ' + $_.Name + '  ' + $_.LastWriteTime) }
Write-Output ''
Write-Output '--- Application log: crashes in the last 24h ---'
Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=(Get-Date).AddHours(-24)} -ErrorAction SilentlyContinue |
  Where-Object { $_.Message -like '*ltool*' -and ($_.ProviderName -match 'Application Error|Windows Error Reporting|\.NET Runtime') } |
  Select-Object -First 6 | ForEach-Object { Write-Output ('  [' + $_.TimeCreated + '] ' + $_.ProviderName + ' :: ' + ($_.Message -split "`n")[0]) }
Write-Output ''
Write-Output '--- tray: does the window exist for the running pid? ---'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
foreach ($p in (Get-Process ltool -ErrorAction SilentlyContinue)) {
    $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $p.Id)
    $wins = [System.Windows.Automation.AutomationElement]::RootElement.FindAll($TC::Children, $cond)
    Write-Output ('  pid ' + $p.Id + ' top-level windows: ' + $wins.Count)
    foreach ($w in $wins) { Write-Output ('    [' + $w.Current.Name + '] offscreen=' + $w.Current.IsOffscreen + ' rect=' + $w.Current.BoundingRectangle) }
}
