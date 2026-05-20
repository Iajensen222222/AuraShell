#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <string>

#include "theme_applier.h"
#include "theme_model.h"

using aura::context::ThemeApplier;
using aura::app::ThemeConfig;
using aura::app::AuraColor;

// ============================================================================
// Helpers
// ============================================================================

static DWORD readHKCU(const wchar_t* path, const wchar_t* name, DWORD fallback = 0) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return fallback;
    DWORD val = 0, size = sizeof(val), type = REG_DWORD;
    if (RegQueryValueExW(hKey, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(&val), &size) != ERROR_SUCCESS)
        val = fallback;
    RegCloseKey(hKey);
    return val;
}

// ============================================================================
// ThemeApplier tests
// ============================================================================

TEST_CASE("ThemeApplier singleton identity", "[integration][theme]") {
    ThemeApplier& a = ThemeApplier::getInstance();
    ThemeApplier& b = ThemeApplier::getInstance();
    REQUIRE(&a == &b);
}

TEST_CASE("ThemeApplier: apply then restoreDefaults round-trips the registry",
          "[integration][theme]") {
    ThemeApplier& ta = ThemeApplier::getInstance();

    // Snapshot registry values BEFORE apply.
    constexpr wchar_t kAccent[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";
    constexpr wchar_t kPerson[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

    DWORD beforeAccent = readHKCU(kAccent, L"AccentColorMenu");
    DWORD beforeLight  = readHKCU(kPerson, L"AppsUseLightTheme");
    DWORD beforeTrans  = readHKCU(kPerson, L"EnableTransparency");

    // Apply a test theme with a recognisable blue accent.
    ThemeConfig t;
    t.themeName   = L"test_apply";
    t.accentColor = AuraColor{0x00, 0x67, 0xC0, 0xFF}; // Windows 11 blue
    t.glowEnabled = true;
    t.showOnHover = true;  // light mode

    ta.apply(t);
    REQUIRE(ta.hasBackup());

    // Verify accent color written (Windows ABGR: 0xFF C0 67 00).
    DWORD expectedABGR = 0xFF'C0'67'00u;
    DWORD writtenAccent = readHKCU(kAccent, L"AccentColorMenu");
    REQUIRE(writtenAccent == expectedABGR);

    // Restore and verify original values come back.
    ta.restoreDefaults();
    REQUIRE_FALSE(ta.hasBackup());

    DWORD afterAccent = readHKCU(kAccent, L"AccentColorMenu");
    DWORD afterLight  = readHKCU(kPerson, L"AppsUseLightTheme");
    DWORD afterTrans  = readHKCU(kPerson, L"EnableTransparency");

    REQUIRE(afterAccent == beforeAccent);
    REQUIRE(afterLight  == beforeLight);
    REQUIRE(afterTrans  == beforeTrans);
}

TEST_CASE("ThemeApplier: restoreDefaults is safe when called with no backup",
          "[integration][theme]") {
    ThemeApplier& ta = ThemeApplier::getInstance();
    REQUIRE_NOTHROW(ta.restoreDefaults());
}
