#pragma once

#include <cstdint>
#include <windows.h>

namespace aura::platform {

/**
 * @class DpiAwareness
 * @brief Manages Windows Per-Monitor DPI awareness (PerMonitorV2)
 * 
 * Provides utilities for:
 * - Enabling PerMonitorV2 DPI awareness (Windows 10+)
 * - Retrieving DPI values for monitors and windows
 * - Computing DPI scale factors
 * - Scaling pixel values and rectangles
 * 
 * Static class - all methods are static.
 */
class DpiAwareness {
public:
    /// Enable PerMonitorV2 DPI awareness for the application
    /// Safe to call multiple times
    static void enablePerMonitorV2();

    // DPI Retrieval

    /// Get DPI value for a specific monitor (96, 120, 144, etc.)
    /// Returns 96 on error (100% zoom)
    static uint32_t getDpiForMonitor(HMONITOR hMonitor);

    /// Get DPI value for the monitor containing a window
    /// Returns 96 on error
    static uint32_t getDpiForWindow(HWND hwnd);

    // Scale Factor Retrieval (normalized to 96 DPI = 1.0)

    /// Get scale factor for a monitor (96 DPI = 1.0, 192 DPI = 2.0)
    static float getScaleFactor(HMONITOR hMonitor);

    /// Get scale factor for the monitor containing a window
    static float getScaleFactorForWindow(HWND hwnd);

    // Pixel/Rect Scaling Utilities

    /// Scale pixel value by DPI scale factor
    /// scaledPixels = pixels * scale
    static int scalePixelsX(int pixels, float scale);

    /// Scale pixel value by DPI scale factor (Y axis)
    static int scalePixelsY(int pixels, float scale);

    /// Scale a RECT by the given scale factor
    static RECT scaleRect(const RECT& rect, float scale);

private:
    DpiAwareness() = delete;
    ~DpiAwareness() = delete;

    static bool s_perMonitorV2Enabled;
};

} // namespace aura::platform
