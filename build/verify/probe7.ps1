$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\software\ltool\ltool.exe'
$seven = 'C:\Program Files\7-Zip\7z.exe'
$t = 'D:\project\cpp\ltool\build\verify\probe7'
$cn = [string][char]0x8BF4 + [char]0x660E
$wNotArch = [string][char]0x770B + [char]0x8D77 + [char]0x6765 + [char]0x4E0D + [char]0x662F
$wDone = [string][char]0x5B8C + [char]0x6210
$wClose = [string][char]0x5173 + [char]0x95ED
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
function AnyButton($w) {
  # 只按"关闭"，不然会点到"打开位置"上，在他桌面上开一堆资源管理器窗口
  $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    if ($b.Current.Name -ne $wClose) { continue }
    $ip = $null
    if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke(); return $b.Current.Name }
  }
  return ''
}

if (Test-Path $t) { Remove-Item $t -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$t" | Out-Null
Set-Content -LiteralPath "$t\$cn.txt" -Value ('A' * 3000) -Encoding UTF8
& $seven a -tgzip "$t\$cn.txt.gz" "$t\$cn.txt" | Out-Null
Set-Content -LiteralPath "$t\readme.txt" -Value 'plain text here' -Encoding ASCII

# 1) 对单文件 gz 走右键解压（过滤器套裸文件，靠 raw 兜底）
$p = Start-Process -FilePath $exe -ArgumentList @('--extract-sub', "$t\$cn.txt.gz") -PassThru
$w = $null
for ($i = 0; $i -lt 50; $i++) {
  Start-Sleep -Milliseconds 200
  $f = @(Wins $p.Id 'QDialog')
  if ($f.Count -gt 0) { $w = $f[0]; break }
}
$txt = ''
for ($i = 0; $i -lt 60; $i++) {
  Start-Sleep -Milliseconds 200
  if ($w) { $txt = Texts $w }
  if ($txt -like ('*' + $wDone + '*')) { break }
}
$log.Add('gz text=' + $txt)
$log.Add('gz closed=' + (AnyButton $w) + ' exited=' + $p.WaitForExit(4000))
$log.Add('gz files=' + ((@(Get-ChildItem -LiteralPath $t -Recurse -File | ForEach-Object { $_.Name }) -join ',')))

# 2) 对纯文本走右键解压：必须给一句"这不是压缩包"，不能默默产出东西
$before = @(Get-ChildItem -LiteralPath "$t\readme" -ErrorAction SilentlyContinue).Count
$p2 = Start-Process -FilePath $exe -ArgumentList @('--extract', "$t\readme.txt") -PassThru
$w2 = $null
for ($i = 0; $i -lt 50; $i++) {
  Start-Sleep -Milliseconds 200
  foreach ($cls in @('QMessageBox', 'QDialog')) {
    $f = @(Wins $p2.Id $cls)
    if ($f.Count -gt 0) { $w2 = $f[0]; break }
  }
  if ($w2) { break }
}
$txt2 = if ($w2) { Texts $w2 } else { '' }
$log.Add('txt class=' + $(if ($w2) { $w2.Current.ClassName } else { '-' }) + ' text=' + $txt2)
$log.Add('txt says 看起来不是=' + ($txt2 -like ('*' + $wNotArch + '*')))
$log.Add('txt button=' + (AnyButton $w2) + ' exited=' + $p2.WaitForExit(4000))
$log.Add('txt nothing created=' + (-not (Test-Path -LiteralPath "$t\readme")))

[System.IO.File]::WriteAllLines("$t\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
