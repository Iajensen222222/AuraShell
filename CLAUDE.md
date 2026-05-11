# CLAUDE.md - AuraShell Project System Instructions

## Project Overview

**AuraShell** is a high-performance Windows 11 customization utility written in modern C++ (C++20). The project implements a modular architecture separating the Taskbar Engine from Visual Enhancements (DirectX overlays), with adaptive performance scaling and comprehensive logging.

**Repository**: https://github.com/Iajensen222222/AuraShell  
**Build System**: CMake 3.22+ with vcpkg for dependency management  
**Target**: Windows 11 (SDK 22621+), MSVC 2022 (v143)

---

## Coding Standards & Conventions

### 1. RAII for Win32 Handles

**Rule**: All Win32 handles (HWND, HANDLE, HKEY, HICON) must be wrapped in RAII smart pointers or custom guard classes. Never use raw handles without cleanup logic.

**Pattern - Custom Handle Guard**:
```cpp
template <typename HandleType, HandleType InvalidValue, auto Deleter>
class HandleGuard {
public:
    explicit HandleGuard(HandleType h = InvalidValue) : m_handle(h) {}
    ~HandleGuard() { 
        if (m_handle != InvalidValue) Deleter(m_handle); 
    }
    HandleType get() const { return m_handle; }
    HandleType release() { auto h = m_handle; m_handle = InvalidValue; return h; }
private:
    HandleType m_handle;
};

// Usage for registry keys
using HKEYGuard = HandleGuard<HKEY, nullptr, [](HKEY k) { RegCloseKey(k); }>;

HKEYGuard hKey(nullptr);
LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\AuraShell", 0, KEY_READ, &hKey);
// Automatic cleanup when hKey goes out of scope
```

**Pattern - COM Smart Pointers**:
```cpp
#include <wrl.h>
using namespace Microsoft::WRL;

ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
// Automatic Release() on destruction
```

**Never do this**:
```cpp
HKEY hKey;
RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\AuraShell", 0, KEY_READ, &hKey);
// ... code that might throw or return early ...
RegCloseKey(hKey); // May be skipped!
```

### 2. Explicit Error Handling for HRESULT

**Rule**: Every Windows API call that returns HRESULT must be checked immediately. No silent failures.

**Pattern - Macro-Based Checking**:
```cpp
#define AURA_HR_CHECK(hr, context) \
    do { \
        if (FAILED(hr)) { \
            AURA_LOG_ERROR("[{}] HRESULT failure: 0x{:08X}", context, hr); \
            throw std::runtime_error("Operation failed"); \
        } \
    } while(0)

#define AURA_HR_LOG(hr, context) \
    do { \
        if (FAILED(hr)) { \
            AURA_LOG_WARN("[{}] HRESULT warning: 0x{:08X}", context, hr); \
        } \
    } while(0)
```

**Usage**:
```cpp
HRESULT hr = device->CreateBuffer(&desc, nullptr, &buffer);
AURA_HR_CHECK(hr, "CreateBuffer");

// For non-critical operations:
HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
AURA_HR_LOG(hr, "CreateTexture2D");
```

**Exception Hierarchy**:
```cpp
// Define specific exceptions
class AudioException : public std::runtime_error {
public:
    explicit AudioException(const std::string& msg, HRESULT hr = S_OK)
        : std::runtime_error(msg), m_hr(hr) {}
    HRESULT getHResult() const { return m_hr; }
private:
    HRESULT m_hr;
};

// Usage
try {
    audio_engine.initializeWASAPI();
} catch (const AudioException& e) {
    AURA_LOG_ERROR("Audio initialization failed: {} (0x{:08X})", 
                   e.what(), e.getHResult());
    // Graceful degradation: disable audio visualizer
    config.disableAudioVisualizer();
}
```

### 3. std::wstring for All Windows Paths

**Rule**: Always use `std::wstring` (UTF-16) for file paths, registry keys, and window class names. Never use `std::string` for paths on Windows.

**Rationale**: Windows APIs use UTF-16 (wchar_t). Using std::string forces unnecessary conversions and may lose data for paths with non-ASCII characters.

