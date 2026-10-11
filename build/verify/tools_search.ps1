$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$T_TOOLS   = Ch @(0x5DE5, 0x5177)                 # 工具 (section)
$T_CLIPSEC = Ch @(0x526A, 0x8D34, 0x677F, 0x5386, 0x53F2)  # 剪贴板历史
$Q_TOOLS   = Ch @(0x5DE5, 0x5177)                 # query "工具"
$Q_DISK    = Ch @(0x78C1, 0x76D8)                 # query "磁盘"
$Q_NET     = Ch @(0x7F51, 0x7EDC)                 # query "网络"
$Q_AD      = Ch @(0x5E7F, 0x544A)                 # query "广告"
$Q_FMT     = Ch @(0x683C, 0x5F0F, 0x5316)         # query "格式化"

$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\tools_search.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }

if (Get-Process ltool -ErrorAction SilentlyContinue) { Say 'ABORT: another ltool running'; Flush; exit 1 }
$proc = Start-Process -FilePath 'D:\project\cpp\ltool\build\Release\ltool.exe' -ArgumentList '--show' -PassThru
$win = $null
for ($i = 0; $i -lt 25; $i++) {
    Start-Sleep -Milliseconds 600
    $win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
        $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $proc.Id)))
    if ($win) { break }
}
if (-not $win) { Say 'ABORT: no launcher window'; Flush; exit 1 }
Start-Sleep -Seconds 2
Say ('window=[' + $win.Current.Name + '] rect=' + $win.Current.BoundingRectangle)

$box = $null
foreach ($e in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Edit)))) {
    if (-not $e.Current.IsOffscreen) { $box = $e }
}
if (-not $box) { Say 'ABORT: no visible Edit control'; Flush; exit 1 }
$vp = $null
try { $vp = $box.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern) } catch { Say ('no ValuePattern: ' + $_.Exception.Message) }
if (-not $vp) { Say 'ABORT: cannot set the query without typing'; Flush; exit 1 }

function Probe([string]$query, [string]$label, [switch]$dump) {
    $vp.SetValue($query)
    Start-Sleep -Milliseconds 900          # 120ms debounce + search
    $secs = @(); $rows = 0; $names = @()
    foreach ($li in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ListItem)))) {
        if ($li.Current.IsOffscreen) { continue }
        $n = $li.Current.Name
        $names += ('[' + $n + ']')
        if ($n -and $n.Contains('──')) { $secs += $n }
        elseif ($n -and $n.Contains($T_TOOLS)) { $secs += $n }
        elseif ($n -and $n.Contains($T_CLIPSEC)) { $secs += $n }
        else { $rows++ }
    }
    Say ('{0} "{1}" -> sections=[{2}] rows={3}' -f $label, $query, ($secs -join ' | '), $rows)
    if ($dump) { foreach ($x in $names) { Say ('    item ' + $x) } }
    return $secs
}

Probe 'json' 'exact keyword  ' -dump | Out-Null
Probe 'base64' 'exact keyword  ' | Out-Null
Probe 'b64' 'exact keyword  ' | Out-Null
Probe 'clip' 'command block  ' | Out-Null
Probe 'rescan' 'command block ' | Out-Null
Probe $Q_NET 'exact tool name' | Out-Null
Probe $Q_FMT 'exact keyword  ' | Out-Null
Probe $Q_AD 'LOOSE (append) ' | Out-Null
Probe $Q_DISK 'LOOSE (append)' -dump | Out-Null
Probe $Q_TOOLS 'catch-all      ' -dump | Out-Null
Probe 'cmd' 'unrelated query ' | Out-Null
Probe 'chrome' 'app query      ' -dump | Out-Null
Flush
Stop-Process -Name ltool -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Start-Process -FilePath 'D:\software\ltool\ltool.exe'
Say 'stopped test instance, restarted installed one'
Flush
Write-Host 'done'
