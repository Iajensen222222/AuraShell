// ui_styles.cpp — AuraShell Phase 10 StyleManager implementation
//
// Implements:
//   • Tri-Mode accent resolution (System / Signature / Custom)
//   • Dark/light ColorTokens derivation
//   • HSL color math (lightenAccent, darkenAccent, withAlpha)
//   • Damped harmonic spring integrator (tickSpring, isSpringSettled)
//   • DPI scaling helper
//   • Observer notification (fixed-array, no heap)
//
// All internal helpers have internal linkage (anonymous namespace).
// No heap allocation occurs after startup.

#include "ui_styles.h"

#include <Windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

#pragma comment(lib, "dwmapi.lib")

namespace aura::ui {

// ============================================================================
// Internal — HSL math
// ============================================================================

namespace {

struct Hsl { float h, s, l; };

// Hue helper for hslToRgb.
float hue2rgb(float const p, float const q, float t) noexcept {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

Hsl rgbToHsl(D2D1_COLOR_F const& c) noexcept {
    float const maxC = std::max({c.r, c.g, c.b});
    float const minC = std::min({c.r, c.g, c.b});
    float const d    = maxC - minC;
    float const l    = (maxC + minC) * 0.5f;

    if (d < 1e-6f) {
        return {0.0f, 0.0f, l};  // achromatic
    }

    float const s = (l > 0.5f) ? d / (2.0f - maxC - minC)
                                : d / (maxC + minC);
    float h = 0.0f;
    if      (maxC == c.r) h = (c.g - c.b) / d + (c.g < c.b ? 6.0f : 0.0f);
    else if (maxC == c.g) h = (c.b - c.r) / d + 2.0f;
    else                  h = (c.r - c.g) / d + 4.0f;
    h /= 6.0f;

    return {h, s, l};
}

D2D1_COLOR_F hslToRgb(Hsl const hsl, float const a) noexcept {
    if (hsl.s < 1e-6f) {
        return {hsl.l, hsl.l, hsl.l, a};  // grey
    }
    float const q = (hsl.l < 0.5f) ? hsl.l * (1.0f + hsl.s)
                                    : hsl.l + hsl.s - hsl.l * hsl.s;
    float const p = 2.0f * hsl.l - q;
    return {
        std::clamp(hue2rgb(p, q, hsl.h + 1.0f / 3.0f), 0.0f, 1.0f),
        std::clamp(hue2rgb(p, q, hsl.h              ), 0.0f, 1.0f),
        std::clamp(hue2rgb(p, q, hsl.h - 1.0f / 3.0f), 0.0f, 1.0f),
        a
    };
}

// Builds a D2D1_COLOR_F from 8-bit sRGB values.
constexpr D2D1_COLOR_F rgb8(uint8_t const r, uint8_t const g,
                             uint8_t const b, float const a = 1.0f) noexcept {
    return {r / 255.0f, g / 255.0f, b / 255.0f, a};
}

// AuraShell brand signature colour: #00F2FF
// R=0, G=242, B=255
constexpr D2D1_COLOR_F SIGNATURE_COLOR = rgb8(0, 242, 255);

} // anonymous namespace

// ============================================================================
// Spring integrator — free functions
// ============================================================================

void tickSpring(SpringState& s, float const target, float const dt) noexcept {
    float const acceleration =
          motion::SPRING_STIFFNESS * (target - s.position)
        - motion::SPRING_DAMPING   *  s.velocity;
    s.velocity += acceleration * dt;
    s.position += s.velocity   * dt;
}

bool isSpringSettled(SpringState const& s,
                     float const target,
                     float const threshold) noexcept {
    return std::fabs(s.position - target) < threshold &&
           std::fabs(s.velocity)           < threshold;
}

// ============================================================================
// StyleManager — singleton
// ============================================================================

StyleManager& StyleManager::getInstance() {
    static StyleManager instance;
    return instance;
}

StyleManager::StyleManager() {
    // Initialise with Signature mode, dark theme, and build the first token set.
    resolveAccent();
    isDark_ = isSystemDark();
    rebuildTokens();
}

// ============================================================================
// Configuration setters
// ============================================================================

void StyleManager::setThemeMode(ThemeMode const mode) {
    std::lock_guard<std::mutex> lk(mutex_);
    themeMode_ = mode;
    resolveAccent();
    rebuildTokens();
    notifyObservers();
}

void StyleManager::setCustomAccent(D2D1_COLOR_F const& color) {
    std::lock_guard<std::mutex> lk(mutex_);
    customAccent_ = color;
    if (themeMode_ == ThemeMode::Custom) {
        resolveAccent();
        rebuildTokens();
        notifyObservers();
    }
}

void StyleManager::setAppTheme(AppTheme const theme) {
    std::lock_guard<std::mutex> lk(mutex_);
    appTheme_ = theme;
    isDark_ = (appTheme_ == AppTheme::Dark) ||
              (appTheme_ == AppTheme::FollowSystem && isSystemDark());
    rebuildTokens();
    notifyObservers();
}

void StyleManager::onSystemSettingChanged() {
    std::lock_guard<std::mutex> lk(mutex_);
    bool const changed = (appTheme_ == AppTheme::FollowSystem);
    if (themeMode_ == ThemeMode::System) {
        resolveAccent();  // refresh DWM colour
    }
    if (changed) {
        isDark_ = isSystemDark();
    }
    rebuildTokens();
    notifyObservers();
}

// ============================================================================
// Queries
// ============================================================================

ThemeMode StyleManager::getThemeMode() const noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    return themeMode_;
}

AppTheme StyleManager::getAppTheme() const noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    return appTheme_;
}

