# PLAN.md - AuraShell Implementation Roadmap

## Executive Summary

AuraShell is a modular Windows 11 customization suite built in C++20. This document outlines the 5-sprint implementation schedule, with each sprint focused on specific deliverables, dependencies, and integration points.

**Total Timeline**: 10-12 weeks  
**Team**: Solo development (or small team)  
**Build System**: CMake 3.22+ with vcpkg  
**Dependencies**: C++/WinRT, spdlog, nlohmann/json, DirectX 11 SDK

---

## Sprint Structure Overview

```
Week 1-2:   Sprint 1 - Workspace Foundation
Week 3-4:   Sprint 2 - Audio Visualizer
Week 5-6:   Sprint 3 - Taskbar Hooking & Effects
Week 7-8:   Sprint 4 - Workspace Theming
Week 9-10:  Sprint 5 - Testing & Polish
Week 11-12: Release Prep & Documentation
```

---

## Sprint 1: Workspace Foundation & Logging System (Weeks 1-2)

### Objectives
- [ ] CMake build system fully configured
- [ ] vcpkg dependency resolution working
- [ ] Core logging infrastructure (spdlog) integrated
- [ ] IPC named pipe handshake working
- [ ] Platform abstraction layer foundation complete
- [ ] First successful build of all targets

### Deliverables

#### 1.1 Build System Configuration
**Files**: `CMakeLists.txt`, `vcpkg.json`, `cmake/*.cmake`

```cmake
# CMakeLists.txt (root)
cmake_minimum_required(VERSION 3.22)
project(AuraShell VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg integration
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    include(${CMAKE_TOOLCHAIN_FILE})
endif()

# Platform-specific settings
if(MSVC)
    add_compile_options(/W4 /WX /permissive-)  # Warnings as errors
    add_compile_options(/MP)                    # Multiprocessor compilation
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2")
endif()

# Subdirectories
add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(samples)

# Installation
install(DIRECTORY data/themes DESTINATION bin/data)
install(DIRECTORY docs DESTINATION .)
```

**vcpkg.json**:
```json
{
  "name": "aurashell",
  "version-string": "0.1.0",
  "dependencies": [
    "cppwinrt",
    "spdlog",
    "nlohmann-json"
  ],
  "features": {
    "tests": {
      "description": "Build unit and integration tests",
      "dependencies": ["catch2"]
    }
  }
}
```

#### 1.2 Logging Infrastructure
**Files**: `src/core/logging/logger.h/cpp`, `src/core/logging/performance_logger.h/cpp`

**Features**:
- spdlog integration with file rotation (10 MB, 5 files max)
- Console sink for debug builds, file sink for all builds
- Module-based logging prefixes (taskbar, visual, audio, config)
- Performance metrics tracking (FPS, CPU%, GPU%, memory)
- Thread-safe async logging

**Deliverable Code**:
```cpp
// logging/logger.h
namespace aura::logging {

class Logger {
public:
    static Logger& getInstance();
    
    void info(const std::string& module, const std::string& msg);
    void debug(const std::string& module, const std::string& msg);
    void warn(const std::string& module, const std::string& msg);
    void error(const std::string& module, const std::string& msg);
    
private:
    Logger();
    std::shared_ptr<spdlog::logger> m_logger;
};

#define AURA_LOG_DEBUG(msg, ...) \
    aura::logging::Logger::getInstance().debug(__FUNCTION__, fmt::format(msg, __VA_ARGS__))
#define AURA_LOG_INFO(msg, ...) \
    aura::logging::Logger::getInstance().info(__FUNCTION__, fmt::format(msg, __VA_ARGS__))
#define AURA_LOG_ERROR(msg, ...) \
    aura::logging::Logger::getInstance().error(__FUNCTION__, fmt::format(msg, __VA_ARGS__))

} // namespace aura::logging
```

#### 1.3 IPC Named Pipe Handshake
**Files**: `src/core/platform/ipc/named_pipe.h/cpp`, `src/core/platform/ipc/message_types.h`

**Requirements**:
- Named pipe server in main app (listens on `\\.\pipe\AuraShell_Control`)
- Named pipe client in service/other modules (connects and sends messages)
- Async I/O (no blocking on main thread)
- Message queue with sequence number tracking
- Timeout handling (5 second default)