**Pattern**:
```cpp
// GOOD: std::wstring for all Windows paths
std::wstring configPath = L"C:\\Users\\iajen\\AppData\\Local\\AuraShell\\config.json";
std::wstring classNameW = L"AuraShell_MainWindow";
std::wstring registryKeyW = L"Software\\AuraShell\\Themes";

// BAD: std::string for Windows paths
std::string configPath = "C:\\Users\\iajen\\AppData\\Local\\AuraShell\\config.json"; // WRONG
std::string className = "AuraShell_MainWindow"; // WRONG

// Conversion when necessary:
std::string utf8_string = /* ... */;
std::wstring utf16_string = std::wstring(utf8_string.begin(), utf8_string.end()); // AVOID

// Better: Use proper UTF-8 to UTF-16 conversion
#include <codecvt>
std::wstring utf8ToWstring(const std::string& utf8) {
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], size);
    return result;
}
```

**Environment Variables**:
```cpp
// Correctly retrieve Windows paths
std::wstring getAppDataPath() {
    wchar_t appData[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
    if (FAILED(hr)) throw std::runtime_error("Failed to get AppData path");
    return std::wstring(appData) + L"\\AuraShell";
}

// Usage
std::wstring configDir = getAppDataPath();
std::wstring configFile = configDir + L"\\config.json";
```

### 4. Naming Conventions

| Construct | Convention | Example |
|-----------|-----------|---------|
| Namespaces | `aura::[feature]` | `aura::taskbar`, `aura::visual` |
| Classes | PascalCase | `AudioEngine`, `WindowManager` |
| Functions | camelCase | `captureAudioFrame()`, `checkWindowVisibility()` |
| Member variables | `m_` prefix (private) | `m_device`, `m_logger` |
| Static members | `s_` prefix | `s_instance`, `s_globalLogger` |
| Constants | SCREAMING_SNAKE_CASE | `MAX_ANIMATION_FRAMES`, `DEFAULT_FPS_TARGET` |
| Local variables | camelCase | `frameCount`, `scaleFactor` |
| boolean functions | `is`, `has`, `can` prefixes | `isVisible()`, `hasAudio()`, `canInject()` |

### 5. Header Organization

Every .h file must follow this structure (no exceptions):
```cpp
#pragma once

// System headers
#include <chrono>
#include <vector>
#include <memory>

// Windows headers
#include <windows.h>
#include <dxgi.h>
#include <d3d11.h>

// External library headers
#include <spdlog/logger.h>
#include <nlohmann/json.hpp>
#include <wrl.h>

// Project headers (relative paths)
#include "aurashell/common.h"
#include "aurashell/platform/window_manager.h"

namespace aura::graphics {

// Forward declarations
class Renderer;
struct DrawContext;

// Class definitions
class Shader final {
    // ...
};

} // namespace aura::graphics
```

---

## Negative Constraints (Explicitly Prohibited)

### 1. Never Use explorer.exe as Primary Test Target

**Constraint**: Do NOT inject or hook into explorer.exe for initial development and debugging. explorer.exe restarts frequently and is critical to Windows stability.

**Requirement**: Always provide a "Dummy Target" for testing:
- **Dummy Target #1**: Notepad (notepad.exe) - simple window, harmless
- **Dummy Target #2**: Custom test window - build a minimal Win32 window in `samples/test_window/`
- **Dummy Target #3**: Calc.exe - another safe target

**When testing DLL injection**:
```cpp
// Test flow:
// 1. Launch dummy target (notepad.exe or custom test window)
// 2. Inject DLL into dummy process
// 3. Verify hook works (mouse position tracking, icon rendering)
// 4. Clean up without affecting Windows stability
// 5. ONLY after successful dummy tests: test with explorer.exe in isolated VM

std::wstring dummyTarget = L"C:\\Windows\\notepad.exe";
HRESULT hr = injectDllIntoProcess(dummyTarget, L"taskbar_injected.dll");
if (SUCCEEDED(hr)) {
    AURA_LOG_INFO("Injection successful on dummy target");
    // Verify functionality
    // ...
    // Later: test real explorer.exe (in controlled environment)
}
```

**Why this matters**:
- explorer.exe manages the taskbar, desktop, and file explorer
- Failed injection crashes the entire shell
- Testing with dummy targets allows safe iteration
- Use Windows Sandbox or VM for explorer.exe testing only

### 2. No Global State Without Synchronization

**Constraint**: Any static/global variable shared between threads must be protected by locks or atomic operations.

**Prohibited**:
```cpp
// BAD: Global without synchronization
static HWND g_taskbarWindow = nullptr; // WRONG - race condition

// GOOD: Protected by mutex
class WindowManager {
private:
    static std::mutex s_taskbarMutex;
    static HWND s_taskbarWindow;
public:
    static HWND getTaskbarWindow() {
        std::lock_guard<std::mutex> lock(s_taskbarMutex);
        return s_taskbarWindow;
    }
};
```

