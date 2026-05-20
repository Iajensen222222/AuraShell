#pragma once

// card_renderer.h — AuraShell Phase 10.4
//
// CardRenderer: stateless Direct2D card-drawing helper.
//
// Rendering model (3 layers, drawn bottom → top):
//   A. Shadow rings  — 4 concentric transparent rects, offset (+1, +2)px
//   B. Gradient fill — LinearGradientBrush #1E1E1E → #161616 top-to-bottom
//   C. Border        — 1px rounded-rect at 8% white + 1px top-highlight at 20% white
//
// Design constraint: D2D 1.0 only — no ID2D1Effect / ID2D1DeviceContext.
// Shadow depth is simulated with shadow-ring geometry (same pattern as glow overlays).
//
// Gradient brush lifetime: created fresh per drawCard() call, released on
// ComPtr scope exit.  WM_PAINT fires rarely for static card content, so
// this is not a hot-path concern.
//
// Zero-heap contract: no explicit new/delete/malloc in any render path.
// COM objects (ComPtr) are heap-managed by the runtime but never explicitly
// allocated by this code.

#include <Windows.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>
#include <cstdint>

namespace aura::app {

class CardRenderer {
public:
    // ---- Geometry constants (logical px at 96 DPI, testable without D2D) ----

    // Corner radius — rounder for premium Lively feel.
    static constexpr float CARD_RADIUS = 12.0f;  // was 8.0f

    // Shadow ring geometry: 4 rings, richer depth for glass-over-dark effect.
    static constexpr int   SHADOW_RING_COUNT       = 4;
    static constexpr float SHADOW_INSET[4]         = {-8.0f, -6.0f, -4.0f, -2.0f};
    static constexpr float SHADOW_ALPHA[4]         = {0.05f,  0.10f,  0.15f,  0.22f}; // was 0.03/0.06/0.09/0.12
    static constexpr float SHADOW_OFFSET_X         =  1.0f;
    static constexpr float SHADOW_OFFSET_Y         =  3.0f;  // was 2.0f — more directional

    // Frosted-glass fill: semi-transparent white overlays over the dark base.
    // The gradient alpha values drive the glass illusion; RGB stays at 1.0.
    static constexpr float GRADIENT_TOP_ALPHA    = 0.07f;   // 7% white at top
    static constexpr float GRADIENT_BOTTOM_ALPHA = 0.03f;   // 3% white at bottom

    // Retained for backward-compat with TDD tests that check the old constants.
    static constexpr float GRADIENT_TOP_R    = 0x1E / 255.0f;
    static constexpr float GRADIENT_TOP_G    = 0x1E / 255.0f;
    static constexpr float GRADIENT_TOP_B    = 0x1E / 255.0f;
    static constexpr float GRADIENT_BOTTOM_R = 0x16 / 255.0f;
    static constexpr float GRADIENT_BOTTOM_G = 0x16 / 255.0f;
    static constexpr float GRADIENT_BOTTOM_B = 0x16 / 255.0f;

    // Border opacity values — slightly more visible for glass cards.
    static constexpr float BORDER_ALPHA          = 0.12f;   // was 0.08f
    static constexpr float HIGHLIGHT_ALPHA       = 0.28f;   // was 0.20f
    static constexpr float HIGHLIGHT_ALPHA_HOVER = 0.40f;   // was 0.30f
    static constexpr float HIGHLIGHT_THICKNESS   = 1.0f;    // exactly 1px

    // Hover elevation: 1.02× scale centered on card bounds.
    // hoverScale(0.0) = 1.0 (flat), hoverScale(1.0) = 1.02 (elevated).
    [[nodiscard]] static constexpr float hoverScale(float const hoverAlpha) noexcept {
        return 1.0f + 0.02f * hoverAlpha;
    }

    // ---- Public API ----------------------------------------------------------

    // Draw a complete card: shadow → gradient fill → border.
    // hoverAlpha: 0.0 = flat, 1.0 = fully hovered. Drives scale + glow bleed.
    // Scale transform must be applied by the caller via rt->SetTransform().
    static void drawCard(
        ID2D1RenderTarget* rt,
        D2D1_RECT_F const& bounds,
        float              hoverAlpha       = 0.0f,
        bool               withTopHighlight = true
    ) noexcept;

    // Draw only the shadow rings (call before filling the card background).
    static void drawShadow(
        ID2D1RenderTarget* rt,
        D2D1_RECT_F const& bounds
    ) noexcept;

    // Draw only the gradient fill.
    static void drawGradientFill(
        ID2D1RenderTarget* rt,
        D2D1_RECT_F const& bounds
    ) noexcept;

    // Draw only the border.  hoverAlpha drives glow bleed: top highlight blends
    // from white toward the AuraShell accent colour (#00F2FF) as alpha → 1.
    static void drawBorder(
        ID2D1RenderTarget* rt,
        D2D1_RECT_F const& bounds,
        float              hoverAlpha       = 0.0f,
        bool               withTopHighlight = true
    ) noexcept;

    // Draw a D2D toggle switch (track + thumb).
    // trackBounds: 44×24 logical-px rect for the toggle track.
    // isOn: true = accent-coloured track, thumb on right.
    static void drawToggle(
        ID2D1RenderTarget* rt,
        D2D1_RECT_F const& trackBounds,
        bool               isOn
    ) noexcept;

    // Expand bounds outward by `amount` pixels — used internally for shadow rings.
    // Exposed as a constexpr helper so tests can verify ring geometry.
    [[nodiscard]] static constexpr D2D1_RECT_F
    expandRect(D2D1_RECT_F const& r, float amount) noexcept {
        return {r.left - amount, r.top - amount,
                r.right + amount, r.bottom + amount};
    }

    // Offset a rect by (dx, dy).
    [[nodiscard]] static constexpr D2D1_RECT_F
    offsetRect(D2D1_RECT_F const& r, float dx, float dy) noexcept {
        return {r.left + dx, r.top + dy,
                r.right + dx, r.bottom + dy};
    }

private:
    CardRenderer() = delete;  // all methods are static — no instances
};

}  // namespace aura::app
