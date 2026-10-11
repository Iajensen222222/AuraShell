using AuraConfig.Services;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.UI;

namespace AuraConfig.Pages;

/// <summary>
/// Wallpaper, Windows accent color and light/dark mode. All system changes go through
/// <see cref="SystemLookService"/>, which backs up the user's original look first.
/// </summary>
public sealed partial class DesktopLookPage : Page
{
    private const int PreviewDecodeWidth = 480;
    private const int ThumbDecodeWidth   = 200;
    private const int ThumbWidth         = 96;
    private const int ThumbHeight        = 54;
    private const int PresetSwatchSize   = 24;
    private const int StatusMs           = 3000;

    private static readonly Color DefaultThemeColor = Color.FromArgb(255, 0x00, 0x78, 0xD4);

    // Windows' own accent palette (Settings → Personalization → Colors), first two rows.
    private static readonly uint[] AccentPresets =
    [
        0xFFB900, 0xFF8C00, 0xE74856, 0xE81123, 0xEA005E, 0xC30052,
        0x0078D4, 0x0063B1, 0x8E8CD8, 0x0099BC, 0x00CC6A, 0x107C10,
    ];

    private static readonly string[] ImageTypes = [".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tif", ".tiff", ".jfif", ".webp"];

    private bool _loading = true;

    public DesktopLookPage()
    {
        InitializeComponent();
        Loaded += OnLoaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        _loading = true;
        BuildAccentPresets();
        RefreshWallpaper();
        RefreshMode();
        RefreshAccent();
        RefreshRestore();
        _loading = false;
    }

    // ── Wallpaper ──────────────────────────────────────────────────────────

    private void RefreshWallpaper()
    {
        var current = SystemLookService.GetCurrentWallpaper();
        WallpaperName.Text = current is null ? "None (solid color)" : Path.GetFileName(current);
        ToolTipService.SetToolTip(WallpaperName, current);
        _ = LoadImageAsync(WallpaperPreview, current, PreviewDecodeWidth);

        var fit = SystemLookService.GetCurrentFit();
        foreach (ComboBoxItem item in FitCombo.Items)
            if ((string)item.Tag == fit.ToString()) FitCombo.SelectedItem = item;

        RebuildRecent();
    }

    private void RebuildRecent()
    {
        RecentPanel.Children.Clear();
        var recent = SystemLookService.GetRecentWallpapers();
        RecentSection.Visibility = recent.Count > 0 ? Visibility.Visible : Visibility.Collapsed;

        foreach (var path in recent)
        {
            var img = new Image { Stretch = Stretch.UniformToFill };
            _ = LoadImageAsync(img, path, ThumbDecodeWidth);
            var btn = new Button
            {
                Padding      = new Thickness(0),
                Width        = ThumbWidth,
                Height       = ThumbHeight,
                CornerRadius = new CornerRadius(4),
                Content      = img,
                Tag          = path,
            };
            ToolTipService.SetToolTip(btn, Path.GetFileName(path));
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(btn, $"Use {Path.GetFileName(path)}");
            btn.Click += (_, _) => ApplyWallpaper(path);

            var remove = new MenuFlyoutItem { Text = "Remove from recent", Icon = new FontIcon { Glyph = "" } };
            remove.Click += (_, _) =>
            {
                SystemLookService.RemoveRecentWallpaper(path);
                RebuildRecent();
            };
            var menu = new MenuFlyout();
            menu.Items.Add(remove);
            btn.ContextFlyout = menu;

            RecentPanel.Children.Add(btn);
        }
    }

    private static async Task LoadImageAsync(Image target, string? path, int decodeWidth)
    {
        target.Source = null;
        if (path is null || !File.Exists(path)) return;
        try
        {
            var file = await Windows.Storage.StorageFile.GetFileFromPathAsync(path);
            using var stream = await file.OpenReadAsync();
            var bmp = new BitmapImage { DecodePixelWidth = decodeWidth };
            await bmp.SetSourceAsync(stream);
            target.Source = bmp;
        }
        catch (Exception ex)
        {
            Logger.Warn("DesktopLookPage", $"Could not load preview for {path}: {ex.Message}");
        }
    }

    private async void Browse_Click(object sender, RoutedEventArgs e)
    {
        var win = (Application.Current as App)?.MainWindow;
        if (win is null) return;
        var picker = new Windows.Storage.Pickers.FileOpenPicker
        {
            SuggestedStartLocation = Windows.Storage.Pickers.PickerLocationId.PicturesLibrary,
            ViewMode               = Windows.Storage.Pickers.PickerViewMode.Thumbnail,
        };
        foreach (var ext in ImageTypes) picker.FileTypeFilter.Add(ext);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, WinRT.Interop.WindowNative.GetWindowHandle(win));

