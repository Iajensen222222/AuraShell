#include "shell_integration.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include "icon_overlay_manager.h"
#include "audio_visualizer.h"
#include "hotkey_manager.h"
#include "theme_preset_loader.h"
#include "workspace_manager.h"
#include "taskbar_controller.h"
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

    AppendMenuW(hMenu, MF_STRING, IDM_OPEN, L"Open AuraShell Config");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // "Themes ▶" — built from the current ThemePresetLoader catalog.
    HMENU hThemes = CreatePopupMenu();
    if (hThemes) {
        buildThemesSubMenu(hThemes);
        AppendMenuW(hMenu, MF_POPUP,
                    reinterpret_cast<UINT_PTR>(hThemes), L"Themes");
    }
    AppendMenuW(hMenu, MF_STRING, IDM_NEXT_THEME, L"Next theme\tWin+Shift+T");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // "Monitor overlays ▶" — one item per detected monitor.
    HMENU hMonitors = CreatePopupMenu();
    if (hMonitors) {
        buildMonitorSubMenu(hMonitors);
        AppendMenuW(hMenu, MF_POPUP,
                    reinterpret_cast<UINT_PTR>(hMonitors), L"Monitor overlays");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    }

    wchar_t toggleLabel[48];
    wcscpy_s(toggleLabel, m_overlaysEnabled ? L"Disable overlays" : L"Enable overlays");
    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE, toggleLabel);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT,   L"Exit AuraShell");

    SetForegroundWindow(m_hwnd);
    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(hMenu);
}

void ShellIntegration::buildThemesSubMenu(HMENU parent) {
    auto const& presets = aura::context::ThemePresetLoader::getInstance().getPresets();
    if (presets.empty()) {
        AppendMenuW(parent, MF_STRING | MF_GRAYED, 0, L"(no themes loaded)");
        return;
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(presets.size()); ++i) {
        UINT flags = MF_STRING | (i == m_currentThemeIdx ? MF_CHECKED : 0u);
        AppendMenuW(parent, flags, IDM_THEME_BASE + i,
                    presets[i].themeName.c_str());
    }
}

void ShellIntegration::buildMonitorSubMenu(HMENU parent) {
    auto const states =
        aura::taskbar::TaskbarController::getInstance().getAllMonitorStates();
    if (states.empty()) {
        AppendMenuW(parent, MF_STRING | MF_GRAYED, 0, L"(no monitors detected)");
        return;
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(states.size()) && i < 4u; ++i) {
        wchar_t label[64];
        swprintf_s(label, i == 0 ? L"Monitor %u — Primary" : L"Monitor %u", i + 1u);
        UINT flags = MF_STRING | (m_monitorEnabled[i] ? MF_CHECKED : 0u);
        AppendMenuW(parent, flags, IDM_MONITOR_BASE + i, label);
    }
}

void ShellIntegration::onThemeSelected(uint32_t idx) {
    auto const& presets = aura::context::ThemePresetLoader::getInstance().getPresets();
    if (idx >= static_cast<uint32_t>(presets.size())) return;

    m_currentThemeIdx = idx;
    aura::context::WorkspaceManager::getInstance().setDefaultTheme(presets[idx]);

    int const n = WideCharToMultiByte(CP_UTF8, 0,
        presets[idx].themeName.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string name(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, presets[idx].themeName.c_str(), -1,
                            &name[0], n, nullptr, nullptr);
    aura::logging::Logger::getInstance().info("shell", "Tray: theme → " + name);
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

    case WM_COMMAND: {
        UINT const id = LOWORD(wp);

        // Theme sub-menu range: IDM_THEME_BASE + preset index.
        if (id >= IDM_THEME_BASE && id < IDM_THEME_BASE + 256u) {
            onThemeSelected(id - IDM_THEME_BASE);
            return 0;
        }

        // Monitor sub-menu range: toggle primary overlay (per-monitor in GAP-5).
        if (id >= IDM_MONITOR_BASE && id < IDM_MONITOR_BASE + 16u) {
            m_overlaysEnabled = !m_overlaysEnabled;
            aura::taskbar::IconOverlayManager::getInstance()
                .setAnimationEnabled(m_overlaysEnabled);
            return 0;
        }

        switch (id) {
        case IDM_OPEN: {
            wchar_t selfPath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
            std::wstring path(selfPath);
            auto slash = path.rfind(L'\\');
            if (slash != std::wstring::npos)
                path = path.substr(0, slash + 1) + L"AuraConfig.exe";
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOW);
            break;
        }
        case IDM_NEXT_THEME:
            // Delegate to HotkeyManager so both tray and Win+Shift+T share
            // the same cycling index and WorkspaceManager call.
            HotkeyManager::getInstance().dispatch(HotkeyAction::CycleTheme);
            break;
        case IDM_TOGGLE:
            m_overlaysEnabled = !m_overlaysEnabled;
            aura::taskbar::IconOverlayManager::getInstance()
                .setAnimationEnabled(m_overlaysEnabled);
            break;
        case IDM_EXIT:
            destroyTrayIcon();
            PostQuitMessage(0);
            break;
        default:
            break;
        }
        return 0;
    }

    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace aura::system
