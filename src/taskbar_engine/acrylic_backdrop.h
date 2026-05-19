#pragma once

#include <atomic>
#include <windows.h>
#include <dwmapi.h>

namespace aura::taskbar {

// Creates a single non-layered companion popup window that carries a genuine
// Windows 11 Acrylic backdrop (DWMSBT_TRANSIENTWINDOW) via DwmSetWindowAttribute.
//
// Layered windows (ULW) cannot host DWM backdrop effects, so this sits behind
// the existing D2D glow overlay and the two windows together produce:
//
//   [acrylic blur behind icon]  ← AcrylicBackdrop HWND (this class)
//   [cyan-blue glow rings]      ← IconOverlayManager's layered HWND (on top)
//
// Lifecycle: initialize() once, then showAt/moveTo/hide per hover event.
// Not a singleton — owned as a member of IconOverlayManager.
class AcrylicBackdrop {
public:
    AcrylicBackdrop() = default;
    ~AcrylicBackdrop() { shutdown(); }
    AcrylicBackdrop(const AcrylicBackdrop&) = delete;
    AcrylicBackdrop& operator=(const AcrylicBackdrop&) = delete;

    // Register the window class and create the backing HWND.
    bool initialize(HINSTANCE hInstance);

    // Release the HWND and unregister the class.
    void shutdown();

    // Show the backdrop centred on iconRect (expanded by MARGIN pixels each side).
    // overlayHwnd — the D2D glow overlay that must remain in front; pass nullptr
    // to skip the explicit Z-order pass.
    void showAt(const RECT& iconRect, HWND overlayHwnd = nullptr);

    // Reposition without a hide/show cycle (used when the taskbar icon moves).
    void moveTo(const RECT& iconRect);

    // Hide the backdrop.
    void hide();

    bool isVisible() const;

private:
    void applyDwmAttributes();

    static LRESULT CALLBACK staticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND      m_hwnd{};
    HINSTANCE m_hInstance{};
    bool      m_initialized{false};
    std::atomic<bool> m_visible{false};

    static constexpr int     MARGIN     = 6;   // px expansion beyond icon rect
    static constexpr wchar_t kClass[]   = L"AuraShell_Acrylic";

    // Windows 11 DWM constants — defined here so we compile on older SDK versions.
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
    static constexpr DWORD DWMWA_SYSTEMBACKDROP_TYPE_VAL = 38;
#else
    static constexpr DWORD DWMWA_SYSTEMBACKDROP_TYPE_VAL = DWMWA_SYSTEMBACKDROP_TYPE;
#endif
    static constexpr int DWMSBT_TRANSIENTWINDOW_VAL = 3;  // Acrylic for flyouts/popups

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
    static constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_VAL = 20;
#else
    static constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_VAL = DWMWA_USE_IMMERSIVE_DARK_MODE;
#endif
};

} // namespace aura::taskbar
