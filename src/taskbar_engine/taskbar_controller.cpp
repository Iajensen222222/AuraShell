#include "taskbar_controller.h"

#include <Windows.h>
#include <shellapi.h>
#include <UIAutomation.h>
#include <comdef.h>
#include <commctrl.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>

#include "logging/logger.h"
#include "dpi_awareness.h"
#include "system_metrics.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")

namespace {

// ============================================================================
// Multi-monitor taskbar discovery callback (Phase 3.5)
// Windows 11 uses Shell_TrayWnd for the primary taskbar and
// Shell_SecondaryTrayWnd for taskbars on secondary monitors.
// Windows 10 uses Shell_TrayWnd for ALL taskbars (multiple instances).
// ============================================================================

struct TaskbarEnumCtx {
    std::vector<HWND> found;
};

BOOL CALLBACK enumTaskbarHwndsProc(HWND const hwnd, LPARAM const lp) noexcept {
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    if (wcscmp(cls, L"Shell_TrayWnd") == 0 ||
        wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
        reinterpret_cast<TaskbarEnumCtx*>(lp)->found.push_back(hwnd);
    }
    return TRUE;  // continue enumeration
}

}  // anonymous namespace

namespace aura::taskbar {

// ============================================================================
// Singleton Implementation
// ============================================================================

TaskbarController& TaskbarController::getInstance() {
    static TaskbarController instance;
    return instance;
}

// ============================================================================
// Constructor & Destructor
// ============================================================================

TaskbarController::TaskbarController()
    : m_running(false), m_messageWindowHwnd(nullptr), m_nextCallbackId(1), m_previouslyVisible(true) {
    // Initialize state to defaults
    m_currentState = {};
    m_previousState = {};
}

TaskbarController::~TaskbarController() {
    shutdown();
}

// ============================================================================
// Initialization & Lifecycle
// ============================================================================

void TaskbarController::initialize() {
    if (m_running) {
        return;  // Already initialized
    }

    try {
        aura::logging::Logger::getInstance().info("taskbar", "TaskbarController::initialize()");

        // Perform initial taskbar state update
        updateTaskbarState();

        // Mark icon cache as dirty to trigger initial enumeration
        m_iconCacheDirty = true;

        // Create message window for receiving WM_SETTINGCHANGE events
        // We use a simple approach with a class name and register if needed
        static bool windowClassRegistered = false;
        if (!windowClassRegistered) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = windowMessageProc;
            wc.hInstance = GetModuleHandle(nullptr);
            wc.lpszClassName = L"AuraShellTaskbarMessageWindow";
            RegisterClassW(&wc);
            windowClassRegistered = true;
        }

        m_messageWindowHwnd = CreateWindowExW(
            0,
            L"AuraShellTaskbarMessageWindow",
            L"AuraShellTaskbarMessage",
            0,
            0, 0, 0, 0,
            HWND_MESSAGE,  // Message-only window
            nullptr,
            GetModuleHandle(nullptr),
            this
        );

        if (!m_messageWindowHwnd) {
            aura::logging::Logger::getInstance().warn("taskbar", "Failed to create message window");
        }

        // Start background monitoring thread
        m_running = true;
        m_monitoringThread = std::thread(&TaskbarController::monitoringThreadProc, this);

        aura::logging::Logger::getInstance().info("taskbar", "TaskbarController initialized successfully");
    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error("taskbar", std::string("Initialize failed: ") + e.what());
        m_running = false;
    }
}

void TaskbarController::shutdown() {
    if (!m_running) {
        return;  // Not running
    }

    try {
        aura::logging::Logger::getInstance().info("taskbar", "TaskbarController::shutdown()");

        m_running = false;

        // Wait for background thread to finish
        if (m_monitoringThread.joinable()) {
            m_monitoringThread.join();
        }

        // Destroy message window
        if (m_messageWindowHwnd && IsWindow(m_messageWindowHwnd)) {
            DestroyWindow(m_messageWindowHwnd);
            m_messageWindowHwnd = nullptr;
        }

        // Clear callbacks
        {
            std::unique_lock<std::shared_mutex> lock(m_callbacksMutex);
            m_stateChangeCallbacks.clear();
            m_visibilityChangeCallbacks.clear();
        }

        aura::logging::Logger::getInstance().info("taskbar", "TaskbarController shutdown complete");
    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error("taskbar", std::string("Shutdown failed: ") + e.what());
    }
}

