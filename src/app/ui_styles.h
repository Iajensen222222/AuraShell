#pragma once

// ui_styles.h — AuraShell Phase 10 Design System
//
// Central style sheet for the AuraConfig Win32/Direct2D hybrid UI.
//
// Layout:
//   1. Includes
//   2. Enumerations  (ThemeMode, AppTheme)
//   3. ColorTokens   (one struct, both dark + light variants live in StyleManager)
//   4. SpringState   (plain data — no heap)
//   5. Free functions (tickSpring, isSpringSettled)
//   6. namespace metrics    — spacing / geometry (constexpr, DPI-scaled at call site)
//   7. namespace glow       — effect intensities (constexpr float)
//   8. namespace motion     — spring + duration constants (constexpr)
//   9. namespace typography — font names + sizes (constexpr)
//  10. StyleManager         — singleton, Tri-Mode accent, observer, color math
//
// Zero-heap guarantee:
//   ColorTokens is a plain struct.
//   SpringState is a plain struct.
//   StyleManager observer list is a fixed std::array<...,16> — no std::vector.
//   All constexpr tokens live in .rodata.

#include <Windows.h>
#include <d2d1.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

namespace aura::ui {

// ============================================================================
// 1. Enumerations
// ============================================================================

// Tri-Mode accent resolution strategy.
// Resolution order: System → Signature (#00F2FF) → Custom
//   System   : DwmGetColorizationColor(); fallback to Signature on failure.
//   Signature: Fixed brand colour #00F2FF. No Windows API needed.
//   Custom   : Caller-supplied D2D1_COLOR_F. Fallback to Signature if all-zero.
enum class ThemeMode : uint8_t {
    System,     // Follow Windows accent colour
    Signature,  // AuraShell brand cyan #00F2FF (default)
    Custom,     // User-selected colour stored in AppConfig
};

// Controls which ColorTokens variant StyleManager exposes.
//   FollowSystem: reads AppsUseLightTheme registry key; updates on WM_SETTINGCHANGE.
//   Dark / Light: explicit override persisted in config.json.
enum class AppTheme : uint8_t {
    FollowSystem,
    Dark,
    Light,
};

// ============================================================================
// 2. ColorTokens — all 17 semantic slots
// ============================================================================

// Every field is a semantic name that encodes PURPOSE, not raw value.
// Two instances (dark_, light_) live inside StyleManager.
// Callers receive a const reference — never copy the struct.
struct ColorTokens {
    // Background layers (three depth levels)
    D2D1_COLOR_F bgBase;        // deepest layer — sits behind Mica
    D2D1_COLOR_F bgSurface;     // card / content panel surface
    D2D1_COLOR_F bgElevated;    // tooltip, popup, floating panel

    // Accent — all derived from the resolved accentPrimary by StyleManager.
    D2D1_COLOR_F accentPrimary; // resolved Tri-Mode value at full alpha
    D2D1_COLOR_F accentDim;     // lightness × 0.65 — inactive/muted
    D2D1_COLOR_F accentHover;   // lightness + 0.15 — hover highlight
    D2D1_COLOR_F accentPressed; // lightness − 0.20 — click feedback
    D2D1_COLOR_F accentGlow;    // accentPrimary at α = glow::ACTIVE — ring fill

    // Text
    D2D1_COLOR_F textPrimary;   // main readable text
    D2D1_COLOR_F textSecondary; // labels, captions (reduced contrast)
    D2D1_COLOR_F textDisabled;  // non-interactive text

    // Borders
    D2D1_COLOR_F borderSubtle;  // hairline dividers — low contrast
    D2D1_COLOR_F borderStrong;  // control outlines
    D2D1_COLOR_F borderFocus;   // keyboard focus ring — equals accentPrimary

