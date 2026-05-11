// Phase 3.5 TDD — Multi-Monitor & Shell Polish
// Tests are partitioned into three classes:
//   1. ALWAYS PASS  — pure math or API-surface checks that work on any hardware
//   2. CONDITIONAL  — wrapped in if (tc.getMonitorCount() >= 2), skipped on single-monitor CI
//   3. SOFT-RESET   — WM_DISPLAYCHANGE re-enumeration path

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <thread>
#include <chrono>
#include <set>

#include <Windows.h>

#include "taskbar_controller.h"
#include "hover_detector.h"
#include "dpi_awareness.h"
#include "system_metrics.h"

using namespace aura::taskbar;
using namespace aura::platform;

// ============================================================================
// Fixture — initialises the full stack and tears it down cleanly
// ============================================================================

struct MultiMonitorFixture {
    MultiMonitorFixture() {
        tc = &TaskbarController::getInstance();
        tc->initialize();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));  // let bg thread enumerate
    }
    ~MultiMonitorFixture() {
        if (tc) tc->shutdown();
    }
    TaskbarController* tc = nullptr;
};

// ============================================================================
// 1. ALWAYS PASS — Data Structure Fields
// ============================================================================

TEST_CASE("MultiMonitor::DataStructureFields", "[multimonitor][api]") {
    // Verify the new monitor-awareness fields exist and have sane defaults.
    // These are compile-time checks — if the struct lacks the field, this TU
    // fails to build (which IS the RED signal before implementation).

    SECTION("TaskbarIconInfo carries hMonitor and taskbarHwnd fields") {
        TaskbarIconInfo icon = {};
        icon.hMonitor    = nullptr;
        icon.taskbarHwnd = nullptr;
        REQUIRE(icon.hMonitor    == nullptr);
        REQUIRE(icon.taskbarHwnd == nullptr);
    }

    SECTION("TaskbarState carries hMonitor field") {
        TaskbarState state = {};
        state.hMonitor = nullptr;
        REQUIRE(state.hMonitor == nullptr);
    }
}

// ============================================================================
// 2. ALWAYS PASS — TaskbarController Multi-Monitor API Surface
// ============================================================================

TEST_CASE("MultiMonitor::TaskbarControllerAPI", "[multimonitor][api]") {
    MultiMonitorFixture fixture;

    SECTION("getMonitorCount returns at least 1") {
        int32_t const count = fixture.tc->getMonitorCount();
        REQUIRE(count >= 1);
    }

    SECTION("getAllMonitorStates returns at least 1 state") {
        auto const states = fixture.tc->getAllMonitorStates();
        REQUIRE(states.size() >= 1);
    }

    SECTION("getAllMonitorStates count matches getMonitorCount") {
        auto const states   = fixture.tc->getAllMonitorStates();
        int32_t const count = fixture.tc->getMonitorCount();
        REQUIRE(static_cast<int32_t>(states.size()) == count);
    }

    SECTION("primary state HWND is always present in getAllMonitorStates") {
        HWND const primaryHwnd = fixture.tc->getTaskbarWindowHandle();
        if (primaryHwnd) {
            auto const states = fixture.tc->getAllMonitorStates();
            bool found = false;
            for (auto const& s : states) {
                if (s.taskbarHwnd == primaryHwnd) { found = true; break; }
            }
            REQUIRE(found == true);
        }
    }

    SECTION("each monitor state has a non-null hMonitor") {
        auto const states = fixture.tc->getAllMonitorStates();
        for (auto const& s : states) {
            REQUIRE(s.hMonitor != nullptr);
        }
    }

    SECTION("each monitor state has a valid taskbar HWND") {
        auto const states = fixture.tc->getAllMonitorStates();
        for (auto const& s : states) {
            if (s.taskbarHwnd) {
                REQUIRE(IsWindow(s.taskbarHwnd) == TRUE);
            }
        }
    }

    SECTION("monitor handles in getAllMonitorStates are distinct") {
        auto const states = fixture.tc->getAllMonitorStates();
        std::set<HMONITOR> seen;
        for (auto const& s : states) {
            REQUIRE(seen.find(s.hMonitor) == seen.end());
            seen.insert(s.hMonitor);
        }
    }
}

