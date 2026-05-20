#include <catch2/catch_test_macros.hpp>
#include <windows.h>

#include "hotkey_manager.h"

using aura::system::HotkeyManager;
using aura::system::HotkeyAction;

// ============================================================================
// Singleton
// ============================================================================

TEST_CASE("HotkeyManager singleton identity", "[system][hotkey]") {
    REQUIRE(&HotkeyManager::getInstance() == &HotkeyManager::getInstance());
}

// ============================================================================
// Lifecycle — initialize on a real message-only HWND
// ============================================================================

TEST_CASE("HotkeyManager initialize and shutdown on message-only HWND", "[system][hotkey]") {
    // Create a temporary message-only HWND for the hotkey target.
    HWND msgHwnd = CreateWindowExW(0, L"STATIC", nullptr, WS_OVERLAPPED,
                                    0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);

    if (!msgHwnd) {
        WARN("Could not create message-only HWND — skipping hotkey registration tests");
        return;
    }

    auto& hm = HotkeyManager::getInstance();

    // initialize() may fail if the hotkeys are owned by another process on
    // this machine — that is acceptable; it must not throw.
    bool ok = hm.initialize(msgHwnd);
    REQUIRE(hm.isInitialized() == ok);

    hm.shutdown();
    REQUIRE_FALSE(hm.isInitialized());

    // Second shutdown must not crash.
    hm.shutdown();

    DestroyWindow(msgHwnd);
}

// ============================================================================
// handleMessage ignores non-WM_HOTKEY messages
// ============================================================================

TEST_CASE("HotkeyManager handleMessage returns false for non-WM_HOTKEY", "[system][hotkey]") {
    auto& hm = HotkeyManager::getInstance();
    REQUIRE_FALSE(hm.handleMessage(WM_PAINT, 0, 0));
    REQUIRE_FALSE(hm.handleMessage(WM_NULL,  0, 0));
    REQUIRE_FALSE(hm.handleMessage(WM_CLOSE, 0, 0));
}

// ============================================================================
// Callback subscription — subscribeAction must not crash
// ============================================================================

TEST_CASE("HotkeyManager subscribeAction does not crash before or after init",
          "[system][hotkey]") {
    auto& hm = HotkeyManager::getInstance();

    bool fired = false;
    REQUIRE_NOTHROW(
        hm.subscribeAction([&](HotkeyAction) { fired = true; })
    );

    // The callback should not have fired without a real WM_HOTKEY.
    REQUIRE_FALSE(fired);
}

// ============================================================================
// setBinding overrides a default mapping before initialize
// ============================================================================

TEST_CASE("HotkeyManager setBinding before initialize does not crash", "[system][hotkey]") {
    auto& hm = HotkeyManager::getInstance();

    // Must be called before initialize(); after init it is a no-op.
    REQUIRE_NOTHROW(
        hm.setBinding(HotkeyAction::ToggleOverlays, MOD_ALT | MOD_SHIFT, 'A')
    );
}

// ============================================================================
// HotkeyAction enum values are unique
// ============================================================================

TEST_CASE("HotkeyAction enum values are distinct", "[system][hotkey]") {
    REQUIRE(static_cast<uint32_t>(HotkeyAction::ToggleOverlays)   !=
            static_cast<uint32_t>(HotkeyAction::ToggleVisualizer));
    REQUIRE(static_cast<uint32_t>(HotkeyAction::ToggleOverlays)   !=
            static_cast<uint32_t>(HotkeyAction::CycleTheme));
    REQUIRE(static_cast<uint32_t>(HotkeyAction::ToggleVisualizer) !=
            static_cast<uint32_t>(HotkeyAction::CycleTheme));
}
