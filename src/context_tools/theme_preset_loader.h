#pragma once

#include <string>
#include <vector>

#include "theme_model.h"

namespace aura::context {

// Loads built-in theme presets from the data/themes/ directory that ships
// alongside the AuraShell binaries. Each .json file in that directory is
// parsed into a ThemeConfig and added to the preset list.
//
// Falls back to a single hardcoded "Windows Blue" default if the directory
// cannot be found (portable / CI environments).
//
// Usage:
//   auto& loader = ThemePresetLoader::getInstance();
//   loader.loadFromDirectory(presetsDir);
//   for (auto& p : loader.getPresets()) { /* populate UI dropdown */ }
class ThemePresetLoader {
public:
    static ThemePresetLoader& getInstance();

    // Load all *.json files from presetsDirectory.
    // Clears any previously loaded presets, then appends the defaults.
    // Returns the number of presets loaded (always >= 1 due to fallback).
    int loadFromDirectory(const std::wstring& presetsDirectory);

    // Return all loaded presets in load order.
    const std::vector<aura::app::ThemeConfig>& getPresets() const;

    // Find a preset by themeName (case-insensitive). Returns nullptr if not found.
    const aura::app::ThemeConfig* findByName(const std::wstring& name) const;

    // Number of loaded presets.
    int count() const;

    bool isLoaded() const;

private:
    ThemePresetLoader();
    void addDefaults();  // Ensures at least "Windows Blue" is always present.

    std::vector<aura::app::ThemeConfig> m_presets;
    bool m_loaded{false};
};

} // namespace aura::context