**IPC Message Types**:
```cpp
// message_types.h
namespace aura::ipc {

enum class MessageType : uint32_t {
    // Connection
    HANDSHAKE_REQUEST = 0x0001,
    HANDSHAKE_RESPONSE = 0x0002,
    
    // Config
    CONFIG_RELOAD = 0x0010,
    CONFIG_APPLY = 0x0011,
    
    // Theme
    THEME_CHANGE = 0x0020,
    
    // Query
    QUERY_STATE = 0x0030,
    
    // Control
    ENABLE_FEATURE = 0x0040,
    DISABLE_FEATURE = 0x0041,
};

struct Message {
    uint32_t messageType;
    uint32_t sequenceNumber;
    uint32_t payloadSize;
    uint8_t payload[2048];
};

} // namespace aura::ipc
```

**Test**: Write a simple test (`samples/01_ipc_handshake/`) that:
1. Launches main app (IPC server)
2. Connects from test client
3. Sends HANDSHAKE_REQUEST
4. Verifies HANDSHAKE_RESPONSE received

**Success Criteria**: IPC messages round-trip in < 10ms

#### 1.4 Platform Abstraction Layer - Foundation
**Files**: `src/core/platform/window_manager.h/cpp`, `src/core/platform/system_metrics.h/cpp`, `src/core/platform/dpi_awareness.h/cpp`

**window_manager.h - Minimal API**:
```cpp
namespace aura::platform {

class WindowManager {
public:
    static WindowManager& getInstance();
    
    // Window enumeration
    HWND findWindowByClass(const std::wstring& className);
    HWND findWindowByTitle(const std::wstring& titlePattern);
    std::vector<HWND> enumAllWindows();
    
    // Window properties
    std::wstring getWindowTitle(HWND hwnd);
    std::wstring getWindowClass(HWND hwnd);
    bool isWindowVisible(HWND hwnd);
    RECT getWindowRect(HWND hwnd);
    
    // DPI awareness
    uint32_t getDpiForWindow(HWND hwnd);
    float getDpiScaleFactor(HWND hwnd);
    
private:
    WindowManager();
    std::unordered_map<HWND, WindowInfo> m_windowCache;
};

} // namespace aura::platform
```

**dpi_awareness.h**:
```cpp
namespace aura::platform {

class DpiAwareness {
public:
    static void enablePerMonitorV2();
    static uint32_t getDpiForMonitor(HMONITOR hMonitor);
    static float getScaleFactor(HMONITOR hMonitor);
    
private:
    // Use GetDpiForMonitor API (PerMonitorV2)
};

} // namespace aura::platform
```

#### 1.5 Common Types Header
**Files**: `src/core/include/aurashell/common.h`

Define foundational types used across all modules:
```cpp
namespace aura {

// Color representation (RGBA)
struct RGBAColor {
    uint8_t r, g, b, a;
};

// 2D vector
struct Vector2D {
    float x, y;
    Vector2D operator+(const Vector2D& other) const { return {x + other.x, y + other.y}; }
    Vector2D operator*(float scalar) const { return {x * scalar, y * scalar}; }
};

// Animation state
struct AnimationState {
    float progress;         // 0.0 to 1.0
    uint64_t startTimeMs;
    uint64_t durationMs;
};

// Performance metrics
struct PerformanceMetrics {
    float fps;
    float cpuUsagePercent;
    float gpuUsagePercent;
    uint64_t memoryUsageMB;
};

} // namespace aura
```

### Sprint 1 Testing
- [ ] Verify CMake build succeeds: `cmake --build build --config Release`
- [ ] Verify all targets build: AuraShell.exe, libaurashell.dll, test executables
- [ ] Run logging test: write 1000 log entries, verify file rotation works
- [ ] Run IPC handshake test: connect, send message, receive response within 5ms
- [ ] Check idle CPU usage: < 1% when app idle with no overlays

### Sprint 1 Exit Criteria
- [ ] CI/CD pipeline (GitHub Actions) builds successfully
- [ ] All logging infrastructure working
- [ ] IPC handshake proven in sample code
- [ ] Platform layer WndManager functional
- [ ] Ready for Sprint 2 (audio layer development)

---

## Sprint 2: Audio Visualizer - WASAPI & DirectX Overlay (Weeks 3-4)

### Objectives
- [ ] WASAPI loopback audio capture working
- [ ] FFT spectrum analysis (128 frequency bands)
- [ ] DirectX 11 overlay window creation
- [ ] Basic bar visualization rendering
- [ ] Adaptive FPS scaling based on CPU/GPU load
- [ ] Audio visualizer sample working (samples/05_audio_visualizer/)

