// page_visuals.cpp — AuraShell Phase 10.6

#include "page_visuals.h"

#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>
#include <algorithm>
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
// D2DColorGrid — pure geometry
// ============================================================================

int32_t D2DColorGrid::hitTest(
    int32_t const panelX, int32_t const panelY,
    int32_t const originX, int32_t const originY
) noexcept {
    int32_t const lx = panelX - originX;
    int32_t const ly = panelY - originY;
    if (lx < 0 || ly < 0) return -1;

    int32_t const cellW = SWATCH_SIZE + SWATCH_GAP;
    int32_t const cellH = SWATCH_SIZE + SWATCH_GAP;

    int32_t const col = lx / cellW;
    int32_t const row = ly / cellH;

    if (col >= COLS || row >= ROWS) return -1;

    // Check if within the swatch itself (not in the gap).
    if ((lx % cellW) >= SWATCH_SIZE) return -1;
    if ((ly % cellH) >= SWATCH_SIZE) return -1;

    int32_t const idx = row * COLS + col;
    return (idx < PRESET_COUNT) ? idx : -1;
}

D2D1_RECT_F D2DColorGrid::swatchBounds(
    int32_t const idx, int32_t const originX, int32_t const originY
) noexcept {
    int32_t const col = idx % COLS;
    int32_t const row = idx / COLS;
    float   const x   = static_cast<float>(originX + col * (SWATCH_SIZE + SWATCH_GAP));
    float   const y   = static_cast<float>(originY + row * (SWATCH_SIZE + SWATCH_GAP));
    return {x, y, x + SWATCH_SIZE, y + SWATCH_SIZE};
}

void D2DColorGrid::draw(
    ID2D1RenderTarget* const rt,
    int32_t const originX,
    int32_t const originY
) const noexcept {
    for (int32_t i = 0; i < PRESET_COUNT; ++i) {
        D2D1_RECT_F const bounds = swatchBounds(i, originX, originY);
        D2D1_ROUNDED_RECT const rr = {bounds, 6.0f, 6.0f};

        // Fill
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
        rt->CreateSolidColorBrush(PRESETS[i].toD2D(), &fill);
        if (fill) rt->FillRoundedRectangle(rr, fill.Get());

        // Selected: 2px accent-coloured border
        if (i == selectedIndex) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
            rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.9f), &selBrush);
            if (selBrush) rt->DrawRoundedRectangle(rr, selBrush.Get(), 2.0f);

            // Outer glow ring
            D2D1_RECT_F const glow = CardRenderer::expandRect(bounds, 3.0f);
            D2D1_ROUNDED_RECT const glowRR = {glow, 9.0f, 9.0f};
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> glowBrush;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(PRESETS[i].r, PRESETS[i].g, PRESETS[i].b, 0.35f),
                &glowBrush
            );
            if (glowBrush) rt->DrawRoundedRectangle(glowRR, glowBrush.Get(), 1.5f);

        } else if (i == hoveredIndex) {
            // Hover: thin white border
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hov;
            rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.45f), &hov);
            if (hov) rt->DrawRoundedRectangle(rr, hov.Get(), 1.5f);
        }
    }
}

// ============================================================================
// D2DSlider — pure geometry
// ============================================================================

float D2DSlider::valueToX(D2D1_RECT_F const& trackBounds) const noexcept {
    float const ratio = (maxVal > minVal)
        ? (value - minVal) / (maxVal - minVal)
        : 0.0f;
    return trackBounds.left + std::clamp(ratio, 0.0f, 1.0f) *
           (trackBounds.right - trackBounds.left);
}

float D2DSlider::xToValue(
    float              const x,
    D2D1_RECT_F        const& trackBounds,
    float              const minV,
    float              const maxV
) noexcept {
    float const w = trackBounds.right - trackBounds.left;
    if (w <= 0.0f) return minV;
    float const ratio = (x - trackBounds.left) / w;
    return minV + std::clamp(ratio, 0.0f, 1.0f) * (maxV - minV);
}

