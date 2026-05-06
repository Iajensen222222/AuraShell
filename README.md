# AuraShell - Windows 11 Customization Suite

![Version](https://img.shields.io/badge/version-0.1.0-blue)
![C++](https://img.shields.io/badge/C%2B%2B-20-brightgreen)
![Platform](https://img.shields.io/badge/platform-Windows%2011-0078d4)
![Build System](https://img.shields.io/badge/build-CMake-064f8c)

**AuraShell** is a high-performance, modular Windows 11 customization utility that gives users granular control over OS shell appearance and behavior. It features fluid animations, modern UI principles like Glassmorphism, and workflow-driven personalization.

## Quick Links

- **[Architecture Overview](./docs/ARCHITECTURE.md)** - Modular design and layered architecture
- **[Implementation Roadmap](./PLAN.md)** - Sprint breakdown and timeline
- **[System Instructions](./CLAUDE.md)** - Coding standards and constraints
- **[Build Guide](#build-quick-start)** - How to compile from source

## Features (v0.1.0 Roadmap)

### Sprint 1-2: Audio Visualizer
- Real-time audio capture via WASAPI
- 128-band FFT spectrum analysis (< 50ms latency)
- DirectX 11 overlay rendering
- Adaptive FPS scaling based on CPU/GPU load
- Custom particle effects and color gradients

### Sprint 3: Taskbar Enhancements
- Hover scale animation on taskbar icons
- Custom cursor trails with motion blur
- Acrylic (glass) blur effect overlays
- Icon customization and replacement
- Smooth easing curve animations (ease-out-quad, cubic bezier)

### Sprint 4: Workspace Theming
- Virtual desktop detection and per-workspace themes
- Theme switching < 500ms
- Registry-based customization
- JSON configuration hot-reload
- Preset themes (Gaming, Cinematic, Productivity)

### Sprint 5: Polish & Release
- Comprehensive test suite
- Performance profiling and optimization (< 1% idle CPU)
- Windows installer (.msi)
- Complete documentation

## Architecture Highlights

```
AuraShell.exe (Main GUI)
    ↓
libaurashell.dll (Core Library)
    ├─ Platform Layer (Win32 abstractions)
    ├─ Graphics Layer (DirectX 11)
    ├─ Audio Layer (WASAPI, FFT)
    ├─ Config Layer (JSON, hot-reload)
    └─ Logging Layer (spdlog)
    
Feature Modules:
    ├─ Taskbar Engine
    ├─ Visual Enhancements (overlays, effects)
    ├─ Context Tools (menus, workspaces)
    └─ System Integration (DLL injection, elevation)
```

**Key Design Decisions:**
- ✅ **Hybrid Architecture**: In-process shared library + optional service for elevation
- ✅ **Modular**: Each feature can be developed and tested independently
- ✅ **Performance-Aware**: Adaptive FPS/latency scaling, < 1% CPU when idle
- ✅ **Safe Injection**: Test with dummy targets (Notepad) before explorer.exe
- ✅ **RAII**: All Win32 handles wrapped in smart pointers
- ✅ **Explicit Error Handling**: Every HRESULT checked, no silent failures

---

## Build Quick-Start Guide

### Prerequisites

**Required:**
- **Visual Studio 2022** (or MSVC v143 compiler)
- **CMake 3.22+** ([Download](https://cmake.org/download/))
- **Windows 11 SDK 22621+** (included with Visual Studio)
- **Git** ([Download](https://git-scm.com/))

**Optional:**
- **Python 3.9+** (for shader compilation scripts)
- **Windows Sandbox** (for safe DLL injection testing)

### Step 1: Install vcpkg (C++ Package Manager)

vcpkg manages our dependencies: C++/WinRT, spdlog, nlohmann/json.

```bash
# Clone vcpkg repository (one-time setup)
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg

# Bootstrap vcpkg
.\bootstrap-vcpkg.bat

# Add to system PATH (optional, for convenience)
# Add C:\vcpkg to your PATH environment variable
```

Verify installation:
```bash
.\vcpkg --version
```

### Step 2: Clone AuraShell Repository

```bash
git clone https://github.com/Iajensen222222/AuraShell.git
cd AuraShell
```

### Step 3: Configure CMake (Generate Build System)

Run CMake to generate Visual Studio project files:

**Option A: Command Prompt**
```cmd
mkdir build
cd build

cmake .. ^
  -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\cmake.toolchain.cmake" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -G "Visual Studio 17 2022"
```

**Option B: PowerShell**
```powershell
mkdir build
cd build

cmake .. `
  -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\cmake.toolchain.cmake" `
  -DCMAKE_BUILD_TYPE=Release `
  -G "Visual Studio 17 2022"
```

**Option C: CMake GUI**
1. Open CMake GUI (`cmake-gui`)
2. Set source directory: `C:\Users\YourName\AuraShell`
3. Set build directory: `C:\Users\YourName\AuraShell\build`
4. Click "Configure"
5. Select "Visual Studio 17 2022" generator
6. Set `CMAKE_TOOLCHAIN_FILE` to `C:\vcpkg\scripts\buildsystems\cmake.toolchain.cmake`
7. Click "Generate"

### Step 4: Build the Project

```bash
# Build all targets (Release mode, optimized)
cmake --build build --config Release --parallel 4

# Build all targets (Debug mode)
cmake --build build --config Debug --parallel 4
```

**Build Output**:
- `build/bin/Release/AuraShell.exe` - Main GUI application
- `build/bin/Release/libaurashell.dll` - Core shared library
- `build/bin/Release/taskbar_injected.dll` - Injected DLL (for explorer.exe)
- `build/bin/Release/AuraShellService.exe` - Optional service (admin)

### Step 5: Run Sample Applications

```bash
# Audio Visualizer Sample - Watch frequency bars animate with music
.\build\bin\Release\samples\05_audio_visualizer.exe

# Play music (Spotify, YouTube, etc.) and watch the visualization!
```

---

## Build Troubleshooting

### Error: "Could not find vcpkg"
**Solution**: Ensure vcpkg toolchain path is correct in CMake command:
```bash
-DCMAKE_TOOLCHAIN_FILE="C:\path\to\vcpkg\scripts\buildsystems\cmake.toolchain.cmake"
```

### Error: "DirectX 11 headers not found"
**Solution**: DirectX headers come with Windows SDK 22621+. In Visual Studio Installer → Modify → Individual Components:
- ✅ MSVC v143 (latest)
- ✅ Windows 11 SDK (version 22621)
- ✅ C++ CMake tools for Windows

### Error: "Fatal error LNK1104: cannot open file 'spdlog.lib'"
**Solution**: vcpkg dependency not installed. Install manually:
```bash
.\vcpkg install cppwinrt nlohmann-json spdlog --triplet x64-windows
```

---

## Documentation

- **[CLAUDE.md](./CLAUDE.md)** - System instructions, coding standards, performance rules
- **[PLAN.md](./PLAN.md)** - Sprint roadmap, IPC message flows, implementation schedule
- **[docs/ARCHITECTURE.md](./docs/ARCHITECTURE.md)** - Detailed architecture overview
- **[docs/PERFORMANCE_GUIDE.md](./docs/PERFORMANCE_GUIDE.md)** - Performance optimization tips

---

## Development Workflow

### Sprint 1: Workspace Foundation
```bash
# 1. Verify build system works
cmake --build build --config Release

# 2. Run logging test
.\build\bin\Release\tests\test_logging.exe

# 3. Run IPC handshake test
.\build\bin\Release\samples\01_ipc_handshake.exe
```

### Sprint 2: Audio Visualizer
```bash
# 1. Test FFT spectrum analyzer
.\build\bin\Release\samples\02_spectrum_analyzer_test.exe

# 2. Run audio visualizer overlay
.\build\bin\Release\samples\05_audio_visualizer.exe
```

### Sprint 3: Taskbar Effects
```bash
# 1. Test hover detection (SAFE: uses Notepad dummy target)
.\build\bin\Release\samples\04_taskbar_hover.exe --target notepad.exe

# 2. Only after successful dummy testing, try explorer.exe (in VM)
```

---

## Performance Targets

| Metric | Target | Monitor |
|--------|--------|---------|
| Idle CPU | < 1% | Task Manager (Details) |
| Audio Visualizer FPS | 60 | Console output |
| Hover Latency | < 16ms | Logs in `%LOCALAPPDATA%\AuraShell\logs\` |
| Theme Apply Time | < 500ms | Performance logs |

---

## Support

- **Issues**: [GitHub Issues](https://github.com/Iajensen222222/AuraShell/issues)
- **Discussions**: [GitHub Discussions](https://github.com/Iajensen222222/AuraShell/discussions)
- **Documentation**: [./docs/](./docs/)

---

## License

MIT License - See LICENSE file for details.

---

**Version**: 0.1.0 (Alpha) | **Last Updated**: 2026-04-29 | **Target Release**: Week 12

**Ready to build? Follow the [Build Quick-Start Guide](#build-quick-start) above! 🚀**