### Deliverables

#### 2.1 WASAPI Audio Capture Layer
**Files**: `src/core/audio/audio_engine.h/cpp`, `src/core/audio/audio_capture.h/cpp`

**Features**:
- Loopback audio capture (system audio, no microphone)
- 48kHz sample rate, 16-bit PCM
- Async capture thread (not blocking render thread)
- Circular buffer (2 seconds of audio history)

**API**:
```cpp
namespace aura::audio {

class AudioEngine {
public:
    static AudioEngine& getInstance();
    
    void initialize();
    void shutdown();
    bool isInitialized() const;
    
    // Audio data access
    const float* getFrequencyBands() const;  // Returns 128 float values
    uint32_t getNumBands() const { return 128; }
};

class AudioCapture {
private:
    // WASAPI device enumeration, format negotiation
    // Loopback capture thread
    std::thread m_captureThread;
    
    void captureThread();
};

} // namespace aura::audio
```

**Sample Output**:
```
Frequency bands (Hz):
[0]: 0-20 Hz (sub-bass) → value 0.45
[1]: 20-40 Hz (bass) → value 0.67
...
[127]: 10000-20000 Hz (treble) → value 0.12
```

#### 2.2 Spectrum Analyzer (FFT)
**Files**: `src/core/audio/spectrum_analyzer.h/cpp`

**Requirements**:
- Input: Raw PCM audio (2048 samples ≈ 42.67ms window @ 48kHz)
- Output: 128 logarithmically-spaced frequency bands
- Window function: Hann window (reduces spectral leakage)
- FFT algorithm: Radix-2 or library-based (consider Kiss FFT or built-in)

**API**:
```cpp
namespace aura::audio {

class SpectrumAnalyzer {
public:
    static SpectrumAnalyzer& getInstance();
    
    void initialize(uint32_t sampleRate);
    
    // Call from audio capture thread
    void analyzeSamples(const int16_t* samples, uint32_t numSamples);
    
    // Call from render thread (non-blocking)
    std::array<float, 128> getFrequencyBands() const;
    
private:
    void performFFT();
    void computeFrequencyBands();
    
    // FFT data
    std::vector<float> m_fftInput;
    std::vector<std::complex<float>> m_fftOutput;
};

} // namespace aura::audio
```

**Modular Debugging** (before integration):
Create `samples/02_spectrum_analyzer_test/` - standalone FFT verification:
```cpp
// Test: Load WAV file, compute FFT, compare output to reference
// Verify magnitude response, frequency accuracy, latency

// Input: sample_music.wav (1 second @ 48kHz)
// Output: frequency_bands_[0-127].txt (expected values)
```

#### 2.3 DirectX 11 Overlay Window
**Files**: `src/core/graphics/dx11_renderer.h/cpp`, `src/core/graphics/renderer.h`, `src/visual_enhancements/overlay_manager.h/cpp`

**Features**:
- Borderless, topmost overlay window
- DirectX 11 device and immediate context
- Swap chain with DXGI
- Render target creation
- PerMonitorV2 DPI awareness

**API**:
```cpp
namespace aura::graphics {

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void initialize() = 0;
    virtual void renderFrame(const RenderContext& ctx) = 0;
    virtual void shutdown() = 0;
};

class DX11Renderer : public Renderer {
public:
    void initialize() override;
    void renderFrame(const RenderContext& ctx) override;
    void shutdown() override;
    
    ID3D11Device* getDevice() { return m_device.Get(); }
    ID3D11DeviceContext* getContext() { return m_context.Get(); }
    
private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
};

} // namespace aura::graphics

namespace aura::visual {

class OverlayManager {
public:
    static OverlayManager& getInstance();
    
    void initialize();
    void shutdown();
    void renderFrame();
    
    void setEnabled(bool enabled);
    bool isEnabled() const;
    
private:
    std::unique_ptr<Renderer> m_renderer;
    HWND m_overlayWindow;
};

} // namespace aura::visual
```

#### 2.4 Audio Visualizer Rendering
**Files**: `src/visual_enhancements/audio_visualizer.h/cpp`, `src/core/graphics/shaders/visualizer.hlsl`

**Visual Design**:
- 128 vertical bars, evenly spaced across screen width
- Color gradient: Blue (bass) → Purple (mids) → Red (treble)
- Height proportional to frequency magnitude
- Smooth interpolation between frames (easing)

