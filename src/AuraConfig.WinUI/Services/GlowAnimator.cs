using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace AuraConfig.Services;

/// <summary>
/// Controls the 1-px DWM border color on all visible top-level windows to
/// match the active theme accent. Supports audio-reactive brightness pulses,
/// idle breathing animation, per-app color overrides, and notification badge colors.
/// </summary>
public sealed class GlowAnimator
{
    // ── DWM / Win32 P/Invoke ───────────────────────────────────────────────

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
    private const int DWMWA_BORDER_COLOR  = 34;
    private const int DWMWA_COLOR_DEFAULT = unchecked((int)0xFFFFFFFF);  // restore to system default

    private delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern int  GetWindowLong(IntPtr hwnd, int nIndex);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("kernel32.dll")] private static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern bool QueryFullProcessImageNameW(IntPtr hProc, int flags,
                                                          System.Text.StringBuilder buf, ref int size);

    private const int  GWL_EXSTYLE                  = -20;
    private const int  WS_EX_TOOLWINDOW             = 0x0080;
    private const uint PROCESS_QUERY_LIMITED_INFO   = 0x1000;

    // ── State ──────────────────────────────────────────────────────────────

    private IntPtr _appHwnd;

    // Current theme accent color (base color for all windows)
    private (byte r, byte g, byte b) _themeColor = (0x00, 0x78, 0xD4);

    // Per-exe override: key = lower-case exe basename (e.g. "spotify.exe")
    private readonly Dictionary<string, (byte r, byte g, byte b)> _appColors  = new();
    // Notification badge colors — cleared independently of _appColors
    private readonly Dictionary<string, (byte r, byte g, byte b)> _badgeColors = new();

    // Breathing animation
    private System.Threading.Timer? _breatheTimer;
    private double  _breathePhase;
    private double  _breatheStep;
    private float   _breatheMaxBoost;
    private bool    _breatheWaveMode;
    private int     _breatheWindowCount;  // snapshotted for wave offset calculation

    // Repaint timer — re-applies the current theme color to all visible windows
    // every RepaintIntervalMs. Two reasons:
    //   1. New windows that opened since the last paint get tinted.
    //   2. DWM resets DWMWA_BORDER_COLOR on activation changes per MS docs; we
    //      have to re-apply ourselves to keep the tint sticky.
    private System.Threading.Timer? _repaintTimer;
    private const int RepaintIntervalMs = 1500;

    // ── Lifecycle ──────────────────────────────────────────────────────────

    /// <summary>Call once with the WinUI app's own HWND after the window is created.</summary>
    public void SetWindowHandle(IntPtr hwnd)
    {
        _appHwnd = hwnd;
        // Apply the current theme color to the app window immediately
        if (_appHwnd != IntPtr.Zero)
        {
            var (r, g, b) = _themeColor;
            int colorRef = PackColorRef(r, g, b);
            DwmSetWindowAttribute(_appHwnd, DWMWA_BORDER_COLOR, ref colorRef, sizeof(int));
        }
    }

    // ── Theme color ────────────────────────────────────────────────────────

    /// <summary>
    /// Sets all visible app window borders to the given theme accent color.
    /// Call whenever the active theme changes. Also starts the repaint timer
    /// (idempotent — subsequent calls just update the color, the timer keeps
    /// ticking).
    /// </summary>
    public void SetColor(byte r, byte g, byte b)
    {
        _themeColor = (r, g, b);
        SetAllWindowBorders(r, g, b);
        EnsureRepaintTimerStarted();
    }

    /// <summary>
    /// Starts (or leaves running) the periodic repaint that re-tints all visible
    /// windows. Required because DWM clears DWMWA_BORDER_COLOR on activation
    /// changes and because new windows opened later need to be picked up.
    /// </summary>
    private void EnsureRepaintTimerStarted()
    {
        if (_repaintTimer is not null) return;
        _repaintTimer = new System.Threading.Timer(
            _ => RepaintTick(),
            null,
            TimeSpan.FromMilliseconds(RepaintIntervalMs),
            TimeSpan.FromMilliseconds(RepaintIntervalMs));
    }

    private void RepaintTick()
    {
        try
        {
            // Skip the repaint while breathing is active — that timer is already
            // touching every window each frame and a second concurrent enum
            // would just churn DWM. (Breathing's tick reads _themeColor so a
            // theme change still takes effect on the next breathe tick.)
            if (_breatheTimer is not null) return;

            var (r, g, b) = _themeColor;
            SetAllWindowBorders(r, g, b);
        }
        catch { /* never let the timer thread crash the app */ }
    }

    /// <summary>
    /// Brightens or dims all borders proportional to <paramref name="magnitude"/>.
    /// Intended for audio-reactive effects (~10fps). magnitude is 0.0–1.0.
    /// </summary>
    public void Pulse(float magnitude)
    {
        var (r, g, b) = _themeColor;
        float boost = 1.0f + magnitude * 0.4f;
        SetAllWindowBorders(
            (byte)Math.Min(255, r * boost),
            (byte)Math.Min(255, g * boost),
            (byte)Math.Min(255, b * boost));
    }

    /// <summary>Resets all window borders back to the Windows system default.</summary>
    public void ClearAllWindowBorders()
    {
        int colorRef = DWMWA_COLOR_DEFAULT;
        EnumWindows((hwnd, _) =>
        {
            if (IsEligibleWindow(hwnd))
                DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, ref colorRef, sizeof(int));
            return true;
        }, IntPtr.Zero);
    }

    // ── Per-app color overrides ────────────────────────────────────────────

    /// <summary>Assigns a fixed border color to all windows belonging to <paramref name="exeName"/>.</summary>
    public void SetAppColor(string exeName, byte r, byte g, byte b)
    {
        _appColors[exeName.ToLowerInvariant()] = (r, g, b);
        SetAllWindowBorders(_themeColor.r, _themeColor.g, _themeColor.b);
    }

    /// <summary>Removes the per-app override for <paramref name="exeName"/>.</summary>
    public void ClearAppColor(string exeName)
    {
        _appColors.Remove(exeName.ToLowerInvariant());
        SetAllWindowBorders(_themeColor.r, _themeColor.g, _themeColor.b);
    }

    // ── Notification badge colors ──────────────────────────────────────────

    /// <summary>Marks an app as having a pending notification — its windows glow with <paramref name="badgeColor"/>.</summary>
    public void SetBadgeColor(string appName, Windows.UI.Color badgeColor)
    {
        _badgeColors[appName.ToLowerInvariant()] = (badgeColor.R, badgeColor.G, badgeColor.B);
        SetAllWindowBorders(_themeColor.r, _themeColor.g, _themeColor.b);
    }

    /// <summary>Clears the notification badge for an app — its windows revert to theme or per-app color.</summary>
    public void ClearBadgeColor(string appName)
    {
        _badgeColors.Remove(appName.ToLowerInvariant());
        SetAllWindowBorders(_themeColor.r, _themeColor.g, _themeColor.b);
    }

    // ── Idle breathing animation ───────────────────────────────────────────

    /// <summary>
    /// Starts a gentle breathing animation on all window borders.
    /// <paramref name="cycleSeconds"/>: full sine cycle length (0.5–5.0 s).
    /// <paramref name="maxBoost"/>: peak brightness above base color (0.0–1.0).
    /// <paramref name="waveMode"/>: if true, each window gets a phase offset for a rolling wave effect.
    /// </summary>
    public void StartBreathing(double cycleSeconds, float maxBoost, bool waveMode)
    {
        StopBreathing();
        _breathePhase    = 0;
        _breatheMaxBoost = Math.Clamp(maxBoost, 0f, 1f);
        _breatheWaveMode = waveMode;
        // 20fps step
        double fps = 20.0;
        _breatheStep = 2 * Math.PI / (cycleSeconds * fps);

        _breatheTimer = new System.Threading.Timer(_ => BreatheTick(),
                                                   null,
                                                   TimeSpan.Zero,
                                                   TimeSpan.FromMilliseconds(1000.0 / fps));
    }

    public void StopBreathing()
    {
        _breatheTimer?.Dispose();
        _breatheTimer = null;
        // Restore static theme color
        SetAllWindowBorders(_themeColor.r, _themeColor.g, _themeColor.b);
    }

    private void BreatheTick()
    {
        _breathePhase = (_breathePhase + _breatheStep) % (2 * Math.PI);
        if (!_breatheWaveMode)
        {
            // All windows pulse in unison
            float t     = (float)((Math.Sin(_breathePhase) + 1.0) / 2.0);  // 0→1→0
            float boost = 1.0f + t * _breatheMaxBoost;
            var (r, g, b) = _themeColor;
            SetAllWindowBorders(
                (byte)Math.Min(255, r * boost),
                (byte)Math.Min(255, g * boost),
                (byte)Math.Min(255, b * boost));
        }
        else
        {
            // Wave mode: enumerate windows and apply a staggered phase offset per window
            int windowIdx = 0;
            int total = Math.Max(_breatheWindowCount, 1);
            var (baseR, baseG, baseB) = _themeColor;
            float phase = (float)_breathePhase;
            float maxB  = _breatheMaxBoost;

            EnumWindows((hwnd, _) =>
            {
                if (!IsEligibleWindow(hwnd)) return true;

                float offset = (float)(windowIdx * 2.0 * Math.PI / total);
                float t      = ((float)Math.Sin(phase + offset) + 1.0f) / 2.0f;
                float boost  = 1.0f + t * maxB;

                var (er, eg, eb) = GetEffectiveColor(hwnd, baseR, baseG, baseB);
                int colorRef = PackColorRef(
                    (byte)Math.Min(255, er * boost),
                    (byte)Math.Min(255, eg * boost),
                    (byte)Math.Min(255, eb * boost));
                DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, ref colorRef, sizeof(int));
                windowIdx++;
                return true;
            }, IntPtr.Zero);

            _breatheWindowCount = Math.Max(windowIdx, 1);
        }
    }

    // ── Core window enumeration ────────────────────────────────────────────

    private void SetAllWindowBorders(byte globalR, byte globalG, byte globalB)
    {
        // Collect all eligible HWNDs first (avoids GC issues with capturing lambdas
        // across the unmanaged EnumWindows boundary).
        var hwnds = new List<IntPtr>(64);
        EnumWindows((hwnd, _) => { if (IsEligibleWindow(hwnd)) hwnds.Add(hwnd); return true; },
                    IntPtr.Zero);

        foreach (var hwnd in hwnds)
        {
            var (r, g, b) = GetEffectiveColor(hwnd, globalR, globalG, globalB);
            int colorRef  = PackColorRef(r, g, b);
            DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, ref colorRef, sizeof(int));
        }
    }

    private (byte r, byte g, byte b) GetEffectiveColor(IntPtr hwnd, byte globalR, byte globalG, byte globalB)
    {
        // Badge color takes priority, then per-app color, then global theme
        string? exe = TryGetExeName(hwnd);
        if (exe != null)
        {
            var key = exe.ToLowerInvariant();
            if (_badgeColors.TryGetValue(key, out var badge)) return badge;
            if (_appColors.TryGetValue(key,  out var app))   return app;
        }
        return (globalR, globalG, globalB);
    }

    private static bool IsEligibleWindow(IntPtr hwnd)
    {
        if (!IsWindowVisible(hwnd)) return false;
        int ex = GetWindowLong(hwnd, GWL_EXSTYLE);
        if ((ex & WS_EX_TOOLWINDOW) != 0) return false;
        return true;
    }

    private static string? TryGetExeName(IntPtr hwnd)
    {
        try
        {
            GetWindowThreadProcessId(hwnd, out uint pid);
            if (pid == 0) return null;
            IntPtr hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFO, false, pid);
            if (hProc == IntPtr.Zero) return null;
            try
            {
                var buf  = new System.Text.StringBuilder(1024);
                int size = buf.Capacity;
                if (!QueryFullProcessImageNameW(hProc, 0, buf, ref size)) return null;
                return System.IO.Path.GetFileName(buf.ToString());
            }
            finally { CloseHandle(hProc); }
        }
        catch { return null; }
    }

    private static int PackColorRef(byte r, byte g, byte b)
        => (b << 16) | (g << 8) | r;  // DWM COLORREF = 0x00BBGGRR
}
