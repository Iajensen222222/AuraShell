# AuraShell Phase 10: UI Design System & Style Sheet

**Date**: 2026-05-10  
**Status**: Phase 10.1 🔵 In Progress — Style Foundation

---

## Executive Summary

Phase 10 transitions AuraShell from functional Win32 controls to a premium, gaming-grade UI powered by a Direct2D design system. The foundation is a centralized style sheet (`ui_styles.h` / `ui_styles.cpp`) that defines every color, spacing value, motion constant, and glow parameter used across the application — enabling runtime theme switching with a single token swap.

---

## Five Design Decisions (Approved)

| Parameter | Decision |
|-----------|----------|
| **Visual tone** | Enthusiast / Gaming — bold glow rings, neon accent, expressive spring physics |
| **Accent color** | Tri-Mode: System (DWM) → Signature `#00F2FF` (fallback) → Custom (user hex) |
| **Dark mode** | System-responsive **+** explicit user toggle. State persisted in `config.json` |
| **Navigation** | Sidebar (0px radius, flush to frame) with icon + label + vertical accent indicator |
| **Motion** | Kinetic spring physics. Stiffness: 180, Damping: 12. 8px control / 12px card radii |

---

## Color Token Semantic Map

All color names encode **purpose**, never raw values. Changing a theme means changing one definition per token — the entire UI updates automatically.

### Background Layers

| Token | Purpose | Dark value | Light value |
|-------|---------|-----------|------------|
| `bgBase` | Deepest layer (behind Mica) | `#0A0A0F` 100% | `#F3F3F3` 100% |
| `bgSurface` | Card / content panel | `#13131A` 88% | `#FFFFFF` 85% |
| `bgElevated` | Tooltip, popup, dropdown | `#1C1C26` 95% | `#F9F9F9` 95% |

### Accent Derived Variants

All derived from the **resolved accent** (`accentPrimary`) via HSL math in `StyleManager`.

| Token | Derivation | Usage |
|-------|-----------|-------|
| `accentPrimary` | Resolved (System / Signature / Custom) | Active state, selected indicator |
| `accentDim` | Lightness × 0.65 | Inactive / muted accent areas |
| `accentHover` | Lightness + 0.15 | Hover state highlight |
| `accentPressed` | Lightness − 0.20 | Pressed / click feedback |
| `accentGlow` | Alpha × 0.35 | Glow ring outer layer |

### Text

| Token | Dark | Light |
|-------|------|-------|
| `textPrimary` | `#F0F0F5` | `#1A1A1A` |
| `textSecondary` | `#9090A0` | `#555565` |
| `textDisabled` | `#4A4A56` | `#ABABBC` |

### Borders

| Token | Dark | Light |
|-------|------|-------|
| `borderSubtle` | `rgba(255,255,255, 0.05)` | `rgba(0,0,0, 0.06)` |
| `borderStrong` | `rgba(255,255,255, 0.12)` | `rgba(0,0,0, 0.15)` |
| `borderFocus` | `accentPrimary` | `accentPrimary` |

### Sidebar

| Token | Dark | Light |
|-------|------|-------|
| `sidebarBg` | `rgba(10,10,15, 0.70)` | `rgba(240,240,245, 0.75)` |
| `sidebarSelected` | `rgba(accent, 0.18)` | `rgba(accent, 0.14)` |
| `sidebarHover` | `rgba(255,255,255, 0.06)` | `rgba(0,0,0, 0.05)` |

---

## Metric Tokens (4px Grid)

All values are **logical pixels at 96 DPI**. Callers scale via `StyleManager::scaled(value, dpiScale)`.

| Token | Value | Usage |
|-------|-------|-------|
| `SPACE_XS` | 4px | Icon padding, tight gaps |
| `SPACE_S` | 8px | Control inner padding |
| `SPACE_M` | 12px | Row gaps |
| `SPACE_L` | 16px | Section padding |
| `SPACE_XL` | 24px | Card inner padding |
| `SPACE_XXL` | 32px | Page section gaps |
| `CTRL_HEIGHT_COMPACT` | 24px | Compact rows |
| `CTRL_HEIGHT_DEFAULT` | 32px | Standard inputs, buttons |
| `CTRL_HEIGHT_LARGE` | 40px | Prominent actions |
| `SIDEBAR_WIDTH` | 200px | Navigation rail |
| `SIDEBAR_ITEM_HEIGHT` | 40px | Nav item hit target |
| `SIDEBAR_ICON_SIZE` | 20px | Icon render size |
| `CONTENT_MARGIN` | 24px | Content area left/right margin |
| `CARD_PADDING` | 16px | Card interior |

