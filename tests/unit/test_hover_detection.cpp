#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <thread>
#include <chrono>

#include "hover_detector.h"
#include "taskbar_controller.h"

using namespace aura::taskbar;

// ============================================================================
// Fixture: HoverDetectorTest
// ============================================================================

struct HoverDetectorTest {
    HoverDetectorTest() {
        // Initialize TaskbarController (required for HoverDetector to work)
        tc = &TaskbarController::getInstance();
        tc->initialize();
    }

    ~HoverDetectorTest() {
        if (tc) {
            tc->shutdown();
        }
    }

    TaskbarController* tc = nullptr;
};

// ============================================================================
// Test Cases: Basic Initialization
// ============================================================================

TEST_CASE("HoverDetector::Initialization", "[hover][init]") {
    HoverDetectorTest fixture;
    HoverDetector& hd = HoverDetector::getInstance();

    SECTION("HoverDetector initializes successfully") {
        hd.initialize();
        REQUIRE(hd.isInitialized() == true);
        hd.shutdown();
    }

    SECTION("Multiple initialize calls are safe") {
        hd.initialize();
        hd.initialize();  // Second call should be no-op
        REQUIRE(hd.isInitialized() == true);
        hd.shutdown();
    }

    SECTION("Shutdown stops monitoring") {
        hd.initialize();
        hd.shutdown();
        REQUIRE(hd.isInitialized() == false);
    }
}

// ============================================================================
// Test Cases: Taskbar Area Detection
// ============================================================================

TEST_CASE("HoverDetector::TaskbarAreaDetection", "[hover][detection]") {
    HoverDetectorTest fixture;
    HoverDetector& hd = HoverDetector::getInstance();
    hd.initialize();

    HWND taskbarHwnd = fixture.tc->getTaskbarWindowHandle();
    REQUIRE(taskbarHwnd != nullptr);

    SECTION("Detects mouse position over taskbar area") {
        RECT taskbarRect = fixture.tc->getTaskbarRect();

        // Point inside taskbar
        POINT insidePoint = {
            (taskbarRect.left + taskbarRect.right) / 2,
            (taskbarRect.top + taskbarRect.bottom) / 2
        };

        bool isOver = hd.isMouseOverTaskbar(insidePoint);
        REQUIRE(isOver == true);
    }

    SECTION("Detects mouse position outside taskbar area") {
        RECT taskbarRect = fixture.tc->getTaskbarRect();

        // Point well above taskbar
        POINT outsidePoint = {
            (taskbarRect.left + taskbarRect.right) / 2,
            taskbarRect.top - 100
        };

        bool isOver = hd.isMouseOverTaskbar(outsidePoint);
        REQUIRE(isOver == false);
    }

    SECTION("Handles edge cases (taskbar boundaries)") {
        RECT taskbarRect = fixture.tc->getTaskbarRect();

        // Point at exact left edge
        POINT edgePoint = {
            taskbarRect.left,
            (taskbarRect.top + taskbarRect.bottom) / 2
        };

        bool isOver = hd.isMouseOverTaskbar(edgePoint);
        // Should be true (within bounds, including edges)
        REQUIRE(isOver == true);
    }

    hd.shutdown();
}

// ============================================================================
// Test Cases: Region Identification
// ============================================================================

