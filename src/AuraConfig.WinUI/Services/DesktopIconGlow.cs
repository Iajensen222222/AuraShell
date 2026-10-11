using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

namespace AuraConfig.Services;

/// <summary>
/// Renders a colored glow overlay around desktop icons on hover.
/// Runs on a dedicated background thread that owns all GDI and Win32 window state.
///
/// Cross-process note: SysListView32 (desktop icon list) lives in explorer.exe.
/// LVM_GETITEMRECT carries a pointer argument; passing a local pointer cross-process
/// causes an access violation in Explorer.  We therefore allocate the RECT inside
/// Explorer's address space via VirtualAllocEx / WriteProcessMemory / ReadProcessMemory
/// before sending the message.
/// </summary>
public sealed class DesktopIconGlow : IDisposable
{
    // ── P/Invoke ───────────────────────────────────────────────────────────

    // Window management
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern ushort RegisterClassExW(ref WNDCLASSEXW wc);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr CreateWindowExW(int exStyle, string cls, string? title, int style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr hInst, IntPtr param);
    [DllImport("user32.dll")] static extern bool   DestroyWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern bool   SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] static extern bool   IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] static extern IntPtr DefWindowProcW(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);

    // Layered window compositing
    [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")] static extern int    ReleaseDC(IntPtr hwnd, IntPtr hdc);
    [DllImport("user32.dll")] static extern bool   UpdateLayeredWindow(IntPtr hwnd, IntPtr hdcDst,
        ref POINT ptDst, ref SIZE size, IntPtr hdcSrc, ref POINT ptSrc,
        uint crKey, ref BLENDFUNCTION blend, uint dwFlags);

    // GDI
    [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr hdc);
    [DllImport("gdi32.dll")] static extern bool   DeleteDC(IntPtr hdc);
    [DllImport("gdi32.dll")] static extern IntPtr CreateDIBSection(IntPtr hdc, ref BITMAPINFOHEADER bmi,
        uint usage, out IntPtr bits, IntPtr section, uint offset);
    [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr hdc, IntPtr obj);
    [DllImport("gdi32.dll")] static extern bool   DeleteObject(IntPtr obj);
    [DllImport("gdi32.dll")] static extern IntPtr CreateRectRgn(int x1, int y1, int x2, int y2);
    [DllImport("gdi32.dll")] static extern int    CombineRgn(IntPtr dest, IntPtr src1, IntPtr src2, int mode);
    [DllImport("user32.dll")] static extern bool  SetWindowRgn(IntPtr hWnd, IntPtr hRgn, bool bRedraw);

    // Desktop window discovery
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowW(string? cls, string? title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string? cls, string? title);
    [DllImport("user32.dll")] static extern int    MapWindowPoints(IntPtr from, IntPtr to, ref RECT pts, int count);
    [DllImport("user32.dll")] static extern bool   EnumWindows(EnumWindowsProc cb, IntPtr lp);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr hwnd, System.Text.StringBuilder sb, int n);

    // LVM messaging (safe cross-process only with VirtualAllocEx buffer)
    [DllImport("user32.dll")] static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] static extern uint   GetWindowThreadProcessId(IntPtr hwnd, out uint pid);

    // Cross-process memory (VirtualAllocEx pattern for LVM_GETITEMRECT)
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] static extern bool   CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr hProc, IntPtr addr, uint size, uint type, uint protect);
    [DllImport("kernel32.dll")] static extern bool   VirtualFreeEx(IntPtr hProc, IntPtr addr, uint size, uint type);
    [DllImport("kernel32.dll")] static extern bool   WriteProcessMemory(IntPtr hProc, IntPtr addr, IntPtr buf, uint size, out IntPtr written);
    [DllImport("kernel32.dll")] static extern bool   ReadProcessMemory(IntPtr hProc, IntPtr addr, IntPtr buf, uint size, out IntPtr read);

    [DllImport("user32.dll")] static extern bool   ShowWindow(IntPtr hwnd, int nCmdShow);

    // Input
    [DllImport("user32.dll")] static extern bool  GetCursorPos(out POINT pt);
    [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vk);

    // Message loop
    [DllImport("user32.dll")] static extern bool   PeekMessageW(out MSG msg, IntPtr hwnd, uint min, uint max, uint remove);
    [DllImport("user32.dll")] static extern bool   TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")] static extern IntPtr DispatchMessageW(ref MSG msg);

    [DllImport("kernel32.dll")] static extern IntPtr GetModuleHandleW(string? mod);

    // Visual theme — used to suppress Explorer's hot-item border
    [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
    static extern int SetWindowTheme(IntPtr hwnd, string? subApp, string? subId);

    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] static extern bool RedrawWindow(IntPtr hwnd, IntPtr lprcUpdate, IntPtr hrgnUpdate, uint flags);

    // ── Structs ────────────────────────────────────────────────────────────

    [StructLayout(LayoutKind.Sequential)] struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] struct SIZE  { public int cx, cy; }
    [StructLayout(LayoutKind.Sequential)] struct RECT  { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] struct MSG   { public IntPtr hwnd; public uint message; public IntPtr wParam, lParam; public uint time; public POINT pt; }

    [StructLayout(LayoutKind.Sequential)]
    struct BLENDFUNCTION { public byte BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; }

    [StructLayout(LayoutKind.Sequential)]
    struct BITMAPINFOHEADER {
        public int biSize, biWidth, biHeight;
        public short biPlanes, biBitCount;
        public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WNDCLASSEXW {
        public int cbSize, style;
        public WndProcDelegate lpfnWndProc;
        public int cbClsExtra, cbWndExtra;
        public IntPtr hInstance, hIcon, hCursor, hbrBackground;
        public string? lpszMenuName;
        public string lpszClassName;
        public IntPtr hIconSm;
    }

    delegate IntPtr WndProcDelegate(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    delegate bool   EnumWindowsProc(IntPtr hwnd, IntPtr lp);

    // ── Constants ──────────────────────────────────────────────────────────

    const int  WS_POPUP          = unchecked((int)0x80000000);
    const int  WS_EX_LAYERED     = 0x00080000;
    const int  WS_EX_TRANSPARENT = 0x00000020;
    const int  WS_EX_NOACTIVATE  = 0x08000000;
    const int  WS_EX_TOOLWINDOW  = 0x00000080;
    const int  SW_SHOWNOACTIVATE = 4;
    // WS_EX_TOPMOST not used — avoids Z-order conflict with Lively Wallpaper and similar apps
    const uint SWP_NOACTIVATE    = 0x0010;
    const uint SWP_NOZORDER      = 0x0004;
    const uint SWP_NOREDRAW      = 0x0008;
    const uint SWP_NOMOVE        = 0x0002;
    const uint SWP_NOSIZE        = 0x0001;
    const uint LVM_GETITEMCOUNT  = 0x1004;
    const uint LVM_GETITEMRECT   = 0x100E;
    const int  LVIR_ICON         = 1;     // icon image only (excludes label)
    const int  LVIR_LABEL        = 2;     // text label area
    const int  RGN_DIFF          = 4;     // CombineRgn: dest = src1 minus src2
    const uint ULW_ALPHA         = 2;
    const byte AC_SRC_OVER       = 0;
    const byte AC_SRC_ALPHA      = 1;
    const int  WM_NCHITTEST      = 0x0084;
    const int  HTTRANSPARENT     = -1;
    const int  VK_LBUTTON        = 0x01;
    const uint PM_REMOVE         = 1;

    // Process access rights for VirtualAllocEx pattern
    const uint PROCESS_VM_OPERATION = 0x0008;
    const uint PROCESS_VM_READ      = 0x0010;
    const uint PROCESS_VM_WRITE     = 0x0020;
    const uint MEM_COMMIT           = 0x00001000;
    const uint MEM_RESERVE          = 0x00002000;
    const uint MEM_RELEASE          = 0x00008000;
    const uint PAGE_READWRITE       = 0x04;

    // Z-order sentinels
    static readonly IntPtr HWND_TOP = IntPtr.Zero;

    const uint WM_THEMECHANGED  = 0x031A;
    const uint RDW_INVALIDATE   = 0x0001;
    const uint RDW_ERASE        = 0x0004;
    const uint RDW_FRAME        = 0x0400;
    const uint RDW_UPDATENOW    = 0x0100;
    const uint RDW_ALLCHILDREN  = 0x0080;
    static readonly IntPtr HWND_BROADCAST = (IntPtr)0xFFFF;
    const int  DebounceMs      = 50;    // hover debounce
    const int  RefreshInterval = 125;   // iterations between icon re-enumeration (~2 s at 16 ms)

    // ── Per-slot GDI state ─────────────────────────────────────────────────

    private sealed class Slot
    {
        public IntPtr Hwnd;
        public IntPtr Hdc;
        public IntPtr Hbmp;
        public IntPtr Pixels;     // ppvBits from CreateDIBSection
        public int W, H;
        public float Alpha;       // current animated alpha [0,1]
        public float Target;      // target alpha
        public bool  NeedPaint;   // bitmap pixels must be recomputed
        public bool  WasVisible;  // tracks fade-in edge for HWND_TOP raise
    }

    // ── State ──────────────────────────────────────────────────────────────

    private byte  _r = 0x00, _g = 0x78, _b = 0xD4;
    private float _intensity    = 0.8f;
    private int   _glowRadius   = 14;   // px the glow extends beyond the icon image
    private int   _cornerRadius = 10;   // rounded-rect corner radius for the SDF
    private float _fadeInSpeed  = 0.18f;
    private float _fadeOutSpeed = 0.14f;
    private volatile bool _enabled = true;
    private volatile bool _colorDirty;
    private volatile bool _layoutDirty;
    private volatile bool _hideIconLabels = false;

    private Thread? _thread;
    private volatile bool _running;
    private bool _disposed;

    private IntPtr _hListView    = IntPtr.Zero;
    private IntPtr _hExplorerProc = IntPtr.Zero;   // Explorer.exe process handle for cross-process LVM

    private readonly List<RECT> _iconRects = new();
    private readonly List<Slot> _slots     = new();

    private int  _hoveredIdx   = -1;
    private long _debounceStart;
    private bool _prevLeftDown;
    private int  _refreshCount;
    private bool _lastEnabled  = true;

    private static readonly WndProcDelegate s_wndProc = WndProc;
    private static bool s_classRegistered;
    private const string WndClass = "AuraDesktopIconOverlay";

    // ── Public API ─────────────────────────────────────────────────────────

    public bool Enabled
    {
        get => _enabled;
        set => _enabled = value;
    }

    public float Intensity
    {
        get => _intensity;
        set { _intensity = Math.Clamp(value, 0f, 1f); _colorDirty = true; }
    }

    // Glow radius in pixels beyond the icon edge (4–40). Triggers a layout rebuild.
    public int GlowRadius
    {
        get => _glowRadius;
        set { _glowRadius = Math.Clamp(value, 4, 40); _layoutDirty = true; }
    }

    // Multiplier for fade speed: 0.2 = slow, 1.0 = normal, 3.0 = fast.
    public float FadeSpeed
    {
        get => _fadeInSpeed / 0.18f;
        set
        {
            float s = Math.Clamp(value, 0.2f, 3f);
            _fadeInSpeed  = 0.18f * s;
            _fadeOutSpeed = 0.14f * s;
        }
    }

    public bool HideIconLabels
    {
        get => _hideIconLabels;
        set { _hideIconLabels = value; _layoutDirty = true; }
    }

    public void SetColor(byte r, byte g, byte b)
    {
        _r = r; _g = g; _b = b;
        _colorDirty = true;
    }

    public void Start()
    {
        if (_running) return;
        // If a previous run was killed without cleanup (Task Manager), the ListView
        // theme override may still be set. Clear it before the thread re-applies it.
        RestoreListViewTheme();
        _running = true;
        _thread  = new Thread(OverlayThreadProc) { IsBackground = true, Name = "AuraDesktopIconOverlay" };
        _thread.Start();
    }

    // Walks the Explorer window hierarchy and returns the desktop SysListView32.
    // Called from any thread — does not use or write any instance fields.
    private static IntPtr LocateDesktopListView()
    {
        IntPtr hProgman = FindWindowW("Progman", null);
        if (hProgman == IntPtr.Zero) return IntPtr.Zero;

        IntPtr hShell = FindWindowExW(hProgman, IntPtr.Zero, "SHELLDLL_DefView", null);
        if (hShell == IntPtr.Zero)
        {
            IntPtr found = IntPtr.Zero;
            var cb = new EnumWindowsProc((hwnd, _) =>
            {
                var sb = new System.Text.StringBuilder(64);
                GetClassNameW(hwnd, sb, 64);
                if (sb.ToString() == "WorkerW")
                {
                    IntPtr c = FindWindowExW(hwnd, IntPtr.Zero, "SHELLDLL_DefView", null);
                    if (c != IntPtr.Zero) { found = c; return false; }
                }
                return true;
            });
            EnumWindows(cb, IntPtr.Zero);
            GC.KeepAlive(cb);
            hShell = found;
        }

        if (hShell == IntPtr.Zero) return IntPtr.Zero;
        return FindWindowExW(hShell, IntPtr.Zero, "SysListView32", null);
    }

    public void Stop()
    {
        _running = false;
        // Restore Explorer's native hover theme synchronously on the calling thread
        // before joining so this always runs even if the process exits immediately after.
        RestoreListViewTheme();
        _thread?.Join(600);
        _thread = null;
    }

    // Clears our theme override so Explorer's ListView restores its native hover.
    // Always does a fresh window walk — never reads _hListView — so it is safe
    // to call from any thread regardless of whether the background thread has
    // written _hListView yet.
    private static void RestoreListViewTheme()
    {
        IntPtr lv = LocateDesktopListView();
        if (lv == IntPtr.Zero || !IsWindow(lv)) return;
        SetWindowTheme(lv, null, null);
        SetWindowRgn(lv, IntPtr.Zero, true);
        // Notify the ListView directly and broadcast system-wide (same as a real theme change).
        // Sending only to the child ListView is not enough — Explorer responds to the broadcast.
        SendMessageW(lv, WM_THEMECHANGED, IntPtr.Zero, IntPtr.Zero);
        PostMessageW(HWND_BROADCAST, WM_THEMECHANGED, IntPtr.Zero, IntPtr.Zero);
        // Force a complete repaint of the ListView frame so the hover style is visible immediately.
        RedrawWindow(lv, IntPtr.Zero, IntPtr.Zero,
            RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Stop();
    }

    // ── Overlay thread ─────────────────────────────────────────────────────

    private void OverlayThreadProc()
    {
        try
        {
            RegisterWindowClass();

            if (!FindListView())
            {
                Logger.Warn("DesktopIconGlow", "SysListView32 not found — desktop icon glow disabled");
                return;
            }

            RefreshIcons();

            while (_running)
            {
                DrainMessages();

                // Detect enable/disable transitions and sync Explorer's native hover theme.
                // SetWindowTheme(" "," ") suppresses the grey selection box; null,null restores it.
                if (_enabled != _lastEnabled)
                {
                    _lastEnabled = _enabled;
                    if (_enabled)
                    {
                        if (_hListView != IntPtr.Zero && IsWindow(_hListView))
                            SetWindowTheme(_hListView, " ", " ");
                    }
                    else
                    {
                        RestoreListViewTheme();
                    }
                    if (!_enabled)
                    {
                        foreach (var s in _slots) s.Target = 0f;
                        _hoveredIdx = -1;
                    }
                }

                PollHover();
                StepAnimation();

                if (_layoutDirty || ++_refreshCount >= RefreshInterval)
                {
                    _layoutDirty  = false;
                    _refreshCount = 0;
                    if (!IsWindow(_hListView))
                    {
                        CloseExplorerHandle();
                        if (!FindListView()) break;
                    }
                    RefreshIcons();
                }

                if (_colorDirty)
                {
                    _colorDirty = false;
                    foreach (var s in _slots) s.NeedPaint = true;
                }

                Thread.Sleep(16);
            }
        }
        catch (Exception ex)
        {
            Logger.Error("DesktopIconGlow", "Overlay thread crashed", ex);
        }
        finally
        {
            // Restore Explorer's ListView theme and region before exiting
            RestoreListViewTheme();

            foreach (var s in _slots) FreeSlot(s);
            _slots.Clear();
            _iconRects.Clear();
            CloseExplorerHandle();
        }
    }

    private static void DrainMessages()
    {
        while (PeekMessageW(out MSG msg, IntPtr.Zero, 0, 0, PM_REMOVE))
        {
            TranslateMessage(ref msg);
            DispatchMessageW(ref msg);
        }
    }

    private void CloseExplorerHandle()
    {
        if (_hExplorerProc != IntPtr.Zero)
        {
            CloseHandle(_hExplorerProc);
            _hExplorerProc = IntPtr.Zero;
        }
    }

    // ── Window class ───────────────────────────────────────────────────────

    private static void RegisterWindowClass()
    {
        if (s_classRegistered) return;
        var wc = new WNDCLASSEXW
        {
            cbSize        = Marshal.SizeOf<WNDCLASSEXW>(),
            lpfnWndProc   = s_wndProc,
            hInstance     = GetModuleHandleW(null),
            lpszClassName = WndClass,
        };
        RegisterClassExW(ref wc);
        s_classRegistered = true;
    }

    private static IntPtr WndProc(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp)
    {
        if (msg == WM_NCHITTEST) return (IntPtr)HTTRANSPARENT;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // ── Desktop window discovery ───────────────────────────────────────────

    private bool FindListView()
    {
        IntPtr hProgman = FindWindowW("Progman", null);
        if (hProgman == IntPtr.Zero) return false;

        // Classic layout: SHELLDLL_DefView is a direct child of Progman
        IntPtr hShell = FindWindowExW(hProgman, IntPtr.Zero, "SHELLDLL_DefView", null);

        // Windows 11 (and Lively Wallpaper / Wallpaper Engine): SHELLDLL_DefView
        // may have been moved into a WorkerW sibling of Progman.
        if (hShell == IntPtr.Zero)
        {
            IntPtr found = IntPtr.Zero;
            EnumWindows((hwnd, _) =>
            {
                var sb = new System.Text.StringBuilder(64);
                GetClassNameW(hwnd, sb, 64);
                if (sb.ToString() != "WorkerW") return true;
                IntPtr candidate = FindWindowExW(hwnd, IntPtr.Zero, "SHELLDLL_DefView", null);
                if (candidate != IntPtr.Zero) { found = candidate; return false; }
                return true;
            }, IntPtr.Zero);
            hShell = found;
        }

        if (hShell == IntPtr.Zero) return false;
        _hListView = FindWindowExW(hShell, IntPtr.Zero, "SysListView32", null);
        if (_hListView == IntPtr.Zero) return false;

        // Open Explorer's process so we can use VirtualAllocEx for LVM_GETITEMRECT.
        // SysListView32 is in explorer.exe; sending a pointer from our heap
        // cross-process would cause an access violation in Explorer and crash it.
        GetWindowThreadProcessId(_hListView, out uint pid);
        _hExplorerProc = OpenProcess(
            PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, pid);

        if (_hExplorerProc == IntPtr.Zero)
        {
            Logger.Warn("DesktopIconGlow", "OpenProcess(Explorer) failed — cannot enumerate icons");
            _hListView = IntPtr.Zero;
            return false;
        }

        // Suppress Explorer's themed hot-item visual (white/gray border on hover)
        // so only our glow effect is visible. Restored to null,null on thread exit.
        SetWindowTheme(_hListView, " ", " ");

        return true;
    }

    // ── Icon enumeration (cross-process safe) ──────────────────────────────

    private void RefreshIcons()
    {
        if (_hListView == IntPtr.Zero || _hExplorerProc == IntPtr.Zero) return;

        // LVM_GETITEMCOUNT carries no pointer — safe cross-process as-is
        int count = (int)SendMessageW(_hListView, LVM_GETITEMCOUNT, IntPtr.Zero, IntPtr.Zero);
        count = Math.Clamp(count, 0, 512);

        _iconRects.Clear();

        if (count > 0)
        {
            int rcSize = Marshal.SizeOf<RECT>();

            // Allocate ONE RECT inside Explorer's address space; reuse for every icon.
            // This is the VirtualAllocEx pattern required for any LVM message whose
            // lParam is a pointer (LVM_GETITEMRECT, LVM_GETITEM, etc.).
            IntPtr remoteRc = VirtualAllocEx(_hExplorerProc, IntPtr.Zero,
                (uint)rcSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

            if (remoteRc != IntPtr.Zero)
            {
                IntPtr localBuf = Marshal.AllocHGlobal(rcSize);
                try
                {
                    for (int i = 0; i < count; i++)
                    {
                        // Write RECT {Left = LVIR_ICON} into Explorer's memory
                        var init = new RECT { Left = LVIR_ICON };
                        Marshal.StructureToPtr(init, localBuf, false);
                        WriteProcessMemory(_hExplorerProc, remoteRc, localBuf, (uint)rcSize, out _);

                        // Send LVM_GETITEMRECT — Explorer writes the result into remoteRc
                        SendMessageW(_hListView, LVM_GETITEMRECT, (IntPtr)i, remoteRc);

                        // Read the result back into our process
                        ReadProcessMemory(_hExplorerProc, remoteRc, localBuf, (uint)rcSize, out _);
                        RECT rc = Marshal.PtrToStructure<RECT>(localBuf);

                        // Convert from SysListView32 client coords → screen coords
                        MapWindowPoints(_hListView, IntPtr.Zero, ref rc, 2);

                        // Force square: expand to max(width, height), centered on icon midpoint
                        int iconW  = rc.Right  - rc.Left;
                        int iconH  = rc.Bottom - rc.Top;
                        int iconCX = (rc.Left + rc.Right)  / 2;
                        int iconCY = (rc.Top  + rc.Bottom) / 2;
                        int side   = Math.Max(iconW, iconH);
                        rc.Left   = iconCX - side / 2;
                        rc.Right  = iconCX + side / 2;
                        rc.Top    = iconCY - side / 2;
                        rc.Bottom = iconCY + side / 2;

                        // Expand by configurable glow radius
                        int glowR  = _glowRadius;
                        rc.Left   -= glowR;
                        rc.Top    -= glowR;
                        rc.Right  += glowR;
                        rc.Bottom += glowR;
                        _iconRects.Add(rc);
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(localBuf);
                    VirtualFreeEx(_hExplorerProc, remoteRc, 0, MEM_RELEASE);
                }
            }
            else
            {
                Logger.Warn("DesktopIconGlow", "VirtualAllocEx failed — skipping icon refresh");
            }
        }

        // Sync overlay window slots to match icon count -------------------

        // Grow
        while (_slots.Count < _iconRects.Count)
            _slots.Add(new Slot());

        // Shrink — destroy windows for icons that no longer exist
        while (_slots.Count > _iconRects.Count)
        {
            FreeSlot(_slots[^1]);
            _slots.RemoveAt(_slots.Count - 1);
        }

        // Create / reposition / repaint per slot
        for (int i = 0; i < _iconRects.Count; i++)
        {
            RECT rc = _iconRects[i];
            int w = rc.Right - rc.Left;
            int h = rc.Bottom - rc.Top;
            if (w <= 0 || h <= 0) continue;

            Slot s = _slots[i];

            if (s.Hwnd == IntPtr.Zero)
            {
                const int exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                    WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
                s.Hwnd = CreateWindowExW(exStyle, WndClass, null, WS_POPUP,
                    rc.Left, rc.Top, w, h,
                    IntPtr.Zero, IntPtr.Zero, GetModuleHandleW(null), IntPtr.Zero);
                // Layered windows must be explicitly shown; UpdateLayeredWindow
                // on a hidden window is silently ignored.
                ShowWindow(s.Hwnd, SW_SHOWNOACTIVATE);
                s.NeedPaint = true;
            }
            else if (s.W != w || s.H != h)
            {
                SetWindowPos(s.Hwnd, IntPtr.Zero, rc.Left, rc.Top, w, h,
                    SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
                s.NeedPaint = true;
            }
            else
            {
                // Position may have shifted (icon moved) without size change
                SetWindowPos(s.Hwnd, IntPtr.Zero, rc.Left, rc.Top, 0, 0,
                    SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOSIZE);
            }

            if (s.NeedPaint || s.Hdc == IntPtr.Zero)
            {
                AllocateBitmap(s, w, h);
                Composite(i);  // seed UpdateLayeredWindow with alpha=0 so the window starts transparent
            }
        }

        if (_hoveredIdx >= _slots.Count) _hoveredIdx = -1;

        ApplyLabelRegion();
    }

    // ── Icon label region (hides label text by clipping the ListView) ──────

    private void ApplyLabelRegion()
    {
        if (_hListView == IntPtr.Zero || _hExplorerProc == IntPtr.Zero) return;

        if (!_hideIconLabels)
        {
            SetWindowRgn(_hListView, IntPtr.Zero, true);
            return;
        }

        int count = Math.Clamp(
            (int)SendMessageW(_hListView, LVM_GETITEMCOUNT, IntPtr.Zero, IntPtr.Zero),
            0, 512);

        // Full region covering the entire ListView (large enough for any screen)
        IntPtr fullRgn = CreateRectRgn(0, 0, 32767, 32767);

        if (count > 0)
        {
            int rcSize  = Marshal.SizeOf<RECT>();
            IntPtr remoteRc = VirtualAllocEx(_hExplorerProc, IntPtr.Zero,
                (uint)rcSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

            if (remoteRc != IntPtr.Zero)
            {
                IntPtr localBuf = Marshal.AllocHGlobal(rcSize);
                try
                {
                    for (int i = 0; i < count; i++)
                    {
                        var init = new RECT { Left = LVIR_LABEL };
                        Marshal.StructureToPtr(init, localBuf, false);
                        WriteProcessMemory(_hExplorerProc, remoteRc, localBuf, (uint)rcSize, out _);
                        SendMessageW(_hListView, LVM_GETITEMRECT, (IntPtr)i, remoteRc);
                        ReadProcessMemory(_hExplorerProc, remoteRc, localBuf, (uint)rcSize, out _);
                        var rc = Marshal.PtrToStructure<RECT>(localBuf);

                        if (rc.Right <= rc.Left || rc.Bottom <= rc.Top) continue;

                        // LVIR_LABEL rects are already in ListView client coords —
                        // the same space as window regions. No mapping needed.
                        IntPtr lblRgn = CreateRectRgn(rc.Left, rc.Top, rc.Right, rc.Bottom);
                        CombineRgn(fullRgn, fullRgn, lblRgn, RGN_DIFF);
                        DeleteObject(lblRgn);
                    }
                }
                finally
                {
                    Marshal.FreeHGlobal(localBuf);
                    VirtualFreeEx(_hExplorerProc, remoteRc, 0, MEM_RELEASE);
                }
            }
        }

        // SetWindowRgn takes ownership of fullRgn on success — do not DeleteObject it
        SetWindowRgn(_hListView, fullRgn, true);
    }

    // ── GDI bitmap management ──────────────────────────────────────────────

    private unsafe void AllocateBitmap(Slot s, int w, int h)
    {
        if (s.Hdc != IntPtr.Zero)  { SelectObject(s.Hdc, IntPtr.Zero); DeleteDC(s.Hdc);   s.Hdc  = IntPtr.Zero; }
        if (s.Hbmp != IntPtr.Zero) { DeleteObject(s.Hbmp);             s.Hbmp = IntPtr.Zero; s.Pixels = IntPtr.Zero; }

        IntPtr screenDC = GetDC(IntPtr.Zero);
        if (screenDC == IntPtr.Zero) return;
        try
        {
            var bmi = new BITMAPINFOHEADER
            {
                biSize      = Marshal.SizeOf<BITMAPINFOHEADER>(),
                biWidth     = w,
                biHeight    = -h,  // top-down
                biPlanes    = 1,
                biBitCount  = 32,
                biCompression = 0, // BI_RGB
            };
            IntPtr hdc = CreateCompatibleDC(screenDC);
            if (hdc == IntPtr.Zero) return;
            IntPtr hbmp = CreateDIBSection(hdc, ref bmi, 0, out IntPtr bits, IntPtr.Zero, 0);
            if (hbmp == IntPtr.Zero) { DeleteDC(hdc); return; }
            SelectObject(hdc, hbmp);
            s.Hdc = hdc; s.Hbmp = hbmp; s.Pixels = bits; s.W = w; s.H = h;
        }
        finally { ReleaseDC(IntPtr.Zero, screenDC); }

        PaintBitmap(s);
        s.NeedPaint = false;
    }

    private unsafe void PaintBitmap(Slot s)
    {
        if (s.Pixels == IntPtr.Zero) return;
        uint* buf = (uint*)s.Pixels.ToPointer();
        int w = s.W, h = s.H;
        new Span<uint>(buf, w * h).Clear();

        byte r = _r, g = _g, b = _b;
        float intensity = _intensity;
        float margin = _glowRadius;

        // Rounded-rect SDF: the icon occupies the centre of the square overlay.
        // _glowRadius pixels of glow surround it on every side.
        // The inner boundary is a rounded rectangle (corner radius = _cornerRadius)
        // so corners of the glow appear soft rather than chamfered.
        float cx = w / 2f, cy = h / 2f;
        float hw = cx - margin, hh = cy - margin;   // icon half-extents
        float cr = Math.Min(_cornerRadius, Math.Min(hw, hh));

        for (int py = 0; py < h; py++)
        {
            for (int px = 0; px < w; px++)
            {
                // Rounded-rect signed distance: negative = inside icon (transparent)
                float qdx = MathF.Abs(px - cx) - (hw - cr);
                float qdy = MathF.Abs(py - cy) - (hh - cr);
                float dist = MathF.Sqrt(MathF.Max(qdx, 0) * MathF.Max(qdx, 0)
                                      + MathF.Max(qdy, 0) * MathF.Max(qdy, 0)) - cr;

                if (dist < 0) continue;           // inside icon — leave transparent
                if (dist >= margin) continue;     // outside glow ring

                // Quadratic falloff: full brightness at icon edge, zero at outer rim
                float t    = dist / margin;
                float pixA = (1f - t) * (1f - t) * intensity;
                if (pixA < 0.004f) continue;

                // Premultiplied BGRA (uint32 LE: A<<24 | R<<16 | G<<8 | B)
                byte a  = (byte)(pixA * 255f);
                byte pb = (byte)(b * pixA);
                byte pg = (byte)(g * pixA);
                byte pr = (byte)(r * pixA);
                buf[py * w + px] = (uint)((a << 24) | (pr << 16) | (pg << 8) | pb);
            }
        }
    }

    private static void FreeSlot(Slot s)
    {
        if (s.Hwnd != IntPtr.Zero) { DestroyWindow(s.Hwnd);  s.Hwnd = IntPtr.Zero; }
        if (s.Hdc  != IntPtr.Zero) { DeleteDC(s.Hdc);        s.Hdc  = IntPtr.Zero; }
        if (s.Hbmp != IntPtr.Zero) { DeleteObject(s.Hbmp);   s.Hbmp = IntPtr.Zero; s.Pixels = IntPtr.Zero; }
    }

    // ── Hover polling ──────────────────────────────────────────────────────

    private void PollHover()
    {
        if (!_enabled || !GetCursorPos(out POINT cursor)) return;

        int hitIdx = -1;
        for (int i = 0; i < _iconRects.Count; i++)
        {
            RECT rc = _iconRects[i];
            // Hit-test against the icon interior (without glow radius)
            int gr = _glowRadius;
            if (cursor.X >= rc.Left + gr && cursor.X < rc.Right  - gr &&
                cursor.Y >= rc.Top  + gr && cursor.Y < rc.Bottom - gr)
            {
                hitIdx = i;
                break;
            }
        }

        if (hitIdx != _hoveredIdx)
        {
            long now = Environment.TickCount64;
            if (_debounceStart == 0) _debounceStart = now;
            if (now - _debounceStart >= DebounceMs)
            {
                int prev    = _hoveredIdx;
                _hoveredIdx = hitIdx;
                _debounceStart = 0;
                if (prev    >= 0 && prev    < _slots.Count) _slots[prev].Target    = 0f;
                if (hitIdx  >= 0 && hitIdx  < _slots.Count) _slots[hitIdx].Target  = 1f;
            }
        }
        else
        {
            _debounceStart = 0;
        }

        _prevLeftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    }

    // ── Animation ──────────────────────────────────────────────────────────

    private void StepAnimation()
    {
        for (int i = 0; i < _slots.Count; i++)
        {
            Slot  s   = _slots[i];
            float cur = s.Alpha;
            float tgt = s.Target;
            float diff = tgt - cur;

            if (MathF.Abs(diff) < 0.005f)
            {
                if (cur != tgt) { s.Alpha = tgt; Composite(i); }
                continue;
            }

            s.Alpha = cur + diff * (tgt > cur ? _fadeInSpeed : _fadeOutSpeed);
            Composite(i);
        }
    }

    // ── Compositing ────────────────────────────────────────────────────────

    private void Composite(int i)
    {
        if (i >= _slots.Count || i >= _iconRects.Count) return;
        Slot s = _slots[i];
        if (s.Hwnd == IntPtr.Zero || s.Hdc == IntPtr.Zero) return;

        RECT rc  = _iconRects[i];
        byte a8  = (byte)(s.Alpha * 255f);
        bool nowVisible = a8 > 0;

        // Raise to top of the non-topmost Z-order the moment a glow starts
        // fading in so it appears above the desktop icon list view without
        // using WS_EX_TOPMOST (which conflicts with apps like Lively Wallpaper).
        if (nowVisible && !s.WasVisible)
            SetWindowPos(s.Hwnd, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
        s.WasVisible = nowVisible;

        IntPtr screenDC = GetDC(IntPtr.Zero);
        if (screenDC == IntPtr.Zero) return;
        try
        {
            POINT ptDst = new POINT { X = rc.Left, Y = rc.Top };
            SIZE  sz    = new SIZE  { cx = s.W, cy = s.H };
            POINT ptSrc = default;
            BLENDFUNCTION blend = new BLENDFUNCTION
            {
                BlendOp             = AC_SRC_OVER,
                SourceConstantAlpha = a8,
                AlphaFormat         = AC_SRC_ALPHA,
            };
            UpdateLayeredWindow(s.Hwnd, screenDC, ref ptDst, ref sz,
                                s.Hdc, ref ptSrc, 0, ref blend, ULW_ALPHA);
        }
        finally { ReleaseDC(IntPtr.Zero, screenDC); }
    }
}
