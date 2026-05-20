# AuraShell Phase 10.6: Personalization Page — Visuals

**Date**: 2026-05-11  
**Status**: Phase 10.6 ✅ Code Complete — Pending build

---

## Executive Summary

- **VisualsPage** (`src/app/page_visuals.h/.cpp`) — replaces the Phase-5 legacy Win32 controls on `m_pages[1]` with a card-based D2D layout: color picker, speed slider, and options toggles.
- **D2DColorGrid** — 12 Fluent preset swatches (2 rows × 6 cols, 44×44px each). Selected swatch has accent-colored 2px border + outer glow ring. Pure-geometry `hitTest()` is headless-testable.
- **D2DSlider** — Custom Direct2D slider for `animSpeedPct` (0–200). Gradient-filled track, white thumb with hover glow. Pure-geometry `valueToX`/`xToValue` are headless-testable.
- **Real-time sync**: color pick or slider change → `StyleManager::setCustomAccent()` (observer fires) → `AppClient::pushTheme()` (IPC) → hero preview invalidated via `m_onColorChanged` callback.

---

## Layout (content area 1060 × 800px)

```
y=16..72    Page header "Visuals" + subtitle (GDI)

y=88..298   CARD 1: Accent Color (D2D gradient card, 1012×210px)
              y=88..132   Card header "Accent Color" + divider
              y=144..238  D2DColorGrid: 2×6 preset swatches
              y=250..278  Hex input row: [Custom:] [EDIT#RRGGBB] [Apply]

y=314..462  CARD 2: Glow Animation (1012×148px)
              y=314..358  Card header "Glow Animation" + divider
              y=370..402  Slider row: [Pulse Speed] [══●═══════] [150%]

y=478..598  CARD 3: Options (1012×120px)
              y=478..522  Card header + divider
              y=526..550  D2D toggle: "Show overlay on hover"
              y=554..578  D2D toggle: "Enable glow effect"
```

---

## Architecture

### D2DColorGrid
- 12 `ColorPreset` values stored as `static constexpr` — no D2D required for constants
- `hitTest(panelX, panelY, originX, originY)` — pure integer math, testable headless
- `swatchBounds(idx, originX, originY)` — returns `D2D1_RECT_F`, testable headless
- `draw(rt, originX, originY, selectedIdx, hoveredIdx)` — D2D rendering

### D2DSlider
- Value range: 0.0f–200.0f (maps `animSpeedPct`)
- `valueToX(value, trackBounds)` — pure float math
- `xToValue(x, trackBounds, minVal, maxVal)` — pure float math + clamp
- `draw(rt, trackBounds, value, isHovered, accentColor)` — D2D rendering

### Real-time Sync Chain
```
User action (color or slider)
  → VisualsPage::applyColor() / onSliderChanged()
      → StyleManager::setCustomAccent(D2D1_COLOR_F)  ← triggers observer
      → m_settings.setTheme(ThemeConfig)
      → m_client.pushTheme(theme)                     ← IPC to service
      → m_onColorChanged()                            ← callback to ConfigWindow
          → m_dashboardPage.onVisible()               ← invalidates hero preview
```

---

## Files Created / Modified

| File | Action | Status |
|------|--------|--------|
| `src/app/page_visuals.h` | Created | ✅ |
| `src/app/page_visuals.cpp` | Created | ✅ |
| `src/app/config_window.h` | Modified — add `m_visualsPage`, remove 11 legacy HWND members | ✅ |
| `src/app/config_window.cpp` | Modified — remove legacy control creation, create VisualsPage, wire callbacks | ✅ |
| `src/app/CMakeLists.txt` | Modified — add page_visuals.cpp | ✅ |
| `tests/unit/test_personalization.cpp` | Created — 11 headless TDD tests | ✅ |
| `tests/CMakeLists.txt` | Modified — add test_personalization.cpp | ✅ |

---

## TDD Tests (test_personalization.cpp — 11 tests)

| # | Test | Category |
|---|------|----------|
| 1 | `D2DColorGrid::PRESET_COUNT == 12` | Color grid |
| 2 | `hitTest` centre of swatch 0 returns 0 | Color grid |
| 3 | `hitTest` centre of swatch 6 (row 2, col 0) returns 6 | Color grid |
| 4 | `hitTest` in gap between swatches returns -1 | Color grid |
| 5 | `D2DSlider::valueToX` at min value == trackBounds.left | Slider |
| 6 | `D2DSlider::valueToX` at max value == trackBounds.right | Slider |
| 7 | `D2DSlider::xToValue` at left edge == minVal | Slider |
| 8 | `D2DSlider::xToValue` at right edge == maxVal | Slider |
| 9 | `animSpeedPct = 0` produces valid ThemeConfig (no crash) | Boundary |
| 10 | `animSpeedPct = 200` produces valid ThemeConfig | Boundary |
| 11 | `D2D1_COLOR_F{0, 0.949, 1, 1}` converts correctly to `AuraColor{0, 242, 255, 255}` | Color conversion |

---

## Memory Budget

| Added | Size | Note |
|-------|------|------|
| `D2DColorGrid` (in VisualsPage) | 12 bytes | `int32_t selected + hovered` |
| `D2DSlider` (in VisualsPage) | 16 bytes | 4 `float` fields |
| `m_onColorChanged` callback | ~48 bytes | `std::function` baseline |
| HWND `m_hexEdit`, `m_applyBtn` | 16 bytes | Two pointers |
| D2D factory + RT (new per page) | ~2 MB | Shared factory pattern (Phase 10.7 will refactor to shared factory) |

---

## Resumption Notes

- **Stopped at**: Phase 10.6 complete
- **Next action**: Phase 10.5-A — responsive window breakpoints (Compact/Standard/Wide sidebar)
- **Known blockers**: MSVC initialization required for build
- **Watch for**: `ColorPreset::toD2D()` is `constexpr` — avoids potential MSVC issues with `constexpr D2D1_COLOR_F[]`

---

*Last Updated: 2026-05-11*