### 3. No Blocking Calls on Render Thread

**Constraint**: The DirectX render thread must never block on I/O or synchronous operations. Use async operations only.

**Prohibited**:
```cpp
// BAD: File I/O on render thread
void Renderer::renderFrame() {
    std::string configData = readConfigFile(); // BLOCKING - WRONG
    updateShaders(configData);
}

// GOOD: Async config updates
class Renderer {
private:
    std::atomic<bool> m_configDirty{false};
    Theme m_currentTheme;
    std::mutex m_themeMutex;
    
    void updateConfigThread() {
        while (m_running) {
            Theme newTheme = configManager.loadThemeAsync();
            {
                std::lock_guard<std::mutex> lock(m_themeMutex);
                m_currentTheme = newTheme;
                m_configDirty = true;
            }
        }
    }
    
    void renderFrame() {
        if (m_configDirty.exchange(false)) {
            std::lock_guard<std::mutex> lock(m_themeMutex);
            applyTheme(m_currentTheme); // Non-blocking
        }
    }
};
```

### 4. No Resource Leaks on Exception

**Constraint**: Every resource acquisition must have a corresponding release. No exceptions to this rule.

**Prohibited**:
```cpp
// BAD: Resource leak if exception is thrown
void processAudio() {
    HANDLE audioBuffer = allocateAudioBuffer(); // May throw after this
    processAudioData(audioBuffer);
    freeAudioBuffer(audioBuffer); // May not execute if exception thrown
}

// GOOD: RAII ensures cleanup
void processAudio() {
    AudioBufferGuard buffer(allocateAudioBuffer()); // Cleanup on destruction
    processAudioData(buffer.get());
}
```

### 5. No Magic Numbers

**Constraint**: All numeric constants must be named and declared at module scope or in constants header.

**Prohibited**:
```cpp
// BAD: Magic numbers
if (frameTime > 16) { // What is 16? Milliseconds? Frames?
    reduceQuality(); // Why 16?
}

// GOOD: Named constants
constexpr uint32_t TARGET_FPS = 60;
constexpr uint32_t FRAME_TIME_MS = 1000 / TARGET_FPS; // 16.67ms

if (frameTime > FRAME_TIME_MS) {
    reduceQuality();
}
```

---

## Performance Rules

### Idle CPU Usage Target: < 1%

**Rule**: When AuraShell is idle (no overlays rendering, no animations active), CPU usage must remain below 1%.

**Metrics to track**:
- Main application: < 0.3% (UI thread idle, minimal polling)
- Overlay renderer: < 0.2% (sleep when vsync inactive)
- Config watcher: < 0.1% (file watcher async)
- IPC service: < 0.2% (waiting on named pipes, no busy-loop)

**Total: < 1% aggregate**

**Implementation strategy**:
```cpp
// Good: Sleep between checks instead of busy-loop
void HoverDetector::pollMousePosition() {
    while (m_running) {
        // Check mouse position
        POINT mousePos;
        GetCursorPos(&mousePos);
        
        // Check if over taskbar (bounding box check)
        if (isMouseOverTaskbar(mousePos)) {
            handleHover(mousePos);
        }
        
        // IMPORTANT: Sleep to reduce CPU usage
        // Polling every 16ms @ 60Hz is sufficient
        // This prevents 100% CPU spin
        Sleep(16); // 16ms = 60Hz sampling rate
    }
}
```

**Monitoring approach**:
```cpp
// Track CPU usage in PerformanceLogger
class PerformanceLogger {
public:
    void recordIdleTime() {
        if (m_isIdle) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_idleStartTime).count();
            
            if (elapsed > 1000) { // Sample every second
                float cpuUsage = getCurrentProcessCpuUsage();
                AURA_LOG_DEBUG("Idle CPU usage: {:.2f}%", cpuUsage);
                
                if (cpuUsage > 1.0f) {
                    AURA_LOG_WARN("Idle CPU exceeded 1% threshold: {:.2f}%", cpuUsage);
                    // Investigate and optimize
                }
                m_idleStartTime = now;
            }
        }
    }
};
```

### Frame Rate Targets

| Scenario | Target FPS | Acceptable Range |
|----------|-----------|------------------|
| Hover animations (idle) | 60 | 55-65 fps |
| Audio visualizer (music) | 60 | 55-65 fps |
| Intensive effects (gaming) | Adaptive | 30-144 fps |
| Screensaver mode | 1 | < 1 |

