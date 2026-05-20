#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <chrono>

#include "performance_logger.h"

using aura::logging::PerformanceLogger;

TEST_CASE("PerformanceLogger singleton identity", "[logging][perf]") {
    REQUIRE(&PerformanceLogger::getInstance() == &PerformanceLogger::getInstance());
}

TEST_CASE("PerformanceLogger initializes and shuts down cleanly", "[logging][perf]") {
    auto& pl = PerformanceLogger::getInstance();
    pl.initialize();
    REQUIRE(pl.isInitialized());
    pl.shutdown();
    REQUIRE_FALSE(pl.isInitialized());
}

TEST_CASE("PerformanceLogger getMemoryMB returns positive value", "[logging][perf]") {
    auto& pl = PerformanceLogger::getInstance();
    pl.initialize();

    // Let the sampler run at least once.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    float mem = pl.getMemoryMB();
    INFO("Working set: " << mem << " MB");
    REQUIRE(mem > 0.0f);
    REQUIRE(mem < 2048.0f); // sanity upper bound

    pl.shutdown();
}

TEST_CASE("PerformanceLogger getIdleCpuPercent is in [0, 100]", "[logging][perf]") {
    auto& pl = PerformanceLogger::getInstance();
    pl.initialize();

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    float cpu = pl.getIdleCpuPercent();
    INFO("Idle CPU: " << cpu << "%");
    REQUIRE(cpu >= 0.0f);
    REQUIRE(cpu <= 100.0f);

    pl.shutdown();
}

TEST_CASE("PerformanceLogger recordFrame drives FPS calculation", "[logging][perf]") {
    auto& pl = PerformanceLogger::getInstance();
    pl.initialize();

    // Simulate 60fps for ~200ms (12 frames at ~16ms intervals).
    for (int i = 0; i < 12; ++i) {
        pl.recordFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    float fps = pl.getAverageFps();
    INFO("Measured FPS: " << fps);

    // Should be approximately 60fps; allow generous range for CI timing jitter.
    REQUIRE(fps > 20.0f);
    REQUIRE(fps < 200.0f);

    pl.shutdown();
}

TEST_CASE("PerformanceLogger getAverageFps returns 0 before any frames recorded",
          "[logging][perf]") {
    // Fresh instance via re-initialize.
    auto& pl = PerformanceLogger::getInstance();
    pl.initialize();

    // Do NOT call recordFrame — FPS should be 0.
    float fps = pl.getAverageFps();
    REQUIRE(fps == 0.0f);

    pl.shutdown();
}

TEST_CASE("PerformanceLogger setCpuWarnThreshold does not crash", "[logging][perf]") {
    auto& pl = PerformanceLogger::getInstance();
    pl.initialize();
    pl.setCpuWarnThreshold(1.0f);
    pl.setCpuWarnThreshold(0.0f);  // disable threshold
    pl.setCpuWarnThreshold(-5.0f); // should clamp to 0
    pl.shutdown();
}
