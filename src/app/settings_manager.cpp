#include "settings_manager.h"

#include <Windows.h>
#include <shlobj.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "logging/logger.h"

#pragma comment(lib, "shell32.lib")

namespace aura::app {

using json = nlohmann::json;

// ============================================================================
// Encoding helpers
// ============================================================================

static std::string wstrToUtf8(std::wstring const& wstr) {
    if (wstr.empty()) return {};
    int const needed = WideCharToMultiByte(
        CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr
    );
    std::string out(static_cast<size_t>(needed > 0 ? needed - 1 : 0), '\0');
    if (needed > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1,
                            &out[0], needed, nullptr, nullptr);
    }
    return out;
}

static std::wstring utf8ToWstr(std::string const& str) {
    if (str.empty()) return {};
    int const needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(needed > 0 ? needed - 1 : 0), L'\0');
    if (needed > 0) {
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &out[0], needed);
    }
    return out;
}

// ============================================================================
// Singleton
// ============================================================================

SettingsManager& SettingsManager::getInstance() {
    static SettingsManager instance;
    return instance;
}

// ============================================================================
// Path
// ============================================================================

std::wstring SettingsManager::getConfigPath() const {
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData))) {
        return L"config.json";
    }
    return std::wstring(appData) + L"\\AuraShell\\config.json";
}

// ============================================================================
// Load
// ============================================================================

bool SettingsManager::load() {
    return loadFrom(getConfigPath());
}

bool SettingsManager::loadFrom(std::wstring const& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        aura::logging::Logger::getInstance().info(
            "settings", "Config not found at path, using defaults"
        );
        return true;
    }

    try {
        json j = json::parse(file);

        AppConfig cfg;
        cfg.version              = j.value("version", 1);
        cfg.autoStartService     = j.value("autoStartService",   true);
        cfg.lastServiceVersion   = j.value("lastServiceVersion", std::string{});
        cfg.autoStartApp         = j.value("autoStartApp",       false);
        cfg.monitorAutoHide      = j.value("monitorAutoHide",    true);
        cfg.enableMultiMonitor   = j.value("enableMultiMonitor", true);

        if (j.contains("activeTheme")) {
            json const& t = j.at("activeTheme");
            cfg.activeTheme.themeName = utf8ToWstr(t.value("themeName", "default"));

            uint32_t const rawSpeed = static_cast<uint32_t>(t.value("animSpeedPct", 100));
            cfg.activeTheme.animSpeedPct = (std::clamp)(rawSpeed, uint32_t{0}, uint32_t{200});

            cfg.activeTheme.showOnHover  = t.value("showOnHover",  true);
            cfg.activeTheme.showOnLaunch = t.value("showOnLaunch", true);
            cfg.activeTheme.glowEnabled  = t.value("glowEnabled",  true);

            if (t.contains("accentColor")) {
                json const& c = t.at("accentColor");
                cfg.activeTheme.accentColor.r = static_cast<uint8_t>(c.value("r", 0x00));
                cfg.activeTheme.accentColor.g = static_cast<uint8_t>(c.value("g", 0x78));
                cfg.activeTheme.accentColor.b = static_cast<uint8_t>(c.value("b", 0xD4));
                cfg.activeTheme.accentColor.a = static_cast<uint8_t>(c.value("a", 0xFF));
            }
        }

        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_config = std::move(cfg);
        }
        aura::logging::Logger::getInstance().info("settings", "Config loaded successfully");
        return true;

    } catch (json::exception const& ex) {
        aura::logging::Logger::getInstance().warn(
            "settings", std::string("Config parse error (using defaults): ") + ex.what()
        );
        return true;  // corrupt file → keep defaults
    }
}

// ============================================================================
// Save
// ============================================================================

bool SettingsManager::save() const {
    return saveTo(getConfigPath());
}

bool SettingsManager::saveTo(std::wstring const& path) const {
    std::filesystem::path const dir =
        std::filesystem::path(path).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::lock_guard<std::mutex> lk(m_mutex);

    json t;
    t["themeName"]    = wstrToUtf8(m_config.activeTheme.themeName);
    t["animSpeedPct"] = m_config.activeTheme.animSpeedPct;
    t["showOnHover"]  = m_config.activeTheme.showOnHover;
    t["showOnLaunch"] = m_config.activeTheme.showOnLaunch;
    t["glowEnabled"]  = m_config.activeTheme.glowEnabled;
    t["accentColor"]  = {
        {"r", m_config.activeTheme.accentColor.r},
        {"g", m_config.activeTheme.accentColor.g},
        {"b", m_config.activeTheme.accentColor.b},
        {"a", m_config.activeTheme.accentColor.a},
    };

    json j;
    j["version"]             = m_config.version;
    j["autoStartService"]    = m_config.autoStartService;
    j["lastServiceVersion"]  = m_config.lastServiceVersion;
    j["autoStartApp"]        = m_config.autoStartApp;
    j["monitorAutoHide"]     = m_config.monitorAutoHide;
    j["enableMultiMonitor"]  = m_config.enableMultiMonitor;
    j["activeTheme"]         = t;

    std::ofstream file(path);
    if (!file.is_open()) {
        aura::logging::Logger::getInstance().error("settings", "Cannot write config file");
        return false;
    }
    file << j.dump(4);
    return file.good();
}

// ============================================================================
// Accessors
// ============================================================================

AppConfig const& SettingsManager::getConfig() const noexcept {
    return m_config;
}

void SettingsManager::setConfig(AppConfig config) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_config = std::move(config);
}

void SettingsManager::setTheme(ThemeConfig theme) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_config.activeTheme = std::move(theme);
}

}  // namespace aura::app
