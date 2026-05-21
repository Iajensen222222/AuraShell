#include "hotkey_manager.h"

#include <cstdio>
#include <windows.h>

#include "icon_overlay_manager.h"
#include "audio_visualizer.h"
#include "workspace_manager.h"
#include "theme_preset_loader.h"
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
        // Toggle overlays by enabling/disabling animations and forcing all
        // active overlays to alpha=0 (off) or restoring hover state (on).
        static bool overlaysOn = true;
        overlaysOn = !overlaysOn;
        auto& mgr = aura::taskbar::IconOverlayManager::getInstance();
        mgr.setAnimationEnabled(overlaysOn);
        if (!overlaysOn) {
            // Force all overlays invisible immediately.
            uint32_t const n = mgr.getOverlayWindowCount();
            for (uint32_t i = 0; i < n; ++i) {
                HWND hw = mgr.getOverlayWindowForIcon(i);
                if (hw) ShowWindow(hw, SW_HIDE);
            }
        } else {
            // Restore: make windows visible again (alpha-transparent by default).
            uint32_t const n = mgr.getOverlayWindowCount();
            for (uint32_t i = 0; i < n; ++i) {
                HWND hw = mgr.getOverlayWindowForIcon(i);
                if (hw) ShowWindow(hw, SW_SHOWNOACTIVATE);
            }
        }
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
        // Cycle through loaded presets by advancing a static index.
        auto& loader = aura::context::ThemePresetLoader::getInstance();
        auto const& presets = loader.getPresets();
        if (presets.empty()) break;

        static int themeIdx = 0;
        themeIdx = (themeIdx + 1) % static_cast<int>(presets.size());
        auto const& next = presets[static_cast<size_t>(themeIdx)];

        auto& wm = aura::context::WorkspaceManager::getInstance();
        wm.setDefaultTheme(next);

        int const _n = WideCharToMultiByte(CP_UTF8, 0,
            next.themeName.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string _name(static_cast<size_t>(_n > 0 ? _n - 1 : 0), '\0');
        if (_n > 0)
            WideCharToMultiByte(CP_UTF8, 0, next.themeName.c_str(), -1,
                                &_name[0], _n, nullptr, nullptr);
        aura::logging::Logger::getInstance().info("hotkey", "Theme cycled → " + _name);
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
