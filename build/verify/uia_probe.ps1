$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase

$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$p = Start-Process -FilePath $exe -ArgumentList '--settings' -PassThru
Write-Output ("pid=" + $p.Id)

function Dump([string]$s) {
    ($s.ToCharArray() | ForEach-Object { [int]$_ }) -join ','
}

$win = $null
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 800
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
        [System.Windows.Automation.TreeScope]::Children, $cond)
    if ($win) { break }
}
if (-not $win) { Write-Output 'NO-WINDOW'; Stop-Process -Id $p.Id -Force; exit 1 }
Write-Output ('WINDOW namecodes=' + (Dump $win.Current.Name) + ' class=' + $win.Current.ClassName)

$all = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants,
    [System.Windows.Automation.Condition]::TrueCondition)
Write-Output ('descendants=' + $all.Count)
foreach ($e in $all) {
    $r = $e.Current.BoundingRectangle
    Write-Output ('{0} | class={1} | namecodes={2} | rect={3},{4},{5},{6}' -f `
        $e.Current.ControlType.ProgrammaticName, $e.Current.ClassName,
        (Dump $e.Current.Name), [int]$r.X, [int]$r.Y, [int]$r.Width, [int]$r.Height)
}
Stop-Process -Id $p.Id -Force
Write-Output 'DONE'
