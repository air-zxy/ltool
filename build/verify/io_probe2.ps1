$ErrorActionPreference = 'SilentlyContinue'
$sandbox = Join-Path $env:TEMP 'ltool_io_probe2'
if (Test-Path $sandbox) { Remove-Item -Recurse -Force $sandbox }
New-Item -ItemType Directory -Force -Path $sandbox | Out-Null
$env:LTOOL_DATA_DIR = $sandbox
$exe = 'D:\software\ltool\ltool.exe'
$out = Join-Path $env:TEMP 'smoke_stdout.txt'
$p = Start-Process -FilePath $exe -ArgumentList '--smoke' -PassThru `
     -RedirectStandardOutput $out -RedirectStandardError (Join-Path $env:TEMP 'smoke_err.txt')
Add-Type -AssemblyName System.Diagnostics.DiagnosticSource 2>$null
$sw = [Diagnostics.Stopwatch]::StartNew()
$peakR = 0.0; $peakP = 0.0; $sumR = 0.0; $sumP = 0.0
$prevMs = [double]$sw.ElapsedMilliseconds
$prevCpu = $p.TotalProcessorTime.TotalMilliseconds
$cpuMs = 0.0
while (-not $p.HasExited) {
    Start-Sleep -Milliseconds 300
    $p.Refresh()
    $now = [double]$sw.ElapsedMilliseconds
    $dt = ($now - $prevMs) / 1000.0
    if ($dt -le 0) { continue }
    $cpuMs += ($p.TotalProcessorTime.TotalMilliseconds - $prevCpu); $prevCpu = $p.TotalProcessorTime.TotalMilliseconds
    $r = (Get-Counter '\PhysicalDisk(0 C:)\Disk Read Bytes/sec', '\Memory\Pages Input/sec').CounterSamples
    $dr = ($r | Where-Object { $_.Path -like '*Disk Read*' }).CookedValue
    $pg = ($r | Where-Object { $_.Path -like '*Pages Input*' }).CookedValue
    if ($dr -gt $peakR) { $peakR = $dr }
    if ($pg -gt $peakP) { $peakP = $pg }
    $sumR += $dr * $dt
    $sumP += $pg * $dt
    '  t={0,5:N0}ms disk_read={1,12:N0} B/s hardfault={2,7:N0}/s ltool_cpu_sofar={3,6:N2}s' -f `
        $now, $dr, $pg, ($cpuMs / 1000.0)
    $prevMs = $now
}
$p.WaitForExit()
$el = $sw.ElapsedMilliseconds
'' 
'RESULT exit={0} wall={1:N0} ms  ltool_cpu={2:N2} core-seconds ({3:N0}% of one core)' -f `
    $p.ExitCode, $el, ($cpuMs / 1000.0), (100 * ($cpuMs / 1000.0) / ($el / 1000.0))
'PEAK C: read = {0:N0} B/s ({1:N1} MB/s)   PEAK hard faults = {2:N0}/s' -f $peakR, ($peakR / 1MB), $peakP
'ROUGH TOTALS during run: disk read {0:N0} MB, hard faults {1:N0} pages (~{2:N0} MB pulled)' -f `
    ($sumR / 1MB), $sumP, ($sumP * 4KB / 1MB)
'' 
'--- smoke stdout ---'
Get-Content $out
'--- sandbox log ---'
Get-Content (Join-Path $sandbox 'log.txt')
