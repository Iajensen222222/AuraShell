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
