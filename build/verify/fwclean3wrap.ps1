$p = Start-Process powershell -Verb RunAs -WindowStyle Hidden -PassThru -ArgumentList @(
  '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
  'D:\project\cpp\ltool\build\verify\fwclean3.ps1'
)
$null = $p.WaitForExit(60000)
"wrapper exit=0x{0:X8} exited=$($p.HasExited)"
