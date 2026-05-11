#pragma once

#include <mutex>
#include <string>

#include "theme_model.h"

namespace aura::app {

class SettingsManager {
public:
    static SettingsManager& getInstance();

    // Loads from %LOCALAPPDATA%\AuraShell\config.json.
    // Returns true even if the file is missing; defaults are used in that case.
    [[nodiscard]] bool load();

    // Loads from an explicit path (used by tests and load()).
    [[nodiscard]] bool loadFrom(std::wstring const& path);

    // Saves to %LOCALAPPDATA%\AuraShell\config.json. Creates the directory if needed.
    [[nodiscard]] bool save() const;

    // Saves to an explicit path (used by tests and save()).
    [[nodiscard]] bool saveTo(std::wstring const& path) const;

    [[nodiscard]] AppConfig const& getConfig() const noexcept;
    void setConfig(AppConfig config);
    void setTheme(ThemeConfig theme);

    [[nodiscard]] std::wstring getConfigPath() const;

private:
    SettingsManager() = default;
    ~SettingsManager() = default;

    SettingsManager(SettingsManager const&)            = delete;
    SettingsManager& operator=(SettingsManager const&) = delete;

    mutable std::mutex m_mutex;
    AppConfig          m_config;
};

}  // namespace aura::app
