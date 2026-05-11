#pragma once

#include <Windows.h>

#include "app_client.h"
#include "settings_manager.h"

namespace aura::app {

// ============================================================================
// ConfigWindow — Win32 window with Windows 11 Mica backdrop.
//
// Layout: 460 × 380 client area, non-resizable.
// Mica applied via DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE).
// Controls: native Win32 EDIT, BUTTON, static, BS_AUTOCHECKBOX, trackbar.
// ============================================================================

class ConfigWindow {
public:
    explicit ConfigWindow(AppClient& client, SettingsManager& settings);
    ~ConfigWindow();

    ConfigWindow(ConfigWindow const&)            = delete;
    ConfigWindow& operator=(ConfigWindow const&) = delete;

    // Register class and create the window.  Call once before runMessageLoop().
    [[nodiscard]] bool create(HINSTANCE hInst);

    // Standard Win32 message pump.  Returns WM_QUIT wParam.
    [[nodiscard]] int runMessageLoop();

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);

    void onApply();
    void onSave();
    void onRestoreDefaults();
    void updateStatusBar();

    [[nodiscard]] ThemeConfig collectFromControls() const;
    void populateControls(ThemeConfig const& theme);

    static bool s_classRegistered;

    // ---- Window ----
    HWND m_hwnd{nullptr};

    // ---- Dependencies ----
    AppClient&       m_client;
    SettingsManager& m_settings;

    // ---- Child controls ----
    HWND m_themeEdit{nullptr};
    HWND m_speedSlider{nullptr};
    HWND m_speedLabel{nullptr};
    HWND m_hoverCheck{nullptr};
    HWND m_launchCheck{nullptr};
    HWND m_glowCheck{nullptr};
    HWND m_swatchStatic{nullptr};
    HWND m_statusStatic{nullptr};
    HWND m_btnApply{nullptr};
    HWND m_btnSave{nullptr};
    HWND m_btnDefaults{nullptr};

    // ---- Layout constants ----
    static constexpr int MARGIN      = 20;
    static constexpr int CLIENT_W    = 460;
    static constexpr int CLIENT_H    = 380;
    static constexpr int CTRL_H      = 22;
    static constexpr int LABEL_W     = 120;
    static constexpr int EDIT_W      = 200;
    static constexpr int SWATCH_W    = 40;
    static constexpr int BTN_W       = 110;
    static constexpr int BTN_H       = 28;

    // ---- Control IDs ----
    static constexpr int ID_THEME_EDIT    = 1001;
    static constexpr int ID_SPEED_SLIDER  = 1002;
    static constexpr int ID_HOVER_CHECK   = 1003;
    static constexpr int ID_LAUNCH_CHECK  = 1004;
    static constexpr int ID_GLOW_CHECK    = 1005;
    static constexpr int ID_SWATCH        = 1006;
    static constexpr int ID_STATUS        = 1007;
    static constexpr int ID_BTN_APPLY     = 1008;
    static constexpr int ID_BTN_SAVE      = 1009;
    static constexpr int ID_BTN_DEFAULTS  = 1010;
    static constexpr int ID_SPEED_LABEL   = 1011;
};

}  // namespace aura::app
