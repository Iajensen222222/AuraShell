// page_dashboard.cpp — AuraShell Redesign: Lively-style Preset Gallery

#include "page_dashboard.h"

#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>
#include <cmath>
#include <cstdio>
#include <cwchar>

#include "logging/logger.h"
#include "message_types.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "comctl32.lib")

namespace aura::app {

using namespace aura::ui;

// ============================================================================
// Static members
// ============================================================================

// (no static class-level booleans needed for gallery design)

// ============================================================================
// Constructor
// ============================================================================

DashboardPage::DashboardPage(AppClient& client) noexcept
    : m_client(client)
{
    // Seed each tile's breathing animation at a different phase so they pulse
    // independently (offset by tile index × a small fraction).
    for (int32_t i = 0; i < PRESET_COUNT; ++i) {
        m_tileAlpha[i]  = static_cast<float>(i % 4) * 0.25f;
        m_tileRising[i] = (i % 2 == 0);
    }
}

DashboardPage::~DashboardPage() {
    onHidden();
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        RemoveWindowSubclass(m_pagePanel, &DashboardPage::subclassProc, 1);
    }
}

// ============================================================================
// create
// ============================================================================

bool DashboardPage::create(HWND const pagePanel, HINSTANCE const /*hInst*/,
                            ID2D1Factory* const factory) noexcept {
    m_pagePanel  = pagePanel;
    m_d2dFactory = factory;

    if (m_d2dFactory) {
        D2D1_RENDER_TARGET_PROPERTIES const rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        m_d2dFactory->CreateDCRenderTarget(&rtProps, m_rt.ReleaseAndGetAddressOf());
    }

    SetWindowSubclass(pagePanel, &DashboardPage::subclassProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));

    aura::logging::Logger::getInstance().info("dashboard", "Preset gallery created");
    return true;
}

// ============================================================================
// Visibility
// ============================================================================

void DashboardPage::onVisible() noexcept {
    refreshServiceStatus();
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        SetTimer(m_pagePanel, ANIM_TIMER, TIMER_MS, nullptr);
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
}

void DashboardPage::onHidden() noexcept {
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        KillTimer(m_pagePanel, ANIM_TIMER);
    }
}

// ============================================================================
// Service status
// ============================================================================

void DashboardPage::refreshServiceStatus() noexcept {
    m_connected      = m_client.isConnected();
    m_uptimeSeconds  = 0;
    m_currentTheme   = L"—";

    if (m_connected) {
        aura::ipc::QueryStateResponse resp{};
        if (m_client.queryState(resp)) {
            m_uptimeSeconds = resp.uptimeSeconds;
            m_currentTheme  = std::wstring(resp.currentTheme);
        }
    }
}

// ============================================================================
// Animation tick
// ============================================================================

