// page_desktop_items.cpp — AuraShell Phase 10.9

#include "page_desktop_items.h"

#include <Windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>
#include <algorithm>
#include <cstdio>
#include <cwchar>

#include "logging/logger.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")

namespace aura::app {

using namespace aura::shell;

// ============================================================================
// Destruction
// ============================================================================

DesktopItemsPage::~DesktopItemsPage() {
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        DragAcceptFiles(m_pagePanel, FALSE);
        RemoveWindowSubclass(m_pagePanel, &DesktopItemsPage::subclassProc, 5);
    }
}

// ============================================================================
// create
// ============================================================================

bool DesktopItemsPage::create(HWND const pagePanel, HINSTANCE const hInst,
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

    // Enable drag-and-drop onto this panel.
    DragAcceptFiles(pagePanel, TRUE);
    // WS_EX_ACCEPTFILES on the child is also needed.
    LONG_PTR const style = GetWindowLongPtrW(pagePanel, GWL_EXSTYLE);
    SetWindowLongPtrW(pagePanel, GWL_EXSTYLE, style | WS_EX_ACCEPTFILES);

    // Buttons
    m_btnBrowse = CreateWindowExW(0, L"BUTTON", L"Browse…",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        BTN_BROWSE_X, BTN_Y, BTN_W, BTN_H,
        pagePanel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_BROWSE)),
        hInst, nullptr);

    m_btnApply = CreateWindowExW(0, L"BUTTON", L"Apply",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        BTN_APPLY_X, BTN_Y, BTN_W, BTN_H,
        pagePanel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_APPLY)),
        hInst, nullptr);

    m_btnRestore = CreateWindowExW(0, L"BUTTON", L"Restore Default",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        BTN_RESTORE_X, BTN_Y, BTN_W + 20, BTN_H,
        pagePanel, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_RESTORE)),
        hInst, nullptr);

    // Subclass ID=5 (others use 1-4).
    SetWindowSubclass(pagePanel, &DesktopItemsPage::subclassProc, 5,
                      reinterpret_cast<DWORD_PTR>(this));

    aura::logging::Logger::getInstance().info("desktop", "DesktopItemsPage created");
    return true;
}

// ============================================================================
// Visibility
// ============================================================================

void DesktopItemsPage::onVisible() noexcept {
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
}

void DesktopItemsPage::onHidden() noexcept { /* no-op */ }

// ============================================================================
// Drag-and-drop
// ============================================================================

