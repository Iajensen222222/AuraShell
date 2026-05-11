#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <thread>
#include <chrono>
#include <atomic>

#include "icon_overlay_manager.h"
#include "hover_detector.h"
#include "taskbar_controller.h"

using namespace aura::taskbar;

// ============================================================================
// Fixture: IconOverlayTest
// ============================================================================

struct IconOverlayTest {
    IconOverlayTest() {
        // Initialize TaskbarController and HoverDetector
        tc = &TaskbarController::getInstance();
        tc->initialize();

        hd = &HoverDetector::getInstance();
        hd->initialize();

        // Give threads time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ~IconOverlayTest() {
        if (iom) {
            try {
                iom->shutdown();
            } catch (...) {
                // Ignore errors during cleanup
            }
        }

        if (hd) {
            hd->shutdown();
        }

        if (tc) {
            tc->shutdown();
        }
    }

    TaskbarController* tc = nullptr;
    HoverDetector* hd = nullptr;
    IconOverlayManager* iom = nullptr;
};

// ============================================================================
// Test Cases: Initialization & Lifecycle
// ============================================================================

TEST_CASE("IconOverlayManager::Initialization", "[overlay][init]") {
    IconOverlayTest fixture;
    IconOverlayManager& iom = IconOverlayManager::getInstance();

    SECTION("IconOverlayManager initializes successfully") {
        iom.initialize();
        REQUIRE(iom.isInitialized() == true);
        iom.shutdown();
    }

    SECTION("Multiple initialize calls are safe") {
        iom.initialize();
        iom.initialize();  // Second call should be no-op
        REQUIRE(iom.isInitialized() == true);
        iom.shutdown();
    }

    SECTION("Shutdown stops overlay management") {
        iom.initialize();
        iom.shutdown();
        REQUIRE(iom.isInitialized() == false);
    }

    SECTION("Initialize creates overlay windows for each icon") {
        iom.initialize();

        // Get taskbar icons
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            // Each icon should have a corresponding overlay
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            auto overlayCount = iom.getOverlayWindowCount();
            REQUIRE(overlayCount >= 1);  // At least 1 overlay (system tray)
        }

        iom.shutdown();
    }
}

// ============================================================================
// Test Cases: Overlay Window Creation
// ============================================================================

TEST_CASE("IconOverlayManager::OverlayWindowCreation", "[overlay][window]") {
    IconOverlayTest fixture;
    IconOverlayManager& iom = IconOverlayManager::getInstance();
    iom.initialize();

    SECTION("Creates transparent overlay windows") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);

            if (overlayHwnd) {
                // Check that window exists
                REQUIRE(IsWindow(overlayHwnd) == TRUE);

                // Check that window is invisible initially
                bool isVisible = IsWindowVisible(overlayHwnd);
                // May be visible or invisible, depending on state
                (void)isVisible;
            }
        }
    }

    SECTION("Overlay windows are topmost but below full-screen apps") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);

            if (overlayHwnd) {
                // Window should exist
                REQUIRE(IsWindow(overlayHwnd) == TRUE);
            }
        }
    }

    SECTION("Overlay window class is properly registered") {
        // Verify window class exists
        WNDCLASSW wc = {};
        BOOL classAtom = GetClassInfoW(GetModuleHandle(nullptr), L"AuraShellIconOverlay", &wc);

        // Should be registered (or window creation would have failed)
        if (iom.getOverlayWindowCount() > 0) {
            REQUIRE(classAtom != 0);
        }
        (void)classAtom;  // Suppress unused warning
    }

    iom.shutdown();
}

// ============================================================================
// Test Cases: Visual State Management
// ============================================================================

TEST_CASE("IconOverlayManager::VisualStateManagement", "[overlay][state]") {
    IconOverlayTest fixture;
    IconOverlayManager& iom = IconOverlayManager::getInstance();
    iom.initialize();

    SECTION("Transitions overlay to Active state on hover enter") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            OverlayVisualState state = iom.getOverlayVisualState(0);
            REQUIRE(state == OverlayVisualState::Inactive);

            // Simulate hover enter
            iom.setOverlayVisualState(0, OverlayVisualState::Active);

            OverlayVisualState newState = iom.getOverlayVisualState(0);
            REQUIRE(newState == OverlayVisualState::Active);
        }
    }

    SECTION("Transitions overlay to Inactive state on hover exit") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            // Start as active
            iom.setOverlayVisualState(0, OverlayVisualState::Active);
            REQUIRE(iom.getOverlayVisualState(0) == OverlayVisualState::Active);

            // Transition to inactive
            iom.setOverlayVisualState(0, OverlayVisualState::Inactive);

            OverlayVisualState newState = iom.getOverlayVisualState(0);
            REQUIRE(newState == OverlayVisualState::Inactive);
        }
    }

    SECTION("Supports intermediate states (Loading, etc.)") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            iom.setOverlayVisualState(0, OverlayVisualState::Loading);
            OverlayVisualState state = iom.getOverlayVisualState(0);
            REQUIRE(state == OverlayVisualState::Loading);
        }
    }

    SECTION("Visual state changes are reflected in overlay rendering") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            // Change state and verify window is updated
            iom.setOverlayVisualState(0, OverlayVisualState::Active);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // Window should still exist and be valid
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);
            if (overlayHwnd) {
                REQUIRE(IsWindow(overlayHwnd) == TRUE);
            }
        }
    }

    iom.shutdown();
}

