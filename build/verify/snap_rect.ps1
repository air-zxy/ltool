$ErrorActionPreference = 'Continue'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Snap {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT v, int size);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, char[] s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr h, uint cmd);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
function Dump([IntPtr]$h, [string]$label) {
  $r = New-Object Snap+RECT
  if (-not [Snap]::GetWindowRect($h, [ref]$r)) { return }
  $v = New-Object Snap+RECT
  $hr = [Snap]::DwmGetWindowAttribute($h, 9, [ref]$v, [Runtime.InteropServices.Marshal]::SizeOf($v))
  $buf = New-Object char[] 200
  $n = [Snap]::GetWindowTextW($h, $buf, 200)
  $title = if ($n -gt 0) { New-Object string($buf, 0, $n) } else { '(no title)' }
  if ($hr -ne 0) { Write-Output ("{0}: DWM attr failed hr={1}" -f $label, $hr); return }
  Write-Output ("{0}  [{1}]" -f $label, $title)
  Write-Output ("   GetWindowRect  L{0} T{1} R{2} B{3}" -f $r.Left, $r.Top, $r.Right, $r.Bottom)
  Write-Output ("   DWM visible    L{0} T{1} R{2} B{3}" -f $v.Left, $v.Top, $v.Right, $v.Bottom)
  Write-Output ("   overhang: left={0}  top={1}  right={2}  bottom={3}  (px)" -f `
      ($v.Left - $r.Left), ($v.Top - $r.Top), ($r.Right - $v.Right), ($r.Bottom - $v.Bottom))
}
$shown = 0
foreach ($pr in (Get-Process | Where-Object { $PSItem.MainWindowHandle -ne 0 })) {
  if ($shown -ge 6) { break }
  $h = [IntPtr]$pr.MainWindowHandle
  if (-not [Snap]::IsWindowVisible($h)) { continue }
  Dump $h $pr.ProcessName
  $shown++
}
Write-Output ('dumped ' + $shown + ' windows')
Write-Output '=> 上边过量为 0、左右下约 7px 就是这次要补的差'
Write-Output '=> 上边过量为 0、左右下约 7px 就是这次要补的差'
