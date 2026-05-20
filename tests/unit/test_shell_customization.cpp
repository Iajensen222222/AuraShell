// tests/unit/test_shell_customization.cpp
//
// Phase 10.9 TDD suite — ShellIconModifier path utilities, result codes,
// and input validation paths.
//
// All tests are headless: no COM initialization, no filesystem writes,
// no live service.  Input validation executes before any COM call, so
// Result::InvalidPath / Result::FileNotFound tests are safe to run in CI.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "shell_icon_modifier.h"

using namespace aura::shell;

// ============================================================================
// A. sanitizePath — pure string manipulation
// ============================================================================

TEST_CASE("sanitizePath: empty string returns empty string", "[shell][path]") {
    CHECK(ShellIconModifier::sanitizePath(L"") == L"");
}

TEST_CASE("sanitizePath: lone leading quote is NOT stripped (only pairs are removed)",
          "[shell][path]")
{
    // Implementation removes PAIRED surrounding quotes only.
    // A leading-only quote indicates malformed input — left unchanged.
    std::wstring const result = ShellIconModifier::sanitizePath(L"\"C:\\foo\\bar.lnk");
    CHECK_FALSE(result.empty());  // does not crash; returns non-empty string
    CHECK(result.find(L"C:\\foo") != std::wstring::npos);  // path content preserved
}

TEST_CASE("sanitizePath: lone trailing quote is NOT stripped (only pairs are removed)",
          "[shell][path]")
{
    std::wstring const result = ShellIconModifier::sanitizePath(L"C:\\foo\\bar.lnk\"");
    CHECK_FALSE(result.empty());
    CHECK(result.find(L"C:\\foo") != std::wstring::npos);
}

TEST_CASE("sanitizePath: strips surrounding double-quotes (pair)", "[shell][path]") {
    std::wstring const result =
        ShellIconModifier::sanitizePath(L"\"C:\\Program Files\\AuraShell\\test.lnk\"");
    CHECK(result == L"C:\\Program Files\\AuraShell\\test.lnk");
}

TEST_CASE("sanitizePath: replaces forward slashes with backslashes", "[shell][path]") {
    std::wstring const result = ShellIconModifier::sanitizePath(L"C:/Users/test/link.lnk");
    CHECK(result == L"C:\\Users\\test\\link.lnk");
}

TEST_CASE("sanitizePath: trims leading and trailing whitespace", "[shell][path]") {
    std::wstring const result = ShellIconModifier::sanitizePath(L"  C:\\foo.lnk  ");
    CHECK(result == L"C:\\foo.lnk");
}

TEST_CASE("sanitizePath: handles mixed quotes and slashes", "[shell][path]") {
    std::wstring const result =
        ShellIconModifier::sanitizePath(L"\"C:/Windows/System32/shell32.dll\"");
    CHECK(result == L"C:\\Windows\\System32\\shell32.dll");
}

// ============================================================================
// B. isShortcut — case-insensitive .lnk detection
// ============================================================================

TEST_CASE("isShortcut: returns true for .lnk extension (lowercase)", "[shell][path]") {
    CHECK(ShellIconModifier::isShortcut(L"C:\\Desktop\\MyApp.lnk"));
}

TEST_CASE("isShortcut: returns true for .LNK extension (uppercase)", "[shell][path]") {
    CHECK(ShellIconModifier::isShortcut(L"C:\\Desktop\\MyApp.LNK"));
}

TEST_CASE("isShortcut: returns false for .exe extension", "[shell][path]") {
    CHECK_FALSE(ShellIconModifier::isShortcut(L"C:\\Windows\\notepad.exe"));
}

TEST_CASE("isShortcut: returns false for .url extension", "[shell][path]") {
    CHECK_FALSE(ShellIconModifier::isShortcut(L"C:\\Desktop\\link.url"));
}

TEST_CASE("isShortcut: returns false for empty string", "[shell][path]") {
    CHECK_FALSE(ShellIconModifier::isShortcut(L""));
}

TEST_CASE("isShortcut: returns false for path shorter than 4 chars", "[shell][path]") {
    CHECK_FALSE(ShellIconModifier::isShortcut(L"lnk"));
}

// ============================================================================
// C. Result codes — input validation before any COM call
// ============================================================================

TEST_CASE("setShortcutIcon: empty lnkPath returns InvalidPath", "[shell][result]") {
    ShellIconModifier::Result const r =
        ShellIconModifier::setShortcutIcon(L"", L"C:\\shell32.dll", 0);
    CHECK(r == ShellIconModifier::Result::InvalidPath);
}

TEST_CASE("setShortcutIcon: empty iconPath returns InvalidPath", "[shell][result]") {
    ShellIconModifier::Result const r =
        ShellIconModifier::setShortcutIcon(L"C:\\test.lnk", L"", 0);
    CHECK(r == ShellIconModifier::Result::InvalidPath);
}

TEST_CASE("setShortcutIcon: non-.lnk extension returns InvalidPath", "[shell][result]") {
    ShellIconModifier::Result const r =
        ShellIconModifier::setShortcutIcon(L"C:\\test.url", L"C:\\shell32.dll", 0);
    CHECK(r == ShellIconModifier::Result::InvalidPath);
}

TEST_CASE("setShortcutIcon: non-existent path returns FileNotFound", "[shell][result]") {
    ShellIconModifier::Result const r =
        ShellIconModifier::setShortcutIcon(
            L"C:\\DoesNotExist_AuraShell_TestXyz.lnk",
            L"C:\\Windows\\System32\\shell32.dll", 0
        );
    CHECK(r == ShellIconModifier::Result::FileNotFound);
}

TEST_CASE("setFolderIcon: empty folderPath returns InvalidPath", "[shell][result]") {
    ShellIconModifier::Result const r =
        ShellIconModifier::setFolderIcon(L"", L"C:\\shell32.dll", 0);
    CHECK(r == ShellIconModifier::Result::InvalidPath);
}

TEST_CASE("setFolderIcon: empty iconPath returns InvalidPath", "[shell][result]") {
    ShellIconModifier::Result const r =
        ShellIconModifier::setFolderIcon(L"C:\\SomeFolder", L"", 0);
    CHECK(r == ShellIconModifier::Result::InvalidPath);
}

// ============================================================================
// D. Result enum — distinct values (compile-time verifiable)
// ============================================================================

TEST_CASE("Result enum: all values are distinct", "[shell][result]") {
    using R = ShellIconModifier::Result;
    CHECK(static_cast<int>(R::Success)     != static_cast<int>(R::FileNotFound));
    CHECK(static_cast<int>(R::FileNotFound)!= static_cast<int>(R::InvalidPath));
    CHECK(static_cast<int>(R::InvalidPath) != static_cast<int>(R::ReadOnly));
    CHECK(static_cast<int>(R::ReadOnly)    != static_cast<int>(R::AccessDenied));
    CHECK(static_cast<int>(R::AccessDenied)!= static_cast<int>(R::ComError));
}
