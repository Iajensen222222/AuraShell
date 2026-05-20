#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace aura::logging {

// Tracks the AuraShell process's idle CPU usage, working-set memory, and
// overlay render FPS. Runs a 1Hz background sampling thread.
//
// PLAN.md Sprint 2.5 exit criterion: idle CPU must remain < 1%.
// This class measures it and logs a WARN whenever the threshold is crossed.
//
// Usage:
//   PerformanceLogger::getInstance().initialize();
//   // In AnimationController tick callback:
//   PerformanceLogger::getInstance().recordFrame();
class PerformanceLogger {
public:
    static PerformanceLogger& getInstance();

    // Start the background sampling thread (1Hz).
    void initialize();

    // Join the sampling thread.
    void shutdown();

    bool isInitialized() const;

    // -----------------------------------------------------------------------
    // Called from the render / animation tick (any thread, lock-free)
    // -----------------------------------------------------------------------

    // Record the timestamp of one rendered frame for FPS calculation.
    void recordFrame();

    // -----------------------------------------------------------------------
    // Queries (thread-safe, atomic reads)
    // -----------------------------------------------------------------------

    // Process CPU usage averaged over the last ~1 second (0.0 – 100.0).
    float getIdleCpuPercent() const;

    // Process working-set memory in megabytes.
    float getMemoryMB() const;

    // Rolling average FPS across the last 60 recorded frames.
    // Returns 0 if fewer than 2 frames have been recorded.
    float getAverageFps() const;

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    // Log a WARN when CPU exceeds this threshold (default 1.0%).
    void setCpuWarnThreshold(float percent);

private:
    PerformanceLogger() = default;
    ~PerformanceLogger() { shutdown(); }
    PerformanceLogger(const PerformanceLogger&) = delete;
    PerformanceLogger& operator=(const PerformanceLogger&) = delete;

    void sampleLoop();
    float computeCpuPercent();
    float computeMemoryMB();
    float computeAvgFps();

    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    bool              m_initialized{false};

    // Last CPU time snapshot for delta calculation
    int64_t m_lastSampleTimeNs{0};
    int64_t m_lastKernelNs{0};
    int64_t m_lastUserNs{0};

    std::atomic<float> m_cpuPercent{0.0f};
    std::atomic<float> m_memoryMB{0.0f};

    // Rolling FPS ring buffer: stores frame timestamp in microseconds.
    // Single-producer (recordFrame on any thread) + single-consumer (sampleLoop).
    static constexpr uint32_t FPS_RING = 64; // power of two
    std::array<int64_t, FPS_RING> m_frameTimes{};
    std::atomic<uint32_t>         m_frameHead{0}; // write index
    std::atomic<float>            m_avgFps{0.0f};

    std::atomic<float> m_warnThreshold{1.0f};
};

} // namespace aura::logging
