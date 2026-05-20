#pragma once

#include <Windows.h>
#include <array>
#include <d2d1.h>
#include <wrl.h>

#include "app_client.h"
#include "settings_manager.h"
#include "navigation_manager.h"
#include "page_dashboard.h"
#include "page_visuals.h"    // Phase 10.6
#include "page_behavior.h"   // Phase 10.7
#include "page_about.h"          // Phase 10.8
#include "page_desktop_items.h"  // Phase 10.9

namespace aura::app {

// ============================================================================
// ConfigWindow — Win32 window with Windows 11 Mica backdrop.
//
// Phase 10.4 layout: 1280 × 800 client area ("Experience" window).
//   • 220px sidebar  (NavigationManager, x=0)
//   • 1060px content (page panels, x=220)
//
// Phase 10.8: owns one shared ID2D1Factory passed to all page modules.
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

    static bool s_classRegistered;

    // ---- Window ----
    HWND m_hwnd{nullptr};

    // ---- Dependencies ----
    AppClient&       m_client;
    SettingsManager& m_settings;

    // ---- Shared D2D factory (Phase 10.8) ----
    // Owned here; raw non-owning pointer passed to each page's create().
    // Pages must not Release() or AddRef() this pointer.
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;

    // ---- Navigation rail ----
    NavigationManager m_nav;

    // ---- Page implementations ----
    DashboardPage    m_dashboardPage;    // Phase 10.3
    VisualsPage      m_visualsPage;     // Phase 10.6
    BehaviorPage     m_behaviorPage;    // Phase 10.7
    AboutPage        m_aboutPage;       // Phase 10.8
    DesktopItemsPage m_desktopItemsPage; // Phase 10.9

    // ---- Page panel root HWNDs ----
    static constexpr int PAGE_COUNT = NavigationManager::ITEM_COUNT;
    std::array<HWND, PAGE_COUNT> m_pages{};

    // ---- Layout constants (Redesign: 1280×800, wider sidebar for Lively style) ----
    static constexpr int SIDEBAR_W = 240;   // was 220 — matches metrics::SIDEBAR_WIDTH
    static constexpr int CLIENT_W  = 1280;
    static constexpr int CLIENT_H  = 800;
    static constexpr int CONTENT_X = SIDEBAR_W;
    static constexpr int CONTENT_W = CLIENT_W - SIDEBAR_W;  // 1040
};

}  // namespace aura::app
