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

struct ThemeConfig {
    std::wstring themeName      = L"default";
    AuraColor    accentColor    = {};
    uint32_t     animSpeedPct   = 100;   // clamped 0-200 on load
    bool         showOnHover    = true;
    bool         showOnLaunch   = true;
    bool         glowEnabled    = true;
};

struct AppConfig {
    ThemeConfig  activeTheme;
    bool         autoStartService   = true;
    std::string  lastServiceVersion;
    int          version            = 1;

    // Phase 10.7: behavior settings
    bool         autoStartApp       = false;  // registry Run key for AuraConfig.exe
    bool         monitorAutoHide    = true;   // taskbar auto-hide detection
    bool         enableMultiMonitor = true;   // multi-monitor overlay support
};

}  // namespace aura::app
