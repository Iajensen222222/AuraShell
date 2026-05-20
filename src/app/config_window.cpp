#include "config_window.h"

#include <Windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <string>

#include "logging/logger.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d2d1.lib")

// SDK 26100 (Windows 11 24H2) defines DWMWA_SYSTEMBACKDROP_TYPE,
// DWM_SYSTEMBACKDROP_TYPE, and DWMWA_USE_IMMERSIVE_DARK_MODE natively in
// dwmapi.h.  No manual definitions are needed for this SDK version.

namespace aura::app {

// ============================================================================
// Static
// ============================================================================

bool ConfigWindow::s_classRegistered = false;

static constexpr wchar_t const* CLASS_NAME = L"AuraConfigWindow";

// ============================================================================
// Constructor / destructor
// ============================================================================

ConfigWindow::ConfigWindow(AppClient& client, SettingsManager& settings)
    : m_client(client)
    , m_settings(settings)
    , m_dashboardPage(client)
    , m_visualsPage(client, settings)
    , m_behaviorPage(client, settings) {}

ConfigWindow::~ConfigWindow() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        DestroyWindow(m_hwnd);
    }
}

// ============================================================================
// create
// ============================================================================

bool ConfigWindow::create(HINSTANCE const hInst) {
    INITCOMMONCONTROLSEX icc = {sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    if (!s_classRegistered) {
        WNDCLASSEXW wc     = {};
        wc.cbSize          = sizeof(WNDCLASSEXW);
        wc.style           = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc     = ConfigWindow::windowProc;
        wc.hInstance       = hInst;
        wc.hCursor         = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground   = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName   = CLASS_NAME;
        wc.hIcon           = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm         = LoadIconW(nullptr, IDI_APPLICATION);

        if (!RegisterClassExW(&wc)) {
            aura::logging::Logger::getInstance().error(
                "config_window", "RegisterClassExW failed"
            );
            return false;
        }
        s_classRegistered = true;
    }

    // Compute window size from desired client size (account for non-client area).
    RECT rc = {0, 0, CLIENT_W, CLIENT_H};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    int const wndW = rc.right  - rc.left;
    int const wndH = rc.bottom - rc.top;

    // Centre on the primary monitor.
    int const screenW = GetSystemMetrics(SM_CXSCREEN);
    int const screenH = GetSystemMetrics(SM_CYSCREEN);
    int const x       = (screenW - wndW) / 2;
    int const y       = (screenH - wndH) / 2;

    m_hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"AuraShell — Configuration",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, wndW, wndH,
        nullptr, nullptr, hInst,
        this  // passed as lParam to WM_NCCREATE
    );

    if (!m_hwnd) {
        aura::logging::Logger::getInstance().error(
            "config_window", "CreateWindowExW failed"
        );
        return false;
    }

    return true;
}

// ============================================================================
// Message pump
// ============================================================================

int ConfigWindow::runMessageLoop() {
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ============================================================================
// windowProc — static trampoline
// ============================================================================

LRESULT CALLBACK ConfigWindow::windowProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam
) {
    ConfigWindow* pThis = nullptr;

    if (msg == WM_NCCREATE) {
        auto const* cs = reinterpret_cast<CREATESTRUCTW const*>(lParam);
        pThis = static_cast<ConfigWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        if (pThis) pThis->m_hwnd = hwnd;
    } else {
        pThis = reinterpret_cast<ConfigWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA)
        );
    }

    if (pThis) return pThis->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// handleMessage
// ============================================================================

