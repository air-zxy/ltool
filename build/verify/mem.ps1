$ErrorActionPreference = 'Stop'
# Sample ltool memory shape: working set / private bytes / handles / threads.
# Keep this file ASCII-only (PS 5.1 without BOM decodes as GBK and eats newlines).
$procs = @(Get-Process ltool -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) { 'no ltool running'; exit 0 }
foreach ($p in $procs) {
  $up = ((Get-Date) - $p.StartTime).TotalMinutes
  '{0} WS={1:N0}MB Private={2:N0}MB PeakWS={3:N0}MB Handles={4} Threads={5} Up={6:N0}min' -f `
    $p.Id, ($p.WorkingSet64 / 1MB), ($p.PrivateMemorySize64 / 1MB), ($p.PeakWorkingSet64 / 1MB), `
    $p.HandleCount, $p.Threads.Count, $up
}
