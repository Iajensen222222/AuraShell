#pragma once

// page_about.h — AuraShell Phase 10.8
//
// AboutPage: read-only information panel.
//   Card 1: App information (version, build platform, repository)
//   Card 2: License (MIT licence header)
//   Card 3: Activity Log — lists log files from %LOCALAPPDATA%\AuraShell\logs,
//            shows last 25 lines of the most recent file in a native EDIT control.

#include <Windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>

#include <cstdint>
#include <string>

#include "card_renderer.h"

namespace aura::app {

class AboutPage {
public:
    AboutPage() noexcept = default;
    ~AboutPage();

    AboutPage(AboutPage const&)            = delete;
    AboutPage& operator=(AboutPage const&) = delete;

    [[nodiscard]] bool create(HWND pagePanel, HINSTANCE hInst,
                              ID2D1Factory* factory = nullptr) noexcept;

    void onVisible() noexcept;
    void onHidden()  noexcept;

private:
    // ---- Layout constants ----
    static constexpr int CONTENT_W = 1060;
    static constexpr int MARGIN    =   24;
    static constexpr int PAD       =   16;

    static constexpr int CARD1_X   = MARGIN;
    static constexpr int CARD1_Y   =   88;
    static constexpr int CARD1_W   = CONTENT_W - MARGIN * 2;  // 1012
    static constexpr int CARD1_H   =  168;

    static constexpr int CARD2_X   = MARGIN;
    static constexpr int CARD2_Y   = CARD1_Y + CARD1_H + 16;  // 272
    static constexpr int CARD2_W   = CARD1_W;
    static constexpr int CARD2_H   =  120;

    static constexpr int CARD3_X   = MARGIN;
    static constexpr int CARD3_Y   = CARD2_Y + CARD2_H + 16;  // 408
    static constexpr int CARD3_W   = CARD1_W;
    static constexpr int CARD3_H   =  280;

    // Log viewer EDIT sits inside Card 3
    static constexpr int LOG_EDIT_X = CARD3_X + PAD;
    static constexpr int LOG_EDIT_Y = CARD3_Y + 60;
    static constexpr int LOG_EDIT_W = CARD3_W - PAD * 2;
    static constexpr int LOG_EDIT_H = CARD3_H - 80;

    static constexpr int ID_LOG_EDIT    = 3001;
    static constexpr int ID_LOG_REFRESH = 3002;

    // ---- Subclass proc ----
    static LRESULT CALLBACK subclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT handlePanelMsg(HWND, UINT, WPARAM, LPARAM) noexcept;

    // ---- Rendering ----
    void drawPageContent(HWND panelHwnd, HDC hdc) noexcept;

    // ---- Log viewer ----
    void loadLatestLog() noexcept;
    [[nodiscard]] static std::wstring findLatestLogFile() noexcept;
    [[nodiscard]] static std::wstring readLastLines(
        std::wstring const& path, int maxLines
    ) noexcept;

    // ---- State ----
    HWND  m_pagePanel{nullptr};
    HWND  m_logEdit  {nullptr};
    HWND  m_refreshBtn{nullptr};

    ID2D1Factory*                                m_d2dFactory{nullptr};  // shared
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget>  m_rt;
};

}  // namespace aura::app
