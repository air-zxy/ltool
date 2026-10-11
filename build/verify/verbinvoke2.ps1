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
$dir = 'D:\project\cpp\ltool\build\verify\verbtest\afolder'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Set-Content -LiteralPath "$dir\inside.txt" -Value 'x' -Encoding ASCII

function TryVerb($verb, $file) {
  $i = [WX.SE+Info]::new()
  $i.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($i)
  $i.fMask = 0x400 -bor 0x0040
  $i.lpVerb = $verb
  $i.lpFile = $file
  $i.nShow = 1
  $ok = [WX.SE]::ShellExecuteExW([ref]$i)
  Write-Output ('verb=[' + $verb + '] file=' + $file + ' ok=' + $ok + ' lasterr=' + [Runtime.InteropServices.Marshal]::GetLastWin32Error())
}

TryVerb 'ltool\shell\fileinfo' $dir
TryVerb 'ltool\shell\compress-zip' $dir
Start-Sleep -Seconds 3
Write-Output ('ltool 进程数=' + @(Get-Process ltool -ErrorAction SilentlyContinue).Count)
Write-Output ('afolder.zip=' + (Test-Path 'D:\project\cpp\ltool\build\verify\verbtest\afolder.zip'))