void D2DSlider::draw(
    ID2D1RenderTarget* const rt,
    D2D1_RECT_F        const& trackBounds,
    D2D1_COLOR_F       const& accentColor
) const noexcept {
    float const thumbX = valueToX(trackBounds);
    float const cy     = (trackBounds.top + trackBounds.bottom) * 0.5f;

    // ---- Track background (dark) ----
    D2D1_RECT_F   const trackFull = {
        trackBounds.left,
        cy - TRACK_H * 0.5f,
        trackBounds.right,
        cy + TRACK_H * 0.5f
    };
    D2D1_ROUNDED_RECT const trackRR = {trackFull, TRACK_H * 0.5f, TRACK_H * 0.5f};
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.2f, 0.22f, 1.0f), &bg);
        if (bg) rt->FillRoundedRectangle(trackRR, bg.Get());
    }

    // ---- Filled portion (accent gradient, left → thumb) ----
    if (thumbX > trackBounds.left + 0.5f) {
        D2D1_RECT_F const filledRect = {
            trackBounds.left,
            cy - TRACK_H * 0.5f,
            thumbX,
            cy + TRACK_H * 0.5f
        };
        D2D1_ROUNDED_RECT const filledRR = {filledRect, TRACK_H * 0.5f, TRACK_H * 0.5f};

        // Gradient: accent full → lighter shade
        D2D1_GRADIENT_STOP const stops[2] = {
            {0.0f, accentColor},
            {1.0f, D2D1::ColorF(accentColor.r * 0.7f + 0.3f,
                                accentColor.g * 0.7f + 0.3f,
                                accentColor.b * 0.7f + 0.3f, 1.0f)}
        };
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> gsc;
        rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2,
                                         D2D1_EXTEND_MODE_CLAMP, &gsc);
        if (gsc) {
            Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> grad;
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES const props = {
                D2D1::Point2F(trackBounds.left, 0),
                D2D1::Point2F(thumbX, 0)
            };
            rt->CreateLinearGradientBrush(props, gsc.Get(), &grad);
            if (grad) rt->FillRoundedRectangle(filledRR, grad.Get());
        }
    }

    // ---- Thumb circle ----
    {
        D2D1_ELLIPSE const thumb = {D2D1::Point2F(thumbX, cy), THUMB_R, THUMB_R};

        // White fill
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wh;
        rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &wh);
        if (wh) rt->FillEllipse(thumb, wh.Get());

        // Accent stroke
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ac;
        rt->CreateSolidColorBrush(accentColor, &ac);
        if (ac) rt->DrawEllipse(thumb, ac.Get(), 1.5f);

        // Hover: additional outer glow ring
        if (hovered) {
            D2D1_ELLIPSE const glow = {D2D1::Point2F(thumbX, cy), THUMB_R + 4.0f, THUMB_R + 4.0f};
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> glowBrush;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(accentColor.r, accentColor.g, accentColor.b, 0.3f),
                &glowBrush
            );
            if (glowBrush) rt->FillEllipse(glow, glowBrush.Get());
        }
    }
}

// ============================================================================
// VisualsPage — construction
// ============================================================================

VisualsPage::VisualsPage(AppClient& client, SettingsManager& settings) noexcept
    : m_client(client), m_settings(settings)
{
    // Seed slider from saved settings.
    m_speedSlider.value = static_cast<float>(settings.getConfig().activeTheme.animSpeedPct);
}

VisualsPage::~VisualsPage() {
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        RemoveWindowSubclass(m_pagePanel, &VisualsPage::subclassProc, 2);
    }
}

// ============================================================================
// create
// ============================================================================