**Pixel Shader (HLSL)**:
```hlsl
// visualizer.hlsl
cbuffer ThemeConstantBuffer : register(b0) {
    float4 colorLow;    // Blue for bass
    float4 colorMid;    // Purple for mids
    float4 colorHigh;   // Red for treble
    float barHeight[128];
};

float4 main(float2 uv : TEXCOORD0) : SV_TARGET {
    // Map UV.x to bar index (0-127)
    int barIndex = int(uv.x * 128.0);
    
    // Get bar height at this position
    float height = barHeight[barIndex];
    
    // Determine color based on frequency band
    float4 color;
    if (barIndex < 42) {
        // Bass (0-20Hz) - Blue
        color = colorLow;
    } else if (barIndex < 85) {
        // Mids (20-2000Hz) - Purple
        color = colorMid;
    } else {
        // Treble (2000-20000Hz) - Red
        color = colorHigh;
    }
    
    // Alpha based on bar height
    color.a = height;
    return color;
}
```

**C++ Integration**:
```cpp
namespace aura::visual {

class AudioVisualizer {
public:
    void initialize(Renderer* renderer);
    void renderFrame(const std::array<float, 128>& frequencyBands);
    
private:
    void updateShaderConstants(const std::array<float, 128>& bands);
    
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11Buffer> m_constantBuffer;
};

} // namespace aura::visual
```

#### 2.5 Adaptive FPS Scaling
**Files**: `src/core/logging/performance_logger.h/cpp`

**Algorithm**:
```cpp
class PerformanceMonitor {
public:
    float getTargetFps() const {
        float cpuUsage = getCpuUsagePercent();
        float gpuUsage = getGpuUsagePercent();
        float maxUsage = std::max(cpuUsage, gpuUsage);
        
        if (maxUsage < 30.0f) return 60.0f;        // Plenty of headroom
        if (maxUsage < 60.0f) return 45.0f;        // Moderate load
        if (maxUsage < 85.0f) return 30.0f;        // High load
        return 15.0f;                               // Very high load
    }
};
```

**Implementation**:
```cpp
void OverlayManager::renderLoop() {
    while (m_running) {
        auto targetFps = m_performanceMonitor.getTargetFps();
        auto frameTimeMs = 1000.0f / targetFps;
        
        auto frameStart = std::chrono::high_resolution_clock::now();
        
        // Render frame
        m_renderer->renderFrame(...);
        
        // Sleep to maintain target FPS
        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto frameElapsedMs = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
        auto sleepTimeMs = std::max(0.0f, frameTimeMs - frameElapsedMs);
        
        if (sleepTimeMs > 0) {
            std::this_thread::sleep_for(std::chrono::duration<float, std::milli>(sleepTimeMs));
        }
    }
}
```

### Sprint 2 Testing
- [ ] Audio capture sample runs, outputs frequency bands to console
- [ ] FFT verification: Load WAV file, compare magnitude response to expected
- [ ] Overlay window appears on screen (full-screen, topmost)
- [ ] Audio visualizer bars move with music (verify latency < 50ms)
- [ ] CPU usage when visualizer running: < 5% (1-2% if adaptive scaling working)
- [ ] GPU usage: < 10%

### Sprint 2 Exit Criteria
- [ ] `samples/05_audio_visualizer/` works end-to-end
- [ ] Audio spectrum analysis accurate and responsive
- [ ] Overlay renders without artifacts
- [ ] Adaptive FPS scaling working (confirmed via logs)
- [ ] Ready for Sprint 3 (taskbar hooking)

---

## Sprint 3: Taskbar Hooking & Acrylic Effects (Weeks 5-6)

### Objectives
- [ ] Taskbar window detection and enumeration
- [ ] Mouse hover detection over taskbar items
- [ ] Animation interpolation engine (easing curves)
- [ ] Icon scaling on hover
- [ ] Acrylic blur effect shader
- [ ] DLL injection framework (into dummy target first)

### Key Deliverables

#### 3.1 Taskbar Window Detection
**Files**: `src/taskbar_engine/taskbar_manager.h/cpp`, `src/taskbar_engine/taskbar_types.h`

**Requirements**:
- Find Shell_TrayWnd (taskbar window) using FindWindowW
- Enumerate taskbar icons programmatically
- Cache taskbar geometry on system change notification