void DashboardPage::onAnimTick() noexcept {
    constexpr float BREATHE_STEP = 0.012f;  // slower breathing = more premium feel

    bool anyDirty = false;

    // Advance tile hover animations
    for (auto& h : m_tileHovers) {
        if (h.tick(static_cast<float>(TIMER_MS))) anyDirty = true;
    }

    // Advance each tile's breathing alpha (independent phase offsets)
    for (int32_t i = 0; i < PRESET_COUNT; ++i) {
        float const step = BREATHE_STEP * (0.8f + 0.4f * (i % 3) * 0.333f);
        m_tileAlpha[i] += m_tileRising[i] ? step : -step;
        if (m_tileAlpha[i] >= 1.0f) { m_tileAlpha[i] = 1.0f;  m_tileRising[i] = false; }
        if (m_tileAlpha[i] <= 0.2f) { m_tileAlpha[i] = 0.2f;  m_tileRising[i] = true;  }
    }

    if (m_pagePanel && IsWindow(m_pagePanel)) {
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
    (void)anyDirty;
}

// ============================================================================
// Pure geometry
// ============================================================================

D2D1_RECT_F DashboardPage::tileBounds(int32_t const idx) noexcept {
    int32_t const col = idx % TILE_COLS;
    int32_t const row = idx / TILE_COLS;
    float const x = static_cast<float>(MARGIN + col * (TILE_W + TILE_GAP));
    float const y = static_cast<float>(GALLERY_Y + row * (TILE_H + TILE_GAP));
    return {x, y, x + TILE_W, y + TILE_H};
}

int32_t DashboardPage::hitTestPreset(int32_t const px, int32_t const py) const noexcept {
    for (int32_t i = 0; i < TILE_COLS * TILE_ROWS; ++i) {
        D2D1_RECT_F const b = tileBounds(i);
        if (px >= b.left && px < b.right && py >= b.top && py < b.bottom) return i;
    }
    return -1;
}

// ============================================================================
// Apply preset
// ============================================================================

void DashboardPage::applyPreset(int32_t const idx) noexcept {
    if (idx < 0 || idx >= PRESET_COUNT) return;
    m_selectedPreset = idx;

    GlowPreset const& p = GLOW_PRESETS[idx];

    // Update StyleManager — triggers observer chain.
    D2D1_COLOR_F const color = {p.accentR, p.accentG, p.accentB, 1.0f};
    StyleManager::getInstance().setCustomAccent(color);
    StyleManager::getInstance().setThemeMode(ThemeMode::Custom);

    // Build ThemeConfig and push to service.
    if (m_client.isConnected()) {
        // We don't have direct access to SettingsManager here so we use AppClient directly.
        // The theme name is the preset name (narrow conversion).
        // Full settings persistence is handled in VisualsPage.
    }

    if (m_pagePanel && IsWindow(m_pagePanel)) {
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
}

// ============================================================================
// Rendering
// ============================================================================

void DashboardPage::drawPresetTile(
    ID2D1RenderTarget* const rt,
    HDC                const hdc,
    int32_t            const tileIdx,
    D2D1_RECT_F        const& bounds,
    float              const hoverAlpha,
    float              const previewAlpha,
    bool               const isSelected
) noexcept {
    GlowPreset const& p = GLOW_PRESETS[tileIdx];

    // ---- Tile background (CardRenderer glass fill) ----
    CardRenderer::drawCard(rt, bounds, hoverAlpha);

    // Selected tile: accent border overlay
    if (isSelected) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
        rt->CreateSolidColorBrush(D2D1::ColorF(p.accentR, p.accentG, p.accentB, 0.7f),
                                   &selBrush);
        if (selBrush) {
            D2D1_ROUNDED_RECT const rr = {bounds, CardRenderer::CARD_RADIUS, CardRenderer::CARD_RADIUS};
            rt->DrawRoundedRectangle(rr, selBrush.Get(), 2.0f);
        }
    }

    // ---- Glow preview (4-ring pattern in tile accent colour) ----
    D2D1_RECT_F const prevBounds = {
        bounds.left  + PREVIEW_PAD,
        bounds.top   + PREVIEW_PAD,
        bounds.right - PREVIEW_PAD,
        bounds.top   + PREVIEW_H
    };
    float const pw = prevBounds.right - prevBounds.left;
    float const ph = prevBounds.bottom - prevBounds.top;

    // Bloom background ellipse
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bloom;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(p.accentR, p.accentG, p.accentB, 0.04f * previewAlpha), &bloom
        );
        if (bloom) {
            D2D1_ELLIPSE const el = {
                D2D1::Point2F(prevBounds.left + pw * 0.5f, prevBounds.top + ph * 0.5f),
                pw * 0.45f, ph * 0.45f
            };
            rt->FillEllipse(el, bloom.Get());
        }
    }

    // 4 concentric glow rings
    constexpr float RING_ALPHA[] = {0.10f, 0.22f, 0.44f, 0.70f};
    constexpr float RING_INSET[] = {24.0f, 18.0f, 12.0f,  7.0f};
    constexpr float RING_CORNER[]= { 6.0f,  5.0f,  4.5f,  4.0f};

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> glow;
    rt->CreateSolidColorBrush(D2D1::ColorF(p.accentR, p.accentG, p.accentB, 1.0f), &glow);
    if (glow) {
        float const intensity = p.glowIntensity * previewAlpha;
        for (int i = 0; i < 4; ++i) {
            float const s = RING_INSET[i];
            glow->SetColor(D2D1::ColorF(p.accentR, p.accentG, p.accentB,
                                        RING_ALPHA[i] * intensity));
            D2D1_ROUNDED_RECT const rr = D2D1::RoundedRect(
                D2D1::RectF(prevBounds.left + s, prevBounds.top + s,
                            prevBounds.right - s, prevBounds.bottom - s),
                RING_CORNER[i], RING_CORNER[i]
            );
            rt->FillRoundedRectangle(rr, glow.Get());
        }
    }

    // ---- Preset name (GDI, drawn after D2D blit in drawGallery) ----
    // Name drawing is deferred to the GDI pass in drawGallery.
    (void)hdc; // GDI text is drawn in the outer pass
}

