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
struct ThemePayload {
    wchar_t  themeName[256];    // Null-terminated theme identifier
    uint32_t colorCount;        // Number of palette entries
    uint32_t animationSpeedPct; // Animation speed multiplier (100 = 1×)
};

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

// PUSH_CONFIG — raw JSON, stored inline in the variable-length payload
// Callers set payloadSize = strlen(json) and copy into payload[].

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
