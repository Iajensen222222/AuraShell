using System.Runtime.InteropServices;

namespace AuraConfig.Models;

// ── Binary protocol — matches the C++ structs exactly ──────────────────────
// Fixed 2064-byte frame: 4×uint32 header + 2048-byte payload.

public enum AuraMessageType : uint
{
    HandshakeRequest  = 0x0001,
    HandshakeResponse = 0x0002,
    ConfigReload      = 0x0010,
    ConfigApply       = 0x0011,
    ThemeChange       = 0x0020,
    QueryState        = 0x0030,
    EnableFeature     = 0x0040,
    DisableFeature    = 0x0041,
    PushTheme         = 0x0050,
    PushConfig        = 0x0051,
    StatusReport      = 0x0060,
    Ack               = 0x0070,
    AudioBands        = 0x0080,
    PerfStats         = 0x0090,
    SetFeatures       = 0x00A0,
}

[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct AuraMessage
{
    public uint MessageType;
    public uint SequenceNumber;
    public uint PayloadSize;
    public uint Reserved;

    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 2048)]
    public byte[] Payload;

    public const int FrameSize = 2064;

    public bool IsValid =>
        Reserved == 0 &&
        PayloadSize <= 2048 &&
        Enum.IsDefined(typeof(AuraMessageType), MessageType);

    public static AuraMessage Create(AuraMessageType type, uint seq = 0)
    {
        return new AuraMessage
        {
            MessageType    = (uint)type,
            SequenceNumber = seq,
            PayloadSize    = 0,
            Reserved       = 0,
            Payload        = new byte[2048]
        };
    }
}

[StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
public struct HandshakePayload
{
    public uint ClientPID;
    public uint ClientVersion;
    public uint Capabilities;
}

/// <summary>
/// Matches C++ PerfStatsPayload: float cpuPercent + float memoryMB + float avgFps + uint _pad.
/// Total size: 16 bytes (Pack=1).
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct PerfStatsPayload
{
    public float CpuPercent;
    public float MemoryMB;
    public float AvgFps;
    public uint  Pad;
}

[StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
public struct QueryStateResponse
{
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string CurrentTheme;
    public uint   ServiceVersion;
    public uint   UptimeSeconds;
    [MarshalAs(UnmanagedType.I1)]
    public bool   IsElevated;
}

/// <summary>
/// Matches C++ ThemePayload exactly (536 bytes, Pack=1).
/// Extended with accent color + visualizer settings so the service can apply
/// live overlay changes without a separate lookup.
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1, CharSet = CharSet.Unicode)]
public struct ThemePayload
{
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
    public string ThemeName;           // 512 bytes
    public uint   ColorCount;          // compat (unused)
    public uint   AnimationSpeedPct;   // 0-200
    public byte   AccentR;
    public byte   AccentG;
    public byte   AccentB;
    public byte   AccentA;             // 255 = fully opaque
    public byte   GlowEnabled;         // 1 = overlays on
    public byte   ShowOnHover;         // 1 = fade in on cursor enter
    public byte   VisualizerEnabled;   // 1 = audio bar shown
    public byte   Pad;
    public uint   VisualizerHeightPx;  // 40-200
    public float  VisualizerBrightness;// 0.0-2.0
}

/// <summary>
/// Matches C++ AudioBandsPayload: float bands[128] + float peak + bool audioPresent.
/// Total size: 512 + 4 + 4 = 520 bytes (Pack=4 pads the trailing bool to 4 bytes).
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct AudioBandsPayload
{
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)]
    public float[] Bands;
    public float Peak;
    [MarshalAs(UnmanagedType.I1)]
    public bool AudioPresent;
}

/// <summary>
/// SET_FEATURES — App→Service: configure taskbar monitoring behavior.
/// Matches C++ FeatureTogglePayload exactly (16 bytes, Pack=1).
/// </summary>
[StructLayout(LayoutKind.Sequential, Pack = 1)]
public struct FeatureTogglePayload
{
    public byte AutoHideEnabled;      // 1 = enable auto-hide, 0 = disable
    public byte MultiMonitorEnabled;  // 1 = enable multi-monitor, 0 = disable
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 14)]
    public byte[] Pad;                // reserved
}

