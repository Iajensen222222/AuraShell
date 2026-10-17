using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.UI;
using GlowShape = AuraConfig.Services.DesktopIconGlow.GlowShape;
using HoverGlowStyle = AuraConfig.Services.DesktopIconGlow.HoverGlowStyle;

namespace AuraConfig.Pages;

/// <summary>
/// Every option of the desktop icon hover glow, with a live preview at the top.
/// Opened from a row on <see cref="DesktopFxPage"/>.
/// </summary>
public sealed partial class HoverGlowPage : Page
{
    // Preview: three icons on a dark "desktop". The glow is drawn by the same code as the
    // real one (DesktopFxPainter) into a bitmap behind each icon.
    private const int    PreviewIconCount   = 3;
    private const int    PreviewIconBox     = 44;
    private const double PreviewIconSpacing = 92;
    private const int    PreviewCornerRadius = 5;   // the real 94 px icon box uses 10
    private const double PreviewFrameMs     = 33;
    // The real glow moves its alpha toward the target by FadeStep × speed every 16 ms frame.
    private const float  EngineFadeInStep   = 0.18f;
    private const float  EngineFadeOutStep  = 0.14f;
    private const double EngineFrameMs      = 16;
    private const int    DefaultLitIcon     = 1;   // lit while the mouse is not over the preview
    private const float  MinVisibleLevel    = 0.003f;

