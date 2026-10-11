$ErrorActionPreference = 'Continue'
# Stop every running ltool.exe so a diagnostic can cold-start. ASCII-only on purpose.
$procs = @(Get-Process ltool -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) { 'nothing running'; exit 0 }
foreach ($p in $procs) {
  'stopping pid ' + $p.Id + ' up=' + [int]((Get-Date) - $p.StartTime).TotalMinutes + 'min'
  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
# Poll instead of a fixed 800ms sleep: the process is usually gone in ~150ms and a
# standing sleep costs every delivery loop. Cap at 3s and report what is left.
$deadline = (Get-Date).AddSeconds(3)
$left = @()
do {
  Start-Sleep -Milliseconds 60
  $left = @(Get-Process ltool -ErrorAction SilentlyContinue)
} while ($left.Count -gt 0 -and (Get-Date) -lt $deadline)
'remaining=' + $left.Count
