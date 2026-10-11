$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 3
$null = Start-Process -FilePath $exe -ArgumentList '--show' -PassThru
Start-Sleep -Seconds 2

$cond = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$win = $null
foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    [System.Windows.Automation.TreeScope]::Children, $cond)) {
  if ($w.Current.ClassName -eq 'SearchWindow') { $win = $w; break }
}
if (-not $win) { 'no SearchWindow found'; exit 1 }
$ec = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ClassNameProperty, 'QLineEdit')
$box = $null
foreach ($e in $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, $ec)) { $box = $e; break }
if (-not $box) { 'no input'; exit 1 }
$vp = $null
if (-not $box.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$vp)) { 'no ValuePattern'; exit 1 }
$vp.SetValue('chrome')
Start-Sleep -Seconds 2

function Shot($file) {
  $h = [Grab5]::Biggest([uint32]$script:procId)
  $r = New-Object Grab5+RECT
  [void][Grab5]::GetWindowRect($h, [ref]$r)
  $w = [int]($r.r - $r.l); $ht = [int]($r.b - $r.t)
  "$file rect ${w}x${ht}"
  $bmp = [System.Drawing.Bitmap]::new($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc()
  [void][Grab5]::PrintWindow($h, $hdc, 2)
  $g.ReleaseHdc($hdc)
  $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
}
$script:procId = $p.Id
Shot 'D:\project\cpp\ltool\build\verify\fav_off_typed.png'
Get-Content (Join-Path $env:APPDATA 'ltool\log.txt') -Encoding UTF8 -Tail 3
Stop-Process -Id $p.Id -Force