// ── High-level models ───────────────────────────────────────────────────────

public class ThemeConfig
{
    public string           Name                 { get; set; } = "default";
    public Windows.UI.Color AccentColor          { get; set; }
    public int              AnimSpeedPct         { get; set; } = 100;
    public bool             ShowOnHover          { get; set; } = true;
    public bool             ShowOnLaunch         { get; set; } = true;
    public bool             GlowEnabled          { get; set; } = true;
    public bool             VisualizerEnabled    { get; set; } = true;
    public int              VisualizerHeightPx   { get; set; } = 80;
    public double           VisualizerBrightness { get; set; } = 1.0;

    public ThemeConfig() { }

    public ThemeConfig(string name, Windows.UI.Color accentColor, int animSpeedPct = 100)
    {
        Name = name; AccentColor = accentColor; AnimSpeedPct = animSpeedPct;
    }
}

public class ServiceState
{
    public bool   IsConnected    { get; set; }
    public string CurrentTheme   { get; set; } = "—";
    public uint   UptimeSeconds  { get; set; }
    public uint   ServiceVersion { get; set; }
    public bool   IsElevated     { get; set; }

    public ServiceState() { }

    public ServiceState(bool connected, string theme, uint uptime, uint version, bool elevated)
    {
        IsConnected = connected; CurrentTheme = theme;
        UptimeSeconds = uptime; ServiceVersion = version; IsElevated = elevated;
    }

    public static ServiceState Disconnected =>
        new(false, "—", 0, 0, false);

    public string UptimeFormatted
    {
        get
        {
            var h = UptimeSeconds / 3600;
            var m = (UptimeSeconds % 3600) / 60;
            var s = UptimeSeconds % 60;
            return h > 0 ? $"{h}h {m:00}m" : $"{m}m {s:00}s";
        }
    }
}

// ── Glow preset definitions (matches C++ GLOW_PRESETS array) ───────────────

/// <summary>
/// XAML bindable data class — must use { get; set; } so XamlTypeInfo.g.cs can assign properties.
/// </summary>
public class GlowPreset
{
    public string Name         { get; set; } = "";
    public byte   R            { get; set; }
    public byte   G            { get; set; }
    public byte   B            { get; set; }
    public int    AnimSpeedPct { get; set; }
    public double GlowIntensity{ get; set; }

    public GlowPreset() { }  // parameterless ctor for XAML

    public GlowPreset(string name, byte r, byte g, byte b, int animSpeed, double intensity)
    {
        Name = name; R = r; G = g; B = b; AnimSpeedPct = animSpeed; GlowIntensity = intensity;
    }

    public Windows.UI.Color Color =>
        Windows.UI.Color.FromArgb(255, R, G, B);

    public string HexColor =>
        $"#{R:X2}{G:X2}{B:X2}";
}

public static class GlowPresets
{
    public static readonly GlowPreset[] All =
    [
        new GlowPreset("AuraShell",    0,  242, 255, 100, 1.00),
        new GlowPreset("Cobalt",       0,  120, 212, 100, 0.90),
        new GlowPreset("Ultraviolet", 124,  77, 255, 120, 1.00),
        new GlowPreset("Sakura",      255,  64, 129,  80, 0.85),
        new GlowPreset("Ember",       255, 109,   0, 140, 0.95),
        new GlowPreset("Solar",       255, 214,   0,  90, 0.80),
        new GlowPreset("Mint",        105, 240, 174, 100, 0.90),
        new GlowPreset("Sky",          64, 196, 255, 110, 0.95),
        new GlowPreset("Plasma",      224,  64, 251, 150, 1.00),
        new GlowPreset("Coral",       255,  82,  82,  80, 0.85),
        new GlowPreset("Teal",        100, 255, 218, 100, 0.90),
        new GlowPreset("Slate",       120, 144, 156,  70, 0.60),
    ];
}
