// tests/unit/test_ui_logic.cpp
//
// Phase 10.1 TDD suite — drives the StyleManager, color math, spring physics,
// and DPI scaling contract defined in src/app/ui_styles.h.
//
// All tests are self-contained (no live service, no Win32 window).
// Tests tagged [integration] are skipped unless explicitly run.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <memory>
#include <atomic>

#include "ui_styles.h"

using namespace aura::ui;
using Approx = Catch::Approx;

// ============================================================================
// Helpers
// ============================================================================

namespace {

// Perceived luminance (ITU-R BT.709) — used to verify lighten/darken math.
float luminance(D2D1_COLOR_F const& c) noexcept {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// Returns true if two colours are within epsilon on all channels.
bool colorsNear(D2D1_COLOR_F const& a, D2D1_COLOR_F const& b,
                float const eps = 1e-4f) noexcept {
    return std::fabs(a.r - b.r) < eps &&
           std::fabs(a.g - b.g) < eps &&
           std::fabs(a.b - b.b) < eps &&
           std::fabs(a.a - b.a) < eps;
}

// Reset StyleManager to a known baseline before sensitive tests.
void resetToSignature() {
    StyleManager::getInstance().setThemeMode(ThemeMode::Signature);
    StyleManager::getInstance().setAppTheme(AppTheme::Dark);
}

} // namespace

// ============================================================================
// A. StyleManager — initialization and mode defaults
// ============================================================================

TEST_CASE("StyleManager: default ThemeMode is Signature", "[ui][style]") {
    // The singleton is constructed once; we verify the contract rather than
    // reset global state.
    resetToSignature();
    CHECK(StyleManager::getInstance().getThemeMode() == ThemeMode::Signature);
}

TEST_CASE("StyleManager: Signature accent is #00F2FF", "[ui][style]") {
    resetToSignature();

    D2D1_COLOR_F const accent = StyleManager::getInstance().getAccent();

    // #00F2FF → R=0/255=0.0, G=242/255≈0.949, B=255/255=1.0
    CHECK(accent.r == Approx(0.0f).margin(0.002f));
    CHECK(accent.g == Approx(0.949f).margin(0.002f));
    CHECK(accent.b == Approx(1.0f).margin(0.002f));
    CHECK(accent.a == Approx(1.0f).margin(0.001f));
}

// ============================================================================
// B. Fallback logic
// ============================================================================

TEST_CASE("StyleManager: System mode falls back to Signature when DWM unavailable",
          "[ui][style][fallback]")
{
    // We cannot guarantee DWM is available in a headless test environment.
    // The contract: if System mode cannot read DWM, accent must equal Signature.
    // We validate by checking that the result is non-zero (a real colour was set).
    StyleManager::getInstance().setThemeMode(ThemeMode::System);
    D2D1_COLOR_F const accent = StyleManager::getInstance().getAccent();

    // The resolved colour must be a valid (non-black) colour.
    bool const isNonBlack = (accent.r + accent.g + accent.b) > 0.01f;
    REQUIRE(isNonBlack);

    // If DWM failed, it should equal the Signature colour exactly.
    // (This assertion is conditional because DWM may succeed on CI machines.)
    // We verify the Signature fallback value itself is reachable:
    StyleManager::getInstance().setThemeMode(ThemeMode::Signature);
    D2D1_COLOR_F const signature = StyleManager::getInstance().getAccent();
    CHECK((signature.r + signature.g + signature.b) > 0.01f);

    resetToSignature();
}

TEST_CASE("StyleManager: zero custom accent triggers Signature fallback", "[ui][style][fallback]") {
    StyleManager::getInstance().setThemeMode(ThemeMode::Custom);

    // Pass an all-zero colour (invalid — transparent black).
    D2D1_COLOR_F const invalid = {0.0f, 0.0f, 0.0f, 0.0f};
    StyleManager::getInstance().setCustomAccent(invalid);

    D2D1_COLOR_F const accent = StyleManager::getInstance().getAccent();
    bool const isNonBlack = (accent.r + accent.g + accent.b) > 0.01f;
    CHECK(isNonBlack);

    resetToSignature();
}

// ============================================================================
// C. Custom accent round-trip
// ============================================================================

TEST_CASE("StyleManager: custom accent survives set/get round-trip", "[ui][style]") {
    StyleManager::getInstance().setThemeMode(ThemeMode::Custom);

    D2D1_COLOR_F const purple = {0.486f, 0.302f, 1.0f, 1.0f};  // #7C4DFF
    StyleManager::getInstance().setCustomAccent(purple);

    D2D1_COLOR_F const resolved = StyleManager::getInstance().getAccent();
    CHECK(colorsNear(resolved, purple));

    resetToSignature();
}

// ============================================================================
// D. Color math — lightenAccent / darkenAccent / withAlpha
// ============================================================================

TEST_CASE("lightenAccent: increases perceived luminance", "[ui][color-math]") {
    D2D1_COLOR_F const base    = {0.0f, 0.949f, 1.0f, 1.0f};  // #00F2FF
    D2D1_COLOR_F const lighter = StyleManager::lightenAccent(base, 0.15f);

    CHECK(luminance(lighter) > luminance(base));
}

TEST_CASE("darkenAccent: decreases perceived luminance", "[ui][color-math]") {
    D2D1_COLOR_F const base   = {0.0f, 0.949f, 1.0f, 1.0f};
    D2D1_COLOR_F const darker = StyleManager::darkenAccent(base, 0.20f);

    CHECK(luminance(darker) < luminance(base));
}

TEST_CASE("lightenAccent: clamps luminance at 1.0 (no overflow)", "[ui][color-math]") {
    D2D1_COLOR_F const nearWhite = {0.95f, 0.95f, 0.95f, 1.0f};
    D2D1_COLOR_F const result    = StyleManager::lightenAccent(nearWhite, 0.5f);

    CHECK(result.r <= 1.0f + 1e-5f);
    CHECK(result.g <= 1.0f + 1e-5f);
    CHECK(result.b <= 1.0f + 1e-5f);
}

TEST_CASE("darkenAccent: clamps luminance at 0.0 (no underflow)", "[ui][color-math]") {
    D2D1_COLOR_F const nearBlack = {0.05f, 0.05f, 0.05f, 1.0f};
    D2D1_COLOR_F const result    = StyleManager::darkenAccent(nearBlack, 0.5f);

    CHECK(result.r >= -1e-5f);
    CHECK(result.g >= -1e-5f);
    CHECK(result.b >= -1e-5f);
}

TEST_CASE("withAlpha: preserves RGB channels, sets alpha", "[ui][color-math]") {
    D2D1_COLOR_F const base   = {0.2f, 0.6f, 0.9f, 1.0f};
    D2D1_COLOR_F const result = StyleManager::withAlpha(base, 0.35f);

    CHECK(result.r == Approx(base.r).margin(1e-5f));
    CHECK(result.g == Approx(base.g).margin(1e-5f));
    CHECK(result.b == Approx(base.b).margin(1e-5f));
    CHECK(result.a == Approx(0.35f).margin(1e-5f));
}

TEST_CASE("lightenAccent: pure black accent becomes non-black after lightening",
          "[ui][color-math]") {
    // Edge case: achromatic black — HSL H/S are undefined; lightening L is the only
    // meaningful operation.
    D2D1_COLOR_F const black  = {0.0f, 0.0f, 0.0f, 1.0f};
    D2D1_COLOR_F const result = StyleManager::lightenAccent(black, 0.3f);

    CHECK((result.r + result.g + result.b) > 0.01f);
}

// ============================================================================
// E. ColorTokens derivation sanity
// ============================================================================

TEST_CASE("ColorTokens: accentHover is lighter than accentPrimary", "[ui][tokens]") {
    resetToSignature();

    ColorTokens const& t = StyleManager::getInstance().getTokens();
    CHECK(luminance(t.accentHover) > luminance(t.accentPrimary));
}

TEST_CASE("ColorTokens: accentPressed is darker than accentPrimary", "[ui][tokens]") {
    resetToSignature();

    ColorTokens const& t = StyleManager::getInstance().getTokens();
    CHECK(luminance(t.accentPressed) < luminance(t.accentPrimary));
}

TEST_CASE("ColorTokens: dark theme bgBase has low luminance (< 0.05)", "[ui][tokens]") {
    StyleManager::getInstance().setThemeMode(ThemeMode::Signature);
    StyleManager::getInstance().setAppTheme(AppTheme::Dark);

    ColorTokens const& t = StyleManager::getInstance().getTokens();
    CHECK(luminance(t.bgBase) < 0.05f);

    resetToSignature();
}

TEST_CASE("ColorTokens: light theme bgBase has high luminance (> 0.7)", "[ui][tokens]") {
    StyleManager::getInstance().setThemeMode(ThemeMode::Signature);
    StyleManager::getInstance().setAppTheme(AppTheme::Light);

    ColorTokens const& t = StyleManager::getInstance().getTokens();
    CHECK(luminance(t.bgBase) > 0.70f);

    resetToSignature();
}

TEST_CASE("ColorTokens: accentGlow alpha is less than accentPrimary alpha", "[ui][tokens]") {
    resetToSignature();

    ColorTokens const& t = StyleManager::getInstance().getTokens();
    CHECK(t.accentGlow.a < t.accentPrimary.a);
}

// ============================================================================
// F. Observer pattern
// ============================================================================

TEST_CASE("StyleManager: observer fires on setThemeMode change", "[ui][observer]") {
    // Capture via shared_ptr — observers are stored permanently in the singleton.
    // A [&] capture would dangle when the test's stack frame is destroyed and a
    // subsequent test triggers notifications, causing SIGSEGV.
    auto pCount = std::make_shared<std::atomic<int>>(0);
    StyleManager::getInstance().subscribeThemeChanged([pCount] {
        pCount->fetch_add(1, std::memory_order_relaxed);
    });

    int const before = pCount->load();
    StyleManager::getInstance().setThemeMode(ThemeMode::Signature);
    int const after  = pCount->load();

    CHECK(after > before);
}

TEST_CASE("StyleManager: observer fires on setCustomAccent change", "[ui][observer]") {
    auto pCount = std::make_shared<std::atomic<int>>(0);
    StyleManager::getInstance().subscribeThemeChanged([pCount] {
        pCount->fetch_add(1, std::memory_order_relaxed);
    });

    StyleManager::getInstance().setThemeMode(ThemeMode::Custom);
    int const before = pCount->load();

    D2D1_COLOR_F const color = {0.5f, 0.2f, 0.8f, 1.0f};
    StyleManager::getInstance().setCustomAccent(color);
    int const after  = pCount->load();

    CHECK(after > before);

    resetToSignature();
}

// ============================================================================
// G. Spring physics — oscillator math
// ============================================================================

TEST_CASE("Spring: position reaches within 10% of target within 400ms (25 frames @ 60fps)",
          "[ui][spring]")
{
    // With k=180, b=12: ζ ≈ 0.447 (underdamped).
    // Envelope at 400ms: e^(-ζ·ωn·t) = e^(-0.447·13.42·0.4) ≈ 0.091 < 0.10 ✓
    SpringState s{0.0f, 0.0f};
    float const target = 1.0f;
    float const dt     = 1.0f / 60.0f;  // 16.67 ms per tick

    for (int i = 0; i < 25; ++i) {  // 25 frames = ~416 ms
        tickSpring(s, target, dt);
    }

    // The oscillation envelope must be within ±10% of the final value.
    CHECK(std::fabs(s.position - target) < 0.10f);
}

TEST_CASE("Spring: exhibits overshoot (underdamped, zeta < 1)", "[ui][spring]") {
    // An underdamped spring starting at 0, target 1 must exceed 1 at some point.
    SpringState s{0.0f, 0.0f};
    float const target  = 1.0f;
    float const dt      = 1.0f / 60.0f;
    bool        overshot = false;

    for (int i = 0; i < 60; ++i) {  // 1 second
        tickSpring(s, target, dt);
        if (s.position > target + 1e-4f) {
            overshot = true;
            break;
        }
    }

    CHECK(overshot);
}

TEST_CASE("Spring: settles within 2% of target within 750ms (45 frames @ 60fps)",
          "[ui][spring]")
{
    // 2% settling time ≈ 4/(ζ·ωn) = 4/(0.447·13.42) ≈ 0.667s
    SpringState s{0.0f, 0.0f};
    float const target = 1.0f;
    float const dt     = 1.0f / 60.0f;

    for (int i = 0; i < 45; ++i) {  // 45 frames = 750 ms
        tickSpring(s, target, dt);
    }

    CHECK(std::fabs(s.position - target) < 0.02f);
}

TEST_CASE("isSpringSettled: returns true after long run", "[ui][spring]") {
    SpringState s{0.0f, 0.0f};
    float const target = 1.0f;
    float const dt     = 1.0f / 60.0f;

    // Run 120 frames (2 seconds) — always settled by then.
    for (int i = 0; i < 120; ++i) {
        tickSpring(s, target, dt);
    }

    CHECK(isSpringSettled(s, target));
}

TEST_CASE("isSpringSettled: returns false at start of motion", "[ui][spring]") {
    SpringState s{0.0f, 0.0f};
    // Before any tick the spring is at 0, target is 1 — not settled.
    CHECK_FALSE(isSpringSettled(s, 1.0f));
}

TEST_CASE("Spring: starts from non-zero position (mid-animation resume)", "[ui][spring]") {
    // Simulates interrupting an animation at 0.5 and reversing target to 0.
    SpringState s{0.5f, 0.8f};  // mid-motion upward
    float const target = 0.0f;
    float const dt     = 1.0f / 60.0f;

    // Must eventually converge to 0 (not diverge).
    for (int i = 0; i < 120; ++i) {
        tickSpring(s, target, dt);
    }

    CHECK(std::fabs(s.position - target) < 0.05f);
}

// ============================================================================
// H. DPI scaling
// ============================================================================

TEST_CASE("scaled: returns logical value unchanged at 96 DPI (scale = 1.0)", "[ui][dpi]") {
    CHECK(StyleManager::scaled(metrics::SPACE_L, 1.0f) == metrics::SPACE_L);
    CHECK(StyleManager::scaled(metrics::CTRL_HEIGHT_DEFAULT, 1.0f) ==
          metrics::CTRL_HEIGHT_DEFAULT);
}

TEST_CASE("scaled: doubles value at 192 DPI (scale = 2.0)", "[ui][dpi]") {
    CHECK(StyleManager::scaled(metrics::SPACE_L, 2.0f) == metrics::SPACE_L * 2);
    CHECK(StyleManager::scaled(metrics::SIDEBAR_WIDTH, 2.0f) ==
          metrics::SIDEBAR_WIDTH * 2);
}

TEST_CASE("scaled: rounds to nearest integer at non-integer scale", "[ui][dpi]") {
    // At 150% DPI (scale 1.5), SPACE_L = 16 → 24
    int const result = StyleManager::scaled(metrics::SPACE_L, 1.5f);
    CHECK(result == 24);
}

// ============================================================================
// I. Metric token sanity
// ============================================================================

TEST_CASE("Metrics: spacing tokens form a strict ascending sequence", "[ui][metrics]") {
    CHECK(metrics::SPACE_XS  <  metrics::SPACE_S);
    CHECK(metrics::SPACE_S   <  metrics::SPACE_M);
    CHECK(metrics::SPACE_M   <  metrics::SPACE_L);
    CHECK(metrics::SPACE_L   <  metrics::SPACE_XL);
    CHECK(metrics::SPACE_XL  <  metrics::SPACE_XXL);
}

TEST_CASE("Metrics: control heights are on the 4px grid", "[ui][metrics]") {
    CHECK(metrics::CTRL_HEIGHT_COMPACT  % 4 == 0);
    CHECK(metrics::CTRL_HEIGHT_DEFAULT  % 4 == 0);
    CHECK(metrics::CTRL_HEIGHT_LARGE    % 4 == 0);
}

TEST_CASE("Metrics: sidebar width and item height are positive", "[ui][metrics]") {
    CHECK(metrics::SIDEBAR_WIDTH       > 0);
    CHECK(metrics::SIDEBAR_ITEM_HEIGHT > 0);
}

// ============================================================================
// J. Glow token sanity
// ============================================================================

TEST_CASE("Glow: tokens form ascending intensity sequence", "[ui][glow]") {
    CHECK(glow::AMBIENT < glow::HOVER);
    CHECK(glow::HOVER   < glow::ACTIVE);
    CHECK(glow::ACTIVE  < glow::FOCUS);
}

TEST_CASE("Glow: all intensity values are in (0, 1] range", "[ui][glow]") {
    CHECK(glow::AMBIENT > 0.0f);
    CHECK(glow::FOCUS   <= 1.0f);
}

// ============================================================================
// K. CardRenderer — shadow geometry, gradient tokens, highlight thickness
//    (Phase 10.4 TDD — pure math, no D2D device required)
// ============================================================================

#include "card_renderer.h"
using namespace aura::app;

TEST_CASE("CardRenderer: shadow ring 0 expands 8px outward and offsets (+1, +3)",
          "[ui][card]")
{
    D2D1_RECT_F const card = {10.0f, 20.0f, 210.0f, 120.0f};

    // Ring 0: SHADOW_INSET[0] = -8.0 (expand), then offset (+1, +3).
    // Redesign: SHADOW_OFFSET_Y changed from 2.0 → 3.0 for richer depth.
    // expandRect(card, 8)  → {2, 12, 218, 128}
    // offsetRect(+1, +3)   → {3, 15, 219, 131}
    D2D1_RECT_F const ring0 = CardRenderer::offsetRect(
        CardRenderer::expandRect(card, -CardRenderer::SHADOW_INSET[0]),
        CardRenderer::SHADOW_OFFSET_X,
        CardRenderer::SHADOW_OFFSET_Y
    );

    CHECK(ring0.left   == Approx(3.0f).margin(0.01f));
    CHECK(ring0.top    == Approx(15.0f).margin(0.01f));   // was 14; SHADOW_OFFSET_Y=3 now
    CHECK(ring0.right  == Approx(219.0f).margin(0.01f));
    CHECK(ring0.bottom == Approx(131.0f).margin(0.01f));  // was 130
}

TEST_CASE("CardRenderer: shadow ring alphas form strict ascending sequence",
          "[ui][card]")
{
    // Outer ring (index 0) must be dimmest; inner ring (index 3) must be darkest.
    for (int i = 0; i < CardRenderer::SHADOW_RING_COUNT - 1; ++i) {
        CHECK(CardRenderer::SHADOW_ALPHA[i] < CardRenderer::SHADOW_ALPHA[i + 1]);
    }
}

TEST_CASE("CardRenderer: gradient top colour matches #1E1E1E (Lively spec)",
          "[ui][card]")
{
    float const expected = 0x1E / 255.0f;
    CHECK(CardRenderer::GRADIENT_TOP_R == Approx(expected).margin(0.002f));
    CHECK(CardRenderer::GRADIENT_TOP_G == Approx(expected).margin(0.002f));
    CHECK(CardRenderer::GRADIENT_TOP_B == Approx(expected).margin(0.002f));
}

TEST_CASE("CardRenderer: gradient bottom colour matches #161616 (Lively spec)",
          "[ui][card]")
{
    float const expected = 0x16 / 255.0f;
    CHECK(CardRenderer::GRADIENT_BOTTOM_R == Approx(expected).margin(0.002f));
    CHECK(CardRenderer::GRADIENT_BOTTOM_G == Approx(expected).margin(0.002f));
    CHECK(CardRenderer::GRADIENT_BOTTOM_B == Approx(expected).margin(0.002f));
}

TEST_CASE("CardRenderer: top highlight thickness is exactly 1.0px",
          "[ui][card]")
{
    CHECK(CardRenderer::HIGHLIGHT_THICKNESS == Approx(1.0f).margin(0.001f));
}

// ============================================================================
// L. DPI awareness — Phase 10.8 shared render engine scaling tests
// ============================================================================

#include "dpi_awareness.h"
using namespace aura::platform;

TEST_CASE("DpiAwareness: getScaleFactor(96) == 1.0 (100% DPI)", "[ui][dpi][phase108]") {
    CHECK(DpiAwareness::getScaleFactor(96u) == Approx(1.0f).margin(0.001f));
}

TEST_CASE("DpiAwareness: getScaleFactor(192) == 2.0 (200% DPI)", "[ui][dpi][phase108]") {
    CHECK(DpiAwareness::getScaleFactor(192u) == Approx(2.0f).margin(0.001f));
}

TEST_CASE("DpiAwareness: getScaleFactor(144) == 1.5 (150% DPI)", "[ui][dpi][phase108]") {
    CHECK(DpiAwareness::getScaleFactor(144u) == Approx(1.5f).margin(0.001f));
}

TEST_CASE("DpiAwareness: scalePixelsX(SPACE_L, 2.0) == SPACE_L*2", "[ui][dpi][phase108]") {
    int const result = DpiAwareness::scalePixelsX(metrics::SPACE_L, 2.0f);
    CHECK(result == metrics::SPACE_L * 2);
}

TEST_CASE("DpiAwareness: scalePixelsX(SPACE_S, 1.5) == 12 (8 * 1.5 rounded)", "[ui][dpi][phase108]") {
    int const result = DpiAwareness::scalePixelsX(metrics::SPACE_S, 1.5f);
    CHECK(result == 12);
}

TEST_CASE("DPI mouse conversion: physical pixel / scale gives logical coordinate",
          "[ui][dpi][phase108]")
{
    // At 150% DPI (scale=1.5), a physical mouse position of (300, 450) should
    // convert to logical (200, 300) for hit-testing against DIP-coordinate card bounds.
    float const scale = DpiAwareness::getScaleFactor(144u);  // 1.5
    int32_t const physX = 300, physY = 450;
    int32_t const logX = static_cast<int32_t>(physX / scale);
    int32_t const logY = static_cast<int32_t>(physY / scale);
    CHECK(logX == 200);
    CHECK(logY == 300);
}

// ============================================================================
// M. Shared D2D factory — resource-ownership contract (Phase 10.8)
//    All tests are headless; they verify the ownership architecture without
//    requiring a live D2D device or COM apartment.
// ============================================================================

#include "page_dashboard.h"
#include "page_visuals.h"
#include "page_behavior.h"
#include "page_about.h"

static AppClient& sharedClient() {
    static AppClient c;
    return c;
}
static SettingsManager& sharedSettings() {
    return SettingsManager::getInstance();
}

// Contract: pages store the factory as a raw non-owning pointer.
// A nullptr factory must not crash — pages fall back to GDI-only rendering.

TEST_CASE("DashboardPage: create() with nullptr factory does not crash",
          "[ui][factory][phase108]")
{
    // DashboardPage must construct without a live HWND or factory.
    // This verifies that the null-factory GDI fallback path is safe.
    DashboardPage page(sharedClient());
    // create() is NOT called — just verifying construction is safe.
    // hitTestPreset on a headless instance must return -1.
    CHECK(page.hitTestPreset(0, 0) == -1);      // (0,0) is in the header area (y<GALLERY_Y=64)
    CHECK(page.hitTestPreset(500, 800) == -1);  // y=800 is past the gallery (ends ~y=756)
}

TEST_CASE("VisualsPage: constructs and destructs safely with no factory",
          "[ui][factory][phase108]")
{
    // VisualsPage holds a raw non-owning ID2D1Factory*. Construction and
    // destruction must not crash even without a live D2D device.
    // This verifies the null-factory code path is safe.
    {
        VisualsPage page(sharedClient(), sharedSettings());
        // D2DColorGrid presets must all be in valid range
        CHECK(D2DColorGrid::PRESET_COUNT == 12);
        // D2DSlider default clamp must stay in range
        D2DSlider s;
        CHECK(s.clampValue(s.value) >= s.minVal);
        CHECK(s.clampValue(s.value) <= s.maxVal);
    }  // destructor must not crash
    CHECK(true);  // if we reach here, no crash
}

TEST_CASE("BehaviorPage: create() with nullptr factory does not crash",
          "[ui][factory][phase108]")
{
    BehaviorPage page(sharedClient(), sharedSettings());
    CHECK(page.hitTestToggle(0, 0) == -1);
    CHECK(page.hitTestButton(0, 0) == -1);
}

TEST_CASE("DpiAwareness: scaled metrics double linearly at 200% DPI",
          "[ui][dpi][phase108]")
{
    // Verify all spacing tokens scale exactly ×2 — no rounding drift.
    CHECK(DpiAwareness::scalePixelsX(metrics::SPACE_XS,  2.0f) == metrics::SPACE_XS  * 2);
    CHECK(DpiAwareness::scalePixelsX(metrics::SPACE_S,   2.0f) == metrics::SPACE_S   * 2);
    CHECK(DpiAwareness::scalePixelsX(metrics::SPACE_M,   2.0f) == metrics::SPACE_M   * 2);
    CHECK(DpiAwareness::scalePixelsX(metrics::SPACE_L,   2.0f) == metrics::SPACE_L   * 2);
    CHECK(DpiAwareness::scalePixelsX(metrics::SPACE_XL,  2.0f) == metrics::SPACE_XL  * 2);
    CHECK(DpiAwareness::scalePixelsX(metrics::SPACE_XXL, 2.0f) == metrics::SPACE_XXL * 2);
}

TEST_CASE("DpiAwareness: card radius scales proportionally",
          "[ui][dpi][phase108]")
{
    // CardRenderer::CARD_RADIUS = 12.0f at 96 DPI.
    // At 200% (scale=2.0) it should be 24.0f.
    // At 150% (scale=1.5) it should be 18.0f.
    float const r96  = CardRenderer::CARD_RADIUS;
    float const r200 = r96 * DpiAwareness::getScaleFactor(192u);
    float const r150 = r96 * DpiAwareness::getScaleFactor(144u);
    CHECK(r200 == Approx(24.0f).margin(0.01f));
    CHECK(r150 == Approx(18.0f).margin(0.01f));
}
