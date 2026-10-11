$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\software\ltool\ltool.exe'
$seven = 'C:\Program Files\7-Zip\7z.exe'
$t = 'D:\project\cpp\ltool\build\verify\finalsmoke'
$cn = [string][char]0x8BF4 + [char]0x660E + '.txt'
$wClose = [string][char]0x5173 + [char]0x95ED
$log = New-Object System.Collections.Generic.List[string]

function Win($pid_) {
  $c = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $pid_)
  foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c)) {
    if ($w.Current.ClassName -eq 'QDialog') { return $w }
  }
  return $null
}
function Texts($w) {
  $r = @()
  foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
    if ($e.Current.ControlType.ProgrammaticName -like '*Text*') { $r += ('[' + $e.Current.Name + ']') }
  }
  return ($r -join ' ')
}
function CloseIt($w) {
  $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    if ($b.Current.Name -eq $wClose) {
      $ip = $null
      if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke(); return $true }
    }
  }
  return $false
}
function Flow($flag, $path, $tag) {
  $p = Start-Process -FilePath $script:exe -ArgumentList @($flag, $path) -PassThru
  $w = $null
  for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 200
    $w = Win $p.Id
    if ($w) { break }
  }
  $txt = ''
  for ($i = 0; $i -lt 100; $i++) {
    Start-Sleep -Milliseconds 200
    if ($w) { $txt = Texts $w }
    if ($txt.Length -gt 0 -and $txt -notlike '*准备中*' -and $txt -notlike '*正在取消*') {
      if ((CloseIt $w)) { break }
    }
  }
  Start-Sleep -Milliseconds 500
  $script:log.Add(('{0} text={1} exited={2}' -f $tag, $txt, $p.WaitForExit(3000)))
  if (-not $p.WaitForExit()) { Stop-Process -Id $p.Id -Force }
}

if (Test-Path $t) { Remove-Item $t -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$t\src\sub" | Out-Null
Set-Content -LiteralPath "$t\src\$cn" -Value ('A' * 4000) -Encoding UTF8
Set-Content -LiteralPath "$t\src\sub\nested.txt" -Value ('B' * 6000) -Encoding UTF8
& $seven a -tzip "$t\pack.zip" "$t\src\*" | Out-Null
& $seven a -ttar "$t\pack.tar" "$t\src\*" | Out-Null

Flow '--extract-sub' "$t\pack.zip" 'zip->sub'
Flow '--extract' "$t\pack.tar" 'tar->cur'
Flow '--compress-7z' "$t\src" 'dir->7z'
Flow '--extract-sub' "$t\src.7z" '7z->sub'
Flow '--compress-zip' "$t\src" 'dir->zip'

$rel = @(Get-ChildItem -LiteralPath $t -Recurse -File | ForEach-Object { $_.FullName.Substring($t.Length + 1) })
$log.Add('tree (' + $rel.Count + ' files): ' + ($rel -join ' ; '))
$lt = & $seven t "$t\src.zip" 2>&1 | Out-String
$log.Add('7z t src.zip: ' + ((($lt -split "`n" | Where-Object { $_ -match 'Ok|Error' }) -join ' ').Trim()))
$lt2 = & $seven t "$t\src.7z" 2>&1 | Out-String
$log.Add('7z t src.7z: ' + ((($lt2 -split "`n" | Where-Object { $_ -match 'Ok|Error' }) -join ' ').Trim()))
[System.IO.File]::WriteAllLines("$t\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
