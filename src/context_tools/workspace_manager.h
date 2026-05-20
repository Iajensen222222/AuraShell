#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

#include "theme_model.h"   // ThemeConfig, AuraColor (src/app/)

namespace aura::context {

// Maintains per-virtual-desktop theme assignments and triggers ThemeApplier
// whenever the active virtual desktop changes.
//
// Lifecycle: initialize() once from main_app.cpp, then call setDesktopTheme()
// from the config UI to assign per-workspace appearances.
class WorkspaceManager {
public:
    static WorkspaceManager& getInstance();

    // Subscribe to VirtualDesktopDetector, apply default theme, start watching.
    // Safe to call even when VirtualDesktopDetector is unavailable (degrades
    // gracefully to a single global theme).
    bool initialize();
    void shutdown();
    bool isInitialized() const;

    // -----------------------------------------------------------------------
    // Per-desktop theme assignment
    // -----------------------------------------------------------------------

    // Assign a theme to a specific virtual desktop GUID. Applying to the
    // current desktop triggers ThemeApplier immediately.
    void setDesktopTheme(GUID desktopId, const aura::app::ThemeConfig& theme);

    // Remove a custom theme; the desktop falls back to the default.
    void clearDesktopTheme(GUID desktopId);

    // Retrieve the theme assigned to a desktop, or default if none set.
    aura::app::ThemeConfig getDesktopTheme(GUID desktopId) const;

    // -----------------------------------------------------------------------
    // Default theme (used when no per-desktop override exists)
    // -----------------------------------------------------------------------

    void setDefaultTheme(const aura::app::ThemeConfig& theme);
    aura::app::ThemeConfig getDefaultTheme() const;

    // Theme currently active on the visible desktop.
    aura::app::ThemeConfig getActiveTheme() const;

    // -----------------------------------------------------------------------
    // Events
    // -----------------------------------------------------------------------

    // Fired on the VirtualDesktopDetector poll thread when the desktop changes
    // AND the resulting theme differs from the previous one.
    using ThemeChangeCallback = std::function<void(const aura::app::ThemeConfig&)>;
    void subscribeThemeChange(ThemeChangeCallback cb);

private:
    WorkspaceManager() = default;
    ~WorkspaceManager() { shutdown(); }
    WorkspaceManager(const WorkspaceManager&) = delete;
    WorkspaceManager& operator=(const WorkspaceManager&) = delete;

    void onDesktopSwitch(GUID prev, GUID next);
    void dispatchTheme(const aura::app::ThemeConfig& theme);

    static std::string guidToString(GUID g);

    mutable std::mutex                                    m_mutex;
    std::map<std::string, aura::app::ThemeConfig>         m_desktopThemes; // GUID → theme
    aura::app::ThemeConfig                                m_defaultTheme;
    aura::app::ThemeConfig                                m_activeTheme;
    std::vector<ThemeChangeCallback>                      m_callbacks;
    bool                                                  m_initialized{false};
};

} // namespace aura::context
