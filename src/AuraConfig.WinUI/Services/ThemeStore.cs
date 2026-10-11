using System.Collections.ObjectModel;
using System.Text.Json;
using System.Text.Json.Serialization;
using AuraConfig.Models;

namespace AuraConfig.Services;

/// <summary>
/// Persisted theme. Extends GlowPreset with edit/save capability and a built-in flag.
/// </summary>
public class AuraTheme
{
    public string Name         { get; set; } = "New Theme";
    public byte   R            { get; set; } = 0;
    public byte   G            { get; set; } = 120;
    public byte   B            { get; set; } = 212;
    public int    AnimSpeedPct         { get; set; } = 100;
    public bool   ShowOnHover          { get; set; } = true;
    public bool   GlowEnabled          { get; set; } = true;
    public bool   VisualizerEnabled    { get; set; } = true;
    public int    VisualizerHeightPx   { get; set; } = 80;
    public double VisualizerBrightness { get; set; } = 1.0;
    public MonitorEntry[] MonitorConfigs { get; set; } =
    [
        new() { Enabled = 1 },
        new() { Enabled = 1 },
        new() { Enabled = 1 },
        new() { Enabled = 1 },
    ];

    // ── Optional Windows look ──────────────────────────────────────────────
    // Defaults leave Windows alone, so themes saved before these existed apply as before.

    /// <summary>Wallpaper to set when the theme is applied; null keeps the current one.</summary>
    public string? WallpaperPath { get; set; }

    [JsonConverter(typeof(JsonStringEnumConverter))]
    public WallpaperFit WallpaperFit { get; set; } = WallpaperFit.Fill;

    /// <summary>Set the Windows accent to this theme's glow color.</summary>
    public bool SetWindowsAccent { get; set; }

    /// <summary>"Light" or "Dark" for Windows and apps; null keeps the current mode.</summary>
    public string? WindowsMode { get; set; }

    [JsonIgnore]
    public bool HasWindowsLook => WallpaperPath is not null || SetWindowsAccent || WindowsMode is not null;

    [JsonIgnore]
    public bool IsBuiltIn { get; init; }

    [JsonIgnore]
    public Windows.UI.Color Color =>
        Windows.UI.Color.FromArgb(255, R, G, B);

    [JsonIgnore]
    public string HexColor => $"#{R:X2}{G:X2}{B:X2}";

    public ThemeConfig ToThemeConfig() => new(Name, Color, AnimSpeedPct)
    {
        ShowOnHover          = ShowOnHover,
        GlowEnabled          = GlowEnabled,
        VisualizerEnabled    = VisualizerEnabled,
        VisualizerHeightPx   = VisualizerHeightPx,
        VisualizerBrightness = VisualizerBrightness,
        PerMonitor           = (MonitorEntry[])MonitorConfigs.Clone(),
    };

    public AuraTheme Clone() => new()
    {
        Name = Name, R = R, G = G, B = B,
        AnimSpeedPct = AnimSpeedPct, ShowOnHover = ShowOnHover, GlowEnabled = GlowEnabled,
        VisualizerEnabled = VisualizerEnabled, VisualizerHeightPx = VisualizerHeightPx,
        VisualizerBrightness = VisualizerBrightness,
        MonitorConfigs = (MonitorEntry[])MonitorConfigs.Clone(),
        WallpaperPath = WallpaperPath, WallpaperFit = WallpaperFit,
        SetWindowsAccent = SetWindowsAccent, WindowsMode = WindowsMode,
        IsBuiltIn = false,
    };

    /// <summary>
    /// Applies the theme's Windows look (wallpaper, accent, light/dark), skipping the parts
    /// the theme leaves alone. Returns false if the wallpaper file is missing or refused.
    /// </summary>
    public bool ApplyWindowsLook()
    {
        bool ok = true;
        if (WallpaperPath is not null)
            ok = SystemLookService.SetWallpaper(WallpaperPath, WallpaperFit);
        if (SetWindowsAccent)
            SystemLookService.SetAccent(R, G, B);
        if (WindowsMode is "Light" or "Dark")
        {
            bool light = WindowsMode == "Light";
            SystemLookService.SetMode(light, light);
        }
        return ok;
    }

    public static AuraTheme FromPreset(GlowPreset p) => new()
    {
        Name = p.Name, R = p.R, G = p.G, B = p.B,
        AnimSpeedPct = p.AnimSpeedPct, ShowOnHover = true, GlowEnabled = true,
        IsBuiltIn = true,
    };

