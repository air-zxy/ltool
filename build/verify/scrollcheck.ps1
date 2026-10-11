$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -ArgumentList '--scrolltest' -PassThru
Start-Sleep -Seconds 4
$cond = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$dlg = $null
foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    [System.Windows.Automation.TreeScope]::Children, $cond)) {
  if ($w.Current.ClassName -eq 'SettingsDialog') { $dlg = $w; break }
}
if (-not $dlg) { 'no dialog'; exit 1 }
$sc = $null
foreach ($e in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants,
    (New-Object System.Windows.Automation.PropertyCondition(
      [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
      [System.Windows.Automation.ControlType]::ScrollBar)))) {
  if (-not $e.Current.IsOffscreen) { $sc = $e; break }
}
'visible ScrollBar elements: ' + $(if ($sc) { 1 } else { 0 })
$sp = $null
$has = $dlg.TryGetCurrentPattern([System.Windows.Automation.ScrollPattern]::Pattern, [ref]$sp)
'dialog ScrollPattern: ' + $has
if ($has) {
  '  horizontallyScrollable=' + $sp.Current.HorizontallyScrollable +
    ' vertMax=' + [math]::Round($sp.Current.VerticalScrollRange, 1) +
    ' vertNow=' + [math]::Round($sp.Current.VerticalScrollPercent, 1)
  $sp.SetScrollPercent(-1, [System.Windows.Automation.ScrollPattern]::Maximum)
  Start-Sleep -Milliseconds 600
  $h = [Grab5]::Biggest([uint32]$p.Id)
  $r = New-Object Grab5+RECT
  [void][Grab5]::GetWindowRect($h, [ref]$r)
  $bmp = [System.Drawing.Bitmap]::new([int]($r.r - $r.l), [int]($r.b - $r.t))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][Grab5]::PrintWindow($h, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save('D:\project\cpp\ltool\build\verify\cfg_bottom.png', [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  'saved cfg_bottom.png'
}
Stop-Process -Id $p.Id -Force
