# AuraShell Phase 9: Deployment & Packaging

**Date**: 2026-05-10  
**Status**: Phase 9 ✅ Code Complete — Pending first production build

---

## Executive Summary

- **Service Self-Registration**: `AuraShellService.exe` now handles `--install`, `--uninstall`, `--start`, and `--stop` via the Windows SCM API, eliminating any external dependency (no `sc.exe`, no PowerShell for service registration).
- **PowerShell Installer** (`installer/install.ps1`): Elevation-guarded, copies binaries, creates AppData directories, registers + starts the service, adds Start Menu shortcuts, and updates system PATH. Doubles as an uninstaller via `-Uninstall`.
- **Inno Setup Script** (`installer/AuraShell.iss`): Full GUI installer producing `AuraShell-1.0-Setup.exe` with Windows 11 version check, per-monitor DPI layout, SCM service integration, and uninstall support.
- **Smoke Test** (`installer/smoke_test.ps1`): 8 validation sections (binaries, registration, service state, memory, AppData dirs, IPC pipe reachability, Start Menu, config app launch). Reports pass/fail/skip counts; exits non-zero on any failure.

---

## Files Created

| File | Purpose | Status |
|------|---------|--------|
| `installer/install.ps1` | PowerShell installer/uninstaller (elevation required) | ✅ Complete |
| `installer/AuraShell.iss` | Inno Setup 6 script → `AuraShell-1.0-Setup.exe` | ✅ Complete |
| `installer/smoke_test.ps1` | Post-install verification (8 check sections) | ✅ Complete |

---

## Files Modified

| File | Change | Status |
|------|--------|--------|
| `src/service/main_service.cpp` | Replaced stub `wmain` with full CLI parser: `--install` (CreateService + failure recovery), `--uninstall` (StopService + DeleteService with 10s poll), `--start` (StartService + poll for RUNNING), `--stop` (ControlService STOP + poll), default → SCM dispatcher with usage message | ✅ Complete |
| `src/service/CMakeLists.txt` | Added `/SUBSYSTEM:CONSOLE` so `--install`/`--uninstall` printf output is visible in elevated terminal; added MSVC flags `/W4 /WX /permissive-` to the exe target | ✅ Complete |

---

## Architecture Decisions

### Self-Registration via Binary Flags (not sc.exe or PowerShell)

**Decision**: Implement `--install` / `--uninstall` directly in `AuraShellService.exe` using `OpenSCManager` / `CreateService` / `DeleteService`.

**Rationale**: The binary records its own absolute path during `--install` via `GetModuleFileNameW`, avoiding relative-path issues when the SCM later launches it. This also means the Inno Setup script and PowerShell installer both call the same code path — single source of truth for service metadata (display name, description, failure recovery actions).

**Service binary path in registry**: `"C:\Program Files\AuraShell\AuraShellService.exe"` (quoted for spaces). No `--run` flag needed — the SCM invokes the exe directly and `StartServiceCtrlDispatcherW` distinguishes SCM launches from CLI launches via `ERROR_FAILED_SERVICE_CONTROLLER_CONNECT`.

### Failure Recovery Policy

The `--install` path configures automatic restart via `ChangeServiceConfig2W` with `SERVICE_CONFIG_FAILURE_ACTIONS`:
- 1st failure: restart after 5 s
- 2nd failure: restart after 10 s
- 3rd+ failure: no action (prevents infinite restart loops if the binary is broken)
- Reset count after 24 h uptime

### Dual Installer Strategy

| Scenario | Use |
|----------|-----|
| Developer machine / CI | `install.ps1` — scriptable, no GUI dependency |
| End-user distribution | `AuraShell.iss` compiled to `AuraShell-1.0-Setup.exe` — standard Windows GUI installer |

Both paths call `AuraShellService.exe --install` and `--start` identically.

### Console Subsystem for Service Exe

**Decision**: `AuraShellService.exe` uses `/SUBSYSTEM:CONSOLE`, not `/SUBSYSTEM:WINDOWS`.

**Rationale**: The SCM never creates a console for a service process regardless of subsystem — the subsystem flag only affects user-launched invocations. Using CONSOLE allows `printf` output from `--install` to reach an elevated terminal without an `AllocConsole()` call. No behavior change for normal service operation.

---

## Service Registration Parameters

```
Service Name    : AuraShellService
Display Name    : AuraShell Visual Service
Description     : Manages AuraShell icon overlays and IPC for the taskbar visual engine.
Start Type      : SERVICE_AUTO_START  (starts with Windows)
Account         : LocalSystem
Binary Path     : "C:\Program Files\AuraShell\AuraShellService.exe"
Error Control   : SERVICE_ERROR_NORMAL
Failure Recovery:
  1st failure   → restart after 5 000 ms
  2nd failure   → restart after 10 000 ms
  3rd+ failure  → SC_ACTION_NONE
  Reset period  → 86 400 s (24 h)
```

