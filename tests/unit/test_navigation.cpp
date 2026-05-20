// tests/unit/test_navigation.cpp
//
// Phase 10.2 TDD suite — drives the NavigationManager contract:
// hit-testing, page state transitions, and spring indicator convergence.
//
// No live Win32 window is required.  NavigationManager is constructed without
// calling create(), so all geometry and state tests run in headless mode.
// Tests that need a live HWND are tagged [integration] and skipped in CI.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <atomic>
#include <cmath>

#include "navigation_manager.h"
#include "ui_styles.h"

using namespace aura::app;
using namespace aura::ui;
using Approx = Catch::Approx;

// ============================================================================
// A. Hit-testing — pure geometry, no Win32
// ============================================================================

TEST_CASE("hitTest: header area (y < HEADER_H) returns nullopt", "[nav][hit]") {
    NavigationManager nav;
    CHECK_FALSE(nav.hitTest(0).has_value());
    CHECK_FALSE(nav.hitTest(30).has_value());
    CHECK_FALSE(nav.hitTest(NavigationManager::HEADER_H - 1).has_value());
}

TEST_CASE("hitTest: exact top boundary of first item maps to Dashboard", "[nav][hit]") {
    NavigationManager nav;
    auto const result = nav.hitTest(NavigationManager::HEADER_H);  // y = 60
    REQUIRE(result.has_value());
    CHECK(result.value() == Page::Dashboard);
}

TEST_CASE("hitTest: y=80 (mid Dashboard item) maps to Dashboard", "[nav][hit]") {
    NavigationManager nav;
    auto const result = nav.hitTest(80);
    REQUIRE(result.has_value());
    CHECK(result.value() == Page::Dashboard);
}

TEST_CASE("hitTest: y=110 maps to Visuals", "[nav][hit]") {
    NavigationManager nav;
    auto const result = nav.hitTest(110);
    REQUIRE(result.has_value());
    CHECK(result.value() == Page::Visuals);
}

TEST_CASE("hitTest: y=180 maps to Behavior (redesign: 48px items)", "[nav][hit]") {
    // With SIDEBAR_ITEM_HEIGHT=48: Behavior = HEADER_H + 2*48 = 156..204, centre=180
    NavigationManager nav;
    auto const result = nav.hitTest(180);
    REQUIRE(result.has_value());
    CHECK(result.value() == Page::Behavior);
}

TEST_CASE("hitTest: y=228 maps to About (redesign: 48px items)", "[nav][hit]") {
    // About = HEADER_H + 3*48 = 204..252, centre=228
    NavigationManager nav;
    auto const result = nav.hitTest(228);
    REQUIRE(result.has_value());
    CHECK(result.value() == Page::About);
}

TEST_CASE("hitTest: y below all items returns nullopt", "[nav][hit]") {
    NavigationManager nav;
    // Redesign: SIDEBAR_ITEM_HEIGHT = 48px. 5 items × 48px + 60px header = 300px.
    // Items end at HEADER_H + ITEM_COUNT * ITEM_HEIGHT = 60 + 5*48 = 300.
    CHECK_FALSE(nav.hitTest(300).has_value());
    CHECK_FALSE(nav.hitTest(400).has_value());
    CHECK_FALSE(nav.hitTest(9999).has_value());
}

TEST_CASE("hitTest: exact bottom boundary of last item (DesktopItems) returns nullopt",
          "[nav][hit]")
{
    NavigationManager nav;
    // DesktopItems occupies [252, 300) with 48px height: 60 + 4*48=252 to 60+5*48=300.
    CHECK_FALSE(nav.hitTest(300).has_value());
    // y=252 is the START of DesktopItems — should return a value.
    CHECK(nav.hitTest(252).has_value());
}

TEST_CASE("hitTest: each page maps to exactly one y-band", "[nav][hit]") {
    NavigationManager nav;
    // Verify all four bands map to distinct pages.
    int32_t const itemH = metrics::SIDEBAR_ITEM_HEIGHT;
    int32_t const base  = NavigationManager::HEADER_H;

    for (int32_t i = 0; i < NavigationManager::ITEM_COUNT; ++i) {
        int32_t const mid = base + i * itemH + itemH / 2;
        auto const result = nav.hitTest(mid);
        REQUIRE(result.has_value());
        CHECK(static_cast<int32_t>(result.value()) == i);
    }
}

// ============================================================================
// B. Indicator target geometry
// ============================================================================

