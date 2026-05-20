#pragma once

// page_dashboard.h — AuraShell Redesign (Lively-style Preset Gallery)
//
// Dashboard is now a Lively Wallpaper-inspired preset gallery:
//   y=0..56     Compact page header ("Presets" title)
//   y=64..756   3×3 grid of animated glow preset tiles
//   y=760..800  Collapsible status bar (service health, theme, uptime)
//
// Each tile shows a live D2D glow preview + name + "Apply" on hover.
// Spring-animated hover with quinticEaseOut (reuses CardHover pattern).

#include <Windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>

#include <array>
#include <cstdint>
#include <string>
#include <algorithm>
#include <cmath>

#include "app_client.h"
#include "card_renderer.h"
#include "ui_styles.h"

namespace aura::app {

// ============================================================================
// CardHover — per-tile animation state (POD, zero-heap)
// Reused from Phase 10.5; now drives preset-tile hover alpha.
// ============================================================================

struct CardHover {
    float currentAlpha{0.0f};
    float startAlpha  {0.0f};
    float targetAlpha {0.0f};
    float elapsedMs   {0.0f};

    void setTarget(float const target) noexcept {
        startAlpha  = currentAlpha;
        targetAlpha = target;
        elapsedMs   = 0.0f;
    }

    bool tick(float const dtMs) noexcept {
        if (currentAlpha == targetAlpha) return false;
        elapsedMs += dtMs;
        float const t     = (std::min)(elapsedMs / static_cast<float>(ui::motion::HOVER_ANIM_MS), 1.0f);
        float const eased = ui::motion::quinticEaseOut(t);
        currentAlpha      = startAlpha + (targetAlpha - startAlpha) * eased;
        if (t >= 1.0f) currentAlpha = targetAlpha;
        return currentAlpha != targetAlpha;
    }
};

// ============================================================================
// GlowPreset — defines one entry in the preset gallery
// ============================================================================

struct GlowPreset {
    wchar_t const* name;
    float          accentR, accentG, accentB;  // sRGB
    uint32_t       animSpeedPct;               // 0–200, default 100
    float          glowIntensity;              // 0.0–1.0 multiplier on GLOW_ACTIVE
};

// 12 built-in presets (matching D2DColorGrid colours from page_visuals.h)
static constexpr GlowPreset GLOW_PRESETS[12] = {
    {L"AuraShell",   0.000f, 0.949f, 1.000f, 100, 1.00f},
    {L"Cobalt",      0.000f, 0.471f, 0.831f, 100, 0.90f},
    {L"Ultraviolet", 0.486f, 0.302f, 1.000f, 120, 1.00f},
    {L"Sakura",      1.000f, 0.251f, 0.506f,  80, 0.85f},
    {L"Ember",       1.000f, 0.427f, 0.000f, 140, 0.95f},
    {L"Solar",       1.000f, 0.839f, 0.000f,  90, 0.80f},
    {L"Mint",        0.412f, 0.941f, 0.682f, 100, 0.90f},
    {L"Sky",         0.251f, 0.769f, 1.000f, 110, 0.95f},
    {L"Plasma",      0.878f, 0.251f, 0.984f, 150, 1.00f},
    {L"Coral",       1.000f, 0.322f, 0.322f,  80, 0.85f},
    {L"Teal",        0.392f, 1.000f, 0.855f, 100, 0.90f},
    {L"Slate",       0.471f, 0.565f, 0.612f,  70, 0.60f},
};
static constexpr int32_t PRESET_COUNT = 12;

// ============================================================================
// DashboardPage
// ============================================================================

class DashboardPage {
public:
    explicit DashboardPage(AppClient& client) noexcept;
    ~DashboardPage();

    DashboardPage(DashboardPage const&)            = delete;
    DashboardPage& operator=(DashboardPage const&) = delete;

    [[nodiscard]] bool create(HWND pagePanel, HINSTANCE hInst,
                              ID2D1Factory* factory = nullptr) noexcept;

    void onVisible() noexcept;
    void onHidden()  noexcept;

