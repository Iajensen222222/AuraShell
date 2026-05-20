#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "taskbar_controller.h"  // TaskbarIconInfo

namespace aura::taskbar {

// Manages a user-defined catalog of exe-path → custom-.ico mappings,
// persisted to %LOCALAPPDATA%\AuraShell\icon_overrides.json.
//
// The overlay layer (IconOverlayManager) handles per-frame visual effects;
// IconReplacer handles durable, per-application icon substitutions that
// survive reboots. No DLL injection, no explorer.exe modification — safe
// by design (CLAUDE.md constraint).
//
// Integration: call loadOverrides() on startup, applyAll() after
// TaskbarController::refreshIconCache().
class IconReplacer {
public:
    static IconReplacer& getInstance();

    // Load persisted overrides from disk. Call once on startup.
    bool loadOverrides();

    // Persist current overrides to disk.
    bool saveOverrides();

    // Associate exePath with a custom .ico file.
    // Persists immediately (calls saveOverrides).
    bool setIconOverride(const std::wstring& exePath,
                         const std::wstring& iconPath);

    // Remove override for exePath; persists immediately.
    bool clearIconOverride(const std::wstring& exePath);

    // Return custom icon path for exePath, or empty wstring if no override.
    std::wstring getOverrideIcon(const std::wstring& exePath) const;

    // True if at least one override is registered for exePath.
    bool hasOverride(const std::wstring& exePath) const;

    // Return all current overrides (copy).
    std::map<std::wstring, std::wstring> getAllOverrides() const;

    // Apply all stored overrides to the given icon set.
    // For each icon whose exe matches an override, the replacement path is
    // stored so the rendering layer can draw the substitute icon.
    // This does NOT write to the registry or modify any system files —
    // it only updates in-memory state for the overlay renderer.
    void applyAll(const std::vector<TaskbarIconInfo>& icons);

private:
    IconReplacer() = default;
    ~IconReplacer() = default;
    IconReplacer(const IconReplacer&) = delete;
    IconReplacer& operator=(const IconReplacer&) = delete;

    std::wstring overridesPath() const; // %LOCALAPPDATA%\AuraShell\icon_overrides.json

    mutable std::mutex                         m_mutex;
    std::map<std::wstring, std::wstring>        m_overrides; // exePath → iconPath
};

} // namespace aura::taskbar
