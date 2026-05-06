#include "system_metrics.h"

#include <windows.h>

namespace aura::platform {

// Static singleton instance
static SystemMetrics* g_instance = nullptr;

// Monitor enumeration callback
static BOOL CALLBACK monitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    (void)hdcMonitor;       // Unused
    (void)lprcMonitor;      // Unused

    if (dwData == 0) {
        return FALSE;
    }

    std::vector<HMONITOR>* monitors = reinterpret_cast<std::vector<HMONITOR>*>(dwData);
    monitors->push_back(hMonitor);

    return TRUE;  // Continue enumeration
}

SystemMetrics::SystemMetrics()
    : m_primaryWidth(0),
      m_primaryHeight(0),
      m_virtualWidth(0),
      m_virtualHeight(0),
      m_monitors() {
    updateMetrics();
}

SystemMetrics& SystemMetrics::getInstance() {
    if (g_instance == nullptr) {
        g_instance = new SystemMetrics();
    }
    return *g_instance;
}

void SystemMetrics::updateMetrics() {
    try {
        // Get primary screen dimensions
        m_primaryWidth = GetSystemMetrics(SM_CXSCREEN);
        m_primaryHeight = GetSystemMetrics(SM_CYSCREEN);

        // Get virtual screen dimensions (all monitors)
        m_virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        m_virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        // Enumerate monitors
        m_monitors.clear();
        EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, reinterpret_cast<LPARAM>(&m_monitors));

    } catch (...) {
        // Set safe defaults
        m_primaryWidth = 1920;
        m_primaryHeight = 1080;
        m_virtualWidth = 1920;
        m_virtualHeight = 1080;
        m_monitors.clear();
    }
}

uint32_t SystemMetrics::getPrimaryScreenWidth() const {
    return m_primaryWidth > 0 ? m_primaryWidth : 1920;
}

uint32_t SystemMetrics::getPrimaryScreenHeight() const {
    return m_primaryHeight > 0 ? m_primaryHeight : 1080;
}

uint32_t SystemMetrics::getVirtualScreenWidth() const {
    return m_virtualWidth > 0 ? m_virtualWidth : 1920;
}

uint32_t SystemMetrics::getVirtualScreenHeight() const {
    return m_virtualHeight > 0 ? m_virtualHeight : 1080;
}

uint32_t SystemMetrics::getMonitorCount() const {
    return static_cast<uint32_t>(m_monitors.size());
}

std::vector<HMONITOR> SystemMetrics::enumMonitors() const {
    return m_monitors;
}

RECT SystemMetrics::getMonitorRect(HMONITOR hMonitor) const {
    if (hMonitor == nullptr) {
        return {0, 0, 0, 0};
    }

    try {
        MONITORINFO monitorInfo = {};
        monitorInfo.cbSize = sizeof(MONITORINFO);

        if (GetMonitorInfoW(hMonitor, &monitorInfo)) {
            return monitorInfo.rcMonitor;
        }
    } catch (...) {
        // Return empty rect on error
    }

    return {0, 0, 0, 0};
}

RECT SystemMetrics::getTaskbarRect() const {
    HWND taskbarWindow = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (taskbarWindow == nullptr) {
        return {0, 0, 0, 0};
    }

    try {
        RECT taskbarRect;
        if (GetWindowRect(taskbarWindow, &taskbarRect)) {
            return taskbarRect;
        }
    } catch (...) {
        // Return empty rect on error
    }

    return {0, 0, 0, 0};
}

RECT SystemMetrics::getPrimaryWorkArea() const {
    RECT workArea = {0, 0, 0, 0};

    try {
        // Get primary monitor
        HMONITOR primaryMonitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (primaryMonitor == nullptr) {
            return workArea;
        }

        MONITORINFO monitorInfo = {};
        monitorInfo.cbSize = sizeof(MONITORINFO);

        if (GetMonitorInfoW(primaryMonitor, &monitorInfo)) {
            // rcWork is the work area (screen minus taskbar)
            workArea = monitorInfo.rcWork;
        }
    } catch (...) {
        // Return empty rect on error
    }

    return workArea;
}

} // namespace aura::platform
