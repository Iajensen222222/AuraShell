# Next Step: Performance Logger Test Suite (TDD RED Phase 2)

**Objective**: Create comprehensive test suite for performance monitoring system  
**Framework**: Catch2 v3  
**Expected Duration**: 45-60 minutes  
**File to Create**: `tests/unit/test_performance_logger.cpp`

---

## Overview

The performance logger is responsible for:
1. **CPU usage monitoring** - Track process CPU % (target < 1% idle)
2. **FPS measurement** - Track rendered frames per second (60 FPS target)
3. **GPU usage** - Monitor graphics card utilization
4. **Memory tracking** - Monitor process memory consumption
5. **Adaptive scaling** - Calculate optimal FPS based on system load

---

## Test Suite Structure (15-20 tests)

### Group 1: Initialization & Configuration (3 tests)

```cpp
TEST_CASE("PerformanceLogger::Initialization", "[performance][logging]") {
    SECTION("Logger initializes successfully") {
        // Should create performance logger
        // Should start background monitoring thread
        // Should be ready to collect metrics
    }

    SECTION("Metrics are initially zero or baseline") {
        // getCpuUsagePercent() returns 0-100%
        // getFpsTarget() returns default (60 FPS)
        // getMemoryUsageMB() >= 0
    }

    SECTION("Performance thresholds are configurable") {
        // Set idle CPU threshold
        // Set FPS range (min/max)
        // Set memory limit
    }
}
```

### Group 2: CPU Usage Monitoring (3 tests)

```cpp
TEST_CASE("PerformanceLogger::CpuUsageMonitoring", "[performance][cpu]") {
    SECTION("CPU usage measurement returns valid percentage") {
        // Should return 0.0 to 100.0%
        // Should handle idle state (< 1%)
        // Should detect high load (> 80%)
    }

    SECTION("CPU usage is updated regularly") {
        // Call getCpuUsagePercent() twice with 500ms delay
        // Values should be different (unless perfectly stable)
        // Should reflect actual process CPU time
    }

    SECTION("Idle CPU usage is below threshold") {
        // When app idle (no rendering, no processing)
        // CPU should be < 1.0%
        // Log warning if exceeded
    }
}
```

### Group 3: FPS & Frame Rate (3 tests)

```cpp
TEST_CASE("PerformanceLogger::FrameRateTracking", "[performance][fps]") {
    SECTION("FPS measurement is accurate") {
        // Record frame times: 16ms, 16ms, 16ms (60 FPS)
        // getFps() should return ~60
        // Allow 2-3 FPS margin for timing variance
    }

    SECTION("Frame rate adapts to system load") {
        // Simulate CPU load < 30%: target 60 FPS
        // Simulate CPU load 30-60%: target 45 FPS
        // Simulate CPU load > 85%: target 15 FPS
        // getAdaptiveFpsTarget() changes accordingly
    }

    SECTION("FPS target respects min/max bounds") {
        // Minimum FPS: 1 fps (screensaver mode)
        // Maximum FPS: 144 fps (high-end gaming)
        // Should not exceed these bounds
    }
}
```

### Group 4: GPU & Memory Monitoring (3 tests)

```cpp
TEST_CASE("PerformanceLogger::ResourceMonitoring", "[performance][resources]") {
    SECTION("Memory usage is tracked") {
        // getMemoryUsageMB() returns MB count
        // Should be > 0
        // Should match process working set roughly
    }

    SECTION("GPU usage can be queried") {
        // getGpuUsagePercent() returns 0-100%
        // Or returns -1 if GPU monitoring unavailable
        // Should not crash if unavailable
    }

    SECTION("Resource metrics are non-negative") {
        // All returned values >= 0 or specific error code
        // No negative memory, negative FPS, etc.
    }
}
```

### Group 5: Adaptive Scaling Algorithm (4 tests)

```cpp
TEST_CASE("PerformanceLogger::AdaptiveScaling", "[performance][scaling]") {
    SECTION("Scaling algorithm computes correct FPS target") {
        // Setup: CPU=15%, GPU=10% → 60 FPS (headroom)
        // Setup: CPU=45%, GPU=35% → 45 FPS (moderate)
        // Setup: CPU=70%, GPU=65% → 30 FPS (high load)
        // Setup: CPU=90%, GPU=88% → 15 FPS (critical)
    }

    SECTION("Scaling is smooth (no jitter)") {
        // Call getAdaptiveFpsTarget() 10 times in quick succession
        // Values should be same or increase gradually
        // Should not jitter between 60 and 15 constantly
    }

    SECTION("Hysteresis prevents oscillation") {
        // Set load to 61% (just above 60% threshold)
        // FPS changes to 45
        // Load drops to 59% → FPS stays 45 (hysteresis)
        // Only changes when significantly below threshold
    }

    SECTION("Backpressure detection works") {
        // If FPS drops below target, reduce target further
        // If FPS is above target, gradually increase target
        // Self-correcting mechanism
    }
}
```

### Group 6: Thread Safety & Concurrency (2 tests)

```cpp
TEST_CASE("PerformanceLogger::ThreadSafety", "[performance][threading]") {
    SECTION("Metrics can be read from any thread") {
        // Create 5 worker threads
        // Each reads getCpuUsagePercent() repeatedly
        // No race conditions or crashes
    }

    SECTION("Monitoring thread doesn't block other work") {
        // While monitoring runs in background
        // Main thread can still do work
        // Latency of metrics update is < 1 second
    }
}
```

