# AuraShell Phase 10.9: Desktop Asset Icon Customization

**Date**: 2026-05-13  
**Status**: Phase 10.9 ✅ Code Complete — Pending build

---

## Executive Summary

- **`ShellIconModifier`** (`src/app/shell_icon_modifier.h/.cpp`) — pure static COM utility class. Uses `IShellLinkW`+`IPersistFile` for shortcut icons; writes `desktop.ini` with `[.ShellClassInfo]\nIconResource=` for folder icons. All COM objects wrapped in `Microsoft::WRL::ComPtr<>`. Calls `SHChangeNotify` after every mutation.
- **`DesktopItemsPage`** (`src/app/page_desktop_items.h/.cpp`) — 5th navigation panel. Three-card layout: Drop Zone (drag-and-drop `WM_DROPFILES`), Current Item preview, and Icon Selection + Apply.
- **Navigation extended**: `Page::DesktopItems = 4` inserted before `Page::Count`. All hit-test geometry and spring animations self-correct — no other navigation code changes needed.

---

## ShellIconModifier API

```
ShellIconModifier::setShortcutIcon(lnkPath, iconPath, iconIndex)
  → validate path (non-empty, ends .lnk)
  → CoCreateInstance(CLSID_ShellLink, IID_IShellLinkW)  ← ComPtr<IShellLinkW>
  → QueryInterface(IID_IPersistFile)                     ← ComPtr<IPersistFile>
  → Load(lnkPath, STGM_READWRITE)
  → SetIconLocation(iconPath, iconIndex)
  → Save(nullptr, TRUE)
  → notifyShellChange(lnkPath)

ShellIconModifier::setFolderIcon(folderPath, iconPath, iconIndex)
  → validate path (non-empty, exists as directory)
  → write desktop.ini: [.ShellClassInfo]\nIconResource=<path,index>
  → SetFileAttributesW(desktop.ini, FILE_ATTRIBUTE_SYSTEM)
  → SetFileAttributesW(folder, current | FILE_ATTRIBUTE_READONLY)
  → notifyShellChange(folderPath)

ShellIconModifier::sanitizePath(path)   ← pure string math, no OS calls
  → strip leading/trailing whitespace
  → strip leading/trailing double-quotes
  → replace '/' with '\'
```

---

## DesktopItemsPage Layout (content area 1060 × 800px)

```
y=16..72    Page header "Desktop Items" + subtitle

y=88..316   CARD 1: Drop Zone (1012×228px)
              Dashed-border D2D canvas in the centre
              Label: "Drag a .lnk shortcut or folder here"
              After drop: shows filename + type badge

y=332..464  CARD 2: Current Item (1012×132px)
              Path display (truncated)
              Item type: "Shortcut" / "Folder" badge
              Current icon path (if readable from .lnk)

y=480..628  CARD 3: Icon Selection (1012×148px)
              [Browse…] button → PickIconDlg (shell32 export)
              Selected: "<icon path>, index N"
              [Apply] button   → ShellIconModifier::set*Icon()
              [Restore Default] → ShellIconModifier::reset*()
```

---

## COM RAII Contract

| Interface | Wrapper | Released by |
|-----------|---------|-------------|
| `IShellLinkW` | `ComPtr<IShellLinkW>` | ComPtr destructor at scope exit |
| `IPersistFile` | `ComPtr<IPersistFile>` | ComPtr destructor at scope exit |

`CoInitializeEx` / `CoUninitialize` are called in `WinMain` (already via `ShellExecuteExW` in BehaviorPage). No per-call COM initialization needed.

---

## TDD: test_shell_customization.cpp (11 headless tests)

| # | Test | Tag |
|---|------|-----|
| 1 | `sanitizePath("")` == `""` | `[shell][path]` |
| 2 | `sanitizePath` strips leading quote | `[shell][path]` |
| 3 | `sanitizePath` strips trailing quote | `[shell][path]` |
| 4 | `sanitizePath` strips both quotes | `[shell][path]` |
| 5 | `sanitizePath` replaces `/` with `\` | `[shell][path]` |
| 6 | `sanitizePath` trims whitespace | `[shell][path]` |
| 7 | `isShortcut` returns true for `.lnk` extension (case-insensitive) | `[shell][path]` |
| 8 | `isShortcut` returns false for `.exe` extension | `[shell][path]` |
| 9 | `setShortcutIcon` with empty lnkPath → `Result::InvalidPath` | `[shell][result]` |
| 10 | `setShortcutIcon` with non-.lnk extension → `Result::InvalidPath` | `[shell][result]` |
| 11 | `setFolderIcon` with empty folderPath → `Result::InvalidPath` | `[shell][result]` |

Tests 9–11 trigger input validation BEFORE any COM call — fully headless.

---

## Files Created / Modified

| File | Action |
|------|--------|
| `src/app/shell_icon_modifier.h` | Created |
| `src/app/shell_icon_modifier.cpp` | Created |
| `src/app/page_desktop_items.h` | Created |
| `src/app/page_desktop_items.cpp` | Created |
| `src/app/navigation_manager.h` | Modified — add `DesktopItems = 4` to Page enum, update NAV_INIT |
| `src/app/config_window.h` | Modified — add `DesktopItemsPage m_desktopItemsPage` |
| `src/app/config_window.cpp` | Modified — PAGE_LABELS, registerPageContent, page-changed callback |
| `src/app/CMakeLists.txt` | Modified — add 2 new cpp files |
| `tests/unit/test_shell_customization.cpp` | Created |
| `tests/CMakeLists.txt` | Modified — add test file |

---

## Memory Contract

- `ShellIconModifier`: no instance state, no heap — pure static. All COM objects stack/ComPtr lifetime.
- `DesktopItemsPage`: adds `std::wstring m_itemPath` and `std::wstring m_selectedIconPath` (~200 bytes). No additional D2D factory (uses shared factory from ConfigWindow).

---

## Resumption Notes

- **Stopped at**: Phase 10.9 complete
- **Next action**: Final smoke test + Phase 9 installer update for packaging
- **Known blockers**: `CoInitialize` must be called before `setShortcutIcon`/`setFolderIcon`. The app's `WinMain` currently calls `ShellExecuteExW` (via BehaviorPage) which implicitly requires COM apartment. Add explicit `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` to `main_app.cpp`.
- **Watch for**: `PickIconDlg` is an undocumented/legacy export from `shell32.dll`. The documented alternative is `SHOpenWithDialog` + `IFileOpenDialog`. For Phase 10.9, use `PickIconDlg` via ordinal or `GetProcAddress`; or substitute with a simple file open dialog for `*.ico` / `*.exe` files.

---

*Last Updated: 2026-05-13*
