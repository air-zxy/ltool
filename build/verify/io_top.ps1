$ErrorActionPreference = 'SilentlyContinue'
$acc = @{}
$runs = 0
for ($i = 0; $i -lt 12; $i++) {
    $x = Get-Counter '\Process(*)\IO Read Bytes/sec', '\Process(*)\IO Write Bytes/sec' -ErrorAction SilentlyContinue
    if (-not $x) { continue }
    $runs++
    foreach ($c in $x.CounterSamples) {
        if ($c.Status -notin 0, 1070016512) { continue }
        if ($c.InstanceName -eq '_total') { continue }
        $k = ($c.InstanceName -replace '#\d+$', '') -replace '\d+$', ''
        if (-not $acc.ContainsKey($k)) { $acc[$k] = @(0.0, 0.0) }
        if ($c.Path.ToLower().EndsWith('io read bytes/sec')) { $acc[$k][0] += $c.CookedValue }
        else { $acc[$k][1] += $c.CookedValue }
    }
    Start-Sleep -Milliseconds 500
}
'samples taken: ' + $runs
'{0,-22} {1,10} {2,10}' -f 'process', 'read KB/s', 'write KB/s'
$acc.GetEnumerator() | Sort-Object { ($_.Value[0] + $_.Value[1]) } -Descending |
    Select-Object -First 10 | ForEach-Object {
        '{0,-22} {1,10:N0} {2,10:N0}' -f $_.Key, ($_.Value[0] / $runs / 1KB), ($_.Value[1] / $runs / 1KB)
    }
'--- every ltool row ---'
$rows = @($acc.GetEnumerator() | Where-Object { $_.Key -like 'ltool*' })
if ($rows.Count -eq 0) { 'ltool absent from the list (0 bytes read/written in every sample window)' }
$rows | ForEach-Object {
    '{0,-22} {1,10:N1} {2,10:N1}' -f $_.Key, ($_.Value[0] / $runs / 1KB), ($_.Value[1] / $runs / 1KB)
}
