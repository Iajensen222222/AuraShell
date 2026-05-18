# Changelog

All notable changes to AuraShell are documented here.  
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).  
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

- Code signing via Microsoft Trusted Signing (pending Azure Identity Validation)
- Winget and Microsoft Store distribution
- Custom domain for the landing page

---

## [0.1.0-alpha] — 2026-05-17

First public alpha. Core modules are implemented, the test suite is green on the C++ engine,
and the WinUI 3 configuration UI is in active debugging. This release is intended for early
testers who can tolerate rough edges and provide structured feedback via the
[beta feedback form](https://github.com/Iajensen222222/AuraShell/discussions).

### Added

**Audio Visualizer**
- Real-time WASAPI loopback audio capture
- 128-band FFT spectrum analysis at < 50 ms input-to-pixel latency
- DirectX 11 overlay rendering at 60 FPS
- Adaptive FPS scaling under CPU/GPU load

**Desktop Icon Changer**
- Per-shortcut and per-folder icon replacement
- Drag-and-drop icon assignment in the configuration UI
- Full rollback — original icons restored by "Restore Defaults" or uninstall

**Taskbar Animations**
- Kinetic spring physics on taskbar icon hover
- Scale and opacity effects, opt-in per workspace
- Idle CPU < 1% when animations are inactive

**Workspace Theming**
- Per-virtual-desktop visual identity (color scheme, accent, overlay style)
- Theme switching on desktop change event
- Registry-backed persistence with automatic backup/restore

**WinUI 3 Configuration UI**
- Five pages: Dashboard, Visuals, Behavior, Desktop Items, Settings
- Native Windows 11 look (WinUI 3, Windows App SDK 2.0)
- Acrylic materials and Fluent Design system

**Core Infrastructure**
- Named-pipe IPC between the UI and the optional elevated service
- spdlog-backed logging to `%LOCALAPPDATA%\AuraShell\logs\` with 10 MB rotation
- RAII-wrapped Win32 handles throughout — no raw handle leaks
- 22 Catch2 unit tests + 1 integration test (C++ engine)
- Inno Setup installer with silent-install support (`/VERYSILENT`)
- One-click uninstaller removes all files and registry keys

### Known limitations

- **WinUI app exits silently on some configurations** — under active investigation. If you
  experience this, run `%LOCALAPPDATA%\AuraShell\winui_crash.log` after attempting to launch
  and include it in your bug report.
- Binaries are currently unsigned. Windows SmartScreen will show a "Windows protected your PC"
  warning on first run — click "More info → Run anyway." Code signing via Microsoft Trusted
  Signing is in progress and will land before v0.2.0.
- The Microsoft Store and winget packages are not yet available (gated on a signed installer).
- The audio visualizer requires WASAPI loopback to be enabled (Windows 11 default — no action
  needed on most systems).

### System requirements

- Windows 11 version 22H2 (build 22621) or later
- DirectX 11-capable GPU
- x64 processor
- 200 MB disk space, 200 MB RAM typical

---

[Unreleased]: https://github.com/Iajensen222222/AuraShell/compare/v0.1.0-alpha...HEAD
[0.1.0-alpha]: https://github.com/Iajensen222222/AuraShell/releases/tag/v0.1.0-alpha
