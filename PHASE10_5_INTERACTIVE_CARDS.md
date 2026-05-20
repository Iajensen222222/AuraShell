# AuraShell Phase 10.5: Interactive Cards & Hover Transitions

**Date**: 2026-05-11  
**Status**: Phase 10.5 ✅ Code Complete — Pending build

---

## Executive Summary

Phase 10.5 makes the 2×2 dashboard grid interactive. Cards respond to mouse hover with a 150ms Quintic Ease-Out elevation transition: 1.02× scale (D2D transform), glow-bleed accent border, and a shadow deepening. The hero preview follows the cursor with 25% parallax. Two D2D toggle switches (Service auto-start, Engine monitoring) are embedded in their respective cards.

---

## Architecture

### 1. `quinticEaseOut` + `HOVER_ANIM_MS` → `ui_styles.h`

Added to `namespace motion`:
```cpp
inline constexpr uint32_t HOVER_ANIM_MS = 150;

[[nodiscard]] constexpr float quinticEaseOut(float const t) noexcept {
    float const u = 1.0f - t;
    return 1.0f - u * u * u * u * u;
}
```
`quinticEaseOut(0.5)` ≈ 0.969 — dramatic deceleration; fast start, precise landing.

### 2. CardRenderer extensions

| Method | Change |
|--------|--------|
| `drawCard(rt, bounds, hoverAlpha, withHighlight)` | `hoverAlpha` parameter added; drives scale + glow bleed |
| `drawBorder(rt, bounds, hoverAlpha, withHighlight)` | `hoverAlpha` added; blends top highlight from white → `#00F2FF` |
| `drawToggle(rt, trackBounds, isOn)` | New static method — D2D toggle switch |
| `hoverScale(hoverAlpha)` | `constexpr` — returns `1.0 + 0.02 * alpha`; testable without D2D |

**Scale transform** applied via `D2D1::Matrix3x2F::Scale(size, center)` before each card draw; reset to identity after. Hovered card drawn last to layer correctly above adjacent cards.

**Glow bleed**: top-highlight `DrawLine` color blends:
- R: `1.0 → 0.0` (white R=1, accent R=0)
- G: `1.0 → 0.949` (white G=1, accent G=0.949)
- B: `1.0` (unchanged, both white and `#00F2FF` have B=1)
- A: `0.20 → 0.30` (slightly more opaque when hovered)

### 3. `CardHover` struct (in `page_dashboard.h`, inline tick)

```cpp
struct CardHover {
    float currentAlpha{0.0f}, startAlpha{0.0f}, targetAlpha{0.0f}, elapsedMs{0.0f};

    void setTarget(float target) noexcept;  // resets elapsed, captures startAlpha
    bool tick(float dtMs) noexcept;         // advances via quinticEaseOut; returns true if still animating
};
```

Zero-heap: POD struct, stack-allocated in `std::array<CardHover, 4>`.

### 4. Mouse Interaction Flow

```
WM_MOUSEMOVE (page panel)
  → hitTestCard(x, y)  [pure math, testable]
  → if hovered card changed: setTarget on old/new card
  → TrackMouseEvent(TME_LEAVE) — request WM_MOUSELEAVE

WM_MOUSELEAVE (page panel)
  → setTarget(0.0) on current hovered card
  → m_hoveredCard = -1

onPreviewTick() (60 Hz timer, already running)
  → tick all 4 CardHover states by 16ms
  → if any card still animating: InvalidateRect(m_pagePanel)
  → page panel WM_PAINT draws cards with updated alphas

WM_LBUTTONDOWN (page panel)
  → hitTestToggle(x, y)  [pure math, testable]
  → toggle m_serviceAutoStart or m_engineMonitoring
  → InvalidateRect
```

### 5. Hero Preview Cursor Follow

```cpp
// In renderHeroPreview():
float const FOLLOW = 0.25f;
float cx = w * 0.5f + (m_heroCursorX >= 0 ? (cursorX - w*0.5f) * FOLLOW : 0);
float cy = h * 0.5f + (m_heroCursorY >= 0 ? (cursorY - h*0.5f) * FOLLOW : 0);
// Use (cx, cy) as glow center instead of (w/2, h/2)
```

