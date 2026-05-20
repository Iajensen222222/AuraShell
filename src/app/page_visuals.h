#pragma once

// page_visuals.h — AuraShell Phase 10.6
//
// Personalization / Visuals page for AuraConfig.exe.
//
// Contains three value-type helpers (testable without D2D) and the VisualsPage class:
//
//   ColorPreset — constexpr color definition (avoids constexpr D2D1_COLOR_F portability issues)
//   D2DColorGrid — 12 Fluent preset swatches, pure-geometry hit-test
//   D2DSlider    — value slider (0–200 range), pure-geometry math
//   VisualsPage  — page implementation: color card + slider card + options card
//
// Real-time sync on color/speed change:
//   StyleManager::setCustomAccent() → observer fires
//   AppClient::pushTheme()         → live taskbar overlay update
//   m_onColorChanged callback      → ConfigWindow invalidates hero preview

#include <Windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "app_client.h"
#include "settings_manager.h"
#include "card_renderer.h"
#include "ui_styles.h"

namespace aura::app {

// ============================================================================
// ColorPreset — constexpr sRGB colour definition
// Using a plain struct avoids potential MSVC constexpr issues with D2D1_COLOR_F.
// ============================================================================

struct ColorPreset {
    float r, g, b;
    constexpr D2D1_COLOR_F toD2D()      const noexcept { return {r, g, b, 1.0f}; }
    constexpr AuraColor    toAuraColor() const noexcept {
        return {
            static_cast<uint8_t>(r * 255.0f + 0.5f),
            static_cast<uint8_t>(g * 255.0f + 0.5f),
            static_cast<uint8_t>(b * 255.0f + 0.5f),
            255
        };
    }
};

// ============================================================================
// D2DColorGrid — 12 Fluent preset colour swatches (2 rows × 6 cols)
// ============================================================================

struct D2DColorGrid {
    static constexpr int32_t PRESET_COUNT = 12;
    static constexpr int32_t COLS         =  6;
    static constexpr int32_t ROWS         =  2;
    static constexpr int32_t SWATCH_SIZE  = 44;  // logical px
    static constexpr int32_t SWATCH_GAP   =  6;

    // 12 curated Fluent colours (sRGB, not linearised).
    static constexpr ColorPreset PRESETS[PRESET_COUNT] = {
        {0.000f, 0.949f, 1.000f},   //  0  #00F2FF  AuraShell cyan
        {0.000f, 0.471f, 0.831f},   //  1  #0078D4  Windows blue
        {0.486f, 0.302f, 1.000f},   //  2  #7C4DFF  Electric violet
        {1.000f, 0.251f, 0.506f},   //  3  #FF4081  Hot pink
        {1.000f, 0.427f, 0.000f},   //  4  #FF6D00  Orange
        {1.000f, 0.839f, 0.000f},   //  5  #FFD600  Amber
        {0.412f, 0.941f, 0.682f},   //  6  #69F0AE  Mint green
        {0.251f, 0.769f, 1.000f},   //  7  #40C4FF  Sky blue
        {0.878f, 0.251f, 0.984f},   //  8  #E040FB  Magenta
        {1.000f, 0.322f, 0.322f},   //  9  #FF5252  Coral red
        {0.392f, 1.000f, 0.855f},   // 10  #64FFDA  Teal
        {0.471f, 0.565f, 0.612f},   // 11  #78909C  Slate
    };

    int32_t selectedIndex{0};   // 0–11; -1 = custom
    int32_t hoveredIndex {-1};  // -1 = none

    // ---- Pure geometry — testable without D2D or Win32 ----

    // Returns swatch index 0–11 under panel-local (panelX, panelY), or -1 if none.
    // originX/Y: top-left of the grid in page-panel coordinates.
    [[nodiscard]] static int32_t hitTest(
        int32_t panelX, int32_t panelY,
        int32_t originX, int32_t originY
    ) noexcept;

    // Returns the D2D1_RECT_F for swatch `idx` (in panel-local coordinates).
    [[nodiscard]] static D2D1_RECT_F swatchBounds(
        int32_t idx, int32_t originX, int32_t originY
    ) noexcept;

    // ---- D2D rendering ----

    // Draw all swatches into the current BeginDraw/EndDraw scope.
    void draw(
        ID2D1RenderTarget* rt,
        int32_t            originX,
        int32_t            originY
    ) const noexcept;
};

// ============================================================================
// D2DSlider — horizontal value slider
// ============================================================================

struct D2DSlider {
    static constexpr float TRACK_H  =  6.0f;  // track height in logical px
    static constexpr float THUMB_R  = 10.0f;  // thumb radius

    float minVal{  0.0f};
    float maxVal{200.0f};
    float value {100.0f};  // current value (animSpeedPct default)
    bool  hovered{false};

    // ---- Pure geometry — testable without D2D or Win32 ----

    // Returns the X coordinate of the thumb centre within trackBounds.
    [[nodiscard]] float valueToX(D2D1_RECT_F const& trackBounds) const noexcept;

    // Computes a value [minVal, maxVal] from a click X within trackBounds.
    [[nodiscard]] static float xToValue(
        float              x,
        D2D1_RECT_F const& trackBounds,
        float              minV,
        float              maxV
    ) noexcept;

    // Clamp value to [minVal, maxVal].
    [[nodiscard]] float clampValue(float v) const noexcept {
        return std::clamp(v, minVal, maxVal);
    }