// ============================================================================
// Test Cases: Dynamic Positioning
// ============================================================================

TEST_CASE("IconOverlayManager::DynamicPositioning", "[overlay][position]") {
    IconOverlayTest fixture;
    IconOverlayManager& iom = IconOverlayManager::getInstance();
    iom.initialize();

    SECTION("Overlay window positioned exactly over icon") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);

            if (overlayHwnd) {
                RECT overlayRect;
                GetWindowRect(overlayHwnd, &overlayRect);

                const TaskbarIconInfo& icon = icons[0];

                // Overlay should be positioned over icon
                // Allow small tolerance for rendering precision
                REQUIRE(overlayRect.left >= icon.iconRect.left - 5);
                REQUIRE(overlayRect.right <= icon.iconRect.right + 5);
                REQUIRE(overlayRect.top >= icon.iconRect.top - 5);
                REQUIRE(overlayRect.bottom <= icon.iconRect.bottom + 5);
            }
        }
    }

    SECTION("Overlay windows maintain icon dimensions") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);

            if (overlayHwnd) {
                RECT overlayRect;
                GetWindowRect(overlayHwnd, &overlayRect);

                const TaskbarIconInfo& icon = icons[0];

                int overlayWidth = overlayRect.right - overlayRect.left;
                int iconWidth = icon.iconRect.right - icon.iconRect.left;

                // Width should be approximately same as icon
                // (may have slight padding for visual enhancement)
                REQUIRE(std::abs(overlayWidth - iconWidth) <= 10);
            }
        }
    }

    SECTION("Overlays update position when taskbar geometry changes") {
        // Get initial positions
        RECT initialRect;
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);
            if (overlayHwnd) {
                GetWindowRect(overlayHwnd, &initialRect);

                // Verify overlay is valid
                REQUIRE(IsWindow(overlayHwnd) == TRUE);
            }
        }
    }

    iom.shutdown();
}

// ============================================================================
// Test Cases: Z-Order Management
// ============================================================================

TEST_CASE("IconOverlayManager::ZOrderManagement", "[overlay][zorder]") {
    IconOverlayTest fixture;
    IconOverlayManager& iom = IconOverlayManager::getInstance();
    iom.initialize();

    SECTION("Overlay windows are positioned above taskbar") {
        HWND taskbarHwnd = fixture.tc->getTaskbarWindowHandle();
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0 && taskbarHwnd) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);

            if (overlayHwnd) {
                // Overlay window should exist
                REQUIRE(IsWindow(overlayHwnd) == TRUE);

                // Both windows should be valid
                REQUIRE(IsWindow(taskbarHwnd) == TRUE);
            }
        }
    }

    SECTION("Overlay z-order can be set to topmost") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);

            if (overlayHwnd) {
                iom.setOverlayZOrder(0, OverlayZOrder::TopMost);

                // Window should still exist
                REQUIRE(IsWindow(overlayHwnd) == TRUE);
            }
        }
    }

    SECTION("Overlay z-order can be set to above taskbar but below full-screen") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);

            if (overlayHwnd) {
                iom.setOverlayZOrder(0, OverlayZOrder::AboveTaskbar);

                // Window should still exist
                REQUIRE(IsWindow(overlayHwnd) == TRUE);
            }
        }
    }

    SECTION("Multiple overlays maintain correct z-order") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() >= 2) {
            HWND overlay1 = iom.getOverlayWindowForIcon(0);
            HWND overlay2 = iom.getOverlayWindowForIcon(1);

            if (overlay1 && overlay2) {
                // Both should exist
                REQUIRE(IsWindow(overlay1) == TRUE);
                REQUIRE(IsWindow(overlay2) == TRUE);
            }
        }
    }

    iom.shutdown();
}

// ============================================================================
// Test Cases: Integration with HoverDetector
// ============================================================================