// ============================================================================
// 3. ALWAYS PASS — Icon Monitor Tagging
// ============================================================================

TEST_CASE("MultiMonitor::IconMonitorTagging", "[multimonitor][icons]") {
    MultiMonitorFixture fixture;

    SECTION("every icon in getTaskbarIcons has a non-null hMonitor") {
        auto const& icons = fixture.tc->getTaskbarIcons();
        for (auto const& icon : icons) {
            REQUIRE(icon.hMonitor != nullptr);
        }
    }

    SECTION("every icon's hMonitor matches a state in getAllMonitorStates") {
        auto const& icons  = fixture.tc->getTaskbarIcons();
        auto const  states = fixture.tc->getAllMonitorStates();

        std::set<HMONITOR> knownMonitors;
        for (auto const& s : states) knownMonitors.insert(s.hMonitor);

        for (auto const& icon : icons) {
            REQUIRE(knownMonitors.count(icon.hMonitor) == 1);
        }
    }

    SECTION("icon indices are unique across all monitors") {
        auto const& icons = fixture.tc->getTaskbarIcons();
        std::set<uint32_t> seen;
        for (auto const& icon : icons) {
            REQUIRE(seen.find(icon.index) == seen.end());
            seen.insert(icon.index);
        }
    }

    SECTION("icon rects are in screen coordinate space (positive or reasonable)") {
        auto const& icons = fixture.tc->getTaskbarIcons();
        int const virtualScreenLeft  = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int const virtualScreenTop   = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int const virtualScreenRight = virtualScreenLeft + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int const virtualScreenBottom = virtualScreenTop + GetSystemMetrics(SM_CYVIRTUALSCREEN);

        for (auto const& icon : icons) {
            // Icon rect must be within the virtual screen bounds
            REQUIRE(icon.iconRect.left   >= virtualScreenLeft  - 50);
            REQUIRE(icon.iconRect.right  <= virtualScreenRight + 50);
            REQUIRE(icon.iconRect.top    >= virtualScreenTop   - 50);
            REQUIRE(icon.iconRect.bottom <= virtualScreenBottom + 50);
        }
    }
}

// ============================================================================
// 4. ALWAYS PASS — DPI Scale Factor Math
// ============================================================================

TEST_CASE("MultiMonitor::DpiScaleMath", "[multimonitor][dpi]") {
    // Pure arithmetic — no hardware required.

    SECTION("96 DPI = 1.0x scale") {
        float const scale = DpiAwareness::getScaleFactor(96);
        REQUIRE(scale == Catch::Approx(1.0f).epsilon(0.01f));
    }

    SECTION("120 DPI = 1.25x scale") {
        float const scale = DpiAwareness::getScaleFactor(120);
        REQUIRE(scale == Catch::Approx(1.25f).epsilon(0.01f));
    }

    SECTION("144 DPI = 1.5x scale") {
        float const scale = DpiAwareness::getScaleFactor(144);
        REQUIRE(scale == Catch::Approx(1.5f).epsilon(0.01f));
    }

    SECTION("192 DPI = 2.0x scale") {
        float const scale = DpiAwareness::getScaleFactor(192);
        REQUIRE(scale == Catch::Approx(2.0f).epsilon(0.01f));
    }

    SECTION("screen rect on 150 percent monitor: physical px = logical × scale") {
        // Given a logical rect (what a 96-DPI app sees) and 150% DPI scale,
        // verify the physical pixel rect that appears on screen.
        float const dpiScale = 1.5f;
        RECT const logicalRect  = {100, 50, 148, 98};  // a 48×48 logical icon
        RECT const physicalRect = {
            static_cast<LONG>(logicalRect.left   * dpiScale),
            static_cast<LONG>(logicalRect.top    * dpiScale),
            static_cast<LONG>(logicalRect.right  * dpiScale),
            static_cast<LONG>(logicalRect.bottom * dpiScale)
        };
        REQUIRE(physicalRect.left   == 150);
        REQUIRE(physicalRect.top    == 75);
        REQUIRE(physicalRect.right  == 222);
        REQUIRE(physicalRect.bottom == 147);
    }

    SECTION("primary monitor DPI is non-zero") {
        MultiMonitorFixture fixture;
        REQUIRE(fixture.tc->getTaskbarDpi() > 0);
    }
}

