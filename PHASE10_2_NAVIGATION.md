# AuraShell Phase 10.2: Navigation Rail & Sidebar

**Date**: 2026-05-10  
**Status**: Phase 10.2 ✅ Code Complete — Pending first build

---

## Executive Summary

- **NavigationManager**: Standalone class that owns the sidebar child window, 4 nav items, page-content HWND routing, and the kinetic accent indicator.
- **Sidebar**: A single GDI-rendered child window (class `AuraNavSidebar`) — zero child buttons, all hit-testing done in WM_LBUTTONDOWN. Custom-drawn items with icon glyph, label, hover state.
- **Kinetic Accent Bar**: 3×28px GDI rectangle at left edge, Y-position driven by `SpringState` from `ui_styles.h`. Timer fires at 60 Hz during animations only; 0% CPU at rest.
- **ConfigWindow expansion**: Client area grows from 460×380 to 760×520 to accommodate 200px sidebar + 560px content pane.

---

## Rendering Decision: GDI vs Direct2D for Phase 10.2

| Option | Chosen | Reason |
|--------|--------|--------|
| GDI (FillRect, TextOut, DrawText) | ✅ | Sidebar repaints are infrequent; spring animation only runs during transitions. GDI is sufficient and avoids a second D2D factory/DCRenderTarget in ConfigWindow. |
| Direct2D | Phase 10.4 | Full Direct2D upgrade planned for Phase 10.4 (custom controls) when a shared factory already exists. |

The spring math, hit-testing, and page routing are rendering-backend-agnostic — upgrading to D2D only touches `drawSidebar()`.

---

## Architecture

```
ConfigWindow (760 × 520 client)
│
├── NavigationManager::m_hwnd  "AuraNavSidebar"  (200 × 520, x=0)
│   │  WM_PAINT      → drawSidebar() — background + items + accent bar
│   │  WM_LBUTTONDOWN→ hitTest() → navigateTo()
│   │  WM_MOUSEMOVE  → hover tracking → InvalidateRect
│   │  WM_MOUSELEAVE → clear hover → InvalidateRect
│   └── WM_TIMER (ID=9001, 16ms) → tickSpring() → InvalidateRect / KillTimer
│
└── Page content HWNDs (560 × 520, x=200)  — one per Page, shown/hidden
    ├── PAGE_DASHBOARD  (Phase 10.3)
    ├── PAGE_VISUALS    (Phase 10.5 — existing controls will migrate here)
    ├── PAGE_BEHAVIOR   (Phase 10.5)
    └── PAGE_ABOUT      (Phase 10.6)
```

---

## NavigationManager — Class Contract

### `enum class Page : uint8_t`
```
Dashboard = 0 | Visuals = 1 | Behavior = 2 | About = 3 | Count (sentinel)
```

### Sidebar Layout (logical px at 96 DPI)

```
y=0   ┌──────────────────────┐
      │  AuraShell  (title)  │  HEADER_H = 60px
y=60  ├──────────────────────┤
      │ [●] Dashboard        │  SIDEBAR_ITEM_HEIGHT = 40px
y=100 ├──────────────────────┤
      │ [●] Visuals          │
y=140 ├──────────────────────┤
      │ [●] Behavior         │
y=180 ├──────────────────────┤
      │ [●] About            │
y=220 └──────────────────────┘

Accent bar geometry per item:
  x = 0
  y = HEADER_H + i*SIDEBAR_ITEM_HEIGHT + (SIDEBAR_ITEM_HEIGHT − ACCENT_BAR_H)/2
    = 60 + i*40 + 6  →  { 66, 106, 146, 186 }
  w = SIDEBAR_ACCENT_BAR_W = 3px
  h = SIDEBAR_ACCENT_BAR_H = 28px
```

### Hit-test formula (pure math, no Win32)

```
if sidebarY < HEADER_H            → nullopt  (title area)
itemIndex = (sidebarY - HEADER_H) / SIDEBAR_ITEM_HEIGHT
if itemIndex ≥ Page::Count        → nullopt  (below items)
return static_cast<Page>(itemIndex)
```

### Spring animation flow

```
navigateTo(newPage)
  → m_currentPage = newPage
  → m_targetY = indicatorTargetY(newPage)
  → SetTimer(m_hwnd, SPRING_TIMER, 16, nullptr)
  → m_animating = true

WM_TIMER
  → tickSpring(m_indicatorSpring, m_targetY, 1/60)
  → m_indicatorY = m_indicatorSpring.position
  → InvalidateRect (redraw sidebar)
  → if isSpringSettled: KillTimer → m_animating = false
```

---

## Test Coverage: `tests/unit/test_navigation.cpp`

| # | Test | Category |
|---|------|----------|
| 1 | `hitTest(30)` → nullopt (header area) | Hit-test |
| 2 | `hitTest(80)` → Dashboard (y=80 in item 0) | Hit-test |
| 3 | `hitTest(110)` → Visuals | Hit-test |
| 4 | `hitTest(150)` → Behavior | Hit-test |
| 5 | `hitTest(195)` → About | Hit-test |
| 6 | `hitTest(300)` → nullopt (below all items) | Hit-test |
| 7 | `hitTest(60)` → Dashboard (exact boundary) | Hit-test |
| 8 | `indicatorTargetY(Dashboard)` == 66 | Geometry |
| 9 | `indicatorTargetY(Visuals)` == 106 | Geometry |
| 10 | `indicatorTargetY` values form strict ascending sequence | Geometry |
| 11 | Default `currentPage()` is Dashboard | State |
| 12 | `navigateTo(Visuals)` → `currentPage() == Visuals` | State |
| 13 | Double `navigateTo(same)` is idempotent | State |
| 14 | Spring starts at Dashboard Y and converges to Visuals Y within 400ms | Spring |
| 15 | Spring exhibits overshoot (underdamped) during transition | Spring |
| 16 | After 750ms spring is within 2% of target | Spring |
| 17 | `pageChangedCallback` fires once on `navigateTo` | Callback |
| 18 | `pageChangedCallback` not fired on same-page navigate | Callback |

---

## Files Created / Modified

| File | Action | Status |
|------|--------|--------|
| `src/app/navigation_manager.h` | Created | ✅ |
| `src/app/navigation_manager.cpp` | Created | ✅ |
| `src/app/config_window.h` | Modified — larger client area, NavigationManager member | ✅ |
| `src/app/config_window.cpp` | Modified — WM_CREATE creates sidebar, WM_SIZE relays to manager | ✅ |
| `src/app/CMakeLists.txt` | Modified — adds navigation_manager.cpp | ✅ |
| `tests/unit/test_navigation.cpp` | Created (TDD) | ✅ |
| `tests/CMakeLists.txt` | Modified — adds test_navigation.cpp | ✅ |

---

## Resumption Notes

- **Stopped at**: Phase 10.2 all source written
- **Next action**: Phase 10.3 — Dashboard page content (service status card + live preview canvas)
- **Known blockers**: MSVC environment still requires initialization before first build
- **Watch for**: `WM_MOUSELEAVE` requires `TrackMouseEvent` to be called on every `WM_MOUSEMOVE`. The `TME_LEAVE` tracking is reset each time the mouse re-enters the window — verify the code calls `TrackMouseEvent` inside `WM_MOUSEMOVE`, not just once in `WM_CREATE`

---

*Last Updated: 2026-05-10*  
*Next Review: After Phase 10.3 (Dashboard page)*
