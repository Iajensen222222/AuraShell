# Changelog

All notable changes to AuraShell are documented here.  
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).  
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

**Sprint 6 additions**
- `ShellIntegration` — message-only HWND handling WM\_TASKBARCREATED (overlay re-anchoring on
  taskbar restart), system tray icon with context menu, WM\_POWERBROADCAST (battery-aware FPS)
- `PerformanceLogger::recordFrame()` wired into `AnimationController::animationLoop()` so
  `getAverageFps()` returns real compositor-synchronised data instead of 0
- `PERF_STATS = 0x0090` IPC message type + `PerfStatsPayload {cpuPercent, memoryMB, avgFps}`
- Reference samples: `01_basic_window` (Mica + dark mode), `02_dpi_aware_overlay` (D2D cursor
  circle over layered window), `03_ipc_client` (console HANDSHAKE → QUERY\_STATE → ACK)

**Sprint 7 additions**
- `HotkeyManager` — `RegisterHotKey` global shortcuts: Win+Shift+A (overlays), Win+Shift+V
  (visualizer), Win+Shift+T (theme cycle); failure-tolerant, wired into `ShellIntegration`
- `IconReplacer` — per-exe custom icon catalog persisted to `icon_overrides.json` via
  `nlohmann/json`; `setIconOverride`, `clearIconOverride`, `applyAll` (in-memory, no explorer.exe)
- `DLL injection sample` (`samples/04_taskbar_dummy_inject`) — notepad.exe dummy target proof
- `AuraShellService.exe` self-registration CLI: `--install`, `--start`, `--stop`, `--uninstall`,
  `--console` (run IPC server without SCM for development)
- InnoSetup 6 installer with VC++ 2022 redist check and service SCM wiring

**Pending for next release**
- Code signing via Microsoft Trusted Signing (pending Azure Identity Validation)
- Winget and Microsoft Store distribution
- Custom domain for the landing page
- `ShellIntegration` wired into `main_app.cpp` (tray icon visible at runtime)
- `ServiceCore::pushPerformanceStats()` sending live CPU/MEM/FPS to DashboardPage

---

## [0.1.0-alpha] — 2026-05-19

