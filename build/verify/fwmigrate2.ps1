$inner = 'D:\project\cpp\ltool\build\verify\fwmigrate2_inner.ps1'
$b = [System.IO.File]::ReadAllBytes($inner)
if (-not ($b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)) {
  [System.IO.File]::WriteAllBytes($inner, ([byte[]](0xEF,0xBB,0xBF) + $b))
  'bom added'
} else { 'bom already present' }
$p = Start-Process powershell -Verb RunAs -WindowStyle Hidden -PassThru -ArgumentList @(
  '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $inner)
$null = $p.WaitForExit(90000)
"elevated exit code = " + $p.ExitCode
