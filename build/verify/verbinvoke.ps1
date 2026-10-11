$ErrorActionPreference = 'Continue'
$src = @'
using System;
using System.Runtime.InteropServices;
namespace WX {
  public static class SE {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct Info {
      public int cbSize; public uint fMask; public IntPtr hwnd;
      [MarshalAs(UnmanagedType.LPWStr)] public string lpVerb;
      [MarshalAs(UnmanagedType.LPWStr)] public string lpFile;
      [MarshalAs(UnmanagedType.LPWStr)] public string lpParameters;
      [MarshalAs(UnmanagedType.LPWStr)] public string lpDirectory;
      public int nShow; public IntPtr hInstApp; public IntPtr lpIDList;
      [MarshalAs(UnmanagedType.LPWStr)] public string lpClass;
      public IntPtr hkeyClass; public uint dwHotKey; public IntPtr hIcon; public IntPtr hProcess;
    }
    [DllImport("shell32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern bool ShellExecuteExW(ref Info p);
  }
}
'@
Add-Type -TypeDefinition $src

$target = 'D:\project\cpp\ltool\build\verify\verbtest\sample.txt'
if (-not (Test-Path -LiteralPath (Split-Path $target))) { New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null }
Set-Content -LiteralPath $target -Value 'verb test' -Encoding ASCII

function TryVerb($verb, $file) {
  $i = [WX.SE+Info]::new()
  $i.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($i)
  $i.fMask = 0x400 -bor 0x0040
  $i.hwnd = [IntPtr]::Zero
  $i.lpVerb = $verb
  $i.lpFile = $file
  $i.nShow = 1
  $ok = [WX.SE]::ShellExecuteExW([ref]$i)
  $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
  Write-Output ('verb=[' + $verb + '] ok=' + $ok + ' lasterr=' + $err)
}

TryVerb 'ltool\shell\compress-zip' $target
TryVerb 'compress-zip' $target
Start-Sleep -Seconds 3
Write-Output ('ltool 进程数=' + @(Get-Process ltool -ErrorAction SilentlyContinue).Count)
