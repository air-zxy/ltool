$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\tools_all.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
$Q = [string][char]0x5DE5 + [string][char]0x5177       # 工具
Say ('query = [' + $Q + '] len=' + $Q.Length)

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
Start-Sleep -Seconds 2
$box = $null
foreach ($e in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Edit)))) {
    if (-not $e.Current.IsOffscreen) { $box = $e }
}
$vp = $box.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
foreach ($q in @($Q, 'tools')) {
    $vp.SetValue($q)
    Start-Sleep -Milliseconds 900
    $secs = @(); $rows = 0
    foreach ($li in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem)))) {
        if ($li.Current.IsOffscreen) { continue }
        $n = $li.Current.Name
        if ($n) { $secs += $n } else { $rows++ }
    }
    Say ('query [' + $q + '] -> labels=[' + ($secs -join ' | ') + '] unnamedRows=' + $rows)
}
Flush
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'restarted installed ltool'
Flush
Write-Host 'done'
