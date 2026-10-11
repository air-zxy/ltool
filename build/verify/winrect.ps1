$ErrorActionPreference = 'Continue'
# 量"自动吸附出来的窗口比看到的窗口大多少"：GetWindowRect vs DWM 的可见边界。
Add-Type -Namespace W -Name P -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R rc);
[DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out R v, int s);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rr, B; }
'@
function Both($h) {
  $g = New-Object W.P+R
  $null = [W.P]::GetWindowRect($h, [ref]$g)
  $v = New-Object W.P+R
  $rc = [W.P]::DwmGetWindowAttribute($h, 9, [ref]$v, [Runtime.InteropServices.Marshal]::SizeOf($v))
  if ($rc -ne 0) { $v = $g }
  [pscustomobject]@{
    差左 = $v.L - $g.L; 差上 = $v.T - $g.T; 差右 = $v.Rr - $g.Rr; 差下 = $v.B - $g.B
    GetWindowRect = ('{0}x{1}' -f ($g.Rr - $g.L), ($g.B - $g.T))
    可见 = ('{0}x{1}' -f ($v.Rr - $v.L), ($v.B - $v.T))
  }
}
$h = [W.P]::GetForegroundWindow()
"前台窗口:"; (Both $h) | Format-List
"--- 桌面上所有可见顶层窗口的前后差（左/上/右/下） ---"
Get-Process | Where-Object { $_.MainWindowHandle -ne 0 } | ForEach-Object {
  $o = Both $_.MainWindowHandle
  if ($o) { '{0,-26} {1,8} 差: 左{2} 上{3} 右{4} 下{5}' -f $_.ProcessName, $o.GetWindowRect, $o.差左, $o.差上, $o.差右, $o.差下 }
}
