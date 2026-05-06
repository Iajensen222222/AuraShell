#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <windows.h>

#include "platform/dpi_awareness.h"

// ============================================================================
// TESTS: DPI Awareness Initialization
// ============================================================================

TEST_CASE("DpiAwareness::Initialization", "[platform][dpi]") {
    using namespace aura::platform;

    SECTION("enablePerMonitorV2 completes successfully") {
        // Should not throw or crash
        DpiAwareness::enablePerMonitorV2();
        REQUIRE(true);
    }

    SECTION("enablePerMonitorV2 can be called multiple times") {
        DpiAwareness::enablePerMonitorV2();
        DpiAwareness::enablePerMonitorV2();
        DpiAwareness::enablePerMonitorV2();

        // Should be safe to call repeatedly
        REQUIRE(true);
    }
}

// ============================================================================
// TESTS: DPI Values
// ============================================================================

TEST_CASE("DpiAwareness::DpiRetrieval", "[platform][dpi]") {
    using namespace aura::platform;

    DpiAwareness::enablePerMonitorV2();

    SECTION("getDpiForWindow returns standard DPI values") {
        HWND desktopWindow = GetDesktopWindow();
        uint32_t dpi = DpiAwareness::getDpiForWindow(desktopWindow);

        // Standard DPI values: 96, 120, 144, 192, etc.
        // Allow range 96-480 (up to 500% zoom)
        REQUIRE(dpi >= 96);
        REQUIRE(dpi <= 480);

        // Should be multiple of 12 (standard Windows DPI increments)
        REQUIRE(dpi % 12 == 0);
    }

    SECTION("getDpiForMonitor returns standard DPI values") {
        HMONITOR primaryMonitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        uint32_t dpi = DpiAwareness::getDpiForMonitor(primaryMonitor);

        REQUIRE(dpi >= 96);
        REQUIRE(dpi <= 480);
        REQUIRE(dpi % 12 == 0);
    }

    SECTION("getDpiForWindow and getDpiForMonitor consistent") {
        HWND desktopWindow = GetDesktopWindow();
        HMONITOR monitor = MonitorFromWindow(desktopWindow, MONITOR_DEFAULTTONULL);

        uint32_t dpiWindow = DpiAwareness::getDpiForWindow(desktopWindow);
        uint32_t dpiMonitor = DpiAwareness::getDpiForMonitor(monitor);

        // Should match (desktop is on primary monitor)
        REQUIRE(dpiWindow == dpiMonitor);
    }
}

// ============================================================================
// TESTS: DPI Scale Factors
// ============================================================================

TEST_CASE("DpiAwareness::ScaleFactors", "[platform][dpi]") {
    using namespace aura::platform;

    DpiAwareness::enablePerMonitorV2();

    SECTION("getScaleFactorForWindow returns normalized value") {
        HWND desktopWindow = GetDesktopWindow();
        float scale = DpiAwareness::getScaleFactorForWindow(desktopWindow);

        // Scale factor: 96 DPI = 1.0, 192 DPI = 2.0, etc.
        REQUIRE(scale >= 1.0f);
        REQUIRE(scale <= 5.0f);
    }

    SECTION("getScaleFactor returns normalized value") {
        HMONITOR primaryMonitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        float scale = DpiAwareness::getScaleFactor(primaryMonitor);

        REQUIRE(scale >= 1.0f);
        REQUIRE(scale <= 5.0f);
    }

    SECTION("Scale factor = DPI / 96.0") {
        HWND desktopWindow = GetDesktopWindow();
        uint32_t dpi = DpiAwareness::getDpiForWindow(desktopWindow);
        float scale = DpiAwareness::getScaleFactorForWindow(desktopWindow);

        float expectedScale = dpi / 96.0f;

        REQUIRE_THAT(scale, Catch::Matchers::WithinAbs(expectedScale, 0.01f));
    }

    SECTION("96 DPI results in 1.0 scale factor") {
        // This is approximate - depends on system DPI
        // But we can verify the ratio
        HWND desktopWindow = GetDesktopWindow();
        float scale = DpiAwareness::getScaleFactorForWindow(desktopWindow);

        // At 96 DPI (100%), scale should be 1.0
        if (DpiAwareness::getDpiForWindow(desktopWindow) == 96) {
            REQUIRE_THAT(scale, Catch::Matchers::WithinAbs(1.0f, 0.01f));
        }
    }
}