First public alpha. All core C++ engine modules are implemented and tested (285/286 pass),
the WinUI 3 configuration UI ships 6 pages with live IPC, and the InnoSetup installer wires
the background service into the Windows SCM. This release targets early testers who can
tolerate rough edges and provide feedback via the
[beta feedback form](https://github.com/Iajensen222222/AuraShell/discussions).

### Added

**Audio Engine (Sprint 2)**
- WASAPI loopback audio capture via `IAudioClient`/`IAudioCaptureClient` (`AudioEngine`)
- 128-band FFT spectrum analysis (`SpectrumAnalyzer`) at < 50 ms input-to-pixel latency
- EMA smoothing and per-band sensitivity multiplier exposed as `setSensitivity()`/`setSmoothing()`
- `AudioEngine::getFrequencyBands()` returns `std::array<float, 128>` for downstream consumers
- `AudioEngine::getPeakLevel()` and `isAudioPresent()` for silence detection

**Audio Visualizer Overlay (Sprint 2)**
- `AudioVisualizerOverlay` — layered Win32 window rendering 128 frequency bars via Direct2D
- Per-pixel-alpha layered window with DIB frame buffer; 60 fps render thread
- `setBrightness()` and `setHeight()` configuration API
- Positioned flush above the Windows taskbar; hides automatically when audio is silent

**Taskbar Hover Animation (Sprint 3)**
- `HoverDetector` — polling-based mouse-over detection with configurable debounce
- 25% scale-up on hover, `OutQuad` easing, 150 ms enter / 180 ms exit timing
- `IconOverlayManager` subscribes `HoverDetector` callbacks; overlay state machine
- Idle CPU < 1% when no animation is running (validated by `PerformanceLogger`)

**DWM Acrylic Backdrop (Sprint 3 — ME-1)**
- `AcrylicBackdrop` in `taskbar_engine/` — applies `DWMSBT_TRANSIENTWINDOW` (value 3) via
  `DwmSetWindowAttribute(DWMWA_SYSTEMBACKDROP_TYPE)` to a companion non-layered HWND
- Frosted-glass acrylic behind the D2D glow rings on icon hover
- Dark-mode immersive color integration (`DWMWA_USE_IMMERSIVE_DARK_MODE`)

**Virtual Desktop Detection (Sprint 4 — ME-2)**
- `VirtualDesktopDetector` — COM singleton wrapping `IVirtualDesktopManager`
- 500 ms polling thread fires `DesktopChangeCallback` with `(prev GUID, next GUID)`
- `isWindowOnCurrentDesktop(HWND)` for per-desktop overlay filtering
- Gracefully degrades when `IVirtualDesktopManager` is unavailable (Session 0 / CI)

**WorkspaceManager (context\_tools)**
- Per-virtual-desktop theme map: `setDesktopTheme(GUID, ThemeConfig)`
- Automatically applies the mapped theme on desktop switch via `VirtualDesktopDetector` callback
- Falls back to the global default theme when no per-desktop entry exists

**ThemeApplier (context\_tools)**
- Writes HKCU accent color (`AccentColorMenu`/`StartColorMenu`) in ABGR format
- Toggles apps/system dark-mode (`AppsUseLightTheme`, `SystemUsesLightTheme`)
- Controls compositor transparency (`EnableTransparency`)
- Broadcasts `WM_SETTINGCHANGE("ImmersiveColorSet")` so the shell picks up changes live
- Backs up original registry values on first apply; `restoreDefaults()` undoes all writes

**ConfigChangeObserver (core/config)**
- `ReadDirectoryChangesW`-based file watcher on the AuraShell config directory
- 300 ms debounce collapses rapid change events into one callback
- Fired on a dedicated thread; consumer callback is invoked on that thread

**IPC Audio Bands Push (Sprint 4 — ME-3)**
- `AUDIO_BANDS = 0x0080` message type added to the named-pipe protocol
- `AudioBandsPayload`: `float bands[128]`, `float peak`, `bool audioPresent` (520 bytes)
- `ServiceCore::pushAudioBands()` serializes the live `AudioEngine` state into a pipe message
- Service receive timeout increased from 2 s to 30 s to sustain persistent connections

**PerformanceLogger (Sprint 5)**
- Background 1 Hz sampler: process CPU % via `GetProcessTimes` delta, working-set via PSAPI,
  overlay FPS via a 64-slot lock-free ring buffer
- `setCpuWarnThreshold()` logs WARN whenever CPU exceeds the target (default 1%)
- Validates the Sprint 2.5 exit criterion: idle CPU < 1% during inactive animation

**WinUI 3 Configuration App (Phase 10)**
- Full 6-page app: Dashboard, Visuals, Behaviors, Desktop Items, Settings, About
- SplitView-based sidebar (replaces NavigationView which crashes in WinUI 3.2.0.1 unpackaged)
- Orange animated selection-pill indicator in the sidebar tracks the active page
- Dashboard: 12 preset glow cards, live service status chip, "Create desktop shortcut" button
- Visuals: accent hex input, animation speed slider, `AudioBandsReceived`-driven 128-bar
  canvas visualizer that updates at ~10 fps via `DispatcherQueue.TryEnqueue`
- Behaviors: auto-start via `HKCU\SOFTWARE\...\Run`, shows live service uptime
- Desktop Items: per-shortcut / per-folder icon replacement via `IShellLinkW` COM
- Settings: dark/light/system theme radio buttons, notifications toggle, JSON persistence
- App icon embedded in EXE and shown in the taskbar (`AppWindow.SetIcon`)
- `Bootstrap.Initialize(0x00020000)` (Windows App SDK 2.0) wires `ms-appx:` URI resolution
- Named-pipe `AuraShellClient` (C#): HANDSHAKE + QUERY_STATE + PUSH_THEME + ACK framing
- `ServiceManager` polls the service every 5 s; `StartAudioStream()` background reader
  dispatches `AudioBandsReceived` events to any subscribed page

**Installer (OA-6)**
- InnoSetup 6 script at `installer/AuraShell.iss`
- `[Run]`: installs service via `--install`, starts it via `--start`, launches UI post-install
- `[UninstallRun]`: stops service (`--stop`) then removes from SCM (`--uninstall`)
- `[Registry]`: deletes `HKCU\SOFTWARE\AuraShell` on uninstall
- Desktop shortcut to `AuraConfig.exe` (optional task in installer)
- VC++ 2022 x64 redistributable check via `RegQueryStringValue` on VS runtime key

**Named Pipe IPC Full Stack**
- `NamedPipeServer` / `NamedPipeClient`: fixed 2064-byte frames (16-byte header + 2048-byte payload)
- HANDSHAKE\_REQUEST / HANDSHAKE\_RESPONSE with PID, version, and capabilities
- QUERY\_STATE → STATUS\_REPORT: current theme, service version, uptime, elevation flag
- PUSH\_THEME → ACK: theme name + animation speed applied and stored in `m_currentTheme`
- Watchdog: `WorkerDisconnectCallback` fires when client drops without graceful ACK
- Deadline-based `connect()` retry loop (replaces fixed retry count)
- DACL-restricted pipe security descriptor via `buildSecurePipeAttributes`

**Testing**
- 285 / 286 Catch2 tests pass (1 known-flaky `HoverDetector::HoverStateTransitions` race)
- New in this release: `test_virtual_desktop`, `test_performance_logger`, `test_navigation`,
  `test_ui_logic`, `test_behavior_logic`, `test_personalization`, `test_shell_customization`,
  `test_ui_interactions`, `test_ipc_handshake` (ODR-fixed), integration `test_full_stack`
- Live IPC integration test (`test_ipc_live.cpp`): child-process `AuraShellService.exe --console`,
  HANDSHAKE RTT < 200 ms verified, QUERY\_STATE uptime asserted > 0

### Fixed

- **WinUI silent exit** — `Bootstrap.Initialize(0x00020000)` was missing when
  `DISABLE_XAML_GENERATED_MAIN` is set; the app silently exited before creating any window.
  Fixed by adding `Bootstrap.Initialize` + `WinRT.ComWrappersSupport.InitializeComWrappers`
  to `Program.cs`.
- **WinUI NavigationView crash (`0x3ac82d`)** — WinUI 3.2.0.1 unpackaged app bug: NavigationView
  crashes when its `Content` property is set with custom ThemeDictionaries active. Fixed by
  replacing NavigationView with a SplitView + custom button-based pane.
- **WinUI InfoBar crash** — InfoBar compositor template crashes in unpackaged WinUI 3.2.0.1.
  Replaced with a plain `Border` + `TextBlock` in `BehaviorPage.xaml`.
- **XamlControlsResources crash** — Adding `<XamlControlsResources>` to `App.xaml`
  `MergedDictionaries` caused `ms-appx:///Microsoft.UI.Xaml/Themes/themeresources.xaml` to
  fail to load (unpackaged apps have no `ms-appx:` package identity by default).
  Fixed by removing `XamlControlsResources` and using plain resource dictionaries.
- **CI vcpkg path** — The root `CMakeLists.txt` hardcoded `../vcpkg` (a local sibling workspace
  path). CI now overrides via `-DCMAKE_TOOLCHAIN_FILE` and bootstraps a fresh vcpkg clone.
- **ODR violation in `test_logging.cpp`** — Forward-declared `Logger` members conflicted with
  the real definition; replaced with `#include "logger.h"`.
- **`std::max` macro conflict in `aurashell_logging`** — `performance_logger.cpp` called
  `std::max(0.0f, percent)` without `NOMINMAX`; Windows.h's `max(a,b)` macro expanded this
  into a syntax error. Fixed by adding `NOMINMAX` to the logging module compile definitions.
- **`std::min` macro conflict in `aurashell_audio`** — Same root cause; `NOMINMAX` added to
  the audio module compile definitions.
- **Logger isolation in tests** — `clearLogs()` deleted the file while spdlog held the handle
  open, leaving subsequent reads seeing stale data. Fixed by flushing, dropping, and
  re-initializing the spdlog logger with `truncate=true` before deleting.
- **`connect()` timeout ignored** — `NamedPipeClient::connect()` had `(void)timeoutMs;`
  suppressing the parameter. Replaced with a deadline-based `std::chrono::steady_clock` loop.
- **Unicode test names failing in CTest** — Windows command-line cannot pass UTF-8 characters
  (`≈`, `×`, `ζ`, `—`) to CTest; test binary would not run. Renamed all affected test cases
  to ASCII equivalents.

### Known limitations

- **Binaries are unsigned** — Windows SmartScreen shows "Windows protected your PC" on first
  run. Code signing via Microsoft Trusted Signing is in progress; click "More info → Run anyway."
- **WinUI AudioBands live visualizer** requires the service's `pushAudioBands()` to be called
  on a timer (the C# reader is wired; the service-side 100 ms push timer is a v0.2 task).
- **HoverDetector::HoverStateTransitions** test is known-flaky due to a race between the
  polling thread and manual `updateMousePosition` calls; marked as a known issue.
- Microsoft Store and winget packages are gated on a signed installer.
- Audio visualizer requires WASAPI loopback (Windows 11 default — no action needed on most
  systems).

### System requirements

- Windows 11 version 22H2 (build 22621) or later
- DirectX 11-capable GPU
- x64 processor
- 200 MB disk space, ~150 MB RAM typical

---

[Unreleased]: https://github.com/Iajensen222222/AuraShell/compare/v0.1.0-alpha...HEAD
[0.1.0-alpha]: https://github.com/Iajensen222222/AuraShell/releases/tag/v0.1.0-alpha
