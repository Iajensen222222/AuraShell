// tests/unit/test_ui_interactions.cpp
//
// Redesign TDD suite — preset gallery hit-tests, CardHover animation, easing.
// (Previously tested module card grid; updated for Lively-style preset gallery.)
//
// All tests are headless (no live Win32 window, no D2D device).
// DashboardPage geometry and CardHover animation are pure math;
// CardRenderer constants are constexpr — zero runtime overhead.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

#include "page_dashboard.h"
#include "card_renderer.h"
#include "ui_styles.h"

using namespace aura::app;
using namespace aura::ui;
using Approx = Catch::Approx;

// ============================================================================
// A. Hit-testing — Lively-style preset gallery geometry
// ============================================================================

// Helper: construct a headless DashboardPage (create() NOT called).
static AppClient& dummyClient() {
    static AppClient c;
    return c;
}

// Gallery layout:
//   TILE_W = 320px, TILE_H = 220px, TILE_GAP = 16px
//   GALLERY_Y = 64px (start of first row)
//   Col 0: x in [24, 344),  Col 1: x in [360, 680),  Col 2: x in [696, 1016)
//   Row 0: y in [64, 284),  Row 1: y in [300, 520),  Row 2: y in [536, 756)
//
//   Tile 0 = (col0, row0), Tile 1 = (col1, row0), Tile 2 = (col2, row0)
//   Tile 3 = (col0, row1), etc.

TEST_CASE("hitTestPreset: tile 0 centre returns 0", "[ui][interact][hit]") {
    DashboardPage dash(dummyClient());
    // Centre of tile 0: x = 24 + 320/2 = 184, y = 64 + 220/2 = 174
    CHECK(dash.hitTestPreset(184, 174) == 0);
}

TEST_CASE("hitTestPreset: tile 1 centre (col1, row0) returns 1", "[ui][interact][hit]") {
    DashboardPage dash(dummyClient());
    // Centre of tile 1: x = 360 + 160 = 520, y = 174
    CHECK(dash.hitTestPreset(520, 174) == 1);
}

TEST_CASE("hitTestPreset: tile 3 centre (col0, row1) returns 3", "[ui][interact][hit]") {
    DashboardPage dash(dummyClient());
    // Row 1 starts at y = 64 + 220 + 16 = 300; centre y = 300 + 110 = 410
    CHECK(dash.hitTestPreset(184, 410) == 3);
}

TEST_CASE("hitTestPreset: tile 8 centre (col2, row2) returns 8", "[ui][interact][hit]") {
    DashboardPage dash(dummyClient());
    // Col 2: x = 24 + 2*(320+16) = 696; centre x = 696 + 160 = 856
    // Row 2: y = 64 + 2*(220+16) = 536; centre y = 536 + 110 = 646
    CHECK(dash.hitTestPreset(856, 646) == 8);
}

TEST_CASE("hitTestPreset: page header area (y < GALLERY_Y) returns -1", "[ui][interact][hit]") {
    DashboardPage dash(dummyClient());
    CHECK(dash.hitTestPreset(184, 30) == -1);  // header
    CHECK(dash.hitTestPreset(184, 63) == -1);  // just before gallery
}

TEST_CASE("hitTestPreset: gap between tiles returns -1", "[ui][interact][hit]") {
    DashboardPage dash(dummyClient());
    // Horizontal gap between col0 and col1: x in [344, 360)
    CHECK(dash.hitTestPreset(350, 174) == -1);
    // Vertical gap between row0 and row1: y in [284, 300)
    CHECK(dash.hitTestPreset(184, 290) == -1);
}

TEST_CASE("hitTestPreset: status bar area (y >= STATUS_Y) returns -1", "[ui][interact][hit]") {
    DashboardPage dash(dummyClient());
    // STATUS_Y = 760
    CHECK(dash.hitTestPreset(184, 770) == -1);
}

// Legacy hitTestCard/hitTestToggle now always return -1 (module grid removed)
TEST_CASE("hitTestCard: always returns -1 after gallery redesign", "[ui][interact][hit]") {
    DashboardPage dash(dummyClient());
    CHECK(dash.hitTestCard(273, 440) == -1);
    CHECK(dash.hitTestCard(787, 624) == -1);
}

