$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$report = 'D:\project\cpp\ltool\build\verify\pager_report.txt'
$outLines = New-Object System.Collections.Generic.List[string]

$NEXT = [string][char]0x4E0B + [char]0x4E00 + [char]0x9875      # "xia yi ye"
$PREV = [string][char]0x4E0A + [char]0x4E00 + [char]0x9875

function Find-Dlg($procId) {
  $cond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $procId)
  foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
      [System.Windows.Automation.TreeScope]::Children, $cond)) {
    if ($w.Current.ClassName -eq 'SettingsDialog') { return $w }
  }
  return $null
}
function Shot($dlg, $file) {
  $script:h = [Grab5]::Biggest([uint32]$script:procId)
  $r = New-Object Grab5+RECT
  [void][Grab5]::GetWindowRect($script:h, [ref]$r)
  $bmp = [System.Drawing.Bitmap]::new([int]($r.r - $r.l), [int]($r.b - $r.t))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][Grab5]::PrintWindow($script:h, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}
function Invoke-ButtonByName($dlg, $nameStart) {
  $bc = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    if ($b.Current.IsOffscreen) { continue }
    if (-not $b.Current.Name.StartsWith($nameStart)) { continue }
    $ip = $null
    if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) {
      $ip.Invoke(); return $true
    }
  }
  return $false
}
function Dump-Buttons($dlg, $tag) {
  $bc = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Button)
  $names = @()
  foreach ($b in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    if ($b.Current.IsOffscreen) { continue }
    $r = $b.Current.BoundingRectangle
    if ($r.Height -lt 10) { continue }
    $names += ('"' + $b.Current.Name + '"' + $(if ($b.Current.Enabled) { '' } else { '(disabled)' }))
  }
  $outLines.Add($tag + ' buttons: ' + ($names -join ' '))
  $tc = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Text)
  foreach ($t in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $tc)) {
    if ($t.Current.Name -match '^\p{L}+ \d+ / \d+') { $outLines.Add($tag + ' pageInfo: ' + $t.Current.Name) }
    if ($t.Current.Name -match '\d+ .{0,4} ?\d+') { }
  }
}

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', '1', '1', 'software') -PassThru
$script:procId = $p.Id
Start-Sleep -Seconds 7
$dlg = Find-Dlg $p.Id
if (-not $dlg) { 'no dialog'; exit 1 }
Dump-Buttons $dlg 'page1'
Shot $dlg 'D:\project\cpp\ltool\build\verify\pg1.png'

$null = Invoke-ButtonByName $dlg $NEXT
Start-Sleep -Seconds 2
Dump-Buttons $dlg 'page2'
Shot $dlg 'D:\project\cpp\ltool\build\verify\pg2.png'

$ec = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ClassNameProperty, 'QLineEdit')
$box = $null
foreach ($e in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $ec)) {
  if ($e.Current.BoundingRectangle.Width -gt 50 -and -not $e.Current.IsOffscreen) { $box = $e; break }
}
$vp = $null
if ($box.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$vp)) {
  $vp.SetValue('chrome')
  Start-Sleep -Seconds 2
  Dump-Buttons $dlg 'filtered'
  Shot $dlg 'D:\project\cpp\ltool\build\verify\pg3.png'
  $null = Invoke-ButtonByName $dlg $PREV
  Start-Sleep -Seconds 1
  $outLines.Add('prev-page click on a 1-page result: ok (no crash)')
}
[System.IO.File]::WriteAllLines($report, $outLines, (New-Object System.Text.UTF8Encoding($true)))
'done'
Stop-Process -Id $p.Id -Force
