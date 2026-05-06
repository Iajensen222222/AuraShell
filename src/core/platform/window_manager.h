#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace aura::platform {

/// Information about a window
struct WindowInfo {
    HWND hwnd;
    std::wstring title;
    std::wstring className;
    bool isVisible;
    RECT rect;
};

/**
 * @class WindowManager
 * @brief Manages Windows window enumeration and properties
 * 
 * Singleton for:
 * - Finding windows by class name or title
 * - Enumerating all windows
 * - Retrieving window properties
 * 
 * Uses a cache for performance.
 */
class WindowManager {
public:
    /// Get singleton instance
    static WindowManager& getInstance();

    // Window Search

    /// Find first window with matching class name
    /// Returns nullptr if not found
    HWND findWindowByClass(const std::wstring& className);

    /// Find first window with title containing pattern
    /// Returns nullptr if not found
    HWND findWindowByTitle(const std::wstring& titlePattern);

    /// Enumerate all visible top-level windows
    std::vector<HWND> enumAllWindows();

    // Window Properties

    /// Get window title/caption
    std::wstring getWindowTitle(HWND hwnd);

    /// Get window class name
    std::wstring getWindowClass(HWND hwnd);

    /// Check if window is visible
    bool isWindowVisible(HWND hwnd);

    /// Get window bounding rectangle
    RECT getWindowRect(HWND hwnd);

    // Cache Management

    /// Clear the window cache
    void clearCache();

private:
    WindowManager();
    ~WindowManager() = default;

    // Prevent copying
    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    void updateCache();

    std::vector<WindowInfo> m_windowCache;
};

} // namespace aura::platform
