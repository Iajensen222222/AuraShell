using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.UI;

namespace AuraConfig.Pages;

/// <summary>
/// Every option of the desktop icon hover glow, with a live preview at the top.
/// Opened from a row on <see cref="DesktopFxPage"/>.
/// </summary>
public sealed partial class HoverGlowPage : Page
{
    // Preview: three icons on a dark "desktop", each with a radial halo behind it.
    private const int    PreviewIconCount   = 3;
    private const double PreviewIconSize    = 36;
    private const double PreviewIconSpacing = 84;
    private const double PreviewFrameMs     = 33;
    // The real glow moves its alpha toward the target by FadeStep × speed every 16 ms frame.
    private const float  EngineFadeInStep   = 0.18f;
    private const float  EngineFadeOutStep  = 0.14f;
    private const double EngineFrameMs      = 16;
    private const int    DefaultLitIcon     = 1;   // lit while the mouse is not over the preview

    private bool _loaded;
    private byte _r = 0x00, _g = 0x78, _b = 0xD4;
    private readonly Ellipse[]      _halos      = new Ellipse[PreviewIconCount];
    private Brush? _haloBrush;
    private (Color color, double offset) _haloBrushKey;
    private readonly float[]        _haloLevels = new float[PreviewIconCount];
    private int _hoveredIcon = -1;
    private readonly DispatcherTimer _previewTimer = new() { Interval = TimeSpan.FromMilliseconds(PreviewFrameMs) };

    public HoverGlowPage()
    {
        InitializeComponent();
        BuildPreview();
        _previewTimer.Tick += (_, _) => UpdatePreview();
        Loaded   += OnLoaded;
        Unloaded += (_, _) => _previewTimer.Stop();
    }

    private DesktopIconGlow? DesktopGlow => (Application.Current as App)?.MainWindow?.DesktopIconGlow;

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        GlowToggle.IsOn = DesktopFxSettings.GetBool(DesktopFxSettings.GlowEnabledKey, true);

        if (DesktopFxSettings.GetColor() is var (r, g, b))
        {
            HexInput.Text = $"#{r:X2}{g:X2}{b:X2}";
            SetPreviewColor(r, g, b);
            ColorPreview.Background = new SolidColorBrush(Color.FromArgb(255, r, g, b));
        }

        GlowSizeSlider.Value  = DesktopFxSettings.GetInt(DesktopFxSettings.GlowRadiusKey,
            DesktopFxSettings.DefaultGlowRadius, 4, 40);
        IntensitySlider.Value = DesktopFxSettings.GetInt(DesktopFxSettings.IntensityKey,
            DesktopFxSettings.DefaultIntensityPct, 0, 100);
        FadeSpeedSlider.Value = DesktopFxSettings.GetInt(DesktopFxSettings.FadeSpeedKey,
            DesktopFxSettings.DefaultFadeSpeedPct, 20, 300);

        var glowStyle = DesktopFxSettings.GetEnum(DesktopFxSettings.GlowStyleKey,
            DesktopIconGlow.HoverGlowStyle.Solid);
        SelectByTag(GlowStyleBox, glowStyle.ToString());
        UpdateCycleControls(glowStyle);
        CycleSecondsSlider.Value = DesktopFxSettings.GetInt(DesktopFxSettings.CycleSecondsKey,
            DesktopFxSettings.DefaultCycleSeconds, 1, 20);

