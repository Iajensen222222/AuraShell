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
    };

    public AuraTheme Clone() => new()
    {
        Name = Name, R = R, G = G, B = B,
        AnimSpeedPct = AnimSpeedPct, ShowOnHover = ShowOnHover, GlowEnabled = GlowEnabled,
        VisualizerEnabled = VisualizerEnabled, VisualizerHeightPx = VisualizerHeightPx,
        VisualizerBrightness = VisualizerBrightness,
        IsBuiltIn = false,
    };

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
    public static ThemeStore Instance { get; } = new();

    private static readonly string FilePath = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "AuraShell", "themes.json");

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
            var json = JsonSerializer.Serialize(CustomThemes.ToList(),
                new JsonSerializerOptions { WriteIndented = true });
            System.IO.File.WriteAllText(FilePath, json);
        }
        catch { }
    }

    private void LoadCustom()
    {
        try
        {
            if (!System.IO.File.Exists(FilePath)) return;
            var list = JsonSerializer.Deserialize<List<AuraTheme>>(
                System.IO.File.ReadAllText(FilePath));
            if (list is null) return;
            foreach (var t in list)
                CustomThemes.Add(t);
        }
        catch { }
    }

    // ── Mutation ───────────────────────────────────────────────────────────

    public AuraTheme AddCustom(AuraTheme theme)
    {
        CustomThemes.Add(theme);
        SaveCustom();
        return theme;
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
