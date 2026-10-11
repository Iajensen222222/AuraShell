using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace AuraConfig.Services;

/// <summary>
/// Applies the app theme and updates the plain-resource brushes in App.xaml in-place.
/// Because App.xaml cannot use ThemeDictionaries (crashes WinUI 3 unpackaged), all color
/// brushes are declared with dark-mode values and this service swaps their Color property
/// whenever the user changes theme.  Updating Color on an existing SolidColorBrush
/// propagates to every bound element immediately without requiring re-navigation.
/// </summary>
internal static class ThemeService
{
    public static void Apply(string themeName, FrameworkElement? root)
    {
        var theme = themeName switch
        {
            "Light"  => ElementTheme.Light,
            "System" => ElementTheme.Default,
            _        => ElementTheme.Dark,
        };

        if (root is not null)
            root.RequestedTheme = theme;

        bool isDark = theme switch
        {
            ElementTheme.Light   => false,
            ElementTheme.Default => IsSystemDark(),
            _                    => true,
        };

        UpdateBrushColors(isDark);
    }

    // ── Brush color tables ─────────────────────────────────────────────────

    private static void UpdateBrushColors(bool isDark)
    {
        var res = Application.Current.Resources;

        // TextBox background states — WinUI 3 unpackaged resolves these from the system
        // (light) theme even when RequestedTheme=Dark is set on a parent; override here.
        SetBrush(res, "TextControlBackground",
            isDark ? Color.FromArgb(0x0F, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0x0D, 0x00, 0x00, 0x00));
        SetBrush(res, "TextControlBackgroundPointerOver",
            isDark ? Color.FromArgb(0x15, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0x12, 0x00, 0x00, 0x00));
        SetBrush(res, "TextControlBackgroundFocused",
            isDark ? Color.FromArgb(0xFF, 0x1C, 0x1C, 0x1C)
                   : Color.FromArgb(0xFF, 0xFF, 0xFF, 0xFF));

        // TextBox foreground (text color) states
        SetBrush(res, "TextControlForeground",
            isDark ? Color.FromArgb(0xFF, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0xFF, 0x1A, 0x1A, 0x1A));
        SetBrush(res, "TextControlForegroundPointerOver",
            isDark ? Color.FromArgb(0xFF, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0xFF, 0x1A, 0x1A, 0x1A));
        SetBrush(res, "TextControlForegroundFocused",
            isDark ? Color.FromArgb(0xFF, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0xFF, 0x1A, 0x1A, 0x1A));

        // Placeholder text (slightly dimmer than the input text)
        SetBrush(res, "TextControlPlaceholderForeground",
            isDark ? Color.FromArgb(0x72, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0x72, 0x00, 0x00, 0x00));
        SetBrush(res, "TextControlPlaceholderForegroundPointerOver",
            isDark ? Color.FromArgb(0x87, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0x87, 0x00, 0x00, 0x00));
        SetBrush(res, "TextControlPlaceholderForegroundFocused",
            isDark ? Color.FromArgb(0x72, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0x72, 0x00, 0x00, 0x00));

        // Secondary body/caption text
        SetBrush(res, "TextFillColorSecondaryBrush",
            isDark ? Color.FromArgb(0xC5, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0x9B, 0x00, 0x00, 0x00));

        // Card backgrounds and borders
        SetBrush(res, "CardBackgroundFillColorDefaultBrush",
            isDark ? Color.FromArgb(0x0D, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0xB3, 0xFF, 0xFF, 0xFF));

        SetBrush(res, "CardStrokeColorDefaultBrush",
            isDark ? Color.FromArgb(0x19, 0x00, 0x00, 0x00)
                   : Color.FromArgb(0x0F, 0x00, 0x00, 0x00));

        // Accent section headers — darker blue in light mode for readability
        SetBrush(res, "AccentTextFillColorPrimaryBrush",
            isDark ? Color.FromArgb(0xFF, 0x60, 0xCD, 0xFF)
                   : Color.FromArgb(0xFF, 0x00, 0x67, 0xC0));

        // Status / semantic fills
        SetBrush(res, "SystemFillColorCautionBrush",
            isDark ? Color.FromArgb(0xFF, 0xFC, 0xE1, 0x00)
                   : Color.FromArgb(0xFF, 0x9D, 0x5D, 0x00));

        SetBrush(res, "SystemFillColorSuccessBrush",
            isDark ? Color.FromArgb(0xFF, 0x6C, 0xCB, 0x5F)
                   : Color.FromArgb(0xFF, 0x0F, 0x7B, 0x0F));

        SetBrush(res, "SystemFillColorSuccessBackgroundBrush",
            isDark ? Color.FromArgb(0xFF, 0x39, 0x3D, 0x1B)
                   : Color.FromArgb(0xFF, 0xDF, 0xF6, 0xDD));

        SetBrush(res, "DividerStrokeColorDefaultBrush",
            isDark ? Color.FromArgb(0x18, 0xFF, 0xFF, 0xFF)
                   : Color.FromArgb(0x14, 0x00, 0x00, 0x00));

        SetBrush(res, "SystemFillColorCautionBackgroundBrush",
            isDark ? Color.FromArgb(0x30, 0xFC, 0xE1, 0x00)
                   : Color.FromArgb(0xFF, 0xFF, 0xF4, 0xCE));
    }

    // ── Helpers ────────────────────────────────────────────────────────────

    private static void SetBrush(ResourceDictionary res, string key, Color color)
    {
        if (res.ContainsKey(key) && res[key] is SolidColorBrush brush)
            brush.Color = color;
    }

    private static bool IsSystemDark()
    {
        try
        {
            using var key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(
                @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize");
            return key?.GetValue("AppsUseLightTheme") is int i && i == 0;
        }
        catch { return true; }
    }
}