### Group 7: Logging Integration (2 tests)

```cpp
TEST_CASE("PerformanceLogger::LoggingIntegration", "[performance][logging]") {
    SECTION("Performance metrics are logged") {
        // Enable performance logging
        // Call logMetrics()
        // Check that metrics appear in log file
        // Format: [timestamp] [PERF] CPU=15.2% GPU=8.1% Memory=85MB FPS=60 Target=60
    }

    SECTION("Warnings logged for threshold violations") {
        // Simulate CPU > 1% in idle mode
        // Should log warning
        // Simulate FPS drops below 30
        // Should log warning
    }
}
```

---

## Interface Definition (Mock Classes for Tests)

```cpp
namespace aura::performance {

class PerformanceLogger {
public:
    static PerformanceLogger& getInstance();

    // Initialization
    void initialize();
    void shutdown();
    bool isMonitoring() const;

    // CPU monitoring
    float getCpuUsagePercent() const;

    // Frame rate
    float getFps() const;
    float getAdaptiveFpsTarget() const;

    // GPU and Memory
    float getGpuUsagePercent() const;  // Returns -1 if unavailable
    uint32_t getMemoryUsageMB() const;

    // Configuration
    void setIdleCpuThreshold(float percent);
    void setFpsRange(uint32_t minFps, uint32_t maxFps);
    void setMemoryLimit(uint32_t limitMB);

    // Logging
    void logMetrics();  // Write metrics to logger
    void enableDetailedLogging(bool enabled);

private:
    PerformanceLogger();
    std::thread m_monitoringThread;
    mutable std::mutex m_metricsMutex;
};

} // namespace aura::performance
```

---

## Constants & Targets (from CLAUDE.md)

```cpp
// Idle CPU Usage Target
constexpr float IDLE_CPU_TARGET = 1.0f;           // < 1%

// Frame Rate Targets
constexpr uint32_t TARGET_FPS_IDLE = 1;           // Screensaver mode
constexpr uint32_t TARGET_FPS_HOVER = 60;         // Hover animations
constexpr uint32_t TARGET_FPS_AUDIO_VIZ = 60;     // Audio visualizer
constexpr uint32_t TARGET_FPS_INTENSIVE = 144;    // Gaming/intensive

// Adaptive Scaling Thresholds
constexpr float CPU_THRESHOLD_1 = 30.0f;          // 60 FPS
constexpr float CPU_THRESHOLD_2 = 60.0f;          // 45 FPS
constexpr float CPU_THRESHOLD_3 = 85.0f;          // 30 FPS
// Above 85% = 15 FPS (critical)

// Memory Targets (Sprint 5 goals)
constexpr uint32_t MEMORY_TARGET_MAIN = 80;       // Main app: 80MB
constexpr uint32_t MEMORY_TARGET_CORE = 30;       // Core lib: 30MB
constexpr uint32_t MEMORY_TARGET_OVERLAY = 50;    // Overlay: 50MB
constexpr uint32_t MEMORY_TARGET_TOTAL = 160;     // Total idle: 160MB
```

---

## How to Create This File

### Step 1: Create the skeleton
```bash
cp tests/unit/test_logging.cpp tests/unit/test_performance_logger.cpp
# Then modify with new test cases
```

### Step 2: Add includes
```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <thread>
#include <chrono>
#include <numeric>
```

### Step 3: Add interface definition
```cpp
namespace aura::performance {
    class PerformanceLogger { /* ... */ };
}
```

### Step 4: Add each TEST_CASE block
Use the 7 groups above

### Step 5: Update tests/CMakeLists.txt
```cmake
add_executable(test_runner
    ...existing tests...
    unit/test_performance_logger.cpp  # ADD THIS LINE
)
```

---

## After Creating Test File

1. Build: `cmake --build build --config Debug`
2. Observe: 15-20 linker errors (EXPECTED - interfaces not implemented)
3. Note: These define what PerformanceLogger must implement
4. Next Session: Implement PerformanceLogger class

---

## Expected Test Output (After Implementation)

```
Running tests for Performance Logger
Test project time: ~2 seconds

Test Results:
  [PASS] PerformanceLogger::Initialization::Logger initializes successfully
  [PASS] PerformanceLogger::Initialization::Metrics are initially zero or baseline
  [PASS] PerformanceLogger::Initialization::Performance thresholds are configurable
  [PASS] PerformanceLogger::CpuUsageMonitoring::CPU usage measurement returns valid percentage
  ... (all 15-20 tests pass)

Performance Benchmark Results:
  Average FPS: 60.0 fps ✓
  Idle CPU: 0.3% ✓
  Memory (idle): 85 MB ✓
  Adaptive scaling: Working correctly ✓
```

---

## Success Criteria

When tests are properly written:
- ✅ 15-20 new tests defined
- ✅ Interface complete and type-safe
- ✅ Compilation succeeds with linker errors (expected)
- ✅ All constants aligned with CLAUDE.md/PLAN.md
- ✅ Thread safety scenarios tested
- ✅ Adaptive algorithm behavior validated

---

## Notes

- Keep tests independent (each can run in isolation)
- Use SECTION() for related assertions within a TEST_CASE
- Don't use `&&` in REQUIRE() (Catch2 limitation)
- Mock classes in tests (real implementation comes next)
- Follow existing test patterns from test_logging.cpp

---

**Ready?** 

When you start Step 2, just follow this template and let the test structure guide the implementation in Phase 2!