// ============================================================================
// 5. ALWAYS PASS — Virtual Screen Coordinate Mapping
// ============================================================================

TEST_CASE("MultiMonitor::VirtualScreenCoordinates", "[multimonitor][coordinates]") {
    SECTION("GetCursorPos returns virtual screen coordinates") {
        POINT p = {};
        BOOL const ok = GetCursorPos(&p);
        REQUIRE(ok == TRUE);
        // Coordinates are valid if within the virtual screen bounds
        int const vsLeft  = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int const vsTop   = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int const vsRight = vsLeft + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int const vsBottom = vsTop + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        REQUIRE(p.x >= vsLeft);
        REQUIRE(p.x <= vsRight);
        REQUIRE(p.y >= vsTop);
        REQUIRE(p.y <= vsBottom);
    }

    SECTION("MonitorFromPoint on primary origin returns a valid monitor") {
        HMONITOR const hMon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        REQUIRE(hMon != nullptr);
    }

    SECTION("MapWindowPoints to HWND_DESKTOP yields virtual screen coords") {
        // This is the same conversion tryEnumerateViaWin32Api uses for icon rects.
        // Verify with a trivial zero-point case.
        POINT pt = {0, 0};
        HWND const desktop = GetDesktopWindow();
        MapWindowPoints(desktop, HWND_DESKTOP, &pt, 1);
        // After mapping desktop→desktop, coordinates should be unchanged
        REQUIRE(pt.x == 0);
        REQUIRE(pt.y == 0);
    }

    SECTION("SystemMetrics enumerates at least one monitor") {
        SystemMetrics& sm = SystemMetrics::getInstance();
        auto const monitors = sm.enumMonitors();
        REQUIRE(monitors.size() >= 1);
    }
}

// ============================================================================
// 6. ALWAYS PASS — Taskbar HWND Discovery (Class Names)
// ============================================================================

TEST_CASE("MultiMonitor::TaskbarHwndDiscovery", "[multimonitor][discovery]") {
    SECTION("FindWindowW finds Shell_TrayWnd") {
        HWND const hwnd = FindWindowW(L"Shell_TrayWnd", nullptr);
        REQUIRE(hwnd != nullptr);
        REQUIRE(IsWindow(hwnd) == TRUE);
    }

    SECTION("all monitor states have expected taskbar class names") {
        MultiMonitorFixture fixture;
        auto const states = fixture.tc->getAllMonitorStates();
        for (auto const& s : states) {
            if (!s.taskbarHwnd) continue;
            wchar_t cls[64] = {};
            GetClassNameW(s.taskbarHwnd, cls, 64);
            bool const isKnownTaskbar =
                (wcscmp(cls, L"Shell_TrayWnd") == 0) ||
                (wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0);
            REQUIRE(isKnownTaskbar == true);
        }
    }

    SECTION("primary monitor state uses Shell_TrayWnd class") {
        MultiMonitorFixture fixture;
        auto const states = fixture.tc->getAllMonitorStates();
        bool foundPrimary = false;
        for (auto const& s : states) {
            if (!s.taskbarHwnd) continue;
            wchar_t cls[64] = {};
            GetClassNameW(s.taskbarHwnd, cls, 64);
            if (wcscmp(cls, L"Shell_TrayWnd") == 0) {
                foundPrimary = true;
                break;
            }
        }
        REQUIRE(foundPrimary == true);
    }
}

// ============================================================================
// 7. SOFT-RESET — WM_DISPLAYCHANGE Re-enumeration
// ============================================================================

