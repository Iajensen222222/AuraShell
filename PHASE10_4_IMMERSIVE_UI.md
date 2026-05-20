# AuraShell Phase 10.4: Immersive "Lively" UI Overhaul

**Date**: 2026-05-11  
**Status**: Phase 10.4 ✅ Code Complete — Pending build

---

## Executive Summary

Phase 10.4 pivots the visual language from "functional Win32" to the **Lively Wallpaper** aesthetic: deep Mica base, translucent gradient cards with directional shadows, a hero glow preview that dominates the Dashboard, and a 2×2 module grid below it. The window grows to **1280×800** to give the layout breathing room.

---

## Files Created

| File | Purpose | Status |
|------|---------|--------|
| `src/app/card_renderer.h` | CardRenderer declaration: gradient fill, shadow rings, acrylic border | ✅ Complete |
| `src/app/card_renderer.cpp` | CardRenderer implementation (D2D 1.0, zero explicit heap) | ✅ Complete |

## Files Modified

| File | Change | Status |
|------|--------|--------|
| `src/app/config_window.h` | CLIENT_W→1280, CLIENT_H→800, SIDEBAR_W→220, CONTENT_W→1060 | ✅ Complete |
| `src/app/page_dashboard.h` | Add CardRenderer member, hero preview at full width, 4 module card rects | ✅ Complete |
| `src/app/page_dashboard.cpp` | Full rewrite: CardRenderer cards, hero preview with bloom, grid layout | ✅ Complete |
| `src/app/CMakeLists.txt` | Add card_renderer.cpp | ✅ Complete |
| `tests/unit/test_ui_logic.cpp` | Add 5 CardRenderer tests (shadow geometry, gradient tokens, highlight thickness) | ✅ Complete |

---

## CardRenderer Architecture

### Rendering Model (3 layers, bottom → top)

```
Layer A: Shadow rings (4×, offset +1/+2px, alpha 3%→12%)
          Drawn BEFORE the card to go underneath
Layer B: Gradient fill (LinearGradientBrush, top #1E1E1E → bottom #161616)
Layer C: Border (2 strokes):
          • Full rounded-rect at rgba(255,255,255, 0.08) — subtle outline
          • Top edge line at rgba(255,255,255, 0.20) — top-light highlight (1px)
```

### Why D2D 1.0 (not 1.1 effects)

Gaussian blur via `ID2D1Effect` requires `ID2D1DeviceContext` (D2D 1.1) which needs a D3D11 device + DXGI swap chain — a substantial architecture change. Instead, the "shadow" is simulated with 4 concentric shadow rings (same pattern as the glow overlay system). This produces a soft depth effect fully compatible with `ID2D1DCRenderTarget` (D2D 1.0).

### Gradient Brush Lifetime

`ID2D1LinearGradientBrush` is bound to a render target. Cards do NOT repaint at 60 Hz — only on invalidation (theme change, resize, hover). Creating the gradient brush fresh per WM_PAINT is acceptable: it's a cheap COM operation and WM_PAINT fires rarely for static content. The brush is released immediately via ComPtr scope exit — zero leak risk.

---

## Dashboard Grid Layout (1280×800, sidebar 220px, content 1060×800)

```
y=0                                               y=72
  [Page header: "Dashboard" + subtitle]
y=80                                              y=340
  [Hero Preview HWND — 1012×260px — D2D glow]
y=356                                             y=524
  [Service card 498×168] | [IPC card 498×168]
y=540                                             y=708
  [Taskbar card 498×168] | [Overlays card 498×168]
```

Hero preview background: `#0D0D0F` — near-black with blue tint. Bloom layer = wide outer glow ring at 3% alpha rendered first, then the main 4 tight glow rings. Maximum contrast = maximum visual impact.

---

## Module Cards

| Card | Icon | Fields |
|------|------|--------|
| Service | `⬡` | Status dot (green/red), Uptime, Theme, Version |
| IPC | `⬡` | Pipe reachable, Client count (1 = connected), Protocol version |
| Taskbar Engine | `⬡` | Monitor count, Icon count, DPI, Auto-hide state |
| Overlay Renderer | `⬡` | Active overlays, Avg render time, Worst frame, CPU indicator |

---

## TDD: test_ui_logic.cpp Additions (5 new tests)

| # | Test | Tag |
|---|------|-----|
| 1 | Shadow ring 0 expands 8px outward and offsets (+1, +2) | `[ui][card]` |
| 2 | Shadow ring alphas form strict ascending sequence (outer dim → inner darker) | `[ui][card]` |
| 3 | Gradient top color = `#1E1E1E` (normalized) | `[ui][card]` |
| 4 | Gradient bottom color = `#161616` (normalized) | `[ui][card]` |
| 5 | Top-highlight thickness == 1.0f (exactly 1px) | `[ui][card]` |

---

## Resumption Notes

- **Stopped at**: Phase 10.4 complete
- **Next action**: Phase 10.5-A — resizable window + responsive breakpoints (Compact/Standard/Wide sidebar)
- **Known blockers**: MSVC env initialization required for build
- **Watch for**: `ID2D1LinearGradientBrush` requires the gradient stop positions to be in the render target's local coordinate space, not screen coordinates. The `startPoint`/`endPoint` must use the card's local rect bounds, not absolute window coords.

---

*Last Updated: 2026-05-11*  
*Next Review: After Phase 10.5-A (responsive window)*
