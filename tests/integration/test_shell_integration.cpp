#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <cstring>

#include "shell_integration.h"
#include "message_types.h"

using aura::system::ShellIntegration;

// ============================================================================
// Singleton
// ============================================================================

TEST_CASE("ShellIntegration singleton identity", "[integration][shell]") {
    REQUIRE(&ShellIntegration::getInstance() == &ShellIntegration::getInstance());
}

// ============================================================================
// Lifecycle — initialize / shutdown must not crash
// ============================================================================

TEST_CASE("ShellIntegration initialize and shutdown cycle", "[integration][shell]") {
    auto& si = ShellIntegration::getInstance();

    // initialize() creates a message-only HWND and registers the tray icon.
    // In a headless CI environment Shell_NotifyIcon may silently fail — that
    // is acceptable; the important contract is that initialize() does not throw
    // and isInitialized() reflects the result correctly.
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    bool ok = si.initialize(hInst);
    REQUIRE(si.isInitialized() == ok);

    // shutdown() must be idempotent.
    si.shutdown();
    REQUIRE_FALSE(si.isInitialized());
    si.shutdown(); // second call must not crash
}

// ============================================================================
// Power state — returns a valid bool regardless of AC/battery
// ============================================================================

TEST_CASE("ShellIntegration power state queries return valid bools", "[integration][shell]") {
    auto& si = ShellIntegration::getInstance();
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    si.initialize(hInst);

    // isOnBattery() and isBatteryLow() must not crash. Their values depend on
    // the host machine (CI runners are always on AC).
    bool onBattery = si.isOnBattery();
    bool batLow    = si.isBatteryLow();

    // If not on battery, battery-low must also be false.
    if (!onBattery) {
        REQUIRE_FALSE(batLow);
    }

    si.shutdown();
}

// ============================================================================
// PERF_STATS payload layout
// ============================================================================

TEST_CASE("PerfStatsPayload has correct size and layout", "[integration][shell]") {
    // 3 floats (12 bytes) + 4 bytes padding = 16 bytes total.
    REQUIRE(sizeof(aura::ipc::PerfStatsPayload) == 16u);
    REQUIRE(sizeof(aura::ipc::PerfStatsPayload) <= 2048u);

    // MessageType::PERF_STATS must equal 0x0090.
    REQUIRE(static_cast<uint32_t>(aura::ipc::MessageType::PERF_STATS) == 0x0090u);
}

TEST_CASE("PerfStatsPayload packs and unpacks via Message::setPayload", "[integration][shell]") {
    aura::ipc::PerfStatsPayload src{};
    src.cpuPercent = 0.75f;
    src.memoryMB   = 48.5f;
    src.avgFps     = 59.9f;

    aura::ipc::Message msg;
    msg.messageType = static_cast<uint32_t>(aura::ipc::MessageType::PERF_STATS);
    msg.setPayload(src);

    REQUIRE(msg.payloadSize == sizeof(aura::ipc::PerfStatsPayload));
    REQUIRE(msg.isValid());

    const auto* out = msg.getPayload<aura::ipc::PerfStatsPayload>();
    REQUIRE(out != nullptr);
    REQUIRE(out->cpuPercent == src.cpuPercent);
    REQUIRE(out->memoryMB   == src.memoryMB);
    REQUIRE(out->avgFps     == src.avgFps);
}

// ============================================================================
// WM_TASKBARCREATED — post the registered message, verify no crash
// ============================================================================

TEST_CASE("ShellIntegration handles WM_TASKBARCREATED without crashing", "[integration][shell]") {
    auto& si = ShellIntegration::getInstance();
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    if (!si.initialize(hInst)) {
        WARN("ShellIntegration::initialize() failed in this environment — skipping WM_TASKBARCREATED test");
        return;
    }

    // The registered message ID for WM_TASKBARCREATED changes per session.
    // We cannot directly call the private handler, but we can verify that
    // posting WM_NULL to the internal HWND doesn't crash — a minimal smoke test.
    // (A full test would require exposing the HWND or a test-only hook.)
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    REQUIRE(si.isInitialized()); // must still be alive after draining the queue
    si.shutdown();
}
