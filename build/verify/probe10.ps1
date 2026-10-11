$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$t = 'D:\project\cpp\ltool\build\verify\probe10'
$cn = [string][char]0x8BF4 + [char]0x660E + '.txt'
$wClose = [string][char]0x5173 + [char]0x95ED
$log = New-Object System.Collections.Generic.List[string]

function Dialog($pid_, $ms) {
  $c = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $pid_)
  $end = (Get-Date).AddMilliseconds($ms)
  while ((Get-Date) -lt $end) {
    foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c)) {
      if ($w.Current.ClassName -eq 'QDialog') { return $w }
    }
    Start-Sleep -Milliseconds 150
  }
  return $null
}
function Rows($w) {
  $r = @()
  $tc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Text)
  foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $tc)) { $r += $e.Current.Name }
  $lc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::ListItem)
  foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $lc)) { $r += ('列表项: ' + $e.Current.Name) }
  return $r
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

if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$t\folder\sub" | Out-Null
foreach ($i in 1..3) { [IO.File]::WriteAllBytes("$t\folder\big$i.bin", (New-Object byte[] 1048576)) }
[IO.File]::WriteAllBytes("$t\folder\sub\small.txt", (New-Object byte[] 2048))
Set-Content -LiteralPath "$t\$cn" -Value ('x' * 4000) -Encoding UTF8

# 1) 文件夹：占用（含子文件）应该给出真实数字，不再是空的
$p = Start-Process -FilePath $exe -ArgumentList @('--fileinfo', "$t\folder") -PassThru
$d = Dialog $p.Id 6000
$log.Add('folder 窗口出现=' + ($null -ne $d))
$rows = @()
for ($i = 0; $i -lt 40; $i++) {
  Start-Sleep -Milliseconds 250
  $rows = Rows $d
  $joined = $rows -join ' ~ '
  if ($joined -notlike '*统计中*' -and $joined.Length -gt 0) { break }
}
$log.Add('folder 内容: ' + (($rows | Where-Object { $_ }) -join ' ~ '))
$log.Add('folder 关闭=' + (CloseIt $d) + ' 退出=' + $p.WaitForExit(4000))

# 2) 文件被别的进程独占打开：下面那块要列出进程
$lockPath = "$t\locked.bin"
[IO.File]::WriteAllBytes($lockPath, (New-Object byte[] 5000))
$holder = Start-Process -FilePath 'powershell.exe' -ArgumentList @(
  '-NoProfile','-Command',("[IO.File]::Open('$lockPath','Open','ReadWrite','None'); Start-Sleep -Seconds 25")) -PassThru
Start-Sleep -Seconds 2
$p2 = Start-Process -FilePath $exe -ArgumentList @('--fileinfo', $lockPath) -PassThru
$d2 = Dialog $p2.Id 6000
$rows2 = @()
for ($i = 0; $i -lt 30; $i++) {
  Start-Sleep -Milliseconds 300
  $rows2 = Rows $d2
  if (($rows2 -join ' ~ ') -match 'powershell') { break }
}
$log.Add('locked 内容: ' + (($rows2 | Where-Object { $_ }) -join ' ~ '))
$log.Add('locked 里出现 powershell=' + (($rows2 -join ' ~ ') -match 'powershell'))
$log.Add('locked 关闭=' + (CloseIt $d2) + ' 退出=' + $p2.WaitForExit(4000))
Stop-Process -Id $holder.Id -Force -ErrorAction SilentlyContinue

# 3) 老的 --whohas 开关仍然进同一个窗口
$p3 = Start-Process -FilePath $exe -ArgumentList @('--whohas', "$t\$cn") -PassThru
$d3 = Dialog $p3.Id 6000
$log.Add('whohas 兼容: 窗口出现=' + ($null -ne $d3) + ' 标题=[' + $(if ($d3) { $d3.Current.Name } else { '-' }) + ']')
if ($d3) { $null = CloseIt $d3 }
$null = $p3.WaitForExit(4000)

[System.IO.File]::WriteAllLines("$t\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