D2D1_COLOR_F StyleManager::getAccent() const noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    return accentPrimary_;
}

bool StyleManager::isDarkTheme() const noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    return isDark_;
}

ColorTokens const& StyleManager::getTokens() const noexcept {
    // No lock needed for the reference itself — the pointer is stable.
    // Individual field reads are safe because rebuildTokens writes the inactive
    // variant and then atomically flips isDark_.  Callers that need a fully
    // consistent snapshot should hold the mutex themselves.
    std::lock_guard<std::mutex> lk(mutex_);
    return isDark_ ? dark_ : light_;
}

// ============================================================================
// Color math — static helpers
// ============================================================================

D2D1_COLOR_F StyleManager::lightenAccent(D2D1_COLOR_F const& base,
                                         float         const amount) noexcept {
    Hsl hsl = rgbToHsl(base);
    hsl.l   = std::min(1.0f, hsl.l + amount);
    return hslToRgb(hsl, base.a);
}

D2D1_COLOR_F StyleManager::darkenAccent(D2D1_COLOR_F const& base,
                                        float         const amount) noexcept {
    Hsl hsl = rgbToHsl(base);
    hsl.l   = std::max(0.0f, hsl.l - amount);
    return hslToRgb(hsl, base.a);
}

D2D1_COLOR_F StyleManager::withAlpha(D2D1_COLOR_F const& base,
                                     float         const alpha) noexcept {
    return {base.r, base.g, base.b, alpha};
}

// ============================================================================
// DPI scaling
// ============================================================================

int32_t StyleManager::scaled(int32_t const logicalPx, float const dpiScale) noexcept {
    return static_cast<int32_t>(
        std::round(static_cast<float>(logicalPx) * dpiScale)
    );
}

// ============================================================================
// Observer registration
// ============================================================================

bool StyleManager::subscribeThemeChanged(ThemeChangedCallback cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (observerCount_ >= MAX_OBSERVERS) {
        return false;
    }
    observers_[observerCount_++] = std::move(cb);
    return true;
}

// ============================================================================
// Private — Tri-Mode accent resolution
// ============================================================================

void StyleManager::resolveAccent() noexcept {
    // Called with mutex_ already held.
    switch (themeMode_) {

    case ThemeMode::System: {
        DWORD colorizationColor = 0;
        BOOL  opaqueBlend       = FALSE;
        HRESULT const hr = DwmGetColorizationColor(&colorizationColor, &opaqueBlend);
        if (SUCCEEDED(hr) && colorizationColor != 0) {
            // COLORREF layout from DWM: 0xAARRGGBB
            uint8_t const r = static_cast<uint8_t>((colorizationColor >> 16) & 0xFF);
            uint8_t const g = static_cast<uint8_t>((colorizationColor >>  8) & 0xFF);
            uint8_t const b = static_cast<uint8_t>( colorizationColor        & 0xFF);
            accentPrimary_ = rgb8(r, g, b);
        } else {
            accentPrimary_ = SIGNATURE_COLOR;  // DWM unavailable — use fallback
        }
        break;
    }

    case ThemeMode::Signature:
        accentPrimary_ = SIGNATURE_COLOR;
        break;

    case ThemeMode::Custom: {
        // Treat all-zero (transparent black) as invalid → fallback to Signature.
        bool const valid = (customAccent_.r + customAccent_.g + customAccent_.b) > 1e-4f;
        accentPrimary_ = valid ? customAccent_ : SIGNATURE_COLOR;
        break;
    }
    }
}

// ============================================================================
// Private — ColorTokens derivation
// ============================================================================

