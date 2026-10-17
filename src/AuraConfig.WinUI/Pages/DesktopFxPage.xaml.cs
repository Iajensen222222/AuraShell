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
    private const double ThumbHaloSolidUntil = 0.4;

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
        GlowThumbHalo.Fill    = HoverGlowPage.MakeHaloBrush(color, ThumbHaloSolidUntil);
        ClickThumbRing.Stroke = new SolidColorBrush(color);
        GlowSummary.Text  = DescribeGlow();
        ClickSummary.Text = DescribeClick();

        _loaded = true;

        // Push the restored state to the live service now that loading is complete.
        if (DesktopGlow is { } dg) DesktopFxSettings.ApplyTo(dg);
    }

    private static string DescribeGlow()
    {
        string style = DesktopFxSettings.GetEnum(DesktopFxSettings.GlowStyleKey,
            DesktopIconGlow.HoverGlowStyle.Solid) switch
        {
            DesktopIconGlow.HoverGlowStyle.ColorCycle => "Color cycle",
            DesktopIconGlow.HoverGlowStyle.Pulse      => "Pulse",
            _                                         => "Solid color",
        };
        string color = AppSettings.Get(DesktopFxSettings.ColorKey) is string hex && hex.Length == 6
            ? $"#{hex}" : "Default color";
        int size       = DesktopFxSettings.GetInt(DesktopFxSettings.GlowRadiusKey, DesktopFxSettings.DefaultGlowRadius, 4, 40);
        int brightness = DesktopFxSettings.GetInt(DesktopFxSettings.IntensityKey, DesktopFxSettings.DefaultIntensityPct, 0, 100);
        return $"{style} · {color} · {Fmt.Px(size)} · {Fmt.Pct(brightness)} bright";
    }

    private static string DescribeClick()
    {
        string style = DesktopFxSettings.GetEnum(DesktopFxSettings.ClickStyleKey,
            DesktopIconGlow.ClickBurstStyle.RingAndSparks) switch
        {
            DesktopIconGlow.ClickBurstStyle.Ring   => "Ring",
            DesktopIconGlow.ClickBurstStyle.Sparks => "Sparks",
            _                                      => "Ring and sparks",
        };
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
