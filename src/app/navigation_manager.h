#pragma once

// navigation_manager.h — AuraShell Phase 10.2 / Phase 10.9
//
// Owns the sidebar child window and the kinetic accent indicator for the
// AuraConfig navigation rail.
//
// Phase 10.9: Added Page::DesktopItems (5th page) for icon customization.

#include <Windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>

#include "ui_styles.h"

namespace aura::app {

// ----------------------------------------------------------------------------
// Page enumeration — one entry per sidebar item.
// Page::Count is a sentinel, never a valid navigation target.
// ----------------------------------------------------------------------------

enum class Page : uint8_t {
    Dashboard    = 0,
    Visuals      = 1,
    Behavior     = 2,
    About        = 3,
    DesktopItems = 4,   // Phase 10.9: shortcut/folder icon customizer
    Count               // sentinel
};

// ----------------------------------------------------------------------------
// NavigationManager
// ----------------------------------------------------------------------------

class NavigationManager {
public:
    // ---- Compile-time layout constants (logical px at 96 DPI) ----
    static constexpr int32_t ITEM_COUNT  = static_cast<int32_t>(Page::Count);
    static constexpr int32_t HEADER_H    = 60;  // title / logo area above items

    // ---- Construction / destruction ----
    NavigationManager() noexcept;
    ~NavigationManager();

    NavigationManager(NavigationManager const&)            = delete;
    NavigationManager& operator=(NavigationManager const&) = delete;

    // ---- Window lifetime ----
    [[nodiscard]] bool create(HWND parent, HINSTANCE hInst) noexcept;
    void onParentResize(int32_t parentH) noexcept;

    // ---- Navigation ----
    void navigateTo(Page page) noexcept;
    [[nodiscard]] Page currentPage() const noexcept;
    void registerPageContent(Page page, HWND contentHwnd) noexcept;

    using PageChangedCallback = std::function<void(Page newPage)>;
    void setPageChangedCallback(PageChangedCallback cb) noexcept;

    // ---- Pure geometry hit-tests (testable without Win32) ----
    [[nodiscard]] std::optional<Page> hitTest(int32_t sidebarY) const noexcept;
    [[nodiscard]] float indicatorTargetY(Page page) const noexcept;

    // ---- Spring state accessors (unit-test inspection) ----
    [[nodiscard]] float                         indicatorY()            const noexcept;
    [[nodiscard]] ui::SpringState const&        indicatorSpring()       const noexcept;
    [[nodiscard]] float                         indicatorSpringTarget() const noexcept;

    // Test-only: replace the internal SpringState.
    void setIndicatorSpringForTest(ui::SpringState const& s) noexcept;

private:
    // ---- Sidebar window class ----
    static constexpr wchar_t const* SIDEBAR_CLASS = L"AuraNavSidebar";
    static constexpr UINT_PTR       SPRING_TIMER  = 9001;
    static constexpr int32_t        TIMER_MS      = 16;  // ~60 Hz

    struct NavEntry {
        wchar_t const* label;
        wchar_t const* icon;   // Segoe MDL2 / Fluent Icons glyph
        HWND           content{nullptr};
    };

    static constexpr NavEntry NAV_INIT[ITEM_COUNT] = {
        {L"Dashboard",     L""},  // Home
        {L"Visuals",       L""},  // Color
        {L"Behavior",      L""},  // Settings
        {L"About",         L""},  // Info
        {L"Desktop Items", L""},  // Phase 10.9: icon customizer
    };

    // ---- Win32 plumbing ----
    [[nodiscard]] static bool registerSidebarClass(HINSTANCE hInst) noexcept;

    static LRESULT CALLBACK sidebarWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleSidebarMsg(HWND hwnd, UINT msg,
                             WPARAM wParam, LPARAM lParam) noexcept;

    void drawSidebar(HWND hwnd, HDC hdc) noexcept;
    void startAnimation(float targetY) noexcept;
    void onAnimationTick() noexcept;
    void showPage(Page page) noexcept;

    // ---- State ----
    HWND   m_hwnd{nullptr};
    Page   m_currentPage{Page::Dashboard};
    Page   m_hoveredItem{Page::Count};   // Page::Count == "none"

    ui::SpringState m_indicatorSpring{};
    float           m_indicatorY{0.0f};
    float           m_targetY{0.0f};
    bool            m_animating{false};

    std::array<NavEntry, ITEM_COUNT> m_items{};
    PageChangedCallback              m_pageChangedCb;

    static bool s_classRegistered;
};

} // namespace aura::app
