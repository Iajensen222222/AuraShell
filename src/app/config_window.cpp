#include "config_window.h"

#include <Windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <string>

#include "logging/logger.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

// Guard for DWMWA_SYSTEMBACKDROP_TYPE — present in SDK 22621 but added here
// defensively in case an older SDK header is picked up by the toolchain.
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
typedef enum {
    DWMSBT_AUTO            = 0,
    DWMSBT_NONE            = 1,
    DWMSBT_MAINWINDOW      = 2,   // Mica
    DWMSBT_TRANSIENTWINDOW = 3,   // Acrylic
    DWMSBT_TABBEDWINDOW    = 4,   // Mica Alt
} DWM_SYSTEMBACKDROP_TYPE;
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

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
    : m_client(client), m_settings(settings) {}

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
        // Apply Mica backdrop (Windows 11 22H2+).
        DWM_SYSTEMBACKDROP_TYPE const micaType = DWMSBT_MAINWINDOW;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                              &micaType, sizeof(micaType));

        // Phase 8: Opt into per-monitor DPI awareness at the window level so
        // WM_DPICHANGED is delivered when the window crosses monitor boundaries.
        // This is a no-op if the process manifest already declares
        // PerMonitorV2, but harmless to call either way.
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

        // ---- Row helpers (y positions) -----
        int y = 40;
        auto const nextRow = [&](int gap = 36) { y += gap; };

        // Row 1: Theme name
        CreateWindowExW(0, L"STATIC", L"Theme name:",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            MARGIN, y + 2, LABEL_W, CTRL_H, hwnd,
            nullptr, hInst, nullptr);
        m_themeEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            MARGIN + LABEL_W + 8, y, EDIT_W, CTRL_H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_THEME_EDIT)),
            hInst, nullptr);
        nextRow();

        // Row 2: Accent color swatch
        CreateWindowExW(0, L"STATIC", L"Accent color:",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            MARGIN, y + 2, LABEL_W, CTRL_H, hwnd,
            nullptr, hInst, nullptr);
        m_swatchStatic = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            MARGIN + LABEL_W + 8, y, SWATCH_W, CTRL_H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SWATCH)),
            hInst, nullptr);
        nextRow();

        // Row 3: Animation speed trackbar
        CreateWindowExW(0, L"STATIC", L"Anim speed:",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            MARGIN, y + 4, LABEL_W, CTRL_H, hwnd,
            nullptr, hInst, nullptr);
        m_speedSlider = CreateWindowExW(
            0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_HORZ | TBS_NOTICKS,
            MARGIN + LABEL_W + 8, y, 160, CTRL_H + 4, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SPEED_SLIDER)),
            hInst, nullptr);
        SendMessageW(m_speedSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 200));
        SendMessageW(m_speedSlider, TBM_SETTICFREQ, 50, 0);

        m_speedLabel = CreateWindowExW(0, L"STATIC", L"100%",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            MARGIN + LABEL_W + 8 + 168, y + 4, 50, CTRL_H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SPEED_LABEL)),
            hInst, nullptr);
        nextRow(44);

        // Row 4-6: Checkboxes
        m_hoverCheck = CreateWindowExW(0, L"BUTTON", L"Show overlay on hover",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            MARGIN + LABEL_W + 8, y, 220, CTRL_H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HOVER_CHECK)),
            hInst, nullptr);
        nextRow(30);

        m_launchCheck = CreateWindowExW(0, L"BUTTON", L"Show overlay on launch",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            MARGIN + LABEL_W + 8, y, 220, CTRL_H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LAUNCH_CHECK)),
            hInst, nullptr);
        nextRow(30);

        m_glowCheck = CreateWindowExW(0, L"BUTTON", L"Enable glow effect",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            MARGIN + LABEL_W + 8, y, 220, CTRL_H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_GLOW_CHECK)),
            hInst, nullptr);
        nextRow(40);

        // Row 7: Status bar
        m_statusStatic = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"STATIC", L"● Service: not connected",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            MARGIN, y, CLIENT_W - MARGIN * 2, CTRL_H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)),
            hInst, nullptr);
        nextRow(46);

        // Row 8: Buttons
        m_btnApply = CreateWindowExW(0, L"BUTTON", L"Apply",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
            MARGIN, y, BTN_W, BTN_H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_APPLY)),
            hInst, nullptr);
        m_btnSave = CreateWindowExW(0, L"BUTTON", L"Save",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            MARGIN + BTN_W + 10, y, BTN_W, BTN_H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_SAVE)),
            hInst, nullptr);
        m_btnDefaults = CreateWindowExW(0, L"BUTTON", L"Restore Defaults",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            MARGIN + (BTN_W + 10) * 2, y, BTN_W + 20, BTN_H, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_DEFAULTS)),
            hInst, nullptr);

        // Populate controls from saved settings.
        populateControls(m_settings.getConfig().activeTheme);
        updateStatusBar();
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

    // ---- Accent color swatch (owner-draw static) ----------------------------
    case WM_DRAWITEM: {
        auto const* dis = reinterpret_cast<DRAWITEMSTRUCT const*>(lParam);
        if (dis->CtlID == ID_SWATCH) {
            AuraColor const& ac = m_settings.getConfig().activeTheme.accentColor;
            HBRUSH const hBrush =
                CreateSolidBrush(RGB(ac.r, ac.g, ac.b));
            FillRect(dis->hDC, &dis->rcItem, hBrush);
            DeleteObject(hBrush);
        }
        return TRUE;
    }

    // ---- Trackbar -----------------------------------------------------------
    case WM_HSCROLL: {
        if (reinterpret_cast<HWND>(lParam) == m_speedSlider) {
            LRESULT const pos =
                SendMessageW(m_speedSlider, TBM_GETPOS, 0, 0);
            std::wstring const label = std::to_wstring(pos) + L"%";
            SetWindowTextW(m_speedLabel, label.c_str());
        }
        return 0;
    }

    // ---- Button commands ----------------------------------------------------
    case WM_COMMAND: {
        int const ctrlId = LOWORD(wParam);
        if (ctrlId == ID_BTN_APPLY)    { onApply(); return 0; }
        if (ctrlId == ID_BTN_SAVE)     { onSave();  return 0; }
        if (ctrlId == ID_BTN_DEFAULTS) { onRestoreDefaults(); return 0; }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    // ---- Phase 8: DPI mixed-mode stress -----------------------------------
    // Fired when the window moves to (or is created on) a monitor with a
    // different DPI scale factor.  We must reposition using the suggested
    // RECT from lParam; failing to do so causes Mica and D2D swatches to
    // clip or stretch when crossing 100% ↔ 200% monitor boundaries.
    case WM_DPICHANGED: {
        UINT const newDpi = HIWORD(wParam);  // horizontal and vertical are always equal
        RECT const* const pRect = reinterpret_cast<RECT const*>(lParam);

        // Resize and reposition to the OS-suggested geometry — this prevents clipping.
        SetWindowPos(hwnd, nullptr,
            pRect->left,
            pRect->top,
            pRect->right  - pRect->left,
            pRect->bottom - pRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        // Force the D2D swatch to repaint at the new DPI.
        if (m_swatchStatic) {
            InvalidateRect(m_swatchStatic, nullptr, TRUE);
        }

        // Re-apply Mica — DWM resets backdrop type on some driver versions
        // when the window crosses DPI boundaries.
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

// ============================================================================
// Button handlers
// ============================================================================

void ConfigWindow::onApply() {
    ThemeConfig const theme = collectFromControls();
    m_settings.setTheme(theme);

    if (m_client.isConnected()) {
        bool const ok = m_client.pushTheme(theme);
        SetWindowTextW(m_statusStatic,
            ok ? L"● Service: theme applied" : L"● Service: push failed");
    } else {
        SetWindowTextW(m_statusStatic, L"● Service: not connected (saved locally)");
    }
}

void ConfigWindow::onSave() {
    ThemeConfig const theme = collectFromControls();
    m_settings.setTheme(theme);

    bool const ok = m_settings.save();
    SetWindowTextW(m_statusStatic,
        ok ? L"● Config saved to disk" : L"● Save failed — check permissions");
}

void ConfigWindow::onRestoreDefaults() {
    ThemeConfig const defaults;
    populateControls(defaults);
    m_settings.setTheme(defaults);
    SetWindowTextW(m_statusStatic, L"● Defaults restored (not yet saved)");
}

// ============================================================================
// Helpers
// ============================================================================

void ConfigWindow::updateStatusBar() {
    if (!m_statusStatic) return;
    SetWindowTextW(m_statusStatic,
        m_client.isConnected()
            ? L"● Service: connected"
            : L"● Service: not connected");
}

ThemeConfig ConfigWindow::collectFromControls() const {
    ThemeConfig theme;

    // Theme name
    wchar_t buf[256] = {};
    GetWindowTextW(m_themeEdit, buf, 255);
    theme.themeName = buf[0] != L'\0' ? buf : L"default";

    // Animation speed
    theme.animSpeedPct = static_cast<uint32_t>(
        SendMessageW(m_speedSlider, TBM_GETPOS, 0, 0)
    );

    // Checkboxes
    theme.showOnHover  = SendMessageW(m_hoverCheck,  BM_GETCHECK, 0, 0) == BST_CHECKED;
    theme.showOnLaunch = SendMessageW(m_launchCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    theme.glowEnabled  = SendMessageW(m_glowCheck,   BM_GETCHECK, 0, 0) == BST_CHECKED;

    // Keep the accent color from the current config (no colour picker in Phase 5).
    theme.accentColor  = m_settings.getConfig().activeTheme.accentColor;

    return theme;
}

void ConfigWindow::populateControls(ThemeConfig const& theme) {
    if (m_themeEdit) {
        SetWindowTextW(m_themeEdit, theme.themeName.c_str());
    }
    if (m_speedSlider) {
        SendMessageW(m_speedSlider, TBM_SETPOS, TRUE,
                     static_cast<LPARAM>(theme.animSpeedPct));
        std::wstring const label = std::to_wstring(theme.animSpeedPct) + L"%";
        SetWindowTextW(m_speedLabel, label.c_str());
    }
    if (m_hoverCheck) {
        SendMessageW(m_hoverCheck,  BM_SETCHECK,
                     theme.showOnHover  ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (m_launchCheck) {
        SendMessageW(m_launchCheck, BM_SETCHECK,
                     theme.showOnLaunch ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (m_glowCheck) {
        SendMessageW(m_glowCheck,   BM_SETCHECK,
                     theme.glowEnabled  ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (m_swatchStatic) {
        InvalidateRect(m_swatchStatic, nullptr, TRUE);
    }
}

}  // namespace aura::app
