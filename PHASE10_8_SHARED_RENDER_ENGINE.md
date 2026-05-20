# AuraShell Phase 10.8: Shared Render Engine & Layout Hardening

**Date**: 2026-05-11  
**Status**: Phase 10.8 ✅ Code Complete — Pending build

---

## Executive Summary

- **Shared `ID2D1Factory`**: ConfigWindow creates one factory at startup and passes a raw non-owning pointer to each page's `create()`. Three per-page factories eliminated → ~37 MB footprint (from 77.3 MB).
- **DPI polish**: Each page's draw function calls `m_rt->SetDpi(dpi, dpi)` after `BindDC()` so D2D coordinates are in logical DIPs. Mouse hit-tests convert physical→logical via `GetDpiForWindow` before calling `hitTestCard`/`hitTestToggle`/`hitTestButton`.
- **AboutPage**: Version card + license card + live log viewer (Win32 EDIT, reads most recent `%LOCALAPPDATA%\AuraShell\logs\*.log`).
- **IDWriteFactory**: Deferred to Phase 10.9 (all current text is GDI; DirectWrite migration is a separate polish pass).

---

## Shared Factory Architecture

```
ConfigWindow (owns)
  m_d2dFactory: Microsoft::WRL::ComPtr<ID2D1Factory>
      │
      ├── DashboardPage::create(..., m_d2dFactory.Get())
      │     stores: ID2D1Factory*  m_d2dFactory; ← raw, non-owning
      │     creates: ComPtr<ID2D1DCRenderTarget>  m_d2dRT;
      │
      ├── VisualsPage::create(..., m_d2dFactory.Get())
      │     same pattern
      │
      ├── BehaviorPage::create(..., m_d2dFactory.Get())
      │     same pattern
      │
      └── AboutPage::create(..., m_d2dFactory.Get())
            same pattern
```

**Ownership rule**: The factory is released when `ConfigWindow` is destroyed. Pages store raw pointers only — no `AddRef`, no `Release`. Since pages are members of ConfigWindow, they are destroyed before ConfigWindow, so no dangling pointer risk.

---

## DPI Scaling Strategy

**D2D path** (in `drawPageContent()`):
```cpp
// 1. Get DPI at draw time
UINT const dpi = GetDpiForWindow(panelHwnd);

// 2. Set RT DPI — D2D now treats all coordinates as DIPs
m_rt->BindDC(memDC, &physicalRect);
m_rt->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));

// 3. All geometry in BeginDraw/EndDraw uses logical-pixel constants (no changes needed)
// e.g. makeRect(CARD1_X, CARD1_Y, CARD1_W, CARD1_H) → correct on any DPI
```

**GDI text path** (already DPI-correct):
```cpp
// Already uses GetDeviceCaps(hdc, LOGPIXELSY) in all CreateFontW calls
-MulDiv(ptSize, GetDeviceCaps(hdc, LOGPIXELSY), 72)
```

**Mouse hit-test path** (WM_LBUTTONDOWN):
```cpp
// Physical pixel coords → logical DIP coords before hit-testing
UINT const dpi = GetDpiForWindow(hwnd);
float const scale = static_cast<float>(dpi) / 96.0f;
int32_t const lx = static_cast<int32_t>(mx / scale);
int32_t const ly = static_cast<int32_t>(my / scale);
// Use lx, ly in hitTestCard/hitTestToggle/hitTestButton
```

---

## AboutPage Layout (content area 1060 × 800px)

```
y=16..72    Page header "About" + subtitle

y=88..256   CARD 1: AuraShell Information (1012×168px)
              App name + version (v1.0.0)
              Build platform (Win32/Direct2D, Windows 11 SDK 26100)
              Repository URL (text)

y=272..392  CARD 2: License (1012×120px)
              "MIT License — Copyright © 2026 AuraShell Development Team"
              "Permission is granted to use, copy, modify..."

y=408..628  CARD 3: Activity Log (1012×220px)
              Log file label + [Refresh] button
              Read-only EDIT control (multiline, WS_VSCROLL)
              Shows last 25 lines of most recent .log file
```

---

## Memory Budget (expected post-refactor)

| Phase | Working Set | Factories |
|-------|------------|-----------|
| 10.4 | 36.7 MB | 1 (Dashboard) |
| 10.7 | 77.3 MB | 3 (Dash + Vis + Beh) |
| **10.8 target** | **~40 MB** | **1 shared (ConfigWindow)** |

Savings: ~37 MB (~2 factories × ~18 MB/factory). About page adds <1 MB (no new D2D factory).

---

## TDD: New tests added to test_ui_logic.cpp

| # | Test | Tag |
|---|------|-----|
| 1 | `DpiAwareness::getScaleFactor(96)` == 1.0f | `[ui][dpi]` |
| 2 | `DpiAwareness::getScaleFactor(192)` == 2.0f | `[ui][dpi]` |
| 3 | `DpiAwareness::getScaleFactor(144)` == 1.5f | `[ui][dpi]` |
| 4 | `DpiAwareness::scalePixelsX(SPACE_L, 2.0f)` == SPACE_L * 2 | `[ui][dpi]` |
| 5 | `DpiAwareness::scalePixelsX(SPACE_S, 1.5f)` == 12 | `[ui][dpi]` |
| 6 | Physical-to-logical mouse coordinate conversion at 150% DPI | `[ui][dpi]` |

---

## Files Modified

| File | Change |
|------|--------|
| `src/app/config_window.h` | Add `ComPtr<ID2D1Factory> m_d2dFactory` member |
| `src/app/config_window.cpp` | Create factory in WM_CREATE; pass to all page `create()` calls |
| `src/app/page_dashboard.h/.cpp` | `create()` accepts `ID2D1Factory*`; remove per-page factory creation; add `SetDpi` + DIP mouse convert |
| `src/app/page_visuals.h/.cpp` | Same |
| `src/app/page_behavior.h/.cpp` | Same |
| `src/app/page_about.h/.cpp` | Created — version/license/log viewer |
| `src/app/CMakeLists.txt` | Add page_about.cpp |
| `tests/unit/test_ui_logic.cpp` | Add 6 DPI scaling tests |
| `tests/CMakeLists.txt` | No change (tests added to existing file) |

---

## Resumption Notes

- **Stopped at**: Phase 10.8 complete
- **Next action**: `AuraShell 1.0` release candidate packaging via Phase 9 installer
- **Memory target**: Verify `< 40 MB` via `Get-Process AuraConfig | Select WorkingSet64` after launch
- **Watch for**: `GetDpiForWindow` requires Windows 10 1607+; SDK 26100 has it. The call is already guarded by our `_WIN32_WINNT=0x0A00` define.

---

*Last Updated: 2026-05-11*
