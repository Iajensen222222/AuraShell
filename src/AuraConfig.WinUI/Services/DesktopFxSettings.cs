namespace AuraConfig.Services;

/// <summary>
/// Saved Desktop FX settings (hover glow and click burst). Shared by MainWindow, which
/// applies them to <see cref="DesktopIconGlow"/> at startup, and DesktopFxPage, which edits them.
/// </summary>
internal static class DesktopFxSettings
{
    public const string GlowEnabledKey    = "DesktopGlowEnabled";
    public const string ColorKey          = "DesktopFxColor";
    public const string GlowRadiusKey     = "DesktopFxGlowRadius";
    public const string IntensityKey      = "DesktopFxIntensity";
    public const string FadeSpeedKey      = "DesktopFxFadeSpeed";
    public const string LabelsVisibleKey  = "DesktopLabelsVisible";
    public const string GlowStyleKey      = "DesktopFxGlowStyle";
    public const string CycleSecondsKey   = "DesktopFxCycleSeconds";
    public const string ClickEnabledKey   = "DesktopFxClickEnabled";
    public const string ClickStyleKey     = "DesktopFxClickStyle";
    public const string ClickSizeKey      = "DesktopFxClickSize";
    public const string ClickDurationKey  = "DesktopFxClickDuration";
    public const string ClickDesktopOnlyKey = "DesktopFxClickDesktopOnly";

    public const int DefaultGlowRadius     = 14;
    public const int DefaultIntensityPct   = 80;
    public const int DefaultFadeSpeedPct   = 100;
    public const int DefaultCycleSeconds   = 6;
    public const int DefaultClickSize      = 48;
    public const int DefaultClickDurationMs = 450;

    public static bool GetBool(string key, bool fallback)
        => AppSettings.Get(key) is string v ? v != "0" : fallback;

    public static void SetBool(string key, bool value)
        => AppSettings.Set(key, value ? "1" : "0");

    public static int GetInt(string key, int fallback, int min, int max)
        => AppSettings.Get(key) is string v && int.TryParse(v, out int n) ? Math.Clamp(n, min, max) : fallback;

    public static T GetEnum<T>(string key, T fallback) where T : struct, Enum
        => AppSettings.Get(key) is string v && Enum.TryParse(v, out T e) && Enum.IsDefined(e) ? e : fallback;

    // Saved color as RRGGBB, or null when none was saved.
    public static (byte r, byte g, byte b)? GetColor()
    {
        if (AppSettings.Get(ColorKey) is not string hex || hex.Length != 6) return null;
        try
        {
            return (Convert.ToByte(hex[0..2], 16), Convert.ToByte(hex[2..4], 16), Convert.ToByte(hex[4..6], 16));
        }
        catch (FormatException) { return null; }
    }

    /// <summary>Pushes every saved Desktop FX setting to the live glow service.</summary>
    public static void ApplyTo(DesktopIconGlow dg)
    {
        if (GetColor() is var (r, g, b)) dg.SetColor(r, g, b);
        dg.Enabled        = GetBool(GlowEnabledKey, true);
        dg.GlowRadius     = GetInt(GlowRadiusKey, DefaultGlowRadius, 4, 40);
        dg.Intensity      = GetInt(IntensityKey, DefaultIntensityPct, 0, 100) / 100f;
        dg.FadeSpeed      = GetInt(FadeSpeedKey, DefaultFadeSpeedPct, 20, 300) / 100f;
        dg.HideIconLabels = !GetBool(LabelsVisibleKey, true);
        dg.GlowStyle      = GetEnum(GlowStyleKey, DesktopIconGlow.HoverGlowStyle.Solid);
        dg.CycleSeconds   = GetInt(CycleSecondsKey, DefaultCycleSeconds, 1, 20);
        dg.ClickBurstEnabled     = GetBool(ClickEnabledKey, true);
        dg.ClickBurstDesktopOnly = GetBool(ClickDesktopOnlyKey, true);
        dg.BurstStyle            = GetEnum(ClickStyleKey, DesktopIconGlow.ClickBurstStyle.RingAndSparks);
        dg.ClickBurstSize        = GetInt(ClickSizeKey, DefaultClickSize, 20, 120);
        dg.ClickBurstDurationMs  = GetInt(ClickDurationKey, DefaultClickDurationMs, 200, 1200);
    }
}