        _loaded = true;
        _previewTimer.Start();
    }

    private void Back_Click(object sender, RoutedEventArgs e)
        => (Application.Current as App)?.MainWindow?.NavigateTo("desktopfx");

    // ── Live preview ───────────────────────────────────────────────────────

    private void BuildPreview()
    {
        double top  = (PreviewCanvas.Height - PreviewIconSize) / 2;
        double left = (PreviewCanvas.Width - PreviewIconSpacing * (PreviewIconCount - 1) - PreviewIconSize) / 2;

        for (int i = 0; i < PreviewIconCount; i++)
        {
            var halo = new Ellipse { IsHitTestVisible = false, Opacity = 0 };
            _halos[i] = halo;
            PreviewCanvas.Children.Add(halo);

            // A transparent background makes the whole icon square hit-testable.
            int index = i;
            var icon = new Border
            {
                Width      = PreviewIconSize,
                Height     = PreviewIconSize,
                Background = new SolidColorBrush(Color.FromArgb(0, 0, 0, 0)),
                Child = new FontIcon
                {
                    Glyph      = i == 1 ? "" : "",   // document between two folders
                    FontSize   = 30,
                    Foreground = new SolidColorBrush(i == 1
                        ? Color.FromArgb(0xFF, 0xE6, 0xE6, 0xE6)
                        : Color.FromArgb(0xFF, 0xF2, 0xC9, 0x4C)),
                },
            };
            icon.PointerEntered += (_, _) => _hoveredIcon = index;
            icon.PointerExited  += (_, _) => { if (_hoveredIcon == index) _hoveredIcon = -1; };
            Canvas.SetLeft(icon, left + i * PreviewIconSpacing);
            Canvas.SetTop(icon, top);
            PreviewCanvas.Children.Add(icon);
        }
        _haloLevels[DefaultLitIcon] = 1f;
    }

    private void SetPreviewColor(byte r, byte g, byte b) => (_r, _g, _b) = (r, g, b);

    private void UpdatePreview()
    {
        var style = Enum.TryParse(SelectedTag(GlowStyleBox), out DesktopIconGlow.HoverGlowStyle s)
            ? s : DesktopIconGlow.HoverGlowStyle.Solid;
        var (r, g, b, level) = DesktopIconGlow.PreviewFrame(_r, _g, _b, style,
            (int)CycleSecondsSlider.Value, Environment.TickCount64);

        // Same exponential fade as the engine, scaled from its 16 ms frames to ours.
        float speed  = (float)FadeSpeedSlider.Value / 100f;
        int   frames = (int)Math.Round(PreviewFrameMs / EngineFrameMs);
        int   lit    = _hoveredIcon >= 0 ? _hoveredIcon : DefaultLitIcon;

        double radius   = GlowSizeSlider.Value;
        double haloSize = PreviewIconSize + 2 * radius;
        double iconTop  = (PreviewCanvas.Height - PreviewIconSize) / 2;
        double iconLeft = (PreviewCanvas.Width - PreviewIconSpacing * (PreviewIconCount - 1) - PreviewIconSize) / 2;
        float  alpha    = GlowToggle.IsOn ? (float)IntensitySlider.Value / 100f * level : 0f;

        // RadialGradientBrush ignores changes to its stops after it is first drawn, so a new
        // brush is made whenever the color or size changes (every frame only in Color cycle).
        var key = (Color.FromArgb(255, r, g, b), PreviewIconSize / haloSize);
        if (_haloBrush is null || key != _haloBrushKey)
        {
            _haloBrushKey = key;
            _haloBrush    = MakeHaloBrush(key.Item1, key.Item2);
        }

        for (int i = 0; i < PreviewIconCount; i++)
        {
            float target = i == lit ? 1f : 0f;
            float step   = (target > _haloLevels[i] ? EngineFadeInStep : EngineFadeOutStep) * speed;
            for (int f = 0; f < frames; f++)
                _haloLevels[i] += (target - _haloLevels[i]) * Math.Min(step, 1f);

            var halo = _halos[i];
            halo.Width  = haloSize;
            halo.Height = haloSize;
            Canvas.SetLeft(halo, iconLeft + i * PreviewIconSpacing + PreviewIconSize / 2 - haloSize / 2);
            Canvas.SetTop(halo, iconTop + PreviewIconSize / 2 - haloSize / 2);
            if (halo.Fill != _haloBrush) halo.Fill = _haloBrush;
            halo.Opacity = alpha * _haloLevels[i];
        }
    }

    /// <summary>Solid <paramref name="color"/> out to <paramref name="solidUntil"/> of the radius, then fading out.</summary>
    internal static RadialGradientBrush MakeHaloBrush(Color color, double solidUntil) => new()
    {
        GradientStops =
        {
            new GradientStop { Color = color, Offset = solidUntil },
            new GradientStop { Color = Color.FromArgb(0, color.R, color.G, color.B), Offset = 1 },
        },
    };

    // ── Starting looks ─────────────────────────────────────────────────────
    //
    // Each look sets style, size, brightness and speed; the color is left alone.

    private void Look_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: string look }) return;
        var (style, cycle, size, brightness, fade) = look switch
        {
            "Subtle"    => (DesktopIconGlow.HoverGlowStyle.Solid,      6, 10,  45, 100),
            "Bold"      => (DesktopIconGlow.HoverGlowStyle.Solid,      6, 30, 100, 150),
            "Rainbow"   => (DesktopIconGlow.HoverGlowStyle.ColorCycle, 6, 20,  90, 100),
            "Breathing" => (DesktopIconGlow.HoverGlowStyle.Pulse,      3, 18,  85,  60),
            _           => (DesktopIconGlow.HoverGlowStyle.Solid,
                            DesktopFxSettings.DefaultCycleSeconds, DesktopFxSettings.DefaultGlowRadius,
                            DesktopFxSettings.DefaultIntensityPct, DesktopFxSettings.DefaultFadeSpeedPct),
        };
        // Each assignment goes through the control's handler, which saves and applies it.
        SelectByTag(GlowStyleBox, style.ToString());
        CycleSecondsSlider.Value = cycle;
        GlowSizeSlider.Value     = size;
        IntensitySlider.Value    = brightness;
        FadeSpeedSlider.Value    = fade;
    }

    private void Reset_Click(object sender, RoutedEventArgs e)
        => Look_Click(new Button { Tag = "Default" }, e);

    private static void SelectByTag(ComboBox box, string tag)
    {
        foreach (var item in box.Items)
        {
            if (item is ComboBoxItem cbi && cbi.Tag as string == tag)
            {
                box.SelectedItem = cbi;
                return;
            }
        }
    }

    private static string? SelectedTag(ComboBox box)
        => (box.SelectedItem as ComboBoxItem)?.Tag as string;

    // ── Enable toggle ──────────────────────────────────────────────────────

    private void GlowToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (!_loaded || GlowToggle is null) return;
        bool on = GlowToggle.IsOn;
        if (DesktopGlow is { } dg) dg.Enabled = on;
        DesktopFxSettings.SetBool(DesktopFxSettings.GlowEnabledKey, on);
    }

    // ── Color ──────────────────────────────────────────────────────────────

    private void HexInput_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == Windows.System.VirtualKey.Enter)
            ApplyHex_Click(sender, null!);
    }

    private void ColorPreview_Click(object sender, RoutedEventArgs e)
    {
        DashboardPage.ShowColorPickerWindow(_r, _g, _b, (nr, ng, nb) =>
        {
            HexInput.Text = $"#{nr:X2}{ng:X2}{nb:X2}";
            ApplyColor(nr, ng, nb);
        });
    }

    private void ApplyHex_Click(object sender, RoutedEventArgs e)
    {
        var raw = HexInput.Text.Trim().TrimStart('#');
        if (raw.Length != 6) return;
        try
        {
            ApplyColor(Convert.ToByte(raw[0..2], 16), Convert.ToByte(raw[2..4], 16), Convert.ToByte(raw[4..6], 16));
        }
        catch (FormatException) { }
    }

    private void ApplyColor(byte r, byte g, byte b)
    {
        SetPreviewColor(r, g, b);
        ColorPreview.Background = new SolidColorBrush(Color.FromArgb(255, r, g, b));
        if (DesktopGlow is { } dg) dg.SetColor(r, g, b);
        AppSettings.Set(DesktopFxSettings.ColorKey, $"{r:X2}{g:X2}{b:X2}");
    }

    // ── Style ──────────────────────────────────────────────────────────────

    private void UpdateCycleControls(DesktopIconGlow.HoverGlowStyle style)
        => CycleControls.Visibility = style == DesktopIconGlow.HoverGlowStyle.Solid
            ? Visibility.Collapsed : Visibility.Visible;

    private void GlowStyleBox_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!_loaded || GlowStyleBox is null || CycleControls is null) return;
        if (!Enum.TryParse(SelectedTag(GlowStyleBox), out DesktopIconGlow.HoverGlowStyle style)) return;
        UpdateCycleControls(style);
        if (DesktopGlow is { } dg) dg.GlowStyle = style;
        AppSettings.Set(DesktopFxSettings.GlowStyleKey, style.ToString());
    }

    private void CycleSecondsSlider_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (!_loaded || CycleSecondsSlider is null) return;
        int seconds = (int)CycleSecondsSlider.Value;
        if (DesktopGlow is { } dg) dg.CycleSeconds = seconds;
        AppSettings.Set(DesktopFxSettings.CycleSecondsKey, seconds.ToString());
    }

    // ── Size and motion ────────────────────────────────────────────────────

    private void GlowSizeSlider_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (!_loaded || GlowSizeSlider is null) return;
        int radius = (int)GlowSizeSlider.Value;
        if (DesktopGlow is { } dg) dg.GlowRadius = radius;
        AppSettings.Set(DesktopFxSettings.GlowRadiusKey, radius.ToString());
    }

    private void IntensitySlider_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (!_loaded || IntensitySlider is null) return;
        if (DesktopGlow is { } dg) dg.Intensity = (float)IntensitySlider.Value / 100f;
        AppSettings.Set(DesktopFxSettings.IntensityKey, ((int)IntensitySlider.Value).ToString());
    }

    private void FadeSpeedSlider_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (!_loaded || FadeSpeedSlider is null) return;
        if (DesktopGlow is { } dg) dg.FadeSpeed = (float)FadeSpeedSlider.Value / 100f;
        AppSettings.Set(DesktopFxSettings.FadeSpeedKey, ((int)FadeSpeedSlider.Value).ToString());
    }
}
