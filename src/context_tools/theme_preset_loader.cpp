#include "theme_preset_loader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "logging/logger.h"

namespace aura::context {

ThemePresetLoader& ThemePresetLoader::getInstance() {
    static ThemePresetLoader instance;
    return instance;
}

ThemePresetLoader::ThemePresetLoader() {
    addDefaults();
}

int ThemePresetLoader::loadFromDirectory(const std::wstring& presetsDirectory) {
    m_presets.clear();

    std::filesystem::path dir(presetsDirectory);
    int loaded = 0;

    if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir)) {
        for (auto const& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.path().extension() != L".json") continue;

            std::ifstream f(entry.path());
            if (!f) continue;

            try {
                nlohmann::json j;
                f >> j;

                aura::app::ThemeConfig cfg;
                if (j.contains("themeName"))
                    cfg.themeName = std::wstring(j["themeName"].get<std::string>().begin(),
                                                 j["themeName"].get<std::string>().end());
                if (j.contains("animSpeedPct"))
                    cfg.animSpeedPct = j["animSpeedPct"].get<uint32_t>();
                if (j.contains("showOnHover"))
                    cfg.showOnHover = j["showOnHover"].get<bool>();
                if (j.contains("showOnLaunch"))
                    cfg.showOnLaunch = j["showOnLaunch"].get<bool>();
                if (j.contains("glowEnabled"))
                    cfg.glowEnabled = j["glowEnabled"].get<bool>();
                if (j.contains("accentColor")) {
                    auto& ac = j["accentColor"];
                    cfg.accentColor.r = static_cast<uint8_t>(ac.value("r", 0));
                    cfg.accentColor.g = static_cast<uint8_t>(ac.value("g", 120));
                    cfg.accentColor.b = static_cast<uint8_t>(ac.value("b", 212));
                    cfg.accentColor.a = static_cast<uint8_t>(ac.value("a", 255));
                }

                m_presets.push_back(cfg);
                ++loaded;
            } catch (std::exception const& e) {
                aura::logging::Logger::getInstance().warn("preset",
                    std::string("Skipped ") + entry.path().filename().string()
                    + ": " + e.what());
            }
        }

        // Sort alphabetically by theme name for a predictable UI order.
        std::sort(m_presets.begin(), m_presets.end(),
            [](auto const& a, auto const& b) { return a.themeName < b.themeName; });
    } else {
        aura::logging::Logger::getInstance().warn("preset",
            "Presets directory not found — using built-in defaults");
    }

    if (m_presets.empty()) addDefaults();

    m_loaded = true;
    aura::logging::Logger::getInstance().info("preset",
        "Loaded " + std::to_string(m_presets.size()) + " theme preset(s)");
    return static_cast<int>(m_presets.size());
}

const std::vector<aura::app::ThemeConfig>& ThemePresetLoader::getPresets() const {
    return m_presets;
}

const aura::app::ThemeConfig* ThemePresetLoader::findByName(const std::wstring& name) const {
    for (auto const& p : m_presets) {
        // Case-insensitive compare via lowercase transformation.
        auto lower = [](std::wstring s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](wchar_t c) { return std::towlower(c); });
            return s;
        };
        if (lower(p.themeName) == lower(name)) return &p;
    }
    return nullptr;
}

int ThemePresetLoader::count() const {
    return static_cast<int>(m_presets.size());
}

bool ThemePresetLoader::isLoaded() const { return m_loaded; }

void ThemePresetLoader::addDefaults() {
    // "Windows Blue" — always present as the canonical default.
    aura::app::ThemeConfig def;
    def.themeName    = L"Windows Blue";
    def.accentColor  = { 0, 120, 212, 255 };
    def.animSpeedPct = 100;
    def.showOnHover  = true;
    def.showOnLaunch = true;
    def.glowEnabled  = true;
    m_presets.push_back(def);
}

} // namespace aura::context
