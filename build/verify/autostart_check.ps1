$ErrorActionPreference = 'Continue'
Write-Output '--- HKCU Run value for ltool ---'
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v ltool 2>&1 | Out-String | Write-Output

Write-Output '--- StartupApproved\Run (Task Manager startup toggle) ---'
reg query "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run" /v ltool 2>&1 | Out-String | Write-Output
$k = Get-Item 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run' -ErrorAction SilentlyContinue
if ($k) {
    $names = $k.GetValueNames()
    Write-Output ('  entries: ' + ($names -join ', '))
    foreach ($n in $names) {
        $b = $k.GetValue($n)
        $disabled = ($b[0] -band 0x80) -ne 0
        Write-Output ('  [' + $n + '] firstbyte=0x' + ('{0:x2}' -f $b[0]) + ' disabled=' + $disabled)
    }
} else {
    Write-Output '  no StartupApproved\Run key at all'
}

Write-Output '--- boot / session times ---'
$os = Get-CimInstance Win32_OperatingSystem
Write-Output ('  last boot: ' + $os.LastBootUpTime)
Write-Output ('  now      : ' + (Get-Date))
Get-CimInstance Win32_LogonSession | ForEach-Object { Write-Output ('  logon ' + $_.LogonId + ' ' + $_.StartTime) }

Write-Output '--- ltool log: first lines and any line from the boot day ---'
$log = Join-Path $env:APPDATA 'ltool\log.txt'
Write-Output ('  file: ' + $log + '  exists=' + (Test-Path $log))
if (Test-Path $log) {
    Get-Content $log -TotalCount 4 | ForEach-Object { Write-Output ('  head: ' + $_) }
    Write-Output '  --- lines mentioning startup/instance/single ---'
    Select-String -Path $log -Pattern 'instance|single|startup|autostart|exit|smoke' | Select-Object -Last 15 | ForEach-Object { Write-Output ('   ' + $_.Line) }
}

Write-Output '--- is a debugger / Image File Execution Options blocking it? ---'
reg query "HKLM\Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\ltool.exe" 2>&1 | Out-String | Write-Output

Write-Output '--- Run key of all users (HKLM) for ltool ---'
reg query "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" /v ltool 2>&1 | Out-String | Write-Output

Write-Output '--- startup folder shortcuts ---'
Get-ChildItem "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup" -ErrorAction SilentlyContinue | ForEach-Object { Write-Output ('  user-startup: ' + $_.Name) }
Get-ChildItem "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\Startup" -ErrorAction SilentlyContinue | ForEach-Object { Write-Output ('  all-users-startup: ' + $_.Name) }

Write-Output '--- scheduled tasks referencing ltool ---'
Get-ScheduledTask -ErrorAction SilentlyContinue | Where-Object { $_.Actions.Execute -like '*ltool*' } | ForEach-Object { Write-Output ('  task: ' + $_.TaskName + ' state=' + $_.State) }
