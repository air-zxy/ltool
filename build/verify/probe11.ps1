$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\software\ltool\ltool.exe'
$t = 'D:\project\cpp\ltool\build\verify\probe11'
$wClose = [string][char]0x5173 + [char]0x95ED
$log = New-Object System.Collections.Generic.List[string]

function TryOpenExclusive($path) {
  # 能独占打开 = 没人占着它
  try {
    $fs = [IO.File]::Open($path, 'Open', 'ReadWrite', 'None')
    $fs.Close()
    return '没被占用'
  } catch { return '占用中: ' + $_.Exception.Message.Split([char]10)[0] }
}
function TryRename($path) {
  $tmp = "$path.rentest"
  try { Rename-Item -LiteralPath $path $tmp -ErrorAction Stop; Rename-Item -LiteralPath $tmp $path -ErrorAction Stop; return '可改名/可删' }
  catch { return '改不动: ' + $_.Exception.Message.Split([char]10)[0] }
}
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

if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$t\stuff\sub" | Out-Null
[IO.File]::WriteAllBytes("$t\stuff\one.bin", (New-Object byte[] 4000000))
[IO.File]::WriteAllBytes("$t\stuff\sub\two.bin", (New-Object byte[] 2000000))

# ---------- 1) 默认（不弹框）：跑完之后源文件/包还被占着吗 ----------
$p = Start-Process -FilePath $exe -ArgumentList @('--compress-zip', "$t\stuff") -PassThru
$exited = $p.WaitForExit(15000)
$log.Add('静默模式 进程退出=' + $exited + ' 退出码=' + $(if ($exited) { $p.ExitCode } else { '-' }))
$log.Add('  源文件 one.bin ' + (TryOpenExclusive "$t\stuff\one.bin") + ' / ' + (TryRename "$t\stuff\one.bin"))
$log.Add('  源目录 stuff   ' + (TryRename "$t\stuff"))
$log.Add('  生成包 stuff.zip ' + (TryOpenExclusive "$t\stuff.zip") + ' / ' + (TryRename "$t\stuff.zip"))

# ---------- 2) 进度窗口开着（开关=true）：窗口没关的时候占不占 ----------
$cfg = Join-Path $env:APPDATA 'ltool\config.ini'
$bak = "$cfg.bak-probe11"
Copy-Item -LiteralPath $cfg $bak -Force
$lines = @(Get-Content -LiteralPath $cfg -Encoding UTF8)
$out = New-Object System.Collections.Generic.List[string]
$done = $false
foreach ($l in $lines) {
  if ($l -match '^\s*archiveProgress\s*=') { $out.Add('archiveProgress=true'); $done = $true; continue }
  $out.Add($l)
  if ($l -eq '[ui]' -and -not $done) { $out.Add('archiveProgress=true'); $done = $true }
}
[System.IO.File]::WriteAllLines($cfg, $out, (New-Object System.Text.UTF8Encoding($false)))
Remove-Item -LiteralPath "$t\stuff.zip" -Force -ErrorAction SilentlyContinue
$p2 = Start-Process -FilePath $exe -ArgumentList @('--compress-7z', "$t\stuff") -PassThru
$d = Dialog $p2.Id 8000
$log.Add('进度窗口模式 窗口出现=' + ($null -ne $d) + ' 窗口还开着时进程活着=' + (-not $p2.WaitForExit(300)))
$log.Add('  源文件 one.bin ' + (TryOpenExclusive "$t\stuff\one.bin"))
$log.Add('  源目录 stuff   ' + (TryRename "$t\stuff"))
$log.Add('  生成包 stuff.7z ' + (TryOpenExclusive "$t\stuff.7z"))
if ($d) {
  $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
  foreach ($b in $d.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
    if ($b.Current.Name -eq $wClose) {
      $ip = $null
      if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) { $ip.Invoke() }
      break
    }
  }
}
$log.Add('  点了关闭之后进程退出=' + $p2.WaitForExit(5000))
Copy-Item -LiteralPath $bak $cfg -Force
Remove-Item -LiteralPath $bak -Force
$log.Add('config 已还原')
[System.IO.File]::WriteAllLines("$t\_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
Write-Output 'done'
