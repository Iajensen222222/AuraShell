using System;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.UI.Xaml;

namespace AuraConfig.Services;

/// <summary>
/// Adds a tray icon for the WinUI app so the window can be hidden (rather than
/// closed) and the user can restore it later.  Built on raw Shell_NotifyIcon
/// P/Invoke — WinUI 3 has no NotifyIcon equivalent and pulling WinForms into
/// the project would be heavier than the 200 lines here.
///
/// Lifecycle:
///   var tray = new TrayIconService();
///   tray.Initialize(myWindow);          // creates the message HWND + adds icon
///   // ...window.AppWindow.Closing handler does:
///   //     args.Cancel = true;
///   //     window.AppWindow.Hide();
///   // tray.ShowRequested fires → window.AppWindow.Show()
///   tray.Dispose();                     // on real app shutdown
/// </summary>
public sealed class TrayIconService : IDisposable
{
    /// <summary>Fired when the user double-clicks the tray icon or chooses "Open".</summary>
    public event Action? ShowRequested;
    /// <summary>Fired when the user chooses "Quit" from the tray menu.</summary>
    public event Action? QuitRequested;

    // ── Win32 ──────────────────────────────────────────────────────────────

    private const int  WM_DESTROY      = 0x0002;
    private const int  WM_COMMAND      = 0x0111;
    private const int  WM_LBUTTONUP    = 0x0202;
    private const int  WM_LBUTTONDBLCLK = 0x0203;
    private const int  WM_RBUTTONUP    = 0x0205;
    private const int  WM_USER         = 0x0400;
    private const int  WM_TRAY         = WM_USER + 1;

    private const uint NIM_ADD     = 0x00000000;
    private const uint NIM_MODIFY  = 0x00000001;
    private const uint NIM_DELETE  = 0x00000002;
    private const uint NIM_SETVERSION = 0x00000004;
    private const uint NIF_MESSAGE = 0x00000001;
    private const uint NIF_ICON    = 0x00000002;
    private const uint NIF_TIP     = 0x00000004;
    private const uint NOTIFYICON_VERSION_4 = 4;

    private const uint MF_STRING    = 0x00000000;
    private const uint MF_SEPARATOR = 0x00000800;
    private const uint TPM_RIGHTBUTTON = 0x0002;
    private const uint TPM_RETURNCMD   = 0x0100;

    private const int IDM_OPEN = 1001;
    private const int IDM_QUIT = 1002;