    private bool _loaded;
    private byte _r = 0x00, _g = 0x78, _b = 0xD4;
    private readonly Image[] _glowImages  = new Image[PreviewIconCount];
    private readonly float[] _glowLevels  = new float[PreviewIconCount];
    private FxBitmap? _glowBitmap;
    private DesktopFxPainter.GlowLook? _paintedLook;
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
            (_r, _g, _b) = (r, g, b);
            ColorPreview.Background = new SolidColorBrush(Color.FromArgb(255, r, g, b));
        }

        GlowSizeSlider.Value  = DesktopFxSettings.GetInt(DesktopFxSettings.GlowRadiusKey,
            DesktopFxSettings.DefaultGlowRadius, 4, 40);
        IntensitySlider.Value = DesktopFxSettings.GetInt(DesktopFxSettings.IntensityKey,
            DesktopFxSettings.DefaultIntensityPct, 0, 100);
        FadeSpeedSlider.Value = DesktopFxSettings.GetInt(DesktopFxSettings.FadeSpeedKey,
            DesktopFxSettings.DefaultFadeSpeedPct, 20, 300);

        var shape = DesktopFxSettings.GetEnum(DesktopFxSettings.GlowShapeKey, GlowShape.SoftEdge);
        SelectByTag(GlowShapeBox, shape.ToString());
        ShapeDescription.Text = DescribeShape(shape);

        var glowStyle = DesktopFxSettings.GetEnum(DesktopFxSettings.GlowStyleKey, HoverGlowStyle.Solid);
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
        double top  = (PreviewCanvas.Height - PreviewIconBox) / 2;
        double left = (PreviewCanvas.Width - PreviewIconSpacing * (PreviewIconCount - 1) - PreviewIconBox) / 2;

        // All glow images first so every icon sits on top of every glow.
        for (int i = 0; i < PreviewIconCount; i++)
        {
            _glowImages[i] = new Image { IsHitTestVisible = false, Opacity = 0, Stretch = Stretch.Fill };
            PreviewCanvas.Children.Add(_glowImages[i]);
        }

        for (int i = 0; i < PreviewIconCount; i++)
        {
            // A transparent background makes the whole icon box hit-testable.
            int index = i;
            var icon = new Border
            {
                Width      = PreviewIconBox,
                Height     = PreviewIconBox,
                Background = new SolidColorBrush(Color.FromArgb(0, 0, 0, 0)),
                Child = new FontIcon
                {
                    Glyph      = i == 1 ? "" : "",   // document between two folders
                    FontSize   = 34,
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
        _glowLevels[DefaultLitIcon] = 1f;
    }

    private DesktopFxPainter.GlowLook CurrentLook() => new(
        _r, _g, _b,
        (float)IntensitySlider.Value / 100f,
        (int)GlowSizeSlider.Value,
        PreviewCornerRadius,
        Enum.TryParse(SelectedTag(GlowStyleBox), out HoverGlowStyle style) ? style : HoverGlowStyle.Solid,
        Enum.TryParse(SelectedTag(GlowShapeBox), out GlowShape shape) ? shape : GlowShape.SoftEdge,
        (float)CycleSecondsSlider.Value);

    private unsafe void UpdatePreview()
    {
        var  look = CurrentLook();
        long now  = Environment.TickCount64;

        // Same exponential fade as the engine, scaled from its 16 ms frames to ours.
        float speed  = (float)FadeSpeedSlider.Value / 100f;
        int   frames = (int)Math.Round(PreviewFrameMs / EngineFrameMs);
        int   lit    = _hoveredIcon >= 0 ? _hoveredIcon : DefaultLitIcon;
        bool  anyVisible = false;
        for (int i = 0; i < PreviewIconCount; i++)
        {
            float target = i == lit && GlowToggle.IsOn ? 1f : 0f;
            float step   = (target > _glowLevels[i] ? EngineFadeInStep : EngineFadeOutStep) * speed;
            for (int f = 0; f < frames; f++)
                _glowLevels[i] += (target - _glowLevels[i]) * Math.Min(step, 1f);
            anyVisible |= _glowLevels[i] > MinVisibleLevel;
        }
        if (!anyVisible)
        {
            foreach (var img in _glowImages) img.Opacity = 0;
            return;
        }

        // One bitmap serves all three icons; redraw it only when it would look different.
        int side = PreviewIconBox + 2 * look.Margin;
        if (_glowBitmap is null || _glowBitmap.Size != side)
        {
            _glowBitmap  = new FxBitmap(side);
            _paintedLook = null;
            double top  = (PreviewCanvas.Height - side) / 2;
            double left = (PreviewCanvas.Width - PreviewIconSpacing * (PreviewIconCount - 1) - side) / 2;
            for (int i = 0; i < PreviewIconCount; i++)
            {
                _glowImages[i].Source = _glowBitmap.Bitmap;
                _glowImages[i].Width  = _glowImages[i].Height = side;
                Canvas.SetLeft(_glowImages[i], left + i * PreviewIconSpacing);
                Canvas.SetTop(_glowImages[i], top);
            }
        }
        if (_paintedLook != look || DesktopFxPainter.IsAnimatedPaint(look.Style))
        {
            fixed (uint* buf = _glowBitmap.Pixels)
                DesktopFxPainter.PaintGlow(buf, side, side, look, now);
            _glowBitmap.Publish();
            _paintedLook = look;
        }

        float level = DesktopFxPainter.GlowLevel(look.Style, look.CycleSeconds, now);
        for (int i = 0; i < PreviewIconCount; i++)
            _glowImages[i].Opacity = _glowLevels[i] * level;
    }

    private static string DescribeShape(GlowShape shape) => shape switch
    {
        GlowShape.Neon      => "A crisp bright line around the icon with a soft glow outside it.",
        GlowShape.RoundHalo => "A round glow centered on the icon instead of following its square.",
        GlowShape.Backlight => "Lights up behind the icon, so it shines through gaps in the logo and around it.",
        _                   => "A soft glow that fades out from the edges of the icon.",
    };

    // ── Starting looks ─────────────────────────────────────────────────────
    //
    // Each look sets shape, style, size, brightness and speed; the color is left alone.

    private void Look_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: string look }) return;
        var (shape, style, cycle, size, brightness, fade) = look switch
        {
            "Subtle"    => (GlowShape.SoftEdge,  HoverGlowStyle.Solid,       6, 10,  45, 100),
            "Neon"      => (GlowShape.Neon,      HoverGlowStyle.Solid,       6, 18, 100, 150),
            "Backlight" => (GlowShape.Backlight, HoverGlowStyle.Solid,       6, 16,  90, 100),
            "Rainbow"   => (GlowShape.RoundHalo, HoverGlowStyle.RainbowRing, 4, 22,  90, 100),
            "Orbit"     => (GlowShape.Neon,      HoverGlowStyle.Orbit,       2, 20, 100, 120),
            "Candle"    => (GlowShape.RoundHalo, HoverGlowStyle.Flicker,     6, 24,  85,  60),
            _           => (GlowShape.SoftEdge,  HoverGlowStyle.Solid,
                            DesktopFxSettings.DefaultCycleSeconds, DesktopFxSettings.DefaultGlowRadius,
                            DesktopFxSettings.DefaultIntensityPct, DesktopFxSettings.DefaultFadeSpeedPct),
        };
        // Each assignment goes through the control's handler, which saves and applies it.
        SelectByTag(GlowShapeBox, shape.ToString());
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
        (_r, _g, _b) = (r, g, b);
        ColorPreview.Background = new SolidColorBrush(Color.FromArgb(255, r, g, b));
        if (DesktopGlow is { } dg) dg.SetColor(r, g, b);
        AppSettings.Set(DesktopFxSettings.ColorKey, $"{r:X2}{g:X2}{b:X2}");
    }

    // ── Shape and style ────────────────────────────────────────────────────

    private void GlowShapeBox_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!_loaded || GlowShapeBox is null) return;
        if (!Enum.TryParse(SelectedTag(GlowShapeBox), out GlowShape shape)) return;
        ShapeDescription.Text = DescribeShape(shape);
        if (DesktopGlow is { } dg) dg.Shape = shape;
        AppSettings.Set(DesktopFxSettings.GlowShapeKey, shape.ToString());
    }

    // Cycle length drives every animated style except the candle flicker, which has its own pace.
    private void UpdateCycleControls(HoverGlowStyle style)
        => CycleControls.Visibility = style is HoverGlowStyle.Solid or HoverGlowStyle.Flicker
            ? Visibility.Collapsed : Visibility.Visible;

    private void GlowStyleBox_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!_loaded || GlowStyleBox is null || CycleControls is null) return;
        if (!Enum.TryParse(SelectedTag(GlowStyleBox), out HoverGlowStyle style)) return;
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
