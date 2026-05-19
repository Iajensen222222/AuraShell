# AuraShell — Architecture Overview

## Module Graph

```
AuraConfig.WinUI  (C#, WinUI 3, unpackaged .exe)
       │
       │  Named Pipe IPC  \\.\pipe\AuraShellService
       │
       ▼
aurashell_service  (Windows service / background process)
       │
       ├── aurashell_platform
       │       ├─ SystemMetrics    — screen & monitor geometry (singleton)
       │       ├─ WindowManager    — window enumeration & properties (singleton)
       │       └─ DpiAwareness     — PerMonitorV2 DPI utilities (static class)
       │
       ├── aurashell_logging       — spdlog rotating file logger (singleton)
       │
       ├── aurashell_ipc
       │       ├─ NamedPipeServer  — service-side pipe listener
       │       ├─ NamedPipeClient  — config-app-side connection
       │       └─ message_types.h  — 2064-byte fixed-width IPC frames
       │
       ├── aurashell_taskbar_engine
       │       ├─ TaskbarController  — hover detection, icon overlay dispatch
       │       ├─ HoverDetector      — per-icon hit-test and scale animation
       │       └─ IconOverlay        — DirectX 11 overlay composition
       │
       └── aurashell_config_app
               ├─ ConfigWindow       — main Win32 config host window
               ├─ NavigationManager  — sidebar nav rail + spring indicator
               ├─ SettingsManager    — JSON config load/save/hot-reload
               └─ Page_*             — DashboardPage, VisualsPage, BehaviorPage,
                                       DesktopItemsPage, AboutPage
```

## Source Layout

```
src/
├── core/
│   ├── logging/           logger.h / logger.cpp
│   └── platform/
│       ├── system_metrics.h/.cpp
│       ├── window_manager.h/.cpp
│       ├── dpi_awareness.h/.cpp
│       └── ipc/
│           ├── message_types.h
│           ├── named_pipe_server.h/.cpp
│           └── named_pipe_client.h/.cpp
├── taskbar_engine/        TaskbarController, HoverDetector, IconOverlay
├── service/               service_core.h/.cpp, pipe_security.h
├── app/                   ConfigWindow, NavigationManager, SettingsManager, pages
└── AuraConfig.WinUI/      C# WinUI 3 project (Program.cs, App.xaml, pages)
```

## IPC Protocol

The config app (WinUI) and the background service communicate via a Windows named pipe.

**Pipe name:** `\\.\pipe\AuraShellService`

**Frame format** (defined in `src/core/platform/ipc/message_types.h`):

| Field | Type | Size | Description |
|---|---|---|---|
| `type` | `MessageType` (enum) | 4 bytes | Message code |
| `payload` | `uint8_t[2048]` | 2048 bytes | Fixed-width typed payload |
| *(padding)* | — | 12 bytes | Alignment to 2064-byte frame |

**Message types** (16 codes):

| Code | Direction | Description |
|---|---|---|
| `HANDSHAKE_REQUEST` | App → Service | Initial connection, sends client PID + version |
| `HANDSHAKE_RESPONSE` | Service → App | Confirms connection, returns service version |
| `APPLY_THEME` | App → Service | Push a `ThemePayload` to the service |
| `QUERY_STATE` | App → Service | Request current service state |
| `QUERY_STATE_RESPONSE` | Service → App | Returns `QueryStateResponse` |
| `STATUS_REPORT` | Service → App | Periodic health ping |
| *(10 reserved)* | — | Future expansion |

**Payload helpers** (`message_types.h`):
```cpp
Message msg;
msg.type = MessageType::APPLY_THEME;
msg.setPayload(ThemePayload{ .themeName = "Dark", .colorCount = 5 });

auto payload = msg.getPayload<ThemePayload>();
```

## Build System

**Requirements:** CMake 3.22+, MSVC 2022 (v143 toolset), Windows SDK 22621+, vcpkg

**Dependencies** (managed via `vcpkg.json`): spdlog, Catch2, nlohmann-json, Microsoft Windows App SDK

**Configure and build:**
```powershell
# Clone fresh vcpkg (or reuse an existing install)
git clone https://github.com/microsoft/vcpkg.git vcpkg-local
.\vcpkg-local\bootstrap-vcpkg.bat -disableMetrics

# Configure
cmake -B build -S . `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="vcpkg-local/scripts/buildsystems/vcpkg.cmake" `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTS=ON

# Build
cmake --build build --config Release

# Run tests
ctest --test-dir build -C Release --output-on-failure
```

The `CMakeLists.txt` at the repo root hardcodes the vcpkg toolchain path relative to the parent
directory (`../vcpkg`). The command-line `-DCMAKE_TOOLCHAIN_FILE` override takes precedence and
does not require a specific directory layout.

## Test Suite

**Unit tests** live in `tests/unit/` and compile into a single `test_runner` executable.
They are headless — no Win32 window, no D2D device, no live service required.

```powershell
ctest --test-dir build -C Release --output-on-failure -L "[unit]"
```

**Integration tests** live in `tests/integration/` and require the AuraShell service to be
running (a live named pipe must be open). They are tagged `[integration]`.

```powershell
# Start the service first, then:
ctest --test-dir build -C Release --output-on-failure -L "[integration]"
```

Test discovery uses `catch_discover_tests(test_runner)` — tests are auto-registered with CTest
from Catch2 tags. No manual CTest `add_test()` calls required.

## Key Design Decisions

**Why a named pipe instead of shared memory?** Named pipes provide natural flow control and
access-control via Windows security descriptors (`pipe_security.h`). Shared memory would require
explicit synchronization primitives and is harder to secure against privilege escalation.

**Why unpackaged WinUI?** AuraShell targets Windows 11 but must run without MSIX packaging for
early alpha builds and developer installs. `Bootstrap.Initialize(0x00020000)` in `Program.cs`
satisfies the Windows App SDK's requirement for a package graph entry without MSIX.

**Why spdlog for logging?** Rotating file sinks, thread-safe async logging, and sub-millisecond
overhead. The `Logger` singleton wraps spdlog to keep the include surface small (forward-declared
in `logger.h`; spdlog headers only in `logger.cpp`).

**Performance targets:** See [PERFORMANCE_GUIDE.md](./PERFORMANCE_GUIDE.md) for CPU, memory, and
frame-rate budgets per module.
