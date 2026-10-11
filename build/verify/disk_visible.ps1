$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, WindowsBase
Add-Type -AssemblyName System.Drawing
function Ch([int[]]$c) { $s = ''; foreach ($x in $c) { $s += [string][char]$x }; return $s }
$AE = [System.Windows.Automation.AutomationElement]
$TC = [System.Windows.Automation.TreeScope]
$CT = [System.Windows.Automation.ControlType]
$log = [System.Collections.Generic.List[string]]::new()
function Say([string]$s) { $log.Add($s); Write-Host ('| ' + $s) }
function Flush { [System.IO.File]::WriteAllText('D:\project\cpp\ltool\build\verify\disk_visible.txt', ($log -join "`n"), (New-Object System.Text.UTF8Encoding($false))) }

$p = Get-Process ltool -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Say 'no ltool running'; Flush; exit 1 }
Say ('pid=' + $p.Id)
$win = [System.Windows.Automation.AutomationElement]::RootElement.FindFirst(
    $TC::Children, (New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $p.Id)))
if (-not $win) { Say 'no top-level window'; Flush; exit 1 }
Say ('window=[' + $win.Current.Name + '] rect=' + $win.Current.BoundingRectangle)
Say '--- VISIBLE text nodes only ---'
foreach ($t in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Text)))) {
    if ($t.Current.Name -and -not $t.Current.IsOffscreen) { Say ('  ' + $t.Current.Name) }
}
Say '--- VISIBLE combo boxes ---'
foreach ($c in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::ComboBox)))) {
    Say ('  offscreen=' + $c.Current.IsOffscreen + ' name=[' + $c.Current.Name + '] w=' + [math]::Round($c.Current.BoundingRectangle.Width,0))
}
Say '--- VISIBLE buttons (first 25) ---'
$n = 0
foreach ($b in $win.FindAll($TC::Descendants, (New-Object System.Windows.Automation.PropertyCondition($AE::ControlTypeProperty, $CT::Button)))) {
    if (-not $b.Current.IsOffscreen -and $b.Current.Name) { Say ('  ' + $b.Current.Name); $n++ }
    if ($n -gt 24) { break }
}
$r = $win.Current.BoundingRectangle
$bmp = New-Object System.Drawing.Bitmap([int]$r.Width, [int]$r.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen([int]$r.X, [int]$r.Y, 0, 0, $bmp.Size)
$g.Dispose(); $bmp.Save('D:\project\cpp\ltool\build\verify\disk_visible.png', [System.Drawing.Imaging.ImageFormat]::Png)
Flush
Write-Host 'done'
