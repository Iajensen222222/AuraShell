using System.Collections.Generic;
using System.Runtime.InteropServices;
using Microsoft.UI.Dispatching;

namespace AuraConfig.Services;

/// <summary>
/// Registers global hotkeys on the main window HWND by subclassing its WndProc.
/// Call Initialize() once from MainWindow after the HWND is available.
/// </summary>
public sealed class HotkeyService
{
    public static readonly HotkeyService Instance = new();

    // ── P/Invokes ──────────────────────────────────────────────────────────

    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
    private static extern IntPtr SetWindowLongPtr(IntPtr hWnd, int nIndex, IntPtr newProc);

    [DllImport("user32.dll")]
    private static extern IntPtr CallWindowProc(IntPtr lpPrev, IntPtr hWnd,
                                                uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll")]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    private const int  GWLP_WNDPROC = -4;
    private const uint WM_HOTKEY    = 0x0312;

    // Win32 modifier flag constants (same as MOD_* in winuser.h)
    public const int MOD_ALT     = 0x0001;
    public const int MOD_CONTROL = 0x0002;
    public const int MOD_SHIFT   = 0x0004;
    public const int MOD_WIN     = 0x0008;

    // ── State ──────────────────────────────────────────────────────────────

    private delegate IntPtr WndProcDelegate(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

    private IntPtr           _hwnd;
    private IntPtr           _originalWndProc;
    private WndProcDelegate? _wndProcDelegate;  // must be kept alive — prevents GC collection
    private DispatcherQueue? _uiQueue;
    private readonly Dictionary<int, Action> _handlers = new();
    private int _nextId = 1;

    // ── Lifecycle ──────────────────────────────────────────────────────────

    /// <summary>
    /// Subclasses the main window's WndProc so WM_HOTKEY is intercepted.
    /// Must be called from the UI thread after the window HWND is available.
    /// </summary>
    public void Initialize(IntPtr hwnd, DispatcherQueue uiQueue)
    {
        _hwnd    = hwnd;
        _uiQueue = uiQueue;

        _wndProcDelegate = WndProc;   // store before taking function pointer
        _originalWndProc = SetWindowLongPtr(
            hwnd, GWLP_WNDPROC,
            Marshal.GetFunctionPointerForDelegate(_wndProcDelegate));

        Logger.Info("HotkeyService", $"WndProc subclassed on HWND 0x{hwnd:X}");
    }

    // ── WndProc ────────────────────────────────────────────────────────────

    private IntPtr WndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        if (msg == WM_HOTKEY)
        {
            int id = wParam.ToInt32();
            if (_handlers.TryGetValue(id, out var handler))
                _uiQueue?.TryEnqueue(() => handler());
            return IntPtr.Zero;
        }
        return CallWindowProc(_originalWndProc, hWnd, msg, wParam, lParam);
    }

    // ── Public API ─────────────────────────────────────────────────────────

    /// <summary>
    /// Registers a hotkey. Returns the assigned id (pass to Unregister to remove it).
    /// Returns -1 if the hotkey is already claimed by another app or if Initialize
    /// has not been called yet.
    /// </summary>
    public int Register(int modifiers, int vk, Action handler)
    {
        if (_hwnd == IntPtr.Zero) return -1;
        int id = _nextId++;
        if (RegisterHotKey(_hwnd, id, (uint)modifiers, (uint)vk))
        {
            _handlers[id] = handler;
            Logger.Debug("HotkeyService", $"Registered hotkey id={id} mods={modifiers} vk={vk}");
            return id;
        }
        Logger.Warn("HotkeyService", $"RegisterHotKey failed for mods={modifiers} vk={vk} (already claimed?)");
        return -1;
    }

    /// <summary>Unregisters a previously registered hotkey by id.</summary>
    public void Unregister(int id)
    {
        if (_hwnd == IntPtr.Zero || id < 0) return;
        UnregisterHotKey(_hwnd, id);
        _handlers.Remove(id);
    }

    /// <summary>Unregisters all hotkeys registered through this service.</summary>
    public void UnregisterAll()
    {
        if (_hwnd == IntPtr.Zero) return;
        foreach (int id in _handlers.Keys.ToList())
            UnregisterHotKey(_hwnd, id);
        _handlers.Clear();
        _nextId = 1;
    }

    // ── Formatting ─────────────────────────────────────────────────────────

    /// <summary>Converts Win32 modifier flags + VK code to a human-readable string.</summary>
    public static string FormatHotkey(int mods, int vk)
    {
        var parts = new List<string>();
        if ((mods & MOD_CONTROL) != 0) parts.Add("Ctrl");
        if ((mods & MOD_SHIFT)   != 0) parts.Add("Shift");
        if ((mods & MOD_ALT)     != 0) parts.Add("Alt");
        if ((mods & MOD_WIN)     != 0) parts.Add("Win");

        string key = ((Windows.System.VirtualKey)vk).ToString();
        if (key.StartsWith("Number")) key = key[6..];   // "Number1" → "1"
        parts.Add(key);

        return string.Join("+", parts);
    }
}
