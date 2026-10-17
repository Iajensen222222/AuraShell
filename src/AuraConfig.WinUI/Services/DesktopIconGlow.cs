using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

namespace AuraConfig.Services;

/// <summary>
/// Renders a colored glow overlay around desktop icons on hover, plus a short burst
/// animation at the mouse tip on left mouse down.
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
    [DllImport("user32.dll")] static extern int   GetSystemMetrics(int index);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(POINT pt);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
    [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr hwnd, uint cmd);
    [DllImport("user32.dll")] static extern int    GetWindowLongW(IntPtr hwnd, int index);

    // Message loop
    [DllImport("user32.dll")] static extern bool   PeekMessageW(out MSG msg, IntPtr hwnd, uint min, uint max, uint remove);
    [DllImport("user32.dll")] static extern bool   TranslateMessage(ref MSG msg);
    [DllImport("user32.dll")] static extern IntPtr DispatchMessageW(ref MSG msg);

    [DllImport("kernel32.dll")] static extern IntPtr GetModuleHandleW(string? mod);

    // Visual theme — used to suppress Explorer's hot-item border
    [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
    static extern int SetWindowTheme(IntPtr hwnd, string? subApp, string? subId);

    // Window properties — snapshot and restore Explorer's own theme association
    [DllImport("user32.dll")] static extern IntPtr GetPropW(IntPtr hwnd, IntPtr atom);
    [DllImport("user32.dll")] static extern bool   SetPropW(IntPtr hwnd, IntPtr atom, IntPtr data);
    [DllImport("user32.dll")] static extern IntPtr RemovePropW(IntPtr hwnd, IntPtr atom);

    [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] static extern bool RedrawWindow(IntPtr hwnd, IntPtr lprcUpdate, IntPtr hrgnUpdate, uint flags);

    // Backlight: render the desktop ListView into our bitmap to copy the hovered icon
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr hwnd, out RECT rect);

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
    const int  WS_EX_TOPMOST     = 0x00000008;   // click bursts only: short-lived, must sit above app windows
    const int  SW_HIDE           = 0;
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
    const int  VK_RBUTTON        = 0x02;
    const int  SM_SWAPBUTTON     = 23;
    const uint GA_ROOT           = 2;
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
    static readonly IntPtr HWND_TOP     = IntPtr.Zero;
    const uint GW_HWNDPREV      = 3;     // window above in Z-order
    const int  GWL_EXSTYLE      = -20;
    const int  ZOrderCheckEvery = 10;    // frames between Z-order re-checks while a glow is visible
    static readonly IntPtr HWND_TOPMOST   = (IntPtr)(-1);
    static readonly IntPtr HWND_NOTOPMOST = (IntPtr)(-2);

    const uint WM_THEMECHANGED  = 0x031A;
    const uint RDW_INVALIDATE   = 0x0001;
    const uint RDW_ERASE        = 0x0004;
    const uint RDW_FRAME        = 0x0400;
    const uint RDW_UPDATENOW    = 0x0100;
    const uint RDW_ALLCHILDREN  = 0x0080;
    static readonly IntPtr HWND_BROADCAST = (IntPtr)0xFFFF;

    // uxtheme keeps a window's SetWindowTheme association in these integer-atom window
    // properties (sub-app name, sub-id list, flags). Their values are atoms in the owning
    // process's local atom table, so a name set from our process means nothing to
    // explorer.exe, and SetWindowTheme(null, null) only deletes them: the desktop then
    // falls back to the plain ListView style with no gray hover box. To give Explorer its
    // own highlight back we save the raw values before suppressing and write them back.
    static readonly ushort[] ThemePropAtoms = { 43280, 43281, 43282 };
    const int  DebounceMs      = 50;    // hover debounce
    const int  RefreshInterval = 125;   // iterations between icon re-enumeration (~2 s at 16 ms)

    // Click burst (drawing constants live in DesktopFxPainter)
    const int   MaxBursts          = 6;      // concurrent bursts; the oldest is recycled beyond this
    const float BurstSpinStep      = 0.37f;  // spark angle offset between consecutive bursts (fraction of a turn)

    // Backlight: PrintWindow without PW_RENDERFULLCONTENT renders the icons with real alpha.
    const uint PW_CLIENTONLY_ALPHA = 0;

    // ── Effect options ─────────────────────────────────────────────────────

    // How the glow's color and brightness move. Values are saved by name: only append.
    public enum HoverGlowStyle { Solid, ColorCycle, Pulse, RainbowRing, Orbit, Flicker }
    // Where the glow is drawn around (or behind) the icon.
    public enum GlowShape { SoftEdge, Neon, RoundHalo, Backlight }
    public enum ClickBurstStyle { Ring, Sparks, RingAndSparks, Ripple, Rays, Confetti, Firework }

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
        public uint[]? Icon;      // Backlight: copy of the icon (premultiplied BGRA), drawn over the glow
        public int IconX, IconY, IconW, IconH;   // where Icon sits in this slot's bitmap
    }

    // One click-burst overlay window, reused across clicks.
    private sealed class Burst
    {
        public IntPtr Hwnd;
        public IntPtr Hdc;
        public IntPtr Hbmp;
        public IntPtr Pixels;
        public int    Size;       // square bitmap side in px
        public int    X, Y;       // top-left in screen coords
        public long   StartMs;
        public bool   Active;
        public float  Spin;       // spark angle offset so consecutive bursts differ
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

    private volatile HoverGlowStyle _glowStyle = HoverGlowStyle.Solid;
    private volatile GlowShape _glowShape = GlowShape.SoftEdge;
    private volatile float _cycleSeconds = 6f;

    private volatile bool _clickEnabled     = true;
    private volatile bool _clickDesktopOnly = true;
    private volatile ClickBurstStyle _clickStyle = ClickBurstStyle.RingAndSparks;
    private volatile int  _clickSize        = 48;    // burst radius in px
    private volatile int  _clickDurationMs  = 450;
    private readonly ConcurrentQueue<POINT> _pendingBursts = new();
    private readonly List<Burst> _bursts = new();
    private int _burstCounter;

    private Thread? _thread;
    private volatile bool _running;
    private bool _disposed;

    private IntPtr _hListView    = IntPtr.Zero;
    private IntPtr _hExplorerProc = IntPtr.Zero;   // Explorer.exe process handle for cross-process LVM

    private readonly List<RECT> _iconRects = new();        // overlay window rects (icon box + glow), screen
    private readonly List<RECT> _iconClientRects = new();  // LVIR_ICON rects, ListView client coords

    // Backlight: one ListView-sized DIB that PrintWindow renders into.
    private IntPtr _capDc, _capBmp, _capBits;
    private int    _capW, _capH;
    private readonly List<Slot> _slots     = new();

    private int  _hoveredIdx   = -1;
    private long _debounceStart;
    private bool _prevLeftDown;
    private int  _refreshCount;
    private int  _zOrderCount;
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

    // Solid: fixed color. ColorCycle: hue rotates from the chosen color. Pulse: brightness breathes.
    public HoverGlowStyle GlowStyle
    {
        get => _glowStyle;
        set { _glowStyle = value; _colorDirty = true; }
    }

    // SoftEdge, Neon, RoundHalo or Backlight (glow behind the icon).
    public GlowShape Shape
    {
        get => _glowShape;
        set { _glowShape = value; _colorDirty = true; }
    }

    // Seconds for one full color cycle or one pulse (1–20).
    public float CycleSeconds
    {
        get => _cycleSeconds;
        set => _cycleSeconds = Math.Clamp(value, 1f, 20f);
    }

    public bool ClickBurstEnabled
    {
        get => _clickEnabled;
        set => _clickEnabled = value;
    }

    // When true, bursts only fire for clicks on the desktop itself, not inside app windows.
    public bool ClickBurstDesktopOnly
    {
        get => _clickDesktopOnly;
        set => _clickDesktopOnly = value;
    }

    public ClickBurstStyle BurstStyle
    {
        get => _clickStyle;
        set => _clickStyle = value;
    }

    // Burst radius in pixels (20–120).
    public int ClickBurstSize
    {
        get => _clickSize;
        set => _clickSize = Math.Clamp(value, 20, 120);
    }

    // Burst length in milliseconds (200–1200).
    public int ClickBurstDurationMs
    {
        get => _clickDurationMs;
        set => _clickDurationMs = Math.Clamp(value, 200, 1200);
    }

    // Plays one burst at the current mouse position (the Desktop FX page's preview button),
    // regardless of the enabled and desktop-only settings.
    public void PreviewBurstAtCursor()
    {
        if (GetCursorPos(out POINT pt)) _pendingBursts.Enqueue(pt);
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

    // ── Explorer's native hover box ────────────────────────────────────────

    /// <summary>Explorer's own theme properties on the desktop ListView, saved before we change them.</summary>
    private sealed class ThemeSnapshot
    {
        public long   Hwnd   { get; set; }
        public uint   Pid    { get; set; }
        public long[] Values { get; set; } = Array.Empty<long>();   // 0 = property absent
    }

    // Shared by the UI thread (Stop) and the overlay thread; guarded by s_themeLock.
    private static readonly object s_themeLock = new();
    private static ThemeSnapshot? s_themeSnapshot;

    // Persisted so a restart after a crash or Task Manager kill can still restore
    // the original values, as long as it is the same Explorer window.
    private static readonly string ThemeSnapshotPath = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "AuraShell", "desktop_theme_snapshot.json");

    private static bool SnapshotMatches(ThemeSnapshot? snap, IntPtr lv, uint pid)
        => snap is not null && snap.Hwnd == lv.ToInt64() && snap.Pid == pid
           && snap.Values.Length == ThemePropAtoms.Length;

    // Caller holds s_themeLock.
    private static ThemeSnapshot? LoadSnapshotLocked(IntPtr lv, uint pid)
    {
        if (SnapshotMatches(s_themeSnapshot, lv, pid)) return s_themeSnapshot;
        try
        {
            if (System.IO.File.Exists(ThemeSnapshotPath))
            {
                var snap = System.Text.Json.JsonSerializer.Deserialize<ThemeSnapshot>(
                    System.IO.File.ReadAllText(ThemeSnapshotPath));
                if (SnapshotMatches(snap, lv, pid))
                {
                    s_themeSnapshot = snap;
                    return snap;
                }
            }
        }
        catch (Exception ex)
        {
            Logger.Warn("DesktopIconGlow", $"Could not read theme snapshot: {ex.Message}");
        }
        return null;
    }

    /// <summary>
    /// Hides Explorer's gray hover/selection box so only our glow shows. Saves Explorer's
    /// own theme properties first (once per Explorer window) so they can be put back.
    /// </summary>
    private static void SuppressNativeHover(IntPtr lv)
    {
        if (lv == IntPtr.Zero || !IsWindow(lv)) return;
        lock (s_themeLock)
        {
            GetWindowThreadProcessId(lv, out uint pid);
            if (LoadSnapshotLocked(lv, pid) is null)
            {
                var values = new long[ThemePropAtoms.Length];
                for (int i = 0; i < ThemePropAtoms.Length; i++)
                    values[i] = GetPropW(lv, (IntPtr)ThemePropAtoms[i]).ToInt64();
                s_themeSnapshot = new ThemeSnapshot { Hwnd = lv.ToInt64(), Pid = pid, Values = values };
                try
                {
                    System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(ThemeSnapshotPath)!);
                    System.IO.File.WriteAllText(ThemeSnapshotPath,
                        System.Text.Json.JsonSerializer.Serialize(s_themeSnapshot));
                }
                catch (Exception ex)
                {
                    Logger.Warn("DesktopIconGlow", $"Could not save theme snapshot: {ex.Message}");
                }
                Logger.Info("DesktopIconGlow",
                    $"Saved Explorer theme properties: {string.Join(", ", Array.ConvertAll(values, v => $"0x{v:X}"))}");
            }
            SetWindowTheme(lv, " ", " ");
        }
    }

    // Puts back Explorer's own theme properties and lets the ListView reload its theme,
    // which brings back the native gray hover box. Always does a fresh window walk —
    // never reads _hListView — so it is safe to call from any thread regardless of
    // whether the background thread has written _hListView yet.
    private static void RestoreListViewTheme()
    {
        IntPtr lv = LocateDesktopListView();
        if (lv == IntPtr.Zero || !IsWindow(lv)) return;
        SetWindowRgn(lv, IntPtr.Zero, true);

        lock (s_themeLock)
        {
            GetWindowThreadProcessId(lv, out uint pid);
            if (LoadSnapshotLocked(lv, pid) is { } snap)
            {
                for (int i = 0; i < ThemePropAtoms.Length; i++)
                {
                    var atom = (IntPtr)ThemePropAtoms[i];
                    if (snap.Values[i] != 0) SetPropW(lv, atom, (IntPtr)snap.Values[i]);
                    else                     RemovePropW(lv, atom);
                }
                SendMessageW(lv, WM_THEMECHANGED, IntPtr.Zero, IntPtr.Zero);
                RedrawWindow(lv, IntPtr.Zero, IntPtr.Zero,
                    RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW | RDW_ALLCHILDREN);
                return;
            }
        }

        // No snapshot for this Explorer window. Explorer never sets a sub-app name on the
        // desktop ListView itself, so if one is present it is a leftover override (from a
        // build without snapshots): clear it. Otherwise leave Explorer's properties alone;
        // clearing them here would delete the very values a later snapshot must capture.
        if (GetPropW(lv, (IntPtr)ThemePropAtoms[0]) == IntPtr.Zero) return;
        SetWindowTheme(lv, null, null);
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

                // Detect enable/disable transitions and swap between our glow and
                // Explorer's native gray hover box.
                if (_enabled != _lastEnabled)
                {
                    _lastEnabled = _enabled;
                    if (_enabled)
                    {
                        SuppressNativeHover(_hListView);
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
                PollClick();
                StepAnimation();
                StepBursts();
                if (++_zOrderCount >= ZOrderCheckEvery)
                {
                    _zOrderCount = 0;
                    KeepGlowsAboveDesktop();
                }

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
                    RepaintAll();
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
            foreach (var b in _bursts) FreeBurst(b);
            _bursts.Clear();
            FreeCapture();
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

        // Suppress Explorer's themed hot-item visual (gray box on hover) so only our glow
        // shows, but only while the glow is on. Restored on disable and on thread exit.
        _lastEnabled = _enabled;
        if (_lastEnabled) SuppressNativeHover(_hListView);

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
        _iconClientRects.Clear();

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
                        _iconClientRects.Add(rc);

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
        var look = CurrentLook();
        DesktopFxPainter.PaintGlow(buf, s.W, s.H, look, Environment.TickCount64);

        // Backlight: the glow covers the icon, so draw the icon back on top of it.
        if (look.Shape == GlowShape.Backlight && s.Icon is { } icon)
            DesktopFxPainter.DrawOver(buf, s.W, s.H, icon, s.IconW, s.IconH, s.IconX, s.IconY);
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

        // An app window can cover an icon; the glow sits above app windows, so only
        // light up when the desktop itself is under the pointer.
        if (hitIdx >= 0 && !IsDesktopAt(cursor)) hitIdx = -1;

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
                if (hitIdx  >= 0 && hitIdx  < _slots.Count)
                {
                    if (_glowShape == GlowShape.Backlight) CaptureIcon(hitIdx);
                    _slots[hitIdx].Target = 1f;
                }
            }
        }
        else
        {
            _debounceStart = 0;
        }
    }

    // ── Animation ──────────────────────────────────────────────────────────

    private void StepAnimation()
    {
        HoverGlowStyle style = _glowStyle;
        for (int i = 0; i < _slots.Count; i++)
        {
            Slot  s   = _slots[i];
            float cur = s.Alpha;
            float tgt = s.Target;
            float diff = tgt - cur;

            if (MathF.Abs(diff) < 0.005f)
            {
                if (cur != tgt) s.Alpha = tgt;
                else if (cur <= 0f || style == HoverGlowStyle.Solid) continue;
            }
            else
            {
                s.Alpha = cur + diff * (tgt > cur ? _fadeInSpeed : _fadeOutSpeed);
            }

            // Animated styles redraw every frame while the glow is visible; idle slots cost nothing.
            if (DesktopFxPainter.IsAnimatedPaint(style) && s.Alpha > 0f) PaintBitmap(s);
            Composite(i);
        }
    }

    // Repaints every slot's bitmap after a color or style change so it shows immediately,
    // not at the next icon refresh.
    private void RepaintAll()
    {
        for (int i = 0; i < _slots.Count; i++)
        {
            Slot s = _slots[i];
            if (s.Pixels == IntPtr.Zero) continue;
            PaintBitmap(s);
            if (s.Alpha > 0f) Composite(i);
        }
    }

    // ── Glow look ──────────────────────────────────────────────────────────

    private DesktopFxPainter.GlowLook CurrentLook() => new(
        _r, _g, _b, _intensity, _glowRadius, _cornerRadius, _glowStyle, _glowShape, _cycleSeconds);

    private (byte r, byte g, byte b) CurrentGlowColor()
        => DesktopFxPainter.GlowColor(CurrentLook(), Environment.TickCount64);

    // ── Click burst ────────────────────────────────────────────────────────

    private void PollClick()
    {
        while (_pendingBursts.TryDequeue(out POINT queued))
            SpawnBurst(queued.X, queued.Y);

        // GetAsyncKeyState reads physical buttons; follow the user's swap-buttons setting.
        int vk = GetSystemMetrics(SM_SWAPBUTTON) != 0 ? VK_RBUTTON : VK_LBUTTON;
        bool down    = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool pressed = down && !_prevLeftDown;
        _prevLeftDown = down;

        if (!pressed || !_clickEnabled || !GetCursorPos(out POINT cursor)) return;
        if (_clickDesktopOnly && !IsDesktopAt(cursor)) return;
        SpawnBurst(cursor.X, cursor.Y);
    }

    // True when the point is over the desktop (wallpaper, icons, or our own glow overlays).
    private static bool IsDesktopAt(POINT pt)
    {
        IntPtr hwnd = WindowFromPoint(pt);
        if (hwnd == IntPtr.Zero) return false;
        IntPtr root = GetAncestor(hwnd, GA_ROOT);
        var sb = new System.Text.StringBuilder(64);
        GetClassNameW(root != IntPtr.Zero ? root : hwnd, sb, 64);
        string cls = sb.ToString();
        return cls == "Progman" || cls == "WorkerW" || cls == WndClass;
    }

    private void SpawnBurst(int cx, int cy)
    {
        int size = DesktopFxPainter.BurstBitmapSize(_clickSize);

        Burst? burst = null;
        foreach (var b in _bursts)
            if (!b.Active) { burst = b; break; }
        if (burst is null && _bursts.Count < MaxBursts)
        {
            burst = new Burst();
            _bursts.Add(burst);
        }
        if (burst is null)
        {
            // All busy: recycle the one that started first.
            burst = _bursts[0];
            foreach (var b in _bursts)
                if (b.StartMs < burst.StartMs) burst = b;
        }

        if (burst.Hwnd == IntPtr.Zero)
        {
            const int exStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
                                WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
            burst.Hwnd = CreateWindowExW(exStyle, WndClass, null, WS_POPUP,
                cx - size / 2, cy - size / 2, size, size,
                IntPtr.Zero, IntPtr.Zero, GetModuleHandleW(null), IntPtr.Zero);
            if (burst.Hwnd == IntPtr.Zero) return;
        }
        if (burst.Size != size || burst.Hdc == IntPtr.Zero)
            AllocateBurstBitmap(burst, size);
        if (burst.Pixels == IntPtr.Zero) return;

        burst.X       = cx - size / 2;
        burst.Y       = cy - size / 2;
        burst.StartMs = Environment.TickCount64;
        burst.Spin    = (++_burstCounter * BurstSpinStep) % 1f;
        burst.Active  = true;

        SetWindowPos(burst.Hwnd, HWND_TOPMOST, burst.X, burst.Y, size, size,
            SWP_NOACTIVATE | SWP_NOREDRAW);
        PaintBurst(burst, 0f);
        CompositeBurst(burst);
        ShowWindow(burst.Hwnd, SW_SHOWNOACTIVATE);
    }

    private void StepBursts()
    {
        if (_bursts.Count == 0) return;
        long  now      = Environment.TickCount64;
        float duration = _clickDurationMs;
        foreach (var b in _bursts)
        {
            if (!b.Active) continue;
            float p = (now - b.StartMs) / duration;
            if (p >= 1f)
            {
                b.Active = false;
                ShowWindow(b.Hwnd, SW_HIDE);
                continue;
            }
            PaintBurst(b, p);
            CompositeBurst(b);
        }
    }

    private static void AllocateBurstBitmap(Burst b, int size)
    {
        if (b.Hdc  != IntPtr.Zero) { DeleteDC(b.Hdc);      b.Hdc  = IntPtr.Zero; }
        if (b.Hbmp != IntPtr.Zero) { DeleteObject(b.Hbmp); b.Hbmp = IntPtr.Zero; b.Pixels = IntPtr.Zero; }

        IntPtr screenDC = GetDC(IntPtr.Zero);
        if (screenDC == IntPtr.Zero) return;
        try
        {
            var bmi = new BITMAPINFOHEADER
            {
                biSize      = Marshal.SizeOf<BITMAPINFOHEADER>(),
                biWidth     = size,
                biHeight    = -size,  // top-down
                biPlanes    = 1,
                biBitCount  = 32,
                biCompression = 0,    // BI_RGB
            };
            IntPtr hdc = CreateCompatibleDC(screenDC);
            if (hdc == IntPtr.Zero) return;
            IntPtr hbmp = CreateDIBSection(hdc, ref bmi, 0, out IntPtr bits, IntPtr.Zero, 0);
            if (hbmp == IntPtr.Zero) { DeleteDC(hdc); return; }
            SelectObject(hdc, hbmp);
            b.Hdc = hdc; b.Hbmp = hbmp; b.Pixels = bits; b.Size = size;
        }
        finally { ReleaseDC(IntPtr.Zero, screenDC); }
    }

    private unsafe void PaintBurst(Burst b, float p)
    {
        var (r, g, bl) = CurrentGlowColor();
        DesktopFxPainter.PaintBurst((uint*)b.Pixels.ToPointer(), b.Size, _clickStyle,
            b.Size / 2 - DesktopFxPainter.BurstPadding, r, g, bl, p, b.Spin);
    }

    private static void CompositeBurst(Burst b)
    {
        IntPtr screenDC = GetDC(IntPtr.Zero);
        if (screenDC == IntPtr.Zero) return;
        try
        {
            POINT ptDst = new POINT { X = b.X, Y = b.Y };
            SIZE  sz    = new SIZE  { cx = b.Size, cy = b.Size };
            POINT ptSrc = default;
            BLENDFUNCTION blend = new BLENDFUNCTION
            {
                BlendOp             = AC_SRC_OVER,
                SourceConstantAlpha = 255,
                AlphaFormat         = AC_SRC_ALPHA,
            };
            UpdateLayeredWindow(b.Hwnd, screenDC, ref ptDst, ref sz,
                                b.Hdc, ref ptSrc, 0, ref blend, ULW_ALPHA);
        }
        finally { ReleaseDC(IntPtr.Zero, screenDC); }
    }

    private static void FreeBurst(Burst b)
    {
        if (b.Hwnd != IntPtr.Zero) { DestroyWindow(b.Hwnd);  b.Hwnd = IntPtr.Zero; }
        if (b.Hdc  != IntPtr.Zero) { DeleteDC(b.Hdc);        b.Hdc  = IntPtr.Zero; }
        if (b.Hbmp != IntPtr.Zero) { DeleteObject(b.Hbmp);   b.Hbmp = IntPtr.Zero; b.Pixels = IntPtr.Zero; }
    }

    // ── Backlight icon capture ─────────────────────────────────────────────
    //
    // The glow windows sit above the desktop, so a glow "behind" the icon is drawn over it
    // and the icon is drawn back on top. PrintWindow without PW_RENDERFULLCONTENT makes the
    // ListView paint itself (icons with real alpha, shortcut arrows included) into our
    // bitmap; we keep the hovered icon's pixels. It costs ~30 ms, so it runs once per hover.

    private unsafe void CaptureIcon(int i)
    {
        if (i >= _slots.Count || i >= _iconClientRects.Count || i >= _iconRects.Count) return;
        if (!GetClientRect(_hListView, out RECT client)) return;
        int w = client.Right, h = client.Bottom;
        if (w <= 0 || h <= 0) return;

        if (_capBits == IntPtr.Zero || _capW != w || _capH != h)
        {
            FreeCapture();
            IntPtr screenDC = GetDC(IntPtr.Zero);
            if (screenDC == IntPtr.Zero) return;
            try
            {
                var bmi = new BITMAPINFOHEADER
                {
                    biSize = Marshal.SizeOf<BITMAPINFOHEADER>(), biWidth = w, biHeight = -h,
                    biPlanes = 1, biBitCount = 32,
                };
                _capDc  = CreateCompatibleDC(screenDC);
                _capBmp = _capDc != IntPtr.Zero
                    ? CreateDIBSection(_capDc, ref bmi, 0, out _capBits, IntPtr.Zero, 0) : IntPtr.Zero;
                if (_capBmp == IntPtr.Zero) { FreeCapture(); return; }
                SelectObject(_capDc, _capBmp);
                _capW = w; _capH = h;
            }
            finally { ReleaseDC(IntPtr.Zero, screenDC); }
        }

        uint* cap = (uint*)_capBits.ToPointer();
        new Span<uint>(cap, w * h).Clear();
        if (!PrintWindow(_hListView, _capDc, PW_CLIENTONLY_ALPHA))
        {
            Logger.Warn("DesktopIconGlow", "PrintWindow(desktop) failed: backlight shows without the icon on top");
            return;
        }

        RECT ic = _iconClientRects[i];
        int x0 = Math.Clamp(ic.Left, 0, w), y0 = Math.Clamp(ic.Top, 0, h);
        int iw = Math.Clamp(ic.Right, 0, w) - x0;
        int ih = Math.Clamp(ic.Bottom, 0, h) - y0;
        if (iw <= 0 || ih <= 0) return;

        var icon = new uint[iw * ih];
        for (int y = 0; y < ih; y++)
            new ReadOnlySpan<uint>(cap + (y0 + y) * w + x0, iw).CopyTo(icon.AsSpan(y * iw, iw));

        // Place it in the slot bitmap: both rects are in screen coords once mapped.
        RECT onScreen = new() { Left = x0, Top = y0, Right = x0 + iw, Bottom = y0 + ih };
        MapWindowPoints(_hListView, IntPtr.Zero, ref onScreen, 2);
        Slot s = _slots[i];
        s.Icon  = icon;
        s.IconW = iw; s.IconH = ih;
        s.IconX = onScreen.Left - _iconRects[i].Left;
        s.IconY = onScreen.Top  - _iconRects[i].Top;
        PaintBitmap(s);
    }

    private void FreeCapture()
    {
        if (_capDc  != IntPtr.Zero) { DeleteDC(_capDc);       _capDc  = IntPtr.Zero; }
        if (_capBmp != IntPtr.Zero) { DeleteObject(_capBmp);  _capBmp = IntPtr.Zero; }
        _capBits = IntPtr.Zero; _capW = _capH = 0;
    }

    // ── Z-order ────────────────────────────────────────────────────────────

    // True when hwnd is in the run of our overlay windows sitting directly above `root`.
    private static bool IsJustAbove(IntPtr hwnd, IntPtr root)
    {
        var sb = new System.Text.StringBuilder(64);
        for (IntPtr w = GetWindow(root, GW_HWNDPREV); w != IntPtr.Zero; w = GetWindow(w, GW_HWNDPREV))
        {
            if (w == hwnd) return true;
            sb.Clear();
            GetClassNameW(w, sb, 64);
            if (sb.ToString() != WndClass) return false;
        }
        return false;
    }

    /// <summary>
    /// Puts a glow window immediately above the desktop's top-level window (Progman or
    /// WorkerW): over the icons, under every app. Never WS_EX_TOPMOST, which would cover
    /// apps and conflicts with wallpaper tools like Lively.
    /// </summary>
    private void PlaceAboveDesktop(IntPtr hwnd)
    {
        const uint flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW;
        IntPtr root = _hListView != IntPtr.Zero ? GetAncestor(_hListView, GA_ROOT) : IntPtr.Zero;
        if (root == IntPtr.Zero)
        {
            SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, flags);
            return;
        }
        if (IsJustAbove(hwnd, root)) return;

        IntPtr above = GetWindow(root, GW_HWNDPREV);
        if (above == IntPtr.Zero || (GetWindowLongW(above, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0)
        {
            // The desktop is the highest normal window (after Show desktop / Win+D).
            // A plain HWND_TOP from this background process can leave us under it; a
            // TOPMOST then NOTOPMOST pair reliably lands at the top of the normal band.
            SetWindowPos(hwnd, HWND_TOPMOST,   0, 0, 0, 0, flags);
            SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
        }
        else
        {
            // Insert below the window that is above the desktop, i.e. right on top of it.
            SetWindowPos(hwnd, above, 0, 0, 0, 0, flags);
        }
    }

    // While a glow is visible the desktop can move in the Z-order (Win+D, clicking the
    // desktop), so re-check every few frames and slip back on top of it if needed.
    private void KeepGlowsAboveDesktop()
    {
        foreach (var s in _slots)
            if (s.WasVisible && s.Hwnd != IntPtr.Zero) PlaceAboveDesktop(s.Hwnd);
    }

    // ── Compositing ────────────────────────────────────────────────────────

    private void Composite(int i)
    {
        if (i >= _slots.Count || i >= _iconRects.Count) return;
        Slot s = _slots[i];
        if (s.Hwnd == IntPtr.Zero || s.Hdc == IntPtr.Zero) return;

        RECT rc  = _iconRects[i];
        byte a8  = (byte)(s.Alpha * DesktopFxPainter.GlowLevel(_glowStyle, _cycleSeconds, Environment.TickCount64) * 255f);
        bool nowVisible = a8 > 0;

        // The moment a glow starts fading in, slot it directly above the desktop so it
        // shows over the icons but stays under every app window.
        if (nowVisible && !s.WasVisible) PlaceAboveDesktop(s.Hwnd);
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