void StyleManager::rebuildTokens() noexcept {
    // Called with mutex_ already held.
    //
    // Dark variant:
    {
        ColorTokens& t = dark_;

        // Lively-aesthetic palette: deeper cool-tinted almost-black base,
        // frosted-glass surface cards (transparent overlays not solid fills).
        t.bgBase      = rgb8( 10,  11,  17, 1.0f);  // #0A0B11 — cool deep dark, luminance ≈ 0.044
        t.bgSurface   = {1.0f, 1.0f, 1.0f, 0.05f}; // frosted glass: 5% white over bgBase
        t.bgElevated  = {1.0f, 1.0f, 1.0f, 0.09f}; // slightly brighter frosted layer

        // Accent derivatives.
        t.accentPrimary = accentPrimary_;
        t.accentDim     = darkenAccent (accentPrimary_, 0.18f);
        t.accentHover   = lightenAccent(accentPrimary_, 0.15f);
        t.accentPressed = darkenAccent (accentPrimary_, 0.20f);
        t.accentGlow    = withAlpha    (accentPrimary_, glow::ACTIVE);

        // Text — cleaner hierarchy, slightly warmer whites.
        t.textPrimary   = rgb8(238, 238, 245, 1.0f);  // #EEEEF5 — slightly warmer
        t.textSecondary = rgb8(124, 124, 142, 1.0f);  // #7C7C8E — more muted
        t.textDisabled  = rgb8( 60,  60,  75, 1.0f);  // #3C3C4B

        // Borders — slightly more visible than before for glass card definition.
        t.borderSubtle = {1.0f, 1.0f, 1.0f, 0.07f};  // was 0.05
        t.borderStrong = {1.0f, 1.0f, 1.0f, 0.14f};  // was 0.12
        t.borderFocus  = accentPrimary_;

        // Sidebar — distinct cooler layer over the deep background.
        t.sidebarBg       = {0.039f, 0.043f, 0.078f, 0.82f}; // #0A0B14 at 82%
        t.sidebarSelected = withAlpha(accentPrimary_, 0.22f); // stronger active pill
        t.sidebarHover    = {1.0f, 1.0f, 1.0f, 0.07f};       // was 0.06
    }

    // Light variant:
    {
        ColorTokens& t = light_;

        // Backgrounds — clean near-white.
        t.bgBase      = rgb8(243, 243, 243, 1.0f);  // #F3F3F3
        t.bgSurface   = rgb8(255, 255, 255, 0.85f); // #FFFFFF at 85%
        t.bgElevated  = rgb8(249, 249, 249, 0.95f); // #F9F9F9 at 95%

        // Accent derivatives — slightly darker for visibility on light BG.
        t.accentPrimary = accentPrimary_;
        t.accentDim     = lightenAccent(accentPrimary_, 0.20f);
        t.accentHover   = darkenAccent (accentPrimary_, 0.10f);  // darker hover on light
        t.accentPressed = darkenAccent (accentPrimary_, 0.25f);
        t.accentGlow    = withAlpha    (accentPrimary_, glow::HOVER);

        // Text — near-black.
        t.textPrimary   = rgb8( 26,  26,  26, 1.0f);  // #1A1A1A
        t.textSecondary = rgb8( 85,  85, 101, 1.0f);  // #555565
        t.textDisabled  = rgb8(171, 171, 188, 1.0f);  // #ABABBC

        // Borders — black at low alpha.
        t.borderSubtle = {0.0f, 0.0f, 0.0f, 0.06f};
        t.borderStrong = {0.0f, 0.0f, 0.0f, 0.15f};
        t.borderFocus  = accentPrimary_;

        // Sidebar — semi-transparent light.
        t.sidebarBg       = rgb8(240, 240, 245, 0.78f);
        t.sidebarSelected = withAlpha(accentPrimary_, 0.14f);
        t.sidebarHover    = {0.0f, 0.0f, 0.0f, 0.05f};
    }

    // Resolve dark/light based on appTheme_ + system preference.
    if (appTheme_ == AppTheme::FollowSystem) {
        isDark_ = isSystemDark();
    } else {
        isDark_ = (appTheme_ == AppTheme::Dark);
    }
}

// ============================================================================
// Private — system theme detection
// ============================================================================

bool StyleManager::isSystemDark() noexcept {
    // AppsUseLightTheme: 0 = dark mode, 1 = light mode (absent = dark).
    DWORD value   = 0;
    DWORD cbValue = sizeof(DWORD);
    LSTATUS const st = RegGetValueW(
        HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &cbValue
    );
    if (st != ERROR_SUCCESS) return true;  // default: dark
    return (value == 0);
}

// ============================================================================
// Private — observer notification
// ============================================================================

void StyleManager::notifyObservers() noexcept {
    // Called with mutex_ already held.  Invoke each registered callback.
    // Callbacks must not call back into StyleManager (deadlock).
    for (size_t i = 0; i < observerCount_; ++i) {
        if (observers_[i]) {
            observers_[i]();
        }
    }
}

} // namespace aura::ui
