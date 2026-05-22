#pragma once

#include <cstdint>
#include <string>

namespace aura::app {

struct AuraColor {
    uint8_t r{0x00};
    uint8_t g{0x78};
    uint8_t b{0xD4};
    uint8_t a{0xFF};
};

struct MonitorConfig {
    AuraColor color   = {};  // all-zero = inherit ThemeConfig.accentColor
    uint8_t   enabled = 1;   // 1 = overlay on for this monitor
    uint8_t   _pad[3] = {};
};

static constexpr int kMaxMonitors = 4;

struct ThemeConfig {
    std::wstring themeName      = L"default";
    AuraColor    accentColor    = {};
    uint32_t     animSpeedPct   = 100;   // clamped 0-200 on load
    bool         showOnHover    = true;
    bool         showOnLaunch   = true;
    bool         glowEnabled    = true;
    MonitorConfig perMonitor[kMaxMonitors] = {};
};

struct AudioVisualizerConfig {
    bool     enabled         = true;
    float    sensitivity     = 1.0f;
    float    smoothing       = 0.25f;
    float    brightness      = 1.0f;
    uint32_t overlayHeightPx = 80;
};

struct HoverConfig {
    bool     enabled  = true;
    float    scaleMax = 0.25f;
    uint32_t enterMs  = 150;
    uint32_t exitMs   = 180;
};

struct AcrylicConfig {
    bool  enabled     = true;
    float tintOpacity = 0.15f;
};

struct AppConfig {
    ThemeConfig  activeTheme;
    bool         autoStartService   = true;
    std::string  lastServiceVersion;
    int          version            = 1;

    // Phase 10.7: behavior settings
    bool         autoStartApp       = false;
    bool         monitorAutoHide    = true;
    bool         enableMultiMonitor = true;

    // Sprint 4+: subsystem configs
    AudioVisualizerConfig audioVisualizer;
    HoverConfig           hover;
    AcrylicConfig         acrylic;
};

}  // namespace aura::app
