#pragma once

// page_desktop_items.h — AuraShell Phase 10.9
//
// DesktopItemsPage: drag-and-drop shortcut/folder icon customizer.
//   Card 1: Drop Zone — accepts .lnk shortcuts or folders via WM_DROPFILES
//   Card 2: Current Item — shows loaded path, type, and current icon location
//   Card 3: Icon Selection — browse + apply + restore default

#include <Windows.h>
#include <shellapi.h>  // HDROP, DragAcceptFiles
#include <commctrl.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl.h>

#include <cstdint>
#include <string>

#include "card_renderer.h"
#include "shell_icon_modifier.h"

namespace aura::app {

class DesktopItemsPage {
public:
    DesktopItemsPage() noexcept = default;
    ~DesktopItemsPage();

    DesktopItemsPage(DesktopItemsPage const&)            = delete;
    DesktopItemsPage& operator=(DesktopItemsPage const&) = delete;

    [[nodiscard]] bool create(HWND pagePanel, HINSTANCE hInst,
                              ID2D1Factory* factory = nullptr) noexcept;

    void onVisible() noexcept;
    void onHidden()  noexcept;

private:
    // ---- Layout constants ----
    static constexpr int CONTENT_W = 1060;
    static constexpr int MARGIN    =   24;
    static constexpr int PAD       =   16;

    // Card 1: Drop Zone
    static constexpr int CARD1_X   = MARGIN;
    static constexpr int CARD1_Y   =   88;
    static constexpr int CARD1_W   = CONTENT_W - MARGIN * 2;  // 1012
    static constexpr int CARD1_H   =  228;

    // Card 2: Current Item
    static constexpr int CARD2_X   = MARGIN;
    static constexpr int CARD2_Y   = CARD1_Y + CARD1_H + 16;  // 332
    static constexpr int CARD2_W   = CARD1_W;
    static constexpr int CARD2_H   =  132;

    // Card 3: Icon Selection
    static constexpr int CARD3_X   = MARGIN;
    static constexpr int CARD3_Y   = CARD2_Y + CARD2_H + 16;  // 480
    static constexpr int CARD3_W   = CARD1_W;
    static constexpr int CARD3_H   =  148;

    // Buttons in Card 3
    static constexpr int BTN_Y     = CARD3_Y + 72;
    static constexpr int BTN_W     = 140;
    static constexpr int BTN_H     =  36;
    static constexpr int BTN_GAP   =  12;
    static constexpr int BTN_BROWSE_X  = CARD3_X + PAD;
    static constexpr int BTN_APPLY_X   = BTN_BROWSE_X + BTN_W + BTN_GAP;
    static constexpr int BTN_RESTORE_X = BTN_APPLY_X  + BTN_W + BTN_GAP;

    static constexpr int ID_BTN_BROWSE  = 4001;
    static constexpr int ID_BTN_APPLY   = 4002;
    static constexpr int ID_BTN_RESTORE = 4003;

    // ---- Subclass proc ----
    static LRESULT CALLBACK subclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT handlePanelMsg(HWND, UINT, WPARAM, LPARAM) noexcept;

    // ---- Rendering ----
    void drawPageContent(HWND panelHwnd, HDC hdc) noexcept;

    // ---- Interaction ----
    void onFileDrop(HWND hwnd, HDROP hDrop) noexcept;
    void onBrowseIcon() noexcept;
    void onApplyIcon() noexcept;
    void onRestoreDefault() noexcept;

    // ---- State ----
    HWND  m_pagePanel{nullptr};
    HWND  m_btnBrowse {nullptr};
    HWND  m_btnApply  {nullptr};
    HWND  m_btnRestore{nullptr};

    std::wstring m_itemPath;          // dropped file or folder path
    std::wstring m_selectedIconPath;  // icon file chosen via Browse
    int          m_selectedIconIndex{0};

    bool m_isShortcut{false};   // true = .lnk, false = folder

    ID2D1Factory*                                m_d2dFactory{nullptr};
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget>  m_rt;
};

}  // namespace aura::app