    // ---- D2D rendering ----

    // Draw the slider track + fill + thumb into the current D2D scope.
    // accentColor drives the filled portion of the track.
    void draw(
        ID2D1RenderTarget* rt,
        D2D1_RECT_F const& trackBounds,
        D2D1_COLOR_F const& accentColor
    ) const noexcept;
};

// ============================================================================
// VisualsPage
// ============================================================================

class VisualsPage {
public:
    explicit VisualsPage(AppClient& client, SettingsManager& settings) noexcept;
    ~VisualsPage();

    VisualsPage(VisualsPage const&)            = delete;
    VisualsPage& operator=(VisualsPage const&) = delete;

    [[nodiscard]] bool create(HWND pagePanel, HINSTANCE hInst,
                              ID2D1Factory* factory = nullptr) noexcept;

    void onVisible() noexcept;
    void onHidden()  noexcept;

    // Wired by ConfigWindow to invalidate the Dashboard hero preview on color change.
    void setOnColorChangedCallback(std::function<void()> cb) noexcept;

private:
    // ---- Page layout constants (content area 1060px wide) ----
    static constexpr int  CONTENT_W = 1060;
    static constexpr int  MARGIN    =   24;
    static constexpr int  PAD       =   16;

    // Card 1: Accent Color
    static constexpr int  CARD1_X   =   MARGIN;
    static constexpr int  CARD1_Y   =   88;
    static constexpr int  CARD1_W   = CONTENT_W - MARGIN * 2;   // 1012
    static constexpr int  CARD1_H   =  210;

    // Color grid origin (within Card 1, below header)
    static constexpr int  GRID_X    = CARD1_X + PAD;
    static constexpr int  GRID_Y    = CARD1_Y + 56;   // header 44px + gap 12px

    // Hex input row (below grid)
    static constexpr int  HEX_ROW_Y = GRID_Y + D2DColorGrid::ROWS * D2DColorGrid::SWATCH_SIZE
                                              + (D2DColorGrid::ROWS - 1) * D2DColorGrid::SWATCH_GAP
                                              + 12;     // gap after grid
    static constexpr int  HEX_LABEL_W  = 110;
    static constexpr int  HEX_EDIT_W   = 120;
    static constexpr int  HEX_BTN_W    =  80;
    static constexpr int  HEX_CTRL_H   =  26;
    static constexpr int  HEX_EDIT_X   = CARD1_X + PAD + HEX_LABEL_W + 8;
    static constexpr int  HEX_BTN_X    = HEX_EDIT_X + HEX_EDIT_W + 8;

    // Card 2: Glow Animation
    static constexpr int  CARD2_X   =   MARGIN;
    static constexpr int  CARD2_Y   = CARD1_Y + CARD1_H + 16;   // 314
    static constexpr int  CARD2_W   = CARD1_W;
    static constexpr int  CARD2_H   =  148;

    // Slider bounds (within Card 2, below header)
    static constexpr int  SLDR_ROW_Y= CARD2_Y + 56;             // 370
    static constexpr int  SLDR_LABEL_W = 140;
    static constexpr int  SLDR_VALUE_W =  60;
    static constexpr int  SLDR_X    = CARD2_X + PAD + SLDR_LABEL_W + 8;
    static constexpr int  SLDR_W    = CARD2_W - PAD * 2 - SLDR_LABEL_W - SLDR_VALUE_W - 16;
    static constexpr int  SLDR_H    =  32;    // hit-test region height (thumb diameter)

    // Card 3: Options
    static constexpr int  CARD3_X   =   MARGIN;
    static constexpr int  CARD3_Y   = CARD2_Y + CARD2_H + 16;   // 478
    static constexpr int  CARD3_W   = CARD1_W;
    static constexpr int  CARD3_H   =  120;

    // Toggle positions within Card 3
    static constexpr int  TOG_HOVER_Y  = CARD3_Y + 52;
    static constexpr int  TOG_GLOW_Y   = CARD3_Y + 82;
    static constexpr int  TOG_X        = CARD3_X + CARD3_W - PAD - 44;
    static constexpr int  TOG_W        = 44;
    static constexpr int  TOG_H        = 24;

    // ---- Control IDs ----
    static constexpr int  ID_HEX_EDIT  = 2001;
    static constexpr int  ID_HEX_APPLY = 2002;

    // ---- Subclass proc ----
    static LRESULT CALLBACK subclassProc(
        HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR
    );
    LRESULT handlePanelMsg(HWND, UINT, WPARAM, LPARAM) noexcept;

    // ---- Rendering ----
    void drawPageContent(HWND panelHwnd, HDC hdc) noexcept;

    // ---- Interaction ----
    void applyColor(D2D1_COLOR_F const& color)  noexcept;
    void applyColorFromHex()                     noexcept;
    void onSliderChanged(float value)            noexcept;
    void syncToSettings()                        noexcept;

    // ---- State ----
    HWND          m_pagePanel {nullptr};
    HWND          m_hexEdit   {nullptr};
    HWND          m_applyBtn  {nullptr};
    AppClient&    m_client;
    SettingsManager& m_settings;

    D2DColorGrid m_colorGrid{};
    D2DSlider    m_speedSlider{};

    bool m_draggingSlider{false};
    std::function<void()> m_onColorChanged;

    ID2D1Factory*                                m_d2dFactory{nullptr};  // shared, not owned
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget>  m_rt;
};

}  // namespace aura::app
