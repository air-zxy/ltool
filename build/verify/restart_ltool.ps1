# Restart the installed ltool instance so it re-scans ltool-plugins (it had scanned the
# 17-dll state before network/shell were synced in). ASCII only on purpose: a .ps1 with
# Chinese text and no BOM makes PowerShell throw a parser error that kills the whole script.
$procs = @(Get-Process ltool -ErrorAction SilentlyContinue)
Write-Output ("before: " + $procs.Count + " instance(s)")
foreach ($p in $procs) { Write-Output ("  pid=" + $p.Id + " since=" + $p.StartTime.ToString("HH:mm:ss")) }
$procs | Stop-Process -Force
Start-Sleep -Milliseconds 900
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Start-Sleep -Seconds 2
$after = @(Get-Process ltool -ErrorAction SilentlyContinue)
foreach ($q in $after) { Write-Output ("after: pid=" + $q.Id + " since=" + $q.StartTime.ToString("HH:mm:ss")) }
$log = Join-Path $env:APPDATA 'ltool\log.txt'
Write-Output ("last log: " + (Get-Content $log -Tail 1 -Encoding UTF8))
