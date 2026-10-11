$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$root = [System.Windows.Automation.AutomationElement]::RootElement
$cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, 25548)
$wins = $root.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)
Write-Output ("windows=" + $wins.Count)
foreach ($w in $wins) {
    if ($w.Current.IsOffscreen) { continue }
    Write-Output ("win: '" + $w.Current.Name + "' class=" + $w.Current.ClassName +
                  " rect=" + $w.Current.BoundingRectangle)
    $editCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Edit)
    $edits = $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $editCond)
    Write-Output ("  edits=" + $edits.Count)
    foreach ($ed in $edits) {
        if ($ed.Current.IsOffscreen) { continue }
        $help = $ed.Current.HelpText
        $acc = $null
        try {
            $lp = $ed.GetCurrentPattern([System.Windows.Automation.LegacyIAccessiblePattern]::Pattern)
            $acc = $lp.Current.Description
        } catch { }
        Write-Output ("  edit name='" + $ed.Current.Name + "' help='" + $help + "' legacyDesc='" + $acc + "'")
    }
    $listCond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::List)
    $lists = $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $listCond)
    foreach ($l in $lists) {
        if ($l.Current.IsOffscreen) { continue }
        $items = $l.FindAll([System.Windows.Automation.TreeScope]::Children,
            [System.Windows.Automation.Condition]::TrueCondition)
        Write-Output ("  list rows=" + $items.Count)
        $names = @()
        foreach ($it in $items) { $names += ("[" + $it.Current.Name + "]") }
        Write-Output ("   " + ($names -join ' '))
    }
}
