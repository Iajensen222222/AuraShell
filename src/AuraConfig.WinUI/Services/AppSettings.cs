using System.Text.Json;

namespace AuraConfig.Services;

/// <summary>
/// Lightweight key-value settings store backed by a JSON file in %LOCALAPPDATA%\AuraShell\.
/// Safe for unpackaged apps — no ApplicationData.Current required.
/// </summary>
internal static class AppSettings
{
    private static readonly string FilePath = System.IO.Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "AuraShell", "settings.json");

    private static Dictionary<string, string> _cache = Load();

    private static Dictionary<string, string> Load()
    {
        try
        {
            if (System.IO.File.Exists(FilePath))
            {
                var json = System.IO.File.ReadAllText(FilePath);
                return JsonSerializer.Deserialize<Dictionary<string, string>>(json)
                       ?? new Dictionary<string, string>();
            }
        }
        catch { }
        return new Dictionary<string, string>();
    }

    private static void Save()
    {
        try
        {
            System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(FilePath)!);
            System.IO.File.WriteAllText(FilePath, JsonSerializer.Serialize(_cache));
        }
        catch { }
    }

    public static string? Get(string key) =>
        _cache.TryGetValue(key, out var v) ? v : null;

    public static void Set(string key, string value)
    {
        _cache[key] = value;
        Save();
    }

    public static void Remove(string key)
    {
        _cache.Remove(key);
        Save();
    }

    public static void Clear()
    {
        _cache.Clear();
        Save();
    }
}
