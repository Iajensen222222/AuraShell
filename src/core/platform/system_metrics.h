#pragma once

#include <cstdint>
#include <vector>
#include <windows.h>

namespace aura::platform {

/**
 * @class SystemMetrics
 * @brief Retrieves Windows system metrics and monitor information
 * 
 * Provides singleton access to:
 * - Screen dimensions (primary and virtual)
 * - Monitor enumeration and properties
 * - Work area (excluding taskbar)
 * 
 * Metrics are cached and updated on construction.
 */
class SystemMetrics {
public:
    /// Get singleton instance
    static SystemMetrics& getInstance();

    // Primary Screen Dimensions

    /// Get primary screen width in pixels
    uint32_t getPrimaryScreenWidth() const;

    /// Get primary screen height in pixels
    uint32_t getPrimaryScreenHeight() const;

    // Virtual Screen Dimensions (all monitors combined)

    /// Get virtual screen width (top-left to bottom-right across all monitors)
    uint32_t getVirtualScreenWidth() const;

    /// Get virtual screen height (top-left to bottom-right across all monitors)
    uint32_t getVirtualScreenHeight() const;

    // Monitor Enumeration

    /// Get number of connected monitors
    uint32_t getMonitorCount() const;

    /// Enumerate all connected monitors (in order)
    std::vector<HMONITOR> enumMonitors() const;

    /// Get bounding rectangle for a specific monitor
    /// Returns {0, 0, 0, 0} on error
    RECT getMonitorRect(HMONITOR hMonitor) const;

    // Work Area (excluding taskbar)

    /// Get taskbar rectangle (typically at bottom of primary screen)
    /// Returns {0, 0, 0, 0} if not found
    RECT getTaskbarRect() const;

    /// Get work area of primary screen (screen minus taskbar)
    RECT getPrimaryWorkArea() const;

private:
    SystemMetrics();
    ~SystemMetrics() = default;

    // Prevent copying
    SystemMetrics(const SystemMetrics&) = delete;
    SystemMetrics& operator=(const SystemMetrics&) = delete;

    void updateMetrics();

    // Member variables
    uint32_t m_primaryWidth;
    uint32_t m_primaryHeight;
    uint32_t m_virtualWidth;
    uint32_t m_virtualHeight;
    mutable std::vector<HMONITOR> m_monitors;
};

} // namespace aura::platform
