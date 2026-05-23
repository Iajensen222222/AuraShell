using AuraConfig.Models;
using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.UI;

namespace AuraConfig.Pages;

public sealed partial class VisualsPage : Page
{
    private const int BandCount = 128;
    private const double BarMaxHeight = 76.0;

    // Local working copy of the current theme config
    private readonly ThemeConfig _theme = new();
    private readonly Rectangle[] _bars = new Rectangle[BandCount];

    public VisualsPage()
    {
        InitializeComponent();
        Loaded   += OnLoaded;
        Unloaded += OnUnloaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        Logger.Info("VisualsPage", "Loaded");
        BuildVisualizerBars();
        ServiceManager.Instance.AudioBandsReceived += OnAudioBandsReceived;
        ServiceManager.Instance.StartAudioStream();

        // Restore last-applied theme into the local working copy + hex input
        // so navigating back to Visuals shows the user's pick, not a default.
        var last = ServiceManager.Instance.LastAppliedTheme;
        if (last is not null)
        {
            _theme.Name              = last.Name;
            _theme.AccentColor       = last.AccentColor;
            _theme.AnimSpeedPct      = last.AnimSpeedPct;
            _theme.ShowOnHover       = last.ShowOnHover;
            _theme.GlowEnabled       = last.GlowEnabled;
            HexInput.Text = $"#{last.AccentColor.R:X2}{last.AccentColor.G:X2}{last.AccentColor.B:X2}";
            SpeedSlider.Value = last.AnimSpeedPct;
            HoverToggle.IsOn  = last.ShowOnHover;
            GlowToggle.IsOn   = last.GlowEnabled;
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        ServiceManager.Instance.AudioBandsReceived -= OnAudioBandsReceived;
        ServiceManager.Instance.StopAudioStream();
    }

    private void BuildVisualizerBars()
    {
        VisualizerCanvas.Children.Clear();
        double canvasW = VisualizerCanvas.ActualWidth > 0
            ? VisualizerCanvas.ActualWidth
            : 600.0;

        double barW   = (canvasW - (BandCount - 1)) / BandCount;
        if (barW < 1.0) barW = 1.0;

        for (int i = 0; i < BandCount; i++)
        {
            var bar = new Rectangle
            {
                Width  = barW,
                Height = 2.0,
                Fill   = new SolidColorBrush(Color.FromArgb(0xFF, 0xFF, 0x8C, 0x00)),
                RadiusX = 1, RadiusY = 1,
            };
            Canvas.SetLeft(bar, i * (barW + 1));
            Canvas.SetTop(bar, BarMaxHeight - 2.0);
            VisualizerCanvas.Children.Add(bar);
            _bars[i] = bar;
        }
    }

    private void OnAudioBandsReceived(object? sender, AudioBandsPayload payload)
    {
        DispatcherQueue.TryEnqueue(() =>
        {
            if (!payload.AudioPresent || payload.Bands is null) return;

            // Rebuild bars if canvas was resized (first real layout pass).
            if (_bars[0] is null || _bars[0].Parent is null)
                BuildVisualizerBars();

            for (int i = 0; i < BandCount && i < payload.Bands.Length; i++)
            {
                double magnitude = Math.Clamp((double)payload.Bands[i], 0.0, 1.0);
                double h = Math.Max(2.0, magnitude * BarMaxHeight);
                _bars[i].Height = h;
                Canvas.SetTop(_bars[i], BarMaxHeight - h);
            }
        });
    }

    // ── Colour ─────────────────────────────────────────────────────────────

    private void HexInput_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == Windows.System.VirtualKey.Enter)
            ApplyHex_Click(sender, null!);
    }

    private async void ApplyHex_Click(object sender, RoutedEventArgs e)
    {
        var raw = HexInput.Text.Trim().TrimStart('#');
        if (raw.Length != 6) return;

        try
        {
            byte r = Convert.ToByte(raw[0..2], 16);
            byte g = Convert.ToByte(raw[2..4], 16);
            byte b = Convert.ToByte(raw[4..6], 16);
            _theme.AccentColor = Windows.UI.Color.FromArgb(255, r, g, b);
            _theme.Name = $"Custom #{raw.ToUpperInvariant()}";
            await PushThemeAsync();
        }
        catch
        {
            // invalid hex — silently ignore
        }
    }

    // ── Animation ──────────────────────────────────────────────────────────

    private async void SpeedSlider_ValueChanged(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        _theme.AnimSpeedPct = (int)e.NewValue;
        await PushThemeAsync();
    }

    // ── Idle breathing ─────────────────────────────────────────────────────

    private GlowAnimator? Animator =>
        (Application.Current as App)?.MainWindow?.GlowAnimator;

    private void BreatheMode_Changed(object sender, SelectionChangedEventArgs e)
    {
        // SelectionChanged fires during XAML parse before BreatheControls exists.
        if (BreatheControls is null || BreatheModeBox is null) return;

        var tag = (BreatheModeBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? "0";
        Logger.Info("VisualsPage", $"Breathe mode → {tag} (0=off, 1=sync, 2=wave)");
        BreatheControls.Visibility = tag != "0" ? Visibility.Visible : Visibility.Collapsed;
        if (tag == "0") { Animator?.StopBreathing(); return; }
        ApplyBreatheSettings(tag == "2");
    }

    private void BreatheSpeed_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        // Slider ValueChanged fires during XAML parse before label exists.
        if (BreatheSpeedLabel is null || BreatheSpeedSlider is null) return;

        BreatheSpeedLabel.Text = $"{BreatheSpeedSlider.Value:F1} s per cycle";
        ApplyBreatheSettings(BreatheModeBox?.SelectedIndex == 2);
    }

    private void BreatheBrightness_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (BreatheBrightnessSlider is null) return;
        ApplyBreatheSettings(BreatheModeBox?.SelectedIndex == 2);
    }

    private void ApplyBreatheSettings(bool waveMode)
    {
        if (BreatheModeBox is null || BreatheSpeedSlider is null || BreatheBrightnessSlider is null) return;
        if (BreatheModeBox.SelectedIndex == 0) return;
        double speed    = BreatheSpeedSlider.Value;
        float  maxBoost = (float)BreatheBrightnessSlider.Value / 100f;
        Animator?.StartBreathing(speed, maxBoost, waveMode);
    }

    // ── Options ────────────────────────────────────────────────────────────

    private async void HoverToggle_Toggled(object sender, RoutedEventArgs e)
    {
        _theme.ShowOnHover = HoverToggle.IsOn;
        await PushThemeAsync();
    }

    private async void GlowToggle_Toggled(object sender, RoutedEventArgs e)
    {
        _theme.GlowEnabled = GlowToggle.IsOn;
        await PushThemeAsync();
    }

    // ── Save as theme shortcut ─────────────────────────────────────────────

    private async void SaveAsTheme_Click(object sender, RoutedEventArgs e)
    {
        var input = new TextBox { PlaceholderText = "Theme name", Text = _theme.Name };
        var dlg = new ContentDialog
        {
            XamlRoot          = XamlRoot,
            Title             = "Save as theme",
            Content           = input,
            PrimaryButtonText = "Save",
            CloseButtonText   = "Cancel",
        };

        if (await dlg.ShowAsync() != ContentDialogResult.Primary) return;

        var name = input.Text.Trim();
        if (string.IsNullOrEmpty(name)) name = _theme.Name;

        ThemeStore.Instance.CreateFromCurrent(_theme, name);
    }

    // ── IPC helper ─────────────────────────────────────────────────────────

    private Task PushThemeAsync() =>
        ServiceManager.Instance.PushThemeAsync(_theme);
}
