// page_behavior.cpp — AuraShell Phase 10.7

#include "page_behavior.h"

#include <Windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <thread>

#include "logging/logger.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")

namespace aura::app {

using namespace aura::ui;

// ============================================================================
// Construction / destruction
// ============================================================================

BehaviorPage::BehaviorPage(AppClient& client, SettingsManager& settings) noexcept
    : m_client(client), m_settings(settings) {}

BehaviorPage::~BehaviorPage() {
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        RemoveWindowSubclass(m_pagePanel, &BehaviorPage::subclassProc, 3);
    }
}

// ============================================================================
// create
// ============================================================================

bool BehaviorPage::create(HWND const pagePanel, HINSTANCE const /*hInst*/,
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
        aura::logging::Logger::getInstance().warn("behavior", "No D2D factory — D2D disabled");
    }

    // Subclass ID=3 (Dashboard uses 1, Visuals uses 2).
    SetWindowSubclass(pagePanel, &BehaviorPage::subclassProc, 3,
                      reinterpret_cast<DWORD_PTR>(this));

    aura::logging::Logger::getInstance().info("behavior", "BehaviorPage created");
    return true;
}

// ============================================================================
// Visibility
// ============================================================================

void BehaviorPage::onVisible() noexcept {
    refreshServiceStatus();
    // Sync auto-start toggle with live registry state.
    AppConfig cfg = m_settings.getConfig();
    cfg.autoStartApp = getRegistryAutoStart();
    m_settings.setConfig(std::move(cfg));

    if (m_pagePanel && IsWindow(m_pagePanel)) {
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }
}

void BehaviorPage::onHidden() noexcept { /* no-op */ }

// ============================================================================
// Pure geometry hit-tests
// ============================================================================

int32_t BehaviorPage::hitTestToggle(int32_t const x, int32_t const y) const noexcept {
    // Toggle 0: monitorAutoHide (Card 1)
    if (x >= TOG_COL1_X && x < TOG_COL1_X + TOG_W &&
        y >= TOG0_Y      && y < TOG0_Y + TOG_H) return 0;
    // Toggle 1: enableMultiMonitor (Card 1)
    if (x >= TOG_COL1_X && x < TOG_COL1_X + TOG_W &&
        y >= TOG1_Y      && y < TOG1_Y + TOG_H) return 1;
    // Toggle 2: autoStartApp (Card 2)
    if (x >= TOG_COL2_X && x < TOG_COL2_X + TOG_W &&
        y >= TOG2_Y      && y < TOG2_Y + TOG_H) return 2;
    return -1;
}

int32_t BehaviorPage::hitTestButton(int32_t const x, int32_t const y) const noexcept {
    if (y < BTN_Y || y >= BTN_Y + BTN_H) return -1;
    if (x >= BTN_START_X && x < BTN_START_X + BTN_W) return 0;  // Start
    if (x >= BTN_STOP_X  && x < BTN_STOP_X  + BTN_W) return 1;  // Stop
    if (x >= BTN_RST_X   && x < BTN_RST_X   + BTN_W) return 2;  // Restart
    return -1;
}

// ============================================================================
// Service status query (non-blocking, < 5ms)
// ============================================================================

void BehaviorPage::refreshServiceStatus() noexcept {
    SC_HANDLE const hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!hSCM) {
        m_serviceStatus = ServiceStatus::Unknown;
        return;
    }

    SC_HANDLE const hSvc = OpenServiceW(
        hSCM, L"AuraShellService", SERVICE_QUERY_STATUS
    );
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        m_serviceStatus = ServiceStatus::NotInstalled;
        return;
    }

    SERVICE_STATUS_PROCESS ssp = {};
    DWORD needed = 0;
    bool const ok = QueryServiceStatusEx(
        hSvc, SC_STATUS_PROCESS_INFO,
        reinterpret_cast<BYTE*>(&ssp), sizeof(ssp), &needed
    ) != FALSE;

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);

    if (!ok) { m_serviceStatus = ServiceStatus::Unknown; return; }

    switch (ssp.dwCurrentState) {
    case SERVICE_RUNNING:       m_serviceStatus = ServiceStatus::Running;      break;
    case SERVICE_STOPPED:       m_serviceStatus = ServiceStatus::Stopped;      break;
    case SERVICE_START_PENDING: m_serviceStatus = ServiceStatus::StartPending; break;
    default:                    m_serviceStatus = ServiceStatus::Unknown;      break;
    }
}

