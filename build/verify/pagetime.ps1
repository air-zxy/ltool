$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition (Get-Content -Raw 'D:\project\cpp\ltool\build\verify\grab_src.txt')
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$NEXT = [string][char]0x4E0B + [char]0x4E00 + [char]0x9875
$report = 'D:\project\cpp\ltool\build\verify\pagetime.txt'
$out = New-Object System.Collections.Generic.List[string]
$log = Join-Path $env:APPDATA 'ltool\log.txt'

function Mark($tag) {
  $lines = @(Get-Content $log -Encoding UTF8 | Where-Object { $_ -like '*uninstall page *' })
  $out.Add($tag + ' -> pages logged: ' + $lines.Count)
  foreach ($l in $lines) { $out.Add('   ' + ($l -replace '^\[[^\]]+\] ', '')) }
}

Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
Start-Sleep -Milliseconds 700
$p = Start-Process -FilePath $exe -ArgumentList @('--settingspage', '1', '1', 'software') -PassThru
Start-Sleep -Seconds 8
Mark 'open'
$cond = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $p.Id)
$dlg = $null
foreach ($w in [System.Windows.Automation.AutomationElement]::RootElement.FindAll(
    [System.Windows.Automation.TreeScope]::Children, $cond)) {
  if ($w.Current.ClassName -eq 'SettingsDialog') { $dlg = $w; break }
}
if (-not $dlg) { $out.Add('no dialog'); [System.IO.File]::WriteAllLines($report, $out); exit 1 }
$bc = New-Object System.Windows.Automation.PropertyCondition(
  [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
  [System.Windows.Automation.ControlType]::Button)
$nb = $null
foreach ($b in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
  if ($b.Current.IsOffscreen) { continue }
  if (-not $b.Current.Name.StartsWith($NEXT)) { continue }
  $nb = $b; break
}
if (-not $nb) { $out.Add('no next button'); [System.IO.File]::WriteAllLines($report, $out); exit 1 }
for ($i = 0; $i -lt 4; $i++) {
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  $ip = $null
  if ($nb.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) {
    $ip.Invoke()
  }
  Start-Sleep -Milliseconds 1200
  $out.Add('next#' + $i + ' wall(invoke+sleep)=' + $sw.ElapsedMilliseconds + 'ms')
}
Mark 'after paging'
Stop-Process -Id $p.Id -Force
[System.IO.File]::WriteAllLines($report, $out, (New-Object System.Text.UTF8Encoding($true)))
'done'
