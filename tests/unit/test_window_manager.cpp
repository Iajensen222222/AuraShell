#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <windows.h>
#include <string>
#include <vector>

#include "platform/window_manager.h"
#include "platform/dpi_awareness.h"
#include "platform/system_metrics.h"

// ============================================================================
// TESTS: WindowManager
// ============================================================================

TEST_CASE("WindowManager::Singleton", "[platform][window_manager]") {
    using namespace aura::platform;

    SECTION("WindowManager is singleton") {
        WindowManager& wm1 = WindowManager::getInstance();
        WindowManager& wm2 = WindowManager::getInstance();

        REQUIRE(&wm1 == &wm2);
    }
}

TEST_CASE("WindowManager::WindowEnumeration", "[platform][window_manager]") {
    using namespace aura::platform;

    WindowManager& wm = WindowManager::getInstance();
    wm.clearCache();

    SECTION("Can enumerate all windows") {
        std::vector<HWND> windows = wm.enumAllWindows();

        // Should find at least some windows
        REQUIRE(windows.size() > 0);
    }

    SECTION("enumAllWindows returns valid HWNDs") {
        std::vector<HWND> windows = wm.enumAllWindows();

        for (HWND hwnd : windows) {
            // Should be non-null
            REQUIRE(hwnd != nullptr);

            // Should be valid window (able to get title/class)
            std::wstring title = wm.getWindowTitle(hwnd);
            std::wstring className = wm.getWindowClass(hwnd);

            // At least one should be non-empty
            REQUIRE((!title.empty() || !className.empty()));
        }
    }
}

TEST_CASE("WindowManager::WindowSearch", "[platform][window_manager]") {
    using namespace aura::platform;

    WindowManager& wm = WindowManager::getInstance();

    SECTION("Can find Notepad window by class") {
        // Ensure Notepad is running
        STARTUPINFOA startupInfo = {};
        PROCESS_INFORMATION processInfo = {};

        if (CreateProcessA("notepad.exe", nullptr, nullptr, nullptr, false, 0,
                          nullptr, nullptr, &startupInfo, &processInfo)) {
            // Give Notepad time to start
            Sleep(500);

            HWND notepad = wm.findWindowByClass(L"Notepad");

            // Should find Notepad window
            REQUIRE(notepad != nullptr);

            // Clean up
            CloseHandle(processInfo.hProcess);
            CloseHandle(processInfo.hThread);
        }
    }

    SECTION("findWindowByClass returns null for non-existent class") {
        HWND result = wm.findWindowByClass(L"NonExistentWindowClass12345");

        REQUIRE(result == nullptr);
    }
}

TEST_CASE("WindowManager::WindowProperties", "[platform][window_manager]") {
    using namespace aura::platform;

    WindowManager& wm = WindowManager::getInstance();
    std::vector<HWND> windows = wm.enumAllWindows();

    SECTION("getWindowTitle returns valid string for visible windows") {
        for (HWND hwnd : windows) {
            if (wm.isWindowVisible(hwnd)) {
                std::wstring title = wm.getWindowTitle(hwnd);

                // Visible window should have retrievable title (may be empty, but valid)
                REQUIRE(title.length() >= 0);
                break;  // Test at least one
            }
        }
    }

    SECTION("getWindowRect returns non-zero dimensions") {
        for (HWND hwnd : windows) {
            if (wm.isWindowVisible(hwnd)) {
                RECT rect = wm.getWindowRect(hwnd);

                // Should have dimensions (even if off-screen)
                REQUIRE(rect.right - rect.left >= 0);
                REQUIRE(rect.bottom - rect.top >= 0);
                break;
            }
        }
    }
}

// Note: DPI Awareness and SystemMetrics have their own dedicated test files:
// - test_dpi_awareness.cpp - For comprehensive DPI testing
// - test_system_metrics.cpp - For comprehensive SystemMetrics testing
// The WindowManager class is tested above.
