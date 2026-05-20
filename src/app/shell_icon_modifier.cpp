// shell_icon_modifier.cpp — AuraShell Phase 10.9

#include "shell_icon_modifier.h"

#include <Windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <wrl.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdio>
#include <cwchar>
#include <string>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

namespace aura::shell {

using Microsoft::WRL::ComPtr;

// ============================================================================
// Internal helpers
// ============================================================================

namespace {

// Returns the DWORD file attributes, or INVALID_FILE_ATTRIBUTES on error.
DWORD fileAttributes(std::wstring const& path) noexcept {
    return GetFileAttributesW(path.c_str());
}

bool exists(std::wstring const& path) noexcept {
    return fileAttributes(path) != INVALID_FILE_ATTRIBUTES;
}

bool isReadOnly(std::wstring const& path) noexcept {
    DWORD const attr = fileAttributes(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    return (attr & FILE_ATTRIBUTE_READONLY) != 0;
}

// Compose the desktop.ini path for a folder.
std::wstring desktopIniPath(std::wstring const& folderPath) noexcept {
    return folderPath + L"\\desktop.ini";
}

}  // anonymous namespace

// ============================================================================
// Path utilities — pure string operations, headless-testable
// ============================================================================

std::wstring ShellIconModifier::sanitizePath(std::wstring const& path) noexcept {
    std::wstring s = path;

    // Trim leading whitespace
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](wchar_t c) {
        return !::iswspace(static_cast<wint_t>(c));
    }));
    // Trim trailing whitespace
    s.erase(std::find_if(s.rbegin(), s.rend(), [](wchar_t c) {
        return !::iswspace(static_cast<wint_t>(c));
    }).base(), s.end());

    // Strip surrounding double-quotes
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
        s = s.substr(1, s.size() - 2);
    }

    // Normalise forward slashes to backslashes
    std::replace(s.begin(), s.end(), L'/', L'\\');

    return s;
}

bool ShellIconModifier::isShortcut(std::wstring const& path) noexcept {
    if (path.size() < 4) return false;

    // Compare last 4 characters case-insensitively against ".lnk"
    std::wstring const ext = path.substr(path.size() - 4);
    std::wstring lower;
    lower.reserve(4);
    for (wchar_t c : ext) lower += static_cast<wchar_t>(::towlower(c));
    return lower == L".lnk";
}

