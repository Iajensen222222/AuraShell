// TDD RED PHASE — Phase 3.4 Animation & Transition Logic
// Tests are written against the planned AnimationController API.
// They will not compile until the implementation files exist.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

#include "animation_controller.h"

using namespace aura::taskbar;
using namespace std::chrono_literals;

// ============================================================================
// Helpers
// ============================================================================

static constexpr float kEps = 0.005f;  // ±0.5% tolerance for easing math

// ============================================================================
// Easing Function Accuracy
// ============================================================================

TEST_CASE("Easing::LinearInterpolation", "[animation][easing]") {
    SECTION("t=0 yields 0") {
        REQUIRE(applyEasing(0.0f, EasingType::Linear) == Catch::Approx(0.0f).epsilon(kEps));
    }
    SECTION("t=0.5 yields 0.5") {
        REQUIRE(applyEasing(0.5f, EasingType::Linear) == Catch::Approx(0.5f).epsilon(kEps));
    }
    SECTION("t=1 yields 1") {
        REQUIRE(applyEasing(1.0f, EasingType::Linear) == Catch::Approx(1.0f).epsilon(kEps));
    }
    SECTION("output is monotone") {
        for (int i = 0; i < 10; ++i) {
            float const t0 = static_cast<float>(i)     / 10.0f;
            float const t1 = static_cast<float>(i + 1) / 10.0f;
            REQUIRE(applyEasing(t1, EasingType::Linear) >= applyEasing(t0, EasingType::Linear));
        }
    }
}

TEST_CASE("Easing::EaseOutQuad", "[animation][easing]") {
    // easeOutQuad: fast start, deceleration. f(t) = 1-(1-t)^2
    SECTION("boundary conditions") {
        REQUIRE(applyEasing(0.0f, EasingType::OutQuad) == Catch::Approx(0.0f).epsilon(kEps));
        REQUIRE(applyEasing(1.0f, EasingType::OutQuad) == Catch::Approx(1.0f).epsilon(kEps));
    }
    SECTION("midpoint is 0.75 — faster than linear") {
        // 1-(1-0.5)^2 = 0.75
        REQUIRE(applyEasing(0.5f, EasingType::OutQuad) == Catch::Approx(0.75f).epsilon(kEps));
    }
    SECTION("output always >= linear at same t (eased out = ahead of linear)") {
        for (int i = 1; i < 10; ++i) {
            float const t = static_cast<float>(i) / 10.0f;
            REQUIRE(applyEasing(t, EasingType::OutQuad) >= applyEasing(t, EasingType::Linear) - kEps);
        }
    }
    SECTION("output is monotone") {
        float prev = 0.0f;
        for (int i = 1; i <= 10; ++i) {
            float const curr = applyEasing(static_cast<float>(i) / 10.0f, EasingType::OutQuad);
            REQUIRE(curr >= prev - kEps);
            prev = curr;
        }
    }
}

TEST_CASE("Easing::EaseInOutCubic", "[animation][easing]") {
    SECTION("boundary conditions") {
        REQUIRE(applyEasing(0.0f, EasingType::InOutCubic) == Catch::Approx(0.0f).epsilon(kEps));
        REQUIRE(applyEasing(1.0f, EasingType::InOutCubic) == Catch::Approx(1.0f).epsilon(kEps));
    }
    SECTION("midpoint is exactly 0.5 — symmetric S-curve") {
        REQUIRE(applyEasing(0.5f, EasingType::InOutCubic) == Catch::Approx(0.5f).epsilon(kEps));
    }
    SECTION("first quarter is slower than linear (ease-in phase)") {
        REQUIRE(applyEasing(0.25f, EasingType::InOutCubic) < applyEasing(0.25f, EasingType::Linear) + kEps);
    }
    SECTION("third quarter is faster than linear (ease-out phase)") {
        REQUIRE(applyEasing(0.75f, EasingType::InOutCubic) > applyEasing(0.75f, EasingType::Linear) - kEps);
    }
    SECTION("output is monotone") {
        float prev = 0.0f;
        for (int i = 1; i <= 20; ++i) {
            float const curr = applyEasing(static_cast<float>(i) / 20.0f, EasingType::InOutCubic);
            REQUIRE(curr >= prev - kEps);
            prev = curr;
        }
    }
}