TEST_CASE("hitTestToggle: always returns -1 after gallery redesign", "[ui][interact][toggle]") {
    DashboardPage dash(dummyClient());
    CHECK(dash.hitTestToggle(484, 500) == -1);
    CHECK(dash.hitTestToggle(484, 684) == -1);
    CHECK(dash.hitTestToggle(100, 100) == -1);
}

// ============================================================================
// C. Quintic ease-out function
// ============================================================================

TEST_CASE("quinticEaseOut(0.0) == 0.0 (starts at zero)", "[ui][easing]") {
    CHECK(motion::quinticEaseOut(0.0f) == Approx(0.0f).margin(1e-6f));
}

TEST_CASE("quinticEaseOut(1.0) == 1.0 (ends at one)", "[ui][easing]") {
    CHECK(motion::quinticEaseOut(1.0f) == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("quinticEaseOut(0.5) ~= 0.969 (dramatic deceleration)", "[ui][easing]") {
    // f(0.5) = 1 - (0.5)^5 = 1 - 0.03125 = 0.96875
    CHECK(motion::quinticEaseOut(0.5f) == Approx(0.96875f).margin(1e-5f));
}

TEST_CASE("quinticEaseOut is monotonically increasing on [0,1]", "[ui][easing]") {
    float prev = 0.0f;
    for (int i = 1; i <= 10; ++i) {
        float const t = static_cast<float>(i) / 10.0f;
        float const v = motion::quinticEaseOut(t);
        CHECK(v > prev);
        prev = v;
    }
}

// ============================================================================
// D. CardHover animation state
// ============================================================================

TEST_CASE("CardHover::setTarget resets elapsed and captures startAlpha",
          "[ui][interact][anim]")
{
    CardHover h;
    h.currentAlpha = 0.6f;
    h.elapsedMs    = 99.0f;

    h.setTarget(1.0f);

    CHECK(h.startAlpha   == Approx(0.6f).margin(1e-5f));
    CHECK(h.targetAlpha  == Approx(1.0f).margin(1e-5f));
    CHECK(h.elapsedMs    == Approx(0.0f).margin(1e-5f));
}

TEST_CASE("CardHover::tick reaches target within 200ms (13 ticks x 16ms)",
          "[ui][interact][anim]")
{
    CardHover h;
    h.setTarget(1.0f);

    for (int i = 0; i < 13; ++i) {
        h.tick(16.0f);
    }
    // 13 × 16ms = 208ms > HOVER_ANIM_MS(150ms) → t=1.0 → quinticEaseOut(1)=1
    CHECK(h.currentAlpha == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("CardHover::tick returns false when settled", "[ui][interact][anim]") {
    CardHover h;
    h.setTarget(1.0f);

    // Run until settled (overshoot HOVER_ANIM_MS)
    for (int i = 0; i < 20; ++i) h.tick(16.0f);

    bool const stillAnimating = h.tick(16.0f);
    CHECK_FALSE(stillAnimating);
}

TEST_CASE("CardHover::tick is correct at exactly t=0.5 (75ms elapsed)",
          "[ui][interact][anim]")
{
    CardHover h;
    h.setTarget(1.0f);

    // Advance to exactly half the animation duration
    h.tick(75.0f);  // 75ms / 150ms = t=0.5

    // currentAlpha should equal quinticEaseOut(0.5) ≈ 0.969
    CHECK(h.currentAlpha == Approx(0.96875f).margin(0.002f));
}

// ============================================================================
// E. CardRenderer — hover scale constants
// ============================================================================

TEST_CASE("CardRenderer::hoverScale(0.0) == 1.0 (flat card)", "[ui][card][interact]") {
    CHECK(CardRenderer::hoverScale(0.0f) == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("CardRenderer::hoverScale(1.0) == 1.02 (fully elevated)", "[ui][card][interact]") {
    CHECK(CardRenderer::hoverScale(1.0f) == Approx(1.02f).margin(1e-6f));
}

TEST_CASE("CardRenderer::hoverScale(0.5) == 1.01 (mid-animation)", "[ui][card][interact]") {
    CHECK(CardRenderer::hoverScale(0.5f) == Approx(1.01f).margin(1e-6f));
}