LRESULT ConfigWindow::handleMessage(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam
) {
    switch (msg) {

    // ---- Creation -----------------------------------------------------------
    case WM_CREATE: {
        // ---- Shared D2D factory (Phase 10.8) — created once, passed to all pages ----
        if (FAILED(D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                m_d2dFactory.ReleaseAndGetAddressOf()))) {
            aura::logging::Logger::getInstance().warn(
                "config_window", "D2D factory creation failed — D2D rendering disabled"
            );
        }

        // ---- Mica backdrop (Windows 11 22H2+) ----
        DWM_SYSTEMBACKDROP_TYPE const micaType = DWMSBT_MAINWINDOW;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                              &micaType, sizeof(micaType));

        // Phase 8: per-monitor DPI awareness for WM_DPICHANGED delivery.
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        // Respect system dark-mode preference.
        DWORD lightTheme = 1;
        DWORD cbSize     = sizeof(DWORD);
        RegGetValueW(
            HKEY_CURRENT_USER,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme",
            RRF_RT_REG_DWORD, nullptr, &lightTheme, &cbSize
        );
        BOOL const darkMode = (lightTheme == 0) ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                              &darkMode, sizeof(BOOL));

        HINSTANCE const hInst = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)
        );

        // ---- Register page panel window class (first call only) ----
        static bool s_panelClassRegistered = false;
        if (!s_panelClassRegistered) {
            WNDCLASSEXW wcp     = {};
            wcp.cbSize          = sizeof(WNDCLASSEXW);
            wcp.style           = CS_HREDRAW | CS_VREDRAW;
            wcp.lpfnWndProc     = DefWindowProcW;
            wcp.hInstance       = hInst;
            wcp.hbrBackground   = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            wcp.lpszClassName   = L"AuraPagePanel";
            RegisterClassExW(&wcp);
            s_panelClassRegistered = true;
        }

        // ---- Create the page panels (hidden; NavigationManager shows them) ----
        // All panels sit at x=SIDEBAR_W, same size as the content area.
        static constexpr wchar_t const* PAGE_LABELS[PAGE_COUNT] = {
            L"Dashboard", L"Visuals", L"Behavior", L"About", L"Desktop Items"
        };
        for (int i = 0; i < PAGE_COUNT; ++i) {
            m_pages[i] = CreateWindowExW(
                0, L"AuraPagePanel", PAGE_LABELS[i],
                WS_CHILD | WS_CLIPSIBLINGS,  // hidden initially
                SIDEBAR_W, 0, CONTENT_W, CLIENT_H,
                hwnd, nullptr, hInst, nullptr
            );
        }

        // ---- Create the navigation sidebar ----
        if (!m_nav.create(hwnd, hInst)) {
            aura::logging::Logger::getInstance().error(
                "config_window", "NavigationManager::create failed"
            );
            return -1;
        }

        // Register page content HWNDs with the NavigationManager.
        m_nav.registerPageContent(Page::Dashboard,    m_pages[0]);
        m_nav.registerPageContent(Page::Visuals,      m_pages[1]);
        m_nav.registerPageContent(Page::Behavior,     m_pages[2]);
        m_nav.registerPageContent(Page::About,        m_pages[3]);
        m_nav.registerPageContent(Page::DesktopItems, m_pages[4]);

        // Shared factory pointer (may be null if D2D init failed — pages handle gracefully).
        ID2D1Factory* const factory = m_d2dFactory.Get();

        // ---- Dashboard page (Phase 10.3 / 10.8) ----
        if (!m_dashboardPage.create(m_pages[0], hInst, factory)) {
            aura::logging::Logger::getInstance().warn(
                "config_window", "DashboardPage::create failed"
            );
        }

        // ---- Visuals page (Phase 10.6 / 10.8) ----
        if (!m_visualsPage.create(m_pages[1], hInst, factory)) {
            aura::logging::Logger::getInstance().warn(
                "config_window", "VisualsPage::create failed"
            );
        }

        // ---- Behavior page (Phase 10.7 / 10.8) ----
        if (!m_behaviorPage.create(m_pages[2], hInst, factory)) {
            aura::logging::Logger::getInstance().warn(
                "config_window", "BehaviorPage::create failed"
            );
        }

        // ---- About page (Phase 10.8) ----
        if (!m_aboutPage.create(m_pages[3], hInst, factory)) {
            aura::logging::Logger::getInstance().warn(
                "config_window", "AboutPage::create failed"
            );
        }

        // ---- Desktop Items page (Phase 10.9) ----
        if (!m_desktopItemsPage.create(m_pages[4], hInst, factory)) {
            aura::logging::Logger::getInstance().warn(
                "config_window", "DesktopItemsPage::create failed"
            );
        }

        // Wire color-changed callback: Visuals color pick → Dashboard preview refresh.
        m_visualsPage.setOnColorChangedCallback([this]() {
            m_dashboardPage.onVisible();
        });

        // Page-changed callback: notify page implementations on navigation.
        m_nav.setPageChangedCallback([this](Page const newPage) {
            if (newPage == Page::Dashboard) {
                m_dashboardPage.onVisible();
                m_visualsPage.onHidden();
                m_behaviorPage.onHidden();
                m_aboutPage.onHidden();
                m_desktopItemsPage.onHidden();
            } else if (newPage == Page::Visuals) {
                m_dashboardPage.onHidden();
                m_visualsPage.onVisible();
                m_behaviorPage.onHidden();
                m_aboutPage.onHidden();
                m_desktopItemsPage.onHidden();
            } else if (newPage == Page::Behavior) {
                m_dashboardPage.onHidden();
                m_visualsPage.onHidden();
                m_behaviorPage.onVisible();
                m_aboutPage.onHidden();
                m_desktopItemsPage.onHidden();
            } else if (newPage == Page::About) {
                m_dashboardPage.onHidden();
                m_visualsPage.onHidden();
                m_behaviorPage.onHidden();
                m_aboutPage.onVisible();
                m_desktopItemsPage.onHidden();
            } else if (newPage == Page::DesktopItems) {
                m_dashboardPage.onHidden();
                m_visualsPage.onHidden();
                m_behaviorPage.onHidden();
                m_aboutPage.onHidden();
                m_desktopItemsPage.onVisible();
            } else {
                m_dashboardPage.onHidden();
                m_visualsPage.onHidden();
                m_behaviorPage.onHidden();
                m_aboutPage.onHidden();
                m_desktopItemsPage.onHidden();
            }
        });

        // ---- Navigate to Dashboard on launch ----
        m_nav.navigateTo(Page::Dashboard);
        m_dashboardPage.onVisible();
        return 0;
    }

    // ---- Forward resize to NavigationManager --------------------------------
    case WM_SIZE: {
        int32_t const newH = HIWORD(lParam);
        m_nav.onParentResize(newH);
        // Resize all page panels to match.
        for (int i = 0; i < PAGE_COUNT; ++i) {
            if (m_pages[i]) {
                SetWindowPos(m_pages[i], nullptr,
                             SIDEBAR_W, 0, CONTENT_W, newH,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        return 0;
    }

    // ---- Background — black lets Mica show through --------------------------
    case WM_ERASEBKGND: {
        HDC  hdc = reinterpret_cast<HDC>(wParam);
        RECT rc  = {};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    // ---- Phase 8: DPI mixed-mode stress -----------------------------------
    case WM_DPICHANGED: {
        UINT const newDpi = HIWORD(wParam);
        RECT const* const pRect = reinterpret_cast<RECT const*>(lParam);

        SetWindowPos(hwnd, nullptr,
            pRect->left, pRect->top,
            pRect->right - pRect->left, pRect->bottom - pRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // Re-apply Mica on DPI boundary cross (guards against DWM driver reset).
        DWM_SYSTEMBACKDROP_TYPE const micaType = DWMSBT_MAINWINDOW;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                              &micaType, sizeof(micaType));

        aura::logging::Logger::getInstance().info(
            "config_window",
            "DPI changed to " + std::to_string(newDpi) +
            " — window repositioned to suggested rect"
        );
        return 0;
    }

    // ---- Quit ---------------------------------------------------------------
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// Legacy button/control handlers removed — Phase 10.6 replaced them with VisualsPage.

}  // namespace aura::app
