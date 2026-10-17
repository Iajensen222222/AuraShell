using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Animation;
using Microsoft.UI.Xaml.Shapes;
using Windows.UI;

namespace AuraConfig.Pages;

/// <summary>
/// Every option of the cursor click burst, with a preview area that plays the burst
/// where you click. Opened from a row on <see cref="DesktopFxPage"/>.
/// </summary>
public sealed partial class ClickBurstPage : Page
{
    private const int    SparkCount        = 8;
    private const double SparkLength       = 7;
    private const double SparkThickness    = 2;
    private const double RingThickness     = 3;
    private const double RingStartScale    = 0.15;

    private bool _loaded;
    private Color _color = Color.FromArgb(255, 0x00, 0x78, 0xD4);

    public ClickBurstPage()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private DesktopIconGlow? DesktopGlow => (Application.Current as App)?.MainWindow?.DesktopIconGlow;

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (DesktopFxSettings.GetColor() is var (r, g, b))
            _color = Color.FromArgb(255, r, g, b);

        ClickToggle.IsOn = DesktopFxSettings.GetBool(DesktopFxSettings.ClickEnabledKey, true);
        SelectByTag(ClickStyleBox, DesktopFxSettings.GetEnum(DesktopFxSettings.ClickStyleKey,
            DesktopIconGlow.ClickBurstStyle.RingAndSparks).ToString());
        ClickSizeSlider.Value     = DesktopFxSettings.GetInt(DesktopFxSettings.ClickSizeKey,
            DesktopFxSettings.DefaultClickSize, 20, 120);
        ClickDurationSlider.Value = DesktopFxSettings.GetInt(DesktopFxSettings.ClickDurationKey,
            DesktopFxSettings.DefaultClickDurationMs, 200, 1200);
        ClickDesktopOnlyToggle.IsOn = DesktopFxSettings.GetBool(DesktopFxSettings.ClickDesktopOnlyKey, true);

