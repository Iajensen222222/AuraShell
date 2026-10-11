using System.Collections.ObjectModel;
using System.Text.Json;
using AuraConfig.Models;

namespace AuraConfig.Services;

/// <summary>
/// Singleton store for user-defined dashboard border-mode items.
/// Persists to %LOCALAPPDATA%\AuraShell\dashboard_items.json.
/// </summary>
public sealed class DashboardItemStore
{
    private static readonly string FilePath = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "AuraShell", "dashboard_items.json");

    private static readonly JsonSerializerOptions JsonOpts =
        new() { WriteIndented = true };

    // Declared after FilePath and JsonOpts: static initializers run in order, and the constructor loads with them.
    public static readonly DashboardItemStore Instance = new();

    public ObservableCollection<DashboardItem> Items { get; } = new();
    public Guid? LastAppliedId { get; private set; }

    private DashboardItemStore() => Load();

    // ── Persistence ────────────────────────────────────────────────────────

    private void Load()
    {
        try
        {
            if (System.IO.File.Exists(FilePath))
            {
                var json = System.IO.File.ReadAllText(FilePath);
                var data = JsonSerializer.Deserialize<SaveData>(json, JsonOpts);
                if (data?.Items is { Count: > 0 })
                {
                    foreach (var item in data.Items) Items.Add(item);
                    LastAppliedId = data.LastAppliedId;
                    return;
                }
            }
        }
        catch (Exception ex)
        {
            Logger.Warn("DashboardItemStore", $"Load failed: {ex.Message}");
        }

        SeedDefaults();
        Save();
    }

    private void SeedDefaults()
    {
        // 12 solid-color presets from the built-in palette
        foreach (var p in GlowPresets.All)
            Items.Add(new DashboardItem { Name = p.Name, Mode = BorderMode.SolidColor, R = p.R, G = p.G, B = p.B });

        // Demo Glow item
        Items.Add(new DashboardItem
        {
            Name = "Electric Glow",
            Description = "Border pulses between blue and black",
            Mode = BorderMode.Glow,
            R = 0x00, G = 0x78, B = 0xD4,
            FadeInSec = 2.0, StayOnColorSec = 1.0, FadeOutSec = 2.0, StayOnBlackSec = 0.5,
        });

        // Demo FadeBetween item
        Items.Add(new DashboardItem
        {
            Name = "Sunrise",
            Description = "Cycles between orange and magenta",
            Mode = BorderMode.FadeBetween,
            R = 0xFF, G = 0x8C, B = 0x00,
            R2 = 0xFF, G2 = 0x00, B2 = 0xFF,
            FadeInSec = 3.0, StayOnColorSec = 0.5, FadeOutSec = 3.0, StayOnBlackSec = 0.0,
        });

        // Demo Rainbow item
        Items.Add(new DashboardItem
        {
            Name = "Rainbow",
            Description = "Smooth cycle through the full color spectrum",
            Mode = BorderMode.Rainbow,
            RainbowColors =
            [
                new() { R = 0xFF, G = 0x00, B = 0x00 },
                new() { R = 0xFF, G = 0x7F, B = 0x00 },
                new() { R = 0xFF, G = 0xFF, B = 0x00 },
                new() { R = 0x00, G = 0xFF, B = 0x00 },
                new() { R = 0x00, G = 0x00, B = 0xFF },
                new() { R = 0x8B, G = 0x00, B = 0xFF },
            ],
            TransitionSec = 2.0,
        });

        // Demo AudioReactive item
        Items.Add(new DashboardItem
        {
            Name        = "Audio Pulse",
            Description = "Bass → purple, treble → green. Brightness follows the beat.",
            Mode        = BorderMode.AudioReactive,
            R  = 0x60, G  = 0x00, B  = 0xC0,   // Bass: deep purple
            R2 = 0x00, G2 = 0xCC, B2 = 0x66,   // Treble: electric green
            AudioHueWeight = 70,
        });

        // Demo LocationGlow item
        Items.Add(new DashboardItem
        {
            Name        = "ChromaZone",
            Description = "Border shifts from red at the top of the screen to blue at the bottom",
            Mode        = BorderMode.LocationGlow,
            R  = 0xFF, G  = 0x22, B  = 0x22,
            R2 = 0x10, G2 = 0x60, B2 = 0xFF,
        });

    }

    private void Save()
    {
        try
        {
            System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(FilePath)!);
            var data = new SaveData { Items = new(Items), LastAppliedId = LastAppliedId };
            System.IO.File.WriteAllText(FilePath, JsonSerializer.Serialize(data, JsonOpts));
        }
        catch (Exception ex)
        {
            Logger.Warn("DashboardItemStore", $"Save failed: {ex.Message}");
        }
    }

    // ── CRUD ───────────────────────────────────────────────────────────────

    public void Add(DashboardItem item)
    {
        Items.Add(item);
        Save();
    }

    public void Update(DashboardItem item)
    {
        for (int i = 0; i < Items.Count; i++)
        {
            if (Items[i].Id == item.Id) { Items[i] = item; Save(); return; }
        }
    }

    public void Delete(Guid id)
    {
        for (int i = Items.Count - 1; i >= 0; i--)
            if (Items[i].Id == id) { Items.RemoveAt(i); break; }
        if (LastAppliedId == id) LastAppliedId = null;
        Save();
    }

    public DashboardItem Duplicate(Guid id)
    {
        var src = Items.First(x => x.Id == id);
        var copy = new DashboardItem
        {
            Name         = src.Name + " (copy)",
            Description  = src.Description,
            Mode         = src.Mode,
            R = src.R, G = src.G, B = src.B,
            R2 = src.R2, G2 = src.G2, B2 = src.B2,
            RainbowColors = new(src.RainbowColors.Select(c => new RainbowColor { R = c.R, G = c.G, B = c.B })),
            FadeInSec      = src.FadeInSec,
            StayOnColorSec = src.StayOnColorSec,
            FadeOutSec     = src.FadeOutSec,
            StayOnBlackSec = src.StayOnBlackSec,
            TransitionSec  = src.TransitionSec,
            AudioHueWeight  = src.AudioHueWeight,
            Brightness      = src.Brightness,
            HotkeyModifiers = src.HotkeyModifiers,
            HotkeyKey       = src.HotkeyKey,
        };
        Items.Add(copy);
        Save();
        return copy;
    }

    public void SetApplied(Guid? id)
    {
        LastAppliedId = id;
        Save();
    }

    /// <summary>Persists the current item order after a drag-to-reorder operation.</summary>
    public void SaveOrder() => Save();

    // ── JSON model ─────────────────────────────────────────────────────────

    private sealed class SaveData
    {
        public List<DashboardItem> Items { get; set; } = new();
        public Guid? LastAppliedId { get; set; }
    }
}