void DashboardPage::drawGallery(HWND const panelHwnd, HDC const hdc) noexcept {
    RECT clientRect{};
    GetClientRect(panelHwnd, &clientRect);
    int const panelW = clientRect.right;
    int const panelH = clientRect.bottom;
    if (panelW <= 0 || panelH <= 0) return;

    HDC const screenDC = GetDC(nullptr);
    if (!screenDC) return;
    HDC const memDC = CreateCompatibleDC(screenDC);
    if (!memDC) { ReleaseDC(nullptr, screenDC); return; }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = panelW;
    bmi.bmiHeader.biHeight      = -panelH;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void*   pBits   = nullptr;
    HBITMAP hBitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP hOldBmp = hBitmap ? static_cast<HBITMAP>(SelectObject(memDC, hBitmap)) : nullptr;
    if (!hBitmap) { DeleteDC(memDC); ReleaseDC(nullptr, screenDC); return; }

    // Deep background
    {
        RECT const bg = {0, 0, panelW, panelH};
        HBRUSH const h = CreateSolidBrush(RGB(10, 11, 17));  // #0A0B11
        FillRect(memDC, &bg, h);
        DeleteObject(h);
    }

    // D2D pass: all tile backgrounds + glow previews
    if (m_rt) {
        RECT const fullRect = {0, 0, panelW, panelH};
        if (SUCCEEDED(m_rt->BindDC(memDC, &fullRect))) {
            UINT const dpi = GetDpiForWindow(panelHwnd ? panelHwnd : GetDesktopWindow());
            m_rt->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));

            m_rt->BeginDraw();
            m_rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

            // Draw non-selected, non-hovered tiles first, then selected on top
            for (int32_t i = 0; i < TILE_COLS * TILE_ROWS; ++i) {
                if (i == m_selectedPreset || i == m_hoveredPreset) continue;
                D2D1_RECT_F const b = tileBounds(i);
                drawPresetTile(m_rt.Get(), memDC, i, b,
                               m_tileHovers[i].currentAlpha,
                               m_tileAlpha[i], false);
            }
            // Hovered tile
            if (m_hoveredPreset >= 0 && m_hoveredPreset != m_selectedPreset) {
                D2D1_RECT_F const b = tileBounds(m_hoveredPreset);
                float const s = CardRenderer::hoverScale(m_tileHovers[m_hoveredPreset].currentAlpha);
                D2D1_POINT_2F const c = D2D1::Point2F((b.left+b.right)*0.5f, (b.top+b.bottom)*0.5f);
                m_rt->SetTransform(D2D1::Matrix3x2F::Scale(D2D1::SizeF(s, s), c));
                drawPresetTile(m_rt.Get(), memDC, m_hoveredPreset, b,
                               m_tileHovers[m_hoveredPreset].currentAlpha,
                               m_tileAlpha[m_hoveredPreset], false);
                m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
            }
            // Selected tile on top
            {
                D2D1_RECT_F const b = tileBounds(m_selectedPreset);
                drawPresetTile(m_rt.Get(), memDC, m_selectedPreset, b,
                               m_tileHovers[m_selectedPreset].currentAlpha,
                               m_tileAlpha[m_selectedPreset], true);
            }

            m_rt->EndDraw();
        }
    }

    // Blit D2D content
    BitBlt(hdc, 0, 0, panelW, panelH, memDC, 0, 0, SRCCOPY);

    // GDI text pass: page header + tile names + status bar
    SetBkMode(hdc, TRANSPARENT);

    auto gdiText = [&](wchar_t const* text, int x, int y, int w, int h,
                       int ptSize, int weight, COLORREF color,
                       UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
        HFONT const f = CreateFontW(
            -MulDiv(ptSize, GetDeviceCaps(hdc, LOGPIXELSY), 72),
            0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI Variable Display"
        );
        HFONT const old = static_cast<HFONT>(SelectObject(hdc, f));
        SetTextColor(hdc, color);
        RECT r = {x, y, x+w, y+h};
        DrawTextW(hdc, text, -1, &r, flags);
        SelectObject(hdc, old); DeleteObject(f);
    };

    // Page header — compact for gallery
    gdiText(L"Presets", MARGIN, 6, 400, 34, 22, FW_BOLD, RGB(238, 238, 245));
    gdiText(L"Choose your Aura glow",
            MARGIN, 36, 600, 22, 11, FW_NORMAL, RGB(80, 100, 120));

    // Tile names + "Apply" indicator
    for (int32_t i = 0; i < TILE_COLS * TILE_ROWS; ++i) {
        D2D1_RECT_F const b = tileBounds(i);
        bool const sel = (i == m_selectedPreset);
        bool const hov = (i == m_hoveredPreset);

        int const nameX = static_cast<int>(b.left) + 12;
        int const nameY = static_cast<int>(b.top  + PREVIEW_H + 2);
        int const nameW = TILE_W - 24;
        int const nameH = 28;

        COLORREF const nameColor = sel ? RGB(0, 242, 255)
                                 : hov ? RGB(220, 220, 235)
                                 :       RGB(155, 155, 175);

        gdiText(GLOW_PRESETS[i].name, nameX, nameY, nameW, nameH,
                12, sel ? FW_SEMIBOLD : FW_NORMAL, nameColor);

        // "✓ Applied" badge on selected tile
        if (sel) {
            gdiText(L"✓ Applied",
                    nameX, nameY + 22, nameW, 18,
                    9, FW_NORMAL, RGB(0, 200, 220));
        }
        // "Apply →" hint on hovered tile
        else if (hov) {
            gdiText(L"Apply  →",
                    nameX, nameY + 22, nameW, 18,
                    9, FW_SEMIBOLD, RGB(0, 180, 200));
        }
    }

    // Status bar
    drawStatusBar(panelHwnd, hdc);

    if (hOldBmp) SelectObject(memDC, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

void DashboardPage::drawStatusBar(HWND const /*panelHwnd*/, HDC const hdc) noexcept {
    // Thin dark status bar at the bottom
    RECT const barBg = {0, STATUS_Y, CONTENT_W, STATUS_Y + STATUS_H};
    HBRUSH const hBg = CreateSolidBrush(RGB(8, 9, 16));
    FillRect(hdc, &barBg, hBg);
    DeleteObject(hBg);

    // 1px top separator
    HPEN const hPen = CreatePen(PS_SOLID, 1, RGB(28, 30, 48));
    HPEN const hOld = static_cast<HPEN>(SelectObject(hdc, hPen));
    MoveToEx(hdc, 0, STATUS_Y, nullptr); LineTo(hdc, CONTENT_W, STATUS_Y);
    SelectObject(hdc, hOld); DeleteObject(hPen);

    // Status text
    SetBkMode(hdc, TRANSPARENT);
    wchar_t statusBuf[128];
    if (m_connected) {
        uint32_t const h = m_uptimeSeconds / 3600;
        uint32_t const m = (m_uptimeSeconds % 3600) / 60;
        std::swprintf(statusBuf, 128, L"● AuraShellService  ·  %s  ·  %uh %02um",
                      m_currentTheme.empty() ? L"—" : m_currentTheme.c_str(), h, m);
    } else {
        std::swprintf(statusBuf, 128, L"○ Service not connected");
    }

    HFONT const f = CreateFontW(
        -MulDiv(11, GetDeviceCaps(hdc, LOGPIXELSY), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI Variable Display"
    );
    HFONT const old = static_cast<HFONT>(SelectObject(hdc, f));
    SetTextColor(hdc, m_connected ? RGB(0, 200, 180) : RGB(100, 100, 120));
    RECT r = {MARGIN, STATUS_Y, CONTENT_W - MARGIN, STATUS_Y + STATUS_H};
    DrawTextW(hdc, statusBuf, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, old); DeleteObject(f);
}

// ============================================================================
// Subclass proc
// ============================================================================

LRESULT CALLBACK DashboardPage::subclassProc(
    HWND const hwnd, UINT const msg,
    WPARAM const wParam, LPARAM const lParam,
    UINT_PTR const /*id*/, DWORD_PTR const ref
) {
    auto* const p = reinterpret_cast<DashboardPage*>(ref);
    if (p) return p->handlePanelMsg(hwnd, msg, wParam, lParam);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT DashboardPage::handlePanelMsg(
    HWND const hwnd, UINT const msg,
    WPARAM const wParam, LPARAM const lParam
) noexcept {
    switch (msg) {

    case WM_ERASEBKGND: {
        HDC const h = reinterpret_cast<HDC>(wParam);
        RECT rc{}; GetClientRect(hwnd, &rc);
        HBRUSH const hBg = CreateSolidBrush(RGB(10, 11, 17));
        FillRect(h, &rc, hBg); DeleteObject(hBg);
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC const hdc = BeginPaint(hwnd, &ps);
        drawGallery(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wParam == ANIM_TIMER) onAnimTick();
        return 0;

    case WM_MOUSEMOVE: {
        float const dpiScale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        int32_t const mx = static_cast<int32_t>(GET_X_LPARAM(lParam) / dpiScale);
        int32_t const my = static_cast<int32_t>(GET_Y_LPARAM(lParam) / dpiScale);

        int32_t const newHov = hitTestPreset(mx, my);
        if (newHov != m_hoveredPreset) {
            if (m_hoveredPreset >= 0) m_tileHovers[m_hoveredPreset].setTarget(0.0f);
            m_hoveredPreset = newHov;
            if (newHov >= 0) m_tileHovers[newHov].setTarget(1.0f);
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd};
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (m_hoveredPreset >= 0) {
            m_tileHovers[m_hoveredPreset].setTarget(0.0f);
            m_hoveredPreset = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        float const dpiScale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        int32_t const mx = static_cast<int32_t>(GET_X_LPARAM(lParam) / dpiScale);
        int32_t const my = static_cast<int32_t>(GET_Y_LPARAM(lParam) / dpiScale);

        int32_t const hit = hitTestPreset(mx, my);
        if (hit >= 0) applyPreset(hit);
        return 0;
    }

    default:
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
}

}  // namespace aura::app
