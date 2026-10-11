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
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] private static extern int  GetSystemMetrics(int nIndex);
    private const int SM_YVIRTUALSCREEN  = 76;
    private const int SM_CYVIRTUALSCREEN = 79;

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern int  GetWindowLong(IntPtr hwnd, int nIndex);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);

    // EVENT_SYSTEM_FOREGROUND lets us react to focus changes instantly instead
    // of waiting up to 1.5s for the repaint timer. Without this hook the user
    // sees DWM's default-red border for whatever fraction of a second elapses
    // between activation and the next RepaintTick.
    private delegate void WinEventDelegate(IntPtr hWinEventHook, uint eventType, IntPtr hwnd,
        int idObject, int idChild, uint dwEventThread, uint dwmsEventTime);
    [DllImport("user32.dll")] private static extern IntPtr SetWinEventHook(
        uint eventMin, uint eventMax, IntPtr hmodWinEventProc,
        WinEventDelegate lpfnWinEventProc, uint idProcess, uint idThread, uint dwFlags);
    [DllImport("user32.dll")] private static extern bool UnhookWinEvent(IntPtr hWinEventHook);
    private const uint EVENT_SYSTEM_FOREGROUND    = 0x0003;
    private const uint EVENT_OBJECT_SHOW          = 0x8002;
    private const uint WINEVENT_OUTOFCONTEXT      = 0x0000;
    private const int  OBJID_WINDOW               = 0;
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

    // ── Animation mode (Glow / FadeBetween / Rainbow) ─────────────────────
    // Runs a continuous 20fps timer that computes the current border color from
    // the mode's timing params and applies it to all windows at once.

    private System.Threading.Timer? _animModeTimer;
    private const int AnimModeIntervalMs = 50;  // 20fps

    private sealed record GlowModeParams(
        byte R, byte G, byte B,
        double FadeIn, double StayOn, double FadeOut, double StayOff);

    private sealed record FadeBetweenParams(
        byte R1, byte G1, byte B1,
        byte R2, byte G2, byte B2,
        double FadeIn, double StayOn1, double FadeOut, double StayOn2);

    private sealed record RainbowParams(
        IReadOnlyList<(byte R, byte G, byte B)> Colors,
        double TransitionSec);

    private sealed record LocationGlowParams(
        byte TopR, byte TopG, byte TopB,
        byte BotR, byte BotG, byte BotB);

    private GlowModeParams?     _glowParams;
    private FadeBetweenParams?  _fadeBetweenParams;
    private RainbowParams?      _rainbowParams;
    private LocationGlowParams? _locationGlowParams;
    private double              _animPhase;     // seconds elapsed

    // Audio reactive mode — event-driven, no timer
    private bool                    _audioReactiveActive;
    private EventHandler<float[]>?  _audioHandler;
    private float                   _smoothedCentroid = 0.5f;

    // Overall brightness multiplier applied to every border color output (1.0 = normal)
    private float _brightness = 1.0f;
    public void SetBrightness(float factor) => _brightness = Math.Clamp(factor, 0f, 4f);

    private bool IsAnimationModeActive => _animModeTimer is not null || _audioReactiveActive;

    public void StartGlowMode(byte r, byte g, byte b,
        double fadeInSec, double stayOnColorSec, double fadeOutSec, double stayOnBlackSec)
    {
        StopAnimationMode();
        _glowParams = new GlowModeParams(r, g, b, fadeInSec, stayOnColorSec, fadeOutSec,
            Math.Max(0, stayOnBlackSec));
        _animPhase  = 0;
        StopBreathing();
        _animModeTimer = new System.Threading.Timer(
            _ => AnimModeTick(), null,
            TimeSpan.FromMilliseconds(AnimModeIntervalMs),
            TimeSpan.FromMilliseconds(AnimModeIntervalMs));
    }

    public void StartFadeBetweenMode(
        byte r1, byte g1, byte b1, byte r2, byte g2, byte b2,
        double fadeInSec, double stayOn1Sec, double fadeOutSec, double stayOn2Sec)
    {
        StopAnimationMode();
        _fadeBetweenParams = new FadeBetweenParams(
            r1, g1, b1, r2, g2, b2,
            fadeInSec, stayOn1Sec, fadeOutSec, stayOn2Sec);
        _animPhase = 0;
        StopBreathing();
        _animModeTimer = new System.Threading.Timer(
            _ => AnimModeTick(), null,
            TimeSpan.FromMilliseconds(AnimModeIntervalMs),
            TimeSpan.FromMilliseconds(AnimModeIntervalMs));
    }

    public void StartRainbowMode(IReadOnlyList<(byte R, byte G, byte B)> colors, double transitionSec)
    {
        if (colors.Count < 2) return;
        StopAnimationMode();
        _rainbowParams = new RainbowParams(colors, Math.Max(0.05, transitionSec));
        _animPhase     = 0;
        StopBreathing();
        _animModeTimer = new System.Threading.Timer(
            _ => AnimModeTick(), null,
            TimeSpan.FromMilliseconds(AnimModeIntervalMs),
            TimeSpan.FromMilliseconds(AnimModeIntervalMs));
    }

    public void StartAudioReactiveMode(
        byte bassR, byte bassG, byte bassB,
        byte trebleR, byte trebleG, byte trebleB,
        int hueWeight)
    {
        StopAnimationMode();
        _themeColor       = (bassR, bassG, bassB);
        _smoothedCentroid = 0.5f;
        StopBreathing();
        _audioHandler = (_, bands) =>
        {
            float raw = ComputeCentroid(bands);
            _smoothedCentroid = 0.65f * _smoothedCentroid + 0.35f * raw;

            float effective = 0.5f + (_smoothedCentroid - 0.5f) * (hueWeight / 100f);
            effective = Math.Clamp(effective, 0f, 1f);
            // S-curve: both endpoints stick to their color; transition compresses to the midrange
            effective = effective < 0.5f
                ? 0.5f * MathF.Pow(2f * effective, 3f)
                : 1f - 0.5f * MathF.Pow(2f * (1f - effective), 3f);

            float energy = 0f;
            foreach (var v in bands) energy += v;
            float brightness = 0.60f + Math.Min(1f, energy / 16f) * 0.40f;

            byte r = (byte)Math.Clamp((int)((bassR + (trebleR - bassR) * effective) * brightness), 0, 255);
            byte g = (byte)Math.Clamp((int)((bassG + (trebleG - bassG) * effective) * brightness), 0, 255);
            byte b = (byte)Math.Clamp((int)((bassB + (trebleB - bassB) * effective) * brightness), 0, 255);
            SetAllWindowBordersImmediate(r, g, b);
        };
        _audioReactiveActive = true;
        LocalAudioCapture.Instance.BandsAvailable += _audioHandler;
        LocalAudioCapture.Instance.Start();
        EnsureRepaintTimerStarted();
    }

    private static float ComputeCentroid(float[] bands)
    {
        float weighted = 0f, total = 0f;
        for (int i = 0; i < bands.Length; i++)
        {
            weighted += bands[i] * i;
            total    += bands[i];
        }
        return total < 0.001f ? 0.5f : weighted / (total * (bands.Length - 1));
    }

    public void StartLocationGlowMode(byte topR, byte topG, byte topB,
                                       byte botR, byte botG, byte botB)
    {
        StopAnimationMode();
        _locationGlowParams = new LocationGlowParams(topR, topG, topB, botR, botG, botB);
        _animPhase = 0;
        StopBreathing();
        _animModeTimer = new System.Threading.Timer(
            _ => AnimModeTick(), null,
            TimeSpan.FromMilliseconds(AnimModeIntervalMs),
            TimeSpan.FromMilliseconds(AnimModeIntervalMs));
    }

    public void StopAnimationMode()
    {
        _animModeTimer?.Dispose();
        _animModeTimer      = null;
        _glowParams         = null;
        _fadeBetweenParams  = null;
        _rainbowParams      = null;
        _locationGlowParams = null;
        _animPhase          = 0;
        if (_audioHandler is not null)
        {
            LocalAudioCapture.Instance.BandsAvailable -= _audioHandler;
            _audioHandler = null;
        }
        _audioReactiveActive = false;
        _smoothedCentroid    = 0.5f;
        // Restore the static theme color so windows don't stay at the last
        // animation frame color after the mode is stopped.
        SetAllWindowBorders(_themeColor.r, _themeColor.g, _themeColor.b);
    }

    private void AnimModeTick()
    {
        try
        {
            _animPhase += AnimModeIntervalMs / 1000.0;

            if (_glowParams is { } gp)
            {
                var color = ComputeGlowColor(gp);
                SetAllWindowBordersImmediate(color.r, color.g, color.b);
            }
            else if (_fadeBetweenParams is { } fb)
            {
                var color = ComputeFadeBetweenColor(fb);
                SetAllWindowBordersImmediate(color.r, color.g, color.b);
            }
            else if (_rainbowParams is { } rp)
            {
                var color = ComputeRainbowColor(rp);
                SetAllWindowBordersImmediate(color.r, color.g, color.b);
            }
            else if (_locationGlowParams is { } lg)
            {
                ApplyLocationGlowTick(lg);
            }
        }
        catch { }
    }

    private void ApplyLocationGlowTick(LocationGlowParams lg)
    {
        int virtualTop    = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (virtualHeight <= 0) return;

        var hwnds = new List<IntPtr>(64);
        EnumWindows((hwnd, _) => { if (IsEligibleWindow(hwnd)) hwnds.Add(hwnd); return true; },
                    IntPtr.Zero);

        foreach (var hwnd in hwnds)
        {
            if (!GetWindowRect(hwnd, out RECT r)) continue;
            int centerY = (r.Top + r.Bottom) / 2;
            double factor = Math.Clamp((double)(centerY - virtualTop) / virtualHeight, 0.0, 1.0);

            byte red   = (byte)Math.Clamp(lg.TopR + (lg.BotR - lg.TopR) * factor, 0, 255);
            byte green = (byte)Math.Clamp(lg.TopG + (lg.BotG - lg.TopG) * factor, 0, 255);
            byte blue  = (byte)Math.Clamp(lg.TopB + (lg.BotB - lg.TopB) * factor, 0, 255);

            int colorRef = PackColorRef(red, green, blue);
            DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, ref colorRef, sizeof(int));
        }
    }

    // Shared factor computation used by both Glow mode and PerSide mode.
    private double ComputeGlowFactor(double fadeIn, double stayOn, double fadeOut, double stayOff)
    {
        double cycle = fadeIn + stayOn + fadeOut + stayOff;
        if (cycle <= 0) return 1.0;
        double t = _animPhase % cycle;
        if (t < fadeIn)
            return fadeIn > 0 ? EaseOutCubic(t / fadeIn) : 1.0;
        if (t < fadeIn + stayOn)
            return 1.0;
        if (t < fadeIn + stayOn + fadeOut)
            return fadeOut > 0 ? 1.0 - EaseInOutSine((t - fadeIn - stayOn) / fadeOut) : 0.0;
        return 0.0;
    }

    private (byte r, byte g, byte b) ComputeGlowColor(GlowModeParams p)
    {
        double factor = ComputeGlowFactor(p.FadeIn, p.StayOn, p.FadeOut, p.StayOff);
        return (
            (byte)Math.Clamp((int)(p.R * factor), 0, 255),
            (byte)Math.Clamp((int)(p.G * factor), 0, 255),
            (byte)Math.Clamp((int)(p.B * factor), 0, 255));
    }

    private (byte r, byte g, byte b) ComputeFadeBetweenColor(FadeBetweenParams p)
    {
        double cycle = p.FadeIn + p.StayOn1 + p.FadeOut + p.StayOn2;
        if (cycle <= 0) return (p.R1, p.G1, p.B1);

        double t = _animPhase % cycle;
        double factor;  // 0.0 = Color1, 1.0 = Color2

        if (t < p.FadeIn)
            factor = p.FadeIn > 0 ? EaseOutCubic(t / p.FadeIn) : 1.0;
        else if (t < p.FadeIn + p.StayOn1)
            factor = 1.0;
        else if (t < p.FadeIn + p.StayOn1 + p.FadeOut)
            factor = p.FadeOut > 0
                ? 1.0 - EaseInOutSine((t - p.FadeIn - p.StayOn1) / p.FadeOut)
                : 0.0;
        else if (p.StayOn2 > 0.001)
            factor = 0.0;
        else
            factor = 0.0;

        return (
            (byte)Math.Clamp((int)(p.R1 + (p.R2 - p.R1) * factor), 0, 255),
            (byte)Math.Clamp((int)(p.G1 + (p.G2 - p.G1) * factor), 0, 255),
            (byte)Math.Clamp((int)(p.B1 + (p.B2 - p.B1) * factor), 0, 255));
    }

    private (byte r, byte g, byte b) ComputeRainbowColor(RainbowParams p)
    {
        int count    = p.Colors.Count;
        double total = count * p.TransitionSec;
        double t     = _animPhase % total;
        int    idx   = (int)(t / p.TransitionSec) % count;
        int    next  = (idx + 1) % count;
        double frac  = EaseInOutSine((t % p.TransitionSec) / p.TransitionSec);

        var (r1, g1, b1) = p.Colors[idx];
        var (r2, g2, b2) = p.Colors[next];
        return (
            (byte)Math.Clamp((int)(r1 + (r2 - r1) * frac), 0, 255),
            (byte)Math.Clamp((int)(g1 + (g2 - g1) * frac), 0, 255),
            (byte)Math.Clamp((int)(b1 + (b2 - b1) * frac), 0, 255));
    }

    private static double EaseOutCubic(double t)
    {
        t = Math.Clamp(t, 0.0, 1.0);
        return 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
    }

    private static double EaseInOutSine(double t)
    {
        t = Math.Clamp(t, 0.0, 1.0);
        return -(Math.Cos(Math.PI * t) - 1.0) / 2.0;
    }

    private void SetAllWindowBordersImmediate(byte r, byte g, byte b)
    {
        var hwnds = new List<IntPtr>(64);
        EnumWindows((hwnd, _) => { if (IsEligibleWindow(hwnd)) hwnds.Add(hwnd); return true; },
                    IntPtr.Zero);
        foreach (var hwnd in hwnds)
        {
            int colorRef = PackColorRef(r, g, b);
            DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, ref colorRef, sizeof(int));
        }
    }

    // Foreground hook + multi-window concurrent fade. Three triggers fire fades:
    //   - EVENT_SYSTEM_FOREGROUND on the NEW foreground (fade-in from black)
    //   - EVENT_SYSTEM_FOREGROUND on the PREVIOUS foreground (Windows resets it
    //     to inactive default on focus loss; we need to fade it back too)
    //   - EVENT_OBJECT_SHOW for newly-shown top-level windows (fade-in instead
    //     of snap when the repaint catches them up to 1500ms later)
    private IntPtr _foregroundHook;
    private IntPtr _objectShowHook;
    private WinEventDelegate? _foregroundCallback;
    private WinEventDelegate? _objectShowCallback;
    private IntPtr _previousForeground;
    private const int FadeFrames     = 12;   // ~12 × 25ms = 300ms total
    private const int FadeIntervalMs = 25;

    private sealed class FadeAnimation
    {
        public IntPtr Hwnd;
        public byte   TargetR, TargetG, TargetB;
        public int    Frame;       // 0 = black, FadeFrames = target reached
    }
    private readonly List<FadeAnimation> _activeFades = new();
    private readonly object _fadesLock = new();
    private System.Threading.Timer? _fadeTimer;

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
            // Skip while breathing or animation mode runs — those timers already
            // touch every window each frame; a second concurrent enum churns DWM.
            if (_breatheTimer is not null || IsAnimationModeActive) return;

            var (r, g, b) = _themeColor;
            SetAllWindowBorders(r, g, b);
        }
        catch { /* never let the timer thread crash the app */ }
    }

    /// <summary>
    /// Subscribes to EVENT_SYSTEM_FOREGROUND (focus changes) and EVENT_OBJECT_SHOW
    /// (new windows) so we can fade them in instantly instead of waiting up to
    /// RepaintIntervalMs for the next tick.  MUST be called from a thread that
    /// processes Win32 messages — the UI thread is the right choice — because
    /// WINEVENT_OUTOFCONTEXT delivers the callback via the registering thread's
    /// message queue.
    /// </summary>
    public void StartForegroundHook()
    {
        if (_foregroundHook == IntPtr.Zero)
        {
            _foregroundCallback = OnForegroundChanged;   // keep delegate alive
            _foregroundHook = SetWinEventHook(
                EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                IntPtr.Zero, _foregroundCallback,
                0, 0, WINEVENT_OUTOFCONTEXT);
        }
        if (_objectShowHook == IntPtr.Zero)
        {
            _objectShowCallback = OnObjectShown;
            _objectShowHook = SetWinEventHook(
                EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW,
                IntPtr.Zero, _objectShowCallback,
                0, 0, WINEVENT_OUTOFCONTEXT);
        }
    }

    private void OnForegroundChanged(IntPtr h, uint eventType, IntPtr hwnd,
        int idObject, int idChild, uint thread, uint time)
    {
        // Animation mode handles all windows on its own 50ms tick; skip fades.
        if (IsAnimationModeActive) return;
        if (idObject != OBJID_WINDOW || hwnd == IntPtr.Zero) return;
        if (!IsEligibleWindow(hwnd)) return;

        // Fade the PREVIOUS foreground too — when the user clicks away, DWM
        // resets that window's border to its inactive default (often red on
        // accented Win11 themes). Without re-fading it here, the user sees the
        // default flash and waits up to 1500ms for the repaint to snap it back.
        if (_previousForeground != IntPtr.Zero &&
            _previousForeground != hwnd &&
            IsEligibleWindow(_previousForeground))
        {
            var (pr, pg, pb) = GetEffectiveColor(
                _previousForeground, _themeColor.r, _themeColor.g, _themeColor.b);
            StartFade(_previousForeground, pr, pg, pb);
        }

        var (r, g, b) = GetEffectiveColor(hwnd, _themeColor.r, _themeColor.g, _themeColor.b);
        StartFade(hwnd, r, g, b);
        _previousForeground = hwnd;
    }

    private void OnObjectShown(IntPtr h, uint eventType, IntPtr hwnd,
        int idObject, int idChild, uint thread, uint time)
    {
        if (idObject != OBJID_WINDOW || hwnd == IntPtr.Zero) return;
        if (!IsEligibleWindow(hwnd)) return;
        if (IsAnimationModeActive) return;

        var (r, g, b) = GetEffectiveColor(hwnd, _themeColor.r, _themeColor.g, _themeColor.b);
        StartFade(hwnd, r, g, b);
    }

    private void StartFade(IntPtr hwnd, byte r, byte g, byte b)
    {
        // Add or replace the fade entry for this hwnd. Multiple windows can
        // fade concurrently — one driving timer steps all of them.
        lock (_fadesLock)
        {
            _activeFades.RemoveAll(f => f.Hwnd == hwnd);
            _activeFades.Add(new FadeAnimation
            {
                Hwnd = hwnd, TargetR = r, TargetG = g, TargetB = b, Frame = 0,
            });

            if (_fadeTimer is null)
            {
                _fadeTimer = new System.Threading.Timer(
                    _ => FadeTick(),
                    null,
                    TimeSpan.FromMilliseconds(FadeIntervalMs),
                    TimeSpan.FromMilliseconds(FadeIntervalMs));
            }
        }

        // Frame 0: black, applied synchronously so the default chrome flash is
        // masked before the user can perceive it.
        int black = PackColorRef(0, 0, 0);
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, ref black, sizeof(int));
    }

    private void FadeTick()
    {
        try
        {
            FadeAnimation[] snapshot;
            lock (_fadesLock) { snapshot = _activeFades.ToArray(); }

            foreach (var fade in snapshot)
            {
                fade.Frame++;
                if (fade.Frame >= FadeFrames)
                {
                    int final = PackColorRef(fade.TargetR, fade.TargetG, fade.TargetB);
                    DwmSetWindowAttribute(fade.Hwnd, DWMWA_BORDER_COLOR, ref final, sizeof(int));
                    continue;
                }

                // Ease-out cubic interpolation from (0,0,0) toward target.
                float t = (float)fade.Frame / FadeFrames;
                float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
                byte r = (byte)Math.Min(255, fade.TargetR * eased);
                byte g = (byte)Math.Min(255, fade.TargetG * eased);
                byte b = (byte)Math.Min(255, fade.TargetB * eased);
                int colorRef = PackColorRef(r, g, b);
                DwmSetWindowAttribute(fade.Hwnd, DWMWA_BORDER_COLOR, ref colorRef, sizeof(int));
            }

            // Drop completed fades and stop the timer if nothing left to drive.
            lock (_fadesLock)
            {
                _activeFades.RemoveAll(f => f.Frame >= FadeFrames);
                if (_activeFades.Count == 0)
                {
                    _fadeTimer?.Dispose();
                    _fadeTimer = null;
                }
            }
        }
        catch
        {
            lock (_fadesLock)
            {
                _fadeTimer?.Dispose();
                _fadeTimer = null;
                _activeFades.Clear();
            }
        }
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

    private int PackColorRef(byte r, byte g, byte b)
    {
        r = (byte)Math.Clamp((int)(r * _brightness), 0, 255);
        g = (byte)Math.Clamp((int)(g * _brightness), 0, 255);
        b = (byte)Math.Clamp((int)(b * _brightness), 0, 255);
        return (b << 16) | (g << 8) | r;  // DWM COLORREF = 0x00BBGGRR
    }
}