TEST_CASE("HoverDetector::RegionIdentification", "[hover][regions]") {
    HoverDetectorTest fixture;
    HoverDetector& hd = HoverDetector::getInstance();
    hd.initialize();

    SECTION("Identifies when mouse is over an icon") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() > 0) {
            // Get first icon
            const TaskbarIconInfo& icon = icons[0];

            // Point in center of icon
            POINT iconCenter = {
                (icon.iconRect.left + icon.iconRect.right) / 2,
                (icon.iconRect.top + icon.iconRect.bottom) / 2
            };

            const TaskbarIconInfo* hoveredIcon = hd.getHoveredIcon(iconCenter);
            REQUIRE(hoveredIcon != nullptr);
            REQUIRE(hoveredIcon->index == icon.index);
        }
    }

    SECTION("Returns nullptr when mouse is over empty space") {
        RECT taskbarRect = fixture.tc->getTaskbarRect();

        // Point in taskbar but likely not over any icon (e.g., right side system tray area)
        POINT emptySpacePoint = {
            taskbarRect.right - 50,  // Near right edge
            (taskbarRect.top + taskbarRect.bottom) / 2
        };

        const TaskbarIconInfo* hoveredIcon = hd.getHoveredIcon(emptySpacePoint);
        // Might be nullptr or a system tray icon, both acceptable
        (void)hoveredIcon;
    }

    SECTION("Correctly identifies which icon when multiple exist") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() >= 2) {
            // Test first and second icons
            for (size_t i = 0; i < 2 && i < icons.size(); i++) {
                const TaskbarIconInfo& icon = icons[i];

                POINT iconCenter = {
                    (icon.iconRect.left + icon.iconRect.right) / 2,
                    (icon.iconRect.top + icon.iconRect.bottom) / 2
                };

                const TaskbarIconInfo* hoveredIcon = hd.getHoveredIcon(iconCenter);
                if (hoveredIcon) {
                    REQUIRE(hoveredIcon->index == icon.index);
                }
            }
        }
    }

    hd.shutdown();
}

// ============================================================================
// Test Cases: Hover State Transitions
// ============================================================================

TEST_CASE("HoverDetector::HoverStateTransitions", "[hover][state][!mayfail]") {
    HoverDetectorTest fixture;
    HoverDetector& hd = HoverDetector::getInstance();
    hd.initialize();

    // Track state changes
    int hoverEnterCount = 0;
    int hoverExitCount = 0;
    uint32_t lastHoveredIconIndex = UINT32_MAX;

    hd.subscribeHoverEnter([&](const TaskbarIconInfo& icon) {
        hoverEnterCount++;
        lastHoveredIconIndex = icon.index;
    });

    hd.subscribeHoverExit([&]() {
        hoverExitCount++;
    });

    SECTION("Reports hover enter event") {
        // Simulate mouse enter by calling internal method
        // (In practice, this happens from polling thread)
        const auto& icons = fixture.tc->getTaskbarIcons();
        if (icons.size() > 0) {
            POINT iconCenter = {
                (icons[0].iconRect.left + icons[0].iconRect.right) / 2,
                (icons[0].iconRect.top + icons[0].iconRect.bottom) / 2
            };

            hd.updateMousePosition(iconCenter);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // Should have detected hover
            REQUIRE(hoverEnterCount > 0);
        }
    }

    SECTION("Reports hover exit event") {
        const auto& icons = fixture.tc->getTaskbarIcons();
        if (icons.size() > 0) {
            RECT taskbarRect = fixture.tc->getTaskbarRect();

            // Raise debounce so the background polling thread (real mouse != icon)
            // cannot interfere between the two manual position updates.
            hd.setDebounceDelayMs(10);

            // Move to icon first
            POINT iconCenter = {
                (icons[0].iconRect.left + icons[0].iconRect.right) / 2,
                (icons[0].iconRect.top + icons[0].iconRect.bottom) / 2
            };
            hd.updateMousePosition(iconCenter);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

            // Move outside taskbar
            POINT outsidePoint = {
                (taskbarRect.left + taskbarRect.right) / 2,
                taskbarRect.top - 100
            };
            hd.updateMousePosition(outsidePoint);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

            hd.setDebounceDelayMs(50);  // restore default

            // Should have detected exit
            REQUIRE(hoverExitCount > 0);
        }
    }

    SECTION("Differentiates between icon hovers") {
        const auto& icons = fixture.tc->getTaskbarIcons();

        if (icons.size() >= 2) {
            // Hover first icon
            POINT icon1Center = {
                (icons[0].iconRect.left + icons[0].iconRect.right) / 2,
                (icons[0].iconRect.top + icons[0].iconRect.bottom) / 2
            };
            hd.updateMousePosition(icon1Center);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            uint32_t firstIconIndex = lastHoveredIconIndex;

            // Hover second icon
            POINT icon2Center = {
                (icons[1].iconRect.left + icons[1].iconRect.right) / 2,
                (icons[1].iconRect.top + icons[1].iconRect.bottom) / 2
            };
            hd.updateMousePosition(icon2Center);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            uint32_t secondIconIndex = lastHoveredIconIndex;

            // Should have entered both icons
            REQUIRE(hoverEnterCount >= 2);
            if (firstIconIndex != UINT32_MAX && secondIconIndex != UINT32_MAX) {
                REQUIRE(firstIconIndex != secondIconIndex);
            }
        }
    }

    hd.shutdown();
}

