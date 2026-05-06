#pragma once

#include <Windows.h>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <cstdint>

namespace aura::taskbar {

// ============================================================================
// Data Structures
// ============================================================================

struct TaskbarIconInfo {
    uint32_t index;                    // Position in taskbar (0 = leftmost)
    HWND targetWindowHwnd;             // Associated window handle
    std::wstring appName;              // Application name / label
    RECT iconRect;                     // Bounding rectangle in screen coords
    uint32_t dpi;                      // DPI at icon location (for scaling)
    bool isVisible;                    // Icon currently visible
    bool isPinned;                     // Pinned vs. running app
};

struct TaskbarState {
    HWND taskbarHwnd;                  // Shell_TrayWnd handle
    RECT taskbarRect;                  // Full taskbar bounding box
    uint32_t taskbarDpi;               // Taskbar monitor's DPI
    bool isVisible;                    // Taskbar is visible (not minimized)
    bool isAutoHideActive;             // Auto-hide mode active
    std::vector<TaskbarIconInfo> icons;// Icon information
    uint64_t lastUpdateTimeMs;         // Last refresh timestamp
    uint64_t lastEventTimeMs;          // When last WM_SETTINGCHANGE received
};

// ============================================================================
// Callback Signatures
// ============================================================================

using TaskbarStateChangeCallback = std::function<void(const TaskbarState&)>;
using TaskbarVisibilityChangeCallback = std::function<void(bool visible)>;

// ============================================================================
// TaskbarController - Hybrid Monitoring Engine
// ============================================================================

class TaskbarController {
public:
    // Singleton access
    static TaskbarController& getInstance();

    // Initialization (call once on app startup)
    void initialize();
    void shutdown();

    // State queries (thread-safe, shared_mutex protects against background thread)
    TaskbarState getCurrentState() const;
    HWND getTaskbarWindowHandle() const;
    RECT getTaskbarRect() const;
    uint32_t getTaskbarDpi() const;
    bool isTaskbarVisible() const;
    bool isTaskbarAutoHidden() const;

    // Icon enumeration (cached, refreshed on state change)
    const std::vector<TaskbarIconInfo>& getTaskbarIcons() const;
    TaskbarIconInfo* findIconAtPosition(int x, int y) const;
    TaskbarIconInfo* findIconByWindow(HWND hwnd) const;

    // Cache management
    void refreshIconCache();           // Manually trigger icon re-enumeration
    void clearCache();                 // Clear all cached state
    uint64_t getLastUpdateTime() const;

    // Observer pattern (for state change notifications)
    uint32_t registerStateChangeCallback(TaskbarStateChangeCallback cb);
    uint32_t registerVisibilityChangeCallback(TaskbarVisibilityChangeCallback cb);
    void unregisterCallback(uint32_t callbackId);

    // Window message handling (call from main message loop)
    // Returns true if message was handled
    bool handleWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    TaskbarController();
    ~TaskbarController();

    // Prevent copying
    TaskbarController(const TaskbarController&) = delete;
    TaskbarController& operator=(const TaskbarController&) = delete;

    // Internal methods (called from background thread)
    void updateTaskbarState();
    void enumerateTaskbarIcons();
    void detectStateChanges();
    void notifyStateChangeCallbacks(const TaskbarState& newState);
    void notifyVisibilityChangeCallbacks(bool visible);

    // Background monitoring thread
    void monitoringThreadProc();

    // Window message callback
    static LRESULT CALLBACK windowMessageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Helper methods for icon enumeration
    bool tryEnumerateViaWin32Api(std::vector<TaskbarIconInfo>& out);
    bool tryEnumerateViaUiAutomation(std::vector<TaskbarIconInfo>& out);

    // Helper for detecting auto-hide state
    bool detectAutoHideState() const;

    // ========================================================================
    // Member Variables - Thread-Safe State
    // ========================================================================

    mutable std::shared_mutex m_stateMutex;
    TaskbarState m_currentState;
    TaskbarState m_previousState;

    // Background thread management
    std::atomic<bool> m_running{false};
    std::thread m_monitoringThread;

    // Icon cache (protected by mutex, invalidated by atomic flag)
    mutable std::shared_mutex m_iconCacheMutex;
    std::vector<TaskbarIconInfo> m_iconCache;
    std::atomic<bool> m_iconCacheDirty{false};

    // Callbacks (observer pattern)
    std::unordered_map<uint32_t, TaskbarStateChangeCallback> m_stateChangeCallbacks;
    std::unordered_map<uint32_t, TaskbarVisibilityChangeCallback> m_visibilityChangeCallbacks;
    uint32_t m_nextCallbackId{1};
    std::shared_mutex m_callbacksMutex;

    // Message window for receiving shell update events
    HWND m_messageWindowHwnd{nullptr};

    // Last queried state for change detection
    bool m_previouslyVisible{true};
};

} // namespace aura::taskbar
