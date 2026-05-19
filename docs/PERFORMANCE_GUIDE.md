# AuraShell — Performance Guide

This document defines the performance contracts for AuraShell, how to measure them, and patterns
for staying within budget. All targets below are **hard constraints**, not aspirational goals —
exceeding them triggers a code review.

## CPU Budget (Idle)

"Idle" means: no animations running, no audio input, no active overlay rendering.

| Module | Budget | Mechanism |
|---|---|---|
| `aurashell_service` | ≤ 0.2% | Named pipe `WaitForSingleObject`, no polling |
| `aurashell_config_app` | ≤ 0.3% | UI message pump (`GetMessage`), no timers when hidden |
| `aurashell_ipc` | ≤ 0.2% | Pipe I/O completion ports, no busy-wait |
| Config file watcher | ≤ 0.1% | `ReadDirectoryChangesW` async, thread sleeps between checks |
| **Total (all modules)** | **≤ 1.0%** | |

Exceeding 1% idle triggers a `WARN` log entry via `Logger::warn("perf", ...)`.

## Memory Budget (Steady-State)

| Component | Budget | Notes |
|---|---|---|
| Main config application | ≤ 80 MB | Includes WinUI 3 runtime overhead |
| Core library (aurashell_*.dll) | ≤ 30 MB | DirectX, audio buffers, config cache |
| Overlay renderer | ≤ 50 MB | Texture cache + DirectX swap chain |
| **Total (idle)** | **≤ 160 MB** | Baseline after launch, before any theme is applied |

Large texture loads (custom wallpapers, icon packs) are streamed and evicted after 30 seconds
of inactivity — they do not count toward the steady-state budget.

## Frame Rate Targets

| Scenario | Target | Acceptable Range | Notes |
|---|---|---|---|
| Hover animations (taskbar) | 60 fps | 55–65 fps | Tied to monitor refresh |
| Audio visualizer (active) | 60 fps | 55–65 fps | Drops to 30 fps when CPU > 80% |
| Adaptive mode (gaming) | 30–144 fps | ≥ 30 fps | Scales with GPU headroom |
| Screensaver / idle | ~1 fps | 0.5–2 fps | Minimal refresh to preserve battery |
| Minimized / hidden | 0 fps | 0 fps | Rendering fully suspended |

## Idle Enforcement Pattern

**Rule:** Never busy-wait. Every poll loop must sleep between iterations.

```cpp
// CORRECT — 60Hz polling, minimal CPU
void HoverDetector::pollLoop() {
    while (m_running) {
        POINT pos;
        GetCursorPos(&pos);
        if (isOverTaskbar(pos))
            handleHover(pos);
        Sleep(16); // 16ms = 60Hz sample rate
    }
}

// WRONG — busy-wait, will peg one CPU core
void HoverDetector::pollLoop() {
    while (m_running) {
        POINT pos;
        GetCursorPos(&pos);
        if (isOverTaskbar(pos))
            handleHover(pos);
        // No sleep — instant 100% CPU
    }
}
```

**Config file watcher** uses `ReadDirectoryChangesW` with an overlapped I/O handle and
`WaitForSingleObject(hEvent, INFINITE)` — the thread parks in the kernel until a change arrives.
No polling required.

**Named pipe server** uses `ConnectNamedPipe` in overlapped mode + I/O completion ports.
The server thread never spins.

## Adaptive Scaling Rules

| Trigger | Action |
|---|---|
| GPU load > 80% for 500ms | Drop overlay FPS to 30 |
| GPU load > 95% for 1s | Suspend overlay entirely |
| Battery < 20% (laptops) | Disable audio visualizer, cap hover FPS at 30 |
| Battery < 10% (laptops) | Suspend all overlays |
| Window minimized | Call `IDXGISwapChain::Present` only once per second |
| Display locked / screensaver | Zero rendering, release GPU resources |

All thresholds are constants defined in `src/taskbar_engine/performance_constants.h` and can be
overridden via the JSON config without recompilation.

## How to Measure

### Quick baseline (Task Manager)
1. Launch AuraShell, wait 30 seconds for startup transients to settle
2. Open Task Manager → Details tab → find `AuraShell.exe` and `AuraShellService.exe`
3. "CPU" column should read < 1% combined while idle

### Accurate sampling (Windows Performance Recorder)
```powershell
# Capture a 30-second CPU trace
wpr -start CPU
Start-Sleep 30
wpr -stop C:\perf\aurashell_idle.etl

# Analyze in Windows Performance Analyzer (WPA)
# Open the .etl, add "CPU Usage (Sampled)" graph, filter to AuraShell processes
```

### CTest performance gate
```powershell
# Run only tests tagged [performance]
ctest --test-dir build -C Release --output-on-failure -L "performance"
```

The `test_idle_cpu_budget` test in `tests/unit/test_logging.cpp` (Logger::Performance section)
verifies that 1000 sequential log calls complete in < 100ms — a proxy for logging overhead.

## Profiling Hotspots

If CPU or memory budget is exceeded, investigate in this order:

1. **Timer leaks** — `SetTimer` calls not paired with `KillTimer`; show up as periodic WM_TIMER floods
2. **Allocation churn** — frequent small heap allocations in the render path; use `VTune` or `ETW` Heap provider
3. **Texture cache misses** — DirectX `CreateTexture2D` called per frame instead of per theme-change
4. **Pipe message flood** — config app polling the service too frequently; check `IPC_POLL_INTERVAL_MS` constant
5. **spdlog flush policy** — flushing after every log call is safe but slow; use `flush_on(spdlog::level::err)` for production builds
