$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\software\ltool\ltool.exe'
$t = 'D:\project\cpp\ltool\build\verify\probe5'
$seven = 'C:\Program Files\7-Zip\7z.exe'
$python = (Get-Command python -ErrorAction SilentlyContinue).Source

$wCancel  = [string][char]0x53D6 + [char]0x6D88                                  # 取消
$wClose   = [string][char]0x5173 + [char]0x95ED                                  # 关闭
$wDonePfx = [string][char]0x5DF2 + [char]0x53D6 + [char]0x6D88                    # 已取消
$wNotArch = [string][char]0x770B + [char]0x8D77 + [char]0x6765 + [char]0x4E0D + [char]0x662F  # 看起来不是

$log = New-Object System.Collections.Generic.List[string]

function Wins($pid_, $class) {
  $c = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $pid_)
  $r = @()
  foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c)) {
    if ($w.Current.ClassName -eq $class) { $r += $w }
  }
  return $r
}
function Texts($w) {
  $r = @()
  foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
    if ($e.Current.ControlType.ProgrammaticName -like '*Text*') { $r += ('[' + $e.Current.Name + ']') }
  }
  return ($r -join ' ')
}
function Click($w, $name) {
  $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    if ($b.Current.Name -eq $name) {
      $ip = $null
      if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke(); return $true }
    }
  }
  return $false
}

if (Test-Path $t) { Remove-Item $t -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$t\big" | Out-Null
for ($i = 0; $i -lt 3; $i++) {
  & fsutil file createnew "$t\big\zeros$i.bin" 125829120 | Out-Null
}
$log.Add('big dir bytes=' + ((Get-ChildItem "$t\big" | Measure-Object Length -Sum).Sum))

# --- A: 压缩中途取消，半成品压缩包必须被删掉 ---
$p = Start-Process -FilePath $exe -ArgumentList @('--compress-zip', "$t\big") -PassThru
$w = $null
for ($i = 0; $i -lt 40; $i++) {
  Start-Sleep -Milliseconds 200
  $f = @(Wins $p.Id 'QDialog')
  if ($f.Count -gt 0) { $w = $f[0]; break }
}
$log.Add('A dialog found=' + ($null -ne $w))
$log.Add('A clicked cancel=' + (Click $w $wCancel))
$txtA = ''
for ($i = 0; $i -lt 150; $i++) {
  Start-Sleep -Milliseconds 200
  $txtA = Texts $w
  if ($txtA -like ('*' + $wDonePfx + '*')) { break }
}
$log.Add('A text=' + $txtA)
$log.Add('A contains 已取消=' + ($txtA -like ('*' + $wDonePfx + '*')))
$log.Add('A zip left behind=' + (Test-Path "$t\big.zip") + ' 7z exists=' + (Test-Path "$t\big.7z"))
$log.Add('A closed=' + (Click $w $wClose) + ' exited=' + $p.WaitForExit(4000))

# --- B: 解压中途取消（用 4000 个小文件拖慢，才来得及按取消），已解出来的东西保留并说明 ---
& $python "$PSScriptRoot\manyfiles.py" "$t\many" 4000 | Out-Null
& $seven a -t7z "$t\many.7z" "$t\many\*" | Out-Null
New-Item -ItemType Directory -Force -Path "$t\exdir" | Out-Null
Copy-Item "$t\many.7z" "$t\exdir\box.7z"
$p2 = Start-Process -FilePath $exe -ArgumentList @('--extract', "$t\exdir\box.7z") -PassThru
$w2 = $null
for ($i = 0; $i -lt 40; $i++) {
  Start-Sleep -Milliseconds 150
  $f = @(Wins $p2.Id 'QDialog')
  if ($f.Count -gt 0) { $w2 = $f[0]; break }
}
$log.Add('B dialog found=' + ($null -ne $w2))
$log.Add('B clicked cancel=' + (Click $w2 $wCancel))
$txtB = ''
for ($i = 0; $i -lt 150; $i++) {
  Start-Sleep -Milliseconds 200
  $txtB = Texts $w2
  if ($txtB -like ('*' + $wDonePfx + '*')) { break }
}
$log.Add('B text=' + $txtB)
$log.Add('B partial files=' + (@(Get-ChildItem "$t\exdir" -Recurse -File -ErrorAction SilentlyContinue).Count))
$log.Add('B closed=' + (Click $w2 $wClose) + ' exited=' + $p2.WaitForExit(4000))

# --- C: 对非压缩包解压，应当给一句人话而不是闪退 ---
Set-Content -LiteralPath "$t\readme.txt" -Value 'not an archive' -Encoding ASCII
$p3 = Start-Process -FilePath $exe -ArgumentList @('--extract', "$t\readme.txt") -PassThru
$w3 = $null
for ($i = 0; $i -lt 40; $i++) {
  Start-Sleep -Milliseconds 200
  foreach ($cls in @('QMessageBox', 'QDialog')) {
    $f = @(Wins $p3.Id $cls)
    if ($f.Count -gt 0) { $w3 = $f[0]; break }
  }
  if ($w3) { break }
}
$log.Add('C dialog found=' + ($null -ne $w3) + ' class=' + $(if ($w3) { $w3.Current.ClassName } else { '-' }))
$txtC = if ($w3) { Texts $w3 } else { '' }
$log.Add('C text=' + $txtC)
$log.Add('C says 看起来不是=' + ($txtC -like ('*' + $wNotArch + '*')))
if ($w3) { $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button); foreach ($b in $w3.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) { $ip = $null; if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke(); break } } }
Start-Sleep -Milliseconds 800
$log.Add('C exited=' + $p3.WaitForExit(4000))

[System.IO.File]::WriteAllLines("$t\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