bool VisualsPage::create(HWND const pagePanel, HINSTANCE const hInst,
                         ID2D1Factory* const factory) noexcept {
    m_pagePanel  = pagePanel;
    m_d2dFactory = factory;  // non-owning

    if (m_d2dFactory) {
        D2D1_RENDER_TARGET_PROPERTIES const rtProps = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        m_d2dFactory->CreateDCRenderTarget(&rtProps, m_rt.ReleaseAndGetAddressOf());
    } else {
        aura::logging::Logger::getInstance().warn("visuals", "No D2D factory — D2D disabled");
    }

    // Hex EDIT control (native Win32).
    m_hexEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"#00F2FF",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_UPPERCASE,
        HEX_EDIT_X, HEX_ROW_Y, HEX_EDIT_W, HEX_CTRL_H,
        pagePanel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HEX_EDIT)),
        hInst, nullptr
    );

    m_applyBtn = CreateWindowExW(
        0, L"BUTTON", L"Apply",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        HEX_BTN_X, HEX_ROW_Y, HEX_BTN_W, HEX_CTRL_H,
        pagePanel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HEX_APPLY)),
        hInst, nullptr
    );

    // Seed color grid selection from saved accent color.
    {
        AuraColor const& saved = m_settings.getConfig().activeTheme.accentColor;
        float const r = saved.r / 255.0f;
        float const g = saved.g / 255.0f;
        float const b = saved.b / 255.0f;
        m_colorGrid.selectedIndex = -1;
        for (int32_t i = 0; i < D2DColorGrid::PRESET_COUNT; ++i) {
            float const dr = D2DColorGrid::PRESETS[i].r - r;
            float const dg = D2DColorGrid::PRESETS[i].g - g;
            float const db = D2DColorGrid::PRESETS[i].b - b;
            if (dr * dr + dg * dg + db * db < 0.01f) {
                m_colorGrid.selectedIndex = i;
                break;
            }
        }
    }

    // Subclass page panel (ID=2 to avoid collision with DashboardPage's ID=1).
    SetWindowSubclass(pagePanel, &VisualsPage::subclassProc, 2,
                      reinterpret_cast<DWORD_PTR>(this));

    aura::logging::Logger::getInstance().info("visuals", "VisualsPage created");
    return true;
}

// ============================================================================
// Visibility
// ============================================================================

void VisualsPage::onVisible() noexcept {
    // Refresh slider from latest saved settings.
    m_speedSlider.value = static_cast<float>(
        m_settings.getConfig().activeTheme.animSpeedPct
    );
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
}

void VisualsPage::onHidden() noexcept { /* no-op */ }

void VisualsPage::setOnColorChangedCallback(std::function<void()> cb) noexcept {
    m_onColorChanged = std::move(cb);
}

// ============================================================================
// Color / slider interaction
// ============================================================================

void VisualsPage::applyColor(D2D1_COLOR_F const& color) noexcept {
    // 1. StyleManager — triggers observer (if anything is subscribed).
    StyleManager::getInstance().setCustomAccent(color);
    StyleManager::getInstance().setThemeMode(ThemeMode::Custom);

    // 2. ThemeConfig — convert D2D float → AuraColor uint8.
    ThemeConfig theme = m_settings.getConfig().activeTheme;
    theme.accentColor.r = static_cast<uint8_t>(color.r * 255.0f + 0.5f);
    theme.accentColor.g = static_cast<uint8_t>(color.g * 255.0f + 0.5f);
    theme.accentColor.b = static_cast<uint8_t>(color.b * 255.0f + 0.5f);
    theme.accentColor.a = 255;
    m_settings.setTheme(theme);

    // 3. IPC — push to service (non-blocking; silently ignored if disconnected).
    if (m_client.isConnected()) {
        bool const ok = m_client.pushTheme(theme);
        (void)ok;
    }

    // 4. Notify ConfigWindow → invalidate hero preview.
    if (m_onColorChanged) m_onColorChanged();

    // 5. Repaint this page.
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
}

void VisualsPage::applyColorFromHex() noexcept {
    if (!m_hexEdit) return;

    wchar_t buf[16] = {};
    GetWindowTextW(m_hexEdit, buf, 15);

    // Parse #RRGGBB (with or without leading #).
    wchar_t const* p = buf;
    if (*p == L'#') ++p;

    unsigned int r = 0, g = 0, b = 0;
    if (std::swscanf(p, L"%02X%02X%02X", &r, &g, &b) == 3) {
        m_colorGrid.selectedIndex = -1;  // custom colour — deselect preset
        D2D1_COLOR_F const color = {
            r / 255.0f, g / 255.0f, b / 255.0f, 1.0f
        };
        applyColor(color);
    }
}

