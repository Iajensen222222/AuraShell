// card_renderer.cpp — AuraShell Phase 10.4 / Phase 10.5 (hover states, toggles)

#include "card_renderer.h"

#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>
#include <algorithm>

#pragma comment(lib, "d2d1.lib")

namespace aura::app {

// ============================================================================
// drawCard — full card: shadow → fill → border (Phase 10.5: hover-aware)
// ============================================================================

void CardRenderer::drawCard(
    ID2D1RenderTarget* const rt,
    D2D1_RECT_F        const& bounds,
    float              const  hoverAlpha,
    bool               const  withTopHighlight
) noexcept {
    drawShadow(rt, bounds);
    drawGradientFill(rt, bounds);
    drawBorder(rt, bounds, hoverAlpha, withTopHighlight);
}

// ============================================================================
// drawShadow — 4 concentric rings, directional (+1, +2)px shadow
// ============================================================================

void CardRenderer::drawShadow(
    ID2D1RenderTarget* const rt,
    D2D1_RECT_F        const& bounds
) noexcept {
    for (int i = 0; i < SHADOW_RING_COUNT; ++i) {
        D2D1_RECT_F const shadowRect = offsetRect(
            expandRect(bounds, -SHADOW_INSET[i]),
            SHADOW_OFFSET_X, SHADOW_OFFSET_Y
        );
        D2D1_ROUNDED_RECT const rr = {shadowRect, CARD_RADIUS, CARD_RADIUS};

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(rt->CreateSolidColorBrush(
                D2D1::ColorF(0.0f, 0.0f, 0.0f, SHADOW_ALPHA[i]), &brush))) {
            rt->FillRoundedRectangle(rr, brush.Get());
        }
    }
}

// ============================================================================
// drawGradientFill — Lively frosted-glass: semi-transparent white gradient
//
// Instead of a solid dark fill, the card surface is a translucent white layer
// at 7% opacity (top) → 3% opacity (bottom).  Rendered over bgBase #0C0D14
// this produces a genuine glass-panel appearance with visible depth.
// ============================================================================

void CardRenderer::drawGradientFill(
    ID2D1RenderTarget* const rt,
    D2D1_RECT_F        const& bounds
) noexcept {
    D2D1_GRADIENT_STOP const stops[2] = {
        {0.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, GRADIENT_TOP_ALPHA)},
        {1.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f, GRADIENT_BOTTOM_ALPHA)}
    };

    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> pGSC;
    HRESULT hr = rt->CreateGradientStopCollection(
        stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &pGSC
    );
    if (FAILED(hr)) return;

    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> pBrush;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES const props = {
        D2D1::Point2F(bounds.left, bounds.top),
        D2D1::Point2F(bounds.left, bounds.bottom)
    };
    hr = rt->CreateLinearGradientBrush(props, pGSC.Get(), &pBrush);
    if (FAILED(hr)) return;

    rt->FillRoundedRectangle({bounds, CARD_RADIUS, CARD_RADIUS}, pBrush.Get());
}

// ============================================================================
// drawBorder — Lively-style: glass border + top-light highlight + accent glow
//
// Three layers:
//   1. Full rounded-rect border at BORDER_ALPHA (glass edge definition)
//   2. Top-edge highlight line: white → accent as hoverAlpha increases
//   3. Accent "neon tube" glow: second top-edge draw at accent colour × hoverAlpha×0.4
//      This creates the signature Lively micro-detail: a glowing neon top edge on hover.
// ============================================================================

