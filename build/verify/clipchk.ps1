$ErrorActionPreference = 'Continue'
Write-Output '--- 还在跑的 ltool（含诊断）---'
Get-CimInstance Win32_Process -Filter "Name='ltool.exe'" | ForEach-Object {
  Write-Output ('PID ' + $_.ProcessId + '  ' + $_.CommandLine)
}
Write-Output '--- 剪贴板能不能开 ---'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CB {
  [DllImport("user32.dll")] public static extern bool OpenClipboard(IntPtr h);
  [DllImport("user32.dll")] public static extern bool CloseClipboard();
  [DllImport("user32.dll")] public static extern bool EmptyClipboard();
}
'@
$h = [IntPtr]::Zero
if ([CB]::OpenClipboard($h)) {
  Write-Output 'OpenClipboard 成功（说明没人占着）'
  [void][CB]::CloseClipboard()
} else {
  Write-Output ('OpenClipboard 失败，err=' + [Runtime.InteropServices.Marshal]::GetLastWin32Error())
}