void VisualsPage::onSliderChanged(float const value) noexcept {
    m_speedSlider.value = m_speedSlider.clampValue(value);

    ThemeConfig theme = m_settings.getConfig().activeTheme;
    theme.animSpeedPct = static_cast<uint32_t>(m_speedSlider.value + 0.5f);
    m_settings.setTheme(theme);

    if (m_client.isConnected()) {
        bool const ok = m_client.pushTheme(theme);
        (void)ok;
    }

    if (m_pagePanel && IsWindow(m_pagePanel)) {
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
}

// ============================================================================
// Rendering
// ============================================================================

void VisualsPage::drawPageContent(HWND const panelHwnd, HDC const hdc) noexcept {
    RECT clientRect{};
    GetClientRect(panelHwnd, &clientRect);
    int const panelW = clientRect.right;
    int const panelH = clientRect.bottom;
    if (panelW <= 0 || panelH <= 0) return;

    // Allocate top-down ARGB32 DIB.
    HDC const screenDC = GetDC(nullptr);
    if (!screenDC) return;
    HDC const memDC = CreateCompatibleDC(screenDC);
    if (!memDC) { ReleaseDC(nullptr, screenDC); return; }

    BITMAPINFO bmi       = {};
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

    // Background
    {
        RECT const bg = {0, 0, panelW, panelH};
        HBRUSH const hBg = CreateSolidBrush(RGB(10, 10, 15));
        FillRect(memDC, &bg, hBg);
        DeleteObject(hBg);
    }

    // D2D: card backgrounds + color grid + slider
    if (m_rt) {
        RECT const fullRect = {0, 0, panelW, panelH};
        if (SUCCEEDED(m_rt->BindDC(memDC, &fullRect))) {
            UINT const dpi = GetDpiForWindow(panelHwnd ? panelHwnd : GetDesktopWindow());
            m_rt->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
            m_rt->BeginDraw();
            m_rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

            auto makeRect = [](int x, int y, int w, int h) -> D2D1_RECT_F {
                return {static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(x + w), static_cast<float>(y + h)};
            };

            // Card 1: Accent Color
            CardRenderer::drawCard(m_rt.Get(), makeRect(CARD1_X, CARD1_Y, CARD1_W, CARD1_H));
            // Color grid
            m_colorGrid.draw(m_rt.Get(), GRID_X, GRID_Y);

            // Card 2: Glow Animation
            CardRenderer::drawCard(m_rt.Get(), makeRect(CARD2_X, CARD2_Y, CARD2_W, CARD2_H));
            // Slider track bounds
            D2D1_RECT_F const sliderTrack = makeRect(
                SLDR_X, SLDR_ROW_Y + (SLDR_H - static_cast<int>(D2DSlider::TRACK_H)) / 2,
                SLDR_W, static_cast<int>(D2DSlider::TRACK_H)
            );
            D2D1_COLOR_F const accent = StyleManager::getInstance().getAccent();
            m_speedSlider.draw(m_rt.Get(), sliderTrack, accent);

            // Card 3: Options
            CardRenderer::drawCard(m_rt.Get(), makeRect(CARD3_X, CARD3_Y, CARD3_W, CARD3_H));

            // Toggles in Card 3
            ThemeConfig const& cfg = m_settings.getConfig().activeTheme;
            CardRenderer::drawToggle(m_rt.Get(),
                makeRect(TOG_X, TOG_HOVER_Y, TOG_W, TOG_H), cfg.showOnHover);
            CardRenderer::drawToggle(m_rt.Get(),
                makeRect(TOG_X, TOG_GLOW_Y,  TOG_W, TOG_H), cfg.glowEnabled);

            m_rt->EndDraw();
        }
    }

    // Blit D2D content.
    BitBlt(hdc, 0, 0, panelW, panelH, memDC, 0, 0, SRCCOPY);

    // GDI text on top.
    SetBkMode(hdc, TRANSPARENT);

    auto gdiText = [&](wchar_t const* text, int x, int y, int w, int h,
                       int ptSize, int weight, COLORREF color) {
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
        DrawTextW(hdc, text, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(f);
    };

    // Page header
    gdiText(L"Visuals",                       MARGIN, 16, 400, 40, 24, FW_BOLD, RGB(238,238,245));
    gdiText(L"Customize your Aura glow experience", MARGIN, 52, 600, 24, 13, FW_NORMAL, RGB(80,100,120));

    // Eyebrow label above Card 1 (all-caps accent, Lively micro-detail)
    gdiText(L"COLOUR",    CARD1_X+PAD, CARD1_Y - 20, 200, 18, 10, FW_SEMIBOLD, RGB(0, 180, 200));

    // Card 1 header
    gdiText(L"Accent Color", CARD1_X+PAD, CARD1_Y+PAD, 300, 30, 14, FW_SEMIBOLD, RGB(238,238,245));
    gdiText(L"Custom hex:", CARD1_X+PAD, HEX_ROW_Y, HEX_LABEL_W, HEX_CTRL_H, 12, FW_NORMAL, RGB(100,120,140));

    // Eyebrow label above Card 2
    gdiText(L"ANIMATION", CARD2_X+PAD, CARD2_Y - 20, 200, 18, 10, FW_SEMIBOLD, RGB(0, 180, 200));

    // Card 2 header
    gdiText(L"Glow Animation",       CARD2_X+PAD, CARD2_Y+PAD, 300, 30, 14, FW_SEMIBOLD, RGB(238,238,245));
    gdiText(L"Pulse Speed",          CARD2_X+PAD, SLDR_ROW_Y,  SLDR_LABEL_W, SLDR_H, 12, FW_NORMAL, RGB(100,120,140));

    // Slider value
    wchar_t valBuf[16];
    std::swprintf(valBuf, 16, L"%u%%", static_cast<uint32_t>(m_speedSlider.value + 0.5f));
    gdiText(valBuf, SLDR_X + SLDR_W + 8, SLDR_ROW_Y, SLDR_VALUE_W, SLDR_H,
            12, FW_SEMIBOLD, RGB(0, 242, 255));

    // Card 3 header + row labels
    gdiText(L"Options",               CARD3_X+PAD, CARD3_Y+PAD,  300, 30, 13, FW_SEMIBOLD, RGB(240,240,245));
    gdiText(L"Show overlay on hover", CARD3_X+PAD, TOG_HOVER_Y, TOG_X - CARD3_X - PAD*2, TOG_H, 12, FW_NORMAL, RGB(200,200,212));
    gdiText(L"Enable glow effect",    CARD3_X+PAD, TOG_GLOW_Y,  TOG_X - CARD3_X - PAD*2, TOG_H, 12, FW_NORMAL, RGB(200,200,212));

    if (hOldBmp) SelectObject(memDC, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// ============================================================================
// Subclass proc
// ============================================================================

LRESULT CALLBACK VisualsPage::subclassProc(
    HWND const hwnd, UINT const msg,
    WPARAM const wParam, LPARAM const lParam,
    UINT_PTR const /*id*/, DWORD_PTR const ref
) {
    auto* const p = reinterpret_cast<VisualsPage*>(ref);
    if (p) return p->handlePanelMsg(hwnd, msg, wParam, lParam);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT VisualsPage::handlePanelMsg(
    HWND const hwnd, UINT const msg,
    WPARAM const wParam, LPARAM const lParam
) noexcept {
    switch (msg) {

    case WM_ERASEBKGND: {
        HDC const hdc = reinterpret_cast<HDC>(wParam);
        RECT rc{}; GetClientRect(hwnd, &rc);
        HBRUSH const h = CreateSolidBrush(RGB(10, 10, 15));
        FillRect(hdc, &rc, h); DeleteObject(h);
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC const hdc = BeginPaint(hwnd, &ps);
        drawPageContent(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // ---- Color grid click ----
    case WM_LBUTTONDOWN: {
        float const dpiScale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        int32_t const mx = static_cast<int32_t>(GET_X_LPARAM(lParam) / dpiScale);
        int32_t const my = static_cast<int32_t>(GET_Y_LPARAM(lParam) / dpiScale);

        // Color preset swatch
        int32_t const swatchIdx = D2DColorGrid::hitTest(mx, my, GRID_X, GRID_Y);
        if (swatchIdx >= 0) {
            m_colorGrid.selectedIndex = swatchIdx;
            applyColor(D2DColorGrid::PRESETS[swatchIdx].toD2D());
            return 0;
        }

        // Slider track / thumb
        D2D1_RECT_F const sliderHitRect = {
            static_cast<float>(SLDR_X - 12),
            static_cast<float>(SLDR_ROW_Y),
            static_cast<float>(SLDR_X + SLDR_W + 12),
            static_cast<float>(SLDR_ROW_Y + SLDR_H)
        };
        if (mx >= sliderHitRect.left && mx <= sliderHitRect.right &&
            my >= sliderHitRect.top  && my <= sliderHitRect.bottom) {
            D2D1_RECT_F const trackBounds = {
                static_cast<float>(SLDR_X),
                static_cast<float>(SLDR_ROW_Y + (SLDR_H - static_cast<int>(D2DSlider::TRACK_H)) / 2),
                static_cast<float>(SLDR_X + SLDR_W),
                static_cast<float>(SLDR_ROW_Y + (SLDR_H + static_cast<int>(D2DSlider::TRACK_H)) / 2)
            };
            float const v = D2DSlider::xToValue(
                static_cast<float>(mx), trackBounds,
                m_speedSlider.minVal, m_speedSlider.maxVal
            );
            onSliderChanged(v);
            m_draggingSlider = true;
            SetCapture(hwnd);
            return 0;
        }

        // Card 3 toggles
        if (mx >= TOG_X && mx < TOG_X + TOG_W) {
            ThemeConfig theme = m_settings.getConfig().activeTheme;
            if (my >= TOG_HOVER_Y && my < TOG_HOVER_Y + TOG_H) {
                theme.showOnHover = !theme.showOnHover;
                m_settings.setTheme(theme);
                if (m_client.isConnected()) { bool ok = m_client.pushTheme(theme); (void)ok; }
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (my >= TOG_GLOW_Y && my < TOG_GLOW_Y + TOG_H) {
                theme.glowEnabled = !theme.glowEnabled;
                m_settings.setTheme(theme);
                if (m_client.isConnected()) { bool ok = m_client.pushTheme(theme); (void)ok; }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int32_t const mx = GET_X_LPARAM(lParam);
        int32_t const my = GET_Y_LPARAM(lParam);

        if (m_draggingSlider) {
            D2D1_RECT_F const trackBounds = {
                static_cast<float>(SLDR_X),
                0, // y doesn't matter for xToValue
                static_cast<float>(SLDR_X + SLDR_W),
                0
            };
            float const v = D2DSlider::xToValue(
                static_cast<float>(mx), trackBounds,
                m_speedSlider.minVal, m_speedSlider.maxVal
            );
            onSliderChanged(v);
            return 0;
        }

        // Hover on color swatches
        int32_t const newHov = D2DColorGrid::hitTest(mx, my, GRID_X, GRID_Y);
        if (newHov != m_colorGrid.hoveredIndex) {
            m_colorGrid.hoveredIndex = newHov;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        // Hover on slider
        bool const overSlider = (mx >= SLDR_X && mx < SLDR_X + SLDR_W &&
                                  my >= SLDR_ROW_Y && my < SLDR_ROW_Y + SLDR_H);
        if (overSlider != m_speedSlider.hovered) {
            m_speedSlider.hovered = overSlider;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (m_draggingSlider) {
            m_draggingSlider = false;
            ReleaseCapture();
        }
        return 0;

    // Apply hex button
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_HEX_APPLY) {
            applyColorFromHex();
            return 0;
        }
        // Enter key in EDIT
        if (LOWORD(wParam) == ID_HEX_EDIT && HIWORD(wParam) == EN_CHANGE) {
            // Live-preview on change? For now, only apply on button press.
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);

    default:
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
}

}  // namespace aura::app
