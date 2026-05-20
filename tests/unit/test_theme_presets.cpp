#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

#include "theme_preset_loader.h"

using aura::context::ThemePresetLoader;

// ============================================================================
// Singleton
// ============================================================================

TEST_CASE("ThemePresetLoader singleton identity", "[context][presets]") {
    REQUIRE(&ThemePresetLoader::getInstance() == &ThemePresetLoader::getInstance());
}

// ============================================================================
// Default preset always present
// ============================================================================

TEST_CASE("ThemePresetLoader always has at least one preset", "[context][presets]") {
    auto& loader = ThemePresetLoader::getInstance();
    REQUIRE(loader.count() >= 1);
    REQUIRE_FALSE(loader.getPresets().empty());
}

TEST_CASE("ThemePresetLoader default preset is Windows Blue", "[context][presets]") {
    auto& loader = ThemePresetLoader::getInstance();
    const auto* p = loader.findByName(L"Windows Blue");
    REQUIRE(p != nullptr);
    REQUIRE(p->accentColor.r == 0);
    REQUIRE(p->accentColor.g == 120);
    REQUIRE(p->accentColor.b == 212);
}

// ============================================================================
// findByName case-insensitivity
// ============================================================================

TEST_CASE("ThemePresetLoader findByName is case-insensitive", "[context][presets]") {
    auto& loader = ThemePresetLoader::getInstance();
    REQUIRE(loader.findByName(L"WINDOWS BLUE") != nullptr);
    REQUIRE(loader.findByName(L"windows blue") != nullptr);
    REQUIRE(loader.findByName(L"Windows Blue") != nullptr);
}

TEST_CASE("ThemePresetLoader findByName returns nullptr for unknown name", "[context][presets]") {
    auto& loader = ThemePresetLoader::getInstance();
    REQUIRE(loader.findByName(L"NoSuchTheme_XYZ") == nullptr);
}

// ============================================================================
// loadFromDirectory with a temp directory
// ============================================================================

TEST_CASE("ThemePresetLoader loadFromDirectory parses valid JSON", "[context][presets]") {
    // Write a temp JSON preset.
    auto tmpDir = std::filesystem::temp_directory_path() / "aurashell_test_presets";
    std::filesystem::create_directories(tmpDir);

    {
        std::ofstream f(tmpDir / "test_preset.json");
        f << R"({
  "themeName": "Test Crimson",
  "accentColor": { "r": 220, "g": 20, "b": 60, "a": 255 },
  "animSpeedPct": 120,
  "showOnHover": true,
  "showOnLaunch": false,
  "glowEnabled": true
})";
    }

    auto& loader = ThemePresetLoader::getInstance();
    int n = loader.loadFromDirectory(tmpDir.wstring());
    REQUIRE(n >= 1);

    const auto* p = loader.findByName(L"Test Crimson");
    REQUIRE(p != nullptr);
    REQUIRE(p->accentColor.r == 220);
    REQUIRE(p->accentColor.g == 20);
    REQUIRE(p->accentColor.b == 60);
    REQUIRE(p->animSpeedPct == 120u);
    REQUIRE_FALSE(p->showOnLaunch);

    // Cleanup
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("ThemePresetLoader loadFromDirectory gracefully handles missing dir",
          "[context][presets]") {
    auto& loader = ThemePresetLoader::getInstance();
    int n = loader.loadFromDirectory(L"C:\\this\\path\\does\\not\\exist");
    // Must fall back to at least the built-in default.
    REQUIRE(n >= 1);
    REQUIRE(loader.findByName(L"Windows Blue") != nullptr);
}

TEST_CASE("ThemePresetLoader loadFromDirectory skips non-json files", "[context][presets]") {
    auto tmpDir = std::filesystem::temp_directory_path() / "aurashell_test_nonjson";
    std::filesystem::create_directories(tmpDir);

    { std::ofstream(tmpDir / "readme.txt") << "not json"; }
    { std::ofstream(tmpDir / "image.png")  << "\x89PNG"; }

    auto& loader = ThemePresetLoader::getInstance();
    int n = loader.loadFromDirectory(tmpDir.wstring());
    // No JSON files → falls back to default (>= 1).
    REQUIRE(n >= 1);

    std::filesystem::remove_all(tmpDir);
}
