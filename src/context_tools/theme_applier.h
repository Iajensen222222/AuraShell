#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

#include "theme_model.h"

namespace aura::context {

// Applies a ThemeConfig to Windows system settings via HKCU registry.
// All writes are HKCU — no elevation required. Before each apply the current
// registry values are backed up so restoreDefaults() can undo every change.
//
// Broadcasts WM_SETTINGCHANGE after writing so the shell picks up the new
// accent color without requiring a logoff/reboot.
class ThemeApplier {
public:
    static ThemeApplier& getInstance();

    // Apply a theme: write registry values and broadcast WM_SETTINGCHANGE.
    void apply(const aura::app::ThemeConfig& theme);

    // Undo all previous applies by restoring backed-up registry values.
    void restoreDefaults();

    bool hasBackup() const;

private:
    ThemeApplier() = default;
    ThemeApplier(const ThemeApplier&) = delete;
    ThemeApplier& operator=(const ThemeApplier&) = delete;

    void backupIfNeeded();
    void writeAccentColor(const aura::app::AuraColor& c);
    void writeDarkMode(bool dark);
    void writeTransparency(bool enabled);
    void broadcastSettingChange();

    // Registry backup: key path → map of value name → DWORD
    struct RegEntry { std::wstring path; std::wstring name; DWORD value; };
    std::vector<RegEntry> m_backup;

    mutable std::mutex m_mutex;
    bool m_hasBackup{false};
};

} // namespace aura::context
