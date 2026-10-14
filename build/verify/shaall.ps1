param([string]$Build = 'D:\project\cpp\ltool\build\Release', [string]$Inst = 'D:\software\ltool')
$ErrorActionPreference = 'Continue'
$names = @('ltoolcore.dll', 'ltool.exe') + @(Get-ChildItem "$Build\ltool-plugins\*.dll" | ForEach-Object { "ltool-plugins\$($_.Name)" })
$bad = 0
foreach ($n in $names) {
  $bp = Join-Path $Build $n
  $ip = Join-Path $Inst $n
  if (-not (Test-Path $ip)) { $bad++; Write-Output ("MISSING  {0}" -f $n); continue }
  $h1 = (Get-FileHash $bp -Algorithm SHA256).Hash.Substring(0, 12)
  $h2 = (Get-FileHash $ip -Algorithm SHA256).Hash.Substring(0, 12)
  $v = if ($h1 -eq $h2) { 'MATCH' } else { $bad++; 'DIFF ' }
  Write-Output ("{0}  {1,-34} build={2} inst={3}" -f $v, $n, $h1, $h2)
}
$installed = @(Get-ChildItem "$Inst\ltool-plugins\*.dll").Count
$built = @(Get-ChildItem "$Build\ltool-plugins\*.dll").Count
Write-Output ("plugins built={0} installed={1} mismatch-or-missing={2}" -f $built, $installed, $bad)
