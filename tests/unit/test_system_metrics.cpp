#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include "system_metrics.h"

using aura::platform::SystemMetrics;

// ============================================================================
// Singleton contract
// ============================================================================
TEST_CASE("SystemMetrics singleton always returns the same instance", "[system_metrics]") {
    SystemMetrics& a = SystemMetrics::getInstance();
    SystemMetrics& b = SystemMetrics::getInstance();
    REQUIRE(&a == &b);
}

// ============================================================================
// Primary screen dimensions
// ============================================================================
TEST_CASE("SystemMetrics primary screen dimensions are valid", "[system_metrics]") {
    SystemMetrics& sm = SystemMetrics::getInstance();

    SECTION("Width is positive and within plausible range") {
        uint32_t w = sm.getPrimaryScreenWidth();
        REQUIRE(w > 0u);
        REQUIRE(w <= 16384u); // Largest commercially available display width (2024)
    }

    SECTION("Height is positive and within plausible range") {
        uint32_t h = sm.getPrimaryScreenHeight();
        REQUIRE(h > 0u);
        REQUIRE(h <= 16384u);
    }

    SECTION("Dimensions agree with Win32 GetSystemMetrics") {
        int w32w = GetSystemMetrics(SM_CXSCREEN);
        int w32h = GetSystemMetrics(SM_CYSCREEN);
        REQUIRE(static_cast<int>(sm.getPrimaryScreenWidth())  == w32w);
        REQUIRE(static_cast<int>(sm.getPrimaryScreenHeight()) == w32h);
    }
}

// ============================================================================
// Virtual screen dimensions (all monitors combined)
// ============================================================================
TEST_CASE("SystemMetrics virtual screen dimensions span all monitors", "[system_metrics]") {
    SystemMetrics& sm = SystemMetrics::getInstance();

    uint32_t vw = sm.getVirtualScreenWidth();
    uint32_t vh = sm.getVirtualScreenHeight();
    uint32_t pw = sm.getPrimaryScreenWidth();
    uint32_t ph = sm.getPrimaryScreenHeight();

    SECTION("Virtual screen is at least as wide as the primary") {
        REQUIRE(vw >= pw);
    }

    SECTION("Virtual screen is at least as tall as the primary") {
        REQUIRE(vh >= ph);
    }

    SECTION("Virtual width agrees with Win32 SM_CXVIRTUALSCREEN") {
        int w32vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        REQUIRE(static_cast<int>(vw) == w32vw);
    }

    SECTION("Virtual height agrees with Win32 SM_CYVIRTUALSCREEN") {
        int w32vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        REQUIRE(static_cast<int>(vh) == w32vh);
    }
}

// ============================================================================
// Monitor enumeration
// ============================================================================
TEST_CASE("SystemMetrics monitor enumeration returns at least one monitor", "[system_metrics]") {
    SystemMetrics& sm = SystemMetrics::getInstance();

    SECTION("Monitor count is at least 1") {
        REQUIRE(sm.getMonitorCount() >= 1u);
    }

    SECTION("enumMonitors returns the same count as getMonitorCount") {
        auto monitors = sm.enumMonitors();
        REQUIRE(static_cast<uint32_t>(monitors.size()) == sm.getMonitorCount());
    }

    SECTION("All returned HMONITOR handles are non-null") {
        auto monitors = sm.enumMonitors();
        for (HMONITOR hMon : monitors) {
            REQUIRE(hMon != nullptr);
        }
    }

    SECTION("Monitor count agrees with Win32 GetSystemMetrics SM_CMONITORS") {
        int w32count = GetSystemMetrics(SM_CMONITORS);
        REQUIRE(static_cast<int>(sm.getMonitorCount()) == w32count);
    }
}

