$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\favorites2.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }

if (Get-Process ltool -ErrorAction SilentlyContinue) { Say 'ABORT: another ltool running'; Flush; exit 1 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--show' -PassThru
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 600
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
        $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    if ($win) { break }
}
if (-not $win) { Say 'ABORT: no window'; Flush; exit 1 }
Start-Sleep -Seconds 3

$list = $null
foreach ($l in $win.FindAll($TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::List)))) {
    if ($l.Current.Name -or $true) { $list = $l }
}
function SnapAll([string]$tag) {
    $secs = @(); $rows = 0
    foreach ($li in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem)))) {
        $n = $li.Current.Name
        if ($n) { $secs += $n } else { $rows++ }
    }
    Say ('{0}: visibleSections=[{1}] visibleRows={2}' -f $tag, ($secs -join ' + '), $rows)
    return $secs
}
SnapAll 'empty, scrolled to top' | Out-Null

$sp = $null
try { $sp = $list.GetCurrentPattern([System.Windows.Automation.ScrollPattern]::Pattern) } catch { Say ('no ScrollPattern: ' + $_.Exception.Message) }
if ($sp) {
    for ($i = 0; $i -lt 8; $i++) {
        $sp.Scroll([System.Windows.Automation.ScrollAmount]::LargeDown)
        Start-Sleep -Milliseconds 300
    }
    SnapAll 'empty, scrolled to bottom' | Out-Null
    $sp.Scroll([System.Windows.Automation.ScrollAmount]::NoAmount) | Out-Null
}
# does the web section work at all? search a known bookmark keyword
$box = $null
foreach ($e in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Edit)))) {
    if (-not $e.Current.IsOffscreen) { $box = $e }
}
$vp = $box.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
$vp.SetValue(''); Start-Sleep -Milliseconds 600
$vp.SetValue('github'); Start-Sleep -Milliseconds 1200
SnapAll 'query github' | Out-Null
$vp.SetValue(''); Start-Sleep -Milliseconds 800
SnapAll 'empty again (top)' | Out-Null
Flush
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'restarted installed ltool'
Flush
Write-Host 'done'
