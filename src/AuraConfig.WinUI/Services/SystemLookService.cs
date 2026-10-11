using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.Win32;

namespace AuraConfig.Services;

/// <summary>How Windows sizes the wallpaper on the screen (Settings → Personalization → Background).</summary>
public enum WallpaperFit { Fill, Fit, Stretch, Tile, Center, Span }

/// <summary>
/// Changes the Windows look outside the AuraShell window: desktop wallpaper, accent color and
/// light/dark mode for apps and for Windows itself. All of these are per-user registry values,
/// so no admin rights are needed.
///
/// Every change first snapshots the user's original values to
/// %LOCALAPPDATA%\AuraShell\look_backup.json (once, until restored), so
/// <see cref="RestoreOriginal"/> can always put the desktop back the way it was.
/// </summary>
internal static class SystemLookService
{
    private const string Tag = "SystemLook";

    private const string DesktopKey     = @"Control Panel\Desktop";
    private const string PersonalizeKey = @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize";
    private const string AccentKey      = @"Software\Microsoft\Windows\CurrentVersion\Explorer\Accent";
    private const string DwmKey         = @"Software\Microsoft\Windows\DWM";

    private const string RecentWallpapersKey = "RecentWallpapers";
    private const string WallpaperFitKey     = "WallpaperFit";
    public  const int    MaxRecentWallpapers = 6;

    // Alpha Windows uses for the DWM colorization color (matches what Settings writes).
    private const uint ColorizationAlpha = 0xC4;
    // Eighth AccentPalette entry: a neutral gray Windows keeps constant across accents.
    private static readonly byte[] PaletteNeutral = [0x4C, 0x4A, 0x48, 0x00];

    private static readonly string AuraDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AuraShell");
    private static readonly string BackupPath    = Path.Combine(AuraDir, "look_backup.json");
    private static readonly string BackupImgDir  = Path.Combine(AuraDir, "look_backup");

    /// <summary>Raised after AuraShell changed the wallpaper, accent or mode.</summary>
    public static event Action? LookChanged;

    // ── Wallpaper ──────────────────────────────────────────────────────────

