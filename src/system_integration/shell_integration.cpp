#include "shell_integration.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include "icon_overlay_manager.h"
#include "audio_visualizer.h"
#include "hotkey_manager.h"
#include "logging/logger.h"

#pragma comment(lib, "shell32.lib")

namespace aura::system {

ShellIntegration& ShellIntegration::getInstance() {
    static ShellIntegration instance;
    return instance;
}

bool ShellIntegration::initialize(HINSTANCE hInstance) {
    if (m_initialized) return true;
    m_hInstance = hInstance;

    // Register the WM_TASKBARCREATED message before the window exists,
    // so we never miss the notification if explorer restarts quickly.
    m_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // Create a message-only window (HWND_MESSAGE parent) — no taskbar entry,
    // no paint events, purely an event sink.
    WNDCLASSEXW wc     = {};
    wc.cbSize          = sizeof(wc);
    wc.lpfnWndProc     = ShellIntegration::staticWndProc;
    wc.hInstance       = hInstance;
    wc.lpszClassName   = kClass;
    RegisterClassExW(&wc); // OK to fail if already registered

    m_hwnd = CreateWindowExW(0, kClass, nullptr, WS_OVERLAPPED,
                             0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, this);
    if (!m_hwnd) {
        aura::logging::Logger::getInstance().error("shell",
            "ShellIntegration: CreateWindowExW failed " + std::to_string(GetLastError()));
        return false;
    }

    createTrayIcon();
    onPowerStatusChange(); // snapshot initial power state

    // Register global hotkeys on the same message-only HWND.
    HotkeyManager::getInstance().initialize(m_hwnd);

    m_initialized = true;
    aura::logging::Logger::getInstance().info("shell", "ShellIntegration initialized");
    return true;
}

void ShellIntegration::shutdown() {
    if (!m_initialized) return;
    HotkeyManager::getInstance().shutdown();
    destroyTrayIcon();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    UnregisterClassW(kClass, m_hInstance);
    m_initialized = false;
}

bool ShellIntegration::isInitialized() const { return m_initialized; }

void ShellIntegration::runMessageLoop() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool ShellIntegration::isOnBattery() const {
    return m_onBattery.load(std::memory_order_relaxed);
}

bool ShellIntegration::isBatteryLow() const {
    return m_batteryLow.load(std::memory_order_relaxed);
}

// ============================================================================
// Tray icon
// ============================================================================

void ShellIntegration::createTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hwnd;
    nid.uID              = TRAY_ICON_ID;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = LoadIconW(nullptr, IDI_APPLICATION); // default icon for now
    wcscpy_s(nid.szTip, L"AuraShell");

    Shell_NotifyIconW(NIM_ADD, &nid);

    // Opt into the Vista+ balloon / quiet-hours API.
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void ShellIntegration::destroyTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hwnd;
    nid.uID    = TRAY_ICON_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShellIntegration::showContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    wchar_t toggleLabel[64];
    wcscpy_s(toggleLabel, m_overlaysEnabled ? L"Disable Overlays" : L"Enable Overlays");

    AppendMenuW(hMenu, MF_STRING, IDM_OPEN,   L"Open AuraShell Config");
    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE, toggleLabel);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT,   L"Exit AuraShell");

    // Required for TrackPopupMenu to dismiss when clicking outside.
    SetForegroundWindow(m_hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}

// ============================================================================
// Event handlers
// ============================================================================

void ShellIntegration::onTaskbarCreated() {
    aura::logging::Logger::getInstance().info("shell",
        "WM_TASKBARCREATED — re-anchoring overlay windows");
    // Re-anchor all overlay windows to the recreated taskbar.
    aura::taskbar::IconOverlayManager::getInstance().updateOverlayPositions();
    // Re-add the tray icon — it disappears when explorer.exe restarts.
    createTrayIcon();
}

void ShellIntegration::onPowerStatusChange() {
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) return;

    bool onBattery = (sps.ACLineStatus == 0);
    bool low       = onBattery && (sps.BatteryLifePercent != 255) &&
                     (sps.BatteryLifePercent < 20);

    m_onBattery.store(onBattery, std::memory_order_relaxed);
    m_batteryLow.store(low,      std::memory_order_relaxed);

    // Dim the audio visualizer when battery is low to reduce GPU load.
    aura::visual::AudioVisualizerOverlay::getInstance().setBrightness(low ? 0.5f : 1.0f);

    if (onBattery) {
        aura::logging::Logger::getInstance().info("shell",
            low ? "Battery low (<20%) — reduced brightness"
                : "On battery — normal operation");
    }
}

// ============================================================================
// Window procedure
// ============================================================================

LRESULT CALLBACK ShellIntegration::staticWndProc(HWND hwnd, UINT msg,
                                                   WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<ShellIntegration*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->wndProc(hwnd, msg, wp, lp)
               : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ShellIntegration::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Route global hotkeys before the standard switch.
    if (HotkeyManager::getInstance().handleMessage(msg, wp, lp))
        return 0;

    // WM_TASKBARCREATED is a dynamically registered message — compare at runtime.
    if (msg == m_wmTaskbarCreated) {
        onTaskbarCreated();
        return 0;
    }

    switch (msg) {
    case WM_POWERBROADCAST:
        if (wp == PBT_APMPOWERSTATUSCHANGE || wp == PBT_APMRESUMESUSPEND)
            onPowerStatusChange();
        return TRUE;

    case WM_TRAY:
        // NOTIFYICON_VERSION_4 packs event in LOWORD(lp).
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            showContextMenu();
            break;
        case WM_LBUTTONDBLCLK:
            // Double-click tray icon → open config.
            PostMessageW(hwnd, WM_COMMAND, IDM_OPEN, 0);
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_OPEN: {
            wchar_t selfPath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
            // Replace service exe name with AuraConfig.exe in the same directory.
            std::wstring path(selfPath);
            auto slash = path.rfind(L'\\');
            if (slash != std::wstring::npos)
                path = path.substr(0, slash + 1) + L"AuraConfig.exe";
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOW);
            break;
        }
        case IDM_TOGGLE:
            m_overlaysEnabled = !m_overlaysEnabled;
            aura::taskbar::IconOverlayManager::getInstance()
                .setAnimationEnabled(m_overlaysEnabled);
            break;
        case IDM_EXIT:
            destroyTrayIcon();
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace aura::system
