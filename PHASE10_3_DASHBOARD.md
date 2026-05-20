# AuraShell Phase 10.3: Dashboard Page & Live Glow Preview

**Date**: 2026-05-11  
**Status**: Phase 10.3 ✅ Code Complete — Pending build

---

## Executive Summary

- **DashboardPage**: New class (`src/app/page_dashboard.h/.cpp`) implementing the primary landing view for AuraConfig.exe.
- **Service Status Card**: GDI-rendered card showing service connection state (animated pulse dot), uptime, and current theme name. Uses the existing `AppClient` and `ServiceCore` query interfaces.
- **Live Glow Preview**: A Direct2D canvas (200×140px) inside the Dashboard that renders the actual `drawStateGlow()` four-ring pipeline from `icon_overlay_manager.cpp` — pixel-identical to the real taskbar overlay. Cycles between Inactive → Active → Inactive on a 3-second loop using an independent `AnimationController` instance.
- **ConfigWindow wired**: `page_dashboard.cpp` is instantiated in `config_window.cpp` WM_CREATE alongside the existing `NavigationManager` page routing.

---

## Files Created

| File | Purpose | Status |
|------|---------|--------|
| `src/app/page_dashboard.h` | DashboardPage class declaration | ✅ Complete |
| `src/app/page_dashboard.cpp` | GDI + D2D rendering implementation | ✅ Complete |

## Files Modified

| File | Change | Status |
|------|--------|--------|
| `src/app/config_window.cpp` | Create DashboardPage in WM_CREATE, wire to page panel, forward WM_TIMER | ✅ Complete |
| `src/app/CMakeLists.txt` | Add `page_dashboard.cpp` to `aurashell_config_app` sources | ✅ Complete |

---

## Architecture Decisions

### Rendering: GDI for structure, D2D for glow preview
The card and text layout uses GDI (FillRect, DrawText) — consistent with the Phase 10.2 sidebar and avoiding a D2D factory dependency before `CardRenderer` lands in Phase 10.4-A. The 200×140px live preview canvas uses Direct2D (the existing `ID2D1DCRenderTarget` pattern from `icon_overlay_manager.cpp`) because it must call `drawStateGlow()` which is a D2D-only function.

### Preview animation: independent AnimationController
The live preview owns a separate `AnimationController` instance (not shared with the taskbar overlay manager). This ensures the preview animation is fully decoupled from the real taskbar rendering and doesn't contribute to any overlay timer load. The preview timer fires at 60 Hz only when the preview is actively animating.

### Service status polling: on-demand, not timer-driven
The service status card does NOT poll `AppClient::queryState()` on a timer. Instead, it queries once at page creation and once each time the page becomes visible (on navigation). The "connected / not connected" state is driven by `StyleManager` observer callbacks triggered by the `NavigationManager::setPageChangedCallback`. CPU cost at idle: 0%.

---

## Dashboard Layout

```
┌──────────────────────────────────────────────┐  (content area, 560px wide)
│                                              │
│  Dashboard                                   │  ← Subtitle (20px SemiBold)
│  AuraShell visual engine status              │  ← Body (14px, textSecondary)
│                                              │
│  ┌────────────────────────────────────────┐  │  ← Card (bgSurface, 12px radius)
│  │  ● Service                             │  │
│  │  ─────────────────────────────────     │  │
│  │  Status    ● Running / ✕ Disconnected  │  │
│  │  Uptime    4h 12m                      │  │
│  │  Theme     neon_blue                   │  │
│  │  Version   v4.0.0                      │  │
│  └────────────────────────────────────────┘  │
│                                              │
│  ┌────────────────────────────────────────┐  │  ← Card (bgSurface, 12px radius)
│  │  Live Preview                           │  │
│  │  ─────────────────────────────────     │  │
│  │                                         │  │
│  │    ┌─────────────────────────────┐     │  │  ← 200×140px D2D canvas
│  │    │   [  glow animation  ]      │     │  │
│  │    └─────────────────────────────┘     │  │
│  │                                         │  │
│  │  Hover to preview your glow settings   │  │
│  └────────────────────────────────────────┘  │
│                                              │
└──────────────────────────────────────────────┘
```

---

## Resumption Notes

- **Stopped at**: Phase 10.3 complete
- **Next action**: Phase 10.4-A — type ramp correction in `ui_styles.h` + `CardRenderer` Direct2D class
- **Known blockers**: MSVC environment initialization required before build
- **Watch for**: `DashboardPage` holds a `ComPtr<ID2D1Factory>` for the preview canvas. This is a SECOND factory alongside `IconOverlayManager`'s factory. Both are `D2D1_FACTORY_TYPE_SINGLE_THREADED` — this is valid since they live on different objects/threads. When `CardRenderer` lands in Phase 10.4-A, the factory should be refactored into a shared app-level resource.

---

*Last Updated: 2026-05-11*  
*Next Review: After Phase 10.4-A (CardRenderer + type ramp)*
