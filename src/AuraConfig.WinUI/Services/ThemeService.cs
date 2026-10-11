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
    /// <summary>Raised after a theme is applied; the argument is true for dark.</summary>
    public static event Action<bool>? ThemeChanged;

    /// <summary>Whether the last applied theme resolved to dark.</summary>
    public static bool IsDark { get; private set; } = true;

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
        IsDark = isDark;
        ThemeChanged?.Invoke(isDark);
    }

    // ── Brush color table ──────────────────────────────────────────────────

    private static Color C(uint argb) => Color.FromArgb(
        (byte)(argb >> 24), (byte)(argb >> 16), (byte)(argb >> 8), (byte)argb);

    // Accent: the app's cyan in dark mode, a deeper blue in light mode for contrast.
    private const uint AccentDark         = 0xFF60CDFF;
    private const uint AccentDarkHover    = 0xE660CDFF;
    private const uint AccentDarkPressed  = 0xCC60CDFF;
    private const uint AccentLight        = 0xFF005FB8;
    private const uint AccentLightHover   = 0xE6005FB8;
    private const uint AccentLightPressed = 0xCC005FB8;

    /// <summary>Key → (dark ARGB, light ARGB) for every brush App.xaml declares.</summary>
    private static readonly (string Key, uint Dark, uint Light)[] BrushTable =
    {
        // TextBox background states — WinUI 3 unpackaged resolves these from the system
        // (light) theme even when RequestedTheme=Dark is set on a parent; override here.
        ("TextControlBackground",                    0x0FFFFFFF, 0xB3FFFFFF),
        ("TextControlBackgroundPointerOver",         0x15FFFFFF, 0x80F9F9F9),
        ("TextControlBackgroundFocused",             0xFF1C1C1C, 0xFFFFFFFF),
        ("TextControlForeground",                    0xFFFFFFFF, 0xFF1A1A1A),
        ("TextControlForegroundPointerOver",         0xFFFFFFFF, 0xFF1A1A1A),
        ("TextControlForegroundFocused",             0xFFFFFFFF, 0xFF1A1A1A),
        ("TextControlPlaceholderForeground",         0x72FFFFFF, 0x72000000),
        ("TextControlPlaceholderForegroundPointerOver", 0x87FFFFFF, 0x87000000),
        ("TextControlPlaceholderForegroundFocused",  0x72FFFFFF, 0x72000000),
        ("TextControlBorderBrush",                   0x24FFFFFF, 0x29000000),
        ("TextControlBorderBrushPointerOver",        0x33FFFFFF, 0x3D000000),
        ("TextControlBorderBrushFocused",            AccentDark, AccentLight),

        // Text, cards, dividers
        ("TextFillColorSecondaryBrush",              0xC5FFFFFF, 0x9B000000),
        ("CardBackgroundFillColorDefaultBrush",      0x0DFFFFFF, 0xB3FFFFFF),
        ("CardStrokeColorDefaultBrush",              0x19000000, 0x14000000),
        ("DividerStrokeColorDefaultBrush",           0x18FFFFFF, 0x14000000),
        ("AccentTextFillColorPrimaryBrush",          AccentDark, 0xFF0067C0),
        ("AccentFillColorDefaultBrush",              AccentDark, AccentLight),

        // Status / semantic fills
        ("SystemFillColorCautionBrush",              0xFFFCE100, 0xFF9D5D00),
        ("SystemFillColorCautionBackgroundBrush",    0x30FCE100, 0xFFFFF4CE),
        ("SystemFillColorSuccessBrush",              0xFF6CCB5F, 0xFF0F7B0F),
        ("SystemFillColorSuccessBackgroundBrush",    0xFF393D1B, 0xFFDFF6DD),
        ("SystemFillColorCriticalBrush",             0xFFFF99A4, 0xFFC42B1C),
        ("SystemFillColorCriticalBackgroundBrush",   0xFF442726, 0xFFFDE7E9),

        // App palette
        ("AppTextPrimaryBrush",                      0xFFFFFFFF, 0xE4000000),
        ("AppAccentFillBrush",                       AccentDark, AccentLight),
        ("AppAccentFillHoverBrush",                  AccentDarkHover, AccentLightHover),
        ("AppAccentFillPressedBrush",                AccentDarkPressed, AccentLightPressed),
        ("AppAccentTextBrush",                       0xFF000000, 0xFFFFFFFF),
        ("AppAccentSubtleBrush",                     0x2660CDFF, 0x1A005FB8),
        ("AppSubtleHoverBrush",                      0x0FFFFFFF, 0x09000000),
        ("AppSubtlePressedBrush",                    0x0AFFFFFF, 0x06000000),
        ("AppNavSelectedBrush",                      0x14FFFFFF, 0x0C000000),
        ("AppDropZoneBrush",                         0x06FFFFFF, 0x80FFFFFF),

        // Built-in control lightweight-styling keys
        ("ButtonBackground",                         0x0FFFFFFF, 0xB3FFFFFF),
        ("ButtonBackgroundPointerOver",              0x15FFFFFF, 0x80F9F9F9),
        ("ButtonBackgroundPressed",                  0x08FFFFFF, 0x4DF9F9F9),
        ("ButtonBackgroundDisabled",                 0x0BFFFFFF, 0x4DF9F9F9),
        ("ButtonForeground",                         0xFFFFFFFF, 0xE4000000),
        ("ButtonForegroundPointerOver",              0xFFFFFFFF, 0xE4000000),
        ("ButtonForegroundPressed",                  0xC5FFFFFF, 0x9E000000),
        ("ButtonForegroundDisabled",                 0x5DFFFFFF, 0x5C000000),
        ("ButtonBorderBrush",                        0x18FFFFFF, 0x29000000),
        ("ButtonBorderBrushPointerOver",             0x18FFFFFF, 0x29000000),
        ("ButtonBorderBrushPressed",                 0x12FFFFFF, 0x14000000),
        ("ButtonBorderBrushDisabled",                0x12FFFFFF, 0x14000000),

        ("ToggleButtonBackground",                   0x0FFFFFFF, 0xB3FFFFFF),
        ("ToggleButtonBackgroundPointerOver",        0x15FFFFFF, 0x80F9F9F9),
        ("ToggleButtonBackgroundPressed",            0x08FFFFFF, 0x4DF9F9F9),
        ("ToggleButtonForeground",                   0xFFFFFFFF, 0xE4000000),
        ("ToggleButtonForegroundPointerOver",        0xFFFFFFFF, 0xE4000000),
        ("ToggleButtonForegroundPressed",            0xC5FFFFFF, 0x9E000000),
        ("ToggleButtonBorderBrush",                  0x18FFFFFF, 0x29000000),
        ("ToggleButtonBorderBrushPointerOver",       0x18FFFFFF, 0x29000000),
        ("ToggleButtonBorderBrushPressed",           0x12FFFFFF, 0x14000000),
        ("ToggleButtonBackgroundChecked",            AccentDark, AccentLight),
        ("ToggleButtonBackgroundCheckedPointerOver", AccentDarkHover, AccentLightHover),
        ("ToggleButtonBackgroundCheckedPressed",     AccentDarkPressed, AccentLightPressed),
        ("ToggleButtonForegroundChecked",            0xFF000000, 0xFFFFFFFF),
        ("ToggleButtonForegroundCheckedPointerOver", 0xFF000000, 0xFFFFFFFF),
        ("ToggleButtonForegroundCheckedPressed",     0xCC000000, 0xCCFFFFFF),

        ("HyperlinkButtonForeground",                AccentDark, 0xFF0067C0),
        ("HyperlinkButtonForegroundPointerOver",     0xFF99EBFF, 0xFF003E92),
        ("HyperlinkButtonForegroundPressed",         AccentDarkPressed, 0xCC0067C0),

        ("TimePickerButtonBackground",               0x0FFFFFFF, 0xB3FFFFFF),
        ("TimePickerButtonBackgroundPointerOver",    0x15FFFFFF, 0x80F9F9F9),
        ("TimePickerButtonBackgroundPressed",        0x08FFFFFF, 0x4DF9F9F9),
        ("TimePickerButtonBorderBrush",              0x24FFFFFF, 0x29000000),
        ("TimePickerButtonBorderBrushPointerOver",   0x33FFFFFF, 0x3D000000),
        ("TimePickerButtonBorderBrushPressed",       0x24FFFFFF, 0x29000000),
        ("TimePickerSpacerFill",                     0x24FFFFFF, 0x29000000),

        ("ComboBoxBackground",                       0x0FFFFFFF, 0xB3FFFFFF),
        ("ComboBoxBackgroundPointerOver",            0x15FFFFFF, 0x80F9F9F9),
        ("ComboBoxBackgroundPressed",                0x08FFFFFF, 0x4DF9F9F9),
        ("ComboBoxBorderBrush",                      0x24FFFFFF, 0x29000000),
        ("ComboBoxBorderBrushPointerOver",           0x33FFFFFF, 0x3D000000),
        ("ComboBoxBorderBrushPressed",               0x24FFFFFF, 0x29000000),

        ("ToggleSwitchFillOn",                       AccentDark, AccentLight),
        ("ToggleSwitchFillOnPointerOver",            AccentDarkHover, AccentLightHover),
        ("ToggleSwitchFillOnPressed",                AccentDarkPressed, AccentLightPressed),
        ("ToggleSwitchKnobFillOn",                   0xFF000000, 0xFFFFFFFF),
        ("ToggleSwitchKnobFillOnPointerOver",        0xFF000000, 0xFFFFFFFF),
        ("ToggleSwitchKnobFillOnPressed",            0xFF000000, 0xFFFFFFFF),

        ("SliderTrackValueFill",                     AccentDark, AccentLight),
        ("SliderTrackValueFillPointerOver",          AccentDark, AccentLight),
        ("SliderTrackValueFillPressed",              AccentDark, AccentLight),
        ("SliderThumbBackground",                    AccentDark, AccentLight),
        ("SliderThumbBackgroundPointerOver",         AccentDarkHover, AccentLightHover),
        ("SliderThumbBackgroundPressed",             AccentDarkPressed, AccentLightPressed),

        ("RadioButtonOuterEllipseCheckedStroke",            AccentDark, AccentLight),
        ("RadioButtonOuterEllipseCheckedStrokePointerOver", AccentDarkHover, AccentLightHover),
        ("RadioButtonOuterEllipseCheckedStrokePressed",     AccentDarkPressed, AccentLightPressed),
        ("RadioButtonOuterEllipseCheckedFill",              AccentDark, AccentLight),
        ("RadioButtonOuterEllipseCheckedFillPointerOver",   AccentDarkHover, AccentLightHover),
        ("RadioButtonOuterEllipseCheckedFillPressed",       AccentDarkPressed, AccentLightPressed),
        ("RadioButtonCheckGlyphFill",                       0xFF000000, 0xFFFFFFFF),
        ("RadioButtonCheckGlyphFillPointerOver",            0xFF000000, 0xFFFFFFFF),
        ("RadioButtonCheckGlyphFillPressed",                0xFF000000, 0xFFFFFFFF),
    };

    private static void UpdateBrushColors(bool isDark)
    {
        var res = Application.Current.Resources;
        foreach (var (key, dark, light) in BrushTable)
            SetBrush(res, key, C(isDark ? dark : light));
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
