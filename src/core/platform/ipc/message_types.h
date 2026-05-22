#pragma once

#include <cstdint>
#include <cstring>

namespace aura::ipc {

// ============================================================================
// MessageType — all IPC message codes used across App and Service
// ============================================================================

enum class MessageType : uint32_t {
    // Phase 1: connection lifecycle
    HANDSHAKE_REQUEST  = 0x0001,
    HANDSHAKE_RESPONSE = 0x0002,

    // Phase 1: configuration
    CONFIG_RELOAD      = 0x0010,
    CONFIG_APPLY       = 0x0011,

    // Phase 1: theming
    THEME_CHANGE       = 0x0020,

    // Phase 1: state queries
    QUERY_STATE        = 0x0030,

    // Phase 1: feature control
    ENABLE_FEATURE     = 0x0040,
    DISABLE_FEATURE    = 0x0041,

    // Phase 4: Service (Manager) → App (Worker) pushes
    PUSH_THEME         = 0x0050,  // Service pushes a new active theme
    PUSH_CONFIG        = 0x0051,  // Service pushes updated config blob

    // Phase 4: service health
    STATUS_REPORT      = 0x0060,  // Service → App: uptime, client count

    // Phase 4: generic ack (sequenceNumber echoes the ack'd message)
    ACK                = 0x0070,

    // Sprint 4: real-time audio band data (Service → App, ~10fps)
    AUDIO_BANDS        = 0x0080,

    // Sprint 6: process performance snapshot (Service → App, ~0.5fps)
    PERF_STATS         = 0x0090,

    // GAP-4: App → Service: set taskbar monitoring feature flags
    SET_FEATURES       = 0x00A0,
};

// ============================================================================
// Message — fixed-width binary frame (2064 bytes)
// Sent over the named pipe as a single atomic write/read.
// ============================================================================

struct Message {
    uint32_t messageType;       // Cast to MessageType
    uint32_t sequenceNumber;    // Monotonic counter; echoed in responses
    uint32_t payloadSize;       // Bytes used in payload[] (0..2048)
    uint32_t reserved;          // Must be zero
    uint8_t  payload[2048];

    Message() noexcept
        : messageType(0), sequenceNumber(0), payloadSize(0), reserved(0)
    { std::memset(payload, 0, sizeof(payload)); }

    [[nodiscard]] bool isValid() const noexcept {
        return messageType != 0 && payloadSize <= 2048;
    }

    template <typename T>
    [[nodiscard]] T const* getPayload() const noexcept {
        if (payloadSize < sizeof(T)) return nullptr;
        return reinterpret_cast<T const*>(payload);
    }

    template <typename T>
    [[nodiscard]] T* getPayload() noexcept {
        if (payloadSize < sizeof(T)) return nullptr;
        return reinterpret_cast<T*>(payload);
    }

    template <typename T>
    void setPayload(T const& data) noexcept {
        static_assert(sizeof(T) <= 2048, "Payload too large for Message frame");
        std::memcpy(payload, &data, sizeof(T));
        payloadSize = static_cast<uint32_t>(sizeof(T));
    }
};

static_assert(sizeof(Message) == 2064, "Message frame must be exactly 2064 bytes");

// ============================================================================
// Payload structures
// ============================================================================

// HANDSHAKE_REQUEST → HANDSHAKE_RESPONSE
struct HandshakePayload {
    uint32_t clientPID;         // Sender's process ID
    uint32_t clientVersion;     // Protocol version (e.g., 0x0400 = 4.0)
    uint32_t capabilities;      // Feature flags bitmask
};

// THEME_CHANGE / PUSH_THEME
// Extended layout carries the full theme inline so the service can apply
// the accent color and visualizer settings without a round-trip lookup.
struct ThemePayload {
    wchar_t  themeName[256];       // 512 bytes — null-terminated display name
    uint32_t colorCount;           // palette entry count (compat, currently unused)
    uint32_t animationSpeedPct;    // 0-200 (100 = 1×)
    uint8_t  accentR;              // glow accent color — red channel
    uint8_t  accentG;              // green channel
    uint8_t  accentB;              // blue channel
    uint8_t  accentA;              // alpha (255 = fully opaque)
    uint8_t  glowEnabled;          // 1 = overlay windows visible
    uint8_t  showOnHover;          // 1 = fade in on cursor enter
    uint8_t  visualizerEnabled;    // 1 = audio visualizer bar shown
    uint8_t  _pad;                 // explicit alignment padding
    uint32_t visualizerHeightPx;   // bar strip height (40-200)
    float    visualizerBrightness; // 0.0-2.0 (1.0 = normal)
};
static_assert(sizeof(ThemePayload) == 536, "ThemePayload size mismatch — update C# struct too");

// QUERY_STATE (request has no payload; response uses this)
struct QueryStateResponse {
    wchar_t  currentTheme[256]; // Active theme name
    uint32_t serviceVersion;    // Service build version
    uint32_t uptimeSeconds;     // Service uptime
    bool     isElevated;        // Service running with elevated token?
    uint8_t  _pad[3];
};

// STATUS_REPORT
struct StatusPayload {
    uint32_t uptimeSeconds;
    uint32_t connectedClients;
    bool     isElevated;
    uint8_t  _pad[3];
};

// PERF_STATS — process performance snapshot (pushed every ~2 seconds)
struct PerfStatsPayload {
    float   cpuPercent;  // process idle CPU usage 0.0 – 100.0
    float   memoryMB;    // working-set memory in MB
    float   avgFps;      // rolling animation FPS (0 when no animations active)
    uint8_t _pad[4];
};
static_assert(sizeof(PerfStatsPayload) <= 2048, "PerfStatsPayload too large");

// PUSH_CONFIG — raw JSON, stored inline in the variable-length payload
// Callers set payloadSize = strlen(json) and copy into payload[].

// SET_FEATURES — App → Service: configure taskbar monitoring flags.
// autoHideEnabled:     1 = apply ABS_AUTOHIDE via SHAppBarMessage, 0 = clear it
// multiMonitorEnabled: 1 = enumerate secondary taskbars, 0 = primary only
struct FeatureTogglePayload {
    uint8_t autoHideEnabled;      // 1 = on, 0 = off
    uint8_t multiMonitorEnabled;  // 1 = on, 0 = off
    uint8_t _pad[14];             // reserved, must be zero
};
static_assert(sizeof(FeatureTogglePayload) == 16, "FeatureTogglePayload must be 16 bytes");

// AUDIO_BANDS — sent by the Service ~10fps when AudioEngine is active.
// The receiver (config app / WinUI) uses the band data to drive visualizer UI.
struct AudioBandsPayload {
    float    bands[128];    // Per-band magnitudes, 0.0 – 1.0 (SpectrumAnalyzer output)
    float    peak;          // Max across all bands (0.0 – 1.0)
    bool     audioPresent;  // True when any band is above silence threshold
    uint8_t  _pad[3];       // Explicit alignment padding
};
static_assert(sizeof(AudioBandsPayload) <= 2048, "AudioBandsPayload too large");

}  // namespace aura::ipc
