// page_about.cpp — AuraShell Phase 10.8

#include "page_about.h"

#include <Windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "logging/logger.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

namespace aura::app {

// ============================================================================
// Destruction
// ============================================================================

AboutPage::~AboutPage() {
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        RemoveWindowSubclass(m_pagePanel, &AboutPage::subclassProc, 4);
    }
}

// ============================================================================
// create
// ============================================================================

bool AboutPage::create(HWND const pagePanel, HINSTANCE const hInst,
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

    // Log viewer EDIT (read-only, multiline, vertical scrollbar).
    m_logEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        LOG_EDIT_X, LOG_EDIT_Y, LOG_EDIT_W, LOG_EDIT_H,
        pagePanel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LOG_EDIT)),
        hInst, nullptr
    );

    // Style the EDIT: dark background, white text (best effort via WM_CTLCOLOREDIT).
    if (m_logEdit) {
        SendMessageW(m_logEdit, EM_SETLIMITTEXT, 256 * 1024, 0);  // 256 KB max
        // Set monospace font for log readability.
        HFONT const hMono = CreateFontW(
            -MulDiv(11, 96, 72), // 11pt at 96 DPI; GDI will scale from device context
            0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_DONTCARE,
            L"Cascadia Code"
        );
        if (hMono) SendMessageW(m_logEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hMono), TRUE);
    }

    // Refresh button.
    m_refreshBtn = CreateWindowExW(
        0, L"BUTTON", L"Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CARD3_X + PAD, CARD3_Y + 28, 80, 26,
        pagePanel,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LOG_REFRESH)),
        hInst, nullptr
    );

    SetWindowSubclass(pagePanel, &AboutPage::subclassProc, 4,
                      reinterpret_cast<DWORD_PTR>(this));

    aura::logging::Logger::getInstance().info("about", "AboutPage created");
    return true;
}

// ============================================================================
// Visibility
// ============================================================================

void AboutPage::onVisible() noexcept {
    loadLatestLog();
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
}

void AboutPage::onHidden() noexcept { /* no-op */ }

// ============================================================================
// Log file utilities
// ============================================================================

std::wstring AboutPage::findLatestLogFile() noexcept {
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData))) {
        return {};
    }
    std::filesystem::path const logDir =
        std::filesystem::path(appData) / L"AuraShell" / L"logs";

    std::error_code ec;
    if (!std::filesystem::exists(logDir, ec)) return {};

    std::filesystem::path latest;
    std::filesystem::file_time_type latestTime{};

    for (auto const& entry : std::filesystem::directory_iterator(logDir, ec)) {
        if (entry.path().extension() == L".log") {
            auto const t = entry.last_write_time(ec);
            if (t > latestTime) {
                latestTime = t;
                latest     = entry.path();
            }
        }
    }
    return latest.wstring();
}

std::wstring AboutPage::readLastLines(
    std::wstring const& path, int const maxLines
) noexcept {
    if (path.empty()) return L"(no log file found)";

    std::ifstream file(path);
    if (!file.is_open()) return L"(cannot open log file)";

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(std::move(line));
    }

    // Keep only the last maxLines entries.
    if (static_cast<int>(lines.size()) > maxLines) {
        lines.erase(lines.begin(),
                    lines.begin() + static_cast<int>(lines.size()) - maxLines);
    }

    // Concatenate into a wide string (UTF-8 → wide conversion per line).
    std::wstring result;
    result.reserve(lines.size() * 120);
    for (auto const& ln : lines) {
        int const needed = MultiByteToWideChar(CP_UTF8, 0, ln.c_str(), -1, nullptr, 0);
        if (needed > 0) {
            std::wstring wide(static_cast<size_t>(needed - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, ln.c_str(), -1, &wide[0], needed);
            result += wide;
        }
        result += L"\r\n";
    }
    return result;
}

void AboutPage::loadLatestLog() noexcept {
    if (!m_logEdit || !IsWindow(m_logEdit)) return;

    std::wstring const logPath = findLatestLogFile();
    std::wstring const content = readLastLines(logPath, 25);
    SetWindowTextW(m_logEdit, content.c_str());

    // Scroll to end.
    int const len = static_cast<int>(GetWindowTextLengthW(m_logEdit));
    SendMessageW(m_logEdit, EM_SETSEL, len, len);
    SendMessageW(m_logEdit, EM_SCROLLCARET, 0, 0);
}