        _loaded = true;
        PlayPreviewAtCenter();
    }

    private void Back_Click(object sender, RoutedEventArgs e)
        => (Application.Current as App)?.MainWindow?.NavigateTo("desktopfx");

    private void ChangeColor_Click(object sender, RoutedEventArgs e)
        => (Application.Current as App)?.MainWindow?.NavigateTo("fx-glow");

    // ── Preview ────────────────────────────────────────────────────────────

    private void PreviewCanvas_PointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (!ClickToggle.IsOn) return;
        var p = e.GetCurrentPoint(PreviewCanvas).Position;
        PlayPreview(p.X, p.Y);
    }

    private void PlayPreviewAtCenter()
    {
        if (ClickToggle.IsOn) PlayPreview(PreviewCanvas.Width / 2, PreviewCanvas.Height / 2);
    }

    private void PlayPreview(double x, double y)
    {
        var style    = Enum.TryParse(SelectedTag(ClickStyleBox), out DesktopIconGlow.ClickBurstStyle s)
            ? s : DesktopIconGlow.ClickBurstStyle.RingAndSparks;
        double size  = ClickSizeSlider.Value;
        var duration = new Duration(TimeSpan.FromMilliseconds(ClickDurationSlider.Value));
        var ease     = new CubicEase { EasingMode = EasingMode.EaseOut };
        var board    = new Storyboard();
        var parts    = new List<UIElement>();

        if (style is DesktopIconGlow.ClickBurstStyle.Ring or DesktopIconGlow.ClickBurstStyle.RingAndSparks)
        {
            var scale = new CompositeTransform { ScaleX = RingStartScale, ScaleY = RingStartScale };
            var ring  = new Ellipse
            {
                Width = size, Height = size,
                Stroke = new SolidColorBrush(_color),
                StrokeThickness = RingThickness,
                RenderTransformOrigin = new Windows.Foundation.Point(0.5, 0.5),
                RenderTransform = scale,
                IsHitTestVisible = false,
            };
            Canvas.SetLeft(ring, x - size / 2);
            Canvas.SetTop(ring, y - size / 2);
            parts.Add(ring);
            board.Children.Add(Animate(scale, "ScaleX", RingStartScale, 1, duration, ease));
            board.Children.Add(Animate(scale, "ScaleY", RingStartScale, 1, duration, ease));
            board.Children.Add(Animate(ring, "Opacity", 1, 0, duration, null));
        }

        if (style is DesktopIconGlow.ClickBurstStyle.Sparks or DesktopIconGlow.ClickBurstStyle.RingAndSparks)
        {
            for (int i = 0; i < SparkCount; i++)
            {
                double angle = i * 360.0 / SparkCount;
                double rad   = angle * Math.PI / 180;
                var move  = new CompositeTransform { Rotation = angle };
                var spark = new Rectangle
                {
                    Width = SparkLength, Height = SparkThickness,
                    RadiusX = 1, RadiusY = 1,
                    Fill = new SolidColorBrush(_color),
                    RenderTransformOrigin = new Windows.Foundation.Point(0.5, 0.5),
                    RenderTransform = move,
                    IsHitTestVisible = false,
                };
                Canvas.SetLeft(spark, x - SparkLength / 2);
                Canvas.SetTop(spark, y - SparkThickness / 2);
                parts.Add(spark);
                board.Children.Add(Animate(move, "TranslateX", 0, Math.Cos(rad) * size / 2, duration, ease));
                board.Children.Add(Animate(move, "TranslateY", 0, Math.Sin(rad) * size / 2, duration, ease));
                board.Children.Add(Animate(spark, "Opacity", 1, 0, duration, null));
            }
        }

        foreach (var part in parts) PreviewCanvas.Children.Add(part);
        board.Completed += (_, _) =>
        {
            foreach (var part in parts) PreviewCanvas.Children.Remove(part);
        };
        board.Begin();
    }

    private static DoubleAnimation Animate(DependencyObject target, string property,
        double from, double to, Duration duration, EasingFunctionBase? ease)
    {
        var anim = new DoubleAnimation { From = from, To = to, Duration = duration, EasingFunction = ease };
        Storyboard.SetTarget(anim, target);
        Storyboard.SetTargetProperty(anim, property);
        return anim;
    }

    private void PreviewBurst_Click(object sender, RoutedEventArgs e)
        => DesktopGlow?.PreviewBurstAtCursor();

    // ── Starting looks ─────────────────────────────────────────────────────

    private void Look_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: string look }) return;
        var (style, size, ms) = look switch
        {
            "Tap"     => (DesktopIconGlow.ClickBurstStyle.Ring,          32, 300),
            "Splash"  => (DesktopIconGlow.ClickBurstStyle.RingAndSparks, 100, 700),
            "Sparkle" => (DesktopIconGlow.ClickBurstStyle.Sparks,        64, 550),
            _         => (DesktopIconGlow.ClickBurstStyle.RingAndSparks,
                          DesktopFxSettings.DefaultClickSize, DesktopFxSettings.DefaultClickDurationMs),
        };
        // Each assignment goes through the control's handler, which saves and applies it.
        SelectByTag(ClickStyleBox, style.ToString());
        ClickSizeSlider.Value     = size;
        ClickDurationSlider.Value = ms;
        PlayPreviewAtCenter();
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

    // ── Settings ───────────────────────────────────────────────────────────

    private void ClickToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (!_loaded || ClickToggle is null) return;
        bool on = ClickToggle.IsOn;
        if (DesktopGlow is { } dg) dg.ClickBurstEnabled = on;
        DesktopFxSettings.SetBool(DesktopFxSettings.ClickEnabledKey, on);
    }

    private void ClickStyleBox_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!_loaded || ClickStyleBox is null) return;
        if (!Enum.TryParse(SelectedTag(ClickStyleBox), out DesktopIconGlow.ClickBurstStyle style)) return;
        if (DesktopGlow is { } dg) dg.BurstStyle = style;
        AppSettings.Set(DesktopFxSettings.ClickStyleKey, style.ToString());
    }

    private void ClickSizeSlider_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (!_loaded || ClickSizeSlider is null) return;
        int size = (int)ClickSizeSlider.Value;
        if (DesktopGlow is { } dg) dg.ClickBurstSize = size;
        AppSettings.Set(DesktopFxSettings.ClickSizeKey, size.ToString());
    }

    private void ClickDurationSlider_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (!_loaded || ClickDurationSlider is null) return;
        int ms = (int)ClickDurationSlider.Value;
        if (DesktopGlow is { } dg) dg.ClickBurstDurationMs = ms;
        AppSettings.Set(DesktopFxSettings.ClickDurationKey, ms.ToString());
    }

    private void ClickDesktopOnlyToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (!_loaded || ClickDesktopOnlyToggle is null) return;
        bool desktopOnly = ClickDesktopOnlyToggle.IsOn;
        if (DesktopGlow is { } dg) dg.ClickBurstDesktopOnly = desktopOnly;
        DesktopFxSettings.SetBool(DesktopFxSettings.ClickDesktopOnlyKey, desktopOnly);
    }
}
