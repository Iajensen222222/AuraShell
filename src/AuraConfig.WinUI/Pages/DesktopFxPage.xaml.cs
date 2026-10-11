using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace AuraConfig.Pages;

public sealed partial class DesktopFxPage : Page
{
    // Suppresses event handlers during InitializeComponent and OnLoaded so that
    // XAML-default property assignments don't overwrite saved AppSettings values.
    private bool _loaded = false;

    public DesktopFxPage()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private DesktopIconGlow? DesktopGlow => (Application.Current as App)?.MainWindow?.DesktopIconGlow;

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        // Restore UI controls from AppSettings — handlers are suppressed (_loaded=false)
        // so these assignments don't overwrite the saved values or trigger side effects.
        GlowToggle.IsOn = DesktopFxSettings.GetBool(DesktopFxSettings.GlowEnabledKey, true);

        if (AppSettings.Get(DesktopFxSettings.ColorKey) is string hex && hex.Length == 6)
        {
            HexInput.Text = "#" + hex;
            ApplyHexString(hex, updateService: false);
        }

        GlowSizeSlider.Value  = DesktopFxSettings.GetInt(DesktopFxSettings.GlowRadiusKey,
            DesktopFxSettings.DefaultGlowRadius, 4, 40);
        IntensitySlider.Value = DesktopFxSettings.GetInt(DesktopFxSettings.IntensityKey,
            DesktopFxSettings.DefaultIntensityPct, 0, 100);
        FadeSpeedSlider.Value = DesktopFxSettings.GetInt(DesktopFxSettings.FadeSpeedKey,
            DesktopFxSettings.DefaultFadeSpeedPct, 20, 300);
        ShowLabelsToggle.IsOn = DesktopFxSettings.GetBool(DesktopFxSettings.LabelsVisibleKey, true);

        var glowStyle = DesktopFxSettings.GetEnum(DesktopFxSettings.GlowStyleKey,
            DesktopIconGlow.HoverGlowStyle.Solid);
        SelectByTag(GlowStyleBox, glowStyle.ToString());
        CycleControls.Visibility = glowStyle == DesktopIconGlow.HoverGlowStyle.Solid
            ? Visibility.Collapsed : Visibility.Visible;
        CycleSecondsSlider.Value = DesktopFxSettings.GetInt(DesktopFxSettings.CycleSecondsKey,
            DesktopFxSettings.DefaultCycleSeconds, 1, 20);

        ClickToggle.IsOn = DesktopFxSettings.GetBool(DesktopFxSettings.ClickEnabledKey, true);
        SelectByTag(ClickStyleBox, DesktopFxSettings.GetEnum(DesktopFxSettings.ClickStyleKey,
            DesktopIconGlow.ClickBurstStyle.RingAndSparks).ToString());
        ClickSizeSlider.Value     = DesktopFxSettings.GetInt(DesktopFxSettings.ClickSizeKey,
            DesktopFxSettings.DefaultClickSize, 20, 120);
        ClickDurationSlider.Value = DesktopFxSettings.GetInt(DesktopFxSettings.ClickDurationKey,
            DesktopFxSettings.DefaultClickDurationMs, 200, 1200);
        ClickDesktopOnlyToggle.IsOn = DesktopFxSettings.GetBool(DesktopFxSettings.ClickDesktopOnlyKey, true);

        _loaded = true;