#### 3.2 Hover Detection & Animation Engine
**Files**: `src/taskbar_engine/hover_detector.h/cpp`, `src/taskbar_engine/animation_engine.h/cpp`

**Hover Detection**:
```cpp
// Poll mouse position, check if over taskbar bounding box
// Trigger animation state machine on enter/exit
```

**Animation Engine - Easing Curves**:
```cpp
class AnimationEngine {
public:
    // Common easing functions
    float easeOutQuad(float t);      // t^2 deceleration
    float easeInOutCubic(float t);   // Smooth start/stop
    float easeOutElastic(float t);   // Bouncy effect
    
    // Interpolation
    float interpolate(float from, float to, float t, EasingType easing);
};
```

#### 3.3 Icon Replacement Hook
**Files**: `src/taskbar_engine/icon_replacer.h/cpp`, `src/taskbar_engine/taskbar_injected.cpp` (DLL entry)

**DLL Injection Strategy** (START WITH DUMMY TARGET):
1. First test: Inject into notepad.exe (harmless)
2. Verify hook works on dummy
3. Then: Test with explorer.exe (in isolated VM or sandbox)

#### 3.4 Acrylic Blur Shader
**Files**: `src/core/graphics/shaders/blur.hlsl`, `src/visual_enhancements/blur_effect.h/cpp`

Implement Gaussian blur for glassmorphism:
```hlsl
// blur.hlsl - Separable Gaussian blur
float4 main(float2 uv : TEXCOORD0) : SV_TARGET {
    float4 result = float4(0, 0, 0, 0);
    
    // Sample neighboring pixels with Gaussian weights
    for (int i = -5; i <= 5; i++) {
        float2 sampleUV = uv + float2(i * pixelSize, 0);
        float weight = GaussianWeight(i, 2.0);  // Sigma = 2.0
        result += tex2D(inputTexture, sampleUV) * weight;
    }
    
    return result;
}
```

### Sprint 3 Testing
- [ ] `samples/04_taskbar_hover/` successfully detects taskbar
- [ ] Mouse position polling: < 50ms latency
- [ ] Animation interpolation: smooth 60fps @ 60Hz
- [ ] Dummy target DLL injection: successful on notepad.exe
- [ ] **NO explorer.exe injection yet - save for Sprint 3 final or Sprint 4**

### Sprint 3 Exit Criteria
- [ ] Taskbar detection working on Windows 11
- [ ] Hover detection framework in place
- [ ] Animation engine with easing curves verified
- [ ] Blur shader working with sample images
- [ ] Dummy target DLL injection proven safe
- [ ] Ready for Sprint 4 (workspace theming)

---

## Sprint 4: Workspace & Virtual Desktop Theming (Weeks 7-8)

### Objectives
- [ ] Virtual desktop detection and tracking
- [ ] Theme application per workspace
- [ ] Registry-based icon substitution
- [ ] Global theme switching
- [ ] Config hot-reload from JSON

### Deliverables

#### 4.1 Virtual Desktop Manager
**Files**: `src/context_tools/workspace_manager.h/cpp`

Detect workspace switches via WM_DISPLAYCHANGE or Virtual Desktop Manager API

#### 4.2 Theme Application Engine
**Files**: `src/context_tools/theme_applier.h/cpp`

Apply color palettes, accent colors, icon substitutions globally

#### 4.3 Config Hot-Reload
**Files**: `src/core/config/change_observer.h/cpp`

File watcher detects JSON changes, broadcasts to all modules

### Sprint 4 Exit Criteria
- [ ] Switch virtual desktops, theme auto-applies
- [ ] Theme JSON hot-reload working (< 500ms apply time)
- [ ] Registry icon substitution proven (in VM)
- [ ] Ready for Sprint 5 (testing & polish)

---

## Sprint 5: Integration Testing, Polish & Release Prep (Weeks 9-10)

### Objectives
- [ ] End-to-end integration tests (all modules working together)
- [ ] Performance profiling (CPU < 1% idle, FPS stable)
- [ ] Installer creation (.msi with vcpkg deps bundled)
- [ ] Documentation complete
- [ ] Release build tested

### Deliverables

#### 5.1 Integration Test Suite
**Files**: `tests/integration/*.cpp`

- Theme application + overlay rendering + audio viz
- Workspace switch + theme change
- DLL injection + hover effects
- Config hot-reload + all modules update