TEST_CASE("MultiMonitor::SoftReset", "[multimonitor][softReset]") {
    MultiMonitorFixture fixture;

    SECTION("refreshIconCache triggers re-enumeration without crash") {
        // refreshIconCache() simulates what WM_DISPLAYCHANGE does
        uint32_t const countBefore = static_cast<uint32_t>(fixture.tc->getTaskbarIcons().size());
        fixture.tc->refreshIconCache();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        uint32_t const countAfter  = static_cast<uint32_t>(fixture.tc->getTaskbarIcons().size());
        // After refresh, count should be sane (same or re-populated)
        REQUIRE(countAfter >= 1);
        // Should not catastrophically change (±3 icons tolerance for timing)
        REQUIRE(std::abs(static_cast<int>(countAfter) - static_cast<int>(countBefore)) <= 3);
    }

    SECTION("double refreshIconCache produces a consistent icon list") {
        // Two rapid refreshes should both converge to the same non-empty list.
        fixture.tc->refreshIconCache();
        fixture.tc->refreshIconCache();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        auto const& icons = fixture.tc->getTaskbarIcons();
        REQUIRE(icons.size() >= 1);
        // All icons should carry valid monitor handles after re-enumeration.
        for (auto const& icon : icons) {
            REQUIRE(icon.hMonitor != nullptr);
        }
    }

    SECTION("getAllMonitorStates is consistent after soft-reset") {
        int32_t const countBefore = fixture.tc->getMonitorCount();
        fixture.tc->refreshIconCache();
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        int32_t const countAfter = fixture.tc->getMonitorCount();
        // Monitor count should not change unless hardware changed
        REQUIRE(countAfter == countBefore);
    }
}

// ============================================================================
// 8. CONDITIONAL — Dual-Monitor Specific Tests
// ============================================================================

TEST_CASE("MultiMonitor::DualMonitor", "[multimonitor][dual][.]") {
    MultiMonitorFixture fixture;

    int32_t const monitorCount = fixture.tc->getMonitorCount();
    if (monitorCount < 2) {
        WARN("Skipping dual-monitor tests — only " << monitorCount << " monitor(s) detected");
        SUCCEED("No secondary monitor present; test environment is single-monitor");
        return;
    }

    SECTION("secondary monitor state exists with distinct HWND") {
        auto const states = fixture.tc->getAllMonitorStates();
        REQUIRE(states.size() >= 2);

        // First state is primary (Shell_TrayWnd), subsequent are secondary
        std::set<HWND> seenHwnds;
        for (auto const& s : states) {
            REQUIRE(seenHwnds.find(s.taskbarHwnd) == seenHwnds.end());
            seenHwnds.insert(s.taskbarHwnd);
        }
    }

    SECTION("secondary monitor has icons in the icon cache") {
        auto const states = fixture.tc->getAllMonitorStates();
        auto const& icons = fixture.tc->getTaskbarIcons();

        // Find a secondary monitor (not the primary Shell_TrayWnd)
        HMONITOR secondaryMonitor = nullptr;
        for (auto const& s : states) {
            wchar_t cls[64] = {};
            GetClassNameW(s.taskbarHwnd, cls, 64);
            if (wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
                secondaryMonitor = s.hMonitor;
                break;
            }
        }

        if (secondaryMonitor) {
            bool foundSecondaryIcon = false;
            for (auto const& icon : icons) {
                if (icon.hMonitor == secondaryMonitor) {
                    foundSecondaryIcon = true;
                    break;
                }
            }
            REQUIRE(foundSecondaryIcon == true);
        }
    }

    SECTION("secondary monitor DPI is independently tracked") {
        auto const states = fixture.tc->getAllMonitorStates();
        // Each state should have its own DPI
        for (auto const& s : states) {
            REQUIRE(s.taskbarDpi > 0);
            REQUIRE(s.taskbarDpi <= 384);  // max reasonable DPI (400%)
        }
    }

    SECTION("hover on secondary monitor icon lands on the right monitor") {
        // Verify that an icon on the secondary monitor's taskbar rect
        // is outside the primary monitor's taskbar rect.
        auto const states   = fixture.tc->getAllMonitorStates();
        RECT const primaryRect = states[0].taskbarRect;

        for (size_t i = 1; i < states.size(); ++i) {
            RECT const secondary = states[i].taskbarRect;
            // Secondary taskbar should not overlap primary taskbar
            bool const overlaps =
                (secondary.left   < primaryRect.right) &&
                (secondary.right  > primaryRect.left)  &&
                (secondary.top    < primaryRect.bottom) &&
                (secondary.bottom > primaryRect.top);
            REQUIRE_FALSE(overlaps);
        }
    }
}