TEST_CASE("Easing::InputClamping", "[animation][easing]") {
    SECTION("t<0 clamps to 0") {
        REQUIRE(applyEasing(-0.5f, EasingType::OutQuad) == Catch::Approx(0.0f).epsilon(kEps));
    }
    SECTION("t>1 clamps to 1") {
        REQUIRE(applyEasing(1.5f, EasingType::OutQuad) == Catch::Approx(1.0f).epsilon(kEps));
    }
}

// ============================================================================
// AnimationController Lifecycle
// ============================================================================

TEST_CASE("AnimationController::Lifecycle", "[animation][lifecycle]") {
    AnimationController ctrl;

    SECTION("initializes and shuts down cleanly") {
        ctrl.initialize(8, [](uint32_t, float) {});
        REQUIRE(ctrl.activeCount() == 0);
        ctrl.shutdown();
    }

    SECTION("double-shutdown is safe") {
        ctrl.initialize(4, [](uint32_t, float) {});
        ctrl.shutdown();
        ctrl.shutdown();  // must not crash
    }

    SECTION("activeCount is 0 before any transitions") {
        ctrl.initialize(4, [](uint32_t, float) {});
        REQUIRE(ctrl.activeCount() == 0);
        REQUIRE_FALSE(ctrl.isAnimating(0));
        ctrl.shutdown();
    }
}

// ============================================================================
// Frame-Rate Independence
// ============================================================================

TEST_CASE("AnimationController::FrameRateIndependence", "[animation][timing]") {
    // Two separate controllers, same transition, different tick intervals.
    // After the same wall-clock duration, both should report the same alpha.
    // We use a 200ms animation and sample at ~100ms (midpoint).

    static constexpr uint32_t kDurationMs = 200;
    static constexpr float    kTolerance  = 0.05f;  // ±5% alpha tolerance

    SECTION("50Hz and 120Hz reach the same alpha at the same wall time") {
        std::atomic<float> alpha50Hz{0.0f};
        std::atomic<float> alpha120Hz{0.0f};

        AnimationController ctrl50, ctrl120;

        // Both controllers target alpha=1.0 with a 200ms linear animation
        ctrl50.initialize(2,  [&alpha50Hz ](uint32_t idx, float a) { if (idx == 0) alpha50Hz  = a; });
        ctrl120.initialize(2, [&alpha120Hz](uint32_t idx, float a) { if (idx == 0) alpha120Hz = a; });

        ctrl50.startTransition(0, 1.0f, kDurationMs, EasingType::Linear);
        ctrl120.startTransition(0, 1.0f, kDurationMs, EasingType::Linear);

        // Sample at exactly the midpoint of the animation
        std::this_thread::sleep_for(std::chrono::milliseconds(kDurationMs / 2));

        float const a50  = alpha50Hz.load();
        float const a120 = alpha120Hz.load();

        // Both should be near 0.5 at the midpoint of a linear animation
        REQUIRE(a50  == Catch::Approx(0.5f).margin(kTolerance));
        REQUIRE(a120 == Catch::Approx(0.5f).margin(kTolerance));

        // And they should be within tolerance of each other
        REQUIRE(std::abs(a50 - a120) < kTolerance);

        ctrl50.shutdown();
        ctrl120.shutdown();
    }
}

// ============================================================================
// Interruptible Animations
// ============================================================================

