#include "workspace_manager.h"

#include <cstdio>

#include "virtual_desktop.h"
#include "theme_applier.h"
#include "icon_overlay_manager.h"
#include "logging/logger.h"

namespace aura::context {

WorkspaceManager& WorkspaceManager::getInstance() {
    static WorkspaceManager instance;
    return instance;
}

bool WorkspaceManager::initialize() {
    if (m_initialized) return true;

    // Subscribe to virtual desktop switches.
    auto& vd = aura::platform::VirtualDesktopDetector::getInstance();
    if (vd.initialize()) {
        vd.subscribeDesktopChange([this](GUID prev, GUID next) {
            this->onDesktopSwitch(prev, next);
        });
        aura::logging::Logger::getInstance().info("workspace",
            "WorkspaceManager: VirtualDesktopDetector connected");
    } else {
        aura::logging::Logger::getInstance().warn("workspace",
            "WorkspaceManager: VirtualDesktopDetector unavailable — single-theme mode");
    }

    // Apply default theme on startup.
    ThemeApplier::getInstance().apply(m_defaultTheme);
    m_activeTheme = m_defaultTheme;

    m_initialized = true;
    return true;
}

void WorkspaceManager::shutdown() {
    if (!m_initialized) return;
    m_initialized = false;
    aura::platform::VirtualDesktopDetector::getInstance().shutdown();
}

bool WorkspaceManager::isInitialized() const { return m_initialized; }

void WorkspaceManager::setDesktopTheme(GUID desktopId,
                                        const aura::app::ThemeConfig& theme) {
    std::string key = guidToString(desktopId);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_desktopThemes[key] = theme;
    }

    // If this is the current desktop, apply immediately.
    auto& vd = aura::platform::VirtualDesktopDetector::getInstance();
    if (vd.isInitialized() &&
        IsEqualGUID(vd.getCurrentDesktopId(), desktopId)) {
        dispatchTheme(theme);
    }
}

void WorkspaceManager::clearDesktopTheme(GUID desktopId) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_desktopThemes.erase(guidToString(desktopId));
}

aura::app::ThemeConfig WorkspaceManager::getDesktopTheme(GUID desktopId) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_desktopThemes.find(guidToString(desktopId));
    return (it != m_desktopThemes.end()) ? it->second : m_defaultTheme;
}

void WorkspaceManager::setDefaultTheme(const aura::app::ThemeConfig& theme) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_defaultTheme = theme;
}

aura::app::ThemeConfig WorkspaceManager::getDefaultTheme() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_defaultTheme;
}

aura::app::ThemeConfig WorkspaceManager::getActiveTheme() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_activeTheme;
}

void WorkspaceManager::subscribeThemeChange(ThemeChangeCallback cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_callbacks.push_back(std::move(cb));
}

// ============================================================================
// Private
// ============================================================================

void WorkspaceManager::onDesktopSwitch(GUID /*prev*/, GUID next) {
    aura::app::ThemeConfig newTheme;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_desktopThemes.find(guidToString(next));
        newTheme = (it != m_desktopThemes.end()) ? it->second : m_defaultTheme;
    }
    dispatchTheme(newTheme);
}

void WorkspaceManager::dispatchTheme(const aura::app::ThemeConfig& theme) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_activeTheme = theme;
    }

    // Apply to the system.
    ThemeApplier::getInstance().apply(theme);

    // Trigger fade-through-black transition on the overlay glow windows.
    auto const& ac = theme.accentColor;
    D2D1_COLOR_F const d2dColor = {
        ac.r / 255.0f, ac.g / 255.0f, ac.b / 255.0f, 1.0f
    };
    aura::taskbar::IconOverlayManager::getInstance().onDesktopSwitch(d2dColor);

    // Notify subscribers (config UI, IPC push, etc.).
    std::vector<ThemeChangeCallback> snapshot;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        snapshot = m_callbacks;
    }
    for (auto& cb : snapshot) {
        try { cb(theme); } catch (...) {}
    }

    int const _n = WideCharToMultiByte(CP_UTF8, 0,
        theme.themeName.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string _name(static_cast<size_t>(_n > 0 ? _n - 1 : 0), '\0');
    if (_n > 0)
        WideCharToMultiByte(CP_UTF8, 0, theme.themeName.c_str(), -1,
                            &_name[0], _n, nullptr, nullptr);
    aura::logging::Logger::getInstance().info("workspace",
        "Theme dispatched: " + _name);
}

std::string WorkspaceManager::guidToString(GUID g) {
    char buf[40];
    std::snprintf(buf, sizeof(buf),
        "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        g.Data1, g.Data2, g.Data3,
        g.Data4[0], g.Data4[1],
        g.Data4[2], g.Data4[3], g.Data4[4],
        g.Data4[5], g.Data4[6], g.Data4[7]);
    return std::string(buf);
}

} // namespace aura::context
