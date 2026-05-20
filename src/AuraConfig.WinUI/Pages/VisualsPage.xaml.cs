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
        BuildVisualizerBars();
        ServiceManager.Instance.AudioBandsReceived += OnAudioBandsReceived;
        ServiceManager.Instance.StartAudioStream();
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

    // ── IPC helper ─────────────────────────────────────────────────────────

    private Task PushThemeAsync() =>
        ServiceManager.Instance.PushThemeAsync(_theme);
}
