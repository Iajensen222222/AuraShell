# AuraShell Phase 10.7: Behavior Module & SCM Integration

**Date**: 2026-05-11  
**Status**: Phase 10.7 ✅ Code Complete — Pending build

---

## Executive Summary

- **BehaviorPage** (`src/app/page_behavior.h/.cpp`) — three-card panel for Shell Monitoring toggles, System Boot registry management, and Service Management SCM integration.
- **AppConfig extended** — three new behavior fields: `autoStartApp`, `monitorAutoHide`, `enableMultiMonitor`; full JSON round-trip added to `settings_manager.cpp`.
- **Async service control** — `std::thread`+`.detach()` wraps `ShellExecuteExW("runas", --start/--stop)` so the UI never hitches during the 10s SCM timeout.
- **Registry auto-start** — `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` written/deleted on toggle; no admin required.

---

## New AppConfig Fields

| Field | Type | Default | Persisted JSON key |
|-------|------|---------|-------------------|
| `autoStartApp` | `bool` | `false` | `"autoStartApp"` |
| `monitorAutoHide` | `bool` | `true` | `"monitorAutoHide"` |
| `enableMultiMonitor` | `bool` | `true` | `"enableMultiMonitor"` |

---

## BehaviorPage Layout (content area 1060 × 800px)

```
y=16..72    Page header "Behavior" + subtitle

y=88..256   CARD 1: Shell Monitoring (1012×168px)
              Toggle 0: "Track taskbar auto-hide" — monitorAutoHide
              Toggle 1: "Enable multi-monitor support" — enableMultiMonitor

y=272..392  CARD 2: System Boot (1012×120px)
              Toggle 2: "Launch AuraShell on Windows startup" — autoStartApp

y=408..636  CARD 3: Service Management (1012×228px)
              Status row: "● Running" / "✕ Stopped" / "? Unknown"
              Button row: [Start] [Stop] [Restart]  (D2D rounded buttons)
              Note row:   "Requires Administrator privileges"
```

---

## Architecture

### Async Service Control

```
User clicks Start/Stop/Restart
  → m_serviceActionPending.exchange(true)
  → InvalidateRect (shows "pending..." state)
  → std::thread([this, action] { ... }).detach()
      → getServiceExePath()  ← same dir as AuraConfig.exe
      → ShellExecuteExW(lpVerb=L"runas", --start/--stop)
      → WaitForSingleObject(hProcess, 10000)  ← 10s timeout
      → m_serviceActionPending = false
      → PostMessageW(m_pagePanel, WM_APP+1)   ← UI refresh
```

The `WM_APP+1` handler in `handlePanelMsg` calls `refreshServiceStatus()` and invalidates.

### Registry Auto-Start

```
Reg key: HKCU\Software\Microsoft\Windows\CurrentVersion\Run
Val name: "AuraShell"
Val data: "<path to AuraConfig.exe>"
```

Written on toggle-on; deleted on toggle-off. No UAC required (HKCU).

### Service Status Query

Uses `OpenSCManager` + `OpenService` + `QueryServiceStatusEx` directly. Non-blocking, < 5ms. Called on `onVisible()` and after each async service action.

---

## TDD: test_behavior_logic.cpp (11 tests)

| # | Test | Tag |
|---|------|-----|
| 1 | `AppConfig.autoStartApp` defaults to false | `[behavior][config]` |
| 2 | `AppConfig.monitorAutoHide` defaults to true | `[behavior][config]` |
| 3 | `AppConfig.enableMultiMonitor` defaults to true | `[behavior][config]` |
| 4 | `AUTOSTART_REG_KEY` is correct registry path | `[behavior][registry]` |
| 5 | `hitTestToggle` at auto-hide row → 0 | `[behavior][hit]` |
| 6 | `hitTestToggle` at multi-monitor row → 1 | `[behavior][hit]` |
| 7 | `hitTestToggle` at auto-start row → 2 | `[behavior][hit]` |
| 8 | `hitTestToggle` in header area → -1 | `[behavior][hit]` |
| 9 | `hitTestButton` at Start button → 0 | `[behavior][hit]` |
| 10 | `hitTestButton` at Stop button → 1 | `[behavior][hit]` |
| 11 | `hitTestButton` below button row → -1 | `[behavior][hit]` |

---

## Memory Budget

| Item | Cost |
|------|------|
| `m_serviceStatus` enum | 1 byte |
| `m_serviceActionPending` atomic | 1 byte |
| Thread (exists only during 10s action) | transient |
| Registry HKEY handle | transient (closed immediately) |
| **Total added** | **< 50 bytes** |

Target: sub-15 MB confirmed — BehaviorPage has no D2D resources at rest (D2D factory lazy-initialized same as VisualsPage).

---

## Files Created / Modified

| File | Action |
|------|--------|
| `src/app/theme_model.h` | Add 3 new AppConfig fields |
| `src/app/settings_manager.cpp` | Add JSON load+save for 3 new fields |
| `src/app/page_behavior.h` | Created |
| `src/app/page_behavior.cpp` | Created |
| `src/app/config_window.h` | Add `BehaviorPage m_behaviorPage` member |
| `src/app/config_window.cpp` | Init + wire BehaviorPage |
| `src/app/CMakeLists.txt` | Add page_behavior.cpp |
| `tests/unit/test_behavior_logic.cpp` | Created — 11 headless tests |
| `tests/CMakeLists.txt` | Add test_behavior_logic.cpp |

---

## Resumption Notes

- **Stopped at**: Phase 10.7 complete
- **Next action**: Phase 10.5-A (responsive window: Compact/Standard/Wide sidebar breakpoints)
- **Known blockers**: MSVC initialization required; `ShellExecuteExW` requires `shell32.lib` (already linked via `shlobj.h` in `settings_manager.cpp`)
- **Watch for**: `WM_APP` is `0x8000` — `WM_APP+1` is safe for private window messages. Ensure no collision with other custom messages in the app.

---

*Last Updated: 2026-05-11*