// ============================================================================
// Monitor rectangles
// ============================================================================
TEST_CASE("SystemMetrics getMonitorRect returns valid RECT for each monitor", "[system_metrics]") {
    SystemMetrics& sm = SystemMetrics::getInstance();
    auto monitors = sm.enumMonitors();

    for (std::size_t i = 0; i < monitors.size(); ++i) {
        HMONITOR hMon = monitors[i];
        RECT r = sm.getMonitorRect(hMon);

        INFO("Monitor " << i << ": left=" << r.left << " top=" << r.top
             << " right=" << r.right << " bottom=" << r.bottom);

        SECTION("Monitor " + std::to_string(i) + " right > left (non-zero width)") {
            REQUIRE(r.right > r.left);
        }

        SECTION("Monitor " + std::to_string(i) + " bottom > top (non-zero height)") {
            REQUIRE(r.bottom > r.top);
        }

        SECTION("Monitor " + std::to_string(i) + " rect agrees with MONITORINFO") {
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            BOOL ok = GetMonitorInfoW(hMon, &mi);
            REQUIRE(ok != FALSE);
            REQUIRE(r.left   == mi.rcMonitor.left);
            REQUIRE(r.top    == mi.rcMonitor.top);
            REQUIRE(r.right  == mi.rcMonitor.right);
            REQUIRE(r.bottom == mi.rcMonitor.bottom);
        }
    }
}

TEST_CASE("SystemMetrics getMonitorRect returns {0,0,0,0} for null handle", "[system_metrics]") {
    SystemMetrics& sm = SystemMetrics::getInstance();
    RECT r = sm.getMonitorRect(nullptr);
    REQUIRE(r.left   == 0);
    REQUIRE(r.top    == 0);
    REQUIRE(r.right  == 0);
    REQUIRE(r.bottom == 0);
}

// ============================================================================
// Taskbar and work area
// ============================================================================
TEST_CASE("SystemMetrics taskbar rect is within primary screen bounds", "[system_metrics]") {
    SystemMetrics& sm = SystemMetrics::getInstance();
    RECT taskbar = sm.getTaskbarRect();

    uint32_t sw = sm.getPrimaryScreenWidth();
    uint32_t sh = sm.getPrimaryScreenHeight();

    // Taskbar must have non-zero area
    bool hasArea = (taskbar.right > taskbar.left) && (taskbar.bottom > taskbar.top);

    if (hasArea) {
        // If a taskbar is found, its coordinates must fit within the primary screen
        REQUIRE(taskbar.left   >= 0);
        REQUIRE(taskbar.top    >= 0);
        REQUIRE(taskbar.right  <= static_cast<LONG>(sw));
        REQUIRE(taskbar.bottom <= static_cast<LONG>(sh));
    }
    // A result of {0,0,0,0} is acceptable when no taskbar window is found (e.g., in CI)
}

TEST_CASE("SystemMetrics primary work area fits within primary screen", "[system_metrics]") {
    SystemMetrics& sm = SystemMetrics::getInstance();
    RECT wa = sm.getPrimaryWorkArea();

    uint32_t sw = sm.getPrimaryScreenWidth();
    uint32_t sh = sm.getPrimaryScreenHeight();

    SECTION("Work area left edge >= 0") {
        REQUIRE(wa.left >= 0);
    }

    SECTION("Work area top edge >= 0") {
        REQUIRE(wa.top >= 0);
    }

    SECTION("Work area right edge <= primary screen width") {
        REQUIRE(wa.right <= static_cast<LONG>(sw));
    }

    SECTION("Work area bottom edge <= primary screen height") {
        REQUIRE(wa.bottom <= static_cast<LONG>(sh));
    }

    SECTION("Work area has non-zero area") {
        REQUIRE(wa.right  > wa.left);
        REQUIRE(wa.bottom > wa.top);
    }

    SECTION("Work area agrees with Win32 SystemParametersInfo SPI_GETWORKAREA") {
        RECT w32wa{};
        BOOL ok = SystemParametersInfoW(SPI_GETWORKAREA, 0, &w32wa, 0);
        REQUIRE(ok != FALSE);
        REQUIRE(wa.left   == w32wa.left);
        REQUIRE(wa.top    == w32wa.top);
        REQUIRE(wa.right  == w32wa.right);
        REQUIRE(wa.bottom == w32wa.bottom);
    }

    SECTION("Work area is smaller than full screen when taskbar is present") {
        RECT taskbar = sm.getTaskbarRect();
        bool taskbarFound = (taskbar.right > taskbar.left) && (taskbar.bottom > taskbar.top);
        if (taskbarFound) {
            uint32_t screenArea = sw * sh;
            uint32_t workArea   = static_cast<uint32_t>(wa.right  - wa.left) *
                                  static_cast<uint32_t>(wa.bottom - wa.top);
            REQUIRE(workArea < screenArea);
        }
    }
}
