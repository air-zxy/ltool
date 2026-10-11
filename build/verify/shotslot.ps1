$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$q = $args[0]
$png = $args[1]
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 600
$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', '1', '1') -PassThru
Start-Sleep -Seconds 7
$cond = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$dlg = $null
foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    [System.Windows.Automation.TreeScope]::Children, $cond)) {
  if ($w.Current.ClassName -eq 'SettingsDialog') { $dlg = $w; break }
}
if (-not $dlg) { 'no SettingsDialog'; exit 1 }
$ec = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ClassNameProperty, 'QLineEdit')
$box = $null
foreach ($e in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $ec)) {
  if ($e.Current.BoundingRectangle.Width -gt 50 -and -not $e.Current.IsOffscreen) { $box = $e; break }
}
$vp = $null
if (-not $box.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$vp)) {
  'no ValuePattern'; exit 1
}
$vp.SetValue($q)
Start-Sleep -Milliseconds 1500
$h = [Grab5]::Biggest([uint32]$p.Id)
$r = New-Object Grab5+RECT
[void][Grab5]::GetWindowRect($h, [ref]$r)
$bmp = [System.Drawing.Bitmap]::new([int]($r.r - $r.l), [int]($r.b - $r.t))
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[void][Grab5]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
$bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
'saved ' + $png
Stop-Process -Id $p.Id -Force