bool ShellIconModifier::isDirectory(std::wstring const& path) noexcept {
    DWORD const attr = fileAttributes(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ShellIconModifier::isFile(std::wstring const& path) noexcept {
    DWORD const attr = fileAttributes(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// ============================================================================
// Shortcut (.lnk) icon
// ============================================================================

ShellIconModifier::Result ShellIconModifier::setShortcutIcon(
    std::wstring const& lnkPath,
    std::wstring const& iconPath,
    int          const  iconIndex
) noexcept {
    // Input validation (no COM needed).
    if (lnkPath.empty() || iconPath.empty()) return Result::InvalidPath;
    if (!isShortcut(lnkPath))               return Result::InvalidPath;
    if (!exists(lnkPath))                   return Result::FileNotFound;
    if (isReadOnly(lnkPath))                return Result::ReadOnly;

    // Create IShellLinkW.
    ComPtr<IShellLinkW> pLink;
    HRESULT hr = CoCreateInstance(
        CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pLink)
    );
    if (FAILED(hr)) return Result::ComError;

    // Load the existing .lnk for reading and writing.
    ComPtr<IPersistFile> pFile;
    hr = pLink.As(&pFile);
    if (FAILED(hr)) return Result::ComError;

    hr = pFile->Load(lnkPath.c_str(), STGM_READWRITE);
    if (FAILED(hr)) return (hr == STG_E_ACCESSDENIED) ? Result::AccessDenied
                                                       : Result::ComError;

    // Apply the new icon.
    hr = pLink->SetIconLocation(iconPath.c_str(), iconIndex);
    if (FAILED(hr)) return Result::ComError;

    // Save back to the same file.
    hr = pFile->Save(nullptr, TRUE);
    if (FAILED(hr)) return (hr == STG_E_ACCESSDENIED) ? Result::AccessDenied
                                                       : Result::ComError;

    notifyShellChange(lnkPath);
    return Result::Success;
}

ShellIconModifier::Result ShellIconModifier::getShortcutIcon(
    std::wstring const& lnkPath,
    std::wstring&       outIconPath,
    int&                outIconIndex
) noexcept {
    if (lnkPath.empty() || !isShortcut(lnkPath)) return Result::InvalidPath;
    if (!exists(lnkPath))                         return Result::FileNotFound;

    ComPtr<IShellLinkW> pLink;
    HRESULT hr = CoCreateInstance(
        CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pLink)
    );
    if (FAILED(hr)) return Result::ComError;

    ComPtr<IPersistFile> pFile;
    hr = pLink.As(&pFile);
    if (FAILED(hr)) return Result::ComError;

    hr = pFile->Load(lnkPath.c_str(), STGM_READ);
    if (FAILED(hr)) return Result::ComError;

    wchar_t iconBuf[MAX_PATH] = {};
    int     iconIdx            = 0;
    hr = pLink->GetIconLocation(iconBuf, MAX_PATH, &iconIdx);
    if (FAILED(hr)) return Result::ComError;

    outIconPath  = iconBuf;
    outIconIndex = iconIdx;
    return Result::Success;
}

// ============================================================================
// Folder icon (desktop.ini)
// ============================================================================

ShellIconModifier::Result ShellIconModifier::setFolderIcon(
    std::wstring const& folderPath,
    std::wstring const& iconPath,
    int          const  iconIndex
) noexcept {
    if (folderPath.empty() || iconPath.empty()) return Result::InvalidPath;
    if (!isDirectory(folderPath))               return Result::FileNotFound;

    std::wstring const iniPath = desktopIniPath(folderPath);

    // Temporarily remove READONLY from folder so we can write inside it.
    DWORD const oldFolderAttr = GetFileAttributesW(folderPath.c_str());
    if (oldFolderAttr != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(folderPath.c_str(),
                           oldFolderAttr & ~FILE_ATTRIBUTE_READONLY);
    }

    // Write desktop.ini.
    {
        wchar_t content[MAX_PATH + 64];
        std::swprintf(content, MAX_PATH + 64,
            L"[.ShellClassInfo]\r\nIconResource=%s,%d\r\n",
            iconPath.c_str(), iconIndex
        );

        HANDLE const hFile = CreateFileW(
            iniPath.c_str(),
            GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
        );
        if (hFile == INVALID_HANDLE_VALUE) {
            // Restore folder attribute before returning.
            if (oldFolderAttr != INVALID_FILE_ATTRIBUTES) {
                SetFileAttributesW(folderPath.c_str(), oldFolderAttr);
            }
            DWORD const err = GetLastError();
            return (err == ERROR_ACCESS_DENIED) ? Result::AccessDenied
                                                : Result::ComError;
        }

        // Write UTF-16 LE with BOM — WritePrivateProfileString writes ANSI;
        // for full Unicode support we write directly.
        static constexpr BYTE BOM[2] = {0xFF, 0xFE};
        DWORD written = 0;
        WriteFile(hFile, BOM, 2, &written, nullptr);
        WriteFile(hFile, content,
                  static_cast<DWORD>(wcslen(content) * sizeof(wchar_t)),
                  &written, nullptr);
        CloseHandle(hFile);
    }

    // Set SYSTEM attribute on desktop.ini so Explorer uses it.
    SetFileAttributesW(iniPath.c_str(),
                       FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);

    // Set READONLY on the folder so Explorer shows the custom icon.
    if (oldFolderAttr != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(folderPath.c_str(),
                           oldFolderAttr | FILE_ATTRIBUTE_READONLY);
    }

    notifyShellChange(folderPath);
    return Result::Success;
}

ShellIconModifier::Result ShellIconModifier::resetFolderIcon(
    std::wstring const& folderPath
) noexcept {
    if (folderPath.empty()) return Result::InvalidPath;
    if (!isDirectory(folderPath)) return Result::FileNotFound;

    std::wstring const iniPath = desktopIniPath(folderPath);

    // Remove READONLY from folder.
    DWORD const attr = GetFileAttributesW(folderPath.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(folderPath.c_str(),
                           attr & ~FILE_ATTRIBUTE_READONLY);
    }

    // Delete or clear the desktop.ini file.
    if (exists(iniPath)) {
        SetFileAttributesW(iniPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(iniPath.c_str());
    }

    notifyShellChange(folderPath);
    return Result::Success;
}

// ============================================================================
// Shell notification
// ============================================================================

void ShellIconModifier::notifyShellChange(std::wstring const& path) noexcept {
    // Notify for the specific item first.
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH | SHCNF_FLUSH,
                   path.c_str(), nullptr);

    // Broadcast a global association change so Explorer refreshes thumbnails.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

}  // namespace aura::shell