#### 5.2 Performance Profiling
Use Windows Performance Analyzer (WPA) or built-in logging

#### 5.3 Installer
Create .msi installer with:
- Visual C++ redistributable bundled
- vcpkg dependencies bundled
- Registry cleanup on uninstall

### Sprint 5 Exit Criteria
- [ ] All integration tests passing
- [ ] CPU usage < 1% when idle
- [ ] Installer builds successfully
- [ ] Documentation complete
- [ ] **Release v0.1.0 ready**

---

## IPC Message Flow Blueprint

### Architecture Overview

```
Main Application (AuraShell.exe)
├── IPC Server (listen on \\.\pipe\AuraShell_Control)
├── Config Manager (JSON file monitoring)
├── Feature Modules (Taskbar, Visual, Audio, Context)
└── GUI / System Tray

        ↔ Named Pipe IPC (async messages)

Service/Injected DLL (Optional)
├── Taskbar Injected DLL (in explorer.exe context)
├── Hook Handlers
└── Theme Application (registry changes)
```

### Message Flow: Theme Change Event

**Scenario**: User selects "Neon Gamer" theme in settings

```
1. User clicks "Apply Theme" button
   ↓
2. SettingsDialog → ConfigManager::loadTheme("neon_gamer")
   ↓
3. ConfigManager validates theme.json (schema check)
   ↓
4. ConfigManager broadcasts to all local modules:
   - Taskbar Engine: Update animation colors, speeds
   - Visual Enhancements: Update shader constants (colors, blur radius)
   - Audio Visualizer: Update frequency band colors
   ↓
5. Each module responds with status (ACK)
   ↓
6. ConfigManager writes to registry for persistence
   ↓
7. ConfigManager sends IPC message to Service (if running):
   
   MESSAGE STRUCTURE:
   {
     messageType: THEME_CHANGE (0x0020)
     sequenceNumber: 42
     payload: {
       themeName: "neon_gamer"
       colorPalette: [RGB triplets x 8]
       animationSpeed: 1.5x
       blurRadius: 12
     }
   }
   
   ↓
8. Service receives message, acknowledges
   ↓
9. Service updates injected taskbar DLL via shared memory
   ↓
10. Main app notifies UI: "Theme applied successfully"
   ↓
11. User sees updated visuals on screen (< 100ms latency)
```

### Message Flow: Config Reload (File Watcher Event)

**Scenario**: User externally edits `config.json` via text editor

```
1. File system change detected on config.json
   ↓
2. FileWatcher callback triggered in ConfigManager
   ↓
3. ConfigManager re-reads config.json from disk
   ↓
4. If valid:
   - Apply changes to memory
   - Broadcast to all modules
   - Log "Config reloaded"
   ↓
5. If invalid (parse error):
   - Rollback to previous config
   - Log error
   - Notify UI (config reload failed)
```

### Message Flow: Audio Visualizer Update

**Scenario**: Music is playing, visualizer updates in real-time

```
Timeline (60 FPS = 16.67ms per frame):

0ms:   Audio capture thread captures 2048 PCM samples
5ms:   SpectrumAnalyzer::analyzeSamples() computes FFT
10ms:  Frequency bands [0-127] ready for reading
12ms:  Render thread reads frequency bands (lock-free if possible)
14ms:  AudioVisualizer::renderFrame() updates shader constants
16ms:  DirectX renders bars to overlay
17ms:  Frame ready for presentation (next vsync)
~20ms: Frame appears on screen (accounting for monitor latency)

Total latency: ~15-20ms (acceptable for audio visualization)
```

### Message Flow: Taskbar Hover Effect

**Scenario**: User hovers mouse over taskbar icon

```
Timeline (60 FPS = 16.67ms per frame):

0ms:   HoverDetector polls GetCursorPos()
1ms:   Compare with taskbar bounding box
2ms:   Mouse over icon detected → AnimationEngine::start()
3ms:   Animation state: progress = 0%

16ms:  Animation state: progress = ~25% (200ms total duration)
       Icon scale: 1.0 + (0.2 * 0.25) = 1.05
       
33ms:  Animation state: progress = ~50%
       Icon scale: 1.0 + (0.2 * 0.5) = 1.10
       
50ms:  Animation state: progress = ~75%
       Icon scale: 1.0 + (0.2 * 0.75) = 1.15
       
67ms:  Animation state: progress = ~100%
       Icon scale: 1.0 + (0.2 * 1.0) = 1.20 (fully scaled)

User moves mouse away:

70ms:  AnimationEngine::reverse()
       Progress now decreases from 100% to 0%
       
200ms: Animation complete, scale back to 1.0

Total user perception latency: 1-2ms (imperceptible)
Smooth animation: 200ms easing (ease-out-quad)
```