void CardRenderer::drawBorder(
    ID2D1RenderTarget* const rt,
    D2D1_RECT_F        const& bounds,
    float              const  hoverAlpha,
    bool               const  withTopHighlight
) noexcept {
    float const ha = std::clamp(hoverAlpha, 0.0f, 1.0f);

    // Layer 1: full 1px rounded-rect border — brightens slightly on hover.
    {
        float const borderA = BORDER_ALPHA + 0.06f * ha;  // 0.12 → 0.18 on hover
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pBorderBrush;
        if (SUCCEEDED(rt->CreateSolidColorBrush(
                D2D1::ColorF(1.0f, 1.0f, 1.0f, borderA), &pBorderBrush))) {
            rt->DrawRoundedRectangle(
                {bounds, CARD_RADIUS, CARD_RADIUS},
                pBorderBrush.Get(), HIGHLIGHT_THICKNESS
            );
        }
    }

    if (!withTopHighlight) return;

    float const y  = bounds.top + HIGHLIGHT_THICKNESS * 0.5f;
    float const x0 = bounds.left  + CARD_RADIUS;
    float const x1 = bounds.right - CARD_RADIUS;
    if (x1 <= x0) return;

    // Layer 2: top-light highlight — blends white → accent as card is hovered.
    {
        D2D1_COLOR_F const highlightColor = {
            1.0f - ha,             // R: white → accent R (0)
            1.0f - 0.051f * ha,   // G: white → accent G (0.949)
            1.0f,                  // B: stays 1.0 (both white and cyan)
            HIGHLIGHT_ALPHA + (HIGHLIGHT_ALPHA_HOVER - HIGHLIGHT_ALPHA) * ha
        };
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pHighBrush;
        if (SUCCEEDED(rt->CreateSolidColorBrush(highlightColor, &pHighBrush))) {
            rt->DrawLine(D2D1::Point2F(x0, y), D2D1::Point2F(x1, y),
                         pHighBrush.Get(), HIGHLIGHT_THICKNESS);
        }
    }

    // Layer 3: accent neon-tube glow — only visible on hover (Lively micro-detail).
    if (ha > 0.01f) {
        float const glowA = ha * 0.40f;  // accent colour at up to 40% alpha
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pGlowBrush;
        if (SUCCEEDED(rt->CreateSolidColorBrush(
                D2D1::ColorF(0.0f, 0.949f, 1.0f, glowA), &pGlowBrush))) {
            // Draw a slightly thicker glow line just above the highlight.
            float const gy = bounds.top - 0.5f;
            rt->DrawLine(D2D1::Point2F(x0, gy), D2D1::Point2F(x1, gy),
                         pGlowBrush.Get(), 2.0f);
        }
    }
}

// ============================================================================
// drawToggle — D2D toggle switch (track + circular thumb)
// ============================================================================

void CardRenderer::drawToggle(
    ID2D1RenderTarget* const rt,
    D2D1_RECT_F        const& trackBounds,
    bool               const  isOn
) noexcept {
    // Track background: accent cyan when on, dark grey when off.
    D2D1_ROUNDED_RECT const track = {trackBounds, 12.0f, 12.0f};
    {
        D2D1_COLOR_F const trackColor = isOn
            ? D2D1::ColorF(0.0f, 0.949f, 1.0f, 0.85f)   // accent #00F2FF at 85%
            : D2D1::ColorF(0.25f, 0.25f, 0.28f, 1.0f);  // dark grey

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBrush;
        if (SUCCEEDED(rt->CreateSolidColorBrush(trackColor, &trackBrush))) {
            rt->FillRoundedRectangle(track, trackBrush.Get());
        }
    }

    // 1px track border.
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
        if (SUCCEEDED(rt->CreateSolidColorBrush(
                D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f), &borderBrush))) {
            rt->DrawRoundedRectangle(track, borderBrush.Get(), 1.0f);
        }
    }

    // Thumb: white circle.  Position: left side (off) or right side (on).
    float const thumbCx = isOn
        ? trackBounds.right - 4.0f - 8.0f   // right: right-edge - padding - radius
        : trackBounds.left  + 4.0f + 8.0f;  // left:  left-edge + padding + radius
    float const thumbCy = (trackBounds.top + trackBounds.bottom) * 0.5f;

    D2D1_ELLIPSE const thumb = {D2D1::Point2F(thumbCx, thumbCy), 8.0f, 8.0f};
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> thumbBrush;
        if (SUCCEEDED(rt->CreateSolidColorBrush(
                D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &thumbBrush))) {
            rt->FillEllipse(thumb, thumbBrush.Get());
        }
    }
}

}  // namespace aura::app
