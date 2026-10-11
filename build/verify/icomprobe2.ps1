$ErrorActionPreference = 'Continue'
$src = @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace MP2 {
  [ComImport, Guid("43826D1E-E618-42B9-8CB2-6582C31DDDD7"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IShellItem {
    void BindToHandler(IntPtr pbc, [In] ref Guid bhid, [In] ref Guid riid, out IntPtr ppv);
    void GetParent(out IShellItem ppsi);
    void GetDisplayName(uint sigdnName, [MarshalAs(UnmanagedType.LPWStr)] out string ppszName);
    void GetAttributes(uint sfgaoMask, out uint psfgaoAttribs);
    void Compare(IShellItem psi, uint hint, out int piOrder);
  }
  [ComImport, Guid("000214E4-0000-0000-C000-000000000047"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IContextMenu {
    [PreserveSig] int QueryContextMenu(IntPtr hMenu, uint indexMenu, uint idCmdFirst, uint idCmdLast, uint uFlags);
    void InvokeCommand(IntPtr pici);
    void GetCommandString(uint idCmd, uint uType, uint reserved, IntPtr pszName, uint cchMax);
  }

  public static class Menu {
    [DllImport("shell32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    static extern void SHCreateItemFromParsingName(string path, IntPtr pbc, [In] ref Guid riid,
                                                   [MarshalAs(UnmanagedType.Interface)] out IShellItem ppv);
    [DllImport("user32.dll")] static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll")] static extern int GetMenuItemCount(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetSubMenu(IntPtr h, int pos);
    [DllImport("user32.dll")] static extern uint GetMenuItemID(IntPtr h, int pos);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern int GetMenuStringW(IntPtr h, uint idItem, StringBuilder buf, int max, uint flags);
    [DllImport("user32.dll")] static extern bool DestroyMenu(IntPtr h);

    static string Text(IntPtr hMenu, uint id) {
      var sb = new StringBuilder(512);
      GetMenuStringW(hMenu, id, sb, sb.Capacity, 0x0400);
      return sb.ToString();
    }

    public static string Dump(string path) {
      var sb = new StringBuilder();
      Guid iidItem = typeof(IShellItem).GUID;
      Guid iidCtx = typeof(IContextMenu).GUID;
      Guid bucket = new Guid("EAB22AC3-30AE-4FFF-B77C-B71C164D9326");
      IShellItem item;
      try { SHCreateItemFromParsingName(path, IntPtr.Zero, ref iidItem, out item); }
      catch (Exception e) { return "SHCreateItemFromParsingName EXC " + e.Message; }
      IntPtr ctxPtr;
      try { item.BindToHandler(IntPtr.Zero, ref bucket, ref iidCtx, out ctxPtr); }
      catch (Exception e) { return "BindToHandler EXC " + e.Message; }
      var ctx = (IContextMenu)Marshal.GetObjectForIUnknown(ctxPtr);
      IntPtr hMenu = CreatePopupMenu();
      int hr = ctx.QueryContextMenu(hMenu, 0, 1, 0x7FFF, 0x40);
      sb.AppendLine("QueryContextMenu hr=0x" + hr.ToString("X8") + " 顶层项数=" + GetMenuItemCount(hMenu));
      int n = GetMenuItemCount(hMenu);
      for (int i = 0; i < n; i++) {
        uint id = GetMenuItemID(hMenu, i);
        string t = Text(hMenu, id).Replace("&", "");
        IntPtr sub = GetSubMenu(hMenu, i);
        if (sub == IntPtr.Zero && t.Length == 0) continue;
        bool hit = t.IndexOf("ltool", StringComparison.OrdinalIgnoreCase) >= 0 || t.IndexOf("ltA") >= 0 || t.IndexOf("ltB") >= 0;
        if (!hit && sub == IntPtr.Zero) continue;
        sb.AppendLine("  [" + i + "] id=" + id + " 文本=[" + t + "] 子菜单=" +
                      (sub == IntPtr.Zero ? "无" : "有(" + GetMenuItemCount(sub) + " 项)"));
        if (sub != IntPtr.Zero) {
          int m = GetMenuItemCount(sub);
          for (int j = 0; j < m; j++) {
            uint sid = GetMenuItemID(sub, j);
            sb.AppendLine("       - [" + j + "] id=" + sid + " 文本=[" + Text(sub, sid).Replace("&", "") + "]");
          }
        }
      }
      DestroyMenu(hMenu);
      Marshal.ReleaseComObject(ctx);
      return sb.ToString();
    }
  }
}
'@
Add-Type -TypeDefinition $src
foreach ($p in @('D:\project\cpp\ltool\build\verify\menutest\sample.txt',
                 'D:\project\cpp\ltool\build\verify\menutest')) {
  Write-Output ('=== ' + $p)
  try { Write-Output ([MP2.Menu]::Dump($p)) } catch { Write-Output ('EXC ' + $_.Exception.Message) }
}