TEST_CASE("AnimationController::InterruptibleAnimation", "[animation][interrupt]") {
    static constexpr uint32_t kFadeInMs  = 300;
    static constexpr uint32_t kFadeOutMs = 200;
    static constexpr float    kTolerance = 0.12f;

    SECTION("fade-out starts from current alpha, not from 1.0") {
        std::atomic<float> lastAlpha{0.0f};

        AnimationController ctrl;
        ctrl.initialize(2, [&lastAlpha](uint32_t idx, float a) {
            if (idx == 0) lastAlpha = a;
        });

        // Start fade-in (0 → 1) over 300ms
        ctrl.startTransition(0, 1.0f, kFadeInMs, EasingType::OutQuad);

        // Interrupt at ~100ms — animation should be roughly 1/3 complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        float const alphaAtInterrupt = lastAlpha.load();

        // At interrupt point, alpha should be between 0.1 and 0.9
        REQUIRE(alphaAtInterrupt > 0.05f);
        REQUIRE(alphaAtInterrupt < 0.95f);

        // Start fade-out from current position (not from 1.0)
        float const capturedFrom = ctrl.getCurrentAlpha(0);
        ctrl.startTransition(0, 0.0f, kFadeOutMs, EasingType::OutQuad);

        // Immediately after interruption, alpha should still be near capturedFrom
        float const alphaJustAfterInterrupt = lastAlpha.load();
        REQUIRE(std::abs(alphaJustAfterInterrupt - capturedFrom) < kTolerance);

        // Wait for fade-out to complete, then alpha should be 0
        std::this_thread::sleep_for(std::chrono::milliseconds(kFadeOutMs + 50));
        REQUIRE(lastAlpha.load() == Catch::Approx(0.0f).margin(kTolerance));

        ctrl.shutdown();
    }

    SECTION("getCurrentAlpha reflects live animation progress") {
        AnimationController ctrl;
        ctrl.initialize(2, [](uint32_t, float) {});

        ctrl.startTransition(0, 1.0f, 400, EasingType::Linear);

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        float const midAlpha = ctrl.getCurrentAlpha(0);

        // Should be near 0.5 at the midpoint
        REQUIRE(midAlpha == Catch::Approx(0.5f).margin(0.15f));

        ctrl.shutdown();
    }
}

// ============================================================================
// Active-Only Ticking
// ============================================================================

TEST_CASE("AnimationController::ActiveOnlyTicking", "[animation][performance]") {
    SECTION("activeCount is 0 after animation completes") {
        AnimationController ctrl;
        int callbackCount = 0;
        ctrl.initialize(4, [&callbackCount](uint32_t, float) { ++callbackCount; });

        ctrl.startTransition(0, 1.0f, 50, EasingType::Linear);  // 50ms animation
        REQUIRE(ctrl.isAnimating(0) == true);

        // Wait for completion
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        REQUIRE(ctrl.activeCount() == 0);
        REQUIRE_FALSE(ctrl.isAnimating(0));
        REQUIRE(ctrl.getCurrentAlpha(0) == Catch::Approx(1.0f).margin(0.01f));

        // Record callback count now that animation is done
        int const countAfterCompletion = callbackCount;

        // Wait another 100ms — no more callbacks should fire
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE(callbackCount == countAfterCompletion);  // no spurious callbacks

        ctrl.shutdown();
    }

    SECTION("multiple simultaneous animations all complete") {
        AnimationController ctrl;
        std::atomic<float> alpha0{0.0f}, alpha1{0.0f}, alpha2{0.0f};

        ctrl.initialize(4, [&](uint32_t idx, float a) {
            if (idx == 0) alpha0 = a;
            if (idx == 1) alpha1 = a;
            if (idx == 2) alpha2 = a;
        });

        ctrl.startTransition(0, 1.0f, 80,  EasingType::Linear);
        ctrl.startTransition(1, 0.5f, 120, EasingType::OutQuad);
        ctrl.startTransition(2, 0.7f, 60,  EasingType::Linear);

        REQUIRE(ctrl.activeCount() == 3);

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        REQUIRE(ctrl.activeCount() == 0);
        REQUIRE(alpha0.load() == Catch::Approx(1.0f).margin(0.02f));
        REQUIRE(alpha1.load() == Catch::Approx(0.5f).margin(0.02f));
        REQUIRE(alpha2.load() == Catch::Approx(0.7f).margin(0.02f));

        ctrl.shutdown();
    }
}

// ============================================================================
// Alpha Scaling in Rendering
// ============================================================================

TEST_CASE("AnimationController::AlphaScaling", "[animation][render]") {
    // Verify that alpha=0 and alpha=1 are the valid extreme outputs
    // and that intermediate values are within [0, 1].
    SECTION("alpha stays in [0, 1] throughout") {
        std::atomic<float> minSeen{1.0f};
        std::atomic<float> maxSeen{0.0f};

        AnimationController ctrl;
        ctrl.initialize(2, [&](uint32_t, float a) {
            float expected = minSeen.load();
            while (a < expected && !minSeen.compare_exchange_weak(expected, a)) {}
            float exp2 = maxSeen.load();
            while (a > exp2 && !maxSeen.compare_exchange_weak(exp2, a)) {}
        });

        ctrl.startTransition(0, 1.0f, 150, EasingType::OutQuad);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        REQUIRE(minSeen.load() >= 0.0f);
        REQUIRE(maxSeen.load() <= 1.0f);

        ctrl.shutdown();
    }
}