    // ---- Pure geometry hit-tests (public, testable headless) ----

    // Returns preset index 0–11 for the tile under (x,y), or -1.
    [[nodiscard]] int32_t hitTestPreset(int32_t x, int32_t y) const noexcept;

    // Legacy hit-tests retained so existing TDD tests compile (return -1 — module grid removed).
    [[nodiscard]] int32_t hitTestCard  ([[maybe_unused]] int32_t x, [[maybe_unused]] int32_t y) const noexcept { return -1; }
    [[nodiscard]] int32_t hitTestToggle([[maybe_unused]] int32_t x, [[maybe_unused]] int32_t y) const noexcept { return -1; }

    // Spring state accessors (for unit-test inspection, unchanged)
    [[nodiscard]] CardHover const& tileHover(int32_t idx) const noexcept {
        return (idx >= 0 && idx < PRESET_COUNT) ? m_tileHovers[idx] : m_tileHovers[0];
    }

private:
    // ---- Gallery layout constants (CONTENT_W = 1040 after SIDEBAR_W=240) ----
    static constexpr int  CONTENT_W   = 1040;
    static constexpr int  MARGIN      =   24;
    static constexpr int  TILE_COLS   =    3;
    static constexpr int  TILE_ROWS   =    3;   // show 9 of 12 on screen
    static constexpr int  TILE_GAP    =   16;
    static constexpr int  TILE_W      = (CONTENT_W - MARGIN * 2 - TILE_GAP * (TILE_COLS - 1)) / TILE_COLS; // 320
    static constexpr int  TILE_H      =  220;
    static constexpr int  GALLERY_Y   =   64;   // below compact header

    // Status bar (bottom of page)
    static constexpr int  STATUS_Y    = 760;
    static constexpr int  STATUS_H    =  40;

    // Preview area inside each tile (centred, fills most of tile)
    static constexpr int  PREVIEW_PAD =   8;
    static constexpr int  PREVIEW_H   = TILE_H - 48; // 172px — bottom 48px for name/apply

    // ---- Timer ----
    static constexpr UINT_PTR ANIM_TIMER = 8001;
    static constexpr int32_t  TIMER_MS   = 16;

    // ---- Subclass proc ----
    static LRESULT CALLBACK subclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT handlePanelMsg(HWND, UINT, WPARAM, LPARAM) noexcept;

    // ---- Rendering ----
    void drawGallery(HWND panelHwnd, HDC hdc) noexcept;
    void drawPresetTile(
        ID2D1RenderTarget* rt, HDC hdc,
        int32_t tileIdx, D2D1_RECT_F const& bounds,
        float hoverAlpha, float previewAlpha, bool isSelected
    ) noexcept;
    void drawStatusBar(HWND panelHwnd, HDC hdc) noexcept;

    // ---- Interaction ----
    void applyPreset(int32_t presetIdx) noexcept;
    void refreshServiceStatus() noexcept;
    void onAnimTick() noexcept;

    // ---- Geometry helpers ----
    [[nodiscard]] static D2D1_RECT_F tileBounds(int32_t idx) noexcept;

    // ---- State ----
    HWND       m_pagePanel{nullptr};
    AppClient& m_client;

    // Service status (cached on onVisible)
    bool         m_connected    {false};
    uint32_t     m_uptimeSeconds{0};
    std::wstring m_currentTheme {L"—"};

    // Gallery state
    int32_t m_selectedPreset{0};   // which preset is currently active
    int32_t m_hoveredPreset {-1};  // which tile the cursor is over

    // Per-tile hover animation (zero-heap)
    std::array<CardHover, PRESET_COUNT> m_tileHovers{};

    // Per-tile preview "breathing" alpha (each tile pulses independently)
    std::array<float, PRESET_COUNT> m_tileAlpha{};
    std::array<bool,  PRESET_COUNT> m_tileRising{};

    // D2D — shared factory (non-owning) + owned render target
    ID2D1Factory*                                m_d2dFactory{nullptr};
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget>  m_rt;
};

}  // namespace aura::app
