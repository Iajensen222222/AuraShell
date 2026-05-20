#include "theme_applier.h"

#include "logging/logger.h"

namespace {

// Registry paths for Windows 11 personalization settings (all HKCU).
constexpr wchar_t kAccentPath[]  =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";
constexpr wchar_t kPersonPath[]  =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

DWORD readDword(HKEY hRoot, const wchar_t* path, const wchar_t* name,
                DWORD fallback) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(hRoot, path, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return fallback;
    DWORD val = 0, size = sizeof(val), type = REG_DWORD;
    LONG res = RegQueryValueExW(hKey, name, nullptr, &type,
                                reinterpret_cast<BYTE*>(&val), &size);
    RegCloseKey(hKey);
    return (res == ERROR_SUCCESS) ? val : fallback;
}

bool writeDword(HKEY hRoot, const wchar_t* path, const wchar_t* name, DWORD val) {
    HKEY hKey = nullptr;
    LONG res = RegCreateKeyExW(hRoot, path, 0, nullptr, 0, KEY_WRITE,
                               nullptr, &hKey, nullptr);
    if (res != ERROR_SUCCESS) return false;
    res = RegSetValueExW(hKey, name, 0, REG_DWORD,
                         reinterpret_cast<const BYTE*>(&val), sizeof(val));
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

} // anonymous namespace

namespace aura::context {

ThemeApplier& ThemeApplier::getInstance() {
    static ThemeApplier instance;
    return instance;
}

void ThemeApplier::apply(const aura::app::ThemeConfig& theme) {
    std::lock_guard<std::mutex> lk(m_mutex);
    backupIfNeeded();

    writeAccentColor(theme.accentColor);

    // showOnHover == false implies a minimal/dark appearance → dark mode.
    writeDarkMode(!theme.showOnHover);

    // glowEnabled maps to transparency (glow needs compositor transparency).
    writeTransparency(theme.glowEnabled);

    broadcastSettingChange();

    int const _needed = WideCharToMultiByte(CP_UTF8, 0,
        theme.themeName.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string _narrow(static_cast<size_t>(_needed > 0 ? _needed - 1 : 0), '\0');
    if (_needed > 0)
        WideCharToMultiByte(CP_UTF8, 0, theme.themeName.c_str(), -1,
                            &_narrow[0], _needed, nullptr, nullptr);
    aura::logging::Logger::getInstance().info("theme",
        "ThemeApplier: applied theme '" + _narrow + "'");
}

void ThemeApplier::restoreDefaults() {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_hasBackup) return;

    for (auto& e : m_backup) {
        writeDword(HKEY_CURRENT_USER, e.path.c_str(), e.name.c_str(), e.value);
    }
    m_backup.clear();
    m_hasBackup = false;
    broadcastSettingChange();
    aura::logging::Logger::getInstance().info("theme", "ThemeApplier: defaults restored");
}

bool ThemeApplier::hasBackup() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_hasBackup;
}

// ============================================================================
// Private helpers
// ============================================================================

void ThemeApplier::backupIfNeeded() {
    if (m_hasBackup) return;

    auto save = [&](const wchar_t* path, const wchar_t* name) {
        DWORD val = readDword(HKEY_CURRENT_USER, path, name, 0);
        m_backup.push_back({ path, name, val });
    };

    save(kAccentPath,  L"AccentColorMenu");
    save(kAccentPath,  L"StartColorMenu");
    save(kPersonPath, L"AppsUseLightTheme");
    save(kPersonPath, L"SystemUsesLightTheme");
    save(kPersonPath, L"EnableTransparency");
    m_hasBackup = true;
}

void ThemeApplier::writeAccentColor(const aura::app::AuraColor& c) {
    // Windows stores accent color as ABGR (little-endian).
    DWORD abgr = (static_cast<DWORD>(c.a) << 24) |
                 (static_cast<DWORD>(c.b) << 16) |
                 (static_cast<DWORD>(c.g) <<  8) |
                 (static_cast<DWORD>(c.r)       );
    writeDword(HKEY_CURRENT_USER, kAccentPath, L"AccentColorMenu", abgr);
    writeDword(HKEY_CURRENT_USER, kAccentPath, L"StartColorMenu",  abgr);
}

void ThemeApplier::writeDarkMode(bool dark) {
    DWORD val = dark ? 0u : 1u; // 0 = dark, 1 = light
    writeDword(HKEY_CURRENT_USER, kPersonPath, L"AppsUseLightTheme",   val);
    writeDword(HKEY_CURRENT_USER, kPersonPath, L"SystemUsesLightTheme", val);
}

void ThemeApplier::writeTransparency(bool enabled) {
    writeDword(HKEY_CURRENT_USER, kPersonPath, L"EnableTransparency",
               enabled ? 1u : 0u);
}

void ThemeApplier::broadcastSettingChange() {
    // Notify all top-level windows that immersive color settings changed.
    // The shell reads this to update the taskbar accent color live.
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"ImmersiveColorSet"),
                        SMTO_ABORTIFHUNG, 500, &result);
}

} // namespace aura::context