TEST_CASE("indicatorTargetY: Dashboard == HEADER_H + centred in first item", "[nav][geom]") {
    NavigationManager nav;
    // Expected: 60 + 0*40 + (40 - 28) / 2 = 60 + 0 + 6 = 66
    float const expected = static_cast<float>(
        NavigationManager::HEADER_H +
        0 * metrics::SIDEBAR_ITEM_HEIGHT +
        (metrics::SIDEBAR_ITEM_HEIGHT - metrics::SIDEBAR_ACCENT_BAR_H) / 2
    );
    CHECK(nav.indicatorTargetY(Page::Dashboard) == Approx(expected).margin(0.5f));
}

TEST_CASE("indicatorTargetY: Visuals == Dashboard + SIDEBAR_ITEM_HEIGHT", "[nav][geom]") {
    NavigationManager nav;
    float const dashY    = nav.indicatorTargetY(Page::Dashboard);
    float const visualsY = nav.indicatorTargetY(Page::Visuals);
    CHECK(visualsY == Approx(dashY + metrics::SIDEBAR_ITEM_HEIGHT).margin(0.5f));
}

TEST_CASE("indicatorTargetY: values are strictly ascending", "[nav][geom]") {
    NavigationManager nav;
    float prev = nav.indicatorTargetY(Page::Dashboard);
    for (int32_t i = 1; i < NavigationManager::ITEM_COUNT; ++i) {
        float const curr = nav.indicatorTargetY(static_cast<Page>(i));
        CHECK(curr > prev);
        prev = curr;
    }
}

// ============================================================================
// C. Page state transitions
// ============================================================================

TEST_CASE("NavigationManager: default currentPage is Dashboard", "[nav][state]") {
    NavigationManager nav;
    CHECK(nav.currentPage() == Page::Dashboard);
}

TEST_CASE("navigateTo: changes currentPage", "[nav][state]") {
    NavigationManager nav;
    nav.navigateTo(Page::Visuals);
    CHECK(nav.currentPage() == Page::Visuals);
}

TEST_CASE("navigateTo: all four pages are reachable", "[nav][state]") {
    NavigationManager nav;
    nav.navigateTo(Page::Dashboard); CHECK(nav.currentPage() == Page::Dashboard);
    nav.navigateTo(Page::Visuals);   CHECK(nav.currentPage() == Page::Visuals);
    nav.navigateTo(Page::Behavior);  CHECK(nav.currentPage() == Page::Behavior);
    nav.navigateTo(Page::About);     CHECK(nav.currentPage() == Page::About);
}

TEST_CASE("navigateTo: same page twice is idempotent", "[nav][state]") {
    NavigationManager nav;
    nav.navigateTo(Page::Visuals);
    nav.navigateTo(Page::Visuals);
    CHECK(nav.currentPage() == Page::Visuals);
}

TEST_CASE("navigateTo: round-trip returns to original page", "[nav][state]") {
    NavigationManager nav;
    nav.navigateTo(Page::About);
    nav.navigateTo(Page::Dashboard);
    CHECK(nav.currentPage() == Page::Dashboard);
}

// ============================================================================
// D. PageChangedCallback
// ============================================================================

TEST_CASE("pageChangedCallback: fires once on navigateTo new page", "[nav][callback]") {
    NavigationManager nav;
    std::atomic<int> count{0};
    Page             lastPage{Page::Dashboard};

    nav.setPageChangedCallback([&](Page const p) {
        lastPage = p;
        count.fetch_add(1, std::memory_order_relaxed);
    });

    nav.navigateTo(Page::Visuals);

    CHECK(count.load() == 1);
    CHECK(lastPage == Page::Visuals);
}

TEST_CASE("pageChangedCallback: does NOT fire when navigating to current page",
          "[nav][callback]")
{
    NavigationManager nav;
    nav.navigateTo(Page::Visuals);  // move away from default first

    std::atomic<int> count{0};
    nav.setPageChangedCallback([&](Page) {
        count.fetch_add(1, std::memory_order_relaxed);
    });

    nav.navigateTo(Page::Visuals);  // same page — no callback expected
    CHECK(count.load() == 0);
}

TEST_CASE("pageChangedCallback: receives correct Page enum on each transition",
          "[nav][callback]")
{
    NavigationManager nav;
    Page lastPage = Page::Dashboard;

    nav.setPageChangedCallback([&](Page const p) { lastPage = p; });

    nav.navigateTo(Page::About);
    CHECK(lastPage == Page::About);

    nav.navigateTo(Page::Behavior);
    CHECK(lastPage == Page::Behavior);
}