// ============================================================================
// Rendering
// ============================================================================

void AboutPage::drawPageContent(HWND const panelHwnd, HDC const hdc) noexcept {
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

    // D2D: card backgrounds
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

            m_rt->EndDraw();
        }
    }

    BitBlt(hdc, 0, 0, panelW, panelH, memDC, 0, 0, SRCCOPY);

    // GDI text
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
        SelectObject(hdc, old); DeleteObject(f);
    };

    // Page header
    gdiText(L"About", MARGIN, 16, 400, 40, 24, FW_BOLD, RGB(238,238,245));
    gdiText(L"AuraShell version information and activity log",
            MARGIN, 52, 700, 24, 13, FW_NORMAL, RGB(144,144,160));

    // Card 1: App info
    gdiText(L"AuraShell",          CARD1_X+PAD, CARD1_Y+PAD, 400, 30, 13, FW_SEMIBOLD, RGB(240,240,245));
    gdiText(L"Version 1.0.0",      CARD1_X+PAD, CARD1_Y+52,  500, 24, 12, FW_NORMAL, RGB(0,242,255));
    gdiText(L"Win32 · Direct2D · Windows 11 SDK 26100",
            CARD1_X+PAD, CARD1_Y+78, 600, 24, 12, FW_NORMAL, RGB(144,144,160));
    gdiText(L"github.com/Iajensen222222/AuraShell",
            CARD1_X+PAD, CARD1_Y+104, 600, 24, 12, FW_NORMAL, RGB(80,160,255));
    gdiText(L"Built with Catch2 · nlohmann/json · spdlog · vcpkg",
            CARD1_X+PAD, CARD1_Y+130, 700, 24, 11, FW_NORMAL, RGB(90,90,100));

    // Card 2: License
    gdiText(L"License",            CARD2_X+PAD, CARD2_Y+PAD, 400, 30, 13, FW_SEMIBOLD, RGB(240,240,245));
    gdiText(L"MIT License  —  Copyright © 2026 AuraShell Development Team",
            CARD2_X+PAD, CARD2_Y+52, 800, 24, 12, FW_NORMAL, RGB(200,200,212));
    gdiText(L"Permission is granted to use, copy, modify, and distribute this software.",
            CARD2_X+PAD, CARD2_Y+78, 900, 24, 11, FW_NORMAL, RGB(120,120,130));

    // Card 3: Activity Log header
    gdiText(L"Activity Log",       CARD3_X+PAD, CARD3_Y+PAD, 400, 26, 13, FW_SEMIBOLD, RGB(240,240,245));
    gdiText(L"%LOCALAPPDATA%\\AuraShell\\logs  —  last 25 lines",
            CARD3_X+PAD+100, CARD3_Y+28, 700, 26, 11, FW_NORMAL, RGB(90,90,100));

    if (hOldBmp) SelectObject(memDC, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// ============================================================================
// Subclass proc
// ============================================================================

LRESULT CALLBACK AboutPage::subclassProc(
    HWND const hwnd, UINT const msg,
    WPARAM const wParam, LPARAM const lParam,
    UINT_PTR const /*id*/, DWORD_PTR const ref
) {
    auto* const p = reinterpret_cast<AboutPage*>(ref);
    if (p) return p->handlePanelMsg(hwnd, msg, wParam, lParam);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT AboutPage::handlePanelMsg(
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

    // Pass Refresh button click to loadLatestLog.
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_LOG_REFRESH) {
            loadLatestLog();
            return 0;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);

    // Handle log EDIT background colour (dark theme).
    case WM_CTLCOLOREDIT: {
        HDC const editDC = reinterpret_cast<HDC>(wParam);
        SetBkColor(editDC, RGB(14, 14, 20));
        SetTextColor(editDC, RGB(200, 210, 220));
        static HBRUSH const editBg = CreateSolidBrush(RGB(14, 14, 20));
        return reinterpret_cast<LRESULT>(editBg);
    }

    default:
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
}

}  // namespace aura::app