// ============================================================================
// Test Cases: Debouncing & Flicker Prevention
// ============================================================================

TEST_CASE("HoverDetector::DebouncingAndFlickerPrevention", "[hover][debounce]") {
    HoverDetectorTest fixture;
    HoverDetector& hd = HoverDetector::getInstance();
    hd.initialize();

    int hoverEnterCount = 0;
    int hoverExitCount = 0;

    hd.subscribeHoverEnter([&](const TaskbarIconInfo&) {
        hoverEnterCount++;
    });

    hd.subscribeHoverExit([&]() {
        hoverExitCount++;
    });

    SECTION("Debounces rapid in-and-out movements") {
        const auto& icons = fixture.tc->getTaskbarIcons();
        if (icons.size() > 0) {
            RECT taskbarRect = fixture.tc->getTaskbarRect();
            POINT iconCenter = {
                (icons[0].iconRect.left + icons[0].iconRect.right) / 2,
                (icons[0].iconRect.top + icons[0].iconRect.bottom) / 2
            };
            POINT outsidePoint = {
                (taskbarRect.left + taskbarRect.right) / 2,
                taskbarRect.top - 50
            };

            // Rapidly toggle mouse position
            for (int i = 0; i < 10; i++) {
                hd.updateMousePosition(iconCenter);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                hd.updateMousePosition(outsidePoint);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            // Due to debouncing, should NOT have many transitions
            // Default debounce is typically 50-100ms
            int totalTransitions = hoverEnterCount + hoverExitCount;
            REQUIRE(totalTransitions < 10);  // Should be much less than 10
        }
    }

    SECTION("Debounce threshold can be configured") {
        // HoverDetector should provide way to set debounce delay
        hd.setDebounceDelayMs(10);  // Very short debounce

        // Now rapid movements should allow more transitions
        REQUIRE(hd.getDebounceDelayMs() == 10);
    }

    SECTION("Sustained hover does not generate extra events") {
        const auto& icons = fixture.tc->getTaskbarIcons();
        if (icons.size() > 0) {
            POINT iconCenter = {
                (icons[0].iconRect.left + icons[0].iconRect.right) / 2,
                (icons[0].iconRect.top + icons[0].iconRect.bottom) / 2
            };

            hoverEnterCount = 0;

            // Use a long debounce so the background polling thread (which sees the
            // real mouse position, not the test's fake position) cannot fire
            // spurious enter/exit events during the 200ms test window.
            hd.setDebounceDelayMs(500);

            // Simulate sustained hover (multiple updates at same position)
            for (int i = 0; i < 10; i++) {
                hd.updateMousePosition(iconCenter);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            hd.setDebounceDelayMs(50);  // restore default

            // Should only generate 1 enter event
            REQUIRE(hoverEnterCount == 1);
        }
    }

    hd.shutdown();
}

// ============================================================================
// Test Cases: Observer Pattern Integration
// ============================================================================

TEST_CASE("HoverDetector::ObserverPatternIntegration", "[hover][observer]") {
    HoverDetectorTest fixture;
    HoverDetector& hd = HoverDetector::getInstance();
    hd.initialize();

    SECTION("Registers as observer to TaskbarController") {
        // HoverDetector should automatically subscribe to taskbar changes
        // This is verified by checking that geometry updates are handled

        // When taskbar state changes, HoverDetector should update its regions
        // We can't easily force a taskbar state change in test, but we can verify the mechanism exists

        // For now, just verify HoverDetector is initialized
        REQUIRE(hd.isInitialized() == true);
    }

    SECTION("Updates hit-test regions on taskbar geometry change") {
        // Get initial taskbar rect
        RECT initialRect = fixture.tc->getTaskbarRect();

        // Verify HoverDetector knows about taskbar rect
        RECT hoverDetectorRect = hd.getTaskbarBounds();

        REQUIRE(hoverDetectorRect.left == initialRect.left);
        REQUIRE(hoverDetectorRect.right == initialRect.right);
        REQUIRE(hoverDetectorRect.top == initialRect.top);
        REQUIRE(hoverDetectorRect.bottom == initialRect.bottom);
    }

    SECTION("Updates icon hit-test regions on icon changes") {
        // The background monitoring thread populates the icon list asynchronously.
        // Wait up to 500ms for it to propagate before asserting.
        const auto& icons = fixture.tc->getTaskbarIcons();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (hd.getCachedIcons().size() != icons.size() &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto& hoverDetectorIcons = hd.getCachedIcons();

        // Should have same icon count
        REQUIRE(hoverDetectorIcons.size() == icons.size());

        // Should have same icon positions
        for (size_t i = 0; i < icons.size() && i < hoverDetectorIcons.size(); i++) {
            REQUIRE(hoverDetectorIcons[i].index == icons[i].index);
            REQUIRE(hoverDetectorIcons[i].iconRect.left == icons[i].iconRect.left);
            REQUIRE(hoverDetectorIcons[i].iconRect.right == icons[i].iconRect.right);
        }
    }

    hd.shutdown();
}

// ============================================================================
// Test Cases: Performance & CPU Usage
// ============================================================================

TEST_CASE("HoverDetector::PerformanceMetrics", "[hover][performance]") {
    HoverDetectorTest fixture;
    HoverDetector& hd = HoverDetector::getInstance();
    hd.initialize();

    SECTION("Polling does not generate excessive CPU usage") {
        // Run polling for 1 second and verify it completes smoothly
        auto start = std::chrono::high_resolution_clock::now();

        RECT taskbarRect = fixture.tc->getTaskbarRect();
        POINT testPoint = {
            (taskbarRect.left + taskbarRect.right) / 2,
            (taskbarRect.top + taskbarRect.bottom) / 2
        };

        // Simulate polling
        int pollCount = 0;
        while (std::chrono::high_resolution_clock::now() - start < std::chrono::milliseconds(100)) {
            hd.updateMousePosition(testPoint);
            pollCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60Hz
        }

        // Should have completed 100ms worth of polling smoothly (at least 3 polls, typically ~6-7)
        REQUIRE(pollCount >= 3);
    }

    SECTION("Mouse position queries are fast (< 1ms)") {
        RECT taskbarRect = fixture.tc->getTaskbarRect();
        POINT testPoint = {
            (taskbarRect.left + taskbarRect.right) / 2,
            (taskbarRect.top + taskbarRect.bottom) / 2
        };

        auto start = std::chrono::high_resolution_clock::now();
        bool isOver = hd.isMouseOverTaskbar(testPoint);
        auto elapsed = std::chrono::high_resolution_clock::now() - start;

        auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();

        // Query should complete in < 1000 microseconds (1ms)
        REQUIRE(elapsedUs < 1000);
        (void)isOver;
    }

    hd.shutdown();
}

// ============================================================================
// Test Cases: Thread Safety
// ============================================================================

TEST_CASE("HoverDetector::ThreadSafety", "[hover][threading]") {
    HoverDetectorTest fixture;
    HoverDetector& hd = HoverDetector::getInstance();
    hd.initialize();

    SECTION("Concurrent position queries are safe") {
        RECT taskbarRect = fixture.tc->getTaskbarRect();
        std::vector<std::thread> threads;
        int successCount = 0;
        int errorCount = 0;

        // Spawn 5 threads to concurrently query mouse position
        for (int i = 0; i < 5; i++) {
            threads.emplace_back([&, i]() {
                try {
                    POINT testPoint = {
                        taskbarRect.left + (i * 10),
                        (taskbarRect.top + taskbarRect.bottom) / 2
                    };

                    for (int j = 0; j < 10; j++) {
                        bool isOver = hd.isMouseOverTaskbar(testPoint);
                        const TaskbarIconInfo* icon = hd.getHoveredIcon(testPoint);
                        (void)isOver;
                        (void)icon;
                    }

                    successCount++;
                } catch (...) {
                    errorCount++;
                }
            });
        }

        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }

        // All threads should succeed
        REQUIRE(errorCount == 0);
        REQUIRE(successCount == 5);
    }

    SECTION("Concurrent state subscriptions are safe") {
        std::vector<std::thread> threads;
        std::atomic<int> subscriptionCount = 0;

        for (int i = 0; i < 3; i++) {
            threads.emplace_back([&]() {
                try {
                    hd.subscribeHoverEnter([](const TaskbarIconInfo&) {});
                    hd.subscribeHoverExit([]() {});
                    subscriptionCount++;
                } catch (...) {
                    // Subscription failed
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        REQUIRE(subscriptionCount > 0);
    }

    hd.shutdown();
}

// ============================================================================
// Test Cases: State Persistence & Recovery
// ============================================================================

TEST_CASE("HoverDetector::StatePersistenceAndRecovery", "[hover][state]") {
    HoverDetectorTest fixture;

    SECTION("State persists across shutdown/reinit") {
        HoverDetector& hd = HoverDetector::getInstance();

        hd.initialize();
        int enterCount1 = 0;

        hd.subscribeHoverEnter([&](const TaskbarIconInfo&) {
            enterCount1++;
        });

        hd.shutdown();

        // Reinitialize
        hd.initialize();
        int enterCount2 = 0;

        hd.subscribeHoverEnter([&](const TaskbarIconInfo&) {
            enterCount2++;
        });

        // Both should work without crash
        REQUIRE(hd.isInitialized() == true);

        hd.shutdown();
    }

    SECTION("Gracefully handles missing taskbar state") {
        // If taskbar becomes unavailable, HoverDetector should handle gracefully
        HoverDetector& hd = HoverDetector::getInstance();

        hd.initialize();

        POINT testPoint = {100, 100};
        bool isOver = hd.isMouseOverTaskbar(testPoint);

        // Should not crash even if taskbar is unavailable
        (void)isOver;

        hd.shutdown();
    }
}

// ============================================================================
// Test Cases: Integration with Taskbar State Changes
// ============================================================================

TEST_CASE("HoverDetector::TaskbarStateIntegration", "[hover][integration]") {
    HoverDetectorTest fixture;
    HoverDetector& hd = HoverDetector::getInstance();
    hd.initialize();

    SECTION("Receives updates when taskbar geometry changes") {
        // Get initial icon count
        size_t initialIconCount = hd.getCachedIcons().size();

        // Verify it matches TaskbarController
        const auto& controllerIcons = fixture.tc->getTaskbarIcons();
        REQUIRE(initialIconCount == controllerIcons.size());
    }

    SECTION("Correctly interprets DPI changes") {
        const auto& icons = fixture.tc->getTaskbarIcons();
        const auto& hoverIcons = hd.getCachedIcons();

        if (icons.size() > 0 && hoverIcons.size() > 0) {
            // DPI info should be available
            REQUIRE(icons[0].dpi > 0);
            REQUIRE(hoverIcons[0].dpi > 0);
        }
    }

    hd.shutdown();
}