### Memory Usage Targets

| Component | Max Memory | Notes |
|-----------|-----------|-------|
| Main application | 80 MB | UI + theme cache |
| Core library (dll) | 30 MB | DirectX, audio, config |
| Overlay renderer | 50 MB | Texture cache, buffers |
| Total (idle) | 160 MB | Baseline on launch |

---

## One-Click Reset & Registry Backup

### Feature Reset Pattern

Every customization must have a "Restore Defaults" code path:

```cpp
class ThemeManager {
public:
    void applyTheme(const Theme& newTheme) {
        // STEP 1: Backup current state before any modifications
        m_previousTheme = m_currentTheme;
        backupRegistry();
        
        try {
            // STEP 2: Apply new theme
            applyThemeToRegistry(newTheme);
            applyThemeToVisuals(newTheme);
            m_currentTheme = newTheme;
        } catch (const std::exception& e) {
            // STEP 3: Rollback on failure
            AURA_LOG_ERROR("Theme application failed: {}", e.what());
            restoreRegistry();
            m_currentTheme = m_previousTheme;
            throw;
        }
    }
    
    void restoreDefaults() {
        // Simple one-click restore
        m_currentTheme = Theme::createDefault();
        applyTheme(m_currentTheme);
        restoreRegistry();
    }
    
private:
    Theme m_currentTheme;
    Theme m_previousTheme;
    std::map<std::wstring, std::wstring> m_registryBackup;
    
    void backupRegistry() {
        m_registryBackup.clear();
        // Save current registry values before modification
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\AuraShell", 
                          0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            // Enumerate and backup all values
            wchar_t valueName[256];
            DWORD valueSize;
            for (DWORD i = 0; ; ++i) {
                valueSize = sizeof(valueName);
                if (RegEnumValueW(hKey, i, valueName, &valueSize, nullptr, 
                                  nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                
                // Store current value in backup
                wchar_t valueData[1024];
                DWORD dataSize = sizeof(valueData);
                RegQueryValueExW(hKey, valueName, nullptr, nullptr, 
                                 (LPBYTE)valueData, &dataSize);
                m_registryBackup[valueName] = valueData;
            }
            RegCloseKey(hKey);
        }
    }
    
    void restoreRegistry() {
        // Restore all backed-up registry values
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\AuraShell", 
                          0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            for (const auto& [name, value] : m_registryBackup) {
                RegSetValueExW(hKey, name.c_str(), 0, REG_SZ, 
                              (const BYTE*)value.c_str(), 
                              (value.length() + 1) * sizeof(wchar_t));
            }
            RegCloseKey(hKey);
        }
    }
};
```

---

## Absolute Path Resolution

**Rule**: All resource loading (DLLs, themes, icons) must use absolute paths to prevent hijacking and loading failures.

```cpp
// GOOD: Absolute path construction
std::wstring getAbsoluteThemePath(const std::wstring& themeName) {
    // Never trust relative paths
    wchar_t appData[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
    if (FAILED(hr)) throw std::runtime_error("Failed to resolve AppData path");
    
    std::wstring themePath = std::wstring(appData) + L"\\AuraShell\\themes\\" + themeName + L".json";
    
    // Verify path is still within expected directory (security check)
    std::filesystem::path resolved = std::filesystem::absolute(themePath);
    std::filesystem::path expectedDir = std::filesystem::absolute(
        std::wstring(appData) + L"\\AuraShell\\themes");
    
    if (resolved.parent_path() != expectedDir) {
        throw std::runtime_error("Path traversal attempt detected");
    }
    
    return resolved.wstring();
}

// Loading DLL with absolute path
HMODULE loadTaskbarDll() {
    wchar_t programFiles[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, programFiles);
    if (FAILED(hr)) throw std::runtime_error("Failed to resolve Program Files");
    
    std::wstring dllPath = std::wstring(programFiles) + 
                          L"\\AuraShell\\injected\\taskbar_injected.dll";
    
    // Verify file exists before loading
    if (!std::filesystem::exists(dllPath)) {
        throw std::runtime_error("DLL not found: " + std::string(dllPath.begin(), dllPath.end()));
    }
    
    HMODULE hModule = LoadLibraryW(dllPath.c_str());
    if (!hModule) {
        throw std::runtime_error("Failed to load DLL: " + std::string(dllPath.begin(), dllPath.end()));
    }
    
    return hModule;
}
```

---

## Debugging & Logging Standards