    // Sidebar navigation rail
    D2D1_COLOR_F sidebarBg;       // semi-transparent, over Mica
    D2D1_COLOR_F sidebarSelected; // selected item pill — accent at low alpha
    D2D1_COLOR_F sidebarHover;    // hover item background
};

// ============================================================================
// 3. SpringState — plain data, stack-allocated
// ============================================================================

struct SpringState {
    float position{0.0f};
    float velocity{0.0f};
};

// ============================================================================
// 4. Spring free functions
// ============================================================================

// Integrate one 16ms tick of a damped harmonic oscillator.
//
//   acceleration = SPRING_STIFFNESS × (target − position)
//                − SPRING_DAMPING   × velocity
//   velocity    += acceleration × dt
//   position    += velocity    × dt
//
// With k=180, b=12: ζ ≈ 0.447 (underdamped, ~20% overshoot, kinetic feel).
// Call at ~60 Hz (dt = 1/60 s) from a WM_TIMER handler.
void tickSpring(SpringState& s, float target, float dt) noexcept;

// Returns true when the spring has converged to within `threshold` of target
// on both position and velocity.  Default threshold matches the 0.2% visual
// resolution used by most 8-bit displays.
[[nodiscard]] bool isSpringSettled(SpringState const& s,
                                   float              target,
                                   float              threshold = 0.002f) noexcept;

// ============================================================================
// 5. namespace metrics — spacing & geometry tokens (all in logical px at 96 DPI)
// ============================================================================

namespace metrics {

// 4-pixel base grid — every spacing value is a multiple of 4.
inline constexpr int32_t SPACE_XS  =  4;
inline constexpr int32_t SPACE_S   =  8;
inline constexpr int32_t SPACE_M   = 12;
inline constexpr int32_t SPACE_L   = 16;
inline constexpr int32_t SPACE_XL  = 24;
inline constexpr int32_t SPACE_XXL = 32;

// Control heights — all multiples of 4.
inline constexpr int32_t CTRL_HEIGHT_COMPACT  = 24;
inline constexpr int32_t CTRL_HEIGHT_DEFAULT  = 32;
inline constexpr int32_t CTRL_HEIGHT_LARGE    = 40;

// Navigation sidebar — widened and taller for Lively-style premium feel
inline constexpr int32_t SIDEBAR_WIDTH        = 240;  // was 200 — more breathing room
inline constexpr int32_t SIDEBAR_ITEM_HEIGHT  =  48;  // was 40 — taller hit targets
inline constexpr int32_t SIDEBAR_ICON_SIZE    =  20;
inline constexpr int32_t SIDEBAR_ACCENT_BAR_W =   3;  // retained for compat; pill replaces it visually
inline constexpr int32_t SIDEBAR_ACCENT_BAR_H =  28;

// Content area
inline constexpr int32_t CONTENT_MARGIN = 24;
inline constexpr int32_t CARD_PADDING   = 16;

// Corner radii (in logical px, cast to float for Direct2D rounded-rect calls).
inline constexpr float RADIUS_SIDEBAR = 0.0f;  // flush to window frame
inline constexpr float RADIUS_CONTROL = 8.0f;  // buttons, toggles, sliders, inputs
inline constexpr float RADIUS_CARD    = 12.0f; // content panels, cards

} // namespace metrics

// ============================================================================
// 6. namespace glow — effect intensity tokens
// ============================================================================

namespace glow {

// Opacity multipliers applied to the animated alpha in drawStateGlow().
// These map directly to the GLOW_ALPHA[4] rings in icon_overlay_manager.cpp.
inline constexpr float AMBIENT      = 0.06f;  // resting — barely perceptible
inline constexpr float HOVER        = 0.35f;  // hovered control / preview panel
inline constexpr float ACTIVE       = 0.58f;  // selected, toggled-on
inline constexpr float FOCUS        = 0.72f;  // keyboard focus ring

inline constexpr float BLUR_RADIUS  = 12.0f;  // Direct2D geometry expansion
inline constexpr float MICA_TINT    = 0.08f;  // accent bleed into Mica backdrop

} // namespace glow

// ============================================================================
// 7. namespace motion — spring & duration constants
// ============================================================================

namespace motion {

// Damped harmonic oscillator parameters.
// ωn = sqrt(STIFFNESS) ≈ 13.4 rad/s
// ζ  = DAMPING / (2 × ωn) ≈ 0.447  (underdamped — "kinetic" overshoot)
inline constexpr float    SPRING_STIFFNESS = 180.0f;
inline constexpr float    SPRING_DAMPING   =  12.0f;

// Timer-based animation durations (milliseconds).
inline constexpr uint32_t ANIM_INSTANT_MS =   0; // no transition
inline constexpr uint32_t ANIM_FAST_MS    = 120; // button press, toggle snap
inline constexpr uint32_t ANIM_MEDIUM_MS  = 240; // panel swap, colour fade
inline constexpr uint32_t ANIM_SPRING_MS  = 280; // sidebar reveal, card slide
inline constexpr uint32_t HOVER_ANIM_MS   = 150; // card hover → elevated transition

// Quintic ease-out: f(t) = 1 − (1−t)⁵
// Starts at maximum velocity and decelerates dramatically.
// Windows 11 standard curve for navigation-level transitions.
// quinticEaseOut(0.0) = 0.0, quinticEaseOut(0.5) ≈ 0.969, quinticEaseOut(1.0) = 1.0
[[nodiscard]] constexpr float quinticEaseOut(float const t) noexcept {
    float const u = 1.0f - t;
    return 1.0f - u * u * u * u * u;
}

} // namespace motion

// ============================================================================
// 8. namespace typography — font name & size tokens
// ============================================================================

namespace typography {

// Segoe UI Variable is the Windows 11 system font.
// "Display" variant optimises for large sizes; "Small" for captions.
inline constexpr wchar_t const* FACE         = L"Segoe UI Variable Display";
inline constexpr wchar_t const* FACE_SMALL   = L"Segoe UI Variable Small";

// Point sizes at 96 DPI — multiply by (dpi / 96.0f) for physical rendering.
inline constexpr float SIZE_CAPTION  = 11.0f;
inline constexpr float SIZE_BODY     = 13.0f;
inline constexpr float SIZE_SUBTITLE = 16.0f;
inline constexpr float SIZE_TITLE    = 20.0f;
inline constexpr float SIZE_DISPLAY  = 28.0f;

// Font weights (DWRITE_FONT_WEIGHT values).
inline constexpr int WEIGHT_REGULAR  = 400;
inline constexpr int WEIGHT_SEMIBOLD = 600;
inline constexpr int WEIGHT_BOLD     = 700;

} // namespace typography

// ============================================================================
// 9. StyleManager — singleton, Tri-Mode accent, dark/light tokens, observers
// ============================================================================

class StyleManager {
public:
    // ---- Singleton ----------------------------------------------------------