---

## Install Flow

```
install.ps1 / AuraShell.iss
          │
          ├─ [1] Validate source binaries exist
          ├─ [2] Copy to C:\Program Files\AuraShell\
          ├─ [3] Create %LOCALAPPDATA%\AuraShell\{logs,themes}
          ├─ [4] AuraShellService.exe --install  (SCM CreateService)
          ├─ [5] AuraShellService.exe --start    (SCM StartService + wait)
          ├─ [6] Create Start Menu shortcuts
          └─ [7] Add install dir to system PATH
```

---

## Uninstall Flow

```
install.ps1 -Uninstall / Inno Setup uninstaller
          │
          ├─ AuraShellService.exe --stop      (ControlService + 10s poll)
          ├─ AuraShellService.exe --uninstall (DeleteService)
          ├─ Remove Start Menu group
          └─ Remove C:\Program Files\AuraShell\
              (leaves %LOCALAPPDATA%\AuraShell\ intact — user config preserved)
```

---

## Smoke Test Coverage

| Section | Checks | Pass Criteria |
|---------|--------|---------------|
| Binaries | AuraShellService.exe, AuraConfig.exe exist | Both present |
| Service Registration | `AuraShellService` in SCM, StartType=Automatic | Registered, AUTO_START |
| Service State | Status=Running, WorkingSet ≤ 2 MB | Running, ≤ 2 MB |
| AppData Dirs | `%LOCALAPPDATA%\AuraShell\{,logs,themes}` | All 3 created |
| Named Pipe IPC | `\\.\pipe\AuraShell_Control` reachable | Test-Path succeeds |
| Start Menu | Group + 2 shortcuts | Both .lnk files present |
| System PATH | Install dir in Machine PATH | Present (skip if absent) |
| Config App Launch | AuraConfig.exe starts and stays alive 3 s | No immediate exit |

**Run:**
```powershell
cd "C:\Program Files\AuraShell"
.\smoke_test.ps1
# Expected: 8+ checks PASS, exit code 0
```

---

## Building the Inno Setup Installer

```batch
REM Prerequisites: Inno Setup 6 (https://jrsoftware.org/isdl.php)
REM Build Release binaries first:
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cd "c:\Users\iajen\App Ideas\Desktop Icon Changer\AuraShell\out\build\x64-Release"
ninja -j8

REM Compile the installer:
cd "c:\Users\iajen\App Ideas\Desktop Icon Changer\AuraShell\installer"
iscc AuraShell.iss

REM Output: ..\dist\AuraShell-1.0-Setup.exe
```

---

## Clean-Machine Deployment Steps

1. Copy `AuraShell-1.0-Setup.exe` to target machine
2. Run as Administrator → follow wizard
3. After install completes:
   ```powershell
   cd "C:\Program Files\AuraShell"
   .\smoke_test.ps1
   ```
4. Expected result: all checks PASS, AuraShellService status = Running
5. Launch **AuraShell Configuration** from Start Menu → status bar shows "● Service: connected"

---

## Memory Budget Verification

| Component | Target | Verification |
|-----------|--------|-------------|
| AuraShellService | < 2 MB working set (idle) | `smoke_test.ps1` section "Service State" |
| AuraConfig (idle) | < 30 MB working set | `Get-Process AuraConfig \| Select WorkingSet64` |
| Named pipe buffer | 4 096 bytes per connection | Fixed in `named_pipe_server.h` |
| Service CLI additions | < 500 bytes stack per invocation | `doInstall` / `doUninstall` are stack-only |

---

## Build Status

| Artifact | Status | Blocker |
|----------|--------|---------|
| AuraShellService.exe | 🔴 Not yet compiled | MSVC env not initialized |
| AuraConfig.exe | 🔴 Not yet compiled | MSVC env not initialized |
| AuraShell-1.0-Setup.exe | 🔴 Not yet built | Requires compiled binaries + Inno Setup |
| smoke_test.ps1 | ✅ Script ready | Run after installation |

---

## Resumption Notes

- **Stopped at**: All Phase 9 source files and scripts written
- **Next action**: Initialize MSVC (`vcvarsall.bat x64`) → `ninja -j8` Release build → run `install.ps1` → run `smoke_test.ps1`
- **Known blockers**:
  1. MSVC environment not initialized (same as Phase 6 blocker)
  2. Inno Setup 6 must be installed to compile the `.iss` script
  3. `AuraShell.iss` `SetupIconFile` line is empty — provide a `.ico` path if an icon asset exists
- **Watch for**: `wmain` signature — `main_service.cpp` uses `wmain(int, wchar_t const* const[])` which requires the MSVC linker to select the Unicode entry point. Confirm `UNICODE` and `_UNICODE` are defined (already set in CMakeLists.txt).

---

*Last Updated: 2026-05-10*  
*Next Review: After first successful Release build and smoke test run*
