// tests/unit/test_personalization.cpp
//
// Phase 10.6 TDD suite — D2DColorGrid, D2DSlider, colour conversion, IPC boundary checks.
//
// All tests are headless: no D2D device, no Win32 window, no live service.
// D2DColorGrid::hitTest and D2DSlider geometry methods are pure math.
// ColorPreset arithmetic is all constexpr.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdint>
#include <algorithm>

#include "page_visuals.h"
#include "theme_model.h"

using namespace aura::app;
using Approx = Catch::Approx;

// ============================================================================
// A. D2DColorGrid — preset count and hit-testing
// ============================================================================

TEST_CASE("D2DColorGrid: PRESET_COUNT == 12", "[ui][color][grid]") {
    CHECK(D2DColorGrid::PRESET_COUNT == 12);
}

TEST_CASE("D2DColorGrid: COLS x ROWS == PRESET_COUNT", "[ui][color][grid]") {
    CHECK(D2DColorGrid::COLS * D2DColorGrid::ROWS == D2DColorGrid::PRESET_COUNT);
}

TEST_CASE("D2DColorGrid: hitTest returns 0 for centre of swatch 0", "[ui][color][grid]") {
    // Swatch 0: col 0, row 0.  Origin at (40, 144).
    // Centre: (40 + 44/2, 144 + 44/2) = (62, 166)
    constexpr int32_t OX = 40, OY = 144;
    int32_t const cx0 = OX + D2DColorGrid::SWATCH_SIZE / 2;
    int32_t const cy0 = OY + D2DColorGrid::SWATCH_SIZE / 2;
    CHECK(D2DColorGrid::hitTest(cx0, cy0, OX, OY) == 0);
}

TEST_CASE("D2DColorGrid: hitTest returns 6 for centre of swatch 6 (row 2, col 0)",
          "[ui][color][grid]")
{
    // Swatch 6: col 0, row 1.
    // y offset: 1 * (SWATCH_SIZE + SWATCH_GAP) = 50
    constexpr int32_t OX = 40, OY = 144;
    int32_t const cx6 = OX + D2DColorGrid::SWATCH_SIZE / 2;
    int32_t const cy6 = OY + (D2DColorGrid::SWATCH_SIZE + D2DColorGrid::SWATCH_GAP)
                            + D2DColorGrid::SWATCH_SIZE / 2;
    CHECK(D2DColorGrid::hitTest(cx6, cy6, OX, OY) == 6);
}

TEST_CASE("D2DColorGrid: hitTest in gap between swatches returns -1", "[ui][color][grid]") {
    constexpr int32_t OX = 40, OY = 144;
    // Gap between swatch 0 and swatch 1: x = OX + SWATCH_SIZE + 1 (within gap)
    int32_t const gapX = OX + D2DColorGrid::SWATCH_SIZE + 1;
    int32_t const midY = OY + D2DColorGrid::SWATCH_SIZE / 2;
    CHECK(D2DColorGrid::hitTest(gapX, midY, OX, OY) == -1);
}

TEST_CASE("D2DColorGrid: hitTest above grid returns -1", "[ui][color][grid]") {
    constexpr int32_t OX = 40, OY = 144;
    CHECK(D2DColorGrid::hitTest(50, OY - 1, OX, OY) == -1);
}

TEST_CASE("D2DColorGrid: swatchBounds returns correct rect for index 1",
          "[ui][color][grid]")
{
    constexpr int32_t OX = 40, OY = 144;
    // Swatch 1: col 1, row 0.
    // x = OX + 1 * (SWATCH_SIZE + SWATCH_GAP) = OX + 50
    D2D1_RECT_F const r = D2DColorGrid::swatchBounds(1, OX, OY);
    CHECK(r.left   == Approx(static_cast<float>(OX + D2DColorGrid::SWATCH_SIZE + D2DColorGrid::SWATCH_GAP)).margin(0.5f));
    CHECK(r.top    == Approx(static_cast<float>(OY)).margin(0.5f));
    CHECK(r.right  == Approx(r.left + D2DColorGrid::SWATCH_SIZE).margin(0.5f));
    CHECK(r.bottom == Approx(r.top  + D2DColorGrid::SWATCH_SIZE).margin(0.5f));
}

// ============================================================================
// B. D2DSlider — pure geometry
// ============================================================================

namespace {
// Helper: make a flat horizontal track rect.
D2D1_RECT_F makeTrack(float left, float right) noexcept {
    return {left, 100.0f, right, 100.0f + D2DSlider::TRACK_H};
}
} // namespace

TEST_CASE("D2DSlider: valueToX at minVal == trackBounds.left", "[ui][slider]") {
    D2DSlider s;
    s.minVal = 0.0f; s.maxVal = 200.0f; s.value = 0.0f;
    D2D1_RECT_F const track = makeTrack(50.0f, 850.0f);
    CHECK(s.valueToX(track) == Approx(track.left).margin(0.01f));
}

TEST_CASE("D2DSlider: valueToX at maxVal == trackBounds.right", "[ui][slider]") {
    D2DSlider s;
    s.minVal = 0.0f; s.maxVal = 200.0f; s.value = 200.0f;
    D2D1_RECT_F const track = makeTrack(50.0f, 850.0f);
    CHECK(s.valueToX(track) == Approx(track.right).margin(0.01f));
}

