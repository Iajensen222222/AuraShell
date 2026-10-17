using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace AuraConfig.Pages;

/// <summary>
/// Desktop FX overview: one row per effect with an on/off switch and a summary of its
/// settings. Clicking a row opens that effect's own page (<see cref="HoverGlowPage"/>,
/// <see cref="ClickBurstPage"/>).
/// </summary>
public sealed partial class DesktopFxPage : Page
{
    // Suppresses event handlers during InitializeComponent and OnLoaded so that
    // XAML-default property assignments don't overwrite saved AppSettings values.
    private bool _loaded = false;

    private static readonly Color DefaultGlowColor = Color.FromArgb(255, 0x00, 0x78, 0xD4);  // DesktopIconGlow's default
    // Overview thumbnail: a small icon box with the glow drawn by the real painter.
    private const int ThumbIconBox = 24;
    private const int ThumbMargin  = 10;
    private const int ThumbCorner  = 4;

    public DesktopFxPage()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private DesktopIconGlow? DesktopGlow => (Application.Current as App)?.MainWindow?.DesktopIconGlow;

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        GlowToggle.IsOn       = DesktopFxSettings.GetBool(DesktopFxSettings.GlowEnabledKey, true);
        ClickToggle.IsOn      = DesktopFxSettings.GetBool(DesktopFxSettings.ClickEnabledKey, true);
        ShowLabelsToggle.IsOn = DesktopFxSettings.GetBool(DesktopFxSettings.LabelsVisibleKey, true);

        var color = DesktopFxSettings.GetColor() is var (r, g, b)
            ? Color.FromArgb(255, r, g, b) : DefaultGlowColor;
        DrawGlowThumb(color);
        ClickThumbRing.Stroke = new SolidColorBrush(color);
        GlowSummary.Text  = DescribeGlow();
        ClickSummary.Text = DescribeClick();

        _loaded = true;

