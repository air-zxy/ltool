$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$exe = $args[0]
$outFile = 'D:/project/cpp/ltool/build/verify/order.txt'

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Output ("stopping pid " + $_.Id)
    Stop-Process -Id $_.Id -Force
}
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -ArgumentList '--show' -PassThru
Start-Sleep -Seconds 4

$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$lines = @()
$found = $false
foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.ClassName -ne 'SearchWindow') { continue }
    if ($w.Current.IsOffscreen) { continue }
    $found = $true
    $listCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::List)
    foreach ($l in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $listCond)) {
        if ($l.Current.IsOffscreen) { continue }
        $items = $l.FindAll([System.Windows.Automation.TreeScope]::Children,
            [System.Windows.Automation.Condition]::TrueCondition)
        $i = 0
        foreach ($it in $items) {
            $n = $it.Current.Name
            if ([string]::IsNullOrWhiteSpace($n)) { $n = '(row)' }
            $lines += ("{0,2} {1}" -f $i, $n)
            $i++
        }
    }
    break
}
if (-not $found) { $lines = @('SearchWindow not found / offscreen') }
[System.IO.File]::WriteAllLines($outFile, $lines, (New-Object System.Text.UTF8Encoding($false)))
Write-Output ("wrote " + $outFile)