    private delegate IntPtr WndProc(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct WNDCLASSEXW
    {
        public uint    cbSize;
        public uint    style;
        public IntPtr  lpfnWndProc;
        public int     cbClsExtra;
        public int     cbWndExtra;
        public IntPtr  hInstance;
        public IntPtr  hIcon;
        public IntPtr  hCursor;
        public IntPtr  hbrBackground;
        public string? lpszMenuName;
        public string  lpszClassName;
        public IntPtr  hIconSm;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NOTIFYICONDATAW
    {
        public uint   cbSize;
        public IntPtr hWnd;
        public uint   uID;
        public uint   uFlags;
        public uint   uCallbackMessage;
        public IntPtr hIcon;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string szTip;
        public uint   dwState;
        public uint   dwStateMask;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
        public string szInfo;
        public uint   uVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string szInfoTitle;
        public uint   dwInfoFlags;
        public Guid   guidItem;
        public IntPtr hBalloonIcon;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT { public int X; public int Y; }

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern ushort RegisterClassExW(ref WNDCLASSEXW wc);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateWindowExW(uint dwExStyle, string lpClassName, string? lpWindowName,
        uint dwStyle, int X, int Y, int nWidth, int nHeight,
        IntPtr hWndParent, IntPtr hMenu, IntPtr hInstance, IntPtr lpParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr DefWindowProcW(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern bool DestroyWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern IntPtr LoadIconW(IntPtr hInstance, IntPtr lpIconName);

    private static readonly IntPtr IDI_APPLICATION = new IntPtr(32512);
    private const uint HWND_MESSAGE_VAL = unchecked((uint)-3);
    private static readonly IntPtr HWND_MESSAGE = new IntPtr(-3);

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern bool Shell_NotifyIconW(uint dwMessage, ref NOTIFYICONDATAW lpData);

    [DllImport("user32.dll")] private static extern IntPtr CreatePopupMenu();
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool AppendMenuW(IntPtr hMenu, uint uFlags, IntPtr uIDNewItem, string? lpNewItem);
    [DllImport("user32.dll")] private static extern bool DestroyMenu(IntPtr hMenu);
    [DllImport("user32.dll")] private static extern bool TrackPopupMenu(IntPtr hMenu, uint uFlags, int x, int y, int reserved, IntPtr hWnd, IntPtr prcRect);
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT pt);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetModuleHandleW(string? lpModuleName);

    // ── Instance state ─────────────────────────────────────────────────────

    private IntPtr _hwnd;
    private WndProc? _wndProcDelegate;  // keep alive — used by RegisterClassExW
    private bool _iconAdded;
    private bool _disposed;

    private const string ClassName = "AuraShellTrayWindow";
    private const uint   TrayIconId = 1;

    public void Initialize(string tooltip = "AuraShell")
    {
        if (_hwnd != IntPtr.Zero) return;

        var hInstance = GetModuleHandleW(null);

        // Register the message-only window class. RegisterClassExW with the same
        // class name will fail if called twice — we ignore that.
        _wndProcDelegate = WindowProc;
        var wc = new WNDCLASSEXW
        {
            cbSize        = (uint)Marshal.SizeOf<WNDCLASSEXW>(),
            lpfnWndProc   = Marshal.GetFunctionPointerForDelegate(_wndProcDelegate),
            hInstance     = hInstance,
            lpszClassName = ClassName,
        };
        RegisterClassExW(ref wc); // ignore ERROR_CLASS_ALREADY_EXISTS

        _hwnd = CreateWindowExW(0, ClassName, null, 0, 0, 0, 0, 0,
            HWND_MESSAGE, IntPtr.Zero, hInstance, IntPtr.Zero);
        if (_hwnd == IntPtr.Zero)
        {
            Logger.Error("TrayIconService", $"CreateWindowExW failed: {Marshal.GetLastWin32Error()}");
            return;
        }

        var nid = new NOTIFYICONDATAW
        {
            cbSize          = (uint)Marshal.SizeOf<NOTIFYICONDATAW>(),
            hWnd            = _hwnd,
            uID             = TrayIconId,
            uFlags          = NIF_MESSAGE | NIF_ICON | NIF_TIP,
            uCallbackMessage = (uint)WM_TRAY,
            hIcon           = LoadIconW(IntPtr.Zero, IDI_APPLICATION),
            szTip           = tooltip,
        };
        if (!Shell_NotifyIconW(NIM_ADD, ref nid))
        {
            Logger.Error("TrayIconService", $"Shell_NotifyIcon NIM_ADD failed: {Marshal.GetLastWin32Error()}");
        }
        else
        {
            _iconAdded = true;
            nid.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, ref nid);
            Logger.Info("TrayIconService", "Tray icon added");
        }
    }

    private IntPtr WindowProc(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        if (msg == WM_TRAY)
        {
            // NOTIFYICON_VERSION_4 packs the mouse event in LOWORD(lParam).
            uint evt = (uint)(lParam.ToInt64() & 0xFFFF);
            if (evt == WM_LBUTTONUP || evt == WM_LBUTTONDBLCLK)
            {
                ShowRequested?.Invoke();
            }
            else if (evt == WM_RBUTTONUP)
            {
                ShowContextMenu();
            }
            return IntPtr.Zero;
        }
        if (msg == WM_COMMAND)
        {
            int id = (int)(wParam.ToInt64() & 0xFFFF);
            if (id == IDM_OPEN) ShowRequested?.Invoke();
            else if (id == IDM_QUIT) QuitRequested?.Invoke();
            return IntPtr.Zero;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    private void ShowContextMenu()
    {
        var menu = CreatePopupMenu();
        if (menu == IntPtr.Zero) return;

        AppendMenuW(menu, MF_STRING, new IntPtr(IDM_OPEN), "Open AuraShell");
        AppendMenuW(menu, MF_SEPARATOR, IntPtr.Zero, null);
        AppendMenuW(menu, MF_STRING, new IntPtr(IDM_QUIT), "Quit");

        SetForegroundWindow(_hwnd);  // required for TrackPopupMenu to dismiss correctly
        if (GetCursorPos(out var pt))
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.X, pt.Y, 0, _hwnd, IntPtr.Zero);
        DestroyMenu(menu);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try
        {
            if (_iconAdded && _hwnd != IntPtr.Zero)
            {
                var nid = new NOTIFYICONDATAW
                {
                    cbSize = (uint)Marshal.SizeOf<NOTIFYICONDATAW>(),
                    hWnd   = _hwnd,
                    uID    = TrayIconId,
                };
                Shell_NotifyIconW(NIM_DELETE, ref nid);
            }
            if (_hwnd != IntPtr.Zero) DestroyWindow(_hwnd);
        }
        catch { }
    }
}
