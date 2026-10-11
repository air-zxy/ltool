$ErrorActionPreference = 'Continue'
$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace MP {
  public static class Menu {
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    static extern int SHCreateItemFromParsingName(string path, IntPtr pbc, ref Guid riid, out IntPtr ppv);
    [DllImport("user32.dll")] static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll")] static extern int GetMenuItemCount(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetSubMenu(IntPtr h, int pos);
    [DllImport("user32.dll")] static extern uint GetMenuItemID(IntPtr h, int pos);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetMenuStringW(IntPtr h, uint idItem, StringBuilder buf, int max, uint flags);
    [DllImport("user32.dll")] static extern bool DestroyMenu(IntPtr h);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate int BindToHandlerDel(IntPtr self, IntPtr hwnd, ref Guid guidBucket, ref Guid riid, out IntPtr ppv);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate int QueryContextMenuDel(IntPtr self, IntPtr hMenu, uint indexMenu, uint idFirst, uint idLast, uint flags);

    static D V<D>(IntPtr iface, int index) where D : class {
      IntPtr vtbl = Marshal.ReadIntPtr(iface);
      return Marshal.GetDelegateForFunctionPointer<D>(Marshal.ReadIntPtr(vtbl, index * IntPtr.Size));
    }
    static string Text(IntPtr hMenu, uint id) {
      var sb = new StringBuilder(512);
      GetMenuStringW(hMenu, id, sb, sb.Capacity, 0x0400 /*MF_BYCOMMAND*/);
      return sb.ToString();
    }

    public static string Dump(string path) {
      var sb = new StringBuilder();
      Guid iidItem = new Guid("43826D1E-E618-42B9-8CB2-6582C31DDDD7");   // IShellItem
      Guid iidCtx = new Guid("000214E4-0000-0000-C000-000000000047");    // IContextMenu
      Guid bucket = new Guid("EAB22AC3-30AE-4FFF-B77C-B71C164D9326");    // BHID_SFUIObject
      IntPtr item;
      int hr = SHCreateItemFromParsingName(path, IntPtr.Zero, ref iidItem, out item);
      if (hr != 0) return "SHCreateItemFromParsingName hr=0x" + hr.ToString("X8");
      try {
        IntPtr ctx;
        hr = V<BindToHandlerDel>(item, 3)(item, IntPtr.Zero, ref bucket, ref iidCtx, out ctx);
        if (hr != 0) return "BindToHandler hr=0x" + hr.ToString("X8");
        try {
          IntPtr hMenu = CreatePopupMenu();
          hr = V<QueryContextMenuDel>(ctx, 3)(ctx, hMenu, 0, 1, 0x7FFF, 0x40 /*CMF_EXPLORE*/);
          sb.AppendLine("QueryContextMenu hr=0x" + hr.ToString("X8") + "  顶层项数=" + GetMenuItemCount(hMenu));
          int n = GetMenuItemCount(hMenu);
          for (int i = 0; i < n; i++) {
            uint id = GetMenuItemID(hMenu, i);
            string t = Text(hMenu, id).Replace("&", "");
            IntPtr sub = GetSubMenu(hMenu, i);
            bool interesting = t.IndexOf("ltool", StringComparison.OrdinalIgnoreCase) >= 0
                            || t.IndexOf("ltA") >= 0 || t.IndexOf("ltB") >= 0;
            if (sub != IntPtr.Zero) interesting = true;
            if (!interesting) continue;
            sb.AppendLine("  [" + i + "] id=" + id + " 文本=[" + t + "] 子菜单=" + (sub == IntPtr.Zero ? "无" : "有(" + GetMenuItemCount(sub) + " 项)"));
            if (sub != IntPtr.Zero) {
              int m = GetMenuItemCount(sub);
              for (int j = 0; j < m; j++) {
                uint sid = GetMenuItemID(sub, j);
                sb.AppendLine("       - [" + j + "] id=" + sid + " 文本=[" + Text(sub, sid).Replace("&", "") + "]");
              }
            }
          }
          DestroyMenu(hMenu);
        } finally { }
      } finally { }
      return sb.ToString();
    }
  }
}
'@
Add-Type -TypeDefinition $src
foreach ($p in @('D:\project\cpp\ltool\build\verify\menutest\sample.txt',
                 'D:\project\cpp\ltool\build\verify\menutest')) {
  Write-Output ('=== ' + $p)
  try { Write-Output ([MP.Menu]::Dump($p)) } catch { Write-Output ('EXC ' + $_.Exception.Message) }
}