// ============================================================================
// State Queries (Thread-Safe)
// ============================================================================

TaskbarState TaskbarController::getCurrentState() const {
    std::shared_lock<std::shared_mutex> lock(m_stateMutex);
    return m_currentState;
}

HWND TaskbarController::getTaskbarWindowHandle() const {
    std::shared_lock<std::shared_mutex> lock(m_stateMutex);
    return m_currentState.taskbarHwnd;
}

RECT TaskbarController::getTaskbarRect() const {
    std::shared_lock<std::shared_mutex> lock(m_stateMutex);
    return m_currentState.taskbarRect;
}

uint32_t TaskbarController::getTaskbarDpi() const {
    std::shared_lock<std::shared_mutex> lock(m_stateMutex);
    return m_currentState.taskbarDpi;
}

bool TaskbarController::isTaskbarVisible() const {
    std::shared_lock<std::shared_mutex> lock(m_stateMutex);
    return m_currentState.isVisible;
}

bool TaskbarController::isTaskbarAutoHidden() const {
    std::shared_lock<std::shared_mutex> lock(m_stateMutex);
    return m_currentState.isAutoHideActive;
}

uint64_t TaskbarController::getLastUpdateTime() const {
    std::shared_lock<std::shared_mutex> lock(m_stateMutex);
    return m_currentState.lastUpdateTimeMs;
}

// ============================================================================
// Multi-Monitor API (Phase 3.5)
// ============================================================================

std::vector<TaskbarState> TaskbarController::getAllMonitorStates() const {
    std::shared_lock<std::shared_mutex> lock(m_allMonitorStatesMutex);
    return m_allMonitorStates;
}

int32_t TaskbarController::getMonitorCount() const {
    std::shared_lock<std::shared_mutex> lock(m_allMonitorStatesMutex);
    return static_cast<int32_t>(m_allMonitorStates.size());
}

// ============================================================================
// Icon Enumeration
// ============================================================================

const std::vector<TaskbarIconInfo>& TaskbarController::getTaskbarIcons() const {
    std::shared_lock<std::shared_mutex> lock(m_iconCacheMutex);
    return m_iconCache;
}

TaskbarIconInfo* TaskbarController::findIconAtPosition(int x, int y) const {
    std::shared_lock<std::shared_mutex> lock(m_iconCacheMutex);

    for (auto& icon : m_iconCache) {
        if (x >= icon.iconRect.left && x <= icon.iconRect.right &&
            y >= icon.iconRect.top && y <= icon.iconRect.bottom) {
            return const_cast<TaskbarIconInfo*>(&icon);
        }
    }

    return nullptr;
}

TaskbarIconInfo* TaskbarController::findIconByWindow(HWND hwnd) const {
    std::shared_lock<std::shared_mutex> lock(m_iconCacheMutex);

    for (auto& icon : m_iconCache) {
        if (icon.targetWindowHwnd == hwnd) {
            return const_cast<TaskbarIconInfo*>(&icon);
        }
    }

    return nullptr;
}

// ============================================================================
// Cache Management
// ============================================================================

void TaskbarController::refreshIconCache() {
    m_iconCacheDirty = true;
}

void TaskbarController::clearCache() {
    {
        std::unique_lock<std::shared_mutex> lock(m_stateMutex);
        m_currentState = {};
        m_previousState = {};
    }
    {
        std::unique_lock<std::shared_mutex> lock(m_iconCacheMutex);
        m_iconCache.clear();
    }
    m_iconCacheDirty = true;

    // Re-fetch state after clearing
    updateTaskbarState();
}

// ============================================================================
// Observer Pattern
// ============================================================================

uint32_t TaskbarController::registerStateChangeCallback(TaskbarStateChangeCallback cb) {
    std::unique_lock<std::shared_mutex> lock(m_callbacksMutex);
    uint32_t id = m_nextCallbackId++;
    m_stateChangeCallbacks[id] = cb;
    return id;
}

uint32_t TaskbarController::registerVisibilityChangeCallback(TaskbarVisibilityChangeCallback cb) {
    std::unique_lock<std::shared_mutex> lock(m_callbacksMutex);
    uint32_t id = m_nextCallbackId++;
    m_visibilityChangeCallbacks[id] = cb;
    return id;
}