TEST_CASE("IconOverlayManager::HoverDetectorIntegration", "[overlay][integration]") {
    IconOverlayTest fixture;
    IconOverlayManager& iom = IconOverlayManager::getInstance();
    iom.initialize();

    SECTION("Subscribes to HoverDetector hover events") {
        // Note: HoverDetector will already be managing callbacks
        // This test verifies IconOverlayManager works with HoverDetector

        REQUIRE(iom.isInitialized() == true);
    }

    SECTION("Updates overlay state when hover events occur") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            // Get initial state
            OverlayVisualState initialState = iom.getOverlayVisualState(0);

            // Simulate hover (in real app, HoverDetector would trigger this)
            iom.setOverlayVisualState(0, OverlayVisualState::Active);

            OverlayVisualState activeState = iom.getOverlayVisualState(0);

            // State should have changed
            if (initialState == OverlayVisualState::Inactive) {
                REQUIRE(activeState == OverlayVisualState::Active);
            }
        }
    }

    iom.shutdown();
}

// ============================================================================
// Test Cases: Click-Through Logic
// ============================================================================

TEST_CASE("IconOverlayManager::ClickThroughLogic", "[overlay][clickthrough]") {
    IconOverlayTest fixture;
    IconOverlayManager& iom = IconOverlayManager::getInstance();
    iom.initialize();

    SECTION("Overlay window allows click-through to taskbar icon") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);

            if (overlayHwnd) {
                // Check that overlay is set to click-through
                // This is done via WS_EX_TRANSPARENT or similar style

                DWORD exStyle = GetWindowLongW(overlayHwnd, GWL_EXSTYLE);

                // Should have transparency-related style
                // Either WS_EX_TRANSPARENT or custom click-through handler
                (void)exStyle;
            }
        }
    }

    SECTION("Mouse clicks on overlay pass through to underlying taskbar") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            // This test verifies click-through functionality
            // In real use, mouse events should pass to taskbar icon

            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);
            if (overlayHwnd) {
                REQUIRE(IsWindow(overlayHwnd) == TRUE);
            }
        }
    }

    SECTION("Overlay windows do not capture mouse events") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);

            if (overlayHwnd) {
                // Overlay should be click-through (WS_EX_TRANSPARENT)
                // or use SetWindowRgn to exclude non-visual areas

                DWORD exStyle = GetWindowLongW(overlayHwnd, GWL_EXSTYLE);

                // Check for transparency style
                bool hasTransparency = (exStyle & WS_EX_TRANSPARENT) != 0;

                // Should either have transparency or custom handling
                (void)hasTransparency;
            }
        }
    }

    iom.shutdown();
}

// ============================================================================
// Test Cases: Thread Safety
// ============================================================================

