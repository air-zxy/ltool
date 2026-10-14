$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')

# "lie biao xian shi" built from code points: a no-BOM .ps1 is decoded as GBK by PS5.1
$want = [string][char]0x5217 + [char]0x8868 + [char]0x663E + [char]0x793A

function Report($root, $tag) {
  $tc = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Text)
  $st = @()
  foreach ($e in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $tc)) {
    if ($e.Current.Name -like ('*' + $want + '*')) { $st += $e.Current.Name }
  }
  $lc = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::ListItem)
  $n = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $lc).Count
  $tag + ': listItems=' + $n + ' status=' + ($st -join ' / ')
}

function Shot($pid_, $file) {
  $h = [Grab5]::Biggest([uint32]$pid_)
  $r = New-Object Grab5+RECT
  [void][Grab5]::GetWindowRect($h, [ref]$r)
  $bmp = [System.Drawing.Bitmap]::new([int]($r.r - $r.l), [int]($r.b - $r.t))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][Grab5]::PrintWindow($h, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}

$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 600
$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', '1', '1', 'software') -PassThru
Start-Sleep -Seconds 6

$cond = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$dlg = $null
foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    [System.Windows.Automation.TreeScope]::Children, $cond)) {
  if ($w.Current.ClassName -eq 'SettingsDialog') { $dlg = $w; break }
}
if (-not $dlg) { 'no SettingsDialog window'; exit 1 }
Report $dlg 'before'

$ec = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ClassNameProperty, 'QLineEdit')
$box = $null
foreach ($e in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $ec)) {
  if ($e.Current.BoundingRectangle.Width -gt 50 -and -not $e.Current.IsOffscreen) { $box = $e; break }
}
if (-not $box) { 'no visible QLineEdit'; exit 1 }
$vp = $null
if (-not $box.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$vp)) {
  'no ValuePattern'; exit 1
}
$vp.SetValue('chrome')
Start-Sleep -Milliseconds 1500
Report $dlg 'typed chrome'
Shot $p.Id 'D:\project\cpp\ltool\build\verify\filterchk.png'
$vp.SetValue('')
Start-Sleep -Milliseconds 1500
Report $dlg 'cleared'
Stop-Process -Id $p.Id -Force
