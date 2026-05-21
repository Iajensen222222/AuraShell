using System;
using System.Runtime.InteropServices;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.UI;

namespace AuraConfig.Services;

/// Drives the slowly-cycling blue-green glow along all four window edges.
internal sealed class GlowAnimator
{
    private static readonly (byte r, byte g, byte b) Blue  = (0x00, 0x78, 0xD4);
    private static readonly (byte r, byte g, byte b) Green = (0x10, 0x7C, 0x10);

    private const int    GlowSize        = 20;    // px — edge strip thickness
    private const byte   GlowAlpha       = 0xBB;  // 73% opacity at peak
    private const double CycleSeconds    = 8.0;   // full blue→green→blue period
    private const double TicksPerSecond  = 30.0;

    private readonly GradientStop[] _stops    = new GradientStop[4]; // one per edge
    private double   _phase                   = 0.0;
    private readonly double _phaseStep        = 2 * Math.PI / (CycleSeconds * TicksPerSecond);

    // Windows 11 21H2+ DWM border color (DWMWA_BORDER_COLOR = 34)
    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
    private const int DWMWA_BORDER_COLOR = 34;
    private IntPtr _hwnd = IntPtr.Zero;

    // -------------------------------------------------------------------------
    // Build the overlay grid — add as the last child of the root Grid so it
    // renders on top of the SplitView but never intercepts mouse/touch input.
    // -------------------------------------------------------------------------
    public Grid BuildOverlay()
    {
        var overlay = new Grid { IsHitTestVisible = false };

        // Top strip: glow color at top, fades to transparent downward
        overlay.Children.Add(MakeStrip(
            height:    GlowSize,
            vAlign:    VerticalAlignment.Top,
            hAlign:    HorizontalAlignment.Stretch,
            startPt:   new Windows.Foundation.Point(0, 0),
            endPt:     new Windows.Foundation.Point(0, 1),
            stopIndex: 0));

        // Bottom strip: glow color at bottom, fades to transparent upward
        overlay.Children.Add(MakeStrip(
            height:    GlowSize,
            vAlign:    VerticalAlignment.Bottom,
            hAlign:    HorizontalAlignment.Stretch,
            startPt:   new Windows.Foundation.Point(0, 1),
            endPt:     new Windows.Foundation.Point(0, 0),
            stopIndex: 1));

        // Left strip: glow color at left, fades to transparent rightward
        overlay.Children.Add(MakeStrip(
            width:     GlowSize,
            vAlign:    VerticalAlignment.Stretch,
            hAlign:    HorizontalAlignment.Left,
            startPt:   new Windows.Foundation.Point(0, 0),
            endPt:     new Windows.Foundation.Point(1, 0),
            stopIndex: 2));

        // Right strip: glow color at right, fades to transparent leftward
        overlay.Children.Add(MakeStrip(
            width:     GlowSize,
            vAlign:    VerticalAlignment.Stretch,
            hAlign:    HorizontalAlignment.Right,
            startPt:   new Windows.Foundation.Point(1, 0),
            endPt:     new Windows.Foundation.Point(0, 0),
            stopIndex: 3));

        return overlay;
    }

    public void SetWindowHandle(IntPtr hwnd) => _hwnd = hwnd;

    // -------------------------------------------------------------------------
    // Called every ~33ms by the DispatcherQueueTimer in MainWindow.
    // -------------------------------------------------------------------------
    public void Tick()
    {
        _phase += _phaseStep;
        if (_phase >= 2 * Math.PI) _phase -= 2 * Math.PI;

        // t oscillates 0→1→0 via sine (0 = blue, 1 = green)
        double t = (Math.Sin(_phase) + 1.0) / 2.0;

        byte r = Lerp(Blue.r,  Green.r,  t);
        byte g = Lerp(Blue.g,  Green.g,  t);
        byte b = Lerp(Blue.b,  Green.b,  t);

        var glowColor = Color.FromArgb(GlowAlpha, r, g, b);
        foreach (var stop in _stops)
            stop.Color = glowColor;

        // Keep the 1px DWM system border in sync (COLORREF = 0x00BBGGRR)
        if (_hwnd != IntPtr.Zero)
        {
            int colorRef = (b << 16) | (g << 8) | r;
            DwmSetWindowAttribute(_hwnd, DWMWA_BORDER_COLOR, ref colorRef, sizeof(int));
        }
    }

    // -------------------------------------------------------------------------

    private Rectangle MakeStrip(
        int height = 0, int width = 0,
        VerticalAlignment   vAlign   = VerticalAlignment.Top,
        HorizontalAlignment hAlign   = HorizontalAlignment.Stretch,
        Windows.Foundation.Point startPt = default,
        Windows.Foundation.Point endPt   = default,
        int stopIndex = 0)
    {
        // Only the edge GradientStop is animated; the inner stop stays transparent.
        var colorStop = new GradientStop { Offset = 0, Color = Colors.Transparent };
        var clearStop = new GradientStop { Offset = 1, Color = Colors.Transparent };
        _stops[stopIndex] = colorStop;

        var brush = new LinearGradientBrush
        {
            StartPoint    = startPt,
            EndPoint      = endPt,
            GradientStops = new GradientStopCollection { colorStop, clearStop },
        };

        var rect = new Rectangle
        {
            Fill                = brush,
            VerticalAlignment   = vAlign,
            HorizontalAlignment = hAlign,
        };
        if (height > 0) rect.Height = height;
        if (width  > 0) rect.Width  = width;
        return rect;
    }

    private static byte Lerp(byte a, byte b, double t) =>
        (byte)(a + (b - a) * t);
}