    public static AuraTheme FromThemeConfig(string name, ThemeConfig cfg) => new()
    {
        Name = name,
        R = cfg.AccentColor.R, G = cfg.AccentColor.G, B = cfg.AccentColor.B,
        AnimSpeedPct = cfg.AnimSpeedPct, ShowOnHover = cfg.ShowOnHover,
        GlowEnabled = cfg.GlowEnabled,
        VisualizerEnabled = cfg.VisualizerEnabled, VisualizerHeightPx = cfg.VisualizerHeightPx,
        VisualizerBrightness = cfg.VisualizerBrightness,
        MonitorConfigs = cfg.PerMonitor is { Length: 4 }
            ? (MonitorEntry[])cfg.PerMonitor.Clone()
            : [new() { Enabled = 1 }, new() { Enabled = 1 }, new() { Enabled = 1 }, new() { Enabled = 1 }],
        IsBuiltIn = false,
    };
}

/// <summary>
/// Application-wide singleton owning the full theme catalog:
/// 12 read-only built-ins + user-created custom themes persisted to
/// %LOCALAPPDATA%\AuraShell\themes.json.
/// </summary>
public sealed class ThemeStore
{
    private static readonly string FilePath = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "AuraShell", "themes.json");

    // MonitorEntry is a struct of public fields, which System.Text.Json skips by default.
    private static readonly JsonSerializerOptions JsonOpts =
        new() { WriteIndented = true, IncludeFields = true };

    // Declared after FilePath and JsonOpts: static initializers run in order, and the constructor loads with them.
    public static ThemeStore Instance { get; } = new();

    public IReadOnlyList<AuraTheme> BuiltInThemes { get; } =
        GlowPresets.All.Select(AuraTheme.FromPreset).ToList();

    public ObservableCollection<AuraTheme> CustomThemes { get; } = new();

    private ThemeStore() => LoadCustom();

    // ── Persistence ────────────────────────────────────────────────────────

    public void SaveCustom()
    {
        try
        {
            System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(FilePath)!);
            var json = JsonSerializer.Serialize(CustomThemes.ToList(), JsonOpts);
            System.IO.File.WriteAllText(FilePath, json);
        }
        catch (Exception ex) { Logger.Error("ThemeStore", "Saving themes.json failed", ex); }
    }

    private void LoadCustom()
    {
        try
        {
            if (!System.IO.File.Exists(FilePath)) return;
            var json = System.IO.File.ReadAllText(FilePath);
            var list = JsonSerializer.Deserialize<List<AuraTheme>>(json, JsonOpts);
            if (list is null) return;
            // Files saved before IncludeFields hold empty {} monitor entries; read those as
            // the default (on, inherit color) rather than "every monitor off".
            bool hasMonitorFields = json.Contains("\"Enabled\"", StringComparison.Ordinal);
            foreach (var t in list)
            {
                if (!hasMonitorFields)
                    for (int i = 0; i < t.MonitorConfigs.Length; i++)
                        t.MonitorConfigs[i].Enabled = 1;
                CustomThemes.Add(t);
            }
        }
        catch (Exception ex) { Logger.Error("ThemeStore", "Loading themes.json failed", ex); }
    }

    // ── Mutation ───────────────────────────────────────────────────────────

    public AuraTheme AddCustom(AuraTheme theme)
    {
        CustomThemes.Add(theme);
        SaveCustom();
        return theme;
    }

    /// <summary>Applies a theme everywhere: the glow (via the service) and its Windows look.</summary>
    public static async Task<bool> ApplyAsync(AuraTheme theme)
    {
        await ServiceManager.Instance.PushThemeAsync(theme.ToThemeConfig());
        return theme.ApplyWindowsLook();
    }

    public void UpdateCustom(AuraTheme theme)
    {
        // Theme is a reference type already in CustomThemes — just save.
        SaveCustom();
    }

    public void DeleteCustom(AuraTheme theme)
    {
        CustomThemes.Remove(theme);
        SaveCustom();
    }

    // ── Helpers ────────────────────────────────────────────────────────────

    /// <summary>Combined list: built-ins first, then custom.</summary>
    public IEnumerable<AuraTheme> AllThemes =>
        BuiltInThemes.Concat(CustomThemes);

    /// <summary>
    /// Creates a new custom theme from the current Visuals settings and saves it.
    /// </summary>
    public AuraTheme CreateFromCurrent(ThemeConfig cfg, string name) =>
        AddCustom(AuraTheme.FromThemeConfig(name, cfg));

    /// <summary>Duplicates a built-in (or custom) theme as a new custom theme.</summary>
    public AuraTheme Duplicate(AuraTheme source)
    {
        var copy = source.Clone();
        copy.Name = $"{source.Name} (copy)";
        return AddCustom(copy);
    }

    /// <summary>
    /// Returns the next theme after <paramref name="current"/> in the full catalog,
    /// wrapping around. Used by Win+Shift+T hotkey.
    /// </summary>
    public AuraTheme CycleNext(AuraTheme? current)
    {
        var all = AllThemes.ToList();
        if (all.Count == 0) return BuiltInThemes[0];
        if (current is null) return all[0];
        int idx = all.IndexOf(current);
        return all[(idx + 1) % all.Count];
    }
}