### Named Pipe Message Protocol

**Connection Handshake**:
```cpp
// Client connects to \\.\pipe\AuraShell_Control
// Client sends:
{
  messageType: HANDSHAKE_REQUEST (0x0001)
  sequenceNumber: 1
  payload: { clientPID, clientVersion }
}

// Server responds:
{
  messageType: HANDSHAKE_RESPONSE (0x0002)
  sequenceNumber: 1
  payload: { serverVersion, acceptedFeatures }
}
```

**Message Timeout Handling**:
```cpp
// If client doesn't receive response within 5 seconds:
// - Log warning
// - Retry once
// - If still no response, fail gracefully (feature disabled)

// If server can't send response:
// - Log error
// - Queue message for retry
// - Notify admin (via event log)
```

**Priority Levels**:
```cpp
enum class MessagePriority {
    CRITICAL = 0,      // Config changes (immediate)
    HIGH = 1,           // Theme changes
    NORMAL = 2,         // Status queries
    LOW = 3,            // Performance data logging
};
```

---

## Dependency Map

```
Applications:
├── AuraShell.exe (Main GUI)
│   └── libaurashell.dll (Core)
│       ├── platform/ (Win32 wrappers)
│       ├── graphics/ (DirectX)
│       ├── audio/ (WASAPI)
│       ├── config/ (JSON)
│       └── logging/ (spdlog)
│
├── AuraShellService.exe (Optional, admin)
│   └── libaurashell.dll (same as above)
│
└── taskbar_injected.dll (Injected into explorer.exe)
    └── Limited set of core functions (platform, logging)

External Dependencies (via vcpkg):
├── cppwinrt (Windows Runtime bindings)
├── spdlog (Logging)
└── nlohmann/json (JSON parsing)

System Dependencies:
├── Windows SDK 22621+
├── DirectX 11 Runtime
└── WASAPI (audio)
```

---

## Build Command Quick Reference

### Clean Build
```bash
cd AuraShell
rm -rf build
cmake -B build `
  -DCMAKE_TOOLCHAIN_FILE="C:\path\to\vcpkg\scripts\buildsystems\cmake.toolchain.cmake" `
  -DCMAKE_BUILD_TYPE=Release `
  -G "Visual Studio 17 2022"
cmake --build build --config Release --parallel 4
```

### Incremental Build
```bash
cmake --build build --config Release --parallel 4
```

### Run Tests
```bash
ctest --build-config Release --output-on-failure
```

### Run Audio Visualizer Sample
```bash
.\build\bin\Release\samples\05_audio_visualizer.exe
```

---

## Risk Mitigation

| Risk | Mitigation |
|------|-----------|
| explorer.exe crashes on DLL injection | Test on dummy target first, use sandbox for explorer.exe testing |
| Audio latency > 50ms | Profile FFT computation, consider GPU-based FFT |
| DirectX overlay flicker | Use vsync, double-buffering, proper present timing |
| Registry corruption | Backup registry before modifications, implement rollback logic |
| Performance degradation | Continuous CPU/GPU monitoring, adaptive FPS scaling |
| IPC message loss | Sequence numbers, retry logic, timeout handling |

---

## Success Metrics

| Metric | Target | How to Measure |
|--------|--------|-----------------|
| Idle CPU usage | < 1% | Performance Monitor, custom logger |
| Audio visualizer latency | < 50ms | Timestamp in FFT input vs. visual output |
| Hover animation smoothness | 60 FPS | GPU counter, frame time logging |
| Theme apply time | < 500ms | Stopwatch + logging |
| DLL injection success rate | > 99% | Test 100 injections on dummy target |
| Code test coverage | > 80% | Codecov integration |

---

## Version History

- **v0.1.0** (Target: Week 12): Initial release with audio visualizer, taskbar hover, theme switching
- **v0.2.0** (Future): Advanced effects, workspace-specific themes, installer
- **v1.0.0** (Future): Stable API, full documentation, community contributions

---

**Document Version**: 1.0  
**Last Updated**: 2026-04-29  
**Next Review**: Weekly during sprints
