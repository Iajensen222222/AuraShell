#include "acrylic_backdrop.h"

#include "logging/logger.h"

namespace aura::taskbar {

bool AcrylicBackdrop::initialize(HINSTANCE hInstance) {
    if (m_initialized) return true;
    m_hInstance = hInstance;

    // Register window class.
    // hbrBackground = nullptr + WM_ERASEBKGND returning 1 lets DWM composite
    // the acrylic without any GDI fill competing with it.
    WNDCLASSEXW wc     = {};
    wc.cbSize          = sizeof(wc);
    wc.lpfnWndProc     = AcrylicBackdrop::staticWndProc;
    wc.hInstance       = hInstance;
    wc.lpszClassName   = kClass;
    wc.hbrBackground   = nullptr;

    if (!RegisterClassExW(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            aura::logging::Logger::getInstance().warn("overlay",
                "AcrylicBackdrop: RegisterClassExW failed " + std::to_string(err));
            return false;
        }
    }

    // Create the window off-screen initially (size 1×1 at -9999,-9999).
    // WS_CAPTION + WS_THICKFRAME are required for DWM to extend the frame into
    // the client area, which is needed for DWMSBT_TRANSIENTWINDOW to render.
    // WM_NCCALCSIZE returning 0 then removes the non-client chrome so the
    // entire window rect is usable as the acrylic surface.
    m_hwnd = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kClass, nullptr,
        WS_POPUP | WS_CAPTION | WS_THICKFRAME,
        -9999, -9999, 1, 1,
        nullptr, nullptr, hInstance, this);

    if (!m_hwnd) {
        aura::logging::Logger::getInstance().warn("overlay",
            "AcrylicBackdrop: CreateWindowExW failed " + std::to_string(GetLastError()));
        return false;
    }

    applyDwmAttributes();

    m_initialized = true;
    return true;
}

void AcrylicBackdrop::shutdown() {
    if (!m_initialized) return;
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    UnregisterClassW(kClass, m_hInstance);
    m_initialized = false;
    m_visible.store(false);
}

void AcrylicBackdrop::showAt(const RECT& iconRect, HWND overlayHwnd) {
    if (!m_initialized) return;

    int x = iconRect.left   - MARGIN;
    int y = iconRect.top    - MARGIN;
    int w = (iconRect.right  - iconRect.left) + MARGIN * 2;
    int h = (iconRect.bottom - iconRect.top)  + MARGIN * 2;

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // Ensure the D2D glow overlay stays in front of us.
    if (overlayHwnd && IsWindow(overlayHwnd)) {
        SetWindowPos(overlayHwnd, m_hwnd,
                     0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    m_visible.store(true);
}

void AcrylicBackdrop::moveTo(const RECT& iconRect) {
    if (!m_initialized || !m_visible.load()) return;

    int x = iconRect.left   - MARGIN;
    int y = iconRect.top    - MARGIN;
    int w = (iconRect.right  - iconRect.left) + MARGIN * 2;
    int h = (iconRect.bottom - iconRect.top)  + MARGIN * 2;

    SetWindowPos(m_hwnd, nullptr, x, y, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void AcrylicBackdrop::hide() {
    if (!m_initialized) return;
    ShowWindow(m_hwnd, SW_HIDE);
    m_visible.store(false);
}

bool AcrylicBackdrop::isVisible() const {
    return m_visible.load(std::memory_order_relaxed);
}

void AcrylicBackdrop::applyDwmAttributes() {
    // Acrylic backdrop (Windows 11 22H2+; no-op on earlier builds).
    int backdrop = DWMSBT_TRANSIENTWINDOW_VAL;
    DwmSetWindowAttribute(m_hwnd,
                          static_cast<DWORD>(DWMWA_SYSTEMBACKDROP_TYPE_VAL),
                          &backdrop, sizeof(backdrop));

    // Respect the system dark-mode preference by asking Windows to
    // read the registry. If the system is in light mode this also works —
    // the acrylic tint colour is chosen by DWM automatically.
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(m_hwnd,
                          static_cast<DWORD>(DWMWA_USE_IMMERSIVE_DARK_MODE_VAL),
                          &darkMode, sizeof(darkMode));
}

LRESULT CALLBACK AcrylicBackdrop::staticWndProc(HWND hwnd, UINT msg,
                                                  WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<AcrylicBackdrop*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->wndProc(hwnd, msg, wp, lp)
               : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT AcrylicBackdrop::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCALCSIZE:
        // Return 0 to use the entire window rect as client area, removing all
        // non-client chrome (title bar, resize borders) while keeping the DWM
        // extended frame that DWMSBT_TRANSIENTWINDOW requires.
        return 0;

    case WM_ERASEBKGND:
        // Claim we erased — DWM composites the acrylic over the transparent area.
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        // Intentionally empty — DWM renders the acrylic, not GDI.
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_NCHITTEST:
        // Always pass through — must not intercept clicks.
        return HTTRANSPARENT;

    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace aura::taskbar
