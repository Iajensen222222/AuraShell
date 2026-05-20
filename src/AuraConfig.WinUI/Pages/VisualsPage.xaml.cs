using AuraConfig.Models;
using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace AuraConfig.Pages;

public sealed partial class VisualsPage : Page
{
    // Local working copy of the current theme config
    private readonly ThemeConfig _theme = new();

    public VisualsPage() => InitializeComponent();

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