### Logging Levels (spdlog)

```cpp
AURA_LOG_TRACE("Detailed execution flow - only in debug builds");
AURA_LOG_DEBUG("High-level function entry/exit, config values");
AURA_LOG_INFO("Feature enabled/disabled, theme applied");
AURA_LOG_WARN("Recoverable errors, degraded performance");
AURA_LOG_ERROR("Unrecoverable errors, feature disabled");
AURA_LOG_CRITICAL("System-level failures, immediate shutdown");
```

### Log Output Format

All logs written to: `%LOCALAPPDATA%\AuraShell\logs\aurashell.log`

Rotating file sink:
- Max file size: 10 MB
- Max files: 5 (keeps 50 MB history)
- Format: `[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [thread_id] [module] message`

```
[2026-04-29 14:23:45.123] [INFO ] [0x1234] [taskbar] Taskbar window detected: HWND=0x00010ABC
[2026-04-29 14:23:45.234] [DEBUG] [0x1234] [animation] Animation started: scale 1.0→1.2, duration=200ms
[2026-04-29 14:23:45.456] [ERROR] [0x5678] [audio] WASAPI initialization failed: 0x88890001
```

---

## Plan Documentation Protocol

**Rule**: Any time an AI agent creates a plan, enters plan mode, or proposes a multi-step implementation strategy for this project, it **must** create or update a corresponding Markdown file that captures the plan's outcome.

### When to Create/Update a Plan Markdown File

- Entering plan mode for any feature, phase, or subsystem
- After completing a phase or major implementation block
- After resolving a significant architectural decision
- After discovering a new requirement or constraint that changes scope

### File Naming Convention

| Scope | File Location | Example |
|-------|--------------|---------|
| Phase completion + next roadmap | `PHASE{N}_COMPLETION_AND_PHASE{N+1}-{M}_ROADMAP.md` | `PHASE5_COMPLETION_AND_PHASE6-8_ROADMAP.md` |
| Single-phase plan | `PHASE{N}_PLAN.md` | `PHASE6_PLAN.md` |
| Subsystem design decision | `docs/ADR_{TOPIC}.md` | `docs/ADR_IPC_PROTOCOL.md` |
| Sprint summary | `docs/SPRINT_{N}_SUMMARY.md` | `docs/SPRINT_2_SUMMARY.md` |

All plan files must be placed in the project root or `docs/` directory so they are discoverable by any agent opening the project.

### Required Contents of Every Plan Markdown File

Every plan file must include:

1. **Date and Status** — `**Date**: YYYY-MM-DD` and `**Status**: Planning | In Progress | Complete`
2. **Executive Summary** — 2–4 bullet points describing what was done or what will be done
3. **Files Created or Modified** — Table listing each file, its purpose, and completion status
4. **Architecture Decisions** — Key choices made and why alternatives were rejected
5. **Build Status** — Whether the code compiles and tests pass
6. **Next Steps** — What the next phase or agent session should pick up

### Agent Handoff Requirement

When ending a session mid-implementation, the plan markdown file **must** be updated to include a "Resumption Notes" section describing exactly where work stopped and what the next agent should do first. This ensures zero onboarding time for the next session.

```markdown
## Resumption Notes
- **Stopped at**: Brief description of last completed action
- **Next action**: First specific thing the next agent should do
- **Known blockers**: List any environment or dependency issues
```

---

## CI/CD & Pre-commit Checks

### Before Every Commit

1. **Code compilation**: Must compile without errors or warnings (W4 MSVC)
2. **Unit tests**: All tests must pass
3. **Static analysis**: clang-tidy scan (warnings only, not blocking)
4. **Memory safety**: No resource leaks (manual inspection)
5. **Naming convention**: Verify against coding standards above

### Git Workflow

```bash
# Never commit directly to main
# Always use feature branches

git checkout -b feature/taskbar-hover-effects
# ... make changes ...
git add src/taskbar_engine/*
git commit -m "feat(taskbar): implement hover scale animation"
# Create pull request on GitHub
# Request review from at least one team member
# After approval, merge to main
```

---

## Contact & Escalation

For questions about project standards or unusual requirements:
- Review the architecture plan: `/PLAN.md` for sprint breakdown
- Check implementation samples: `/samples/` for reference code
- Consult performance guide: `/docs/PERFORMANCE_GUIDE.md`
- Create an issue on GitHub for discussion

---

**Document Version**: 1.0  
**Last Updated**: 2026-04-29  
**Maintainer**: AuraShell Development Team
