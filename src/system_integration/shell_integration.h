#pragma once

#include <atomic>
#include <windows.h>
#include <shellapi.h>

namespace aura::system {

// Handles three critical "glue" events that a real background Windows app needs:
//
//  A) WM_TASKBARCREATED — fired when explorer.exe restarts after a crash.
//     Re-anchors all IconOverlayManager windows to the new taskbar.
//
//  B) System tray icon — Shell_NotifyIcon with right-click context menu
//     (Open config, Toggle overlays, Exit).
//
//  C) WM_POWERBROADCAST — reduces animation brightness when on battery < 20%.
//
// Lifecycle: initialize() creates a message-only HWND, then call runMessageLoop()
// on the main thread (blocks until WM_QUIT).
class ShellIntegration {
public:
    static ShellIntegration& getInstance();

    // Create the message-only HWND, register tray icon, and start listening.
    bool initialize(HINSTANCE hInstance);

    // Remove the tray icon and destroy the HWND.
    void shutdown();

    bool isInitialized() const;

    // Block on GetMessage until WM_QUIT. Call on the main thread after
    // initialize() succeeds. Returns when the user selects "Exit" from the tray.
    void runMessageLoop();

    // Power state queries (updated on every WM_POWERBROADCAST)
    bool isOnBattery() const;
    bool isBatteryLow() const;   // true when battery < 20%

private:
    ShellIntegration() = default;
    ~ShellIntegration() { shutdown(); }
    ShellIntegration(const ShellIntegration&) = delete;
    ShellIntegration& operator=(const ShellIntegration&) = delete;

    void createTrayIcon();
    void destroyTrayIcon();
    void showContextMenu();
    void onTaskbarCreated();
    void onPowerStatusChange();

    static LRESULT CALLBACK staticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND      m_hwnd{};
    HINSTANCE m_hInstance{};
    bool      m_initialized{false};
    bool      m_overlaysEnabled{true};

    // Power state — updated on every WM_POWERBROADCAST
    std::atomic<bool> m_onBattery{false};
    std::atomic<bool> m_batteryLow{false};

    // Registered message IDs
    UINT m_wmTaskbarCreated{0};

    // Tray icon ID and callback message
    static constexpr UINT TRAY_ICON_ID  = 1;
    static constexpr UINT WM_TRAY       = WM_APP + 1;

    // Context menu item IDs
    static constexpr UINT IDM_OPEN      = 1001;
    static constexpr UINT IDM_TOGGLE    = 1002;
    static constexpr UINT IDM_EXIT      = 1003;

    static constexpr wchar_t kClass[]   = L"AuraShell_Shell";
};

} // namespace aura::system