        var file = await picker.PickSingleFileAsync();
        if (file is not null) ApplyWallpaper(file.Path);
    }

    private WallpaperFit SelectedFit =>
        FitCombo.SelectedItem is ComboBoxItem { Tag: string tag } && Enum.TryParse(tag, out WallpaperFit f)
            ? f : WallpaperFit.Fill;

    private void ApplyWallpaper(string path)
    {
        if (SystemLookService.SetWallpaper(path, SelectedFit))
            ShowStatus($"Wallpaper set to {Path.GetFileName(path)}");
        else
            ShowStatus("Windows couldn't use that picture as the wallpaper.", isError: true);
        _loading = true;
        RefreshWallpaper();
        RefreshRestore();
        _loading = false;
    }

    private void FitCombo_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_loading || FitCombo is null) return;
        var current = SystemLookService.GetCurrentWallpaper();
        if (current is null) return;
        if (SystemLookService.SetWallpaper(current, SelectedFit))
        {
            ShowStatus($"Wallpaper fit: {SelectedFit}");
            RefreshRestore();
        }
    }

    // ── Light / dark ───────────────────────────────────────────────────────

    private void RefreshMode()
    {
        bool sys = SystemLookService.IsSystemLight, apps = SystemLookService.IsAppsLight;
        (sys == apps ? (sys ? ModeLight : ModeDark) : ModeCustom).IsChecked = true;
        CustomModePanel.Visibility = sys == apps ? Visibility.Collapsed : Visibility.Visible;
        (sys  ? SystemLight : SystemDark).IsChecked = true;
        (apps ? AppsLight   : AppsDark).IsChecked   = true;
    }

    private void ModeRadio_Checked(object sender, RoutedEventArgs e)
    {
        if (_loading || CustomModePanel is null || sender is not RadioButton { Tag: string mode }) return;
        CustomModePanel.Visibility = mode == "Custom" ? Visibility.Visible : Visibility.Collapsed;
        if (mode == "Custom") return;   // the two rows below decide

        bool light = mode == "Light";
        SetMode(light, light);
        _loading = true;
        (light ? SystemLight : SystemDark).IsChecked = true;
        (light ? AppsLight   : AppsDark).IsChecked   = true;
        _loading = false;
    }

    private void CustomMode_Checked(object sender, RoutedEventArgs e)
    {
        if (_loading || SystemLight is null || AppsLight is null) return;
        SetMode(SystemLight.IsChecked == true, AppsLight.IsChecked == true);
    }

    private void SetMode(bool systemLight, bool appsLight)
    {
        SystemLookService.SetMode(systemLight, appsLight);
        // If the AuraShell window follows Windows, repaint it now.
        if (AppSettings.Get("AppTheme") == "System")
            ThemeService.Apply("System", (Application.Current as App)?.MainWindow?.Content as FrameworkElement);
        ShowStatus(systemLight == appsLight
            ? $"Windows is now in {(systemLight ? "light" : "dark")} mode"
            : $"Windows {(systemLight ? "light" : "dark")}, apps {(appsLight ? "light" : "dark")}");
        RefreshRestore();
    }

    // ── Accent ─────────────────────────────────────────────────────────────

    private void BuildAccentPresets()
    {
        AccentPresetPanel.Children.Clear();
        foreach (var rgb in AccentPresets)
        {
            var c = Color.FromArgb(255, (byte)(rgb >> 16), (byte)(rgb >> 8), (byte)rgb);
            var hex = $"#{rgb:X6}";
            var btn = new Button
            {
                Padding = new Thickness(2),
                Content = new Border
                {
                    Width        = PresetSwatchSize,
                    Height       = PresetSwatchSize,
                    CornerRadius = new CornerRadius(3),
                    Background   = new SolidColorBrush(c),
                },
            };
            ToolTipService.SetToolTip(btn, hex);
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(btn, $"Accent {hex}");
            btn.Click += (_, _) => ApplyAccent(c.R, c.G, c.B);
            AccentPresetPanel.Children.Add(btn);
        }
    }

    private void RefreshAccent()
    {
        var (r, g, b) = SystemLookService.GetAccent();
        AccentSwatch.Background = new SolidColorBrush(Color.FromArgb(255, r, g, b));
        AccentHexInput.Text     = $"#{r:X2}{g:X2}{b:X2}";

        var tc = ThemeColor;
        ThemeColorSwatch.Background = new SolidColorBrush(tc);
        var name = ServiceManager.Instance.LastAppliedTheme?.Name;
        ThemeColorDescription.Text = name is null
            ? $"Use your glow color #{tc.R:X2}{tc.G:X2}{tc.B:X2} as the Windows accent."
            : $"Use the glow color of “{name}” (#{tc.R:X2}{tc.G:X2}{tc.B:X2}) as the Windows accent.";

        TaskbarAccentToggle.IsOn  = SystemLookService.IsAccentOnTaskbar;
        TitleBarAccentToggle.IsOn = SystemLookService.IsAccentOnTitleBars;
    }

    private static Color ThemeColor =>
        ServiceManager.Instance.LastAppliedTheme?.AccentColor ?? DefaultThemeColor;

    private void ApplyAccent(byte r, byte g, byte b)
    {
        SystemLookService.SetAccent(r, g, b);
        ShowStatus($"Accent color set to #{r:X2}{g:X2}{b:X2}");
        _loading = true;
        RefreshAccent();
        RefreshRestore();
        _loading = false;
    }

    private void AccentSwatch_Click(object sender, RoutedEventArgs e)
    {
        var (r, g, b) = SystemLookService.GetAccent();
        DashboardPage.ShowColorPickerWindow(r, g, b, (nr, ng, nb) =>
            DispatcherQueue.TryEnqueue(() => ApplyAccent(nr, ng, nb)));
    }

    private void AccentHexInput_KeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == Windows.System.VirtualKey.Enter) AccentHexSet_Click(sender, null!);
    }

    private void AccentHexSet_Click(object sender, RoutedEventArgs e)
    {
        if (TryParseHex(AccentHexInput.Text, out var c)) ApplyAccent(c.R, c.G, c.B);
        else ShowStatus("Type a color like #0078D4.", isError: true);
    }

    private void UseThemeColor_Click(object sender, RoutedEventArgs e)
    {
        var c = ThemeColor;
        ApplyAccent(c.R, c.G, c.B);
    }

    private void AccentSurface_Toggled(object sender, RoutedEventArgs e)
    {
        if (_loading || TaskbarAccentToggle is null || TitleBarAccentToggle is null) return;
        SystemLookService.SetAccentSurfaces(TaskbarAccentToggle.IsOn, TitleBarAccentToggle.IsOn);
        RefreshRestore();
    }

    internal static bool TryParseHex(string text, out Color color)
    {
        color = default;
        var raw = text.Trim().TrimStart('#');
        if (raw.Length != 6 || !uint.TryParse(raw, System.Globalization.NumberStyles.HexNumber, null, out var v))
            return false;
        color = Color.FromArgb(255, (byte)(v >> 16), (byte)(v >> 8), (byte)v);
        return true;
    }

    // ── Restore ────────────────────────────────────────────────────────────

    private void RefreshRestore()
    {
        RestoreBtn.IsEnabled = SystemLookService.HasBackup;
        RestoreDescription.Text = SystemLookService.HasBackup
            ? "Put back the wallpaper, accent and modes you had before AuraShell changed them."
            : "Nothing to restore: AuraShell hasn't changed your Windows look.";
    }

    private void Restore_Click(object sender, RoutedEventArgs e)
    {
        if (SystemLookService.RestoreOriginal())
            ShowStatus("Your original wallpaper and colors are back");
        else
            ShowStatus("Couldn't restore your original look. See the app log.", isError: true);

        if (AppSettings.Get("AppTheme") == "System")
            ThemeService.Apply("System", (Application.Current as App)?.MainWindow?.Content as FrameworkElement);
        OnLoaded(this, null!);
    }

    // ── Status ─────────────────────────────────────────────────────────────

    private int _statusVersion;

    private async void ShowStatus(string message, bool isError = false)
    {
        var res = Application.Current.Resources;
        StatusBanner.Background = (Brush)res[isError ? "SystemFillColorCriticalBackgroundBrush" : "SystemFillColorSuccessBackgroundBrush"];
        var fg = (Brush)res[isError ? "SystemFillColorCriticalBrush" : "SystemFillColorSuccessBrush"];
        StatusText.Foreground = fg;
        StatusIcon.Foreground = fg;
        StatusIcon.Glyph      = isError ? "" : "";
        StatusText.Text         = message;
        StatusBanner.Visibility = Visibility.Visible;

        int version = ++_statusVersion;
        await Task.Delay(StatusMs);
        if (version == _statusVersion) StatusBanner.Visibility = Visibility.Collapsed;
    }
}
