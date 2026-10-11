$ErrorActionPreference = 'Continue'
$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace Probe {
  public static class Menu {
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    static extern int SHParseDisplayName(string pszName, IntPtr pbc, out IntPtr ppidl, uint sfgaoIn, out uint psfgaoOut);
    [DllImport("shell32.dll")]
    static extern int SHBindToParent(IntPtr pidl, ref Guid riid, out IntPtr ppv, out IntPtr ppidlLast);
    [DllImport("ole32.dll")]
    static extern void CoTaskMemFree(IntPtr pv);
    [DllImport("user32.dll")]
    static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll")]
    static extern int GetMenuItemCount(IntPtr hMenu);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetMenuStringW(IntPtr hMenu, uint idItem, IntPtr uBuffer, int maxCount, uint flags);
    [DllImport("user32.dll")]
    static extern IntPtr GetSubMenu(IntPtr hMenu, int pos);
    [DllImport("user32.dll")]
    static extern uint GetMenuItemID(IntPtr hMenu, int pos);
    [DllImport("user32.dll")]
    static extern bool DestroyMenu(IntPtr hMenu);

    delegate int GetUIObjectOfDel(IntPtr self, IntPtr hwndOwner, uint cItems, IntPtr apidl, ref Guid riid, uint rgrfEIO, out IntPtr ppvOut);
    delegate int QueryContextMenuDel(IntPtr self, IntPtr hMenu, uint indexMenu, uint idCmdFirst, uint idCmdLast, uint uFlags);

    static T Vtbl<T>(IntPtr iface, int index) where T : class {
      IntPtr vtbl = Marshal.ReadIntPtr(iface);
      IntPtr fn = Marshal.ReadIntPtr(vtbl, index * IntPtr.Size);
      return Marshal.GetDelegateForFunctionPointer<T>(fn);
    }

    static string MenuText(IntPtr hMenu, uint id) {
      int len = GetMenuStringW(hMenu, id, IntPtr.Zero, 0, 0x0400 /*MF_BYCOMMAND*/);
      if (len <= 0) return "";
      IntPtr buf = Marshal.AllocHGlobal((len + 2) * 2);
      try {
        GetMenuStringW(hMenu, id, buf, len + 1, 0x0400);
        return Marshal.PtrToStringUni(buf);
      } finally { Marshal.FreeHGlobal(buf); }
    }

    public static string Dump(string path) {
      var sb = new StringBuilder();
      Guid iidFolder = new Guid("000214E4-0000-0000-C000-000000000046"); // IShellFolder
      Guid iidCtx = new Guid("000214E4-0000-0000-C000-000000000047");    // IContextMenu
      IntPtr pidl; uint attr;
      int hr = SHParseDisplayName(path, IntPtr.Zero, out pidl, 0, out attr);
      if (hr != 0) return "SHParseDisplayName hr=0x" + hr.ToString("X8");
      IntPtr parent; IntPtr last;
      hr = SHBindToParent(pidl, ref iidFolder, out parent, out last);
      if (hr != 0) return "SHBindToParent hr=0x" + hr.ToString("X8");
      try {
        var guoo = Vtbl<GetUIObjectOfDel>(parent, 5);
        IntPtr ctx;
        hr = guoo(parent, IntPtr.Zero, 1, last, ref iidCtx, 0, out ctx);
        if (hr != 0) return "GetUIObjectOf hr=0x" + hr.ToString("X8");
        try {
          IntPtr hMenu = CreatePopupMenu();
          var qcm = Vtbl<QueryContextMenuDel>(ctx, 3);
          hr = qcm(ctx, hMenu, 0, 1, 0x7FFF, 0x00000000 | 0x00000040); // CMF_NORMAL|CMF_EXPLORE
          sb.AppendLine("QueryContextMenu hr=0x" + hr.ToString("X8") + " 项数=" + GetMenuItemCount(hMenu));
          int n = GetMenuItemCount(hMenu);
          for (int i = 0; i < n; i++) {
            uint id = GetMenuItemID(hMenu, i);
            string txt = MenuText(hMenu, id).Replace("&", "").Replace("\t", " ");
            IntPtr sub = GetSubMenu(hMenu, i);
            if (sub == IntPtr.Zero && txt.IndexOf("ltool", StringComparison.OrdinalIgnoreCase) < 0) continue;
            sb.AppendLine("  [" + i + "] id=" + id + " 文本=[" + txt + "] 子菜单=" + (sub == IntPtr.Zero ? "无" : "有"));
            if (sub != IntPtr.Zero) {
              int m = GetMenuItemCount(sub);
              for (int j = 0; j < m; j++) {
                uint sid = GetMenuItemID(sub, j);
                string stxt = MenuText(sub, sid).Replace("&", "");
                IntPtr sub2 = GetSubMenu(sub, j);
                sb.AppendLine("       - " + stxt + (sub2 != IntPtr.Zero ? "  (还有下一级)" : ""));
              }
            }
          }
          DestroyMenu(hMenu);
        } finally { /* 短命进程，不手动 Release */ }
      } finally { CoTaskMemFree(last); CoTaskMemFree(pidl); }
      return sb.ToString();
    }
  }
}
'@
Add-Type -TypeDefinition $src -ReferencedAssemblies System.Runtime
foreach ($p in @('D:\project\cpp\ltool\build\verify\menuprobe\afile.txt',
                 'D:\project\cpp\ltool\build\verify\menuprobe\afolder')) {
  Write-Output ('=== ' + $p)
  try { Write-Output ([Probe.Menu]::Dump($p)) } catch { Write-Output ('EXC: ' + $_.Exception.Message) }
}
