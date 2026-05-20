#pragma once

// page_behavior.h — AuraShell Phase 10.7
//
// BehaviorPage: three-card panel for system-level configuration.
//
//   Card 1: Shell Monitoring — auto-hide tracking and multi-monitor toggles
//   Card 2: System Boot      — Windows registry auto-start toggle
//   Card 3: Service Control  — live SCM status + Start / Stop / Restart buttons
//
// Service control actions run on a detached std::thread (async, 10s SCM timeout)
// and post WM_APP+1 to the page panel HWND when complete — UI never hitches.
//
// Registry auto-start uses HKCU Run key (no elevation required).
// Service start/stop uses ShellExecuteExW with "runas" verb (UAC prompt).

#include <Windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "app_client.h"
#include "settings_manager.h"
#include "card_renderer.h"
#include "ui_styles.h"

namespace aura::app {

// ============================================================================
// Service state enumeration
// ============================================================================

enum class ServiceStatus : uint8_t {
    Unknown,
    NotInstalled,
    Stopped,
    StartPending,
    Running,
};

// ============================================================================
// Service control actions
// ============================================================================

enum class ServiceAction : uint8_t { Start, Stop, Restart };

// ============================================================================
// BehaviorPage
// ============================================================================

class BehaviorPage {
public:
    explicit BehaviorPage(AppClient& client, SettingsManager& settings) noexcept;
    ~BehaviorPage();

    BehaviorPage(BehaviorPage const&)            = delete;
    BehaviorPage& operator=(BehaviorPage const&) = delete;

    [[nodiscard]] bool create(HWND pagePanel, HINSTANCE hInst,
                              ID2D1Factory* factory = nullptr) noexcept;

    void onVisible() noexcept;
    void onHidden()  noexcept;

    // ---- Pure geometry hit-tests — public so they are testable headless ----

    // Returns toggle index 0–2 or -1.
    // 0 = monitorAutoHide (Card 1)
    // 1 = enableMultiMonitor (Card 1)
    // 2 = autoStartApp (Card 2)
    [[nodiscard]] int32_t hitTestToggle(int32_t x, int32_t y) const noexcept;

    // Returns button index 0=Start, 1=Stop, 2=Restart, or -1.
    [[nodiscard]] int32_t hitTestButton(int32_t x, int32_t y) const noexcept;

    // ---- Registry auto-start helpers (exposed for testing indirectly) ----
    static constexpr wchar_t const* AUTOSTART_REG_KEY  =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    static constexpr wchar_t const* AUTOSTART_VAL_NAME = L"AuraShell";

private:
    // ---- Layout constants (content area 1060px wide) -------------------------
    static constexpr int CONTENT_W = 1060;
    static constexpr int MARGIN    =   24;
    static constexpr int PAD       =   16;
    static constexpr int TOG_W     =   44;
    static constexpr int TOG_H     =   24;

    // Card 1: Shell Monitoring
    static constexpr int CARD1_X   = MARGIN;
    static constexpr int CARD1_Y   =   88;
    static constexpr int CARD1_W   = CONTENT_W - MARGIN * 2;  // 1012
    static constexpr int CARD1_H   =  168;

    // Toggle positions within Card 1
    static constexpr int TOG_COL1_X = CARD1_X + CARD1_W - PAD - TOG_W;  // right-aligned
    static constexpr int TOG0_Y     = CARD1_Y + 56;   // auto-hide
    static constexpr int TOG1_Y     = CARD1_Y + 96;   // multi-monitor

    // Card 2: System Boot
    static constexpr int CARD2_X   = MARGIN;
    static constexpr int CARD2_Y   = CARD1_Y + CARD1_H + 16;  // 272
    static constexpr int CARD2_W   = CARD1_W;
    static constexpr int CARD2_H   =  120;

    // Toggle position within Card 2
    static constexpr int TOG_COL2_X = CARD2_X + CARD2_W - PAD - TOG_W;
    static constexpr int TOG2_Y     = CARD2_Y + 56;  // auto-start

    // Card 3: Service Management
    static constexpr int CARD3_X   = MARGIN;
    static constexpr int CARD3_Y   = CARD2_Y + CARD2_H + 16;  // 408
    static constexpr int CARD3_W   = CARD1_W;
    static constexpr int CARD3_H   =  228;

    // Service buttons within Card 3
    static constexpr int BTN_W     = 140;
    static constexpr int BTN_H     =  36;
    static constexpr int BTN_GAP   =  12;
    static constexpr int BTN_Y     = CARD3_Y + 104;
    static constexpr int BTN_START_X = CARD3_X + PAD;
    static constexpr int BTN_STOP_X  = BTN_START_X + BTN_W + BTN_GAP;
    static constexpr int BTN_RST_X   = BTN_STOP_X  + BTN_W + BTN_GAP;

    // ---- Subclass proc ----
    static LRESULT CALLBACK subclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT handlePanelMsg(HWND, UINT, WPARAM, LPARAM) noexcept;

    // ---- Rendering ----
    void drawPageContent(HWND panelHwnd, HDC hdc) noexcept;
    void drawServiceButton(
        ID2D1RenderTarget* rt, int x, int y, int w, int h,
        wchar_t const* label, bool isPrimary, bool isPending
    ) noexcept;

    // ---- Service control ----
    void executeServiceAction(ServiceAction action) noexcept;
    void refreshServiceStatus() noexcept;

    // ---- Registry auto-start ----
    static void  setRegistryAutoStart(bool enable) noexcept;
    [[nodiscard]] static bool getRegistryAutoStart() noexcept;

    // ---- Helpers ----
    [[nodiscard]] static std::wstring getServiceExePath() noexcept;

    // ---- State ----
    HWND          m_pagePanel{nullptr};
    AppClient&    m_client;
    SettingsManager& m_settings;

    ServiceStatus          m_serviceStatus{ServiceStatus::Unknown};
    std::atomic<bool>      m_serviceActionPending{false};

    ID2D1Factory*                                m_d2dFactory{nullptr};  // shared, not owned
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget>  m_rt;
};

}  // namespace aura::app