// ============================================================================
// Async service control
// ============================================================================

std::wstring BehaviorPage::getServiceExePath() noexcept {
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);

    // Replace filename with AuraShellService.exe (same directory).
    wchar_t* lastSlash = wcsrchr(selfPath, L'\\');
    if (lastSlash) {
        *(lastSlash + 1) = L'\0';
        wcscat_s(selfPath, L"AuraShellService.exe");
    }
    return std::wstring(selfPath);
}

void BehaviorPage::executeServiceAction(ServiceAction const action) noexcept {
    // Atomic guard — ignore if a previous action is still pending.
    bool expected = false;
    if (!m_serviceActionPending.compare_exchange_strong(expected, true)) return;

    // Show pending state immediately.
    if (m_pagePanel && IsWindow(m_pagePanel)) {
        InvalidateRect(m_pagePanel, nullptr, FALSE);
    }

    std::thread([this, action]() {
        std::wstring const svcExe = getServiceExePath();

        auto runCmd = [&svcExe](wchar_t const* const flag) noexcept {
            SHELLEXECUTEINFOW sei = {};
            sei.cbSize       = sizeof(SHELLEXECUTEINFOW);
            sei.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            sei.lpVerb       = L"runas";  // request elevation for SCM operations
            sei.lpFile       = svcExe.c_str();
            sei.lpParameters = flag;
            sei.nShow        = SW_HIDE;
            if (ShellExecuteExW(&sei) && sei.hProcess) {
                WaitForSingleObject(sei.hProcess, 10000);  // 10s timeout
                CloseHandle(sei.hProcess);
            }
        };

        switch (action) {
        case ServiceAction::Start:
            runCmd(L"--start");
            break;
        case ServiceAction::Stop:
            runCmd(L"--stop");
            break;
        case ServiceAction::Restart:
            runCmd(L"--stop");
            Sleep(500);   // brief settle time after stop
            runCmd(L"--start");
            break;
        }

        m_serviceActionPending.store(false, std::memory_order_release);

        // Post to page panel — WM_APP+1 triggers status refresh + repaint.
        if (m_pagePanel && IsWindow(m_pagePanel)) {
            PostMessageW(m_pagePanel, WM_APP + 1, 0, 0);
        }
    }).detach();
}

// ============================================================================
// Registry auto-start (HKCU — no admin required)
// ============================================================================

void BehaviorPage::setRegistryAutoStart(bool const enable) noexcept {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_REG_KEY, 0,
                      KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return;
    }

    if (enable) {
        wchar_t appPath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, appPath, MAX_PATH);
        RegSetValueExW(hKey, AUTOSTART_VAL_NAME, 0, REG_SZ,
            reinterpret_cast<BYTE const*>(appPath),
            static_cast<DWORD>((wcslen(appPath) + 1) * sizeof(wchar_t))
        );
    } else {
        RegDeleteValueW(hKey, AUTOSTART_VAL_NAME);
    }
    RegCloseKey(hKey);
}

bool BehaviorPage::getRegistryAutoStart() noexcept {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_REG_KEY, 0,
                      KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0, size = 0;
    bool const exists =
        RegQueryValueExW(hKey, AUTOSTART_VAL_NAME, nullptr, &type, nullptr, &size)
        == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return exists;
}

// ============================================================================
// D2D service button helper
// ============================================================================

void BehaviorPage::drawServiceButton(
    ID2D1RenderTarget* const rt,
    int const x, int const y, int const w, int const h,
    wchar_t const* const label,
    bool const isPrimary,
    bool const isPending
) noexcept {
    D2D1_RECT_F const bounds = {
        static_cast<float>(x), static_cast<float>(y),
        static_cast<float>(x + w), static_cast<float>(y + h)
    };
    D2D1_ROUNDED_RECT const rr = {bounds, 8.0f, 8.0f};

    // Fill: accent (primary) or dark surface (secondary) or grey (pending)
    D2D1_COLOR_F const fillColor = isPending
        ? D2D1::ColorF(0.2f, 0.2f, 0.22f, 0.5f)
        : isPrimary
            ? D2D1::ColorF(0.0f, 0.949f, 1.0f, 0.25f)   // accent-tinted
            : D2D1::ColorF(0.18f, 0.18f, 0.22f, 1.0f);  // dark surface

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
    rt->CreateSolidColorBrush(fillColor, &fill);
    if (fill) rt->FillRoundedRectangle(rr, fill.Get());

    // Border
    D2D1_COLOR_F const borderColor = isPrimary
        ? D2D1::ColorF(0.0f, 0.949f, 1.0f, 0.55f)
        : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f);
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border;
    rt->CreateSolidColorBrush(borderColor, &border);
    if (border) rt->DrawRoundedRectangle(rr, border.Get(), 1.0f);

    // GDI text for label is drawn in the outer GDI pass — see drawPageContent.
    (void)label;  // label drawn via GDI after D2D pass
}

