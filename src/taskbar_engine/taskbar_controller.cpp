#include "taskbar_engine/taskbar_controller.h"

#include <Windows.h>
#include <shellapi.h>
#include <UIAutomation.h>
#include <comdef.h>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>

#include "core/logging/logger.h"
#include "platform/dpi_awareness.h"
#include "platform/system_metrics.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")

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
        AURA_LOG_INFO("taskbar", "TaskbarController::initialize()");

        // Perform initial taskbar state update
        updateTaskbarState();

        // Create message window for receiving WM_SETTINGCHANGE events
        // We use a simple approach with a class name and register if needed
        WNDCLASS wc = {};
        wc.lpfnWndProc = windowMessageProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"AuraShellTaskbarMessageWindow";

        RegisterClass(&wc);

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
            AURA_LOG_WARN("taskbar", "Failed to create message window");
        }

        // Start background monitoring thread
        m_running = true;
        m_monitoringThread = std::thread(&TaskbarController::monitoringThreadProc, this);

        AURA_LOG_INFO("taskbar", "TaskbarController initialized successfully");
    } catch (const std::exception& e) {
        AURA_LOG_ERROR("taskbar", "Initialize failed: {}", e.what());
        m_running = false;
    }
}

void TaskbarController::shutdown() {
    if (!m_running) {
        return;  // Not running
    }

    try {
        AURA_LOG_INFO("taskbar", "TaskbarController::shutdown()");

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

        AURA_LOG_INFO("taskbar", "TaskbarController shutdown complete");
    } catch (const std::exception& e) {
        AURA_LOG_ERROR("taskbar", "Shutdown failed: {}", e.what());
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
    if (msg == WM_SETTINGCHANGE || msg == WM_DISPLAYCHANGE) {
        // Mark cache as dirty for immediate refresh
        m_iconCacheDirty = true;

        // Record the event time
        {
            std::unique_lock<std::shared_mutex> lock(m_stateMutex);
            m_currentState.lastEventTimeMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()
                ).count();
        }

        AURA_LOG_DEBUG("taskbar", "Received shell update message: {}", msg);
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
        AURA_LOG_DEBUG("taskbar", "Background monitoring thread started");

        while (m_running) {
            // Perform shallow check: verify HWND and position validity
            HWND current = FindWindowW(L"Shell_TrayWnd", nullptr);

            {
                std::unique_lock<std::shared_mutex> lock(m_stateMutex);

                if (current != m_currentState.taskbarHwnd) {
                    // HWND changed (shell restart?)
                    AURA_LOG_WARN("taskbar", "Shell_TrayWnd HWND changed, performing full refresh");
                    lock.unlock();
                    updateTaskbarState();
                    lock.lock();
                } else if (m_currentState.taskbarHwnd != nullptr) {
                    // Verify window still exists
                    if (!IsWindow(m_currentState.taskbarHwnd)) {
                        AURA_LOG_WARN("taskbar", "Shell_TrayWnd became invalid, performing full refresh");
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

        AURA_LOG_DEBUG("taskbar", "Background monitoring thread stopped");
    } catch (const std::exception& e) {
        AURA_LOG_ERROR("taskbar", "Background thread exception: {}", e.what());
    }
}

// ============================================================================
// State Update & Change Detection
// ============================================================================

void TaskbarController::updateTaskbarState() {
    try {
        TaskbarState newState = {};

        // Find taskbar window
        HWND taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", nullptr);

        if (!taskbar_hwnd) {
            AURA_LOG_WARN("taskbar", "Shell_TrayWnd not found");
            return;  // Shell unavailable, keep previous state
        }

        newState.taskbarHwnd = taskbar_hwnd;

        // Get taskbar rectangle
        RECT rect = {};
        if (!GetWindowRect(taskbar_hwnd, &rect)) {
            AURA_LOG_WARN("taskbar", "Failed to get taskbar window rect");
            return;
        }

        newState.taskbarRect = rect;

        // Get taskbar visibility
        newState.isVisible = IsWindowVisible(taskbar_hwnd) == TRUE;

        // Get DPI using DpiAwareness module
        HMONITOR hMonitor = MonitorFromWindow(taskbar_hwnd, MONITOR_DEFAULTTOPRIMARY);
        newState.taskbarDpi = aura::platform::DpiAwareness::getDpiForMonitor(hMonitor);

        // Detect auto-hide state
        newState.isAutoHideActive = detectAutoHideState();

        // Update timestamp
        newState.lastUpdateTimeMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()
            ).count();

        // Update state atomically
        {
            std::unique_lock<std::shared_mutex> lock(m_stateMutex);
            m_previousState = m_currentState;
            m_currentState = newState;
        }

        // Detect state changes and notify callbacks
        detectStateChanges();

    } catch (const std::exception& e) {
        AURA_LOG_ERROR("taskbar", "updateTaskbarState failed: {}", e.what());
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
            AURA_LOG_DEBUG("taskbar", "State change detected (geometry={}, dpi={}, autohide={})",
                           geometry_changed, dpi_changed, autohide_changed);
            lock.unlock();
            notifyStateChangeCallbacks(m_currentState);
            lock.lock();
        }

    } catch (const std::exception& e) {
        AURA_LOG_ERROR("taskbar", "detectStateChanges failed: {}", e.what());
    }
}

// ============================================================================
// Icon Enumeration (Hybrid Win32 + UI Automation)
// ============================================================================

void TaskbarController::enumerateTaskbarIcons() {
    try {
        std::vector<TaskbarIconInfo> newIcons;

        // Strategy 1: Try Win32 API (faster, Windows 11 specific)
        bool successWin32 = tryEnumerateViaWin32Api(newIcons);

        // Strategy 2: Fallback to UI Automation if Win32 fails
        if (!successWin32 || newIcons.empty()) {
            AURA_LOG_DEBUG("taskbar", "Falling back to UI Automation for icon enumeration");
            tryEnumerateViaUiAutomation(newIcons);
        }

        // Update cache
        {
            std::unique_lock<std::shared_mutex> lock(m_iconCacheMutex);
            m_iconCache = newIcons;
        }

        AURA_LOG_DEBUG("taskbar", "Icon enumeration complete: {} icons found", newIcons.size());

    } catch (const std::exception& e) {
        AURA_LOG_ERROR("taskbar", "Icon enumeration failed: {}", e.what());
    }
}

bool TaskbarController::tryEnumerateViaWin32Api(std::vector<TaskbarIconInfo>& out) {
    try {
        HWND taskbar_hwnd = getTaskbarWindowHandle();
        if (!taskbar_hwnd || !IsWindow(taskbar_hwnd)) {
            return false;
        }

        // Find ReBar control
        HWND hReBar = FindWindowExW(taskbar_hwnd, nullptr, L"ReBarWindow32", nullptr);
        if (!hReBar) {
            AURA_LOG_DEBUG("taskbar", "ReBar not found");
            return false;
        }

        // Find toolbar
        HWND hToolbar = FindWindowExW(hReBar, nullptr, L"ToolbarWindow32", nullptr);
        if (!hToolbar) {
            AURA_LOG_DEBUG("taskbar", "Toolbar not found");
            return false;
        }

        // Query button count
        int button_count = static_cast<int>(SendMessage(hToolbar, TB_BUTTONCOUNT, 0, 0));
        AURA_LOG_DEBUG("taskbar", "Found {} toolbar buttons", button_count);

        uint32_t index = 0;

        for (int i = 0; i < button_count; ++i) {
            RECT btn_rect = {};
            SendMessage(hToolbar, TB_GETITEMRECT, i, (LPARAM)&btn_rect);

            // Convert to screen coordinates
            MapWindowPoints(hToolbar, HWND_DESKTOP, (LPPOINT)&btn_rect, 2);

            TaskbarIconInfo icon = {};
            icon.index = index++;
            icon.iconRect = btn_rect;
            icon.dpi = getTaskbarDpi();
            icon.isVisible = true;
            icon.isPinned = false;
            icon.appName = L"Unknown";

            out.push_back(icon);
        }

        return true;

    } catch (const std::exception& e) {
        AURA_LOG_DEBUG("taskbar", "Win32 icon enumeration failed: {}", e.what());
        return false;
    }
}

bool TaskbarController::tryEnumerateViaUiAutomation(std::vector<TaskbarIconInfo>& out) {
    try {
        // UI Automation approach - more reliable but slower
        IUIAutomation* pAutomation = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IUIAutomation, (void**)&pAutomation);

        if (FAILED(hr) || !pAutomation) {
            AURA_LOG_DEBUG("taskbar", "Failed to create UI Automation: 0x{:08X}", hr);
            return false;
        }

        // Get taskbar HWND
        HWND taskbar_hwnd = getTaskbarWindowHandle();
        if (!taskbar_hwnd) {
            pAutomation->Release();
            return false;
        }

        // Get UI element for taskbar
        IUIAutomationElement* pTaskbarElement = nullptr;
        hr = pAutomation->ElementFromHandle(taskbar_hwnd, &pTaskbarElement);

        if (FAILED(hr) || !pTaskbarElement) {
            AURA_LOG_DEBUG("taskbar", "Failed to get taskbar element: 0x{:08X}", hr);
            pAutomation->Release();
            return false;
        }

        // For now, just add a placeholder
        // Full implementation would walk the element tree and extract button positions
        TaskbarIconInfo placeholder = {};
        placeholder.index = 0;
        placeholder.dpi = getTaskbarDpi();
        placeholder.isVisible = true;
        placeholder.appName = L"SystemTray";
        out.push_back(placeholder);

        pTaskbarElement->Release();
        pAutomation->Release();

        return true;

    } catch (const std::exception& e) {
        AURA_LOG_DEBUG("taskbar", "UI Automation enumeration failed: {}", e.what());
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

        AURA_LOG_DEBUG("taskbar", "Auto-hide state: {}", is_autohide ? "active" : "inactive");

        return is_autohide;

    } catch (const std::exception& e) {
        AURA_LOG_DEBUG("taskbar", "Auto-hide detection failed: {}", e.what());
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
                AURA_LOG_ERROR("taskbar", "State change callback #{} failed: {}", id, e.what());
            }
        }

    } catch (const std::exception& e) {
        AURA_LOG_ERROR("taskbar", "notifyStateChangeCallbacks failed: {}", e.what());
    }
}

void TaskbarController::notifyVisibilityChangeCallbacks(bool visible) {
    try {
        std::shared_lock<std::shared_mutex> lock(m_callbacksMutex);

        for (auto& [id, callback] : m_visibilityChangeCallbacks) {
            try {
                callback(visible);
            } catch (const std::exception& e) {
                AURA_LOG_ERROR("taskbar", "Visibility change callback #{} failed: {}", id, e.what());
            }
        }

    } catch (const std::exception& e) {
        AURA_LOG_ERROR("taskbar", "notifyVisibilityChangeCallbacks failed: {}", e.what());
    }
}

} // namespace aura::taskbar