void DesktopItemsPage::onFileDrop(HWND const /*hwnd*/, HDROP const hDrop) noexcept {
    wchar_t pathBuf[MAX_PATH] = {};
    UINT const count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    if (count > 0) {
        DragQueryFileW(hDrop, 0, pathBuf, MAX_PATH);
        m_itemPath = ShellIconModifier::sanitizePath(std::wstring(pathBuf));
        m_isShortcut = ShellIconModifier::isShortcut(m_itemPath);

        // Pre-populate selected icon from existing shortcut.
        if (m_isShortcut) {
            std::wstring iconPath;
            int          iconIdx = 0;
            if (ShellIconModifier::getShortcutIcon(m_itemPath, iconPath, iconIdx)
                    == ShellIconModifier::Result::Success) {
                m_selectedIconPath  = iconPath;
                m_selectedIconIndex = iconIdx;
            }
        }
    }
    DragFinish(hDrop);

    if (m_pagePanel && IsWindow(m_pagePanel)) {
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
}

// ============================================================================
// Icon selection via GetOpenFileName (avoids undocumented PickIconDlg)
// ============================================================================

void DesktopItemsPage::onBrowseIcon() noexcept {
    wchar_t fileBuf[MAX_PATH] = {};
    if (!m_selectedIconPath.empty()) {
        wcsncpy_s(fileBuf, m_selectedIconPath.c_str(), MAX_PATH - 1);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner   = m_pagePanel;
    ofn.lpstrFilter = L"Icon sources (*.ico;*.exe;*.dll)\0*.ico;*.exe;*.dll\0"
                      L"All files (*.*)\0*.*\0";
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Select Icon Source";

    if (GetOpenFileNameW(&ofn)) {
        m_selectedIconPath  = ShellIconModifier::sanitizePath(fileBuf);
        m_selectedIconIndex = 0;  // default to first icon in the file
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
}

// ============================================================================
// Apply / restore
// ============================================================================

void DesktopItemsPage::onApplyIcon() noexcept {
    if (m_itemPath.empty() || m_selectedIconPath.empty()) return;

    ShellIconModifier::Result result;
    if (m_isShortcut) {
        result = ShellIconModifier::setShortcutIcon(
            m_itemPath, m_selectedIconPath, m_selectedIconIndex
        );
    } else {
        result = ShellIconModifier::setFolderIcon(
            m_itemPath, m_selectedIconPath, m_selectedIconIndex
        );
    }

    wchar_t const* msg = (result == ShellIconModifier::Result::Success)
        ? L"Icon applied successfully."
        : (result == ShellIconModifier::Result::FileNotFound) ? L"File not found."
        : (result == ShellIconModifier::Result::ReadOnly)     ? L"File is read-only."
        : (result == ShellIconModifier::Result::AccessDenied) ? L"Access denied."
        : L"Error applying icon.";

    MessageBoxW(m_pagePanel, msg, L"AuraShell — Desktop Items",
                MB_OK | (result == ShellIconModifier::Result::Success
                    ? MB_ICONINFORMATION : MB_ICONWARNING));

    InvalidateRect(m_pagePanel, nullptr, FALSE);
}

void DesktopItemsPage::onRestoreDefault() noexcept {
    if (m_itemPath.empty()) return;

    ShellIconModifier::Result result;
    if (m_isShortcut) {
        result = ShellIconModifier::setShortcutIcon(m_itemPath, L"", 0);
    } else {
        result = ShellIconModifier::resetFolderIcon(m_itemPath);
    }

    if (result == ShellIconModifier::Result::Success) {
        m_selectedIconPath.clear();
        m_selectedIconIndex = 0;
    }
    InvalidateRect(m_pagePanel, nullptr, FALSE);
}

// ============================================================================
// Rendering
// ============================================================================

void DesktopItemsPage::drawPageContent(HWND const panelHwnd, HDC const hdc) noexcept {
    RECT clientRect{};
    GetClientRect(panelHwnd, &clientRect);
    int const panelW = clientRect.right;
    int const panelH = clientRect.bottom;
    if (panelW <= 0 || panelH <= 0) return;

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
        HBRUSH const h = CreateSolidBrush(RGB(10, 10, 15));
        FillRect(memDC, &bg, h); DeleteObject(h);
    }

    // D2D card backgrounds
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

            CardRenderer::drawCard(m_rt.Get(), makeRect(CARD1_X, CARD1_Y, CARD1_W, CARD1_H));
            CardRenderer::drawCard(m_rt.Get(), makeRect(CARD2_X, CARD2_Y, CARD2_W, CARD2_H));
            CardRenderer::drawCard(m_rt.Get(), makeRect(CARD3_X, CARD3_Y, CARD3_W, CARD3_H));

            // Drop zone dashed border inside Card 1
            if (m_itemPath.empty()) {
                D2D1_RECT_F const dropZone = makeRect(
                    CARD1_X + PAD * 2, CARD1_Y + 56,
                    CARD1_W - PAD * 4, CARD1_H - 72
                );
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dashBrush;
                m_rt->CreateSolidColorBrush(
                    D2D1::ColorF(0.0f, 0.949f, 1.0f, 0.25f), &dashBrush
                );
                if (dashBrush) {
                    // Draw drop-zone border as 4 solid lines (simple dashed effect).
                    m_rt->DrawLine(
                        D2D1::Point2F(dropZone.left, dropZone.top),
                        D2D1::Point2F(dropZone.right, dropZone.top),
                        dashBrush.Get(), 1.5f
                    );
                    m_rt->DrawLine(
                        D2D1::Point2F(dropZone.right, dropZone.top),
                        D2D1::Point2F(dropZone.right, dropZone.bottom),
                        dashBrush.Get(), 1.5f
                    );
                    m_rt->DrawLine(
                        D2D1::Point2F(dropZone.right, dropZone.bottom),
                        D2D1::Point2F(dropZone.left, dropZone.bottom),
                        dashBrush.Get(), 1.5f
                    );
                    m_rt->DrawLine(
                        D2D1::Point2F(dropZone.left, dropZone.bottom),
                        D2D1::Point2F(dropZone.left, dropZone.top),
                        dashBrush.Get(), 1.5f
                    );
                }
            }

            m_rt->EndDraw();
        }
    }

    BitBlt(hdc, 0, 0, panelW, panelH, memDC, 0, 0, SRCCOPY);

    // GDI text
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

    // Page header
    gdiText(L"Desktop Items", MARGIN, 16, 500, 40, 24, FW_BOLD, RGB(238,238,245));
    gdiText(L"Customise icons for shortcuts and folders",
            MARGIN, 52, 600, 24, 13, FW_NORMAL, RGB(144,144,160));

    // Card 1: Drop Zone
    gdiText(L"Drop Zone",  CARD1_X+PAD, CARD1_Y+PAD, 400, 28, 13, FW_SEMIBOLD, RGB(240,240,245));
    if (m_itemPath.empty()) {
        gdiText(L"Drag a shortcut (.lnk) or folder here",
                CARD1_X+PAD, CARD1_Y + CARD1_H/2 - 20, CARD1_W - PAD*2, 40,
                14, FW_NORMAL, RGB(0, 242, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        gdiText(L"Supports .lnk shortcuts and filesystem folders",
                CARD1_X+PAD, CARD1_Y + CARD1_H/2 + 10, CARD1_W - PAD*2, 28,
                11, FW_NORMAL, RGB(90,90,100), DT_CENTER | DT_SINGLELINE);
    } else {
        wchar_t const* typeBadge = m_isShortcut ? L"[ Shortcut ]" : L"[ Folder ]";
        COLORREF const typeColor = m_isShortcut ? RGB(0, 242, 255) : RGB(255, 200, 80);
        gdiText(typeBadge, CARD1_X+PAD, CARD1_Y+56, 160, 28, 12, FW_SEMIBOLD, typeColor);
        // Truncated path
        std::wstring const& path = m_itemPath;
        std::wstring display = (path.size() > 80)
            ? (L"…" + path.substr(path.size() - 77))
            : path;
        gdiText(display.c_str(), CARD1_X+PAD, CARD1_Y+88, CARD1_W - PAD*2, 28,
                12, FW_NORMAL, RGB(200,200,212));
    }

    // Card 2: Current Item
    gdiText(L"Current Item", CARD2_X+PAD, CARD2_Y+PAD, 400, 28, 13, FW_SEMIBOLD, RGB(240,240,245));
    if (m_itemPath.empty()) {
        gdiText(L"No item selected",
                CARD2_X+PAD, CARD2_Y+52, CARD2_W-PAD*2, 28, 12, FW_NORMAL, RGB(90,90,100));
    } else {
        gdiText(m_selectedIconPath.empty() ? L"(default icon)" : m_selectedIconPath.c_str(),
                CARD2_X+PAD, CARD2_Y+52, CARD2_W-PAD*2, 28, 12, FW_NORMAL, RGB(200,200,212));
        if (!m_selectedIconPath.empty()) {
            wchar_t idxBuf[32];
            std::swprintf(idxBuf, 32, L"Index: %d", m_selectedIconIndex);
            gdiText(idxBuf, CARD2_X+PAD, CARD2_Y+80, 200, 24, 11, FW_NORMAL, RGB(144,144,160));
        }
    }

    // Card 3: Icon Selection
    gdiText(L"Icon Selection", CARD3_X+PAD, CARD3_Y+PAD, 400, 28, 13, FW_SEMIBOLD, RGB(240,240,245));
    gdiText(L"Select an icon source (.ico, .exe, or .dll), then click Apply.",
            CARD3_X+PAD, CARD3_Y+40, CARD3_W-PAD*2, 24, 11, FW_NORMAL, RGB(90,90,100));

    // Button labels (GDI over D2D buttons)
    gdiText(L"Browse…", BTN_BROWSE_X,  BTN_Y, BTN_W,    BTN_H, 12, FW_NORMAL, RGB(200,200,212));
    gdiText(L"Apply",        BTN_APPLY_X,   BTN_Y, BTN_W,    BTN_H, 12, FW_SEMIBOLD, m_itemPath.empty() ? RGB(80,80,80) : RGB(0,242,255));
    gdiText(L"Restore Default", BTN_RESTORE_X, BTN_Y, BTN_W+20, BTN_H, 12, FW_NORMAL, RGB(200,200,212));

    if (hOldBmp) SelectObject(memDC, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// ============================================================================
// Subclass proc
// ============================================================================

LRESULT CALLBACK DesktopItemsPage::subclassProc(
    HWND const hwnd, UINT const msg,
    WPARAM const wParam, LPARAM const lParam,
    UINT_PTR const /*id*/, DWORD_PTR const ref
) {
    auto* const p = reinterpret_cast<DesktopItemsPage*>(ref);
    if (p) return p->handlePanelMsg(hwnd, msg, wParam, lParam);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT DesktopItemsPage::handlePanelMsg(
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

    case WM_DROPFILES:
        onFileDrop(hwnd, reinterpret_cast<HDROP>(wParam));
        return 0;

    case WM_COMMAND: {
        int const id = LOWORD(wParam);
        if (id == ID_BTN_BROWSE)  { onBrowseIcon();   return 0; }
        if (id == ID_BTN_APPLY)   { onApplyIcon();    return 0; }
        if (id == ID_BTN_RESTORE) { onRestoreDefault(); return 0; }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    default:
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
}

}  // namespace aura::app
