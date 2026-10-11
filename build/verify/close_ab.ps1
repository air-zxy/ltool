$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]

function Run-Case([string]$exe, [string]$label) {
    Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { $_.WaitForExit(2000) | Out-Null }
    Get-Process ltool -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Seconds 2
    $proc = Start-Process -FilePath $exe -ArgumentList '--settings' -PassThru
    $win = $null
    for ($i = 0; $i -lt 25; $i++) {
        Start-Sleep -Milliseconds 700
        if ($proc.HasExited) { break }
        $cond = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)
        $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst($TC::Children, $cond)
        if ($win) { break }
    }
    if (-not $win) { Write-Host ($label + ': no window, process exited=' + $proc.HasExited); return }
    $pat = $win.GetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern)
    $pat.Close()
    Start-Sleep -Seconds 3
    $proc.Refresh()
    $alive = -not $proc.HasExited
    Write-Host ($label + ': after closing settings dialog, app alive = ' + $alive)
    if ($alive) { $proc.Kill() }
    return $alive
}

$a = Run-Case 'D:\software\ltool\ltool.exe' 'INSTALLED (before this fix)'
$b = Run-Case 'D:\project\cpp\ltool\build\Release\ltool.exe' 'NEW BUILD (quitOnLastWindowClosed=false)'
Write-Host ('RESULT installed-alive=' + $a + ' new-alive=' + $b)
