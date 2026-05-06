# Phase 2: GREEN Implementation - Logger & Performance Logger

**Date**: 2026-04-30  
**Phase**: TDD GREEN Phase - Implementation  
**Status**: 🚀 BEGINNING NOW

---

## Implementation Sequence

### Step 1: Create Logger Infrastructure (IMMEDIATE)
1. Create `src/core/logging/` directory structure
2. Implement `logger.h` - Logger class interface
3. Implement `logger.cpp` - Logger singleton + spdlog integration
4. Update `src/core/CMakeLists.txt` to include logging module
5. Build and verify 11 logging tests PASS

### Step 2: Create Performance Logger (NEXT)
1. Implement `performance_logger.h` - PerformanceLogger interface  
2. Implement `performance_logger.cpp` - CPU/GPU/Memory/FPS tracking
3. Update CMakeLists.txt
4. Build and verify performance tests PASS

### Step 3: Resolve Platform Layer Tests (AFTER)
1. Implement WindowManager, DpiAwareness, SystemMetrics
2. Link against platform implementations
3. Watch platform tests turn GREEN

---

## Logger Implementation Details

### What the Tests Expect
From `test_logging.cpp`:
- ✅ Singleton pattern via `getInstance()`
- ✅ `debug(module, msg)`, `info()`, `warn()`, `error()`
- ✅ `setOutputPath()`, `getOutputPath()`, `clearLogs()`, `isInitialized()`
- ✅ Log file in `%LOCALAPPDATA%\AuraShell\logs\aurashell.log`
- ✅ Format: `[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [module] message`
- ✅ Thread-safe logging

### spdlog Integration Pattern
```cpp
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Create logger with:
// - Console sink (debug builds)
// - File sink (all builds)
// - Rotating: 10 MB files, keep 5 files
// - Format: [YYYY-MM-DD HH:MM:SS.mmm] [level] [thread_id] message
```

---

## File Structure

```
src/core/
├── logging/
│   ├── CMakeLists.txt              (NEW - build config)
│   ├── logger.h                    (NEW - interface)
│   ├── logger.cpp                  (NEW - implementation)
│   ├── performance_logger.h        (NEW - interface)
│   └── performance_logger.cpp      (NEW - implementation)
├── platform/
│   ├── ipc/
│   │   ├── named_pipe_server.cpp
│   │   ├── named_pipe_client.cpp
│   │   └── ...
│   └── CMakeLists.txt
└── CMakeLists.txt                  (MODIFIED - add logging subdirectory)
```

---

## Next Command

After this plan is implemented:

```bash
# Build
cmake --build build --config Debug

# Run tests
ctest --build-config Debug --output-on-failure -V

# Expected: 11 logging tests PASS ✅
```

