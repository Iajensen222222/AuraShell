using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace AuraConfig.Pages;

/// <summary>
/// Every option of the cursor click burst, with a preview area that plays the burst
/// where you click. Opened from a row on <see cref="DesktopFxPage"/>.
/// </summary>
public sealed partial class ClickBurstPage : Page
{
    private const double PreviewFrameMs = 16;
    private const float  BurstSpinStep  = 0.37f;   // same spark angle step between bursts as the engine

    private bool _loaded;
    private Color _color = Color.FromArgb(255, 0x00, 0x78, 0xD4);
    private readonly List<PreviewBurst> _previewBursts = new();
    private readonly DispatcherTimer _previewTimer = new() { Interval = TimeSpan.FromMilliseconds(PreviewFrameMs) };
    private int _burstCounter;

    public ClickBurstPage()
    {
        InitializeComponent();
        PreviewCanvas.Clip = new RectangleGeometry { Rect = new Windows.Foundation.Rect(0, 0, PreviewCanvas.Width, PreviewCanvas.Height) };
        _previewTimer.Tick += (_, _) => DrawPreviewBursts();
        Loaded   += OnLoaded;
        Unloaded += (_, _) => _previewTimer.Stop();
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
    //
    // Bursts in the preview are drawn by the same code as the real ones (DesktopFxPainter).

    private sealed class PreviewBurst
    {
        public required Image    Image;
        public required FxBitmap Bitmap;
        public required long     StartMs;
        public required float    Spin;
    }

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
        int side = DesktopFxPainter.BurstBitmapSize((int)ClickSizeSlider.Value);
        var burst = new PreviewBurst
        {
            Bitmap  = new FxBitmap(side),
            Image   = new Image { Width = side, Height = side, Stretch = Stretch.Fill, IsHitTestVisible = false },
            StartMs = Environment.TickCount64,
            Spin    = (++_burstCounter * BurstSpinStep) % 1f,
        };
        burst.Image.Source = burst.Bitmap.Bitmap;
        Canvas.SetLeft(burst.Image, x - side / 2.0);
        Canvas.SetTop(burst.Image, y - side / 2.0);
        PreviewCanvas.Children.Add(burst.Image);
        _previewBursts.Add(burst);
        DrawPreviewBursts();
        _previewTimer.Start();
    }

    private unsafe void DrawPreviewBursts()
    {
        var style = Enum.TryParse(SelectedTag(ClickStyleBox), out DesktopIconGlow.ClickBurstStyle s)
            ? s : DesktopIconGlow.ClickBurstStyle.RingAndSparks;
        double duration = ClickDurationSlider.Value;
        long now = Environment.TickCount64;

        for (int i = _previewBursts.Count - 1; i >= 0; i--)
        {
            var b = _previewBursts[i];
            float p = (float)((now - b.StartMs) / duration);
            if (p >= 1f)
            {
                PreviewCanvas.Children.Remove(b.Image);
                _previewBursts.RemoveAt(i);
                continue;
            }
            fixed (uint* buf = b.Bitmap.Pixels)
                DesktopFxPainter.PaintBurst(buf, b.Bitmap.Size, style,
                    b.Bitmap.Size / 2 - DesktopFxPainter.BurstPadding, _color.R, _color.G, _color.B, p, b.Spin);
            b.Bitmap.Publish();
        }
        if (_previewBursts.Count == 0) _previewTimer.Stop();
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
            "Drop"    => (DesktopIconGlow.ClickBurstStyle.Ripple,        72, 900),
            "Party"   => (DesktopIconGlow.ClickBurstStyle.Confetti,      80, 1000),
            "Firework"=> (DesktopIconGlow.ClickBurstStyle.Firework,      96, 1100),
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
