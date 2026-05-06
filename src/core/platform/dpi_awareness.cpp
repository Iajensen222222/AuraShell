#include "dpi_awareness.h"

#include <windows.h>
#include <shellscalingapi.h>

namespace aura::platform {

// Static member initialization
bool DpiAwareness::s_perMonitorV2Enabled = false;

void DpiAwareness::enablePerMonitorV2() {
    if (s_perMonitorV2Enabled) {
        return;  // Already enabled, safe to call multiple times
    }

    try {
        // Try to enable PerMonitorV2 (Windows 10 1607+)
        // SetProcessDpiAwarenessContext requires:
        // - Windows 10 version 1607 or later
        // - The app manifest with appropriate dpiAware setting

        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ((DPI_AWARENESS_CONTEXT)-4)
        // This enables per-monitor DPI awareness for Windows 8.1 and newer
        BOOL result = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        if (result) {
            s_perMonitorV2Enabled = true;
        }
        // If it fails (older Windows), fall back - that's acceptable
    } catch (...) {
        // Silently fail - may not be supported on this OS
    }
}

uint32_t DpiAwareness::getDpiForMonitor(HMONITOR hMonitor) {
    if (hMonitor == nullptr) {
        return 96;  // Default: 100% zoom
    }

    try {
        UINT dpiX = 96;
        UINT dpiY = 96;

        // Try GetDpiForMonitor (Windows 8.1+)
        if (SUCCEEDED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
            return static_cast<uint32_t>(dpiX);
        }

        // Fallback: use GetDeviceCaps
        HDC hdc = GetDC(nullptr);
        if (hdc != nullptr) {
            int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(nullptr, hdc);
            if (dpi > 0) {
                return static_cast<uint32_t>(dpi);
            }
        }

        return 96;  // Default
    } catch (...) {
        return 96;
    }
}

uint32_t DpiAwareness::getDpiForWindow(HWND hwnd) {
    if (hwnd == nullptr) {
        return 96;
    }

    try {
        // Get the monitor containing this window
        HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        return getDpiForMonitor(hMonitor);
    } catch (...) {
        return 96;
    }
}

float DpiAwareness::getScaleFactor(HMONITOR hMonitor) {
    uint32_t dpi = getDpiForMonitor(hMonitor);
    return dpi / 96.0f;
}

float DpiAwareness::getScaleFactorForWindow(HWND hwnd) {
    uint32_t dpi = getDpiForWindow(hwnd);
    return dpi / 96.0f;
}

int DpiAwareness::scalePixelsX(int pixels, float scale) {
    return static_cast<int>(pixels * scale + 0.5f);
}

int DpiAwareness::scalePixelsY(int pixels, float scale) {
    return static_cast<int>(pixels * scale + 0.5f);
}

RECT DpiAwareness::scaleRect(const RECT& rect, float scale) {
    RECT scaled;
    scaled.left = scalePixelsX(rect.left, scale);
    scaled.top = scalePixelsY(rect.top, scale);
    scaled.right = scalePixelsX(rect.right, scale);
    scaled.bottom = scalePixelsY(rect.bottom, scale);
    return scaled;
}

} // namespace aura::platform
