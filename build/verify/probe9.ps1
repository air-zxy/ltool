$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\project\cpp\ltool\build\Release\ltool.exe'
$seven = 'C:\Program Files\7-Zip\7z.exe'
$t = 'D:\project\cpp\ltool\build\verify\probe9'
$cfg = Join-Path $env:APPDATA 'ltool\config.ini'
$cfgBak = "$cfg.bak-probe9"
if (Test-Path -LiteralPath $cfg) { Copy-Item -LiteralPath $cfg $cfgBak -Force }
$cn = [string][char]0x8BF4 + [char]0x660E + '.txt'
$wClose = [string][char]0x5173 + [char]0x95ED
$log = New-Object System.Collections.Generic.List[string]

function Set-Progress($on) {
  # 只动 archiveProgress 这一行，别的一律原样保留；跑完从 .bak-probe9 整体还原
  $lines = @('[General]')
  if (Test-Path -LiteralPath $cfg) { $lines = @(Get-Content -LiteralPath $cfg -Encoding UTF8) }
  $out = New-Object System.Collections.Generic.List[string]
  $done = $false
  $inGeneral = $false
  foreach ($l in $lines) {
    if ($l -match '^\s*\[(.+)\]\s*$') {
      if ($Matches[1] -eq 'ui' -and -not $done) {
        $out.Add($l); $out.Add('archiveProgress=' + $on); $done = $true; $inGeneral = $true; continue
      }
      $inGeneral = ($Matches[1] -eq 'ui')
      $out.Add($l); continue
    }
    if ($l -match '^\s*archiveProgress\s*=') { continue }
    $out.Add($l)
  }
  if (-not $done) { $out = @('[ui]', ('archiveProgress=' + $on)) + $out }
  [System.IO.File]::WriteAllLines($cfg, $out, (New-Object System.Text.UTF8Encoding($false)))
}

function HasDialog($pid_, $ms) {
  $c = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $pid_)
  $deadline = (Get-Date).AddMilliseconds($ms)
  while ((Get-Date) -lt $deadline) {
    foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c)) {
      if ($w.Current.ClassName -eq 'QDialog' -or $w.Current.ClassName -eq 'QMessageBox') { return $w }
    }
    Start-Sleep -Milliseconds 120
  }
  return $null
}

if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$t\src\sub" | Out-Null
Set-Content -LiteralPath "$t\src\$cn" -Value ('A' * 5000) -Encoding UTF8
Set-Content -LiteralPath "$t\src\sub\nested.txt" -Value ('B' * 7000) -Encoding UTF8
& $seven a -tzip "$t\pack.zip" "$t\src\*" | Out-Null
Set-Content -LiteralPath "$t\readme.txt" -Value 'plain text' -Encoding ASCII

# ---------- A: 默认（不弹框）----------
Set-Progress 'false'
$log.Add('A config archiveProgress=false')
$p = Start-Process -FilePath $exe -ArgumentList @('--extract-sub', "$t\pack.zip") -PassThru
$exited = $p.WaitForExit(6000)
$log.Add('A extract 进程自己退出了=' + $exited + ' 窗口=' + ($null -ne (HasDialog $p.Id 300)))
$log.Add('A 解出来的文件=' + ((@(Get-ChildItem -LiteralPath "$t\pack" -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Name }) -join ',')))
$p2 = Start-Process -FilePath $exe -ArgumentList @('--compress-zip', "$t\src") -PassThru
$log.Add('A compress 进程自己退出了=' + $p2.WaitForExit(6000) + ' zip=' + (Test-Path "$t\src.zip"))

# ---------- B: 不弹框时失败仍要弹框 ----------
$p3 = Start-Process -FilePath $exe -ArgumentList @('--extract', "$t\readme.txt") -PassThru
$box = HasDialog $p3.Id 4000
$log.Add('B 非压缩包时弹了框=' + ($null -ne $box) + ' class=' + $(if ($box) { $box.Current.ClassName } else { '-' }))
if ($box) {
  $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $box.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    $ip = $null
    if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke(); break }
  }
  $null = $p3.WaitForExit(4000)
}
$log.Add('B 进程退出=' + $p3.HasExited)

# ---------- C: 开关打开 → 进度窗口回来，并且能取消 ----------
Set-Progress 'true'
$log.Add('C config archiveProgress=true')
$p4 = Start-Process -FilePath $exe -ArgumentList @('--extract-sub', "$t\pack.zip") -PassThru
$dlg = HasDialog $p4.Id 4000
$log.Add('C 弹了进度窗=' + ($null -ne $dlg) + ' 进程还活着=' + (-not $p4.WaitForExit(200)))
if ($dlg) {
  $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    if ($b.Current.Name -eq $wClose) {
      $ip = $null
      if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke() }
      break
    }
  }
  $null = $p4.WaitForExit(5000)
}
$log.Add('C 关掉后进程退出=' + $p4.HasExited)

# ---------- 恢复默认 ----------
Set-Progress 'false'
if (Test-Path -LiteralPath $cfgBak) {
  Copy-Item -LiteralPath $cfgBak $cfg -Force
  Remove-Item -LiteralPath $cfgBak -Force
  $log.Add('config.ini 已从备份整体还原')
}
$log.Add('现在 [ui] 段 = ' + ((@(Get-Content -LiteralPath $cfg -Encoding UTF8) | Where-Object { $_ -like 'archiveProgress*' -or $_ -like 'favoritesOnEmpty*' }) -join ' ; '))
[System.IO.File]::WriteAllLines("$t\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
