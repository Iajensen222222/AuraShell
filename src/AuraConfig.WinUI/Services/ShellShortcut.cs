using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

namespace AuraConfig.Services;

/// <summary>Creates Windows .lnk shortcut files via IShellLink COM.</summary>
internal static class ShellShortcut
{
    [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
    private class ShellLinkClass { }

    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown),
     Guid("000214F9-0000-0000-C000-000000000046")]
    private interface IShellLink
    {
        void GetPath([MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder buf,
                     int maxLen, IntPtr pfd, uint flags);
        void GetIDList(out IntPtr ppidl);
        void SetIDList(IntPtr pidl);
        void GetDescription([MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder buf, int maxLen);
        void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string desc);
        void GetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder buf, int maxLen);
        void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string dir);
        void GetArguments([MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder buf, int maxLen);
        void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string args);
        void GetHotkey(out short hotkey);
        void SetHotkey(short hotkey);
        void GetShowCmd(out int show);
        void SetShowCmd(int show);
        void GetIconLocation([MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder buf,
                              int maxLen, out int iconIndex);
        void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string path, int iconIndex);
        void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string relPath, uint reserved);
        void Resolve(IntPtr hwnd, uint flags);
        void SetPath([MarshalAs(UnmanagedType.LPWStr)] string path);
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern void SHChangeNotify(int wEventId, int uFlags,
                                               string? dwItem1, IntPtr dwItem2);

    private const int SHCNE_UPDATEITEM = 0x00002000;
    private const int SHCNF_PATH       = 0x0001;
    private const int STGM_READ        = 0x00000000;

    /// <summary>
    /// Sets a custom icon on an existing .lnk shortcut file.
    /// <paramref name="iconSource"/> may be an .ico, .exe, or .dll path.
    /// <paramref name="iconIndex"/> selects which icon within a multi-icon file.
    /// </summary>
    public static bool SetIcon(string lnkPath, string iconSource, int iconIndex = 0)
    {
        try
        {
            var link    = (IShellLink)new ShellLinkClass();
            var persist = (IPersistFile)link;

            persist.Load(lnkPath, STGM_READ);
            link.SetIconLocation(iconSource, iconIndex);
            persist.Save(lnkPath, fRemember: true);

            SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH, lnkPath, IntPtr.Zero);
            return true;
        }
        catch { return false; }
    }

    /// <summary>Removes any custom icon from a .lnk shortcut (restores default).</summary>
    public static bool ClearIcon(string lnkPath)
    {
        try
        {
            var link    = (IShellLink)new ShellLinkClass();
            var persist = (IPersistFile)link;

            persist.Load(lnkPath, STGM_READ);
            link.SetIconLocation(string.Empty, 0);
            persist.Save(lnkPath, fRemember: true);

            SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH, lnkPath, IntPtr.Zero);
            return true;
        }
        catch { return false; }
    }

    public static bool Create(string lnkPath, string targetPath,
                               string workingDir = "", string description = "")
    {
        try
        {
            var link    = (IShellLink)new ShellLinkClass();
            var persist = (IPersistFile)link;

            link.SetPath(targetPath);
            if (!string.IsNullOrEmpty(workingDir))  link.SetWorkingDirectory(workingDir);
            if (!string.IsNullOrEmpty(description)) link.SetDescription(description);

            // Embed the app icon if it's available alongside the executable.
            string iconPath = System.IO.Path.Combine(
                System.IO.Path.GetDirectoryName(targetPath) ?? "",
                "Assets", "AppIcon.ico");
            if (System.IO.File.Exists(iconPath))
                link.SetIconLocation(iconPath, 0);

            persist.Save(lnkPath, fRemember: true);
            return true;
        }
        catch { return false; }
    }
}
