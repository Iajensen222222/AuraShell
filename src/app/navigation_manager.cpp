// navigation_manager.cpp — AuraShell Phase 10.2

#include "navigation_manager.h"

#include <Windows.h>
#include <windowsx.h>   // GET_X_LPARAM, GET_Y_LPARAM
#include <cmath>
#include <cstring>

#include "logging/logger.h"
#include "ui_styles.h"

namespace aura::app {

using namespace aura::ui;

// ============================================================================
// Static members
// ============================================================================

bool NavigationManager::s_classRegistered = false;

// ============================================================================
// Constructor
// ============================================================================

NavigationManager::NavigationManager() noexcept {
    // Copy the compile-time item definitions.
    for (int32_t i = 0; i < ITEM_COUNT; ++i) {
        m_items[i].label   = NAV_INIT[i].label;
        m_items[i].icon    = NAV_INIT[i].icon;
        m_items[i].content = nullptr;
    }

    // Seed the spring at the Dashboard position so the bar appears instantly
    // on first paint without animation.
    float const dashY       = indicatorTargetY(Page::Dashboard);
    m_indicatorSpring       = {dashY, 0.0f};
    m_indicatorY            = dashY;
    m_targetY               = dashY;
}

NavigationManager::~NavigationManager() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        KillTimer(m_hwnd, SPRING_TIMER);
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// ============================================================================
// Window creation
// ============================================================================

bool NavigationManager::create(HWND const parent, HINSTANCE const hInst) noexcept {
    if (!registerSidebarClass(hInst)) return false;

    RECT parentRect{};
    GetClientRect(parent, &parentRect);
    int32_t const parentH = parentRect.bottom - parentRect.top;

    m_hwnd = CreateWindowExW(
        0,
        SIDEBAR_CLASS,
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0,
        metrics::SIDEBAR_WIDTH, parentH,
        parent,
        nullptr,
        hInst,
        this   // passed to WM_NCCREATE via CREATESTRUCTW
    );

    if (!m_hwnd) {
        aura::logging::Logger::getInstance().error(
            "nav", "Failed to create sidebar window"
        );
        return false;
    }

    aura::logging::Logger::getInstance().info(
        "nav", "Sidebar created successfully"
    );
    return true;
}

void NavigationManager::onParentResize(int32_t const parentH) noexcept {
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, nullptr,
                 0, 0, metrics::SIDEBAR_WIDTH, parentH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

// ============================================================================
// Navigation
// ============================================================================

void NavigationManager::navigateTo(Page const page) noexcept {
    if (page == m_currentPage) return;          // idempotent
    if (page >= Page::Count) return;            // invalid

    m_currentPage = page;

    float const target = indicatorTargetY(page);
    startAnimation(target);
    showPage(page);

    if (m_pageChangedCb) {
        m_pageChangedCb(page);
    }
}

Page NavigationManager::currentPage() const noexcept {
    return m_currentPage;
}

void NavigationManager::registerPageContent(Page const page,
                                            HWND  const contentHwnd) noexcept {
    if (page >= Page::Count) return;
    m_items[static_cast<int32_t>(page)].content = contentHwnd;
}

void NavigationManager::setPageChangedCallback(PageChangedCallback cb) noexcept {
    m_pageChangedCb = std::move(cb);
}

// ============================================================================
// Pure geometry — safe in unit tests (no Win32 required)
// ============================================================================

std::optional<Page> NavigationManager::hitTest(int32_t const sidebarY) const noexcept {
    if (sidebarY < HEADER_H) return std::nullopt;

    int32_t const idx = (sidebarY - HEADER_H) / metrics::SIDEBAR_ITEM_HEIGHT;
    if (idx < 0 || idx >= ITEM_COUNT) return std::nullopt;

    return static_cast<Page>(idx);
}

float NavigationManager::indicatorTargetY(Page const page) const noexcept {
    int32_t const i = static_cast<int32_t>(page);
    return static_cast<float>(
        HEADER_H
        + i * metrics::SIDEBAR_ITEM_HEIGHT
        + (metrics::SIDEBAR_ITEM_HEIGHT - metrics::SIDEBAR_ACCENT_BAR_H) / 2
    );
}

// ============================================================================
// Spring accessors
// ============================================================================

float NavigationManager::indicatorY() const noexcept {
    return m_indicatorY;
}

SpringState const& NavigationManager::indicatorSpring() const noexcept {
    return m_indicatorSpring;
}

float NavigationManager::indicatorSpringTarget() const noexcept {
    return m_targetY;
}

void NavigationManager::setIndicatorSpringForTest(SpringState const& s) noexcept {
    m_indicatorSpring = s;
    m_indicatorY      = s.position;
}

// ============================================================================
// Private — spring & animation
// ============================================================================

void NavigationManager::startAnimation(float const targetY) noexcept {
    m_targetY  = targetY;
    m_animating = true;

    if (m_hwnd) {
        SetTimer(m_hwnd, SPRING_TIMER, TIMER_MS, nullptr);
    }
}

void NavigationManager::onAnimationTick() noexcept {
    constexpr float dt = 1.0f / 60.0f;

    tickSpring(m_indicatorSpring, m_targetY, dt);
    m_indicatorY = m_indicatorSpring.position;

    if (m_hwnd) {
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    if (isSpringSettled(m_indicatorSpring, m_targetY)) {
        m_indicatorY = m_targetY;
        m_indicatorSpring = {m_targetY, 0.0f};
        m_animating  = false;
        if (m_hwnd) {
            KillTimer(m_hwnd, SPRING_TIMER);
        }
    }
}

// ============================================================================
// Private — page content management
// ============================================================================

void NavigationManager::showPage(Page const page) noexcept {
    for (int32_t i = 0; i < ITEM_COUNT; ++i) {
        HWND const content = m_items[i].content;
        if (!content) continue;
        ShowWindow(content,
                   (static_cast<Page>(i) == page) ? SW_SHOW : SW_HIDE);
    }
}

// ============================================================================
// Rendering — GDI (Phase 10.2)
// ============================================================================

void NavigationManager::drawSidebar(HWND const hwnd, HDC const hdc) noexcept {
    RECT clientRect{};
    GetClientRect(hwnd, &clientRect);

    // ---- Background — Lively-style cooler dark panel ----
    // Slightly distinct from the main content area to create visual separation.
    HBRUSH const hBgBrush = CreateSolidBrush(RGB(11, 12, 20));  // #0B0C14
    FillRect(hdc, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    // ---- Right separator (subtle, refined) ----
    RECT const sepRect = {clientRect.right - 1, 0, clientRect.right, clientRect.bottom};
    HBRUSH const hSep = CreateSolidBrush(RGB(30, 32, 48));  // 14% white equiv
    FillRect(hdc, &sepRect, hSep);
    DeleteObject(hSep);

    // ---- Header: "AuraShell" — bold, 18pt, with accent underline ----
    {
        HFONT const hTitleFont = CreateFontW(
            -MulDiv(18, GetDeviceCaps(hdc, LOGPIXELSY), 72),  // 18pt — was 16pt
            0, 0, 0, FW_BOLD,  // Bold — was SemiBold
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI Variable Display"
        );
        HFONT const hOldFont = static_cast<HFONT>(SelectObject(hdc, hTitleFont));
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(238, 238, 245));  // textPrimary

        RECT headerR = {metrics::SPACE_L, 0, clientRect.right, HEADER_H - 12};
        DrawTextW(hdc, L"AuraShell", -1, &headerR, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Version label below title
        HFONT const hVerFont = CreateFontW(
            -MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72),
            0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI Variable Display"
        );
        SelectObject(hdc, hVerFont);
        SetTextColor(hdc, RGB(80, 100, 120));  // dim accent-tinted secondary
        RECT verR = {metrics::SPACE_L, HEADER_H - 18, clientRect.right, HEADER_H};
        DrawTextW(hdc, L"v1.0", -1, &verR, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hOldFont);
        DeleteObject(hTitleFont);
        DeleteObject(hVerFont);

        // Accent underline below header (1px cyan line — Lively separator detail)
        HPEN const hAccentPen = CreatePen(PS_SOLID, 1, RGB(0, 80, 90));
        HPEN const hOldPen    = static_cast<HPEN>(SelectObject(hdc, hAccentPen));
        MoveToEx(hdc, metrics::SPACE_L, HEADER_H - 1, nullptr);
        LineTo(hdc, clientRect.right - metrics::SPACE_L, HEADER_H - 1);
        SelectObject(hdc, hOldPen);
        DeleteObject(hAccentPen);
    }

    // ---- Nav items — Lively-style full-width active pill ----
    {
        HFONT const hItemFont = CreateFontW(
            -MulDiv(13, GetDeviceCaps(hdc, LOGPIXELSY), 72),
            0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI Variable Display"
        );
        HFONT const hSelectedFont = CreateFontW(
            -MulDiv(13, GetDeviceCaps(hdc, LOGPIXELSY), 72),
            0, 0, 0, FW_SEMIBOLD,  // SemiBold for selected item
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI Variable Display"
        );
        SetBkMode(hdc, TRANSPARENT);

        for (int32_t i = 0; i < ITEM_COUNT; ++i) {
            Page  const page       = static_cast<Page>(i);
            bool  const isSelected = (page == m_currentPage);
            bool  const isHovered  = (page == m_hoveredItem) && !isSelected;

            int32_t const itemTop = HEADER_H + i * metrics::SIDEBAR_ITEM_HEIGHT;

            // Lively-style full-width pill (inset 6px from each side, rounded)
            if (isSelected || isHovered) {
                // Selected: deep teal accent fill  |  Hovered: subtle white glow
                COLORREF const pillColor = isSelected
                    ? RGB(0, 45, 50)     // dark accent fill — #002D32
                    : RGB(22, 24, 38);   // hover — slightly lighter than bg
                HBRUSH const hPill = CreateSolidBrush(pillColor);
                RECT const pillRect = {
                    6, itemTop + 4,
                    clientRect.right - 6,
                    itemTop + metrics::SIDEBAR_ITEM_HEIGHT - 4
                };
                // GDI FillRect for the pill background (RoundRect for rounded corners)
                HPEN const hNoPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
                HPEN const hOldP  = static_cast<HPEN>(SelectObject(hdc, hNoPen));
                HBRUSH const hOldB = static_cast<HBRUSH>(SelectObject(hdc, hPill));
                RoundRect(hdc, pillRect.left, pillRect.top,
                               pillRect.right, pillRect.bottom, 8, 8);
                SelectObject(hdc, hOldB);
                SelectObject(hdc, hOldP);
                DeleteObject(hPill);
            }

            // Left accent stripe (2px) — only on selected item
            if (isSelected) {
                HBRUSH const hStripe = CreateSolidBrush(RGB(0, 242, 255));
                RECT const stripeRect = {6, itemTop + 4, 9, itemTop + metrics::SIDEBAR_ITEM_HEIGHT - 4};
                FillRect(hdc, &stripeRect, hStripe);
                DeleteObject(hStripe);
            }

            // Label text — SemiBold when selected, Normal otherwise
            HFONT const hFont = isSelected ? hSelectedFont : hItemFont;
            SelectObject(hdc, hFont);
            SetTextColor(hdc, isSelected
                ? RGB(238, 238, 245)   // textPrimary (bright white)
                : isHovered
                    ? RGB(190, 190, 210) // slightly brightened hover label
                    : RGB(124, 124, 142) // textSecondary (muted)
            );

            // Label sits 16px from left (after stripe + icon space)
            RECT labelRect = {
                metrics::SPACE_XL + 2, itemTop,
                clientRect.right - metrics::SPACE_S,
                itemTop + metrics::SIDEBAR_ITEM_HEIGHT
            };
            DrawTextW(hdc, m_items[i].label, -1, &labelRect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        DeleteObject(hItemFont);
        DeleteObject(hSelectedFont);
    }
}

// ============================================================================
// Window class registration
// ============================================================================

bool NavigationManager::registerSidebarClass(HINSTANCE const hInst) noexcept {
    if (s_classRegistered) return true;

    WNDCLASSEXW wc     = {};
    wc.cbSize          = sizeof(WNDCLASSEXW);
    wc.style           = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc     = sidebarWndProc;
    wc.hInstance       = hInst;
    wc.hCursor         = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground   = nullptr;  // WM_ERASEBKGND suppressed — we paint everything
    wc.lpszClassName   = SIDEBAR_CLASS;

    if (!RegisterClassExW(&wc)) {
        DWORD const err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            aura::logging::Logger::getInstance().error(
                "nav",
                "RegisterClassExW failed for sidebar: " + std::to_string(err)
            );
            return false;
        }
    }
    s_classRegistered = true;
    return true;
}

// ============================================================================
// Window procedure — static trampoline
// ============================================================================

LRESULT CALLBACK NavigationManager::sidebarWndProc(
    HWND const hwnd, UINT const msg, WPARAM const wParam, LPARAM const lParam
) {
    NavigationManager* pThis = nullptr;

    if (msg == WM_NCCREATE) {
        auto const* cs = reinterpret_cast<CREATESTRUCTW const*>(lParam);
        pThis = static_cast<NavigationManager*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<NavigationManager*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA)
        );
    }

    if (pThis) return pThis->handleSidebarMsg(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Window procedure — instance handler
// ============================================================================

LRESULT NavigationManager::handleSidebarMsg(
    HWND const hwnd, UINT const msg,
    WPARAM const wParam, LPARAM const lParam
) noexcept {
    switch (msg) {

    // Suppress GDI background erase to prevent flicker during spring repaints.
    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC const hdc = BeginPaint(hwnd, &ps);
        drawSidebar(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // Hit-test click → navigate.
    case WM_LBUTTONDOWN: {
        int32_t const y = GET_Y_LPARAM(lParam);
        auto const hit  = hitTest(y);
        if (hit.has_value()) {
            navigateTo(hit.value());
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    // Hover tracking — highlights item under cursor.
    case WM_MOUSEMOVE: {
        int32_t const y   = GET_Y_LPARAM(lParam);
        Page    const hit = hitTest(y).value_or(Page::Count);

        if (hit != m_hoveredItem) {
            m_hoveredItem = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
        }

        // Request WM_MOUSELEAVE so we can clear the hover when the cursor
        // leaves the sidebar entirely.  Must be re-requested every WM_MOUSEMOVE.
        TRACKMOUSEEVENT tme  = {};
        tme.cbSize           = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags          = TME_LEAVE;
        tme.hwndTrack        = hwnd;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (m_hoveredItem != Page::Count) {
            m_hoveredItem = Page::Count;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    // Spring animation tick.
    case WM_TIMER:
        if (wParam == SPRING_TIMER) {
            onAnimationTick();
        }
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace aura::app
