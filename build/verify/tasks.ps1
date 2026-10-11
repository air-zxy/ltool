$ErrorActionPreference = 'Stop'
$s = New-Object -ComObject Schedule.Service
$s.Connect()
$root = $s.GetFolder('\')
$all = @()
function Walk($f) {
    $t = $f.GetTasks(1)
    foreach ($x in $t) { $script:all += $x }
    $subs = $f.GetFolders(0)
    foreach ($sub in $subs) { Walk $sub }
}
Walk $root
Write-Output ('total tasks visible = ' + $all.Count)
$boot = 0
$logon = 0
foreach ($x in $all) {
    $xml = $x.Xml
    if ($xml -match 'BootTrigger') { $boot++ }
    if ($xml -match 'LogonTrigger') { $logon++ }
}
Write-Output ('boot=' + $boot + ' logon=' + $logon)
$i = 0
foreach ($x in $all) {
    if ($x.Xml -match 'LogonTrigger|BootTrigger') {
        Write-Output ('sample: ' + $x.Path + ' enabled=' + $x.Enabled)
        $i++
        if ($i -ge 6) { break }
    }
}
