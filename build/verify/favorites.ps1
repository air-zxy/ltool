$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\favorites.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }
function Snap([string]$tag) {
    # 不按 IsOffscreen 过滤：QListWidget 只把视口内的条目算"可见"，过滤掉就看不见后面的节了
    $secs = @(); $rows = 0
    foreach ($li in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem)))) {
        $n = $li.Current.Name
        if ($n) { $secs += $n } else { $rows++ }
    }
    $r = $win.Current.BoundingRectangle
    Say ('{0}: sections=[{1}] rows={2} window={3}x{4}' -f $tag, ($secs -join ' '), $rows, [math]::Round($r.Width), [math]::Round($r.Height))
    return $secs
}

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
Say '--- empty query on open (the favorites panel) ---'
Snap 'open' | Out-Null

$box = $null
foreach ($e in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Edit)))) {
    if (-not $e.Current.IsOffscreen) { $box = $e }
}
$vp = $box.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)

# regression: typed queries still route correctly
$vp.SetValue('json'); Start-Sleep -Milliseconds 700; Snap 'query json' | Out-Null
$vp.SetValue('chrome'); Start-Sleep -Milliseconds 900; Snap 'query chrome' | Out-Null

# how long until the favorites panel is rebuilt after clearing the query
$sw = [Diagnostics.Stopwatch]::StartNew()
$vp.SetValue('')
$ready = $false
while ($sw.Elapsed.TotalMilliseconds -lt 4000) {
    Start-Sleep -Milliseconds 50
    foreach ($li in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem)))) {
        if ($li.Current.Name -and $li.Current.Name.Contains([string][char]0x5E94 + [string][char]0x7528)) { $ready = $true; break }
    }
    if ($ready) { break }
}
$sw.Stop()
Say ('favorites rebuilt in ' + [math]::Round($sw.Elapsed.TotalMilliseconds) + 'ms  (found 应用 section = ' + $ready + ')')
Snap 'cleared' | Out-Null
Flush
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'restarted installed ltool'
Flush
Write-Host 'done'
