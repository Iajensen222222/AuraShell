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
        if (AppSettings.Get("DesktopGlowEnabled") is string en)
            GlowToggle.IsOn = en != "0";

        if (AppSettings.Get("DesktopFxColor") is string hex && hex.Length == 6)
        {
            HexInput.Text = "#" + hex;
            ApplyHexString(hex, updateService: true);
        }

        if (AppSettings.Get("DesktopFxGlowRadius") is string rs
            && int.TryParse(rs, out int r))
            GlowSizeSlider.Value = Math.Clamp(r, 4, 40);

        if (AppSettings.Get("DesktopFxIntensity") is string ins
            && int.TryParse(ins, out int intensity))
            IntensitySlider.Value = Math.Clamp(intensity, 0, 100);

        if (AppSettings.Get("DesktopFxFadeSpeed") is string fs
            && int.TryParse(fs, out int f))
            FadeSpeedSlider.Value = Math.Clamp(f, 20, 300);

        bool labelsVisible = AppSettings.Get("DesktopLabelsVisible") is string lv ? lv != "0" : true;
        ShowLabelsToggle.IsOn = labelsVisible;

        _loaded = true;

        // Push the restored state to the live service now that loading is complete.
        if (DesktopGlow is { } dg)
        {
            dg.Enabled      = GlowToggle.IsOn;
            dg.GlowRadius   = (int)GlowSizeSlider.Value;
            dg.Intensity    = (float)IntensitySlider.Value / 100f;
            dg.FadeSpeed    = (float)FadeSpeedSlider.Value / 100f;
            dg.HideIconLabels = !labelsVisible;
        }
    }

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
}