// ============================================================================
// Rendering
// ============================================================================

void BehaviorPage::drawPageContent(HWND const panelHwnd, HDC const hdc) noexcept {
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
        HBRUSH const hBg = CreateSolidBrush(RGB(10, 10, 15));
        FillRect(memDC, &bg, hBg);
        DeleteObject(hBg);
    }

    // D2D: card backgrounds + toggles + service buttons
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

            AppConfig const& cfg = m_settings.getConfig();

            // Card 1: Shell Monitoring
            CardRenderer::drawCard(m_rt.Get(), makeRect(CARD1_X, CARD1_Y, CARD1_W, CARD1_H));
            CardRenderer::drawToggle(m_rt.Get(), makeRect(TOG_COL1_X, TOG0_Y, TOG_W, TOG_H),
                                     cfg.monitorAutoHide);
            CardRenderer::drawToggle(m_rt.Get(), makeRect(TOG_COL1_X, TOG1_Y, TOG_W, TOG_H),
                                     cfg.enableMultiMonitor);

            // Card 2: System Boot
            CardRenderer::drawCard(m_rt.Get(), makeRect(CARD2_X, CARD2_Y, CARD2_W, CARD2_H));
            CardRenderer::drawToggle(m_rt.Get(), makeRect(TOG_COL2_X, TOG2_Y, TOG_W, TOG_H),
                                     cfg.autoStartApp);

            // Card 3: Service Management
            CardRenderer::drawCard(m_rt.Get(), makeRect(CARD3_X, CARD3_Y, CARD3_W, CARD3_H));

            bool const pending = m_serviceActionPending.load(std::memory_order_relaxed);

            // Service action buttons
            drawServiceButton(m_rt.Get(), BTN_START_X, BTN_Y, BTN_W, BTN_H,
                              L"Start",   true,  pending);
            drawServiceButton(m_rt.Get(), BTN_STOP_X,  BTN_Y, BTN_W, BTN_H,
                              L"Stop",    false, pending);
            drawServiceButton(m_rt.Get(), BTN_RST_X,   BTN_Y, BTN_W, BTN_H,
                              L"Restart", false, pending);

            m_rt->EndDraw();
        }
    }

    // Blit D2D content.
    BitBlt(hdc, 0, 0, panelW, panelH, memDC, 0, 0, SRCCOPY);

    // GDI text overlay.
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

    AppConfig const& cfg = m_settings.getConfig();

    // Page header
    gdiText(L"Behavior", MARGIN, 16, 400, 40, 24, FW_BOLD, RGB(238,238,245));
    gdiText(L"Taskbar integration, startup, and service management",
            MARGIN, 52, 700, 24, 13, FW_NORMAL, RGB(144,144,160));

    // Card 1
    gdiText(L"Shell Monitoring", CARD1_X+PAD, CARD1_Y+PAD, 400, 30, 13, FW_SEMIBOLD, RGB(240,240,245));
    gdiText(L"Track taskbar auto-hide", CARD1_X+PAD, TOG0_Y, CARD1_W-PAD*2-TOG_W-8, TOG_H,
            12, FW_NORMAL, cfg.monitorAutoHide ? RGB(240,240,245) : RGB(144,144,160));
    gdiText(L"Enable multi-monitor support", CARD1_X+PAD, TOG1_Y, CARD1_W-PAD*2-TOG_W-8, TOG_H,
            12, FW_NORMAL, cfg.enableMultiMonitor ? RGB(240,240,245) : RGB(144,144,160));

    // Card 2
    gdiText(L"System Boot", CARD2_X+PAD, CARD2_Y+PAD, 400, 30, 13, FW_SEMIBOLD, RGB(240,240,245));
    gdiText(L"Launch AuraShell on Windows startup",
            CARD2_X+PAD, TOG2_Y, CARD2_W-PAD*2-TOG_W-8, TOG_H,
            12, FW_NORMAL, cfg.autoStartApp ? RGB(240,240,245) : RGB(144,144,160));

    // Card 3
    gdiText(L"Service Management", CARD3_X+PAD, CARD3_Y+PAD, 400, 30, 13, FW_SEMIBOLD, RGB(240,240,245));

    // Service status dot + text
    bool const pending = m_serviceActionPending.load(std::memory_order_relaxed);
    if (pending) {
        gdiText(L"⟳  Action in progress...", CARD3_X+PAD, CARD3_Y+60, 500, 28,
                12, FW_NORMAL, RGB(255, 200, 80));
    } else {
        switch (m_serviceStatus) {
        case ServiceStatus::Running:
            gdiText(L"● AuraShellService — Running", CARD3_X+PAD, CARD3_Y+60, 500, 28,
                    12, FW_SEMIBOLD, RGB(80, 220, 120));
            break;
        case ServiceStatus::Stopped:
            gdiText(L"✕  AuraShellService — Stopped", CARD3_X+PAD, CARD3_Y+60, 500, 28,
                    12, FW_SEMIBOLD, RGB(220, 80, 80));
            break;
        case ServiceStatus::NotInstalled:
            gdiText(L"—  Service not installed  (run AuraShellService.exe --install)",
                    CARD3_X+PAD, CARD3_Y+60, 700, 28, 12, FW_NORMAL, RGB(144,144,160));
            break;
        default:
            gdiText(L"?  Status unknown", CARD3_X+PAD, CARD3_Y+60, 400, 28,
                    12, FW_NORMAL, RGB(144,144,160));
            break;
        }
    }

    // Service button labels (GDI drawn over the D2D buttons)
    wchar_t const* const btnLabels[3] = {L"Start", L"Stop", L"Restart"};
    int const btnXs[3] = {BTN_START_X, BTN_STOP_X, BTN_RST_X};
    for (int i = 0; i < 3; ++i) {
        COLORREF const col = (i == 0 && !pending) ? RGB(0, 242, 255) : RGB(200, 200, 212);
        gdiText(btnLabels[i], btnXs[i], BTN_Y, BTN_W, BTN_H, 13, FW_SEMIBOLD, col);
    }

    // Admin note
    gdiText(L"ℹ  Service Start/Stop requires Administrator privileges.",
            CARD3_X+PAD, BTN_Y + BTN_H + 12, CARD3_W - PAD*2, 24,
            11, FW_NORMAL, RGB(90, 90, 100));

    if (hOldBmp) SelectObject(memDC, hOldBmp);
    DeleteObject(hBitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// ============================================================================
// Subclass proc
// ============================================================================

LRESULT CALLBACK BehaviorPage::subclassProc(
    HWND const hwnd, UINT const msg,
    WPARAM const wParam, LPARAM const lParam,
    UINT_PTR const /*id*/, DWORD_PTR const ref
) {
    auto* const p = reinterpret_cast<BehaviorPage*>(ref);
    if (p) return p->handlePanelMsg(hwnd, msg, wParam, lParam);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT BehaviorPage::handlePanelMsg(
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

    // Async service action completed — refresh status and repaint.
    case WM_APP + 1:
        refreshServiceStatus();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        float const dpiScale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        int32_t const mx = static_cast<int32_t>(GET_X_LPARAM(lParam) / dpiScale);
        int32_t const my = static_cast<int32_t>(GET_Y_LPARAM(lParam) / dpiScale);

        // Toggle hit-test
        int32_t const tog = hitTestToggle(mx, my);
        if (tog >= 0) {
            AppConfig cfg = m_settings.getConfig();
            switch (tog) {
            case 0:
                cfg.monitorAutoHide = !cfg.monitorAutoHide;
                m_settings.setConfig(std::move(cfg));
                break;
            case 1:
                cfg.enableMultiMonitor = !cfg.enableMultiMonitor;
                m_settings.setConfig(std::move(cfg));
                break;
            case 2: {
                bool const newState = !cfg.autoStartApp;
                cfg.autoStartApp = newState;
                m_settings.setConfig(std::move(cfg));
                setRegistryAutoStart(newState);
                break;
            }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Service button hit-test (only if no action pending)
        if (!m_serviceActionPending.load(std::memory_order_relaxed)) {
            int32_t const btn = hitTestButton(mx, my);
            if (btn == 0) { executeServiceAction(ServiceAction::Start);   return 0; }
            if (btn == 1) { executeServiceAction(ServiceAction::Stop);    return 0; }
            if (btn == 2) { executeServiceAction(ServiceAction::Restart); return 0; }
        }
        return 0;
    }

    default:
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }
}

}  // namespace aura::app