    static StyleManager& getInstance();

    // ---- Configuration (safe to call from the UI thread at any time) -------

    // Change the accent resolution mode.  Triggers resolveAccent() →
    // rebuildTokens() → notifies all observers.
    void setThemeMode(ThemeMode mode);

    // Override the accent colour used when mode == ThemeMode::Custom.
    // An all-zero colour (transparent black) is treated as invalid and falls
    // back to the Signature colour.
    void setCustomAccent(D2D1_COLOR_F const& color);

    // Set the dark/light preference.  FollowSystem queries the registry.
    void setAppTheme(AppTheme theme);

    // Call from WM_SETTINGCHANGE (wParam == SPI_SETPERSONALCOLORS or any
    // personalisation change) to refresh the system accent and dark-mode state.
    void onSystemSettingChanged();

    // ---- Queries (const, thread-safe) ---------------------------------------

    [[nodiscard]] ThemeMode          getThemeMode()  const noexcept;
    [[nodiscard]] AppTheme           getAppTheme()   const noexcept;
    [[nodiscard]] D2D1_COLOR_F       getAccent()     const noexcept;
    [[nodiscard]] bool               isDarkTheme()   const noexcept;

    // Returns the live resolved token set for the current dark/light state.
    // The reference is stable for the lifetime of StyleManager but the VALUES
    // it points to may change when rebuildTokens() runs — callers that cache
    // individual fields must re-read after receiving a ThemeChangedCallback.
    [[nodiscard]] ColorTokens const& getTokens()     const noexcept;

