#pragma once

// shell_icon_modifier.h — AuraShell Phase 10.9
//
// ShellIconModifier: pure-static COM utility for customising desktop asset icons.
//
// Shortcut (.lnk) icons:
//   Uses CoCreateInstance(CLSID_ShellLink) → IShellLinkW + IPersistFile.
//   All COM interfaces held in ComPtr<> — no manual AddRef/Release.
//
// Folder icons:
//   Writes [.ShellClassInfo]\nIconResource= to desktop.ini inside the folder.
//   Applies SYSTEM attribute to desktop.ini and READONLY to the folder so
//   Windows Explorer respects the custom icon.
//
// Shell notification:
//   Every successful mutation calls SHChangeNotify to trigger an immediate
//   shell icon cache refresh.
//
// Path utilities are pure string operations — no OS calls, fully testable.

#include <Windows.h>
#include <cstdint>
#include <string>

namespace aura::shell {

class ShellIconModifier {
public:
    // ---- Result codes -------------------------------------------------------

    enum class Result : uint8_t {
        Success,
        FileNotFound,   // path does not exist on disk
        InvalidPath,    // empty, wrong extension, or malformed path
        ReadOnly,       // file/folder is write-protected
        AccessDenied,   // OS refused the write (permissions)
        ComError,       // IShellLink / IPersistFile / desktop.ini write failed
    };

    // ---- Shortcut (.lnk) icon -----------------------------------------------

    // Sets the icon for a .lnk file.
    // Prerequisites: lnkPath exists, ends in ".lnk" (case-insensitive), is writable.
    [[nodiscard]] static Result setShortcutIcon(
        std::wstring const& lnkPath,
        std::wstring const& iconPath,
        int                 iconIndex = 0
    ) noexcept;

    // Reads the current icon from a .lnk file.
    [[nodiscard]] static Result getShortcutIcon(
        std::wstring const& lnkPath,
        std::wstring&       outIconPath,
        int&                outIconIndex
    ) noexcept;

    // ---- Folder icon (desktop.ini) ------------------------------------------

    // Applies a custom folder icon via desktop.ini.
    // Creates or overwrites desktop.ini; sets SYSTEM attribute on it;
    // sets READONLY on the folder so Explorer honours the setting.
    [[nodiscard]] static Result setFolderIcon(
        std::wstring const& folderPath,
        std::wstring const& iconPath,
        int                 iconIndex = 0
    ) noexcept;

    // Removes the custom folder icon by deleting the IconResource line from
    // desktop.ini and removing the READONLY flag from the folder.
    [[nodiscard]] static Result resetFolderIcon(
        std::wstring const& folderPath
    ) noexcept;

    // ---- Shell notification --------------------------------------------------

    // Sends SHCNE_UPDATEITEM for `path` and SHCNE_ASSOCCHANGED globally,
    // forcing Explorer to refresh its icon cache immediately.
    static void notifyShellChange(std::wstring const& path) noexcept;

    // ---- Path utilities (pure string math — no OS calls, headless-testable) --

    // Strip surrounding whitespace and double-quotes; replace '/' with '\'.
    [[nodiscard]] static std::wstring sanitizePath(
        std::wstring const& path
    ) noexcept;

    // Returns true if `path` ends with ".lnk" (case-insensitive).
    [[nodiscard]] static bool isShortcut(std::wstring const& path) noexcept;

    // Returns true if `path` names an existing directory (calls GetFileAttributesW).
    [[nodiscard]] static bool isDirectory(std::wstring const& path) noexcept;

    // Returns true if `path` is a regular file that exists on disk.
    [[nodiscard]] static bool isFile(std::wstring const& path) noexcept;

private:
    ShellIconModifier() = delete;  // all methods are static — no instances
};

}  // namespace aura::shell