// ============================================================================
// TESTS: DPI Pixel Scaling
// ============================================================================

TEST_CASE("DpiAwareness::PixelScaling", "[platform][dpi]") {
    using namespace aura::platform;

    DpiAwareness::enablePerMonitorV2();

    SECTION("scalePixelsX multiplies by scale factor") {
        float scale = 2.0f;
        int scaledPixels = DpiAwareness::scalePixelsX(100, scale);

        REQUIRE(scaledPixels == 200);
    }

    SECTION("scalePixelsY multiplies by scale factor") {
        float scale = 1.5f;
        int scaledPixels = DpiAwareness::scalePixelsY(100, scale);

        REQUIRE(scaledPixels == 150);
    }

    SECTION("Scaling preserves aspect ratio") {
        float scale = 1.5f;
        int scaledX = DpiAwareness::scalePixelsX(100, scale);
        int scaledY = DpiAwareness::scalePixelsY(100, scale);

        REQUIRE(scaledX == scaledY);
    }

    SECTION("Scale factor of 1.0 preserves pixels") {
        int scaledX = DpiAwareness::scalePixelsX(123, 1.0f);
        int scaledY = DpiAwareness::scalePixelsY(456, 1.0f);

        REQUIRE(scaledX == 123);
        REQUIRE(scaledY == 456);
    }
}

// ============================================================================
// TESTS: DPI Rectangle Scaling
// ============================================================================

TEST_CASE("DpiAwareness::RectScaling", "[platform][dpi]") {
    using namespace aura::platform;

    DpiAwareness::enablePerMonitorV2();

    SECTION("scaleRect scales all dimensions") {
        RECT original = {100, 100, 200, 200};
        float scale = 2.0f;

        RECT scaled = DpiAwareness::scaleRect(original, scale);

        REQUIRE(scaled.left == 200);
        REQUIRE(scaled.top == 200);
        REQUIRE(scaled.right == 400);
        REQUIRE(scaled.bottom == 400);
    }

    SECTION("scaleRect preserves rectangle dimensions") {
        RECT original = {50, 100, 150, 250};
        float scale = 1.5f;

        RECT scaled = DpiAwareness::scaleRect(original, scale);

        int origWidth = original.right - original.left;
        int origHeight = original.bottom - original.top;
        int scaledWidth = scaled.right - scaled.left;
        int scaledHeight = scaled.bottom - scaled.top;

        REQUIRE(scaledWidth == (int)(origWidth * scale));
        REQUIRE(scaledHeight == (int)(origHeight * scale));
    }

    SECTION("scaleRect with scale 1.0 preserves rectangle") {
        RECT original = {10, 20, 110, 120};

        RECT scaled = DpiAwareness::scaleRect(original, 1.0f);

        REQUIRE(scaled.left == original.left);
        REQUIRE(scaled.top == original.top);
        REQUIRE(scaled.right == original.right);
        REQUIRE(scaled.bottom == original.bottom);
    }
}

// ============================================================================
// TESTS: Multiple Monitors (if available)
// ============================================================================

TEST_CASE("DpiAwareness::MultiMonitor", "[platform][dpi]") {
    using namespace aura::platform;

    DpiAwareness::enablePerMonitorV2();

    SECTION("Different monitors may have different DPI values") {
        HMONITOR primaryMonitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);

        // Find a secondary monitor if it exists
        std::vector<HMONITOR> monitors;
        EnumDisplayMonitors(nullptr, nullptr,
            [](HMONITOR hMon, HDC, LPRECT, LPARAM lp) -> BOOL {
                reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(hMon);
                return TRUE;
            }, (LPARAM)&monitors);

        if (monitors.size() >= 2) {
            HMONITOR monitor1 = monitors[0];
            HMONITOR monitor2 = monitors[1];

            uint32_t dpi1 = DpiAwareness::getDpiForMonitor(monitor1);
            uint32_t dpi2 = DpiAwareness::getDpiForMonitor(monitor2);

            // Both should be valid DPI values
            REQUIRE(dpi1 >= 96);
            REQUIRE(dpi1 <= 480);
            REQUIRE(dpi2 >= 96);
            REQUIRE(dpi2 <= 480);
        } else {
            // Single monitor - DPI should still be retrievable
            uint32_t dpi = DpiAwareness::getDpiForMonitor(primaryMonitor);
            REQUIRE(dpi >= 96);
            REQUIRE(dpi <= 480);
        }
    }
}
