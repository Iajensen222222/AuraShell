#include "window_manager.h"

#include <windows.h>
#include <algorithm>
#include <cctype>

namespace aura::platform {

static WindowManager* g_instance = nullptr;

// Window enumeration callback for EnumWindows
static BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (lParam == 0) {
        return FALSE;
    }

    std::vector<WindowInfo>* windows = reinterpret_cast<std::vector<WindowInfo>*>(lParam);

    // Get window title
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));

    // Get window class
    wchar_t className[256] = {};
    GetClassNameW(hwnd, className, sizeof(className) / sizeof(wchar_t));

    // Check if visible
    BOOL visible = IsWindowVisible(hwnd);

    // Get window rect
    RECT rect = {};
    GetWindowRect(hwnd, &rect);

    if (visible && (title[0] != '\0' || className[0] != '\0')) {
        WindowInfo info;
        info.hwnd = hwnd;
        info.title = std::wstring(title);
        info.className = std::wstring(className);
        info.isVisible = (visible == TRUE);
        info.rect = rect;

        windows->push_back(info);
    }

    return TRUE;
}

WindowManager::WindowManager() : m_windowCache() {
    updateCache();
}

WindowManager& WindowManager::getInstance() {
    if (g_instance == nullptr) {
        g_instance = new WindowManager();
    }
    return *g_instance;
}

void WindowManager::updateCache() {
    try {
        m_windowCache.clear();
        EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&m_windowCache));
    } catch (...) {
        m_windowCache.clear();
    }
}

HWND WindowManager::findWindowByClass(const std::wstring& className) {
    // Update cache if empty
    if (m_windowCache.empty()) {
        updateCache();
    }

    try {
        for (const auto& info : m_windowCache) {
            if (info.className == className) {
                return info.hwnd;
            }
        }
    } catch (...) {
        // Silently fail
    }

    return nullptr;
}

HWND WindowManager::findWindowByTitle(const std::wstring& titlePattern) {
    if (m_windowCache.empty()) {
        updateCache();
    }

    try {
        // Convert pattern to lowercase for case-insensitive comparison
        std::wstring pattern_lower = titlePattern;
        for (auto& c : pattern_lower) {
            if (c >= L'A' && c <= L'Z') {
                c = c - L'A' + L'a';
            }
        }

        for (const auto& info : m_windowCache) {
            std::wstring title_lower = info.title;
            for (auto& c : title_lower) {
                if (c >= L'A' && c <= L'Z') {
                    c = c - L'A' + L'a';
                }
            }

            if (title_lower.find(pattern_lower) != std::wstring::npos) {
                return info.hwnd;
            }
        }
    } catch (...) {
        // Silently fail
    }

    return nullptr;
}

std::vector<HWND> WindowManager::enumAllWindows() {
    if (m_windowCache.empty()) {
        updateCache();
    }

    std::vector<HWND> result;
    try {
        for (const auto& info : m_windowCache) {
            result.push_back(info.hwnd);
        }
    } catch (...) {
        result.clear();
    }

    return result;
}

std::wstring WindowManager::getWindowTitle(HWND hwnd) {
    if (hwnd == nullptr) {
        return L"";
    }

    try {
        wchar_t title[256] = {};
        GetWindowTextW(hwnd, title, sizeof(title) / sizeof(wchar_t));
        return std::wstring(title);
    } catch (...) {
        return L"";
    }
}

std::wstring WindowManager::getWindowClass(HWND hwnd) {
    if (hwnd == nullptr) {
        return L"";
    }

    try {
        wchar_t className[256] = {};
        GetClassNameW(hwnd, className, sizeof(className) / sizeof(wchar_t));
        return std::wstring(className);
    } catch (...) {
        return L"";
    }
}

bool WindowManager::isWindowVisible(HWND hwnd) {
    if (hwnd == nullptr) {
        return false;
    }

    try {
        return (IsWindowVisible(hwnd) == TRUE);
    } catch (...) {
        return false;
    }
}

RECT WindowManager::getWindowRect(HWND hwnd) {
    RECT rect = {0, 0, 0, 0};

    if (hwnd == nullptr) {
        return rect;
    }

    try {
        GetWindowRect(hwnd, &rect);
    } catch (...) {
        // Return empty rect on error
    }

    return rect;
}

void WindowManager::clearCache() {
    try {
        m_windowCache.clear();
        updateCache();
    } catch (...) {
        m_windowCache.clear();
    }
}

} // namespace aura::platform
