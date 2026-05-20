#include "performance_logger.h"

#include <algorithm>
#include <cstdio>
#include <windows.h>
#include <psapi.h>

#include "logger.h"

#pragma comment(lib, "psapi.lib")

namespace {

// Convert FILETIME (100ns intervals since 1601) to nanoseconds as int64.
int64_t fileTimeToNs(const FILETIME& ft) noexcept {
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>(uli.QuadPart) * 100; // 100ns → ns
}

} // anonymous namespace

namespace aura::logging {

PerformanceLogger& PerformanceLogger::getInstance() {
    static PerformanceLogger instance;
    return instance;
}

void PerformanceLogger::initialize() {
    if (m_initialized) return;

    // Capture initial CPU timestamps so the first delta is valid.
    FILETIME creat, exit, kern, user;
    GetProcessTimes(GetCurrentProcess(), &creat, &exit, &kern, &user);
    m_lastKernelNs    = fileTimeToNs(kern);
    m_lastUserNs      = fileTimeToNs(user);
    m_lastSampleTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    m_running.store(true);
    m_thread = std::thread(&PerformanceLogger::sampleLoop, this);
    m_initialized = true;
}

void PerformanceLogger::shutdown() {
    if (!m_initialized) return;
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();
    m_initialized = false;
}

bool PerformanceLogger::isInitialized() const { return m_initialized; }

void PerformanceLogger::recordFrame() {
    int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    uint32_t head = m_frameHead.load(std::memory_order_relaxed);
    m_frameTimes[head % FPS_RING] = now;
    m_frameHead.store(head + 1, std::memory_order_release);
}

float PerformanceLogger::getIdleCpuPercent() const {
    return m_cpuPercent.load(std::memory_order_relaxed);
}

float PerformanceLogger::getMemoryMB() const {
    return m_memoryMB.load(std::memory_order_relaxed);
}

float PerformanceLogger::getAverageFps() const {
    return m_avgFps.load(std::memory_order_relaxed);
}

void PerformanceLogger::setCpuWarnThreshold(float percent) {
    m_warnThreshold.store(std::max(0.0f, percent));
}

// ============================================================================
// Private
// ============================================================================

void PerformanceLogger::sampleLoop() {
    while (m_running.load(std::memory_order_relaxed)) {
        Sleep(1000); // 1Hz sampling

        float cpu = computeCpuPercent();
        float mem = computeMemoryMB();
        float fps = computeAvgFps();

        m_cpuPercent.store(cpu, std::memory_order_relaxed);
        m_memoryMB.store(mem,   std::memory_order_relaxed);
        m_avgFps.store(fps,     std::memory_order_relaxed);

        // Log to debug; warn if over threshold.
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "perf: CPU=%.2f%% MEM=%.1fMB FPS=%.1f", cpu, mem, fps);
        Logger::getInstance().debug("perf", buf);

        float threshold = m_warnThreshold.load(std::memory_order_relaxed);
        if (threshold > 0.0f && cpu > threshold) {
            std::snprintf(buf, sizeof(buf),
                "Idle CPU %.2f%% exceeds %.2f%% target — investigate render loop", cpu, threshold);
            Logger::getInstance().warn("perf", buf);
        }
    }
}

float PerformanceLogger::computeCpuPercent() {
    FILETIME creat, exit, kern, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creat, &exit, &kern, &user))
        return 0.0f;

    int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    int64_t kernNs = fileTimeToNs(kern);
    int64_t userNs = fileTimeToNs(user);

    int64_t wallDelta  = nowNs - m_lastSampleTimeNs;
    int64_t cpuDelta   = (kernNs - m_lastKernelNs) + (userNs - m_lastUserNs);

    m_lastSampleTimeNs = nowNs;
    m_lastKernelNs     = kernNs;
    m_lastUserNs       = userNs;

    if (wallDelta <= 0) return 0.0f;

    // SYSTEM_INFO gives logical processor count for normalising to 0-100%.
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int64_t cores = std::max<int64_t>(1, si.dwNumberOfProcessors);

    float pct = static_cast<float>(cpuDelta) /
                static_cast<float>(wallDelta * cores) * 100.0f;
    return std::clamp(pct, 0.0f, 100.0f);
}

float PerformanceLogger::computeMemoryMB() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                              sizeof(pmc)))
        return 0.0f;
    return static_cast<float>(pmc.WorkingSetSize) / (1024.0f * 1024.0f);
}

float PerformanceLogger::computeAvgFps() {
    uint32_t head = m_frameHead.load(std::memory_order_acquire);
    if (head < 2) return 0.0f;

    // Use up to FPS_RING most recent frames.
    uint32_t count = std::min<uint32_t>(head, FPS_RING);
    if (count < 2) return 0.0f;

    // Newest and oldest timestamps in the ring.
    int64_t newest = m_frameTimes[(head - 1) % FPS_RING];
    int64_t oldest = m_frameTimes[(head - count) % FPS_RING];

    int64_t spanUs = newest - oldest;
    if (spanUs <= 0) return 0.0f;

    return static_cast<float>(count - 1) / (static_cast<float>(spanUs) * 1e-6f);
}

} // namespace aura::logging