void TaskbarController::unregisterCallback(uint32_t callbackId) {
    std::unique_lock<std::shared_mutex> lock(m_callbacksMutex);
    m_stateChangeCallbacks.erase(callbackId);
    m_visibilityChangeCallbacks.erase(callbackId);
}

// ============================================================================
// Window Message Handling
// ============================================================================

bool TaskbarController::handleWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)hwnd;  // Unused
    (void)wParam;  // Unused
    (void)lParam;  // Unused

    if (msg == WM_SETTINGCHANGE || msg == WM_DISPLAYCHANGE) {
        // Mark icon cache dirty so the monitoring thread re-enumerates icons.
        m_iconCacheDirty = true;

        // WM_DISPLAYCHANGE also requires re-discovering all monitor HWNDs and
        // rebuilding m_allMonitorStates — the soft-reset path.
        if (msg == WM_DISPLAYCHANGE) {
            m_displayChangePending = true;
            aura::logging::Logger::getInstance().debug(
                "taskbar", "WM_DISPLAYCHANGE received — soft-reset scheduled"
            );
        }

        // Record the event time.
        {
            std::unique_lock<std::shared_mutex> lock(m_stateMutex);
            m_currentState.lastEventTimeMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()
                ).count();
        }

        aura::logging::Logger::getInstance().debug("taskbar", "Received shell update message");
        return true;
    }

    return false;
}

