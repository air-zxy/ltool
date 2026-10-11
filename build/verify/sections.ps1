$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$procId = [int] (Start-Process -FilePath 'D:/project/cpp/ltool/build/Release/ltool.exe' `
    -ArgumentList '--show' -PassThru).Id
Start-Sleep -Seconds 4

$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $procId)
$lines = @("pid=$procId")
foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.ClassName -ne 'SearchWindow') { continue }
    $listCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::List)
    $sbCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::ScrollBar)
    $list = $null
    $vsb = $null
    foreach ($l in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $listCond)) {
        if (-not $l.Current.IsOffscreen) { $list = $l; break }
    }
    foreach ($s in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $sbCond)) {
        if ($s.Current.BoundingRectangle.Width -lt 12) { $vsb = $s; break }
    }
    if ($null -eq $list) { $lines += 'no list'; break }
# (ascii-only: non-ASCII comments break PS5.1 no-BOM parsing)
    $tp = $null
    if ($w.TryGetCurrentPattern([System.Windows.Automation.TransformPattern]::Pattern, [ref] $tp)) {
        $tp.Resize(760, 1400)
        Start-Sleep -Milliseconds 500
        $lines += 'resized window'
    } else {
        $lines += 'no TransformPattern'
    }
# (ascii-only: non-ASCII comments break PS5.1 no-BOM parsing)
    $seen = New-Object System.Collections.Generic.List[string]
    for ($round = 0; $round -lt 3; $round++) {
        $items = $list.FindAll([System.Windows.Automation.TreeScope]::Children,
            [System.Windows.Automation.Condition]::TrueCondition)
        foreach ($it in $items) {
            $n = $it.Current.Name
            if ($n.StartsWith([string][char]0x2500) -and -not $seen.Contains($n)) { $seen.Add($n) }
        }
        $lines += ("round{0} rows={1}" -f $round, $items.Count)
        Start-Sleep -Milliseconds 300
    }
    $lines += ('sections: ' + ($seen -join ' | '))
    break
}
[System.IO.File]::WriteAllLines('D:/project/cpp/ltool/build/verify/sections.txt', $lines,
    (New-Object System.Text.UTF8Encoding($false)))
Write-Output 'wrote sections.txt'