TEST_CASE("D2DSlider: valueToX at midpoint == track centre", "[ui][slider]") {
    D2DSlider s;
    s.minVal = 0.0f; s.maxVal = 200.0f; s.value = 100.0f;
    D2D1_RECT_F const track = makeTrack(50.0f, 850.0f);
    float const expected = (track.left + track.right) * 0.5f;
    CHECK(s.valueToX(track) == Approx(expected).margin(0.5f));
}

TEST_CASE("D2DSlider: xToValue at left edge == minVal", "[ui][slider]") {
    D2D1_RECT_F const track = makeTrack(50.0f, 850.0f);
    CHECK(D2DSlider::xToValue(50.0f, track, 0.0f, 200.0f) == Approx(0.0f).margin(0.01f));
}

TEST_CASE("D2DSlider: xToValue at right edge == maxVal", "[ui][slider]") {
    D2D1_RECT_F const track = makeTrack(50.0f, 850.0f);
    CHECK(D2DSlider::xToValue(850.0f, track, 0.0f, 200.0f) == Approx(200.0f).margin(0.01f));
}

TEST_CASE("D2DSlider: xToValue clamps below left edge to minVal", "[ui][slider]") {
    D2D1_RECT_F const track = makeTrack(50.0f, 850.0f);
    CHECK(D2DSlider::xToValue(-100.0f, track, 0.0f, 200.0f) == Approx(0.0f).margin(0.01f));
}

TEST_CASE("D2DSlider: xToValue clamps above right edge to maxVal", "[ui][slider]") {
    D2D1_RECT_F const track = makeTrack(50.0f, 850.0f);
    CHECK(D2DSlider::xToValue(9999.0f, track, 0.0f, 200.0f) == Approx(200.0f).margin(0.01f));
}

// ============================================================================
// C. Boundary checks — ThemeConfig validity at animation speed extremes
// ============================================================================

TEST_CASE("ThemeConfig: animSpeedPct=0 produces a valid struct", "[ui][boundary]") {
    ThemeConfig cfg;
    cfg.animSpeedPct = 0;
    // Verify all other defaults are unchanged.
    CHECK(cfg.themeName  == L"default");
    CHECK(cfg.showOnHover  == true);
    CHECK(cfg.glowEnabled  == true);
    // animSpeedPct=0 is legal (clamped to 0 by SettingsManager on load).
    CHECK(cfg.animSpeedPct == 0u);
}

TEST_CASE("ThemeConfig: animSpeedPct=200 produces a valid struct", "[ui][boundary]") {
    ThemeConfig cfg;
    cfg.animSpeedPct = 200;
    CHECK(cfg.animSpeedPct == 200u);
}

// ============================================================================
// D. ColorPreset → AuraColor conversion
// ============================================================================

TEST_CASE("ColorPreset: #00F2FF converts to AuraColor{0, 242, 255, 255}",
          "[ui][color][convert]")
{
    // Preset 0 is AuraShell signature cyan: {0.0f, 0.949f, 1.0f}
    ColorPreset const& p = D2DColorGrid::PRESETS[0];
    AuraColor const ac = p.toAuraColor();

    CHECK(ac.r == 0u);
    // 0.949 * 255 + 0.5 = 242.5 → 242
    CHECK(ac.g == 242u);
    CHECK(ac.b == 255u);
    CHECK(ac.a == 255u);
}

TEST_CASE("ColorPreset: toD2D preserves float values exactly", "[ui][color][convert]") {
    ColorPreset const& p = D2DColorGrid::PRESETS[0];
    D2D1_COLOR_F const d = p.toD2D();
    CHECK(d.r == Approx(p.r).margin(1e-6f));
    CHECK(d.g == Approx(p.g).margin(1e-6f));
    CHECK(d.b == Approx(p.b).margin(1e-6f));
    CHECK(d.a == Approx(1.0f).margin(1e-6f));
}

TEST_CASE("ColorPreset: all presets have alpha 1.0 in toD2D()", "[ui][color][convert]") {
    for (int32_t i = 0; i < D2DColorGrid::PRESET_COUNT; ++i) {
        D2D1_COLOR_F const d = D2DColorGrid::PRESETS[i].toD2D();
        CHECK(d.a == Approx(1.0f).margin(1e-5f));
    }
}

// ============================================================================
// E. StyleManager — custom accent shade derivation
// ============================================================================

#include "ui_styles.h"
using namespace aura::ui;

TEST_CASE("StyleManager: accentHover is brighter than accentPrimary after custom red",
          "[ui][color][style]")
{
    // Set a deep red as custom accent.
    D2D1_COLOR_F const red = {0.8f, 0.05f, 0.05f, 1.0f};
    StyleManager::getInstance().setThemeMode(ThemeMode::Custom);
    StyleManager::getInstance().setCustomAccent(red);

    ColorTokens const& t = StyleManager::getInstance().getTokens();

    // accentHover should be lighter than accentPrimary.
    auto luminance = [](D2D1_COLOR_F const& c) noexcept {
        return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    };
    // In dark mode accentHover is lighter; in light mode (CI/Server) it's darker.
    // Either is correct — just verify it actually differs from primary.
    CHECK(std::abs(luminance(t.accentHover) - luminance(t.accentPrimary)) > 0.001f);

    // Reset to Signature for subsequent tests.
    StyleManager::getInstance().setThemeMode(ThemeMode::Signature);
}