// ============================================================================
// E. Spring indicator — convergence and underdamping
// ============================================================================

// Helper: manually step the NavigationManager's spring N frames.
// NavigationManager exposes indicatorSpring() and indicatorY() for inspection;
// we drive the spring directly to isolate the math from WM_TIMER.
namespace {
void stepSpring(NavigationManager& nav, float const targetY, int const frames) {
    float const dt = 1.0f / 60.0f;
    // We access the spring state through the public test API and integrate
    // using the same tickSpring free function used internally.
    SpringState s = nav.indicatorSpring();
    for (int i = 0; i < frames; ++i) {
        tickSpring(s, targetY, dt);
    }
    // The test validates the resulting state, not a side-effectful call.
    // We verify properties of the spring math rather than internal state.
    (void)nav;

    // Store result in a local and check below (called from TEST_CASE).
    nav.setIndicatorSpringForTest(s);
}
} // namespace

TEST_CASE("Spring indicator: reaches within 10% of Visuals Y after 400ms (25 frames)",
          "[nav][spring]")
{
    NavigationManager nav;
    float const targetY = nav.indicatorTargetY(Page::Visuals);
    float const startY  = nav.indicatorTargetY(Page::Dashboard);

    // Prime the spring at Dashboard Y.
    SpringState s{startY, 0.0f};
    float const dt = 1.0f / 60.0f;

    for (int i = 0; i < 25; ++i) {
        tickSpring(s, targetY, dt);
    }

    CHECK(std::fabs(s.position - targetY) < 0.10f * std::fabs(targetY - startY) + 0.5f);
}

TEST_CASE("Spring indicator: exhibits overshoot (underdamped)", "[nav][spring]") {
    NavigationManager nav;
    float const startY  = nav.indicatorTargetY(Page::Dashboard);
    float const targetY = nav.indicatorTargetY(Page::About);  // largest displacement

    SpringState s{startY, 0.0f};
    float const dt = 1.0f / 60.0f;
    bool overshot = false;

    for (int i = 0; i < 90; ++i) {
        tickSpring(s, targetY, dt);
        if (s.position > targetY + 0.1f) {
            overshot = true;
            break;
        }
    }

    CHECK(overshot);
}

TEST_CASE("Spring indicator: settles within 2% of target after 750ms (45 frames)",
          "[nav][spring]")
{
    NavigationManager nav;
    float const startY  = nav.indicatorTargetY(Page::Dashboard);
    float const targetY = nav.indicatorTargetY(Page::Visuals);

    SpringState s{startY, 0.0f};
    float const dt    = 1.0f / 60.0f;
    float const range = std::fabs(targetY - startY);

    for (int i = 0; i < 45; ++i) {
        tickSpring(s, targetY, dt);
    }

    CHECK(std::fabs(s.position - targetY) < 0.02f * range + 0.5f);
}

TEST_CASE("isSpringSettled: returns true after full convergence", "[nav][spring]") {
    float const targetY = 106.0f;  // Visuals target
    SpringState s{66.0f, 0.0f};    // start at Dashboard
    float const dt = 1.0f / 60.0f;

    for (int i = 0; i < 120; ++i) {  // 2 seconds — always settled
        tickSpring(s, targetY, dt);
    }

    CHECK(isSpringSettled(s, targetY));
}

// ============================================================================
// F. NavigationManager internal spring — navigateTo seeds the spring correctly
// ============================================================================

TEST_CASE("navigateTo: sets spring target to indicatorTargetY of new page",
          "[nav][spring]")
{
    NavigationManager nav;
    nav.navigateTo(Page::Behavior);

    float const expectedTarget = nav.indicatorTargetY(Page::Behavior);
    float const actualTarget   = nav.indicatorSpringTarget();

    CHECK(actualTarget == Approx(expectedTarget).margin(0.5f));
}

TEST_CASE("navigateTo: spring starts from current indicator position (not target)",
          "[nav][spring]")
{
    NavigationManager nav;
    // Dashboard is default — spring starts settled at Dashboard Y.
    float const dashY = nav.indicatorTargetY(Page::Dashboard);

    nav.navigateTo(Page::About);

    // Immediately after navigateTo, position is still near dashY (spring not yet ticked).
    CHECK(nav.indicatorY() == Approx(dashY).margin(1.0f));
}
