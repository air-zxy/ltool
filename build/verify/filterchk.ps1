$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')

function Get-StatusText($root) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Text)
  $out = @()
  foreach ($e in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond)) {
    if ($e.Current.Name -like '*列表显示*') { $out += $e.Current.Name }
  }
  return ($out -join ' | ')
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
'before: ' + (Get-StatusText $dlg)

$ec = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ClassNameProperty, 'QLineEdit')
$box = $null
foreach ($e in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $ec)) {
  $r = $e.Current.BoundingRectangle
  if ($r.Width -gt 50 -and -not $e.Current.IsOffscreen) { $box = $e; break }
}
if (-not $box) { 'no visible QLineEdit found'; exit 1 }
$vp = $null
if (-not $box.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$vp)) {
  'QLineEdit has no ValuePattern'; exit 1
}
$vp.SetValue('chrome')
Start-Sleep -Milliseconds 1200
'after typing "chrome": ' + (Get-StatusText $dlg)
$vp.SetValue('')
Start-Sleep -Milliseconds 1200
'after clearing: ' + (Get-StatusText $dlg)

$h = [Grab5]::Biggest([uint32]$p.Id)
$r = New-Object Grab5+RECT
[void][Grab5]::GetWindowRect($h, [ref]$r)
$bmp = [System.Drawing.Bitmap]::new([int]($r.r - $r.l), [int]($r.b - $r.t))
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][Grab5]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$bmp.Save('D:\project\cpp\ltool\build\verify\filterchk.png', [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
'saved filterchk.png'
Stop-Process -Id $p.Id -Force
