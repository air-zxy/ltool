$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

# ASCII-only script: non-ASCII text in a no-BOM .ps1 breaks PS5.1 line parsing
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath 'D:/project/cpp/ltool/build/Release/ltool.exe' -ArgumentList '--settings' -PassThru
Start-Sleep -Seconds 6

$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$lines = @("pid=" + $p.Id)
foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.IsOffscreen) { continue }
    $lines += ("win class=" + $w.Current.ClassName + " name=" + $w.Current.Name)
}
$cbCond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::CheckBox)
$txCond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Text)
$ci = 0
foreach ($w in $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
    foreach ($c in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cbCond)) {
        $r = $c.Current.BoundingRectangle
        $ck = 'off'
        try {
            $tg = $null
            if ($c.TryGetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern, [ref] $tg)) {
                $ck = 'toggle=' + $tg.Current.ToggleState
            }
        } catch { }
        $lines += ("cb[{0}] {1} rect={2},{3} {4}x{5}" -f $ci, $c.Current.Name,
                  [int]$r.X, [int]$r.Y, [int]$r.Width, [int]$r.Height)
        $ci++
    }
    foreach ($t in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $txCond)) {
        if ($t.Current.Name -like '*UseEnglish*') { $lines += ('text: ' + $t.Current.Name) }
    }
}
[System.IO.File]::WriteAllLines('D:/project/cpp/ltool/build/verify/ime2.txt', $lines,
    (New-Object System.Text.UTF8Encoding($false)))
Write-Output 'wrote ime2.txt'