LRESULT CALLBACK TaskbarController::windowMessageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }

    TaskbarController* pThis = nullptr;

    if (msg == WM_SETTINGCHANGE || msg == WM_DISPLAYCHANGE) {
        pThis = reinterpret_cast<TaskbarController*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (pThis) {
            return pThis->handleWindowMessage(hwnd, msg, wParam, lParam) ? 0 : DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Background Monitoring Thread
// ============================================================================

void TaskbarController::monitoringThreadProc() {
    try {
        // Initialize COM for this thread (required for UI Automation)
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(hr)) {
            aura::logging::Logger::getInstance().warn("taskbar", "Failed to initialize COM for background thread");
        }

        aura::logging::Logger::getInstance().debug("taskbar", "Background monitoring thread started");

        while (m_running) {
            // Soft-reset: WM_DISPLAYCHANGE was received — re-enumerate ALL monitors.
            // This must run on the monitoring thread because it uses COM (UI Automation).
            if (m_displayChangePending.exchange(false)) {
                aura::logging::Logger::getInstance().debug(
                    "taskbar", "Soft-reset: re-enumerating monitors after WM_DISPLAYCHANGE"
                );
                updateTaskbarState();
            }

            // Perform shallow check: verify HWND and position validity
            HWND current = FindWindowW(L"Shell_TrayWnd", nullptr);

            {
                std::unique_lock<std::shared_mutex> lock(m_stateMutex);

                if (current != m_currentState.taskbarHwnd) {
                    // HWND changed (shell restart?)
                    aura::logging::Logger::getInstance().warn("taskbar", "Shell_TrayWnd HWND changed, performing full refresh");
                    lock.unlock();
                    updateTaskbarState();
                    lock.lock();
                } else if (m_currentState.taskbarHwnd != nullptr) {
                    // Verify window still exists
                    if (!IsWindow(m_currentState.taskbarHwnd)) {
                        aura::logging::Logger::getInstance().warn("taskbar", "Shell_TrayWnd became invalid, performing full refresh");
                        lock.unlock();
                        updateTaskbarState();
                        lock.lock();
                    }
                }
            }

            // Check if icon cache needs refresh
            if (m_iconCacheDirty.exchange(false)) {
                enumerateTaskbarIcons();
            }

            // Sleep for heartbeat interval (5 seconds)
            for (int i = 0; i < 50 && m_running; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        aura::logging::Logger::getInstance().debug("taskbar", "Background monitoring thread stopped");

        // Cleanup COM
        CoUninitialize();

    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error("taskbar", std::string("Background thread exception: ") + e.what());
        CoUninitialize();
    }
}

// ============================================================================
// State Update & Change Detection
// ============================================================================

void TaskbarController::updateTaskbarState() {
    try {
        uint64_t const nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();

        // --- Phase 3.5: enumerate ALL taskbar windows (primary + secondary) ---
        TaskbarEnumCtx enumCtx;
        EnumWindows(enumTaskbarHwndsProc, reinterpret_cast<LPARAM>(&enumCtx));

        if (enumCtx.found.empty()) {
            aura::logging::Logger::getInstance().warn("taskbar", "No taskbar windows found via EnumWindows");
            return;
        }

        // Build a TaskbarState for each discovered taskbar HWND.
        bool const autoHide = detectAutoHideState();
        std::vector<TaskbarState> newAllStates;
        newAllStates.reserve(enumCtx.found.size());

        for (HWND const twnd : enumCtx.found) {
            if (!IsWindow(twnd)) continue;

            TaskbarState s = {};
            s.taskbarHwnd = twnd;
            GetWindowRect(twnd, &s.taskbarRect);
            s.isVisible        = IsWindowVisible(twnd) == TRUE;
            s.hMonitor         = MonitorFromWindow(twnd, MONITOR_DEFAULTTONEAREST);
            s.taskbarDpi       = aura::platform::DpiAwareness::getDpiForMonitor(s.hMonitor);
            s.isAutoHideActive = autoHide;
            s.lastUpdateTimeMs = nowMs;

            newAllStates.push_back(s);
        }

        if (newAllStates.empty()) return;

        // Identify the primary state (Shell_TrayWnd class).
        TaskbarState primaryState = newAllStates[0];  // fallback: first found
        for (auto const& s : newAllStates) {
            wchar_t cls[64] = {};
            GetClassNameW(s.taskbarHwnd, cls, 64);
            if (wcscmp(cls, L"Shell_TrayWnd") == 0) {
                primaryState = s;
                break;
            }
        }

        // Commit all-monitor states.
        {
            std::unique_lock<std::shared_mutex> lock(m_allMonitorStatesMutex);
            m_allMonitorStates = newAllStates;
        }

        // Commit primary state (backward-compat with single-monitor callers).
        {
            std::unique_lock<std::shared_mutex> lock(m_stateMutex);
            m_previousState = m_currentState;
            m_currentState  = primaryState;
        }

        // Icon cache needs refresh after monitor re-enumeration.
        m_iconCacheDirty = true;

        aura::logging::Logger::getInstance().debug(
            "taskbar",
            std::string("updateTaskbarState: found ") + std::to_string(newAllStates.size()) + " taskbar(s)"
        );

        detectStateChanges();

    } catch (std::exception const& e) {
        aura::logging::Logger::getInstance().error(
            "taskbar", std::string("updateTaskbarState failed: ") + e.what()
        );
    }
}

void TaskbarController::detectStateChanges() {
    try {
        std::shared_lock<std::shared_mutex> lock(m_stateMutex);

        if (m_currentState.isVisible != m_previousState.isVisible) {
            lock.unlock();
            notifyVisibilityChangeCallbacks(m_currentState.isVisible);
            lock.lock();
        }

        // Check if position, DPI, or other properties changed
        bool geometry_changed =
            (m_currentState.taskbarRect.left != m_previousState.taskbarRect.left) ||
            (m_currentState.taskbarRect.top != m_previousState.taskbarRect.top) ||
            (m_currentState.taskbarRect.right != m_previousState.taskbarRect.right) ||
            (m_currentState.taskbarRect.bottom != m_previousState.taskbarRect.bottom);

        bool dpi_changed = (m_currentState.taskbarDpi != m_previousState.taskbarDpi);
        bool autohide_changed = (m_currentState.isAutoHideActive != m_previousState.isAutoHideActive);

        if (geometry_changed || dpi_changed || autohide_changed) {
            aura::logging::Logger::getInstance().debug("taskbar", std::string("State change detected (geometry=") + std::to_string(geometry_changed) + ", dpi=" + std::to_string(dpi_changed) + ", autohide=" + std::to_string(autohide_changed) + ")");
            lock.unlock();
            notifyStateChangeCallbacks(m_currentState);
            lock.lock();
        }

    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error("taskbar", std::string("detectStateChanges failed: ") + e.what());
    }
}

// ============================================================================
// Icon Enumeration (Hybrid Win32 + UI Automation)
// ============================================================================

void TaskbarController::enumerateTaskbarIcons() {
    try {
        // Snapshot the per-monitor state list without holding m_allMonitorStatesMutex
        // for the duration of potentially slow Win32/UIAutomation calls.
        std::vector<TaskbarState> monitorStates;
        {
            std::shared_lock<std::shared_mutex> lock(m_allMonitorStatesMutex);
            monitorStates = m_allMonitorStates;
        }

        // If no per-monitor states exist yet, fall back to primary only.
        if (monitorStates.empty()) {
            TaskbarState const primary = getCurrentState();
            if (primary.taskbarHwnd) monitorStates.push_back(primary);
        }

        std::vector<TaskbarIconInfo> allIcons;
        uint32_t globalIdx = 0;

        for (TaskbarState const& ms : monitorStates) {
            if (!ms.taskbarHwnd || !IsWindow(ms.taskbarHwnd)) continue;

            std::vector<TaskbarIconInfo> monitorIcons;

            bool const win32Ok = tryEnumerateViaWin32Api(monitorIcons, ms.taskbarHwnd, ms.hMonitor);
            aura::logging::Logger::getInstance().debug(
                "taskbar",
                std::string("Win32 enumeration result: ") + (win32Ok ? "success" : "failed") +
                ", icons: " + std::to_string(monitorIcons.size())
            );

            if (!win32Ok || monitorIcons.empty()) {
                aura::logging::Logger::getInstance().debug("taskbar", "Attempting UI Automation for icon enumeration");
                bool const uiaOk = tryEnumerateViaUiAutomation(monitorIcons, ms.taskbarHwnd, ms.hMonitor);
                aura::logging::Logger::getInstance().debug(
                    "taskbar",
                    std::string("UI Automation result: ") + (uiaOk ? "success" : "failed") +
                    ", icons: " + std::to_string(monitorIcons.size())
                );
            }

            // Placeholder when both strategies yield nothing for this monitor.
            if (monitorIcons.empty()) {
                TaskbarIconInfo ph = {};
                ph.hMonitor    = ms.hMonitor;
                ph.taskbarHwnd = ms.taskbarHwnd;
                ph.dpi         = ms.taskbarDpi;
                ph.isVisible   = true;
                ph.appName     = L"SystemTray";
                ph.iconRect    = {ms.taskbarRect.right - 100, ms.taskbarRect.top,
                                  ms.taskbarRect.right -  20, ms.taskbarRect.bottom};
                monitorIcons.push_back(ph);
            }

            // Assign globally unique sequential indices and propagate monitor tags.
            for (TaskbarIconInfo& icon : monitorIcons) {
                icon.index       = globalIdx++;
                icon.hMonitor    = ms.hMonitor;
                icon.taskbarHwnd = ms.taskbarHwnd;
            }

            allIcons.insert(allIcons.end(), monitorIcons.begin(), monitorIcons.end());
        }

        // Commit flat icon cache (used by getTaskbarIcons() / HoverDetector).
        {
            std::unique_lock<std::shared_mutex> lock(m_iconCacheMutex);
            m_iconCache = allIcons;
        }

        // Propagate all icons into m_currentState.icons so that IconOverlayManager
        // callback receives them directly (avoids the getTaskbarIcons() fallback).
        {
            std::unique_lock<std::shared_mutex> lock(m_stateMutex);
            m_currentState.icons = allIcons;
        }

        aura::logging::Logger::getInstance().debug(
            "taskbar",
            std::string("Icon enumeration complete: ") + std::to_string(allIcons.size()) + " icons found"
        );

        // Notify observers with the updated primary state (which now carries all icons).
        notifyStateChangeCallbacks(getCurrentState());

    } catch (std::exception const& e) {
        aura::logging::Logger::getInstance().error(
            "taskbar", std::string("Icon enumeration failed: ") + e.what()
        );
    }
}

bool TaskbarController::tryEnumerateViaWin32Api(
    std::vector<TaskbarIconInfo>& out,
    HWND const taskbar_hwnd,
    HMONITOR const hMonitor
) {
    try {
        if (!taskbar_hwnd || !IsWindow(taskbar_hwnd)) {
            return false;
        }

        // Windows 11 taskbar structure:
        // Shell_TrayWnd -> various child windows
        // Try multiple paths to find the toolbar

        // Path 1: Shell_TrayWnd -> ReBarWindow32 -> ToolbarWindow32
        HWND hReBar = FindWindowExW(taskbar_hwnd, nullptr, L"ReBarWindow32", nullptr);
        HWND hToolbar = nullptr;

        if (hReBar) {
            hToolbar = FindWindowExW(hReBar, nullptr, L"ToolbarWindow32", nullptr);
            if (hToolbar) {
                aura::logging::Logger::getInstance().debug("taskbar", "Found toolbar via ReBar path");
            }
        }

        // Path 2: Shell_TrayWnd -> TrayNotifyWnd (system tray area)
        if (!hToolbar) {
            HWND hTrayNotify = FindWindowExW(taskbar_hwnd, nullptr, L"TrayNotifyWnd", nullptr);
            if (hTrayNotify) {
                hToolbar = FindWindowExW(hTrayNotify, nullptr, L"ToolbarWindow32", nullptr);
                if (hToolbar) {
                    aura::logging::Logger::getInstance().debug("taskbar", "Found toolbar via TrayNotifyWnd path");
                }
            }
        }

        // Path 3: Direct ToolbarWindow32 search
        if (!hToolbar) {
            hToolbar = FindWindowExW(taskbar_hwnd, nullptr, L"ToolbarWindow32", nullptr);
            if (hToolbar) {
                aura::logging::Logger::getInstance().debug("taskbar", "Found toolbar via direct search");
            }
        }

        if (!hToolbar) {
            aura::logging::Logger::getInstance().debug("taskbar", "Toolbar not found via any Win32 path");
            return false;
        }

        // Query button count
        int button_count = static_cast<int>(SendMessage(hToolbar, TB_BUTTONCOUNT, 0, 0));
        aura::logging::Logger::getInstance().debug("taskbar", std::string("Found ") + std::to_string(button_count) + " toolbar buttons");

        uint32_t index = 0;

        for (int i = 0; i < button_count; ++i) {
            RECT btn_rect = {};
            SendMessage(hToolbar, TB_GETITEMRECT, i, (LPARAM)&btn_rect);

            // Convert to screen coordinates
            MapWindowPoints(hToolbar, HWND_DESKTOP, (LPPOINT)&btn_rect, 2);

            TaskbarIconInfo icon = {};
            icon.index       = index++;
            icon.iconRect    = btn_rect;
            icon.dpi         = aura::platform::DpiAwareness::getDpiForMonitor(hMonitor);
            icon.hMonitor    = hMonitor;
            icon.taskbarHwnd = taskbar_hwnd;
            icon.isVisible   = true;
            icon.isPinned    = false;
            icon.appName     = L"Unknown";

            out.push_back(icon);
        }

        return true;

    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().debug("taskbar", std::string("Win32 icon enumeration failed: ") + e.what());
        return false;
    }
}

bool TaskbarController::tryEnumerateViaUiAutomation(
    std::vector<TaskbarIconInfo>& out,
    HWND const taskbar_hwnd,
    HMONITOR const hMonitor
) {
    try {
        // UI Automation approach - more reliable for Windows 11 XAML taskbar
        // Note: COM must be initialized on this thread before calling this method

        IUIAutomation* pAutomation = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IUIAutomation, (void**)&pAutomation);

        if (FAILED(hr)) {
            aura::logging::Logger::getInstance().debug("taskbar", std::string("CoCreateInstance failed: 0x") + std::to_string(hr));
            return false;
        }

        if (!pAutomation) {
            aura::logging::Logger::getInstance().debug("taskbar", "UI Automation pointer is null");
            return false;
        }

        // Validate the caller-supplied taskbar HWND
        if (!taskbar_hwnd || !IsWindow(taskbar_hwnd)) {
            aura::logging::Logger::getInstance().debug("taskbar", "Taskbar HWND is null or invalid");
            pAutomation->Release();
            return false;
        }

        // Get UI element for this specific taskbar
        IUIAutomationElement* pTaskbarElement = nullptr;
        hr = pAutomation->ElementFromHandle(taskbar_hwnd, &pTaskbarElement);

        if (FAILED(hr) || !pTaskbarElement) {
            aura::logging::Logger::getInstance().debug("taskbar", std::string("Failed to get taskbar element: 0x") + std::to_string(hr));
            pAutomation->Release();
            return false;
        }

        // Successfully got the taskbar element via UI Automation
        // For now, add a system tray icon as a proof-of-concept
        // Full implementation would walk the element tree and extract button positions
        aura::logging::Logger::getInstance().debug("taskbar", "Successfully acquired taskbar element via UI Automation");

        TaskbarIconInfo icon = {};
        icon.index       = 0;
        icon.dpi         = aura::platform::DpiAwareness::getDpiForMonitor(hMonitor);
        icon.hMonitor    = hMonitor;
        icon.taskbarHwnd = taskbar_hwnd;
        icon.isVisible   = true;
        icon.isPinned    = false;
        icon.appName     = L"SystemTray";

        // Position at right edge of this monitor's taskbar (system tray area)
        RECT taskbarRect = {};
        GetWindowRect(taskbar_hwnd, &taskbarRect);
        icon.iconRect = {taskbarRect.right - 100, taskbarRect.top,
                         taskbarRect.right -  20, taskbarRect.bottom};

        out.push_back(icon);

        pTaskbarElement->Release();
        pAutomation->Release();

        return true;

    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().debug("taskbar", std::string("UI Automation enumeration exception: ") + e.what());
        return false;
    }
}

// ============================================================================
// Auto-Hide Detection
// ============================================================================

bool TaskbarController::detectAutoHideState() const {
    try {
        HWND taskbar_hwnd = getTaskbarWindowHandle();
        if (!taskbar_hwnd) {
            return false;
        }

        APPBARDATA abd = {};
        abd.cbSize = sizeof(APPBARDATA);
        abd.hWnd = taskbar_hwnd;

        UINT_PTR result = SHAppBarMessage(ABM_GETSTATE, &abd);

        // ABS_AUTOHIDE = 0x0000001
        bool is_autohide = (result & ABS_AUTOHIDE) != 0;

        aura::logging::Logger::getInstance().debug("taskbar", std::string("Auto-hide state: ") + (is_autohide ? "active" : "inactive"));

        return is_autohide;

    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().debug("taskbar", std::string("Auto-hide detection failed: ") + e.what());
        return false;
    }
}

// ============================================================================
// Callback Notification
// ============================================================================

void TaskbarController::notifyStateChangeCallbacks(const TaskbarState& newState) {
    try {
        std::shared_lock<std::shared_mutex> lock(m_callbacksMutex);

        for (auto& [id, callback] : m_stateChangeCallbacks) {
            try {
                callback(newState);
            } catch (const std::exception& e) {
                aura::logging::Logger::getInstance().error("taskbar", std::string("State change callback failed: ") + e.what());
            }
        }

    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error("taskbar", std::string("notifyStateChangeCallbacks failed: ") + e.what());
    }
}

void TaskbarController::notifyVisibilityChangeCallbacks(bool visible) {
    try {
        std::shared_lock<std::shared_mutex> lock(m_callbacksMutex);

        for (auto& [id, callback] : m_visibilityChangeCallbacks) {
            try {
                callback(visible);
            } catch (const std::exception& e) {
                aura::logging::Logger::getInstance().error("taskbar", std::string("Visibility change callback failed: ") + e.what());
            }
        }

    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error("taskbar", std::string("notifyVisibilityChangeCallbacks failed: ") + e.what());
    }
}

// ============================================================================
// GAP-4: Feature toggles — called from ServiceCore on SET_FEATURES receipt
// ============================================================================

void TaskbarController::setAutoHideEnabled(bool enable) {
    HWND taskbarHwnd;
    {
        std::shared_lock<std::shared_mutex> lk(m_stateMutex);
        taskbarHwnd = m_currentState.taskbarHwnd;
    }

    if (taskbarHwnd) {
        APPBARDATA abd = {};
        abd.cbSize = sizeof(abd);
        abd.hWnd   = taskbarHwnd;
        // Preserve ABS_ALWAYSONTOP; only flip ABS_AUTOHIDE.
        UINT state = static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &abd));
        if (enable) state |=  ABS_AUTOHIDE;
        else        state &= ~static_cast<UINT>(ABS_AUTOHIDE);
        abd.lParam = static_cast<LPARAM>(state);
        SHAppBarMessage(ABM_SETSTATE, &abd);
    }

    m_autoHideEnabled.store(enable, std::memory_order_relaxed);
    aura::logging::Logger::getInstance().info(
        "taskbar", std::string("Auto-hide ") + (enable ? "enabled" : "disabled")
    );
}

void TaskbarController::setMultiMonitorEnabled(bool enable) {
    m_multiMonitorEnabled.store(enable, std::memory_order_relaxed);
    // Trigger a re-scan so the monitor list reflects the new mode immediately.
    m_displayChangePending.store(true, std::memory_order_relaxed);
    aura::logging::Logger::getInstance().info(
        "taskbar", std::string("Multi-monitor ") + (enable ? "enabled" : "disabled")
    );
}

} // namespace aura::taskbar