### 6. Toggle Positions (page panel coordinates)

| Toggle | Card | x | y | w | h |
|--------|------|---|---|---|---|
| Service auto-start | Card 0 (Service) | CARD_COL1_X + CARD_W - 60 | GRID_ROW1_Y + CARD_H - 36 | 44 | 24 |
| Engine monitoring | Card 2 (Taskbar) | CARD_COL1_X + CARD_W - 60 | GRID_ROW2_Y + CARD_H - 36 | 44 | 24 |

---

## TDD: `tests/unit/test_ui_interactions.cpp` (10 tests)

| # | Test | Tag |
|---|------|-----|
| 1 | `hitTestCard`: Service card center → 0 | `[ui][interact][hit]` |
| 2 | `hitTestCard`: IPC card center → 1 | `[ui][interact][hit]` |
| 3 | `hitTestCard`: Taskbar card center → 2 | `[ui][interact][hit]` |
| 4 | `hitTestCard`: Overlays card center → 3 | `[ui][interact][hit]` |
| 5 | `hitTestCard`: header area (y=30) → -1 | `[ui][interact][hit]` |
| 6 | `hitTestToggle`: Service toggle → 0 | `[ui][interact][toggle]` |
| 7 | `quinticEaseOut(0.0)` = 0.0 | `[ui][easing]` |
| 8 | `quinticEaseOut(1.0)` = 1.0 | `[ui][easing]` |
| 9 | `quinticEaseOut(0.5)` ≈ 0.969 | `[ui][easing]` |
| 10 | `CardHover::tick()` reaches target within 200ms (13 ticks × 16ms) | `[ui][interact][anim]` |
| 11 | `CardRenderer::hoverScale(0.0)` = 1.0, `hoverScale(1.0)` = 1.02 | `[ui][card]` |

---

## Files Modified

| File | Change |
|------|--------|
| `src/app/ui_styles.h` | Add `quinticEaseOut`, `HOVER_ANIM_MS` to `namespace motion` |
| `src/app/card_renderer.h` | Add `hoverAlpha` to `drawCard`/`drawBorder`, add `drawToggle`, add `hoverScale` |
| `src/app/card_renderer.cpp` | Implement hover scale transform, glow bleed, toggle |
| `src/app/page_dashboard.h` | Add `CardHover` struct, hover state array, cursor tracking, toggle bools, hit-test methods |
| `src/app/page_dashboard.cpp` | Add `WM_MOUSEMOVE`/`WM_MOUSELEAVE`/`WM_LBUTTONDOWN` handlers, animation tick, toggle draw, cursor-follow |
| `tests/unit/test_ui_interactions.cpp` | Created — 11 headless tests |
| `tests/CMakeLists.txt` | Add `unit/test_ui_interactions.cpp` |

---

## Memory Budget

| Added | Size | Note |
|-------|------|------|
| `m_cardHovers[4]` | 64 bytes | `CardHover` is 4 floats = 16 bytes × 4 |
| `m_heroCursorX/Y` | 8 bytes | Two `int32_t` |
| `m_serviceAutoStart`, `m_engineMonitoring` | 2 bytes | Two `bool` |
| D2D scale transform | 0 bytes heap | `Matrix3x2F` is a stack value passed to `SetTransform` |
| **Total added** | **~74 bytes** | Well within 36.7 MB footprint |

---

## Resumption Notes

- **Stopped at**: Phase 10.5 complete
- **Next action**: Phase 10.5-A (responsive window: Compact/Standard/Wide breakpoints)
- **Known blockers**: MSVC initialization required
- **Watch for**: `D2D1::Matrix3x2F::Scale` with a center point uses the overload `Scale(D2D1_SIZE_F, D2D1_POINT_2F)` — include `d2d1helper.h` to get the helper constructors

---

*Last Updated: 2026-05-11*
