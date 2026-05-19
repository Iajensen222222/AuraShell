#include <catch2/catch_test_macros.hpp>
#include <windows.h>

#include "virtual_desktop.h"

using aura::platform::VirtualDesktopDetector;

// ============================================================================
// Initialization
// ============================================================================

TEST_CASE("VirtualDesktopDetector initializes without crashing", "[platform][virtual_desktop]") {
    auto& vd = VirtualDesktopDetector::getInstance();
    // initialize() may return false on CI (no IVirtualDesktopManager in Session 0)
    // but must never throw or crash.
    bool ok = vd.initialize();
    REQUIRE_NOTHROW(vd.shutdown());
    (void)ok;
}

// ============================================================================
// Desktop ID query
// ============================================================================

TEST_CASE("VirtualDesktopDetector returns a desktop GUID when available", "[platform][virtual_desktop]") {
    auto& vd = VirtualDesktopDetector::getInstance();
    bool ok  = vd.initialize();

    if (!ok) {
        WARN("IVirtualDesktopManager unavailable in this environment — skipping GUID tests");
        return;
    }

    GUID id = vd.getCurrentDesktopId();
    // On a real Windows 11 desktop session the GUID must be non-null.
    bool isNonNull = (id.Data1 | id.Data2 | id.Data3) != 0;
    for (auto b : id.Data4) isNonNull |= (b != 0);
    REQUIRE(isNonNull);

    vd.shutdown();
}

// ============================================================================
// isWindowOnCurrentDesktop (smoke test — desktop window is always on current)
// ============================================================================

TEST_CASE("VirtualDesktopDetector reports desktop window is on current desktop",
          "[platform][virtual_desktop]") {
    auto& vd = VirtualDesktopDetector::getInstance();
    bool ok  = vd.initialize();

    if (!ok) {
        WARN("IVirtualDesktopManager unavailable — skipping window-on-desktop test");
        return;
    }

    // GetDesktopWindow() is always on the current virtual desktop.
    HWND desktop = GetDesktopWindow();
    REQUIRE(vd.isWindowOnCurrentDesktop(desktop));

    // nullptr should not crash — returns true (fail-open).
    REQUIRE(vd.isWindowOnCurrentDesktop(nullptr));

    vd.shutdown();
}

// ============================================================================
// Callback subscription (smoke — must not crash with zero events fired)
// ============================================================================

TEST_CASE("VirtualDesktopDetector callback subscription does not crash", "[platform][virtual_desktop]") {
    auto& vd = VirtualDesktopDetector::getInstance();
    bool  ok = vd.initialize();

    bool fired = false;
    vd.subscribeDesktopChange([&](GUID, GUID) { fired = true; });

    // Don't switch desktops during tests — just verify the subscription path
    // is safe. fired will remain false.
    REQUIRE_FALSE(fired);

    if (ok) vd.shutdown();
}