        // Push the restored state to the live service now that loading is complete.
        if (DesktopGlow is { } dg) DesktopFxSettings.ApplyTo(dg);
    }

    private unsafe void DrawGlowThumb(Color color)
    {
        var look = new DesktopFxPainter.GlowLook(color.R, color.G, color.B, 1f, ThumbMargin, ThumbCorner,
            DesktopFxSettings.GetEnum(DesktopFxSettings.GlowStyleKey, DesktopIconGlow.HoverGlowStyle.Solid),
            DesktopFxSettings.GetEnum(DesktopFxSettings.GlowShapeKey, DesktopIconGlow.GlowShape.SoftEdge),
            DesktopFxSettings.DefaultCycleSeconds);
        var bmp = new FxBitmap(ThumbIconBox + 2 * ThumbMargin);
        fixed (uint* buf = bmp.Pixels)
            DesktopFxPainter.PaintGlow(buf, bmp.Size, bmp.Size, look, 0);
        bmp.Publish();
        GlowThumb.Source = bmp.Bitmap;
    }

    internal static string StyleName(DesktopIconGlow.HoverGlowStyle style) => style switch
    {
        DesktopIconGlow.HoverGlowStyle.ColorCycle  => "Color cycle",
        DesktopIconGlow.HoverGlowStyle.Pulse       => "Pulse",
        DesktopIconGlow.HoverGlowStyle.RainbowRing => "Rainbow ring",
        DesktopIconGlow.HoverGlowStyle.Orbit       => "Orbiting light",
        DesktopIconGlow.HoverGlowStyle.Flicker     => "Candle flicker",
        _                                          => "Solid color",
    };

    internal static string ShapeName(DesktopIconGlow.GlowShape shape) => shape switch
    {
        DesktopIconGlow.GlowShape.Neon      => "Neon outline",
        DesktopIconGlow.GlowShape.RoundHalo => "Round halo",
        DesktopIconGlow.GlowShape.Backlight => "Backlight",
        _                                   => "Soft edge",
    };

    internal static string BurstName(DesktopIconGlow.ClickBurstStyle style) => style switch
    {
        DesktopIconGlow.ClickBurstStyle.Ring     => "Ring",
        DesktopIconGlow.ClickBurstStyle.Sparks   => "Sparks",
        DesktopIconGlow.ClickBurstStyle.Ripple   => "Ripple",
        DesktopIconGlow.ClickBurstStyle.Rays     => "Rays",
        DesktopIconGlow.ClickBurstStyle.Confetti => "Confetti",
        DesktopIconGlow.ClickBurstStyle.Firework => "Firework",
        _                                        => "Ring and sparks",
    };

    private static string DescribeGlow()
    {
        string style = StyleName(DesktopFxSettings.GetEnum(DesktopFxSettings.GlowStyleKey,
            DesktopIconGlow.HoverGlowStyle.Solid));
        string shape = ShapeName(DesktopFxSettings.GetEnum(DesktopFxSettings.GlowShapeKey,
            DesktopIconGlow.GlowShape.SoftEdge));
        string color = AppSettings.Get(DesktopFxSettings.ColorKey) is string hex && hex.Length == 6
            ? $"#{hex}" : "Default color";
        int size       = DesktopFxSettings.GetInt(DesktopFxSettings.GlowRadiusKey, DesktopFxSettings.DefaultGlowRadius, 4, 40);
        int brightness = DesktopFxSettings.GetInt(DesktopFxSettings.IntensityKey, DesktopFxSettings.DefaultIntensityPct, 0, 100);
        return $"{shape} · {style} · {color} · {Fmt.Px(size)} · {Fmt.Pct(brightness)} bright";
    }

    private static string DescribeClick()
    {
        string style = BurstName(DesktopFxSettings.GetEnum(DesktopFxSettings.ClickStyleKey,
            DesktopIconGlow.ClickBurstStyle.RingAndSparks));
        int size = DesktopFxSettings.GetInt(DesktopFxSettings.ClickSizeKey, DesktopFxSettings.DefaultClickSize, 20, 120);
        int ms   = DesktopFxSettings.GetInt(DesktopFxSettings.ClickDurationKey, DesktopFxSettings.DefaultClickDurationMs, 200, 1200);
        string where = DesktopFxSettings.GetBool(DesktopFxSettings.ClickDesktopOnlyKey, true) ? "desktop only" : "everywhere";
        return $"{style} · {Fmt.Px(size)} · {Fmt.Ms(ms)} · {where}";
    }

    // ── Rows ───────────────────────────────────────────────────────────────

    private void GlowRow_Click(object sender, RoutedEventArgs e)
        => (Application.Current as App)?.MainWindow?.NavigateTo("fx-glow");

    private void ClickRow_Click(object sender, RoutedEventArgs e)
        => (Application.Current as App)?.MainWindow?.NavigateTo("fx-click");

    private void GlowToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (!_loaded || GlowToggle is null) return;
        bool on = GlowToggle.IsOn;
        if (DesktopGlow is { } dg) dg.Enabled = on;
        DesktopFxSettings.SetBool(DesktopFxSettings.GlowEnabledKey, on);
    }

    private void ClickToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (!_loaded || ClickToggle is null) return;
        bool on = ClickToggle.IsOn;
        if (DesktopGlow is { } dg) dg.ClickBurstEnabled = on;
        DesktopFxSettings.SetBool(DesktopFxSettings.ClickEnabledKey, on);
    }

    // ── Icon label visibility ──────────────────────────────────────────────
    //
    // Delegates to DesktopIconGlow.HideIconLabels which clips the SysListView32
    // window region to exclude all label rects (LVIR_LABEL), making the desktop
    // wallpaper show through where labels would appear. Works for any wallpaper.

    private void ShowLabelsToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (!_loaded || ShowLabelsToggle is null) return;
        bool visible = ShowLabelsToggle.IsOn;
        if (DesktopGlow is { } dg) dg.HideIconLabels = !visible;
        DesktopFxSettings.SetBool(DesktopFxSettings.LabelsVisibleKey, visible);
    }
}
