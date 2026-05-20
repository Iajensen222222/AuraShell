#include "icon_replacer.h"

#include <filesystem>
#include <fstream>
#include <shlobj.h>

#include <nlohmann/json.hpp>

#include "logging/logger.h"

#pragma comment(lib, "shell32.lib")

namespace {

// Narrow UTF-16 path → UTF-8 for nlohmann/json string keys.
std::string wstrToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring utf8ToWstr(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

} // anonymous namespace

namespace aura::taskbar {

IconReplacer& IconReplacer::getInstance() {
    static IconReplacer instance;
    return instance;
}

std::wstring IconReplacer::overridesPath() const {
    wchar_t appData[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
    std::filesystem::path p(appData);
    p /= L"AuraShell";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    p /= L"icon_overrides.json";
    return p.wstring();
}

bool IconReplacer::loadOverrides() {
    std::wstring path = overridesPath();
    std::ifstream f(path);
    if (!f) return true; // not found = no overrides, not an error

    try {
        nlohmann::json j;
        f >> j;
        std::lock_guard<std::mutex> lk(m_mutex);
        m_overrides.clear();
        for (auto& [key, val] : j.items()) {
            m_overrides[utf8ToWstr(key)] = utf8ToWstr(val.get<std::string>());
        }
        aura::logging::Logger::getInstance().info("icon_replacer",
            "Loaded " + std::to_string(m_overrides.size()) + " icon override(s)");
        return true;
    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().warn("icon_replacer",
            std::string("Failed to parse icon_overrides.json: ") + e.what());
        return false;
    }
}

bool IconReplacer::saveOverrides() {
    nlohmann::json j;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& [exe, icon] : m_overrides)
            j[wstrToUtf8(exe)] = wstrToUtf8(icon);
    }

    try {
        std::wstring path = overridesPath();
        std::ofstream f(path);
        if (!f) {
            aura::logging::Logger::getInstance().error("icon_replacer",
                "Cannot write icon_overrides.json");
            return false;
        }
        f << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        aura::logging::Logger::getInstance().error("icon_replacer",
            std::string("Failed to save icon overrides: ") + e.what());
        return false;
    }
}

bool IconReplacer::setIconOverride(const std::wstring& exePath,
                                    const std::wstring& iconPath) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_overrides[exePath] = iconPath;
    }
    aura::logging::Logger::getInstance().info("icon_replacer",
        "Override set: " + wstrToUtf8(exePath) + " → " + wstrToUtf8(iconPath));
    return saveOverrides();
}

bool IconReplacer::clearIconOverride(const std::wstring& exePath) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_overrides.find(exePath);
        if (it == m_overrides.end()) return true; // nothing to remove
        m_overrides.erase(it);
    }
    aura::logging::Logger::getInstance().info("icon_replacer",
        "Override cleared: " + wstrToUtf8(exePath));
    return saveOverrides();
}

std::wstring IconReplacer::getOverrideIcon(const std::wstring& exePath) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_overrides.find(exePath);
    return (it != m_overrides.end()) ? it->second : std::wstring{};
}

bool IconReplacer::hasOverride(const std::wstring& exePath) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_overrides.count(exePath) > 0;
}

std::map<std::wstring, std::wstring> IconReplacer::getAllOverrides() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_overrides;
}

void IconReplacer::applyAll(const std::vector<TaskbarIconInfo>& icons) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_overrides.empty()) return;

    for (const auto& icon : icons) {
        auto it = m_overrides.find(icon.appName);
        if (it == m_overrides.end()) continue;

        aura::logging::Logger::getInstance().debug("icon_replacer",
            "Would apply override for " + wstrToUtf8(icon.appName) +
            " → " + wstrToUtf8(it->second));
        // Future: pass override path to IconOverlayManager for custom rendering.
        // The overlay renderer currently draws glow rings; the next iteration
        // will draw the substitute .ico image from it->second.
    }
}

} // namespace aura::taskbar