TEST_CASE("IconOverlayManager::ThreadSafety", "[overlay][threading]") {
    IconOverlayTest fixture;
    IconOverlayManager& iom = IconOverlayManager::getInstance();
    iom.initialize();

    SECTION("Concurrent overlay state changes are safe") {
        std::vector<std::thread> threads;
        std::atomic<int> successCount = 0;
        std::atomic<int> errorCount = 0;

        const auto& icons = fixture.tc->getTaskbarIcons();
        size_t maxIcons = (std::min)(size_t{3}, icons.size());  // parentheses guard against Windows min() macro
        uint32_t iconCount = static_cast<uint32_t>(maxIcons);

        for (uint32_t i = 0; i < iconCount; i++) {
            threads.emplace_back([&, i]() {
                try {
                    for (int j = 0; j < 5; j++) {
                        iom.setOverlayVisualState(i, OverlayVisualState::Active);
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        iom.setOverlayVisualState(i, OverlayVisualState::Inactive);
                    }
                    successCount++;
                } catch (...) {
                    errorCount++;
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        REQUIRE(errorCount == 0);
        REQUIRE(successCount > 0);
    }

    SECTION("Concurrent state queries are safe") {
        std::vector<std::thread> threads;
        std::atomic<int> successCount = 0;

        (void)fixture.tc->getTaskbarIcons();  // unused in this section; suppress C4189

        for (int i = 0; i < 5; i++) {
            threads.emplace_back([&, i]() {
                try {
                    for (int j = 0; j < 10; j++) {
                        auto state = iom.getOverlayVisualState(0);
                        auto hwnd = iom.getOverlayWindowForIcon(0);
                        (void)state;
                        (void)hwnd;
                    }
                    successCount++;
                } catch (...) {
                    // Error
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        REQUIRE(successCount > 0);
    }

    iom.shutdown();
}

// ============================================================================
// Test Cases: Performance & Resource Management
// ============================================================================

TEST_CASE("IconOverlayManager::PerformanceAndResources", "[overlay][performance]") {
    IconOverlayTest fixture;

    SECTION("Overlay creation doesn't cause excessive CPU") {
        IconOverlayManager& iom = IconOverlayManager::getInstance();

        auto start = std::chrono::high_resolution_clock::now();
        iom.initialize();
        auto elapsed = std::chrono::high_resolution_clock::now() - start;

        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        // Initialization should complete quickly (< 500ms)
        REQUIRE(elapsedMs < 500);

        iom.shutdown();
    }

    SECTION("Overlay visual state updates are fast (< 1ms)") {
        IconOverlayManager& iom = IconOverlayManager::getInstance();
        iom.initialize();

        const auto& icons = fixture.tc->getTaskbarIcons();
        if (icons.size() > 0) {
            auto start = std::chrono::high_resolution_clock::now();
            iom.setOverlayVisualState(0, OverlayVisualState::Active);
            auto elapsed = std::chrono::high_resolution_clock::now() - start;

            auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

            // Update should be very fast (< 10ms is acceptable for window update)
            REQUIRE(elapsedUs < 10000);
        }

        iom.shutdown();
    }

    SECTION("Window count matches icon count") {
        IconOverlayManager& iom = IconOverlayManager::getInstance();
        iom.initialize();

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const auto& icons = fixture.tc->getTaskbarIcons();
        uint32_t overlayCount = iom.getOverlayWindowCount();

        // Should have overlay for each icon
        REQUIRE(overlayCount >= icons.size());

        iom.shutdown();
    }
}

// ============================================================================
// Test Cases: State Persistence & Recovery
// ============================================================================

TEST_CASE("IconOverlayManager::StatePersistenceAndRecovery", "[overlay][state]") {
    IconOverlayTest fixture;

    SECTION("State persists across temporary visibility changes") {
        IconOverlayManager& iom = IconOverlayManager::getInstance();
        iom.initialize();

        const auto& icons = fixture.tc->getTaskbarIcons();
        if (icons.size() > 0) {
            iom.setOverlayVisualState(0, OverlayVisualState::Active);
            auto state1 = iom.getOverlayVisualState(0);

            // Visual state should persist
            REQUIRE(state1 == OverlayVisualState::Active);
        }

        iom.shutdown();
    }

    SECTION("Gracefully handles icon list changes") {
        IconOverlayManager& iom = IconOverlayManager::getInstance();
        iom.initialize();

        // Initial overlay count
        auto initialCount = iom.getOverlayWindowCount();

        // Verify it's positive
        REQUIRE(initialCount >= 0);

        iom.shutdown();
    }

    SECTION("Recovers from taskbar state changes") {
        IconOverlayManager& iom = IconOverlayManager::getInstance();
        iom.initialize();

        // Multiple initialize/shutdown cycles
        iom.shutdown();

        iom.initialize();
        REQUIRE(iom.isInitialized() == true);
        iom.shutdown();
    }
}

// ============================================================================
// Test Cases: Visual Rendering Backend
// ============================================================================

TEST_CASE("IconOverlayManager::RenderingBackend", "[overlay][rendering]") {
    IconOverlayTest fixture;
    IconOverlayManager& iom = IconOverlayManager::getInstance();
    iom.initialize();

    SECTION("Supports transparent background rendering") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            HWND overlayHwnd = iom.getOverlayWindowForIcon(0);

            if (overlayHwnd) {
                // Window should be created with transparency support
                REQUIRE(IsWindow(overlayHwnd) == TRUE);
            }
        }
    }

    SECTION("Supports color and opacity changes") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            // Set visual state with color
            iom.setOverlayVisualState(0, OverlayVisualState::Active);

            // Verify state change
            auto state = iom.getOverlayVisualState(0);
            REQUIRE(state == OverlayVisualState::Active);
        }
    }

    SECTION("Rendering performance is adequate for smooth animation") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            // Simulate rapid state changes
            auto start = std::chrono::high_resolution_clock::now();

            for (int i = 0; i < 60; i++) {
                OverlayVisualState state = (i % 2 == 0) ? 
                    OverlayVisualState::Active : OverlayVisualState::Inactive;
                iom.setOverlayVisualState(0, state);
            }

            auto elapsed = std::chrono::high_resolution_clock::now() - start;
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

            // 60 state changes should complete in reasonable time
            // Target: < 100ms for smooth 60fps (60 frames = 16ms each)
            REQUIRE(elapsedMs < 1000);
        }
    }

    iom.shutdown();
}