    // ---- Color math helpers (static, no heap) --------------------------------

    // Increase HSL lightness by `amount` (clamped to [0, 1]).
    [[nodiscard]] static D2D1_COLOR_F lightenAccent(D2D1_COLOR_F const& base,
                                                    float               amount) noexcept;

    // Decrease HSL lightness by `amount` (clamped to [0, 1]).
    [[nodiscard]] static D2D1_COLOR_F darkenAccent (D2D1_COLOR_F const& base,
                                                    float               amount) noexcept;

    // Replace alpha channel; RGB is preserved exactly.
    [[nodiscard]] static D2D1_COLOR_F withAlpha    (D2D1_COLOR_F const& base,
                                                    float               alpha)  noexcept;

    // ---- DPI scaling helper -------------------------------------------------

    // Scale a logical-pixel constant by the monitor DPI factor.
    // E.g.: scaled(metrics::SPACE_L, 1.5f) == 24 at 144 DPI.
    // Uses rounding (not truncation) for sub-pixel accuracy.
    [[nodiscard]] static int32_t scaled(int32_t logicalPx, float dpiScale) noexcept;

    // ---- Observer (zero-heap — fixed array, registered at startup) ----------

    // Maximum number of simultaneous theme-change listeners.
    static constexpr size_t MAX_OBSERVERS = 16;

    using ThemeChangedCallback = std::function<void()>;

    // Register a callback that fires after rebuildTokens() completes.
    // The callback runs on whichever thread called setThemeMode /
    // setCustomAccent / setAppTheme — typically the UI thread.
    // Registration is permanent for the lifetime of StyleManager.
    // Returns false if the observer slot limit has been reached.
    bool subscribeThemeChanged(ThemeChangedCallback cb);

private:
    // ---- Construction -------------------------------------------------------
    StyleManager();
    ~StyleManager() = default;
    StyleManager(StyleManager const&)            = delete;
    StyleManager& operator=(StyleManager const&) = delete;

    // ---- Internal resolution pipeline ---------------------------------------

    // Step 1: determine accentPrimary_ from themeMode_.
    //   System   → DwmGetColorizationColor().  Falls back to Signature on failure.
    //   Signature→ #00F2FF (R=0, G=0.949, B=1.0).
    //   Custom   → customAccent_, or Signature if all-zero.
    void resolveAccent() noexcept;

    // Step 2: derive all 17 ColorTokens fields for both dark_ and light_.
    void rebuildTokens() noexcept;

    // Step 3: fire all registered observers.
    void notifyObservers() noexcept;

    // Returns true when AppsUseLightTheme registry value is 0 (dark mode on).
    static bool isSystemDark() noexcept;

    // ---- State (mutex-guarded) ----------------------------------------------
    mutable std::mutex mutex_;

    ThemeMode    themeMode_     {ThemeMode::Signature};
    AppTheme     appTheme_      {AppTheme::FollowSystem};
    D2D1_COLOR_F customAccent_  {};          // user-provided; all-zero = invalid
    D2D1_COLOR_F accentPrimary_ {};          // resolved from themeMode_
    bool         isDark_        {true};      // derived from appTheme_ + system query

    // Resolved token sets — rebuildTokens() writes both; getTokens() returns
    // a reference to whichever is active (dark_ or light_).
    ColorTokens dark_  {};
    ColorTokens light_ {};

    // Fixed-size observer array — no heap allocation.
    std::array<ThemeChangedCallback, MAX_OBSERVERS> observers_ {};
    size_t observerCount_ {0};
};

} // namespace aura::ui
