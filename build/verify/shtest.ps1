$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
$Auto = [System.Windows.Automation.AutomationElement]
Add-Type -AssemblyName UIAutomationTypes
$exe = 'D:\software\ltool\ltool.exe'
$t = 'D:\project\cpp\ltool\build\verify\shtest'
$seven = 'C:\Program Files\7-Zip\7z.exe'

# 中文字面量全部按码点拼，避免无 BOM 脚本被 GBK 解码
$wDone   = [string][char]0x5B8C + [char]0x6210                       # 完成
$wClose  = [string][char]0x5173 + [char]0x95ED                       # 关闭
$wChinese = [string][char]0x8BF4 + [char]0x660E + '.txt'             # 说明.txt
$wFail   = [string][char]0x5931 + [char]0x8D25                       # 失败

function Item-Zip($dir, $name) {
  if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
  New-Item -ItemType Directory -Force -Path "$dir\src\sub" | Out-Null
  Set-Content -LiteralPath "$dir\src\$wChinese" -Value ('A' * 3000) -Encoding UTF8
  Set-Content -LiteralPath "$dir\src\sub\nested.txt" -Value ('B' * 4000) -Encoding UTF8
  Set-Content -LiteralPath "$dir\src\plain.bin" -Value ([byte[]](1..255)) -Encoding Byte
  & $seven a -tzip "$dir\$name" "$dir\src\*" | Out-Null
}

function Drive($args_, $tag, $log) {
  $p = Start-Process -FilePath $script:exe -ArgumentList $args_ -PassThru
  $cond = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $p.Id)
  $dlg = $null
  $deadline = (Get-Date).AddSeconds(25)
  $text = ''
  while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 300
    foreach ($w in $Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) {
      if ($w.Current.ClassName -eq 'QDialog') { $dlg = $w; break }
    }
    if (-not $dlg) { continue }
    $tc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Text)
    $all = @()
    foreach ($e in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $tc)) { $all += $e.Current.Name }
    $text = ($all -join ' | ')
    if ($text -like ('*' + $script:wDone + '*') -or $text -like ('*' + $script:wFail + '*') -or $text -like '*=%') {
      # 有些失败/完成态写在标签里，等到进度条走完再收尾
      $bc = New-Object System.Windows.Automation.PropertyCondition($Auto::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
      $closeReady = $false
      foreach ($b in $dlg.FindAll([System.Windows.Automation.TreeScope]::Descendants, $bc)) {
        if ($b.Current.Name -eq $script:wClose) {
          $ip = $null
          if ($b.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$ip)) {
            if ($b.Current.Enabled) { $ip.Invoke(); $closeReady = $true }
          }
          break
        }
      }
      if ($closeReady) { break }
    }
  }
  Start-Sleep -Milliseconds 400
  $alive = -not $p.WaitForExit()
  $script:log.Add(('[{0}] dialogText={1} processAlive={2}' -f $tag, $text, $alive))
  if ($alive) { Stop-Process -Id $p.Id -Force }
}

$log = New-Object System.Collections.Generic.List[string]

Item-Zip "$t\ex" 'in.zip'
Drive @('--extract', "$t\ex\in.zip") 'extract-current' $log
Item-Zip "$t\ex2" 'in.zip'
Drive @('--extract-sub', "$t\ex2\in.zip") 'extract-sub' $log

New-Item -ItemType Directory -Force -Path "$t\mk1\packme\deep" | Out-Null
Set-Content -LiteralPath "$t\mk1\packme\$wChinese" -Value ('C' * 2500) -Encoding UTF8
Set-Content -LiteralPath "$t\mk1\packme\deep\x.txt" -Value ('D' * 3500) -Encoding UTF8
Drive @('--compress-zip', "$t\mk1\packme") 'compress-zip-dir' $log
Copy-Item "$t\mk1\packme" "$t\mk2\packme" -Recurse -Force -ErrorAction SilentlyContinue
if (-not (Test-Path "$t\mk2")) { New-Item -ItemType Directory -Force -Path "$t\mk2" | Out-Null }
Copy-Item "$t\mk1\packme" "$t\mk2\packme" -Recurse -Force
Drive @('--compress-7z', "$t\mk2\packme") 'compress-7z-dir' $log
New-Item -ItemType Directory -Force -Path "$t\mk3" | Out-Null
Set-Content -LiteralPath "$t\mk3\one.txt" -Value ('E' * 1500) -Encoding UTF8
Drive @('--compress-zip', "$t\mk3\one.txt") 'compress-zip-file' $log

foreach ($check in @(
  "$t\ex\$wChinese", "$t\ex\sub\nested.txt", "$t\ex\plain.bin",
  "$t\ex2\in\$wChinese", "$t\ex2\in\sub\nested.txt",
  "$t\mk1\packme.zip", "$t\mk2\packme.7z", "$t\mk3\one.zip")) {
  $log.Add(('exists {0} = {1}' -f $check, (Test-Path -LiteralPath $check)))
}
$lt = & $seven l "$t\mk1\packme.zip" 2>&1 | Out-String
$log.Add('7z -l packme.zip: ' + ((($lt -split "`n" | Where-Object { $_ -match 'Ok|Error|\.txt|\.bin' }) -join ' ').Trim()))
$lt2 = & $seven l "$t\mk2\packme.7z" 2>&1 | Out-String
$log.Add('7z -l packme.7z: ' + ((($lt2 -split "`n" | Where-Object { $_ -match 'Ok|Error|\.txt|\.bin' }) -join ' ').Trim()))
[System.IO.File]::WriteAllLines("$t..shtest_report.txt", $log, (New-Object System.Text.UTF8Encoding($true)))
'done'