    private const uint SPI_GETDESKWALLPAPER = 0x0073;
    private const uint SPI_SETDESKWALLPAPER = 0x0014;
    private const uint SPIF_UPDATEINIFILE   = 0x01;
    private const uint SPIF_SENDCHANGE      = 0x02;
    private const int  MaxPathChars         = 520;

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool SystemParametersInfo(uint action, uint param, string? pvParam, uint winIni);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true, EntryPoint = "SystemParametersInfoW")]
    private static extern bool SystemParametersInfoGet(uint action, uint param,
        [Out] char[] pvParam, uint winIni);

    /// <summary>Path of the current wallpaper image, or null for a solid color / unknown.</summary>
    public static string? GetCurrentWallpaper()
    {
        var buf = new char[MaxPathChars];
        if (!SystemParametersInfoGet(SPI_GETDESKWALLPAPER, (uint)buf.Length, buf, 0)) return null;
        var path = new string(buf).TrimEnd('\0');
        return string.IsNullOrWhiteSpace(path) ? null : path;
    }

    public static WallpaperFit GetCurrentFit()
    {
        using var key = Registry.CurrentUser.OpenSubKey(DesktopKey);
        var style = key?.GetValue("WallpaperStyle") as string ?? "10";
        var tile  = key?.GetValue("TileWallpaper")  as string ?? "0";
        if (tile == "1") return WallpaperFit.Tile;
        return style switch
        {
            "6"  => WallpaperFit.Fit,
            "2"  => WallpaperFit.Stretch,
            "0"  => WallpaperFit.Center,
            "22" => WallpaperFit.Span,
            _    => WallpaperFit.Fill,
        };
    }

    private static (string Style, string Tile) FitToRegistry(WallpaperFit fit) => fit switch
    {
        WallpaperFit.Fit     => ("6", "0"),
        WallpaperFit.Stretch => ("2", "0"),
        WallpaperFit.Tile    => ("0", "1"),
        WallpaperFit.Center  => ("0", "0"),
        WallpaperFit.Span    => ("22", "0"),
        _                    => ("10", "0"),
    };

    /// <summary>Sets the desktop wallpaper. Returns false (and logs) if Windows refused it.</summary>
    public static bool SetWallpaper(string path, WallpaperFit fit)
    {
        if (!File.Exists(path))
        {
            Logger.Warn(Tag, $"Wallpaper not found: {path}");
            return false;
        }
        EnsureBackup();

        var (style, tile) = FitToRegistry(fit);
        using (var key = Registry.CurrentUser.CreateSubKey(DesktopKey))
        {
            key.SetValue("WallpaperStyle", style, RegistryValueKind.String);
            key.SetValue("TileWallpaper",  tile,  RegistryValueKind.String);
        }

        if (!SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, path, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
        {
            Logger.Error(Tag, $"SPI_SETDESKWALLPAPER failed ({Marshal.GetLastWin32Error()}) for {path}");
            return false;
        }

        AppSettings.Set(WallpaperFitKey, fit.ToString());
        AddRecentWallpaper(path);
        Logger.Info(Tag, $"Wallpaper set to {path} ({fit})");
        LookChanged?.Invoke();
        return true;
    }

    /// <summary>The fit the user last picked in AuraShell, else the current Windows one.</summary>
    public static WallpaperFit PreferredFit =>
        Enum.TryParse(AppSettings.Get(WallpaperFitKey), out WallpaperFit f) ? f : GetCurrentFit();

    /// <summary>Recently used wallpapers, newest first, skipping files that no longer exist.</summary>
    public static List<string> GetRecentWallpapers()
    {
        try
        {
            var json = AppSettings.Get(RecentWallpapersKey);
            var list = json is null ? [] : JsonSerializer.Deserialize<List<string>>(json) ?? [];
            return list.Where(File.Exists).ToList();
        }
        catch { return []; }
    }

    private static void AddRecentWallpaper(string path)
    {
        var list = GetRecentWallpapers();
        list.RemoveAll(p => string.Equals(p, path, StringComparison.OrdinalIgnoreCase));
        list.Insert(0, path);
        if (list.Count > MaxRecentWallpapers) list.RemoveRange(MaxRecentWallpapers, list.Count - MaxRecentWallpapers);
        AppSettings.Set(RecentWallpapersKey, JsonSerializer.Serialize(list));
    }

    public static void RemoveRecentWallpaper(string path)
    {
        var list = GetRecentWallpapers();
        list.RemoveAll(p => string.Equals(p, path, StringComparison.OrdinalIgnoreCase));
        AppSettings.Set(RecentWallpapersKey, JsonSerializer.Serialize(list));
    }

    // ── Light / dark mode ──────────────────────────────────────────────────

    /// <summary>True when Windows (taskbar, Start) uses light mode.</summary>
    public static bool IsSystemLight => ReadDword(PersonalizeKey, "SystemUsesLightTheme", 0) != 0;

    /// <summary>True when apps use light mode.</summary>
    public static bool IsAppsLight => ReadDword(PersonalizeKey, "AppsUseLightTheme", 1) != 0;

    public static void SetMode(bool systemLight, bool appsLight)
    {
        EnsureBackup();
        using (var key = Registry.CurrentUser.CreateSubKey(PersonalizeKey))
        {
            key.SetValue("SystemUsesLightTheme", systemLight ? 1 : 0, RegistryValueKind.DWord);
            key.SetValue("AppsUseLightTheme",    appsLight   ? 1 : 0, RegistryValueKind.DWord);
        }
        Logger.Info(Tag, $"Mode set: Windows {(systemLight ? "light" : "dark")}, apps {(appsLight ? "light" : "dark")}");
        BroadcastColorChange();
        LookChanged?.Invoke();
    }

    // ── Accent color ───────────────────────────────────────────────────────

    /// <summary>The current Windows accent color as (R, G, B).</summary>
    public static (byte R, byte G, byte B) GetAccent()
    {
        // AccentColorMenu is ABGR.
        uint abgr = (uint)ReadDword(AccentKey, "AccentColorMenu", unchecked((int)0xFFD47800));
        return ((byte)abgr, (byte)(abgr >> 8), (byte)(abgr >> 16));
    }

    /// <summary>Whether Windows picks the accent automatically from the wallpaper.</summary>
    public static bool IsAutoAccent => ReadString(DesktopKey, "AutoColorization") == "1"
                                    || ReadDword(DesktopKey, "AutoColorization", 0) == 1;

    /// <summary>Accent shown on Start and the taskbar (only visible in dark Windows mode).</summary>
    public static bool IsAccentOnTaskbar => ReadDword(PersonalizeKey, "ColorPrevalence", 0) != 0;

    /// <summary>Accent shown on title bars and window borders.</summary>
    public static bool IsAccentOnTitleBars => ReadDword(DwmKey, "ColorPrevalence", 0) != 0;

    /// <summary>
    /// Sets the Windows accent color: the palette Start, the taskbar and apps read, plus the
    /// DWM colorization used for title bars. Turns off "pick accent from wallpaper",
    /// otherwise Windows would replace it again on the next wallpaper change.
    /// </summary>
    public static void SetAccent(byte r, byte g, byte b)
    {
        EnsureBackup();
        var palette = BuildPalette(r, g, b);
        uint abgr     = 0xFF000000u | ((uint)b << 16) | ((uint)g << 8) | r;
        uint startAbgr = PaletteEntryToAbgr(palette, 4);          // dark1, as Windows does
        uint argb      = (ColorizationAlpha << 24) | ((uint)r << 16) | ((uint)g << 8) | b;

        using (var key = Registry.CurrentUser.CreateSubKey(AccentKey))
        {
            key.SetValue("AccentPalette",   palette, RegistryValueKind.Binary);
            key.SetValue("AccentColorMenu", unchecked((int)abgr),      RegistryValueKind.DWord);
            key.SetValue("StartColorMenu",  unchecked((int)startAbgr), RegistryValueKind.DWord);
        }
        using (var key = Registry.CurrentUser.CreateSubKey(DwmKey))
        {
            key.SetValue("AccentColor",           unchecked((int)abgr), RegistryValueKind.DWord);
            key.SetValue("ColorizationColor",     unchecked((int)argb), RegistryValueKind.DWord);
            key.SetValue("ColorizationAfterglow", unchecked((int)argb), RegistryValueKind.DWord);
        }
        using (var key = Registry.CurrentUser.CreateSubKey(DesktopKey))
            key.SetValue("AutoColorization", 0, RegistryValueKind.DWord);

        Logger.Info(Tag, $"Accent set to #{r:X2}{g:X2}{b:X2}");
        BroadcastColorChange();
        LookChanged?.Invoke();
    }

    public static void SetAccentSurfaces(bool onTaskbar, bool onTitleBars)
    {
        EnsureBackup();
        using (var key = Registry.CurrentUser.CreateSubKey(PersonalizeKey))
            key.SetValue("ColorPrevalence", onTaskbar ? 1 : 0, RegistryValueKind.DWord);
        using (var key = Registry.CurrentUser.CreateSubKey(DwmKey))
            key.SetValue("ColorPrevalence", onTitleBars ? 1 : 0, RegistryValueKind.DWord);
        BroadcastColorChange();
        LookChanged?.Invoke();
    }

    // Lightness steps Windows-like palettes use: 3 lighter shades, the base, 3 darker shades.
    private static readonly double[] LightSteps = [0.60, 0.40, 0.15];
    private static readonly double[] DarkSteps  = [0.85, 0.60, 0.33];

    /// <summary>Builds the 32-byte AccentPalette (8 × RGBA) Windows derives its accent shades from.</summary>
    internal static byte[] BuildPalette(byte r, byte g, byte b)
    {
        var (h, s, l) = RgbToHsl(r, g, b);
        var bytes = new List<byte>(32);
        foreach (var step in LightSteps) AddRgb(bytes, HslToRgb(h, s, l + (1 - l) * step));
        AddRgb(bytes, (r, g, b));
        foreach (var step in DarkSteps)  AddRgb(bytes, HslToRgb(h, s, l * step));
        bytes.AddRange(PaletteNeutral);
        return bytes.ToArray();

        static void AddRgb(List<byte> list, (byte R, byte G, byte B) c)
        {
            list.Add(c.R); list.Add(c.G); list.Add(c.B); list.Add(0);
        }
    }

    private static uint PaletteEntryToAbgr(byte[] palette, int index)
    {
        int i = index * 4;
        return 0xFF000000u | ((uint)palette[i + 2] << 16) | ((uint)palette[i + 1] << 8) | palette[i];
    }

    private static (double H, double S, double L) RgbToHsl(byte r8, byte g8, byte b8)
    {
        double r = r8 / 255.0, g = g8 / 255.0, b = b8 / 255.0;
        double max = Math.Max(r, Math.Max(g, b)), min = Math.Min(r, Math.Min(g, b));
        double l = (max + min) / 2, h = 0, s = 0;
        if (max != min)
        {
            double d = max - min;
            s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
            if (max == r)      h = (g - b) / d + (g < b ? 6 : 0);
            else if (max == g) h = (b - r) / d + 2;
            else               h = (r - g) / d + 4;
            h /= 6;
        }
        return (h, s, l);
    }

    private static (byte R, byte G, byte B) HslToRgb(double h, double s, double l)
    {
        l = Math.Clamp(l, 0, 1);
        if (s == 0) { byte v = (byte)Math.Round(l * 255); return (v, v, v); }
        double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        double p = 2 * l - q;
        return (Channel(p, q, h + 1.0 / 3), Channel(p, q, h), Channel(p, q, h - 1.0 / 3));

        static byte Channel(double p, double q, double t)
        {
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            double v = t < 1.0 / 6 ? p + (q - p) * 6 * t
                     : t < 0.5     ? q
                     : t < 2.0 / 3 ? p + (q - p) * (2.0 / 3 - t) * 6
                     : p;
            return (byte)Math.Round(Math.Clamp(v, 0, 1) * 255);
        }
    }

    // ── Backup / restore ───────────────────────────────────────────────────

    /// <summary>The user's look from before AuraShell first changed it.</summary>
    private sealed class LookBackup
    {
        public string?  Wallpaper           { get; set; }
        public string?  WallpaperCopy       { get; set; }
        public string?  WallpaperStyle      { get; set; }
        public string?  TileWallpaper       { get; set; }
        public int      SystemUsesLightTheme { get; set; }
        public int      AppsUseLightTheme   { get; set; }
        public int      TaskbarPrevalence   { get; set; }
        public int      TitleBarPrevalence  { get; set; }
        public int      AutoColorization    { get; set; }
        public byte[]?  AccentPalette       { get; set; }
        public int?     AccentColorMenu     { get; set; }
        public int?     StartColorMenu      { get; set; }
        public int?     DwmAccentColor      { get; set; }
        public int?     ColorizationColor   { get; set; }
        public int?     ColorizationAfterglow { get; set; }
        public DateTime Taken               { get; set; }
    }

    public static bool HasBackup => File.Exists(BackupPath);

    /// <summary>Snapshots the current look, unless a snapshot is already waiting to be restored.</summary>
    private static void EnsureBackup()
    {
        if (HasBackup) return;
        try
        {
            using var desktop = Registry.CurrentUser.OpenSubKey(DesktopKey);
            using var accent  = Registry.CurrentUser.OpenSubKey(AccentKey);
            using var dwm     = Registry.CurrentUser.OpenSubKey(DwmKey);

            var wallpaper = GetCurrentWallpaper();
            var backup = new LookBackup
            {
                Wallpaper            = wallpaper,
                WallpaperCopy        = CopyForBackup(wallpaper),
                WallpaperStyle       = desktop?.GetValue("WallpaperStyle") as string,
                TileWallpaper        = desktop?.GetValue("TileWallpaper") as string,
                SystemUsesLightTheme = ReadDword(PersonalizeKey, "SystemUsesLightTheme", 0),
                AppsUseLightTheme    = ReadDword(PersonalizeKey, "AppsUseLightTheme", 1),
                TaskbarPrevalence    = ReadDword(PersonalizeKey, "ColorPrevalence", 0),
                TitleBarPrevalence   = ReadDword(DwmKey, "ColorPrevalence", 0),
                AutoColorization     = IsAutoAccent ? 1 : 0,
                AccentPalette        = accent?.GetValue("AccentPalette") as byte[],
                AccentColorMenu      = accent?.GetValue("AccentColorMenu") as int?,
                StartColorMenu       = accent?.GetValue("StartColorMenu") as int?,
                DwmAccentColor       = dwm?.GetValue("AccentColor") as int?,
                ColorizationColor    = dwm?.GetValue("ColorizationColor") as int?,
                ColorizationAfterglow = dwm?.GetValue("ColorizationAfterglow") as int?,
                Taken                = DateTime.Now,
            };
            Directory.CreateDirectory(AuraDir);
            File.WriteAllText(BackupPath, JsonSerializer.Serialize(backup,
                new JsonSerializerOptions { WriteIndented = true }));
            Logger.Info(Tag, $"Backed up the original look (wallpaper {wallpaper ?? "none"})");
        }
        catch (Exception ex)
        {
            Logger.Error(Tag, "Could not back up the original look", ex);
        }
    }

    /// <summary>
    /// Copies the wallpaper next to the backup so a restore still works if the original
    /// file is moved, or was a temporary file Windows replaces (TranscodedWallpaper).
    /// </summary>
    private static string? CopyForBackup(string? path)
    {
        if (path is null || !File.Exists(path)) return null;
        try
        {
            Directory.CreateDirectory(BackupImgDir);
            var ext  = Path.GetExtension(path);
            var dest = Path.Combine(BackupImgDir, "original" + (string.IsNullOrEmpty(ext) ? ".jpg" : ext));
            File.Copy(path, dest, overwrite: true);
            return dest;
        }
        catch (Exception ex)
        {
            Logger.Warn(Tag, $"Could not copy wallpaper for backup: {ex.Message}");
            return null;
        }
    }

    /// <summary>Puts back the wallpaper, accent and modes from before AuraShell changed them.</summary>
    public static bool RestoreOriginal()
    {
        if (!HasBackup) return false;
        try
        {
            var backup = JsonSerializer.Deserialize<LookBackup>(File.ReadAllText(BackupPath));
            if (backup is null) return false;

            using (var key = Registry.CurrentUser.CreateSubKey(DesktopKey))
            {
                if (backup.WallpaperStyle is not null) key.SetValue("WallpaperStyle", backup.WallpaperStyle, RegistryValueKind.String);
                if (backup.TileWallpaper  is not null) key.SetValue("TileWallpaper",  backup.TileWallpaper,  RegistryValueKind.String);
                key.SetValue("AutoColorization", backup.AutoColorization, RegistryValueKind.DWord);
            }
            var wallpaper = backup.Wallpaper is not null && File.Exists(backup.Wallpaper)
                ? backup.Wallpaper : backup.WallpaperCopy;
            if (wallpaper is not null)
                SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, wallpaper, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

            using (var key = Registry.CurrentUser.CreateSubKey(PersonalizeKey))
            {
                key.SetValue("SystemUsesLightTheme", backup.SystemUsesLightTheme, RegistryValueKind.DWord);
                key.SetValue("AppsUseLightTheme",    backup.AppsUseLightTheme,    RegistryValueKind.DWord);
                key.SetValue("ColorPrevalence",      backup.TaskbarPrevalence,    RegistryValueKind.DWord);
            }
            using (var key = Registry.CurrentUser.CreateSubKey(AccentKey))
            {
                if (backup.AccentPalette   is not null) key.SetValue("AccentPalette",   backup.AccentPalette, RegistryValueKind.Binary);
                if (backup.AccentColorMenu is int acm)  key.SetValue("AccentColorMenu", acm, RegistryValueKind.DWord);
                if (backup.StartColorMenu  is int scm)  key.SetValue("StartColorMenu",  scm, RegistryValueKind.DWord);
            }
            using (var key = Registry.CurrentUser.CreateSubKey(DwmKey))
            {
                key.SetValue("ColorPrevalence", backup.TitleBarPrevalence, RegistryValueKind.DWord);
                if (backup.DwmAccentColor        is int ac) key.SetValue("AccentColor",           ac, RegistryValueKind.DWord);
                if (backup.ColorizationColor     is int cc) key.SetValue("ColorizationColor",     cc, RegistryValueKind.DWord);
                if (backup.ColorizationAfterglow is int ca) key.SetValue("ColorizationAfterglow", ca, RegistryValueKind.DWord);
            }

            BroadcastColorChange();
            File.Delete(BackupPath);
            Logger.Info(Tag, $"Restored the look from {backup.Taken:g}");
            LookChanged?.Invoke();
            return true;
        }
        catch (Exception ex)
        {
            Logger.Error(Tag, "Restoring the original look failed", ex);
            return false;
        }
    }

    // ── Helpers ────────────────────────────────────────────────────────────

    private const uint WM_SETTINGCHANGE   = 0x001A;
    private const uint SMTO_ABORTIFHUNG   = 0x0002;
    private const uint BroadcastTimeoutMs = 1000;
    private static readonly IntPtr HWND_BROADCAST = new(0xFFFF);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint msg, UIntPtr wParam,
        string lParam, uint flags, uint timeout, out UIntPtr result);

    /// <summary>
    /// Tells Explorer and running apps that the color settings changed. Runs on a pool thread:
    /// a broadcast waits on every top-level window, and a hung one would freeze the UI.
    /// </summary>
    private static void BroadcastColorChange() => Task.Run(() =>
    {
        SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, UIntPtr.Zero, "ImmersiveColorSet",
            SMTO_ABORTIFHUNG, BroadcastTimeoutMs, out _);
    });

    private static int ReadDword(string subKey, string name, int fallback)
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(subKey);
            return key?.GetValue(name) is int v ? v : fallback;
        }
        catch { return fallback; }
    }

    private static string? ReadString(string subKey, string name)
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(subKey);
            return key?.GetValue(name) as string;
        }
        catch { return null; }
    }
}