        // Push the restored state to the live service now that loading is complete.
        if (DesktopGlow is { } dg) DesktopFxSettings.ApplyTo(dg);
    }

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
        AppSettings.Set("DesktopGlowEnabled", on ? "1" : "0");
    }

    // ── Color ──────────────────────────────────────────────────────────────

    private void HexInput_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == Windows.System.VirtualKey.Enter)
            ApplyHex_Click(sender, null!);
    }

    private void ColorPreview_Click(object sender, RoutedEventArgs e)
    {
        byte r = 0xFF, g = 0x8C, b = 0x00;
        var raw = HexInput.Text.Trim().TrimStart('#');
        if (raw.Length == 6)
        {
            try
            {
                r = Convert.ToByte(raw[0..2], 16);
                g = Convert.ToByte(raw[2..4], 16);
                b = Convert.ToByte(raw[4..6], 16);
            }
            catch { }
        }

        DashboardPage.ShowColorPickerWindow(r, g, b, (nr, ng, nb) =>
        {
            string hex = $"{nr:X2}{ng:X2}{nb:X2}";
            HexInput.Text = "#" + hex;
            ApplyHexString(hex, updateService: true);
            AppSettings.Set("DesktopFxColor", hex);
        });
    }

    private void ApplyHex_Click(object sender, RoutedEventArgs e)
    {
        var raw = HexInput.Text.Trim().TrimStart('#');
        if (raw.Length != 6) return;
        try
        {
            ApplyHexString(raw, updateService: true);
            AppSettings.Set("DesktopFxColor", raw.ToUpperInvariant());
        }
        catch { }
    }

    private void ApplyHexString(string hex6, bool updateService)
    {
        byte r = Convert.ToByte(hex6[0..2], 16);
        byte g = Convert.ToByte(hex6[2..4], 16);
        byte b = Convert.ToByte(hex6[4..6], 16);

        ColorPreview.Background = new SolidColorBrush(Color.FromArgb(255, r, g, b));

        if (updateService && DesktopGlow is { } dg)
            dg.SetColor(r, g, b);
    }

    // ── Glow size ──────────────────────────────────────────────────────────

    private void GlowSizeSlider_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (!_loaded || GlowSizeSlider is null) return;
        int radius = (int)GlowSizeSlider.Value;
        if (DesktopGlow is { } dg) dg.GlowRadius = radius;
        AppSettings.Set("DesktopFxGlowRadius", radius.ToString());
    }

    // ── Brightness ─────────────────────────────────────────────────────────

    private void IntensitySlider_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (!_loaded || IntensitySlider is null) return;
        float intensity = (float)IntensitySlider.Value / 100f;
        if (DesktopGlow is { } dg) dg.Intensity = intensity;
        AppSettings.Set("DesktopFxIntensity", ((int)IntensitySlider.Value).ToString());
    }

    // ── Fade speed ─────────────────────────────────────────────────────────

    private void FadeSpeedSlider_Changed(object sender,
        Microsoft.UI.Xaml.Controls.Primitives.RangeBaseValueChangedEventArgs e)
    {
        if (!_loaded || FadeSpeedSlider is null) return;
        float speed = (float)FadeSpeedSlider.Value / 100f;
        if (DesktopGlow is { } dg) dg.FadeSpeed = speed;
        AppSettings.Set("DesktopFxFadeSpeed", ((int)FadeSpeedSlider.Value).ToString());
    }

    // ── Icon label visibility ──────────────────────────────────────────────
    //
    // Delegates to DesktopIconGlow.HideIconLabels which clips the SysListView32
    // window region to exclude all label rects (LVIR_LABEL), making the desktop
    // wallpaper show through where labels would appear. Works for any wallpaper.

    private static void SetIconLabelsVisible(bool visible)
    {
        var dg = (Application.Current as App)?.MainWindow?.DesktopIconGlow;
        if (dg != null) dg.HideIconLabels = !visible;
    }

    private void ShowLabelsToggle_Toggled(object sender, RoutedEventArgs e)
    {
        if (!_loaded || ShowLabelsToggle is null) return;
        bool visible = ShowLabelsToggle.IsOn;
        SetIconLabelsVisible(visible);
        AppSettings.Set("DesktopLabelsVisible", visible ? "1" : "0");
    }

    // ── Glow style ─────────────────────────────────────────────────────────

    private void GlowStyleBox_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!_loaded || GlowStyleBox is null || CycleControls is null) return;
        if (!Enum.TryParse(SelectedTag(GlowStyleBox), out DesktopIconGlow.HoverGlowStyle style)) return;
        CycleControls.Visibility = style == DesktopIconGlow.HoverGlowStyle.Solid
            ? Visibility.Collapsed : Visibility.Visible;
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

    // ── Click burst ────────────────────────────────────────────────────────

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

    private void PreviewBurst_Click(object sender, RoutedEventArgs e)
        => DesktopGlow?.PreviewBurstAtCursor();
}