---

## Corner Radii

| Token | Value | Applied to |
|-------|-------|-----------|
| `RADIUS_SIDEBAR` | 0.0px | Sidebar (flush to window frame) |
| `RADIUS_CONTROL` | 8.0px | Buttons, toggles, sliders, inputs |
| `RADIUS_CARD` | 12.0px | Content cards, panels |

---

## Glow Effect Tokens

Maps directly to the `GLOW_RINGS` / `GLOW_ALPHA` constants in `icon_overlay_manager.cpp`.

| Token | Value | Usage |
|-------|-------|-------|
| `GLOW_AMBIENT` | 0.06 | Control resting state — barely perceptible |
| `GLOW_HOVER` | 0.35 | Hovered control / preview |
| `GLOW_ACTIVE` | 0.58 | Selected, toggled-on state |
| `GLOW_FOCUS` | 0.72 | Keyboard focus ring |
| `GLOW_BLUR_RADIUS` | 12.0px | D2D blur for glow spread |
| `MICA_TINT_OPACITY` | 0.08 | Accent color bleed into Mica backdrop |

---

## Motion / Spring Tokens

| Token | Value | Usage |
|-------|-------|-------|
| `SPRING_STIFFNESS` | 180.0 | Spring restoring force constant (k) |
| `SPRING_DAMPING` | 12.0 | Damping coefficient (b) |
| `ANIM_INSTANT_MS` | 0 | No transition |
| `ANIM_FAST_MS` | 120 | Button press, toggle snap |
| `ANIM_MEDIUM_MS` | 240 | Panel transitions, color fade |
| `ANIM_SPRING_MS` | 280 | Sidebar reveal, card slide-in |

**Physics model** (integrated per 16ms tick on UI thread via `WM_TIMER`):
```
acceleration = stiffness × (target − position) − damping × velocity
velocity    += acceleration × dt
position    += velocity × dt
settled      = |velocity| < 0.002 && |target − position| < 0.002
```

With k=180, b=12: ζ ≈ 0.447 (underdamped), ~20% overshoot, settles within ~650ms to 2% threshold. Visually the motion reads as "kinetic" — alive with intention.

---

## Typography Tokens

| Token | Value |
|-------|-------|
| `FONT_FACE` | `"Segoe UI Variable Display"` |
| `FONT_FACE_SMALL` | `"Segoe UI Variable Small"` |
| `FONT_SIZE_CAPTION` | 11.0pt |
| `FONT_SIZE_BODY` | 13.0pt |
| `FONT_SIZE_SUBTITLE` | 16.0pt |
| `FONT_SIZE_TITLE` | 20.0pt |
| `FONT_SIZE_DISPLAY` | 28.0pt |
| `FONT_WEIGHT_REGULAR` | 400 |
| `FONT_WEIGHT_SEMIBOLD` | 600 |
| `FONT_WEIGHT_BOLD` | 700 |

---

## Architecture: `ui_styles.h` / `ui_styles.cpp`

```
ui_styles.h  (pure declaration, all constexpr tokens live here)
│
├── namespace aura::ui
│   ├── enum class ThemeMode : uint8_t  { System, Signature, Custom }
│   ├── enum class AppTheme  : uint8_t  { FollowSystem, Dark, Light }
│   ├── struct ColorTokens              { 17 D2D1_COLOR_F fields }
│   ├── struct SpringState              { float position, velocity }
│   ├── namespace metrics               { all constexpr int32_t tokens }
│   ├── namespace glow                  { all constexpr float tokens }
│   ├── namespace motion                { spring + duration tokens }
│   ├── namespace typography            { font name + size tokens }
│   └── class StyleManager             { singleton, mutable, mutex-guarded }
│
ui_styles.cpp  (runtime logic — no constexpr, no inline)
│
├── StyleManager::resolveAccent()  — reads DWM / uses signature / uses custom
├── StyleManager::rebuildTokens()  — derives all 17 ColorTokens from resolved accent
├── HSL math helpers               — rgbToHsl, hslToRgb, hue2rgb (internal linkage)
├── tickSpring()                   — damped harmonic oscillator integration
└── isSpringSettled()              — convergence detection
```

**Zero-heap guarantee**: `ColorTokens` is a plain struct (stack/static). `SpringState` is a plain struct. Observer callbacks use a fixed `std::array<ThemeChangedCallback, 16>` — no `std::vector`, no dynamic allocation in the style system.

