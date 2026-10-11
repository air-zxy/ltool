$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$Auto = [System.Windows.Automation.AutomationElement]
$exe = 'D:\software\ltool\ltool.exe'
$t = 'D:\project\cpp\ltool\build\verify\probe2'
$cn = [string][char]0x8BF4 + [char]0x660E + '.txt'

if (Test-Path $t) { Remove-Item $t -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$t\src\sub" | Out-Null
Set-Content -LiteralPath "$t\src\$cn" -Value ('A' * 2000) -Encoding UTF8
Set-Content -LiteralPath "$t\src\sub\nested.txt" -Value ('B' * 3000) -Encoding UTF8
& 'C:\Program Files\7-Zip\7z.exe' a -tzip "$t\box.zip" "$t\src\*" | Out-Null

Write-Output ('before: ' + $cn + ' exists=' + (Test-Path -LiteralPath "$t\$cn"))
$p = Start-Process -FilePath $exe -ArgumentList @('--extract', "$t\box.zip") -PassThru
Write-Output ('pid=' + $p.Id)
for ($i = 0; $i -lt 12; $i++) {
  Start-Sleep -Milliseconds 500
  $all = Get-Process ltool -ErrorAction SilentlyContinue
  $mine = @($all | Where-Object { $_.Id -eq $p.Id })
  $wins = @()
  $cond = New-Object System.Windows.Automation.PropertyCondition($Auto::ProcessIdProperty, $p.Id)
  try { $wins = @($Auto::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond)) } catch {}
  Write-Output ('t=' + (($i + 1)) * 500 + 'ms procAlive=' + ($mine.Count -gt 0) + ' wins=' + $wins.Count +
                ' [' + (($wins | ForEach-Object { $_.Current.ClassName + ':' + $_.Current.Name }) -join ',') + ']' +
                ' extracted=' + (Test-Path -LiteralPath "$t\$cn"))
  if ($wins.Count -gt 0) {
    foreach ($w in $wins) {
      foreach ($e in $w.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) {
        Write-Output ('    ' + $e.Current.ControlType.ProgrammaticName + ' | ' + $e.Current.ClassName + ' | ' + $e.Current.Name)
      }
    }
    break
  }
}
Write-Output ('after: extracted=' + (Test-Path -LiteralPath "$t\$cn") + ' nested=' + (Test-Path -LiteralPath "$t\sub\nested.txt"))
Write-Output ('ltool pids: ' + ((Get-Process ltool -ErrorAction SilentlyContinue | ForEach-Object { $_.Id }) -join ','))
