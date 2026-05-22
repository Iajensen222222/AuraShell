#pragma once

#include <functional>
#include <mutex>
#include <vector>
#include <windows.h>

namespace aura::system {

enum class HotkeyAction : uint32_t {
    ToggleOverlays   = 1,  // Win+Shift+A — show/hide all taskbar overlays
    ToggleVisualizer = 2,  // Win+Shift+V — show/hide audio visualizer
    CycleTheme       = 3,  // Win+Shift+T — cycle WorkspaceManager themes
};

// Registers Win32 global hotkeys on a message HWND and dispatches them to
// subscriber callbacks. Failure-tolerant: if a hotkey is owned by another
// application, a WARN is logged and initialization continues.
//
// Wire-up: call initialize(m_hwnd) from ShellIntegration::initialize(), and
// route WM_HOTKEY through handleMessage() in ShellIntegration::wndProc().
class HotkeyManager {
public:
    static HotkeyManager& getInstance();

    // Register all default hotkeys on msgHwnd. Returns true if at least one
    // hotkey was registered successfully.
    bool initialize(HWND msgHwnd);

    // UnregisterHotKey for every registered hotkey.
    void shutdown();

    bool isInitialized() const;

    // Call from the message loop when msg == WM_HOTKEY.
    // Returns true if the message was consumed.
    bool handleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // Register a callback invoked on the message thread when a hotkey fires.
    using HotkeyCallback = std::function<void(HotkeyAction)>;
    void subscribeAction(HotkeyCallback cb);

    // Override a specific hotkey's modifiers+VK (call before initialize()).
    void setBinding(HotkeyAction action, UINT modifiers, UINT vk);

    // Trigger a built-in effect directly (without a registered hotkey).
    // Used by ShellIntegration tray menu to share the same code path as WM_HOTKEY.
    void dispatch(HotkeyAction action);

private:
    HotkeyManager() = default;
    ~HotkeyManager() { shutdown(); }
    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    struct Binding {
        HotkeyAction action;
        UINT         id;        // RegisterHotKey id (equals static_cast<int>(action))
        UINT         modifiers; // MOD_WIN | MOD_SHIFT etc.
        UINT         vk;        // Virtual-key code
        bool         registered{false};
    };

    HWND                     m_hwnd{};
    std::vector<Binding>     m_bindings;
    std::vector<HotkeyCallback> m_callbacks;
    mutable std::mutex       m_mutex;
    bool                     m_initialized{false};
};

} // namespace aura::system
