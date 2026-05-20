#include "hotkey_manager.h"

#include "icon_overlay_manager.h"
#include "audio_visualizer.h"
#include "workspace_manager.h"
#include "logging/logger.h"

namespace aura::system {

HotkeyManager& HotkeyManager::getInstance() {
    static HotkeyManager instance;
    return instance;
}

bool HotkeyManager::initialize(HWND msgHwnd) {
    if (m_initialized) return true;
    m_hwnd = msgHwnd;

    // Default bindings — can be overridden before initialize() via setBinding().
    if (m_bindings.empty()) {
        m_bindings = {
            { HotkeyAction::ToggleOverlays,   1, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, 'A' },
            { HotkeyAction::ToggleVisualizer,  2, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, 'V' },
            { HotkeyAction::CycleTheme,        3, MOD_WIN | MOD_SHIFT | MOD_NOREPEAT, 'T' },
        };
    }

    int successCount = 0;
    for (auto& b : m_bindings) {
        if (RegisterHotKey(msgHwnd, static_cast<int>(b.id), b.modifiers, b.vk)) {
            b.registered = true;
            ++successCount;
        } else {
            aura::logging::Logger::getInstance().warn("hotkey",
                "Failed to register hotkey id=" + std::to_string(b.id) +
                " (0x" + [](DWORD e){ char buf[16]; std::snprintf(buf,16,"%08lX",e); return std::string(buf); }(GetLastError()) +
                ") — another app may own it");
        }
    }

    m_initialized = (successCount > 0);
    if (m_initialized) {
        aura::logging::Logger::getInstance().info("hotkey",
            std::to_string(successCount) + "/" + std::to_string(m_bindings.size()) +
            " hotkeys registered");
    }
    return m_initialized;
}

void HotkeyManager::shutdown() {
    if (!m_initialized) return;
    for (auto& b : m_bindings) {
        if (b.registered) {
            UnregisterHotKey(m_hwnd, static_cast<int>(b.id));
            b.registered = false;
        }
    }
    m_initialized = false;
}

bool HotkeyManager::isInitialized() const { return m_initialized; }

bool HotkeyManager::handleMessage(UINT msg, WPARAM wp, LPARAM /*lp*/) {
    if (msg != WM_HOTKEY) return false;

    for (const auto& b : m_bindings) {
        if (b.registered && b.id == static_cast<UINT>(wp)) {
            dispatch(b.action);
            return true;
        }
    }
    return false;
}

void HotkeyManager::subscribeAction(HotkeyCallback cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_callbacks.push_back(std::move(cb));
}

void HotkeyManager::setBinding(HotkeyAction action, UINT modifiers, UINT vk) {
    if (m_initialized) return; // too late to change; ignore
    for (auto& b : m_bindings) {
        if (b.action == action) { b.modifiers = modifiers; b.vk = vk; return; }
    }
    // Not found — add a new entry.
    m_bindings.push_back({ action, static_cast<UINT>(action), modifiers, vk, false });
}

void HotkeyManager::dispatch(HotkeyAction action) {
    // Execute the built-in effect first, then notify external subscribers.
    switch (action) {
    case HotkeyAction::ToggleOverlays: {
        auto& mgr = aura::taskbar::IconOverlayManager::getInstance();
        // AnimationEnabled acts as the master switch for the overlay system.
        // getRenderStats().frameCount > 0 means overlays are active.
        static bool overlaysOn = true;
        overlaysOn = !overlaysOn;
        mgr.setAnimationEnabled(overlaysOn);
        aura::logging::Logger::getInstance().info("hotkey",
            overlaysOn ? "Overlays enabled" : "Overlays disabled");
        break;
    }
    case HotkeyAction::ToggleVisualizer: {
        auto& viz = aura::visual::AudioVisualizerOverlay::getInstance();
        if (viz.isVisible()) viz.hide();
        else                 viz.show();
        aura::logging::Logger::getInstance().info("hotkey",
            viz.isVisible() ? "Visualizer shown" : "Visualizer hidden");
        break;
    }
    case HotkeyAction::CycleTheme: {
        // WorkspaceManager cycling: apply the default theme then rotate.
        // Future: maintain an index into a user-defined theme list.
        auto& wm = aura::context::WorkspaceManager::getInstance();
        aura::app::ThemeConfig next = wm.getDefaultTheme();
        // Simple toggle: invert glowEnabled as a visible change signal.
        next.glowEnabled = !next.glowEnabled;
        wm.setDefaultTheme(next);
        aura::logging::Logger::getInstance().info("hotkey", "Theme cycled (glowEnabled toggled)");
        break;
    }
    }

    // Notify external subscribers.
    std::vector<HotkeyCallback> snapshot;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        snapshot = m_callbacks;
    }
    for (auto& cb : snapshot) {
        try { cb(action); } catch (...) {}
    }
}

} // namespace aura::system