---

## `StyleManager` — Tri-Mode Accent Resolution

```
setThemeMode(System)
    └─ DwmGetColorizationColor()
           ├─ OK  → extract ARGB, normalize to D2D1_COLOR_F
           └─ FAIL → fallback: Signature #00F2FF

setThemeMode(Signature)
    └─ Always #00F2FF (R=0.0, G=0.949, B=1.0)

setThemeMode(Custom)
    └─ customAccent_ (set via setCustomAccent())
           └─ if all-zero (invalid) → fallback: Signature #00F2FF
```

After accent is resolved → `rebuildTokens()` derives hover/pressed/glow variants → notifies all observers.

---

## Test Coverage: `tests/unit/test_ui_logic.cpp`

| # | Test | Category |
|---|------|----------|
| 1 | Default ThemeMode is Signature | StyleManager init |
| 2 | Signature accent is `#00F2FF` | Color resolution |
| 3 | System mode falls back to Signature when DWM unavailable | Fallback |
| 4 | Custom accent survives get/set round-trip | Custom mode |
| 5 | Zero custom accent triggers Signature fallback | Fallback |
| 6 | `lightenAccent` increases HSL luminance | Color math |
| 7 | `darkenAccent` decreases HSL luminance | Color math |
| 8 | `lightenAccent` clamps at L=1.0 (no overflow) | Color math |
| 9 | `darkenAccent` clamps at L=0.0 (no underflow) | Color math |
| 10 | `withAlpha` preserves RGB, sets alpha | Color math |
| 11 | `accentHover` is lighter than `accentPrimary` | Token derivation |
| 12 | `accentPressed` is darker than `accentPrimary` | Token derivation |
| 13 | Observer fires once on `setThemeMode` | Observer |
| 14 | Observer fires on `setCustomAccent` | Observer |
| 15 | Spring reaches target vicinity within 400ms (10% band) | Spring physics |
| 16 | Spring exhibits overshoot (underdamped ζ < 1) | Spring physics |
| 17 | `isSpringSettled` returns true after convergence | Spring physics |
| 18 | `scaled()` returns logical value at 96 DPI (scale=1.0) | DPI |
| 19 | `scaled()` doubles value at 192 DPI (scale=2.0) | DPI |
| 20 | Dark tokens have low luminance for `bgBase` | Token sanity |
| 21 | Light tokens have high luminance for `bgBase` | Token sanity |

---

## Phase 10 Execution Plan

| Sub-phase | Deliverable | Depends on |
|-----------|------------|-----------|
| **10.1** | `ui_styles.h`, `ui_styles.cpp`, `test_ui_logic.cpp` | — |
| **10.2** | `navigation_manager.h/.cpp`, sidebar rendering | 10.1 |
| **10.3** | `page_dashboard.h/.cpp`, live preview canvas | 10.2 |
| **10.4** | `d2d_toggle.h/.cpp`, `d2d_slider.h/.cpp` | 10.1 |
| **10.5** | `page_visuals.h/.cpp`, `page_behavior.h/.cpp` | 10.3, 10.4 |
| **10.6** | `page_about.h/.cpp`, log viewer | 10.2 |
| **10.7** | `UIAnimationTimer`, spring engine wired to controls | 10.4 |
| **10.8** | Full DPI scaling pass, per-monitor V2 validation | 10.7 |

---

## Files Created in Phase 10.1

| File | Status |
|------|--------|
| `src/app/ui_styles.h` | ✅ Complete |
| `src/app/ui_styles.cpp` | ✅ Complete |
| `tests/unit/test_ui_logic.cpp` | ✅ Complete |
| `src/app/CMakeLists.txt` | ✅ Updated |
| `tests/CMakeLists.txt` | ✅ Updated |

---

## Resumption Notes

- **Stopped at**: Phase 10.1 — all style foundation files written
- **Next action**: Phase 10.2 — `NavigationManager` + sidebar rendering using tokens from `StyleManager`
- **Known blockers**: MSVC build environment not initialized (same as previous phases)
- **Watch for**: `DwmGetColorizationColor` requires `dwmapi.lib` (already linked in `config_window.cpp` via `#pragma comment`; ensure `ui_styles.cpp` has the same or that the linker inherits it from `aurashell_config_app`)

---

*Last Updated: 2026-05-10*  
*Next Review: After Phase 10.2 sidebar implementation*
